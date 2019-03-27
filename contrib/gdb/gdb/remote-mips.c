/* Remote debugging interface for MIPS remote debugging protocol.

   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002 Free Software Foundation, Inc.

   Contributed by Cygnus Support.  Written by Ian Lance Taylor
   <ian@cygnus.com>.

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
#include "inferior.h"
#include "bfd.h"
#include "symfile.h"
#include "gdbcmd.h"
#include "gdbcore.h"
#include "serial.h"
#include "target.h"
#include "remote-utils.h"
#include "gdb_string.h"
#include "gdb_stat.h"
#include "regcache.h"
#include <ctype.h>
#include "mips-tdep.h"


/* Breakpoint types.  Values 0, 1, and 2 must agree with the watch
   types passed by breakpoint.c to target_insert_watchpoint.
   Value 3 is our own invention, and is used for ordinary instruction
   breakpoints.  Value 4 is used to mark an unused watchpoint in tables.  */
enum break_type
  {
    BREAK_WRITE,		/* 0 */
    BREAK_READ,			/* 1 */
    BREAK_ACCESS,		/* 2 */
    BREAK_FETCH,		/* 3 */
    BREAK_UNUSED		/* 4 */
  };

/* Prototypes for local functions.  */

static int mips_readchar (int timeout);

static int mips_receive_header (unsigned char *hdr, int *pgarbage,
				int ch, int timeout);

static int mips_receive_trailer (unsigned char *trlr, int *pgarbage,
				 int *pch, int timeout);

static int mips_cksum (const unsigned char *hdr,
		       const unsigned char *data, int len);

static void mips_send_packet (const char *s, int get_ack);

static void mips_send_command (const char *cmd, int prompt);

static int mips_receive_packet (char *buff, int throw_error, int timeout);

static ULONGEST mips_request (int cmd, ULONGEST addr, ULONGEST data,
			      int *perr, int timeout, char *buff);

static void mips_initialize (void);

static void mips_open (char *name, int from_tty);

static void pmon_open (char *name, int from_tty);

static void ddb_open (char *name, int from_tty);

static void lsi_open (char *name, int from_tty);

static void mips_close (int quitting);

static void mips_detach (char *args, int from_tty);

static void mips_resume (ptid_t ptid, int step,
                         enum target_signal siggnal);

static ptid_t mips_wait (ptid_t ptid,
                               struct target_waitstatus *status);

static int mips_map_regno (int regno);

static void mips_fetch_registers (int regno);

static void mips_prepare_to_store (void);

static void mips_store_registers (int regno);

static unsigned int mips_fetch_word (CORE_ADDR addr);

static int mips_store_word (CORE_ADDR addr, unsigned int value,
			    char *old_contents);

static int mips_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len,
			     int write, 
			     struct mem_attrib *attrib,
			     struct target_ops *target);

static void mips_files_info (struct target_ops *ignore);

static void mips_create_inferior (char *execfile, char *args, char **env);

static void mips_mourn_inferior (void);

static int pmon_makeb64 (unsigned long v, char *p, int n, int *chksum);

static int pmon_zeroset (int recsize, char **buff, int *amount,
			 unsigned int *chksum);

static int pmon_checkset (int recsize, char **buff, int *value);

static void pmon_make_fastrec (char **outbuf, unsigned char *inbuf,
			       int *inptr, int inamount, int *recsize,
			       unsigned int *csum, unsigned int *zerofill);

static int pmon_check_ack (char *mesg);

static void pmon_start_download (void);

static void pmon_end_download (int final, int bintotal);

static void pmon_download (char *buffer, int length);

static void pmon_load_fast (char *file);

static void mips_load (char *file, int from_tty);

static int mips_make_srec (char *buffer, int type, CORE_ADDR memaddr,
			   unsigned char *myaddr, int len);

static int set_breakpoint (CORE_ADDR addr, int len, enum break_type type);

static int clear_breakpoint (CORE_ADDR addr, int len, enum break_type type);

static int common_breakpoint (int set, CORE_ADDR addr, int len,
			      enum break_type type);

/* Forward declarations.  */
extern struct target_ops mips_ops;
extern struct target_ops pmon_ops;
extern struct target_ops ddb_ops;
/* *INDENT-OFF* */
/* The MIPS remote debugging interface is built on top of a simple
   packet protocol.  Each packet is organized as follows:

   SYN  The first character is always a SYN (ASCII 026, or ^V).  SYN
   may not appear anywhere else in the packet.  Any time a SYN is
   seen, a new packet should be assumed to have begun.

   TYPE_LEN
   This byte contains the upper five bits of the logical length
   of the data section, plus a single bit indicating whether this
   is a data packet or an acknowledgement.  The documentation
   indicates that this bit is 1 for a data packet, but the actual
   board uses 1 for an acknowledgement.  The value of the byte is
   0x40 + (ack ? 0x20 : 0) + (len >> 6)
   (we always have 0 <= len < 1024).  Acknowledgement packets do
   not carry data, and must have a data length of 0.

   LEN1 This byte contains the lower six bits of the logical length of
   the data section.  The value is
   0x40 + (len & 0x3f)

   SEQ  This byte contains the six bit sequence number of the packet.
   The value is
   0x40 + seq
   An acknowlegment packet contains the sequence number of the
   packet being acknowledged plus 1 modulo 64.  Data packets are
   transmitted in sequence.  There may only be one outstanding
   unacknowledged data packet at a time.  The sequence numbers
   are independent in each direction.  If an acknowledgement for
   the previous packet is received (i.e., an acknowledgement with
   the sequence number of the packet just sent) the packet just
   sent should be retransmitted.  If no acknowledgement is
   received within a timeout period, the packet should be
   retransmitted.  This has an unfortunate failure condition on a
   high-latency line, as a delayed acknowledgement may lead to an
   endless series of duplicate packets.

   DATA The actual data bytes follow.  The following characters are
   escaped inline with DLE (ASCII 020, or ^P):
   SYN (026)    DLE S
   DLE (020)    DLE D
   ^C  (003)    DLE C
   ^S  (023)    DLE s
   ^Q  (021)    DLE q
   The additional DLE characters are not counted in the logical
   length stored in the TYPE_LEN and LEN1 bytes.

   CSUM1
   CSUM2
   CSUM3
   These bytes contain an 18 bit checksum of the complete
   contents of the packet excluding the SEQ byte and the
   CSUM[123] bytes.  The checksum is simply the twos complement
   addition of all the bytes treated as unsigned characters.  The
   values of the checksum bytes are:
   CSUM1: 0x40 + ((cksum >> 12) & 0x3f)
   CSUM2: 0x40 + ((cksum >> 6) & 0x3f)
   CSUM3: 0x40 + (cksum & 0x3f)

   It happens that the MIPS remote debugging protocol always
   communicates with ASCII strings.  Because of this, this
   implementation doesn't bother to handle the DLE quoting mechanism,
   since it will never be required.  */
/* *INDENT-ON* */


/* The SYN character which starts each packet.  */
#define SYN '\026'

/* The 0x40 used to offset each packet (this value ensures that all of
   the header and trailer bytes, other than SYN, are printable ASCII
   characters).  */
#define HDR_OFFSET 0x40

/* The indices of the bytes in the packet header.  */
#define HDR_INDX_SYN 0
#define HDR_INDX_TYPE_LEN 1
#define HDR_INDX_LEN1 2
#define HDR_INDX_SEQ 3
#define HDR_LENGTH 4

/* The data/ack bit in the TYPE_LEN header byte.  */
#define TYPE_LEN_DA_BIT 0x20
#define TYPE_LEN_DATA 0
#define TYPE_LEN_ACK TYPE_LEN_DA_BIT

/* How to compute the header bytes.  */
#define HDR_SET_SYN(data, len, seq) (SYN)
#define HDR_SET_TYPE_LEN(data, len, seq) \
  (HDR_OFFSET \
   + ((data) ? TYPE_LEN_DATA : TYPE_LEN_ACK) \
   + (((len) >> 6) & 0x1f))
#define HDR_SET_LEN1(data, len, seq) (HDR_OFFSET + ((len) & 0x3f))
#define HDR_SET_SEQ(data, len, seq) (HDR_OFFSET + (seq))

/* Check that a header byte is reasonable.  */
#define HDR_CHECK(ch) (((ch) & HDR_OFFSET) == HDR_OFFSET)

/* Get data from the header.  These macros evaluate their argument
   multiple times.  */
#define HDR_IS_DATA(hdr) \
  (((hdr)[HDR_INDX_TYPE_LEN] & TYPE_LEN_DA_BIT) == TYPE_LEN_DATA)
#define HDR_GET_LEN(hdr) \
  ((((hdr)[HDR_INDX_TYPE_LEN] & 0x1f) << 6) + (((hdr)[HDR_INDX_LEN1] & 0x3f)))
#define HDR_GET_SEQ(hdr) ((unsigned int)(hdr)[HDR_INDX_SEQ] & 0x3f)

/* The maximum data length.  */
#define DATA_MAXLEN 1023

/* The trailer offset.  */
#define TRLR_OFFSET HDR_OFFSET

/* The indices of the bytes in the packet trailer.  */
#define TRLR_INDX_CSUM1 0
#define TRLR_INDX_CSUM2 1
#define TRLR_INDX_CSUM3 2
#define TRLR_LENGTH 3

/* How to compute the trailer bytes.  */
#define TRLR_SET_CSUM1(cksum) (TRLR_OFFSET + (((cksum) >> 12) & 0x3f))
#define TRLR_SET_CSUM2(cksum) (TRLR_OFFSET + (((cksum) >>  6) & 0x3f))
#define TRLR_SET_CSUM3(cksum) (TRLR_OFFSET + (((cksum)      ) & 0x3f))

/* Check that a trailer byte is reasonable.  */
#define TRLR_CHECK(ch) (((ch) & TRLR_OFFSET) == TRLR_OFFSET)

/* Get data from the trailer.  This evaluates its argument multiple
   times.  */
#define TRLR_GET_CKSUM(trlr) \
  ((((trlr)[TRLR_INDX_CSUM1] & 0x3f) << 12) \
   + (((trlr)[TRLR_INDX_CSUM2] & 0x3f) <<  6) \
   + ((trlr)[TRLR_INDX_CSUM3] & 0x3f))

/* The sequence number modulos.  */
#define SEQ_MODULOS (64)

/* PMON commands to load from the serial port or UDP socket.  */
#define LOAD_CMD	"load -b -s tty0\r"
#define LOAD_CMD_UDP	"load -b -s udp\r"

/* The target vectors for the four different remote MIPS targets.
   These are initialized with code in _initialize_remote_mips instead
   of static initializers, to make it easier to extend the target_ops
   vector later.  */
struct target_ops mips_ops, pmon_ops, ddb_ops, lsi_ops;

enum mips_monitor_type
  {
    /* IDT/SIM monitor being used: */
    MON_IDT,
    /* PMON monitor being used: */
    MON_PMON,			/* 3.0.83 [COGENT,EB,FP,NET] Algorithmics Ltd. Nov  9 1995 17:19:50 */
    MON_DDB,			/* 2.7.473 [DDBVR4300,EL,FP,NET] Risq Modular Systems,  Thu Jun 6 09:28:40 PDT 1996 */
    MON_LSI,			/* 4.3.12 [EB,FP], LSI LOGIC Corp. Tue Feb 25 13:22:14 1997 */
    /* Last and unused value, for sizing vectors, etc. */
    MON_LAST
  };
static enum mips_monitor_type mips_monitor = MON_LAST;

/* The monitor prompt text.  If the user sets the PMON prompt
   to some new value, the GDB `set monitor-prompt' command must also
   be used to inform GDB about the expected prompt.  Otherwise, GDB
   will not be able to connect to PMON in mips_initialize().
   If the `set monitor-prompt' command is not used, the expected
   default prompt will be set according the target:
   target               prompt
   -----                -----
   pmon         PMON> 
   ddb          NEC010>
   lsi          PMON>
 */
static char *mips_monitor_prompt;

/* Set to 1 if the target is open.  */
static int mips_is_open;

/* Currently active target description (if mips_is_open == 1) */
static struct target_ops *current_ops;

/* Set to 1 while the connection is being initialized.  */
static int mips_initializing;

/* Set to 1 while the connection is being brought down.  */
static int mips_exiting;

/* The next sequence number to send.  */
static unsigned int mips_send_seq;

/* The next sequence number we expect to receive.  */
static unsigned int mips_receive_seq;

/* The time to wait before retransmitting a packet, in seconds.  */
static int mips_retransmit_wait = 3;

/* The number of times to try retransmitting a packet before giving up.  */
static int mips_send_retries = 10;

/* The number of garbage characters to accept when looking for an
   SYN for the next packet.  */
static int mips_syn_garbage = 10;

/* The time to wait for a packet, in seconds.  */
static int mips_receive_wait = 5;

/* Set if we have sent a packet to the board but have not yet received
   a reply.  */
static int mips_need_reply = 0;

/* Handle used to access serial I/O stream.  */
static struct serial *mips_desc;

/* UDP handle used to download files to target.  */
static struct serial *udp_desc;
static int udp_in_use;

/* TFTP filename used to download files to DDB board, in the form
   host:filename.  */
static char *tftp_name;		/* host:filename */
static char *tftp_localname;	/* filename portion of above */
static int tftp_in_use;
static FILE *tftp_file;

/* Counts the number of times the user tried to interrupt the target (usually
   via ^C.  */
static int interrupt_count;

/* If non-zero, means that the target is running. */
static int mips_wait_flag = 0;

/* If non-zero, monitor supports breakpoint commands. */
static int monitor_supports_breakpoints = 0;

/* Data cache header.  */

#if 0				/* not used (yet?) */
static DCACHE *mips_dcache;
#endif

/* Non-zero means that we've just hit a read or write watchpoint */
static int hit_watchpoint;

/* Table of breakpoints/watchpoints (used only on LSI PMON target).
   The table is indexed by a breakpoint number, which is an integer
   from 0 to 255 returned by the LSI PMON when a breakpoint is set.
 */
#define MAX_LSI_BREAKPOINTS 256
struct lsi_breakpoint_info
  {
    enum break_type type;	/* type of breakpoint */
    CORE_ADDR addr;		/* address of breakpoint */
    int len;			/* length of region being watched */
    unsigned long value;	/* value to watch */
  }
lsi_breakpoints[MAX_LSI_BREAKPOINTS];

/* Error/warning codes returned by LSI PMON for breakpoint commands.
   Warning values may be ORed together; error values may not.  */
#define W_WARN	0x100		/* This bit is set if the error code is a warning */
#define W_MSK   0x101		/* warning: Range feature is supported via mask */
#define W_VAL   0x102		/* warning: Value check is not supported in hardware */
#define W_QAL   0x104		/* warning: Requested qualifiers are not supported in hardware */

#define E_ERR	0x200		/* This bit is set if the error code is an error */
#define E_BPT   0x200		/* error: No such breakpoint number */
#define E_RGE   0x201		/* error: Range is not supported */
#define E_QAL   0x202		/* error: The requested qualifiers can not be used */
#define E_OUT   0x203		/* error: Out of hardware resources */
#define E_NON   0x204		/* error: Hardware breakpoint not supported */

struct lsi_error
  {
    int code;			/* error code */
    char *string;		/* string associated with this code */
  };

struct lsi_error lsi_warning_table[] =
{
  {W_MSK, "Range feature is supported via mask"},
  {W_VAL, "Value check is not supported in hardware"},
  {W_QAL, "Requested qualifiers are not supported in hardware"},
  {0, NULL}
};

struct lsi_error lsi_error_table[] =
{
  {E_BPT, "No such breakpoint number"},
  {E_RGE, "Range is not supported"},
  {E_QAL, "The requested qualifiers can not be used"},
  {E_OUT, "Out of hardware resources"},
  {E_NON, "Hardware breakpoint not supported"},
  {0, NULL}
};

/* Set to 1 with the 'set monitor-warnings' command to enable printing
   of warnings returned by PMON when hardware breakpoints are used.  */
static int monitor_warnings;


static void
close_ports (void)
{
  mips_is_open = 0;
  serial_close (mips_desc);

  if (udp_in_use)
    {
      serial_close (udp_desc);
      udp_in_use = 0;
    }
  tftp_in_use = 0;
}

/* Handle low-level error that we can't recover from.  Note that just
   error()ing out from target_wait or some such low-level place will cause
   all hell to break loose--the rest of GDB will tend to get left in an
   inconsistent state.  */

static NORETURN void
mips_error (char *string,...)
{
  va_list args;

  va_start (args, string);

  target_terminal_ours ();
  wrap_here ("");		/* Force out any buffered output */
  gdb_flush (gdb_stdout);
  if (error_pre_print)
    fputs_filtered (error_pre_print, gdb_stderr);
  vfprintf_filtered (gdb_stderr, string, args);
  fprintf_filtered (gdb_stderr, "\n");
  va_end (args);
  gdb_flush (gdb_stderr);

  /* Clean up in such a way that mips_close won't try to talk to the
     board (it almost surely won't work since we weren't able to talk to
     it).  */
  close_ports ();

  printf_unfiltered ("Ending remote MIPS debugging.\n");
  target_mourn_inferior ();

  throw_exception (RETURN_ERROR);
}

/* putc_readable - print a character, displaying non-printable chars in
   ^x notation or in hex.  */

static void
fputc_readable (int ch, struct ui_file *file)
{
  if (ch == '\n')
    fputc_unfiltered ('\n', file);
  else if (ch == '\r')
    fprintf_unfiltered (file, "\\r");
  else if (ch < 0x20)		/* ASCII control character */
    fprintf_unfiltered (file, "^%c", ch + '@');
  else if (ch >= 0x7f)		/* non-ASCII characters (rubout or greater) */
    fprintf_unfiltered (file, "[%02x]", ch & 0xff);
  else
    fputc_unfiltered (ch, file);
}


/* puts_readable - print a string, displaying non-printable chars in
   ^x notation or in hex.  */

static void
fputs_readable (const char *string, struct ui_file *file)
{
  int c;

  while ((c = *string++) != '\0')
    fputc_readable (c, file);
}


/* Wait until STRING shows up in mips_desc.  Returns 1 if successful, else 0 if
   timed out.  TIMEOUT specifies timeout value in seconds.
 */

static int
mips_expect_timeout (const char *string, int timeout)
{
  const char *p = string;

  if (remote_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "Expected \"");
      fputs_readable (string, gdb_stdlog);
      fprintf_unfiltered (gdb_stdlog, "\", got \"");
    }

  immediate_quit++;
  while (1)
    {
      int c;

      /* Must use serial_readchar() here cuz mips_readchar would get
	 confused if we were waiting for the mips_monitor_prompt... */

      c = serial_readchar (mips_desc, timeout);

      if (c == SERIAL_TIMEOUT)
	{
	  if (remote_debug)
	    fprintf_unfiltered (gdb_stdlog, "\": FAIL\n");
	  return 0;
	}

      if (remote_debug)
	fputc_readable (c, gdb_stdlog);

      if (c == *p++)
	{
	  if (*p == '\0')
	    {
	      immediate_quit--;
	      if (remote_debug)
		fprintf_unfiltered (gdb_stdlog, "\": OK\n");
	      return 1;
	    }
	}
      else
	{
	  p = string;
	  if (c == *p)
	    p++;
	}
    }
}

/* Wait until STRING shows up in mips_desc.  Returns 1 if successful, else 0 if
   timed out.  The timeout value is hard-coded to 2 seconds.  Use
   mips_expect_timeout if a different timeout value is needed.
 */

static int
mips_expect (const char *string)
{
  return mips_expect_timeout (string, remote_timeout);
}

/* Read a character from the remote, aborting on error.  Returns
   SERIAL_TIMEOUT on timeout (since that's what serial_readchar()
   returns).  FIXME: If we see the string mips_monitor_prompt from the
   board, then we are debugging on the main console port, and we have
   somehow dropped out of remote debugging mode.  In this case, we
   automatically go back in to remote debugging mode.  This is a hack,
   put in because I can't find any way for a program running on the
   remote board to terminate without also ending remote debugging
   mode.  I assume users won't have any trouble with this; for one
   thing, the IDT documentation generally assumes that the remote
   debugging port is not the console port.  This is, however, very
   convenient for DejaGnu when you only have one connected serial
   port.  */

static int
mips_readchar (int timeout)
{
  int ch;
  static int state = 0;
  int mips_monitor_prompt_len = strlen (mips_monitor_prompt);

  {
    int i;

    i = timeout;
    if (i == -1 && watchdog > 0)
      i = watchdog;
  }

  if (state == mips_monitor_prompt_len)
    timeout = 1;
  ch = serial_readchar (mips_desc, timeout);

  if (ch == SERIAL_TIMEOUT && timeout == -1)	/* Watchdog went off */
    {
      target_mourn_inferior ();
      error ("Watchdog has expired.  Target detached.\n");
    }

  if (ch == SERIAL_EOF)
    mips_error ("End of file from remote");
  if (ch == SERIAL_ERROR)
    mips_error ("Error reading from remote: %s", safe_strerror (errno));
  if (remote_debug > 1)
    {
      /* Don't use _filtered; we can't deal with a QUIT out of
         target_wait, and I think this might be called from there.  */
      if (ch != SERIAL_TIMEOUT)
	fprintf_unfiltered (gdb_stdlog, "Read '%c' %d 0x%x\n", ch, ch, ch);
      else
	fprintf_unfiltered (gdb_stdlog, "Timed out in read\n");
    }

  /* If we have seen mips_monitor_prompt and we either time out, or
     we see a @ (which was echoed from a packet we sent), reset the
     board as described above.  The first character in a packet after
     the SYN (which is not echoed) is always an @ unless the packet is
     more than 64 characters long, which ours never are.  */
  if ((ch == SERIAL_TIMEOUT || ch == '@')
      && state == mips_monitor_prompt_len
      && !mips_initializing
      && !mips_exiting)
    {
      if (remote_debug > 0)
	/* Don't use _filtered; we can't deal with a QUIT out of
	   target_wait, and I think this might be called from there.  */
	fprintf_unfiltered (gdb_stdlog, "Reinitializing MIPS debugging mode\n");

      mips_need_reply = 0;
      mips_initialize ();

      state = 0;

      /* At this point, about the only thing we can do is abort the command
         in progress and get back to command level as quickly as possible. */

      error ("Remote board reset, debug protocol re-initialized.");
    }

  if (ch == mips_monitor_prompt[state])
    ++state;
  else
    state = 0;

  return ch;
}

/* Get a packet header, putting the data in the supplied buffer.
   PGARBAGE is a pointer to the number of garbage characters received
   so far.  CH is the last character received.  Returns 0 for success,
   or -1 for timeout.  */

static int
mips_receive_header (unsigned char *hdr, int *pgarbage, int ch, int timeout)
{
  int i;

  while (1)
    {
      /* Wait for a SYN.  mips_syn_garbage is intended to prevent
         sitting here indefinitely if the board sends us one garbage
         character per second.  ch may already have a value from the
         last time through the loop.  */
      while (ch != SYN)
	{
	  ch = mips_readchar (timeout);
	  if (ch == SERIAL_TIMEOUT)
	    return -1;
	  if (ch != SYN)
	    {
	      /* Printing the character here lets the user of gdb see
	         what the program is outputting, if the debugging is
	         being done on the console port.  Don't use _filtered:
	         we can't deal with a QUIT out of target_wait and
	         buffered target output confuses the user. */
 	      if (!mips_initializing || remote_debug > 0)
  		{
		  if (isprint (ch) || isspace (ch))
		    {
		      fputc_unfiltered (ch, gdb_stdtarg);
		    }
		  else
		    {
		      fputc_readable (ch, gdb_stdtarg);
		    }
		  gdb_flush (gdb_stdtarg);
  		}
	      
	      /* Only count unprintable characters. */
	      if (! (isprint (ch) || isspace (ch)))
		(*pgarbage) += 1;

	      if (mips_syn_garbage > 0
		  && *pgarbage > mips_syn_garbage)
		mips_error ("Debug protocol failure:  more than %d characters before a sync.",
			    mips_syn_garbage);
	    }
	}

      /* Get the packet header following the SYN.  */
      for (i = 1; i < HDR_LENGTH; i++)
	{
	  ch = mips_readchar (timeout);
	  if (ch == SERIAL_TIMEOUT)
	    return -1;
	  /* Make sure this is a header byte.  */
	  if (ch == SYN || !HDR_CHECK (ch))
	    break;

	  hdr[i] = ch;
	}

      /* If we got the complete header, we can return.  Otherwise we
         loop around and keep looking for SYN.  */
      if (i >= HDR_LENGTH)
	return 0;
    }
}

/* Get a packet header, putting the data in the supplied buffer.
   PGARBAGE is a pointer to the number of garbage characters received
   so far.  The last character read is returned in *PCH.  Returns 0
   for success, -1 for timeout, -2 for error.  */

static int
mips_receive_trailer (unsigned char *trlr, int *pgarbage, int *pch, int timeout)
{
  int i;
  int ch;

  for (i = 0; i < TRLR_LENGTH; i++)
    {
      ch = mips_readchar (timeout);
      *pch = ch;
      if (ch == SERIAL_TIMEOUT)
	return -1;
      if (!TRLR_CHECK (ch))
	return -2;
      trlr[i] = ch;
    }
  return 0;
}

/* Get the checksum of a packet.  HDR points to the packet header.
   DATA points to the packet data.  LEN is the length of DATA.  */

static int
mips_cksum (const unsigned char *hdr, const unsigned char *data, int len)
{
  const unsigned char *p;
  int c;
  int cksum;

  cksum = 0;

  /* The initial SYN is not included in the checksum.  */
  c = HDR_LENGTH - 1;
  p = hdr + 1;
  while (c-- != 0)
    cksum += *p++;

  c = len;
  p = data;
  while (c-- != 0)
    cksum += *p++;

  return cksum;
}

/* Send a packet containing the given ASCII string.  */

static void
mips_send_packet (const char *s, int get_ack)
{
  /* unsigned */ int len;
  unsigned char *packet;
  int cksum;
  int try;

  len = strlen (s);
  if (len > DATA_MAXLEN)
    mips_error ("MIPS protocol data packet too long: %s", s);

  packet = (unsigned char *) alloca (HDR_LENGTH + len + TRLR_LENGTH + 1);

  packet[HDR_INDX_SYN] = HDR_SET_SYN (1, len, mips_send_seq);
  packet[HDR_INDX_TYPE_LEN] = HDR_SET_TYPE_LEN (1, len, mips_send_seq);
  packet[HDR_INDX_LEN1] = HDR_SET_LEN1 (1, len, mips_send_seq);
  packet[HDR_INDX_SEQ] = HDR_SET_SEQ (1, len, mips_send_seq);

  memcpy (packet + HDR_LENGTH, s, len);

  cksum = mips_cksum (packet, packet + HDR_LENGTH, len);
  packet[HDR_LENGTH + len + TRLR_INDX_CSUM1] = TRLR_SET_CSUM1 (cksum);
  packet[HDR_LENGTH + len + TRLR_INDX_CSUM2] = TRLR_SET_CSUM2 (cksum);
  packet[HDR_LENGTH + len + TRLR_INDX_CSUM3] = TRLR_SET_CSUM3 (cksum);

  /* Increment the sequence number.  This will set mips_send_seq to
     the sequence number we expect in the acknowledgement.  */
  mips_send_seq = (mips_send_seq + 1) % SEQ_MODULOS;

  /* We can only have one outstanding data packet, so we just wait for
     the acknowledgement here.  Keep retransmitting the packet until
     we get one, or until we've tried too many times.  */
  for (try = 0; try < mips_send_retries; try++)
    {
      int garbage;
      int ch;

      if (remote_debug > 0)
	{
	  /* Don't use _filtered; we can't deal with a QUIT out of
	     target_wait, and I think this might be called from there.  */
	  packet[HDR_LENGTH + len + TRLR_LENGTH] = '\0';
	  fprintf_unfiltered (gdb_stdlog, "Writing \"%s\"\n", packet + 1);
	}

      if (serial_write (mips_desc, packet,
			HDR_LENGTH + len + TRLR_LENGTH) != 0)
	mips_error ("write to target failed: %s", safe_strerror (errno));

      if (!get_ack)
	return;

      garbage = 0;
      ch = 0;
      while (1)
	{
	  unsigned char hdr[HDR_LENGTH + 1];
	  unsigned char trlr[TRLR_LENGTH + 1];
	  int err;
	  unsigned int seq;

	  /* Get the packet header.  If we time out, resend the data
	     packet.  */
	  err = mips_receive_header (hdr, &garbage, ch, mips_retransmit_wait);
	  if (err != 0)
	    break;

	  ch = 0;

	  /* If we get a data packet, assume it is a duplicate and
	     ignore it.  FIXME: If the acknowledgement is lost, this
	     data packet may be the packet the remote sends after the
	     acknowledgement.  */
	  if (HDR_IS_DATA (hdr))
	    {
	      int i;

	      /* Ignore any errors raised whilst attempting to ignore
	         packet. */

	      len = HDR_GET_LEN (hdr);

	      for (i = 0; i < len; i++)
		{
		  int rch;

		  rch = mips_readchar (remote_timeout);
		  if (rch == SYN)
		    {
		      ch = SYN;
		      break;
		    }
		  if (rch == SERIAL_TIMEOUT)
		    break;
		  /* ignore the character */
		}

	      if (i == len)
		(void) mips_receive_trailer (trlr, &garbage, &ch,
					     remote_timeout);

	      /* We don't bother checking the checksum, or providing an
	         ACK to the packet. */
	      continue;
	    }

	  /* If the length is not 0, this is a garbled packet.  */
	  if (HDR_GET_LEN (hdr) != 0)
	    continue;

	  /* Get the packet trailer.  */
	  err = mips_receive_trailer (trlr, &garbage, &ch,
				      mips_retransmit_wait);

	  /* If we timed out, resend the data packet.  */
	  if (err == -1)
	    break;

	  /* If we got a bad character, reread the header.  */
	  if (err != 0)
	    continue;

	  /* If the checksum does not match the trailer checksum, this
	     is a bad packet; ignore it.  */
	  if (mips_cksum (hdr, (unsigned char *) NULL, 0)
	      != TRLR_GET_CKSUM (trlr))
	    continue;

	  if (remote_debug > 0)
	    {
	      hdr[HDR_LENGTH] = '\0';
	      trlr[TRLR_LENGTH] = '\0';
	      /* Don't use _filtered; we can't deal with a QUIT out of
	         target_wait, and I think this might be called from there.  */
	      fprintf_unfiltered (gdb_stdlog, "Got ack %d \"%s%s\"\n",
				  HDR_GET_SEQ (hdr), hdr + 1, trlr);
	    }

	  /* If this ack is for the current packet, we're done.  */
	  seq = HDR_GET_SEQ (hdr);
	  if (seq == mips_send_seq)
	    return;

	  /* If this ack is for the last packet, resend the current
	     packet.  */
	  if ((seq + 1) % SEQ_MODULOS == mips_send_seq)
	    break;

	  /* Otherwise this is a bad ack; ignore it.  Increment the
	     garbage count to ensure that we do not stay in this loop
	     forever.  */
	  ++garbage;
	}
    }

  mips_error ("Remote did not acknowledge packet");
}

/* Receive and acknowledge a packet, returning the data in BUFF (which
   should be DATA_MAXLEN + 1 bytes).  The protocol documentation
   implies that only the sender retransmits packets, so this code just
   waits silently for a packet.  It returns the length of the received
   packet.  If THROW_ERROR is nonzero, call error() on errors.  If not,
   don't print an error message and return -1.  */

static int
mips_receive_packet (char *buff, int throw_error, int timeout)
{
  int ch;
  int garbage;
  int len;
  unsigned char ack[HDR_LENGTH + TRLR_LENGTH + 1];
  int cksum;

  ch = 0;
  garbage = 0;
  while (1)
    {
      unsigned char hdr[HDR_LENGTH];
      unsigned char trlr[TRLR_LENGTH];
      int i;
      int err;

      if (mips_receive_header (hdr, &garbage, ch, timeout) != 0)
	{
	  if (throw_error)
	    mips_error ("Timed out waiting for remote packet");
	  else
	    return -1;
	}

      ch = 0;

      /* An acknowledgement is probably a duplicate; ignore it.  */
      if (!HDR_IS_DATA (hdr))
	{
	  len = HDR_GET_LEN (hdr);
	  /* Check if the length is valid for an ACK, we may aswell
	     try and read the remainder of the packet: */
	  if (len == 0)
	    {
	      /* Ignore the error condition, since we are going to
	         ignore the packet anyway. */
	      (void) mips_receive_trailer (trlr, &garbage, &ch, timeout);
	    }
	  /* Don't use _filtered; we can't deal with a QUIT out of
	     target_wait, and I think this might be called from there.  */
	  if (remote_debug > 0)
	    fprintf_unfiltered (gdb_stdlog, "Ignoring unexpected ACK\n");
	  continue;
	}

      len = HDR_GET_LEN (hdr);
      for (i = 0; i < len; i++)
	{
	  int rch;

	  rch = mips_readchar (timeout);
	  if (rch == SYN)
	    {
	      ch = SYN;
	      break;
	    }
	  if (rch == SERIAL_TIMEOUT)
	    {
	      if (throw_error)
		mips_error ("Timed out waiting for remote packet");
	      else
		return -1;
	    }
	  buff[i] = rch;
	}

      if (i < len)
	{
	  /* Don't use _filtered; we can't deal with a QUIT out of
	     target_wait, and I think this might be called from there.  */
	  if (remote_debug > 0)
	    fprintf_unfiltered (gdb_stdlog,
				"Got new SYN after %d chars (wanted %d)\n",
				i, len);
	  continue;
	}

      err = mips_receive_trailer (trlr, &garbage, &ch, timeout);
      if (err == -1)
	{
	  if (throw_error)
	    mips_error ("Timed out waiting for packet");
	  else
	    return -1;
	}
      if (err == -2)
	{
	  /* Don't use _filtered; we can't deal with a QUIT out of
	     target_wait, and I think this might be called from there.  */
	  if (remote_debug > 0)
	    fprintf_unfiltered (gdb_stdlog, "Got SYN when wanted trailer\n");
	  continue;
	}

      /* If this is the wrong sequence number, ignore it.  */
      if (HDR_GET_SEQ (hdr) != mips_receive_seq)
	{
	  /* Don't use _filtered; we can't deal with a QUIT out of
	     target_wait, and I think this might be called from there.  */
	  if (remote_debug > 0)
	    fprintf_unfiltered (gdb_stdlog,
				"Ignoring sequence number %d (want %d)\n",
				HDR_GET_SEQ (hdr), mips_receive_seq);
	  continue;
	}

      if (mips_cksum (hdr, buff, len) == TRLR_GET_CKSUM (trlr))
	break;

      if (remote_debug > 0)
	/* Don't use _filtered; we can't deal with a QUIT out of
	   target_wait, and I think this might be called from there.  */
	printf_unfiltered ("Bad checksum; data %d, trailer %d\n",
			   mips_cksum (hdr, buff, len),
			   TRLR_GET_CKSUM (trlr));

      /* The checksum failed.  Send an acknowledgement for the
         previous packet to tell the remote to resend the packet.  */
      ack[HDR_INDX_SYN] = HDR_SET_SYN (0, 0, mips_receive_seq);
      ack[HDR_INDX_TYPE_LEN] = HDR_SET_TYPE_LEN (0, 0, mips_receive_seq);
      ack[HDR_INDX_LEN1] = HDR_SET_LEN1 (0, 0, mips_receive_seq);
      ack[HDR_INDX_SEQ] = HDR_SET_SEQ (0, 0, mips_receive_seq);

      cksum = mips_cksum (ack, (unsigned char *) NULL, 0);

      ack[HDR_LENGTH + TRLR_INDX_CSUM1] = TRLR_SET_CSUM1 (cksum);
      ack[HDR_LENGTH + TRLR_INDX_CSUM2] = TRLR_SET_CSUM2 (cksum);
      ack[HDR_LENGTH + TRLR_INDX_CSUM3] = TRLR_SET_CSUM3 (cksum);

      if (remote_debug > 0)
	{
	  ack[HDR_LENGTH + TRLR_LENGTH] = '\0';
	  /* Don't use _filtered; we can't deal with a QUIT out of
	     target_wait, and I think this might be called from there.  */
	  printf_unfiltered ("Writing ack %d \"%s\"\n", mips_receive_seq,
			     ack + 1);
	}

      if (serial_write (mips_desc, ack, HDR_LENGTH + TRLR_LENGTH) != 0)
	{
	  if (throw_error)
	    mips_error ("write to target failed: %s", safe_strerror (errno));
	  else
	    return -1;
	}
    }

  if (remote_debug > 0)
    {
      buff[len] = '\0';
      /* Don't use _filtered; we can't deal with a QUIT out of
         target_wait, and I think this might be called from there.  */
      printf_unfiltered ("Got packet \"%s\"\n", buff);
    }

  /* We got the packet.  Send an acknowledgement.  */
  mips_receive_seq = (mips_receive_seq + 1) % SEQ_MODULOS;

  ack[HDR_INDX_SYN] = HDR_SET_SYN (0, 0, mips_receive_seq);
  ack[HDR_INDX_TYPE_LEN] = HDR_SET_TYPE_LEN (0, 0, mips_receive_seq);
  ack[HDR_INDX_LEN1] = HDR_SET_LEN1 (0, 0, mips_receive_seq);
  ack[HDR_INDX_SEQ] = HDR_SET_SEQ (0, 0, mips_receive_seq);

  cksum = mips_cksum (ack, (unsigned char *) NULL, 0);

  ack[HDR_LENGTH + TRLR_INDX_CSUM1] = TRLR_SET_CSUM1 (cksum);
  ack[HDR_LENGTH + TRLR_INDX_CSUM2] = TRLR_SET_CSUM2 (cksum);
  ack[HDR_LENGTH + TRLR_INDX_CSUM3] = TRLR_SET_CSUM3 (cksum);

  if (remote_debug > 0)
    {
      ack[HDR_LENGTH + TRLR_LENGTH] = '\0';
      /* Don't use _filtered; we can't deal with a QUIT out of
         target_wait, and I think this might be called from there.  */
      printf_unfiltered ("Writing ack %d \"%s\"\n", mips_receive_seq,
			 ack + 1);
    }

  if (serial_write (mips_desc, ack, HDR_LENGTH + TRLR_LENGTH) != 0)
    {
      if (throw_error)
	mips_error ("write to target failed: %s", safe_strerror (errno));
      else
	return -1;
    }

  return len;
}

/* Optionally send a request to the remote system and optionally wait
   for the reply.  This implements the remote debugging protocol,
   which is built on top of the packet protocol defined above.  Each
   request has an ADDR argument and a DATA argument.  The following
   requests are defined:

   \0   don't send a request; just wait for a reply
   i    read word from instruction space at ADDR
   d    read word from data space at ADDR
   I    write DATA to instruction space at ADDR
   D    write DATA to data space at ADDR
   r    read register number ADDR
   R    set register number ADDR to value DATA
   c    continue execution (if ADDR != 1, set pc to ADDR)
   s    single step (if ADDR != 1, set pc to ADDR)

   The read requests return the value requested.  The write requests
   return the previous value in the changed location.  The execution
   requests return a UNIX wait value (the approximate signal which
   caused execution to stop is in the upper eight bits).

   If PERR is not NULL, this function waits for a reply.  If an error
   occurs, it sets *PERR to 1 and sets errno according to what the
   target board reports.  */

static ULONGEST
mips_request (int cmd,
	      ULONGEST addr,
	      ULONGEST data,
	      int *perr,
	      int timeout,
	      char *buff)
{
  char myBuff[DATA_MAXLEN + 1];
  int len;
  int rpid;
  char rcmd;
  int rerrflg;
  unsigned long rresponse;

  if (buff == (char *) NULL)
    buff = myBuff;

  if (cmd != '\0')
    {
      if (mips_need_reply)
	internal_error (__FILE__, __LINE__,
			"mips_request: Trying to send command before reply");
      sprintf (buff, "0x0 %c 0x%s 0x%s", cmd, paddr_nz (addr), paddr_nz (data));
      mips_send_packet (buff, 1);
      mips_need_reply = 1;
    }

  if (perr == (int *) NULL)
    return 0;

  if (!mips_need_reply)
    internal_error (__FILE__, __LINE__,
		    "mips_request: Trying to get reply before command");

  mips_need_reply = 0;

  len = mips_receive_packet (buff, 1, timeout);
  buff[len] = '\0';

  if (sscanf (buff, "0x%x %c 0x%x 0x%lx",
	      &rpid, &rcmd, &rerrflg, &rresponse) != 4
      || (cmd != '\0' && rcmd != cmd))
    mips_error ("Bad response from remote board");

  if (rerrflg != 0)
    {
      *perr = 1;

      /* FIXME: This will returns MIPS errno numbers, which may or may
         not be the same as errno values used on other systems.  If
         they stick to common errno values, they will be the same, but
         if they don't, they must be translated.  */
      errno = rresponse;

      return 0;
    }

  *perr = 0;
  return rresponse;
}

static void
mips_initialize_cleanups (void *arg)
{
  mips_initializing = 0;
}

static void
mips_exit_cleanups (void *arg)
{
  mips_exiting = 0;
}

static void
mips_send_command (const char *cmd, int prompt)
{
  serial_write (mips_desc, cmd, strlen (cmd));
  mips_expect (cmd);
  mips_expect ("\n");
  if (prompt)
    mips_expect (mips_monitor_prompt);
}

/* Enter remote (dbx) debug mode: */
static void
mips_enter_debug (void)
{
  /* Reset the sequence numbers, ready for the new debug sequence: */
  mips_send_seq = 0;
  mips_receive_seq = 0;

  if (mips_monitor != MON_IDT)
    mips_send_command ("debug\r", 0);
  else				/* assume IDT monitor by default */
    mips_send_command ("db tty0\r", 0);

  sleep (1);
  serial_write (mips_desc, "\r", sizeof "\r" - 1);

  /* We don't need to absorb any spurious characters here, since the
     mips_receive_header will eat up a reasonable number of characters
     whilst looking for the SYN, however this avoids the "garbage"
     being displayed to the user. */
  if (mips_monitor != MON_IDT)
    mips_expect ("\r");

  {
    char buff[DATA_MAXLEN + 1];
    if (mips_receive_packet (buff, 1, 3) < 0)
      mips_error ("Failed to initialize (didn't receive packet).");
  }
}

/* Exit remote (dbx) debug mode, returning to the monitor prompt: */
static int
mips_exit_debug (void)
{
  int err;
  struct cleanup *old_cleanups = make_cleanup (mips_exit_cleanups, NULL);

  mips_exiting = 1;

  if (mips_monitor != MON_IDT)
    {
      /* The DDB (NEC) and MiniRISC (LSI) versions of PMON exit immediately,
         so we do not get a reply to this command: */
      mips_request ('x', 0, 0, NULL, mips_receive_wait, NULL);
      mips_need_reply = 0;
      if (!mips_expect (" break!"))
	return -1;
    }
  else
    mips_request ('x', 0, 0, &err, mips_receive_wait, NULL);

  if (!mips_expect (mips_monitor_prompt))
    return -1;

  do_cleanups (old_cleanups);

  return 0;
}

/* Initialize a new connection to the MIPS board, and make sure we are
   really connected.  */

static void
mips_initialize (void)
{
  int err;
  struct cleanup *old_cleanups = make_cleanup (mips_initialize_cleanups, NULL);
  int j;

  /* What is this code doing here?  I don't see any way it can happen, and
     it might mean mips_initializing didn't get cleared properly.
     So I'll make it a warning.  */

  if (mips_initializing)
    {
      warning ("internal error: mips_initialize called twice");
      return;
    }

  mips_wait_flag = 0;
  mips_initializing = 1;

  /* At this point, the packit protocol isn't responding.  We'll try getting
     into the monitor, and restarting the protocol.  */

  /* Force the system into the monitor.  After this we *should* be at
     the mips_monitor_prompt.  */
  if (mips_monitor != MON_IDT)
    j = 0;			/* start by checking if we are already at the prompt */
  else
    j = 1;			/* start by sending a break */
  for (; j <= 4; j++)
    {
      switch (j)
	{
	case 0:		/* First, try sending a CR */
	  serial_flush_input (mips_desc);
	  serial_write (mips_desc, "\r", 1);
	  break;
	case 1:		/* First, try sending a break */
	  serial_send_break (mips_desc);
	  break;
	case 2:		/* Then, try a ^C */
	  serial_write (mips_desc, "\003", 1);
	  break;
	case 3:		/* Then, try escaping from download */
	  {
	    if (mips_monitor != MON_IDT)
	      {
		char tbuff[7];

		/* We shouldn't need to send multiple termination
		   sequences, since the target performs line (or
		   block) reads, and then processes those
		   packets. In-case we were downloading a large packet
		   we flush the output buffer before inserting a
		   termination sequence. */
		serial_flush_output (mips_desc);
		sprintf (tbuff, "\r/E/E\r");
		serial_write (mips_desc, tbuff, 6);
	      }
	    else
	      {
		char srec[10];
		int i;

		/* We are possibly in binary download mode, having
		   aborted in the middle of an S-record.  ^C won't
		   work because of binary mode.  The only reliable way
		   out is to send enough termination packets (8 bytes)
		   to fill up and then overflow the largest size
		   S-record (255 bytes in this case).  This amounts to
		   256/8 + 1 packets.
		 */

		mips_make_srec (srec, '7', 0, NULL, 0);

		for (i = 1; i <= 33; i++)
		  {
		    serial_write (mips_desc, srec, 8);

		    if (serial_readchar (mips_desc, 0) >= 0)
		      break;	/* Break immediatly if we get something from
				   the board. */
		  }
	      }
	  }
	  break;
	case 4:
	  mips_error ("Failed to initialize.");
	}

      if (mips_expect (mips_monitor_prompt))
	break;
    }

  if (mips_monitor != MON_IDT)
    {
      /* Sometimes PMON ignores the first few characters in the first
         command sent after a load.  Sending a blank command gets
         around that.  */
      mips_send_command ("\r", -1);

      /* Ensure the correct target state: */
      if (mips_monitor != MON_LSI)
	mips_send_command ("set regsize 64\r", -1);
      mips_send_command ("set hostport tty0\r", -1);
      mips_send_command ("set brkcmd \"\"\r", -1);
      /* Delete all the current breakpoints: */
      mips_send_command ("db *\r", -1);
      /* NOTE: PMON does not have breakpoint support through the
         "debug" mode, only at the monitor command-line. */
    }

  mips_enter_debug ();

  /* Clear all breakpoints: */
  if ((mips_monitor == MON_IDT
       && clear_breakpoint (-1, 0, BREAK_UNUSED) == 0)
      || mips_monitor == MON_LSI)
    monitor_supports_breakpoints = 1;
  else
    monitor_supports_breakpoints = 0;

  do_cleanups (old_cleanups);

  /* If this doesn't call error, we have connected; we don't care if
     the request itself succeeds or fails.  */

  mips_request ('r', 0, 0, &err, mips_receive_wait, NULL);
}

/* Open a connection to the remote board.  */
static void
common_open (struct target_ops *ops, char *name, int from_tty,
	     enum mips_monitor_type new_monitor,
	     const char *new_monitor_prompt)
{
  char *ptype;
  char *serial_port_name;
  char *remote_name = 0;
  char *local_name = 0;
  char **argv;

  if (name == 0)
    error (
	    "To open a MIPS remote debugging connection, you need to specify what serial\n\
device is attached to the target board (e.g., /dev/ttya).\n"
	    "If you want to use TFTP to download to the board, specify the name of a\n"
	    "temporary file to be used by GDB for downloads as the second argument.\n"
	    "This filename must be in the form host:filename, where host is the name\n"
	    "of the host running the TFTP server, and the file must be readable by the\n"
	    "world.  If the local name of the temporary file differs from the name as\n"
	    "seen from the board via TFTP, specify that name as the third parameter.\n");

  /* Parse the serial port name, the optional TFTP name, and the
     optional local TFTP name.  */
  if ((argv = buildargv (name)) == NULL)
    nomem (0);
  make_cleanup_freeargv (argv);

  serial_port_name = xstrdup (argv[0]);
  if (argv[1])			/* remote TFTP name specified? */
    {
      remote_name = argv[1];
      if (argv[2])		/* local TFTP filename specified? */
	local_name = argv[2];
    }

  target_preopen (from_tty);

  if (mips_is_open)
    unpush_target (current_ops);

  /* Open and initialize the serial port.  */
  mips_desc = serial_open (serial_port_name);
  if (mips_desc == NULL)
    perror_with_name (serial_port_name);

  if (baud_rate != -1)
    {
      if (serial_setbaudrate (mips_desc, baud_rate))
	{
	  serial_close (mips_desc);
	  perror_with_name (serial_port_name);
	}
    }

  serial_raw (mips_desc);

  /* Open and initialize the optional download port.  If it is in the form
     hostname#portnumber, it's a UDP socket.  If it is in the form
     hostname:filename, assume it's the TFTP filename that must be
     passed to the DDB board to tell it where to get the load file.  */
  if (remote_name)
    {
      if (strchr (remote_name, '#'))
	{
	  udp_desc = serial_open (remote_name);
	  if (!udp_desc)
	    perror_with_name ("Unable to open UDP port");
	  udp_in_use = 1;
	}
      else
	{
	  /* Save the remote and local names of the TFTP temp file.  If
	     the user didn't specify a local name, assume it's the same
	     as the part of the remote name after the "host:".  */
	  if (tftp_name)
	    xfree (tftp_name);
	  if (tftp_localname)
	    xfree (tftp_localname);
	  if (local_name == NULL)
	    if ((local_name = strchr (remote_name, ':')) != NULL)
	      local_name++;	/* skip over the colon */
	  if (local_name == NULL)
	    local_name = remote_name;	/* local name same as remote name */
	  tftp_name = xstrdup (remote_name);
	  tftp_localname = xstrdup (local_name);
	  tftp_in_use = 1;
	}
    }

  current_ops = ops;
  mips_is_open = 1;

  /* Reset the expected monitor prompt if it's never been set before.  */
  if (mips_monitor_prompt == NULL)
    mips_monitor_prompt = xstrdup (new_monitor_prompt);
  mips_monitor = new_monitor;

  mips_initialize ();

  if (from_tty)
    printf_unfiltered ("Remote MIPS debugging using %s\n", serial_port_name);

  /* Switch to using remote target now.  */
  push_target (ops);

  /* FIXME: Should we call start_remote here?  */

  /* Try to figure out the processor model if possible.  */
  deprecated_mips_set_processor_regs_hack ();

  /* This is really the job of start_remote however, that makes an
     assumption that the target is about to print out a status message
     of some sort.  That doesn't happen here (in fact, it may not be
     possible to get the monitor to send the appropriate packet).  */

  flush_cached_frames ();
  registers_changed ();
  stop_pc = read_pc ();
  print_stack_frame (get_selected_frame (), -1, 1);
  xfree (serial_port_name);
}

static void
mips_open (char *name, int from_tty)
{
  const char *monitor_prompt = NULL;
  if (TARGET_ARCHITECTURE != NULL
      && TARGET_ARCHITECTURE->arch == bfd_arch_mips)
    {
    switch (TARGET_ARCHITECTURE->mach)
      {
      case bfd_mach_mips4100:
      case bfd_mach_mips4300:
      case bfd_mach_mips4600:
      case bfd_mach_mips4650:
      case bfd_mach_mips5000:
	monitor_prompt = "<RISQ> ";
	break;
      }
    }
  if (monitor_prompt == NULL)
    monitor_prompt = "<IDT>";
  common_open (&mips_ops, name, from_tty, MON_IDT, monitor_prompt);
}

static void
pmon_open (char *name, int from_tty)
{
  common_open (&pmon_ops, name, from_tty, MON_PMON, "PMON> ");
}

static void
ddb_open (char *name, int from_tty)
{
  common_open (&ddb_ops, name, from_tty, MON_DDB, "NEC010>");
}

static void
lsi_open (char *name, int from_tty)
{
  int i;

  /* Clear the LSI breakpoint table.  */
  for (i = 0; i < MAX_LSI_BREAKPOINTS; i++)
    lsi_breakpoints[i].type = BREAK_UNUSED;

  common_open (&lsi_ops, name, from_tty, MON_LSI, "PMON> ");
}

/* Close a connection to the remote board.  */

static void
mips_close (int quitting)
{
  if (mips_is_open)
    {
      /* Get the board out of remote debugging mode.  */
      (void) mips_exit_debug ();

      close_ports ();
    }
}

/* Detach from the remote board.  */

static void
mips_detach (char *args, int from_tty)
{
  if (args)
    error ("Argument given to \"detach\" when remotely debugging.");

  pop_target ();

  mips_close (1);

  if (from_tty)
    printf_unfiltered ("Ending remote MIPS debugging.\n");
}

/* Tell the target board to resume.  This does not wait for a reply
   from the board, except in the case of single-stepping on LSI boards,
   where PMON does return a reply.  */

static void
mips_resume (ptid_t ptid, int step, enum target_signal siggnal)
{
  int err;

  /* LSI PMON requires returns a reply packet "0x1 s 0x0 0x57f" after
     a single step, so we wait for that.  */
  mips_request (step ? 's' : 'c', 1, siggnal,
		mips_monitor == MON_LSI && step ? &err : (int *) NULL,
		mips_receive_wait, NULL);
}

/* Return the signal corresponding to SIG, where SIG is the number which
   the MIPS protocol uses for the signal.  */
static enum target_signal
mips_signal_from_protocol (int sig)
{
  /* We allow a few more signals than the IDT board actually returns, on
     the theory that there is at least *some* hope that perhaps the numbering
     for these signals is widely agreed upon.  */
  if (sig <= 0
      || sig > 31)
    return TARGET_SIGNAL_UNKNOWN;

  /* Don't want to use target_signal_from_host because we are converting
     from MIPS signal numbers, not host ones.  Our internal numbers
     match the MIPS numbers for the signals the board can return, which
     are: SIGINT, SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP.  */
  return (enum target_signal) sig;
}

/* Wait until the remote stops, and return a wait status.  */

static ptid_t
mips_wait (ptid_t ptid, struct target_waitstatus *status)
{
  int rstatus;
  int err;
  char buff[DATA_MAXLEN];
  int rpc, rfp, rsp;
  char flags[20];
  int nfields;
  int i;

  interrupt_count = 0;
  hit_watchpoint = 0;

  /* If we have not sent a single step or continue command, then the
     board is waiting for us to do something.  Return a status
     indicating that it is stopped.  */
  if (!mips_need_reply)
    {
      status->kind = TARGET_WAITKIND_STOPPED;
      status->value.sig = TARGET_SIGNAL_TRAP;
      return inferior_ptid;
    }

  /* No timeout; we sit here as long as the program continues to execute.  */
  mips_wait_flag = 1;
  rstatus = mips_request ('\000', 0, 0, &err, -1, buff);
  mips_wait_flag = 0;
  if (err)
    mips_error ("Remote failure: %s", safe_strerror (errno));

  /* On returning from a continue, the PMON monitor seems to start
     echoing back the messages we send prior to sending back the
     ACK. The code can cope with this, but to try and avoid the
     unnecessary serial traffic, and "spurious" characters displayed
     to the user, we cheat and reset the debug protocol. The problems
     seems to be caused by a check on the number of arguments, and the
     command length, within the monitor causing it to echo the command
     as a bad packet. */
  if (mips_monitor == MON_PMON)
    {
      mips_exit_debug ();
      mips_enter_debug ();
    }

  /* See if we got back extended status.  If so, pick out the pc, fp, sp, etc... */

  nfields = sscanf (buff, "0x%*x %*c 0x%*x 0x%*x 0x%x 0x%x 0x%x 0x%*x %s",
		    &rpc, &rfp, &rsp, flags);
  if (nfields >= 3)
    {
      char buf[MAX_REGISTER_SIZE];

      store_unsigned_integer (buf, DEPRECATED_REGISTER_RAW_SIZE (PC_REGNUM), rpc);
      supply_register (PC_REGNUM, buf);

      store_unsigned_integer (buf, DEPRECATED_REGISTER_RAW_SIZE (PC_REGNUM), rfp);
      supply_register (30, buf);	/* This register they are avoiding and so it is unnamed */

      store_unsigned_integer (buf, DEPRECATED_REGISTER_RAW_SIZE (SP_REGNUM), rsp);
      supply_register (SP_REGNUM, buf);

      store_unsigned_integer (buf, DEPRECATED_REGISTER_RAW_SIZE (DEPRECATED_FP_REGNUM), 0);
      supply_register (DEPRECATED_FP_REGNUM, buf);

      if (nfields == 9)
	{
	  int i;

	  for (i = 0; i <= 2; i++)
	    if (flags[i] == 'r' || flags[i] == 'w')
	      hit_watchpoint = 1;
	    else if (flags[i] == '\000')
	      break;
	}
    }

  if (strcmp (target_shortname, "lsi") == 0)
    {
#if 0
      /* If this is an LSI PMON target, see if we just hit a hardrdware watchpoint.
         Right now, PMON doesn't give us enough information to determine which
         breakpoint we hit.  So we have to look up the PC in our own table
         of breakpoints, and if found, assume it's just a normal instruction
         fetch breakpoint, not a data watchpoint.  FIXME when PMON
         provides some way to tell us what type of breakpoint it is.  */
      int i;
      CORE_ADDR pc = read_pc ();

      hit_watchpoint = 1;
      for (i = 0; i < MAX_LSI_BREAKPOINTS; i++)
	{
	  if (lsi_breakpoints[i].addr == pc
	      && lsi_breakpoints[i].type == BREAK_FETCH)
	    {
	      hit_watchpoint = 0;
	      break;
	    }
	}
#else
      /* If a data breakpoint was hit, PMON returns the following packet:
         0x1 c 0x0 0x57f 0x1
         The return packet from an ordinary breakpoint doesn't have the
         extra 0x01 field tacked onto the end.  */
      if (nfields == 1 && rpc == 1)
	hit_watchpoint = 1;
#endif
    }

  /* NOTE: The following (sig) numbers are defined by PMON:
     SPP_SIGTRAP     5       breakpoint
     SPP_SIGINT      2
     SPP_SIGSEGV     11
     SPP_SIGBUS      10
     SPP_SIGILL      4
     SPP_SIGFPE      8
     SPP_SIGTERM     15 */

  /* Translate a MIPS waitstatus.  We use constants here rather than WTERMSIG
     and so on, because the constants we want here are determined by the
     MIPS protocol and have nothing to do with what host we are running on.  */
  if ((rstatus & 0xff) == 0)
    {
      status->kind = TARGET_WAITKIND_EXITED;
      status->value.integer = (((rstatus) >> 8) & 0xff);
    }
  else if ((rstatus & 0xff) == 0x7f)
    {
      status->kind = TARGET_WAITKIND_STOPPED;
      status->value.sig = mips_signal_from_protocol (((rstatus) >> 8) & 0xff);

      /* If the stop PC is in the _exit function, assume
         we hit the 'break 0x3ff' instruction in _exit, so this
         is not a normal breakpoint.  */
      if (strcmp (target_shortname, "lsi") == 0)
	{
	  char *func_name;
	  CORE_ADDR func_start;
	  CORE_ADDR pc = read_pc ();

	  find_pc_partial_function (pc, &func_name, &func_start, NULL);
	  if (func_name != NULL && strcmp (func_name, "_exit") == 0
	      && func_start == pc)
	    status->kind = TARGET_WAITKIND_EXITED;
	}
    }
  else
    {
      status->kind = TARGET_WAITKIND_SIGNALLED;
      status->value.sig = mips_signal_from_protocol (rstatus & 0x7f);
    }

  return inferior_ptid;
}

/* We have to map between the register numbers used by gdb and the
   register numbers used by the debugging protocol.  This function
   assumes that we are using tm-mips.h.  */

#define REGNO_OFFSET 96

static int
mips_map_regno (int regno)
{
  if (regno < 32)
    return regno;
  if (regno >= mips_regnum (current_gdbarch)->fp0
      && regno < mips_regnum (current_gdbarch)->fp0 + 32)
    return regno - mips_regnum (current_gdbarch)->fp0 + 32;
  else if (regno == mips_regnum (current_gdbarch)->pc)
    return REGNO_OFFSET + 0;
  else if (regno == mips_regnum (current_gdbarch)->cause)
    return REGNO_OFFSET + 1;
  else if (regno == mips_regnum (current_gdbarch)->hi)
    return REGNO_OFFSET + 2;
  else if (regno == mips_regnum (current_gdbarch)->lo)
    return REGNO_OFFSET + 3;
  else if (regno == mips_regnum (current_gdbarch)->fp_control_status)
    return REGNO_OFFSET + 4;
  else if (regno == mips_regnum (current_gdbarch)->fp_implementation_revision)
    return REGNO_OFFSET + 5;
  else
    /* FIXME: Is there a way to get the status register?  */
    return 0;
}

/* Fetch the remote registers.  */

static void
mips_fetch_registers (int regno)
{
  unsigned LONGEST val;
  int err;

  if (regno == -1)
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	mips_fetch_registers (regno);
      return;
    }

  if (regno == DEPRECATED_FP_REGNUM || regno == ZERO_REGNUM)
    /* DEPRECATED_FP_REGNUM on the mips is a hack which is just
       supposed to read zero (see also mips-nat.c).  */
    val = 0;
  else
    {
      /* If PMON doesn't support this register, don't waste serial
         bandwidth trying to read it.  */
      int pmon_reg = mips_map_regno (regno);
      if (regno != 0 && pmon_reg == 0)
	val = 0;
      else
	{
	  /* Unfortunately the PMON version in the Vr4300 board has been
	     compiled without the 64bit register access commands. This
	     means we cannot get hold of the full register width. */
	  if (mips_monitor == MON_DDB)
	    val = (unsigned) mips_request ('t', pmon_reg, 0,
					   &err, mips_receive_wait, NULL);
	  else
	    val = mips_request ('r', pmon_reg, 0,
				&err, mips_receive_wait, NULL);
	  if (err)
	    mips_error ("Can't read register %d: %s", regno,
			safe_strerror (errno));
	}
    }

  {
    char buf[MAX_REGISTER_SIZE];

    /* We got the number the register holds, but gdb expects to see a
       value in the target byte ordering.  */
    store_unsigned_integer (buf, DEPRECATED_REGISTER_RAW_SIZE (regno), val);
    supply_register (regno, buf);
  }
}

/* Prepare to store registers.  The MIPS protocol can store individual
   registers, so this function doesn't have to do anything.  */

static void
mips_prepare_to_store (void)
{
}

/* Store remote register(s).  */

static void
mips_store_registers (int regno)
{
  int err;

  if (regno == -1)
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	mips_store_registers (regno);
      return;
    }

  mips_request ('R', mips_map_regno (regno),
		read_register (regno),
		&err, mips_receive_wait, NULL);
  if (err)
    mips_error ("Can't write register %d: %s", regno, safe_strerror (errno));
}

/* Fetch a word from the target board.  */

static unsigned int
mips_fetch_word (CORE_ADDR addr)
{
  unsigned int val;
  int err;

  val = mips_request ('d', addr, 0, &err, mips_receive_wait, NULL);
  if (err)
    {
      /* Data space failed; try instruction space.  */
      val = mips_request ('i', addr, 0, &err,
			  mips_receive_wait, NULL);
      if (err)
	mips_error ("Can't read address 0x%s: %s",
		    paddr_nz (addr), safe_strerror (errno));
    }
  return val;
}

/* Store a word to the target board.  Returns errno code or zero for
   success.  If OLD_CONTENTS is non-NULL, put the old contents of that
   memory location there.  */

/* FIXME! make sure only 32-bit quantities get stored! */
static int
mips_store_word (CORE_ADDR addr, unsigned int val, char *old_contents)
{
  int err;
  unsigned int oldcontents;

  oldcontents = mips_request ('D', addr, val, &err,
			      mips_receive_wait, NULL);
  if (err)
    {
      /* Data space failed; try instruction space.  */
      oldcontents = mips_request ('I', addr, val, &err,
				  mips_receive_wait, NULL);
      if (err)
	return errno;
    }
  if (old_contents != NULL)
    store_unsigned_integer (old_contents, 4, oldcontents);
  return 0;
}

/* Read or write LEN bytes from inferior memory at MEMADDR,
   transferring to or from debugger address MYADDR.  Write to inferior
   if SHOULD_WRITE is nonzero.  Returns length of data written or
   read; 0 for error.  Note that protocol gives us the correct value
   for a longword, since it transfers values in ASCII.  We want the
   byte values, so we have to swap the longword values.  */

static int mask_address_p = 1;

static int
mips_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int write,
		  struct mem_attrib *attrib, struct target_ops *target)
{
  int i;
  CORE_ADDR addr;
  int count;
  char *buffer;
  int status;

  /* PMON targets do not cope well with 64 bit addresses.  Mask the
     value down to 32 bits. */
  if (mask_address_p)
    memaddr &= (CORE_ADDR) 0xffffffff;

  /* Round starting address down to longword boundary.  */
  addr = memaddr & ~3;
  /* Round ending address up; get number of longwords that makes.  */
  count = (((memaddr + len) - addr) + 3) / 4;
  /* Allocate buffer of that many longwords.  */
  buffer = alloca (count * 4);

  if (write)
    {
      /* Fill start and end extra bytes of buffer with existing data.  */
      if (addr != memaddr || len < 4)
	{
	  /* Need part of initial word -- fetch it.  */
	  store_unsigned_integer (&buffer[0], 4, mips_fetch_word (addr));
	}

      if (count > 1)
	{
	  /* Need part of last word -- fetch it.  FIXME: we do this even
	     if we don't need it.  */
	  store_unsigned_integer (&buffer[(count - 1) * 4], 4,
				  mips_fetch_word (addr + (count - 1) * 4));
	}

      /* Copy data to be written over corresponding part of buffer */

      memcpy ((char *) buffer + (memaddr & 3), myaddr, len);

      /* Write the entire buffer.  */

      for (i = 0; i < count; i++, addr += 4)
	{
	  status = mips_store_word (addr,
			       extract_unsigned_integer (&buffer[i * 4], 4),
				    NULL);
	  /* Report each kilobyte (we download 32-bit words at a time) */
	  if (i % 256 == 255)
	    {
	      printf_unfiltered ("*");
	      gdb_flush (gdb_stdout);
	    }
	  if (status)
	    {
	      errno = status;
	      return 0;
	    }
	  /* FIXME: Do we want a QUIT here?  */
	}
      if (count >= 256)
	printf_unfiltered ("\n");
    }
  else
    {
      /* Read all the longwords */
      for (i = 0; i < count; i++, addr += 4)
	{
	  store_unsigned_integer (&buffer[i * 4], 4, mips_fetch_word (addr));
	  QUIT;
	}

      /* Copy appropriate bytes out of the buffer.  */
      memcpy (myaddr, buffer + (memaddr & 3), len);
    }
  return len;
}

/* Print info on this target.  */

static void
mips_files_info (struct target_ops *ignore)
{
  printf_unfiltered ("Debugging a MIPS board over a serial line.\n");
}

/* Kill the process running on the board.  This will actually only
   work if we are doing remote debugging over the console input.  I
   think that if IDT/sim had the remote debug interrupt enabled on the
   right port, we could interrupt the process with a break signal.  */

static void
mips_kill (void)
{
  if (!mips_wait_flag)
    return;

  interrupt_count++;

  if (interrupt_count >= 2)
    {
      interrupt_count = 0;

      target_terminal_ours ();

      if (query ("Interrupted while waiting for the program.\n\
Give up (and stop debugging it)? "))
	{
	  /* Clean up in such a way that mips_close won't try to talk to the
	     board (it almost surely won't work since we weren't able to talk to
	     it).  */
	  mips_wait_flag = 0;
	  close_ports ();

	  printf_unfiltered ("Ending remote MIPS debugging.\n");
	  target_mourn_inferior ();

	  throw_exception (RETURN_QUIT);
	}

      target_terminal_inferior ();
    }

  if (remote_debug > 0)
    printf_unfiltered ("Sending break\n");

  serial_send_break (mips_desc);

#if 0
  if (mips_is_open)
    {
      char cc;

      /* Send a ^C.  */
      cc = '\003';
      serial_write (mips_desc, &cc, 1);
      sleep (1);
      target_mourn_inferior ();
    }
#endif
}

/* Start running on the target board.  */

static void
mips_create_inferior (char *execfile, char *args, char **env)
{
  CORE_ADDR entry_pt;

  if (args && *args)
    {
      warning ("\
Can't pass arguments to remote MIPS board; arguments ignored.");
      /* And don't try to use them on the next "run" command.  */
      execute_command ("set args", 0);
    }

  if (execfile == 0 || exec_bfd == 0)
    error ("No executable file specified");

  entry_pt = (CORE_ADDR) bfd_get_start_address (exec_bfd);

  init_wait_for_inferior ();

  /* FIXME: Should we set inferior_ptid here?  */

  proceed (entry_pt, TARGET_SIGNAL_DEFAULT, 0);
}

/* Clean up after a process.  Actually nothing to do.  */

static void
mips_mourn_inferior (void)
{
  if (current_ops != NULL)
    unpush_target (current_ops);
  generic_mourn_inferior ();
}

/* We can write a breakpoint and read the shadow contents in one
   operation.  */

/* Insert a breakpoint.  On targets that don't have built-in
   breakpoint support, we read the contents of the target location and
   stash it, then overwrite it with a breakpoint instruction.  ADDR is
   the target location in the target machine.  CONTENTS_CACHE is a
   pointer to memory allocated for saving the target contents.  It is
   guaranteed by the caller to be long enough to save the breakpoint
   length returned by BREAKPOINT_FROM_PC.  */

static int
mips_insert_breakpoint (CORE_ADDR addr, char *contents_cache)
{
  if (monitor_supports_breakpoints)
    return set_breakpoint (addr, MIPS_INSTLEN, BREAK_FETCH);
  else
    return memory_insert_breakpoint (addr, contents_cache);
}

static int
mips_remove_breakpoint (CORE_ADDR addr, char *contents_cache)
{
  if (monitor_supports_breakpoints)
    return clear_breakpoint (addr, MIPS_INSTLEN, BREAK_FETCH);
  else
    return memory_remove_breakpoint (addr, contents_cache);
}

/* Tell whether this target can support a hardware breakpoint.  CNT
   is the number of hardware breakpoints already installed.  This
   implements the TARGET_CAN_USE_HARDWARE_WATCHPOINT macro.  */

int
mips_can_use_watchpoint (int type, int cnt, int othertype)
{
  return cnt < MAX_LSI_BREAKPOINTS && strcmp (target_shortname, "lsi") == 0;
}


/* Compute a don't care mask for the region bounding ADDR and ADDR + LEN - 1.
   This is used for memory ref breakpoints.  */

static unsigned long
calculate_mask (CORE_ADDR addr, int len)
{
  unsigned long mask;
  int i;

  mask = addr ^ (addr + len - 1);

  for (i = 32; i >= 0; i--)
    if (mask == 0)
      break;
    else
      mask >>= 1;

  mask = (unsigned long) 0xffffffff >> i;

  return mask;
}


/* Set a data watchpoint.  ADDR and LEN should be obvious.  TYPE is 0
   for a write watchpoint, 1 for a read watchpoint, or 2 for a read/write
   watchpoint. */

int
mips_insert_watchpoint (CORE_ADDR addr, int len, int type)
{
  if (set_breakpoint (addr, len, type))
    return -1;

  return 0;
}

int
mips_remove_watchpoint (CORE_ADDR addr, int len, int type)
{
  if (clear_breakpoint (addr, len, type))
    return -1;

  return 0;
}

int
mips_stopped_by_watchpoint (void)
{
  return hit_watchpoint;
}


/* Insert a breakpoint.  */

static int
set_breakpoint (CORE_ADDR addr, int len, enum break_type type)
{
  return common_breakpoint (1, addr, len, type);
}


/* Clear a breakpoint.  */

static int
clear_breakpoint (CORE_ADDR addr, int len, enum break_type type)
{
  return common_breakpoint (0, addr, len, type);
}


/* Check the error code from the return packet for an LSI breakpoint
   command.  If there's no error, just return 0.  If it's a warning,
   print the warning text and return 0.  If it's an error, print
   the error text and return 1.  <ADDR> is the address of the breakpoint
   that was being set.  <RERRFLG> is the error code returned by PMON. 
   This is a helper function for common_breakpoint.  */

static int
check_lsi_error (CORE_ADDR addr, int rerrflg)
{
  struct lsi_error *err;
  char *saddr = paddr_nz (addr);	/* printable address string */

  if (rerrflg == 0)		/* no error */
    return 0;

  /* Warnings can be ORed together, so check them all.  */
  if (rerrflg & W_WARN)
    {
      if (monitor_warnings)
	{
	  int found = 0;
	  for (err = lsi_warning_table; err->code != 0; err++)
	    {
	      if ((err->code & rerrflg) == err->code)
		{
		  found = 1;
		  fprintf_unfiltered (gdb_stderr,
				  "common_breakpoint (0x%s): Warning: %s\n",
				      saddr,
				      err->string);
		}
	    }
	  if (!found)
	    fprintf_unfiltered (gdb_stderr,
			"common_breakpoint (0x%s): Unknown warning: 0x%x\n",
				saddr,
				rerrflg);
	}
      return 0;
    }

  /* Errors are unique, i.e. can't be ORed together.  */
  for (err = lsi_error_table; err->code != 0; err++)
    {
      if ((err->code & rerrflg) == err->code)
	{
	  fprintf_unfiltered (gdb_stderr,
			      "common_breakpoint (0x%s): Error: %s\n",
			      saddr,
			      err->string);
	  return 1;
	}
    }
  fprintf_unfiltered (gdb_stderr,
		      "common_breakpoint (0x%s): Unknown error: 0x%x\n",
		      saddr,
		      rerrflg);
  return 1;
}


/* This routine sends a breakpoint command to the remote target.

   <SET> is 1 if setting a breakpoint, or 0 if clearing a breakpoint.
   <ADDR> is the address of the breakpoint.
   <LEN> the length of the region to break on.
   <TYPE> is the type of breakpoint:
   0 = write                    (BREAK_WRITE)
   1 = read                     (BREAK_READ)
   2 = read/write               (BREAK_ACCESS)
   3 = instruction fetch        (BREAK_FETCH)

   Return 0 if successful; otherwise 1.  */

static int
common_breakpoint (int set, CORE_ADDR addr, int len, enum break_type type)
{
  char buf[DATA_MAXLEN + 1];
  char cmd, rcmd;
  int rpid, rerrflg, rresponse, rlen;
  int nfields;

  addr = ADDR_BITS_REMOVE (addr);

  if (mips_monitor == MON_LSI)
    {
      if (set == 0)		/* clear breakpoint */
	{
	  /* The LSI PMON "clear breakpoint" has this form:
	     <pid> 'b' <bptn> 0x0
	     reply:
	     <pid> 'b' 0x0 <code>

	     <bptn> is a breakpoint number returned by an earlier 'B' command.
	     Possible return codes: OK, E_BPT.  */

	  int i;

	  /* Search for the breakpoint in the table.  */
	  for (i = 0; i < MAX_LSI_BREAKPOINTS; i++)
	    if (lsi_breakpoints[i].type == type
		&& lsi_breakpoints[i].addr == addr
		&& lsi_breakpoints[i].len == len)
	      break;

	  /* Clear the table entry and tell PMON to clear the breakpoint.  */
	  if (i == MAX_LSI_BREAKPOINTS)
	    {
	      warning ("common_breakpoint: Attempt to clear bogus breakpoint at %s\n",
		       paddr_nz (addr));
	      return 1;
	    }

	  lsi_breakpoints[i].type = BREAK_UNUSED;
	  sprintf (buf, "0x0 b 0x%x 0x0", i);
	  mips_send_packet (buf, 1);

	  rlen = mips_receive_packet (buf, 1, mips_receive_wait);
	  buf[rlen] = '\0';

	  nfields = sscanf (buf, "0x%x b 0x0 0x%x", &rpid, &rerrflg);
	  if (nfields != 2)
	    mips_error ("common_breakpoint: Bad response from remote board: %s", buf);

	  return (check_lsi_error (addr, rerrflg));
	}
      else
	/* set a breakpoint */
	{
	  /* The LSI PMON "set breakpoint" command has this form:
	     <pid> 'B' <addr> 0x0
	     reply:
	     <pid> 'B' <bptn> <code>

	     The "set data breakpoint" command has this form:

	     <pid> 'A' <addr1> <type> [<addr2>  [<value>]]

	     where: type= "0x1" = read
	     "0x2" = write
	     "0x3" = access (read or write)

	     The reply returns two values:
	     bptn - a breakpoint number, which is a small integer with
	     possible values of zero through 255.
	     code - an error return code, a value of zero indicates a
	     succesful completion, other values indicate various
	     errors and warnings.

	     Possible return codes: OK, W_QAL, E_QAL, E_OUT, E_NON.  

	   */

	  if (type == BREAK_FETCH)	/* instruction breakpoint */
	    {
	      cmd = 'B';
	      sprintf (buf, "0x0 B 0x%s 0x0", paddr_nz (addr));
	    }
	  else
	    /* watchpoint */
	    {
	      cmd = 'A';
	      sprintf (buf, "0x0 A 0x%s 0x%x 0x%s", paddr_nz (addr),
		     type == BREAK_READ ? 1 : (type == BREAK_WRITE ? 2 : 3),
		       paddr_nz (addr + len - 1));
	    }
	  mips_send_packet (buf, 1);

	  rlen = mips_receive_packet (buf, 1, mips_receive_wait);
	  buf[rlen] = '\0';

	  nfields = sscanf (buf, "0x%x %c 0x%x 0x%x",
			    &rpid, &rcmd, &rresponse, &rerrflg);
	  if (nfields != 4 || rcmd != cmd || rresponse > 255)
	    mips_error ("common_breakpoint: Bad response from remote board: %s", buf);

	  if (rerrflg != 0)
	    if (check_lsi_error (addr, rerrflg))
	      return 1;

	  /* rresponse contains PMON's breakpoint number.  Record the
	     information for this breakpoint so we can clear it later.  */
	  lsi_breakpoints[rresponse].type = type;
	  lsi_breakpoints[rresponse].addr = addr;
	  lsi_breakpoints[rresponse].len = len;

	  return 0;
	}
    }
  else
    {
      /* On non-LSI targets, the breakpoint command has this form:
         0x0 <CMD> <ADDR> <MASK> <FLAGS>
         <MASK> is a don't care mask for addresses.
         <FLAGS> is any combination of `r', `w', or `f' for read/write/fetch.
       */
      unsigned long mask;

      mask = calculate_mask (addr, len);
      addr &= ~mask;

      if (set)			/* set a breakpoint */
	{
	  char *flags;
	  switch (type)
	    {
	    case BREAK_WRITE:	/* write */
	      flags = "w";
	      break;
	    case BREAK_READ:	/* read */
	      flags = "r";
	      break;
	    case BREAK_ACCESS:	/* read/write */
	      flags = "rw";
	      break;
	    case BREAK_FETCH:	/* fetch */
	      flags = "f";
	      break;
	    default:
	      internal_error (__FILE__, __LINE__, "failed internal consistency check");
	    }

	  cmd = 'B';
	  sprintf (buf, "0x0 B 0x%s 0x%s %s", paddr_nz (addr),
		   paddr_nz (mask), flags);
	}
      else
	{
	  cmd = 'b';
	  sprintf (buf, "0x0 b 0x%s", paddr_nz (addr));
	}

      mips_send_packet (buf, 1);

      rlen = mips_receive_packet (buf, 1, mips_receive_wait);
      buf[rlen] = '\0';

      nfields = sscanf (buf, "0x%x %c 0x%x 0x%x",
			&rpid, &rcmd, &rerrflg, &rresponse);

      if (nfields != 4 || rcmd != cmd)
	mips_error ("common_breakpoint: Bad response from remote board: %s",
		    buf);

      if (rerrflg != 0)
	{
	  /* Ddb returns "0x0 b 0x16 0x0\000", whereas
	     Cogent returns "0x0 b 0xffffffff 0x16\000": */
	  if (mips_monitor == MON_DDB)
	    rresponse = rerrflg;
	  if (rresponse != 22)	/* invalid argument */
	    fprintf_unfiltered (gdb_stderr,
			     "common_breakpoint (0x%s):  Got error: 0x%x\n",
				paddr_nz (addr), rresponse);
	  return 1;
	}
    }
  return 0;
}

static void
send_srec (char *srec, int len, CORE_ADDR addr)
{
  while (1)
    {
      int ch;

      serial_write (mips_desc, srec, len);

      ch = mips_readchar (remote_timeout);

      switch (ch)
	{
	case SERIAL_TIMEOUT:
	  error ("Timeout during download.");
	  break;
	case 0x6:		/* ACK */
	  return;
	case 0x15:		/* NACK */
	  fprintf_unfiltered (gdb_stderr, "Download got a NACK at byte %s!  Retrying.\n", paddr_u (addr));
	  continue;
	default:
	  error ("Download got unexpected ack char: 0x%x, retrying.\n", ch);
	}
    }
}

/*  Download a binary file by converting it to S records. */

static void
mips_load_srec (char *args)
{
  bfd *abfd;
  asection *s;
  char *buffer, srec[1024];
  unsigned int i;
  unsigned int srec_frame = 200;
  int reclen;
  static int hashmark = 1;

  buffer = alloca (srec_frame * 2 + 256);

  abfd = bfd_openr (args, 0);
  if (!abfd)
    {
      printf_filtered ("Unable to open file %s\n", args);
      return;
    }

  if (bfd_check_format (abfd, bfd_object) == 0)
    {
      printf_filtered ("File is not an object file\n");
      return;
    }

/* This actually causes a download in the IDT binary format: */
  mips_send_command (LOAD_CMD, 0);

  for (s = abfd->sections; s; s = s->next)
    {
      if (s->flags & SEC_LOAD)
	{
	  unsigned int numbytes;

	  /* FIXME!  vma too small????? */
	  printf_filtered ("%s\t: 0x%4lx .. 0x%4lx  ", s->name,
			   (long) s->vma,
			   (long) (s->vma + s->_raw_size));
	  gdb_flush (gdb_stdout);

	  for (i = 0; i < s->_raw_size; i += numbytes)
	    {
	      numbytes = min (srec_frame, s->_raw_size - i);

	      bfd_get_section_contents (abfd, s, buffer, i, numbytes);

	      reclen = mips_make_srec (srec, '3', s->vma + i, buffer, numbytes);
	      send_srec (srec, reclen, s->vma + i);

	      if (ui_load_progress_hook)
		ui_load_progress_hook (s->name, i);

	      if (hashmark)
		{
		  putchar_unfiltered ('#');
		  gdb_flush (gdb_stdout);
		}

	    }			/* Per-packet (or S-record) loop */

	  putchar_unfiltered ('\n');
	}			/* Loadable sections */
    }
  if (hashmark)
    putchar_unfiltered ('\n');

  /* Write a type 7 terminator record. no data for a type 7, and there
     is no data, so len is 0.  */

  reclen = mips_make_srec (srec, '7', abfd->start_address, NULL, 0);

  send_srec (srec, reclen, abfd->start_address);

  serial_flush_input (mips_desc);
}

/*
 * mips_make_srec -- make an srecord. This writes each line, one at a
 *      time, each with it's own header and trailer line.
 *      An srecord looks like this:
 *
 * byte count-+     address
 * start ---+ |        |       data        +- checksum
 *          | |        |                   |
 *        S01000006F6B692D746573742E73726563E4
 *        S315000448600000000000000000FC00005900000000E9
 *        S31A0004000023C1400037DE00F023604000377B009020825000348D
 *        S30B0004485A0000000000004E
 *        S70500040000F6
 *
 *      S<type><length><address><data><checksum>
 *
 *      Where
 *      - length
 *        is the number of bytes following upto the checksum. Note that
 *        this is not the number of chars following, since it takes two
 *        chars to represent a byte.
 *      - type
 *        is one of:
 *        0) header record
 *        1) two byte address data record
 *        2) three byte address data record
 *        3) four byte address data record
 *        7) four byte address termination record
 *        8) three byte address termination record
 *        9) two byte address termination record
 *       
 *      - address
 *        is the start address of the data following, or in the case of
 *        a termination record, the start address of the image
 *      - data
 *        is the data.
 *      - checksum
 *        is the sum of all the raw byte data in the record, from the length
 *        upwards, modulo 256 and subtracted from 255.
 *
 * This routine returns the length of the S-record.
 *
 */

static int
mips_make_srec (char *buf, int type, CORE_ADDR memaddr, unsigned char *myaddr,
		int len)
{
  unsigned char checksum;
  int i;

  /* Create the header for the srec. addr_size is the number of bytes in the address,
     and 1 is the number of bytes in the count.  */

  /* FIXME!! bigger buf required for 64-bit! */
  buf[0] = 'S';
  buf[1] = type;
  buf[2] = len + 4 + 1;		/* len + 4 byte address + 1 byte checksum */
  /* This assumes S3 style downloads (4byte addresses). There should
     probably be a check, or the code changed to make it more
     explicit. */
  buf[3] = memaddr >> 24;
  buf[4] = memaddr >> 16;
  buf[5] = memaddr >> 8;
  buf[6] = memaddr;
  memcpy (&buf[7], myaddr, len);

  /* Note that the checksum is calculated on the raw data, not the
     hexified data.  It includes the length, address and the data
     portions of the packet.  */
  checksum = 0;
  buf += 2;			/* Point at length byte */
  for (i = 0; i < len + 4 + 1; i++)
    checksum += *buf++;

  *buf = ~checksum;

  return len + 8;
}

/* The following manifest controls whether we enable the simple flow
   control support provided by the monitor. If enabled the code will
   wait for an affirmative ACK between transmitting packets. */
#define DOETXACK (1)

/* The PMON fast-download uses an encoded packet format constructed of
   3byte data packets (encoded as 4 printable ASCII characters), and
   escape sequences (preceded by a '/'):

   'K'     clear checksum
   'C'     compare checksum (12bit value, not included in checksum calculation)
   'S'     define symbol name (for addr) terminated with "," and padded to 4char boundary
   'Z'     zero fill multiple of 3bytes
   'B'     byte (12bit encoded value, of 8bit data)
   'A'     address (36bit encoded value)
   'E'     define entry as original address, and exit load

   The packets are processed in 4 character chunks, so the escape
   sequences that do not have any data (or variable length data)
   should be padded to a 4 character boundary.  The decoder will give
   an error if the complete message block size is not a multiple of
   4bytes (size of record).

   The encoding of numbers is done in 6bit fields.  The 6bit value is
   used to index into this string to get the specific character
   encoding for the value: */
static char encoding[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789,.";

/* Convert the number of bits required into an encoded number, 6bits
   at a time (range 0..63).  Keep a checksum if required (passed
   pointer non-NULL). The function returns the number of encoded
   characters written into the buffer. */
static int
pmon_makeb64 (unsigned long v, char *p, int n, int *chksum)
{
  int count = (n / 6);

  if ((n % 12) != 0)
    {
      fprintf_unfiltered (gdb_stderr,
			  "Fast encoding bitcount must be a multiple of 12bits: %dbit%s\n", n, (n == 1) ? "" : "s");
      return (0);
    }
  if (n > 36)
    {
      fprintf_unfiltered (gdb_stderr,
			  "Fast encoding cannot process more than 36bits at the moment: %dbits\n", n);
      return (0);
    }

  /* Deal with the checksum: */
  if (chksum != NULL)
    {
      switch (n)
	{
	case 36:
	  *chksum += ((v >> 24) & 0xFFF);
	case 24:
	  *chksum += ((v >> 12) & 0xFFF);
	case 12:
	  *chksum += ((v >> 0) & 0xFFF);
	}
    }

  do
    {
      n -= 6;
      *p++ = encoding[(v >> n) & 0x3F];
    }
  while (n > 0);

  return (count);
}

/* Shorthand function (that could be in-lined) to output the zero-fill
   escape sequence into the data stream. */
static int
pmon_zeroset (int recsize, char **buff, int *amount, unsigned int *chksum)
{
  int count;

  sprintf (*buff, "/Z");
  count = pmon_makeb64 (*amount, (*buff + 2), 12, chksum);
  *buff += (count + 2);
  *amount = 0;
  return (recsize + count + 2);
}

static int
pmon_checkset (int recsize, char **buff, int *value)
{
  int count;

  /* Add the checksum (without updating the value): */
  sprintf (*buff, "/C");
  count = pmon_makeb64 (*value, (*buff + 2), 12, NULL);
  *buff += (count + 2);
  sprintf (*buff, "\n");
  *buff += 2;			/* include zero terminator */
  /* Forcing a checksum validation clears the sum: */
  *value = 0;
  return (recsize + count + 3);
}

/* Amount of padding we leave after at the end of the output buffer,
   for the checksum and line termination characters: */
#define CHECKSIZE (4 + 4 + 4 + 2)
/* zero-fill, checksum, transfer end and line termination space. */

/* The amount of binary data loaded from the object file in a single
   operation: */
#define BINCHUNK (1024)

/* Maximum line of data accepted by the monitor: */
#define MAXRECSIZE (550)
/* NOTE: This constant depends on the monitor being used. This value
   is for PMON 5.x on the Cogent Vr4300 board. */

static void
pmon_make_fastrec (char **outbuf, unsigned char *inbuf, int *inptr,
		   int inamount, int *recsize, unsigned int *csum,
		   unsigned int *zerofill)
{
  int count = 0;
  char *p = *outbuf;

  /* This is a simple check to ensure that our data will fit within
     the maximum allowable record size. Each record output is 4bytes
     in length. We must allow space for a pending zero fill command,
     the record, and a checksum record. */
  while ((*recsize < (MAXRECSIZE - CHECKSIZE)) && ((inamount - *inptr) > 0))
    {
      /* Process the binary data: */
      if ((inamount - *inptr) < 3)
	{
	  if (*zerofill != 0)
	    *recsize = pmon_zeroset (*recsize, &p, zerofill, csum);
	  sprintf (p, "/B");
	  count = pmon_makeb64 (inbuf[*inptr], &p[2], 12, csum);
	  p += (2 + count);
	  *recsize += (2 + count);
	  (*inptr)++;
	}
      else
	{
	  unsigned int value = ((inbuf[*inptr + 0] << 16) | (inbuf[*inptr + 1] << 8) | inbuf[*inptr + 2]);
	  /* Simple check for zero data. TODO: A better check would be
	     to check the last, and then the middle byte for being zero
	     (if the first byte is not). We could then check for
	     following runs of zeros, and if above a certain size it is
	     worth the 4 or 8 character hit of the byte insertions used
	     to pad to the start of the zeroes. NOTE: This also depends
	     on the alignment at the end of the zero run. */
	  if (value == 0x00000000)
	    {
	      (*zerofill)++;
	      if (*zerofill == 0xFFF)	/* 12bit counter */
		*recsize = pmon_zeroset (*recsize, &p, zerofill, csum);
	    }
	  else
	    {
	      if (*zerofill != 0)
		*recsize = pmon_zeroset (*recsize, &p, zerofill, csum);
	      count = pmon_makeb64 (value, p, 24, csum);
	      p += count;
	      *recsize += count;
	    }
	  *inptr += 3;
	}
    }

  *outbuf = p;
  return;
}

static int
pmon_check_ack (char *mesg)
{
#if defined(DOETXACK)
  int c;

  if (!tftp_in_use)
    {
      c = serial_readchar (udp_in_use ? udp_desc : mips_desc,
			   remote_timeout);
      if ((c == SERIAL_TIMEOUT) || (c != 0x06))
	{
	  fprintf_unfiltered (gdb_stderr,
			      "Failed to receive valid ACK for %s\n", mesg);
	  return (-1);		/* terminate the download */
	}
    }
#endif /* DOETXACK */
  return (0);
}

/* pmon_download - Send a sequence of characters to the PMON download port,
   which is either a serial port or a UDP socket.  */

static void
pmon_start_download (void)
{
  if (tftp_in_use)
    {
      /* Create the temporary download file.  */
      if ((tftp_file = fopen (tftp_localname, "w")) == NULL)
	perror_with_name (tftp_localname);
    }
  else
    {
      mips_send_command (udp_in_use ? LOAD_CMD_UDP : LOAD_CMD, 0);
      mips_expect ("Downloading from ");
      mips_expect (udp_in_use ? "udp" : "tty0");
      mips_expect (", ^C to abort\r\n");
    }
}

static int
mips_expect_download (char *string)
{
  if (!mips_expect (string))
    {
      fprintf_unfiltered (gdb_stderr, "Load did not complete successfully.\n");
      if (tftp_in_use)
	remove (tftp_localname);	/* Remove temporary file */
      return 0;
    }
  else
    return 1;
}

static void
pmon_check_entry_address (char *entry_address, int final)
{
  char hexnumber[9];		/* includes '\0' space */
  mips_expect_timeout (entry_address, tftp_in_use ? 15 : remote_timeout);
  sprintf (hexnumber, "%x", final);
  mips_expect (hexnumber);
  mips_expect ("\r\n");
}

static int
pmon_check_total (int bintotal)
{
  char hexnumber[9];		/* includes '\0' space */
  mips_expect ("\r\ntotal = 0x");
  sprintf (hexnumber, "%x", bintotal);
  mips_expect (hexnumber);
  return mips_expect_download (" bytes\r\n");
}

static void
pmon_end_download (int final, int bintotal)
{
  char hexnumber[9];		/* includes '\0' space */

  if (tftp_in_use)
    {
      static char *load_cmd_prefix = "load -b -s ";
      char *cmd;
      struct stat stbuf;

      /* Close off the temporary file containing the load data.  */
      fclose (tftp_file);
      tftp_file = NULL;

      /* Make the temporary file readable by the world.  */
      if (stat (tftp_localname, &stbuf) == 0)
	chmod (tftp_localname, stbuf.st_mode | S_IROTH);

      /* Must reinitialize the board to prevent PMON from crashing.  */
      mips_send_command ("initEther\r", -1);

      /* Send the load command.  */
      cmd = xmalloc (strlen (load_cmd_prefix) + strlen (tftp_name) + 2);
      strcpy (cmd, load_cmd_prefix);
      strcat (cmd, tftp_name);
      strcat (cmd, "\r");
      mips_send_command (cmd, 0);
      xfree (cmd);
      if (!mips_expect_download ("Downloading from "))
	return;
      if (!mips_expect_download (tftp_name))
	return;
      if (!mips_expect_download (", ^C to abort\r\n"))
	return;
    }

  /* Wait for the stuff that PMON prints after the load has completed.
     The timeout value for use in the tftp case (15 seconds) was picked
     arbitrarily but might be too small for really large downloads. FIXME. */
  switch (mips_monitor)
    {
    case MON_LSI:
      pmon_check_ack ("termination");
      pmon_check_entry_address ("Entry address is ", final);
      if (!pmon_check_total (bintotal))
	return;
      break;
    default:
      pmon_check_entry_address ("Entry Address  = ", final);
      pmon_check_ack ("termination");
      if (!pmon_check_total (bintotal))
	return;
      break;
    }

  if (tftp_in_use)
    remove (tftp_localname);	/* Remove temporary file */
}

static void
pmon_download (char *buffer, int length)
{
  if (tftp_in_use)
    fwrite (buffer, 1, length, tftp_file);
  else
    serial_write (udp_in_use ? udp_desc : mips_desc, buffer, length);
}

static void
pmon_load_fast (char *file)
{
  bfd *abfd;
  asection *s;
  unsigned char *binbuf;
  char *buffer;
  int reclen;
  unsigned int csum = 0;
  int hashmark = !tftp_in_use;
  int bintotal = 0;
  int final = 0;
  int finished = 0;

  buffer = (char *) xmalloc (MAXRECSIZE + 1);
  binbuf = (unsigned char *) xmalloc (BINCHUNK);

  abfd = bfd_openr (file, 0);
  if (!abfd)
    {
      printf_filtered ("Unable to open file %s\n", file);
      return;
    }

  if (bfd_check_format (abfd, bfd_object) == 0)
    {
      printf_filtered ("File is not an object file\n");
      return;
    }

  /* Setup the required download state: */
  mips_send_command ("set dlproto etxack\r", -1);
  mips_send_command ("set dlecho off\r", -1);
  /* NOTE: We get a "cannot set variable" message if the variable is
     already defined to have the argument we give. The code doesn't
     care, since it just scans to the next prompt anyway. */
  /* Start the download: */
  pmon_start_download ();

  /* Zero the checksum */
  sprintf (buffer, "/Kxx\n");
  reclen = strlen (buffer);
  pmon_download (buffer, reclen);
  finished = pmon_check_ack ("/Kxx");

  for (s = abfd->sections; s && !finished; s = s->next)
    if (s->flags & SEC_LOAD)	/* only deal with loadable sections */
      {
	bintotal += s->_raw_size;
	final = (s->vma + s->_raw_size);

	printf_filtered ("%s\t: 0x%4x .. 0x%4x  ", s->name, (unsigned int) s->vma,
			 (unsigned int) (s->vma + s->_raw_size));
	gdb_flush (gdb_stdout);

	/* Output the starting address */
	sprintf (buffer, "/A");
	reclen = pmon_makeb64 (s->vma, &buffer[2], 36, &csum);
	buffer[2 + reclen] = '\n';
	buffer[3 + reclen] = '\0';
	reclen += 3;		/* for the initial escape code and carriage return */
	pmon_download (buffer, reclen);
	finished = pmon_check_ack ("/A");

	if (!finished)
	  {
	    unsigned int binamount;
	    unsigned int zerofill = 0;
	    char *bp = buffer;
	    unsigned int i;

	    reclen = 0;

	    for (i = 0; ((i < s->_raw_size) && !finished); i += binamount)
	      {
		int binptr = 0;

		binamount = min (BINCHUNK, s->_raw_size - i);

		bfd_get_section_contents (abfd, s, binbuf, i, binamount);

		/* This keeps a rolling checksum, until we decide to output
		   the line: */
		for (; ((binamount - binptr) > 0);)
		  {
		    pmon_make_fastrec (&bp, binbuf, &binptr, binamount, &reclen, &csum, &zerofill);
		    if (reclen >= (MAXRECSIZE - CHECKSIZE))
		      {
			reclen = pmon_checkset (reclen, &bp, &csum);
			pmon_download (buffer, reclen);
			finished = pmon_check_ack ("data record");
			if (finished)
			  {
			    zerofill = 0;	/* do not transmit pending zerofills */
			    break;
			  }

			if (ui_load_progress_hook)
			  ui_load_progress_hook (s->name, i);

			if (hashmark)
			  {
			    putchar_unfiltered ('#');
			    gdb_flush (gdb_stdout);
			  }

			bp = buffer;
			reclen = 0;	/* buffer processed */
		      }
		  }
	      }

	    /* Ensure no out-standing zerofill requests: */
	    if (zerofill != 0)
	      reclen = pmon_zeroset (reclen, &bp, &zerofill, &csum);

	    /* and then flush the line: */
	    if (reclen > 0)
	      {
		reclen = pmon_checkset (reclen, &bp, &csum);
		/* Currently pmon_checkset outputs the line terminator by
		   default, so we write out the buffer so far: */
		pmon_download (buffer, reclen);
		finished = pmon_check_ack ("record remnant");
	      }
	  }

	putchar_unfiltered ('\n');
      }

  /* Terminate the transfer. We know that we have an empty output
     buffer at this point. */
  sprintf (buffer, "/E/E\n");	/* include dummy padding characters */
  reclen = strlen (buffer);
  pmon_download (buffer, reclen);

  if (finished)
    {				/* Ignore the termination message: */
      serial_flush_input (udp_in_use ? udp_desc : mips_desc);
    }
  else
    {				/* Deal with termination message: */
      pmon_end_download (final, bintotal);
    }

  return;
}

/* mips_load -- download a file. */

static void
mips_load (char *file, int from_tty)
{
  /* Get the board out of remote debugging mode.  */
  if (mips_exit_debug ())
    error ("mips_load:  Couldn't get into monitor mode.");

  if (mips_monitor != MON_IDT)
    pmon_load_fast (file);
  else
    mips_load_srec (file);

  mips_initialize ();

  /* Finally, make the PC point at the start address */
  if (mips_monitor != MON_IDT)
    {
      /* Work around problem where PMON monitor updates the PC after a load
         to a different value than GDB thinks it has. The following ensures
         that the write_pc() WILL update the PC value: */
      deprecated_register_valid[PC_REGNUM] = 0;
    }
  if (exec_bfd)
    write_pc (bfd_get_start_address (exec_bfd));

  inferior_ptid = null_ptid;	/* No process now */

/* This is necessary because many things were based on the PC at the time that
   we attached to the monitor, which is no longer valid now that we have loaded
   new code (and just changed the PC).  Another way to do this might be to call
   normal_stop, except that the stack may not be valid, and things would get
   horribly confused... */

  clear_symtab_users ();
}


/* Pass the command argument as a packet to PMON verbatim.  */

static void
pmon_command (char *args, int from_tty)
{
  char buf[DATA_MAXLEN + 1];
  int rlen;

  sprintf (buf, "0x0 %s", args);
  mips_send_packet (buf, 1);
  printf_filtered ("Send packet: %s\n", buf);

  rlen = mips_receive_packet (buf, 1, mips_receive_wait);
  buf[rlen] = '\0';
  printf_filtered ("Received packet: %s\n", buf);
}

extern initialize_file_ftype _initialize_remote_mips; /* -Wmissing-prototypes */

void
_initialize_remote_mips (void)
{
  /* Initialize the fields in mips_ops that are common to all four targets.  */
  mips_ops.to_longname = "Remote MIPS debugging over serial line";
  mips_ops.to_close = mips_close;
  mips_ops.to_detach = mips_detach;
  mips_ops.to_resume = mips_resume;
  mips_ops.to_fetch_registers = mips_fetch_registers;
  mips_ops.to_store_registers = mips_store_registers;
  mips_ops.to_prepare_to_store = mips_prepare_to_store;
  mips_ops.to_xfer_memory = mips_xfer_memory;
  mips_ops.to_files_info = mips_files_info;
  mips_ops.to_insert_breakpoint = mips_insert_breakpoint;
  mips_ops.to_remove_breakpoint = mips_remove_breakpoint;
  mips_ops.to_insert_watchpoint = mips_insert_watchpoint;
  mips_ops.to_remove_watchpoint = mips_remove_watchpoint;
  mips_ops.to_stopped_by_watchpoint = mips_stopped_by_watchpoint;
  mips_ops.to_can_use_hw_breakpoint = mips_can_use_watchpoint;
  mips_ops.to_kill = mips_kill;
  mips_ops.to_load = mips_load;
  mips_ops.to_create_inferior = mips_create_inferior;
  mips_ops.to_mourn_inferior = mips_mourn_inferior;
  mips_ops.to_stratum = process_stratum;
  mips_ops.to_has_all_memory = 1;
  mips_ops.to_has_memory = 1;
  mips_ops.to_has_stack = 1;
  mips_ops.to_has_registers = 1;
  mips_ops.to_has_execution = 1;
  mips_ops.to_magic = OPS_MAGIC;

  /* Copy the common fields to all four target vectors.  */
  pmon_ops = ddb_ops = lsi_ops = mips_ops;

  /* Initialize target-specific fields in the target vectors.  */
  mips_ops.to_shortname = "mips";
  mips_ops.to_doc = "\
Debug a board using the MIPS remote debugging protocol over a serial line.\n\
The argument is the device it is connected to or, if it contains a colon,\n\
HOST:PORT to access a board over a network";
  mips_ops.to_open = mips_open;
  mips_ops.to_wait = mips_wait;

  pmon_ops.to_shortname = "pmon";
  pmon_ops.to_doc = "\
Debug a board using the PMON MIPS remote debugging protocol over a serial\n\
line. The argument is the device it is connected to or, if it contains a\n\
colon, HOST:PORT to access a board over a network";
  pmon_ops.to_open = pmon_open;
  pmon_ops.to_wait = mips_wait;

  ddb_ops.to_shortname = "ddb";
  ddb_ops.to_doc = "\
Debug a board using the PMON MIPS remote debugging protocol over a serial\n\
line. The first argument is the device it is connected to or, if it contains\n\
a colon, HOST:PORT to access a board over a network.  The optional second\n\
parameter is the temporary file in the form HOST:FILENAME to be used for\n\
TFTP downloads to the board.  The optional third parameter is the local name\n\
of the TFTP temporary file, if it differs from the filename seen by the board.";
  ddb_ops.to_open = ddb_open;
  ddb_ops.to_wait = mips_wait;

  lsi_ops.to_shortname = "lsi";
  lsi_ops.to_doc = pmon_ops.to_doc;
  lsi_ops.to_open = lsi_open;
  lsi_ops.to_wait = mips_wait;

  /* Add the targets.  */
  add_target (&mips_ops);
  add_target (&pmon_ops);
  add_target (&ddb_ops);
  add_target (&lsi_ops);

  add_show_from_set (
		      add_set_cmd ("timeout", no_class, var_zinteger,
				   (char *) &mips_receive_wait,
		       "Set timeout in seconds for remote MIPS serial I/O.",
				   &setlist),
		      &showlist);

  add_show_from_set (
		  add_set_cmd ("retransmit-timeout", no_class, var_zinteger,
			       (char *) &mips_retransmit_wait,
			       "Set retransmit timeout in seconds for remote MIPS serial I/O.\n\
This is the number of seconds to wait for an acknowledgement to a packet\n\
before resending the packet.", &setlist),
		      &showlist);

  add_show_from_set (
		   add_set_cmd ("syn-garbage-limit", no_class, var_zinteger,
				(char *) &mips_syn_garbage,
				"Set the maximum number of characters to ignore when scanning for a SYN.\n\
This is the maximum number of characters GDB will ignore when trying to\n\
synchronize with the remote system.  A value of -1 means that there is no limit\n\
(Note that these characters are printed out even though they are ignored.)",
				&setlist),
		      &showlist);

  add_show_from_set
    (add_set_cmd ("monitor-prompt", class_obscure, var_string,
		  (char *) &mips_monitor_prompt,
		  "Set the prompt that GDB expects from the monitor.",
		  &setlist),
     &showlist);

  add_show_from_set (
	       add_set_cmd ("monitor-warnings", class_obscure, var_zinteger,
			    (char *) &monitor_warnings,
			    "Set printing of monitor warnings.\n"
		"When enabled, monitor warnings about hardware breakpoints "
			    "will be displayed.",
			    &setlist),
		      &showlist);

  add_com ("pmon <command>", class_obscure, pmon_command,
	   "Send a packet to PMON (must be in debug mode).");

  add_show_from_set (add_set_cmd ("mask-address", no_class,
				  var_boolean, &mask_address_p,
				  "Set zeroing of upper 32 bits of 64-bit addresses when talking to PMON targets.\n\
Use \"on\" to enable the masking and \"off\" to disable it.\n",
				  &setlist),
		     &showlist);
}
