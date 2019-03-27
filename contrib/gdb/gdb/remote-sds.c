/* Remote target communications for serial-line targets using SDS' protocol.

   Copyright 1997, 1998, 1999, 2000, 2001, 2002, 2004 Free Software
   Foundation, Inc.

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

/* This interface was written by studying the behavior of the SDS
   monitor on an ADS 821/860 board, and by consulting the
   documentation of the monitor that is available on Motorola's web
   site.  -sts 8/13/97 */

#include "defs.h"
#include "gdb_string.h"
#include <fcntl.h>
#include "frame.h"
#include "inferior.h"
#include "bfd.h"
#include "symfile.h"
#include "target.h"
#include "gdbcmd.h"
#include "objfiles.h"
#include "gdb-stabs.h"
#include "gdbthread.h"
#include "gdbcore.h"
#include "regcache.h"

#ifdef USG
#include <sys/types.h>
#endif

#include <signal.h>
#include "serial.h"

extern void _initialize_remote_sds (void);

/* Declarations of local functions. */

static int sds_write_bytes (CORE_ADDR, char *, int);

static int sds_read_bytes (CORE_ADDR, char *, int);

static void sds_files_info (struct target_ops *ignore);

static int sds_xfer_memory (CORE_ADDR, char *, int, int, 
			    struct mem_attrib *, struct target_ops *);

static void sds_prepare_to_store (void);

static void sds_fetch_registers (int);

static void sds_resume (ptid_t, int, enum target_signal);

static int sds_start_remote (void *);

static void sds_open (char *, int);

static void sds_close (int);

static void sds_store_registers (int);

static void sds_mourn (void);

static void sds_create_inferior (char *, char *, char **);

static void sds_load (char *, int);

static int getmessage (unsigned char *, int);

static int putmessage (unsigned char *, int);

static int sds_send (unsigned char *, int);

static int readchar (int);

static ptid_t sds_wait (ptid_t, struct target_waitstatus *);

static void sds_kill (void);

static int fromhex (int);

static void sds_detach (char *, int);

static void sds_interrupt (int);

static void sds_interrupt_twice (int);

static void interrupt_query (void);

static int read_frame (char *);

static int sds_insert_breakpoint (CORE_ADDR, char *);

static int sds_remove_breakpoint (CORE_ADDR, char *);

static void init_sds_ops (void);

static void sds_command (char *args, int from_tty);

/* Define the target operations vector. */

static struct target_ops sds_ops;

/* This was 5 seconds, which is a long time to sit and wait.
   Unless this is going though some terminal server or multiplexer or
   other form of hairy serial connection, I would think 2 seconds would
   be plenty.  */

static int sds_timeout = 2;

/* Descriptor for I/O to remote machine.  Initialize it to NULL so
   that sds_open knows that we don't have a file open when the program
   starts.  */

static struct serial *sds_desc = NULL;

/* This limit comes from the monitor.  */

#define	PBUFSIZ	250

/* Maximum number of bytes to read/write at once.  The value here
   is chosen to fill up a packet (the headers account for the 32).  */
#define MAXBUFBYTES ((PBUFSIZ-32)/2)

static int next_msg_id;

static int just_started;

static int message_pending;


/* Clean up connection to a remote debugger.  */

static void
sds_close (int quitting)
{
  if (sds_desc)
    serial_close (sds_desc);
  sds_desc = NULL;
}

/* Stub for catch_errors.  */

static int
sds_start_remote (void *dummy)
{
  int c;
  unsigned char buf[200];

  immediate_quit++;		/* Allow user to interrupt it */

  /* Ack any packet which the remote side has already sent.  */
  serial_write (sds_desc, "{#*\r\n", 5);
  serial_write (sds_desc, "{#}\r\n", 5);

  while ((c = readchar (1)) >= 0)
    printf_unfiltered ("%c", c);
  printf_unfiltered ("\n");

  next_msg_id = 251;

  buf[0] = 26;
  sds_send (buf, 1);

  buf[0] = 0;
  sds_send (buf, 1);

  immediate_quit--;

  start_remote ();		/* Initialize gdb process mechanisms */
  return 1;
}

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */

static void
sds_open (char *name, int from_tty)
{
  if (name == 0)
    error ("To open a remote debug connection, you need to specify what serial\n\
device is attached to the remote system (e.g. /dev/ttya).");

  target_preopen (from_tty);

  unpush_target (&sds_ops);

  sds_desc = serial_open (name);
  if (!sds_desc)
    perror_with_name (name);

  if (baud_rate != -1)
    {
      if (serial_setbaudrate (sds_desc, baud_rate))
	{
	  serial_close (sds_desc);
	  perror_with_name (name);
	}
    }


  serial_raw (sds_desc);

  /* If there is something sitting in the buffer we might take it as a
     response to a command, which would be bad.  */
  serial_flush_input (sds_desc);

  if (from_tty)
    {
      puts_filtered ("Remote debugging using ");
      puts_filtered (name);
      puts_filtered ("\n");
    }
  push_target (&sds_ops);	/* Switch to using remote target now */

  just_started = 1;

  /* Start the remote connection; if error (0), discard this target.
     In particular, if the user quits, be sure to discard it (we'd be
     in an inconsistent state otherwise).  */
  if (!catch_errors (sds_start_remote, NULL,
		     "Couldn't establish connection to remote target\n",
		     RETURN_MASK_ALL))
    pop_target ();
}

/* This takes a program previously attached to and detaches it.  After
   this is done, GDB can be used to debug some other program.  We
   better not have left any breakpoints in the target program or it'll
   die when it hits one.  */

static void
sds_detach (char *args, int from_tty)
{
  char buf[PBUFSIZ];

  if (args)
    error ("Argument given to \"detach\" when remotely debugging.");

#if 0
  /* Tell the remote target to detach.  */
  strcpy (buf, "D");
  sds_send (buf, 1);
#endif

  pop_target ();
  if (from_tty)
    puts_filtered ("Ending remote debugging.\n");
}

/* Convert hex digit A to a number.  */

static int
fromhex (int a)
{
  if (a >= '0' && a <= '9')
    return a - '0';
  else if (a >= 'a' && a <= 'f')
    return a - 'a' + 10;
  else
    error ("Reply contains invalid hex digit %d", a);
}

static int
tob64 (unsigned char *inbuf, char *outbuf, int len)
{
  int i, sum;
  char *p;

  if (len % 3 != 0)
    error ("bad length");

  p = outbuf;
  for (i = 0; i < len; i += 3)
    {
      /* Collect the next three bytes into a number.  */
      sum = ((long) *inbuf++) << 16;
      sum |= ((long) *inbuf++) << 8;
      sum |= ((long) *inbuf++);

      /* Spit out 4 6-bit encodings.  */
      *p++ = ((sum >> 18) & 0x3f) + '0';
      *p++ = ((sum >> 12) & 0x3f) + '0';
      *p++ = ((sum >> 6) & 0x3f) + '0';
      *p++ = (sum & 0x3f) + '0';
    }
  return (p - outbuf);
}

static int
fromb64 (char *inbuf, char *outbuf, int len)
{
  int i, sum;

  if (len % 4 != 0)
    error ("bad length");

  for (i = 0; i < len; i += 4)
    {
      /* Collect 4 6-bit digits.  */
      sum = (*inbuf++ - '0') << 18;
      sum |= (*inbuf++ - '0') << 12;
      sum |= (*inbuf++ - '0') << 6;
      sum |= (*inbuf++ - '0');

      /* Now take the resulting 24-bit number and get three bytes out
         of it.  */
      *outbuf++ = (sum >> 16) & 0xff;
      *outbuf++ = (sum >> 8) & 0xff;
      *outbuf++ = sum & 0xff;
    }

  return (len / 4) * 3;
}


/* Tell the remote machine to resume.  */

static enum target_signal last_sent_signal = TARGET_SIGNAL_0;
int last_sent_step;

static void
sds_resume (ptid_t ptid, int step, enum target_signal siggnal)
{
  unsigned char buf[PBUFSIZ];

  last_sent_signal = siggnal;
  last_sent_step = step;

  buf[0] = (step ? 21 : 20);
  buf[1] = 0;			/* (should be signal?) */

  sds_send (buf, 2);
}

/* Send a message to target to halt it.  Target will respond, and send
   us a message pending notice.  */

static void
sds_interrupt (int signo)
{
  unsigned char buf[PBUFSIZ];

  /* If this doesn't work, try more severe steps.  */
  signal (signo, sds_interrupt_twice);

  if (remote_debug)
    fprintf_unfiltered (gdb_stdlog, "sds_interrupt called\n");

  buf[0] = 25;
  sds_send (buf, 1);
}

static void (*ofunc) ();

/* The user typed ^C twice.  */

static void
sds_interrupt_twice (int signo)
{
  signal (signo, ofunc);

  interrupt_query ();

  signal (signo, sds_interrupt);
}

/* Ask the user what to do when an interrupt is received.  */

static void
interrupt_query (void)
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

/* If nonzero, ignore the next kill.  */
int kill_kludge;

/* Wait until the remote machine stops, then return, storing status in
   STATUS just as `wait' would.  Returns "pid" (though it's not clear
   what, if anything, that means in the case of this target).  */

static ptid_t
sds_wait (ptid_t ptid, struct target_waitstatus *status)
{
  unsigned char buf[PBUFSIZ];
  int retlen;

  status->kind = TARGET_WAITKIND_EXITED;
  status->value.integer = 0;

  ofunc = (void (*)()) signal (SIGINT, sds_interrupt);

  signal (SIGINT, ofunc);

  if (just_started)
    {
      just_started = 0;
      status->kind = TARGET_WAITKIND_STOPPED;
      return inferior_ptid;
    }

  while (1)
    {
      getmessage (buf, 1);

      if (message_pending)
	{
	  buf[0] = 26;
	  retlen = sds_send (buf, 1);
	  if (remote_debug)
	    {
	      fprintf_unfiltered (gdb_stdlog, "Signals: %02x%02x %02x %02x\n",
				  buf[0], buf[1],
				  buf[2], buf[3]);
	    }
	  message_pending = 0;
	  status->kind = TARGET_WAITKIND_STOPPED;
	  status->value.sig = TARGET_SIGNAL_TRAP;
	  goto got_status;
	}
    }
got_status:
  return inferior_ptid;
}

static unsigned char sprs[16];

/* Read the remote registers into the block REGS.  */
/* Currently we just read all the registers, so we don't use regno.  */

static void
sds_fetch_registers (int regno)
{
  unsigned char buf[PBUFSIZ];
  int i, retlen;
  char *regs = alloca (DEPRECATED_REGISTER_BYTES);

  /* Unimplemented registers read as all bits zero.  */
  memset (regs, 0, DEPRECATED_REGISTER_BYTES);

  buf[0] = 18;
  buf[1] = 1;
  buf[2] = 0;
  retlen = sds_send (buf, 3);

  for (i = 0; i < 4 * 6; ++i)
    regs[i + 4 * 32 + 8 * 32] = buf[i];
  for (i = 0; i < 4 * 4; ++i)
    sprs[i] = buf[i + 4 * 7];

  buf[0] = 18;
  buf[1] = 2;
  buf[2] = 0;
  retlen = sds_send (buf, 3);

  for (i = 0; i < retlen; i++)
    regs[i] = buf[i];

  /* (should warn about reply too short) */

  for (i = 0; i < NUM_REGS; i++)
    supply_register (i, &regs[DEPRECATED_REGISTER_BYTE (i)]);
}

/* Prepare to store registers.  Since we may send them all, we have to
   read out the ones we don't want to change first.  */

static void
sds_prepare_to_store (void)
{
  /* Make sure the entire registers array is valid.  */
  deprecated_read_register_bytes (0, (char *) NULL, DEPRECATED_REGISTER_BYTES);
}

/* Store register REGNO, or all registers if REGNO == -1, from the contents
   of REGISTERS.  FIXME: ignores errors.  */

static void
sds_store_registers (int regno)
{
  unsigned char *p, buf[PBUFSIZ];
  int i;

  /* Store all the special-purpose registers.  */
  p = buf;
  *p++ = 19;
  *p++ = 1;
  *p++ = 0;
  *p++ = 0;
  for (i = 0; i < 4 * 6; i++)
    *p++ = deprecated_registers[i + 4 * 32 + 8 * 32];
  for (i = 0; i < 4 * 1; i++)
    *p++ = 0;
  for (i = 0; i < 4 * 4; i++)
    *p++ = sprs[i];

  sds_send (buf, p - buf);

  /* Store all the general-purpose registers.  */
  p = buf;
  *p++ = 19;
  *p++ = 2;
  *p++ = 0;
  *p++ = 0;
  for (i = 0; i < 4 * 32; i++)
    *p++ = deprecated_registers[i];

  sds_send (buf, p - buf);

}

/* Write memory data directly to the remote machine.  This does not
   inform the data cache; the data cache uses this.  MEMADDR is the
   address in the remote memory space.  MYADDR is the address of the
   buffer in our space.  LEN is the number of bytes.

   Returns number of bytes transferred, or 0 for error.  */

static int
sds_write_bytes (CORE_ADDR memaddr, char *myaddr, int len)
{
  int max_buf_size;		/* Max size of packet output buffer */
  int origlen;
  unsigned char buf[PBUFSIZ];
  int todo;
  int i;

  /* Chop the transfer down if necessary */

  max_buf_size = 150;

  origlen = len;
  while (len > 0)
    {
      todo = min (len, max_buf_size);

      buf[0] = 13;
      buf[1] = 0;
      buf[2] = (int) (memaddr >> 24) & 0xff;
      buf[3] = (int) (memaddr >> 16) & 0xff;
      buf[4] = (int) (memaddr >> 8) & 0xff;
      buf[5] = (int) (memaddr) & 0xff;
      buf[6] = 1;
      buf[7] = 0;

      for (i = 0; i < todo; i++)
	buf[i + 8] = myaddr[i];

      sds_send (buf, 8 + todo);

      /* (should look at result) */

      myaddr += todo;
      memaddr += todo;
      len -= todo;
    }
  return origlen;
}

/* Read memory data directly from the remote machine.  This does not
   use the data cache; the data cache uses this.  MEMADDR is the
   address in the remote memory space.  MYADDR is the address of the
   buffer in our space.  LEN is the number of bytes.

   Returns number of bytes transferred, or 0 for error.  */

static int
sds_read_bytes (CORE_ADDR memaddr, char *myaddr, int len)
{
  int max_buf_size;		/* Max size of packet output buffer */
  int origlen, retlen;
  unsigned char buf[PBUFSIZ];
  int todo;
  int i;

  /* Chop the transfer down if necessary */

  max_buf_size = 150;

  origlen = len;
  while (len > 0)
    {
      todo = min (len, max_buf_size);

      buf[0] = 12;
      buf[1] = 0;
      buf[2] = (int) (memaddr >> 24) & 0xff;
      buf[3] = (int) (memaddr >> 16) & 0xff;
      buf[4] = (int) (memaddr >> 8) & 0xff;
      buf[5] = (int) (memaddr) & 0xff;
      buf[6] = (int) (todo >> 8) & 0xff;
      buf[7] = (int) (todo) & 0xff;
      buf[8] = 1;

      retlen = sds_send (buf, 9);

      if (retlen - 2 != todo)
	{
	  return 0;
	}

      /* Reply describes memory byte by byte. */

      for (i = 0; i < todo; i++)
	myaddr[i] = buf[i + 2];

      myaddr += todo;
      memaddr += todo;
      len -= todo;
    }

  return origlen;
}

/* Read or write LEN bytes from inferior memory at MEMADDR,
   transferring to or from debugger address MYADDR.  Write to inferior
   if SHOULD_WRITE is nonzero.  Returns length of data written or
   read; 0 for error.  TARGET is unused.  */

static int
sds_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int should_write,
		 struct mem_attrib *attrib, struct target_ops *target)
{
  int res;

  if (should_write)
    res = sds_write_bytes (memaddr, myaddr, len);
  else
    res = sds_read_bytes (memaddr, myaddr, len);
  
  return res;
}


static void
sds_files_info (struct target_ops *ignore)
{
  puts_filtered ("Debugging over a serial connection, using SDS protocol.\n");
}

/* Stuff for dealing with the packets which are part of this protocol.
   See comment at top of file for details.  */

/* Read a single character from the remote end, masking it down to 7 bits. */

static int
readchar (int timeout)
{
  int ch;

  ch = serial_readchar (sds_desc, timeout);

  if (remote_debug > 1 && ch >= 0)
    fprintf_unfiltered (gdb_stdlog, "%c(%x)", ch, ch);

  switch (ch)
    {
    case SERIAL_EOF:
      error ("Remote connection closed");
    case SERIAL_ERROR:
      perror_with_name ("Remote communication error");
    case SERIAL_TIMEOUT:
      return ch;
    default:
      return ch & 0x7f;
    }
}

/* An SDS-style checksum is a sum of the bytes modulo 253.  (Presumably
   because 253, 254, and 255 are special flags in the protocol.)  */

static int
compute_checksum (int csum, char *buf, int len)
{
  int i;

  for (i = 0; i < len; ++i)
    csum += (unsigned char) buf[i];

  csum %= 253;
  return csum;
}

/* Send the command in BUF to the remote machine, and read the reply
   into BUF also.  */

static int
sds_send (unsigned char *buf, int len)
{
  putmessage (buf, len);

  return getmessage (buf, 0);
}

/* Send a message to the remote machine.  */

static int
putmessage (unsigned char *buf, int len)
{
  int i, enclen;
  unsigned char csum = 0;
  char buf2[PBUFSIZ], buf3[PBUFSIZ];
  unsigned char header[3];
  char *p;

  /* Copy the packet into buffer BUF2, encapsulating it
     and giving it a checksum.  */

  if (len > 170)		/* Prosanity check */
    internal_error (__FILE__, __LINE__, "failed internal consistency check");

  if (remote_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "Message to send: \"");
      for (i = 0; i < len; ++i)
	fprintf_unfiltered (gdb_stdlog, "%02x", buf[i]);
      fprintf_unfiltered (gdb_stdlog, "\"\n");
    }

  p = buf2;
  *p++ = '$';

  if (len % 3 != 0)
    {
      buf[len] = '\0';
      buf[len + 1] = '\0';
    }

  header[1] = next_msg_id;

  header[2] = len;

  csum = compute_checksum (csum, buf, len);
  csum = compute_checksum (csum, header + 1, 2);

  header[0] = csum;

  tob64 (header, p, 3);
  p += 4;
  enclen = tob64 (buf, buf3, ((len + 2) / 3) * 3);

  for (i = 0; i < enclen; ++i)
    *p++ = buf3[i];
  *p++ = '\r';
  *p++ = '\n';

  next_msg_id = (next_msg_id + 3) % 245;

  /* Send it over and over until we get a positive ack.  */

  while (1)
    {
      if (remote_debug)
	{
	  *p = '\0';
	  fprintf_unfiltered (gdb_stdlog, "Sending encoded: \"%s\"", buf2);
	  fprintf_unfiltered (gdb_stdlog,
			      "  (Checksum %d, id %d, length %d)\n",
			      header[0], header[1], header[2]);
	  gdb_flush (gdb_stdlog);
	}
      if (serial_write (sds_desc, buf2, p - buf2))
	perror_with_name ("putmessage: write failed");

      return 1;
    }
}

/* Come here after finding the start of the frame.  Collect the rest
   into BUF.  Returns 0 on any error, 1 on success.  */

static int
read_frame (char *buf)
{
  char *bp;
  int c;

  bp = buf;

  while (1)
    {
      c = readchar (sds_timeout);

      switch (c)
	{
	case SERIAL_TIMEOUT:
	  if (remote_debug)
	    fputs_filtered ("Timeout in mid-message, retrying\n", gdb_stdlog);
	  return 0;
	case '$':
	  if (remote_debug)
	    fputs_filtered ("Saw new packet start in middle of old one\n",
			    gdb_stdlog);
	  return 0;		/* Start a new packet, count retries */
	case '\r':
	  break;

	case '\n':
	  {
	    *bp = '\000';
	    if (remote_debug)
	      fprintf_unfiltered (gdb_stdlog, "Received encoded: \"%s\"\n",
				  buf);
	    return 1;
	  }

	default:
	  if (bp < buf + PBUFSIZ - 1)
	    {
	      *bp++ = c;
	      continue;
	    }

	  *bp = '\0';
	  puts_filtered ("Message too long: ");
	  puts_filtered (buf);
	  puts_filtered ("\n");

	  return 0;
	}
    }
}

/* Read a packet from the remote machine, with error checking,
   and store it in BUF.  BUF is expected to be of size PBUFSIZ.
   If FOREVER, wait forever rather than timing out; this is used
   while the target is executing user code.  */

static int
getmessage (unsigned char *buf, int forever)
{
  int c, c2, c3;
  int tries;
  int timeout;
  int val, i, len, csum;
  unsigned char header[3];
  unsigned char inbuf[500];

  strcpy (buf, "timeout");

  if (forever)
    {
      timeout = watchdog > 0 ? watchdog : -1;
    }

  else
    timeout = sds_timeout;

#define MAX_TRIES 3

  for (tries = 1; tries <= MAX_TRIES; tries++)
    {
      /* This can loop forever if the remote side sends us characters
         continuously, but if it pauses, we'll get a zero from readchar
         because of timeout.  Then we'll count that as a retry.  */

      /* Note that we will only wait forever prior to the start of a packet.
         After that, we expect characters to arrive at a brisk pace.  They
         should show up within sds_timeout intervals.  */

      do
	{
	  c = readchar (timeout);

	  if (c == SERIAL_TIMEOUT)
	    {
	      if (forever)	/* Watchdog went off.  Kill the target. */
		{
		  target_mourn_inferior ();
		  error ("Watchdog has expired.  Target detached.\n");
		}
	      if (remote_debug)
		fputs_filtered ("Timed out.\n", gdb_stdlog);
	      goto retry;
	    }
	}
      while (c != '$' && c != '{');

      /* We might have seen a "trigraph", a sequence of three characters
         that indicate various sorts of communication state.  */

      if (c == '{')
	{
	  /* Read the other two chars of the trigraph. */
	  c2 = readchar (timeout);
	  c3 = readchar (timeout);
	  if (remote_debug)
	    fprintf_unfiltered (gdb_stdlog, "Trigraph %c%c%c received\n",
				c, c2, c3);
	  if (c3 == '+')
	    {
	      message_pending = 1;
	      return 0;		/*???? */
	    }
	  continue;
	}

      val = read_frame (inbuf);

      if (val == 1)
	{
	  fromb64 (inbuf, header, 4);
	  /* (should check out other bits) */
	  fromb64 (inbuf + 4, buf, strlen (inbuf) - 4);

	  len = header[2];

	  csum = 0;
	  csum = compute_checksum (csum, buf, len);
	  csum = compute_checksum (csum, header + 1, 2);

	  if (csum != header[0])
	    fprintf_unfiltered (gdb_stderr,
			    "Checksum mismatch: computed %d, received %d\n",
				csum, header[0]);

	  if (header[2] == 0xff)
	    fprintf_unfiltered (gdb_stderr, "Requesting resend...\n");

	  if (remote_debug)
	    {
	      fprintf_unfiltered (gdb_stdlog,
				"... (Got checksum %d, id %d, length %d)\n",
				  header[0], header[1], header[2]);
	      fprintf_unfiltered (gdb_stdlog, "Message received: \"");
	      for (i = 0; i < len; ++i)
		{
		  fprintf_unfiltered (gdb_stdlog, "%02x", (unsigned char) buf[i]);
		}
	      fprintf_unfiltered (gdb_stdlog, "\"\n");
	    }

	  /* no ack required? */
	  return len;
	}

      /* Try the whole thing again.  */
    retry:
      /* need to do something here */
      ;
    }

  /* We have tried hard enough, and just can't receive the packet.  Give up. */

  printf_unfiltered ("Ignoring packet error, continuing...\n");
  return 0;
}

static void
sds_kill (void)
{
  /* Don't try to do anything to the target.  */
}

static void
sds_mourn (void)
{
  unpush_target (&sds_ops);
  generic_mourn_inferior ();
}

static void
sds_create_inferior (char *exec_file, char *args, char **env)
{
  inferior_ptid = pid_to_ptid (42000);

  /* Clean up from the last time we were running.  */
  clear_proceed_status ();

  /* Let the remote process run.  */
  proceed (bfd_get_start_address (exec_bfd), TARGET_SIGNAL_0, 0);
}

static void
sds_load (char *filename, int from_tty)
{
  generic_load (filename, from_tty);

  inferior_ptid = null_ptid;
}

/* The SDS monitor has commands for breakpoint insertion, although it
   it doesn't actually manage the breakpoints, it just returns the
   replaced instruction back to the debugger.  */

static int
sds_insert_breakpoint (CORE_ADDR addr, char *contents_cache)
{
  int i, retlen;
  unsigned char *p, buf[PBUFSIZ];

  p = buf;
  *p++ = 16;
  *p++ = 0;
  *p++ = (int) (addr >> 24) & 0xff;
  *p++ = (int) (addr >> 16) & 0xff;
  *p++ = (int) (addr >> 8) & 0xff;
  *p++ = (int) (addr) & 0xff;

  retlen = sds_send (buf, p - buf);

  for (i = 0; i < 4; ++i)
    contents_cache[i] = buf[i + 2];

  return 0;
}

static int
sds_remove_breakpoint (CORE_ADDR addr, char *contents_cache)
{
  int i, retlen;
  unsigned char *p, buf[PBUFSIZ];

  p = buf;
  *p++ = 17;
  *p++ = 0;
  *p++ = (int) (addr >> 24) & 0xff;
  *p++ = (int) (addr >> 16) & 0xff;
  *p++ = (int) (addr >> 8) & 0xff;
  *p++ = (int) (addr) & 0xff;
  for (i = 0; i < 4; ++i)
    *p++ = contents_cache[i];

  retlen = sds_send (buf, p - buf);

  return 0;
}

static void
init_sds_ops (void)
{
  sds_ops.to_shortname = "sds";
  sds_ops.to_longname = "Remote serial target with SDS protocol";
  sds_ops.to_doc = "Use a remote computer via a serial line; using the SDS protocol.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  sds_ops.to_open = sds_open;
  sds_ops.to_close = sds_close;
  sds_ops.to_detach = sds_detach;
  sds_ops.to_resume = sds_resume;
  sds_ops.to_wait = sds_wait;
  sds_ops.to_fetch_registers = sds_fetch_registers;
  sds_ops.to_store_registers = sds_store_registers;
  sds_ops.to_prepare_to_store = sds_prepare_to_store;
  sds_ops.to_xfer_memory = sds_xfer_memory;
  sds_ops.to_files_info = sds_files_info;
  sds_ops.to_insert_breakpoint = sds_insert_breakpoint;
  sds_ops.to_remove_breakpoint = sds_remove_breakpoint;
  sds_ops.to_kill = sds_kill;
  sds_ops.to_load = sds_load;
  sds_ops.to_create_inferior = sds_create_inferior;
  sds_ops.to_mourn_inferior = sds_mourn;
  sds_ops.to_stratum = process_stratum;
  sds_ops.to_has_all_memory = 1;
  sds_ops.to_has_memory = 1;
  sds_ops.to_has_stack = 1;
  sds_ops.to_has_registers = 1;
  sds_ops.to_has_execution = 1;
  sds_ops.to_magic = OPS_MAGIC;
}

/* Put a command string, in args, out to the monitor and display the
   reply message.  */

static void
sds_command (char *args, int from_tty)
{
  char *p;
  int i, len, retlen;
  unsigned char buf[1000];

  /* Convert hexadecimal chars into a byte buffer.  */
  p = args;
  len = 0;
  while (*p != '\0')
    {
      buf[len++] = fromhex (p[0]) * 16 + fromhex (p[1]);
      if (p[1] == '\0')
	break;
      p += 2;
    }

  retlen = sds_send (buf, len);

  printf_filtered ("Reply is ");
  for (i = 0; i < retlen; ++i)
    {
      printf_filtered ("%02x", buf[i]);
    }
  printf_filtered ("\n");
}

void
_initialize_remote_sds (void)
{
  init_sds_ops ();
  add_target (&sds_ops);

  add_show_from_set (add_set_cmd ("sdstimeout", no_class,
				  var_integer, (char *) &sds_timeout,
			     "Set timeout value for sds read.\n", &setlist),
		     &showlist);

  add_com ("sds", class_obscure, sds_command,
	   "Send a command to the SDS monitor.");
}
