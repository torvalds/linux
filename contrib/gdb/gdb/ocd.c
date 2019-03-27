/* Target communications support for Macraigor Systems' On-Chip Debugging

   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2004 Free
   Software Foundation, Inc.

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
#include "gdbcore.h"
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
#include <sys/types.h>
#include <signal.h>
#include "serial.h"
#include "ocd.h"
#include "regcache.h"

/* Prototypes for local functions */

static int ocd_read_bytes (CORE_ADDR memaddr, char *myaddr, int len);

static int ocd_start_remote (void *dummy);

static int readchar (int timeout);

static void ocd_interrupt (int signo);

static void ocd_interrupt_twice (int signo);

static void interrupt_query (void);

static unsigned char *ocd_do_command (int cmd, int *statusp, int *lenp);

static void ocd_put_packet (unsigned char *packet, int pktlen);

static unsigned char *ocd_get_packet (int cmd, int *pktlen, int timeout);

static struct target_ops *current_ops = NULL;

static int last_run_status;

/* Descriptor for I/O to remote machine.  Initialize it to NULL so that
   ocd_open knows that we don't have a file open when the program
   starts.  */
static struct serial *ocd_desc = NULL;

void
ocd_error (char *s, int error_code)
{
  char buf[100];

  fputs_filtered (s, gdb_stderr);
  fputs_filtered (" ", gdb_stderr);

  switch (error_code)
    {
    case 0x1:
      s = "Unknown fault";
      break;
    case 0x2:
      s = "Power failed";
      break;
    case 0x3:
      s = "Cable disconnected";
      break;
    case 0x4:
      s = "Couldn't enter OCD mode";
      break;
    case 0x5:
      s = "Target stuck in reset";
      break;
    case 0x6:
      s = "OCD hasn't been initialized";
      break;
    case 0x7:
      s = "Write verify failed";
      break;
    case 0x8:
      s = "Reg buff error (during MPC5xx fp reg read/write)";
      break;
    case 0x9:
      s = "Invalid CPU register access attempt failed";
      break;
    case 0x11:
      s = "Bus error";
      break;
    case 0x12:
      s = "Checksum error";
      break;
    case 0x13:
      s = "Illegal command";
      break;
    case 0x14:
      s = "Parameter error";
      break;
    case 0x15:
      s = "Internal error";
      break;
    case 0x80:
      s = "Flash erase error";
      break;
    default:
      sprintf (buf, "Unknown error code %d", error_code);
      s = buf;
    }

  error ("%s", s);
}

/*  Return nonzero if the thread TH is still alive on the remote system.  */

int
ocd_thread_alive (ptid_t th)
{
  return 1;
}

/* Clean up connection to a remote debugger.  */

void
ocd_close (int quitting)
{
  if (ocd_desc)
    serial_close (ocd_desc);
  ocd_desc = NULL;
}

/* Stub for catch_errors.  */

static int
ocd_start_remote (void *dummy)
{
  unsigned char buf[10], *p;
  int pktlen;
  int status;
  int error_code;
  int speed;
  enum ocd_target_type target_type;

  target_type = *(enum ocd_target_type *) dummy;

  immediate_quit++;		/* Allow user to interrupt it */

  serial_send_break (ocd_desc);	/* Wake up the wiggler */

  speed = 80;			/* Divide clock by 4000 */

  buf[0] = OCD_INIT;
  buf[1] = speed >> 8;
  buf[2] = speed & 0xff;
  buf[3] = target_type;
  ocd_put_packet (buf, 4);	/* Init OCD params */
  p = ocd_get_packet (buf[0], &pktlen, remote_timeout);

  if (pktlen < 2)
    error ("Truncated response packet from OCD device");

  status = p[1];
  error_code = p[2];

  if (error_code != 0)
    ocd_error ("OCD_INIT:", error_code);

  ocd_do_command (OCD_AYT, &status, &pktlen);

  p = ocd_do_command (OCD_GET_VERSION, &status, &pktlen);

  printf_unfiltered ("[Wiggler version %x.%x, capability 0x%x]\n",
		     p[0], p[1], (p[2] << 16) | p[3]);

  /* If processor is still running, stop it.  */

  if (!(status & OCD_FLAG_BDM))
    ocd_stop ();

  /* When using a target box, we want to asynchronously return status when
     target stops.  The OCD_SET_CTL_FLAGS command is ignored by Wigglers.dll
     when using a parallel Wiggler */
  buf[0] = OCD_SET_CTL_FLAGS;
  buf[1] = 0;
  buf[2] = 1;
  ocd_put_packet (buf, 3);

  p = ocd_get_packet (buf[0], &pktlen, remote_timeout);

  if (pktlen < 2)
    error ("Truncated response packet from OCD device");

  status = p[1];
  error_code = p[2];

  if (error_code != 0)
    ocd_error ("OCD_SET_CTL_FLAGS:", error_code);

  immediate_quit--;

/* This is really the job of start_remote however, that makes an assumption
   that the target is about to print out a status message of some sort.  That
   doesn't happen here (in fact, it may not be possible to get the monitor to
   send the appropriate packet).  */

  flush_cached_frames ();
  registers_changed ();
  stop_pc = read_pc ();
  print_stack_frame (get_selected_frame (), -1, 1);

  buf[0] = OCD_LOG_FILE;
  buf[1] = 3;			/* close existing WIGGLERS.LOG */
  ocd_put_packet (buf, 2);
  p = ocd_get_packet (buf[0], &pktlen, remote_timeout);

  buf[0] = OCD_LOG_FILE;
  buf[1] = 2;			/* append to existing WIGGLERS.LOG */
  ocd_put_packet (buf, 2);
  p = ocd_get_packet (buf[0], &pktlen, remote_timeout);

  return 1;
}

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */

void
ocd_open (char *name, int from_tty, enum ocd_target_type target_type,
	  struct target_ops *ops)
{
  unsigned char buf[10], *p;
  int pktlen;

  if (name == 0)
    error ("To open an OCD connection, you need to specify the\n\
device the OCD device is attached to (e.g. /dev/ttya).");

  target_preopen (from_tty);

  current_ops = ops;

  unpush_target (current_ops);

  ocd_desc = serial_open (name);
  if (!ocd_desc)
    perror_with_name (name);

  if (baud_rate != -1)
    {
      if (serial_setbaudrate (ocd_desc, baud_rate))
	{
	  serial_close (ocd_desc);
	  perror_with_name (name);
	}
    }

  serial_raw (ocd_desc);

  /* If there is something sitting in the buffer we might take it as a
     response to a command, which would be bad.  */
  serial_flush_input (ocd_desc);

  if (from_tty)
    {
      puts_filtered ("Remote target wiggler connected to ");
      puts_filtered (name);
      puts_filtered ("\n");
    }
  push_target (current_ops);	/* Switch to using remote target now */

  /* Without this, some commands which require an active target (such as kill)
     won't work.  This variable serves (at least) double duty as both the pid
     of the target process (if it has such), and as a flag indicating that a
     target is active.  These functions should be split out into seperate
     variables, especially since GDB will someday have a notion of debugging
     several processes.  */

  inferior_ptid = pid_to_ptid (42000);
  /* Start the remote connection; if error (0), discard this target.
     In particular, if the user quits, be sure to discard it
     (we'd be in an inconsistent state otherwise).  */
  if (!catch_errors (ocd_start_remote, &target_type,
		     "Couldn't establish connection to remote target\n",
		     RETURN_MASK_ALL))
    {
      pop_target ();
      error ("Failed to connect to OCD.");
    }
}

/* This takes a program previously attached to and detaches it.  After
   this is done, GDB can be used to debug some other program.  We
   better not have left any breakpoints in the target program or it'll
   die when it hits one.  */

void
ocd_detach (char *args, int from_tty)
{
  if (args)
    error ("Argument given to \"detach\" when remotely debugging.");

  pop_target ();
  if (from_tty)
    puts_filtered ("Ending remote debugging.\n");
}

/* Tell the remote machine to resume.  */

void
ocd_resume (ptid_t ptid, int step, enum target_signal siggnal)
{
  int pktlen;

  if (step)
    ocd_do_command (OCD_STEP, &last_run_status, &pktlen);
  else
    ocd_do_command (OCD_RUN, &last_run_status, &pktlen);
}

void
ocd_stop (void)
{
  int status;
  int pktlen;

  ocd_do_command (OCD_STOP, &status, &pktlen);

  if (!(status & OCD_FLAG_BDM))
    error ("Can't stop target via BDM");
}

static volatile int ocd_interrupt_flag;

/* Send ^C to target to halt it.  Target will respond, and send us a
   packet.  */

static void
ocd_interrupt (int signo)
{
  /* If this doesn't work, try more severe steps.  */
  signal (signo, ocd_interrupt_twice);

  if (remote_debug)
    printf_unfiltered ("ocd_interrupt called\n");

  {
    char buf[1];

    ocd_stop ();
    buf[0] = OCD_AYT;
    ocd_put_packet (buf, 1);
    ocd_interrupt_flag = 1;
  }
}

static void (*ofunc) ();

/* The user typed ^C twice.  */
static void
ocd_interrupt_twice (int signo)
{
  signal (signo, ofunc);

  interrupt_query ();

  signal (signo, ocd_interrupt);
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
static int kill_kludge;

/* Wait until the remote machine stops, then return,
   storing status in STATUS just as `wait' would.
   Returns "pid" (though it's not clear what, if anything, that
   means in the case of this target).  */

int
ocd_wait (void)
{
  unsigned char *p;
  int error_code;
  int pktlen;
  char buf[1];

  ocd_interrupt_flag = 0;

  /* Target might already be stopped by the time we get here. */
  /* If we aren't already stopped, we need to loop until we've dropped
     back into BDM mode */

  while (!(last_run_status & OCD_FLAG_BDM))
    {
      buf[0] = OCD_AYT;
      ocd_put_packet (buf, 1);
      p = ocd_get_packet (OCD_AYT, &pktlen, -1);

      ofunc = (void (*)()) signal (SIGINT, ocd_interrupt);
      signal (SIGINT, ofunc);

      if (pktlen < 2)
	error ("Truncated response packet from OCD device");

      last_run_status = p[1];
      error_code = p[2];

      if (error_code != 0)
	ocd_error ("target_wait:", error_code);

      if (last_run_status & OCD_FLAG_PWF)
	error ("OCD device lost VCC at BDM interface.");
      else if (last_run_status & OCD_FLAG_CABLE_DISC)
	error ("OCD device cable appears to have been disconnected.");
    }

  if (ocd_interrupt_flag)
    return 1;
  else
    return 0;
}

/* Read registers from the OCD device.  Specify the starting and ending
   register number.  Return the number of regs actually read in *NUMREGS.
   Returns a pointer to a static array containing the register contents.  */

unsigned char *
ocd_read_bdm_registers (int first_bdm_regno, int last_bdm_regno, int *reglen)
{
  unsigned char buf[10];
  int i;
  unsigned char *p;
  unsigned char *regs;
  int error_code, status;
  int pktlen;

  buf[0] = OCD_READ_REGS;
  buf[1] = first_bdm_regno >> 8;
  buf[2] = first_bdm_regno & 0xff;
  buf[3] = last_bdm_regno >> 8;
  buf[4] = last_bdm_regno & 0xff;

  ocd_put_packet (buf, 5);
  p = ocd_get_packet (OCD_READ_REGS, &pktlen, remote_timeout);

  status = p[1];
  error_code = p[2];

  if (error_code != 0)
    ocd_error ("read_bdm_registers:", error_code);

  i = p[3];
  if (i == 0)
    i = 256;

  if (i > pktlen - 4
      || ((i & 3) != 0))
    error ("Register block size bad:  %d", i);

  *reglen = i;

  regs = p + 4;

  return regs;
}

/* Read register BDM_REGNO and returns its value ala read_register() */

CORE_ADDR
ocd_read_bdm_register (int bdm_regno)
{
  int reglen;
  unsigned char *p;
  CORE_ADDR regval;

  p = ocd_read_bdm_registers (bdm_regno, bdm_regno, &reglen);
  regval = extract_unsigned_integer (p, reglen);

  return regval;
}

void
ocd_write_bdm_registers (int first_bdm_regno, unsigned char *regptr, int reglen)
{
  unsigned char *buf;
  unsigned char *p;
  int error_code, status;
  int pktlen;

  buf = alloca (4 + reglen);

  buf[0] = OCD_WRITE_REGS;
  buf[1] = first_bdm_regno >> 8;
  buf[2] = first_bdm_regno & 0xff;
  buf[3] = reglen;
  memcpy (buf + 4, regptr, reglen);

  ocd_put_packet (buf, 4 + reglen);
  p = ocd_get_packet (OCD_WRITE_REGS, &pktlen, remote_timeout);

  if (pktlen < 3)
    error ("Truncated response packet from OCD device");

  status = p[1];
  error_code = p[2];

  if (error_code != 0)
    ocd_error ("ocd_write_bdm_registers:", error_code);
}

void
ocd_write_bdm_register (int bdm_regno, CORE_ADDR reg)
{
  unsigned char buf[4];

  store_unsigned_integer (buf, 4, reg);

  ocd_write_bdm_registers (bdm_regno, buf, 4);
}

void
ocd_prepare_to_store (void)
{
}

/* Write memory data directly to the remote machine.
   This does not inform the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.

   Returns number of bytes transferred, or 0 for error.  */

static int write_mem_command = OCD_WRITE_MEM;

int
ocd_write_bytes (CORE_ADDR memaddr, char *myaddr, int len)
{
  char buf[256 + 10];
  unsigned char *p;
  int origlen;

  origlen = len;

  buf[0] = write_mem_command;
  buf[5] = 1;			/* Write as bytes */
  buf[6] = 0;			/* Don't verify */

  while (len > 0)
    {
      int numbytes;
      int pktlen;
      int status, error_code;

      numbytes = min (len, 256 - 8);

      buf[1] = memaddr >> 24;
      buf[2] = memaddr >> 16;
      buf[3] = memaddr >> 8;
      buf[4] = memaddr;

      buf[7] = numbytes;

      memcpy (&buf[8], myaddr, numbytes);
      ocd_put_packet (buf, 8 + numbytes);
      p = ocd_get_packet (OCD_WRITE_MEM, &pktlen, remote_timeout);
      if (pktlen < 3)
	error ("Truncated response packet from OCD device");

      status = p[1];
      error_code = p[2];

      if (error_code == 0x11)	/* Got a bus error? */
	{
	  CORE_ADDR error_address;

	  error_address = p[3] << 24;
	  error_address |= p[4] << 16;
	  error_address |= p[5] << 8;
	  error_address |= p[6];
	  numbytes = error_address - memaddr;

	  len -= numbytes;

	  errno = EIO;

	  break;
	}
      else if (error_code != 0)
	ocd_error ("ocd_write_bytes:", error_code);

      len -= numbytes;
      memaddr += numbytes;
      myaddr += numbytes;
    }

  return origlen - len;
}

/* Read memory data directly from the remote machine.
   This does not use the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.

   Returns number of bytes transferred, or 0 for error.  */

static int
ocd_read_bytes (CORE_ADDR memaddr, char *myaddr, int len)
{
  char buf[256 + 10];
  unsigned char *p;
  int origlen;

  origlen = len;

  buf[0] = OCD_READ_MEM;
  buf[5] = 1;			/* Read as bytes */

  while (len > 0)
    {
      int numbytes;
      int pktlen;
      int status, error_code;

      numbytes = min (len, 256 - 7);

      buf[1] = memaddr >> 24;
      buf[2] = memaddr >> 16;
      buf[3] = memaddr >> 8;
      buf[4] = memaddr;

      buf[6] = numbytes;

      ocd_put_packet (buf, 7);
      p = ocd_get_packet (OCD_READ_MEM, &pktlen, remote_timeout);
      if (pktlen < 4)
	error ("Truncated response packet from OCD device");

      status = p[1];
      error_code = p[2];

      if (error_code == 0x11)	/* Got a bus error? */
	{
	  CORE_ADDR error_address;

	  error_address = p[3] << 24;
	  error_address |= p[4] << 16;
	  error_address |= p[5] << 8;
	  error_address |= p[6];
	  numbytes = error_address - memaddr;

	  len -= numbytes;

	  errno = EIO;

	  break;
	}
      else if (error_code != 0)
	ocd_error ("ocd_read_bytes:", error_code);

      memcpy (myaddr, &p[4], numbytes);

      len -= numbytes;
      memaddr += numbytes;
      myaddr += numbytes;
    }

  return origlen - len;
}

/* Read or write LEN bytes from inferior memory at MEMADDR, transferring
   to or from debugger address MYADDR.  Write to inferior if SHOULD_WRITE is
   nonzero.  Returns length of data written or read; 0 for error.  TARGET
   is ignored.  */

int
ocd_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int should_write,
		 struct mem_attrib *attrib, struct target_ops *target)
{
  int res;

  if (should_write)
    res = ocd_write_bytes (memaddr, myaddr, len);
  else
    res = ocd_read_bytes (memaddr, myaddr, len);

  return res;
}

void
ocd_files_info (struct target_ops *ignore)
{
  puts_filtered ("Debugging a target over a serial line.\n");
}

/* Stuff for dealing with the packets which are part of this protocol.
   See comment at top of file for details.  */

/* Read a single character from the remote side, handling wierd errors. */

static int
readchar (int timeout)
{
  int ch;

  ch = serial_readchar (ocd_desc, timeout);

  switch (ch)
    {
    case SERIAL_EOF:
      error ("Remote connection closed");
    case SERIAL_ERROR:
      perror_with_name ("Remote communication error");
    case SERIAL_TIMEOUT:
    default:
      return ch;
    }
}

/* Send a packet to the OCD device.  The packet framed by a SYN character,
   a byte count and a checksum.  The byte count only counts the number of
   bytes between the count and the checksum.  A count of zero actually
   means 256.  Any SYNs within the packet (including the checksum and
   count) must be quoted.  The quote character must be quoted as well.
   Quoting is done by replacing the character with the two-character sequence
   DLE, {char} | 0100.  Note that the quoting mechanism has no effect on the
   byte count.  */

static void
ocd_put_packet (unsigned char *buf, int len)
{
  unsigned char checksum;
  unsigned char c;
  unsigned char *packet, *packet_ptr;

  packet = alloca (len + 1 + 1);	/* packet + SYN + checksum */
  packet_ptr = packet;

  checksum = 0;

  *packet_ptr++ = 0x55;

  while (len-- > 0)
    {
      c = *buf++;

      checksum += c;
      *packet_ptr++ = c;
    }

  *packet_ptr++ = -checksum;
  if (serial_write (ocd_desc, packet, packet_ptr - packet))
    perror_with_name ("output_packet: write failed");
}

/* Get a packet from the OCD device.  Timeout is only enforced for the
   first byte of the packet.  Subsequent bytes are expected to arrive in
   time <= remote_timeout.  Returns a pointer to a static buffer containing
   the payload of the packet.  *LENP contains the length of the packet.
 */

static unsigned char *
ocd_get_packet (int cmd, int *lenp, int timeout)
{
  int ch;
  int len;
  static unsigned char packet[512];
  unsigned char *packet_ptr;
  unsigned char checksum;

  ch = readchar (timeout);

  if (ch < 0)
    error ("ocd_get_packet (readchar): %d", ch);

  if (ch != 0x55)
    error ("ocd_get_packet (readchar): %d", ch);

/* Found the start of a packet */

  packet_ptr = packet;
  checksum = 0;

/* Read command char.  That sort of tells us how long the packet is. */

  ch = readchar (timeout);

  if (ch < 0)
    error ("ocd_get_packet (readchar): %d", ch);

  *packet_ptr++ = ch;
  checksum += ch;

/* Get status. */

  ch = readchar (timeout);

  if (ch < 0)
    error ("ocd_get_packet (readchar): %d", ch);
  *packet_ptr++ = ch;
  checksum += ch;

/* Get error code. */

  ch = readchar (timeout);

  if (ch < 0)
    error ("ocd_get_packet (readchar): %d", ch);
  *packet_ptr++ = ch;
  checksum += ch;

  switch (ch)			/* Figure out length of packet */
    {
    case 0x7:			/* Write verify error? */
      len = 8;			/* write address, value read back */
      break;
    case 0x11:			/* Bus error? */
      /* write address, read flag */
    case 0x15:			/* Internal error */
      len = 5;			/* error code, vector */
      break;
    default:			/* Error w/no params */
      len = 0;
      break;
    case 0x0:			/* Normal result */
      switch (packet[0])
	{
	case OCD_AYT:		/* Are You There? */
	case OCD_SET_BAUD_RATE:	/* Set Baud Rate */
	case OCD_INIT:		/* Initialize OCD device */
	case OCD_SET_SPEED:	/* Set Speed */
	case OCD_SET_FUNC_CODE:	/* Set Function Code */
	case OCD_SET_CTL_FLAGS:	/* Set Control Flags */
	case OCD_SET_BUF_ADDR:	/* Set Register Buffer Address */
	case OCD_RUN:		/* Run Target from PC  */
	case OCD_RUN_ADDR:	/* Run Target from Specified Address  */
	case OCD_STOP:		/* Stop Target */
	case OCD_RESET_RUN:	/* Reset Target and Run */
	case OCD_RESET:	/* Reset Target and Halt */
	case OCD_STEP:		/* Single Step */
	case OCD_WRITE_REGS:	/* Write Register */
	case OCD_WRITE_MEM:	/* Write Memory */
	case OCD_FILL_MEM:	/* Fill Memory */
	case OCD_MOVE_MEM:	/* Move Memory */
	case OCD_WRITE_INT_MEM:	/* Write Internal Memory */
	case OCD_JUMP:		/* Jump to Subroutine */
	case OCD_ERASE_FLASH:	/* Erase flash memory */
	case OCD_PROGRAM_FLASH:	/* Write flash memory */
	case OCD_EXIT_MON:	/* Exit the flash programming monitor  */
	case OCD_ENTER_MON:	/* Enter the flash programming monitor  */
	case OCD_LOG_FILE:	/* Make Wigglers.dll save Wigglers.log */
	case OCD_SET_CONNECTION:	/* Set type of connection in Wigglers.dll */
	  len = 0;
	  break;
	case OCD_GET_VERSION:	/* Get Version */
	  len = 10;
	  break;
	case OCD_GET_STATUS_MASK:	/* Get Status Mask */
	  len = 1;
	  break;
	case OCD_GET_CTRS:	/* Get Error Counters */
	case OCD_READ_REGS:	/* Read Register */
	case OCD_READ_MEM:	/* Read Memory */
	case OCD_READ_INT_MEM:	/* Read Internal Memory */
	  len = 257;
	  break;
	default:
	  error ("ocd_get_packet: unknown packet type 0x%x\n", ch);
	}
    }

  if (len == 257)		/* Byte stream? */
    {				/* Yes, byte streams contain the length */
      ch = readchar (timeout);

      if (ch < 0)
	error ("ocd_get_packet (readchar): %d", ch);
      *packet_ptr++ = ch;
      checksum += ch;
      len = ch;
      if (len == 0)
	len = 256;
    }

  while (len-- >= 0)		/* Do rest of packet and checksum */
    {
      ch = readchar (timeout);

      if (ch < 0)
	error ("ocd_get_packet (readchar): %d", ch);
      *packet_ptr++ = ch;
      checksum += ch;
    }

  if (checksum != 0)
    error ("ocd_get_packet: bad packet checksum");

  if (cmd != -1 && cmd != packet[0])
    error ("Response phase error.  Got 0x%x, expected 0x%x", packet[0], cmd);

  *lenp = packet_ptr - packet - 1;	/* Subtract checksum byte */
  return packet;
}

/* Execute a simple (one-byte) command.  Returns a pointer to the data
   following the error code.  */

static unsigned char *
ocd_do_command (int cmd, int *statusp, int *lenp)
{
  unsigned char buf[100], *p;
  int status, error_code;
  char errbuf[100];

  unsigned char logbuf[100];
  int logpktlen;

  buf[0] = cmd;
  ocd_put_packet (buf, 1);	/* Send command */
  p = ocd_get_packet (*buf, lenp, remote_timeout);

  if (*lenp < 3)
    error ("Truncated response packet from OCD device");

  status = p[1];
  error_code = p[2];

  if (error_code != 0)
    {
      sprintf (errbuf, "ocd_do_command (0x%x):", cmd);
      ocd_error (errbuf, error_code);
    }

  if (status & OCD_FLAG_PWF)
    error ("OCD device can't detect VCC at BDM interface.");
  else if (status & OCD_FLAG_CABLE_DISC)
    error ("BDM cable appears to be disconnected.");

  *statusp = status;

  logbuf[0] = OCD_LOG_FILE;
  logbuf[1] = 3;		/* close existing WIGGLERS.LOG */
  ocd_put_packet (logbuf, 2);
  ocd_get_packet (logbuf[0], &logpktlen, remote_timeout);

  logbuf[0] = OCD_LOG_FILE;
  logbuf[1] = 2;		/* append to existing WIGGLERS.LOG */
  ocd_put_packet (logbuf, 2);
  ocd_get_packet (logbuf[0], &logpktlen, remote_timeout);

  return p + 3;
}

void
ocd_kill (void)
{
  /* For some mysterious reason, wait_for_inferior calls kill instead of
     mourn after it gets TARGET_WAITKIND_SIGNALLED.  Work around it.  */
  if (kill_kludge)
    {
      kill_kludge = 0;
      target_mourn_inferior ();
      return;
    }

  /* Don't wait for it to die.  I'm not really sure it matters whether
     we do or not.  */
  target_mourn_inferior ();
}

void
ocd_mourn (void)
{
  unpush_target (current_ops);
  generic_mourn_inferior ();
}

/* All we actually do is set the PC to the start address of exec_bfd, and start
   the program at that point.  */

void
ocd_create_inferior (char *exec_file, char *args, char **env)
{
  if (args && (*args != '\000'))
    error ("Args are not supported by BDM.");

  clear_proceed_status ();
  proceed (bfd_get_start_address (exec_bfd), TARGET_SIGNAL_0, 0);
}

void
ocd_load (char *args, int from_tty)
{
  generic_load (args, from_tty);

  inferior_ptid = null_ptid;

/* This is necessary because many things were based on the PC at the time that
   we attached to the monitor, which is no longer valid now that we have loaded
   new code (and just changed the PC).  Another way to do this might be to call
   normal_stop, except that the stack may not be valid, and things would get
   horribly confused... */

  clear_symtab_users ();
}

/* This should be defined for each target */
/* But we want to be able to compile this file for some configurations
   not yet supported fully */

#define BDM_BREAKPOINT {0x0,0x0,0x0,0x0}	/* For ppc 8xx */

/* BDM (at least on CPU32) uses a different breakpoint */

int
ocd_insert_breakpoint (CORE_ADDR addr, char *contents_cache)
{
  static char break_insn[] = BDM_BREAKPOINT;
  int val;

  val = target_read_memory (addr, contents_cache, sizeof (break_insn));

  if (val == 0)
    val = target_write_memory (addr, break_insn, sizeof (break_insn));

  return val;
}

int
ocd_remove_breakpoint (CORE_ADDR addr, char *contents_cache)
{
  static char break_insn[] = BDM_BREAKPOINT;
  int val;

  val = target_write_memory (addr, contents_cache, sizeof (break_insn));

  return val;
}

static void
bdm_command (char *args, int from_tty)
{
  error ("bdm command must be followed by `reset'");
}

static void
bdm_reset_command (char *args, int from_tty)
{
  int status, pktlen;

  if (!ocd_desc)
    error ("Not connected to OCD device.");

  ocd_do_command (OCD_RESET, &status, &pktlen);
  dcache_invalidate (target_dcache);
  registers_changed ();
}

static void
bdm_restart_command (char *args, int from_tty)
{
  int status, pktlen;

  if (!ocd_desc)
    error ("Not connected to OCD device.");

  ocd_do_command (OCD_RESET_RUN, &status, &pktlen);
  last_run_status = status;
  clear_proceed_status ();
  wait_for_inferior ();
  normal_stop ();
}

/* Temporary replacement for target_store_registers().  This prevents
   generic_load from trying to set the PC.  */

static void
noop_store_registers (int regno)
{
}

static void
bdm_update_flash_command (char *args, int from_tty)
{
  int status, pktlen;
  struct cleanup *old_chain; 
  void (*store_registers_tmp) (int);

  if (!ocd_desc)
    error ("Not connected to OCD device.");

  if (!args)
    error ("Must specify file containing new OCD code.");

/*  old_chain = make_cleanup (flash_cleanup, 0); */

  ocd_do_command (OCD_ENTER_MON, &status, &pktlen);

  ocd_do_command (OCD_ERASE_FLASH, &status, &pktlen);

  write_mem_command = OCD_PROGRAM_FLASH;
  store_registers_tmp = current_target.to_store_registers;
  current_target.to_store_registers = noop_store_registers;

  generic_load (args, from_tty);

  current_target.to_store_registers = store_registers_tmp;
  write_mem_command = OCD_WRITE_MEM;

  ocd_do_command (OCD_EXIT_MON, &status, &pktlen);

/*  discard_cleanups (old_chain); */
}

extern initialize_file_ftype _initialize_remote_ocd; /* -Wmissing-prototypes */

void
_initialize_remote_ocd (void)
{
  extern struct cmd_list_element *cmdlist;
  static struct cmd_list_element *ocd_cmd_list = NULL;

  add_show_from_set (add_set_cmd ("remotetimeout", no_class,
				  var_integer, (char *) &remote_timeout,
			  "Set timeout value for remote read.\n", &setlist),
		     &showlist);

  add_prefix_cmd ("ocd", class_obscure, bdm_command, "", &ocd_cmd_list, "ocd ",
		  0, &cmdlist);

  add_cmd ("reset", class_obscure, bdm_reset_command, "", &ocd_cmd_list);
  add_cmd ("restart", class_obscure, bdm_restart_command, "", &ocd_cmd_list);
  add_cmd ("update-flash", class_obscure, bdm_update_flash_command, "", &ocd_cmd_list);
}
