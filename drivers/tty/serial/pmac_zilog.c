// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for PowerMac Z85c30 based ESCC cell found in the
 * "macio" ASICs of various PowerMac models
 * 
 * Copyright (C) 2003 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 * Derived from drivers/macintosh/macserial.c by Paul Mackerras
 * and drivers/serial/sunzilog.c by David S. Miller
 *
 * Hrm... actually, I ripped most of sunzilog (Thanks David !) and
 * adapted special tweaks needed for us. I don't think it's worth
 * merging back those though. The DMA code still has to get in
 * and once done, I expect that driver to remain fairly stable in
 * the long term, unless we change the driver model again...
 *
 * 2004-08-06 Harald Welte <laforge@gnumonks.org>
 *	- Enable BREAK interrupt
 *	- Add support for sysreq
 *
 * TODO:   - Add DMA support
 *         - Defer port shutdown to a few seconds after close
 *         - maybe put something right into uap->clk_divisor
 */

#undef DEBUG
#undef USE_CTRL_O_SYSRQ

#include <linux/module.h>
#include <linux/tty.h>

#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/bitops.h>
#include <linux/sysrq.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/sections.h>
#include <linux/io.h>
#include <asm/irq.h>

#ifdef CONFIG_PPC_PMAC
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/macio.h>
#else
#include <linux/platform_device.h>
#define of_machine_is_compatible(x) (0)
#endif

#include <linux/serial.h>
#include <linux/serial_core.h>

#include "pmac_zilog.h"

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Driver for the Mac and PowerMac serial ports.");
MODULE_LICENSE("GPL");

#ifdef CONFIG_SERIAL_PMACZILOG_TTYS
#define PMACZILOG_MAJOR		TTY_MAJOR
#define PMACZILOG_MINOR		64
#define PMACZILOG_NAME		"ttyS"
#else
#define PMACZILOG_MAJOR		204
#define PMACZILOG_MINOR		192
#define PMACZILOG_NAME		"ttyPZ"
#endif

#define pmz_debug(fmt, arg...)	pr_debug("ttyPZ%d: " fmt, uap->port.line, ## arg)
#define pmz_error(fmt, arg...)	pr_err("ttyPZ%d: " fmt, uap->port.line, ## arg)
#define pmz_info(fmt, arg...)	pr_info("ttyPZ%d: " fmt, uap->port.line, ## arg)

/*
 * For the sake of early serial console, we can do a pre-probe
 * (optional) of the ports at rather early boot time.
 */
static struct uart_pmac_port	pmz_ports[MAX_ZS_PORTS];
static int			pmz_ports_count;

static struct uart_driver pmz_uart_reg = {
	.owner		=	THIS_MODULE,
	.driver_name	=	PMACZILOG_NAME,
	.dev_name	=	PMACZILOG_NAME,
	.major		=	PMACZILOG_MAJOR,
	.minor		=	PMACZILOG_MINOR,
};


/* 
 * Load all registers to reprogram the port
 * This function must only be called when the TX is not busy.  The UART
 * port lock must be held and local interrupts disabled.
 */
static void pmz_load_zsregs(struct uart_pmac_port *uap, u8 *regs)
{
	int i;

	/* Let pending transmits finish.  */
	for (i = 0; i < 1000; i++) {
		unsigned char stat = read_zsreg(uap, R1);
		if (stat & ALL_SNT)
			break;
		udelay(100);
	}

	ZS_CLEARERR(uap);
	zssync(uap);
	ZS_CLEARFIFO(uap);
	zssync(uap);
	ZS_CLEARERR(uap);

	/* Disable all interrupts.  */
	write_zsreg(uap, R1,
		    regs[R1] & ~(RxINT_MASK | TxINT_ENAB | EXT_INT_ENAB));

	/* Set parity, sync config, stop bits, and clock divisor.  */
	write_zsreg(uap, R4, regs[R4]);

	/* Set misc. TX/RX control bits.  */
	write_zsreg(uap, R10, regs[R10]);

	/* Set TX/RX controls sans the enable bits.  */
	write_zsreg(uap, R3, regs[R3] & ~RxENABLE);
	write_zsreg(uap, R5, regs[R5] & ~TxENABLE);

	/* now set R7 "prime" on ESCC */
	write_zsreg(uap, R15, regs[R15] | EN85C30);
	write_zsreg(uap, R7, regs[R7P]);

	/* make sure we use R7 "non-prime" on ESCC */
	write_zsreg(uap, R15, regs[R15] & ~EN85C30);

	/* Synchronous mode config.  */
	write_zsreg(uap, R6, regs[R6]);
	write_zsreg(uap, R7, regs[R7]);

	/* Disable baud generator.  */
	write_zsreg(uap, R14, regs[R14] & ~BRENAB);

	/* Clock mode control.  */
	write_zsreg(uap, R11, regs[R11]);

	/* Lower and upper byte of baud rate generator divisor.  */
	write_zsreg(uap, R12, regs[R12]);
	write_zsreg(uap, R13, regs[R13]);
	
	/* Now rewrite R14, with BRENAB (if set).  */
	write_zsreg(uap, R14, regs[R14]);

	/* Reset external status interrupts.  */
	write_zsreg(uap, R0, RES_EXT_INT);
	write_zsreg(uap, R0, RES_EXT_INT);

	/* Rewrite R3/R5, this time without enables masked.  */
	write_zsreg(uap, R3, regs[R3]);
	write_zsreg(uap, R5, regs[R5]);

	/* Rewrite R1, this time without IRQ enabled masked.  */
	write_zsreg(uap, R1, regs[R1]);

	/* Enable interrupts */
	write_zsreg(uap, R9, regs[R9]);
}

/* 
 * We do like sunzilog to avoid disrupting pending Tx
 * Reprogram the Zilog channel HW registers with the copies found in the
 * software state struct.  If the transmitter is busy, we defer this update
 * until the next TX complete interrupt.  Else, we do it right now.
 *
 * The UART port lock must be held and local interrupts disabled.
 */
static void pmz_maybe_update_regs(struct uart_pmac_port *uap)
{
	if (!ZS_REGS_HELD(uap)) {
		if (ZS_TX_ACTIVE(uap)) {
			uap->flags |= PMACZILOG_FLAG_REGS_HELD;
		} else {
			pmz_debug("pmz: maybe_update_regs: updating\n");
			pmz_load_zsregs(uap, uap->curregs);
		}
	}
}

static void pmz_interrupt_control(struct uart_pmac_port *uap, int enable)
{
	if (enable) {
		uap->curregs[1] |= INT_ALL_Rx | TxINT_ENAB;
		if (!ZS_IS_EXTCLK(uap))
			uap->curregs[1] |= EXT_INT_ENAB;
	} else {
		uap->curregs[1] &= ~(EXT_INT_ENAB | TxINT_ENAB | RxINT_MASK);
	}
	write_zsreg(uap, R1, uap->curregs[1]);
}

static bool pmz_receive_chars(struct uart_pmac_port *uap)
	__must_hold(&uap->port.lock)
{
	struct tty_port *port;
	unsigned char ch, r1, drop, flag;

	/* Sanity check, make sure the old bug is no longer happening */
	if (uap->port.state == NULL) {
		WARN_ON(1);
		(void)read_zsdata(uap);
		return false;
	}
	port = &uap->port.state->port;

	while (1) {
		drop = 0;

		r1 = read_zsreg(uap, R1);
		ch = read_zsdata(uap);

		if (r1 & (PAR_ERR | Rx_OVR | CRC_ERR)) {
			write_zsreg(uap, R0, ERR_RES);
			zssync(uap);
		}

		ch &= uap->parity_mask;
		if (ch == 0 && uap->flags & PMACZILOG_FLAG_BREAK) {
			uap->flags &= ~PMACZILOG_FLAG_BREAK;
		}

#if defined(CONFIG_MAGIC_SYSRQ) && defined(CONFIG_SERIAL_CORE_CONSOLE)
#ifdef USE_CTRL_O_SYSRQ
		/* Handle the SysRq ^O Hack */
		if (ch == '\x0f') {
			uap->port.sysrq = jiffies + HZ*5;
			goto next_char;
		}
#endif /* USE_CTRL_O_SYSRQ */
		if (uap->port.sysrq) {
			int swallow;
			spin_unlock(&uap->port.lock);
			swallow = uart_handle_sysrq_char(&uap->port, ch);
			spin_lock(&uap->port.lock);
			if (swallow)
				goto next_char;
		}
#endif /* CONFIG_MAGIC_SYSRQ && CONFIG_SERIAL_CORE_CONSOLE */

		/* A real serial line, record the character and status.  */
		if (drop)
			goto next_char;

		flag = TTY_NORMAL;
		uap->port.icount.rx++;

		if (r1 & (PAR_ERR | Rx_OVR | CRC_ERR | BRK_ABRT)) {
			if (r1 & BRK_ABRT) {
				pmz_debug("pmz: got break !\n");
				r1 &= ~(PAR_ERR | CRC_ERR);
				uap->port.icount.brk++;
				if (uart_handle_break(&uap->port))
					goto next_char;
			}
			else if (r1 & PAR_ERR)
				uap->port.icount.parity++;
			else if (r1 & CRC_ERR)
				uap->port.icount.frame++;
			if (r1 & Rx_OVR)
				uap->port.icount.overrun++;
			r1 &= uap->port.read_status_mask;
			if (r1 & BRK_ABRT)
				flag = TTY_BREAK;
			else if (r1 & PAR_ERR)
				flag = TTY_PARITY;
			else if (r1 & CRC_ERR)
				flag = TTY_FRAME;
		}

		if (uap->port.ignore_status_mask == 0xff ||
		    (r1 & uap->port.ignore_status_mask) == 0) {
			tty_insert_flip_char(port, ch, flag);
		}
		if (r1 & Rx_OVR)
			tty_insert_flip_char(port, 0, TTY_OVERRUN);
	next_char:
		ch = read_zsreg(uap, R0);
		if (!(ch & Rx_CH_AV))
			break;
	}

	return true;
}

static void pmz_status_handle(struct uart_pmac_port *uap)
{
	unsigned char status;

	status = read_zsreg(uap, R0);
	write_zsreg(uap, R0, RES_EXT_INT);
	zssync(uap);

	if (ZS_IS_OPEN(uap) && ZS_WANTS_MODEM_STATUS(uap)) {
		if (status & SYNC_HUNT)
			uap->port.icount.dsr++;

		/* The Zilog just gives us an interrupt when DCD/CTS/etc. change.
		 * But it does not tell us which bit has changed, we have to keep
		 * track of this ourselves.
		 * The CTS input is inverted for some reason.  -- paulus
		 */
		if ((status ^ uap->prev_status) & DCD)
			uart_handle_dcd_change(&uap->port,
					       (status & DCD));
		if ((status ^ uap->prev_status) & CTS)
			uart_handle_cts_change(&uap->port,
					       !(status & CTS));

		wake_up_interruptible(&uap->port.state->port.delta_msr_wait);
	}

	if (status & BRK_ABRT)
		uap->flags |= PMACZILOG_FLAG_BREAK;

	uap->prev_status = status;
}

static void pmz_transmit_chars(struct uart_pmac_port *uap)
{
	struct circ_buf *xmit;

	if (ZS_IS_CONS(uap)) {
		unsigned char status = read_zsreg(uap, R0);

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

	uap->flags &= ~PMACZILOG_FLAG_TX_ACTIVE;

	if (ZS_REGS_HELD(uap)) {
		pmz_load_zsregs(uap, uap->curregs);
		uap->flags &= ~PMACZILOG_FLAG_REGS_HELD;
	}

	if (ZS_TX_STOPPED(uap)) {
		uap->flags &= ~PMACZILOG_FLAG_TX_STOPPED;
		goto ack_tx_int;
	}

	/* Under some circumstances, we see interrupts reported for
	 * a closed channel. The interrupt mask in R1 is clear, but
	 * R3 still signals the interrupts and we see them when taking
	 * an interrupt for the other channel (this could be a qemu
	 * bug but since the ESCC doc doesn't specify precsiely whether
	 * R3 interrup status bits are masked by R1 interrupt enable
	 * bits, better safe than sorry). --BenH.
	 */
	if (!ZS_IS_OPEN(uap))
		goto ack_tx_int;

	if (uap->port.x_char) {
		uap->flags |= PMACZILOG_FLAG_TX_ACTIVE;
		write_zsdata(uap, uap->port.x_char);
		zssync(uap);
		uap->port.icount.tx++;
		uap->port.x_char = 0;
		return;
	}

	if (uap->port.state == NULL)
		goto ack_tx_int;
	xmit = &uap->port.state->xmit;
	if (uart_circ_empty(xmit)) {
		uart_write_wakeup(&uap->port);
		goto ack_tx_int;
	}
	if (uart_tx_stopped(&uap->port))
		goto ack_tx_int;

	uap->flags |= PMACZILOG_FLAG_TX_ACTIVE;
	write_zsdata(uap, xmit->buf[xmit->tail]);
	zssync(uap);

	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	uap->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&uap->port);

	return;

ack_tx_int:
	write_zsreg(uap, R0, RES_Tx_P);
	zssync(uap);
}

/* Hrm... we register that twice, fixme later.... */
static irqreturn_t pmz_interrupt(int irq, void *dev_id)
{
	struct uart_pmac_port *uap = dev_id;
	struct uart_pmac_port *uap_a;
	struct uart_pmac_port *uap_b;
	int rc = IRQ_NONE;
	bool push;
	u8 r3;

	uap_a = pmz_get_port_A(uap);
	uap_b = uap_a->mate;

	spin_lock(&uap_a->port.lock);
	r3 = read_zsreg(uap_a, R3);

	/* Channel A */
	push = false;
	if (r3 & (CHAEXT | CHATxIP | CHARxIP)) {
		if (!ZS_IS_OPEN(uap_a)) {
			pmz_debug("ChanA interrupt while not open !\n");
			goto skip_a;
		}
		write_zsreg(uap_a, R0, RES_H_IUS);
		zssync(uap_a);		
		if (r3 & CHAEXT)
			pmz_status_handle(uap_a);
		if (r3 & CHARxIP)
			push = pmz_receive_chars(uap_a);
		if (r3 & CHATxIP)
			pmz_transmit_chars(uap_a);
		rc = IRQ_HANDLED;
	}
 skip_a:
	spin_unlock(&uap_a->port.lock);
	if (push)
		tty_flip_buffer_push(&uap->port.state->port);

	if (!uap_b)
		goto out;

	spin_lock(&uap_b->port.lock);
	push = false;
	if (r3 & (CHBEXT | CHBTxIP | CHBRxIP)) {
		if (!ZS_IS_OPEN(uap_b)) {
			pmz_debug("ChanB interrupt while not open !\n");
			goto skip_b;
		}
		write_zsreg(uap_b, R0, RES_H_IUS);
		zssync(uap_b);
		if (r3 & CHBEXT)
			pmz_status_handle(uap_b);
		if (r3 & CHBRxIP)
			push = pmz_receive_chars(uap_b);
		if (r3 & CHBTxIP)
			pmz_transmit_chars(uap_b);
		rc = IRQ_HANDLED;
	}
 skip_b:
	spin_unlock(&uap_b->port.lock);
	if (push)
		tty_flip_buffer_push(&uap->port.state->port);

 out:
	return rc;
}

/*
 * Peek the status register, lock not held by caller
 */
static inline u8 pmz_peek_status(struct uart_pmac_port *uap)
{
	unsigned long flags;
	u8 status;
	
	spin_lock_irqsave(&uap->port.lock, flags);
	status = read_zsreg(uap, R0);
	spin_unlock_irqrestore(&uap->port.lock, flags);

	return status;
}

/* 
 * Check if transmitter is empty
 * The port lock is not held.
 */
static unsigned int pmz_tx_empty(struct uart_port *port)
{
	unsigned char status;

	status = pmz_peek_status(to_pmz(port));
	if (status & Tx_BUF_EMP)
		return TIOCSER_TEMT;
	return 0;
}

/* 
 * Set Modem Control (RTS & DTR) bits
 * The port lock is held and interrupts are disabled.
 * Note: Shall we really filter out RTS on external ports or
 * should that be dealt at higher level only ?
 */
static void pmz_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned char set_bits, clear_bits;

        /* Do nothing for irda for now... */
	if (ZS_IS_IRDA(uap))
		return;
	/* We get called during boot with a port not up yet */
	if (!(ZS_IS_OPEN(uap) || ZS_IS_CONS(uap)))
		return;

	set_bits = clear_bits = 0;

	if (ZS_IS_INTMODEM(uap)) {
		if (mctrl & TIOCM_RTS)
			set_bits |= RTS;
		else
			clear_bits |= RTS;
	}
	if (mctrl & TIOCM_DTR)
		set_bits |= DTR;
	else
		clear_bits |= DTR;

	/* NOTE: Not subject to 'transmitter active' rule.  */ 
	uap->curregs[R5] |= set_bits;
	uap->curregs[R5] &= ~clear_bits;

	write_zsreg(uap, R5, uap->curregs[R5]);
	pmz_debug("pmz_set_mctrl: set bits: %x, clear bits: %x -> %x\n",
		  set_bits, clear_bits, uap->curregs[R5]);
	zssync(uap);
}

/* 
 * Get Modem Control bits (only the input ones, the core will
 * or that with a cached value of the control ones)
 * The port lock is held and interrupts are disabled.
 */
static unsigned int pmz_get_mctrl(struct uart_port *port)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned char status;
	unsigned int ret;

	status = read_zsreg(uap, R0);

	ret = 0;
	if (status & DCD)
		ret |= TIOCM_CAR;
	if (status & SYNC_HUNT)
		ret |= TIOCM_DSR;
	if (!(status & CTS))
		ret |= TIOCM_CTS;

	return ret;
}

/* 
 * Stop TX side. Dealt like sunzilog at next Tx interrupt,
 * though for DMA, we will have to do a bit more.
 * The port lock is held and interrupts are disabled.
 */
static void pmz_stop_tx(struct uart_port *port)
{
	to_pmz(port)->flags |= PMACZILOG_FLAG_TX_STOPPED;
}

/* 
 * Kick the Tx side.
 * The port lock is held and interrupts are disabled.
 */
static void pmz_start_tx(struct uart_port *port)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned char status;

	uap->flags |= PMACZILOG_FLAG_TX_ACTIVE;
	uap->flags &= ~PMACZILOG_FLAG_TX_STOPPED;

	status = read_zsreg(uap, R0);

	/* TX busy?  Just wait for the TX done interrupt.  */
	if (!(status & Tx_BUF_EMP))
		return;

	/* Send the first character to jump-start the TX done
	 * IRQ sending engine.
	 */
	if (port->x_char) {
		write_zsdata(uap, port->x_char);
		zssync(uap);
		port->icount.tx++;
		port->x_char = 0;
	} else {
		struct circ_buf *xmit = &port->state->xmit;

		if (uart_circ_empty(xmit))
			return;
		write_zsdata(uap, xmit->buf[xmit->tail]);
		zssync(uap);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_write_wakeup(&uap->port);
	}
}

/* 
 * Stop Rx side, basically disable emitting of
 * Rx interrupts on the port. We don't disable the rx
 * side of the chip proper though
 * The port lock is held.
 */
static void pmz_stop_rx(struct uart_port *port)
{
	struct uart_pmac_port *uap = to_pmz(port);

	/* Disable all RX interrupts.  */
	uap->curregs[R1] &= ~RxINT_MASK;
	pmz_maybe_update_regs(uap);
}

/* 
 * Enable modem status change interrupts
 * The port lock is held.
 */
static void pmz_enable_ms(struct uart_port *port)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned char new_reg;

	if (ZS_IS_IRDA(uap))
		return;
	new_reg = uap->curregs[R15] | (DCDIE | SYNCIE | CTSIE);
	if (new_reg != uap->curregs[R15]) {
		uap->curregs[R15] = new_reg;

		/* NOTE: Not subject to 'transmitter active' rule. */
		write_zsreg(uap, R15, uap->curregs[R15]);
	}
}

/* 
 * Control break state emission
 * The port lock is not held.
 */
static void pmz_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned char set_bits, clear_bits, new_reg;
	unsigned long flags;

	set_bits = clear_bits = 0;

	if (break_state)
		set_bits |= SND_BRK;
	else
		clear_bits |= SND_BRK;

	spin_lock_irqsave(&port->lock, flags);

	new_reg = (uap->curregs[R5] | set_bits) & ~clear_bits;
	if (new_reg != uap->curregs[R5]) {
		uap->curregs[R5] = new_reg;
		write_zsreg(uap, R5, uap->curregs[R5]);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

#ifdef CONFIG_PPC_PMAC

/*
 * Turn power on or off to the SCC and associated stuff
 * (port drivers, modem, IR port, etc.)
 * Returns the number of milliseconds we should wait before
 * trying to use the port.
 */
static int pmz_set_scc_power(struct uart_pmac_port *uap, int state)
{
	int delay = 0;
	int rc;

	if (state) {
		rc = pmac_call_feature(
			PMAC_FTR_SCC_ENABLE, uap->node, uap->port_type, 1);
		pmz_debug("port power on result: %d\n", rc);
		if (ZS_IS_INTMODEM(uap)) {
			rc = pmac_call_feature(
				PMAC_FTR_MODEM_ENABLE, uap->node, 0, 1);
			delay = 2500;	/* wait for 2.5s before using */
			pmz_debug("modem power result: %d\n", rc);
		}
	} else {
		/* TODO: Make that depend on a timer, don't power down
		 * immediately
		 */
		if (ZS_IS_INTMODEM(uap)) {
			rc = pmac_call_feature(
				PMAC_FTR_MODEM_ENABLE, uap->node, 0, 0);
			pmz_debug("port power off result: %d\n", rc);
		}
		pmac_call_feature(PMAC_FTR_SCC_ENABLE, uap->node, uap->port_type, 0);
	}
	return delay;
}

#else

static int pmz_set_scc_power(struct uart_pmac_port *uap, int state)
{
	return 0;
}

#endif /* !CONFIG_PPC_PMAC */

/*
 * FixZeroBug....Works around a bug in the SCC receiving channel.
 * Inspired from Darwin code, 15 Sept. 2000  -DanM
 *
 * The following sequence prevents a problem that is seen with O'Hare ASICs
 * (most versions -- also with some Heathrow and Hydra ASICs) where a zero
 * at the input to the receiver becomes 'stuck' and locks up the receiver.
 * This problem can occur as a result of a zero bit at the receiver input
 * coincident with any of the following events:
 *
 *	The SCC is initialized (hardware or software).
 *	A framing error is detected.
 *	The clocking option changes from synchronous or X1 asynchronous
 *		clocking to X16, X32, or X64 asynchronous clocking.
 *	The decoding mode is changed among NRZ, NRZI, FM0, or FM1.
 *
 * This workaround attempts to recover from the lockup condition by placing
 * the SCC in synchronous loopback mode with a fast clock before programming
 * any of the asynchronous modes.
 */
static void pmz_fix_zero_bug_scc(struct uart_pmac_port *uap)
{
	write_zsreg(uap, 9, ZS_IS_CHANNEL_A(uap) ? CHRA : CHRB);
	zssync(uap);
	udelay(10);
	write_zsreg(uap, 9, (ZS_IS_CHANNEL_A(uap) ? CHRA : CHRB) | NV);
	zssync(uap);

	write_zsreg(uap, 4, X1CLK | MONSYNC);
	write_zsreg(uap, 3, Rx8);
	write_zsreg(uap, 5, Tx8 | RTS);
	write_zsreg(uap, 9, NV);	/* Didn't we already do this? */
	write_zsreg(uap, 11, RCBR | TCBR);
	write_zsreg(uap, 12, 0);
	write_zsreg(uap, 13, 0);
	write_zsreg(uap, 14, (LOOPBAK | BRSRC));
	write_zsreg(uap, 14, (LOOPBAK | BRSRC | BRENAB));
	write_zsreg(uap, 3, Rx8 | RxENABLE);
	write_zsreg(uap, 0, RES_EXT_INT);
	write_zsreg(uap, 0, RES_EXT_INT);
	write_zsreg(uap, 0, RES_EXT_INT);	/* to kill some time */

	/* The channel should be OK now, but it is probably receiving
	 * loopback garbage.
	 * Switch to asynchronous mode, disable the receiver,
	 * and discard everything in the receive buffer.
	 */
	write_zsreg(uap, 9, NV);
	write_zsreg(uap, 4, X16CLK | SB_MASK);
	write_zsreg(uap, 3, Rx8);

	while (read_zsreg(uap, 0) & Rx_CH_AV) {
		(void)read_zsreg(uap, 8);
		write_zsreg(uap, 0, RES_EXT_INT);
		write_zsreg(uap, 0, ERR_RES);
	}
}

/*
 * Real startup routine, powers up the hardware and sets up
 * the SCC. Returns a delay in ms where you need to wait before
 * actually using the port, this is typically the internal modem
 * powerup delay. This routine expect the lock to be taken.
 */
static int __pmz_startup(struct uart_pmac_port *uap)
{
	int pwr_delay = 0;

	memset(&uap->curregs, 0, sizeof(uap->curregs));

	/* Power up the SCC & underlying hardware (modem/irda) */
	pwr_delay = pmz_set_scc_power(uap, 1);

	/* Nice buggy HW ... */
	pmz_fix_zero_bug_scc(uap);

	/* Reset the channel */
	uap->curregs[R9] = 0;
	write_zsreg(uap, 9, ZS_IS_CHANNEL_A(uap) ? CHRA : CHRB);
	zssync(uap);
	udelay(10);
	write_zsreg(uap, 9, 0);
	zssync(uap);

	/* Clear the interrupt registers */
	write_zsreg(uap, R1, 0);
	write_zsreg(uap, R0, ERR_RES);
	write_zsreg(uap, R0, ERR_RES);
	write_zsreg(uap, R0, RES_H_IUS);
	write_zsreg(uap, R0, RES_H_IUS);

	/* Setup some valid baud rate */
	uap->curregs[R4] = X16CLK | SB1;
	uap->curregs[R3] = Rx8;
	uap->curregs[R5] = Tx8 | RTS;
	if (!ZS_IS_IRDA(uap))
		uap->curregs[R5] |= DTR;
	uap->curregs[R12] = 0;
	uap->curregs[R13] = 0;
	uap->curregs[R14] = BRENAB;

	/* Clear handshaking, enable BREAK interrupts */
	uap->curregs[R15] = BRKIE;

	/* Master interrupt enable */
	uap->curregs[R9] |= NV | MIE;

	pmz_load_zsregs(uap, uap->curregs);

	/* Enable receiver and transmitter.  */
	write_zsreg(uap, R3, uap->curregs[R3] |= RxENABLE);
	write_zsreg(uap, R5, uap->curregs[R5] |= TxENABLE);

	/* Remember status for DCD/CTS changes */
	uap->prev_status = read_zsreg(uap, R0);

	return pwr_delay;
}

static void pmz_irda_reset(struct uart_pmac_port *uap)
{
	unsigned long flags;

	spin_lock_irqsave(&uap->port.lock, flags);
	uap->curregs[R5] |= DTR;
	write_zsreg(uap, R5, uap->curregs[R5]);
	zssync(uap);
	spin_unlock_irqrestore(&uap->port.lock, flags);
	msleep(110);

	spin_lock_irqsave(&uap->port.lock, flags);
	uap->curregs[R5] &= ~DTR;
	write_zsreg(uap, R5, uap->curregs[R5]);
	zssync(uap);
	spin_unlock_irqrestore(&uap->port.lock, flags);
	msleep(10);
}

/*
 * This is the "normal" startup routine, using the above one
 * wrapped with the lock and doing a schedule delay
 */
static int pmz_startup(struct uart_port *port)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned long flags;
	int pwr_delay = 0;

	uap->flags |= PMACZILOG_FLAG_IS_OPEN;

	/* A console is never powered down. Else, power up and
	 * initialize the chip
	 */
	if (!ZS_IS_CONS(uap)) {
		spin_lock_irqsave(&port->lock, flags);
		pwr_delay = __pmz_startup(uap);
		spin_unlock_irqrestore(&port->lock, flags);
	}	
	sprintf(uap->irq_name, PMACZILOG_NAME"%d", uap->port.line);
	if (request_irq(uap->port.irq, pmz_interrupt, IRQF_SHARED,
			uap->irq_name, uap)) {
		pmz_error("Unable to register zs interrupt handler.\n");
		pmz_set_scc_power(uap, 0);
		return -ENXIO;
	}

	/* Right now, we deal with delay by blocking here, I'll be
	 * smarter later on
	 */
	if (pwr_delay != 0) {
		pmz_debug("pmz: delaying %d ms\n", pwr_delay);
		msleep(pwr_delay);
	}

	/* IrDA reset is done now */
	if (ZS_IS_IRDA(uap))
		pmz_irda_reset(uap);

	/* Enable interrupt requests for the channel */
	spin_lock_irqsave(&port->lock, flags);
	pmz_interrupt_control(uap, 1);
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static void pmz_shutdown(struct uart_port *port)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* Disable interrupt requests for the channel */
	pmz_interrupt_control(uap, 0);

	if (!ZS_IS_CONS(uap)) {
		/* Disable receiver and transmitter */
		uap->curregs[R3] &= ~RxENABLE;
		uap->curregs[R5] &= ~TxENABLE;

		/* Disable break assertion */
		uap->curregs[R5] &= ~SND_BRK;
		pmz_maybe_update_regs(uap);
	}

	spin_unlock_irqrestore(&port->lock, flags);

	/* Release interrupt handler */
	free_irq(uap->port.irq, uap);

	spin_lock_irqsave(&port->lock, flags);

	uap->flags &= ~PMACZILOG_FLAG_IS_OPEN;

	if (!ZS_IS_CONS(uap))
		pmz_set_scc_power(uap, 0);	/* Shut the chip down */

	spin_unlock_irqrestore(&port->lock, flags);
}

/* Shared by TTY driver and serial console setup.  The port lock is held
 * and local interrupts are disabled.
 */
static void pmz_convert_to_zs(struct uart_pmac_port *uap, unsigned int cflag,
			      unsigned int iflag, unsigned long baud)
{
	int brg;

	/* Switch to external clocking for IrDA high clock rates. That
	 * code could be re-used for Midi interfaces with different
	 * multipliers
	 */
	if (baud >= 115200 && ZS_IS_IRDA(uap)) {
		uap->curregs[R4] = X1CLK;
		uap->curregs[R11] = RCTRxCP | TCTRxCP;
		uap->curregs[R14] = 0; /* BRG off */
		uap->curregs[R12] = 0;
		uap->curregs[R13] = 0;
		uap->flags |= PMACZILOG_FLAG_IS_EXTCLK;
	} else {
		switch (baud) {
		case ZS_CLOCK/16:	/* 230400 */
			uap->curregs[R4] = X16CLK;
			uap->curregs[R11] = 0;
			uap->curregs[R14] = 0;
			break;
		case ZS_CLOCK/32:	/* 115200 */
			uap->curregs[R4] = X32CLK;
			uap->curregs[R11] = 0;
			uap->curregs[R14] = 0;
			break;
		default:
			uap->curregs[R4] = X16CLK;
			uap->curregs[R11] = TCBR | RCBR;
			brg = BPS_TO_BRG(baud, ZS_CLOCK / 16);
			uap->curregs[R12] = (brg & 255);
			uap->curregs[R13] = ((brg >> 8) & 255);
			uap->curregs[R14] = BRENAB;
		}
		uap->flags &= ~PMACZILOG_FLAG_IS_EXTCLK;
	}

	/* Character size, stop bits, and parity. */
	uap->curregs[3] &= ~RxN_MASK;
	uap->curregs[5] &= ~TxN_MASK;

	switch (cflag & CSIZE) {
	case CS5:
		uap->curregs[3] |= Rx5;
		uap->curregs[5] |= Tx5;
		uap->parity_mask = 0x1f;
		break;
	case CS6:
		uap->curregs[3] |= Rx6;
		uap->curregs[5] |= Tx6;
		uap->parity_mask = 0x3f;
		break;
	case CS7:
		uap->curregs[3] |= Rx7;
		uap->curregs[5] |= Tx7;
		uap->parity_mask = 0x7f;
		break;
	case CS8:
	default:
		uap->curregs[3] |= Rx8;
		uap->curregs[5] |= Tx8;
		uap->parity_mask = 0xff;
		break;
	}
	uap->curregs[4] &= ~(SB_MASK);
	if (cflag & CSTOPB)
		uap->curregs[4] |= SB2;
	else
		uap->curregs[4] |= SB1;
	if (cflag & PARENB)
		uap->curregs[4] |= PAR_ENAB;
	else
		uap->curregs[4] &= ~PAR_ENAB;
	if (!(cflag & PARODD))
		uap->curregs[4] |= PAR_EVEN;
	else
		uap->curregs[4] &= ~PAR_EVEN;

	uap->port.read_status_mask = Rx_OVR;
	if (iflag & INPCK)
		uap->port.read_status_mask |= CRC_ERR | PAR_ERR;
	if (iflag & (IGNBRK | BRKINT | PARMRK))
		uap->port.read_status_mask |= BRK_ABRT;

	uap->port.ignore_status_mask = 0;
	if (iflag & IGNPAR)
		uap->port.ignore_status_mask |= CRC_ERR | PAR_ERR;
	if (iflag & IGNBRK) {
		uap->port.ignore_status_mask |= BRK_ABRT;
		if (iflag & IGNPAR)
			uap->port.ignore_status_mask |= Rx_OVR;
	}

	if ((cflag & CREAD) == 0)
		uap->port.ignore_status_mask = 0xff;
}


/*
 * Set the irda codec on the imac to the specified baud rate.
 */
static void pmz_irda_setup(struct uart_pmac_port *uap, unsigned long *baud)
{
	u8 cmdbyte;
	int t, version;

	switch (*baud) {
	/* SIR modes */
	case 2400:
		cmdbyte = 0x53;
		break;
	case 4800:
		cmdbyte = 0x52;
		break;
	case 9600:
		cmdbyte = 0x51;
		break;
	case 19200:
		cmdbyte = 0x50;
		break;
	case 38400:
		cmdbyte = 0x4f;
		break;
	case 57600:
		cmdbyte = 0x4e;
		break;
	case 115200:
		cmdbyte = 0x4d;
		break;
	/* The FIR modes aren't really supported at this point, how
	 * do we select the speed ? via the FCR on KeyLargo ?
	 */
	case 1152000:
		cmdbyte = 0;
		break;
	case 4000000:
		cmdbyte = 0;
		break;
	default: /* 9600 */
		cmdbyte = 0x51;
		*baud = 9600;
		break;
	}

	/* Wait for transmitter to drain */
	t = 10000;
	while ((read_zsreg(uap, R0) & Tx_BUF_EMP) == 0
	       || (read_zsreg(uap, R1) & ALL_SNT) == 0) {
		if (--t <= 0) {
			pmz_error("transmitter didn't drain\n");
			return;
		}
		udelay(10);
	}

	/* Drain the receiver too */
	t = 100;
	(void)read_zsdata(uap);
	(void)read_zsdata(uap);
	(void)read_zsdata(uap);
	mdelay(10);
	while (read_zsreg(uap, R0) & Rx_CH_AV) {
		read_zsdata(uap);
		mdelay(10);
		if (--t <= 0) {
			pmz_error("receiver didn't drain\n");
			return;
		}
	}

	/* Switch to command mode */
	uap->curregs[R5] |= DTR;
	write_zsreg(uap, R5, uap->curregs[R5]);
	zssync(uap);
	mdelay(1);

	/* Switch SCC to 19200 */
	pmz_convert_to_zs(uap, CS8, 0, 19200);		
	pmz_load_zsregs(uap, uap->curregs);
	mdelay(1);

	/* Write get_version command byte */
	write_zsdata(uap, 1);
	t = 5000;
	while ((read_zsreg(uap, R0) & Rx_CH_AV) == 0) {
		if (--t <= 0) {
			pmz_error("irda_setup timed out on get_version byte\n");
			goto out;
		}
		udelay(10);
	}
	version = read_zsdata(uap);

	if (version < 4) {
		pmz_info("IrDA: dongle version %d not supported\n", version);
		goto out;
	}

	/* Send speed mode */
	write_zsdata(uap, cmdbyte);
	t = 5000;
	while ((read_zsreg(uap, R0) & Rx_CH_AV) == 0) {
		if (--t <= 0) {
			pmz_error("irda_setup timed out on speed mode byte\n");
			goto out;
		}
		udelay(10);
	}
	t = read_zsdata(uap);
	if (t != cmdbyte)
		pmz_error("irda_setup speed mode byte = %x (%x)\n", t, cmdbyte);

	pmz_info("IrDA setup for %ld bps, dongle version: %d\n",
		 *baud, version);

	(void)read_zsdata(uap);
	(void)read_zsdata(uap);
	(void)read_zsdata(uap);

 out:
	/* Switch back to data mode */
	uap->curregs[R5] &= ~DTR;
	write_zsreg(uap, R5, uap->curregs[R5]);
	zssync(uap);

	(void)read_zsdata(uap);
	(void)read_zsdata(uap);
	(void)read_zsdata(uap);
}


static void __pmz_set_termios(struct uart_port *port, struct ktermios *termios,
			      const struct ktermios *old)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned long baud;

	/* XXX Check which revs of machines actually allow 1 and 4Mb speeds
	 * on the IR dongle. Note that the IRTTY driver currently doesn't know
	 * about the FIR mode and high speed modes. So these are unused. For
	 * implementing proper support for these, we should probably add some
	 * DMA as well, at least on the Rx side, which isn't a simple thing
	 * at this point.
	 */
	if (ZS_IS_IRDA(uap)) {
		/* Calc baud rate */
		baud = uart_get_baud_rate(port, termios, old, 1200, 4000000);
		pmz_debug("pmz: switch IRDA to %ld bauds\n", baud);
		/* Cet the irda codec to the right rate */
		pmz_irda_setup(uap, &baud);
		/* Set final baud rate */
		pmz_convert_to_zs(uap, termios->c_cflag, termios->c_iflag, baud);
		pmz_load_zsregs(uap, uap->curregs);
		zssync(uap);
	} else {
		baud = uart_get_baud_rate(port, termios, old, 1200, 230400);
		pmz_convert_to_zs(uap, termios->c_cflag, termios->c_iflag, baud);
		/* Make sure modem status interrupts are correctly configured */
		if (UART_ENABLE_MS(&uap->port, termios->c_cflag)) {
			uap->curregs[R15] |= DCDIE | SYNCIE | CTSIE;
			uap->flags |= PMACZILOG_FLAG_MODEM_STATUS;
		} else {
			uap->curregs[R15] &= ~(DCDIE | SYNCIE | CTSIE);
			uap->flags &= ~PMACZILOG_FLAG_MODEM_STATUS;
		}

		/* Load registers to the chip */
		pmz_maybe_update_regs(uap);
	}
	uart_update_timeout(port, termios->c_cflag, baud);
}

/* The port lock is not held.  */
static void pmz_set_termios(struct uart_port *port, struct ktermios *termios,
			    const struct ktermios *old)
{
	struct uart_pmac_port *uap = to_pmz(port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);	

	/* Disable IRQs on the port */
	pmz_interrupt_control(uap, 0);

	/* Setup new port configuration */
	__pmz_set_termios(port, termios, old);

	/* Re-enable IRQs on the port */
	if (ZS_IS_OPEN(uap))
		pmz_interrupt_control(uap, 1);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *pmz_type(struct uart_port *port)
{
	struct uart_pmac_port *uap = to_pmz(port);

	if (ZS_IS_IRDA(uap))
		return "Z85c30 ESCC - Infrared port";
	else if (ZS_IS_INTMODEM(uap))
		return "Z85c30 ESCC - Internal modem";
	return "Z85c30 ESCC - Serial port";
}

/* We do not request/release mappings of the registers here, this
 * happens at early serial probe time.
 */
static void pmz_release_port(struct uart_port *port)
{
}

static int pmz_request_port(struct uart_port *port)
{
	return 0;
}

/* These do not need to do anything interesting either.  */
static void pmz_config_port(struct uart_port *port, int flags)
{
}

/* We do not support letting the user mess with the divisor, IRQ, etc. */
static int pmz_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return -EINVAL;
}

#ifdef CONFIG_CONSOLE_POLL

static int pmz_poll_get_char(struct uart_port *port)
{
	struct uart_pmac_port *uap =
		container_of(port, struct uart_pmac_port, port);
	int tries = 2;

	while (tries) {
		if ((read_zsreg(uap, R0) & Rx_CH_AV) != 0)
			return read_zsdata(uap);
		if (tries--)
			udelay(5);
	}

	return NO_POLL_CHAR;
}

static void pmz_poll_put_char(struct uart_port *port, unsigned char c)
{
	struct uart_pmac_port *uap =
		container_of(port, struct uart_pmac_port, port);

	/* Wait for the transmit buffer to empty. */
	while ((read_zsreg(uap, R0) & Tx_BUF_EMP) == 0)
		udelay(5);
	write_zsdata(uap, c);
}

#endif /* CONFIG_CONSOLE_POLL */

static const struct uart_ops pmz_pops = {
	.tx_empty	=	pmz_tx_empty,
	.set_mctrl	=	pmz_set_mctrl,
	.get_mctrl	=	pmz_get_mctrl,
	.stop_tx	=	pmz_stop_tx,
	.start_tx	=	pmz_start_tx,
	.stop_rx	=	pmz_stop_rx,
	.enable_ms	=	pmz_enable_ms,
	.break_ctl	=	pmz_break_ctl,
	.startup	=	pmz_startup,
	.shutdown	=	pmz_shutdown,
	.set_termios	=	pmz_set_termios,
	.type		=	pmz_type,
	.release_port	=	pmz_release_port,
	.request_port	=	pmz_request_port,
	.config_port	=	pmz_config_port,
	.verify_port	=	pmz_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	=	pmz_poll_get_char,
	.poll_put_char	=	pmz_poll_put_char,
#endif
};

#ifdef CONFIG_PPC_PMAC

/*
 * Setup one port structure after probing, HW is down at this point,
 * Unlike sunzilog, we don't need to pre-init the spinlock as we don't
 * register our console before uart_add_one_port() is called
 */
static int __init pmz_init_port(struct uart_pmac_port *uap)
{
	struct device_node *np = uap->node;
	const char *conn;
	const struct slot_names_prop {
		int	count;
		char	name[1];
	} *slots;
	int len;
	struct resource r_ports;

	/*
	 * Request & map chip registers
	 */
	if (of_address_to_resource(np, 0, &r_ports))
		return -ENODEV;
	uap->port.mapbase = r_ports.start;
	uap->port.membase = ioremap(uap->port.mapbase, 0x1000);

	uap->control_reg = uap->port.membase;
	uap->data_reg = uap->control_reg + 0x10;

	/*
	 * Detect port type
	 */
	if (of_device_is_compatible(np, "cobalt"))
		uap->flags |= PMACZILOG_FLAG_IS_INTMODEM;
	conn = of_get_property(np, "AAPL,connector", &len);
	if (conn && (strcmp(conn, "infrared") == 0))
		uap->flags |= PMACZILOG_FLAG_IS_IRDA;
	uap->port_type = PMAC_SCC_ASYNC;
	/* 1999 Powerbook G3 has slot-names property instead */
	slots = of_get_property(np, "slot-names", &len);
	if (slots && slots->count > 0) {
		if (strcmp(slots->name, "IrDA") == 0)
			uap->flags |= PMACZILOG_FLAG_IS_IRDA;
		else if (strcmp(slots->name, "Modem") == 0)
			uap->flags |= PMACZILOG_FLAG_IS_INTMODEM;
	}
	if (ZS_IS_IRDA(uap))
		uap->port_type = PMAC_SCC_IRDA;
	if (ZS_IS_INTMODEM(uap)) {
		struct device_node* i2c_modem =
			of_find_node_by_name(NULL, "i2c-modem");
		if (i2c_modem) {
			const char* mid =
				of_get_property(i2c_modem, "modem-id", NULL);
			if (mid) switch(*mid) {
			case 0x04 :
			case 0x05 :
			case 0x07 :
			case 0x08 :
			case 0x0b :
			case 0x0c :
				uap->port_type = PMAC_SCC_I2S1;
			}
			printk(KERN_INFO "pmac_zilog: i2c-modem detected, id: %d\n",
				mid ? (*mid) : 0);
			of_node_put(i2c_modem);
		} else {
			printk(KERN_INFO "pmac_zilog: serial modem detected\n");
		}
	}

	/*
	 * Init remaining bits of "port" structure
	 */
	uap->port.iotype = UPIO_MEM;
	uap->port.irq = irq_of_parse_and_map(np, 0);
	uap->port.uartclk = ZS_CLOCK;
	uap->port.fifosize = 1;
	uap->port.ops = &pmz_pops;
	uap->port.type = PORT_PMAC_ZILOG;
	uap->port.flags = 0;

	/*
	 * Fixup for the port on Gatwick for which the device-tree has
	 * missing interrupts. Normally, the macio_dev would contain
	 * fixed up interrupt info, but we use the device-tree directly
	 * here due to early probing so we need the fixup too.
	 */
	if (uap->port.irq == 0 &&
	    np->parent && np->parent->parent &&
	    of_device_is_compatible(np->parent->parent, "gatwick")) {
		/* IRQs on gatwick are offset by 64 */
		uap->port.irq = irq_create_mapping(NULL, 64 + 15);
	}

	/* Setup some valid baud rate information in the register
	 * shadows so we don't write crap there before baud rate is
	 * first initialized.
	 */
	pmz_convert_to_zs(uap, CS8, 0, 9600);

	return 0;
}

/*
 * Get rid of a port on module removal
 */
static void pmz_dispose_port(struct uart_pmac_port *uap)
{
	struct device_node *np;

	np = uap->node;
	iounmap(uap->control_reg);
	uap->node = NULL;
	of_node_put(np);
	memset(uap, 0, sizeof(struct uart_pmac_port));
}

/*
 * Called upon match with an escc node in the device-tree.
 */
static int pmz_attach(struct macio_dev *mdev, const struct of_device_id *match)
{
	struct uart_pmac_port *uap;
	int i;
	
	/* Iterate the pmz_ports array to find a matching entry
	 */
	for (i = 0; i < MAX_ZS_PORTS; i++)
		if (pmz_ports[i].node == mdev->ofdev.dev.of_node)
			break;
	if (i >= MAX_ZS_PORTS)
		return -ENODEV;


	uap = &pmz_ports[i];
	uap->dev = mdev;
	uap->port.dev = &mdev->ofdev.dev;
	dev_set_drvdata(&mdev->ofdev.dev, uap);

	/* We still activate the port even when failing to request resources
	 * to work around bugs in ancient Apple device-trees
	 */
	if (macio_request_resources(uap->dev, "pmac_zilog"))
		printk(KERN_WARNING "%pOFn: Failed to request resource"
		       ", port still active\n",
		       uap->node);
	else
		uap->flags |= PMACZILOG_FLAG_RSRC_REQUESTED;

	return uart_add_one_port(&pmz_uart_reg, &uap->port);
}

/*
 * That one should not be called, macio isn't really a hotswap device,
 * we don't expect one of those serial ports to go away...
 */
static int pmz_detach(struct macio_dev *mdev)
{
	struct uart_pmac_port	*uap = dev_get_drvdata(&mdev->ofdev.dev);
	
	if (!uap)
		return -ENODEV;

	uart_remove_one_port(&pmz_uart_reg, &uap->port);

	if (uap->flags & PMACZILOG_FLAG_RSRC_REQUESTED) {
		macio_release_resources(uap->dev);
		uap->flags &= ~PMACZILOG_FLAG_RSRC_REQUESTED;
	}
	dev_set_drvdata(&mdev->ofdev.dev, NULL);
	uap->dev = NULL;
	uap->port.dev = NULL;
	
	return 0;
}


static int pmz_suspend(struct macio_dev *mdev, pm_message_t pm_state)
{
	struct uart_pmac_port *uap = dev_get_drvdata(&mdev->ofdev.dev);

	if (uap == NULL) {
		printk("HRM... pmz_suspend with NULL uap\n");
		return 0;
	}

	uart_suspend_port(&pmz_uart_reg, &uap->port);

	return 0;
}


static int pmz_resume(struct macio_dev *mdev)
{
	struct uart_pmac_port *uap = dev_get_drvdata(&mdev->ofdev.dev);

	if (uap == NULL)
		return 0;

	uart_resume_port(&pmz_uart_reg, &uap->port);

	return 0;
}

/*
 * Probe all ports in the system and build the ports array, we register
 * with the serial layer later, so we get a proper struct device which
 * allows the tty to attach properly. This is later than it used to be
 * but the tty layer really wants it that way.
 */
static int __init pmz_probe(void)
{
	struct device_node	*node_p, *node_a, *node_b, *np;
	int			count = 0;
	int			rc;

	/*
	 * Find all escc chips in the system
	 */
	for_each_node_by_name(node_p, "escc") {
		/*
		 * First get channel A/B node pointers
		 * 
		 * TODO: Add routines with proper locking to do that...
		 */
		node_a = node_b = NULL;
		for_each_child_of_node(node_p, np) {
			if (of_node_name_prefix(np, "ch-a"))
				node_a = of_node_get(np);
			else if (of_node_name_prefix(np, "ch-b"))
				node_b = of_node_get(np);
		}
		if (!node_a && !node_b) {
			of_node_put(node_a);
			of_node_put(node_b);
			printk(KERN_ERR "pmac_zilog: missing node %c for escc %pOF\n",
				(!node_a) ? 'a' : 'b', node_p);
			continue;
		}

		/*
		 * Fill basic fields in the port structures
		 */
		if (node_b != NULL) {
			pmz_ports[count].mate		= &pmz_ports[count+1];
			pmz_ports[count+1].mate		= &pmz_ports[count];
		}
		pmz_ports[count].flags		= PMACZILOG_FLAG_IS_CHANNEL_A;
		pmz_ports[count].node		= node_a;
		pmz_ports[count+1].node		= node_b;
		pmz_ports[count].port.line	= count;
		pmz_ports[count+1].port.line	= count+1;

		/*
		 * Setup the ports for real
		 */
		rc = pmz_init_port(&pmz_ports[count]);
		if (rc == 0 && node_b != NULL)
			rc = pmz_init_port(&pmz_ports[count+1]);
		if (rc != 0) {
			of_node_put(node_a);
			of_node_put(node_b);
			memset(&pmz_ports[count], 0, sizeof(struct uart_pmac_port));
			memset(&pmz_ports[count+1], 0, sizeof(struct uart_pmac_port));
			continue;
		}
		count += 2;
	}
	pmz_ports_count = count;

	return 0;
}

#else

/* On PCI PowerMacs, pmz_probe() does an explicit search of the OpenFirmware
 * tree to obtain the device_nodes needed to start the console before the
 * macio driver. On Macs without OpenFirmware, global platform_devices take
 * the place of those device_nodes.
 */
extern struct platform_device scc_a_pdev, scc_b_pdev;

static int __init pmz_init_port(struct uart_pmac_port *uap)
{
	struct resource *r_ports;
	int irq;

	r_ports = platform_get_resource(uap->pdev, IORESOURCE_MEM, 0);
	if (!r_ports)
		return -ENODEV;

	irq = platform_get_irq(uap->pdev, 0);
	if (irq < 0)
		return irq;

	uap->port.mapbase  = r_ports->start;
	uap->port.membase  = (unsigned char __iomem *) r_ports->start;
	uap->port.iotype   = UPIO_MEM;
	uap->port.irq      = irq;
	uap->port.uartclk  = ZS_CLOCK;
	uap->port.fifosize = 1;
	uap->port.ops      = &pmz_pops;
	uap->port.type     = PORT_PMAC_ZILOG;
	uap->port.flags    = 0;

	uap->control_reg   = uap->port.membase;
	uap->data_reg      = uap->control_reg + 4;
	uap->port_type     = 0;
	uap->port.has_sysrq = IS_ENABLED(CONFIG_SERIAL_PMACZILOG_CONSOLE);

	pmz_convert_to_zs(uap, CS8, 0, 9600);

	return 0;
}

static int __init pmz_probe(void)
{
	int err;

	pmz_ports_count = 0;

	pmz_ports[0].port.line = 0;
	pmz_ports[0].flags     = PMACZILOG_FLAG_IS_CHANNEL_A;
	pmz_ports[0].pdev      = &scc_a_pdev;
	err = pmz_init_port(&pmz_ports[0]);
	if (err)
		return err;
	pmz_ports_count++;

	pmz_ports[0].mate      = &pmz_ports[1];
	pmz_ports[1].mate      = &pmz_ports[0];
	pmz_ports[1].port.line = 1;
	pmz_ports[1].flags     = 0;
	pmz_ports[1].pdev      = &scc_b_pdev;
	err = pmz_init_port(&pmz_ports[1]);
	if (err)
		return err;
	pmz_ports_count++;

	return 0;
}

static void pmz_dispose_port(struct uart_pmac_port *uap)
{
	memset(uap, 0, sizeof(struct uart_pmac_port));
}

static int __init pmz_attach(struct platform_device *pdev)
{
	struct uart_pmac_port *uap;
	int i;

	/* Iterate the pmz_ports array to find a matching entry */
	for (i = 0; i < pmz_ports_count; i++)
		if (pmz_ports[i].pdev == pdev)
			break;
	if (i >= pmz_ports_count)
		return -ENODEV;

	uap = &pmz_ports[i];
	uap->port.dev = &pdev->dev;
	platform_set_drvdata(pdev, uap);

	return uart_add_one_port(&pmz_uart_reg, &uap->port);
}

static int __exit pmz_detach(struct platform_device *pdev)
{
	struct uart_pmac_port *uap = platform_get_drvdata(pdev);

	if (!uap)
		return -ENODEV;

	uart_remove_one_port(&pmz_uart_reg, &uap->port);

	uap->port.dev = NULL;

	return 0;
}

#endif /* !CONFIG_PPC_PMAC */

#ifdef CONFIG_SERIAL_PMACZILOG_CONSOLE

static void pmz_console_write(struct console *con, const char *s, unsigned int count);
static int __init pmz_console_setup(struct console *co, char *options);

static struct console pmz_console = {
	.name	=	PMACZILOG_NAME,
	.write	=	pmz_console_write,
	.device	=	uart_console_device,
	.setup	=	pmz_console_setup,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
	.data   =	&pmz_uart_reg,
};

#define PMACZILOG_CONSOLE	&pmz_console
#else /* CONFIG_SERIAL_PMACZILOG_CONSOLE */
#define PMACZILOG_CONSOLE	(NULL)
#endif /* CONFIG_SERIAL_PMACZILOG_CONSOLE */

/*
 * Register the driver, console driver and ports with the serial
 * core
 */
static int __init pmz_register(void)
{
	pmz_uart_reg.nr = pmz_ports_count;
	pmz_uart_reg.cons = PMACZILOG_CONSOLE;

	/*
	 * Register this driver with the serial core
	 */
	return uart_register_driver(&pmz_uart_reg);
}

#ifdef CONFIG_PPC_PMAC

static const struct of_device_id pmz_match[] =
{
	{
	.name		= "ch-a",
	},
	{
	.name		= "ch-b",
	},
	{},
};
MODULE_DEVICE_TABLE (of, pmz_match);

static struct macio_driver pmz_driver = {
	.driver = {
		.name 		= "pmac_zilog",
		.owner		= THIS_MODULE,
		.of_match_table	= pmz_match,
	},
	.probe		= pmz_attach,
	.remove		= pmz_detach,
	.suspend	= pmz_suspend,
	.resume		= pmz_resume,
};

#else

static struct platform_driver pmz_driver = {
	.remove		= __exit_p(pmz_detach),
	.driver		= {
		.name		= "scc",
	},
};

#endif /* !CONFIG_PPC_PMAC */

static int __init init_pmz(void)
{
	int rc, i;

	/* 
	 * First, we need to do a direct OF-based probe pass. We
	 * do that because we want serial console up before the
	 * macio stuffs calls us back, and since that makes it
	 * easier to pass the proper number of channels to
	 * uart_register_driver()
	 */
	if (pmz_ports_count == 0)
		pmz_probe();

	/*
	 * Bail early if no port found
	 */
	if (pmz_ports_count == 0)
		return -ENODEV;

	/*
	 * Now we register with the serial layer
	 */
	rc = pmz_register();
	if (rc) {
		printk(KERN_ERR 
			"pmac_zilog: Error registering serial device, disabling pmac_zilog.\n"
		 	"pmac_zilog: Did another serial driver already claim the minors?\n"); 
		/* effectively "pmz_unprobe()" */
		for (i=0; i < pmz_ports_count; i++)
			pmz_dispose_port(&pmz_ports[i]);
		return rc;
	}

	/*
	 * Then we register the macio driver itself
	 */
#ifdef CONFIG_PPC_PMAC
	return macio_register_driver(&pmz_driver);
#else
	return platform_driver_probe(&pmz_driver, pmz_attach);
#endif
}

static void __exit exit_pmz(void)
{
	int i;

#ifdef CONFIG_PPC_PMAC
	/* Get rid of macio-driver (detach from macio) */
	macio_unregister_driver(&pmz_driver);
#else
	platform_driver_unregister(&pmz_driver);
#endif

	for (i = 0; i < pmz_ports_count; i++) {
		struct uart_pmac_port *uport = &pmz_ports[i];
#ifdef CONFIG_PPC_PMAC
		if (uport->node != NULL)
			pmz_dispose_port(uport);
#else
		if (uport->pdev != NULL)
			pmz_dispose_port(uport);
#endif
	}
	/* Unregister UART driver */
	uart_unregister_driver(&pmz_uart_reg);
}

#ifdef CONFIG_SERIAL_PMACZILOG_CONSOLE

static void pmz_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct uart_pmac_port *uap =
		container_of(port, struct uart_pmac_port, port);

	/* Wait for the transmit buffer to empty. */
	while ((read_zsreg(uap, R0) & Tx_BUF_EMP) == 0)
		udelay(5);
	write_zsdata(uap, ch);
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 */
static void pmz_console_write(struct console *con, const char *s, unsigned int count)
{
	struct uart_pmac_port *uap = &pmz_ports[con->index];
	unsigned long flags;

	spin_lock_irqsave(&uap->port.lock, flags);

	/* Turn of interrupts and enable the transmitter. */
	write_zsreg(uap, R1, uap->curregs[1] & ~TxINT_ENAB);
	write_zsreg(uap, R5, uap->curregs[5] | TxENABLE | RTS | DTR);

	uart_console_write(&uap->port, s, count, pmz_console_putchar);

	/* Restore the values in the registers. */
	write_zsreg(uap, R1, uap->curregs[1]);
	/* Don't disable the transmitter. */

	spin_unlock_irqrestore(&uap->port.lock, flags);
}

/*
 * Setup the serial console
 */
static int __init pmz_console_setup(struct console *co, char *options)
{
	struct uart_pmac_port *uap;
	struct uart_port *port;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	unsigned long pwr_delay;

	/*
	 * XServe's default to 57600 bps
	 */
	if (of_machine_is_compatible("RackMac1,1")
	    || of_machine_is_compatible("RackMac1,2")
	    || of_machine_is_compatible("MacRISC4"))
		baud = 57600;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= pmz_ports_count)
		co->index = 0;
	uap = &pmz_ports[co->index];
#ifdef CONFIG_PPC_PMAC
	if (uap->node == NULL)
		return -ENODEV;
#else
	if (uap->pdev == NULL)
		return -ENODEV;
#endif
	port = &uap->port;

	/*
	 * Mark port as beeing a console
	 */
	uap->flags |= PMACZILOG_FLAG_IS_CONS;

	/*
	 * Temporary fix for uart layer who didn't setup the spinlock yet
	 */
	spin_lock_init(&port->lock);

	/*
	 * Enable the hardware
	 */
	pwr_delay = __pmz_startup(uap);
	if (pwr_delay)
		mdelay(pwr_delay);
	
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static int __init pmz_console_init(void)
{
	/* Probe ports */
	pmz_probe();

	if (pmz_ports_count == 0)
		return -ENODEV;

	/* TODO: Autoprobe console based on OF */
	/* pmz_console.index = i; */
	register_console(&pmz_console);

	return 0;

}
console_initcall(pmz_console_init);
#endif /* CONFIG_SERIAL_PMACZILOG_CONSOLE */

module_init(init_pmz);
module_exit(exit_pmz);
