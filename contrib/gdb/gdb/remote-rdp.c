/* Remote debugging for the ARM RDP interface.

   Copyright 1994, 1995, 1998, 1999, 2000, 2001, 2002, 2003 Free
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
   Boston, MA 02111-1307, USA.  


 */


/* 
   Much of this file (in particular the SWI stuff) is based on code by
   David Taylor (djt1000@uk.ac.cam.hermes).

   I hacked on and simplified it by removing a lot of sexy features he
   had added, and some of the (unix specific) workarounds he'd done
   for other GDB problems - which if they still exist should be fixed
   in GDB, not in a remote-foo thing .  I also made it conform more to
   the doc I have; which may be wrong.

   Steve Chamberlain (sac@cygnus.com).
 */


#include "defs.h"
#include "inferior.h"
#include "value.h"
#include "gdb/callback.h"
#include "command.h"
#include <ctype.h>
#include <fcntl.h>
#include "symfile.h"
#include "remote-utils.h"
#include "gdb_string.h"
#include "gdbcore.h"
#include "regcache.h"
#include "serial.h"

#include "arm-tdep.h"

#ifdef HAVE_TIME_H
#include <time.h>
#endif

extern struct target_ops remote_rdp_ops;
static struct serial *io;
static host_callback *callback = &default_callback;

struct
  {
    int step_info;
    int break_info;
    int model_info;
    int target_info;
    int can_step;
    char command_line[10];
    int rdi_level;
    int rdi_stopped_status;
  }
ds;



/* Definitions for the RDP protocol. */

#define RDP_MOUTHFULL   		(1<<6)
#define FPU_COPRO_NUMBER 		1

#define RDP_OPEN 	 		0
#define RDP_OPEN_TYPE_COLD 		0
#define RDP_OPEN_TYPE_WARM 		1
#define RDP_OPEN_TYPE_BAUDRATE          2

#define RDP_OPEN_BAUDRATE_9600       	1
#define RDP_OPEN_BAUDRATE_19200        	2
#define RDP_OPEN_BAUDRATE_38400        	3

#define RDP_OPEN_TYPE_RETURN_SEX	(1<<3)

#define RDP_CLOSE 			1

#define RDP_MEM_READ 			2

#define RDP_MEM_WRITE 			3

#define RDP_CPU_READ 			4
#define RDP_CPU_WRITE 			5
#define RDP_CPU_READWRITE_MODE_CURRENT 255
#define RDP_CPU_READWRITE_MASK_PC 	(1<<16)
#define RDP_CPU_READWRITE_MASK_CPSR 	(1<<17)
#define RDP_CPU_READWRITE_MASK_SPSR 	(1<<18)

#define RDP_COPRO_READ   		6
#define RDP_COPRO_WRITE 		7
#define RDP_FPU_READWRITE_MASK_FPS 	(1<<8)

#define RDP_SET_BREAK			0xa
#define RDP_SET_BREAK_TYPE_PC_EQUAL     0
#define RDP_SET_BREAK_TYPE_GET_HANDLE   (0x10)

#define RDP_CLEAR_BREAK 		0xb

#define RDP_EXEC 			0x10
#define RDP_EXEC_TYPE_SYNC 		0

#define RDP_STEP 			0x11

#define RDP_INFO  			0x12
#define RDP_INFO_ABOUT_STEP 		2
#define RDP_INFO_ABOUT_STEP_GT_1	1
#define RDP_INFO_ABOUT_STEP_TO_JMP 	2
#define RDP_INFO_ABOUT_STEP_1		4
#define RDP_INFO_ABOUT_TARGET 		0
#define RDP_INFO_ABOUT_BREAK 		1
#define RDP_INFO_ABOUT_BREAK_COMP	1
#define RDP_INFO_ABOUT_BREAK_RANGE 	2
#define RDP_INFO_ABOUT_BREAK_BYTE_READ 	4
#define RDP_INFO_ABOUT_BREAK_HALFWORD_READ 8
#define RDP_INFO_ABOUT_BREAK_WORD_READ (1<<4)
#define RDP_INFO_ABOUT_BREAK_BYTE_WRITE (1<<5)
#define RDP_INFO_ABOUT_BREAK_HALFWORD_WRITE (1<<6)
#define RDP_INFO_ABOUT_BREAK_WORD_WRITE (1<<7)
#define RDP_INFO_ABOUT_BREAK_MASK 	(1<<8)
#define RDP_INFO_ABOUT_BREAK_THREAD_BREAK (1<<9)
#define RDP_INFO_ABOUT_BREAK_THREAD_WATCH (1<<10)
#define RDP_INFO_ABOUT_BREAK_COND 	(1<<11)
#define RDP_INFO_VECTOR_CATCH		(0x180)
#define RDP_INFO_ICEBREAKER		(7)
#define RDP_INFO_SET_CMDLINE            (0x300)

#define RDP_SELECT_CONFIG		(0x16)
#define RDI_ConfigCPU			0
#define RDI_ConfigSystem		1
#define RDI_MatchAny			0
#define RDI_MatchExactly		1
#define RDI_MatchNoEarlier		2

#define RDP_RESET 			0x7f

/* Returns from RDP */
#define RDP_RES_STOPPED 		0x20
#define RDP_RES_SWI 			0x21
#define RDP_RES_FATAL 			0x5e
#define RDP_RES_VALUE 			0x5f
#define RDP_RES_VALUE_LITTLE_ENDIAN     240
#define RDP_RES_VALUE_BIG_ENDIAN 	241
#define RDP_RES_RESET			0x7f
#define RDP_RES_AT_BREAKPOINT    	143
#define RDP_RES_IDUNNO			0xe6
#define RDP_OSOpReply           	0x13
#define RDP_OSOpWord            	2
#define RDP_OSOpNothing         	0

static int timeout = 2;

static char *commandline = NULL;

static int
remote_rdp_xfer_inferior_memory (CORE_ADDR memaddr, char *myaddr, int len,
				 int write, 
				 struct mem_attrib *attrib,
				 struct target_ops *target);


/* Stuff for talking to the serial layer. */

static unsigned char
get_byte (void)
{
  int c = serial_readchar (io, timeout);

  if (remote_debug)
    fprintf_unfiltered (gdb_stdlog, "[%02x]\n", c);

  if (c == SERIAL_TIMEOUT)
    {
      if (timeout == 0)
	return (unsigned char) c;

      error ("Timeout reading from remote_system");
    }

  return c;
}

/* Note that the target always speaks little-endian to us,
   even if it's a big endian machine. */
static unsigned int
get_word (void)
{
  unsigned int val = 0;
  unsigned int c;
  int n;
  for (n = 0; n < 4; n++)
    {
      c = get_byte ();
      val |= c << (n * 8);
    }
  return val;
}

static void
put_byte (char val)
{
  if (remote_debug)
    fprintf_unfiltered (gdb_stdlog, "(%02x)\n", val);
  serial_write (io, &val, 1);
}

static void
put_word (int val)
{
  /* We always send in little endian */
  unsigned char b[4];
  b[0] = val;
  b[1] = val >> 8;
  b[2] = val >> 16;
  b[3] = val >> 24;

  if (remote_debug)
    fprintf_unfiltered (gdb_stdlog, "(%04x)", val);

  serial_write (io, b, 4);
}



/* Stuff for talking to the RDP layer. */

/* This is a bit more fancy that need be so that it syncs even in nasty cases.

   I'be been unable to make it reliably sync up with the change
   baudrate open command.  It likes to sit and say it's been reset,
   with no more action.  So I took all that code out.  I'd rather sync
   reliably at 9600 than wait forever for a possible 19200 connection.

 */
static void
rdp_init (int cold, int tty)
{
  int sync = 0;
  int type = cold ? RDP_OPEN_TYPE_COLD : RDP_OPEN_TYPE_WARM;
  int baudtry = 9600;

  time_t now = time (0);
  time_t stop_time = now + 10;	/* Try and sync for 10 seconds, then give up */


  while (time (0) < stop_time && !sync)
    {
      int restype;
      QUIT;

      serial_flush_input (io);
      serial_flush_output (io);

      if (tty)
	printf_unfiltered ("Trying to connect at %d baud.\n", baudtry);

      /*
         ** It seems necessary to reset an EmbeddedICE to get it going.
         ** This has the side benefit of displaying the startup banner.
       */
      if (cold)
	{
	  put_byte (RDP_RESET);
	  while ((restype = serial_readchar (io, 1)) > 0)
	    {
	      switch (restype)
		{
		case SERIAL_TIMEOUT:
		  break;
		case RDP_RESET:
		  /* Sent at start of reset process: ignore */
		  break;
		default:
		  printf_unfiltered ("%c", isgraph (restype) ? restype : ' ');
		  break;
		}
	    }

	  if (restype == 0)
	    {
	      /* Got end-of-banner mark */
	      printf_filtered ("\n");
	    }
	}

      put_byte (RDP_OPEN);

      put_byte (type | RDP_OPEN_TYPE_RETURN_SEX);
      put_word (0);

      while (!sync && (restype = serial_readchar (io, 1)) > 0)
	{
	  if (remote_debug)
	    fprintf_unfiltered (gdb_stdlog, "[%02x]\n", restype);

	  switch (restype)
	    {
	    case SERIAL_TIMEOUT:
	      break;

	    case RDP_RESET:
	      while ((restype = serial_readchar (io, 1)) == RDP_RESET)
		;
	      do
		{
		  printf_unfiltered ("%c", isgraph (restype) ? restype : ' ');
		}
	      while ((restype = serial_readchar (io, 1)) > 0);

	      if (tty)
		{
		  printf_unfiltered ("\nThe board has sent notification that it was reset.\n");
		  printf_unfiltered ("Waiting for it to settle down...\n");
		}
	      sleep (3);
	      if (tty)
		printf_unfiltered ("\nTrying again.\n");
	      cold = 0;
	      break;

	    default:
	      break;

	    case RDP_RES_VALUE:
	      {
		int resval = serial_readchar (io, 1);

		if (remote_debug)
		  fprintf_unfiltered (gdb_stdlog, "[%02x]\n", resval);

		switch (resval)
		  {
		  case SERIAL_TIMEOUT:
		    break;
		  case RDP_RES_VALUE_LITTLE_ENDIAN:
#if 0
		    /* FIXME: cagney/2003-11-22: Ever since the ARM
                       was multi-arched (in 2002-02-08), this
                       assignment has had no effect.  There needs to
                       be some sort of check/decision based on the
                       current architecture's byte-order vs the remote
                       target's byte order.  For the moment disable
                       the assignment to keep things building.  */
		    target_byte_order = BFD_ENDIAN_LITTLE;
#endif
		    sync = 1;
		    break;
		  case RDP_RES_VALUE_BIG_ENDIAN:
#if 0
		    /* FIXME: cagney/2003-11-22: Ever since the ARM
                       was multi-arched (in 2002-02-08), this
                       assignment has had no effect.  There needs to
                       be some sort of check/decision based on the
                       current architecture's byte-order vs the remote
                       target's byte order.  For the moment disable
                       the assignment to keep things building.  */
		    target_byte_order = BFD_ENDIAN_BIG;
#endif
		    sync = 1;
		    break;
		  default:
		    break;
		  }
	      }
	    }
	}
    }

  if (!sync)
    {
      error ("Couldn't reset the board, try pressing the reset button");
    }
}


static void
send_rdp (char *template,...)
{
  char buf[200];
  char *dst = buf;
  va_list alist;
  va_start (alist, template);

  while (*template)
    {
      unsigned int val;
      int *pi;
      int *pstat;
      char *pc;
      int i;
      switch (*template++)
	{
	case 'b':
	  val = va_arg (alist, int);
	  *dst++ = val;
	  break;
	case 'w':
	  val = va_arg (alist, int);
	  *dst++ = val;
	  *dst++ = val >> 8;
	  *dst++ = val >> 16;
	  *dst++ = val >> 24;
	  break;
	case 'S':
	  val = get_byte ();
	  if (val != RDP_RES_VALUE)
	    {
	      printf_unfiltered ("got bad res value of %d, %x\n", val, val);
	    }
	  break;
	case 'V':
	  pstat = va_arg (alist, int *);
	  pi = va_arg (alist, int *);

	  *pstat = get_byte ();
	  /* Check the result was zero, if not read the syndrome */
	  if (*pstat)
	    {
	      *pi = get_word ();
	    }
	  break;
	case 'Z':
	  /* Check the result code */
	  switch (get_byte ())
	    {
	    case 0:
	      /* Success */
	      break;
	    case 253:
	      /* Target can't do it; never mind */
	      printf_unfiltered ("RDP: Insufficient privilege\n");
	      return;
	    case 254:
	      /* Target can't do it; never mind */
	      printf_unfiltered ("RDP: Unimplemented message\n");
	      return;
	    case 255:
	      error ("Command garbled");
	      break;
	    default:
	      error ("Corrupt reply from target");
	      break;
	    }
	  break;
	case 'W':
	  /* Read a word from the target */
	  pi = va_arg (alist, int *);
	  *pi = get_word ();
	  break;
	case 'P':
	  /* Read in some bytes from the target. */
	  pc = va_arg (alist, char *);
	  val = va_arg (alist, int);
	  for (i = 0; i < val; i++)
	    {
	      pc[i] = get_byte ();
	    }
	  break;
	case 'p':
	  /* send what's being pointed at */
	  pc = va_arg (alist, char *);
	  val = va_arg (alist, int);
	  dst = buf;
	  serial_write (io, pc, val);
	  break;
	case '-':
	  /* Send whats in the queue */
	  if (dst != buf)
	    {
	      serial_write (io, buf, dst - buf);
	      dst = buf;
	    }
	  break;
	case 'B':
	  pi = va_arg (alist, int *);
	  *pi = get_byte ();
	  break;
	default:
	  internal_error (__FILE__, __LINE__, "failed internal consistency check");
	}
    }
  va_end (alist);

  if (dst != buf)
    internal_error (__FILE__, __LINE__, "failed internal consistency check");
}


static int
rdp_write (CORE_ADDR memaddr, char *buf, int len)
{
  int res;
  int val;

  send_rdp ("bww-p-SV", RDP_MEM_WRITE, memaddr, len, buf, len, &res, &val);

  if (res)
    {
      return val;
    }
  return len;
}


static int
rdp_read (CORE_ADDR memaddr, char *buf, int len)
{
  int res;
  int val;
  send_rdp ("bww-S-P-V",
	    RDP_MEM_READ, memaddr, len,
	    buf, len,
	    &res, &val);
  if (res)
    {
      return val;
    }
  return len;
}

static void
rdp_fetch_one_register (int mask, char *buf)
{
  int val;
  send_rdp ("bbw-SWZ", RDP_CPU_READ, RDP_CPU_READWRITE_MODE_CURRENT, mask, &val);
  store_signed_integer (buf, 4, val);
}

static void
rdp_fetch_one_fpu_register (int mask, char *buf)
{
#if 0
  /* !!! Since the PIE board doesn't work as documented,
     and it doesn't have FPU hardware anyway and since it
     slows everything down, I've disabled this. */
  int val;
  if (mask == RDP_FPU_READWRITE_MASK_FPS)
    {
      /* this guy is only a word */
      send_rdp ("bbw-SWZ", RDP_COPRO_READ, FPU_COPRO_NUMBER, mask, &val);
      store_signed_integer (buf, 4, val);
    }
  else
    {
      /* There are 12 bytes long 
         !! fixme about endianness 
       */
      int dummy;		/* I've seen these come back as four words !! */
      send_rdp ("bbw-SWWWWZ", RDP_COPRO_READ, FPU_COPRO_NUMBER, mask, buf + 0, buf + 4, buf + 8, &dummy);
    }
#endif
  memset (buf, 0, MAX_REGISTER_SIZE);
}


static void
rdp_store_one_register (int mask, char *buf)
{
  int val = extract_unsigned_integer (buf, 4);

  send_rdp ("bbww-SZ",
	    RDP_CPU_WRITE, RDP_CPU_READWRITE_MODE_CURRENT, mask, val);
}


static void
rdp_store_one_fpu_register (int mask, char *buf)
{
#if 0
  /* See comment in fetch_one_fpu_register */
  if (mask == RDP_FPU_READWRITE_MASK_FPS)
    {
      int val = extract_unsigned_integer (buf, 4);
      /* this guy is only a word */
      send_rdp ("bbww-SZ", RDP_COPRO_WRITE,
		FPU_COPRO_NUMBER,
		mask, val);
    }
  else
    {
      /* There are 12 bytes long 
         !! fixme about endianness 
       */
      int dummy = 0;
      /* I've seen these come as four words, not the three advertized !! */
      printf ("Sending mask %x\n", mask);
      send_rdp ("bbwwwww-SZ",
		RDP_COPRO_WRITE,
		FPU_COPRO_NUMBER,
		mask,
		*(int *) (buf + 0),
		*(int *) (buf + 4),
		*(int *) (buf + 8),
		0);

      printf ("done mask %x\n", mask);
    }
#endif
}


/* Convert between GDB requests and the RDP layer. */

static void
remote_rdp_fetch_register (int regno)
{
  if (regno == -1)
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	remote_rdp_fetch_register (regno);
    }
  else
    {
      char buf[MAX_REGISTER_SIZE];
      if (regno < 15)
	rdp_fetch_one_register (1 << regno, buf);
      else if (regno == ARM_PC_REGNUM)
	rdp_fetch_one_register (RDP_CPU_READWRITE_MASK_PC, buf);
      else if (regno == ARM_PS_REGNUM)
	rdp_fetch_one_register (RDP_CPU_READWRITE_MASK_CPSR, buf);
      else if (regno == ARM_FPS_REGNUM)
	rdp_fetch_one_fpu_register (RDP_FPU_READWRITE_MASK_FPS, buf);
      else if (regno >= ARM_F0_REGNUM && regno <= ARM_F7_REGNUM)
	rdp_fetch_one_fpu_register (1 << (regno - ARM_F0_REGNUM), buf);
      else
	{
	  printf ("Help me with fetch reg %d\n", regno);
	}
      supply_register (regno, buf);
    }
}


static void
remote_rdp_store_register (int regno)
{
  if (regno == -1)
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	remote_rdp_store_register (regno);
    }
  else
    {
      char tmp[MAX_REGISTER_SIZE];
      deprecated_read_register_gen (regno, tmp);
      if (regno < 15)
	rdp_store_one_register (1 << regno, tmp);
      else if (regno == ARM_PC_REGNUM)
	rdp_store_one_register (RDP_CPU_READWRITE_MASK_PC, tmp);
      else if (regno == ARM_PS_REGNUM)
	rdp_store_one_register (RDP_CPU_READWRITE_MASK_CPSR, tmp);
      else if (regno >= ARM_F0_REGNUM && regno <= ARM_F7_REGNUM)
	rdp_store_one_fpu_register (1 << (regno - ARM_F0_REGNUM), tmp);
      else
	{
	  printf ("Help me with reg %d\n", regno);
	}
    }
}

static void
remote_rdp_kill (void)
{
  callback->shutdown (callback);
}


static void
rdp_info (void)
{
  send_rdp ("bw-S-W-Z", RDP_INFO, RDP_INFO_ABOUT_STEP,
	    &ds.step_info);
  send_rdp ("bw-S-W-Z", RDP_INFO, RDP_INFO_ABOUT_BREAK,
	    &ds.break_info);
  send_rdp ("bw-S-WW-Z", RDP_INFO, RDP_INFO_ABOUT_TARGET,
	    &ds.target_info,
	    &ds.model_info);

  ds.can_step = ds.step_info & RDP_INFO_ABOUT_STEP_1;

  ds.rdi_level = (ds.target_info >> 5) & 3;
}


static void
rdp_execute_start (void)
{
  /* Start it off, but don't wait for it */
  send_rdp ("bb-", RDP_EXEC, RDP_EXEC_TYPE_SYNC);
}


static void
rdp_set_command_line (char *command, char *args)
{
  /*
     ** We could use RDP_INFO_SET_CMDLINE to send this, but EmbeddedICE systems
     ** don't implement that, and get all confused at the unexpected text.
     ** Instead, just keep a copy, and send it when the target does a SWI_GetEnv
   */

  if (commandline != NULL)
    xfree (commandline);

  xasprintf (&commandline, "%s %s", command, args);
}

static void
rdp_catch_vectors (void)
{
  /*
     ** We want the target monitor to intercept the abort vectors
     ** i.e. stop the program if any of these are used.
   */
  send_rdp ("bww-SZ", RDP_INFO, RDP_INFO_VECTOR_CATCH,
  /*
     ** Specify a bitmask including
     **  the reset vector
     **  the undefined instruction vector
     **  the prefetch abort vector
     **  the data abort vector
     **  the address exception vector
   */
	    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5)
    );
}



#define a_byte 1
#define a_word 2
#define a_string 3


typedef struct
{
  CORE_ADDR n;
  const char *s;
}
argsin;

#define ABYTE 1
#define AWORD 2
#define ASTRING 3
#define ADDRLEN 4

#define SWI_WriteC                      0x0
#define SWI_Write0                      0x2
#define SWI_ReadC                       0x4
#define SWI_CLI                         0x5
#define SWI_GetEnv                      0x10
#define SWI_Exit                        0x11
#define SWI_EnterOS                     0x16

#define SWI_GetErrno                    0x60
#define SWI_Clock                       0x61

#define SWI_Time                        0x63
#define SWI_Remove                      0x64
#define SWI_Rename                      0x65
#define SWI_Open                        0x66

#define SWI_Close                       0x68
#define SWI_Write                       0x69
#define SWI_Read                        0x6a
#define SWI_Seek                        0x6b
#define SWI_Flen                        0x6c

#define SWI_IsTTY                       0x6e
#define SWI_TmpNam                      0x6f
#define SWI_InstallHandler              0x70
#define SWI_GenerateError               0x71


#ifndef O_BINARY
#define O_BINARY 0
#endif

static int translate_open_mode[] =
{
  O_RDONLY,			/* "r"   */
  O_RDONLY + O_BINARY,		/* "rb"  */
  O_RDWR,			/* "r+"  */
  O_RDWR + O_BINARY,		/* "r+b" */
  O_WRONLY + O_CREAT + O_TRUNC,	/* "w"   */
  O_WRONLY + O_BINARY + O_CREAT + O_TRUNC,	/* "wb"  */
  O_RDWR + O_CREAT + O_TRUNC,	/* "w+"  */
  O_RDWR + O_BINARY + O_CREAT + O_TRUNC,	/* "w+b" */
  O_WRONLY + O_APPEND + O_CREAT,	/* "a"   */
  O_WRONLY + O_BINARY + O_APPEND + O_CREAT,	/* "ab"  */
  O_RDWR + O_APPEND + O_CREAT,	/* "a+"  */
  O_RDWR + O_BINARY + O_APPEND + O_CREAT	/* "a+b" */
};

static int
exec_swi (int swi, argsin *args)
{
  int i;
  char c;
  switch (swi)
    {
    case SWI_WriteC:
      callback->write_stdout (callback, &c, 1);
      return 0;
    case SWI_Write0:
      for (i = 0; i < args->n; i++)
	callback->write_stdout (callback, args->s, strlen (args->s));
      return 0;
    case SWI_ReadC:
      callback->read_stdin (callback, &c, 1);
      args->n = c;
      return 1;
    case SWI_CLI:
      args->n = callback->system (callback, args->s);
      return 1;
    case SWI_GetErrno:
      args->n = callback->get_errno (callback);
      return 1;
    case SWI_Time:
      args->n = callback->time (callback, NULL);
      return 1;

    case SWI_Clock:
      /* return number of centi-seconds... */
      args->n =
#ifdef CLOCKS_PER_SEC
	(CLOCKS_PER_SEC >= 100)
	? (clock () / (CLOCKS_PER_SEC / 100))
	: ((clock () * 100) / CLOCKS_PER_SEC);
#else
      /* presume unix... clock() returns microseconds */
	clock () / 10000;
#endif
      return 1;

    case SWI_Remove:
      args->n = callback->unlink (callback, args->s);
      return 1;
    case SWI_Rename:
      args->n = callback->rename (callback, args[0].s, args[1].s);
      return 1;

    case SWI_Open:
      /* Now we need to decode the Demon open mode */
      i = translate_open_mode[args[1].n];

      /* Filename ":tt" is special: it denotes stdin/out */
      if (strcmp (args->s, ":tt") == 0)
	{
	  if (i == O_RDONLY)	/* opening tty "r" */
	    args->n = 0 /* stdin */ ;
	  else
	    args->n = 1 /* stdout */ ;
	}
      else
	args->n = callback->open (callback, args->s, i);
      return 1;

    case SWI_Close:
      args->n = callback->close (callback, args->n);
      return 1;

    case SWI_Write:
      /* Return the number of bytes *not* written */
      args->n = args[1].n -
	callback->write (callback, args[0].n, args[1].s, args[1].n);
      return 1;

    case SWI_Read:
      {
	char *copy = alloca (args[2].n);
	int done = callback->read (callback, args[0].n, copy, args[2].n);
	if (done > 0)
	  remote_rdp_xfer_inferior_memory (args[1].n, copy, done, 1, 0, 0);
	args->n = args[2].n - done;
	return 1;
      }

    case SWI_Seek:
      /* Return non-zero on failure */
      args->n = callback->lseek (callback, args[0].n, args[1].n, 0) < 0;
      return 1;

    case SWI_Flen:
      {
	long old = callback->lseek (callback, args->n, 0, SEEK_CUR);
	args->n = callback->lseek (callback, args->n, 0, SEEK_END);
	callback->lseek (callback, args->n, old, 0);
	return 1;
      }

    case SWI_IsTTY:
      args->n = callback->isatty (callback, args->n);
      return 1;

    case SWI_GetEnv:
      if (commandline != NULL)
	{
	  int len = strlen (commandline);
	  if (len > 255)
	    {
	      len = 255;
	      commandline[255] = '\0';
	    }
	  remote_rdp_xfer_inferior_memory (args[0].n,
					   commandline, len + 1, 1, 0, 0);
	}
      else
	remote_rdp_xfer_inferior_memory (args[0].n, "", 1, 1, 0, 0);
      return 1;

    default:
      return 0;
    }
}


static void
handle_swi (void)
{
  argsin args[3];
  char *buf;
  int len;
  int count = 0;

  int swino = get_word ();
  int type = get_byte ();
  while (type != 0)
    {
      switch (type & 0x3)
	{
	case ABYTE:
	  args[count].n = get_byte ();
	  break;

	case AWORD:
	  args[count].n = get_word ();
	  break;

	case ASTRING:
	  /* If the word is under 32 bytes it will be sent otherwise
	     an address to it is passed. Also: Special case of 255 */

	  len = get_byte ();
	  if (len > 32)
	    {
	      if (len == 255)
		{
		  len = get_word ();
		}
	      buf = alloca (len);
	      remote_rdp_xfer_inferior_memory (get_word (),
					       buf,
					       len,
					       0,
					       0,
					       0);
	    }
	  else
	    {
	      int i;
	      buf = alloca (len + 1);
	      for (i = 0; i < len; i++)
		buf[i] = get_byte ();
	      buf[i] = 0;
	    }
	  args[count].n = len;
	  args[count].s = buf;
	  break;

	default:
	  error ("Unimplemented SWI argument");
	}

      type = type >> 2;
      count++;
    }

  if (exec_swi (swino, args))
    {
      /* We have two options here reply with either a byte or a word
         which is stored in args[0].n. There is no harm in replying with
         a word all the time, so thats what I do! */
      send_rdp ("bbw-", RDP_OSOpReply, RDP_OSOpWord, args[0].n);
    }
  else
    {
      send_rdp ("bb-", RDP_OSOpReply, RDP_OSOpNothing);
    }
}

static void
rdp_execute_finish (void)
{
  int running = 1;

  while (running)
    {
      int res;
      res = serial_readchar (io, 1);
      while (res == SERIAL_TIMEOUT)
	{
	  QUIT;
	  printf_filtered ("Waiting for target..\n");
	  res = serial_readchar (io, 1);
	}

      switch (res)
	{
	case RDP_RES_SWI:
	  handle_swi ();
	  break;
	case RDP_RES_VALUE:
	  send_rdp ("B", &ds.rdi_stopped_status);
	  running = 0;
	  break;
	case RDP_RESET:
	  printf_filtered ("Target reset\n");
	  running = 0;
	  break;
	default:
	  printf_filtered ("Ignoring %x\n", res);
	  break;
	}
    }
}


static void
rdp_execute (void)
{
  rdp_execute_start ();
  rdp_execute_finish ();
}

static int
remote_rdp_insert_breakpoint (CORE_ADDR addr, char *save)
{
  int res;
  if (ds.rdi_level > 0)
    {
      send_rdp ("bwb-SWB",
		RDP_SET_BREAK,
		addr,
		RDP_SET_BREAK_TYPE_PC_EQUAL | RDP_SET_BREAK_TYPE_GET_HANDLE,
		save,
		&res);
    }
  else
    {
      send_rdp ("bwb-SB",
		RDP_SET_BREAK,
		addr,
		RDP_SET_BREAK_TYPE_PC_EQUAL,
		&res);
    }
  return res;
}

static int
remote_rdp_remove_breakpoint (CORE_ADDR addr, char *save)
{
  int res;
  if (ds.rdi_level > 0)
    {
      send_rdp ("b-p-S-B",
		RDP_CLEAR_BREAK,
		save, 4,
		&res);
    }
  else
    {
      send_rdp ("bw-S-B",
		RDP_CLEAR_BREAK,
		addr,
		&res);
    }
  return res;
}

static void
rdp_step (void)
{
  if (ds.can_step && 0)
    {
      /* The pie board can't do steps so I can't test this, and
         the other code will always work. */
      int status;
      send_rdp ("bbw-S-B",
		RDP_STEP, 0, 1,
		&status);
    }
  else
    {
      char handle[4];
      CORE_ADDR pc = read_register (ARM_PC_REGNUM);
      pc = arm_get_next_pc (pc);
      remote_rdp_insert_breakpoint (pc, handle);
      rdp_execute ();
      remote_rdp_remove_breakpoint (pc, handle);
    }
}

static void
remote_rdp_open (char *args, int from_tty)
{
  int not_icebreaker;

  if (!args)
    error_no_arg ("serial port device name");

  baud_rate = 9600;

  target_preopen (from_tty);

  io = serial_open (args);

  if (!io)
    perror_with_name (args);

  serial_raw (io);

  rdp_init (1, from_tty);


  if (from_tty)
    {
      printf_unfiltered ("Remote RDP debugging using %s at %d baud\n", args, baud_rate);
    }

  rdp_info ();

  /* Need to set up the vector interception state */
  rdp_catch_vectors ();

  /*
     ** If it's an EmbeddedICE, we need to set the processor config.
     ** Assume we can always have ARM7TDI...
   */
  send_rdp ("bw-SB", RDP_INFO, RDP_INFO_ICEBREAKER, &not_icebreaker);
  if (!not_icebreaker)
    {
      const char *CPU = "ARM7TDI";
      int ICEversion;
      int len = strlen (CPU);

      send_rdp ("bbbbw-p-SWZ",
		RDP_SELECT_CONFIG,
		RDI_ConfigCPU,	/* Aspect: set the CPU */
		len,		/* The number of bytes in the name */
		RDI_MatchAny,	/* We'll take whatever we get */
		0,		/* We'll take whatever version's there */
		CPU, len,
		&ICEversion);
    }

  /* command line initialised on 'run' */

  push_target (&remote_rdp_ops);

  callback->init (callback);
  flush_cached_frames ();
  registers_changed ();
  stop_pc = read_pc ();
  print_stack_frame (get_selected_frame (), -1, 1);
}



/* Close out all files and local state before this target loses control. */

static void
remote_rdp_close (int quitting)
{
  callback->shutdown (callback);
  if (io)
    serial_close (io);
  io = 0;
}


/* Resume execution of the target process.  STEP says whether to single-step
   or to run free; SIGGNAL is the signal value (e.g. SIGINT) to be given
   to the target, or zero for no signal.  */

static void
remote_rdp_resume (ptid_t ptid, int step, enum target_signal siggnal)
{
  if (step)
    rdp_step ();
  else
    rdp_execute ();
}

/* Wait for inferior process to do something.  Return pid of child,
   or -1 in case of error; store status through argument pointer STATUS,
   just as `wait' would.  */

static ptid_t
remote_rdp_wait (ptid_t ptid, struct target_waitstatus *status)
{
  switch (ds.rdi_stopped_status)
    {
    default:
    case RDP_RES_RESET:
    case RDP_RES_SWI:
      status->kind = TARGET_WAITKIND_EXITED;
      status->value.integer = read_register (0);
      break;
    case RDP_RES_AT_BREAKPOINT:
      status->kind = TARGET_WAITKIND_STOPPED;
      /* The signal in sigrc is a host signal.  That probably
         should be fixed.  */
      status->value.sig = TARGET_SIGNAL_TRAP;
      break;
#if 0
    case rdp_signalled:
      status->kind = TARGET_WAITKIND_SIGNALLED;
      /* The signal in sigrc is a host signal.  That probably
         should be fixed.  */
      status->value.sig = target_signal_from_host (sigrc);
      break;
#endif
    }

  return inferior_ptid;
}

/* Get ready to modify the registers array.  On machines which store
   individual registers, this doesn't need to do anything.  On machines
   which store all the registers in one fell swoop, this makes sure
   that registers contains all the registers from the program being
   debugged.  */

static void
remote_rdp_prepare_to_store (void)
{
  /* Do nothing, since we can store individual regs */
}

/* Transfer LEN bytes between GDB address MYADDR and target address
   MEMADDR.  If WRITE is non-zero, transfer them to the target,
   otherwise transfer them from the target.  TARGET is unused.

   Returns the number of bytes transferred. */

static int
remote_rdp_xfer_inferior_memory (CORE_ADDR memaddr, char *myaddr, int len,
				 int write, struct mem_attrib *attrib,
				 struct target_ops *target)
{
  /* I infer from D Taylor's code that there's a limit on the amount
     we can transfer in one chunk.. */
  int done = 0;
  while (done < len)
    {
      int justdone;
      int thisbite = len - done;
      if (thisbite > RDP_MOUTHFULL)
	thisbite = RDP_MOUTHFULL;

      QUIT;

      if (write)
	{
	  justdone = rdp_write (memaddr + done, myaddr + done, thisbite);
	}
      else
	{
	  justdone = rdp_read (memaddr + done, myaddr + done, thisbite);
	}

      done += justdone;

      if (justdone != thisbite)
	break;
    }
  return done;
}



struct yn
{
  const char *name;
  int bit;
};
static struct yn stepinfo[] =
{
  {"Step more than one instruction", RDP_INFO_ABOUT_STEP_GT_1},
  {"Step to jump", RDP_INFO_ABOUT_STEP_TO_JMP},
  {"Step one instruction", RDP_INFO_ABOUT_STEP_1},
  {0}
};

static struct yn breakinfo[] =
{
  {"comparison breakpoints supported", RDP_INFO_ABOUT_BREAK_COMP},
  {"range breakpoints supported", RDP_INFO_ABOUT_BREAK_RANGE},
  {"watchpoints for byte reads supported", RDP_INFO_ABOUT_BREAK_BYTE_READ},
  {"watchpoints for half-word reads supported", RDP_INFO_ABOUT_BREAK_HALFWORD_READ},
  {"watchpoints for word reads supported", RDP_INFO_ABOUT_BREAK_WORD_READ},
  {"watchpoints for byte writes supported", RDP_INFO_ABOUT_BREAK_BYTE_WRITE},
  {"watchpoints for half-word writes supported", RDP_INFO_ABOUT_BREAK_HALFWORD_WRITE},
  {"watchpoints for word writes supported", RDP_INFO_ABOUT_BREAK_WORD_WRITE},
  {"mask break/watch-points supported", RDP_INFO_ABOUT_BREAK_MASK},
{"thread-specific breakpoints supported", RDP_INFO_ABOUT_BREAK_THREAD_BREAK},
{"thread-specific watchpoints supported", RDP_INFO_ABOUT_BREAK_THREAD_WATCH},
  {"conditional breakpoints supported", RDP_INFO_ABOUT_BREAK_COND},
  {0}
};


static void
dump_bits (struct yn *t, int info)
{
  while (t->name)
    {
      printf_unfiltered ("  %-45s : %s\n", t->name, (info & t->bit) ? "Yes" : "No");
      t++;
    }
}

static void
remote_rdp_files_info (struct target_ops *target)
{
  printf_filtered ("Target capabilities:\n");
  dump_bits (stepinfo, ds.step_info);
  dump_bits (breakinfo, ds.break_info);
  printf_unfiltered ("target level RDI %x\n", (ds.target_info >> 5) & 3);
}


static void
remote_rdp_create_inferior (char *exec_file, char *allargs, char **env)
{
  CORE_ADDR entry_point;

  if (exec_file == 0 || exec_bfd == 0)
    error ("No executable file specified.");

  entry_point = (CORE_ADDR) bfd_get_start_address (exec_bfd);

  remote_rdp_kill ();
  remove_breakpoints ();
  init_wait_for_inferior ();

  /* This gives us a chance to set up the command line */
  rdp_set_command_line (exec_file, allargs);

  inferior_ptid = pid_to_ptid (42);
  insert_breakpoints ();	/* Needed to get correct instruction in cache */

  /*
     ** RDP targets don't provide any facility to set the top of memory,
     ** so we don't bother to look for MEMSIZE in the environment.
   */

  /* Let's go! */
  proceed (entry_point, TARGET_SIGNAL_DEFAULT, 0);
}

/* Attach doesn't need to do anything */
static void
remote_rdp_attach (char *args, int from_tty)
{
  return;
}

/* Define the target subroutine names */

struct target_ops remote_rdp_ops;

static void
init_remote_rdp_ops (void)
{
  remote_rdp_ops.to_shortname = "rdp";
  remote_rdp_ops.to_longname = "Remote Target using the RDProtocol";
  remote_rdp_ops.to_doc = "Use a remote ARM system which uses the ARM Remote Debugging Protocol";
  remote_rdp_ops.to_open = remote_rdp_open;
  remote_rdp_ops.to_close = remote_rdp_close;
  remote_rdp_ops.to_attach = remote_rdp_attach;
  remote_rdp_ops.to_resume = remote_rdp_resume;
  remote_rdp_ops.to_wait = remote_rdp_wait;
  remote_rdp_ops.to_fetch_registers = remote_rdp_fetch_register;
  remote_rdp_ops.to_store_registers = remote_rdp_store_register;
  remote_rdp_ops.to_prepare_to_store = remote_rdp_prepare_to_store;
  remote_rdp_ops.to_xfer_memory = remote_rdp_xfer_inferior_memory;
  remote_rdp_ops.to_files_info = remote_rdp_files_info;
  remote_rdp_ops.to_insert_breakpoint = remote_rdp_insert_breakpoint;
  remote_rdp_ops.to_remove_breakpoint = remote_rdp_remove_breakpoint;
  remote_rdp_ops.to_kill = remote_rdp_kill;
  remote_rdp_ops.to_load = generic_load;
  remote_rdp_ops.to_create_inferior = remote_rdp_create_inferior;
  remote_rdp_ops.to_mourn_inferior = generic_mourn_inferior;
  remote_rdp_ops.to_stratum = process_stratum;
  remote_rdp_ops.to_has_all_memory = 1;
  remote_rdp_ops.to_has_memory = 1;
  remote_rdp_ops.to_has_stack = 1;
  remote_rdp_ops.to_has_registers = 1;
  remote_rdp_ops.to_has_execution = 1;
  remote_rdp_ops.to_magic = OPS_MAGIC;
}

extern initialize_file_ftype _initialize_remote_rdp; /* -Wmissing-prototypes */

void
_initialize_remote_rdp (void)
{
  init_remote_rdp_ops ();
  add_target (&remote_rdp_ops);
}
