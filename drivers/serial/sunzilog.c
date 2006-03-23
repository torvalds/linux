/*
 * sunzilog.c
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
 *  Copyright (C) 2002 David S. Miller (davem@redhat.com)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
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

#include <asm/io.h>
#include <asm/irq.h>
#ifdef CONFIG_SPARC64
#include <asm/fhc.h>
#endif
#include <asm/sbus.h>

#if defined(CONFIG_SERIAL_SUNZILOG_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#include "suncore.h"
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
	sbus_readb(&((__channel)->control))
#endif

static int num_sunzilog;
#define NUM_SUNZILOG	num_sunzilog
#define NUM_CHANNELS	(NUM_SUNZILOG * 2)

#define KEYBOARD_LINE 0x2
#define MOUSE_LINE    0x3

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

	unsigned int cflag;

	unsigned char			parity_mask;
	unsigned char			prev_status;

#ifdef CONFIG_SERIO
	struct serio			*serio;
	int				serio_open;
#endif
};

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

	sbus_writeb(reg, &channel->control);
	ZSDELAY();
	retval = sbus_readb(&channel->control);
	ZSDELAY();

	return retval;
}

static void write_zsreg(struct zilog_channel __iomem *channel,
			unsigned char reg, unsigned char value)
{
	sbus_writeb(reg, &channel->control);
	ZSDELAY();
	sbus_writeb(value, &channel->control);
	ZSDELAY();
}

static void sunzilog_clear_fifo(struct zilog_channel __iomem *channel)
{
	int i;

	for (i = 0; i < 32; i++) {
		unsigned char regval;

		regval = sbus_readb(&channel->control);
		ZSDELAY();
		if (regval & Rx_CH_AV)
			break;

		regval = read_zsreg(channel, R1);
		sbus_readb(&channel->data);
		ZSDELAY();

		if (regval & (PAR_ERR | Rx_OVR | CRC_ERR)) {
			sbus_writeb(ERR_RES, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);
		}
	}
}

/* This function must only be called when the TX is not busy.  The UART
 * port lock must be held and local interrupts disabled.
 */
static void __load_zsregs(struct zilog_channel __iomem *channel, unsigned char *regs)
{
	int i;

	/* Let pending transmits finish.  */
	for (i = 0; i < 1000; i++) {
		unsigned char stat = read_zsreg(channel, R1);
		if (stat & ALL_SNT)
			break;
		udelay(100);
	}

	sbus_writeb(ERR_RES, &channel->control);
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
	write_zsreg(channel, R15, regs[R15]);

	/* Reset external status interrupts.  */
	write_zsreg(channel, R0, RES_EXT_INT);
	write_zsreg(channel, R0, RES_EXT_INT);

	/* Rewrite R3/R5, this time without enables masked.  */
	write_zsreg(channel, R3, regs[R3]);
	write_zsreg(channel, R5, regs[R5]);

	/* Rewrite R1, this time without IRQ enabled masked.  */
	write_zsreg(channel, R1, regs[R1]);
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
					 unsigned char ch, int is_break,
					 struct pt_regs *regs)
{
	if (ZS_IS_KEYB(up)) {
		/* Stop-A is handled by drivers/char/keyboard.c now. */
#ifdef CONFIG_SERIO
		if (up->serio_open)
			serio_interrupt(up->serio, ch, 0, regs);
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
				serio_interrupt(up->serio, ch, 0, regs);
#endif
			break;
		};
	}
}

static struct tty_struct *
sunzilog_receive_chars(struct uart_sunzilog_port *up,
		       struct zilog_channel __iomem *channel,
		       struct pt_regs *regs)
{
	struct tty_struct *tty;
	unsigned char ch, r1, flag;

	tty = NULL;
	if (up->port.info != NULL &&		/* Unopened serial console */
	    up->port.info->tty != NULL)		/* Keyboard || mouse */
		tty = up->port.info->tty;

	for (;;) {

		r1 = read_zsreg(channel, R1);
		if (r1 & (PAR_ERR | Rx_OVR | CRC_ERR)) {
			sbus_writeb(ERR_RES, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);
		}

		ch = sbus_readb(&channel->control);
		ZSDELAY();

		/* This funny hack depends upon BRK_ABRT not interfering
		 * with the other bits we care about in R1.
		 */
		if (ch & BRK_ABRT)
			r1 |= BRK_ABRT;

		if (!(ch & Rx_CH_AV))
			break;

		ch = sbus_readb(&channel->data);
		ZSDELAY();

		ch &= up->parity_mask;

		if (unlikely(ZS_IS_KEYB(up)) || unlikely(ZS_IS_MOUSE(up))) {
			sunzilog_kbdms_receive_chars(up, ch, 0, regs);
			continue;
		}

		if (tty == NULL) {
			uart_handle_sysrq_char(&up->port, ch, regs);
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
		if (uart_handle_sysrq_char(&up->port, ch, regs))
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
				   struct zilog_channel __iomem *channel,
				   struct pt_regs *regs)
{
	unsigned char status;

	status = sbus_readb(&channel->control);
	ZSDELAY();

	sbus_writeb(RES_EXT_INT, &channel->control);
	ZSDELAY();
	ZS_WSYNC(channel);

	if (status & BRK_ABRT) {
		if (ZS_IS_MOUSE(up))
			sunzilog_kbdms_receive_chars(up, 0, 1, regs);
		if (ZS_IS_CONS(up)) {
			/* Wait for BREAK to deassert to avoid potentially
			 * confusing the PROM.
			 */
			while (1) {
				status = sbus_readb(&channel->control);
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

		wake_up_interruptible(&up->port.info->delta_msr_wait);
	}

	up->prev_status = status;
}

static void sunzilog_transmit_chars(struct uart_sunzilog_port *up,
				    struct zilog_channel __iomem *channel)
{
	struct circ_buf *xmit;

	if (ZS_IS_CONS(up)) {
		unsigned char status = sbus_readb(&channel->control);
		ZSDELAY();

		/* TX still busy?  Just wait for the next TX done interrupt.
		 *
		 * It can occur because of how we do serial console writes.  It would
		 * be nice to transmit console writes just like we normally would for
		 * a TTY line. (ie. buffered and TX interrupt driven).  That is not
		 * easy because console writes cannot sleep.  One solution might be
		 * to poll on enough port->xmit space becomming free.  -DaveM
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
		sbus_writeb(up->port.x_char, &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}

	if (up->port.info == NULL)
		goto ack_tx_int;
	xmit = &up->port.info->xmit;
	if (uart_circ_empty(xmit))
		goto ack_tx_int;

	if (uart_tx_stopped(&up->port))
		goto ack_tx_int;

	up->flags |= SUNZILOG_FLAG_TX_ACTIVE;
	sbus_writeb(xmit->buf[xmit->tail], &channel->data);
	ZSDELAY();
	ZS_WSYNC(channel);

	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	up->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	return;

ack_tx_int:
	sbus_writeb(RES_Tx_P, &channel->control);
	ZSDELAY();
	ZS_WSYNC(channel);
}

static irqreturn_t sunzilog_interrupt(int irq, void *dev_id, struct pt_regs *regs)
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
			sbus_writeb(RES_H_IUS, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);

			if (r3 & CHARxIP)
				tty = sunzilog_receive_chars(up, channel, regs);
			if (r3 & CHAEXT)
				sunzilog_status_handle(up, channel, regs);
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
			sbus_writeb(RES_H_IUS, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);

			if (r3 & CHBRxIP)
				tty = sunzilog_receive_chars(up, channel, regs);
			if (r3 & CHBEXT)
				sunzilog_status_handle(up, channel, regs);
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
	status = sbus_readb(&channel->control);
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

	status = sbus_readb(&channel->control);
	ZSDELAY();

	/* TX busy?  Just wait for the TX done interrupt.  */
	if (!(status & Tx_BUF_EMP))
		return;

	/* Send the first character to jump-start the TX done
	 * IRQ sending engine.
	 */
	if (port->x_char) {
		sbus_writeb(port->x_char, &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		port->icount.tx++;
		port->x_char = 0;
	} else {
		struct circ_buf *xmit = &port->info->xmit;

		sbus_writeb(xmit->buf[xmit->tail], &channel->data);
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
		write_zsreg(channel, R15, up->curregs[R15]);
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
	up->prev_status = sbus_readb(&channel->control);

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
	up->curregs[3] &= ~RxN_MASK;
	up->curregs[5] &= ~TxN_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		up->curregs[3] |= Rx5;
		up->curregs[5] |= Tx5;
		up->parity_mask = 0x1f;
		break;
	case CS6:
		up->curregs[3] |= Rx6;
		up->curregs[5] |= Tx6;
		up->parity_mask = 0x3f;
		break;
	case CS7:
		up->curregs[3] |= Rx7;
		up->curregs[5] |= Tx7;
		up->parity_mask = 0x7f;
		break;
	case CS8:
	default:
		up->curregs[3] |= Rx8;
		up->curregs[5] |= Tx8;
		up->parity_mask = 0xff;
		break;
	};
	up->curregs[4] &= ~0x0c;
	if (cflag & CSTOPB)
		up->curregs[4] |= SB2;
	else
		up->curregs[4] |= SB1;
	if (cflag & PARENB)
		up->curregs[4] |= PAR_ENAB;
	else
		up->curregs[4] &= ~PAR_ENAB;
	if (!(cflag & PARODD))
		up->curregs[4] |= PAR_EVEN;
	else
		up->curregs[4] &= ~PAR_EVEN;

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
sunzilog_set_termios(struct uart_port *port, struct termios *termios,
		     struct termios *old)
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
	return "SunZilog";
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
};

static struct uart_sunzilog_port *sunzilog_port_table;
static struct zilog_layout __iomem **sunzilog_chip_regs;

static struct uart_sunzilog_port *sunzilog_irq_chain;
static int zilog_irq = -1;

static struct uart_driver sunzilog_reg = {
	.owner		=	THIS_MODULE,
	.driver_name	=	"ttyS",
	.devfs_name	=	"tts/",
	.dev_name	=	"ttyS",
	.major		=	TTY_MAJOR,
};

static void * __init alloc_one_table(unsigned long size)
{
	void *ret;

	ret = kmalloc(size, GFP_KERNEL);
	if (ret != NULL)
		memset(ret, 0, size);

	return ret;
}

static void __init sunzilog_alloc_tables(void)
{
	sunzilog_port_table = 
		alloc_one_table(NUM_CHANNELS * sizeof(struct uart_sunzilog_port));
	sunzilog_chip_regs = 
		alloc_one_table(NUM_SUNZILOG * sizeof(struct zilog_layout __iomem *));

	if (sunzilog_port_table == NULL || sunzilog_chip_regs == NULL) {
		prom_printf("SunZilog: Cannot allocate tables.\n");
		prom_halt();
	}
}

#ifdef CONFIG_SPARC64

/* We used to attempt to use the address property of the Zilog device node
 * but that totally is not necessary on sparc64.
 */
static struct zilog_layout __iomem * __init get_zs_sun4u(int chip, int zsnode)
{
	void __iomem *mapped_addr;
	unsigned int sun4u_ino;
	struct sbus_bus *sbus = NULL;
	struct sbus_dev *sdev = NULL;
	int err;

	if (central_bus == NULL) {
		for_each_sbus(sbus) {
			for_each_sbusdev(sdev, sbus) {
				if (sdev->prom_node == zsnode)
					goto found;
			}
		}
	}
 found:
	if (sdev == NULL && central_bus == NULL) {
		prom_printf("SunZilog: sdev&&central == NULL for "
			    "Zilog %d in get_zs_sun4u.\n", chip);
		prom_halt();
	}
	if (central_bus == NULL) {
		mapped_addr =
			sbus_ioremap(&sdev->resource[0], 0,
				     PAGE_SIZE,
				     "Zilog Registers");
	} else {
		struct linux_prom_registers zsregs[1];

		err = prom_getproperty(zsnode, "reg",
				       (char *) &zsregs[0],
				       sizeof(zsregs));
		if (err == -1) {
			prom_printf("SunZilog: Cannot map "
				    "Zilog %d regs on "
				    "central bus.\n", chip);
			prom_halt();
		}
		apply_fhc_ranges(central_bus->child,
				 &zsregs[0], 1);
		apply_central_ranges(central_bus, &zsregs[0], 1);
		mapped_addr = (void __iomem *)
			((((u64)zsregs[0].which_io)<<32UL) |
			((u64)zsregs[0].phys_addr));
	}

	if (zilog_irq == -1) {
		if (central_bus) {
			unsigned long iclr, imap;

			iclr = central_bus->child->fhc_regs.uregs
				+ FHC_UREGS_ICLR;
			imap = central_bus->child->fhc_regs.uregs
				+ FHC_UREGS_IMAP;
			zilog_irq = build_irq(12, 0, iclr, imap);
		} else {
			err = prom_getproperty(zsnode, "interrupts",
					       (char *) &sun4u_ino,
					       sizeof(sun4u_ino));
			zilog_irq = sbus_build_irq(sbus_root, sun4u_ino);
		}
	}

	return (struct zilog_layout __iomem *) mapped_addr;
}
#else /* CONFIG_SPARC64 */

/*
 * XXX The sun4d case is utterly screwed: it tries to re-walk the tree
 * (for the 3rd time) in order to find bootbus and cpu. Streamline it.
 */
static struct zilog_layout __iomem * __init get_zs_sun4cmd(int chip, int node)
{
	struct linux_prom_irqs irq_info[2];
	void __iomem *mapped_addr = NULL;
	int zsnode, cpunode, bbnode;
	struct linux_prom_registers zsreg[4];
	struct resource res;

	if (sparc_cpu_model == sun4d) {
		int walk;

		zsnode = 0;
		bbnode = 0;
		cpunode = 0;
		for (walk = prom_getchild(prom_root_node);
		     (walk = prom_searchsiblings(walk, "cpu-unit")) != 0;
		     walk = prom_getsibling(walk)) {
			bbnode = prom_getchild(walk);
			if (bbnode &&
			    (bbnode = prom_searchsiblings(bbnode, "bootbus"))) {
				if ((zsnode = prom_getchild(bbnode)) == node) {
					cpunode = walk;
					break;
				}
			}
		}
		if (!walk) {
			prom_printf("SunZilog: Cannot find the %d'th bootbus on sun4d.\n",
				    (chip / 2));
			prom_halt();
		}

		if (prom_getproperty(zsnode, "reg",
				     (char *) zsreg, sizeof(zsreg)) == -1) {
			prom_printf("SunZilog: Cannot map Zilog %d\n", chip);
			prom_halt();
		}
		/* XXX Looks like an off by one? */
		prom_apply_generic_ranges(bbnode, cpunode, zsreg, 1);
		res.start = zsreg[0].phys_addr;
		res.end = res.start + (8 - 1);
		res.flags = zsreg[0].which_io | IORESOURCE_IO;
		mapped_addr = sbus_ioremap(&res, 0, 8, "Zilog Serial");

	} else {
		zsnode = node;

#if 0 /* XXX When was this used? */
		if (prom_getintdefault(zsnode, "slave", -1) != chipid) {
			zsnode = prom_getsibling(zsnode);
			continue;
		}
#endif

		/*
		 * "address" is only present on ports that OBP opened
		 * (from Mitch Bradley's "Hitchhiker's Guide to OBP").
		 * We do not use it.
		 */

		if (prom_getproperty(zsnode, "reg",
				     (char *) zsreg, sizeof(zsreg)) == -1) {
			prom_printf("SunZilog: Cannot map Zilog %d\n", chip);
			prom_halt();
		}
		if (sparc_cpu_model == sun4m)	/* Crude. Pass parent. XXX */
			prom_apply_obio_ranges(zsreg, 1);
		res.start = zsreg[0].phys_addr;
		res.end = res.start + (8 - 1);
		res.flags = zsreg[0].which_io | IORESOURCE_IO;
		mapped_addr = sbus_ioremap(&res, 0, 8, "Zilog Serial");
	}

	if (prom_getproperty(zsnode, "intr",
			     (char *) irq_info, sizeof(irq_info))
		    % sizeof(struct linux_prom_irqs)) {
		prom_printf("SunZilog: Cannot get IRQ property for Zilog %d.\n",
			    chip);
		prom_halt();
	}
	if (zilog_irq == -1) {
		zilog_irq = irq_info[0].pri;
	} else if (zilog_irq != irq_info[0].pri) {
		/* XXX. Dumb. Should handle per-chip IRQ, for add-ons. */
		prom_printf("SunZilog: Inconsistent IRQ layout for Zilog %d.\n",
			    chip);
		prom_halt();
	}

	return (struct zilog_layout __iomem *) mapped_addr;
}
#endif /* !(CONFIG_SPARC64) */

/* Get the address of the registers for SunZilog instance CHIP.  */
static struct zilog_layout __iomem * __init get_zs(int chip, int node)
{
	if (chip < 0 || chip >= NUM_SUNZILOG) {
		prom_printf("SunZilog: Illegal chip number %d in get_zs.\n", chip);
		prom_halt();
	}

#ifdef CONFIG_SPARC64
	return get_zs_sun4u(chip, node);
#else

	if (sparc_cpu_model == sun4) {
		struct resource res;

		/* Not probe-able, hard code it. */
		switch (chip) {
		case 0:
			res.start = 0xf1000000;
			break;
		case 1:
			res.start = 0xf0000000;
			break;
		};
		zilog_irq = 12;
		res.end = (res.start + (8 - 1));
		res.flags = IORESOURCE_IO;
		return sbus_ioremap(&res, 0, 8, "SunZilog");
	}

	return get_zs_sun4cmd(chip, node);
#endif
}

#define ZS_PUT_CHAR_MAX_DELAY	2000	/* 10 ms */

static void sunzilog_putchar(struct uart_port *port, int ch)
{
	struct zilog_channel *channel = ZILOG_CHANNEL_FROM_PORT(port);
	int loops = ZS_PUT_CHAR_MAX_DELAY;

	/* This is a timed polling loop so do not switch the explicit
	 * udelay with ZSDELAY as that is a NOP on some platforms.  -DaveM
	 */
	do {
		unsigned char val = sbus_readb(&channel->control);
		if (val & Tx_BUF_EMP) {
			ZSDELAY();
			break;
		}
		udelay(5);
	} while (--loops);

	sbus_writeb(ch, &channel->data);
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

	spin_lock_irqsave(&up->port.lock, flags);
	uart_console_write(&up->port, s, count, sunzilog_putchar);
	udelay(2);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int __init sunzilog_console_setup(struct console *con, char *options)
{
	struct uart_sunzilog_port *up = &sunzilog_port_table[con->index];
	unsigned long flags;
	int baud, brg;

	printk(KERN_INFO "Console: ttyS%d (SunZilog zs%d)\n",
	       (sunzilog_reg.minor - 64) + con->index, con->index);

	/* Get firmware console settings.  */
	sunserial_console_termios(con);

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

	up->curregs[R15] = BRKIE;
	sunzilog_convert_to_zs(up, con->cflag, 0, brg);

	sunzilog_set_mctrl(&up->port, TIOCM_DTR | TIOCM_RTS);
	__sunzilog_startup(up);

	spin_unlock_irqrestore(&up->port.lock, flags);

	return 0;
}

static struct console sunzilog_console = {
	.name	=	"ttyS",
	.write	=	sunzilog_console_write,
	.device	=	uart_console_device,
	.setup	=	sunzilog_console_setup,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
	.data   =	&sunzilog_reg,
};

static int __init sunzilog_console_init(void)
{
	int i;

	if (con_is_present())
		return 0;

	for (i = 0; i < NUM_CHANNELS; i++) {
		int this_minor = sunzilog_reg.minor + i;

		if ((this_minor - 64) == (serial_console - 1))
			break;
	}
	if (i == NUM_CHANNELS)
		return 0;

	sunzilog_console.index = i;
	sunzilog_port_table[i].flags |= SUNZILOG_FLAG_IS_CONS;
	register_console(&sunzilog_console);
	return 0;
}

static inline struct console *SUNZILOG_CONSOLE(void)
{
	int i;

	if (con_is_present())
		return NULL;

	for (i = 0; i < NUM_CHANNELS; i++) {
		int this_minor = sunzilog_reg.minor + i;

		if ((this_minor - 64) == (serial_console - 1))
			break;
	}
	if (i == NUM_CHANNELS)
		return NULL;

	sunzilog_console.index = i;
	sunzilog_port_table[i].flags |= SUNZILOG_FLAG_IS_CONS;

	return &sunzilog_console;
}

#else
#define SUNZILOG_CONSOLE()	(NULL)
#define sunzilog_console_init() do { } while (0)
#endif

/*
 * We scan the PROM tree recursively. This is the most reliable way
 * to find Zilog nodes on various platforms. However, we face an extreme
 * shortage of kernel stack, so we must be very careful. To that end,
 * we scan only to a certain depth, and we use a common property buffer
 * in the scan structure.
 */
#define ZS_PROPSIZE  128
#define ZS_SCAN_DEPTH	5

struct zs_probe_scan {
	int depth;
	void (*scanner)(struct zs_probe_scan *t, int node);

	int devices;
	char prop[ZS_PROPSIZE];
};

static int __inline__ sunzilog_node_ok(int node, const char *name, int len)
{
	if (strncmp(name, "zs", len) == 0)
		return 1;
	/* Don't fold this procedure just yet. Compare to su_node_ok(). */
	return 0;
}

static void __init sunzilog_scan(struct zs_probe_scan *t, int node)
{
	int len;

	for (; node != 0; node = prom_getsibling(node)) {
		len = prom_getproperty(node, "name", t->prop, ZS_PROPSIZE);
		if (len <= 1)
			continue;		/* Broken PROM node */
		if (sunzilog_node_ok(node, t->prop, len)) {
			(*t->scanner)(t, node);
		} else {
			if (t->depth < ZS_SCAN_DEPTH) {
				t->depth++;
				sunzilog_scan(t, prom_getchild(node));
				--t->depth;
			}
		}
	}
}

static void __init sunzilog_prepare(void)
{
	struct uart_sunzilog_port *up;
	struct zilog_layout __iomem *rp;
	int channel, chip;

	/*
	 * Temporary fix.
	 */
	for (channel = 0; channel < NUM_CHANNELS; channel++)
		spin_lock_init(&sunzilog_port_table[channel].port.lock);

	sunzilog_irq_chain = up = &sunzilog_port_table[0];
	for (channel = 0; channel < NUM_CHANNELS - 1; channel++)
		up[channel].next = &up[channel + 1];
	up[channel].next = NULL;

	for (chip = 0; chip < NUM_SUNZILOG; chip++) {
		rp = sunzilog_chip_regs[chip];
		up[(chip * 2) + 0].port.membase = (void __iomem *)&rp->channelA;
		up[(chip * 2) + 1].port.membase = (void __iomem *)&rp->channelB;

		/* Channel A */
		up[(chip * 2) + 0].port.iotype = UPIO_MEM;
		up[(chip * 2) + 0].port.irq = zilog_irq;
		up[(chip * 2) + 0].port.uartclk = ZS_CLOCK;
		up[(chip * 2) + 0].port.fifosize = 1;
		up[(chip * 2) + 0].port.ops = &sunzilog_pops;
		up[(chip * 2) + 0].port.type = PORT_SUNZILOG;
		up[(chip * 2) + 0].port.flags = 0;
		up[(chip * 2) + 0].port.line = (chip * 2) + 0;
		up[(chip * 2) + 0].flags |= SUNZILOG_FLAG_IS_CHANNEL_A;

		/* Channel B */
		up[(chip * 2) + 1].port.iotype = UPIO_MEM;
		up[(chip * 2) + 1].port.irq = zilog_irq;
		up[(chip * 2) + 1].port.uartclk = ZS_CLOCK;
		up[(chip * 2) + 1].port.fifosize = 1;
		up[(chip * 2) + 1].port.ops = &sunzilog_pops;
		up[(chip * 2) + 1].port.type = PORT_SUNZILOG;
		up[(chip * 2) + 1].port.flags = 0;
		up[(chip * 2) + 1].port.line = (chip * 2) + 1;
		up[(chip * 2) + 1].flags |= 0;
	}
}

static void __init sunzilog_init_kbdms(struct uart_sunzilog_port *up, int channel)
{
	int baud, brg;

	if (channel == KEYBOARD_LINE) {
		up->flags |= SUNZILOG_FLAG_CONS_KEYB;
		up->cflag = B1200 | CS8 | CLOCAL | CREAD;
		baud = 1200;
	} else {
		up->flags |= SUNZILOG_FLAG_CONS_MOUSE;
		up->cflag = B4800 | CS8 | CLOCAL | CREAD;
		baud = 4800;
	}
	printk(KERN_INFO "zs%d at 0x%p (irq = %s) is a SunZilog\n",
	       channel, up->port.membase, __irq_itoa(zilog_irq));

	up->curregs[R15] = BRKIE;
	brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);
	sunzilog_convert_to_zs(up, up->cflag, 0, brg);
	sunzilog_set_mctrl(&up->port, TIOCM_DTR | TIOCM_RTS);
	__sunzilog_startup(up);
}

#ifdef CONFIG_SERIO
static void __init sunzilog_register_serio(struct uart_sunzilog_port *up, int channel)
{
	struct serio *serio;

	up->serio = serio = kmalloc(sizeof(struct serio), GFP_KERNEL);
	if (serio) {
		memset(serio, 0, sizeof(*serio));

		serio->port_data = up;

		serio->id.type = SERIO_RS232;
		if (channel == KEYBOARD_LINE) {
			serio->id.proto = SERIO_SUNKBD;
			strlcpy(serio->name, "zskbd", sizeof(serio->name));
		} else {
			serio->id.proto = SERIO_SUN;
			serio->id.extra = 1;
			strlcpy(serio->name, "zsms", sizeof(serio->name));
		}
		strlcpy(serio->phys,
			(channel == KEYBOARD_LINE ? "zs/serio0" : "zs/serio1"),
			sizeof(serio->phys));

		serio->write = sunzilog_serio_write;
		serio->open = sunzilog_serio_open;
		serio->close = sunzilog_serio_close;

		serio_register_port(serio);
	} else {
		printk(KERN_WARNING "zs%d: not enough memory for serio port\n",
			channel);
	}
}
#endif

static void __init sunzilog_init_hw(void)
{
	int i;

	for (i = 0; i < NUM_CHANNELS; i++) {
		struct uart_sunzilog_port *up = &sunzilog_port_table[i];
		struct zilog_channel __iomem *channel = ZILOG_CHANNEL_FROM_PORT(&up->port);
		unsigned long flags;
		int baud, brg;

		spin_lock_irqsave(&up->port.lock, flags);

		if (ZS_IS_CHANNEL_A(up)) {
			write_zsreg(channel, R9, FHWRES);
			ZSDELAY_LONG();
			(void) read_zsreg(channel, R0);
		}

		if (i == KEYBOARD_LINE || i == MOUSE_LINE) {
			sunzilog_init_kbdms(up, i);
			up->curregs[R9] |= (NV | MIE);
			write_zsreg(channel, R9, up->curregs[R9]);
		} else {
			/* Normal serial TTY. */
			up->parity_mask = 0xff;
			up->curregs[R1] = EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB;
			up->curregs[R4] = PAR_EVEN | X16CLK | SB1;
			up->curregs[R3] = RxENAB | Rx8;
			up->curregs[R5] = TxENAB | Tx8;
			up->curregs[R9] = NV | MIE;
			up->curregs[R10] = NRZ;
			up->curregs[R11] = TCBR | RCBR;
			baud = 9600;
			brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);
			up->curregs[R12] = (brg & 0xff);
			up->curregs[R13] = (brg >> 8) & 0xff;
			up->curregs[R14] = BRSRC | BRENAB;
			__load_zsregs(channel, up->curregs);
			write_zsreg(channel, R9, up->curregs[R9]);
		}

		spin_unlock_irqrestore(&up->port.lock, flags);

#ifdef CONFIG_SERIO
		if (i == KEYBOARD_LINE || i == MOUSE_LINE)
			sunzilog_register_serio(up, i);
#endif
	}
}

static struct zilog_layout __iomem * __init get_zs(int chip, int node);

static void __init sunzilog_scan_probe(struct zs_probe_scan *t, int node)
{
	sunzilog_chip_regs[t->devices] = get_zs(t->devices, node);
	t->devices++;
}

static int __init sunzilog_ports_init(void)
{
	struct zs_probe_scan scan;
	int ret;
	int uart_count;
	int i;

	printk(KERN_DEBUG "SunZilog: %d chips.\n", NUM_SUNZILOG);

	scan.scanner = sunzilog_scan_probe;
	scan.depth = 0;
	scan.devices = 0;
	sunzilog_scan(&scan, prom_getchild(prom_root_node));

	sunzilog_prepare();

	if (request_irq(zilog_irq, sunzilog_interrupt, SA_SHIRQ,
			"SunZilog", sunzilog_irq_chain)) {
		prom_printf("SunZilog: Unable to register zs interrupt handler.\n");
		prom_halt();
	}

	sunzilog_init_hw();

	/* We can only init this once we have probed the Zilogs
	 * in the system. Do not count channels assigned to keyboards
	 * or mice when we are deciding how many ports to register.
	 */
	uart_count = 0;
	for (i = 0; i < NUM_CHANNELS; i++) {
		struct uart_sunzilog_port *up = &sunzilog_port_table[i];

		if (ZS_IS_KEYB(up) || ZS_IS_MOUSE(up))
			continue;

		uart_count++;
	}
		
	sunzilog_reg.nr = uart_count;
	sunzilog_reg.minor = sunserial_current_minor;

	ret = uart_register_driver(&sunzilog_reg);
	if (ret == 0) {
		sunzilog_reg.tty_driver->name_base = sunzilog_reg.minor - 64;
		sunzilog_reg.cons = SUNZILOG_CONSOLE();

		sunserial_current_minor += uart_count;

		for (i = 0; i < NUM_CHANNELS; i++) {
			struct uart_sunzilog_port *up = &sunzilog_port_table[i];

			if (ZS_IS_KEYB(up) || ZS_IS_MOUSE(up))
				continue;

			if (uart_add_one_port(&sunzilog_reg, &up->port)) {
				printk(KERN_ERR
				    "SunZilog: failed to add port zs%d\n", i);
			}
		}
	}

	return ret;
}

static void __init sunzilog_scan_count(struct zs_probe_scan *t, int node)
{
	t->devices++;
}

static int __init sunzilog_ports_count(void)
{
	struct zs_probe_scan scan;

	/* Sun4 Zilog setup is hard coded, no probing to do.  */
	if (sparc_cpu_model == sun4)
		return 2;

	scan.scanner = sunzilog_scan_count;
	scan.depth = 0;
	scan.devices = 0;

	sunzilog_scan(&scan, prom_getchild(prom_root_node));

	return scan.devices;
}

static int __init sunzilog_init(void)
{

	NUM_SUNZILOG = sunzilog_ports_count();
	if (NUM_SUNZILOG == 0)
		return -ENODEV;

	sunzilog_alloc_tables();

	sunzilog_ports_init();

	return 0;
}

static void __exit sunzilog_exit(void)
{
	int i;

	for (i = 0; i < NUM_CHANNELS; i++) {
		struct uart_sunzilog_port *up = &sunzilog_port_table[i];

		if (ZS_IS_KEYB(up) || ZS_IS_MOUSE(up)) {
#ifdef CONFIG_SERIO
			if (up->serio) {
				serio_unregister_port(up->serio);
				up->serio = NULL;
			}
#endif
		} else
			uart_remove_one_port(&sunzilog_reg, &up->port);
	}

	uart_unregister_driver(&sunzilog_reg);
}

module_init(sunzilog_init);
module_exit(sunzilog_exit);

MODULE_AUTHOR("David S. Miller");
MODULE_DESCRIPTION("Sun Zilog serial port driver");
MODULE_LICENSE("GPL");
