// SPDX-License-Identifier: GPL-2.0+
/*
 *  Driver for AMBA serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
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
 * This is a generic driver for ARM AMBA-type serial ports.  They
 * have a lot of 16550-like features, but are not register compatible.
 * Note that although they do have CTS, DCD and DSR inputs, they do
 * not have an RI input, nor do they have DTR or RTS outputs.  If
 * required, these have to be supplied via some other means (eg, GPIO)
 * and hooked into this driver.
 */

#if defined(CONFIG_SERIAL_AMBA_PL010_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>

#define UART_NR		8

#define SERIAL_AMBA_MAJOR	204
#define SERIAL_AMBA_MINOR	16
#define SERIAL_AMBA_NR		UART_NR

#define AMBA_ISR_PASS_LIMIT	256

#define UART_RX_DATA(s)		(((s) & UART01x_FR_RXFE) == 0)
#define UART_TX_READY(s)	(((s) & UART01x_FR_TXFF) == 0)

#define UART_DUMMY_RSR_RX	256
#define UART_PORT_SIZE		64

/*
 * We wrap our port structure around the generic uart_port.
 */
struct uart_amba_port {
	struct uart_port	port;
	struct clk		*clk;
	struct amba_device	*dev;
	struct amba_pl010_data	*data;
	unsigned int		old_status;
};

static void pl010_stop_tx(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned int cr;

	cr = readb(uap->port.membase + UART010_CR);
	cr &= ~UART010_CR_TIE;
	writel(cr, uap->port.membase + UART010_CR);
}

static void pl010_start_tx(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned int cr;

	cr = readb(uap->port.membase + UART010_CR);
	cr |= UART010_CR_TIE;
	writel(cr, uap->port.membase + UART010_CR);
}

static void pl010_stop_rx(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned int cr;

	cr = readb(uap->port.membase + UART010_CR);
	cr &= ~(UART010_CR_RIE | UART010_CR_RTIE);
	writel(cr, uap->port.membase + UART010_CR);
}

static void pl010_disable_ms(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int cr;

	cr = readb(uap->port.membase + UART010_CR);
	cr &= ~UART010_CR_MSIE;
	writel(cr, uap->port.membase + UART010_CR);
}

static void pl010_enable_ms(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned int cr;

	cr = readb(uap->port.membase + UART010_CR);
	cr |= UART010_CR_MSIE;
	writel(cr, uap->port.membase + UART010_CR);
}

static void pl010_rx_chars(struct uart_amba_port *uap)
{
	unsigned int status, ch, flag, rsr, max_count = 256;

	status = readb(uap->port.membase + UART01x_FR);
	while (UART_RX_DATA(status) && max_count--) {
		ch = readb(uap->port.membase + UART01x_DR);
		flag = TTY_NORMAL;

		uap->port.icount.rx++;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		rsr = readb(uap->port.membase + UART01x_RSR) | UART_DUMMY_RSR_RX;
		if (unlikely(rsr & UART01x_RSR_ANY)) {
			writel(0, uap->port.membase + UART01x_ECR);

			if (rsr & UART01x_RSR_BE) {
				rsr &= ~(UART01x_RSR_FE | UART01x_RSR_PE);
				uap->port.icount.brk++;
				if (uart_handle_break(&uap->port))
					goto ignore_char;
			} else if (rsr & UART01x_RSR_PE)
				uap->port.icount.parity++;
			else if (rsr & UART01x_RSR_FE)
				uap->port.icount.frame++;
			if (rsr & UART01x_RSR_OE)
				uap->port.icount.overrun++;

			rsr &= uap->port.read_status_mask;

			if (rsr & UART01x_RSR_BE)
				flag = TTY_BREAK;
			else if (rsr & UART01x_RSR_PE)
				flag = TTY_PARITY;
			else if (rsr & UART01x_RSR_FE)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(&uap->port, ch))
			goto ignore_char;

		uart_insert_char(&uap->port, rsr, UART01x_RSR_OE, ch, flag);

	ignore_char:
		status = readb(uap->port.membase + UART01x_FR);
	}
	spin_unlock(&uap->port.lock);
	tty_flip_buffer_push(&uap->port.state->port);
	spin_lock(&uap->port.lock);
}

static void pl010_tx_chars(struct uart_amba_port *uap)
{
	struct circ_buf *xmit = &uap->port.state->xmit;
	int count;

	if (uap->port.x_char) {
		writel(uap->port.x_char, uap->port.membase + UART01x_DR);
		uap->port.icount.tx++;
		uap->port.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&uap->port)) {
		pl010_stop_tx(&uap->port);
		return;
	}

	count = uap->port.fifosize >> 1;
	do {
		writel(xmit->buf[xmit->tail], uap->port.membase + UART01x_DR);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		uap->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&uap->port);

	if (uart_circ_empty(xmit))
		pl010_stop_tx(&uap->port);
}

static void pl010_modem_status(struct uart_amba_port *uap)
{
	unsigned int status, delta;

	writel(0, uap->port.membase + UART010_ICR);

	status = readb(uap->port.membase + UART01x_FR) & UART01x_FR_MODEM_ANY;

	delta = status ^ uap->old_status;
	uap->old_status = status;

	if (!delta)
		return;

	if (delta & UART01x_FR_DCD)
		uart_handle_dcd_change(&uap->port, status & UART01x_FR_DCD);

	if (delta & UART01x_FR_DSR)
		uap->port.icount.dsr++;

	if (delta & UART01x_FR_CTS)
		uart_handle_cts_change(&uap->port, status & UART01x_FR_CTS);

	wake_up_interruptible(&uap->port.state->port.delta_msr_wait);
}

static irqreturn_t pl010_int(int irq, void *dev_id)
{
	struct uart_amba_port *uap = dev_id;
	unsigned int status, pass_counter = AMBA_ISR_PASS_LIMIT;
	int handled = 0;

	spin_lock(&uap->port.lock);

	status = readb(uap->port.membase + UART010_IIR);
	if (status) {
		do {
			if (status & (UART010_IIR_RTIS | UART010_IIR_RIS))
				pl010_rx_chars(uap);
			if (status & UART010_IIR_MIS)
				pl010_modem_status(uap);
			if (status & UART010_IIR_TIS)
				pl010_tx_chars(uap);

			if (pass_counter-- == 0)
				break;

			status = readb(uap->port.membase + UART010_IIR);
		} while (status & (UART010_IIR_RTIS | UART010_IIR_RIS |
				   UART010_IIR_TIS));
		handled = 1;
	}

	spin_unlock(&uap->port.lock);

	return IRQ_RETVAL(handled);
}

static unsigned int pl010_tx_empty(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned int status = readb(uap->port.membase + UART01x_FR);
	return status & UART01x_FR_BUSY ? 0 : TIOCSER_TEMT;
}

static unsigned int pl010_get_mctrl(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned int result = 0;
	unsigned int status;

	status = readb(uap->port.membase + UART01x_FR);
	if (status & UART01x_FR_DCD)
		result |= TIOCM_CAR;
	if (status & UART01x_FR_DSR)
		result |= TIOCM_DSR;
	if (status & UART01x_FR_CTS)
		result |= TIOCM_CTS;

	return result;
}

static void pl010_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);

	if (uap->data)
		uap->data->set_mctrl(uap->dev, uap->port.membase, mctrl);
}

static void pl010_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned long flags;
	unsigned int lcr_h;

	spin_lock_irqsave(&uap->port.lock, flags);
	lcr_h = readb(uap->port.membase + UART010_LCRH);
	if (break_state == -1)
		lcr_h |= UART01x_LCRH_BRK;
	else
		lcr_h &= ~UART01x_LCRH_BRK;
	writel(lcr_h, uap->port.membase + UART010_LCRH);
	spin_unlock_irqrestore(&uap->port.lock, flags);
}

static int pl010_startup(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	int retval;

	/*
	 * Try to enable the clock producer.
	 */
	retval = clk_prepare_enable(uap->clk);
	if (retval)
		goto out;

	uap->port.uartclk = clk_get_rate(uap->clk);

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(uap->port.irq, pl010_int, 0, "uart-pl010", uap);
	if (retval)
		goto clk_dis;

	/*
	 * initialise the old status of the modem signals
	 */
	uap->old_status = readb(uap->port.membase + UART01x_FR) & UART01x_FR_MODEM_ANY;

	/*
	 * Finally, enable interrupts
	 */
	writel(UART01x_CR_UARTEN | UART010_CR_RIE | UART010_CR_RTIE,
	       uap->port.membase + UART010_CR);

	return 0;

 clk_dis:
	clk_disable_unprepare(uap->clk);
 out:
	return retval;
}

static void pl010_shutdown(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);

	/*
	 * Free the interrupt
	 */
	free_irq(uap->port.irq, uap);

	/*
	 * disable all interrupts, disable the port
	 */
	writel(0, uap->port.membase + UART010_CR);

	/* disable break condition and fifos */
	writel(readb(uap->port.membase + UART010_LCRH) &
		~(UART01x_LCRH_BRK | UART01x_LCRH_FEN),
	       uap->port.membase + UART010_LCRH);

	/*
	 * Shut down the clock producer
	 */
	clk_disable_unprepare(uap->clk);
}

static void
pl010_set_termios(struct uart_port *port, struct ktermios *termios,
		     struct ktermios *old)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned int lcr_h, old_cr;
	unsigned long flags;
	unsigned int baud, quot;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, uap->port.uartclk/16); 
	quot = uart_get_divisor(port, baud);

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr_h = UART01x_LCRH_WLEN_5;
		break;
	case CS6:
		lcr_h = UART01x_LCRH_WLEN_6;
		break;
	case CS7:
		lcr_h = UART01x_LCRH_WLEN_7;
		break;
	default: // CS8
		lcr_h = UART01x_LCRH_WLEN_8;
		break;
	}
	if (termios->c_cflag & CSTOPB)
		lcr_h |= UART01x_LCRH_STP2;
	if (termios->c_cflag & PARENB) {
		lcr_h |= UART01x_LCRH_PEN;
		if (!(termios->c_cflag & PARODD))
			lcr_h |= UART01x_LCRH_EPS;
	}
	if (uap->port.fifosize > 1)
		lcr_h |= UART01x_LCRH_FEN;

	spin_lock_irqsave(&uap->port.lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	uap->port.read_status_mask = UART01x_RSR_OE;
	if (termios->c_iflag & INPCK)
		uap->port.read_status_mask |= UART01x_RSR_FE | UART01x_RSR_PE;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		uap->port.read_status_mask |= UART01x_RSR_BE;

	/*
	 * Characters to ignore
	 */
	uap->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		uap->port.ignore_status_mask |= UART01x_RSR_FE | UART01x_RSR_PE;
	if (termios->c_iflag & IGNBRK) {
		uap->port.ignore_status_mask |= UART01x_RSR_BE;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			uap->port.ignore_status_mask |= UART01x_RSR_OE;
	}

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0)
		uap->port.ignore_status_mask |= UART_DUMMY_RSR_RX;

	/* first, disable everything */
	old_cr = readb(uap->port.membase + UART010_CR) & ~UART010_CR_MSIE;

	if (UART_ENABLE_MS(port, termios->c_cflag))
		old_cr |= UART010_CR_MSIE;

	writel(0, uap->port.membase + UART010_CR);

	/* Set baud rate */
	quot -= 1;
	writel((quot & 0xf00) >> 8, uap->port.membase + UART010_LCRM);
	writel(quot & 0xff, uap->port.membase + UART010_LCRL);

	/*
	 * ----------v----------v----------v----------v-----
	 * NOTE: MUST BE WRITTEN AFTER UARTLCR_M & UARTLCR_L
	 * ----------^----------^----------^----------^-----
	 */
	writel(lcr_h, uap->port.membase + UART010_LCRH);
	writel(old_cr, uap->port.membase + UART010_CR);

	spin_unlock_irqrestore(&uap->port.lock, flags);
}

static void pl010_set_ldisc(struct uart_port *port, struct ktermios *termios)
{
	if (termios->c_line == N_PPS) {
		port->flags |= UPF_HARDPPS_CD;
		spin_lock_irq(&port->lock);
		pl010_enable_ms(port);
		spin_unlock_irq(&port->lock);
	} else {
		port->flags &= ~UPF_HARDPPS_CD;
		if (!UART_ENABLE_MS(port, termios->c_cflag)) {
			spin_lock_irq(&port->lock);
			pl010_disable_ms(port);
			spin_unlock_irq(&port->lock);
		}
	}
}

static const char *pl010_type(struct uart_port *port)
{
	return port->type == PORT_AMBA ? "AMBA" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void pl010_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, UART_PORT_SIZE);
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int pl010_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, UART_PORT_SIZE, "uart-pl010")
			!= NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void pl010_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_AMBA;
		pl010_request_port(port);
	}
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int pl010_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_AMBA)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= nr_irqs)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static const struct uart_ops amba_pl010_pops = {
	.tx_empty	= pl010_tx_empty,
	.set_mctrl	= pl010_set_mctrl,
	.get_mctrl	= pl010_get_mctrl,
	.stop_tx	= pl010_stop_tx,
	.start_tx	= pl010_start_tx,
	.stop_rx	= pl010_stop_rx,
	.enable_ms	= pl010_enable_ms,
	.break_ctl	= pl010_break_ctl,
	.startup	= pl010_startup,
	.shutdown	= pl010_shutdown,
	.set_termios	= pl010_set_termios,
	.set_ldisc	= pl010_set_ldisc,
	.type		= pl010_type,
	.release_port	= pl010_release_port,
	.request_port	= pl010_request_port,
	.config_port	= pl010_config_port,
	.verify_port	= pl010_verify_port,
};

static struct uart_amba_port *amba_ports[UART_NR];

#ifdef CONFIG_SERIAL_AMBA_PL010_CONSOLE

static void pl010_console_putchar(struct uart_port *port, int ch)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	unsigned int status;

	do {
		status = readb(uap->port.membase + UART01x_FR);
		barrier();
	} while (!UART_TX_READY(status));
	writel(ch, uap->port.membase + UART01x_DR);
}

static void
pl010_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_amba_port *uap = amba_ports[co->index];
	unsigned int status, old_cr;

	clk_enable(uap->clk);

	/*
	 *	First save the CR then disable the interrupts
	 */
	old_cr = readb(uap->port.membase + UART010_CR);
	writel(UART01x_CR_UARTEN, uap->port.membase + UART010_CR);

	uart_console_write(&uap->port, s, count, pl010_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the TCR
	 */
	do {
		status = readb(uap->port.membase + UART01x_FR);
		barrier();
	} while (status & UART01x_FR_BUSY);
	writel(old_cr, uap->port.membase + UART010_CR);

	clk_disable(uap->clk);
}

static void __init
pl010_console_get_options(struct uart_amba_port *uap, int *baud,
			     int *parity, int *bits)
{
	if (readb(uap->port.membase + UART010_CR) & UART01x_CR_UARTEN) {
		unsigned int lcr_h, quot;
		lcr_h = readb(uap->port.membase + UART010_LCRH);

		*parity = 'n';
		if (lcr_h & UART01x_LCRH_PEN) {
			if (lcr_h & UART01x_LCRH_EPS)
				*parity = 'e';
			else
				*parity = 'o';
		}

		if ((lcr_h & 0x60) == UART01x_LCRH_WLEN_7)
			*bits = 7;
		else
			*bits = 8;

		quot = readb(uap->port.membase + UART010_LCRL) |
		       readb(uap->port.membase + UART010_LCRM) << 8;
		*baud = uap->port.uartclk / (16 * (quot + 1));
	}
}

static int __init pl010_console_setup(struct console *co, char *options)
{
	struct uart_amba_port *uap;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR)
		co->index = 0;
	uap = amba_ports[co->index];
	if (!uap)
		return -ENODEV;

	ret = clk_prepare(uap->clk);
	if (ret)
		return ret;

	uap->port.uartclk = clk_get_rate(uap->clk);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		pl010_console_get_options(uap, &baud, &parity, &bits);

	return uart_set_options(&uap->port, co, baud, parity, bits, flow);
}

static struct uart_driver amba_reg;
static struct console amba_console = {
	.name		= "ttyAM",
	.write		= pl010_console_write,
	.device		= uart_console_device,
	.setup		= pl010_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &amba_reg,
};

#define AMBA_CONSOLE	&amba_console
#else
#define AMBA_CONSOLE	NULL
#endif

static DEFINE_MUTEX(amba_reg_lock);
static struct uart_driver amba_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "ttyAM",
	.dev_name		= "ttyAM",
	.major			= SERIAL_AMBA_MAJOR,
	.minor			= SERIAL_AMBA_MINOR,
	.nr			= UART_NR,
	.cons			= AMBA_CONSOLE,
};

static int pl010_probe(struct amba_device *dev, const struct amba_id *id)
{
	struct uart_amba_port *uap;
	void __iomem *base;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(amba_ports); i++)
		if (amba_ports[i] == NULL)
			break;

	if (i == ARRAY_SIZE(amba_ports))
		return -EBUSY;

	uap = devm_kzalloc(&dev->dev, sizeof(struct uart_amba_port),
			   GFP_KERNEL);
	if (!uap)
		return -ENOMEM;

	base = devm_ioremap(&dev->dev, dev->res.start,
			    resource_size(&dev->res));
	if (!base)
		return -ENOMEM;

	uap->clk = devm_clk_get(&dev->dev, NULL);
	if (IS_ERR(uap->clk))
		return PTR_ERR(uap->clk);

	uap->port.dev = &dev->dev;
	uap->port.mapbase = dev->res.start;
	uap->port.membase = base;
	uap->port.iotype = UPIO_MEM;
	uap->port.irq = dev->irq[0];
	uap->port.fifosize = 16;
	uap->port.ops = &amba_pl010_pops;
	uap->port.flags = UPF_BOOT_AUTOCONF;
	uap->port.line = i;
	uap->dev = dev;
	uap->data = dev_get_platdata(&dev->dev);

	amba_ports[i] = uap;

	amba_set_drvdata(dev, uap);

	mutex_lock(&amba_reg_lock);
	if (!amba_reg.state) {
		ret = uart_register_driver(&amba_reg);
		if (ret < 0) {
			mutex_unlock(&amba_reg_lock);
			dev_err(uap->port.dev,
				"Failed to register AMBA-PL010 driver\n");
			return ret;
		}
	}
	mutex_unlock(&amba_reg_lock);

	ret = uart_add_one_port(&amba_reg, &uap->port);
	if (ret)
		amba_ports[i] = NULL;

	return ret;
}

static int pl010_remove(struct amba_device *dev)
{
	struct uart_amba_port *uap = amba_get_drvdata(dev);
	int i;
	bool busy = false;

	uart_remove_one_port(&amba_reg, &uap->port);

	for (i = 0; i < ARRAY_SIZE(amba_ports); i++)
		if (amba_ports[i] == uap)
			amba_ports[i] = NULL;
		else if (amba_ports[i])
			busy = true;

	if (!busy)
		uart_unregister_driver(&amba_reg);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pl010_suspend(struct device *dev)
{
	struct uart_amba_port *uap = dev_get_drvdata(dev);

	if (uap)
		uart_suspend_port(&amba_reg, &uap->port);

	return 0;
}

static int pl010_resume(struct device *dev)
{
	struct uart_amba_port *uap = dev_get_drvdata(dev);

	if (uap)
		uart_resume_port(&amba_reg, &uap->port);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pl010_dev_pm_ops, pl010_suspend, pl010_resume);

static const struct amba_id pl010_ids[] = {
	{
		.id	= 0x00041010,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, pl010_ids);

static struct amba_driver pl010_driver = {
	.drv = {
		.name	= "uart-pl010",
		.pm	= &pl010_dev_pm_ops,
	},
	.id_table	= pl010_ids,
	.probe		= pl010_probe,
	.remove		= pl010_remove,
};

static int __init pl010_init(void)
{
	printk(KERN_INFO "Serial: AMBA driver\n");

	return  amba_driver_register(&pl010_driver);
}

static void __exit pl010_exit(void)
{
	amba_driver_unregister(&pl010_driver);
}

module_init(pl010_init);
module_exit(pl010_exit);

MODULE_AUTHOR("ARM Ltd/Deep Blue Solutions Ltd");
MODULE_DESCRIPTION("ARM AMBA serial port driver");
MODULE_LICENSE("GPL");
