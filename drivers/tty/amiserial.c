// SPDX-License-Identifier: GPL-2.0
/*
 * Serial driver for the amiga builtin port.
 *
 * This code was created by taking serial.c version 4.30 from kernel
 * release 2.3.22, replacing all hardware related stuff with the
 * corresponding amiga hardware actions, and removing all irrelevant
 * code. As a consequence, it uses many of the constants and names
 * associated with the registers and bits of 16550 compatible UARTS -
 * but only to keep track of status, etc in the state variables. It
 * was done this was to make it easier to keep the code in line with
 * (non hardware specific) changes to serial.c.
 *
 * The port is registered with the tty driver as minor device 64, and
 * therefore other ports should should only use 65 upwards.
 *
 * Richard Lucock 28/12/99
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 
 * 		1998, 1999  Theodore Ts'o
 *
 */

#include <linux/delay.h>

/* Set of debugging defines */

#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW
#undef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT

/* Sanity checks */

#if defined(MODULE) && defined(SERIAL_DEBUG_MCOUNT)
#define DBG_CNT(s) printk("(%s): [%x] refc=%d, serc=%d, ttyc=%d -> %s\n", \
 tty->name, (info->tport.flags), serial_driver->refcount,info->count,tty->count,s)
#else
#define DBG_CNT(s)
#endif

/*
 * End of serial driver configuration section.
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
static char *serial_version = "4.30";

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/circ_buf.h>
#include <linux/console.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>

#include <asm/setup.h>


#include <asm/irq.h>

#include <asm/amigahw.h>
#include <asm/amigaints.h>

struct serial_state {
	struct tty_port		tport;
	struct circ_buf		xmit;
	struct async_icount	icount;

	unsigned long		port;
	int			baud_base;
	int			xmit_fifo_size;
	int			custom_divisor;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			quot;
	int			IER; 	/* Interrupt Enable Register */
	int			MCR; 	/* Modem control register */
	int			x_char;	/* xon/xoff character */
};

#define custom amiga_custom
static char *serial_name = "Amiga-builtin serial driver";

static struct tty_driver *serial_driver;

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

static unsigned char current_ctl_bits;

static void change_speed(struct tty_struct *tty, struct serial_state *info,
		struct ktermios *old);
static void rs_wait_until_sent(struct tty_struct *tty, int timeout);


static struct serial_state rs_table[1];

#define NR_PORTS ARRAY_SIZE(rs_table)

#include <linux/uaccess.h>

#define serial_isroot()	(capable(CAP_SYS_ADMIN))

/* some serial hardware definitions */
#define SDR_OVRUN   (1<<15)
#define SDR_RBF     (1<<14)
#define SDR_TBE     (1<<13)
#define SDR_TSRE    (1<<12)

#define SERPER_PARENB    (1<<15)

#define AC_SETCLR   (1<<15)
#define AC_UARTBRK  (1<<11)

#define SER_DTR     (1<<7)
#define SER_RTS     (1<<6)
#define SER_DCD     (1<<5)
#define SER_CTS     (1<<4)
#define SER_DSR     (1<<3)

static __inline__ void rtsdtr_ctrl(int bits)
{
    ciab.pra = ((bits & (SER_RTS | SER_DTR)) ^ (SER_RTS | SER_DTR)) | (ciab.pra & ~(SER_RTS | SER_DTR));
}

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void rs_stop(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	local_irq_save(flags);
	if (info->IER & UART_IER_THRI) {
		info->IER &= ~UART_IER_THRI;
		/* disable Tx interrupt and remove any pending interrupts */
		custom.intena = IF_TBE;
		mb();
		custom.intreq = IF_TBE;
		mb();
	}
	local_irq_restore(flags);
}

static void rs_start(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	local_irq_save(flags);
	if (info->xmit.head != info->xmit.tail
	    && info->xmit.buf
	    && !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		custom.intena = IF_SETCLR | IF_TBE;
		mb();
		/* set a pending Tx Interrupt, transmitter should restart now */
		custom.intreq = IF_SETCLR | IF_TBE;
		mb();
	}
	local_irq_restore(flags);
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * Note: rs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * rs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 * 
 * gcc -S -DKERNEL -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer serial.c
 *
 * and look at the resulting assemble code in serial.s.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

static void receive_chars(struct serial_state *info)
{
        int status;
	int serdatr;
	unsigned char ch, flag;
	struct	async_icount *icount;
	int oe = 0;

	icount = &info->icount;

	status = UART_LSR_DR; /* We obviously have a character! */
	serdatr = custom.serdatr;
	mb();
	custom.intreq = IF_RBF;
	mb();

	if((serdatr & 0x1ff) == 0)
	    status |= UART_LSR_BI;
	if(serdatr & SDR_OVRUN)
	    status |= UART_LSR_OE;

	ch = serdatr & 0xff;
	icount->rx++;

#ifdef SERIAL_DEBUG_INTR
	printk("DR%02x:%02x...", ch, status);
#endif
	flag = TTY_NORMAL;

	/*
	 * We don't handle parity or frame errors - but I have left
	 * the code in, since I'm not sure that the errors can't be
	 * detected.
	 */

	if (status & (UART_LSR_BI | UART_LSR_PE |
		      UART_LSR_FE | UART_LSR_OE)) {
	  /*
	   * For statistics only
	   */
	  if (status & UART_LSR_BI) {
	    status &= ~(UART_LSR_FE | UART_LSR_PE);
	    icount->brk++;
	  } else if (status & UART_LSR_PE)
	    icount->parity++;
	  else if (status & UART_LSR_FE)
	    icount->frame++;
	  if (status & UART_LSR_OE)
	    icount->overrun++;

	  /*
	   * Now check to see if character should be
	   * ignored, and mask off conditions which
	   * should be ignored.
	   */
	  if (status & info->ignore_status_mask)
	    goto out;

	  status &= info->read_status_mask;

	  if (status & (UART_LSR_BI)) {
#ifdef SERIAL_DEBUG_INTR
	    printk("handling break....");
#endif
	    flag = TTY_BREAK;
	    if (info->tport.flags & ASYNC_SAK)
	      do_SAK(info->tport.tty);
	  } else if (status & UART_LSR_PE)
	    flag = TTY_PARITY;
	  else if (status & UART_LSR_FE)
	    flag = TTY_FRAME;
	  if (status & UART_LSR_OE) {
	    /*
	     * Overrun is special, since it's
	     * reported immediately, and doesn't
	     * affect the current character
	     */
	     oe = 1;
	  }
	}
	tty_insert_flip_char(&info->tport, ch, flag);
	if (oe == 1)
		tty_insert_flip_char(&info->tport, 0, TTY_OVERRUN);
	tty_flip_buffer_push(&info->tport);
out:
	return;
}

static void transmit_chars(struct serial_state *info)
{
	custom.intreq = IF_TBE;
	mb();
	if (info->x_char) {
	        custom.serdat = info->x_char | 0x100;
		mb();
		info->icount.tx++;
		info->x_char = 0;
		return;
	}
	if (info->xmit.head == info->xmit.tail
	    || info->tport.tty->stopped
	    || info->tport.tty->hw_stopped) {
		info->IER &= ~UART_IER_THRI;
	        custom.intena = IF_TBE;
		mb();
		return;
	}

	custom.serdat = info->xmit.buf[info->xmit.tail++] | 0x100;
	mb();
	info->xmit.tail = info->xmit.tail & (SERIAL_XMIT_SIZE-1);
	info->icount.tx++;

	if (CIRC_CNT(info->xmit.head,
		     info->xmit.tail,
		     SERIAL_XMIT_SIZE) < WAKEUP_CHARS)
		tty_wakeup(info->tport.tty);

#ifdef SERIAL_DEBUG_INTR
	printk("THRE...");
#endif
	if (info->xmit.head == info->xmit.tail) {
	        custom.intena = IF_TBE;
		mb();
		info->IER &= ~UART_IER_THRI;
	}
}

static void check_modem_status(struct serial_state *info)
{
	struct tty_port *port = &info->tport;
	unsigned char status = ciab.pra & (SER_DCD | SER_CTS | SER_DSR);
	unsigned char dstatus;
	struct	async_icount *icount;

	/* Determine bits that have changed */
	dstatus = status ^ current_ctl_bits;
	current_ctl_bits = status;

	if (dstatus) {
		icount = &info->icount;
		/* update input line counters */
		if (dstatus & SER_DSR)
			icount->dsr++;
		if (dstatus & SER_DCD) {
			icount->dcd++;
		}
		if (dstatus & SER_CTS)
			icount->cts++;
		wake_up_interruptible(&port->delta_msr_wait);
	}

	if (tty_port_check_carrier(port) && (dstatus & SER_DCD)) {
#if (defined(SERIAL_DEBUG_OPEN) || defined(SERIAL_DEBUG_INTR))
		printk("ttyS%d CD now %s...", info->line,
		       (!(status & SER_DCD)) ? "on" : "off");
#endif
		if (!(status & SER_DCD))
			wake_up_interruptible(&port->open_wait);
		else {
#ifdef SERIAL_DEBUG_OPEN
			printk("doing serial hangup...");
#endif
			if (port->tty)
				tty_hangup(port->tty);
		}
	}
	if (tty_port_cts_enabled(port)) {
		if (port->tty->hw_stopped) {
			if (!(status & SER_CTS)) {
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
				printk("CTS tx start...");
#endif
				port->tty->hw_stopped = 0;
				info->IER |= UART_IER_THRI;
				custom.intena = IF_SETCLR | IF_TBE;
				mb();
				/* set a pending Tx Interrupt, transmitter should restart now */
				custom.intreq = IF_SETCLR | IF_TBE;
				mb();
				tty_wakeup(port->tty);
				return;
			}
		} else {
			if ((status & SER_CTS)) {
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
				printk("CTS tx stop...");
#endif
				port->tty->hw_stopped = 1;
				info->IER &= ~UART_IER_THRI;
				/* disable Tx interrupt and remove any pending interrupts */
				custom.intena = IF_TBE;
				mb();
				custom.intreq = IF_TBE;
				mb();
			}
		}
	}
}

static irqreturn_t ser_vbl_int( int irq, void *data)
{
        /* vbl is just a periodic interrupt we tie into to update modem status */
	struct serial_state *info = data;
	/*
	 * TBD - is it better to unregister from this interrupt or to
	 * ignore it if MSI is clear ?
	 */
	if(info->IER & UART_IER_MSI)
	  check_modem_status(info);
	return IRQ_HANDLED;
}

static irqreturn_t ser_rx_int(int irq, void *dev_id)
{
	struct serial_state *info = dev_id;

#ifdef SERIAL_DEBUG_INTR
	printk("ser_rx_int...");
#endif

	if (!info->tport.tty)
		return IRQ_NONE;

	receive_chars(info);
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
	return IRQ_HANDLED;
}

static irqreturn_t ser_tx_int(int irq, void *dev_id)
{
	struct serial_state *info = dev_id;

	if (custom.serdatr & SDR_TBE) {
#ifdef SERIAL_DEBUG_INTR
	  printk("ser_tx_int...");
#endif

	  if (!info->tport.tty)
		return IRQ_NONE;

	  transmit_chars(info);
#ifdef SERIAL_DEBUG_INTR
	  printk("end.\n");
#endif
	}
	return IRQ_HANDLED;
}

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * ---------------------------------------------------------------
 * Low level utility subroutines for the serial driver:  routines to
 * figure out the appropriate timeout for an interrupt chain, routines
 * to initialize and startup a serial port, and routines to shutdown a
 * serial port.  Useful stuff like that.
 * ---------------------------------------------------------------
 */

static int startup(struct tty_struct *tty, struct serial_state *info)
{
	struct tty_port *port = &info->tport;
	unsigned long flags;
	int	retval=0;
	unsigned long page;

	page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	local_irq_save(flags);

	if (tty_port_initialized(port)) {
		free_page(page);
		goto errout;
	}

	if (info->xmit.buf)
		free_page(page);
	else
		info->xmit.buf = (unsigned char *) page;

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttys%d ...", info->line);
#endif

	/* Clear anything in the input buffer */

	custom.intreq = IF_RBF;
	mb();

	retval = request_irq(IRQ_AMIGA_VERTB, ser_vbl_int, 0, "serial status", info);
	if (retval) {
	  if (serial_isroot()) {
	      set_bit(TTY_IO_ERROR, &tty->flags);
	    retval = 0;
	  }
	  goto errout;
	}

	/* enable both Rx and Tx interrupts */
	custom.intena = IF_SETCLR | IF_RBF | IF_TBE;
	mb();
	info->IER = UART_IER_MSI;

	/* remember current state of the DCD and CTS bits */
	current_ctl_bits = ciab.pra & (SER_DCD | SER_CTS | SER_DSR);

	info->MCR = 0;
	if (C_BAUD(tty))
	  info->MCR = SER_DTR | SER_RTS;
	rtsdtr_ctrl(info->MCR);

	clear_bit(TTY_IO_ERROR, &tty->flags);
	info->xmit.head = info->xmit.tail = 0;

	/*
	 * and set the speed of the serial port
	 */
	change_speed(tty, info, NULL);

	tty_port_set_initialized(port, 1);
	local_irq_restore(flags);
	return 0;

errout:
	local_irq_restore(flags);
	return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct tty_struct *tty, struct serial_state *info)
{
	unsigned long	flags;
	struct serial_state *state;

	if (!tty_port_initialized(&info->tport))
		return;

	state = info;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d ....\n", info->line);
#endif

	local_irq_save(flags); /* Disable interrupts */

	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */
	wake_up_interruptible(&info->tport.delta_msr_wait);

	/*
	 * Free the IRQ, if necessary
	 */
	free_irq(IRQ_AMIGA_VERTB, info);

	if (info->xmit.buf) {
		free_page((unsigned long) info->xmit.buf);
		info->xmit.buf = NULL;
	}

	info->IER = 0;
	custom.intena = IF_RBF | IF_TBE;
	mb();

	/* disable break condition */
	custom.adkcon = AC_UARTBRK;
	mb();

	if (C_HUPCL(tty))
		info->MCR &= ~(SER_DTR|SER_RTS);
	rtsdtr_ctrl(info->MCR);

	set_bit(TTY_IO_ERROR, &tty->flags);

	tty_port_set_initialized(&info->tport, 0);
	local_irq_restore(flags);
}


/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct tty_struct *tty, struct serial_state *info,
			 struct ktermios *old_termios)
{
	struct tty_port *port = &info->tport;
	int	quot = 0, baud_base, baud;
	unsigned cflag, cval = 0;
	int	bits;
	unsigned long	flags;

	cflag = tty->termios.c_cflag;

	/* Byte size is always 8 bits plus parity bit if requested */

	cval = 3; bits = 10;
	if (cflag & CSTOPB) {
		cval |= 0x04;
		bits++;
	}
	if (cflag & PARENB) {
		cval |= UART_LCR_PARITY;
		bits++;
	}
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
#ifdef CMSPAR
	if (cflag & CMSPAR)
		cval |= UART_LCR_SPAR;
#endif

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);
	if (!baud)
		baud = 9600;	/* B0 transition handled in rs_set_termios */
	baud_base = info->baud_base;
	if (baud == 38400 && (port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST)
		quot = info->custom_divisor;
	else {
		if (baud == 134)
			/* Special case since 134 is really 134.5 */
			quot = (2*baud_base / 269);
		else if (baud)
			quot = baud_base / baud;
	}
	/* If the quotient is zero refuse the change */
	if (!quot && old_termios) {
		/* FIXME: Will need updating for new tty in the end */
		tty->termios.c_cflag &= ~CBAUD;
		tty->termios.c_cflag |= (old_termios->c_cflag & CBAUD);
		baud = tty_get_baud_rate(tty);
		if (!baud)
			baud = 9600;
		if (baud == 38400 &&
		    (port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST)
			quot = info->custom_divisor;
		else {
			if (baud == 134)
				/* Special case since 134 is really 134.5 */
				quot = (2*baud_base / 269);
			else if (baud)
				quot = baud_base / baud;
		}
	}
	/* As a last resort, if the quotient is zero, default to 9600 bps */
	if (!quot)
		quot = baud_base / 9600;
	info->quot = quot;
	info->timeout = ((info->xmit_fifo_size*HZ*bits*quot) / baud_base);
	info->timeout += HZ/50;		/* Add .02 seconds of slop */

	/* CTS flow control flag and modem status interrupts */
	info->IER &= ~UART_IER_MSI;
	if (port->flags & ASYNC_HARDPPS_CD)
		info->IER |= UART_IER_MSI;
	tty_port_set_cts_flow(port, cflag & CRTSCTS);
	if (cflag & CRTSCTS)
		info->IER |= UART_IER_MSI;
	tty_port_set_check_carrier(port, ~cflag & CLOCAL);
	if (~cflag & CLOCAL)
		info->IER |= UART_IER_MSI;
	/* TBD:
	 * Does clearing IER_MSI imply that we should disable the VBL interrupt ?
	 */

	/*
	 * Set up parity check flag
	 */

	info->read_status_mask = UART_LSR_OE | UART_LSR_DR;
	if (I_INPCK(tty))
		info->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (I_BRKINT(tty) || I_PARMRK(tty))
		info->read_status_mask |= UART_LSR_BI;

	/*
	 * Characters to ignore
	 */
	info->ignore_status_mask = 0;
	if (I_IGNPAR(tty))
		info->ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (I_IGNBRK(tty)) {
		info->ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore 
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(tty))
			info->ignore_status_mask |= UART_LSR_OE;
	}
	/*
	 * !!! ignore all characters if CREAD is not set
	 */
	if ((cflag & CREAD) == 0)
		info->ignore_status_mask |= UART_LSR_DR;
	local_irq_save(flags);

	{
	  short serper;

	/* Set up the baud rate */
	  serper = quot - 1;

	/* Enable or disable parity bit */

	if(cval & UART_LCR_PARITY)
	  serper |= (SERPER_PARENB);

	custom.serper = serper;
	mb();
	}

	local_irq_restore(flags);
}

static int rs_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct serial_state *info;
	unsigned long flags;

	info = tty->driver_data;

	if (!info->xmit.buf)
		return 0;

	local_irq_save(flags);
	if (CIRC_SPACE(info->xmit.head,
		       info->xmit.tail,
		       SERIAL_XMIT_SIZE) == 0) {
		local_irq_restore(flags);
		return 0;
	}

	info->xmit.buf[info->xmit.head++] = ch;
	info->xmit.head &= SERIAL_XMIT_SIZE-1;
	local_irq_restore(flags);
	return 1;
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	if (info->xmit.head == info->xmit.tail
	    || tty->stopped
	    || tty->hw_stopped
	    || !info->xmit.buf)
		return;

	local_irq_save(flags);
	info->IER |= UART_IER_THRI;
	custom.intena = IF_SETCLR | IF_TBE;
	mb();
	/* set a pending Tx Interrupt, transmitter should restart now */
	custom.intreq = IF_SETCLR | IF_TBE;
	mb();
	local_irq_restore(flags);
}

static int rs_write(struct tty_struct * tty, const unsigned char *buf, int count)
{
	int	c, ret = 0;
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	if (!info->xmit.buf)
		return 0;

	local_irq_save(flags);
	while (1) {
		c = CIRC_SPACE_TO_END(info->xmit.head,
				      info->xmit.tail,
				      SERIAL_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0) {
			break;
		}
		memcpy(info->xmit.buf + info->xmit.head, buf, c);
		info->xmit.head = ((info->xmit.head + c) &
				   (SERIAL_XMIT_SIZE-1));
		buf += c;
		count -= c;
		ret += c;
	}
	local_irq_restore(flags);

	if (info->xmit.head != info->xmit.tail
	    && !tty->stopped
	    && !tty->hw_stopped
	    && !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		local_irq_disable();
		custom.intena = IF_SETCLR | IF_TBE;
		mb();
		/* set a pending Tx Interrupt, transmitter should restart now */
		custom.intreq = IF_SETCLR | IF_TBE;
		mb();
		local_irq_restore(flags);
	}
	return ret;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;

	return CIRC_SPACE(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

static int rs_chars_in_buffer(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;

	return CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	local_irq_save(flags);
	info->xmit.head = info->xmit.tail = 0;
	local_irq_restore(flags);
	tty_wakeup(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void rs_send_xchar(struct tty_struct *tty, char ch)
{
	struct serial_state *info = tty->driver_data;
        unsigned long flags;

	info->x_char = ch;
	if (ch) {
		/* Make sure transmit interrupts are on */

	        /* Check this ! */
	        local_irq_save(flags);
		if(!(custom.intenar & IF_TBE)) {
		    custom.intena = IF_SETCLR | IF_TBE;
		    mb();
		    /* set a pending Tx Interrupt, transmitter should restart now */
		    custom.intreq = IF_SETCLR | IF_TBE;
		    mb();
		}
		local_irq_restore(flags);

		info->IER |= UART_IER_THRI;
	}
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_throttle(struct tty_struct * tty)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	printk("throttle %s ....\n", tty_name(tty));
#endif

	if (I_IXOFF(tty))
		rs_send_xchar(tty, STOP_CHAR(tty));

	if (C_CRTSCTS(tty))
		info->MCR &= ~SER_RTS;

	local_irq_save(flags);
	rtsdtr_ctrl(info->MCR);
	local_irq_restore(flags);
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	printk("unthrottle %s ....\n", tty_name(tty));
#endif

	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			rs_send_xchar(tty, START_CHAR(tty));
	}
	if (C_CRTSCTS(tty))
		info->MCR |= SER_RTS;
	local_irq_save(flags);
	rtsdtr_ctrl(info->MCR);
	local_irq_restore(flags);
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct tty_struct *tty, struct serial_struct *ss)
{
	struct serial_state *state = tty->driver_data;

	tty_lock(tty);
	ss->line = tty->index;
	ss->port = state->port;
	ss->flags = state->tport.flags;
	ss->xmit_fifo_size = state->xmit_fifo_size;
	ss->baud_base = state->baud_base;
	ss->close_delay = state->tport.close_delay;
	ss->closing_wait = state->tport.closing_wait;
	ss->custom_divisor = state->custom_divisor;
	tty_unlock(tty);
	return 0;
}

static int set_serial_info(struct tty_struct *tty, struct serial_struct *ss)
{
	struct serial_state *state = tty->driver_data;
	struct tty_port *port = &state->tport;
	bool change_spd;
	int 			retval = 0;

	tty_lock(tty);
	change_spd = ((ss->flags ^ port->flags) & ASYNC_SPD_MASK) ||
		ss->custom_divisor != state->custom_divisor;
	if (ss->irq || ss->port != state->port ||
			ss->xmit_fifo_size != state->xmit_fifo_size) {
		tty_unlock(tty);
		return -EINVAL;
	}
  
	if (!serial_isroot()) {
		if ((ss->baud_base != state->baud_base) ||
		    (ss->close_delay != port->close_delay) ||
		    (ss->xmit_fifo_size != state->xmit_fifo_size) ||
		    ((ss->flags & ~ASYNC_USR_MASK) !=
		     (port->flags & ~ASYNC_USR_MASK))) {
			tty_unlock(tty);
			return -EPERM;
		}
		port->flags = ((port->flags & ~ASYNC_USR_MASK) |
			       (ss->flags & ASYNC_USR_MASK));
		state->custom_divisor = ss->custom_divisor;
		goto check_and_exit;
	}

	if (ss->baud_base < 9600) {
		tty_unlock(tty);
		return -EINVAL;
	}

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	state->baud_base = ss->baud_base;
	port->flags = ((port->flags & ~ASYNC_FLAGS) |
			(ss->flags & ASYNC_FLAGS));
	state->custom_divisor = ss->custom_divisor;
	port->close_delay = ss->close_delay * HZ/100;
	port->closing_wait = ss->closing_wait * HZ/100;

check_and_exit:
	if (tty_port_initialized(port)) {
		if (change_spd) {
			/* warn about deprecation unless clearing */
			if (ss->flags & ASYNC_SPD_MASK)
				dev_warn_ratelimited(tty->dev, "use of SPD flags is deprecated\n");
			change_speed(tty, state, NULL);
		}
	} else
		retval = startup(tty, state);
	tty_unlock(tty);
	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space. 
 */
static int get_lsr_info(struct serial_state *info, unsigned int __user *value)
{
	unsigned char status;
	unsigned int result;
	unsigned long flags;

	local_irq_save(flags);
	status = custom.serdatr;
	mb();
	local_irq_restore(flags);
	result = ((status & SDR_TSRE) ? TIOCSER_TEMT : 0);
	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}


static int rs_tiocmget(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;
	unsigned char control, status;
	unsigned long flags;

	if (tty_io_error(tty))
		return -EIO;

	control = info->MCR;
	local_irq_save(flags);
	status = ciab.pra;
	local_irq_restore(flags);
	return    ((control & SER_RTS) ? TIOCM_RTS : 0)
		| ((control & SER_DTR) ? TIOCM_DTR : 0)
		| (!(status  & SER_DCD) ? TIOCM_CAR : 0)
		| (!(status  & SER_DSR) ? TIOCM_DSR : 0)
		| (!(status  & SER_CTS) ? TIOCM_CTS : 0);
}

static int rs_tiocmset(struct tty_struct *tty, unsigned int set,
						unsigned int clear)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;

	if (tty_io_error(tty))
		return -EIO;

	local_irq_save(flags);
	if (set & TIOCM_RTS)
		info->MCR |= SER_RTS;
	if (set & TIOCM_DTR)
		info->MCR |= SER_DTR;
	if (clear & TIOCM_RTS)
		info->MCR &= ~SER_RTS;
	if (clear & TIOCM_DTR)
		info->MCR &= ~SER_DTR;
	rtsdtr_ctrl(info->MCR);
	local_irq_restore(flags);
	return 0;
}

/*
 * rs_break() --- routine which turns the break handling on or off
 */
static int rs_break(struct tty_struct *tty, int break_state)
{
	unsigned long flags;

	local_irq_save(flags);
	if (break_state == -1)
	  custom.adkcon = AC_SETCLR | AC_UARTBRK;
	else
	  custom.adkcon = AC_UARTBRK;
	mb();
	local_irq_restore(flags);
	return 0;
}

/*
 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
 * Return: write counters to the user passed counter struct
 * NB: both 1->0 and 0->1 transitions are counted except for
 *     RI where only 0->1 is counted.
 */
static int rs_get_icount(struct tty_struct *tty,
				struct serial_icounter_struct *icount)
{
	struct serial_state *info = tty->driver_data;
	struct async_icount cnow;
	unsigned long flags;

	local_irq_save(flags);
	cnow = info->icount;
	local_irq_restore(flags);
	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->frame = cnow.frame;
	icount->overrun = cnow.overrun;
	icount->parity = cnow.parity;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	return 0;
}

static int rs_ioctl(struct tty_struct *tty,
		    unsigned int cmd, unsigned long arg)
{
	struct serial_state *info = tty->driver_data;
	struct async_icount cprev, cnow;	/* kernel counter temps */
	void __user *argp = (void __user *)arg;
	unsigned long flags;
	DEFINE_WAIT(wait);
	int ret;

	if ((cmd != TIOCSERCONFIG) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty_io_error(tty))
		    return -EIO;
	}

	switch (cmd) {
		case TIOCSERCONFIG:
			return 0;

		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, argp);

		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
 		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		case TIOCMIWAIT:
			local_irq_save(flags);
			/* note the counters on entry */
			cprev = info->icount;
			local_irq_restore(flags);
			while (1) {
				prepare_to_wait(&info->tport.delta_msr_wait,
						&wait, TASK_INTERRUPTIBLE);
				local_irq_save(flags);
				cnow = info->icount; /* atomic copy */
				local_irq_restore(flags);
				if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr && 
				    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts) {
					ret = -EIO; /* no change => error */
					break;
				}
				if ( ((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
				     ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
				     ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
				     ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
					ret = 0;
					break;
				}
				schedule();
				/* see if a signal did it */
				if (signal_pending(current)) {
					ret = -ERESTARTSYS;
					break;
				}
				cprev = cnow;
			}
			finish_wait(&info->tport.delta_msr_wait, &wait);
			return ret;

		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct serial_state *info = tty->driver_data;
	unsigned long flags;
	unsigned int cflag = tty->termios.c_cflag;

	change_speed(tty, info, old_termios);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD)) {
		info->MCR &= ~(SER_DTR|SER_RTS);
		local_irq_save(flags);
		rtsdtr_ctrl(info->MCR);
		local_irq_restore(flags);
	}

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		info->MCR |= SER_DTR;
		if (!C_CRTSCTS(tty) || !tty_throttled(tty))
			info->MCR |= SER_RTS;
		local_irq_save(flags);
		rtsdtr_ctrl(info->MCR);
		local_irq_restore(flags);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) && !C_CRTSCTS(tty)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}

#if 0
	/*
	 * No need to wake up processes in open wait, since they
	 * sample the CLOCAL flag once, and don't recheck it.
	 * XXX  It's not clear whether the current behavior is correct
	 * or not.  Hence, this may change.....
	 */
	if (!(old_termios->c_cflag & CLOCAL) && C_CLOCAL(tty))
		wake_up_interruptible(&info->open_wait);
#endif
}

/*
 * ------------------------------------------------------------
 * rs_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct serial_state *state = tty->driver_data;
	struct tty_port *port = &state->tport;

	if (tty_port_close_start(port, tty, filp) == 0)
		return;

	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	state->read_status_mask &= ~UART_LSR_DR;
	if (tty_port_initialized(port)) {
	        /* disable receive interrupts */
	        custom.intena = IF_RBF;
		mb();
		/* clear any pending receive interrupt */
		custom.intreq = IF_RBF;
		mb();

		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		rs_wait_until_sent(tty, state->timeout);
	}
	shutdown(tty, state);
	rs_flush_buffer(tty);
		
	tty_ldisc_flush(tty);
	port->tty = NULL;

	tty_port_close_end(port, tty);
}

/*
 * rs_wait_until_sent() --- wait until the transmitter is empty
 */
static void rs_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct serial_state *info = tty->driver_data;
	unsigned long orig_jiffies, char_time;
	int lsr;

	if (info->xmit_fifo_size == 0)
		return; /* Just in case.... */

	orig_jiffies = jiffies;

	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 * 
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = (info->timeout - HZ/50) / info->xmit_fifo_size;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout)
	  char_time = min_t(unsigned long, char_time, timeout);
	/*
	 * If the transmitter hasn't cleared in twice the approximate
	 * amount of time to send the entire FIFO, it probably won't
	 * ever clear.  This assumes the UART isn't doing flow
	 * control, which is currently the case.  Hence, if it ever
	 * takes longer than info->timeout, this is probably due to a
	 * UART bug of some kind.  So, we clamp the timeout parameter at
	 * 2*info->timeout.
	 */
	if (!timeout || timeout > 2*info->timeout)
		timeout = 2*info->timeout;
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("In rs_wait_until_sent(%d) check=%lu...", timeout, char_time);
	printk("jiff=%lu...", jiffies);
#endif
	while(!((lsr = custom.serdatr) & SDR_TSRE)) {
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
		printk("serdatr = %d (jiff=%lu)...", lsr, jiffies);
#endif
		msleep_interruptible(jiffies_to_msecs(char_time));
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
	__set_current_state(TASK_RUNNING);

#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("lsr = %d (jiff=%lu)...done\n", lsr, jiffies);
#endif
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void rs_hangup(struct tty_struct *tty)
{
	struct serial_state *info = tty->driver_data;

	rs_flush_buffer(tty);
	shutdown(tty, info);
	info->tport.count = 0;
	tty_port_set_active(&info->tport, 0);
	info->tport.tty = NULL;
	wake_up_interruptible(&info->tport.open_wait);
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct serial_state *info = rs_table + tty->index;
	struct tty_port *port = &info->tport;
	int retval;

	port->count++;
	port->tty = tty;
	tty->driver_data = info;
	tty->port = port;

	retval = startup(tty, info);
	if (retval) {
		return retval;
	}

	return tty_port_block_til_ready(port, tty, filp);
}

/*
 * /proc fs routines....
 */

static inline void line_info(struct seq_file *m, int line,
		struct serial_state *state)
{
	char	stat_buf[30], control, status;
	unsigned long flags;

	seq_printf(m, "%d: uart:amiga_builtin", line);

	local_irq_save(flags);
	status = ciab.pra;
	control = tty_port_initialized(&state->tport) ? state->MCR : status;
	local_irq_restore(flags);

	stat_buf[0] = 0;
	stat_buf[1] = 0;
	if(!(control & SER_RTS))
		strcat(stat_buf, "|RTS");
	if(!(status & SER_CTS))
		strcat(stat_buf, "|CTS");
	if(!(control & SER_DTR))
		strcat(stat_buf, "|DTR");
	if(!(status & SER_DSR))
		strcat(stat_buf, "|DSR");
	if(!(status & SER_DCD))
		strcat(stat_buf, "|CD");

	if (state->quot)
		seq_printf(m, " baud:%d", state->baud_base / state->quot);

	seq_printf(m, " tx:%d rx:%d", state->icount.tx, state->icount.rx);

	if (state->icount.frame)
		seq_printf(m, " fe:%d", state->icount.frame);

	if (state->icount.parity)
		seq_printf(m, " pe:%d", state->icount.parity);

	if (state->icount.brk)
		seq_printf(m, " brk:%d", state->icount.brk);

	if (state->icount.overrun)
		seq_printf(m, " oe:%d", state->icount.overrun);

	/*
	 * Last thing is the RS-232 status lines
	 */
	seq_printf(m, " %s\n", stat_buf+1);
}

static int rs_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "serinfo:1.0 driver:%s\n", serial_version);
	line_info(m, 0, &rs_table[0]);
	return 0;
}

/*
 * ---------------------------------------------------------------------
 * rs_init() and friends
 *
 * rs_init() is called at boot-time to initialize the serial driver.
 * ---------------------------------------------------------------------
 */

/*
 * This routine prints out the appropriate serial driver version
 * number, and identifies which options were configured into this
 * driver.
 */
static void show_serial_version(void)
{
 	printk(KERN_INFO "%s version %s\n", serial_name, serial_version);
}


static const struct tty_operations serial_ops = {
	.open = rs_open,
	.close = rs_close,
	.write = rs_write,
	.put_char = rs_put_char,
	.flush_chars = rs_flush_chars,
	.write_room = rs_write_room,
	.chars_in_buffer = rs_chars_in_buffer,
	.flush_buffer = rs_flush_buffer,
	.ioctl = rs_ioctl,
	.throttle = rs_throttle,
	.unthrottle = rs_unthrottle,
	.set_termios = rs_set_termios,
	.stop = rs_stop,
	.start = rs_start,
	.hangup = rs_hangup,
	.break_ctl = rs_break,
	.send_xchar = rs_send_xchar,
	.wait_until_sent = rs_wait_until_sent,
	.tiocmget = rs_tiocmget,
	.tiocmset = rs_tiocmset,
	.get_icount = rs_get_icount,
	.set_serial = set_serial_info,
	.get_serial = get_serial_info,
	.proc_show = rs_proc_show,
};

static int amiga_carrier_raised(struct tty_port *port)
{
	return !(ciab.pra & SER_DCD);
}

static void amiga_dtr_rts(struct tty_port *port, int raise)
{
	struct serial_state *info = container_of(port, struct serial_state,
			tport);
	unsigned long flags;

	if (raise)
		info->MCR |= SER_DTR|SER_RTS;
	else
		info->MCR &= ~(SER_DTR|SER_RTS);

	local_irq_save(flags);
	rtsdtr_ctrl(info->MCR);
	local_irq_restore(flags);
}

static const struct tty_port_operations amiga_port_ops = {
	.carrier_raised = amiga_carrier_raised,
	.dtr_rts = amiga_dtr_rts,
};

/*
 * The serial driver boot-time initialization code!
 */
static int __init amiga_serial_probe(struct platform_device *pdev)
{
	unsigned long flags;
	struct serial_state * state;
	int error;

	serial_driver = alloc_tty_driver(NR_PORTS);
	if (!serial_driver)
		return -ENOMEM;

	show_serial_version();

	/* Initialize the tty_driver structure */

	serial_driver->driver_name = "amiserial";
	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(serial_driver, &serial_ops);

	state = rs_table;
	state->port = (int)&custom.serdatr; /* Just to give it a value */
	state->custom_divisor = 0;
	state->icount.cts = state->icount.dsr = 
	  state->icount.rng = state->icount.dcd = 0;
	state->icount.rx = state->icount.tx = 0;
	state->icount.frame = state->icount.parity = 0;
	state->icount.overrun = state->icount.brk = 0;
	tty_port_init(&state->tport);
	state->tport.ops = &amiga_port_ops;
	tty_port_link_device(&state->tport, serial_driver, 0);

	error = tty_register_driver(serial_driver);
	if (error)
		goto fail_put_tty_driver;

	printk(KERN_INFO "ttyS0 is the amiga builtin serial port\n");

	/* Hardware set up */

	state->baud_base = amiga_colorclock;
	state->xmit_fifo_size = 1;

	/* set ISRs, and then disable the rx interrupts */
	error = request_irq(IRQ_AMIGA_TBE, ser_tx_int, 0, "serial TX", state);
	if (error)
		goto fail_unregister;

	error = request_irq(IRQ_AMIGA_RBF, ser_rx_int, 0,
			    "serial RX", state);
	if (error)
		goto fail_free_irq;

	local_irq_save(flags);

	/* turn off Rx and Tx interrupts */
	custom.intena = IF_RBF | IF_TBE;
	mb();

	/* clear any pending interrupt */
	custom.intreq = IF_RBF | IF_TBE;
	mb();

	local_irq_restore(flags);

	/*
	 * set the appropriate directions for the modem control flags,
	 * and clear RTS and DTR
	 */
	ciab.ddra |= (SER_DTR | SER_RTS);   /* outputs */
	ciab.ddra &= ~(SER_DCD | SER_CTS | SER_DSR);  /* inputs */

	platform_set_drvdata(pdev, state);

	return 0;

fail_free_irq:
	free_irq(IRQ_AMIGA_TBE, state);
fail_unregister:
	tty_unregister_driver(serial_driver);
fail_put_tty_driver:
	tty_port_destroy(&state->tport);
	put_tty_driver(serial_driver);
	return error;
}

static int __exit amiga_serial_remove(struct platform_device *pdev)
{
	int error;
	struct serial_state *state = platform_get_drvdata(pdev);

	/* printk("Unloading %s: version %s\n", serial_name, serial_version); */
	error = tty_unregister_driver(serial_driver);
	if (error)
		printk("SERIAL: failed to unregister serial driver (%d)\n",
		       error);
	put_tty_driver(serial_driver);
	tty_port_destroy(&state->tport);

	free_irq(IRQ_AMIGA_TBE, state);
	free_irq(IRQ_AMIGA_RBF, state);

	return error;
}

static struct platform_driver amiga_serial_driver = {
	.remove = __exit_p(amiga_serial_remove),
	.driver   = {
		.name	= "amiga-serial",
	},
};

module_platform_driver_probe(amiga_serial_driver, amiga_serial_probe);


#if defined(CONFIG_SERIAL_CONSOLE) && !defined(MODULE)

/*
 * ------------------------------------------------------------
 * Serial console driver
 * ------------------------------------------------------------
 */

static void amiga_serial_putc(char c)
{
	custom.serdat = (unsigned char)c | 0x100;
	while (!(custom.serdatr & 0x2000))
		barrier();
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console must be locked when we get here.
 */
static void serial_console_write(struct console *co, const char *s,
				unsigned count)
{
	unsigned short intena = custom.intenar;

	custom.intena = IF_TBE;

	while (count--) {
		if (*s == '\n')
			amiga_serial_putc('\r');
		amiga_serial_putc(*s++);
	}

	custom.intena = IF_SETCLR | (intena & IF_TBE);
}

static struct tty_driver *serial_console_device(struct console *c, int *index)
{
	*index = 0;
	return serial_driver;
}

static struct console sercons = {
	.name =		"ttyS",
	.write =	serial_console_write,
	.device =	serial_console_device,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

/*
 *	Register console.
 */
static int __init amiserial_console_init(void)
{
	if (!MACH_IS_AMIGA)
		return -ENODEV;

	register_console(&sercons);
	return 0;
}
console_initcall(amiserial_console_init);

#endif /* CONFIG_SERIAL_CONSOLE && !MODULE */

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:amiga-serial");
