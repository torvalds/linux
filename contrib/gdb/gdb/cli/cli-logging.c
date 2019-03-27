/* Command-line output logging for GDB, the GNU debugger.

   Copyright 2003
   Free Software Foundation, Inc.

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
#include "gdbcmd.h"
#include "ui-out.h"

#include "gdb_string.h"

/* These hold the pushed copies of the gdb output files.
   If NULL then nothing has yet been pushed.  */
struct saved_output_files
{
  struct ui_file *out;
  struct ui_file *err;
  struct ui_file *log;
  struct ui_file *targ;
};
static struct saved_output_files saved_output;
static char *saved_filename;

static char *logging_filename;
int logging_overwrite, logging_redirect;

/* If we've pushed output files, close them and pop them.  */
static void
pop_output_files (void)
{
  /* Only delete one of the files -- they are all set to the same
     value.  */
  ui_file_delete (gdb_stdout);
  gdb_stdout = saved_output.out;
  gdb_stderr = saved_output.err;
  gdb_stdlog = saved_output.log;
  gdb_stdtarg = saved_output.targ;
  saved_output.out = NULL;
  saved_output.err = NULL;
  saved_output.log = NULL;
  saved_output.targ = NULL;

  ui_out_redirect (uiout, NULL);
}

/* This is a helper for the `set logging' command.  */
static void
handle_redirections (int from_tty)
{
  struct ui_file *output;

  if (saved_filename != NULL)
    {
      fprintf_unfiltered (gdb_stdout, "Already logging to %s.\n",
			  saved_filename);
      return;
    }

  output = gdb_fopen (logging_filename, logging_overwrite ? "w" : "a");
  if (output == NULL)
    perror_with_name ("set logging");

  /* Redirects everything to gdb_stdout while this is running.  */
  if (!logging_redirect)
    {
      output = tee_file_new (gdb_stdout, 0, output, 1);
      if (output == NULL)
	perror_with_name ("set logging");
      if (from_tty)
	fprintf_unfiltered (gdb_stdout, "Copying output to %s.\n",
			    logging_filename);
    }
  else if (from_tty)
    fprintf_unfiltered (gdb_stdout, "Redirecting output to %s.\n",
			logging_filename);

  saved_filename = xstrdup (logging_filename);
  saved_output.out = gdb_stdout;
  saved_output.err = gdb_stderr;
  saved_output.log = gdb_stdlog;
  saved_output.targ = gdb_stdtarg;

  gdb_stdout = output;
  gdb_stderr = output;
  gdb_stdlog = output;
  gdb_stdtarg = output;

  if (ui_out_redirect (uiout, gdb_stdout) < 0)
    warning ("Current output protocol does not support redirection");
}

static void
set_logging_on (char *args, int from_tty)
{
  char *rest = args;
  if (rest && *rest)
    {
      xfree (logging_filename);
      logging_filename = xstrdup (rest);
    }
  handle_redirections (from_tty);
}

static void 
set_logging_off (char *args, int from_tty)
{
  if (saved_filename == NULL)
    return;

  pop_output_files ();
  if (from_tty)
    fprintf_unfiltered (gdb_stdout, "Done logging to %s.\n", saved_filename);
  xfree (saved_filename);
  saved_filename = NULL;
}

static void
set_logging_command (char *args, int from_tty)
{
  printf_unfiltered ("\"set logging\" lets you log output to a file.\n");
  printf_unfiltered ("Usage: set logging on [FILENAME]\n");
  printf_unfiltered ("       set logging off\n");
  printf_unfiltered ("       set logging file FILENAME\n");
  printf_unfiltered ("       set logging overwrite [on|off]\n");
  printf_unfiltered ("       set logging redirect [on|off]\n");
}

void
show_logging_command (char *args, int from_tty)
{
  if (saved_filename)
    printf_unfiltered ("Currently logging to \"%s\".\n", saved_filename);
  if (saved_filename == NULL
      || strcmp (logging_filename, saved_filename) != 0)
    printf_unfiltered ("Future logs will be written to %s.\n",
		       logging_filename);

  if (logging_overwrite)
    printf_unfiltered ("Logs will overwrite the log file.\n");
  else
    printf_unfiltered ("Logs will be appended to the log file.\n");

  if (logging_redirect)
    printf_unfiltered ("Output will be sent only to the log file.\n");
  else
    printf_unfiltered ("Output will be logged and displayed.\n");
}

void
_initialize_cli_logging (void)
{
  static struct cmd_list_element *set_logging_cmdlist, *show_logging_cmdlist;

  
  add_prefix_cmd ("logging", class_support, set_logging_command,
		  "Set logging options", &set_logging_cmdlist,
		  "set logging ", 0, &setlist);
  add_prefix_cmd ("logging", class_support, show_logging_command,
		  "Show logging options", &show_logging_cmdlist,
		  "show logging ", 0, &showlist);
  add_setshow_boolean_cmd ("overwrite", class_support, &logging_overwrite,
			   "Set whether logging overwrites or appends "
			   "to the log file.\n",
			   "Show whether logging overwrites or appends "
			   "to the log file.\n",
			   NULL, NULL, &set_logging_cmdlist, &show_logging_cmdlist);
  add_setshow_boolean_cmd ("redirect", class_support, &logging_redirect,
			   "Set the logging output mode.\n"
			   "If redirect is off, output will go to both the "
			   "screen and the log file.\n"
			   "If redirect is on, output will go only to the log "
			   "file.",
			   "Show the logging output mode.\n"
			   "If redirect is off, output will go to both the "
			   "screen and the log file.\n"
			   "If redirect is on, output will go only to the log "
			   "file.",
			   NULL, NULL, &set_logging_cmdlist, &show_logging_cmdlist);
  add_setshow_cmd ("file", class_support, var_filename, &logging_filename,
		   "Set the current logfile.", "Show the current logfile.",
		   NULL, NULL, &set_logging_cmdlist, &show_logging_cmdlist);
  add_cmd ("on", class_support, set_logging_on,
	   "Enable logging.", &set_logging_cmdlist);
  add_cmd ("off", class_support, set_logging_off,
	   "Disable logging.", &set_logging_cmdlist);

  logging_filename = xstrdup ("gdb.txt");
}
