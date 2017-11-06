// SPDX-License-Identifier: GPL-2.0+
/*
 * High Speed Serial Ports on NXP LPC32xx SoC
 *
 * Authors: Kevin Wells <kevin.wells@nxp.com>
 *          Roland Stigge <stigge@antcom.de>
 *
 * Copyright (C) 2010 NXP Semiconductors
 * Copyright (C) 2012 Roland Stigge
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
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/nmi.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <mach/platform.h>
#include <mach/hardware.h>

/*
 * High Speed UART register offsets
 */
#define LPC32XX_HSUART_FIFO(x)			((x) + 0x00)
#define LPC32XX_HSUART_LEVEL(x)			((x) + 0x04)
#define LPC32XX_HSUART_IIR(x)			((x) + 0x08)
#define LPC32XX_HSUART_CTRL(x)			((x) + 0x0C)
#define LPC32XX_HSUART_RATE(x)			((x) + 0x10)

#define LPC32XX_HSU_BREAK_DATA			(1 << 10)
#define LPC32XX_HSU_ERROR_DATA			(1 << 9)
#define LPC32XX_HSU_RX_EMPTY			(1 << 8)

#define LPC32XX_HSU_TX_LEV(n)			(((n) >> 8) & 0xFF)
#define LPC32XX_HSU_RX_LEV(n)			((n) & 0xFF)

#define LPC32XX_HSU_TX_INT_SET			(1 << 6)
#define LPC32XX_HSU_RX_OE_INT			(1 << 5)
#define LPC32XX_HSU_BRK_INT			(1 << 4)
#define LPC32XX_HSU_FE_INT			(1 << 3)
#define LPC32XX_HSU_RX_TIMEOUT_INT		(1 << 2)
#define LPC32XX_HSU_RX_TRIG_INT			(1 << 1)
#define LPC32XX_HSU_TX_INT			(1 << 0)

#define LPC32XX_HSU_HRTS_INV			(1 << 21)
#define LPC32XX_HSU_HRTS_TRIG_8B		(0x0 << 19)
#define LPC32XX_HSU_HRTS_TRIG_16B		(0x1 << 19)
#define LPC32XX_HSU_HRTS_TRIG_32B		(0x2 << 19)
#define LPC32XX_HSU_HRTS_TRIG_48B		(0x3 << 19)
#define LPC32XX_HSU_HRTS_EN			(1 << 18)
#define LPC32XX_HSU_TMO_DISABLED		(0x0 << 16)
#define LPC32XX_HSU_TMO_INACT_4B		(0x1 << 16)
#define LPC32XX_HSU_TMO_INACT_8B		(0x2 << 16)
#define LPC32XX_HSU_TMO_INACT_16B		(0x3 << 16)
#define LPC32XX_HSU_HCTS_INV			(1 << 15)
#define LPC32XX_HSU_HCTS_EN			(1 << 14)
#define LPC32XX_HSU_OFFSET(n)			((n) << 9)
#define LPC32XX_HSU_BREAK			(1 << 8)
#define LPC32XX_HSU_ERR_INT_EN			(1 << 7)
#define LPC32XX_HSU_RX_INT_EN			(1 << 6)
#define LPC32XX_HSU_TX_INT_EN			(1 << 5)
#define LPC32XX_HSU_RX_TL1B			(0x0 << 2)
#define LPC32XX_HSU_RX_TL4B			(0x1 << 2)
#define LPC32XX_HSU_RX_TL8B			(0x2 << 2)
#define LPC32XX_HSU_RX_TL16B			(0x3 << 2)
#define LPC32XX_HSU_RX_TL32B			(0x4 << 2)
#define LPC32XX_HSU_RX_TL48B			(0x5 << 2)
#define LPC32XX_HSU_TX_TLEMPTY			(0x0 << 0)
#define LPC32XX_HSU_TX_TL0B			(0x0 << 0)
#define LPC32XX_HSU_TX_TL4B			(0x1 << 0)
#define LPC32XX_HSU_TX_TL8B			(0x2 << 0)
#define LPC32XX_HSU_TX_TL16B			(0x3 << 0)

#define MODNAME "lpc32xx_hsuart"

struct lpc32xx_hsuart_port {
	struct uart_port port;
};

#define FIFO_READ_LIMIT 128
#define MAX_PORTS 3
#define LPC32XX_TTY_NAME "ttyTX"
static struct lpc32xx_hsuart_port lpc32xx_hs_ports[MAX_PORTS];

#ifdef CONFIG_SERIAL_HS_LPC32XX_CONSOLE
static void wait_for_xmit_empty(struct uart_port *port)
{
	unsigned int timeout = 10000;

	do {
		if (LPC32XX_HSU_TX_LEV(readl(LPC32XX_HSUART_LEVEL(
							port->membase))) == 0)
			break;
		if (--timeout == 0)
			break;
		udelay(1);
	} while (1);
}

static void wait_for_xmit_ready(struct uart_port *port)
{
	unsigned int timeout = 10000;

	while (1) {
		if (LPC32XX_HSU_TX_LEV(readl(LPC32XX_HSUART_LEVEL(
							port->membase))) < 32)
			break;
		if (--timeout == 0)
			break;
		udelay(1);
	}
}

static void lpc32xx_hsuart_console_putchar(struct uart_port *port, int ch)
{
	wait_for_xmit_ready(port);
	writel((u32)ch, LPC32XX_HSUART_FIFO(port->membase));
}

static void lpc32xx_hsuart_console_write(struct console *co, const char *s,
					 unsigned int count)
{
	struct lpc32xx_hsuart_port *up = &lpc32xx_hs_ports[co->index];
	unsigned long flags;
	int locked = 1;

	touch_nmi_watchdog();
	local_irq_save(flags);
	if (up->port.sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&up->port.lock);
	else
		spin_lock(&up->port.lock);

	uart_console_write(&up->port, s, count, lpc32xx_hsuart_console_putchar);
	wait_for_xmit_empty(&up->port);

	if (locked)
		spin_unlock(&up->port.lock);
	local_irq_restore(flags);
}

static int __init lpc32xx_hsuart_console_setup(struct console *co,
					       char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index >= MAX_PORTS)
		co->index = 0;

	port = &lpc32xx_hs_ports[co->index].port;
	if (!port->membase)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver lpc32xx_hsuart_reg;
static struct console lpc32xx_hsuart_console = {
	.name		= LPC32XX_TTY_NAME,
	.write		= lpc32xx_hsuart_console_write,
	.device		= uart_console_device,
	.setup		= lpc32xx_hsuart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &lpc32xx_hsuart_reg,
};

static int __init lpc32xx_hsuart_console_init(void)
{
	register_console(&lpc32xx_hsuart_console);
	return 0;
}
console_initcall(lpc32xx_hsuart_console_init);

#define LPC32XX_HSUART_CONSOLE (&lpc32xx_hsuart_console)
#else
#define LPC32XX_HSUART_CONSOLE NULL
#endif

static struct uart_driver lpc32xx_hs_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= MODNAME,
	.dev_name	= LPC32XX_TTY_NAME,
	.nr		= MAX_PORTS,
	.cons		= LPC32XX_HSUART_CONSOLE,
};
static int uarts_registered;

static unsigned int __serial_get_clock_div(unsigned long uartclk,
					   unsigned long rate)
{
	u32 div, goodrate, hsu_rate, l_hsu_rate, comprate;
	u32 rate_diff;

	/* Find the closest divider to get the desired clock rate */
	div = uartclk / rate;
	goodrate = hsu_rate = (div / 14) - 1;
	if (hsu_rate != 0)
		hsu_rate--;

	/* Tweak divider */
	l_hsu_rate = hsu_rate + 3;
	rate_diff = 0xFFFFFFFF;

	while (hsu_rate < l_hsu_rate) {
		comprate = uartclk / ((hsu_rate + 1) * 14);
		if (abs(comprate - rate) < rate_diff) {
			goodrate = hsu_rate;
			rate_diff = abs(comprate - rate);
		}

		hsu_rate++;
	}
	if (hsu_rate > 0xFF)
		hsu_rate = 0xFF;

	return goodrate;
}

static void __serial_uart_flush(struct uart_port *port)
{
	u32 tmp;
	int cnt = 0;

	while ((readl(LPC32XX_HSUART_LEVEL(port->membase)) > 0) &&
	       (cnt++ < FIFO_READ_LIMIT))
		tmp = readl(LPC32XX_HSUART_FIFO(port->membase));
}

static void __serial_lpc32xx_rx(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	unsigned int tmp, flag;

	/* Read data from FIFO and push into terminal */
	tmp = readl(LPC32XX_HSUART_FIFO(port->membase));
	while (!(tmp & LPC32XX_HSU_RX_EMPTY)) {
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (tmp & LPC32XX_HSU_ERROR_DATA) {
			/* Framing error */
			writel(LPC32XX_HSU_FE_INT,
			       LPC32XX_HSUART_IIR(port->membase));
			port->icount.frame++;
			flag = TTY_FRAME;
			tty_insert_flip_char(tport, 0, TTY_FRAME);
		}

		tty_insert_flip_char(tport, (tmp & 0xFF), flag);

		tmp = readl(LPC32XX_HSUART_FIFO(port->membase));
	}

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tport);
	spin_lock(&port->lock);
}

static void __serial_lpc32xx_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int tmp;

	if (port->x_char) {
		writel((u32)port->x_char, LPC32XX_HSUART_FIFO(port->membase));
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		goto exit_tx;

	/* Transfer data */
	while (LPC32XX_HSU_TX_LEV(readl(
		LPC32XX_HSUART_LEVEL(port->membase))) < 64) {
		writel((u32) xmit->buf[xmit->tail],
		       LPC32XX_HSUART_FIFO(port->membase));
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

exit_tx:
	if (uart_circ_empty(xmit)) {
		tmp = readl(LPC32XX_HSUART_CTRL(port->membase));
		tmp &= ~LPC32XX_HSU_TX_INT_EN;
		writel(tmp, LPC32XX_HSUART_CTRL(port->membase));
	}
}

static irqreturn_t serial_lpc32xx_interrupt(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct tty_port *tport = &port->state->port;
	u32 status;

	spin_lock(&port->lock);

	/* Read UART status and clear latched interrupts */
	status = readl(LPC32XX_HSUART_IIR(port->membase));

	if (status & LPC32XX_HSU_BRK_INT) {
		/* Break received */
		writel(LPC32XX_HSU_BRK_INT, LPC32XX_HSUART_IIR(port->membase));
		port->icount.brk++;
		uart_handle_break(port);
	}

	/* Framing error */
	if (status & LPC32XX_HSU_FE_INT)
		writel(LPC32XX_HSU_FE_INT, LPC32XX_HSUART_IIR(port->membase));

	if (status & LPC32XX_HSU_RX_OE_INT) {
		/* Receive FIFO overrun */
		writel(LPC32XX_HSU_RX_OE_INT,
		       LPC32XX_HSUART_IIR(port->membase));
		port->icount.overrun++;
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
		tty_schedule_flip(tport);
	}

	/* Data received? */
	if (status & (LPC32XX_HSU_RX_TIMEOUT_INT | LPC32XX_HSU_RX_TRIG_INT))
		__serial_lpc32xx_rx(port);

	/* Transmit data request? */
	if ((status & LPC32XX_HSU_TX_INT) && (!uart_tx_stopped(port))) {
		writel(LPC32XX_HSU_TX_INT, LPC32XX_HSUART_IIR(port->membase));
		__serial_lpc32xx_tx(port);
	}

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

/* port->lock is not held.  */
static unsigned int serial_lpc32xx_tx_empty(struct uart_port *port)
{
	unsigned int ret = 0;

	if (LPC32XX_HSU_TX_LEV(readl(LPC32XX_HSUART_LEVEL(port->membase))) == 0)
		ret = TIOCSER_TEMT;

	return ret;
}

/* port->lock held by caller.  */
static void serial_lpc32xx_set_mctrl(struct uart_port *port,
				     unsigned int mctrl)
{
	/* No signals are supported on HS UARTs */
}

/* port->lock is held by caller and interrupts are disabled.  */
static unsigned int serial_lpc32xx_get_mctrl(struct uart_port *port)
{
	/* No signals are supported on HS UARTs */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/* port->lock held by caller.  */
static void serial_lpc32xx_stop_tx(struct uart_port *port)
{
	u32 tmp;

	tmp = readl(LPC32XX_HSUART_CTRL(port->membase));
	tmp &= ~LPC32XX_HSU_TX_INT_EN;
	writel(tmp, LPC32XX_HSUART_CTRL(port->membase));
}

/* port->lock held by caller.  */
static void serial_lpc32xx_start_tx(struct uart_port *port)
{
	u32 tmp;

	__serial_lpc32xx_tx(port);
	tmp = readl(LPC32XX_HSUART_CTRL(port->membase));
	tmp |= LPC32XX_HSU_TX_INT_EN;
	writel(tmp, LPC32XX_HSUART_CTRL(port->membase));
}

/* port->lock held by caller.  */
static void serial_lpc32xx_stop_rx(struct uart_port *port)
{
	u32 tmp;

	tmp = readl(LPC32XX_HSUART_CTRL(port->membase));
	tmp &= ~(LPC32XX_HSU_RX_INT_EN | LPC32XX_HSU_ERR_INT_EN);
	writel(tmp, LPC32XX_HSUART_CTRL(port->membase));

	writel((LPC32XX_HSU_BRK_INT | LPC32XX_HSU_RX_OE_INT |
		LPC32XX_HSU_FE_INT), LPC32XX_HSUART_IIR(port->membase));
}

/* port->lock is not held.  */
static void serial_lpc32xx_break_ctl(struct uart_port *port,
				     int break_state)
{
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&port->lock, flags);
	tmp = readl(LPC32XX_HSUART_CTRL(port->membase));
	if (break_state != 0)
		tmp |= LPC32XX_HSU_BREAK;
	else
		tmp &= ~LPC32XX_HSU_BREAK;
	writel(tmp, LPC32XX_HSUART_CTRL(port->membase));
	spin_unlock_irqrestore(&port->lock, flags);
}

/* LPC3250 Errata HSUART.1: Hang workaround via loopback mode on inactivity */
static void lpc32xx_loopback_set(resource_size_t mapbase, int state)
{
	int bit;
	u32 tmp;

	switch (mapbase) {
	case LPC32XX_HS_UART1_BASE:
		bit = 0;
		break;
	case LPC32XX_HS_UART2_BASE:
		bit = 1;
		break;
	case LPC32XX_HS_UART7_BASE:
		bit = 6;
		break;
	default:
		WARN(1, "lpc32xx_hs: Warning: Unknown port at %08x\n", mapbase);
		return;
	}

	tmp = readl(LPC32XX_UARTCTL_CLOOP);
	if (state)
		tmp |= (1 << bit);
	else
		tmp &= ~(1 << bit);
	writel(tmp, LPC32XX_UARTCTL_CLOOP);
}

/* port->lock is not held.  */
static int serial_lpc32xx_startup(struct uart_port *port)
{
	int retval;
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&port->lock, flags);

	__serial_uart_flush(port);

	writel((LPC32XX_HSU_TX_INT | LPC32XX_HSU_FE_INT |
		LPC32XX_HSU_BRK_INT | LPC32XX_HSU_RX_OE_INT),
	       LPC32XX_HSUART_IIR(port->membase));

	writel(0xFF, LPC32XX_HSUART_RATE(port->membase));

	/*
	 * Set receiver timeout, HSU offset of 20, no break, no interrupts,
	 * and default FIFO trigger levels
	 */
	tmp = LPC32XX_HSU_TX_TL8B | LPC32XX_HSU_RX_TL32B |
		LPC32XX_HSU_OFFSET(20) | LPC32XX_HSU_TMO_INACT_4B;
	writel(tmp, LPC32XX_HSUART_CTRL(port->membase));

	lpc32xx_loopback_set(port->mapbase, 0); /* get out of loopback mode */

	spin_unlock_irqrestore(&port->lock, flags);

	retval = request_irq(port->irq, serial_lpc32xx_interrupt,
			     0, MODNAME, port);
	if (!retval)
		writel((tmp | LPC32XX_HSU_RX_INT_EN | LPC32XX_HSU_ERR_INT_EN),
		       LPC32XX_HSUART_CTRL(port->membase));

	return retval;
}

/* port->lock is not held.  */
static void serial_lpc32xx_shutdown(struct uart_port *port)
{
	u32 tmp;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	tmp = LPC32XX_HSU_TX_TL8B | LPC32XX_HSU_RX_TL32B |
		LPC32XX_HSU_OFFSET(20) | LPC32XX_HSU_TMO_INACT_4B;
	writel(tmp, LPC32XX_HSUART_CTRL(port->membase));

	lpc32xx_loopback_set(port->mapbase, 1); /* go to loopback mode */

	spin_unlock_irqrestore(&port->lock, flags);

	free_irq(port->irq, port);
}

/* port->lock is not held.  */
static void serial_lpc32xx_set_termios(struct uart_port *port,
				       struct ktermios *termios,
				       struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud, quot;
	u32 tmp;

	/* Always 8-bit, no parity, 1 stop bit */
	termios->c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
	termios->c_cflag |= CS8;

	termios->c_cflag &= ~(HUPCL | CMSPAR | CLOCAL | CRTSCTS);

	baud = uart_get_baud_rate(port, termios, old, 0,
				  port->uartclk / 14);

	quot = __serial_get_clock_div(port->uartclk, baud);

	spin_lock_irqsave(&port->lock, flags);

	/* Ignore characters? */
	tmp = readl(LPC32XX_HSUART_CTRL(port->membase));
	if ((termios->c_cflag & CREAD) == 0)
		tmp &= ~(LPC32XX_HSU_RX_INT_EN | LPC32XX_HSU_ERR_INT_EN);
	else
		tmp |= LPC32XX_HSU_RX_INT_EN | LPC32XX_HSU_ERR_INT_EN;
	writel(tmp, LPC32XX_HSUART_CTRL(port->membase));

	writel(quot, LPC32XX_HSUART_RATE(port->membase));

	uart_update_timeout(port, termios->c_cflag, baud);

	spin_unlock_irqrestore(&port->lock, flags);

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);
}

static const char *serial_lpc32xx_type(struct uart_port *port)
{
	return MODNAME;
}

static void serial_lpc32xx_release_port(struct uart_port *port)
{
	if ((port->iotype == UPIO_MEM32) && (port->mapbase)) {
		if (port->flags & UPF_IOREMAP) {
			iounmap(port->membase);
			port->membase = NULL;
		}

		release_mem_region(port->mapbase, SZ_4K);
	}
}

static int serial_lpc32xx_request_port(struct uart_port *port)
{
	int ret = -ENODEV;

	if ((port->iotype == UPIO_MEM32) && (port->mapbase)) {
		ret = 0;

		if (!request_mem_region(port->mapbase, SZ_4K, MODNAME))
			ret = -EBUSY;
		else if (port->flags & UPF_IOREMAP) {
			port->membase = ioremap(port->mapbase, SZ_4K);
			if (!port->membase) {
				release_mem_region(port->mapbase, SZ_4K);
				ret = -ENOMEM;
			}
		}
	}

	return ret;
}

static void serial_lpc32xx_config_port(struct uart_port *port, int uflags)
{
	int ret;

	ret = serial_lpc32xx_request_port(port);
	if (ret < 0)
		return;
	port->type = PORT_UART00;
	port->fifosize = 64;

	__serial_uart_flush(port);

	writel((LPC32XX_HSU_TX_INT | LPC32XX_HSU_FE_INT |
		LPC32XX_HSU_BRK_INT | LPC32XX_HSU_RX_OE_INT),
	       LPC32XX_HSUART_IIR(port->membase));

	writel(0xFF, LPC32XX_HSUART_RATE(port->membase));

	/* Set receiver timeout, HSU offset of 20, no break, no interrupts,
	   and default FIFO trigger levels */
	writel(LPC32XX_HSU_TX_TL8B | LPC32XX_HSU_RX_TL32B |
	       LPC32XX_HSU_OFFSET(20) | LPC32XX_HSU_TMO_INACT_4B,
	       LPC32XX_HSUART_CTRL(port->membase));
}

static int serial_lpc32xx_verify_port(struct uart_port *port,
				      struct serial_struct *ser)
{
	int ret = 0;

	if (ser->type != PORT_UART00)
		ret = -EINVAL;

	return ret;
}

static const struct uart_ops serial_lpc32xx_pops = {
	.tx_empty	= serial_lpc32xx_tx_empty,
	.set_mctrl	= serial_lpc32xx_set_mctrl,
	.get_mctrl	= serial_lpc32xx_get_mctrl,
	.stop_tx	= serial_lpc32xx_stop_tx,
	.start_tx	= serial_lpc32xx_start_tx,
	.stop_rx	= serial_lpc32xx_stop_rx,
	.break_ctl	= serial_lpc32xx_break_ctl,
	.startup	= serial_lpc32xx_startup,
	.shutdown	= serial_lpc32xx_shutdown,
	.set_termios	= serial_lpc32xx_set_termios,
	.type		= serial_lpc32xx_type,
	.release_port	= serial_lpc32xx_release_port,
	.request_port	= serial_lpc32xx_request_port,
	.config_port	= serial_lpc32xx_config_port,
	.verify_port	= serial_lpc32xx_verify_port,
};

/*
 * Register a set of serial devices attached to a platform device
 */
static int serial_hs_lpc32xx_probe(struct platform_device *pdev)
{
	struct lpc32xx_hsuart_port *p = &lpc32xx_hs_ports[uarts_registered];
	int ret = 0;
	struct resource *res;

	if (uarts_registered >= MAX_PORTS) {
		dev_err(&pdev->dev,
			"Error: Number of possible ports exceeded (%d)!\n",
			uarts_registered + 1);
		return -ENXIO;
	}

	memset(p, 0, sizeof(*p));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Error getting mem resource for HS UART port %d\n",
			uarts_registered);
		return -ENXIO;
	}
	p->port.mapbase = res->start;
	p->port.membase = NULL;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error getting irq for HS UART port %d\n",
			uarts_registered);
		return ret;
	}
	p->port.irq = ret;

	p->port.iotype = UPIO_MEM32;
	p->port.uartclk = LPC32XX_MAIN_OSC_FREQ;
	p->port.regshift = 2;
	p->port.flags = UPF_BOOT_AUTOCONF | UPF_FIXED_PORT | UPF_IOREMAP;
	p->port.dev = &pdev->dev;
	p->port.ops = &serial_lpc32xx_pops;
	p->port.line = uarts_registered++;
	spin_lock_init(&p->port.lock);

	/* send port to loopback mode by default */
	lpc32xx_loopback_set(p->port.mapbase, 1);

	ret = uart_add_one_port(&lpc32xx_hs_reg, &p->port);

	platform_set_drvdata(pdev, p);

	return ret;
}

/*
 * Remove serial ports registered against a platform device.
 */
static int serial_hs_lpc32xx_remove(struct platform_device *pdev)
{
	struct lpc32xx_hsuart_port *p = platform_get_drvdata(pdev);

	uart_remove_one_port(&lpc32xx_hs_reg, &p->port);

	return 0;
}


#ifdef CONFIG_PM
static int serial_hs_lpc32xx_suspend(struct platform_device *pdev,
				     pm_message_t state)
{
	struct lpc32xx_hsuart_port *p = platform_get_drvdata(pdev);

	uart_suspend_port(&lpc32xx_hs_reg, &p->port);

	return 0;
}

static int serial_hs_lpc32xx_resume(struct platform_device *pdev)
{
	struct lpc32xx_hsuart_port *p = platform_get_drvdata(pdev);

	uart_resume_port(&lpc32xx_hs_reg, &p->port);

	return 0;
}
#else
#define serial_hs_lpc32xx_suspend	NULL
#define serial_hs_lpc32xx_resume	NULL
#endif

static const struct of_device_id serial_hs_lpc32xx_dt_ids[] = {
	{ .compatible = "nxp,lpc3220-hsuart" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, serial_hs_lpc32xx_dt_ids);

static struct platform_driver serial_hs_lpc32xx_driver = {
	.probe		= serial_hs_lpc32xx_probe,
	.remove		= serial_hs_lpc32xx_remove,
	.suspend	= serial_hs_lpc32xx_suspend,
	.resume		= serial_hs_lpc32xx_resume,
	.driver		= {
		.name	= MODNAME,
		.of_match_table	= serial_hs_lpc32xx_dt_ids,
	},
};

static int __init lpc32xx_hsuart_init(void)
{
	int ret;

	ret = uart_register_driver(&lpc32xx_hs_reg);
	if (ret)
		return ret;

	ret = platform_driver_register(&serial_hs_lpc32xx_driver);
	if (ret)
		uart_unregister_driver(&lpc32xx_hs_reg);

	return ret;
}

static void __exit lpc32xx_hsuart_exit(void)
{
	platform_driver_unregister(&serial_hs_lpc32xx_driver);
	uart_unregister_driver(&lpc32xx_hs_reg);
}

module_init(lpc32xx_hsuart_init);
module_exit(lpc32xx_hsuart_exit);

MODULE_AUTHOR("Kevin Wells <kevin.wells@nxp.com>");
MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("NXP LPC32XX High Speed UART driver");
MODULE_LICENSE("GPL");
