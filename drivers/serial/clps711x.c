/*
 *  linux/drivers/char/clps711x.c
 *
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

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware/clps7111.h>

#define UART_NR		2

#define SERIAL_CLPS711X_MAJOR	204
#define SERIAL_CLPS711X_MINOR	40
#define SERIAL_CLPS711X_NR	UART_NR

/*
 * We use the relevant SYSCON register as a base address for these ports.
 */
#define UBRLCR(port)		((port)->iobase + UBRLCR1 - SYSCON1)
#define UARTDR(port)		((port)->iobase + UARTDR1 - SYSCON1)
#define SYSFLG(port)		((port)->iobase + SYSFLG1 - SYSCON1)
#define SYSCON(port)		((port)->iobase + SYSCON1 - SYSCON1)

#define TX_IRQ(port)		((port)->irq)
#define RX_IRQ(port)		((port)->irq + 1)

#define UART_ANY_ERR		(UARTDR_FRMERR | UARTDR_PARERR | UARTDR_OVERR)

#define tx_enabled(port)	((port)->unused[0])

static void clps711xuart_stop_tx(struct uart_port *port)
{
	if (tx_enabled(port)) {
		disable_irq(TX_IRQ(port));
		tx_enabled(port) = 0;
	}
}

static void clps711xuart_start_tx(struct uart_port *port)
{
	if (!tx_enabled(port)) {
		enable_irq(TX_IRQ(port));
		tx_enabled(port) = 1;
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
	struct tty_struct *tty = port->info->port.tty;
	unsigned int status, ch, flg;

	status = clps_readl(SYSFLG(port));
	while (!(status & SYSFLG_URXFE)) {
		ch = clps_readl(UARTDR(port));

		port->icount.rx++;

		flg = TTY_NORMAL;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		if (unlikely(ch & UART_ANY_ERR)) {
			if (ch & UARTDR_PARERR)
				port->icount.parity++;
			else if (ch & UARTDR_FRMERR)
				port->icount.frame++;
			if (ch & UARTDR_OVERR)
				port->icount.overrun++;

			ch &= port->read_status_mask;

			if (ch & UARTDR_PARERR)
				flg = TTY_PARITY;
			else if (ch & UARTDR_FRMERR)
				flg = TTY_FRAME;

#ifdef SUPPORT_SYSRQ
			port->sysrq = 0;
#endif
		}

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		/*
		 * CHECK: does overrun affect the current character?
		 * ASSUMPTION: it does not.
		 */
		uart_insert_char(port, ch, UARTDR_OVERR, ch, flg);

	ignore_char:
		status = clps_readl(SYSFLG(port));
	}
	tty_flip_buffer_push(tty);
	return IRQ_HANDLED;
}

static irqreturn_t clps711xuart_int_tx(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct circ_buf *xmit = &port->info->xmit;
	int count;

	if (port->x_char) {
		clps_writel(port->x_char, UARTDR(port));
		port->icount.tx++;
		port->x_char = 0;
		return IRQ_HANDLED;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		clps711xuart_stop_tx(port);
		return IRQ_HANDLED;
	}

	count = port->fifosize >> 1;
	do {
		clps_writel(xmit->buf[xmit->tail], UARTDR(port));
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		clps711xuart_stop_tx(port);

	return IRQ_HANDLED;
}

static unsigned int clps711xuart_tx_empty(struct uart_port *port)
{
	unsigned int status = clps_readl(SYSFLG(port));
	return status & SYSFLG_UBUSY ? 0 : TIOCSER_TEMT;
}

static unsigned int clps711xuart_get_mctrl(struct uart_port *port)
{
	unsigned int port_addr;
	unsigned int result = 0;
	unsigned int status;

	port_addr = SYSFLG(port);
	if (port_addr == SYSFLG1) {
		status = clps_readl(SYSFLG1);
		if (status & SYSFLG1_DCD)
			result |= TIOCM_CAR;
		if (status & SYSFLG1_DSR)
			result |= TIOCM_DSR;
		if (status & SYSFLG1_CTS)
			result |= TIOCM_CTS;
	}

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
	if (break_state == -1)
		ubrlcr |= UBRLCR_BREAK;
	else
		ubrlcr &= ~UBRLCR_BREAK;
	clps_writel(ubrlcr, UBRLCR(port));
	spin_unlock_irqrestore(&port->lock, flags);
}

static int clps711xuart_startup(struct uart_port *port)
{
	unsigned int syscon;
	int retval;

	tx_enabled(port) = 1;

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

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
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
	if (port->fifosize > 1)
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

static struct uart_ops clps711x_pops = {
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

static struct uart_port clps711x_ports[UART_NR] = {
	{
		.iobase		= SYSCON1,
		.irq		= IRQ_UTXINT1, /* IRQ_URXINT1, IRQ_UMSINT */
		.uartclk	= 3686400,
		.fifosize	= 16,
		.ops		= &clps711x_pops,
		.line		= 0,
		.flags		= UPF_BOOT_AUTOCONF,
	},
	{
		.iobase		= SYSCON2,
		.irq		= IRQ_UTXINT2, /* IRQ_URXINT2 */
		.uartclk	= 3686400,
		.fifosize	= 16,
		.ops		= &clps711x_pops,
		.line		= 1,
		.flags		= UPF_BOOT_AUTOCONF,
	}
};

#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE
static void clps711xuart_console_putchar(struct uart_port *port, int ch)
{
	while (clps_readl(SYSFLG(port)) & SYSFLG_UTXFF)
		barrier();
	clps_writel(ch, UARTDR(port));
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 *
 *	Note that this is called with interrupts already disabled
 */
static void
clps711xuart_console_write(struct console *co, const char *s,
			   unsigned int count)
{
	struct uart_port *port = clps711x_ports + co->index;
	unsigned int status, syscon;

	/*
	 *	Ensure that the port is enabled.
	 */
	syscon = clps_readl(SYSCON(port));
	clps_writel(syscon | SYSCON_UARTEN, SYSCON(port));

	uart_console_write(port, s, count, clps711xuart_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the uart state.
	 */
	do {
		status = clps_readl(SYSFLG(port));
	} while (status & SYSFLG_UBUSY);

	clps_writel(syscon, SYSCON(port));
}

static void __init
clps711xuart_console_get_options(struct uart_port *port, int *baud,
				 int *parity, int *bits)
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

static int __init clps711xuart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	port = uart_get_console(clps711x_ports, UART_NR, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		clps711xuart_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver clps711x_reg;
static struct console clps711x_console = {
	.name		= "ttyCL",
	.write		= clps711xuart_console_write,
	.device		= uart_console_device,
	.setup		= clps711xuart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &clps711x_reg,
};

static int __init clps711xuart_console_init(void)
{
	register_console(&clps711x_console);
	return 0;
}
console_initcall(clps711xuart_console_init);

#define CLPS711X_CONSOLE	&clps711x_console
#else
#define CLPS711X_CONSOLE	NULL
#endif

static struct uart_driver clps711x_reg = {
	.driver_name		= "ttyCL",
	.dev_name		= "ttyCL",
	.major			= SERIAL_CLPS711X_MAJOR,
	.minor			= SERIAL_CLPS711X_MINOR,
	.nr			= UART_NR,

	.cons			= CLPS711X_CONSOLE,
};

static int __init clps711xuart_init(void)
{
	int ret, i;

	printk(KERN_INFO "Serial: CLPS711x driver\n");

	ret = uart_register_driver(&clps711x_reg);
	if (ret)
		return ret;

	for (i = 0; i < UART_NR; i++)
		uart_add_one_port(&clps711x_reg, &clps711x_ports[i]);

	return 0;
}

static void __exit clps711xuart_exit(void)
{
	int i;

	for (i = 0; i < UART_NR; i++)
		uart_remove_one_port(&clps711x_reg, &clps711x_ports[i]);

	uart_unregister_driver(&clps711x_reg);
}

module_init(clps711xuart_init);
module_exit(clps711xuart_exit);

MODULE_AUTHOR("Deep Blue Solutions Ltd");
MODULE_DESCRIPTION("CLPS-711x generic serial driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV(SERIAL_CLPS711X_MAJOR, SERIAL_CLPS711X_MINOR);
