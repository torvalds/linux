/* TUI support I/O functions.

   Copyright 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free Software
   Foundation, Inc.

   Contributed by Hewlett-Packard Company.

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
#include "terminal.h"
#include "target.h"
#include "event-loop.h"
#include "event-top.h"
#include "command.h"
#include "top.h"
#include "readline/readline.h"
#include "tui/tui.h"
#include "tui/tui-data.h"
#include "tui/tui-io.h"
#include "tui/tui-command.h"
#include "tui/tui-win.h"
#include "tui/tui-wingeneral.h"
#include "tui/tui-file.h"
#include "ui-out.h"
#include "cli-out.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>

#include "gdb_curses.h"

int
key_is_start_sequence (int ch)
{
  return (ch == 27);
}

int
key_is_end_sequence (int ch)
{
  return (ch == 126);
}

int
key_is_backspace (int ch)
{
  return (ch == 8);
}

int
key_is_command_char (int ch)
{
  return ((ch == KEY_NPAGE) || (ch == KEY_PPAGE)
	  || (ch == KEY_LEFT) || (ch == KEY_RIGHT)
	  || (ch == KEY_UP) || (ch == KEY_DOWN)
	  || (ch == KEY_SF) || (ch == KEY_SR)
	  || (ch == (int)'\f') || key_is_start_sequence (ch));
}

/* Use definition from readline 4.3.  */
#undef CTRL_CHAR
#define CTRL_CHAR(c) ((c) < control_character_threshold && (((c) & 0x80) == 0))

/* This file controls the IO interactions between gdb and curses.
   When the TUI is enabled, gdb has two modes a curses and a standard
   mode.

   In curses mode, the gdb outputs are made in a curses command window.
   For this, the gdb_stdout and gdb_stderr are redirected to the specific
   ui_file implemented by TUI.  The output is handled by tui_puts().
   The input is also controlled by curses with tui_getc().  The readline
   library uses this function to get its input.  Several readline hooks
   are installed to redirect readline output to the TUI (see also the
   note below).

   In normal mode, the gdb outputs are restored to their origin, that
   is as if TUI is not used.  Readline also uses its original getc()
   function with stdin.

   Note SCz/2001-07-21: the current readline is not clean in its management of
   the output.  Even if we install a redisplay handler, it sometimes writes on
   a stdout file.  It is important to redirect every output produced by
   readline, otherwise the curses window will be garbled.  This is implemented
   with a pipe that TUI reads and readline writes to.  A gdb input handler
   is created so that reading the pipe is handled automatically.
   This will probably not work on non-Unix platforms.  The best fix is
   to make readline clean enougth so that is never write on stdout.

   Note SCz/2002-09-01: we now use more readline hooks and it seems that
   with them we don't need the pipe anymore (verified by creating the pipe
   and closing its end so that write causes a SIGPIPE).  The old pipe code
   is still there and can be conditionally removed by
   #undef TUI_USE_PIPE_FOR_READLINE.  */

/* For gdb 5.3, prefer to continue the pipe hack as a backup wheel.  */
#define TUI_USE_PIPE_FOR_READLINE
/*#undef TUI_USE_PIPE_FOR_READLINE*/

/* TUI output files.  */
static struct ui_file *tui_stdout;
static struct ui_file *tui_stderr;
struct ui_out *tui_out;

/* GDB output files in non-curses mode.  */
static struct ui_file *tui_old_stdout;
static struct ui_file *tui_old_stderr;
struct ui_out *tui_old_uiout;

/* Readline previous hooks.  */
static Function *tui_old_rl_getc_function;
static VFunction *tui_old_rl_redisplay_function;
static VFunction *tui_old_rl_prep_terminal;
static VFunction *tui_old_rl_deprep_terminal;
static int tui_old_readline_echoing_p;

/* Readline output stream.
   Should be removed when readline is clean.  */
static FILE *tui_rl_outstream;
static FILE *tui_old_rl_outstream;
#ifdef TUI_USE_PIPE_FOR_READLINE
static int tui_readline_pipe[2];
#endif

/* The last gdb prompt that was registered in readline.
   This may be the main gdb prompt or a secondary prompt.  */
static char *tui_rl_saved_prompt;

static unsigned int tui_handle_resize_during_io (unsigned int);

static void
tui_putc (char c)
{
  char buf[2];

  buf[0] = c;
  buf[1] = 0;
  tui_puts (buf);
}

/* Print the string in the curses command window.  */
void
tui_puts (const char *string)
{
  static int tui_skip_line = -1;
  char c;
  WINDOW *w;

  w = TUI_CMD_WIN->generic.handle;
  while ((c = *string++) != 0)
    {
      /* Catch annotation and discard them.  We need two \032 and
         discard until a \n is seen.  */
      if (c == '\032')
        {
          tui_skip_line++;
        }
      else if (tui_skip_line != 1)
        {
          tui_skip_line = -1;
          waddch (w, c);
        }
      else if (c == '\n')
        tui_skip_line = -1;
    }
  getyx (w, TUI_CMD_WIN->detail.command_info.cur_line,
         TUI_CMD_WIN->detail.command_info.curch);
  TUI_CMD_WIN->detail.command_info.start_line = TUI_CMD_WIN->detail.command_info.cur_line;

  /* We could defer the following.  */
  wrefresh (w);
  fflush (stdout);
}

/* Readline callback.
   Redisplay the command line with its prompt after readline has
   changed the edited text.  */
void
tui_redisplay_readline (void)
{
  int prev_col;
  int height;
  int col, line;
  int c_pos;
  int c_line;
  int in;
  WINDOW *w;
  char *prompt;
  int start_line;

  /* Detect when we temporarily left SingleKey and now the readline
     edit buffer is empty, automatically restore the SingleKey mode.  */
  if (tui_current_key_mode == TUI_ONE_COMMAND_MODE && rl_end == 0)
    tui_set_key_mode (TUI_SINGLE_KEY_MODE);

  if (tui_current_key_mode == TUI_SINGLE_KEY_MODE)
    prompt = "";
  else
    prompt = tui_rl_saved_prompt;
  
  c_pos = -1;
  c_line = -1;
  w = TUI_CMD_WIN->generic.handle;
  start_line = TUI_CMD_WIN->detail.command_info.start_line;
  wmove (w, start_line, 0);
  prev_col = 0;
  height = 1;
  for (in = 0; prompt && prompt[in]; in++)
    {
      waddch (w, prompt[in]);
      getyx (w, line, col);
      if (col < prev_col)
        height++;
      prev_col = col;
    }
  for (in = 0; in < rl_end; in++)
    {
      unsigned char c;
      
      c = (unsigned char) rl_line_buffer[in];
      if (in == rl_point)
	{
          getyx (w, c_line, c_pos);
	}

      if (CTRL_CHAR (c) || c == RUBOUT)
	{
          waddch (w, '^');
          waddch (w, CTRL_CHAR (c) ? UNCTRL (c) : '?');
	}
      else
	{
          waddch (w, c);
	}
      if (c == '\n')
        {
          getyx (w, TUI_CMD_WIN->detail.command_info.start_line,
                 TUI_CMD_WIN->detail.command_info.curch);
        }
      getyx (w, line, col);
      if (col < prev_col)
        height++;
      prev_col = col;
    }
  wclrtobot (w);
  getyx (w, TUI_CMD_WIN->detail.command_info.start_line,
         TUI_CMD_WIN->detail.command_info.curch);
  if (c_line >= 0)
    {
      wmove (w, c_line, c_pos);
      TUI_CMD_WIN->detail.command_info.cur_line = c_line;
      TUI_CMD_WIN->detail.command_info.curch = c_pos;
    }
  TUI_CMD_WIN->detail.command_info.start_line -= height - 1;

  wrefresh (w);
  fflush(stdout);
}

/* Readline callback to prepare the terminal.  It is called once
   each time we enter readline.  Terminal is already setup in curses mode.  */
static void
tui_prep_terminal (int notused1)
{
  /* Save the prompt registered in readline to correctly display it.
     (we can't use gdb_prompt() due to secondary prompts and can't use
     rl_prompt because it points to an alloca buffer).  */
  xfree (tui_rl_saved_prompt);
  tui_rl_saved_prompt = xstrdup (rl_prompt);
}

/* Readline callback to restore the terminal.  It is called once
   each time we leave readline.  There is nothing to do in curses mode.  */
static void
tui_deprep_terminal (void)
{
}

#ifdef TUI_USE_PIPE_FOR_READLINE
/* Read readline output pipe and feed the command window with it.
   Should be removed when readline is clean.  */
static void
tui_readline_output (int code, gdb_client_data data)
{
  int size;
  char buf[256];

  size = read (tui_readline_pipe[0], buf, sizeof (buf) - 1);
  if (size > 0 && tui_active)
    {
      buf[size] = 0;
      tui_puts (buf);
    }
}
#endif

/* Return the portion of PATHNAME that should be output when listing
   possible completions.  If we are hacking filename completion, we
   are only interested in the basename, the portion following the
   final slash.  Otherwise, we return what we were passed.

   Comes from readline/complete.c  */
static char *
printable_part (char *pathname)
{
  char *temp;

  temp = rl_filename_completion_desired ? strrchr (pathname, '/') : (char *)NULL;
#if defined (__MSDOS__)
  if (rl_filename_completion_desired && temp == 0 && isalpha (pathname[0]) && pathname[1] == ':')
    temp = pathname + 1;
#endif
  return (temp ? ++temp : pathname);
}

/* Output TO_PRINT to rl_outstream.  If VISIBLE_STATS is defined and we
   are using it, check for and output a single character for `special'
   filenames.  Return the number of characters we output. */

#define PUTX(c) \
    do { \
      if (CTRL_CHAR (c)) \
        { \
          tui_puts ("^"); \
          tui_putc (UNCTRL (c)); \
          printed_len += 2; \
        } \
      else if (c == RUBOUT) \
	{ \
	  tui_puts ("^?"); \
	  printed_len += 2; \
	} \
      else \
	{ \
	  tui_putc (c); \
	  printed_len++; \
	} \
    } while (0)

static int
print_filename (char *to_print, char *full_pathname)
{
  int printed_len = 0;
  char *s;

  for (s = to_print; *s; s++)
    {
      PUTX (*s);
    }
  return printed_len;
}

/* The user must press "y" or "n".  Non-zero return means "y" pressed.
   Comes from readline/complete.c  */
static int
get_y_or_n (void)
{
  extern int _rl_abort_internal ();
  int c;

  for (;;)
    {
      c = rl_read_key ();
      if (c == 'y' || c == 'Y' || c == ' ')
	return (1);
      if (c == 'n' || c == 'N' || c == RUBOUT)
	return (0);
      if (c == ABORT_CHAR)
	_rl_abort_internal ();
      beep ();
    }
}

/* A convenience function for displaying a list of strings in
   columnar format on readline's output stream.  MATCHES is the list
   of strings, in argv format, LEN is the number of strings in MATCHES,
   and MAX is the length of the longest string in MATCHES.

   Comes from readline/complete.c and modified to write in
   the TUI command window using tui_putc/tui_puts.  */
static void
tui_rl_display_match_list (char **matches, int len, int max)
{
  typedef int QSFUNC (const void *, const void *);
  extern int _rl_qsort_string_compare (char **, char **);
  extern int _rl_print_completions_horizontally;
  
  int count, limit, printed_len;
  int i, j, k, l;
  char *temp;

  /* Screen dimension correspond to the TUI command window.  */
  int screenwidth = TUI_CMD_WIN->generic.width;

  /* If there are many items, then ask the user if she really wants to
     see them all. */
  if (len >= rl_completion_query_items)
    {
      char msg[256];

      sprintf (msg, "\nDisplay all %d possibilities? (y or n)", len);
      tui_puts (msg);
      if (get_y_or_n () == 0)
	{
	  tui_puts ("\n");
	  return;
	}
    }

  /* How many items of MAX length can we fit in the screen window? */
  max += 2;
  limit = screenwidth / max;
  if (limit != 1 && (limit * max == screenwidth))
    limit--;

  /* Avoid a possible floating exception.  If max > screenwidth,
     limit will be 0 and a divide-by-zero fault will result. */
  if (limit == 0)
    limit = 1;

  /* How many iterations of the printing loop? */
  count = (len + (limit - 1)) / limit;

  /* Watch out for special case.  If LEN is less than LIMIT, then
     just do the inner printing loop.
	   0 < len <= limit  implies  count = 1. */

  /* Sort the items if they are not already sorted. */
  if (rl_ignore_completion_duplicates == 0)
    qsort (matches + 1, len, sizeof (char *),
           (QSFUNC *)_rl_qsort_string_compare);

  tui_putc ('\n');

  if (_rl_print_completions_horizontally == 0)
    {
      /* Print the sorted items, up-and-down alphabetically, like ls. */
      for (i = 1; i <= count; i++)
	{
	  for (j = 0, l = i; j < limit; j++)
	    {
	      if (l > len || matches[l] == 0)
		break;
	      else
		{
		  temp = printable_part (matches[l]);
		  printed_len = print_filename (temp, matches[l]);

		  if (j + 1 < limit)
		    for (k = 0; k < max - printed_len; k++)
		      tui_putc (' ');
		}
	      l += count;
	    }
	  tui_putc ('\n');
	}
    }
  else
    {
      /* Print the sorted items, across alphabetically, like ls -x. */
      for (i = 1; matches[i]; i++)
	{
	  temp = printable_part (matches[i]);
	  printed_len = print_filename (temp, matches[i]);
	  /* Have we reached the end of this line? */
	  if (matches[i+1])
	    {
	      if (i && (limit > 1) && (i % limit) == 0)
		tui_putc ('\n');
	      else
		for (k = 0; k < max - printed_len; k++)
		  tui_putc (' ');
	    }
	}
      tui_putc ('\n');
    }
}

/* Setup the IO for curses or non-curses mode.
   - In non-curses mode, readline and gdb use the standard input and
   standard output/error directly.
   - In curses mode, the standard output/error is controlled by TUI
   with the tui_stdout and tui_stderr.  The output is redirected in
   the curses command window.  Several readline callbacks are installed
   so that readline asks for its input to the curses command window
   with wgetch().  */
void
tui_setup_io (int mode)
{
  extern int readline_echoing_p;
 
  if (mode)
    {
      /* Redirect readline to TUI.  */
      tui_old_rl_redisplay_function = rl_redisplay_function;
      tui_old_rl_deprep_terminal = rl_deprep_term_function;
      tui_old_rl_prep_terminal = rl_prep_term_function;
      tui_old_rl_getc_function = rl_getc_function;
      tui_old_rl_outstream = rl_outstream;
      tui_old_readline_echoing_p = readline_echoing_p;
      rl_redisplay_function = tui_redisplay_readline;
      rl_deprep_term_function = tui_deprep_terminal;
      rl_prep_term_function = tui_prep_terminal;
      rl_getc_function = tui_getc;
      readline_echoing_p = 0;
      rl_outstream = tui_rl_outstream;
      rl_prompt = 0;
      rl_completion_display_matches_hook = tui_rl_display_match_list;
      rl_already_prompted = 0;

      /* Keep track of previous gdb output.  */
      tui_old_stdout = gdb_stdout;
      tui_old_stderr = gdb_stderr;
      tui_old_uiout = uiout;

      /* Reconfigure gdb output.  */
      gdb_stdout = tui_stdout;
      gdb_stderr = tui_stderr;
      gdb_stdlog = gdb_stdout;	/* for moment */
      gdb_stdtarg = gdb_stderr;	/* for moment */
      uiout = tui_out;

      /* Save tty for SIGCONT.  */
      savetty ();
    }
  else
    {
      /* Restore gdb output.  */
      gdb_stdout = tui_old_stdout;
      gdb_stderr = tui_old_stderr;
      gdb_stdlog = gdb_stdout;	/* for moment */
      gdb_stdtarg = gdb_stderr;	/* for moment */
      uiout = tui_old_uiout;

      /* Restore readline.  */
      rl_redisplay_function = tui_old_rl_redisplay_function;
      rl_deprep_term_function = tui_old_rl_deprep_terminal;
      rl_prep_term_function = tui_old_rl_prep_terminal;
      rl_getc_function = tui_old_rl_getc_function;
      rl_outstream = tui_old_rl_outstream;
      rl_completion_display_matches_hook = 0;
      readline_echoing_p = tui_old_readline_echoing_p;
      rl_already_prompted = 0;

      /* Save tty for SIGCONT.  */
      savetty ();
    }
}

#ifdef SIGCONT
/* Catch SIGCONT to restore the terminal and refresh the screen.  */
static void
tui_cont_sig (int sig)
{
  if (tui_active)
    {
      /* Restore the terminal setting because another process (shell)
         might have changed it.  */
      resetty ();

      /* Force a refresh of the screen.  */
      tui_refresh_all_win ();

      /* Update cursor position on the screen.  */
      wmove (TUI_CMD_WIN->generic.handle,
             TUI_CMD_WIN->detail.command_info.start_line,
             TUI_CMD_WIN->detail.command_info.curch);
      wrefresh (TUI_CMD_WIN->generic.handle);
    }
  signal (sig, tui_cont_sig);
}
#endif

/* Initialize the IO for gdb in curses mode.  */
void
tui_initialize_io (void)
{
#ifdef SIGCONT
  signal (SIGCONT, tui_cont_sig);
#endif

  /* Create tui output streams.  */
  tui_stdout = tui_fileopen (stdout);
  tui_stderr = tui_fileopen (stderr);
  tui_out = tui_out_new (tui_stdout);

  /* Create the default UI.  It is not created because we installed
     a init_ui_hook.  */
  tui_old_uiout = uiout = cli_out_new (gdb_stdout);

#ifdef TUI_USE_PIPE_FOR_READLINE
  /* Temporary solution for readline writing to stdout:
     redirect readline output in a pipe, read that pipe and
     output the content in the curses command window.  */
  if (pipe (tui_readline_pipe) != 0)
    {
      fprintf_unfiltered (gdb_stderr, "Cannot create pipe for readline");
      exit (1);
    }
  tui_rl_outstream = fdopen (tui_readline_pipe[1], "w");
  if (tui_rl_outstream == 0)
    {
      fprintf_unfiltered (gdb_stderr, "Cannot redirect readline output");
      exit (1);
    }
  setvbuf (tui_rl_outstream, (char*) NULL, _IOLBF, 0);

#ifdef O_NONBLOCK
  (void) fcntl (tui_readline_pipe[0], F_SETFL, O_NONBLOCK);
#else
#ifdef O_NDELAY
  (void) fcntl (tui_readline_pipe[0], F_SETFL, O_NDELAY);
#endif
#endif
  add_file_handler (tui_readline_pipe[0], tui_readline_output, 0);
#else
  tui_rl_outstream = stdout;
#endif
}

/* Get a character from the command window.  This is called from the readline
   package.  */
int
tui_getc (FILE *fp)
{
  int ch;
  WINDOW *w;

  w = TUI_CMD_WIN->generic.handle;

#ifdef TUI_USE_PIPE_FOR_READLINE
  /* Flush readline output.  */
  tui_readline_output (GDB_READABLE, 0);
#endif

  ch = wgetch (w);
  ch = tui_handle_resize_during_io (ch);

  /* The \n must be echoed because it will not be printed by readline.  */
  if (ch == '\n')
    {
      /* When hitting return with an empty input, gdb executes the last
         command.  If we emit a newline, this fills up the command window
         with empty lines with gdb prompt at beginning.  Instead of that,
         stay on the same line but provide a visual effect to show the
         user we recognized the command.  */
      if (rl_end == 0)
        {
          wmove (w, TUI_CMD_WIN->detail.command_info.cur_line, 0);

          /* Clear the line.  This will blink the gdb prompt since
             it will be redrawn at the same line.  */
          wclrtoeol (w);
          wrefresh (w);
          napms (20);
        }
      else
        {
          wmove (w, TUI_CMD_WIN->detail.command_info.cur_line,
                 TUI_CMD_WIN->detail.command_info.curch);
          waddch (w, ch);
        }
    }
  
  if (key_is_command_char (ch))
    {				/* Handle prev/next/up/down here */
      ch = tui_dispatch_ctrl_char (ch);
    }
  
  if (ch == '\n' || ch == '\r' || ch == '\f')
    TUI_CMD_WIN->detail.command_info.curch = 0;
  if (ch == KEY_BACKSPACE)
    return '\b';
  
  return ch;
}


/* Cleanup when a resize has occured.
   Returns the character that must be processed.  */
static unsigned int
tui_handle_resize_during_io (unsigned int original_ch)
{
  if (tui_win_resized ())
    {
      tui_refresh_all_win ();
      dont_repeat ();
      tui_set_win_resized_to (FALSE);
      return '\n';
    }
  else
    return original_ch;
}
