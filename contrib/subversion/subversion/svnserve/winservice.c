/*
 * winservice.c : Implementation of Windows Service support
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_errno.h>

#include "svn_error.h"

#include "svn_private_config.h"
#include "winservice.h"

/*
Design Notes
------------

The code in this file allows svnserve to run as a Windows service.
Windows Services are only supported on operating systems derived
from Windows NT, which is basically all modern versions of Windows
(2000, XP, Server, Vista, etc.) and excludes the Windows 9x line.

Windows Services are processes that are started and controlled by
the Service Control Manager.  When the SCM wants to start a service,
it creates the process, then waits for the process to connect to
the SCM, so that the SCM and service process can communicate.
This is done using the StartServiceCtrlDispatcher function.

In order to minimize changes to the svnserve startup logic, this
implementation differs slightly from most service implementations.
In most services, main() immediately calls StartServiceCtrlDispatcher,
which does not return control to main() until the SCM sends the
"stop" request to the service, and the service stops.


Installing the Service
----------------------

Installation is beyond the scope of source code comments.  There
is a separate document that describes how to install and uninstall
the service.  Basically, you create a Windows Service, give it a
binary path that points to svnserve.exe, and make sure that you
specify --service on the command line.


Starting the Service
--------------------

First, the SCM decides that it wants to start a service.  It creates
the process for the service, passing it the command-line that is
stored in the service configuration (stored in the registry).

Next, main() runs.  The command-line should contain the --service
argument, which is the hint that svnserve is running under the SCM,
not as a standalone process.  main() calls winservice_start().

winservice_start() creates an event object (winservice_start_event),
and creates and starts a separate thread, the "dispatcher" thread.
winservice_start() then waits for either winservice_start_event
to fire (meaning: "the dispatcher thread successfully connected
to the SCM, and now the service is starting") or for the dispatcher
thread to exit (meaning: "failed to connect to SCM").

If the dispatcher thread quit, then winservice_start returns an error.
If the start event fired, then winservice_start returns a success code
(SVN_NO_ERROR).  At this point, the service is now in the "starting"
state, from the perspective of the SCM.  winservice_start also registers
an atexit handler, which handles cleaning up some of the service logic,
as explained below in "Stopping the Service".

Next, control returns to main(), which performs the usual startup
logic for svnserve.  Mostly, it creates the listener socket.  If
main() was able to start the service, then it calls the function
winservice_running().

winservice_running() informs the SCM that the service has finished
starting, and is now in the "running" state.  main() then does its
work, accepting client sockets and processing SVN requests.

Stopping the Service
--------------------

At some point, the SCM will decide to stop the service, either because
an administrator chose to stop the service, or the system is shutting
down.  To do this, the SCM calls winservice_handler() with the
SERVICE_CONTROL_STOP control code.  When this happens,
winservice_handler() will inform the SCM that the service is now
in the "stopping" state, and will call winservice_notify_stop().

winservice_notify_stop() is responsible for cleanly shutting down the
svnserve logic (waiting for client requests to finish, stopping database
access, etc.).  Right now, all it does is close the listener socket,
which causes the apr_socket_accept() call in main() to fail.  main()
then calls exit(), which processes all atexit() handlers, which
results in winservice_stop() being called.

winservice_stop() notifies the SCM that the service is now stopped,
and then waits for the dispatcher thread to exit.  Because all services
in the process have now stopped, the call to StartServiceCtrlDispatcher
(in the dispatcher thread) finally returns, and winservice_stop() returns,
and the process finally exits.
*/


#ifdef WIN32

#include <assert.h>
#include <winsvc.h>

/* This is just a placeholder, and doesn't actually constrain the
  service name.  You have to provide *some* service name to the SCM
  API, but for services that are marked SERVICE_WIN32_OWN_PROCESS (as
  is the case for svnserve), the service name is ignored.  It *is*
  relevant for service binaries that run more than one service in a
  single process. */
#define WINSERVICE_SERVICE_NAME "svnserve"


/* Win32 handle to the dispatcher thread. */
static HANDLE winservice_dispatcher_thread = NULL;

/* Win32 event handle, used to notify winservice_start() that we have
   successfully connected to the SCM. */
static HANDLE winservice_start_event = NULL;

/* RPC handle that allows us to notify the SCM of changes in our
   service status. */
static SERVICE_STATUS_HANDLE winservice_status_handle = NULL;

/* Our current idea of the service status (stopped, running, controls
   accepted, exit code, etc.) */
static SERVICE_STATUS winservice_status;


#ifdef SVN_DEBUG
static void dbg_print(const char* text)
{
  OutputDebugStringA(text);
}
#else
/* Make sure dbg_print compiles to nothing in release builds. */
#define dbg_print(text)
#endif


static void winservice_atexit(void);

/* Notifies the Service Control Manager of the current state of the
   service. */
static void
winservice_update_state(void)
{
  if (winservice_status_handle != NULL)
    {
      if (!SetServiceStatus(winservice_status_handle, &winservice_status))
        {
          dbg_print("SetServiceStatus - FAILED\r\n");
        }
    }
}


/* This function cleans up state associated with the service support.
   If the dispatcher thread handle is non-NULL, then this function
   will wait for the dispatcher thread to exit. */
static void
winservice_cleanup(void)
{
  if (winservice_start_event != NULL)
    {
      CloseHandle(winservice_start_event);
      winservice_start_event = NULL;
    }

  if (winservice_dispatcher_thread != NULL)
    {
      dbg_print("winservice_cleanup:"
                " waiting for dispatcher thread to exit\r\n");
      WaitForSingleObject(winservice_dispatcher_thread, INFINITE);
      CloseHandle(winservice_dispatcher_thread);
      winservice_dispatcher_thread = NULL;
    }
}


/* The SCM invokes this function to cause state changes in the
   service. */
static void WINAPI
winservice_handler(DWORD control)
{
  switch (control)
    {
    case SERVICE_CONTROL_INTERROGATE:
      /* The SCM just wants to check our state.  We are required to
         call SetServiceStatus, but we don't need to make any state
         changes. */
      dbg_print("SERVICE_CONTROL_INTERROGATE\r\n");
      winservice_update_state();
      break;

    case SERVICE_CONTROL_STOP:
      dbg_print("SERVICE_CONTROL_STOP\r\n");
      winservice_status.dwCurrentState = SERVICE_STOP_PENDING;
      winservice_update_state();
      winservice_notify_stop();
      break;
    }
}


/* This is the "service main" routine (in the Win32 terminology).

   Normally, this function (thread) implements the "main" loop of a
   service.  However, in order to minimize changes to the svnserve
   main() function, this function is running in a different thread,
   and main() is blocked in winservice_start(), waiting for
   winservice_start_event.  So this function (thread) only needs to
   signal that event to "start" the service.

   If this function succeeds, it signals winservice_start_event, which
   wakes up the winservice_start() frame that is blocked. */
static void WINAPI
winservice_service_main(DWORD argc, LPTSTR *argv)
{
  DWORD error;

  assert(winservice_start_event != NULL);

  winservice_status_handle =
    RegisterServiceCtrlHandler(WINSERVICE_SERVICE_NAME, winservice_handler);
  if (winservice_status_handle == NULL)
    {
      /* Ok, that's not fair.  We received a request to start a service,
         and now we cannot bind to the SCM in order to update status?
         Bring down the app. */
      error = GetLastError();
      dbg_print("RegisterServiceCtrlHandler FAILED\r\n");
      /* Put the error code somewhere where winservice_start can find it. */
      winservice_status.dwWin32ExitCode = error;
      SetEvent(winservice_start_event);
      return;
    }

  winservice_status.dwCurrentState = SERVICE_START_PENDING;
  winservice_status.dwWin32ExitCode = ERROR_SUCCESS;
  winservice_update_state();

  dbg_print("winservice_service_main: service is starting\r\n");
  SetEvent(winservice_start_event);
}


static const SERVICE_TABLE_ENTRY winservice_service_table[] =
  {
    { WINSERVICE_SERVICE_NAME, winservice_service_main },
    { NULL, NULL }
  };


/* This is the thread routine for the "dispatcher" thread.  The
   purpose of this thread is to connect this process with the Service
   Control Manager, which allows this process to receive control
   requests from the SCM, and allows this process to update the SCM
   with status information.

   The StartServiceCtrlDispatcher connects this process to the SCM.
   If it succeeds, then it will not return until all of the services
   running in this process have stopped.  (In our case, there is only
   one service per process.) */
static DWORD WINAPI
winservice_dispatcher_thread_routine(PVOID arg)
{
  dbg_print("winservice_dispatcher_thread_routine: starting\r\n");

  if (!StartServiceCtrlDispatcher(winservice_service_table))
    {
      /* This is a common error.  Usually, it means the user has
         invoked the service with the --service flag directly.  This
         is incorrect.  The only time the --service flag is passed is
         when the process is being started by the SCM. */
      DWORD error = GetLastError();

      dbg_print("dispatcher: FAILED to connect to SCM\r\n");
      return error;
    }

  dbg_print("dispatcher: SCM is done using this process -- exiting\r\n");
  return ERROR_SUCCESS;
}


/* If svnserve needs to run as a Win32 service, then we need to
   coordinate with the Service Control Manager (SCM) before
   continuing.  This function call registers the svnserve.exe process
   with the SCM, waits for the "start" command from the SCM (which
   will come very quickly), and confirms that those steps succeeded.

   After this call succeeds, the service should perform whatever work
   it needs to start the service, and then the service should call
   winservice_running() (if no errors occurred) or winservice_stop()
   (if something failed during startup). */
svn_error_t *
winservice_start(void)
{
  HANDLE handles[2];
  DWORD thread_id;
  DWORD error_code;
  apr_status_t apr_status;
  DWORD wait_status;

  dbg_print("winservice_start: starting svnserve as a service...\r\n");

  ZeroMemory(&winservice_status, sizeof(winservice_status));
  winservice_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  winservice_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  winservice_status.dwCurrentState = SERVICE_STOPPED;

  /* Create the event that will wake up this thread when the SCM
     creates the ServiceMain thread. */
  winservice_start_event = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (winservice_start_event == NULL)
    {
      apr_status = apr_get_os_error();
      return svn_error_wrap_apr(apr_status,
                                _("Failed to create winservice_start_event"));
    }

  winservice_dispatcher_thread =
    (HANDLE)CreateThread(NULL, 0, winservice_dispatcher_thread_routine,
                         NULL, 0, &thread_id);
  if (winservice_dispatcher_thread == NULL)
    {
      apr_status = apr_get_os_error();
      winservice_cleanup();
      return svn_error_wrap_apr(apr_status,
                                _("The service failed to start"));
    }

  /* Next, we wait for the "start" event to fire (meaning the service
     logic has successfully started), or for the dispatch thread to
     exit (meaning the service logic could not start). */

  handles[0] = winservice_start_event;
  handles[1] = winservice_dispatcher_thread;
  wait_status = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
  switch (wait_status)
    {
    case WAIT_OBJECT_0:
      dbg_print("winservice_start: service is now starting\r\n");

      /* We no longer need the start event. */
      CloseHandle(winservice_start_event);
      winservice_start_event = NULL;

      /* Register our cleanup logic. */
      atexit(winservice_atexit);
      return SVN_NO_ERROR;

    case WAIT_OBJECT_0+1:
      /* The dispatcher thread exited without starting the service.
         This happens when the dispatcher fails to connect to the SCM. */
      dbg_print("winservice_start: dispatcher thread has failed\r\n");

      if (GetExitCodeThread(winservice_dispatcher_thread, &error_code))
        {
          dbg_print("winservice_start: dispatcher thread failed\r\n");

          if (error_code == ERROR_SUCCESS)
            error_code = ERROR_INTERNAL_ERROR;

        }
      else
        {
          error_code = ERROR_INTERNAL_ERROR;
        }

      CloseHandle(winservice_dispatcher_thread);
      winservice_dispatcher_thread = NULL;

      winservice_cleanup();

      return svn_error_wrap_apr
        (APR_FROM_OS_ERROR(error_code),
         _("Failed to connect to Service Control Manager"));

    default:
      /* This should never happen! This indicates that our handles are
         broken, or some other highly unusual error.  There is nothing
         rational that we can do to recover. */
      apr_status = apr_get_os_error();
      dbg_print("winservice_start: WaitForMultipleObjects failed!\r\n");

      winservice_cleanup();
      return svn_error_wrap_apr
        (apr_status, _("The service failed to start; an internal error"
                       " occurred while starting the service"));
    }
}


/* main() calls this function in order to inform the SCM that the
   service has successfully started.  This is required; otherwise, the
   SCM will believe that the service is stuck in the "starting" state,
   and management tools will also believe that the service is stuck. */
void
winservice_running(void)
{
  winservice_status.dwCurrentState = SERVICE_RUNNING;
  winservice_update_state();
  dbg_print("winservice_notify_running: service is now running\r\n");
}


/* main() calls this function in order to notify the SCM that the
   service has stopped.  This function also handles cleaning up the
   dispatcher thread (the one that we created above in
   winservice_start. */
static void
winservice_stop(DWORD exit_code)
{
  dbg_print("winservice_stop - notifying SCM that service has stopped\r\n");
  winservice_status.dwCurrentState = SERVICE_STOPPED;
  winservice_status.dwWin32ExitCode = exit_code;
  winservice_update_state();

  if (winservice_dispatcher_thread != NULL)
    {
      dbg_print("waiting for dispatcher thread to exit...\r\n");
      WaitForSingleObject(winservice_dispatcher_thread, INFINITE);
      dbg_print("dispatcher thread has exited.\r\n");

      CloseHandle(winservice_dispatcher_thread);
      winservice_dispatcher_thread = NULL;
    }
  else
    {
      /* There was no dispatcher thread.  So we never started in
         the first place. */
      exit_code = winservice_status.dwWin32ExitCode;
      dbg_print("dispatcher thread was not running\r\n");
    }

  if (winservice_start_event != NULL)
    {
      CloseHandle(winservice_start_event);
      winservice_start_event = NULL;
    }

  dbg_print("winservice_stop - service has stopped\r\n");
}


/* This function is installed as an atexit-handler.  This is done so
  that we don't need to alter every exit() call in main(). */
static void
winservice_atexit(void)
{
  dbg_print("winservice_atexit - stopping\r\n");
  winservice_stop(ERROR_SUCCESS);
}


svn_boolean_t
winservice_is_stopping(void)
{
  return (winservice_status.dwCurrentState == SERVICE_STOP_PENDING);
}

#endif /* WIN32 */
