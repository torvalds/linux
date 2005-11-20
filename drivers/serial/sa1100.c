/*
 *  linux/drivers/char/sa1100.c
 *
 *  Driver for SA11x0 serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  $Id: sa1100.c,v 1.50 2002/07/29 14:41:04 rmk Exp $
 *
 */
#include <linux/config.h>

#if defined(CONFIG_SERIAL_SA1100_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach/serial_sa1100.h>

/* We've been assigned a range on the "Low-density serial ports" major */
#define SERIAL_SA1100_MAJOR	204
#define MINOR_START		5

#define NR_PORTS		3

#define SA1100_ISR_PASS_LIMIT	256

/*
 * Convert from ignore_status_mask or read_status_mask to UTSR[01]
 */
#define SM_TO_UTSR0(x)	((x) & 0xff)
#define SM_TO_UTSR1(x)	((x) >> 8)
#define UTSR0_TO_SM(x)	((x))
#define UTSR1_TO_SM(x)	((x) << 8)

#define UART_GET_UTCR0(sport)	__raw_readl((sport)->port.membase + UTCR0)
#define UART_GET_UTCR1(sport)	__raw_readl((sport)->port.membase + UTCR1)
#define UART_GET_UTCR2(sport)	__raw_readl((sport)->port.membase + UTCR2)
#define UART_GET_UTCR3(sport)	__raw_readl((sport)->port.membase + UTCR3)
#define UART_GET_UTSR0(sport)	__raw_readl((sport)->port.membase + UTSR0)
#define UART_GET_UTSR1(sport)	__raw_readl((sport)->port.membase + UTSR1)
#define UART_GET_CHAR(sport)	__raw_readl((sport)->port.membase + UTDR)

#define UART_PUT_UTCR0(sport,v)	__raw_writel((v),(sport)->port.membase + UTCR0)
#define UART_PUT_UTCR1(sport,v)	__raw_writel((v),(sport)->port.membase + UTCR1)
#define UART_PUT_UTCR2(sport,v)	__raw_writel((v),(sport)->port.membase + UTCR2)
#define UART_PUT_UTCR3(sport,v)	__raw_writel((v),(sport)->port.membase + UTCR3)
#define UART_PUT_UTSR0(sport,v)	__raw_writel((v),(sport)->port.membase + UTSR0)
#define UART_PUT_UTSR1(sport,v)	__raw_writel((v),(sport)->port.membase + UTSR1)
#define UART_PUT_CHAR(sport,v)	__raw_writel((v),(sport)->port.membase + UTDR)

/*
 * This is the size of our serial port register set.
 */
#define UART_PORT_SIZE	0x24

/*
 * This determines how often we check the modem status signals
 * for any change.  They generally aren't connected to an IRQ
 * so we have to poll them.  We also check immediately before
 * filling the TX fifo incase CTS has been dropped.
 */
#define MCTRL_TIMEOUT	(250*HZ/1000)

struct sa1100_port {
	struct uart_port	port;
	struct timer_list	timer;
	unsigned int		old_status;
};

/*
 * Handle any change of modem status signal since we were last called.
 */
static void sa1100_mctrl_check(struct sa1100_port *sport)
{
	unsigned int status, changed;

	status = sport->port.ops->get_mctrl(&sport->port);
	changed = status ^ sport->old_status;

	if (changed == 0)
		return;

	sport->old_status = status;

	if (changed & TIOCM_RI)
		sport->port.icount.rng++;
	if (changed & TIOCM_DSR)
		sport->port.icount.dsr++;
	if (changed & TIOCM_CAR)
		uart_handle_dcd_change(&sport->port, status & TIOCM_CAR);
	if (changed & TIOCM_CTS)
		uart_handle_cts_change(&sport->port, status & TIOCM_CTS);

	wake_up_interruptible(&sport->port.info->delta_msr_wait);
}

/*
 * This is our per-port timeout handler, for checking the
 * modem status signals.
 */
static void sa1100_timeout(unsigned long data)
{
	struct sa1100_port *sport = (struct sa1100_port *)data;
	unsigned long flags;

	if (sport->port.info) {
		spin_lock_irqsave(&sport->port.lock, flags);
		sa1100_mctrl_check(sport);
		spin_unlock_irqrestore(&sport->port.lock, flags);

		mod_timer(&sport->timer, jiffies + MCTRL_TIMEOUT);
	}
}

/*
 * interrupts disabled on entry
 */
static void sa1100_stop_tx(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;
	u32 utcr3;

	utcr3 = UART_GET_UTCR3(sport);
	UART_PUT_UTCR3(sport, utcr3 & ~UTCR3_TIE);
	sport->port.read_status_mask &= ~UTSR0_TO_SM(UTSR0_TFS);
}

/*
 * port locked and interrupts disabled
 */
static void sa1100_start_tx(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;
	u32 utcr3;

	utcr3 = UART_GET_UTCR3(sport);
	sport->port.read_status_mask |= UTSR0_TO_SM(UTSR0_TFS);
	UART_PUT_UTCR3(sport, utcr3 | UTCR3_TIE);
}

/*
 * Interrupts enabled
 */
static void sa1100_stop_rx(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;
	u32 utcr3;

	utcr3 = UART_GET_UTCR3(sport);
	UART_PUT_UTCR3(sport, utcr3 & ~UTCR3_RIE);
}

/*
 * Set the modem control timer to fire immediately.
 */
static void sa1100_enable_ms(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;

	mod_timer(&sport->timer, jiffies);
}

static void
sa1100_rx_chars(struct sa1100_port *sport, struct pt_regs *regs)
{
	struct tty_struct *tty = sport->port.info->tty;
	unsigned int status, ch, flg;

	status = UTSR1_TO_SM(UART_GET_UTSR1(sport)) |
		 UTSR0_TO_SM(UART_GET_UTSR0(sport));
	while (status & UTSR1_TO_SM(UTSR1_RNE)) {
		ch = UART_GET_CHAR(sport);

		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			goto ignore_char;
		sport->port.icount.rx++;

		flg = TTY_NORMAL;

		/*
		 * note that the error handling code is
		 * out of the main execution path
		 */
		if (status & UTSR1_TO_SM(UTSR1_PRE | UTSR1_FRE | UTSR1_ROR)) {
			if (status & UTSR1_TO_SM(UTSR1_PRE))
				sport->port.icount.parity++;
			else if (status & UTSR1_TO_SM(UTSR1_FRE))
				sport->port.icount.frame++;
			if (status & UTSR1_TO_SM(UTSR1_ROR))
				sport->port.icount.overrun++;

			status &= sport->port.read_status_mask;

			if (status & UTSR1_TO_SM(UTSR1_PRE))
				flg = TTY_PARITY;
			else if (status & UTSR1_TO_SM(UTSR1_FRE))
				flg = TTY_FRAME;

#ifdef SUPPORT_SYSRQ
			sport->port.sysrq = 0;
#endif
		}

		if (uart_handle_sysrq_char(&sport->port, ch, regs))
			goto ignore_char;

		uart_insert_char(&sport->port, status, UTSR1_TO_SM(UTSR1_ROR), ch, flg);

	ignore_char:
		status = UTSR1_TO_SM(UART_GET_UTSR1(sport)) |
			 UTSR0_TO_SM(UART_GET_UTSR0(sport));
	}
	tty_flip_buffer_push(tty);
}

static void sa1100_tx_chars(struct sa1100_port *sport)
{
	struct circ_buf *xmit = &sport->port.info->xmit;

	if (sport->port.x_char) {
		UART_PUT_CHAR(sport, sport->port.x_char);
		sport->port.icount.tx++;
		sport->port.x_char = 0;
		return;
	}

	/*
	 * Check the modem control lines before
	 * transmitting anything.
	 */
	sa1100_mctrl_check(sport);

	if (uart_circ_empty(xmit) || uart_tx_stopped(&sport->port)) {
		sa1100_stop_tx(&sport->port);
		return;
	}

	/*
	 * Tried using FIFO (not checking TNF) for fifo fill:
	 * still had the '4 bytes repeated' problem.
	 */
	while (UART_GET_UTSR1(sport) & UTSR1_TNF) {
		UART_PUT_CHAR(sport, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		sport->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

	if (uart_circ_empty(xmit))
		sa1100_stop_tx(&sport->port);
}

static irqreturn_t sa1100_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sa1100_port *sport = dev_id;
	unsigned int status, pass_counter = 0;

	spin_lock(&sport->port.lock);
	status = UART_GET_UTSR0(sport);
	status &= SM_TO_UTSR0(sport->port.read_status_mask) | ~UTSR0_TFS;
	do {
		if (status & (UTSR0_RFS | UTSR0_RID)) {
			/* Clear the receiver idle bit, if set */
			if (status & UTSR0_RID)
				UART_PUT_UTSR0(sport, UTSR0_RID);
			sa1100_rx_chars(sport, regs);
		}

		/* Clear the relevant break bits */
		if (status & (UTSR0_RBB | UTSR0_REB))
			UART_PUT_UTSR0(sport, status & (UTSR0_RBB | UTSR0_REB));

		if (status & UTSR0_RBB)
			sport->port.icount.brk++;

		if (status & UTSR0_REB)
			uart_handle_break(&sport->port);

		if (status & UTSR0_TFS)
			sa1100_tx_chars(sport);
		if (pass_counter++ > SA1100_ISR_PASS_LIMIT)
			break;
		status = UART_GET_UTSR0(sport);
		status &= SM_TO_UTSR0(sport->port.read_status_mask) |
			  ~UTSR0_TFS;
	} while (status & (UTSR0_TFS | UTSR0_RFS | UTSR0_RID));
	spin_unlock(&sport->port.lock);

	return IRQ_HANDLED;
}

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static unsigned int sa1100_tx_empty(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;

	return UART_GET_UTSR1(sport) & UTSR1_TBY ? 0 : TIOCSER_TEMT;
}

static unsigned int sa1100_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void sa1100_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

/*
 * Interrupts always disabled.
 */
static void sa1100_break_ctl(struct uart_port *port, int break_state)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;
	unsigned long flags;
	unsigned int utcr3;

	spin_lock_irqsave(&sport->port.lock, flags);
	utcr3 = UART_GET_UTCR3(sport);
	if (break_state == -1)
		utcr3 |= UTCR3_BRK;
	else
		utcr3 &= ~UTCR3_BRK;
	UART_PUT_UTCR3(sport, utcr3);
	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static int sa1100_startup(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;
	int retval;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(sport->port.irq, sa1100_int, 0,
			     "sa11x0-uart", sport);
	if (retval)
		return retval;

	/*
	 * Finally, clear and enable interrupts
	 */
	UART_PUT_UTSR0(sport, -1);
	UART_PUT_UTCR3(sport, UTCR3_RXE | UTCR3_TXE | UTCR3_RIE);

	/*
	 * Enable modem status interrupts
	 */
	spin_lock_irq(&sport->port.lock);
	sa1100_enable_ms(&sport->port);
	spin_unlock_irq(&sport->port.lock);

	return 0;
}

static void sa1100_shutdown(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;

	/*
	 * Stop our timer.
	 */
	del_timer_sync(&sport->timer);

	/*
	 * Free the interrupt
	 */
	free_irq(sport->port.irq, sport);

	/*
	 * Disable all interrupts, port and break condition.
	 */
	UART_PUT_UTCR3(sport, 0);
}

static void
sa1100_set_termios(struct uart_port *port, struct termios *termios,
		   struct termios *old)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;
	unsigned long flags;
	unsigned int utcr0, old_utcr3, baud, quot;
	unsigned int old_csize = old ? old->c_cflag & CSIZE : CS8;

	/*
	 * We only support CS7 and CS8.
	 */
	while ((termios->c_cflag & CSIZE) != CS7 &&
	       (termios->c_cflag & CSIZE) != CS8) {
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= old_csize;
		old_csize = CS8;
	}

	if ((termios->c_cflag & CSIZE) == CS8)
		utcr0 = UTCR0_DSS;
	else
		utcr0 = 0;

	if (termios->c_cflag & CSTOPB)
		utcr0 |= UTCR0_SBS;
	if (termios->c_cflag & PARENB) {
		utcr0 |= UTCR0_PE;
		if (!(termios->c_cflag & PARODD))
			utcr0 |= UTCR0_OES;
	}

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
	quot = uart_get_divisor(port, baud);

	spin_lock_irqsave(&sport->port.lock, flags);

	sport->port.read_status_mask &= UTSR0_TO_SM(UTSR0_TFS);
	sport->port.read_status_mask |= UTSR1_TO_SM(UTSR1_ROR);
	if (termios->c_iflag & INPCK)
		sport->port.read_status_mask |=
				UTSR1_TO_SM(UTSR1_FRE | UTSR1_PRE);
	if (termios->c_iflag & (BRKINT | PARMRK))
		sport->port.read_status_mask |=
				UTSR0_TO_SM(UTSR0_RBB | UTSR0_REB);

	/*
	 * Characters to ignore
	 */
	sport->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		sport->port.ignore_status_mask |=
				UTSR1_TO_SM(UTSR1_FRE | UTSR1_PRE);
	if (termios->c_iflag & IGNBRK) {
		sport->port.ignore_status_mask |=
				UTSR0_TO_SM(UTSR0_RBB | UTSR0_REB);
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			sport->port.ignore_status_mask |=
				UTSR1_TO_SM(UTSR1_ROR);
	}

	del_timer_sync(&sport->timer);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * disable interrupts and drain transmitter
	 */
	old_utcr3 = UART_GET_UTCR3(sport);
	UART_PUT_UTCR3(sport, old_utcr3 & ~(UTCR3_RIE | UTCR3_TIE));

	while (UART_GET_UTSR1(sport) & UTSR1_TBY)
		barrier();

	/* then, disable everything */
	UART_PUT_UTCR3(sport, 0);

	/* set the parity, stop bits and data size */
	UART_PUT_UTCR0(sport, utcr0);

	/* set the baud rate */
	quot -= 1;
	UART_PUT_UTCR1(sport, ((quot & 0xf00) >> 8));
	UART_PUT_UTCR2(sport, (quot & 0xff));

	UART_PUT_UTSR0(sport, -1);

	UART_PUT_UTCR3(sport, old_utcr3);

	if (UART_ENABLE_MS(&sport->port, termios->c_cflag))
		sa1100_enable_ms(&sport->port);

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static const char *sa1100_type(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;

	return sport->port.type == PORT_SA1100 ? "SA1100" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void sa1100_release_port(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;

	release_mem_region(sport->port.mapbase, UART_PORT_SIZE);
}

/*
 * Request the memory region(s) being used by 'port'.
 */
static int sa1100_request_port(struct uart_port *port)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;

	return request_mem_region(sport->port.mapbase, UART_PORT_SIZE,
			"sa11x0-uart") != NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void sa1100_config_port(struct uart_port *port, int flags)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;

	if (flags & UART_CONFIG_TYPE &&
	    sa1100_request_port(&sport->port) == 0)
		sport->port.type = PORT_SA1100;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 * The only change we allow are to the flags and type, and
 * even then only between PORT_SA1100 and PORT_UNKNOWN
 */
static int
sa1100_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	struct sa1100_port *sport = (struct sa1100_port *)port;
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_SA1100)
		ret = -EINVAL;
	if (sport->port.irq != ser->irq)
		ret = -EINVAL;
	if (ser->io_type != SERIAL_IO_MEM)
		ret = -EINVAL;
	if (sport->port.uartclk / 16 != ser->baud_base)
		ret = -EINVAL;
	if ((void *)sport->port.mapbase != ser->iomem_base)
		ret = -EINVAL;
	if (sport->port.iobase != ser->port)
		ret = -EINVAL;
	if (ser->hub6 != 0)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops sa1100_pops = {
	.tx_empty	= sa1100_tx_empty,
	.set_mctrl	= sa1100_set_mctrl,
	.get_mctrl	= sa1100_get_mctrl,
	.stop_tx	= sa1100_stop_tx,
	.start_tx	= sa1100_start_tx,
	.stop_rx	= sa1100_stop_rx,
	.enable_ms	= sa1100_enable_ms,
	.break_ctl	= sa1100_break_ctl,
	.startup	= sa1100_startup,
	.shutdown	= sa1100_shutdown,
	.set_termios	= sa1100_set_termios,
	.type		= sa1100_type,
	.release_port	= sa1100_release_port,
	.request_port	= sa1100_request_port,
	.config_port	= sa1100_config_port,
	.verify_port	= sa1100_verify_port,
};

static struct sa1100_port sa1100_ports[NR_PORTS];

/*
 * Setup the SA1100 serial ports.  Note that we don't include the IrDA
 * port here since we have our own SIR/FIR driver (see drivers/net/irda)
 *
 * Note also that we support "console=ttySAx" where "x" is either 0 or 1.
 * Which serial port this ends up being depends on the machine you're
 * running this kernel on.  I'm not convinced that this is a good idea,
 * but that's the way it traditionally works.
 *
 * Note that NanoEngine UART3 becomes UART2, and UART2 is no longer
 * used here.
 */
static void __init sa1100_init_ports(void)
{
	static int first = 1;
	int i;

	if (!first)
		return;
	first = 0;

	for (i = 0; i < NR_PORTS; i++) {
		sa1100_ports[i].port.uartclk   = 3686400;
		sa1100_ports[i].port.ops       = &sa1100_pops;
		sa1100_ports[i].port.fifosize  = 8;
		sa1100_ports[i].port.line      = i;
		sa1100_ports[i].port.iotype    = SERIAL_IO_MEM;
		init_timer(&sa1100_ports[i].timer);
		sa1100_ports[i].timer.function = sa1100_timeout;
		sa1100_ports[i].timer.data     = (unsigned long)&sa1100_ports[i];
	}

	/*
	 * make transmit lines outputs, so that when the port
	 * is closed, the output is in the MARK state.
	 */
	PPDR |= PPC_TXD1 | PPC_TXD3;
	PPSR |= PPC_TXD1 | PPC_TXD3;
}

void __init sa1100_register_uart_fns(struct sa1100_port_fns *fns)
{
	if (fns->get_mctrl)
		sa1100_pops.get_mctrl = fns->get_mctrl;
	if (fns->set_mctrl)
		sa1100_pops.set_mctrl = fns->set_mctrl;

	sa1100_pops.pm       = fns->pm;
	sa1100_pops.set_wake = fns->set_wake;
}

void __init sa1100_register_uart(int idx, int port)
{
	if (idx >= NR_PORTS) {
		printk(KERN_ERR "%s: bad index number %d\n", __FUNCTION__, idx);
		return;
	}

	switch (port) {
	case 1:
		sa1100_ports[idx].port.membase = (void __iomem *)&Ser1UTCR0;
		sa1100_ports[idx].port.mapbase = _Ser1UTCR0;
		sa1100_ports[idx].port.irq     = IRQ_Ser1UART;
		sa1100_ports[idx].port.flags   = ASYNC_BOOT_AUTOCONF;
		break;

	case 2:
		sa1100_ports[idx].port.membase = (void __iomem *)&Ser2UTCR0;
		sa1100_ports[idx].port.mapbase = _Ser2UTCR0;
		sa1100_ports[idx].port.irq     = IRQ_Ser2ICP;
		sa1100_ports[idx].port.flags   = ASYNC_BOOT_AUTOCONF;
		break;

	case 3:
		sa1100_ports[idx].port.membase = (void __iomem *)&Ser3UTCR0;
		sa1100_ports[idx].port.mapbase = _Ser3UTCR0;
		sa1100_ports[idx].port.irq     = IRQ_Ser3UART;
		sa1100_ports[idx].port.flags   = ASYNC_BOOT_AUTOCONF;
		break;

	default:
		printk(KERN_ERR "%s: bad port number %d\n", __FUNCTION__, port);
	}
}


#ifdef CONFIG_SERIAL_SA1100_CONSOLE

/*
 * Interrupts are disabled on entering
 */
static void
sa1100_console_write(struct console *co, const char *s, unsigned int count)
{
	struct sa1100_port *sport = &sa1100_ports[co->index];
	unsigned int old_utcr3, status, i;

	/*
	 *	First, save UTCR3 and then disable interrupts
	 */
	old_utcr3 = UART_GET_UTCR3(sport);
	UART_PUT_UTCR3(sport, (old_utcr3 & ~(UTCR3_RIE | UTCR3_TIE)) |
				UTCR3_TXE);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++) {
		do {
			status = UART_GET_UTSR1(sport);
		} while (!(status & UTSR1_TNF));
		UART_PUT_CHAR(sport, s[i]);
		if (s[i] == '\n') {
			do {
				status = UART_GET_UTSR1(sport);
			} while (!(status & UTSR1_TNF));
			UART_PUT_CHAR(sport, '\r');
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore UTCR3
	 */
	do {
		status = UART_GET_UTSR1(sport);
	} while (status & UTSR1_TBY);
	UART_PUT_UTCR3(sport, old_utcr3);
}

/*
 * If the port was already initialised (eg, by a boot loader),
 * try to determine the current setup.
 */
static void __init
sa1100_console_get_options(struct sa1100_port *sport, int *baud,
			   int *parity, int *bits)
{
	unsigned int utcr3;

	utcr3 = UART_GET_UTCR3(sport) & (UTCR3_RXE | UTCR3_TXE);
	if (utcr3 == (UTCR3_RXE | UTCR3_TXE)) {
		/* ok, the port was enabled */
		unsigned int utcr0, quot;

		utcr0 = UART_GET_UTCR0(sport);

		*parity = 'n';
		if (utcr0 & UTCR0_PE) {
			if (utcr0 & UTCR0_OES)
				*parity = 'e';
			else
				*parity = 'o';
		}

		if (utcr0 & UTCR0_DSS)
			*bits = 8;
		else
			*bits = 7;

		quot = UART_GET_UTCR2(sport) | UART_GET_UTCR1(sport) << 8;
		quot &= 0xfff;
		*baud = sport->port.uartclk / (16 * (quot + 1));
	}
}

static int __init
sa1100_console_setup(struct console *co, char *options)
{
	struct sa1100_port *sport;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= NR_PORTS)
		co->index = 0;
	sport = &sa1100_ports[co->index];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		sa1100_console_get_options(sport, &baud, &parity, &bits);

	return uart_set_options(&sport->port, co, baud, parity, bits, flow);
}

static struct uart_driver sa1100_reg;
static struct console sa1100_console = {
	.name		= "ttySA",
	.write		= sa1100_console_write,
	.device		= uart_console_device,
	.setup		= sa1100_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &sa1100_reg,
};

static int __init sa1100_rs_console_init(void)
{
	sa1100_init_ports();
	register_console(&sa1100_console);
	return 0;
}
console_initcall(sa1100_rs_console_init);

#define SA1100_CONSOLE	&sa1100_console
#else
#define SA1100_CONSOLE	NULL
#endif

static struct uart_driver sa1100_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "ttySA",
	.dev_name		= "ttySA",
	.devfs_name		= "ttySA",
	.major			= SERIAL_SA1100_MAJOR,
	.minor			= MINOR_START,
	.nr			= NR_PORTS,
	.cons			= SA1100_CONSOLE,
};

static int sa1100_serial_suspend(struct platform_device *dev, pm_message_t state)
{
	struct sa1100_port *sport = platform_get_drvdata(dev);

	if (sport)
		uart_suspend_port(&sa1100_reg, &sport->port);

	return 0;
}

static int sa1100_serial_resume(struct platform_device *dev)
{
	struct sa1100_port *sport = platform_get_drvdata(dev);

	if (sport)
		uart_resume_port(&sa1100_reg, &sport->port);

	return 0;
}

static int sa1100_serial_probe(struct platform_device *dev)
{
	struct resource *res = dev->resource;
	int i;

	for (i = 0; i < dev->num_resources; i++, res++)
		if (res->flags & IORESOURCE_MEM)
			break;

	if (i < dev->num_resources) {
		for (i = 0; i < NR_PORTS; i++) {
			if (sa1100_ports[i].port.mapbase != res->start)
				continue;

			sa1100_ports[i].port.dev = &dev->dev;
			uart_add_one_port(&sa1100_reg, &sa1100_ports[i].port);
			platform_set_drvdata(dev, &sa1100_ports[i]);
			break;
		}
	}

	return 0;
}

static int sa1100_serial_remove(struct platform_device *pdev)
{
	struct sa1100_port *sport = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (sport)
		uart_remove_one_port(&sa1100_reg, &sport->port);

	return 0;
}

static struct platform_driver sa11x0_serial_driver = {
	.probe		= sa1100_serial_probe,
	.remove		= sa1100_serial_remove,
	.suspend	= sa1100_serial_suspend,
	.resume		= sa1100_serial_resume,
	.driver		= {
		.name	= "sa11x0-uart",
	},
};

static int __init sa1100_serial_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: SA11x0 driver $Revision: 1.50 $\n");

	sa1100_init_ports();

	ret = uart_register_driver(&sa1100_reg);
	if (ret == 0) {
		ret = platform_driver_register(&sa11x0_serial_driver);
		if (ret)
			uart_unregister_driver(&sa1100_reg);
	}
	return ret;
}

static void __exit sa1100_serial_exit(void)
{
	platform_driver_unregister(&sa11x0_serial_driver);
	uart_unregister_driver(&sa1100_reg);
}

module_init(sa1100_serial_init);
module_exit(sa1100_serial_exit);

MODULE_AUTHOR("Deep Blue Solutions Ltd");
MODULE_DESCRIPTION("SA1100 generic serial port driver $Revision: 1.50 $");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(SERIAL_SA1100_MAJOR);
