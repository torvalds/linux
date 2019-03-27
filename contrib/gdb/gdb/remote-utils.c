/* Generic support for remote debugging interfaces.

   Copyright 1993, 1994, 1995, 1996, 1998, 2000, 2001
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

/*  This file actually contains two distinct logical "packages".  They
   are packaged together in this one file because they are typically
   used together.

   The first package is an addition to the serial package.  The
   addition provides reading and writing with debugging output and
   timeouts based on user settable variables.  These routines are
   intended to support serial port based remote backends.  These
   functions are prefixed with sr_.

   The second package is a collection of more or less generic
   functions for use by remote backends.  They support user settable
   variables for debugging, retries, and the like.  

   Todo:

   * a pass through mode a la kermit or telnet.
   * autobaud.
   * ask remote to change his baud rate.
 */

#include <ctype.h>

#include "defs.h"
#include "gdb_string.h"
#include "gdbcmd.h"
#include "target.h"
#include "serial.h"
#include "gdbcore.h"		/* for exec_bfd */
#include "inferior.h"		/* for generic_mourn_inferior */
#include "remote-utils.h"
#include "regcache.h"


void _initialize_sr_support (void);

struct _sr_settings sr_settings =
{
  4,				/* timeout:
				   remote-hms.c had 2
				   remote-bug.c had "with a timeout of 2, we time out waiting for
				   the prompt after an s-record dump."

				   remote.c had (2): This was 5 seconds, which is a long time to
				   sit and wait. Unless this is going though some terminal server
				   or multiplexer or other form of hairy serial connection, I
				   would think 2 seconds would be plenty.
				 */

  10,				/* retries */
  NULL,				/* device */
  NULL,				/* descriptor */
};

struct gr_settings *gr_settings = NULL;

static void usage (char *, char *);
static void sr_com (char *, int);

static void
usage (char *proto, char *junk)
{
  if (junk != NULL)
    fprintf_unfiltered (gdb_stderr, "Unrecognized arguments: `%s'.\n", junk);

  error ("Usage: target %s [DEVICE [SPEED [DEBUG]]]\n\
where DEVICE is the name of a device or HOST:PORT", proto);

  return;
}

#define CHECKDONE(p, q) \
{ \
  if (q == p) \
    { \
      if (*p == '\0') \
	return; \
      else \
	usage(proto, p); \
    } \
}

void
sr_scan_args (char *proto, char *args)
{
  int n;
  char *p, *q;

  /* if no args, then nothing to do. */
  if (args == NULL || *args == '\0')
    return;

  /* scan off white space.  */
  for (p = args; isspace (*p); ++p);;

  /* find end of device name.  */
  for (q = p; *q != '\0' && !isspace (*q); ++q);;

  /* check for missing or empty device name.  */
  CHECKDONE (p, q);
  sr_set_device (savestring (p, q - p));

  /* look for baud rate.  */
  n = strtol (q, &p, 10);

  /* check for missing or empty baud rate.  */
  CHECKDONE (p, q);
  baud_rate = n;

  /* look for debug value.  */
  n = strtol (p, &q, 10);

  /* check for missing or empty debug value.  */
  CHECKDONE (p, q);
  sr_set_debug (n);

  /* scan off remaining white space.  */
  for (p = q; isspace (*p); ++p);;

  /* if not end of string, then there's unrecognized junk. */
  if (*p != '\0')
    usage (proto, p);

  return;
}

void
gr_generic_checkin (void)
{
  sr_write_cr ("");
  gr_expect_prompt ();
}

void
gr_open (char *args, int from_tty, struct gr_settings *gr)
{
  target_preopen (from_tty);
  sr_scan_args (gr->ops->to_shortname, args);
  unpush_target (gr->ops);

  gr_settings = gr;

  if (sr_get_desc () != NULL)
    gr_close (0);

  /* If no args are specified, then we use the device specified by a
     previous command or "set remotedevice".  But if there is no
     device, better stop now, not dump core.  */

  if (sr_get_device () == NULL)
    usage (gr->ops->to_shortname, NULL);

  sr_set_desc (serial_open (sr_get_device ()));
  if (!sr_get_desc ())
    perror_with_name ((char *) sr_get_device ());

  if (baud_rate != -1)
    {
      if (serial_setbaudrate (sr_get_desc (), baud_rate) != 0)
	{
	  serial_close (sr_get_desc ());
	  perror_with_name (sr_get_device ());
	}
    }

  serial_raw (sr_get_desc ());

  /* If there is something sitting in the buffer we might take it as a
     response to a command, which would be bad.  */
  serial_flush_input (sr_get_desc ());

  /* default retries */
  if (sr_get_retries () == 0)
    sr_set_retries (1);

  /* default clear breakpoint function */
  if (gr_settings->clear_all_breakpoints == NULL)
    gr_settings->clear_all_breakpoints = remove_breakpoints;

  if (from_tty)
    {
      printf_filtered ("Remote debugging using `%s'", sr_get_device ());
      if (baud_rate != -1)
	printf_filtered (" at baud rate of %d",
			 baud_rate);
      printf_filtered ("\n");
    }

  push_target (gr->ops);
  gr_checkin ();
  gr_clear_all_breakpoints ();
  return;
}

/* Read a character from the remote system masking it down to 7 bits
   and doing all the fancy timeout stuff.  */

int
sr_readchar (void)
{
  int buf;

  buf = serial_readchar (sr_get_desc (), sr_get_timeout ());

  if (buf == SERIAL_TIMEOUT)
    error ("Timeout reading from remote system.");

  if (sr_get_debug () > 0)
    printf_unfiltered ("%c", buf);

  return buf & 0x7f;
}

int
sr_pollchar (void)
{
  int buf;

  buf = serial_readchar (sr_get_desc (), 0);
  if (buf == SERIAL_TIMEOUT)
    buf = 0;
  if (sr_get_debug () > 0)
    {
      if (buf)
	printf_unfiltered ("%c", buf);
      else
	printf_unfiltered ("<empty character poll>");
    }

  return buf & 0x7f;
}

/* Keep discarding input from the remote system, until STRING is found.
   Let the user break out immediately.  */
void
sr_expect (char *string)
{
  char *p = string;

  immediate_quit++;
  while (1)
    {
      if (sr_readchar () == *p)
	{
	  p++;
	  if (*p == '\0')
	    {
	      immediate_quit--;
	      return;
	    }
	}
      else
	p = string;
    }
}

void
sr_write (char *a, int l)
{
  int i;

  if (serial_write (sr_get_desc (), a, l) != 0)
    perror_with_name ("sr_write: Error writing to remote");

  if (sr_get_debug () > 0)
    for (i = 0; i < l; i++)
      printf_unfiltered ("%c", a[i]);

  return;
}

void
sr_write_cr (char *s)
{
  sr_write (s, strlen (s));
  sr_write ("\r", 1);
  return;
}

int
sr_timed_read (char *buf, int n)
{
  int i;
  char c;

  i = 0;
  while (i < n)
    {
      c = sr_readchar ();

      if (c == 0)
	return i;
      buf[i] = c;
      i++;

    }
  return i;
}

/* Get a hex digit from the remote system & return its value. If
   ignore_space is nonzero, ignore spaces (not newline, tab, etc).  */

int
sr_get_hex_digit (int ignore_space)
{
  int ch;

  while (1)
    {
      ch = sr_readchar ();
      if (ch >= '0' && ch <= '9')
	return ch - '0';
      else if (ch >= 'A' && ch <= 'F')
	return ch - 'A' + 10;
      else if (ch >= 'a' && ch <= 'f')
	return ch - 'a' + 10;
      else if (ch != ' ' || !ignore_space)
	{
	  gr_expect_prompt ();
	  error ("Invalid hex digit from remote system.");
	}
    }
}

/* Get a byte from the remote and put it in *BYT.  Accept any number
   leading spaces.  */
void
sr_get_hex_byte (char *byt)
{
  int val;

  val = sr_get_hex_digit (1) << 4;
  val |= sr_get_hex_digit (0);
  *byt = val;
}

/* Read a 32-bit hex word from the remote, preceded by a space  */
long
sr_get_hex_word (void)
{
  long val;
  int j;

  val = 0;
  for (j = 0; j < 8; j++)
    val = (val << 4) + sr_get_hex_digit (j == 0);
  return val;
}

/* Put a command string, in args, out to the remote.  The remote is assumed to
   be in raw mode, all writing/reading done through desc.
   Ouput from the remote is placed on the users terminal until the
   prompt from the remote is seen.
   FIXME: Can't handle commands that take input.  */

static void
sr_com (char *args, int fromtty)
{
  sr_check_open ();

  if (!args)
    return;

  /* Clear all input so only command relative output is displayed */

  sr_write_cr (args);
  sr_write ("\030", 1);
  registers_changed ();
  gr_expect_prompt ();
}

void
gr_close (int quitting)
{
  gr_clear_all_breakpoints ();

  if (sr_is_open ())
    {
      serial_close (sr_get_desc ());
      sr_set_desc (NULL);
    }

  return;
}

/* gr_detach()
   takes a program previously attached to and detaches it.
   We better not have left any breakpoints
   in the program or it'll die when it hits one.
   Close the open connection to the remote debugger.
   Use this when you want to detach and do something else
   with your gdb.  */

void
gr_detach (char *args, int from_tty)
{
  if (args)
    error ("Argument given to \"detach\" when remotely debugging.");

  if (sr_is_open ())
    gr_clear_all_breakpoints ();

  pop_target ();
  if (from_tty)
    puts_filtered ("Ending remote debugging.\n");

  return;
}

void
gr_files_info (struct target_ops *ops)
{
#ifdef __GO32__
  printf_filtered ("\tAttached to DOS asynctsr\n");
#else
  printf_filtered ("\tAttached to %s", sr_get_device ());
  if (baud_rate != -1)
    printf_filtered ("at %d baud", baud_rate);
  printf_filtered ("\n");
#endif

  if (exec_bfd)
    {
      printf_filtered ("\tand running program %s\n",
		       bfd_get_filename (exec_bfd));
    }
  printf_filtered ("\tusing the %s protocol.\n", ops->to_shortname);
}

void
gr_mourn (void)
{
  gr_clear_all_breakpoints ();
  unpush_target (gr_get_ops ());
  generic_mourn_inferior ();
}

void
gr_kill (void)
{
  return;
}

/* This is called not only when we first attach, but also when the
   user types "run" after having attached.  */
void
gr_create_inferior (char *execfile, char *args, char **env)
{
  int entry_pt;

  if (args && *args)
    error ("Can't pass arguments to remote process.");

  if (execfile == 0 || exec_bfd == 0)
    error ("No executable file specified");

  entry_pt = (int) bfd_get_start_address (exec_bfd);
  sr_check_open ();

  gr_kill ();
  gr_clear_all_breakpoints ();

  init_wait_for_inferior ();
  gr_checkin ();

  insert_breakpoints ();	/* Needed to get correct instruction in cache */
  proceed (entry_pt, -1, 0);
}

/* Given a null terminated list of strings LIST, read the input until we find one of
   them.  Return the index of the string found or -1 on error.  '?' means match
   any single character. Note that with the algorithm we use, the initial
   character of the string cannot recur in the string, or we will not find some
   cases of the string in the input.  If PASSTHROUGH is non-zero, then
   pass non-matching data on.  */

int
gr_multi_scan (char *list[], int passthrough)
{
  char *swallowed = NULL;	/* holding area */
  char *swallowed_p = swallowed;	/* Current position in swallowed.  */
  int ch;
  int ch_handled;
  int i;
  int string_count;
  int max_length;
  char **plist;

  /* Look through the strings.  Count them.  Find the largest one so we can
     allocate a holding area.  */

  for (max_length = string_count = i = 0;
       list[i] != NULL;
       ++i, ++string_count)
    {
      int length = strlen (list[i]);

      if (length > max_length)
	max_length = length;
    }

  /* if we have no strings, then something is wrong. */
  if (string_count == 0)
    return (-1);

  /* otherwise, we will need a holding area big enough to hold almost two
     copies of our largest string.  */
  swallowed_p = swallowed = alloca (max_length << 1);

  /* and a list of pointers to current scan points. */
  plist = (char **) alloca (string_count * sizeof (*plist));

  /* and initialize */
  for (i = 0; i < string_count; ++i)
    plist[i] = list[i];

  for (ch = sr_readchar (); /* loop forever */ ; ch = sr_readchar ())
    {
      QUIT;			/* Let user quit and leave process running */
      ch_handled = 0;

      for (i = 0; i < string_count; ++i)
	{
	  if (ch == *plist[i] || *plist[i] == '?')
	    {
	      ++plist[i];
	      if (*plist[i] == '\0')
		return (i);

	      if (!ch_handled)
		*swallowed_p++ = ch;

	      ch_handled = 1;
	    }
	  else
	    plist[i] = list[i];
	}

      if (!ch_handled)
	{
	  char *p;

	  /* Print out any characters which have been swallowed.  */
	  if (passthrough)
	    {
	      for (p = swallowed; p < swallowed_p; ++p)
		fputc_unfiltered (*p, gdb_stdout);

	      fputc_unfiltered (ch, gdb_stdout);
	    }

	  swallowed_p = swallowed;
	}
    }
#if 0
  /* Never reached.  */
  return (-1);
#endif
}

/* Get ready to modify the registers array.  On machines which store
   individual registers, this doesn't need to do anything.  On machines
   which store all the registers in one fell swoop, this makes sure
   that registers contains all the registers from the program being
   debugged.  */

void
gr_prepare_to_store (void)
{
  /* Do nothing, since we assume we can store individual regs */
}

void
_initialize_sr_support (void)
{
/* FIXME-now: if target is open... */
  add_show_from_set (add_set_cmd ("remotedevice", no_class,
				  var_filename, (char *) &sr_settings.device,
				  "Set device for remote serial I/O.\n\
This device is used as the serial port when debugging using remote\n\
targets.", &setlist),
		     &showlist);

  add_com ("remote <command>", class_obscure, sr_com,
	   "Send a command to the remote monitor.");

}
