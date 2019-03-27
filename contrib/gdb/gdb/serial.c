/* Generic serial interface routines

   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002 Free Software Foundation, Inc.

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
#include <ctype.h>
#include "serial.h"
#include "gdb_string.h"
#include "gdbcmd.h"

extern void _initialize_serial (void);

/* Is serial being debugged? */

static int global_serial_debug_p;

/* Linked list of serial I/O handlers */

static struct serial_ops *serial_ops_list = NULL;

/* This is the last serial stream opened.  Used by connect command. */

static struct serial *last_serial_opened = NULL;

/* Pointer to list of scb's. */

static struct serial *scb_base;

/* Non-NULL gives filename which contains a recording of the remote session,
   suitable for playback by gdbserver. */

static char *serial_logfile = NULL;
static struct ui_file *serial_logfp = NULL;

static struct serial_ops *serial_interface_lookup (char *);
static void serial_logchar (struct ui_file *stream, int ch_type, int ch, int timeout);
static const char logbase_hex[] = "hex";
static const char logbase_octal[] = "octal";
static const char logbase_ascii[] = "ascii";
static const char *logbase_enums[] =
{logbase_hex, logbase_octal, logbase_ascii, NULL};
static const char *serial_logbase = logbase_ascii;


static int serial_current_type = 0;

/* Log char CH of type CHTYPE, with TIMEOUT */

/* Define bogus char to represent a BREAK.  Should be careful to choose a value
   that can't be confused with a normal char, or an error code.  */
#define SERIAL_BREAK 1235

static void
serial_logchar (struct ui_file *stream, int ch_type, int ch, int timeout)
{
  if (ch_type != serial_current_type)
    {
      fprintf_unfiltered (stream, "\n%c ", ch_type);
      serial_current_type = ch_type;
    }

  if (serial_logbase != logbase_ascii)
    fputc_unfiltered (' ', stream);

  switch (ch)
    {
    case SERIAL_TIMEOUT:
      fprintf_unfiltered (stream, "<Timeout: %d seconds>", timeout);
      return;
    case SERIAL_ERROR:
      fprintf_unfiltered (stream, "<Error: %s>", safe_strerror (errno));
      return;
    case SERIAL_EOF:
      fputs_unfiltered ("<Eof>", stream);
      return;
    case SERIAL_BREAK:
      fputs_unfiltered ("<Break>", stream);
      return;
    default:
      if (serial_logbase == logbase_hex)
	fprintf_unfiltered (stream, "%02x", ch & 0xff);
      else if (serial_logbase == logbase_octal)
	fprintf_unfiltered (stream, "%03o", ch & 0xff);
      else
	switch (ch)
	  {
	  case '\\':
	    fputs_unfiltered ("\\\\", stream);
	    break;
	  case '\b':
	    fputs_unfiltered ("\\b", stream);
	    break;
	  case '\f':
	    fputs_unfiltered ("\\f", stream);
	    break;
	  case '\n':
	    fputs_unfiltered ("\\n", stream);
	    break;
	  case '\r':
	    fputs_unfiltered ("\\r", stream);
	    break;
	  case '\t':
	    fputs_unfiltered ("\\t", stream);
	    break;
	  case '\v':
	    fputs_unfiltered ("\\v", stream);
	    break;
	  default:
	    fprintf_unfiltered (stream, isprint (ch) ? "%c" : "\\x%02x", ch & 0xFF);
	    break;
	  }
    }
}

void
serial_log_command (const char *cmd)
{
  if (!serial_logfp)
    return;

  serial_current_type = 'c';

  fputs_unfiltered ("\nc ", serial_logfp);
  fputs_unfiltered (cmd, serial_logfp);

  /* Make sure that the log file is as up-to-date as possible,
     in case we are getting ready to dump core or something. */
  gdb_flush (serial_logfp);
}


static struct serial_ops *
serial_interface_lookup (char *name)
{
  struct serial_ops *ops;

  for (ops = serial_ops_list; ops; ops = ops->next)
    if (strcmp (name, ops->name) == 0)
      return ops;

  return NULL;
}

void
serial_add_interface (struct serial_ops *optable)
{
  optable->next = serial_ops_list;
  serial_ops_list = optable;
}

/* Open up a device or a network socket, depending upon the syntax of NAME. */

struct serial *
serial_open (const char *name)
{
  struct serial *scb;
  struct serial_ops *ops;
  const char *open_name = name;

  for (scb = scb_base; scb; scb = scb->next)
    if (scb->name && strcmp (scb->name, name) == 0)
      {
	scb->refcnt++;
	return scb;
      }

  if (strcmp (name, "pc") == 0)
    ops = serial_interface_lookup ("pc");
  else if (strchr (name, ':'))
    ops = serial_interface_lookup ("tcp");
  else if (strncmp (name, "lpt", 3) == 0)
    ops = serial_interface_lookup ("parallel");
  else if (strncmp (name, "|", 1) == 0)
    {
      ops = serial_interface_lookup ("pipe");
      open_name = name + 1; /* discard ``|'' */
    }
  else
    ops = serial_interface_lookup ("hardwire");

  if (!ops)
    return NULL;

  scb = XMALLOC (struct serial);

  scb->ops = ops;

  scb->bufcnt = 0;
  scb->bufp = scb->buf;

  if (scb->ops->open (scb, open_name))
    {
      xfree (scb);
      return NULL;
    }

  scb->name = xstrdup (name);
  scb->next = scb_base;
  scb->refcnt = 1;
  scb->debug_p = 0;
  scb->async_state = 0;
  scb->async_handler = NULL;
  scb->async_context = NULL;
  scb_base = scb;

  last_serial_opened = scb;

  if (serial_logfile != NULL)
    {
      serial_logfp = gdb_fopen (serial_logfile, "w");
      if (serial_logfp == NULL)
	perror_with_name (serial_logfile);
    }

  return scb;
}

struct serial *
serial_fdopen (const int fd)
{
  struct serial *scb;
  struct serial_ops *ops;

  for (scb = scb_base; scb; scb = scb->next)
    if (scb->fd == fd)
      {
	scb->refcnt++;
	return scb;
      }

  ops = serial_interface_lookup ("hardwire");

  if (!ops)
    return NULL;

  scb = XMALLOC (struct serial);

  scb->ops = ops;

  scb->bufcnt = 0;
  scb->bufp = scb->buf;

  scb->fd = fd;

  scb->name = NULL;
  scb->next = scb_base;
  scb->refcnt = 1;
  scb->debug_p = 0;
  scb->async_state = 0;
  scb->async_handler = NULL;
  scb->async_context = NULL;
  scb_base = scb;

  last_serial_opened = scb;

  return scb;
}

static void
do_serial_close (struct serial *scb, int really_close)
{
  struct serial *tmp_scb;

  last_serial_opened = NULL;

  if (serial_logfp)
    {
      fputs_unfiltered ("\nEnd of log\n", serial_logfp);
      serial_current_type = 0;

      /* XXX - What if serial_logfp == gdb_stdout or gdb_stderr? */
      ui_file_delete (serial_logfp);
      serial_logfp = NULL;
    }

/* This is bogus.  It's not our fault if you pass us a bad scb...!  Rob, you
   should fix your code instead.  */

  if (!scb)
    return;

  scb->refcnt--;
  if (scb->refcnt > 0)
    return;

  /* ensure that the FD has been taken out of async mode */
  if (scb->async_handler != NULL)
    serial_async (scb, NULL, NULL);

  if (really_close)
    scb->ops->close (scb);

  if (scb->name)
    xfree (scb->name);

  if (scb_base == scb)
    scb_base = scb_base->next;
  else
    for (tmp_scb = scb_base; tmp_scb; tmp_scb = tmp_scb->next)
      {
	if (tmp_scb->next != scb)
	  continue;

	tmp_scb->next = tmp_scb->next->next;
	break;
      }

  xfree (scb);
}

void
serial_close (struct serial *scb)
{
  do_serial_close (scb, 1);
}

void
serial_un_fdopen (struct serial *scb)
{
  do_serial_close (scb, 0);
}

int
serial_readchar (struct serial *scb, int timeout)
{
  int ch;

  /* FIXME: cagney/1999-10-11: Don't enable this check until the ASYNC
     code is finished. */
  if (0 && serial_is_async_p (scb) && timeout < 0)
    internal_error (__FILE__, __LINE__,
		    "serial_readchar: blocking read in async mode");

  ch = scb->ops->readchar (scb, timeout);
  if (serial_logfp != NULL)
    {
      serial_logchar (serial_logfp, 'r', ch, timeout);

      /* Make sure that the log file is as up-to-date as possible,
         in case we are getting ready to dump core or something. */
      gdb_flush (serial_logfp);
    }
  if (serial_debug_p (scb))
    {
      fprintf_unfiltered (gdb_stdlog, "[");
      serial_logchar (gdb_stdlog, 'r', ch, timeout);
      fprintf_unfiltered (gdb_stdlog, "]");
      gdb_flush (gdb_stdlog);
    }

  return (ch);
}

int
serial_write (struct serial *scb, const char *str, int len)
{
  if (serial_logfp != NULL)
    {
      int count;

      for (count = 0; count < len; count++)
	serial_logchar (serial_logfp, 'w', str[count] & 0xff, 0);

      /* Make sure that the log file is as up-to-date as possible,
         in case we are getting ready to dump core or something. */
      gdb_flush (serial_logfp);
    }

  return (scb->ops->write (scb, str, len));
}

void
serial_printf (struct serial *desc, const char *format,...)
{
  va_list args;
  char *buf;
  va_start (args, format);

  xvasprintf (&buf, format, args);
  serial_write (desc, buf, strlen (buf));

  xfree (buf);
  va_end (args);
}

int
serial_drain_output (struct serial *scb)
{
  return scb->ops->drain_output (scb);
}

int
serial_flush_output (struct serial *scb)
{
  return scb->ops->flush_output (scb);
}

int
serial_flush_input (struct serial *scb)
{
  return scb->ops->flush_input (scb);
}

int
serial_send_break (struct serial *scb)
{
  if (serial_logfp != NULL)
    serial_logchar (serial_logfp, 'w', SERIAL_BREAK, 0);

  return (scb->ops->send_break (scb));
}

void
serial_raw (struct serial *scb)
{
  scb->ops->go_raw (scb);
}

serial_ttystate
serial_get_tty_state (struct serial *scb)
{
  return scb->ops->get_tty_state (scb);
}

int
serial_set_tty_state (struct serial *scb, serial_ttystate ttystate)
{
  return scb->ops->set_tty_state (scb, ttystate);
}

void
serial_print_tty_state (struct serial *scb,
			serial_ttystate ttystate,
			struct ui_file *stream)
{
  scb->ops->print_tty_state (scb, ttystate, stream);
}

int
serial_noflush_set_tty_state (struct serial *scb,
			      serial_ttystate new_ttystate,
			      serial_ttystate old_ttystate)
{
  return scb->ops->noflush_set_tty_state (scb, new_ttystate, old_ttystate);
}

int
serial_setbaudrate (struct serial *scb, int rate)
{
  return scb->ops->setbaudrate (scb, rate);
}

int
serial_setstopbits (struct serial *scb, int num)
{
  return scb->ops->setstopbits (scb, num);
}

int
serial_can_async_p (struct serial *scb)
{
  return (scb->ops->async != NULL);
}

int
serial_is_async_p (struct serial *scb)
{
  return (scb->ops->async != NULL) && (scb->async_handler != NULL);
}

void
serial_async (struct serial *scb,
	      serial_event_ftype *handler,
	      void *context)
{
  /* Only change mode if there is a need. */
  if ((scb->async_handler == NULL)
      != (handler == NULL))
    scb->ops->async (scb, handler != NULL);
  scb->async_handler = handler;
  scb->async_context = context;
}

int
deprecated_serial_fd (struct serial *scb)
{
  /* FIXME: should this output a warning that deprecated code is being
     called? */
  if (scb->fd < 0)
    {
      internal_error (__FILE__, __LINE__,
		      "serial: FD not valid");
    }
  return scb->fd; /* sigh */
}

void
serial_debug (struct serial *scb, int debug_p)
{
  scb->debug_p = debug_p;
}

int
serial_debug_p (struct serial *scb)
{
  return scb->debug_p || global_serial_debug_p;
}


#if 0
/* The connect command is #if 0 because I hadn't thought of an elegant
   way to wait for I/O on two `struct serial *'s simultaneously.  Two
   solutions came to mind:

   1) Fork, and have have one fork handle the to user direction,
   and have the other hand the to target direction.  This
   obviously won't cut it for MSDOS.

   2) Use something like select.  This assumes that stdin and
   the target side can both be waited on via the same
   mechanism.  This may not be true for DOS, if GDB is
   talking to the target via a TCP socket.
   -grossman, 8 Jun 93 */

/* Connect the user directly to the remote system.  This command acts just like
   the 'cu' or 'tip' command.  Use <CR>~. or <CR>~^D to break out.  */

static struct serial *tty_desc;	/* Controlling terminal */

static void
cleanup_tty (serial_ttystate ttystate)
{
  printf_unfiltered ("\r\n[Exiting connect mode]\r\n");
  serial_set_tty_state (tty_desc, ttystate);
  xfree (ttystate);
  serial_close (tty_desc);
}

static void
connect_command (char *args, int fromtty)
{
  int c;
  char cur_esc = 0;
  serial_ttystate ttystate;
  struct serial *port_desc;		/* TTY port */

  dont_repeat ();

  if (args)
    fprintf_unfiltered (gdb_stderr, "This command takes no args.  They have been ignored.\n");

  printf_unfiltered ("[Entering connect mode.  Use ~. or ~^D to escape]\n");

  tty_desc = serial_fdopen (0);
  port_desc = last_serial_opened;

  ttystate = serial_get_tty_state (tty_desc);

  serial_raw (tty_desc);
  serial_raw (port_desc);

  make_cleanup (cleanup_tty, ttystate);

  while (1)
    {
      int mask;

      mask = serial_wait_2 (tty_desc, port_desc, -1);

      if (mask & 2)
	{			/* tty input */
	  char cx;

	  while (1)
	    {
	      c = serial_readchar (tty_desc, 0);

	      if (c == SERIAL_TIMEOUT)
		break;

	      if (c < 0)
		perror_with_name ("connect");

	      cx = c;
	      serial_write (port_desc, &cx, 1);

	      switch (cur_esc)
		{
		case 0:
		  if (c == '\r')
		    cur_esc = c;
		  break;
		case '\r':
		  if (c == '~')
		    cur_esc = c;
		  else
		    cur_esc = 0;
		  break;
		case '~':
		  if (c == '.' || c == '\004')
		    return;
		  else
		    cur_esc = 0;
		}
	    }
	}

      if (mask & 1)
	{			/* Port input */
	  char cx;

	  while (1)
	    {
	      c = serial_readchar (port_desc, 0);

	      if (c == SERIAL_TIMEOUT)
		break;

	      if (c < 0)
		perror_with_name ("connect");

	      cx = c;

	      serial_write (tty_desc, &cx, 1);
	    }
	}
    }
}
#endif /* 0 */

/* Serial set/show framework.  */

static struct cmd_list_element *serial_set_cmdlist;
static struct cmd_list_element *serial_show_cmdlist;

static void
serial_set_cmd (char *args, int from_tty)
{
  printf_unfiltered ("\"set serial\" must be followed by the name of a command.\n");
  help_list (serial_set_cmdlist, "set serial ", -1, gdb_stdout);
}

static void
serial_show_cmd (char *args, int from_tty)
{
  cmd_show_list (serial_show_cmdlist, from_tty, "");
}


void
_initialize_serial (void)
{
#if 0
  add_com ("connect", class_obscure, connect_command,
	   "Connect the terminal directly up to the command monitor.\n\
Use <CR>~. or <CR>~^D to break out.");
#endif /* 0 */

  add_prefix_cmd ("serial", class_maintenance, serial_set_cmd, "\
Set default serial/parallel port configuration.",
		  &serial_set_cmdlist, "set serial ",
		  0/*allow-unknown*/,
		  &setlist);

  add_prefix_cmd ("serial", class_maintenance, serial_show_cmd, "\
Show default serial/parallel port configuration.",
		  &serial_show_cmdlist, "show serial ",
		  0/*allow-unknown*/,
		  &showlist);

  add_show_from_set
    (add_set_cmd ("remotelogfile", no_class,
		  var_filename, (char *) &serial_logfile,
		  "Set filename for remote session recording.\n\
This file is used to record the remote session for future playback\n\
by gdbserver.",
		  &setlist),
     &showlist);

  add_show_from_set
    (add_set_enum_cmd ("remotelogbase", no_class,
		       logbase_enums, &serial_logbase,
		       "Set numerical base for remote session logging",
		       &setlist),
     &showlist);

  add_show_from_set (add_set_cmd ("serial",
				  class_maintenance,
				  var_zinteger,
				  (char *)&global_serial_debug_p,
				  "Set serial debugging.\n\
When non-zero, serial port debugging is enabled.", &setdebuglist),
		     &showdebuglist);
}
