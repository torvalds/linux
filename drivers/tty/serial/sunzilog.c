/* sunzilog.c: Zilog serial driver for Sparc systems.
 *
 * Driver for Zilog serial chips found on Sun workstations and
 * servers.  This driver could actually be made more generic.
 *
 * This is based on the old drivers/sbus/char/zs.c code.  A lot
 * of code has been simply moved over directly from there but
 * much has been rewritten.  Credits therefore go out to Eddie
 * C. Dost, Pete Zaitcev, Ted Ts'o and Alex Buell for their
 * work there.
 *
 * Copyright (C) 2002, 2006, 2007 David S. Miller (davem@davemloft.net)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/circ_buf.h>
#include <linux/serial.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/spinlock.h>
#ifdef CONFIG_SERIO
#include <linux/serio.h>
#endif
#include <linux/init.h>
#include <linux/of_device.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/setup.h>

#if defined(CONFIG_SERIAL_SUNZILOG_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>
#include <linux/sunserialcore.h>

#include "sunzilog.h"

/* On 32-bit sparcs we need to delay after register accesses
 * to accommodate sun4 systems, but we do not need to flush writes.
 * On 64-bit sparc we only need to flush single writes to ensure
 * completion.
 */
#ifndef CONFIG_SPARC64
#define ZSDELAY()		udelay(5)
#define ZSDELAY_LONG()		udelay(20)
#define ZS_WSYNC(channel)	do { } while (0)
#else
#define ZSDELAY()
#define ZSDELAY_LONG()
#define ZS_WSYNC(__channel) \
	readb(&((__channel)->control))
#endif

#define ZS_CLOCK		4915200 /* Zilog input clock rate. */
#define ZS_CLOCK_DIVISOR	16      /* Divisor this driver uses. */

/*
 * We wrap our port structure around the generic uart_port.
 */
struct uart_sunzilog_port {
	struct uart_port		port;

	/* IRQ servicing chain.  */
	struct uart_sunzilog_port	*next;

	/* Current values of Zilog write registers.  */
	unsigned char			curregs[NUM_ZSREGS];

	unsigned int			flags;
#define SUNZILOG_FLAG_CONS_KEYB		0x00000001
#define SUNZILOG_FLAG_CONS_MOUSE	0x00000002
#define SUNZILOG_FLAG_IS_CONS		0x00000004
#define SUNZILOG_FLAG_IS_KGDB		0x00000008
#define SUNZILOG_FLAG_MODEM_STATUS	0x00000010
#define SUNZILOG_FLAG_IS_CHANNEL_A	0x00000020
#define SUNZILOG_FLAG_REGS_HELD		0x00000040
#define SUNZILOG_FLAG_TX_STOPPED	0x00000080
#define SUNZILOG_FLAG_TX_ACTIVE		0x00000100
#define SUNZILOG_FLAG_ESCC		0x00000200
#define SUNZILOG_FLAG_ISR_HANDLER	0x00000400

	unsigned int cflag;

	unsigned char			parity_mask;
	unsigned char			prev_status;

#ifdef CONFIG_SERIO
	struct serio			serio;
	int				serio_open;
#endif
};

static void sunzilog_putchar(struct uart_port *port, int ch);

#define ZILOG_CHANNEL_FROM_PORT(PORT)	((struct zilog_channel __iomem *)((PORT)->membase))
#define UART_ZILOG(PORT)		((struct uart_sunzilog_port *)(PORT))

#define ZS_IS_KEYB(UP)	((UP)->flags & SUNZILOG_FLAG_CONS_KEYB)
#define ZS_IS_MOUSE(UP)	((UP)->flags & SUNZILOG_FLAG_CONS_MOUSE)
#define ZS_IS_CONS(UP)	((UP)->flags & SUNZILOG_FLAG_IS_CONS)
#define ZS_IS_KGDB(UP)	((UP)->flags & SUNZILOG_FLAG_IS_KGDB)
#define ZS_WANTS_MODEM_STATUS(UP)	((UP)->flags & SUNZILOG_FLAG_MODEM_STATUS)
#define ZS_IS_CHANNEL_A(UP)	((UP)->flags & SUNZILOG_FLAG_IS_CHANNEL_A)
#define ZS_REGS_HELD(UP)	((UP)->flags & SUNZILOG_FLAG_REGS_HELD)
#define ZS_TX_STOPPED(UP)	((UP)->flags & SUNZILOG_FLAG_TX_STOPPED)
#define ZS_TX_ACTIVE(UP)	((UP)->flags & SUNZILOG_FLAG_TX_ACTIVE)

/* Reading and writing Zilog8530 registers.  The delays are to make this
 * driver work on the Sun4 which needs a settling delay after each chip
 * register access, other machines handle this in hardware via auxiliary
 * flip-flops which implement the settle time we do in software.
 *
 * The port lock must be held and local IRQs must be disabled
 * when {read,write}_zsreg is invoked.
 */
static unsigned char read_zsreg(struct zilog_channel __iomem *channel,
				unsigned char reg)
{
	unsigned char retval;

	writeb(reg, &channel->control);
	ZSDELAY();
	retval = readb(&channel->control);
	ZSDELAY();

	return retval;
}

static void write_zsreg(struct zilog_channel __iomem *channel,
			unsigned char reg, unsigned char value)
{
	writeb(reg, &channel->control);
	ZSDELAY();
	writeb(value, &channel->control);
	ZSDELAY();
}

static void sunzilog_clear_fifo(struct zilog_channel __iomem *channel)
{
	int i;

	for (i = 0; i < 32; i++) {
		unsigned char regval;

		regval = readb(&channel->control);
		ZSDELAY();
		if (regval & Rx_CH_AV)
			break;

		regval = read_zsreg(channel, R1);
		readb(&channel->data);
		ZSDELAY();

		if (regval & (PAR_ERR | Rx_OVR | CRC_ERR)) {
			writeb(ERR_RES, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);
		}
	}
}

/* This function must only be called when the TX is not busy.  The UART
 * port lock must be held and local interrupts disabled.
 */
static int __load_zsregs(struct zilog_channel __iomem *channel, unsigned char *regs)
{
	int i;
	int escc;
	unsigned char r15;

	/* Let pending transmits finish.  */
	for (i = 0; i < 1000; i++) {
		unsigned char stat = read_zsreg(channel, R1);
		if (stat & ALL_SNT)
			break;
		udelay(100);
	}

	writeb(ERR_RES, &channel->control);
	ZSDELAY();
	ZS_WSYNC(channel);

	sunzilog_clear_fifo(channel);

	/* Disable all interrupts.  */
	write_zsreg(channel, R1,
		    regs[R1] & ~(RxINT_MASK | TxINT_ENAB | EXT_INT_ENAB));

	/* Set parity, sync config, stop bits, and clock divisor.  */
	write_zsreg(channel, R4, regs[R4]);

	/* Set misc. TX/RX control bits.  */
	write_zsreg(channel, R10, regs[R10]);

	/* Set TX/RX controls sans the enable bits.  */
	write_zsreg(channel, R3, regs[R3] & ~RxENAB);
	write_zsreg(channel, R5, regs[R5] & ~TxENAB);

	/* Synchronous mode config.  */
	write_zsreg(channel, R6, regs[R6]);
	write_zsreg(channel, R7, regs[R7]);

	/* Don't mess with the interrupt vector (R2, unused by us) and
	 * master interrupt control (R9).  We make sure this is setup
	 * properly at probe time then never touch it again.
	 */

	/* Disable baud generator.  */
	write_zsreg(channel, R14, regs[R14] & ~BRENAB);

	/* Clock mode control.  */
	write_zsreg(channel, R11, regs[R11]);

	/* Lower and upper byte of baud rate generator divisor.  */
	write_zsreg(channel, R12, regs[R12]);
	write_zsreg(channel, R13, regs[R13]);
	
	/* Now rewrite R14, with BRENAB (if set).  */
	write_zsreg(channel, R14, regs[R14]);

	/* External status interrupt control.  */
	write_zsreg(channel, R15, (regs[R15] | WR7pEN) & ~FIFOEN);

	/* ESCC Extension Register */
	r15 = read_zsreg(channel, R15);
	if (r15 & 0x01)	{
		write_zsreg(channel, R7,  regs[R7p]);

		/* External status interrupt and FIFO control.  */
		write_zsreg(channel, R15, regs[R15] & ~WR7pEN);
		escc = 1;
	} else {
		 /* Clear FIFO bit case it is an issue */
		regs[R15] &= ~FIFOEN;
		escc = 0;
	}

	/* Reset external status interrupts.  */
	write_zsreg(channel, R0, RES_EXT_INT); /* First Latch  */
	write_zsreg(channel, R0, RES_EXT_INT); /* Second Latch */

	/* Rewrite R3/R5, this time without enables masked.  */
	write_zsreg(channel, R3, regs[R3]);
	write_zsreg(channel, R5, regs[R5]);

	/* Rewrite R1, this time without IRQ enabled masked.  */
	write_zsreg(channel, R1, regs[R1]);

	return escc;
}

/* Reprogram the Zilog channel HW registers with the copies found in the
 * software state struct.  If the transmitter is busy, we defer this update
 * until the next TX complete interrupt.  Else, we do it right now.
 *
 * The UART port lock must be held and local interrupts disabled.
 */
static void sunzilog_maybe_update_regs(struct uart_sunzilog_port *up,
				       struct zilog_channel __iomem *channel)
{
	if (!ZS_REGS_HELD(up)) {
		if (ZS_TX_ACTIVE(up)) {
			up->flags |= SUNZILOG_FLAG_REGS_HELD;
		} else {
			__load_zsregs(channel, up->curregs);
		}
	}
}

static void sunzilog_change_mouse_baud(struct uart_sunzilog_port *up)
{
	unsigned int cur_cflag = up->cflag;
	int brg, new_baud;

	up->cflag &= ~CBAUD;
	up->cflag |= suncore_mouse_baud_cflag_next(cur_cflag, &new_baud);

	brg = BPS_TO_BRG(new_baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);
	up->curregs[R12] = (brg & 0xff);
	up->curregs[R13] = (brg >> 8) & 0xff;
	sunzilog_maybe_update_regs(up, ZILOG_CHANNEL_FROM_PORT(&up->port));
}

static void sunzilog_kbdms_receive_chars(struct uart_sunzilog_port *up,
					 unsigned char ch, int is_break)
{
	if (ZS_IS_KEYB(up)) {
		/* Stop-A is handled by drivers/char/keyboard.c now. */
#ifdef CONFIG_SERIO
		if (up->serio_open)
			serio_interrupt(&up->serio, ch, 0);
#endif
	} else if (ZS_IS_MOUSE(up)) {
		int ret = suncore_mouse_baud_detection(ch, is_break);

		switch (ret) {
		case 2:
			sunzilog_change_mouse_baud(up);
			/* fallthru */
		case 1:
			break;

		case 0:
#ifdef CONFIG_SERIO
			if (up->serio_open)
				serio_interrupt(&up->serio, ch, 0);
#endif
			break;
		};
	}
}

static struct tty_struct *
sunzilog_receive_chars(struct uart_sunzilog_port *up,
		       struct zilog_channel __iomem *channel)
{
	struct tty_struct *tty;
	unsigned char ch, r1, flag;

	tty = NULL;
	if (up->port.state != NULL &&		/* Unopened serial console */
	    up->port.state->port.tty != NULL)	/* Keyboard || mouse */
		tty = up->port.state->port.tty;

	for (;;) {

		r1 = read_zsreg(channel, R1);
		if (r1 & (PAR_ERR | Rx_OVR | CRC_ERR)) {
			writeb(ERR_RES, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);
		}

		ch = readb(&channel->control);
		ZSDELAY();

		/* This funny hack depends upon BRK_ABRT not interfering
		 * with the other bits we care about in R1.
		 */
		if (ch & BRK_ABRT)
			r1 |= BRK_ABRT;

		if (!(ch & Rx_CH_AV))
			break;

		ch = readb(&channel->data);
		ZSDELAY();

		ch &= up->parity_mask;

		if (unlikely(ZS_IS_KEYB(up)) || unlikely(ZS_IS_MOUSE(up))) {
			sunzilog_kbdms_receive_chars(up, ch, 0);
			continue;
		}

		if (tty == NULL) {
			uart_handle_sysrq_char(&up->port, ch);
			continue;
		}

		/* A real serial line, record the character and status.  */
		flag = TTY_NORMAL;
		up->port.icount.rx++;
		if (r1 & (BRK_ABRT | PAR_ERR | Rx_OVR | CRC_ERR)) {
			if (r1 & BRK_ABRT) {
				r1 &= ~(PAR_ERR | CRC_ERR);
				up->port.icount.brk++;
				if (uart_handle_break(&up->port))
					continue;
			}
			else if (r1 & PAR_ERR)
				up->port.icount.parity++;
			else if (r1 & CRC_ERR)
				up->port.icount.frame++;
			if (r1 & Rx_OVR)
				up->port.icount.overrun++;
			r1 &= up->port.read_status_mask;
			if (r1 & BRK_ABRT)
				flag = TTY_BREAK;
			else if (r1 & PAR_ERR)
				flag = TTY_PARITY;
			else if (r1 & CRC_ERR)
				flag = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&up->port, ch))
			continue;

		if (up->port.ignore_status_mask == 0xff ||
		    (r1 & up->port.ignore_status_mask) == 0) {
		    	tty_insert_flip_char(tty, ch, flag);
		}
		if (r1 & Rx_OVR)
			tty_insert_flip_char(tty, 0, TTY_OVERRUN);
	}

	return tty;
}

static void sunzilog_status_handle(struct uart_sunzilog_port *up,
				   struct zilog_channel __iomem *channel)
{
	unsigned char status;

	status = readb(&channel->control);
	ZSDELAY();

	writeb(RES_EXT_INT, &channel->control);
	ZSDELAY();
	ZS_WSYNC(channel);

	if (status & BRK_ABRT) {
		if (ZS_IS_MOUSE(up))
			sunzilog_kbdms_receive_chars(up, 0, 1);
		if (ZS_IS_CONS(up)) {
			/* Wait for BREAK to deassert to avoid potentially
			 * confusing the PROM.
			 */
			while (1) {
				status = readb(&channel->control);
				ZSDELAY();
				if (!(status & BRK_ABRT))
					break;
			}
			sun_do_break();
			return;
		}
	}

	if (ZS_WANTS_MODEM_STATUS(up)) {
		if (status & SYNC)
			up->port.icount.dsr++;

		/* The Zilog just gives us an interrupt when DCD/CTS/etc. change.
		 * But it does not tell us which bit has changed, we have to keep
		 * track of this ourselves.
		 */
		if ((status ^ up->prev_status) ^ DCD)
			uart_handle_dcd_change(&up->port,
					       (status & DCD));
		if ((status ^ up->prev_status) ^ CTS)
			uart_handle_cts_change(&up->port,
					       (status & CTS));

		wake_up_interruptible(&up->port.state->port.delta_msr_wait);
	}

	up->prev_status = status;
}

static void sunzilog_transmit_chars(struct uart_sunzilog_port *up,
				    struct zilog_channel __iomem *channel)
{
	struct circ_buf *xmit;

	if (ZS_IS_CONS(up)) {
		unsigned char status = readb(&channel->control);
		ZSDELAY();

		/* TX still busy?  Just wait for the next TX done interrupt.
		 *
		 * It can occur because of how we do serial console writes.  It would
		 * be nice to transmit console writes just like we normally would for
		 * a TTY line. (ie. buffered and TX interrupt driven).  That is not
		 * easy because console writes cannot sleep.  One solution might be
		 * to poll on enough port->xmit space becoming free.  -DaveM
		 */
		if (!(status & Tx_BUF_EMP))
			return;
	}

	up->flags &= ~SUNZILOG_FLAG_TX_ACTIVE;

	if (ZS_REGS_HELD(up)) {
		__load_zsregs(channel, up->curregs);
		up->flags &= ~SUNZILOG_FLAG_REGS_HELD;
	}

	if (ZS_TX_STOPPED(up)) {
		up->flags &= ~SUNZILOG_FLAG_TX_STOPPED;
		goto ack_tx_int;
	}

	if (up->port.x_char) {
		up->flags |= SUNZILOG_FLAG_TX_ACTIVE;
		writeb(up->port.x_char, &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}

	if (up->port.state == NULL)
		goto ack_tx_int;
	xmit = &up->port.state->xmit;
	if (uart_circ_empty(xmit))
		goto ack_tx_int;

	if (uart_tx_stopped(&up->port))
		goto ack_tx_int;

	up->flags |= SUNZILOG_FLAG_TX_ACTIVE;
	writeb(xmit->buf[xmit->tail], &channel->data);
	ZSDELAY();
	ZS_WSYNC(channel);

	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	up->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	return;

ack_tx_int:
	writeb(RES_Tx_P, &channel->control);
	ZSDELAY();
	ZS_WSYNC(channel);
}

static irqreturn_t sunzilog_interrupt(int irq, void *dev_id)
{
	struct uart_sunzilog_port *up = dev_id;

	while (up) {
		struct zilog_channel __iomem *channel
			= ZILOG_CHANNEL_FROM_PORT(&up->port);
		struct tty_struct *tty;
		unsigned char r3;

		spin_lock(&up->port.lock);
		r3 = read_zsreg(channel, R3);

		/* Channel A */
		tty = NULL;
		if (r3 & (CHAEXT | CHATxIP | CHARxIP)) {
			writeb(RES_H_IUS, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);

			if (r3 & CHARxIP)
				tty = sunzilog_receive_chars(up, channel);
			if (r3 & CHAEXT)
				sunzilog_status_handle(up, channel);
			if (r3 & CHATxIP)
				sunzilog_transmit_chars(up, channel);
		}
		spin_unlock(&up->port.lock);

		if (tty)
			tty_flip_buffer_push(tty);

		/* Channel B */
		up = up->next;
		channel = ZILOG_CHANNEL_FROM_PORT(&up->port);

		spin_lock(&up->port.lock);
		tty = NULL;
		if (r3 & (CHBEXT | CHBTxIP | CHBRxIP)) {
			writeb(RES_H_IUS, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);

			if (r3 & CHBRxIP)
				tty = sunzilog_receive_chars(up, channel);
			if (r3 & CHBEXT)
				sunzilog_status_handle(up, channel);
			if (r3 & CHBTxIP)
				sunzilog_transmit_chars(up, channel);
		}
		spin_unlock(&up->port.lock);

		if (tty)
			tty_flip_buffer_push(tty);

		up = up->next;
	}

	return IRQ_HANDLED;
}

/* A convenient way to quickly get R0 status.  The caller must _not_ hold the
 * port lock, it is acquired here.
 */
static __inline__ unsigned char sunzilog_read_channel_status(struct uart_port *port)
{
	struct zilog_channel __iomem *channel;
	unsigned char status;

	channel = ZILOG_CHANNEL_FROM_PORT(port);
	status = readb(&channel->control);
	ZSDELAY();

	return status;
}

/* The port lock is not held.  */
static unsigned int sunzilog_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	unsigned char status;
	unsigned int ret;

	spin_lock_irqsave(&port->lock, flags);

	status = sunzilog_read_channel_status(port);

	spin_unlock_irqrestore(&port->lock, flags);

	if (status & Tx_BUF_EMP)
		ret = TIOCSER_TEMT;
	else
		ret = 0;

	return ret;
}

/* The port lock is held and interrupts are disabled.  */
static unsigned int sunzilog_get_mctrl(struct uart_port *port)
{
	unsigned char status;
	unsigned int ret;

	status = sunzilog_read_channel_status(port);

	ret = 0;
	if (status & DCD)
		ret |= TIOCM_CAR;
	if (status & SYNC)
		ret |= TIOCM_DSR;
	if (status & CTS)
		ret |= TIOCM_CTS;

	return ret;
}

/* The port lock is held and interrupts are disabled.  */
static void sunzilog_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel __iomem *channel = ZILOG_CHANNEL_FROM_PORT(port);
	unsigned char set_bits, clear_bits;

	set_bits = clear_bits = 0;

	if (mctrl & TIOCM_RTS)
		set_bits |= RTS;
	else
		clear_bits |= RTS;
	if (mctrl & TIOCM_DTR)
		set_bits |= DTR;
	else
		clear_bits |= DTR;

	/* NOTE: Not subject to 'transmitter active' rule.  */ 
	up->curregs[R5] |= set_bits;
	up->curregs[R5] &= ~clear_bits;
	write_zsreg(channel, R5, up->curregs[R5]);
}

/* The port lock is held and interrupts are disabled.  */
static void sunzilog_stop_tx(struct uart_port *port)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;

	up->flags |= SUNZILOG_FLAG_TX_STOPPED;
}

/* The port lock is held and interrupts are disabled.  */
static void sunzilog_start_tx(struct uart_port *port)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel __iomem *channel = ZILOG_CHANNEL_FROM_PORT(port);
	unsigned char status;

	up->flags |= SUNZILOG_FLAG_TX_ACTIVE;
	up->flags &= ~SUNZILOG_FLAG_TX_STOPPED;

	status = readb(&channel->control);
	ZSDELAY();

	/* TX busy?  Just wait for the TX done interrupt.  */
	if (!(status & Tx_BUF_EMP))
		return;

	/* Send the first character to jump-start the TX done
	 * IRQ sending engine.
	 */
	if (port->x_char) {
		writeb(port->x_char, &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		port->icount.tx++;
		port->x_char = 0;
	} else {
		struct circ_buf *xmit = &port->state->xmit;

		writeb(xmit->buf[xmit->tail], &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_write_wakeup(&up->port);
	}
}

/* The port lock is held.  */
static void sunzilog_stop_rx(struct uart_port *port)
{
	struct uart_sunzilog_port *up = UART_ZILOG(port);
	struct zilog_channel __iomem *channel;

	if (ZS_IS_CONS(up))
		return;

	channel = ZILOG_CHANNEL_FROM_PORT(port);

	/* Disable all RX interrupts.  */
	up->curregs[R1] &= ~RxINT_MASK;
	sunzilog_maybe_update_regs(up, channel);
}

/* The port lock is held.  */
static void sunzilog_enable_ms(struct uart_port *port)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel __iomem *channel = ZILOG_CHANNEL_FROM_PORT(port);
	unsigned char new_reg;

	new_reg = up->curregs[R15] | (DCDIE | SYNCIE | CTSIE);
	if (new_reg != up->curregs[R15]) {
		up->curregs[R15] = new_reg;

		/* NOTE: Not subject to 'transmitter active' rule.  */ 
		write_zsreg(channel, R15, up->curregs[R15] & ~WR7pEN);
	}
}

/* The port lock is not held.  */
static void sunzilog_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel __iomem *channel = ZILOG_CHANNEL_FROM_PORT(port);
	unsigned char set_bits, clear_bits, new_reg;
	unsigned long flags;

	set_bits = clear_bits = 0;

	if (break_state)
		set_bits |= SND_BRK;
	else
		clear_bits |= SND_BRK;

	spin_lock_irqsave(&port->lock, flags);

	new_reg = (up->curregs[R5] | set_bits) & ~clear_bits;
	if (new_reg != up->curregs[R5]) {
		up->curregs[R5] = new_reg;

		/* NOTE: Not subject to 'transmitter active' rule.  */ 
		write_zsreg(channel, R5, up->curregs[R5]);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static void __sunzilog_startup(struct uart_sunzilog_port *up)
{
	struct zilog_channel __iomem *channel;

	channel = ZILOG_CHANNEL_FROM_PORT(&up->port);
	up->prev_status = readb(&channel->control);

	/* Enable receiver and transmitter.  */
	up->curregs[R3] |= RxENAB;
	up->curregs[R5] |= TxENAB;

	up->curregs[R1] |= EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB;
	sunzilog_maybe_update_regs(up, channel);
}

static int sunzilog_startup(struct uart_port *port)
{
	struct uart_sunzilog_port *up = UART_ZILOG(port);
	unsigned long flags;

	if (ZS_IS_CONS(up))
		return 0;

	spin_lock_irqsave(&port->lock, flags);
	__sunzilog_startup(up);
	spin_unlock_irqrestore(&port->lock, flags);
	return 0;
}

/*
 * The test for ZS_IS_CONS is explained by the following e-mail:
 *****
 * From: Russell King <rmk@arm.linux.org.uk>
 * Date: Sun, 8 Dec 2002 10:18:38 +0000
 *
 * On Sun, Dec 08, 2002 at 02:43:36AM -0500, Pete Zaitcev wrote:
 * > I boot my 2.5 boxes using "console=ttyS0,9600" argument,
 * > and I noticed that something is not right with reference
 * > counting in this case. It seems that when the console
 * > is open by kernel initially, this is not accounted
 * > as an open, and uart_startup is not called.
 *
 * That is correct.  We are unable to call uart_startup when the serial
 * console is initialised because it may need to allocate memory (as
 * request_irq does) and the memory allocators may not have been
 * initialised.
 *
 * 1. initialise the port into a state where it can send characters in the
 *    console write method.
 *
 * 2. don't do the actual hardware shutdown in your shutdown() method (but
 *    do the normal software shutdown - ie, free irqs etc)
 *****
 */
static void sunzilog_shutdown(struct uart_port *port)
{
	struct uart_sunzilog_port *up = UART_ZILOG(port);
	struct zilog_channel __iomem *channel;
	unsigned long flags;

	if (ZS_IS_CONS(up))
		return;

	spin_lock_irqsave(&port->lock, flags);

	channel = ZILOG_CHANNEL_FROM_PORT(port);

	/* Disable receiver and transmitter.  */
	up->curregs[R3] &= ~RxENAB;
	up->curregs[R5] &= ~TxENAB;

	/* Disable all interrupts and BRK assertion.  */
	up->curregs[R1] &= ~(EXT_INT_ENAB | TxINT_ENAB | RxINT_MASK);
	up->curregs[R5] &= ~SND_BRK;
	sunzilog_maybe_update_regs(up, channel);

	spin_unlock_irqrestore(&port->lock, flags);
}

/* Shared by TTY driver and serial console setup.  The port lock is held
 * and local interrupts are disabled.
 */
static void
sunzilog_convert_to_zs(struct uart_sunzilog_port *up, unsigned int cflag,
		       unsigned int iflag, int brg)
{

	up->curregs[R10] = NRZ;
	up->curregs[R11] = TCBR | RCBR;

	/* Program BAUD and clock source. */
	up->curregs[R4] &= ~XCLK_MASK;
	up->curregs[R4] |= X16CLK;
	up->curregs[R12] = brg & 0xff;
	up->curregs[R13] = (brg >> 8) & 0xff;
	up->curregs[R14] = BRSRC | BRENAB;

	/* Character size, stop bits, and parity. */
	up->curregs[R3] &= ~RxN_MASK;
	up->curregs[R5] &= ~TxN_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		up->curregs[R3] |= Rx5;
		up->curregs[R5] |= Tx5;
		up->parity_mask = 0x1f;
		break;
	case CS6:
		up->curregs[R3] |= Rx6;
		up->curregs[R5] |= Tx6;
		up->parity_mask = 0x3f;
		break;
	case CS7:
		up->curregs[R3] |= Rx7;
		up->curregs[R5] |= Tx7;
		up->parity_mask = 0x7f;
		break;
	case CS8:
	default:
		up->curregs[R3] |= Rx8;
		up->curregs[R5] |= Tx8;
		up->parity_mask = 0xff;
		break;
	};
	up->curregs[R4] &= ~0x0c;
	if (cflag & CSTOPB)
		up->curregs[R4] |= SB2;
	else
		up->curregs[R4] |= SB1;
	if (cflag & PARENB)
		up->curregs[R4] |= PAR_ENAB;
	else
		up->curregs[R4] &= ~PAR_ENAB;
	if (!(cflag & PARODD))
		up->curregs[R4] |= PAR_EVEN;
	else
		up->curregs[R4] &= ~PAR_EVEN;

	up->port.read_status_mask = Rx_OVR;
	if (iflag & INPCK)
		up->port.read_status_mask |= CRC_ERR | PAR_ERR;
	if (iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= BRK_ABRT;

	up->port.ignore_status_mask = 0;
	if (iflag & IGNPAR)
		up->port.ignore_status_mask |= CRC_ERR | PAR_ERR;
	if (iflag & IGNBRK) {
		up->port.ignore_status_mask |= BRK_ABRT;
		if (iflag & IGNPAR)
			up->port.ignore_status_mask |= Rx_OVR;
	}

	if ((cflag & CREAD) == 0)
		up->port.ignore_status_mask = 0xff;
}

/* The port lock is not held.  */
static void
sunzilog_set_termios(struct uart_port *port, struct ktermios *termios,
		     struct ktermios *old)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	unsigned long flags;
	int baud, brg;

	baud = uart_get_baud_rate(port, termios, old, 1200, 76800);

	spin_lock_irqsave(&up->port.lock, flags);

	brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);

	sunzilog_convert_to_zs(up, termios->c_cflag, termios->c_iflag, brg);

	if (UART_ENABLE_MS(&up->port, termios->c_cflag))
		up->flags |= SUNZILOG_FLAG_MODEM_STATUS;
	else
		up->flags &= ~SUNZILOG_FLAG_MODEM_STATUS;

	up->cflag = termios->c_cflag;

	sunzilog_maybe_update_regs(up, ZILOG_CHANNEL_FROM_PORT(port));

	uart_update_timeout(port, termios->c_cflag, baud);

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static const char *sunzilog_type(struct uart_port *port)
{
	struct uart_sunzilog_port *up = UART_ZILOG(port);

	return (up->flags & SUNZILOG_FLAG_ESCC) ? "zs (ESCC)" : "zs";
}

/* We do not request/release mappings of the registers here, this
 * happens at early serial probe time.
 */
static void sunzilog_release_port(struct uart_port *port)
{
}

static int sunzilog_request_port(struct uart_port *port)
{
	return 0;
}

/* These do not need to do anything interesting either.  */
static void sunzilog_config_port(struct uart_port *port, int flags)
{
}

/* We do not support letting the user mess with the divisor, IRQ, etc. */
static int sunzilog_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return -EINVAL;
}

#ifdef CONFIG_CONSOLE_POLL
static int sunzilog_get_poll_char(struct uart_port *port)
{
	unsigned char ch, r1;
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel __iomem *channel
		= ZILOG_CHANNEL_FROM_PORT(&up->port);


	r1 = read_zsreg(channel, R1);
	if (r1 & (PAR_ERR | Rx_OVR | CRC_ERR)) {
		writeb(ERR_RES, &channel->control);
		ZSDELAY();
		ZS_WSYNC(channel);
	}

	ch = readb(&channel->control);
	ZSDELAY();

	/* This funny hack depends upon BRK_ABRT not interfering
	 * with the other bits we care about in R1.
	 */
	if (ch & BRK_ABRT)
		r1 |= BRK_ABRT;

	if (!(ch & Rx_CH_AV))
		return NO_POLL_CHAR;

	ch = readb(&channel->data);
	ZSDELAY();

	ch &= up->parity_mask;
	return ch;
}

static void sunzilog_put_poll_char(struct uart_port *port,
			unsigned char ch)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *)port;

	sunzilog_putchar(&up->port, ch);
}
#endif /* CONFIG_CONSOLE_POLL */

static struct uart_ops sunzilog_pops = {
	.tx_empty	=	sunzilog_tx_empty,
	.set_mctrl	=	sunzilog_set_mctrl,
	.get_mctrl	=	sunzilog_get_mctrl,
	.stop_tx	=	sunzilog_stop_tx,
	.start_tx	=	sunzilog_start_tx,
	.stop_rx	=	sunzilog_stop_rx,
	.enable_ms	=	sunzilog_enable_ms,
	.break_ctl	=	sunzilog_break_ctl,
	.startup	=	sunzilog_startup,
	.shutdown	=	sunzilog_shutdown,
	.set_termios	=	sunzilog_set_termios,
	.type		=	sunzilog_type,
	.release_port	=	sunzilog_release_port,
	.request_port	=	sunzilog_request_port,
	.config_port	=	sunzilog_config_port,
	.verify_port	=	sunzilog_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	=	sunzilog_get_poll_char,
	.poll_put_char	=	sunzilog_put_poll_char,
#endif
};

static int uart_chip_count;
static struct uart_sunzilog_port *sunzilog_port_table;
static struct zilog_layout __iomem **sunzilog_chip_regs;

static struct uart_sunzilog_port *sunzilog_irq_chain;

static struct uart_driver sunzilog_reg = {
	.owner		=	THIS_MODULE,
	.driver_name	=	"sunzilog",
	.dev_name	=	"ttyS",
	.major		=	TTY_MAJOR,
};

static int __init sunzilog_alloc_tables(int num_sunzilog)
{
	struct uart_sunzilog_port *up;
	unsigned long size;
	int num_channels = num_sunzilog * 2;
	int i;

	size = num_channels * sizeof(struct uart_sunzilog_port);
	sunzilog_port_table = kzalloc(size, GFP_KERNEL);
	if (!sunzilog_port_table)
		return -ENOMEM;

	for (i = 0; i < num_channels; i++) {
		up = &sunzilog_port_table[i];

		spin_lock_init(&up->port.lock);

		if (i == 0)
			sunzilog_irq_chain = up;

		if (i < num_channels - 1)
			up->next = up + 1;
		else
			up->next = NULL;
	}

	size = num_sunzilog * sizeof(struct zilog_layout __iomem *);
	sunzilog_chip_regs = kzalloc(size, GFP_KERNEL);
	if (!sunzilog_chip_regs) {
		kfree(sunzilog_port_table);
		sunzilog_irq_chain = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void sunzilog_free_tables(void)
{
	kfree(sunzilog_port_table);
	sunzilog_irq_chain = NULL;
	kfree(sunzilog_chip_regs);
}

#define ZS_PUT_CHAR_MAX_DELAY	2000	/* 10 ms */

static void sunzilog_putchar(struct uart_port *port, int ch)
{
	struct zilog_channel __iomem *channel = ZILOG_CHANNEL_FROM_PORT(port);
	int loops = ZS_PUT_CHAR_MAX_DELAY;

	/* This is a timed polling loop so do not switch the explicit
	 * udelay with ZSDELAY as that is a NOP on some platforms.  -DaveM
	 */
	do {
		unsigned char val = readb(&channel->control);
		if (val & Tx_BUF_EMP) {
			ZSDELAY();
			break;
		}
		udelay(5);
	} while (--loops);

	writeb(ch, &channel->data);
	ZSDELAY();
	ZS_WSYNC(channel);
}

#ifdef CONFIG_SERIO

static DEFINE_SPINLOCK(sunzilog_serio_lock);

static int sunzilog_serio_write(struct serio *serio, unsigned char ch)
{
	struct uart_sunzilog_port *up = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave(&sunzilog_serio_lock, flags);

	sunzilog_putchar(&up->port, ch);

	spin_unlock_irqrestore(&sunzilog_serio_lock, flags);

	return 0;
}

static int sunzilog_serio_open(struct serio *serio)
{
	struct uart_sunzilog_port *up = serio->port_data;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sunzilog_serio_lock, flags);
	if (!up->serio_open) {
		up->serio_open = 1;
		ret = 0;
	} else
		ret = -EBUSY;
	spin_unlock_irqrestore(&sunzilog_serio_lock, flags);

	return ret;
}

static void sunzilog_serio_close(struct serio *serio)
{
	struct uart_sunzilog_port *up = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave(&sunzilog_serio_lock, flags);
	up->serio_open = 0;
	spin_unlock_irqrestore(&sunzilog_serio_lock, flags);
}

#endif /* CONFIG_SERIO */

#ifdef CONFIG_SERIAL_SUNZILOG_CONSOLE
static void
sunzilog_console_write(struct console *con, const char *s, unsigned int count)
{
	struct uart_sunzilog_port *up = &sunzilog_port_table[con->index];
	unsigned long flags;
	int locked = 1;

	local_irq_save(flags);
	if (up->port.sysrq) {
		locked = 0;
	} else if (oops_in_progress) {
		locked = spin_trylock(&up->port.lock);
	} else
		spin_lock(&up->port.lock);

	uart_console_write(&up->port, s, count, sunzilog_putchar);
	udelay(2);

	if (locked)
		spin_unlock(&up->port.lock);
	local_irq_restore(flags);
}

static int __init sunzilog_console_setup(struct console *con, char *options)
{
	struct uart_sunzilog_port *up = &sunzilog_port_table[con->index];
	unsigned long flags;
	int baud, brg;

	if (up->port.type != PORT_SUNZILOG)
		return -1;

	printk(KERN_INFO "Console: ttyS%d (SunZilog zs%d)\n",
	       (sunzilog_reg.minor - 64) + con->index, con->index);

	/* Get firmware console settings.  */
	sunserial_console_termios(con, up->port.dev->of_node);

	/* Firmware console speed is limited to 150-->38400 baud so
	 * this hackish cflag thing is OK.
	 */
	switch (con->cflag & CBAUD) {
	case B150: baud = 150; break;
	case B300: baud = 300; break;
	case B600: baud = 600; break;
	case B1200: baud = 1200; break;
	case B2400: baud = 2400; break;
	case B4800: baud = 4800; break;
	default: case B9600: baud = 9600; break;
	case B19200: baud = 19200; break;
	case B38400: baud = 38400; break;
	};

	brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);

	spin_lock_irqsave(&up->port.lock, flags);

	up->curregs[R15] |= BRKIE;
	sunzilog_convert_to_zs(up, con->cflag, 0, brg);

	sunzilog_set_mctrl(&up->port, TIOCM_DTR | TIOCM_RTS);
	__sunzilog_startup(up);

	spin_unlock_irqrestore(&up->port.lock, flags);

	return 0;
}

static struct console sunzilog_console_ops = {
	.name	=	"ttyS",
	.write	=	sunzilog_console_write,
	.device	=	uart_console_device,
	.setup	=	sunzilog_console_setup,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
	.data   =	&sunzilog_reg,
};

static inline struct console *SUNZILOG_CONSOLE(void)
{
	return &sunzilog_console_ops;
}

#else
#define SUNZILOG_CONSOLE()	(NULL)
#endif

static void sunzilog_init_kbdms(struct uart_sunzilog_port *up)
{
	int baud, brg;

	if (up->flags & SUNZILOG_FLAG_CONS_KEYB) {
		up->cflag = B1200 | CS8 | CLOCAL | CREAD;
		baud = 1200;
	} else {
		up->cflag = B4800 | CS8 | CLOCAL | CREAD;
		baud = 4800;
	}

	up->curregs[R15] |= BRKIE;
	brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);
	sunzilog_convert_to_zs(up, up->cflag, 0, brg);
	sunzilog_set_mctrl(&up->port, TIOCM_DTR | TIOCM_RTS);
	__sunzilog_startup(up);
}

#ifdef CONFIG_SERIO
static void sunzilog_register_serio(struct uart_sunzilog_port *up)
{
	struct serio *serio = &up->serio;

	serio->port_data = up;

	serio->id.type = SERIO_RS232;
	if (up->flags & SUNZILOG_FLAG_CONS_KEYB) {
		serio->id.proto = SERIO_SUNKBD;
		strlcpy(serio->name, "zskbd", sizeof(serio->name));
	} else {
		serio->id.proto = SERIO_SUN;
		serio->id.extra = 1;
		strlcpy(serio->name, "zsms", sizeof(serio->name));
	}
	strlcpy(serio->phys,
		((up->flags & SUNZILOG_FLAG_CONS_KEYB) ?
		 "zs/serio0" : "zs/serio1"),
		sizeof(serio->phys));

	serio->write = sunzilog_serio_write;
	serio->open = sunzilog_serio_open;
	serio->close = sunzilog_serio_close;
	serio->dev.parent = up->port.dev;

	serio_register_port(serio);
}
#endif

static void sunzilog_init_hw(struct uart_sunzilog_port *up)
{
	struct zilog_channel __iomem *channel;
	unsigned long flags;
	int baud, brg;

	channel = ZILOG_CHANNEL_FROM_PORT(&up->port);

	spin_lock_irqsave(&up->port.lock, flags);
	if (ZS_IS_CHANNEL_A(up)) {
		write_zsreg(channel, R9, FHWRES);
		ZSDELAY_LONG();
		(void) read_zsreg(channel, R0);
	}

	if (up->flags & (SUNZILOG_FLAG_CONS_KEYB |
			 SUNZILOG_FLAG_CONS_MOUSE)) {
		up->curregs[R1] = EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB;
		up->curregs[R4] = PAR_EVEN | X16CLK | SB1;
		up->curregs[R3] = RxENAB | Rx8;
		up->curregs[R5] = TxENAB | Tx8;
		up->curregs[R6] = 0x00; /* SDLC Address */
		up->curregs[R7] = 0x7E; /* SDLC Flag    */
		up->curregs[R9] = NV;
		up->curregs[R7p] = 0x00;
		sunzilog_init_kbdms(up);
		/* Only enable interrupts if an ISR handler available */
		if (up->flags & SUNZILOG_FLAG_ISR_HANDLER)
			up->curregs[R9] |= MIE;
		write_zsreg(channel, R9, up->curregs[R9]);
	} else {
		/* Normal serial TTY. */
		up->parity_mask = 0xff;
		up->curregs[R1] = EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB;
		up->curregs[R4] = PAR_EVEN | X16CLK | SB1;
		up->curregs[R3] = RxENAB | Rx8;
		up->curregs[R5] = TxENAB | Tx8;
		up->curregs[R6] = 0x00; /* SDLC Address */
		up->curregs[R7] = 0x7E; /* SDLC Flag    */
		up->curregs[R9] = NV;
		up->curregs[R10] = NRZ;
		up->curregs[R11] = TCBR | RCBR;
		baud = 9600;
		brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);
		up->curregs[R12] = (brg & 0xff);
		up->curregs[R13] = (brg >> 8) & 0xff;
		up->curregs[R14] = BRSRC | BRENAB;
		up->curregs[R15] = FIFOEN; /* Use FIFO if on ESCC */
		up->curregs[R7p] = TxFIFO_LVL | RxFIFO_LVL;
		if (__load_zsregs(channel, up->curregs)) {
			up->flags |= SUNZILOG_FLAG_ESCC;
		}
		/* Only enable interrupts if an ISR handler available */
		if (up->flags & SUNZILOG_FLAG_ISR_HANDLER)
			up->curregs[R9] |= MIE;
		write_zsreg(channel, R9, up->curregs[R9]);
	}

	spin_unlock_irqrestore(&up->port.lock, flags);

#ifdef CONFIG_SERIO
	if (up->flags & (SUNZILOG_FLAG_CONS_KEYB |
			 SUNZILOG_FLAG_CONS_MOUSE))
		sunzilog_register_serio(up);
#endif
}

static int zilog_irq;

static int zs_probe(struct platform_device *op)
{
	static int kbm_inst, uart_inst;
	int inst;
	struct uart_sunzilog_port *up;
	struct zilog_layout __iomem *rp;
	int keyboard_mouse = 0;
	int err;

	if (of_find_property(op->dev.of_node, "keyboard", NULL))
		keyboard_mouse = 1;

	/* uarts must come before keyboards/mice */
	if (keyboard_mouse)
		inst = uart_chip_count + kbm_inst;
	else
		inst = uart_inst;

	sunzilog_chip_regs[inst] = of_ioremap(&op->resource[0], 0,
					      sizeof(struct zilog_layout),
					      "zs");
	if (!sunzilog_chip_regs[inst])
		return -ENOMEM;

	rp = sunzilog_chip_regs[inst];

	if (!zilog_irq)
		zilog_irq = op->archdata.irqs[0];

	up = &sunzilog_port_table[inst * 2];

	/* Channel A */
	up[0].port.mapbase = op->resource[0].start + 0x00;
	up[0].port.membase = (void __iomem *) &rp->channelA;
	up[0].port.iotype = UPIO_MEM;
	up[0].port.irq = op->archdata.irqs[0];
	up[0].port.uartclk = ZS_CLOCK;
	up[0].port.fifosize = 1;
	up[0].port.ops = &sunzilog_pops;
	up[0].port.type = PORT_SUNZILOG;
	up[0].port.flags = 0;
	up[0].port.line = (inst * 2) + 0;
	up[0].port.dev = &op->dev;
	up[0].flags |= SUNZILOG_FLAG_IS_CHANNEL_A;
	if (keyboard_mouse)
		up[0].flags |= SUNZILOG_FLAG_CONS_KEYB;
	sunzilog_init_hw(&up[0]);

	/* Channel B */
	up[1].port.mapbase = op->resource[0].start + 0x04;
	up[1].port.membase = (void __iomem *) &rp->channelB;
	up[1].port.iotype = UPIO_MEM;
	up[1].port.irq = op->archdata.irqs[0];
	up[1].port.uartclk = ZS_CLOCK;
	up[1].port.fifosize = 1;
	up[1].port.ops = &sunzilog_pops;
	up[1].port.type = PORT_SUNZILOG;
	up[1].port.flags = 0;
	up[1].port.line = (inst * 2) + 1;
	up[1].port.dev = &op->dev;
	up[1].flags |= 0;
	if (keyboard_mouse)
		up[1].flags |= SUNZILOG_FLAG_CONS_MOUSE;
	sunzilog_init_hw(&up[1]);

	if (!keyboard_mouse) {
		if (sunserial_console_match(SUNZILOG_CONSOLE(), op->dev.of_node,
					    &sunzilog_reg, up[0].port.line,
					    false))
			up->flags |= SUNZILOG_FLAG_IS_CONS;
		err = uart_add_one_port(&sunzilog_reg, &up[0].port);
		if (err) {
			of_iounmap(&op->resource[0],
				   rp, sizeof(struct zilog_layout));
			return err;
		}
		if (sunserial_console_match(SUNZILOG_CONSOLE(), op->dev.of_node,
					    &sunzilog_reg, up[1].port.line,
					    false))
			up->flags |= SUNZILOG_FLAG_IS_CONS;
		err = uart_add_one_port(&sunzilog_reg, &up[1].port);
		if (err) {
			uart_remove_one_port(&sunzilog_reg, &up[0].port);
			of_iounmap(&op->resource[0],
				   rp, sizeof(struct zilog_layout));
			return err;
		}
		uart_inst++;
	} else {
		printk(KERN_INFO "%s: Keyboard at MMIO 0x%llx (irq = %d) "
		       "is a %s\n",
		       dev_name(&op->dev),
		       (unsigned long long) up[0].port.mapbase,
		       op->archdata.irqs[0], sunzilog_type(&up[0].port));
		printk(KERN_INFO "%s: Mouse at MMIO 0x%llx (irq = %d) "
		       "is a %s\n",
		       dev_name(&op->dev),
		       (unsigned long long) up[1].port.mapbase,
		       op->archdata.irqs[0], sunzilog_type(&up[1].port));
		kbm_inst++;
	}

	dev_set_drvdata(&op->dev, &up[0]);

	return 0;
}

static void zs_remove_one(struct uart_sunzilog_port *up)
{
	if (ZS_IS_KEYB(up) || ZS_IS_MOUSE(up)) {
#ifdef CONFIG_SERIO
		serio_unregister_port(&up->serio);
#endif
	} else
		uart_remove_one_port(&sunzilog_reg, &up->port);
}

static int zs_remove(struct platform_device *op)
{
	struct uart_sunzilog_port *up = dev_get_drvdata(&op->dev);
	struct zilog_layout __iomem *regs;

	zs_remove_one(&up[0]);
	zs_remove_one(&up[1]);

	regs = sunzilog_chip_regs[up[0].port.line / 2];
	of_iounmap(&op->resource[0], regs, sizeof(struct zilog_layout));

	dev_set_drvdata(&op->dev, NULL);

	return 0;
}

static const struct of_device_id zs_match[] = {
	{
		.name = "zs",
	},
	{},
};
MODULE_DEVICE_TABLE(of, zs_match);

static struct platform_driver zs_driver = {
	.driver = {
		.name = "zs",
		.owner = THIS_MODULE,
		.of_match_table = zs_match,
	},
	.probe		= zs_probe,
	.remove		= zs_remove,
};

static int __init sunzilog_init(void)
{
	struct device_node *dp;
	int err;
	int num_keybms = 0;
	int num_sunzilog = 0;

	for_each_node_by_name(dp, "zs") {
		num_sunzilog++;
		if (of_find_property(dp, "keyboard", NULL))
			num_keybms++;
	}

	if (num_sunzilog) {
		err = sunzilog_alloc_tables(num_sunzilog);
		if (err)
			goto out;

		uart_chip_count = num_sunzilog - num_keybms;

		err = sunserial_register_minors(&sunzilog_reg,
						uart_chip_count * 2);
		if (err)
			goto out_free_tables;
	}

	err = platform_driver_register(&zs_driver);
	if (err)
		goto out_unregister_uart;

	if (zilog_irq) {
		struct uart_sunzilog_port *up = sunzilog_irq_chain;
		err = request_irq(zilog_irq, sunzilog_interrupt, IRQF_SHARED,
				  "zs", sunzilog_irq_chain);
		if (err)
			goto out_unregister_driver;

		/* Enable Interrupts */
		while (up) {
			struct zilog_channel __iomem *channel;

			/* printk (KERN_INFO "Enable IRQ for ZILOG Hardware %p\n", up); */
			channel          = ZILOG_CHANNEL_FROM_PORT(&up->port);
			up->flags       |= SUNZILOG_FLAG_ISR_HANDLER;
			up->curregs[R9] |= MIE;
			write_zsreg(channel, R9, up->curregs[R9]);
			up = up->next;
		}
	}

out:
	return err;

out_unregister_driver:
	platform_driver_unregister(&zs_driver);

out_unregister_uart:
	if (num_sunzilog) {
		sunserial_unregister_minors(&sunzilog_reg, num_sunzilog);
		sunzilog_reg.cons = NULL;
	}

out_free_tables:
	sunzilog_free_tables();
	goto out;
}

static void __exit sunzilog_exit(void)
{
	platform_driver_unregister(&zs_driver);

	if (zilog_irq) {
		struct uart_sunzilog_port *up = sunzilog_irq_chain;

		/* Disable Interrupts */
		while (up) {
			struct zilog_channel __iomem *channel;

			/* printk (KERN_INFO "Disable IRQ for ZILOG Hardware %p\n", up); */
			channel          = ZILOG_CHANNEL_FROM_PORT(&up->port);
			up->flags       &= ~SUNZILOG_FLAG_ISR_HANDLER;
			up->curregs[R9] &= ~MIE;
			write_zsreg(channel, R9, up->curregs[R9]);
			up = up->next;
		}

		free_irq(zilog_irq, sunzilog_irq_chain);
		zilog_irq = 0;
	}

	if (sunzilog_reg.nr) {
		sunserial_unregister_minors(&sunzilog_reg, sunzilog_reg.nr);
		sunzilog_free_tables();
	}
}

module_init(sunzilog_init);
module_exit(sunzilog_exit);

MODULE_AUTHOR("David S. Miller");
MODULE_DESCRIPTION("Sun Zilog serial port driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
