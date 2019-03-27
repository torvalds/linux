/* TUI Interpreter definitions for GDB, the GNU debugger.

   Copyright 2003 Free Software Foundation, Inc.

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
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "interps.h"
#include "top.h"
#include "event-top.h"
#include "event-loop.h"
#include "ui-out.h"
#include "cli-out.h"
#include "tui/tui-data.h"
#include "readline/readline.h"
#include "tui/tui-win.h"
#include "tui/tui.h"
#include "tui/tui-io.h"

/* Set to 1 when the TUI mode must be activated when we first start gdb.  */
static int tui_start_enabled = 0;

/* Cleanup the tui before exiting.  */

static void
tui_exit (void)
{
  /* Disable the tui.  Curses mode is left leaving the screen
     in a clean state (see endwin()).  */
  tui_disable ();
}

/* These implement the TUI interpreter.  */

static void *
tui_init (void)
{
  /* Install exit handler to leave the screen in a good shape.  */
  atexit (tui_exit);

  tui_initialize_static_data ();

  tui_initialize_io ();
  tui_initialize_readline ();

  return NULL;
}

static int
tui_resume (void *data)
{
  struct ui_file *stream;

  /* gdb_setup_readline will change gdb_stdout.  If the TUI was previously
     writing to gdb_stdout, then set it to the new gdb_stdout afterwards.  */

  stream = cli_out_set_stream (tui_old_uiout, gdb_stdout);
  if (stream != gdb_stdout)
    {
      cli_out_set_stream (tui_old_uiout, stream);
      stream = NULL;
    }

  gdb_setup_readline ();

  if (stream != NULL)
    cli_out_set_stream (tui_old_uiout, gdb_stdout);

  if (tui_start_enabled)
    tui_enable ();
  return 1;
}

static int
tui_suspend (void *data)
{
  tui_start_enabled = tui_active;
  tui_disable ();
  return 1;
}

/* Display the prompt if we are silent.  */

static int
tui_display_prompt_p (void *data)
{
  if (interp_quiet_p (NULL))
    return 0;
  else
    return 1;
}

static int
tui_exec (void *data, const char *command_str)
{
  internal_error (__FILE__, __LINE__, "tui_exec called");
}


/* Initialize all the necessary variables, start the event loop,
   register readline, and stdin, start the loop.  */

static void
tui_command_loop (void *data)
{
  int length;
  char *a_prompt;
  char *gdb_prompt = get_prompt ();

  /* If we are using readline, set things up and display the first
     prompt, otherwise just print the prompt.  */
  if (async_command_editing_p)
    {
      /* Tell readline what the prompt to display is and what function
         it will need to call after a whole line is read. This also
         displays the first prompt.  */
      length = strlen (PREFIX (0)) + strlen (gdb_prompt) + strlen (SUFFIX (0)) + 1;
      a_prompt = (char *) xmalloc (length);
      strcpy (a_prompt, PREFIX (0));
      strcat (a_prompt, gdb_prompt);
      strcat (a_prompt, SUFFIX (0));
      rl_callback_handler_install (a_prompt, input_handler);
    }
  else
    display_gdb_prompt (0);

  /* Loop until there is nothing to do. This is the entry point to the
     event loop engine. gdb_do_one_event, called via catch_errors()
     will process one event for each invocation.  It blocks waits for
     an event and then processes it.  >0 when an event is processed, 0
     when catch_errors() caught an error and <0 when there are no
     longer any event sources registered.  */
  while (1)
    {
      int result = catch_errors (gdb_do_one_event, 0, "", RETURN_MASK_ALL);
      if (result < 0)
	break;

      /* Update gdb output according to TUI mode.  Since catch_errors
         preserves the uiout from changing, this must be done at top
         level of event loop.  */
      if (tui_active)
        uiout = tui_out;
      else
        uiout = tui_old_uiout;
      
      if (result == 0)
	{
	  /* FIXME: this should really be a call to a hook that is
	     interface specific, because interfaces can display the
	     prompt in their own way.  */
	  display_gdb_prompt (0);
	  /* This call looks bizarre, but it is required.  If the user
	     entered a command that caused an error,
	     after_char_processing_hook won't be called from
	     rl_callback_read_char_wrapper.  Using a cleanup there
	     won't work, since we want this function to be called
	     after a new prompt is printed.  */
	  if (after_char_processing_hook)
	    (*after_char_processing_hook) ();
	  /* Maybe better to set a flag to be checked somewhere as to
	     whether display the prompt or not.  */
	}
    }

  /* We are done with the event loop. There are no more event sources
     to listen to.  So we exit GDB.  */
  return;
}

void
_initialize_tui_interp (void)
{
  static const struct interp_procs procs = {
    tui_init,
    tui_resume,
    tui_suspend,
    tui_exec,
    tui_display_prompt_p,
    tui_command_loop,
  };
  struct interp *tui_interp;

  /* Create a default uiout builder for the TUI. */
  tui_out = tui_out_new (gdb_stdout);
  interp_add (interp_new ("tui", NULL, tui_out, &procs));
  if (interpreter_p && strcmp (interpreter_p, "tui") == 0)
    tui_start_enabled = 1;

  if (interpreter_p && strcmp (interpreter_p, INTERP_CONSOLE) == 0)
    {
      xfree (interpreter_p);
      interpreter_p = xstrdup ("tui");
    }
}
