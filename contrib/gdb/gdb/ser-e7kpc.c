/* Remote serial interface using Renesas E7000 PC ISA card in a PC
   Copyright 1994, 1996, 1997, 1998, 1999, 2000
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
#if defined __GO32__ || defined _WIN32
#include "serial.h"
#include "gdb_string.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef __GO32__
#include <sys/dos.h>
#endif

static int e7000pc_open (struct serial *scb, const char *name);
static void e7000pc_raw (struct serial *scb);
static int e7000pc_readchar (struct serial *scb, int timeout);
static int e7000pc_setbaudrate (struct serial *scb, int rate);
static int e7000pc_write (struct serial *scb, const char *str, int len);
static void e7000pc_close (struct serial *scb);
static serial_ttystate e7000pc_get_tty_state (struct serial *scb);
static int e7000pc_set_tty_state (struct serial *scb, serial_ttystate state);

#define OFF_DPD 	0x0000
#define OFF_DDP 	0x1000
#define OFF_CPD 	0x2000
#define OFF_CDP 	0x2400
#define OFF_FA  	0x3000
#define OFF_FB  	0x3002
#define OFF_FC  	0x3004
#define OFF_IRQTOD	0x3008
#define OFF_IRQTOP 	0x300a
#define OFF_READY  	0x300c
#define OFF_PON    	0x300e

#define IDLE       0x0000
#define CMD_CI     0x4349
#define CMD_CO     0x434f
#define CMD_LO     0x4c4f
#define CMD_LS     0x4c53
#define CMD_SV     0x5356
#define CMD_SS     0x5353
#define CMD_OK     0x4f4b
#define CMD_ER     0x4552
#define CMD_NF     0x4e46
#define CMD_AB     0x4142
#define CMD_ED     0x4544
#define CMD_CE     0x4345

static unsigned long fa;
static unsigned long irqtod;
static unsigned long ready;
static unsigned long fb;
static unsigned long cpd;
static unsigned long cdp;
static unsigned long ready;
static unsigned long pon;
static unsigned long irqtop;
static unsigned long board_at;

#ifdef __GO32__

#define SET_BYTE(x,y)   { char _buf = y;dosmemput(&_buf,1, x);}
#define SET_WORD(x,y)   { short _buf = y;dosmemput(&_buf,2, x);}
#define GET_BYTE(x)     ( dosmemget(x,1,&bb), bb)
#define GET_WORD(x)     ( dosmemget(x,2,&sb), sb)
static unsigned char bb;
static unsigned short sb;

#else /* win32 */

#define SET_BYTE(x,y)   *(volatile unsigned char *)(x) = (y)
#define SET_WORD(x,y)   *(volatile unsigned short *)(x) = (y)
#define GET_BYTE(x)     (*(volatile unsigned char *)(x))
#define GET_WORD(x)     (*(volatile unsigned short *)(x))
#define dosmemget(FROM, LEN, TO) memcpy ((void *)(TO), (void *)(FROM), (LEN))
#define dosmemput(FROM, LEN, TO) memcpy ((void *)(TO), (void *)(FROM), (LEN))
#endif

static struct sw
  {
    int sw;
    int addr;
  }
sigs[] =
{
  {
    0x14, 0xd0000
  }
  ,
  {
    0x15, 0xd4000
  }
  ,
  {
    0x16, 0xd8000
  }
  ,
  {
    0x17, 0xdc000
  }
  ,
    0
};

#define get_ds_base() 0

static int
e7000pc_init (void)
{
  int try;
  unsigned long dsbase;

  dsbase = get_ds_base ();

  /* Look around in memory for the board's signature */

  for (try = 0; sigs[try].sw; try++)
    {
      int val;
      board_at = sigs[try].addr - dsbase;
      fa = board_at + OFF_FA;
      fb = board_at + OFF_FB;
      cpd = board_at + OFF_CPD;
      cdp = board_at + OFF_CDP;
      ready = board_at + OFF_READY;
      pon = board_at + OFF_PON;
      irqtop = board_at + OFF_IRQTOP;
      irqtod = board_at + OFF_IRQTOD;

      val = GET_WORD (ready);

      if (val == (0xaaa0 | sigs[try].sw))
	{
	  if (GET_WORD (pon) & 0xf)
	    {
	      SET_WORD (fa, 0);
	      SET_WORD (fb, 0);

	      SET_WORD (irqtop, 1);	/* Disable interrupts from e7000 */
	      SET_WORD (ready, 1);
	      printf_filtered ("\nConnected to the E7000PC at address 0x%x\n",
			       sigs[try].addr);
	      return 1;
	    }
	  error ("The E7000 PC board is working, but the E7000 is turned off.\n");
	  return 0;
	}
    }

  error ("GDB cannot connect to the E7000 PC board, check that it is installed\n\
and that the switch settings are correct.  Some other DOS programs can \n\
stop the board from working.  Try starting from a very minimal boot, \n\
perhaps you need to disable EMM386 over the region where the board has\n\
its I/O space, remove other unneeded cards, etc etc\n");
  return 0;

}

static int pbuf_size;
static int pbuf_index;

/* Return next byte from cdp.  If no more, then return -1.  */

static int
e7000_get (void)
{
  static char pbuf[1000];
  char tmp[1000];
  int x;

  if (pbuf_index < pbuf_size)
    {
      x = pbuf[pbuf_index++];
    }
  else if ((GET_WORD (fb) & 1))
    {
      int i;
      pbuf_size = GET_WORD (cdp + 2);

      dosmemget (cdp + 8, pbuf_size + 1, tmp);

      /* Tell the E7000 we've eaten */
      SET_WORD (fb, 0);
      /* Swap it around */
      for (i = 0; i < pbuf_size; i++)
	{
	  pbuf[i] = tmp[i ^ 1];
	}
      pbuf_index = 0;
      x = pbuf[pbuf_index++];
    }
  else
    {
      x = -1;
    }
  return x;
}

/* Works just like read(), except that it takes a TIMEOUT in seconds.  Note
   that TIMEOUT == 0 is a poll, and TIMEOUT == -1 means wait forever. */

static int
dosasync_read (int fd, char *buf, int len, int timeout)
{
  long now;
  long then;
  int i = 0;

  /* Then look for some more if we're still hungry */
  time (&now);
  then = now + timeout;
  while (i < len)
    {
      int ch = e7000_get ();

      /* While there's room in the buffer, and we've already
         read the stuff in, suck it over */
      if (ch != -1)
	{
	  buf[i++] = ch;
	  while (i < len && pbuf_index < pbuf_size)
	    {
	      ch = e7000_get ();
	      if (ch == -1)
		break;
	      buf[i++] = ch;
	    }
	}

      time (&now);

      if (timeout == 0)
	return i;
      if (now >= then && timeout > 0)
	{
	  return i;
	}
    }
  return len;
}


static int
dosasync_write (int fd, const char *buf, int len)
{
  int i;
  char dummy[1000];

  /* Construct copy locally */
  ((short *) dummy)[0] = CMD_CI;
  ((short *) dummy)[1] = len;
  ((short *) dummy)[2] = 0;
  ((short *) dummy)[3] = 0;
  for (i = 0; i < len; i++)
    {
      dummy[(8 + i) ^ 1] = buf[i];
    }

  /* Wait for the card to get ready */
  while (GET_WORD (fa) & 1);

  /* Blast onto the ISA card */
  dosmemput (dummy, 8 + len + 1, cpd);

  SET_WORD (fa, 1);
  SET_WORD (irqtod, 1);		/* Interrupt the E7000 */

  return len;
}

static int
e7000pc_open (struct serial *scb, const char *name)
{
  if (strncasecmp (name, "pc", 2) != 0)
    {
      errno = ENOENT;
      return -1;
    }

  scb->fd = e7000pc_init ();

  if (!scb->fd)
    return -1;

  return 0;
}

static int
e7000pc_noop (struct serial *scb)
{
  return 0;
}

static void
e7000pc_raw (struct serial *scb)
{
  /* Always in raw mode */
}

static int
e7000pc_readchar (struct serial *scb, int timeout)
{
  char buf;

top:

  if (dosasync_read (scb->fd, &buf, 1, timeout))
    {
      if (buf == 0)
	goto top;
      return buf;
    }
  else
    return SERIAL_TIMEOUT;
}

struct e7000pc_ttystate
{
  int dummy;
};

/* e7000pc_{get set}_tty_state() are both dummys to fill out the function
   vector.  Someday, they may do something real... */

static serial_ttystate
e7000pc_get_tty_state (struct serial *scb)
{
  struct e7000pc_ttystate *state;

  state = (struct e7000pc_ttystate *) xmalloc (sizeof *state);

  return (serial_ttystate) state;
}

static int
e7000pc_set_tty_state (struct serial *scb, serial_ttystate ttystate)
{
  return 0;
}

static int
e7000pc_noflush_set_tty_state (struct serial *scb,
			       serial_ttystate new_ttystate,
			       serial_ttystate old_ttystate)
{
  return 0;
}

static void
e7000pc_print_tty_state (struct serial *scb,
			 serial_ttystate ttystate,
			 struct ui_file *stream)
{
  /* Nothing to print.  */
  return;
}

static int
e7000pc_setbaudrate (struct serial *scb, int rate)
{
  return 0;
}

static int
e7000pc_setstopbits (struct serial *scb, int rate)
{
  return 0;
}

static int
e7000pc_write (struct serial *scb, const char *str, int len)
{
  dosasync_write (scb->fd, str, len);

  return 0;
}

static void
e7000pc_close (struct serial *scb)
{
}

static struct serial_ops e7000pc_ops =
{
  "pc",
  0,
  e7000pc_open,
  e7000pc_close,
  e7000pc_readchar,
  e7000pc_write,
  e7000pc_noop,			/* flush output */
  e7000pc_noop,			/* flush input */
  e7000pc_noop,			/* send break -- currently used only for nindy */
  e7000pc_raw,
  e7000pc_get_tty_state,
  e7000pc_set_tty_state,
  e7000pc_print_tty_state,
  e7000pc_noflush_set_tty_state,
  e7000pc_setbaudrate,
  e7000pc_setstopbits,
  e7000pc_noop,			/* wait for output to drain */
};

#endif /*_WIN32 or __GO32__*/

extern initialize_file_ftype _initialize_ser_e7000pc; /* -Wmissing-prototypes */

void
_initialize_ser_e7000pc (void)
{
#if defined __GO32__ || defined _WIN32
  serial_add_interface (&e7000pc_ops);
#endif  
}
