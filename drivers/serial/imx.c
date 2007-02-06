/*
 *  linux/drivers/serial/imx.c
 *
 *  Driver for Motorola IMX serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Author: Sascha Hauer <sascha@saschahauer.de>
 *  Copyright (C) 2004 Pengutronix
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
 * [29-Mar-2005] Mike Lee
 * Added hardware handshake
 */

#if defined(CONFIG_SERIAL_IMX_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
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
#include <asm/arch/imx-uart.h>

/* We've been assigned a range on the "Low-density serial ports" major */
#define SERIAL_IMX_MAJOR	204
#define MINOR_START		41

#define NR_PORTS		2

#define IMX_ISR_PASS_LIMIT	256

/*
 * This is the size of our serial port register set.
 */
#define UART_PORT_SIZE	0x100

/*
 * This determines how often we check the modem status signals
 * for any change.  They generally aren't connected to an IRQ
 * so we have to poll them.  We also check immediately before
 * filling the TX fifo incase CTS has been dropped.
 */
#define MCTRL_TIMEOUT	(250*HZ/1000)

#define DRIVER_NAME "IMX-uart"

struct imx_port {
	struct uart_port	port;
	struct timer_list	timer;
	unsigned int		old_status;
	int			txirq,rxirq,rtsirq;
	int			have_rtscts:1;
};

/*
 * Handle any change of modem status signal since we were last called.
 */
static void imx_mctrl_check(struct imx_port *sport)
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
static void imx_timeout(unsigned long data)
{
	struct imx_port *sport = (struct imx_port *)data;
	unsigned long flags;

	if (sport->port.info) {
		spin_lock_irqsave(&sport->port.lock, flags);
		imx_mctrl_check(sport);
		spin_unlock_irqrestore(&sport->port.lock, flags);

		mod_timer(&sport->timer, jiffies + MCTRL_TIMEOUT);
	}
}

/*
 * interrupts disabled on entry
 */
static void imx_stop_tx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	UCR1((u32)sport->port.membase) &= ~UCR1_TXMPTYEN;
}

/*
 * interrupts disabled on entry
 */
static void imx_stop_rx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	UCR2((u32)sport->port.membase) &= ~UCR2_RXEN;
}

/*
 * Set the modem control timer to fire immediately.
 */
static void imx_enable_ms(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	mod_timer(&sport->timer, jiffies);
}

static inline void imx_transmit_buffer(struct imx_port *sport)
{
	struct circ_buf *xmit = &sport->port.info->xmit;

	while (!(UTS((u32)sport->port.membase) & UTS_TXFULL)) {
		/* send xmit->buf[xmit->tail]
		 * out the port here */
		URTX0((u32)sport->port.membase) = xmit->buf[xmit->tail];
		xmit->tail = (xmit->tail + 1) &
		         (UART_XMIT_SIZE - 1);
		sport->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	}

	if (uart_circ_empty(xmit))
		imx_stop_tx(&sport->port);
}

/*
 * interrupts disabled on entry
 */
static void imx_start_tx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	UCR1((u32)sport->port.membase) |= UCR1_TXMPTYEN;

	imx_transmit_buffer(sport);
}

static irqreturn_t imx_rtsint(int irq, void *dev_id)
{
	struct imx_port *sport = (struct imx_port *)dev_id;
	unsigned int val = USR1((u32)sport->port.membase)&USR1_RTSS;
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock, flags);

	USR1((u32)sport->port.membase) = USR1_RTSD;
	uart_handle_cts_change(&sport->port, !!val);
	wake_up_interruptible(&sport->port.info->delta_msr_wait);

	spin_unlock_irqrestore(&sport->port.lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t imx_txint(int irq, void *dev_id)
{
	struct imx_port *sport = (struct imx_port *)dev_id;
	struct circ_buf *xmit = &sport->port.info->xmit;
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock,flags);
	if (sport->port.x_char)
	{
		/* Send next char */
		URTX0((u32)sport->port.membase) = sport->port.x_char;
		goto out;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&sport->port)) {
		imx_stop_tx(&sport->port);
		goto out;
	}

	imx_transmit_buffer(sport);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

out:
	spin_unlock_irqrestore(&sport->port.lock,flags);
	return IRQ_HANDLED;
}

static irqreturn_t imx_rxint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	unsigned int rx,flg,ignored = 0;
	struct tty_struct *tty = sport->port.info->tty;
	unsigned long flags;

	rx = URXD0((u32)sport->port.membase);
	spin_lock_irqsave(&sport->port.lock,flags);

	do {
		flg = TTY_NORMAL;
		sport->port.icount.rx++;

		if( USR2((u32)sport->port.membase) & USR2_BRCD ) {
			USR2((u32)sport->port.membase) |= USR2_BRCD;
			if(uart_handle_break(&sport->port))
				goto ignore_char;
		}

		if (uart_handle_sysrq_char
		            (&sport->port, (unsigned char)rx))
			goto ignore_char;

		if( rx & (URXD_PRERR | URXD_OVRRUN | URXD_FRMERR) )
			goto handle_error;

	error_return:
		tty_insert_flip_char(tty, rx, flg);

	ignore_char:
		rx = URXD0((u32)sport->port.membase);
	} while(rx & URXD_CHARRDY);

out:
	spin_unlock_irqrestore(&sport->port.lock,flags);
	tty_flip_buffer_push(tty);
	return IRQ_HANDLED;

handle_error:
	if (rx & URXD_PRERR)
		sport->port.icount.parity++;
	else if (rx & URXD_FRMERR)
		sport->port.icount.frame++;
	if (rx & URXD_OVRRUN)
		sport->port.icount.overrun++;

	if (rx & sport->port.ignore_status_mask) {
		if (++ignored > 100)
			goto out;
		goto ignore_char;
	}

	rx &= sport->port.read_status_mask;

	if (rx & URXD_PRERR)
		flg = TTY_PARITY;
	else if (rx & URXD_FRMERR)
		flg = TTY_FRAME;
	if (rx & URXD_OVRRUN)
		flg = TTY_OVERRUN;

#ifdef SUPPORT_SYSRQ
	sport->port.sysrq = 0;
#endif
	goto error_return;
}

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static unsigned int imx_tx_empty(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	return USR2((u32)sport->port.membase) & USR2_TXDC ?  TIOCSER_TEMT : 0;
}

/*
 * We have a modem side uart, so the meanings of RTS and CTS are inverted.
 */
static unsigned int imx_get_mctrl(struct uart_port *port)
{
        struct imx_port *sport = (struct imx_port *)port;
        unsigned int tmp = TIOCM_DSR | TIOCM_CAR;

        if (USR1((u32)sport->port.membase) & USR1_RTSS)
                tmp |= TIOCM_CTS;

        if (UCR2((u32)sport->port.membase) & UCR2_CTS)
                tmp |= TIOCM_RTS;

        return tmp;
}

static void imx_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
        struct imx_port *sport = (struct imx_port *)port;

        if (mctrl & TIOCM_RTS)
                UCR2((u32)sport->port.membase) |= UCR2_CTS;
        else
                UCR2((u32)sport->port.membase) &= ~UCR2_CTS;
}

/*
 * Interrupts always disabled.
 */
static void imx_break_ctl(struct uart_port *port, int break_state)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock, flags);

	if ( break_state != 0 )
		UCR1((u32)sport->port.membase) |= UCR1_SNDBRK;
	else
		UCR1((u32)sport->port.membase) &= ~UCR1_SNDBRK;

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

#define TXTL 2 /* reset default */
#define RXTL 1 /* reset default */

static int imx_setup_ufcr(struct imx_port *sport, unsigned int mode)
{
	unsigned int val;
	unsigned int ufcr_rfdiv;

	/* set receiver / transmitter trigger level.
	 * RFDIV is set such way to satisfy requested uartclk value
	 */
	val = TXTL<<10 | RXTL;
	ufcr_rfdiv = (imx_get_perclk1() + sport->port.uartclk / 2) / sport->port.uartclk;

	if(!ufcr_rfdiv)
		ufcr_rfdiv = 1;

	if(ufcr_rfdiv >= 7)
		ufcr_rfdiv = 6;
	else
		ufcr_rfdiv = 6 - ufcr_rfdiv;

	val |= UFCR_RFDIV & (ufcr_rfdiv << 7);

	UFCR((u32)sport->port.membase) = val;

	return 0;
}

static int imx_startup(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	int retval;
	unsigned long flags;

	imx_setup_ufcr(sport, 0);

	/* disable the DREN bit (Data Ready interrupt enable) before
	 * requesting IRQs
	 */
	UCR4((u32)sport->port.membase) &= ~UCR4_DREN;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(sport->rxirq, imx_rxint, 0,
			     DRIVER_NAME, sport);
	if (retval) goto error_out1;

	retval = request_irq(sport->txirq, imx_txint, 0,
			     DRIVER_NAME, sport);
	if (retval) goto error_out2;

	retval = request_irq(sport->rtsirq, imx_rtsint,
			     (sport->rtsirq < IMX_IRQS) ? 0 :
			       IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			     DRIVER_NAME, sport);
	if (retval) goto error_out3;

	/*
	 * Finally, clear and enable interrupts
	 */

	USR1((u32)sport->port.membase) = USR1_RTSD;
	UCR1((u32)sport->port.membase) |=
	                 (UCR1_TXMPTYEN | UCR1_RRDYEN | UCR1_RTSDEN | UCR1_UARTEN);

	UCR2((u32)sport->port.membase) |= (UCR2_RXEN | UCR2_TXEN);
	/*
	 * Enable modem status interrupts
	 */
	spin_lock_irqsave(&sport->port.lock,flags);
	imx_enable_ms(&sport->port);
	spin_unlock_irqrestore(&sport->port.lock,flags);

	return 0;

error_out3:
	free_irq(sport->txirq, sport);
error_out2:
	free_irq(sport->rxirq, sport);
error_out1:
	return retval;
}

static void imx_shutdown(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	/*
	 * Stop our timer.
	 */
	del_timer_sync(&sport->timer);

	/*
	 * Free the interrupts
	 */
	free_irq(sport->rtsirq, sport);
	free_irq(sport->txirq, sport);
	free_irq(sport->rxirq, sport);

	/*
	 * Disable all interrupts, port and break condition.
	 */

	UCR1((u32)sport->port.membase) &=
	                 ~(UCR1_TXMPTYEN | UCR1_RRDYEN | UCR1_RTSDEN | UCR1_UARTEN);
}

static void
imx_set_termios(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long flags;
	unsigned int ucr2, old_ucr1, old_txrxen, baud, quot;
	unsigned int old_csize = old ? old->c_cflag & CSIZE : CS8;

	/*
	 * If we don't support modem control lines, don't allow
	 * these to be set.
	 */
	if (0) {
		termios->c_cflag &= ~(HUPCL | CRTSCTS | CMSPAR);
		termios->c_cflag |= CLOCAL;
	}

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
		ucr2 = UCR2_WS | UCR2_SRST | UCR2_IRTS;
	else
		ucr2 = UCR2_SRST | UCR2_IRTS;

	if (termios->c_cflag & CRTSCTS) {
		if( sport->have_rtscts ) {
			ucr2 &= ~UCR2_IRTS;
			ucr2 |= UCR2_CTSC;
		} else {
			termios->c_cflag &= ~CRTSCTS;
		}
	}

	if (termios->c_cflag & CSTOPB)
		ucr2 |= UCR2_STPB;
	if (termios->c_cflag & PARENB) {
		ucr2 |= UCR2_PREN;
		if (termios->c_cflag & PARODD)
			ucr2 |= UCR2_PROE;
	}

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);

	spin_lock_irqsave(&sport->port.lock, flags);

	sport->port.read_status_mask = 0;
	if (termios->c_iflag & INPCK)
		sport->port.read_status_mask |= (URXD_FRMERR | URXD_PRERR);
	if (termios->c_iflag & (BRKINT | PARMRK))
		sport->port.read_status_mask |= URXD_BRK;

	/*
	 * Characters to ignore
	 */
	sport->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		sport->port.ignore_status_mask |= URXD_PRERR;
	if (termios->c_iflag & IGNBRK) {
		sport->port.ignore_status_mask |= URXD_BRK;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			sport->port.ignore_status_mask |= URXD_OVRRUN;
	}

	del_timer_sync(&sport->timer);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * disable interrupts and drain transmitter
	 */
	old_ucr1 = UCR1((u32)sport->port.membase);
	UCR1((u32)sport->port.membase) &= ~(UCR1_TXMPTYEN | UCR1_RRDYEN | UCR1_RTSDEN);

	while ( !(USR2((u32)sport->port.membase) & USR2_TXDC))
		barrier();

	/* then, disable everything */
	old_txrxen = UCR2((u32)sport->port.membase) & ( UCR2_TXEN | UCR2_RXEN );
	UCR2((u32)sport->port.membase) &= ~( UCR2_TXEN | UCR2_RXEN);

	/* set the parity, stop bits and data size */
	UCR2((u32)sport->port.membase) = ucr2;

	/* set the baud rate. We assume uartclk = 16 MHz
	 *
	 * baud * 16   UBIR - 1
	 * --------- = --------
	 *  uartclk    UBMR - 1
	 */
	UBIR((u32)sport->port.membase) = (baud / 100) - 1;
	UBMR((u32)sport->port.membase) = 10000 - 1;

	UCR1((u32)sport->port.membase) = old_ucr1;
	UCR2((u32)sport->port.membase) |= old_txrxen;

	if (UART_ENABLE_MS(&sport->port, termios->c_cflag))
		imx_enable_ms(&sport->port);

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static const char *imx_type(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	return sport->port.type == PORT_IMX ? "IMX" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void imx_release_port(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	release_mem_region(sport->port.mapbase, UART_PORT_SIZE);
}

/*
 * Request the memory region(s) being used by 'port'.
 */
static int imx_request_port(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	return request_mem_region(sport->port.mapbase, UART_PORT_SIZE,
			"imx-uart") != NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void imx_config_port(struct uart_port *port, int flags)
{
	struct imx_port *sport = (struct imx_port *)port;

	if (flags & UART_CONFIG_TYPE &&
	    imx_request_port(&sport->port) == 0)
		sport->port.type = PORT_IMX;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 * The only change we allow are to the flags and type, and
 * even then only between PORT_IMX and PORT_UNKNOWN
 */
static int
imx_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	struct imx_port *sport = (struct imx_port *)port;
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_IMX)
		ret = -EINVAL;
	if (sport->port.irq != ser->irq)
		ret = -EINVAL;
	if (ser->io_type != UPIO_MEM)
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

static struct uart_ops imx_pops = {
	.tx_empty	= imx_tx_empty,
	.set_mctrl	= imx_set_mctrl,
	.get_mctrl	= imx_get_mctrl,
	.stop_tx	= imx_stop_tx,
	.start_tx	= imx_start_tx,
	.stop_rx	= imx_stop_rx,
	.enable_ms	= imx_enable_ms,
	.break_ctl	= imx_break_ctl,
	.startup	= imx_startup,
	.shutdown	= imx_shutdown,
	.set_termios	= imx_set_termios,
	.type		= imx_type,
	.release_port	= imx_release_port,
	.request_port	= imx_request_port,
	.config_port	= imx_config_port,
	.verify_port	= imx_verify_port,
};

static struct imx_port imx_ports[] = {
	{
	.txirq  = UART1_MINT_TX,
	.rxirq  = UART1_MINT_RX,
	.rtsirq = UART1_MINT_RTS,
	.port	= {
		.type		= PORT_IMX,
		.iotype		= UPIO_MEM,
		.membase	= (void *)IMX_UART1_BASE,
		.mapbase	= IMX_UART1_BASE, /* FIXME */
		.irq		= UART1_MINT_RX,
		.uartclk	= 16000000,
		.fifosize	= 32,
		.flags		= UPF_BOOT_AUTOCONF,
		.ops		= &imx_pops,
		.line		= 0,
	},
	}, {
	.txirq  = UART2_MINT_TX,
	.rxirq  = UART2_MINT_RX,
	.rtsirq = UART2_MINT_RTS,
	.port	= {
		.type		= PORT_IMX,
		.iotype		= UPIO_MEM,
		.membase	= (void *)IMX_UART2_BASE,
		.mapbase	= IMX_UART2_BASE, /* FIXME */
		.irq		= UART2_MINT_RX,
		.uartclk	= 16000000,
		.fifosize	= 32,
		.flags		= UPF_BOOT_AUTOCONF,
		.ops		= &imx_pops,
		.line		= 1,
	},
	}
};

/*
 * Setup the IMX serial ports.
 * Note also that we support "console=ttySMXx" where "x" is either 0 or 1.
 * Which serial port this ends up being depends on the machine you're
 * running this kernel on.  I'm not convinced that this is a good idea,
 * but that's the way it traditionally works.
 *
 */
static void __init imx_init_ports(void)
{
	static int first = 1;
	int i;

	if (!first)
		return;
	first = 0;

	for (i = 0; i < ARRAY_SIZE(imx_ports); i++) {
		init_timer(&imx_ports[i].timer);
		imx_ports[i].timer.function = imx_timeout;
		imx_ports[i].timer.data     = (unsigned long)&imx_ports[i];
	}
}

#ifdef CONFIG_SERIAL_IMX_CONSOLE
static void imx_console_putchar(struct uart_port *port, int ch)
{
	struct imx_port *sport = (struct imx_port *)port;
	while ((UTS((u32)sport->port.membase) & UTS_TXFULL))
		barrier();
	URTX0((u32)sport->port.membase) = ch;
}

/*
 * Interrupts are disabled on entering
 */
static void
imx_console_write(struct console *co, const char *s, unsigned int count)
{
	struct imx_port *sport = &imx_ports[co->index];
	unsigned int old_ucr1, old_ucr2;

	/*
	 *	First, save UCR1/2 and then disable interrupts
	 */
	old_ucr1 = UCR1((u32)sport->port.membase);
	old_ucr2 = UCR2((u32)sport->port.membase);

	UCR1((u32)sport->port.membase) =
	                   (old_ucr1 | UCR1_UARTCLKEN | UCR1_UARTEN)
	                   & ~(UCR1_TXMPTYEN | UCR1_RRDYEN | UCR1_RTSDEN);
	UCR2((u32)sport->port.membase) = old_ucr2 | UCR2_TXEN;

	uart_console_write(&sport->port, s, count, imx_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore UCR1/2
	 */
	while (!(USR2((u32)sport->port.membase) & USR2_TXDC));

	UCR1((u32)sport->port.membase) = old_ucr1;
	UCR2((u32)sport->port.membase) = old_ucr2;
}

/*
 * If the port was already initialised (eg, by a boot loader),
 * try to determine the current setup.
 */
static void __init
imx_console_get_options(struct imx_port *sport, int *baud,
			   int *parity, int *bits)
{

	if ( UCR1((u32)sport->port.membase) | UCR1_UARTEN ) {
		/* ok, the port was enabled */
		unsigned int ucr2, ubir,ubmr, uartclk;
		unsigned int baud_raw;
		unsigned int ucfr_rfdiv;

		ucr2 = UCR2((u32)sport->port.membase);

		*parity = 'n';
		if (ucr2 & UCR2_PREN) {
			if (ucr2 & UCR2_PROE)
				*parity = 'o';
			else
				*parity = 'e';
		}

		if (ucr2 & UCR2_WS)
			*bits = 8;
		else
			*bits = 7;

		ubir = UBIR((u32)sport->port.membase) & 0xffff;
		ubmr = UBMR((u32)sport->port.membase) & 0xffff;


		ucfr_rfdiv = (UFCR((u32)sport->port.membase) & UFCR_RFDIV) >> 7;
		if (ucfr_rfdiv == 6)
			ucfr_rfdiv = 7;
		else
			ucfr_rfdiv = 6 - ucfr_rfdiv;

		uartclk = imx_get_perclk1();
		uartclk /= ucfr_rfdiv;

		{	/*
			 * The next code provides exact computation of
			 *   baud_raw = round(((uartclk/16) * (ubir + 1)) / (ubmr + 1))
			 * without need of float support or long long division,
			 * which would be required to prevent 32bit arithmetic overflow
			 */
			unsigned int mul = ubir + 1;
			unsigned int div = 16 * (ubmr + 1);
			unsigned int rem = uartclk % div;

			baud_raw = (uartclk / div) * mul;
			baud_raw += (rem * mul + div / 2) / div;
			*baud = (baud_raw + 50) / 100 * 100;
		}

		if(*baud != baud_raw)
			printk(KERN_INFO "Serial: Console IMX rounded baud rate from %d to %d\n",
				baud_raw, *baud);
	}
}

static int __init
imx_console_setup(struct console *co, char *options)
{
	struct imx_port *sport;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= ARRAY_SIZE(imx_ports))
		co->index = 0;
	sport = &imx_ports[co->index];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		imx_console_get_options(sport, &baud, &parity, &bits);

	imx_setup_ufcr(sport, 0);

	return uart_set_options(&sport->port, co, baud, parity, bits, flow);
}

static struct uart_driver imx_reg;
static struct console imx_console = {
	.name		= "ttySMX",
	.write		= imx_console_write,
	.device		= uart_console_device,
	.setup		= imx_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &imx_reg,
};

static int __init imx_rs_console_init(void)
{
	imx_init_ports();
	register_console(&imx_console);
	return 0;
}
console_initcall(imx_rs_console_init);

#define IMX_CONSOLE	&imx_console
#else
#define IMX_CONSOLE	NULL
#endif

static struct uart_driver imx_reg = {
	.owner          = THIS_MODULE,
	.driver_name    = DRIVER_NAME,
	.dev_name       = "ttySMX",
	.major          = SERIAL_IMX_MAJOR,
	.minor          = MINOR_START,
	.nr             = ARRAY_SIZE(imx_ports),
	.cons           = IMX_CONSOLE,
};

static int serial_imx_suspend(struct platform_device *dev, pm_message_t state)
{
        struct imx_port *sport = platform_get_drvdata(dev);

        if (sport)
                uart_suspend_port(&imx_reg, &sport->port);

        return 0;
}

static int serial_imx_resume(struct platform_device *dev)
{
        struct imx_port *sport = platform_get_drvdata(dev);

        if (sport)
                uart_resume_port(&imx_reg, &sport->port);

        return 0;
}

static int serial_imx_probe(struct platform_device *dev)
{
	struct imxuart_platform_data *pdata;

	imx_ports[dev->id].port.dev = &dev->dev;

	pdata = (struct imxuart_platform_data *)dev->dev.platform_data;
	if(pdata && (pdata->flags & IMXUART_HAVE_RTSCTS))
		imx_ports[dev->id].have_rtscts = 1;

	uart_add_one_port(&imx_reg, &imx_ports[dev->id].port);
	platform_set_drvdata(dev, &imx_ports[dev->id]);
	return 0;
}

static int serial_imx_remove(struct platform_device *dev)
{
	struct imx_port *sport = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if (sport)
		uart_remove_one_port(&imx_reg, &sport->port);

	return 0;
}

static struct platform_driver serial_imx_driver = {
        .probe          = serial_imx_probe,
        .remove         = serial_imx_remove,

	.suspend	= serial_imx_suspend,
	.resume		= serial_imx_resume,
	.driver		= {
	        .name	= "imx-uart",
	},
};

static int __init imx_serial_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: IMX driver\n");

	imx_init_ports();

	ret = uart_register_driver(&imx_reg);
	if (ret)
		return ret;

	ret = platform_driver_register(&serial_imx_driver);
	if (ret != 0)
		uart_unregister_driver(&imx_reg);

	return 0;
}

static void __exit imx_serial_exit(void)
{
	uart_unregister_driver(&imx_reg);
	platform_driver_unregister(&serial_imx_driver);
}

module_init(imx_serial_init);
module_exit(imx_serial_exit);

MODULE_AUTHOR("Sascha Hauer");
MODULE_DESCRIPTION("IMX generic serial port driver");
MODULE_LICENSE("GPL");
