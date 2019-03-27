/* Remote debugging interface for boot monitors, for GDB.

   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002 Free Software Foundation, Inc.

   Contributed by Cygnus Support.  Written by Rob Savoye for Cygnus.
   Resurrected from the ashes by Stu Grossman.

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

/* This file was derived from various remote-* modules. It is a collection
   of generic support functions so GDB can talk directly to a ROM based
   monitor. This saves use from having to hack an exception based handler
   into existence, and makes for quick porting.

   This module talks to a debug monitor called 'MONITOR', which
   We communicate with MONITOR via either a direct serial line, or a TCP
   (or possibly TELNET) stream to a terminal multiplexor,
   which in turn talks to the target board.  */

/* FIXME 32x64: This code assumes that registers and addresses are at
   most 32 bits long.  If they can be larger, you will need to declare
   values as LONGEST and use %llx or some such to print values when
   building commands to send to the monitor.  Since we don't know of
   any actual 64-bit targets with ROM monitors that use this code,
   it's not an issue right now.  -sts 4/18/96  */

#include "defs.h"
#include "gdbcore.h"
#include "target.h"
#include <signal.h>
#include <ctype.h>
#include "gdb_string.h"
#include <sys/types.h>
#include "command.h"
#include "serial.h"
#include "monitor.h"
#include "gdbcmd.h"
#include "inferior.h"
#include "gdb_regex.h"
#include "srec.h"
#include "regcache.h"

static char *dev_name;
static struct target_ops *targ_ops;

static void monitor_vsprintf (char *sndbuf, char *pattern, va_list args);

static int readchar (int timeout);

static void monitor_fetch_register (int regno);
static void monitor_store_register (int regno);

static void monitor_printable_string (char *newstr, char *oldstr, int len);
static void monitor_error (char *function, char *message, CORE_ADDR memaddr, int len, char *string, int final_char);
static void monitor_detach (char *args, int from_tty);
static void monitor_resume (ptid_t ptid, int step, enum target_signal sig);
static void monitor_interrupt (int signo);
static void monitor_interrupt_twice (int signo);
static void monitor_interrupt_query (void);
static void monitor_wait_cleanup (void *old_timeout);

static ptid_t monitor_wait (ptid_t ptid, struct target_waitstatus *status);
static void monitor_fetch_registers (int regno);
static void monitor_store_registers (int regno);
static void monitor_prepare_to_store (void);
static int monitor_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len,
				int write, 
				struct mem_attrib *attrib,
				struct target_ops *target);
static void monitor_files_info (struct target_ops *ops);
static int monitor_insert_breakpoint (CORE_ADDR addr, char *shadow);
static int monitor_remove_breakpoint (CORE_ADDR addr, char *shadow);
static void monitor_kill (void);
static void monitor_load (char *file, int from_tty);
static void monitor_mourn_inferior (void);
static void monitor_stop (void);

static int monitor_read_memory (CORE_ADDR addr, char *myaddr, int len);
static int monitor_write_memory (CORE_ADDR addr, char *myaddr, int len);
static int monitor_write_memory_bytes (CORE_ADDR addr, char *myaddr, int len);
static int monitor_write_memory_block (CORE_ADDR memaddr,
				       char *myaddr, int len);
static int monitor_expect_regexp (struct re_pattern_buffer *pat,
				  char *buf, int buflen);
static void monitor_dump_regs (void);
#if 0
static int from_hex (int a);
static unsigned long get_hex_word (void);
#endif
static void parse_register_dump (char *, int);

static struct monitor_ops *current_monitor;

static int hashmark;		/* flag set by "set hash" */

static int timeout = 30;

static int in_monitor_wait = 0;	/* Non-zero means we are in monitor_wait() */

static void (*ofunc) ();	/* Old SIGINT signal handler */

static CORE_ADDR *breakaddr;

/* Descriptor for I/O to remote machine.  Initialize it to NULL so
   that monitor_open knows that we don't have a file open when the
   program starts.  */

static struct serial *monitor_desc = NULL;

/* Pointer to regexp pattern matching data */

static struct re_pattern_buffer register_pattern;
static char register_fastmap[256];

static struct re_pattern_buffer getmem_resp_delim_pattern;
static char getmem_resp_delim_fastmap[256];

static struct re_pattern_buffer setmem_resp_delim_pattern;
static char setmem_resp_delim_fastmap[256];

static struct re_pattern_buffer setreg_resp_delim_pattern;
static char setreg_resp_delim_fastmap[256];

static int dump_reg_flag;	/* Non-zero means do a dump_registers cmd when
				   monitor_wait wakes up.  */

static int first_time = 0;	/* is this the first time we're executing after 
				   gaving created the child proccess? */

#define TARGET_BUF_SIZE 2048

/* Monitor specific debugging information.  Typically only useful to
   the developer of a new monitor interface. */

static void monitor_debug (const char *fmt, ...) ATTR_FORMAT(printf, 1, 2);

static int monitor_debug_p = 0;

/* NOTE: This file alternates between monitor_debug_p and remote_debug
   when determining if debug information is printed.  Perhaphs this
   could be simplified. */

static void
monitor_debug (const char *fmt, ...)
{
  if (monitor_debug_p)
    {
      va_list args;
      va_start (args, fmt);
      vfprintf_filtered (gdb_stdlog, fmt, args);
      va_end (args);
    }
}


/* Convert a string into a printable representation, Return # byte in
   the new string.  When LEN is >0 it specifies the size of the
   string.  Otherwize strlen(oldstr) is used. */

static void
monitor_printable_string (char *newstr, char *oldstr, int len)
{
  int ch;
  int i;

  if (len <= 0)
    len = strlen (oldstr);

  for (i = 0; i < len; i++)
    {
      ch = oldstr[i];
      switch (ch)
	{
	default:
	  if (isprint (ch))
	    *newstr++ = ch;

	  else
	    {
	      sprintf (newstr, "\\x%02x", ch & 0xff);
	      newstr += 4;
	    }
	  break;

	case '\\':
	  *newstr++ = '\\';
	  *newstr++ = '\\';
	  break;
	case '\b':
	  *newstr++ = '\\';
	  *newstr++ = 'b';
	  break;
	case '\f':
	  *newstr++ = '\\';
	  *newstr++ = 't';
	  break;
	case '\n':
	  *newstr++ = '\\';
	  *newstr++ = 'n';
	  break;
	case '\r':
	  *newstr++ = '\\';
	  *newstr++ = 'r';
	  break;
	case '\t':
	  *newstr++ = '\\';
	  *newstr++ = 't';
	  break;
	case '\v':
	  *newstr++ = '\\';
	  *newstr++ = 'v';
	  break;
	}
    }

  *newstr++ = '\0';
}

/* Print monitor errors with a string, converting the string to printable
   representation.  */

static void
monitor_error (char *function, char *message,
	       CORE_ADDR memaddr, int len, char *string, int final_char)
{
  int real_len = (len == 0 && string != (char *) 0) ? strlen (string) : len;
  char *safe_string = alloca ((real_len * 4) + 1);
  monitor_printable_string (safe_string, string, real_len);

  if (final_char)
    error ("%s (0x%s): %s: %s%c", function, paddr_nz (memaddr), message, safe_string, final_char);
  else
    error ("%s (0x%s): %s: %s", function, paddr_nz (memaddr), message, safe_string);
}

/* Convert hex digit A to a number.  */

static int
fromhex (int a)
{
  if (a >= '0' && a <= '9')
    return a - '0';
  else if (a >= 'a' && a <= 'f')
    return a - 'a' + 10;
  else if (a >= 'A' && a <= 'F')
    return a - 'A' + 10;
  else
    error ("Invalid hex digit %d", a);
}

/* monitor_vsprintf - similar to vsprintf but handles 64-bit addresses

   This function exists to get around the problem that many host platforms
   don't have a printf that can print 64-bit addresses.  The %A format
   specification is recognized as a special case, and causes the argument
   to be printed as a 64-bit hexadecimal address.

   Only format specifiers of the form "[0-9]*[a-z]" are recognized.
   If it is a '%s' format, the argument is a string; otherwise the
   argument is assumed to be a long integer.

   %% is also turned into a single %.
 */

static void
monitor_vsprintf (char *sndbuf, char *pattern, va_list args)
{
  char format[10];
  char fmt;
  char *p;
  int i;
  long arg_int;
  CORE_ADDR arg_addr;
  char *arg_string;

  for (p = pattern; *p; p++)
    {
      if (*p == '%')
	{
	  /* Copy the format specifier to a separate buffer.  */
	  format[0] = *p++;
	  for (i = 1; *p >= '0' && *p <= '9' && i < (int) sizeof (format) - 2;
	       i++, p++)
	    format[i] = *p;
	  format[i] = fmt = *p;
	  format[i + 1] = '\0';

	  /* Fetch the next argument and print it.  */
	  switch (fmt)
	    {
	    case '%':
	      strcpy (sndbuf, "%");
	      break;
	    case 'A':
	      arg_addr = va_arg (args, CORE_ADDR);
	      strcpy (sndbuf, paddr_nz (arg_addr));
	      break;
	    case 's':
	      arg_string = va_arg (args, char *);
	      sprintf (sndbuf, format, arg_string);
	      break;
	    default:
	      arg_int = va_arg (args, long);
	      sprintf (sndbuf, format, arg_int);
	      break;
	    }
	  sndbuf += strlen (sndbuf);
	}
      else
	*sndbuf++ = *p;
    }
  *sndbuf = '\0';
}


/* monitor_printf_noecho -- Send data to monitor, but don't expect an echo.
   Works just like printf.  */

void
monitor_printf_noecho (char *pattern,...)
{
  va_list args;
  char sndbuf[2000];
  int len;

  va_start (args, pattern);

  monitor_vsprintf (sndbuf, pattern, args);

  len = strlen (sndbuf);
  if (len + 1 > sizeof sndbuf)
    internal_error (__FILE__, __LINE__, "failed internal consistency check");

  if (monitor_debug_p)
    {
      char *safe_string = (char *) alloca ((strlen (sndbuf) * 4) + 1);
      monitor_printable_string (safe_string, sndbuf, 0);
      fprintf_unfiltered (gdb_stdlog, "sent[%s]\n", safe_string);
    }

  monitor_write (sndbuf, len);
}

/* monitor_printf -- Send data to monitor and check the echo.  Works just like
   printf.  */

void
monitor_printf (char *pattern,...)
{
  va_list args;
  char sndbuf[2000];
  int len;

  va_start (args, pattern);

  monitor_vsprintf (sndbuf, pattern, args);

  len = strlen (sndbuf);
  if (len + 1 > sizeof sndbuf)
    internal_error (__FILE__, __LINE__, "failed internal consistency check");

  if (monitor_debug_p)
    {
      char *safe_string = (char *) alloca ((len * 4) + 1);
      monitor_printable_string (safe_string, sndbuf, 0);
      fprintf_unfiltered (gdb_stdlog, "sent[%s]\n", safe_string);
    }

  monitor_write (sndbuf, len);

  /* We used to expect that the next immediate output was the characters we
     just output, but sometimes some extra junk appeared before the characters
     we expected, like an extra prompt, or a portmaster sending telnet negotiations.
     So, just start searching for what we sent, and skip anything unknown.  */
  monitor_debug ("ExpectEcho\n");
  monitor_expect (sndbuf, (char *) 0, 0);
}


/* Write characters to the remote system.  */

void
monitor_write (char *buf, int buflen)
{
  if (serial_write (monitor_desc, buf, buflen))
    fprintf_unfiltered (gdb_stderr, "serial_write failed: %s\n",
			safe_strerror (errno));
}


/* Read a binary character from the remote system, doing all the fancy
   timeout stuff, but without interpreting the character in any way,
   and without printing remote debug information.  */

int
monitor_readchar (void)
{
  int c;
  int looping;

  do
    {
      looping = 0;
      c = serial_readchar (monitor_desc, timeout);

      if (c >= 0)
	c &= 0xff;		/* don't lose bit 7 */
    }
  while (looping);

  if (c >= 0)
    return c;

  if (c == SERIAL_TIMEOUT)
    error ("Timeout reading from remote system.");

  perror_with_name ("remote-monitor");
}


/* Read a character from the remote system, doing all the fancy
   timeout stuff.  */

static int
readchar (int timeout)
{
  int c;
  static enum
    {
      last_random, last_nl, last_cr, last_crnl
    }
  state = last_random;
  int looping;

  do
    {
      looping = 0;
      c = serial_readchar (monitor_desc, timeout);

      if (c >= 0)
	{
	  c &= 0x7f;
	  /* This seems to interfere with proper function of the
	     input stream */
	  if (monitor_debug_p || remote_debug)
	    {
	      char buf[2];
	      buf[0] = c;
	      buf[1] = '\0';
	      puts_debug ("read -->", buf, "<--");
	    }

	}

      /* Canonicialize \n\r combinations into one \r */
      if ((current_monitor->flags & MO_HANDLE_NL) != 0)
	{
	  if ((c == '\r' && state == last_nl)
	      || (c == '\n' && state == last_cr))
	    {
	      state = last_crnl;
	      looping = 1;
	    }
	  else if (c == '\r')
	    state = last_cr;
	  else if (c != '\n')
	    state = last_random;
	  else
	    {
	      state = last_nl;
	      c = '\r';
	    }
	}
    }
  while (looping);

  if (c >= 0)
    return c;

  if (c == SERIAL_TIMEOUT)
#if 0
    /* I fail to see how detaching here can be useful */
    if (in_monitor_wait)	/* Watchdog went off */
      {
	target_mourn_inferior ();
	error ("GDB serial timeout has expired.  Target detached.\n");
      }
    else
#endif
      error ("Timeout reading from remote system.");

  perror_with_name ("remote-monitor");
}

/* Scan input from the remote system, until STRING is found.  If BUF is non-
   zero, then collect input until we have collected either STRING or BUFLEN-1
   chars.  In either case we terminate BUF with a 0.  If input overflows BUF
   because STRING can't be found, return -1, else return number of chars in BUF
   (minus the terminating NUL).  Note that in the non-overflow case, STRING
   will be at the end of BUF.  */

int
monitor_expect (char *string, char *buf, int buflen)
{
  char *p = string;
  int obuflen = buflen;
  int c;

  if (monitor_debug_p)
    {
      char *safe_string = (char *) alloca ((strlen (string) * 4) + 1);
      monitor_printable_string (safe_string, string, 0);
      fprintf_unfiltered (gdb_stdlog, "MON Expecting '%s'\n", safe_string);
    }

  immediate_quit++;
  while (1)
    {
      if (buf)
	{
	  if (buflen < 2)
	    {
	      *buf = '\000';
	      immediate_quit--;
	      return -1;
	    }

	  c = readchar (timeout);
	  if (c == '\000')
	    continue;
	  *buf++ = c;
	  buflen--;
	}
      else
	c = readchar (timeout);

      /* Don't expect any ^C sent to be echoed */

      if (*p == '\003' || c == *p)
	{
	  p++;
	  if (*p == '\0')
	    {
	      immediate_quit--;

	      if (buf)
		{
		  *buf++ = '\000';
		  return obuflen - buflen;
		}
	      else
		return 0;
	    }
	}
      else
	{
	  /* We got a character that doesn't match the string.  We need to
	     back up p, but how far?  If we're looking for "..howdy" and the
	     monitor sends "...howdy"?  There's certainly a match in there,
	     but when we receive the third ".", we won't find it if we just
	     restart the matching at the beginning of the string.

	     This is a Boyer-Moore kind of situation.  We want to reset P to
	     the end of the longest prefix of STRING that is a suffix of
	     what we've read so far.  In the example above, that would be
	     ".." --- the longest prefix of "..howdy" that is a suffix of
	     "...".  This longest prefix could be the empty string, if C
	     is nowhere to be found in STRING.

	     If this longest prefix is not the empty string, it must contain
	     C, so let's search from the end of STRING for instances of C,
	     and see if the portion of STRING before that is a suffix of
	     what we read before C.  Actually, we can search backwards from
	     p, since we know no prefix can be longer than that.

	     Note that we can use STRING itself, along with C, as a record
	     of what we've received so far.  :) */
	  int i;

	  for (i = (p - string) - 1; i >= 0; i--)
	    if (string[i] == c)
	      {
		/* Is this prefix a suffix of what we've read so far?
		   In other words, does
                     string[0 .. i-1] == string[p - i, p - 1]? */
		if (! memcmp (string, p - i, i))
		  {
		    p = string + i + 1;
		    break;
		  }
	      }
	  if (i < 0)
	    p = string;
	}
    }
}

/* Search for a regexp.  */

static int
monitor_expect_regexp (struct re_pattern_buffer *pat, char *buf, int buflen)
{
  char *mybuf;
  char *p;
  monitor_debug ("MON Expecting regexp\n");
  if (buf)
    mybuf = buf;
  else
    {
      mybuf = alloca (TARGET_BUF_SIZE);
      buflen = TARGET_BUF_SIZE;
    }

  p = mybuf;
  while (1)
    {
      int retval;

      if (p - mybuf >= buflen)
	{			/* Buffer about to overflow */

/* On overflow, we copy the upper half of the buffer to the lower half.  Not
   great, but it usually works... */

	  memcpy (mybuf, mybuf + buflen / 2, buflen / 2);
	  p = mybuf + buflen / 2;
	}

      *p++ = readchar (timeout);

      retval = re_search (pat, mybuf, p - mybuf, 0, p - mybuf, NULL);
      if (retval >= 0)
	return 1;
    }
}

/* Keep discarding input until we see the MONITOR prompt.

   The convention for dealing with the prompt is that you
   o give your command
   o *then* wait for the prompt.

   Thus the last thing that a procedure does with the serial line will
   be an monitor_expect_prompt().  Exception: monitor_resume does not
   wait for the prompt, because the terminal is being handed over to
   the inferior.  However, the next thing which happens after that is
   a monitor_wait which does wait for the prompt.  Note that this
   includes abnormal exit, e.g. error().  This is necessary to prevent
   getting into states from which we can't recover.  */

int
monitor_expect_prompt (char *buf, int buflen)
{
  monitor_debug ("MON Expecting prompt\n");
  return monitor_expect (current_monitor->prompt, buf, buflen);
}

/* Get N 32-bit words from remote, each preceded by a space, and put
   them in registers starting at REGNO.  */

#if 0
static unsigned long
get_hex_word (void)
{
  unsigned long val;
  int i;
  int ch;

  do
    ch = readchar (timeout);
  while (isspace (ch));

  val = from_hex (ch);

  for (i = 7; i >= 1; i--)
    {
      ch = readchar (timeout);
      if (!isxdigit (ch))
	break;
      val = (val << 4) | from_hex (ch);
    }

  return val;
}
#endif

static void
compile_pattern (char *pattern, struct re_pattern_buffer *compiled_pattern,
		 char *fastmap)
{
  int tmp;
  const char *val;

  compiled_pattern->fastmap = fastmap;

  tmp = re_set_syntax (RE_SYNTAX_EMACS);
  val = re_compile_pattern (pattern,
			    strlen (pattern),
			    compiled_pattern);
  re_set_syntax (tmp);

  if (val)
    error ("compile_pattern: Can't compile pattern string `%s': %s!", pattern, val);

  if (fastmap)
    re_compile_fastmap (compiled_pattern);
}

/* Open a connection to a remote debugger. NAME is the filename used
   for communication.  */

void
monitor_open (char *args, struct monitor_ops *mon_ops, int from_tty)
{
  char *name;
  char **p;

  if (mon_ops->magic != MONITOR_OPS_MAGIC)
    error ("Magic number of monitor_ops struct wrong.");

  targ_ops = mon_ops->target;
  name = targ_ops->to_shortname;

  if (!args)
    error ("Use `target %s DEVICE-NAME' to use a serial port, or \n\
`target %s HOST-NAME:PORT-NUMBER' to use a network connection.", name, name);

  target_preopen (from_tty);

  /* Setup pattern for register dump */

  if (mon_ops->register_pattern)
    compile_pattern (mon_ops->register_pattern, &register_pattern,
		     register_fastmap);

  if (mon_ops->getmem.resp_delim)
    compile_pattern (mon_ops->getmem.resp_delim, &getmem_resp_delim_pattern,
		     getmem_resp_delim_fastmap);

  if (mon_ops->setmem.resp_delim)
    compile_pattern (mon_ops->setmem.resp_delim, &setmem_resp_delim_pattern,
                     setmem_resp_delim_fastmap);

  if (mon_ops->setreg.resp_delim)
    compile_pattern (mon_ops->setreg.resp_delim, &setreg_resp_delim_pattern,
                     setreg_resp_delim_fastmap);
  
  unpush_target (targ_ops);

  if (dev_name)
    xfree (dev_name);
  dev_name = xstrdup (args);

  monitor_desc = serial_open (dev_name);

  if (!monitor_desc)
    perror_with_name (dev_name);

  if (baud_rate != -1)
    {
      if (serial_setbaudrate (monitor_desc, baud_rate))
	{
	  serial_close (monitor_desc);
	  perror_with_name (dev_name);
	}
    }

  serial_raw (monitor_desc);

  serial_flush_input (monitor_desc);

  /* some systems only work with 2 stop bits */

  serial_setstopbits (monitor_desc, mon_ops->stopbits);

  current_monitor = mon_ops;

  /* See if we can wake up the monitor.  First, try sending a stop sequence,
     then send the init strings.  Last, remove all breakpoints.  */

  if (current_monitor->stop)
    {
      monitor_stop ();
      if ((current_monitor->flags & MO_NO_ECHO_ON_OPEN) == 0)
	{
	  monitor_debug ("EXP Open echo\n");
	  monitor_expect_prompt (NULL, 0);
	}
    }

  /* wake up the monitor and see if it's alive */
  for (p = mon_ops->init; *p != NULL; p++)
    {
      /* Some of the characters we send may not be echoed,
         but we hope to get a prompt at the end of it all. */

      if ((current_monitor->flags & MO_NO_ECHO_ON_OPEN) == 0)
	monitor_printf (*p);
      else
	monitor_printf_noecho (*p);
      monitor_expect_prompt (NULL, 0);
    }

  serial_flush_input (monitor_desc);

  /* Alloc breakpoints */
  if (mon_ops->set_break != NULL)
    {
      if (mon_ops->num_breakpoints == 0)
	mon_ops->num_breakpoints = 8;

      breakaddr = (CORE_ADDR *) xmalloc (mon_ops->num_breakpoints * sizeof (CORE_ADDR));
      memset (breakaddr, 0, mon_ops->num_breakpoints * sizeof (CORE_ADDR));
    }

  /* Remove all breakpoints */

  if (mon_ops->clr_all_break)
    {
      monitor_printf (mon_ops->clr_all_break);
      monitor_expect_prompt (NULL, 0);
    }

  if (from_tty)
    printf_unfiltered ("Remote target %s connected to %s\n", name, dev_name);

  push_target (targ_ops);

  inferior_ptid = pid_to_ptid (42000);	/* Make run command think we are busy... */

  /* Give monitor_wait something to read */

  monitor_printf (current_monitor->line_term);

  start_remote ();
}

/* Close out all files and local state before this target loses
   control.  */

void
monitor_close (int quitting)
{
  if (monitor_desc)
    serial_close (monitor_desc);

  /* Free breakpoint memory */
  if (breakaddr != NULL)
    {
      xfree (breakaddr);
      breakaddr = NULL;
    }

  monitor_desc = NULL;
}

/* Terminate the open connection to the remote debugger.  Use this
   when you want to detach and do something else with your gdb.  */

static void
monitor_detach (char *args, int from_tty)
{
  pop_target ();		/* calls monitor_close to do the real work */
  if (from_tty)
    printf_unfiltered ("Ending remote %s debugging\n", target_shortname);
}

/* Convert VALSTR into the target byte-ordered value of REGNO and store it.  */

char *
monitor_supply_register (int regno, char *valstr)
{
  ULONGEST val;
  unsigned char regbuf[MAX_REGISTER_SIZE];
  char *p;

  val = 0;
  p = valstr;
  while (p && *p != '\0')
    {
      if (*p == '\r' || *p == '\n')
        {
          while (*p != '\0') 
              p++;
          break;
        }
      if (isspace (*p))
        {
          p++;
          continue;
        }
      if (!isxdigit (*p) && *p != 'x')
        {
          break;
        }

      val <<= 4;
      val += fromhex (*p++);
    }
  monitor_debug ("Supplying Register %d %s\n", regno, valstr);

  if (val == 0 && valstr == p)
    error ("monitor_supply_register (%d):  bad value from monitor: %s.",
	   regno, valstr);

  /* supply register stores in target byte order, so swap here */

  store_unsigned_integer (regbuf, DEPRECATED_REGISTER_RAW_SIZE (regno), val);

  supply_register (regno, regbuf);

  return p;
}

/* Tell the remote machine to resume.  */

static void
monitor_resume (ptid_t ptid, int step, enum target_signal sig)
{
  /* Some monitors require a different command when starting a program */
  monitor_debug ("MON resume\n");
  if (current_monitor->flags & MO_RUN_FIRST_TIME && first_time == 1)
    {
      first_time = 0;
      monitor_printf ("run\r");
      if (current_monitor->flags & MO_NEED_REGDUMP_AFTER_CONT)
	dump_reg_flag = 1;
      return;
    }
  if (step)
    monitor_printf (current_monitor->step);
  else
    {
      if (current_monitor->continue_hook)
	(*current_monitor->continue_hook) ();
      else
	monitor_printf (current_monitor->cont);
      if (current_monitor->flags & MO_NEED_REGDUMP_AFTER_CONT)
	dump_reg_flag = 1;
    }
}

/* Parse the output of a register dump command.  A monitor specific
   regexp is used to extract individual register descriptions of the
   form REG=VAL.  Each description is split up into a name and a value
   string which are passed down to monitor specific code.  */

static void
parse_register_dump (char *buf, int len)
{
  monitor_debug ("MON Parsing  register dump\n");
  while (1)
    {
      int regnamelen, vallen;
      char *regname, *val;
      /* Element 0 points to start of register name, and element 1
         points to the start of the register value.  */
      struct re_registers register_strings;

      memset (&register_strings, 0, sizeof (struct re_registers));

      if (re_search (&register_pattern, buf, len, 0, len,
		     &register_strings) == -1)
	break;

      regnamelen = register_strings.end[1] - register_strings.start[1];
      regname = buf + register_strings.start[1];
      vallen = register_strings.end[2] - register_strings.start[2];
      val = buf + register_strings.start[2];

      current_monitor->supply_register (regname, regnamelen, val, vallen);

      buf += register_strings.end[0];
      len -= register_strings.end[0];
    }
}

/* Send ^C to target to halt it.  Target will respond, and send us a
   packet.  */

static void
monitor_interrupt (int signo)
{
  /* If this doesn't work, try more severe steps.  */
  signal (signo, monitor_interrupt_twice);

  if (monitor_debug_p || remote_debug)
    fprintf_unfiltered (gdb_stdlog, "monitor_interrupt called\n");

  target_stop ();
}

/* The user typed ^C twice.  */

static void
monitor_interrupt_twice (int signo)
{
  signal (signo, ofunc);

  monitor_interrupt_query ();

  signal (signo, monitor_interrupt);
}

/* Ask the user what to do when an interrupt is received.  */

static void
monitor_interrupt_query (void)
{
  target_terminal_ours ();

  if (query ("Interrupted while waiting for the program.\n\
Give up (and stop debugging it)? "))
    {
      target_mourn_inferior ();
      throw_exception (RETURN_QUIT);
    }

  target_terminal_inferior ();
}

static void
monitor_wait_cleanup (void *old_timeout)
{
  timeout = *(int *) old_timeout;
  signal (SIGINT, ofunc);
  in_monitor_wait = 0;
}



static void
monitor_wait_filter (char *buf,
		     int bufmax,
		     int *ext_resp_len,
		     struct target_waitstatus *status)
{
  int resp_len;
  do
    {
      resp_len = monitor_expect_prompt (buf, bufmax);
      *ext_resp_len = resp_len;

      if (resp_len <= 0)
	fprintf_unfiltered (gdb_stderr, "monitor_wait:  excessive response from monitor: %s.", buf);
    }
  while (resp_len < 0);

  /* Print any output characters that were preceded by ^O.  */
  /* FIXME - This would be great as a user settabgle flag */
  if (monitor_debug_p || remote_debug
      || current_monitor->flags & MO_PRINT_PROGRAM_OUTPUT)
    {
      int i;

      for (i = 0; i < resp_len - 1; i++)
	if (buf[i] == 0x0f)
	  putchar_unfiltered (buf[++i]);
    }
}



/* Wait until the remote machine stops, then return, storing status in
   status just as `wait' would.  */

static ptid_t
monitor_wait (ptid_t ptid, struct target_waitstatus *status)
{
  int old_timeout = timeout;
  char buf[TARGET_BUF_SIZE];
  int resp_len;
  struct cleanup *old_chain;

  status->kind = TARGET_WAITKIND_EXITED;
  status->value.integer = 0;

  old_chain = make_cleanup (monitor_wait_cleanup, &old_timeout);
  monitor_debug ("MON wait\n");

#if 0
  /* This is somthing other than a maintenance command */
    in_monitor_wait = 1;
  timeout = watchdog > 0 ? watchdog : -1;
#else
  timeout = -1;		/* Don't time out -- user program is running. */
#endif

  ofunc = (void (*)()) signal (SIGINT, monitor_interrupt);

  if (current_monitor->wait_filter)
    (*current_monitor->wait_filter) (buf, sizeof (buf), &resp_len, status);
  else
    monitor_wait_filter (buf, sizeof (buf), &resp_len, status);

#if 0				/* Transferred to monitor wait filter */
  do
    {
      resp_len = monitor_expect_prompt (buf, sizeof (buf));

      if (resp_len <= 0)
	fprintf_unfiltered (gdb_stderr, "monitor_wait:  excessive response from monitor: %s.", buf);
    }
  while (resp_len < 0);

  /* Print any output characters that were preceded by ^O.  */
  /* FIXME - This would be great as a user settabgle flag */
  if (monitor_debug_p || remote_debug
      || current_monitor->flags & MO_PRINT_PROGRAM_OUTPUT)
    {
      int i;

      for (i = 0; i < resp_len - 1; i++)
	if (buf[i] == 0x0f)
	  putchar_unfiltered (buf[++i]);
    }
#endif

  signal (SIGINT, ofunc);

  timeout = old_timeout;
#if 0
  if (dump_reg_flag && current_monitor->dump_registers)
    {
      dump_reg_flag = 0;
      monitor_printf (current_monitor->dump_registers);
      resp_len = monitor_expect_prompt (buf, sizeof (buf));
    }

  if (current_monitor->register_pattern)
    parse_register_dump (buf, resp_len);
#else
  monitor_debug ("Wait fetching registers after stop\n");
  monitor_dump_regs ();
#endif

  status->kind = TARGET_WAITKIND_STOPPED;
  status->value.sig = TARGET_SIGNAL_TRAP;

  discard_cleanups (old_chain);

  in_monitor_wait = 0;

  return inferior_ptid;
}

/* Fetch register REGNO, or all registers if REGNO is -1. Returns
   errno value.  */

static void
monitor_fetch_register (int regno)
{
  const char *name;
  char *zerobuf;
  char *regbuf;
  int i;

  regbuf  = alloca (MAX_REGISTER_SIZE * 2 + 1);
  zerobuf = alloca (MAX_REGISTER_SIZE);
  memset (zerobuf, 0, MAX_REGISTER_SIZE);

  if (current_monitor->regname != NULL)
    name = current_monitor->regname (regno);
  else
    name = current_monitor->regnames[regno];
  monitor_debug ("MON fetchreg %d '%s'\n", regno, name ? name : "(null name)");

  if (!name || (*name == '\0'))
    {
      monitor_debug ("No register known for %d\n", regno);
      supply_register (regno, zerobuf);
      return;
    }

  /* send the register examine command */

  monitor_printf (current_monitor->getreg.cmd, name);

  /* If RESP_DELIM is specified, we search for that as a leading
     delimiter for the register value.  Otherwise, we just start
     searching from the start of the buf.  */

  if (current_monitor->getreg.resp_delim)
    {
      monitor_debug ("EXP getreg.resp_delim\n");
      monitor_expect (current_monitor->getreg.resp_delim, NULL, 0);
      /* Handle case of first 32 registers listed in pairs.  */
      if (current_monitor->flags & MO_32_REGS_PAIRED
	  && (regno & 1) != 0 && regno < 32)
	{
	  monitor_debug ("EXP getreg.resp_delim\n");
	  monitor_expect (current_monitor->getreg.resp_delim, NULL, 0);
	}
    }

  /* Skip leading spaces and "0x" if MO_HEX_PREFIX flag is set */
  if (current_monitor->flags & MO_HEX_PREFIX)
    {
      int c;
      c = readchar (timeout);
      while (c == ' ')
	c = readchar (timeout);
      if ((c == '0') && ((c = readchar (timeout)) == 'x'))
	;
      else
	error ("Bad value returned from monitor while fetching register %x.",
	       regno);
    }

  /* Read upto the maximum number of hex digits for this register, skipping
     spaces, but stop reading if something else is seen.  Some monitors
     like to drop leading zeros.  */

  for (i = 0; i < DEPRECATED_REGISTER_RAW_SIZE (regno) * 2; i++)
    {
      int c;
      c = readchar (timeout);
      while (c == ' ')
	c = readchar (timeout);

      if (!isxdigit (c))
	break;

      regbuf[i] = c;
    }

  regbuf[i] = '\000';		/* terminate the number */
  monitor_debug ("REGVAL '%s'\n", regbuf);

  /* If TERM is present, we wait for that to show up.  Also, (if TERM
     is present), we will send TERM_CMD if that is present.  In any
     case, we collect all of the output into buf, and then wait for
     the normal prompt.  */

  if (current_monitor->getreg.term)
    {
      monitor_debug ("EXP getreg.term\n");
      monitor_expect (current_monitor->getreg.term, NULL, 0);		/* get response */
    }

  if (current_monitor->getreg.term_cmd)
    {
      monitor_debug ("EMIT getreg.term.cmd\n");
      monitor_printf (current_monitor->getreg.term_cmd);
    }
  if (!current_monitor->getreg.term ||	/* Already expected or */
      current_monitor->getreg.term_cmd)		/* ack expected */
    monitor_expect_prompt (NULL, 0);	/* get response */

  monitor_supply_register (regno, regbuf);
}

/* Sometimes, it takes several commands to dump the registers */
/* This is a primitive for use by variations of monitor interfaces in
   case they need to compose the operation.
 */
int
monitor_dump_reg_block (char *block_cmd)
{
  char buf[TARGET_BUF_SIZE];
  int resp_len;
  monitor_printf (block_cmd);
  resp_len = monitor_expect_prompt (buf, sizeof (buf));
  parse_register_dump (buf, resp_len);
  return 1;
}


/* Read the remote registers into the block regs.  */
/* Call the specific function if it has been provided */

static void
monitor_dump_regs (void)
{
  char buf[TARGET_BUF_SIZE];
  int resp_len;
  if (current_monitor->dumpregs)
    (*(current_monitor->dumpregs)) ();	/* call supplied function */
  else if (current_monitor->dump_registers)	/* default version */
    {
      monitor_printf (current_monitor->dump_registers);
      resp_len = monitor_expect_prompt (buf, sizeof (buf));
      parse_register_dump (buf, resp_len);
    }
  else
    internal_error (__FILE__, __LINE__, "failed internal consistency check");			/* Need some way to read registers */
}

static void
monitor_fetch_registers (int regno)
{
  monitor_debug ("MON fetchregs\n");
  if (current_monitor->getreg.cmd)
    {
      if (regno >= 0)
	{
	  monitor_fetch_register (regno);
	  return;
	}

      for (regno = 0; regno < NUM_REGS; regno++)
	monitor_fetch_register (regno);
    }
  else
    {
      monitor_dump_regs ();
    }
}

/* Store register REGNO, or all if REGNO == 0.  Return errno value.  */

static void
monitor_store_register (int regno)
{
  const char *name;
  ULONGEST val;
  
  if (current_monitor->regname != NULL)
    name = current_monitor->regname (regno);
  else
    name = current_monitor->regnames[regno];
  
  if (!name || (*name == '\0'))
    {
      monitor_debug ("MON Cannot store unknown register\n");
      return;
    }

  val = read_register (regno);
  monitor_debug ("MON storeg %d %s\n", regno,
		 phex (val, DEPRECATED_REGISTER_RAW_SIZE (regno)));

  /* send the register deposit command */

  if (current_monitor->flags & MO_REGISTER_VALUE_FIRST)
    monitor_printf (current_monitor->setreg.cmd, val, name);
  else if (current_monitor->flags & MO_SETREG_INTERACTIVE)
    monitor_printf (current_monitor->setreg.cmd, name);
  else
    monitor_printf (current_monitor->setreg.cmd, name, val);

  if (current_monitor->setreg.resp_delim)
    {
      monitor_debug ("EXP setreg.resp_delim\n");
      monitor_expect_regexp (&setreg_resp_delim_pattern, NULL, 0);
      if (current_monitor->flags & MO_SETREG_INTERACTIVE)
	monitor_printf ("%s\r", paddr_nz (val));
    }
  if (current_monitor->setreg.term)
    {
      monitor_debug ("EXP setreg.term\n");
      monitor_expect (current_monitor->setreg.term, NULL, 0);
      if (current_monitor->flags & MO_SETREG_INTERACTIVE)
	monitor_printf ("%s\r", paddr_nz (val));
      monitor_expect_prompt (NULL, 0);
    }
  else
    monitor_expect_prompt (NULL, 0);
  if (current_monitor->setreg.term_cmd)		/* Mode exit required */
    {
      monitor_debug ("EXP setreg_termcmd\n");
      monitor_printf ("%s", current_monitor->setreg.term_cmd);
      monitor_expect_prompt (NULL, 0);
    }
}				/* monitor_store_register */

/* Store the remote registers.  */

static void
monitor_store_registers (int regno)
{
  if (regno >= 0)
    {
      monitor_store_register (regno);
      return;
    }

  for (regno = 0; regno < NUM_REGS; regno++)
    monitor_store_register (regno);
}

/* Get ready to modify the registers array.  On machines which store
   individual registers, this doesn't need to do anything.  On machines
   which store all the registers in one fell swoop, this makes sure
   that registers contains all the registers from the program being
   debugged.  */

static void
monitor_prepare_to_store (void)
{
  /* Do nothing, since we can store individual regs */
}

static void
monitor_files_info (struct target_ops *ops)
{
  printf_unfiltered ("\tAttached to %s at %d baud.\n", dev_name, baud_rate);
}

static int
monitor_write_memory (CORE_ADDR memaddr, char *myaddr, int len)
{
  unsigned int val, hostval;
  char *cmd;
  int i;

  monitor_debug ("MON write %d %s\n", len, paddr (memaddr));

  if (current_monitor->flags & MO_ADDR_BITS_REMOVE)
    memaddr = ADDR_BITS_REMOVE (memaddr);

  /* Use memory fill command for leading 0 bytes.  */

  if (current_monitor->fill)
    {
      for (i = 0; i < len; i++)
	if (myaddr[i] != 0)
	  break;

      if (i > 4)		/* More than 4 zeros is worth doing */
	{
	  monitor_debug ("MON FILL %d\n", i);
	  if (current_monitor->flags & MO_FILL_USES_ADDR)
	    monitor_printf (current_monitor->fill, memaddr, (memaddr + i) - 1, 0);
	  else
	    monitor_printf (current_monitor->fill, memaddr, i, 0);

	  monitor_expect_prompt (NULL, 0);

	  return i;
	}
    }

#if 0
  /* Can't actually use long longs if VAL is an int (nice idea, though).  */
  if ((memaddr & 0x7) == 0 && len >= 8 && current_monitor->setmem.cmdll)
    {
      len = 8;
      cmd = current_monitor->setmem.cmdll;
    }
  else
#endif
  if ((memaddr & 0x3) == 0 && len >= 4 && current_monitor->setmem.cmdl)
    {
      len = 4;
      cmd = current_monitor->setmem.cmdl;
    }
  else if ((memaddr & 0x1) == 0 && len >= 2 && current_monitor->setmem.cmdw)
    {
      len = 2;
      cmd = current_monitor->setmem.cmdw;
    }
  else
    {
      len = 1;
      cmd = current_monitor->setmem.cmdb;
    }

  val = extract_unsigned_integer (myaddr, len);

  if (len == 4)
    {
      hostval = *(unsigned int *) myaddr;
      monitor_debug ("Hostval(%08x) val(%08x)\n", hostval, val);
    }


  if (current_monitor->flags & MO_NO_ECHO_ON_SETMEM)
    monitor_printf_noecho (cmd, memaddr, val);
  else if (current_monitor->flags & MO_SETMEM_INTERACTIVE)
    {

      monitor_printf_noecho (cmd, memaddr);

      if (current_monitor->setmem.resp_delim)
        {
          monitor_debug ("EXP setmem.resp_delim");
          monitor_expect_regexp (&setmem_resp_delim_pattern, NULL, 0); 
	  monitor_printf ("%x\r", val);
       }
      if (current_monitor->setmem.term)
	{
	  monitor_debug ("EXP setmem.term");
	  monitor_expect (current_monitor->setmem.term, NULL, 0);
	  monitor_printf ("%x\r", val);
	}
      if (current_monitor->setmem.term_cmd)
	{			/* Emit this to get out of the memory editing state */
	  monitor_printf ("%s", current_monitor->setmem.term_cmd);
	  /* Drop through to expecting a prompt */
	}
    }
  else
    monitor_printf (cmd, memaddr, val);

  monitor_expect_prompt (NULL, 0);

  return len;
}


static int
monitor_write_memory_bytes (CORE_ADDR memaddr, char *myaddr, int len)
{
  unsigned char val;
  int written = 0;
  if (len == 0)
    return 0;
  /* Enter the sub mode */
  monitor_printf (current_monitor->setmem.cmdb, memaddr);
  monitor_expect_prompt (NULL, 0);
  while (len)
    {
      val = *myaddr;
      monitor_printf ("%x\r", val);
      myaddr++;
      memaddr++;
      written++;
      /* If we wanted to, here we could validate the address */
      monitor_expect_prompt (NULL, 0);
      len--;
    }
  /* Now exit the sub mode */
  monitor_printf (current_monitor->getreg.term_cmd);
  monitor_expect_prompt (NULL, 0);
  return written;
}


static void
longlongendswap (unsigned char *a)
{
  int i, j;
  unsigned char x;
  i = 0;
  j = 7;
  while (i < 4)
    {
      x = *(a + i);
      *(a + i) = *(a + j);
      *(a + j) = x;
      i++, j--;
    }
}
/* Format 32 chars of long long value, advance the pointer */
static char *hexlate = "0123456789abcdef";
static char *
longlong_hexchars (unsigned long long value,
		   char *outbuff)
{
  if (value == 0)
    {
      *outbuff++ = '0';
      return outbuff;
    }
  else
    {
      static unsigned char disbuf[8];	/* disassembly buffer */
      unsigned char *scan, *limit;	/* loop controls */
      unsigned char c, nib;
      int leadzero = 1;
      scan = disbuf;
      limit = scan + 8;
      {
	unsigned long long *dp;
	dp = (unsigned long long *) scan;
	*dp = value;
      }
      longlongendswap (disbuf);	/* FIXME: ONly on big endian hosts */
      while (scan < limit)
	{
	  c = *scan++;		/* a byte of our long long value */
	  if (leadzero)
	    {
	      if (c == 0)
		continue;
	      else
		leadzero = 0;	/* henceforth we print even zeroes */
	    }
	  nib = c >> 4;		/* high nibble bits */
	  *outbuff++ = hexlate[nib];
	  nib = c & 0x0f;	/* low nibble bits */
	  *outbuff++ = hexlate[nib];
	}
      return outbuff;
    }
}				/* longlong_hexchars */



/* I am only going to call this when writing virtual byte streams.
   Which possably entails endian conversions
 */
static int
monitor_write_memory_longlongs (CORE_ADDR memaddr, char *myaddr, int len)
{
  static char hexstage[20];	/* At least 16 digits required, plus null */
  char *endstring;
  long long *llptr;
  long long value;
  int written = 0;
  llptr = (unsigned long long *) myaddr;
  if (len == 0)
    return 0;
  monitor_printf (current_monitor->setmem.cmdll, memaddr);
  monitor_expect_prompt (NULL, 0);
  while (len >= 8)
    {
      value = *llptr;
      endstring = longlong_hexchars (*llptr, hexstage);
      *endstring = '\0';	/* NUll terminate for printf */
      monitor_printf ("%s\r", hexstage);
      llptr++;
      memaddr += 8;
      written += 8;
      /* If we wanted to, here we could validate the address */
      monitor_expect_prompt (NULL, 0);
      len -= 8;
    }
  /* Now exit the sub mode */
  monitor_printf (current_monitor->getreg.term_cmd);
  monitor_expect_prompt (NULL, 0);
  return written;
}				/* */



/* ----- MONITOR_WRITE_MEMORY_BLOCK ---------------------------- */
/* This is for the large blocks of memory which may occur in downloading.
   And for monitors which use interactive entry,
   And for monitors which do not have other downloading methods.
   Without this, we will end up calling monitor_write_memory many times
   and do the entry and exit of the sub mode many times
   This currently assumes...
   MO_SETMEM_INTERACTIVE
   ! MO_NO_ECHO_ON_SETMEM
   To use this, the you have to patch the monitor_cmds block with
   this function. Otherwise, its not tuned up for use by all
   monitor variations.
 */

static int
monitor_write_memory_block (CORE_ADDR memaddr, char *myaddr, int len)
{
  int written;
  written = 0;
  /* FIXME: This would be a good place to put the zero test */
#if 1
  if ((len > 8) && (((len & 0x07)) == 0) && current_monitor->setmem.cmdll)
    {
      return monitor_write_memory_longlongs (memaddr, myaddr, len);
    }
#endif
  written = monitor_write_memory_bytes (memaddr, myaddr, len);
  return written;
}

/* This is an alternate form of monitor_read_memory which is used for monitors
   which can only read a single byte/word/etc. at a time.  */

static int
monitor_read_memory_single (CORE_ADDR memaddr, char *myaddr, int len)
{
  unsigned int val;
  char membuf[sizeof (int) * 2 + 1];
  char *p;
  char *cmd;

  monitor_debug ("MON read single\n");
#if 0
  /* Can't actually use long longs (nice idea, though).  In fact, the
     call to strtoul below will fail if it tries to convert a value
     that's too big to fit in a long.  */
  if ((memaddr & 0x7) == 0 && len >= 8 && current_monitor->getmem.cmdll)
    {
      len = 8;
      cmd = current_monitor->getmem.cmdll;
    }
  else
#endif
  if ((memaddr & 0x3) == 0 && len >= 4 && current_monitor->getmem.cmdl)
    {
      len = 4;
      cmd = current_monitor->getmem.cmdl;
    }
  else if ((memaddr & 0x1) == 0 && len >= 2 && current_monitor->getmem.cmdw)
    {
      len = 2;
      cmd = current_monitor->getmem.cmdw;
    }
  else
    {
      len = 1;
      cmd = current_monitor->getmem.cmdb;
    }

  /* Send the examine command.  */

  monitor_printf (cmd, memaddr);

  /* If RESP_DELIM is specified, we search for that as a leading
     delimiter for the memory value.  Otherwise, we just start
     searching from the start of the buf.  */

  if (current_monitor->getmem.resp_delim)
    {
      monitor_debug ("EXP getmem.resp_delim\n");
      monitor_expect_regexp (&getmem_resp_delim_pattern, NULL, 0);
    }

  /* Now, read the appropriate number of hex digits for this loc,
     skipping spaces.  */

  /* Skip leading spaces and "0x" if MO_HEX_PREFIX flag is set. */
  if (current_monitor->flags & MO_HEX_PREFIX)
    {
      int c;

      c = readchar (timeout);
      while (c == ' ')
	c = readchar (timeout);
      if ((c == '0') && ((c = readchar (timeout)) == 'x'))
	;
      else
	monitor_error ("monitor_read_memory_single", 
		       "bad response from monitor",
		       memaddr, 0, NULL, 0);
    }

  {
    int i;
    for (i = 0; i < len * 2; i++)
      {
	int c;

	while (1)
	  {
	    c = readchar (timeout);
	    if (isxdigit (c))
	      break;
	    if (c == ' ')
	      continue;
	    
	    monitor_error ("monitor_read_memory_single",
			   "bad response from monitor",
			   memaddr, i, membuf, 0);
	  }
      membuf[i] = c;
    }
    membuf[i] = '\000';		/* terminate the number */
  }

/* If TERM is present, we wait for that to show up.  Also, (if TERM is
   present), we will send TERM_CMD if that is present.  In any case, we collect
   all of the output into buf, and then wait for the normal prompt.  */

  if (current_monitor->getmem.term)
    {
      monitor_expect (current_monitor->getmem.term, NULL, 0);	/* get response */

      if (current_monitor->getmem.term_cmd)
	{
	  monitor_printf (current_monitor->getmem.term_cmd);
	  monitor_expect_prompt (NULL, 0);
	}
    }
  else
    monitor_expect_prompt (NULL, 0);	/* get response */

  p = membuf;
  val = strtoul (membuf, &p, 16);

  if (val == 0 && membuf == p)
    monitor_error ("monitor_read_memory_single",
		   "bad value from monitor",
		   memaddr, 0, membuf, 0);

  /* supply register stores in target byte order, so swap here */

  store_unsigned_integer (myaddr, len, val);

  return len;
}

/* Copy LEN bytes of data from debugger memory at MYADDR to inferior's
   memory at MEMADDR.  Returns length moved.  Currently, we do no more
   than 16 bytes at a time.  */

static int
monitor_read_memory (CORE_ADDR memaddr, char *myaddr, int len)
{
  unsigned int val;
  char buf[512];
  char *p, *p1;
  int resp_len;
  int i;
  CORE_ADDR dumpaddr;

  if (len <= 0)
    {
      monitor_debug ("Zero length call to monitor_read_memory\n");
      return 0;
    }

  monitor_debug ("MON read block ta(%s) ha(%lx) %d\n",
		 paddr_nz (memaddr), (long) myaddr, len);

  if (current_monitor->flags & MO_ADDR_BITS_REMOVE)
    memaddr = ADDR_BITS_REMOVE (memaddr);

  if (current_monitor->flags & MO_GETMEM_READ_SINGLE)
    return monitor_read_memory_single (memaddr, myaddr, len);

  len = min (len, 16);

  /* Some dumpers align the first data with the preceeding 16
     byte boundary. Some print blanks and start at the
     requested boundary. EXACT_DUMPADDR
   */

  dumpaddr = (current_monitor->flags & MO_EXACT_DUMPADDR)
    ? memaddr : memaddr & ~0x0f;

  /* See if xfer would cross a 16 byte boundary.  If so, clip it.  */
  if (((memaddr ^ (memaddr + len - 1)) & ~0xf) != 0)
    len = ((memaddr + len) & ~0xf) - memaddr;

  /* send the memory examine command */

  if (current_monitor->flags & MO_GETMEM_NEEDS_RANGE)
    monitor_printf (current_monitor->getmem.cmdb, memaddr, memaddr + len);
  else if (current_monitor->flags & MO_GETMEM_16_BOUNDARY)
    monitor_printf (current_monitor->getmem.cmdb, dumpaddr);
  else
    monitor_printf (current_monitor->getmem.cmdb, memaddr, len);

  /* If TERM is present, we wait for that to show up.  Also, (if TERM
     is present), we will send TERM_CMD if that is present.  In any
     case, we collect all of the output into buf, and then wait for
     the normal prompt.  */

  if (current_monitor->getmem.term)
    {
      resp_len = monitor_expect (current_monitor->getmem.term, buf, sizeof buf);	/* get response */

      if (resp_len <= 0)
	monitor_error ("monitor_read_memory",
		       "excessive response from monitor",
		       memaddr, resp_len, buf, 0);

      if (current_monitor->getmem.term_cmd)
	{
	  serial_write (monitor_desc, current_monitor->getmem.term_cmd,
			strlen (current_monitor->getmem.term_cmd));
	  monitor_expect_prompt (NULL, 0);
	}
    }
  else
    resp_len = monitor_expect_prompt (buf, sizeof buf);		/* get response */

  p = buf;

  /* If RESP_DELIM is specified, we search for that as a leading
     delimiter for the values.  Otherwise, we just start searching
     from the start of the buf.  */

  if (current_monitor->getmem.resp_delim)
    {
      int retval, tmp;
      struct re_registers resp_strings;
      monitor_debug ("MON getmem.resp_delim %s\n", current_monitor->getmem.resp_delim);

      memset (&resp_strings, 0, sizeof (struct re_registers));
      tmp = strlen (p);
      retval = re_search (&getmem_resp_delim_pattern, p, tmp, 0, tmp,
			  &resp_strings);

      if (retval < 0)
	monitor_error ("monitor_read_memory",
		       "bad response from monitor",
		       memaddr, resp_len, buf, 0);

      p += resp_strings.end[0];
#if 0
      p = strstr (p, current_monitor->getmem.resp_delim);
      if (!p)
	monitor_error ("monitor_read_memory",
		       "bad response from monitor",
		       memaddr, resp_len, buf, 0);
      p += strlen (current_monitor->getmem.resp_delim);
#endif
    }
  monitor_debug ("MON scanning  %d ,%lx '%s'\n", len, (long) p, p);
  if (current_monitor->flags & MO_GETMEM_16_BOUNDARY)
    {
      char c;
      int fetched = 0;
      i = len;
      c = *p;


      while (!(c == '\000' || c == '\n' || c == '\r') && i > 0)
	{
	  if (isxdigit (c))
	    {
	      if ((dumpaddr >= memaddr) && (i > 0))
		{
		  val = fromhex (c) * 16 + fromhex (*(p + 1));
		  *myaddr++ = val;
		  if (monitor_debug_p || remote_debug)
		    fprintf_unfiltered (gdb_stdlog, "[%02x]", val);
		  --i;
		  fetched++;
		}
	      ++dumpaddr;
	      ++p;
	    }
	  ++p;			/* skip a blank or other non hex char */
	  c = *p;
	}
      if (fetched == 0)
	error ("Failed to read via monitor");
      if (monitor_debug_p || remote_debug)
	fprintf_unfiltered (gdb_stdlog, "\n");
      return fetched;		/* Return the number of bytes actually read */
    }
  monitor_debug ("MON scanning bytes\n");

  for (i = len; i > 0; i--)
    {
      /* Skip non-hex chars, but bomb on end of string and newlines */

      while (1)
	{
	  if (isxdigit (*p))
	    break;

	  if (*p == '\000' || *p == '\n' || *p == '\r')
	    monitor_error ("monitor_read_memory",
			   "badly terminated response from monitor",
			   memaddr, resp_len, buf, 0);
	  p++;
	}

      val = strtoul (p, &p1, 16);

      if (val == 0 && p == p1)
	monitor_error ("monitor_read_memory",
		       "bad value from monitor",
		       memaddr, resp_len, buf, 0);

      *myaddr++ = val;

      if (i == 1)
	break;

      p = p1;
    }

  return len;
}

/* Transfer LEN bytes between target address MEMADDR and GDB address
   MYADDR.  Returns 0 for success, errno code for failure. TARGET is
   unused. */

static int
monitor_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int write,
		     struct mem_attrib *attrib, struct target_ops *target)
{
  int res;

  if (write)
    {
      if (current_monitor->flags & MO_HAS_BLOCKWRITES)
	res = monitor_write_memory_block(memaddr, myaddr, len);
      else
	res = monitor_write_memory(memaddr, myaddr, len);
    }
  else
    {
      res = monitor_read_memory(memaddr, myaddr, len);
    }

  return res;
}

static void
monitor_kill (void)
{
  return;			/* ignore attempts to kill target system */
}

/* All we actually do is set the PC to the start address of exec_bfd, and start
   the program at that point.  */

static void
monitor_create_inferior (char *exec_file, char *args, char **env)
{
  if (args && (*args != '\000'))
    error ("Args are not supported by the monitor.");

  first_time = 1;
  clear_proceed_status ();
  proceed (bfd_get_start_address (exec_bfd), TARGET_SIGNAL_0, 0);
}

/* Clean up when a program exits.
   The program actually lives on in the remote processor's RAM, and may be
   run again without a download.  Don't leave it full of breakpoint
   instructions.  */

static void
monitor_mourn_inferior (void)
{
  unpush_target (targ_ops);
  generic_mourn_inferior ();	/* Do all the proper things now */
}

/* Tell the monitor to add a breakpoint.  */

static int
monitor_insert_breakpoint (CORE_ADDR addr, char *shadow)
{
  int i;
  const unsigned char *bp;
  int bplen;

  monitor_debug ("MON inst bkpt %s\n", paddr (addr));
  if (current_monitor->set_break == NULL)
    error ("No set_break defined for this monitor");

  if (current_monitor->flags & MO_ADDR_BITS_REMOVE)
    addr = ADDR_BITS_REMOVE (addr);

  /* Determine appropriate breakpoint size for this address.  */
  bp = gdbarch_breakpoint_from_pc (current_gdbarch, &addr, &bplen);

  for (i = 0; i < current_monitor->num_breakpoints; i++)
    {
      if (breakaddr[i] == 0)
	{
	  breakaddr[i] = addr;
	  monitor_read_memory (addr, shadow, bplen);
	  monitor_printf (current_monitor->set_break, addr);
	  monitor_expect_prompt (NULL, 0);
	  return 0;
	}
    }

  error ("Too many breakpoints (> %d) for monitor.", current_monitor->num_breakpoints);
}

/* Tell the monitor to remove a breakpoint.  */

static int
monitor_remove_breakpoint (CORE_ADDR addr, char *shadow)
{
  int i;

  monitor_debug ("MON rmbkpt %s\n", paddr (addr));
  if (current_monitor->clr_break == NULL)
    error ("No clr_break defined for this monitor");

  if (current_monitor->flags & MO_ADDR_BITS_REMOVE)
    addr = ADDR_BITS_REMOVE (addr);

  for (i = 0; i < current_monitor->num_breakpoints; i++)
    {
      if (breakaddr[i] == addr)
	{
	  breakaddr[i] = 0;
	  /* some monitors remove breakpoints based on the address */
	  if (current_monitor->flags & MO_CLR_BREAK_USES_ADDR)
	    monitor_printf (current_monitor->clr_break, addr);
	  else if (current_monitor->flags & MO_CLR_BREAK_1_BASED)
	    monitor_printf (current_monitor->clr_break, i + 1);
	  else
	    monitor_printf (current_monitor->clr_break, i);
	  monitor_expect_prompt (NULL, 0);
	  return 0;
	}
    }
  fprintf_unfiltered (gdb_stderr,
		      "Can't find breakpoint associated with 0x%s\n",
		      paddr_nz (addr));
  return 1;
}

/* monitor_wait_srec_ack -- wait for the target to send an acknowledgement for
   an S-record.  Return non-zero if the ACK is received properly.  */

static int
monitor_wait_srec_ack (void)
{
  int ch;

  if (current_monitor->flags & MO_SREC_ACK_PLUS)
    {
      return (readchar (timeout) == '+');
    }
  else if (current_monitor->flags & MO_SREC_ACK_ROTATE)
    {
      /* Eat two backspaces, a "rotating" char (|/-\), and a space.  */
      if ((ch = readchar (1)) < 0)
	return 0;
      if ((ch = readchar (1)) < 0)
	return 0;
      if ((ch = readchar (1)) < 0)
	return 0;
      if ((ch = readchar (1)) < 0)
	return 0;
    }
  return 1;
}

/* monitor_load -- download a file. */

static void
monitor_load (char *file, int from_tty)
{
  monitor_debug ("MON load\n");

  if (current_monitor->load_routine)
    current_monitor->load_routine (monitor_desc, file, hashmark);
  else
    {				/* The default is ascii S-records */
      int n;
      unsigned long load_offset;
      char buf[128];

      /* enable user to specify address for downloading as 2nd arg to load */
      n = sscanf (file, "%s 0x%lx", buf, &load_offset);
      if (n > 1)
	file = buf;
      else
	load_offset = 0;

      monitor_printf (current_monitor->load);
      if (current_monitor->loadresp)
	monitor_expect (current_monitor->loadresp, NULL, 0);

      load_srec (monitor_desc, file, (bfd_vma) load_offset,
		 32, SREC_ALL, hashmark,
		 current_monitor->flags & MO_SREC_ACK ?
		 monitor_wait_srec_ack : NULL);

      monitor_expect_prompt (NULL, 0);
    }

  /* Finally, make the PC point at the start address */
  if (exec_bfd)
    write_pc (bfd_get_start_address (exec_bfd));

  /* There used to be code here which would clear inferior_ptid and
     call clear_symtab_users.  None of that should be necessary:
     monitor targets should behave like remote protocol targets, and
     since generic_load does none of those things, this function
     shouldn't either.

     Furthermore, clearing inferior_ptid is *incorrect*.  After doing
     a load, we still have a valid connection to the monitor, with a
     live processor state to fiddle with.  The user can type
     `continue' or `jump *start' and make the program run.  If they do
     these things, however, GDB will be talking to a running program
     while inferior_ptid is null_ptid; this makes things like
     reinit_frame_cache very confused.  */
}

static void
monitor_stop (void)
{
  monitor_debug ("MON stop\n");
  if ((current_monitor->flags & MO_SEND_BREAK_ON_STOP) != 0)
    serial_send_break (monitor_desc);
  if (current_monitor->stop)
    monitor_printf_noecho (current_monitor->stop);
}

/* Put a COMMAND string out to MONITOR.  Output from MONITOR is placed
   in OUTPUT until the prompt is seen. FIXME: We read the characters
   ourseleves here cause of a nasty echo.  */

static void
monitor_rcmd (char *command,
	      struct ui_file *outbuf)
{
  char *p;
  int resp_len;
  char buf[1000];

  if (monitor_desc == NULL)
    error ("monitor target not open.");

  p = current_monitor->prompt;

  /* Send the command.  Note that if no args were supplied, then we're
     just sending the monitor a newline, which is sometimes useful.  */

  monitor_printf ("%s\r", (command ? command : ""));

  resp_len = monitor_expect_prompt (buf, sizeof buf);

  fputs_unfiltered (buf, outbuf);	/* Output the response */
}

/* Convert hex digit A to a number.  */

#if 0
static int
from_hex (int a)
{
  if (a >= '0' && a <= '9')
    return a - '0';
  if (a >= 'a' && a <= 'f')
    return a - 'a' + 10;
  if (a >= 'A' && a <= 'F')
    return a - 'A' + 10;

  error ("Reply contains invalid hex digit 0x%x", a);
}
#endif

char *
monitor_get_dev_name (void)
{
  return dev_name;
}

static struct target_ops monitor_ops;

static void
init_base_monitor_ops (void)
{
  monitor_ops.to_close = monitor_close;
  monitor_ops.to_detach = monitor_detach;
  monitor_ops.to_resume = monitor_resume;
  monitor_ops.to_wait = monitor_wait;
  monitor_ops.to_fetch_registers = monitor_fetch_registers;
  monitor_ops.to_store_registers = monitor_store_registers;
  monitor_ops.to_prepare_to_store = monitor_prepare_to_store;
  monitor_ops.to_xfer_memory = monitor_xfer_memory;
  monitor_ops.to_files_info = monitor_files_info;
  monitor_ops.to_insert_breakpoint = monitor_insert_breakpoint;
  monitor_ops.to_remove_breakpoint = monitor_remove_breakpoint;
  monitor_ops.to_kill = monitor_kill;
  monitor_ops.to_load = monitor_load;
  monitor_ops.to_create_inferior = monitor_create_inferior;
  monitor_ops.to_mourn_inferior = monitor_mourn_inferior;
  monitor_ops.to_stop = monitor_stop;
  monitor_ops.to_rcmd = monitor_rcmd;
  monitor_ops.to_stratum = process_stratum;
  monitor_ops.to_has_all_memory = 1;
  monitor_ops.to_has_memory = 1;
  monitor_ops.to_has_stack = 1;
  monitor_ops.to_has_registers = 1;
  monitor_ops.to_has_execution = 1;
  monitor_ops.to_magic = OPS_MAGIC;
}				/* init_base_monitor_ops */

/* Init the target_ops structure pointed at by OPS */

void
init_monitor_ops (struct target_ops *ops)
{
  if (monitor_ops.to_magic != OPS_MAGIC)
    init_base_monitor_ops ();

  memcpy (ops, &monitor_ops, sizeof monitor_ops);
}

/* Define additional commands that are usually only used by monitors.  */

extern initialize_file_ftype _initialize_remote_monitors; /* -Wmissing-prototypes */

void
_initialize_remote_monitors (void)
{
  init_base_monitor_ops ();
  add_show_from_set (add_set_cmd ("hash", no_class, var_boolean,
				  (char *) &hashmark,
				  "Set display of activity while downloading a file.\n\
When enabled, a hashmark \'#\' is displayed.",
				  &setlist),
		     &showlist);

  add_show_from_set
    (add_set_cmd ("monitor", no_class, var_zinteger,
		  (char *) &monitor_debug_p,
		  "Set debugging of remote monitor communication.\n\
When enabled, communication between GDB and the remote monitor\n\
is displayed.", &setdebuglist),
     &showdebuglist);
}
