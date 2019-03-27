/* Remote serial interface for local (hardwired) serial ports for GO32.
   Copyright 1992, 1993, 2000, 2001 Free Software Foundation, Inc.

   Contributed by Nigel Stephens, Algorithmics Ltd. (nigel@algor.co.uk).

   This version uses DPMI interrupts to handle buffered i/o
   without the separate "asynctsr" program.

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
#include "gdbcmd.h"
#include "serial.h"
#include "gdb_string.h"


/*
 * NS16550 UART registers
 */

#define COM1ADDR	0x3f8
#define COM2ADDR	0x2f8
#define COM3ADDR	0x3e8
#define COM4ADDR	0x3e0

#define	com_data	0	/* data register (R/W) */
#define	com_dlbl	0	/* divisor latch low (W) */
#define	com_ier		1	/* interrupt enable (W) */
#define	com_dlbh	1	/* divisor latch high (W) */
#define	com_iir		2	/* interrupt identification (R) */
#define	com_fifo	2	/* FIFO control (W) */
#define	com_lctl	3	/* line control register (R/W) */
#define	com_cfcr	3	/* line control register (R/W) */
#define	com_mcr		4	/* modem control register (R/W) */
#define	com_lsr		5	/* line status register (R/W) */
#define	com_msr		6	/* modem status register (R/W) */

/*
 * Constants for computing 16 bit baud rate divisor (lower byte
 * in com_dlbl, upper in com_dlbh) from 1.8432MHz crystal.  Divisor is
 * 1.8432 MHz / (16 * X) for X bps.  If the baud rate can't be set
 * to within +- (desired_rate*SPEED_TOLERANCE/1000) bps, we fail.
 */
#define COMTICK		(1843200/16)
#define SPEED_TOLERANCE	30	/* thousandths; real == desired +- 3.0% */

/* interrupt enable register */
#define	IER_ERXRDY	0x1	/* int on rx ready */
#define	IER_ETXRDY	0x2	/* int on tx ready */
#define	IER_ERLS	0x4	/* int on line status change */
#define	IER_EMSC	0x8	/* int on modem status change */

/* interrupt identification register */
#define	IIR_FIFO_MASK	0xc0	/* set if FIFOs are enabled */
#define	IIR_IMASK	0xf	/* interrupt cause mask */
#define	IIR_NOPEND	0x1	/* nothing pending */
#define	IIR_RLS		0x6	/* receive line status */
#define	IIR_RXRDY	0x4	/* receive ready */
#define	IIR_RXTOUT	0xc	/* receive timeout */
#define	IIR_TXRDY	0x2	/* transmit ready */
#define	IIR_MLSC	0x0	/* modem status */


/* fifo control register */
#define	FIFO_ENABLE	0x01	/* enable fifo */
#define	FIFO_RCV_RST	0x02	/* reset receive fifo */
#define	FIFO_XMT_RST	0x04	/* reset transmit fifo */
#define	FIFO_DMA_MODE	0x08	/* enable dma mode */
#define	FIFO_TRIGGER_1	0x00	/* trigger at 1 char */
#define	FIFO_TRIGGER_4	0x40	/* trigger at 4 chars */
#define	FIFO_TRIGGER_8	0x80	/* trigger at 8 chars */
#define	FIFO_TRIGGER_14	0xc0	/* trigger at 14 chars */

/* character format control register */
#define	CFCR_DLAB	0x80	/* divisor latch */
#define	CFCR_SBREAK	0x40	/* send break */
#define	CFCR_PZERO	0x30	/* zero parity */
#define	CFCR_PONE	0x20	/* one parity */
#define	CFCR_PEVEN	0x10	/* even parity */
#define	CFCR_PODD	0x00	/* odd parity */
#define	CFCR_PENAB	0x08	/* parity enable */
#define	CFCR_STOPB	0x04	/* 2 stop bits */
#define	CFCR_8BITS	0x03	/* 8 data bits */
#define	CFCR_7BITS	0x02	/* 7 data bits */
#define	CFCR_6BITS	0x01	/* 6 data bits */
#define	CFCR_5BITS	0x00	/* 5 data bits */

/* modem control register */
#define	MCR_LOOPBACK	0x10	/* loopback */
#define	MCR_IENABLE	0x08	/* output 2 = int enable */
#define	MCR_DRS		0x04	/* output 1 = xxx */
#define	MCR_RTS		0x02	/* enable RTS */
#define	MCR_DTR		0x01	/* enable DTR */

/* line status register */
#define	LSR_RCV_FIFO	0x80	/* error in receive fifo */
#define	LSR_TSRE	0x40	/* transmitter empty */
#define	LSR_TXRDY	0x20	/* transmitter ready */
#define	LSR_BI		0x10	/* break detected */
#define	LSR_FE		0x08	/* framing error */
#define	LSR_PE		0x04	/* parity error */
#define	LSR_OE		0x02	/* overrun error */
#define	LSR_RXRDY	0x01	/* receiver ready */
#define	LSR_RCV_MASK	0x1f

/* modem status register */
#define	MSR_DCD		0x80
#define	MSR_RI		0x40
#define	MSR_DSR		0x20
#define	MSR_CTS		0x10
#define	MSR_DDCD	0x08
#define	MSR_TERI	0x04
#define	MSR_DDSR	0x02
#define	MSR_DCTS	0x01

#include <time.h>
#include <dos.h>
#include <go32.h>
#include <dpmi.h>
typedef unsigned long u_long;

/* 16550 rx fifo trigger point */
#define FIFO_TRIGGER	FIFO_TRIGGER_4

/* input buffer size */
#define CBSIZE	4096

#define RAWHZ	18

#ifdef DOS_STATS
#define CNT_RX		16
#define CNT_TX		17
#define CNT_STRAY	18
#define CNT_ORUN	19
#define NCNT		20

static int intrcnt;
static int cnts[NCNT];
static char *cntnames[NCNT] =
{
  /* h/w interrupt counts. */
  "mlsc", "nopend", "txrdy", "?3",
  "rxrdy", "?5", "rls", "?7",
  "?8", "?9", "?a", "?b",
  "rxtout", "?d", "?e", "?f",
  /* s/w counts. */
  "rxcnt", "txcnt", "stray", "swoflo"
};

#define COUNT(x) cnts[x]++
#else
#define COUNT(x)
#endif

/* Main interrupt controller port addresses. */
#define ICU_BASE	0x20
#define ICU_OCW2	(ICU_BASE + 0)
#define ICU_MASK	(ICU_BASE + 1)

/* Original interrupt controller mask register. */
unsigned char icu_oldmask;

/* Maximum of 8 interrupts (we don't handle the slave icu yet). */
#define NINTR	8

static struct intrupt
  {
    char inuse;
    struct dos_ttystate *port;
    _go32_dpmi_seginfo old_rmhandler;
    _go32_dpmi_seginfo old_pmhandler;
    _go32_dpmi_seginfo new_rmhandler;
    _go32_dpmi_seginfo new_pmhandler;
    _go32_dpmi_registers regs;
  }
intrupts[NINTR];


static struct dos_ttystate
  {
    int base;
    int irq;
    int refcnt;
    struct intrupt *intrupt;
    int fifo;
    int baudrate;
    unsigned char cbuf[CBSIZE];
    unsigned int first;
    unsigned int count;
    int txbusy;
    unsigned char old_mcr;
    int ferr;
    int perr;
    int oflo;
    int msr;
  }
ports[4] =
{
  {
    COM1ADDR, 4, 0, NULL, 0, 0, "", 0, 0, 0, 0, 0, 0, 0, 0
  }
  ,
  {
    COM2ADDR, 3, 0, NULL, 0, 0, "", 0, 0, 0, 0, 0, 0, 0, 0
  }
  ,
  {
    COM3ADDR, 4, 0, NULL, 0, 0, "", 0, 0, 0, 0, 0, 0, 0, 0
  }
  ,
  {
    COM4ADDR, 3, 0, NULL, 0, 0, "", 0, 0, 0, 0, 0, 0, 0, 0
  }
};

static int dos_open (struct serial *scb, const char *name);
static void dos_raw (struct serial *scb);
static int dos_readchar (struct serial *scb, int timeout);
static int dos_setbaudrate (struct serial *scb, int rate);
static int dos_write (struct serial *scb, const char *str, int len);
static void dos_close (struct serial *scb);
static serial_ttystate dos_get_tty_state (struct serial *scb);
static int dos_set_tty_state (struct serial *scb, serial_ttystate state);
static int dos_baudconv (int rate);

#define inb(p,a)	inportb((p)->base + (a))
#define outb(p,a,v)	outportb((p)->base + (a), (v))
#define disable()	asm volatile ("cli");
#define enable()	asm volatile ("sti");


static int
dos_getc (volatile struct dos_ttystate *port)
{
  int c;

  if (port->count == 0)
    return -1;

  c = port->cbuf[port->first];
  disable ();
  port->first = (port->first + 1) & (CBSIZE - 1);
  port->count--;
  enable ();
  return c;
}


static int
dos_putc (int c, struct dos_ttystate *port)
{
  if (port->count >= CBSIZE - 1)
    return -1;
  port->cbuf[(port->first + port->count) & (CBSIZE - 1)] = c;
  port->count++;
  return 0;
}



static void
dos_comisr (int irq)
{
  struct dos_ttystate *port;
  unsigned char iir, lsr, c;

  disable ();			/* Paranoia */
  outportb (ICU_OCW2, 0x20);	/* End-Of-Interrupt */
#ifdef DOS_STATS
  ++intrcnt;
#endif

  port = intrupts[irq].port;
  if (!port)
    {
      COUNT (CNT_STRAY);
      return;			/* not open */
    }

  while (1)
    {
      iir = inb (port, com_iir) & IIR_IMASK;
      switch (iir)
	{

	case IIR_RLS:
	  lsr = inb (port, com_lsr);
	  goto rx;

	case IIR_RXTOUT:
	case IIR_RXRDY:
	  lsr = 0;

	rx:
	  do
	    {
	      c = inb (port, com_data);
	      if (lsr & (LSR_BI | LSR_FE | LSR_PE | LSR_OE))
		{
		  if (lsr & (LSR_BI | LSR_FE))
		    port->ferr++;
		  else if (lsr & LSR_PE)
		    port->perr++;
		  if (lsr & LSR_OE)
		    port->oflo++;
		}

	      if (dos_putc (c, port) < 0)
		{
		  COUNT (CNT_ORUN);
		}
	      else
		{
		  COUNT (CNT_RX);
		}
	    }
	  while ((lsr = inb (port, com_lsr)) & LSR_RXRDY);
	  break;

	case IIR_MLSC:
	  /* could be used to flowcontrol Tx */
	  port->msr = inb (port, com_msr);
	  break;

	case IIR_TXRDY:
	  port->txbusy = 0;
	  break;

	case IIR_NOPEND:
	  /* no more pending interrupts, all done */
	  return;

	default:
	  /* unexpected interrupt, ignore */
	  break;
	}
      COUNT (iir);
    }
}

#define ISRNAME(x) dos_comisr##x
#define ISR(x) static void ISRNAME(x)(void) {dos_comisr(x);}

ISR (0) ISR (1) ISR (2) ISR (3) /* OK */
ISR (4) ISR (5) ISR (6) ISR (7) /* OK */

typedef void (*isr_t) (void);

static isr_t isrs[NINTR] =
  {
       ISRNAME (0), ISRNAME (1), ISRNAME (2), ISRNAME (3),
       ISRNAME (4), ISRNAME (5), ISRNAME (6), ISRNAME (7)
  };



static struct intrupt *
dos_hookirq (unsigned int irq)
{
  struct intrupt *intr;
  unsigned int vec;
  isr_t isr;

  if (irq >= NINTR)
    return 0;

  intr = &intrupts[irq];
  if (intr->inuse)
    return 0;

  vec = 0x08 + irq;
  isr = isrs[irq];

  /* setup real mode handler */
  _go32_dpmi_get_real_mode_interrupt_vector (vec, &intr->old_rmhandler);

  intr->new_rmhandler.pm_selector = _go32_my_cs ();
  intr->new_rmhandler.pm_offset = (u_long) isr;
  if (_go32_dpmi_allocate_real_mode_callback_iret (&intr->new_rmhandler,
						   &intr->regs))
    {
      return 0;
    }

  if (_go32_dpmi_set_real_mode_interrupt_vector (vec, &intr->new_rmhandler))
    {
      return 0;
    }

  /* setup protected mode handler */
  _go32_dpmi_get_protected_mode_interrupt_vector (vec, &intr->old_pmhandler);

  intr->new_pmhandler.pm_selector = _go32_my_cs ();
  intr->new_pmhandler.pm_offset = (u_long) isr;
  _go32_dpmi_allocate_iret_wrapper (&intr->new_pmhandler);

  if (_go32_dpmi_set_protected_mode_interrupt_vector (vec,
						      &intr->new_pmhandler))
    {
      return 0;
    }

  /* setup interrupt controller mask */
  disable ();
  outportb (ICU_MASK, inportb (ICU_MASK) & ~(1 << irq));
  enable ();

  intr->inuse = 1;
  return intr;
}


static void
dos_unhookirq (struct intrupt *intr)
{
  unsigned int irq, vec;
  unsigned char mask;

  irq = intr - intrupts;
  vec = 0x08 + irq;

  /* restore old interrupt mask bit */
  mask = 1 << irq;
  disable ();
  outportb (ICU_MASK, inportb (ICU_MASK) | (mask & icu_oldmask));
  enable ();

  /* remove real mode handler */
  _go32_dpmi_set_real_mode_interrupt_vector (vec, &intr->old_rmhandler);
  _go32_dpmi_free_real_mode_callback (&intr->new_rmhandler);

  /* remove protected mode handler */
  _go32_dpmi_set_protected_mode_interrupt_vector (vec, &intr->old_pmhandler);
  _go32_dpmi_free_iret_wrapper (&intr->new_pmhandler);
  intr->inuse = 0;
}



static int
dos_open (struct serial *scb, const char *name)
{
  struct dos_ttystate *port;
  int fd, i;

  if (strncasecmp (name, "/dev/", 5) == 0)
    name += 5;
  else if (strncasecmp (name, "\\dev\\", 5) == 0)
    name += 5;

  if (strlen (name) != 4 || strncasecmp (name, "com", 3) != 0)
    {
      errno = ENOENT;
      return -1;
    }

  if (name[3] < '1' || name[3] > '4')
    {
      errno = ENOENT;
      return -1;
    }

  /* FIXME: this is a Bad Idea (tm)!  One should *never* invent file
     handles, since they might be already used by other files/devices.
     The Right Way to do this is to create a real handle by dup()'ing
     some existing one.  */
  fd = name[3] - '1';
  port = &ports[fd];
  if (port->refcnt++ > 0)
    {
      /* Device already opened another user.  Just point at it. */
      scb->fd = fd;
      return 0;
    }

  /* force access to ID reg */
  outb (port, com_cfcr, 0);
  outb (port, com_iir, 0);
  for (i = 0; i < 17; i++)
    {
      if ((inb (port, com_iir) & 0x38) == 0)
	goto ok;
      (void) inb (port, com_data);	/* clear recv */
    }
  errno = ENODEV;
  return -1;

ok:
  /* disable all interrupts in chip */
  outb (port, com_ier, 0);

  /* tentatively enable 16550 fifo, and see if it responds */
  outb (port, com_fifo,
	FIFO_ENABLE | FIFO_RCV_RST | FIFO_XMT_RST | FIFO_TRIGGER);
  sleep (1);
  port->fifo = ((inb (port, com_iir) & IIR_FIFO_MASK) == IIR_FIFO_MASK);

  /* clear pending status reports. */
  (void) inb (port, com_lsr);
  (void) inb (port, com_msr);

  /* enable external interrupt gate (to avoid floating IRQ) */
  outb (port, com_mcr, MCR_IENABLE);

  /* hook up interrupt handler and initialise icu */
  port->intrupt = dos_hookirq (port->irq);
  if (!port->intrupt)
    {
      outb (port, com_mcr, 0);
      outb (port, com_fifo, 0);
      errno = ENODEV;
      return -1;
    }

  disable ();

  /* record port */
  port->intrupt->port = port;
  scb->fd = fd;

  /* clear rx buffer, tx busy flag and overflow count */
  port->first = port->count = 0;
  port->txbusy = 0;
  port->oflo = 0;

  /* set default baud rate and mode: 9600,8,n,1 */
  i = dos_baudconv (port->baudrate = 9600);
  outb (port, com_cfcr, CFCR_DLAB);
  outb (port, com_dlbl, i & 0xff);
  outb (port, com_dlbh, i >> 8);
  outb (port, com_cfcr, CFCR_8BITS);

  /* enable all interrupts */
  outb (port, com_ier, IER_ETXRDY | IER_ERXRDY | IER_ERLS | IER_EMSC);

  /* enable DTR & RTS */
  outb (port, com_mcr, MCR_DTR | MCR_RTS | MCR_IENABLE);

  enable ();

  return 0;
}


static void
dos_close (struct serial *scb)
{
  struct dos_ttystate *port;
  struct intrupt *intrupt;

  if (!scb)
    return;

  port = &ports[scb->fd];

  if (port->refcnt-- > 1)
    return;

  if (!(intrupt = port->intrupt))
    return;

  /* disable interrupts, fifo, flow control */
  disable ();
  port->intrupt = 0;
  intrupt->port = 0;
  outb (port, com_fifo, 0);
  outb (port, com_ier, 0);
  enable ();

  /* unhook handler, and disable interrupt gate */
  dos_unhookirq (intrupt);
  outb (port, com_mcr, 0);

  /* Check for overflow errors */
  if (port->oflo)
    {
      fprintf_unfiltered (gdb_stderr,
			  "Serial input overruns occurred.\n");
      fprintf_unfiltered (gdb_stderr, "This system %s handle %d baud.\n",
			  port->fifo ? "cannot" : "needs a 16550 to",
			  port->baudrate);
    }
}



static int
dos_noop (struct serial *scb)
{
  return 0;
}

static void
dos_raw (struct serial *scb)
{
  /* Always in raw mode */
}

static int
dos_readchar (struct serial *scb, int timeout)
{
  struct dos_ttystate *port = &ports[scb->fd];
  long then;
  int c;

  then = rawclock () + (timeout * RAWHZ);
  while ((c = dos_getc (port)) < 0)
    {
      if (timeout >= 0 && (rawclock () - then) >= 0)
	return SERIAL_TIMEOUT;
    }

  return c;
}


static serial_ttystate
dos_get_tty_state (struct serial *scb)
{
  struct dos_ttystate *port = &ports[scb->fd];
  struct dos_ttystate *state;

  /* Are they asking about a port we opened?  */
  if (port->refcnt <= 0)
    {
      /* We've never heard about this port.  We should fail this call,
	 unless they are asking about one of the 3 standard handles,
	 in which case we pretend the handle was open by us if it is
	 connected to a terminal device.  This is beacuse Unix
	 terminals use the serial interface, so GDB expects the
	 standard handles to go through here.  */
      if (scb->fd >= 3 || !isatty (scb->fd))
	return NULL;
    }

  state = (struct dos_ttystate *) xmalloc (sizeof *state);
  *state = *port;
  return (serial_ttystate) state;
}

static int
dos_set_tty_state (struct serial *scb, serial_ttystate ttystate)
{
  struct dos_ttystate *state;

  state = (struct dos_ttystate *) ttystate;
  dos_setbaudrate (scb, state->baudrate);
  return 0;
}

static int
dos_noflush_set_tty_state (struct serial *scb, serial_ttystate new_ttystate,
			   serial_ttystate old_ttystate)
{
  struct dos_ttystate *state;

  state = (struct dos_ttystate *) new_ttystate;
  dos_setbaudrate (scb, state->baudrate);
  return 0;
}

static int
dos_flush_input (struct serial *scb)
{
  struct dos_ttystate *port = &ports[scb->fd];
  disable ();
  port->first = port->count = 0;
  if (port->fifo)
    outb (port, com_fifo, FIFO_ENABLE | FIFO_RCV_RST | FIFO_TRIGGER);
  enable ();
  return 0;
}

static void
dos_print_tty_state (struct serial *scb, serial_ttystate ttystate,
		     struct ui_file *stream)
{
  /* Nothing to print */
  return;
}

static int
dos_baudconv (int rate)
{
  long x, err;

  if (rate <= 0)
    return -1;

#define divrnd(n, q)	(((n) * 2 / (q) + 1) / 2) /* divide and round off */
  x = divrnd (COMTICK, rate);
  if (x <= 0)
    return -1;

  err = divrnd (1000 * COMTICK, x * rate) - 1000;
  if (err < 0)
    err = -err;
  if (err > SPEED_TOLERANCE)
    return -1;
#undef divrnd
  return x;
}


static int
dos_setbaudrate (struct serial *scb, int rate)
{
  struct dos_ttystate *port = &ports[scb->fd];

  if (port->baudrate != rate)
    {
      int x;
      unsigned char cfcr;

      x = dos_baudconv (rate);
      if (x <= 0)
	{
	  fprintf_unfiltered (gdb_stderr, "%d: impossible baudrate\n", rate);
	  errno = EINVAL;
	  return -1;
	}

      disable ();
      cfcr = inb (port, com_cfcr);

      outb (port, com_cfcr, CFCR_DLAB);
      outb (port, com_dlbl, x & 0xff);
      outb (port, com_dlbh, x >> 8);
      outb (port, com_cfcr, cfcr);
      port->baudrate = rate;
      enable ();
    }

  return 0;
}

static int
dos_setstopbits (struct serial *scb, int num)
{
  struct dos_ttystate *port = &ports[scb->fd];
  unsigned char cfcr;

  disable ();
  cfcr = inb (port, com_cfcr);

  switch (num)
    {
    case SERIAL_1_STOPBITS:
      outb (port, com_cfcr, cfcr & ~CFCR_STOPB);
      break;
    case SERIAL_1_AND_A_HALF_STOPBITS:
    case SERIAL_2_STOPBITS:
      outb (port, com_cfcr, cfcr | CFCR_STOPB);
      break;
    default:
      enable ();
      return 1;
    }
  enable ();

  return 0;
}

static int
dos_write (struct serial *scb, const char *str, int len)
{
  volatile struct dos_ttystate *port = &ports[scb->fd];
  int fifosize = port->fifo ? 16 : 1;
  long then;
  int cnt;

  while (len > 0)
    {
      /* send the data, fifosize bytes at a time */
      cnt = fifosize > len ? len : fifosize;
      port->txbusy = 1;
      /* Francisco Pastor <fpastor.etra-id@etra.es> says OUTSB messes
	 up the communications with UARTs with FIFOs.  */
#ifdef UART_FIFO_WORKS
      outportsb (port->base + com_data, str, cnt);
      str += cnt;
      len -= cnt;
#else
      for ( ; cnt > 0; cnt--, len--)
	outportb (port->base + com_data, *str++);
#endif
#ifdef DOS_STATS
      cnts[CNT_TX] += cnt;
#endif
      /* wait for transmission to complete (max 1 sec) */
      then = rawclock () + RAWHZ;
      while (port->txbusy)
	{
	  if ((rawclock () - then) >= 0)
	    {
	      errno = EIO;
	      return SERIAL_ERROR;
	    }
	}
    }
  return 0;
}


static int
dos_sendbreak (struct serial *scb)
{
  volatile struct dos_ttystate *port = &ports[scb->fd];
  unsigned char cfcr;
  long then;

  cfcr = inb (port, com_cfcr);
  outb (port, com_cfcr, cfcr | CFCR_SBREAK);

  /* 0.25 sec delay */
  then = rawclock () + RAWHZ / 4;
  while ((rawclock () - then) < 0)
    continue;

  outb (port, com_cfcr, cfcr);
  return 0;
}


static struct serial_ops dos_ops =
{
  "hardwire",
  0,
  dos_open,
  dos_close,
  dos_readchar,
  dos_write,
  dos_noop,			/* flush output */
  dos_flush_input,
  dos_sendbreak,
  dos_raw,
  dos_get_tty_state,
  dos_set_tty_state,
  dos_print_tty_state,
  dos_noflush_set_tty_state,
  dos_setbaudrate,
  dos_setstopbits,
  dos_noop,			/* wait for output to drain */
  (void (*)(struct serial *, int))NULL	/* change into async mode */
};


static void
dos_info (char *arg, int from_tty)
{
  struct dos_ttystate *port;
#ifdef DOS_STATS
  int i;
#endif

  for (port = ports; port < &ports[4]; port++)
    {
      if (port->baudrate == 0)
	continue;
      printf_filtered ("Port:\tCOM%ld (%sactive)\n", (long)(port - ports) + 1,
		       port->intrupt ? "" : "not ");
      printf_filtered ("Addr:\t0x%03x (irq %d)\n", port->base, port->irq);
      printf_filtered ("16550:\t%s\n", port->fifo ? "yes" : "no");
      printf_filtered ("Speed:\t%d baud\n", port->baudrate);
      printf_filtered ("Errs:\tframing %d parity %d overflow %d\n\n",
		       port->ferr, port->perr, port->oflo);
    }

#ifdef DOS_STATS
  printf_filtered ("\nTotal interrupts: %d\n", intrcnt);
  for (i = 0; i < NCNT; i++)
    if (cnts[i])
      printf_filtered ("%s:\t%d\n", cntnames[i], cnts[i]);
#endif
}


void
_initialize_ser_dos (void)
{
  serial_add_interface (&dos_ops);

  /* Save original interrupt mask register. */
  icu_oldmask = inportb (ICU_MASK);

  /* Mark fixed motherboard irqs as inuse. */
  intrupts[0].inuse =		/* timer tick */
    intrupts[1].inuse =		/* keyboard */
    intrupts[2].inuse = 1;	/* slave icu */

  add_show_from_set (
		      add_set_cmd ("com1base", class_obscure, var_zinteger,
				   (char *) &ports[0].base,
				   "Set COM1 base i/o port address.",
				   &setlist),
		      &showlist);

  add_show_from_set (
		      add_set_cmd ("com1irq", class_obscure, var_zinteger,
				   (char *) &ports[0].irq,
				   "Set COM1 interrupt request.",
				   &setlist),
		      &showlist);

  add_show_from_set (
		      add_set_cmd ("com2base", class_obscure, var_zinteger,
				   (char *) &ports[1].base,
				   "Set COM2 base i/o port address.",
				   &setlist),
		      &showlist);

  add_show_from_set (
		      add_set_cmd ("com2irq", class_obscure, var_zinteger,
				   (char *) &ports[1].irq,
				   "Set COM2 interrupt request.",
				   &setlist),
		      &showlist);

  add_show_from_set (
		      add_set_cmd ("com3base", class_obscure, var_zinteger,
				   (char *) &ports[2].base,
				   "Set COM3 base i/o port address.",
				   &setlist),
		      &showlist);

  add_show_from_set (
		      add_set_cmd ("com3irq", class_obscure, var_zinteger,
				   (char *) &ports[2].irq,
				   "Set COM3 interrupt request.",
				   &setlist),
		      &showlist);

  add_show_from_set (
		      add_set_cmd ("com4base", class_obscure, var_zinteger,
				   (char *) &ports[3].base,
				   "Set COM4 base i/o port address.",
				   &setlist),
		      &showlist);

  add_show_from_set (
		      add_set_cmd ("com4irq", class_obscure, var_zinteger,
				   (char *) &ports[3].irq,
				   "Set COM4 interrupt request.",
				   &setlist),
		      &showlist);

  add_info ("serial", dos_info,
	    "Print DOS serial port status.");
}
