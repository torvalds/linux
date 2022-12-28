// SPDX-License-Identifier: GPL-2.0+
/*
 *  Driver for CLPS711x serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/console.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/clps711x.h>

#include "serial_mctrl_gpio.h"

#define UART_CLPS711X_DEVNAME	"ttyCL"
#define UART_CLPS711X_NR	2
#define UART_CLPS711X_MAJOR	204
#define UART_CLPS711X_MINOR	40

#define UARTDR_OFFSET		(0x00)
#define UBRLCR_OFFSET		(0x40)

#define UARTDR_FRMERR		(1 << 8)
#define UARTDR_PARERR		(1 << 9)
#define UARTDR_OVERR		(1 << 10)

#define UBRLCR_BAUD_MASK	((1 << 12) - 1)
#define UBRLCR_BREAK		(1 << 12)
#define UBRLCR_PRTEN		(1 << 13)
#define UBRLCR_EVENPRT		(1 << 14)
#define UBRLCR_XSTOP		(1 << 15)
#define UBRLCR_FIFOEN		(1 << 16)
#define UBRLCR_WRDLEN5		(0 << 17)
#define UBRLCR_WRDLEN6		(1 << 17)
#define UBRLCR_WRDLEN7		(2 << 17)
#define UBRLCR_WRDLEN8		(3 << 17)
#define UBRLCR_WRDLEN_MASK	(3 << 17)

struct clps711x_port {
	struct uart_port	port;
	unsigned int		tx_enabled;
	int			rx_irq;
	struct regmap		*syscon;
	struct mctrl_gpios	*gpios;
};

static struct uart_driver clps711x_uart = {
	.owner		= THIS_MODULE,
	.driver_name	= UART_CLPS711X_DEVNAME,
	.dev_name	= UART_CLPS711X_DEVNAME,
	.major		= UART_CLPS711X_MAJOR,
	.minor		= UART_CLPS711X_MINOR,
	.nr		= UART_CLPS711X_NR,
};

static void uart_clps711x_stop_tx(struct uart_port *port)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);

	if (s->tx_enabled) {
		disable_irq(port->irq);
		s->tx_enabled = 0;
	}
}

static void uart_clps711x_start_tx(struct uart_port *port)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);

	if (!s->tx_enabled) {
		s->tx_enabled = 1;
		enable_irq(port->irq);
	}
}

static irqreturn_t uart_clps711x_int_rx(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct clps711x_port *s = dev_get_drvdata(port->dev);
	unsigned int status, flg;
	u16 ch;

	for (;;) {
		u32 sysflg = 0;

		regmap_read(s->syscon, SYSFLG_OFFSET, &sysflg);
		if (sysflg & SYSFLG_URXFE)
			break;

		ch = readw(port->membase + UARTDR_OFFSET);
		status = ch & (UARTDR_FRMERR | UARTDR_PARERR | UARTDR_OVERR);
		ch &= 0xff;

		port->icount.rx++;
		flg = TTY_NORMAL;

		if (unlikely(status)) {
			if (status & UARTDR_PARERR)
				port->icount.parity++;
			else if (status & UARTDR_FRMERR)
				port->icount.frame++;
			else if (status & UARTDR_OVERR)
				port->icount.overrun++;

			status &= port->read_status_mask;

			if (status & UARTDR_PARERR)
				flg = TTY_PARITY;
			else if (status & UARTDR_FRMERR)
				flg = TTY_FRAME;
			else if (status & UARTDR_OVERR)
				flg = TTY_OVERRUN;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;

		if (status & port->ignore_status_mask)
			continue;

		uart_insert_char(port, status, UARTDR_OVERR, ch, flg);
	}

	tty_flip_buffer_push(&port->state->port);

	return IRQ_HANDLED;
}

static irqreturn_t uart_clps711x_int_tx(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct clps711x_port *s = dev_get_drvdata(port->dev);
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		writew(port->x_char, port->membase + UARTDR_OFFSET);
		port->icount.tx++;
		port->x_char = 0;
		return IRQ_HANDLED;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		if (s->tx_enabled) {
			disable_irq_nosync(port->irq);
			s->tx_enabled = 0;
		}
		return IRQ_HANDLED;
	}

	while (!uart_circ_empty(xmit)) {
		u32 sysflg = 0;

		writew(xmit->buf[xmit->tail], port->membase + UARTDR_OFFSET);
		uart_xmit_advance(port, 1);

		regmap_read(s->syscon, SYSFLG_OFFSET, &sysflg);
		if (sysflg & SYSFLG_UTXFF)
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	return IRQ_HANDLED;
}

static unsigned int uart_clps711x_tx_empty(struct uart_port *port)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);
	u32 sysflg = 0;

	regmap_read(s->syscon, SYSFLG_OFFSET, &sysflg);

	return (sysflg & SYSFLG_UBUSY) ? 0 : TIOCSER_TEMT;
}

static unsigned int uart_clps711x_get_mctrl(struct uart_port *port)
{
	unsigned int result = TIOCM_DSR | TIOCM_CTS | TIOCM_CAR;
	struct clps711x_port *s = dev_get_drvdata(port->dev);

	return mctrl_gpio_get(s->gpios, &result);
}

static void uart_clps711x_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);

	mctrl_gpio_set(s->gpios, mctrl);
}

static void uart_clps711x_break_ctl(struct uart_port *port, int break_state)
{
	unsigned int ubrlcr;

	ubrlcr = readl(port->membase + UBRLCR_OFFSET);
	if (break_state)
		ubrlcr |= UBRLCR_BREAK;
	else
		ubrlcr &= ~UBRLCR_BREAK;
	writel(ubrlcr, port->membase + UBRLCR_OFFSET);
}

static void uart_clps711x_set_ldisc(struct uart_port *port,
				    struct ktermios *termios)
{
	if (!port->line) {
		struct clps711x_port *s = dev_get_drvdata(port->dev);

		regmap_update_bits(s->syscon, SYSCON_OFFSET, SYSCON1_SIREN,
				   (termios->c_line == N_IRDA) ? SYSCON1_SIREN : 0);
	}
}

static int uart_clps711x_startup(struct uart_port *port)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);

	/* Disable break */
	writel(readl(port->membase + UBRLCR_OFFSET) & ~UBRLCR_BREAK,
	       port->membase + UBRLCR_OFFSET);

	/* Enable the port */
	return regmap_update_bits(s->syscon, SYSCON_OFFSET,
				  SYSCON_UARTEN, SYSCON_UARTEN);
}

static void uart_clps711x_shutdown(struct uart_port *port)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);

	/* Disable the port */
	regmap_update_bits(s->syscon, SYSCON_OFFSET, SYSCON_UARTEN, 0);
}

static void uart_clps711x_set_termios(struct uart_port *port,
				      struct ktermios *termios,
				      const struct ktermios *old)
{
	u32 ubrlcr;
	unsigned int baud, quot;

	/* Mask termios capabilities we don't support */
	termios->c_cflag &= ~CMSPAR;
	termios->c_iflag &= ~(BRKINT | IGNBRK);

	/* Ask the core to calculate the divisor for us */
	baud = uart_get_baud_rate(port, termios, old, port->uartclk / 4096,
						      port->uartclk / 16);
	quot = uart_get_divisor(port, baud);

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		ubrlcr = UBRLCR_WRDLEN5;
		break;
	case CS6:
		ubrlcr = UBRLCR_WRDLEN6;
		break;
	case CS7:
		ubrlcr = UBRLCR_WRDLEN7;
		break;
	case CS8:
	default:
		ubrlcr = UBRLCR_WRDLEN8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		ubrlcr |= UBRLCR_XSTOP;

	if (termios->c_cflag & PARENB) {
		ubrlcr |= UBRLCR_PRTEN;
		if (!(termios->c_cflag & PARODD))
			ubrlcr |= UBRLCR_EVENPRT;
	}

	/* Enable FIFO */
	ubrlcr |= UBRLCR_FIFOEN;

	/* Set read status mask */
	port->read_status_mask = UARTDR_OVERR;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UARTDR_PARERR | UARTDR_FRMERR;

	/* Set status ignore mask */
	port->ignore_status_mask = 0;
	if (!(termios->c_cflag & CREAD))
		port->ignore_status_mask |= UARTDR_OVERR | UARTDR_PARERR |
					    UARTDR_FRMERR;

	uart_update_timeout(port, termios->c_cflag, baud);

	writel(ubrlcr | (quot - 1), port->membase + UBRLCR_OFFSET);
}

static const char *uart_clps711x_type(struct uart_port *port)
{
	return (port->type == PORT_CLPS711X) ? "CLPS711X" : NULL;
}

static void uart_clps711x_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_CLPS711X;
}

static void uart_clps711x_nop_void(struct uart_port *port)
{
}

static int uart_clps711x_nop_int(struct uart_port *port)
{
	return 0;
}

static const struct uart_ops uart_clps711x_ops = {
	.tx_empty	= uart_clps711x_tx_empty,
	.set_mctrl	= uart_clps711x_set_mctrl,
	.get_mctrl	= uart_clps711x_get_mctrl,
	.stop_tx	= uart_clps711x_stop_tx,
	.start_tx	= uart_clps711x_start_tx,
	.stop_rx	= uart_clps711x_nop_void,
	.break_ctl	= uart_clps711x_break_ctl,
	.set_ldisc	= uart_clps711x_set_ldisc,
	.startup	= uart_clps711x_startup,
	.shutdown	= uart_clps711x_shutdown,
	.set_termios	= uart_clps711x_set_termios,
	.type		= uart_clps711x_type,
	.config_port	= uart_clps711x_config_port,
	.release_port	= uart_clps711x_nop_void,
	.request_port	= uart_clps711x_nop_int,
};

#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE
static void uart_clps711x_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);
	u32 sysflg = 0;

	/* Wait for FIFO is not full */
	do {
		regmap_read(s->syscon, SYSFLG_OFFSET, &sysflg);
	} while (sysflg & SYSFLG_UTXFF);

	writew(ch, port->membase + UARTDR_OFFSET);
}

static void uart_clps711x_console_write(struct console *co, const char *c,
					unsigned n)
{
	struct uart_port *port = clps711x_uart.state[co->index].uart_port;
	struct clps711x_port *s = dev_get_drvdata(port->dev);
	u32 sysflg = 0;

	uart_console_write(port, c, n, uart_clps711x_console_putchar);

	/* Wait for transmitter to become empty */
	do {
		regmap_read(s->syscon, SYSFLG_OFFSET, &sysflg);
	} while (sysflg & SYSFLG_UBUSY);
}

static int uart_clps711x_console_setup(struct console *co, char *options)
{
	int baud = 38400, bits = 8, parity = 'n', flow = 'n';
	int ret, index = co->index;
	struct clps711x_port *s;
	struct uart_port *port;
	unsigned int quot;
	u32 ubrlcr;

	if (index < 0 || index >= UART_CLPS711X_NR)
		return -EINVAL;

	port = clps711x_uart.state[index].uart_port;
	if (!port)
		return -ENODEV;

	s = dev_get_drvdata(port->dev);

	if (!options) {
		u32 syscon = 0;

		regmap_read(s->syscon, SYSCON_OFFSET, &syscon);
		if (syscon & SYSCON_UARTEN) {
			ubrlcr = readl(port->membase + UBRLCR_OFFSET);

			if (ubrlcr & UBRLCR_PRTEN) {
				if (ubrlcr & UBRLCR_EVENPRT)
					parity = 'e';
				else
					parity = 'o';
			}

			if ((ubrlcr & UBRLCR_WRDLEN_MASK) == UBRLCR_WRDLEN7)
				bits = 7;

			quot = ubrlcr & UBRLCR_BAUD_MASK;
			baud = port->uartclk / (16 * (quot + 1));
		}
	} else
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	ret = uart_set_options(port, co, baud, parity, bits, flow);
	if (ret)
		return ret;

	return regmap_update_bits(s->syscon, SYSCON_OFFSET,
				  SYSCON_UARTEN, SYSCON_UARTEN);
}

static struct console clps711x_console = {
	.name	= UART_CLPS711X_DEVNAME,
	.device	= uart_console_device,
	.write	= uart_clps711x_console_write,
	.setup	= uart_clps711x_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};
#endif

static int uart_clps711x_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clps711x_port *s;
	struct resource *res;
	struct clk *uart_clk;
	int irq, ret;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	uart_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(uart_clk))
		return PTR_ERR(uart_clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	s->port.membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(s->port.membase))
		return PTR_ERR(s->port.membase);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	s->port.irq = irq;

	s->rx_irq = platform_get_irq(pdev, 1);
	if (s->rx_irq < 0)
		return s->rx_irq;

	s->syscon = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(s->syscon))
		return PTR_ERR(s->syscon);

	s->port.line		= of_alias_get_id(np, "serial");
	s->port.dev		= &pdev->dev;
	s->port.iotype		= UPIO_MEM32;
	s->port.mapbase		= res->start;
	s->port.type		= PORT_CLPS711X;
	s->port.fifosize	= 16;
	s->port.has_sysrq	= IS_ENABLED(CONFIG_SERIAL_CLPS711X_CONSOLE);
	s->port.flags		= UPF_SKIP_TEST | UPF_FIXED_TYPE;
	s->port.uartclk		= clk_get_rate(uart_clk);
	s->port.ops		= &uart_clps711x_ops;

	platform_set_drvdata(pdev, s);

	s->gpios = mctrl_gpio_init_noauto(&pdev->dev, 0);
	if (IS_ERR(s->gpios))
	    return PTR_ERR(s->gpios);

	ret = uart_add_one_port(&clps711x_uart, &s->port);
	if (ret)
		return ret;

	/* Disable port */
	if (!uart_console(&s->port))
		regmap_update_bits(s->syscon, SYSCON_OFFSET, SYSCON_UARTEN, 0);

	s->tx_enabled = 1;

	ret = devm_request_irq(&pdev->dev, s->port.irq, uart_clps711x_int_tx, 0,
			       dev_name(&pdev->dev), &s->port);
	if (ret) {
		uart_remove_one_port(&clps711x_uart, &s->port);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, s->rx_irq, uart_clps711x_int_rx, 0,
			       dev_name(&pdev->dev), &s->port);
	if (ret)
		uart_remove_one_port(&clps711x_uart, &s->port);

	return ret;
}

static int uart_clps711x_remove(struct platform_device *pdev)
{
	struct clps711x_port *s = platform_get_drvdata(pdev);

	return uart_remove_one_port(&clps711x_uart, &s->port);
}

static const struct of_device_id __maybe_unused clps711x_uart_dt_ids[] = {
	{ .compatible = "cirrus,ep7209-uart", },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_uart_dt_ids);

static struct platform_driver clps711x_uart_platform = {
	.driver = {
		.name		= "clps711x-uart",
		.of_match_table	= of_match_ptr(clps711x_uart_dt_ids),
	},
	.probe	= uart_clps711x_probe,
	.remove	= uart_clps711x_remove,
};

static int __init uart_clps711x_init(void)
{
	int ret;

#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE
	clps711x_uart.cons = &clps711x_console;
	clps711x_console.data = &clps711x_uart;
#endif

	ret = uart_register_driver(&clps711x_uart);
	if (ret)
		return ret;

	return platform_driver_register(&clps711x_uart_platform);
}
module_init(uart_clps711x_init);

static void __exit uart_clps711x_exit(void)
{
	platform_driver_unregister(&clps711x_uart_platform);
	uart_unregister_driver(&clps711x_uart);
}
module_exit(uart_clps711x_exit);

MODULE_AUTHOR("Deep Blue Solutions Ltd");
MODULE_DESCRIPTION("CLPS711X serial driver");
MODULE_LICENSE("GPL");
