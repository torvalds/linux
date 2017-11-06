// SPDX-License-Identifier: GPL-2.0+
/*
 *	Support for the asynchronous serial interface (DUART) included
 *	in the BCM1250 and derived System-On-a-Chip (SOC) devices.
 *
 *	Copyright (c) 2007  Maciej W. Rozycki
 *
 *	Derived from drivers/char/sb1250_duart.c for which the following
 *	copyright applies:
 *
 *	Copyright (c) 2000, 2001, 2002, 2003, 2004  Broadcom Corporation
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	References:
 *
 *	"BCM1250/BCM1125/BCM1125H User Manual", Broadcom Corporation
 */

#if defined(CONFIG_SERIAL_SB1250_DUART_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/compiler.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/major.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/spinlock.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/types.h>

#include <linux/refcount.h>
#include <asm/io.h>
#include <asm/war.h>

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_uart.h>
#include <asm/sibyte/swarm.h>


#if defined(CONFIG_SIBYTE_BCM1x55) || defined(CONFIG_SIBYTE_BCM1x80)
#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/bcm1480_int.h>

#define SBD_CHANREGS(line)	A_BCM1480_DUART_CHANREG((line), 0)
#define SBD_CTRLREGS(line)	A_BCM1480_DUART_CTRLREG((line), 0)
#define SBD_INT(line)		(K_BCM1480_INT_UART_0 + (line))

#define DUART_CHANREG_SPACING	BCM1480_DUART_CHANREG_SPACING

#define R_DUART_IMRREG(line)	R_BCM1480_DUART_IMRREG(line)
#define R_DUART_INCHREG(line)	R_BCM1480_DUART_INCHREG(line)
#define R_DUART_ISRREG(line)	R_BCM1480_DUART_ISRREG(line)

#elif defined(CONFIG_SIBYTE_SB1250) || defined(CONFIG_SIBYTE_BCM112X)
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>

#define SBD_CHANREGS(line)	A_DUART_CHANREG((line), 0)
#define SBD_CTRLREGS(line)	A_DUART_CTRLREG(0)
#define SBD_INT(line)		(K_INT_UART_0 + (line))

#else
#error invalid SB1250 UART configuration

#endif


MODULE_AUTHOR("Maciej W. Rozycki <macro@linux-mips.org>");
MODULE_DESCRIPTION("BCM1xxx on-chip DUART serial driver");
MODULE_LICENSE("GPL");


#define DUART_MAX_CHIP 2
#define DUART_MAX_SIDE 2

/*
 * Per-port state.
 */
struct sbd_port {
	struct sbd_duart	*duart;
	struct uart_port	port;
	unsigned char __iomem	*memctrl;
	int			tx_stopped;
	int			initialised;
};

/*
 * Per-DUART state for the shared register space.
 */
struct sbd_duart {
	struct sbd_port		sport[2];
	unsigned long		mapctrl;
	refcount_t		map_guard;
};

#define to_sport(uport) container_of(uport, struct sbd_port, port)

static struct sbd_duart sbd_duarts[DUART_MAX_CHIP];


/*
 * Reading and writing SB1250 DUART registers.
 *
 * There are three register spaces: two per-channel ones and
 * a shared one.  We have to define accessors appropriately.
 * All registers are 64-bit and all but the Baud Rate Clock
 * registers only define 8 least significant bits.  There is
 * also a workaround to take into account.  Raw accessors use
 * the full register width, but cooked ones truncate it
 * intentionally so that the rest of the driver does not care.
 */
static u64 __read_sbdchn(struct sbd_port *sport, int reg)
{
	void __iomem *csr = sport->port.membase + reg;

	return __raw_readq(csr);
}

static u64 __read_sbdshr(struct sbd_port *sport, int reg)
{
	void __iomem *csr = sport->memctrl + reg;

	return __raw_readq(csr);
}

static void __write_sbdchn(struct sbd_port *sport, int reg, u64 value)
{
	void __iomem *csr = sport->port.membase + reg;

	__raw_writeq(value, csr);
}

static void __write_sbdshr(struct sbd_port *sport, int reg, u64 value)
{
	void __iomem *csr = sport->memctrl + reg;

	__raw_writeq(value, csr);
}

/*
 * In bug 1956, we get glitches that can mess up uart registers.  This
 * "read-mode-reg after any register access" is an accepted workaround.
 */
static void __war_sbd1956(struct sbd_port *sport)
{
	__read_sbdchn(sport, R_DUART_MODE_REG_1);
	__read_sbdchn(sport, R_DUART_MODE_REG_2);
}

static unsigned char read_sbdchn(struct sbd_port *sport, int reg)
{
	unsigned char retval;

	retval = __read_sbdchn(sport, reg);
	if (SIBYTE_1956_WAR)
		__war_sbd1956(sport);
	return retval;
}

static unsigned char read_sbdshr(struct sbd_port *sport, int reg)
{
	unsigned char retval;

	retval = __read_sbdshr(sport, reg);
	if (SIBYTE_1956_WAR)
		__war_sbd1956(sport);
	return retval;
}

static void write_sbdchn(struct sbd_port *sport, int reg, unsigned int value)
{
	__write_sbdchn(sport, reg, value);
	if (SIBYTE_1956_WAR)
		__war_sbd1956(sport);
}

static void write_sbdshr(struct sbd_port *sport, int reg, unsigned int value)
{
	__write_sbdshr(sport, reg, value);
	if (SIBYTE_1956_WAR)
		__war_sbd1956(sport);
}


static int sbd_receive_ready(struct sbd_port *sport)
{
	return read_sbdchn(sport, R_DUART_STATUS) & M_DUART_RX_RDY;
}

static int sbd_receive_drain(struct sbd_port *sport)
{
	int loops = 10000;

	while (sbd_receive_ready(sport) && --loops)
		read_sbdchn(sport, R_DUART_RX_HOLD);
	return loops;
}

static int __maybe_unused sbd_transmit_ready(struct sbd_port *sport)
{
	return read_sbdchn(sport, R_DUART_STATUS) & M_DUART_TX_RDY;
}

static int __maybe_unused sbd_transmit_drain(struct sbd_port *sport)
{
	int loops = 10000;

	while (!sbd_transmit_ready(sport) && --loops)
		udelay(2);
	return loops;
}

static int sbd_transmit_empty(struct sbd_port *sport)
{
	return read_sbdchn(sport, R_DUART_STATUS) & M_DUART_TX_EMT;
}

static int sbd_line_drain(struct sbd_port *sport)
{
	int loops = 10000;

	while (!sbd_transmit_empty(sport) && --loops)
		udelay(2);
	return loops;
}


static unsigned int sbd_tx_empty(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);

	return sbd_transmit_empty(sport) ? TIOCSER_TEMT : 0;
}

static unsigned int sbd_get_mctrl(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);
	unsigned int mctrl, status;

	status = read_sbdshr(sport, R_DUART_IN_PORT);
	status >>= (uport->line) % 2;
	mctrl = (!(status & M_DUART_IN_PIN0_VAL) ? TIOCM_CTS : 0) |
		(!(status & M_DUART_IN_PIN4_VAL) ? TIOCM_CAR : 0) |
		(!(status & M_DUART_RIN0_PIN) ? TIOCM_RNG : 0) |
		(!(status & M_DUART_IN_PIN2_VAL) ? TIOCM_DSR : 0);
	return mctrl;
}

static void sbd_set_mctrl(struct uart_port *uport, unsigned int mctrl)
{
	struct sbd_port *sport = to_sport(uport);
	unsigned int clr = 0, set = 0, mode2;

	if (mctrl & TIOCM_DTR)
		set |= M_DUART_SET_OPR2;
	else
		clr |= M_DUART_CLR_OPR2;
	if (mctrl & TIOCM_RTS)
		set |= M_DUART_SET_OPR0;
	else
		clr |= M_DUART_CLR_OPR0;
	clr <<= (uport->line) % 2;
	set <<= (uport->line) % 2;

	mode2 = read_sbdchn(sport, R_DUART_MODE_REG_2);
	mode2 &= ~M_DUART_CHAN_MODE;
	if (mctrl & TIOCM_LOOP)
		mode2 |= V_DUART_CHAN_MODE_LCL_LOOP;
	else
		mode2 |= V_DUART_CHAN_MODE_NORMAL;

	write_sbdshr(sport, R_DUART_CLEAR_OPR, clr);
	write_sbdshr(sport, R_DUART_SET_OPR, set);
	write_sbdchn(sport, R_DUART_MODE_REG_2, mode2);
}

static void sbd_stop_tx(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);

	write_sbdchn(sport, R_DUART_CMD, M_DUART_TX_DIS);
	sport->tx_stopped = 1;
};

static void sbd_start_tx(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);
	unsigned int mask;

	/* Enable tx interrupts.  */
	mask = read_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2));
	mask |= M_DUART_IMR_TX;
	write_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2), mask);

	/* Go!, go!, go!...  */
	write_sbdchn(sport, R_DUART_CMD, M_DUART_TX_EN);
	sport->tx_stopped = 0;
};

static void sbd_stop_rx(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);

	write_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2), 0);
};

static void sbd_enable_ms(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);

	write_sbdchn(sport, R_DUART_AUXCTL_X,
		     M_DUART_CIN_CHNG_ENA | M_DUART_CTS_CHNG_ENA);
}

static void sbd_break_ctl(struct uart_port *uport, int break_state)
{
	struct sbd_port *sport = to_sport(uport);

	if (break_state == -1)
		write_sbdchn(sport, R_DUART_CMD, V_DUART_MISC_CMD_START_BREAK);
	else
		write_sbdchn(sport, R_DUART_CMD, V_DUART_MISC_CMD_STOP_BREAK);
}


static void sbd_receive_chars(struct sbd_port *sport)
{
	struct uart_port *uport = &sport->port;
	struct uart_icount *icount;
	unsigned int status, ch, flag;
	int count;

	for (count = 16; count; count--) {
		status = read_sbdchn(sport, R_DUART_STATUS);
		if (!(status & M_DUART_RX_RDY))
			break;

		ch = read_sbdchn(sport, R_DUART_RX_HOLD);

		flag = TTY_NORMAL;

		icount = &uport->icount;
		icount->rx++;

		if (unlikely(status &
			     (M_DUART_RCVD_BRK | M_DUART_FRM_ERR |
			      M_DUART_PARITY_ERR | M_DUART_OVRUN_ERR))) {
			if (status & M_DUART_RCVD_BRK) {
				icount->brk++;
				if (uart_handle_break(uport))
					continue;
			} else if (status & M_DUART_FRM_ERR)
				icount->frame++;
			else if (status & M_DUART_PARITY_ERR)
				icount->parity++;
			if (status & M_DUART_OVRUN_ERR)
				icount->overrun++;

			status &= uport->read_status_mask;
			if (status & M_DUART_RCVD_BRK)
				flag = TTY_BREAK;
			else if (status & M_DUART_FRM_ERR)
				flag = TTY_FRAME;
			else if (status & M_DUART_PARITY_ERR)
				flag = TTY_PARITY;
		}

		if (uart_handle_sysrq_char(uport, ch))
			continue;

		uart_insert_char(uport, status, M_DUART_OVRUN_ERR, ch, flag);
	}

	tty_flip_buffer_push(&uport->state->port);
}

static void sbd_transmit_chars(struct sbd_port *sport)
{
	struct uart_port *uport = &sport->port;
	struct circ_buf *xmit = &sport->port.state->xmit;
	unsigned int mask;
	int stop_tx;

	/* XON/XOFF chars.  */
	if (sport->port.x_char) {
		write_sbdchn(sport, R_DUART_TX_HOLD, sport->port.x_char);
		sport->port.icount.tx++;
		sport->port.x_char = 0;
		return;
	}

	/* If nothing to do or stopped or hardware stopped.  */
	stop_tx = (uart_circ_empty(xmit) || uart_tx_stopped(&sport->port));

	/* Send char.  */
	if (!stop_tx) {
		write_sbdchn(sport, R_DUART_TX_HOLD, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		sport->port.icount.tx++;

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_write_wakeup(&sport->port);
	}

	/* Are we are done?  */
	if (stop_tx || uart_circ_empty(xmit)) {
		/* Disable tx interrupts.  */
		mask = read_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2));
		mask &= ~M_DUART_IMR_TX;
		write_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2), mask);
	}
}

static void sbd_status_handle(struct sbd_port *sport)
{
	struct uart_port *uport = &sport->port;
	unsigned int delta;

	delta = read_sbdshr(sport, R_DUART_INCHREG((uport->line) % 2));
	delta >>= (uport->line) % 2;

	if (delta & (M_DUART_IN_PIN0_VAL << S_DUART_IN_PIN_CHNG))
		uart_handle_cts_change(uport, !(delta & M_DUART_IN_PIN0_VAL));

	if (delta & (M_DUART_IN_PIN2_VAL << S_DUART_IN_PIN_CHNG))
		uport->icount.dsr++;

	if (delta & ((M_DUART_IN_PIN2_VAL | M_DUART_IN_PIN0_VAL) <<
		     S_DUART_IN_PIN_CHNG))
		wake_up_interruptible(&uport->state->port.delta_msr_wait);
}

static irqreturn_t sbd_interrupt(int irq, void *dev_id)
{
	struct sbd_port *sport = dev_id;
	struct uart_port *uport = &sport->port;
	irqreturn_t status = IRQ_NONE;
	unsigned int intstat;
	int count;

	for (count = 16; count; count--) {
		intstat = read_sbdshr(sport,
				      R_DUART_ISRREG((uport->line) % 2));
		intstat &= read_sbdshr(sport,
				       R_DUART_IMRREG((uport->line) % 2));
		intstat &= M_DUART_ISR_ALL;
		if (!intstat)
			break;

		if (intstat & M_DUART_ISR_RX)
			sbd_receive_chars(sport);
		if (intstat & M_DUART_ISR_IN)
			sbd_status_handle(sport);
		if (intstat & M_DUART_ISR_TX)
			sbd_transmit_chars(sport);

		status = IRQ_HANDLED;
	}

	return status;
}


static int sbd_startup(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);
	unsigned int mode1;
	int ret;

	ret = request_irq(sport->port.irq, sbd_interrupt,
			  IRQF_SHARED, "sb1250-duart", sport);
	if (ret)
		return ret;

	/* Clear the receive FIFO.  */
	sbd_receive_drain(sport);

	/* Clear the interrupt registers.  */
	write_sbdchn(sport, R_DUART_CMD, V_DUART_MISC_CMD_RESET_BREAK_INT);
	read_sbdshr(sport, R_DUART_INCHREG((uport->line) % 2));

	/* Set rx/tx interrupt to FIFO available.  */
	mode1 = read_sbdchn(sport, R_DUART_MODE_REG_1);
	mode1 &= ~(M_DUART_RX_IRQ_SEL_RXFULL | M_DUART_TX_IRQ_SEL_TXEMPT);
	write_sbdchn(sport, R_DUART_MODE_REG_1, mode1);

	/* Disable tx, enable rx.  */
	write_sbdchn(sport, R_DUART_CMD, M_DUART_TX_DIS | M_DUART_RX_EN);
	sport->tx_stopped = 1;

	/* Enable interrupts.  */
	write_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2),
		     M_DUART_IMR_IN | M_DUART_IMR_RX);

	return 0;
}

static void sbd_shutdown(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);

	write_sbdchn(sport, R_DUART_CMD, M_DUART_TX_DIS | M_DUART_RX_DIS);
	sport->tx_stopped = 1;
	free_irq(sport->port.irq, sport);
}


static void sbd_init_port(struct sbd_port *sport)
{
	struct uart_port *uport = &sport->port;

	if (sport->initialised)
		return;

	/* There is no DUART reset feature, so just set some sane defaults.  */
	write_sbdchn(sport, R_DUART_CMD, V_DUART_MISC_CMD_RESET_TX);
	write_sbdchn(sport, R_DUART_CMD, V_DUART_MISC_CMD_RESET_RX);
	write_sbdchn(sport, R_DUART_MODE_REG_1, V_DUART_BITS_PER_CHAR_8);
	write_sbdchn(sport, R_DUART_MODE_REG_2, 0);
	write_sbdchn(sport, R_DUART_FULL_CTL,
		     V_DUART_INT_TIME(0) | V_DUART_SIG_FULL(15));
	write_sbdchn(sport, R_DUART_OPCR_X, 0);
	write_sbdchn(sport, R_DUART_AUXCTL_X, 0);
	write_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2), 0);

	sport->initialised = 1;
}

static void sbd_set_termios(struct uart_port *uport, struct ktermios *termios,
			    struct ktermios *old_termios)
{
	struct sbd_port *sport = to_sport(uport);
	unsigned int mode1 = 0, mode2 = 0, aux = 0;
	unsigned int mode1mask = 0, mode2mask = 0, auxmask = 0;
	unsigned int oldmode1, oldmode2, oldaux;
	unsigned int baud, brg;
	unsigned int command;

	mode1mask |= ~(M_DUART_PARITY_MODE | M_DUART_PARITY_TYPE_ODD |
		       M_DUART_BITS_PER_CHAR);
	mode2mask |= ~M_DUART_STOP_BIT_LEN_2;
	auxmask |= ~M_DUART_CTS_CHNG_ENA;

	/* Byte size.  */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
	case CS6:
		/* Unsupported, leave unchanged.  */
		mode1mask |= M_DUART_PARITY_MODE;
		break;
	case CS7:
		mode1 |= V_DUART_BITS_PER_CHAR_7;
		break;
	case CS8:
	default:
		mode1 |= V_DUART_BITS_PER_CHAR_8;
		break;
	}

	/* Parity and stop bits.  */
	if (termios->c_cflag & CSTOPB)
		mode2 |= M_DUART_STOP_BIT_LEN_2;
	else
		mode2 |= M_DUART_STOP_BIT_LEN_1;
	if (termios->c_cflag & PARENB)
		mode1 |= V_DUART_PARITY_MODE_ADD;
	else
		mode1 |= V_DUART_PARITY_MODE_NONE;
	if (termios->c_cflag & PARODD)
		mode1 |= M_DUART_PARITY_TYPE_ODD;
	else
		mode1 |= M_DUART_PARITY_TYPE_EVEN;

	baud = uart_get_baud_rate(uport, termios, old_termios, 1200, 5000000);
	brg = V_DUART_BAUD_RATE(baud);
	/* The actual lower bound is 1221bps, so compensate.  */
	if (brg > M_DUART_CLK_COUNTER)
		brg = M_DUART_CLK_COUNTER;

	uart_update_timeout(uport, termios->c_cflag, baud);

	uport->read_status_mask = M_DUART_OVRUN_ERR;
	if (termios->c_iflag & INPCK)
		uport->read_status_mask |= M_DUART_FRM_ERR |
					   M_DUART_PARITY_ERR;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		uport->read_status_mask |= M_DUART_RCVD_BRK;

	uport->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		uport->ignore_status_mask |= M_DUART_FRM_ERR |
					     M_DUART_PARITY_ERR;
	if (termios->c_iflag & IGNBRK) {
		uport->ignore_status_mask |= M_DUART_RCVD_BRK;
		if (termios->c_iflag & IGNPAR)
			uport->ignore_status_mask |= M_DUART_OVRUN_ERR;
	}

	if (termios->c_cflag & CREAD)
		command = M_DUART_RX_EN;
	else
		command = M_DUART_RX_DIS;

	if (termios->c_cflag & CRTSCTS)
		aux |= M_DUART_CTS_CHNG_ENA;
	else
		aux &= ~M_DUART_CTS_CHNG_ENA;

	spin_lock(&uport->lock);

	if (sport->tx_stopped)
		command |= M_DUART_TX_DIS;
	else
		command |= M_DUART_TX_EN;

	oldmode1 = read_sbdchn(sport, R_DUART_MODE_REG_1) & mode1mask;
	oldmode2 = read_sbdchn(sport, R_DUART_MODE_REG_2) & mode2mask;
	oldaux = read_sbdchn(sport, R_DUART_AUXCTL_X) & auxmask;

	if (!sport->tx_stopped)
		sbd_line_drain(sport);
	write_sbdchn(sport, R_DUART_CMD, M_DUART_TX_DIS | M_DUART_RX_DIS);

	write_sbdchn(sport, R_DUART_MODE_REG_1, mode1 | oldmode1);
	write_sbdchn(sport, R_DUART_MODE_REG_2, mode2 | oldmode2);
	write_sbdchn(sport, R_DUART_CLK_SEL, brg);
	write_sbdchn(sport, R_DUART_AUXCTL_X, aux | oldaux);

	write_sbdchn(sport, R_DUART_CMD, command);

	spin_unlock(&uport->lock);
}


static const char *sbd_type(struct uart_port *uport)
{
	return "SB1250 DUART";
}

static void sbd_release_port(struct uart_port *uport)
{
	struct sbd_port *sport = to_sport(uport);
	struct sbd_duart *duart = sport->duart;

	iounmap(sport->memctrl);
	sport->memctrl = NULL;
	iounmap(uport->membase);
	uport->membase = NULL;

	if(refcount_dec_and_test(&duart->map_guard))
		release_mem_region(duart->mapctrl, DUART_CHANREG_SPACING);
	release_mem_region(uport->mapbase, DUART_CHANREG_SPACING);
}

static int sbd_map_port(struct uart_port *uport)
{
	const char *err = KERN_ERR "sbd: Cannot map MMIO\n";
	struct sbd_port *sport = to_sport(uport);
	struct sbd_duart *duart = sport->duart;

	if (!uport->membase)
		uport->membase = ioremap_nocache(uport->mapbase,
						 DUART_CHANREG_SPACING);
	if (!uport->membase) {
		printk(err);
		return -ENOMEM;
	}

	if (!sport->memctrl)
		sport->memctrl = ioremap_nocache(duart->mapctrl,
						 DUART_CHANREG_SPACING);
	if (!sport->memctrl) {
		printk(err);
		iounmap(uport->membase);
		uport->membase = NULL;
		return -ENOMEM;
	}

	return 0;
}

static int sbd_request_port(struct uart_port *uport)
{
	const char *err = KERN_ERR "sbd: Unable to reserve MMIO resource\n";
	struct sbd_duart *duart = to_sport(uport)->duart;
	int ret = 0;

	if (!request_mem_region(uport->mapbase, DUART_CHANREG_SPACING,
				"sb1250-duart")) {
		printk(err);
		return -EBUSY;
	}
	refcount_inc(&duart->map_guard);
	if (refcount_read(&duart->map_guard) == 1) {
		if (!request_mem_region(duart->mapctrl, DUART_CHANREG_SPACING,
					"sb1250-duart")) {
			refcount_dec(&duart->map_guard);
			printk(err);
			ret = -EBUSY;
		}
	}
	if (!ret) {
		ret = sbd_map_port(uport);
		if (ret) {
			if (refcount_dec_and_test(&duart->map_guard))
				release_mem_region(duart->mapctrl,
						   DUART_CHANREG_SPACING);
		}
	}
	if (ret) {
		release_mem_region(uport->mapbase, DUART_CHANREG_SPACING);
		return ret;
	}
	return 0;
}

static void sbd_config_port(struct uart_port *uport, int flags)
{
	struct sbd_port *sport = to_sport(uport);

	if (flags & UART_CONFIG_TYPE) {
		if (sbd_request_port(uport))
			return;

		uport->type = PORT_SB1250_DUART;

		sbd_init_port(sport);
	}
}

static int sbd_verify_port(struct uart_port *uport, struct serial_struct *ser)
{
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_SB1250_DUART)
		ret = -EINVAL;
	if (ser->irq != uport->irq)
		ret = -EINVAL;
	if (ser->baud_base != uport->uartclk / 16)
		ret = -EINVAL;
	return ret;
}


static const struct uart_ops sbd_ops = {
	.tx_empty	= sbd_tx_empty,
	.set_mctrl	= sbd_set_mctrl,
	.get_mctrl	= sbd_get_mctrl,
	.stop_tx	= sbd_stop_tx,
	.start_tx	= sbd_start_tx,
	.stop_rx	= sbd_stop_rx,
	.enable_ms	= sbd_enable_ms,
	.break_ctl	= sbd_break_ctl,
	.startup	= sbd_startup,
	.shutdown	= sbd_shutdown,
	.set_termios	= sbd_set_termios,
	.type		= sbd_type,
	.release_port	= sbd_release_port,
	.request_port	= sbd_request_port,
	.config_port	= sbd_config_port,
	.verify_port	= sbd_verify_port,
};

/* Initialize SB1250 DUART port structures.  */
static void __init sbd_probe_duarts(void)
{
	static int probed;
	int chip, side;
	int max_lines, line;

	if (probed)
		return;

	/* Set the number of available units based on the SOC type.  */
	switch (soc_type) {
	case K_SYS_SOC_TYPE_BCM1x55:
	case K_SYS_SOC_TYPE_BCM1x80:
		max_lines = 4;
		break;
	default:
		/* Assume at least two serial ports at the normal address.  */
		max_lines = 2;
		break;
	}

	probed = 1;

	for (chip = 0, line = 0; chip < DUART_MAX_CHIP && line < max_lines;
	     chip++) {
		sbd_duarts[chip].mapctrl = SBD_CTRLREGS(line);

		for (side = 0; side < DUART_MAX_SIDE && line < max_lines;
		     side++, line++) {
			struct sbd_port *sport = &sbd_duarts[chip].sport[side];
			struct uart_port *uport = &sport->port;

			sport->duart	= &sbd_duarts[chip];

			uport->irq	= SBD_INT(line);
			uport->uartclk	= 100000000 / 20 * 16;
			uport->fifosize	= 16;
			uport->iotype	= UPIO_MEM;
			uport->flags	= UPF_BOOT_AUTOCONF;
			uport->ops	= &sbd_ops;
			uport->line	= line;
			uport->mapbase	= SBD_CHANREGS(line);
		}
	}
}


#ifdef CONFIG_SERIAL_SB1250_DUART_CONSOLE
/*
 * Serial console stuff.  Very basic, polling driver for doing serial
 * console output.  The console_lock is held by the caller, so we
 * shouldn't be interrupted for more console activity.
 */
static void sbd_console_putchar(struct uart_port *uport, int ch)
{
	struct sbd_port *sport = to_sport(uport);

	sbd_transmit_drain(sport);
	write_sbdchn(sport, R_DUART_TX_HOLD, ch);
}

static void sbd_console_write(struct console *co, const char *s,
			      unsigned int count)
{
	int chip = co->index / DUART_MAX_SIDE;
	int side = co->index % DUART_MAX_SIDE;
	struct sbd_port *sport = &sbd_duarts[chip].sport[side];
	struct uart_port *uport = &sport->port;
	unsigned long flags;
	unsigned int mask;

	/* Disable transmit interrupts and enable the transmitter. */
	spin_lock_irqsave(&uport->lock, flags);
	mask = read_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2));
	write_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2),
		     mask & ~M_DUART_IMR_TX);
	write_sbdchn(sport, R_DUART_CMD, M_DUART_TX_EN);
	spin_unlock_irqrestore(&uport->lock, flags);

	uart_console_write(&sport->port, s, count, sbd_console_putchar);

	/* Restore transmit interrupts and the transmitter enable. */
	spin_lock_irqsave(&uport->lock, flags);
	sbd_line_drain(sport);
	if (sport->tx_stopped)
		write_sbdchn(sport, R_DUART_CMD, M_DUART_TX_DIS);
	write_sbdshr(sport, R_DUART_IMRREG((uport->line) % 2), mask);
	spin_unlock_irqrestore(&uport->lock, flags);
}

static int __init sbd_console_setup(struct console *co, char *options)
{
	int chip = co->index / DUART_MAX_SIDE;
	int side = co->index % DUART_MAX_SIDE;
	struct sbd_port *sport = &sbd_duarts[chip].sport[side];
	struct uart_port *uport = &sport->port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	if (!sport->duart)
		return -ENXIO;

	ret = sbd_map_port(uport);
	if (ret)
		return ret;

	sbd_init_port(sport);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	return uart_set_options(uport, co, baud, parity, bits, flow);
}

static struct uart_driver sbd_reg;
static struct console sbd_console = {
	.name	= "duart",
	.write	= sbd_console_write,
	.device	= uart_console_device,
	.setup	= sbd_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &sbd_reg
};

static int __init sbd_serial_console_init(void)
{
	sbd_probe_duarts();
	register_console(&sbd_console);

	return 0;
}

console_initcall(sbd_serial_console_init);

#define SERIAL_SB1250_DUART_CONSOLE	&sbd_console
#else
#define SERIAL_SB1250_DUART_CONSOLE	NULL
#endif /* CONFIG_SERIAL_SB1250_DUART_CONSOLE */


static struct uart_driver sbd_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= "sb1250_duart",
	.dev_name	= "duart",
	.major		= TTY_MAJOR,
	.minor		= SB1250_DUART_MINOR_BASE,
	.nr		= DUART_MAX_CHIP * DUART_MAX_SIDE,
	.cons		= SERIAL_SB1250_DUART_CONSOLE,
};

/* Set up the driver and register it.  */
static int __init sbd_init(void)
{
	int i, ret;

	sbd_probe_duarts();

	ret = uart_register_driver(&sbd_reg);
	if (ret)
		return ret;

	for (i = 0; i < DUART_MAX_CHIP * DUART_MAX_SIDE; i++) {
		struct sbd_duart *duart = &sbd_duarts[i / DUART_MAX_SIDE];
		struct sbd_port *sport = &duart->sport[i % DUART_MAX_SIDE];
		struct uart_port *uport = &sport->port;

		if (sport->duart)
			uart_add_one_port(&sbd_reg, uport);
	}

	return 0;
}

/* Unload the driver.  Unregister stuff, get ready to go away.  */
static void __exit sbd_exit(void)
{
	int i;

	for (i = DUART_MAX_CHIP * DUART_MAX_SIDE - 1; i >= 0; i--) {
		struct sbd_duart *duart = &sbd_duarts[i / DUART_MAX_SIDE];
		struct sbd_port *sport = &duart->sport[i % DUART_MAX_SIDE];
		struct uart_port *uport = &sport->port;

		if (sport->duart)
			uart_remove_one_port(&sbd_reg, uport);
	}

	uart_unregister_driver(&sbd_reg);
}

module_init(sbd_init);
module_exit(sbd_exit);
