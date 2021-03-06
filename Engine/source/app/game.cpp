//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#include "platform/platform.h"
#include "platform/platformInput.h"

#include "app/game.h"
#include "math/mMath.h"
#include "core/dnet.h"
#include "core/stream/fileStream.h"
#include "core/frameAllocator.h"
#include "core/iTickable.h"
#include "core/strings/findMatch.h"
#include "console/simBase.h"
#include "console/console.h"
#include "console/consoleTypes.h"
#include "gui/controls/guiMLTextCtrl.h"
#ifdef TORQUE_TGB_ONLY
#include "T2D/oldModel/networking/t2dGameConnection.h"
#include "T2D/oldModel/networking/t2dNetworkServerSceneProcess.h"
#include "T2D/oldModel/networking/t2dNetworkClientSceneProcess.h"
#else
#include "T3D/gameBase/gameConnection.h"
#include "T3D/gameFunctions.h"
#include "T3D/gameBase/gameProcess.h"
#endif
#include "platform/profiler.h"
#include "gfx/gfxCubemap.h"
#include "gfx/gfxTextureManager.h"
#include "sfx/sfxSystem.h"

#ifdef TORQUE_PLAYER
// See matching #ifdef in editor/editor.cpp
bool gEditingMission = false;
#endif

//--------------------------------------------------------------------------

ConsoleFunctionGroupBegin( InputManagement, "Functions that let you deal with input from scripts" );

ConsoleFunction( deactivateDirectInput, void, 1, 1, "()"
            "@brief Disables DirectInput.\n\n"
            "Also deactivates any connected joysticks.\n\n"
			"@ingroup Input" )
{
   TORQUE_UNUSED(argc); TORQUE_UNUSED(argv);
   if ( Input::isActive() )
      Input::deactivate();
}

ConsoleFunction( activateDirectInput, void, 1, 1,"()"
            "@brief Activates DirectInput.\n\n"
            "Also activates any connected joysticks."
			"@ingroup Input")
{
   TORQUE_UNUSED(argc); TORQUE_UNUSED(argv);
   if ( !Input::isActive() )
      Input::activate();
}
ConsoleFunctionGroupEnd( InputManagement );

//--------------------------------------------------------------------------

static const U32 MaxPlayerNameLength = 16;
ConsoleFunction( strToPlayerName, const char*, 2, 2, "strToPlayerName( string )" )
{
   TORQUE_UNUSED(argc);

   const char* ptr = argv[1];

	// Strip leading spaces and underscores:
   while ( *ptr == ' ' || *ptr == '_' )
      ptr++;

   U32 len = dStrlen( ptr );
   if ( len )
   {
      char* ret = Con::getReturnBuffer( MaxPlayerNameLength + 1 );
      char* rptr = ret;
      ret[MaxPlayerNameLength - 1] = '\0';
      ret[MaxPlayerNameLength] = '\0';
      bool space = false;

      U8 ch;
      while ( *ptr && dStrlen( ret ) < MaxPlayerNameLength )
      {
         ch = (U8) *ptr;

         // Strip all illegal characters:
         if ( ch < 32 || ch == ',' || ch == '.' || ch == '\'' || ch == '`' )
         {
            ptr++;
            continue;
         }

         // Don't allow double spaces or space-underline combinations:
         if ( ch == ' ' || ch == '_' )
         {
            if ( space )
            {
               ptr++;
               continue;
            }
            else
               space = true;
         }
         else
            space = false;

         *rptr++ = *ptr;
         ptr++;
      }
      *rptr = '\0';

		//finally, strip out the ML text control chars...
		return GuiMLTextCtrl::stripControlChars(ret);
   }

	return( "" );
}

ConsoleFunctionGroupBegin( Platform , "General platform functions.");

ConsoleFunction( lockMouse, void, 2, 2, "(bool isLocked)"
            "@brief Lock or unlock the mouse to the window.\n\n"
            "When true, prevents the mouse from leaving the bounds of the game window.\n\n"
            "@ingroup Input")
{
   Platform::setWindowLocked(dAtob(argv[1]));
}


ConsoleFunction( setNetPort, bool, 2, 3, "(int port, bool bind=true)"
   "@brief Set the network port for the game to use.\n\n"

   "@param port The port to use.\n"
   "@param bind True if bind() should be called on the port.\n"

   "@returns True if the port was successfully opened.\n"

   "This will trigger a windows firewall prompt.  "
   "If you don't have firewall tunneling tech you can set this to false to avoid the prompt.\n\n"
   "@ingroup Networking")
{
   bool bind = true;
   if (argc == 3)
      bind = dAtob(argv[2]);
   return Net::openPort(dAtoi(argv[1]), bind);
}

ConsoleFunction( closeNetPort, void, 1, 1, "()"
   "@brief Closes the current network port\n\n"
   "@ingroup Networking")
{
   Net::closePort();
}

ConsoleFunction( saveJournal, void, 2, 2, "(string filename)"
                "Save the journal to the specified file.\n\n"
				"@ingroup Platform")
{
   Journal::Record(argv[1]);
}

ConsoleFunction( playJournal, void, 2, 3, "(string filename)"
                "@brief Begin playback of a journal from a specified field.\n\n"
				"@param filename Name and path of file journal file\n"
				"@ingroup Platform")
{
   // CodeReview - BJG 4/24/2007 - The break flag needs to be wired back in.
   // bool jBreak = (argc > 2)? dAtob(argv[2]): false;
   Journal::Play(argv[1]);
}

ConsoleFunction( getSimTime, S32, 1, 1, "()"
				"Return the current sim time in milliseconds.\n\n"
                "@brief Sim time is time since the game started.\n\n"
				"@ingroup Platform")
{
   return Sim::getCurrentTime();
}

ConsoleFunction( getRealTime, S32, 1, 1, "()"
				"@brief Return the current real time in milliseconds.\n\n"
                "Real time is platform defined; typically time since the computer booted.\n\n"
				"@ingroup Platform")
{
   return Platform::getRealMilliseconds();
}

ConsoleFunctionGroupEnd(Platform);

//-----------------------------------------------------------------------------

bool clientProcess(U32 timeDelta)
{
   bool ret = true;

#ifndef TORQUE_TGB_ONLY
   ret = ClientProcessList::get()->advanceTime(timeDelta);
#else
	ret = gt2dNetworkClientProcess.advanceTime( timeDelta );
#endif

   ITickable::advanceTime(timeDelta);

#ifndef TORQUE_TGB_ONLY
   // Determine if we're lagging
   GameConnection* connection = GameConnection::getConnectionToServer();
   if(connection)
	{
      connection->detectLag();
	}
#else
   // Determine if we're lagging
   t2dGameConnection* connection = t2dGameConnection::getConnectionToServer();
   if(connection)
	{
      connection->detectLag();
	}
#endif

   // Let SFX process.
   SFX->_update();

   return ret;
}

bool serverProcess(U32 timeDelta)
{
   bool ret = true;
#ifndef TORQUE_TGB_ONLY
   ret =  ServerProcessList::get()->advanceTime(timeDelta);
#else
   ret =  gt2dNetworkServerProcess.advanceTime( timeDelta );
#endif
   return ret;
}

