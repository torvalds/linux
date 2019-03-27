/* Remote debugging interface for Tandem ST2000 phone switch, for GDB.

   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1998, 1999, 2000,
   2001, 2002 Free Software Foundation, Inc.

   Contributed by Cygnus Support.  Written by Jim Kingdon for Cygnus.

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

/* This file was derived from remote-eb.c, which did a similar job, but for
   an AMD-29K running EBMON.  That file was in turn derived from remote.c
   as mentioned in the following comment (left in for comic relief):

   "This is like remote.c but is for an esoteric situation--
   having an a29k board in a PC hooked up to a unix machine with
   a serial line, and running ctty com1 on the PC, through which
   the unix machine can run ebmon.  Not to mention that the PC
   has PC/NFS, so it can access the same executables that gdb can,
   over the net in real time."

   In reality, this module talks to a debug monitor called 'STDEBUG', which
   runs in a phone switch.  We communicate with STDEBUG via either a direct
   serial line, or a TCP (or possibly TELNET) stream to a terminal multiplexor,
   which in turn talks to the phone switch. */

#include "defs.h"
#include "gdbcore.h"
#include "target.h"
#include "gdb_string.h"
#include <sys/types.h>
#include "serial.h"
#include "regcache.h"

extern struct target_ops st2000_ops;	/* Forward declaration */

static void st2000_close ();
static void st2000_fetch_register ();
static void st2000_store_register ();

#define LOG_FILE "st2000.log"
#if defined (LOG_FILE)
FILE *log_file;
#endif

static int timeout = 24;

/* Descriptor for I/O to remote machine.  Initialize it to -1 so that
   st2000_open knows that we don't have a file open when the program
   starts.  */

static struct serial *st2000_desc;

/* Send data to stdebug.  Works just like printf. */

static void
printf_stdebug (char *pattern,...)
{
  va_list args;
  char buf[200];

  va_start (args, pattern);

  vsprintf (buf, pattern, args);
  va_end (args);

  if (serial_write (st2000_desc, buf, strlen (buf)))
    fprintf_unfiltered (gdb_stderr, "serial_write failed: %s\n",
			safe_strerror (errno));
}

/* Read a character from the remote system, doing all the fancy timeout
   stuff.  */

static int
readchar (int timeout)
{
  int c;

  c = serial_readchar (st2000_desc, timeout);

#ifdef LOG_FILE
  putc (c & 0x7f, log_file);
#endif

  if (c >= 0)
    return c & 0x7f;

  if (c == SERIAL_TIMEOUT)
    {
      if (timeout == 0)
	return c;		/* Polls shouldn't generate timeout errors */

      error ("Timeout reading from remote system.");
    }

  perror_with_name ("remote-st2000");
}

/* Scan input from the remote system, until STRING is found.  If DISCARD is
   non-zero, then discard non-matching input, else print it out.
   Let the user break out immediately.  */
static void
expect (char *string, int discard)
{
  char *p = string;
  int c;

  immediate_quit++;
  while (1)
    {
      c = readchar (timeout);
      if (c == *p++)
	{
	  if (*p == '\0')
	    {
	      immediate_quit--;
	      return;
	    }
	}
      else
	{
	  if (!discard)
	    {
	      fwrite (string, 1, (p - 1) - string, stdout);
	      putchar ((char) c);
	      fflush (stdout);
	    }
	  p = string;
	}
    }
}

/* Keep discarding input until we see the STDEBUG prompt.

   The convention for dealing with the prompt is that you
   o give your command
   o *then* wait for the prompt.

   Thus the last thing that a procedure does with the serial line
   will be an expect_prompt().  Exception:  st2000_resume does not
   wait for the prompt, because the terminal is being handed over
   to the inferior.  However, the next thing which happens after that
   is a st2000_wait which does wait for the prompt.
   Note that this includes abnormal exit, e.g. error().  This is
   necessary to prevent getting into states from which we can't
   recover.  */
static void
expect_prompt (int discard)
{
#if defined (LOG_FILE)
  /* This is a convenient place to do this.  The idea is to do it often
     enough that we never lose much data if we terminate abnormally.  */
  fflush (log_file);
#endif
  expect ("dbug> ", discard);
}

/* Get a hex digit from the remote system & return its value.
   If ignore_space is nonzero, ignore spaces (not newline, tab, etc).  */
static int
get_hex_digit (int ignore_space)
{
  int ch;
  while (1)
    {
      ch = readchar (timeout);
      if (ch >= '0' && ch <= '9')
	return ch - '0';
      else if (ch >= 'A' && ch <= 'F')
	return ch - 'A' + 10;
      else if (ch >= 'a' && ch <= 'f')
	return ch - 'a' + 10;
      else if (ch == ' ' && ignore_space)
	;
      else
	{
	  expect_prompt (1);
	  error ("Invalid hex digit from remote system.");
	}
    }
}

/* Get a byte from stdebug and put it in *BYT.  Accept any number
   leading spaces.  */
static void
get_hex_byte (char *byt)
{
  int val;

  val = get_hex_digit (1) << 4;
  val |= get_hex_digit (0);
  *byt = val;
}

/* Get N 32-bit words from remote, each preceded by a space,
   and put them in registers starting at REGNO.  */
static void
get_hex_regs (int n, int regno)
{
  long val;
  int i;

  for (i = 0; i < n; i++)
    {
      int j;

      val = 0;
      for (j = 0; j < 8; j++)
	val = (val << 4) + get_hex_digit (j == 0);
      supply_register (regno++, (char *) &val);
    }
}

/* This is called not only when we first attach, but also when the
   user types "run" after having attached.  */
static void
st2000_create_inferior (char *execfile, char *args, char **env)
{
  int entry_pt;

  if (args && *args)
    error ("Can't pass arguments to remote STDEBUG process");

  if (execfile == 0 || exec_bfd == 0)
    error ("No executable file specified");

  entry_pt = (int) bfd_get_start_address (exec_bfd);

/* The "process" (board) is already stopped awaiting our commands, and
   the program is already downloaded.  We just set its PC and go.  */

  clear_proceed_status ();

  /* Tell wait_for_inferior that we've started a new process.  */
  init_wait_for_inferior ();

  /* Set up the "saved terminal modes" of the inferior
     based on what modes we are starting it with.  */
  target_terminal_init ();

  /* Install inferior's terminal modes.  */
  target_terminal_inferior ();

  /* insert_step_breakpoint ();  FIXME, do we need this?  */
  /* Let 'er rip... */
  proceed ((CORE_ADDR) entry_pt, TARGET_SIGNAL_DEFAULT, 0);
}

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */

static int baudrate = 9600;
static char dev_name[100];

static void
st2000_open (char *args, int from_tty)
{
  int n;
  char junk[100];

  target_preopen (from_tty);

  n = sscanf (args, " %s %d %s", dev_name, &baudrate, junk);

  if (n != 2)
    error ("Bad arguments.  Usage: target st2000 <device> <speed>\n\
or target st2000 <host> <port>\n");

  st2000_close (0);

  st2000_desc = serial_open (dev_name);

  if (!st2000_desc)
    perror_with_name (dev_name);

  if (serial_setbaudrate (st2000_desc, baudrate))
    {
      serial_close (dev_name);
      perror_with_name (dev_name);
    }

  serial_raw (st2000_desc);

  push_target (&st2000_ops);

#if defined (LOG_FILE)
  log_file = fopen (LOG_FILE, "w");
  if (log_file == NULL)
    perror_with_name (LOG_FILE);
#endif

  /* Hello?  Are you there?  */
  printf_stdebug ("\003");	/* ^C wakes up dbug */

  expect_prompt (1);

  if (from_tty)
    printf ("Remote %s connected to %s\n", target_shortname,
	    dev_name);
}

/* Close out all files and local state before this target loses control. */

static void
st2000_close (int quitting)
{
  serial_close (st2000_desc);

#if defined (LOG_FILE)
  if (log_file)
    {
      if (ferror (log_file))
	fprintf_unfiltered (gdb_stderr, "Error writing log file.\n");
      if (fclose (log_file) != 0)
	fprintf_unfiltered (gdb_stderr, "Error closing log file.\n");
    }
#endif
}

/* Terminate the open connection to the remote debugger.
   Use this when you want to detach and do something else
   with your gdb.  */
static void
st2000_detach (int from_tty)
{
  pop_target ();		/* calls st2000_close to do the real work */
  if (from_tty)
    printf ("Ending remote %s debugging\n", target_shortname);
}

/* Tell the remote machine to resume.  */

static void
st2000_resume (ptid_t ptid, int step, enum target_signal sig)
{
  if (step)
    {
      printf_stdebug ("ST\r");
      /* Wait for the echo.  */
      expect ("ST\r", 1);
    }
  else
    {
      printf_stdebug ("GO\r");
      /* Swallow the echo.  */
      expect ("GO\r", 1);
    }
}

/* Wait until the remote machine stops, then return,
   storing status in STATUS just as `wait' would.  */

static ptid_t
st2000_wait (ptid_t ptid, struct target_waitstatus *status)
{
  int old_timeout = timeout;

  status->kind = TARGET_WAITKIND_EXITED;
  status->value.integer = 0;

  timeout = 0;			/* Don't time out -- user program is running. */

  expect_prompt (0);		/* Wait for prompt, outputting extraneous text */

  status->kind = TARGET_WAITKIND_STOPPED;
  status->value.sig = TARGET_SIGNAL_TRAP;

  timeout = old_timeout;

  return inferior_ptid;
}

/* Return the name of register number REGNO in the form input and
   output by STDEBUG.  Currently, REGISTER_NAME just happens return
   exactly what STDEBUG wants.  Lets take advantage of that just as
   long as possible! */

static char *
get_reg_name (int regno)
{
  static char buf[50];
  const char *p;
  char *b;

  b = buf;

  for (p = REGISTER_NAME (regno); *p; p++)
    *b++ = toupper (*p);
  *b = '\000';

  return buf;
}

/* Read the remote registers into the block REGS.  */

static void
st2000_fetch_registers (void)
{
  int regno;

  /* Yeah yeah, I know this is horribly inefficient.  But it isn't done
     very often...  I'll clean it up later.  */

  for (regno = 0; regno <= PC_REGNUM; regno++)
    st2000_fetch_register (regno);
}

/* Fetch register REGNO, or all registers if REGNO is -1.
   Returns errno value.  */
static void
st2000_fetch_register (int regno)
{
  if (regno == -1)
    st2000_fetch_registers ();
  else
    {
      char *name = get_reg_name (regno);
      printf_stdebug ("DR %s\r", name);
      expect (name, 1);
      expect (" : ", 1);
      get_hex_regs (1, regno);
      expect_prompt (1);
    }
  return;
}

/* Store the remote registers from the contents of the block REGS.  */

static void
st2000_store_registers (void)
{
  int regno;

  for (regno = 0; regno <= PC_REGNUM; regno++)
    st2000_store_register (regno);

  registers_changed ();
}

/* Store register REGNO, or all if REGNO == 0.
   Return errno value.  */
static void
st2000_store_register (int regno)
{
  if (regno == -1)
    st2000_store_registers ();
  else
    {
      printf_stdebug ("PR %s %x\r", get_reg_name (regno),
		      read_register (regno));

      expect_prompt (1);
    }
}

/* Get ready to modify the registers array.  On machines which store
   individual registers, this doesn't need to do anything.  On machines
   which store all the registers in one fell swoop, this makes sure
   that registers contains all the registers from the program being
   debugged.  */

static void
st2000_prepare_to_store (void)
{
  /* Do nothing, since we can store individual regs */
}

static void
st2000_files_info (void)
{
  printf ("\tAttached to %s at %d baud.\n",
	  dev_name, baudrate);
}

/* Copy LEN bytes of data from debugger memory at MYADDR
   to inferior's memory at MEMADDR.  Returns length moved.  */
static int
st2000_write_inferior_memory (CORE_ADDR memaddr, unsigned char *myaddr, int len)
{
  int i;

  for (i = 0; i < len; i++)
    {
      printf_stdebug ("PM.B %x %x\r", memaddr + i, myaddr[i]);
      expect_prompt (1);
    }
  return len;
}

/* Read LEN bytes from inferior memory at MEMADDR.  Put the result
   at debugger address MYADDR.  Returns length moved.  */
static int
st2000_read_inferior_memory (CORE_ADDR memaddr, char *myaddr, int len)
{
  int i;

  /* Number of bytes read so far.  */
  int count;

  /* Starting address of this pass.  */
  unsigned long startaddr;

  /* Number of bytes to read in this pass.  */
  int len_this_pass;

  /* Note that this code works correctly if startaddr is just less
     than UINT_MAX (well, really CORE_ADDR_MAX if there was such a
     thing).  That is, something like
     st2000_read_bytes (CORE_ADDR_MAX - 4, foo, 4)
     works--it never adds len to memaddr and gets 0.  */
  /* However, something like
     st2000_read_bytes (CORE_ADDR_MAX - 3, foo, 4)
     doesn't need to work.  Detect it and give up if there's an attempt
     to do that.  */
  if (((memaddr - 1) + len) < memaddr)
    {
      errno = EIO;
      return 0;
    }

  startaddr = memaddr;
  count = 0;
  while (count < len)
    {
      len_this_pass = 16;
      if ((startaddr % 16) != 0)
	len_this_pass -= startaddr % 16;
      if (len_this_pass > (len - count))
	len_this_pass = (len - count);

      printf_stdebug ("DI.L %x %x\r", startaddr, len_this_pass);
      expect (":  ", 1);

      for (i = 0; i < len_this_pass; i++)
	get_hex_byte (&myaddr[count++]);

      expect_prompt (1);

      startaddr += len_this_pass;
    }
  return len;
}

/* Transfer LEN bytes between GDB address MYADDR and target address
   MEMADDR.  If WRITE is non-zero, transfer them to the target,
   otherwise transfer them from the target.  TARGET is unused.

   Returns the number of bytes transferred. */

static int
st2000_xfer_inferior_memory (CORE_ADDR memaddr, char *myaddr, int len,
			     int write, struct mem_attrib *attrib,
			     struct target_ops *target)
{
  if (write)
    return st2000_write_inferior_memory (memaddr, myaddr, len);
  else
    return st2000_read_inferior_memory (memaddr, myaddr, len);
}

static void
st2000_kill (char *args, int from_tty)
{
  return;			/* Ignore attempts to kill target system */
}

/* Clean up when a program exits.

   The program actually lives on in the remote processor's RAM, and may be
   run again without a download.  Don't leave it full of breakpoint
   instructions.  */

static void
st2000_mourn_inferior (void)
{
  remove_breakpoints ();
  unpush_target (&st2000_ops);
  generic_mourn_inferior ();	/* Do all the proper things now */
}

#define MAX_STDEBUG_BREAKPOINTS 16

static CORE_ADDR breakaddr[MAX_STDEBUG_BREAKPOINTS] =
{0};

static int
st2000_insert_breakpoint (CORE_ADDR addr, char *shadow)
{
  int i;
  CORE_ADDR bp_addr = addr;
  int bp_size = 0;

  BREAKPOINT_FROM_PC (&bp_addr, &bp_size);

  for (i = 0; i <= MAX_STDEBUG_BREAKPOINTS; i++)
    if (breakaddr[i] == 0)
      {
	breakaddr[i] = addr;

	st2000_read_inferior_memory (bp_addr, shadow, bp_size);
	printf_stdebug ("BR %x H\r", addr);
	expect_prompt (1);
	return 0;
      }

  fprintf_unfiltered (gdb_stderr, "Too many breakpoints (> 16) for STDBUG\n");
  return 1;
}

static int
st2000_remove_breakpoint (CORE_ADDR addr, char *shadow)
{
  int i;

  for (i = 0; i < MAX_STDEBUG_BREAKPOINTS; i++)
    if (breakaddr[i] == addr)
      {
	breakaddr[i] = 0;

	printf_stdebug ("CB %d\r", i);
	expect_prompt (1);
	return 0;
      }

  fprintf_unfiltered (gdb_stderr,
		      "Can't find breakpoint associated with 0x%x\n", addr);
  return 1;
}


/* Put a command string, in args, out to STDBUG.  Output from STDBUG is placed
   on the users terminal until the prompt is seen. */

static void
st2000_command (char *args, int fromtty)
{
  if (!st2000_desc)
    error ("st2000 target not open.");

  if (!args)
    error ("Missing command.");

  printf_stdebug ("%s\r", args);
  expect_prompt (0);
}

/* Connect the user directly to STDBUG.  This command acts just like the
   'cu' or 'tip' command.  Use <CR>~. or <CR>~^D to break out.  */

/*static struct ttystate ttystate; */

static void
cleanup_tty (void)
{
  printf ("\r\n[Exiting connect mode]\r\n");
/*  serial_restore(0, &ttystate); */
}

#if 0
/* This all should now be in serial.c */

static void
connect_command (char *args, int fromtty)
{
  fd_set readfds;
  int numfds;
  int c;
  char cur_esc = 0;

  dont_repeat ();

  if (st2000_desc < 0)
    error ("st2000 target not open.");

  if (args)
    fprintf ("This command takes no args.  They have been ignored.\n");

  printf ("[Entering connect mode.  Use ~. or ~^D to escape]\n");

  serial_raw (0, &ttystate);

  make_cleanup (cleanup_tty, 0);

  FD_ZERO (&readfds);

  while (1)
    {
      do
	{
	  FD_SET (0, &readfds);
	  FD_SET (deprecated_serial_fd (st2000_desc), &readfds);
	  numfds = select (sizeof (readfds) * 8, &readfds, 0, 0, 0);
	}
      while (numfds == 0);

      if (numfds < 0)
	perror_with_name ("select");

      if (FD_ISSET (0, &readfds))
	{			/* tty input, send to stdebug */
	  c = getchar ();
	  if (c < 0)
	    perror_with_name ("connect");

	  printf_stdebug ("%c", c);
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

      if (FD_ISSET (deprecated_serial_fd (st2000_desc), &readfds))
	{
	  while (1)
	    {
	      c = readchar (0);
	      if (c < 0)
		break;
	      putchar (c);
	    }
	  fflush (stdout);
	}
    }
}
#endif /* 0 */

/* Define the target subroutine names */

struct target_ops st2000_ops;

static void
init_st2000_ops (void)
{
  st2000_ops.to_shortname = "st2000";
  st2000_ops.to_longname = "Remote serial Tandem ST2000 target";
  st2000_ops.to_doc = "Use a remote computer running STDEBUG connected by a serial line;\n\
or a network connection.\n\
Arguments are the name of the device for the serial line,\n\
the speed to connect at in bits per second.";
  st2000_ops.to_open = st2000_open;
  st2000_ops.to_close = st2000_close;
  st2000_ops.to_detach = st2000_detach;
  st2000_ops.to_resume = st2000_resume;
  st2000_ops.to_wait = st2000_wait;
  st2000_ops.to_fetch_registers = st2000_fetch_register;
  st2000_ops.to_store_registers = st2000_store_register;
  st2000_ops.to_prepare_to_store = st2000_prepare_to_store;
  st2000_ops.to_xfer_memory = st2000_xfer_inferior_memory;
  st2000_ops.to_files_info = st2000_files_info;
  st2000_ops.to_insert_breakpoint = st2000_insert_breakpoint;
  st2000_ops.to_remove_breakpoint = st2000_remove_breakpoint;	/* Breakpoints */
  st2000_ops.to_kill = st2000_kill;
  st2000_ops.to_create_inferior = st2000_create_inferior;
  st2000_ops.to_mourn_inferior = st2000_mourn_inferior;
  st2000_ops.to_stratum = process_stratum;
  st2000_ops.to_has_all_memory = 1;
  st2000_ops.to_has_memory = 1;
  st2000_ops.to_has_stack = 1;
  st2000_ops.to_has_registers = 1;
  st2000_ops.to_has_execution = 1;	/* all mem, mem, stack, regs, exec */
  st2000_ops.to_magic = OPS_MAGIC;	/* Always the last thing */
};

void
_initialize_remote_st2000 (void)
{
  init_st2000_ops ();
  add_target (&st2000_ops);
  add_com ("st2000 <command>", class_obscure, st2000_command,
	   "Send a command to the STDBUG monitor.");
  add_com ("connect", class_obscure, connect_command,
	   "Connect the terminal directly up to the STDBUG command monitor.\n\
Use <CR>~. or <CR>~^D to break out.");
}
