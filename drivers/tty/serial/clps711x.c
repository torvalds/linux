/*
 *  Driver for CLPS711x serial ports
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
 */

#if defined(CONFIG_SERIAL_CLPS711X_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#define UART_CLPS711X_NAME	"uart-clps711x"
#define UART_CLPS711X_NR	2
#define UART_CLPS711X_MAJOR	204
#define UART_CLPS711X_MINOR	40

#define UBRLCR(port)		((port)->line ? UBRLCR2 : UBRLCR1)
#define UARTDR(port)		((port)->line ? UARTDR2 : UARTDR1)
#define SYSFLG(port)		((port)->line ? SYSFLG2 : SYSFLG1)
#define SYSCON(port)		((port)->line ? SYSCON2 : SYSCON1)
#define TX_IRQ(port)		((port)->line ? IRQ_UTXINT2 : IRQ_UTXINT1)
#define RX_IRQ(port)		((port)->line ? IRQ_URXINT2 : IRQ_URXINT1)

struct clps711x_port {
	struct uart_driver	uart;
	struct clk		*uart_clk;
	struct uart_port	port[UART_CLPS711X_NR];
	int			tx_enabled[UART_CLPS711X_NR];
#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE
	struct console		console;
#endif
};

static void clps711xuart_stop_tx(struct uart_port *port)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);

	if (s->tx_enabled[port->line]) {
		disable_irq(TX_IRQ(port));
		s->tx_enabled[port->line] = 0;
	}
}

static void clps711xuart_start_tx(struct uart_port *port)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);

	if (!s->tx_enabled[port->line]) {
		enable_irq(TX_IRQ(port));
		s->tx_enabled[port->line] = 1;
	}
}

static void clps711xuart_stop_rx(struct uart_port *port)
{
	disable_irq(RX_IRQ(port));
}

static void clps711xuart_enable_ms(struct uart_port *port)
{
}

static irqreturn_t clps711xuart_int_rx(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct tty_struct *tty = tty_port_tty_get(&port->state->port);
	unsigned int status, ch, flg;

	if (!tty)
		return IRQ_HANDLED;

	for (;;) {
		status = clps_readl(SYSFLG(port));
		if (status & SYSFLG_URXFE)
			break;

		ch = clps_readw(UARTDR(port));
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

	tty_flip_buffer_push(tty);

	tty_kref_put(tty);

	return IRQ_HANDLED;
}

static irqreturn_t clps711xuart_int_tx(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct clps711x_port *s = dev_get_drvdata(port->dev);
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		clps_writel(port->x_char, UARTDR(port));
		port->icount.tx++;
		port->x_char = 0;
		return IRQ_HANDLED;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		disable_irq_nosync(TX_IRQ(port));
		s->tx_enabled[port->line] = 0;
		return IRQ_HANDLED;
	}

	while (!uart_circ_empty(xmit)) {
		clps_writew(xmit->buf[xmit->tail], UARTDR(port));
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (clps_readl(SYSFLG(port) & SYSFLG_UTXFF))
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	return IRQ_HANDLED;
}

static unsigned int clps711xuart_tx_empty(struct uart_port *port)
{
	unsigned int status = clps_readl(SYSFLG(port));
	return status & SYSFLG_UBUSY ? 0 : TIOCSER_TEMT;
}

static unsigned int clps711xuart_get_mctrl(struct uart_port *port)
{
	unsigned int status, result = 0;

	if (port->line == 0) {
		status = clps_readl(SYSFLG1);
		if (status & SYSFLG1_DCD)
			result |= TIOCM_CAR;
		if (status & SYSFLG1_DSR)
			result |= TIOCM_DSR;
		if (status & SYSFLG1_CTS)
			result |= TIOCM_CTS;
	} else
		result = TIOCM_DSR | TIOCM_CTS | TIOCM_CAR;

	return result;
}

static void
clps711xuart_set_mctrl_null(struct uart_port *port, unsigned int mctrl)
{
}

static void clps711xuart_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;
	unsigned int ubrlcr;

	spin_lock_irqsave(&port->lock, flags);

	ubrlcr = clps_readl(UBRLCR(port));
	if (break_state)
		ubrlcr |= UBRLCR_BREAK;
	else
		ubrlcr &= ~UBRLCR_BREAK;
	clps_writel(ubrlcr, UBRLCR(port));

	spin_unlock_irqrestore(&port->lock, flags);
}

static int clps711xuart_startup(struct uart_port *port)
{
	struct clps711x_port *s = dev_get_drvdata(port->dev);
	unsigned int syscon;
	int retval;

	s->tx_enabled[port->line] = 1;

	/*
	 * Allocate the IRQs
	 */
	retval = request_irq(TX_IRQ(port), clps711xuart_int_tx, 0,
			     "clps711xuart_tx", port);
	if (retval)
		return retval;

	retval = request_irq(RX_IRQ(port), clps711xuart_int_rx, 0,
			     "clps711xuart_rx", port);
	if (retval) {
		free_irq(TX_IRQ(port), port);
		return retval;
	}

	/*
	 * enable the port
	 */
	syscon = clps_readl(SYSCON(port));
	syscon |= SYSCON_UARTEN;
	clps_writel(syscon, SYSCON(port));

	return 0;
}

static void clps711xuart_shutdown(struct uart_port *port)
{
	unsigned int ubrlcr, syscon;

	/*
	 * Free the interrupt
	 */
	free_irq(TX_IRQ(port), port);	/* TX interrupt */
	free_irq(RX_IRQ(port), port);	/* RX interrupt */

	/*
	 * disable the port
	 */
	syscon = clps_readl(SYSCON(port));
	syscon &= ~SYSCON_UARTEN;
	clps_writel(syscon, SYSCON(port));

	/*
	 * disable break condition and fifos
	 */
	ubrlcr = clps_readl(UBRLCR(port));
	ubrlcr &= ~(UBRLCR_FIFOEN | UBRLCR_BREAK);
	clps_writel(ubrlcr, UBRLCR(port));
}

static void
clps711xuart_set_termios(struct uart_port *port, struct ktermios *termios,
			 struct ktermios *old)
{
	unsigned int ubrlcr, baud, quot;
	unsigned long flags;

	/*
	 * We don't implement CREAD.
	 */
	termios->c_cflag |= CREAD;

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
	default: // CS8
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

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = UARTDR_OVERR;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UARTDR_PARERR | UARTDR_FRMERR;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= UARTDR_FRMERR | UARTDR_PARERR;
	if (termios->c_iflag & IGNBRK) {
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns to (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= UARTDR_OVERR;
	}

	quot -= 1;

	clps_writel(ubrlcr | quot, UBRLCR(port));

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *clps711xuart_type(struct uart_port *port)
{
	return port->type == PORT_CLPS711X ? "CLPS711x" : NULL;
}

/*
 * Configure/autoconfigure the port.
 */
static void clps711xuart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_CLPS711X;
}

static void clps711xuart_release_port(struct uart_port *port)
{
}

static int clps711xuart_request_port(struct uart_port *port)
{
	return 0;
}

static struct uart_ops uart_clps711x_ops = {
	.tx_empty	= clps711xuart_tx_empty,
	.set_mctrl	= clps711xuart_set_mctrl_null,
	.get_mctrl	= clps711xuart_get_mctrl,
	.stop_tx	= clps711xuart_stop_tx,
	.start_tx	= clps711xuart_start_tx,
	.stop_rx	= clps711xuart_stop_rx,
	.enable_ms	= clps711xuart_enable_ms,
	.break_ctl	= clps711xuart_break_ctl,
	.startup	= clps711xuart_startup,
	.shutdown	= clps711xuart_shutdown,
	.set_termios	= clps711xuart_set_termios,
	.type		= clps711xuart_type,
	.config_port	= clps711xuart_config_port,
	.release_port	= clps711xuart_release_port,
	.request_port	= clps711xuart_request_port,
};

#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE
static void uart_clps711x_console_putchar(struct uart_port *port, int ch)
{
	while (clps_readl(SYSFLG(port)) & SYSFLG_UTXFF)
		barrier();

	clps_writew(ch, UARTDR(port));
}

static void uart_clps711x_console_write(struct console *co, const char *c,
					unsigned n)
{
	struct clps711x_port *s = (struct clps711x_port *)co->data;
	struct uart_port *port = &s->port[co->index];
	u32 syscon;

	/* Ensure that the port is enabled */
	syscon = clps_readl(SYSCON(port));
	clps_writel(syscon | SYSCON_UARTEN, SYSCON(port));

	uart_console_write(port, c, n, uart_clps711x_console_putchar);

	/* Wait for transmitter to become empty */
	while (clps_readl(SYSFLG(port)) & SYSFLG_UBUSY)
		barrier();

	/* Restore the uart state */
	clps_writel(syscon, SYSCON(port));
}

static void uart_clps711x_console_get_options(struct uart_port *port,
					      int *baud, int *parity,
					      int *bits)
{
	if (clps_readl(SYSCON(port)) & SYSCON_UARTEN) {
		unsigned int ubrlcr, quot;

		ubrlcr = clps_readl(UBRLCR(port));

		*parity = 'n';
		if (ubrlcr & UBRLCR_PRTEN) {
			if (ubrlcr & UBRLCR_EVENPRT)
				*parity = 'e';
			else
				*parity = 'o';
		}

		if ((ubrlcr & UBRLCR_WRDLEN_MASK) == UBRLCR_WRDLEN7)
			*bits = 7;
		else
			*bits = 8;

		quot = ubrlcr & UBRLCR_BAUD_MASK;
		*baud = port->uartclk / (16 * (quot + 1));
	}
}

static int uart_clps711x_console_setup(struct console *co, char *options)
{
	int baud = 38400, bits = 8, parity = 'n', flow = 'n';
	struct clps711x_port *s = (struct clps711x_port *)co->data;
	struct uart_port *port = &s->port[(co->index > 0) ? co->index : 0];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		uart_clps711x_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}
#endif

static int __devinit uart_clps711x_probe(struct platform_device *pdev)
{
	struct clps711x_port *s;
	int ret, i;

	s = devm_kzalloc(&pdev->dev, sizeof(struct clps711x_port), GFP_KERNEL);
	if (!s) {
		dev_err(&pdev->dev, "Error allocating port structure\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, s);

	s->uart_clk = devm_clk_get(&pdev->dev, "uart");
	if (IS_ERR(s->uart_clk)) {
		dev_err(&pdev->dev, "Can't get UART clocks\n");
		ret = PTR_ERR(s->uart_clk);
		goto err_out;
	}

	s->uart.owner		= THIS_MODULE;
	s->uart.dev_name	= "ttyCL";
	s->uart.major		= UART_CLPS711X_MAJOR;
	s->uart.minor		= UART_CLPS711X_MINOR;
	s->uart.nr		= UART_CLPS711X_NR;
#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE
	s->uart.cons		= &s->console;
	s->uart.cons->device	= uart_console_device;
	s->uart.cons->write	= uart_clps711x_console_write;
	s->uart.cons->setup	= uart_clps711x_console_setup;
	s->uart.cons->flags	= CON_PRINTBUFFER;
	s->uart.cons->index	= -1;
	s->uart.cons->data	= s;
	strcpy(s->uart.cons->name, "ttyCL");
#endif
	ret = uart_register_driver(&s->uart);
	if (ret) {
		dev_err(&pdev->dev, "Registering UART driver failed\n");
		devm_clk_put(&pdev->dev, s->uart_clk);
		goto err_out;
	}

	for (i = 0; i < UART_CLPS711X_NR; i++) {
		s->port[i].line		= i;
		s->port[i].dev		= &pdev->dev;
		s->port[i].irq		= TX_IRQ(&s->port[i]);
		s->port[i].iobase	= SYSCON(&s->port[i]);
		s->port[i].type		= PORT_CLPS711X;
		s->port[i].fifosize	= 16;
		s->port[i].flags	= UPF_SKIP_TEST | UPF_FIXED_TYPE;
		s->port[i].uartclk	= clk_get_rate(s->uart_clk);
		s->port[i].ops		= &uart_clps711x_ops;
		WARN_ON(uart_add_one_port(&s->uart, &s->port[i]));
	}

	return 0;

err_out:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int __devexit uart_clps711x_remove(struct platform_device *pdev)
{
	struct clps711x_port *s = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < UART_CLPS711X_NR; i++)
		uart_remove_one_port(&s->uart, &s->port[i]);

	devm_clk_put(&pdev->dev, s->uart_clk);
	uart_unregister_driver(&s->uart);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver clps711x_uart_driver = {
	.driver = {
		.name	= UART_CLPS711X_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	= uart_clps711x_probe,
	.remove	= __devexit_p(uart_clps711x_remove),
};
module_platform_driver(clps711x_uart_driver);

static struct platform_device clps711x_uart_device = {
	.name	= UART_CLPS711X_NAME,
};

static int __init uart_clps711x_init(void)
{
	return platform_device_register(&clps711x_uart_device);
}
module_init(uart_clps711x_init);

static void __exit uart_clps711x_exit(void)
{
	platform_device_unregister(&clps711x_uart_device);
}
module_exit(uart_clps711x_exit);

MODULE_AUTHOR("Deep Blue Solutions Ltd");
MODULE_DESCRIPTION("CLPS711X serial driver");
MODULE_LICENSE("GPL");
