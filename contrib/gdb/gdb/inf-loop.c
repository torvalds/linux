/* Handling of inferior events for the event loop for GDB, the GNU debugger.
   Copyright 1999 Free Software Foundation, Inc.
   Written by Elena Zannoni <ezannoni@cygnus.com> of Cygnus Solutions.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA. */

#include "defs.h"
#include "inferior.h"		/* For fetch_inferior_event. */
#include "target.h"             /* For enum inferior_event_type. */
#include "event-loop.h"
#include "event-top.h"
#include "inf-loop.h"
#include "remote.h"

static int fetch_inferior_event_wrapper (gdb_client_data client_data);
static void complete_execution (void);

void
inferior_event_handler_wrapper (gdb_client_data client_data)
{
  inferior_event_handler (INF_QUIT_REQ, client_data);
}

/* General function to handle events in the inferior. So far it just
   takes care of detecting errors reported by select() or poll(),
   otherwise it assumes that all is OK, and goes on reading data from
   the fd. This however may not always be what we want to do. */
void
inferior_event_handler (enum inferior_event_type event_type, 
			gdb_client_data client_data)
{
  switch (event_type)
    {
    case INF_ERROR:
      printf_unfiltered ("error detected from target.\n");
      target_async (NULL, 0);
      pop_target ();
      discard_all_continuations ();
      do_exec_error_cleanups (ALL_CLEANUPS);
      break;

    case INF_REG_EVENT:
      /* Use catch errors for now, until the inner layers of
	 fetch_inferior_event (i.e. readchar) can return meaningful
	 error status.  If an error occurs while getting an event from
	 the target, just get rid of the target. */
      if (!catch_errors (fetch_inferior_event_wrapper, 
			 client_data, "", RETURN_MASK_ALL))
	{
	  target_async (NULL, 0);
	  pop_target ();
	  discard_all_continuations ();
	  do_exec_error_cleanups (ALL_CLEANUPS);
	  display_gdb_prompt (0);
	}
      break;

    case INF_EXEC_COMPLETE:
      /* Is there anything left to do for the command issued to
         complete? */
      do_all_continuations ();
      /* Reset things after target has stopped for the async commands. */
      complete_execution ();
      break;

    case INF_EXEC_CONTINUE:
      /* Is there anything left to do for the command issued to
         complete? */
      do_all_intermediate_continuations ();
      break;

    case INF_QUIT_REQ: 
      /* FIXME: ezannoni 1999-10-04. This call should really be a
	 target vector entry, so that it can be used for any kind of
	 targets. */
      async_remote_interrupt_twice (NULL);
      break;

    case INF_TIMER:
    default:
      printf_unfiltered ("Event type not recognized.\n");
      break;
    }
}

static int 
fetch_inferior_event_wrapper (gdb_client_data client_data)
{
  fetch_inferior_event (client_data);
  return 1;
}

/* Reset proper settings after an asynchronous command has finished.
   If the execution command was in synchronous mode, register stdin
   with the event loop, and reset the prompt. */

static void
complete_execution (void)
{
  target_executing = 0;
  
  /* Unregister the inferior from the event loop. This is done so that
     when the inferior is not running we don't get distracted by
     spurious inferior output. */
  target_async (NULL, 0);

  if (sync_execution)
    {
      do_exec_error_cleanups (ALL_CLEANUPS);
      display_gdb_prompt (0);
    }
  else
    {
      if (exec_done_display_p)
	printf_unfiltered ("completed.\n");
    }
}
