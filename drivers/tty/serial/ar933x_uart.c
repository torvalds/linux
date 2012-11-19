/*
 *  Atheros AR933X SoC built-in UART driver
 *
 *  Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <asm/div64.h>

#include <asm/mach-ath79/ar933x_uart.h>
#include <asm/mach-ath79/ar933x_uart_platform.h>

#define DRIVER_NAME "ar933x-uart"

#define AR933X_UART_MAX_SCALE	0xff
#define AR933X_UART_MAX_STEP	0xffff

#define AR933X_UART_MIN_BAUD	300
#define AR933X_UART_MAX_BAUD	3000000

#define AR933X_DUMMY_STATUS_RD	0x01

static struct uart_driver ar933x_uart_driver;

struct ar933x_uart_port {
	struct uart_port	port;
	unsigned int		ier;	/* shadow Interrupt Enable Register */
	unsigned int		min_baud;
	unsigned int		max_baud;
};

static inline unsigned int ar933x_uart_read(struct ar933x_uart_port *up,
					    int offset)
{
	return readl(up->port.membase + offset);
}

static inline void ar933x_uart_write(struct ar933x_uart_port *up,
				     int offset, unsigned int value)
{
	writel(value, up->port.membase + offset);
}

static inline void ar933x_uart_rmw(struct ar933x_uart_port *up,
				  unsigned int offset,
				  unsigned int mask,
				  unsigned int val)
{
	unsigned int t;

	t = ar933x_uart_read(up, offset);
	t &= ~mask;
	t |= val;
	ar933x_uart_write(up, offset, t);
}

static inline void ar933x_uart_rmw_set(struct ar933x_uart_port *up,
				       unsigned int offset,
				       unsigned int val)
{
	ar933x_uart_rmw(up, offset, 0, val);
}

static inline void ar933x_uart_rmw_clear(struct ar933x_uart_port *up,
					 unsigned int offset,
					 unsigned int val)
{
	ar933x_uart_rmw(up, offset, val, 0);
}

static inline void ar933x_uart_start_tx_interrupt(struct ar933x_uart_port *up)
{
	up->ier |= AR933X_UART_INT_TX_EMPTY;
	ar933x_uart_write(up, AR933X_UART_INT_EN_REG, up->ier);
}

static inline void ar933x_uart_stop_tx_interrupt(struct ar933x_uart_port *up)
{
	up->ier &= ~AR933X_UART_INT_TX_EMPTY;
	ar933x_uart_write(up, AR933X_UART_INT_EN_REG, up->ier);
}

static inline void ar933x_uart_putc(struct ar933x_uart_port *up, int ch)
{
	unsigned int rdata;

	rdata = ch & AR933X_UART_DATA_TX_RX_MASK;
	rdata |= AR933X_UART_DATA_TX_CSR;
	ar933x_uart_write(up, AR933X_UART_DATA_REG, rdata);
}

static unsigned int ar933x_uart_tx_empty(struct uart_port *port)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;
	unsigned long flags;
	unsigned int rdata;

	spin_lock_irqsave(&up->port.lock, flags);
	rdata = ar933x_uart_read(up, AR933X_UART_DATA_REG);
	spin_unlock_irqrestore(&up->port.lock, flags);

	return (rdata & AR933X_UART_DATA_TX_CSR) ? 0 : TIOCSER_TEMT;
}

static unsigned int ar933x_uart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR;
}

static void ar933x_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void ar933x_uart_start_tx(struct uart_port *port)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;

	ar933x_uart_start_tx_interrupt(up);
}

static void ar933x_uart_stop_tx(struct uart_port *port)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;

	ar933x_uart_stop_tx_interrupt(up);
}

static void ar933x_uart_stop_rx(struct uart_port *port)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;

	up->ier &= ~AR933X_UART_INT_RX_VALID;
	ar933x_uart_write(up, AR933X_UART_INT_EN_REG, up->ier);
}

static void ar933x_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		ar933x_uart_rmw_set(up, AR933X_UART_CS_REG,
				    AR933X_UART_CS_TX_BREAK);
	else
		ar933x_uart_rmw_clear(up, AR933X_UART_CS_REG,
				      AR933X_UART_CS_TX_BREAK);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void ar933x_uart_enable_ms(struct uart_port *port)
{
}

/*
 * baudrate = (clk / (scale + 1)) * (step * (1 / 2^17))
 */
static unsigned long ar933x_uart_get_baud(unsigned int clk,
					  unsigned int scale,
					  unsigned int step)
{
	u64 t;
	u32 div;

	div = (2 << 16) * (scale + 1);
	t = clk;
	t *= step;
	t += (div / 2);
	do_div(t, div);

	return t;
}

static void ar933x_uart_get_scale_step(unsigned int clk,
				       unsigned int baud,
				       unsigned int *scale,
				       unsigned int *step)
{
	unsigned int tscale;
	long min_diff;

	*scale = 0;
	*step = 0;

	min_diff = baud;
	for (tscale = 0; tscale < AR933X_UART_MAX_SCALE; tscale++) {
		u64 tstep;
		int diff;

		tstep = baud * (tscale + 1);
		tstep *= (2 << 16);
		do_div(tstep, clk);

		if (tstep > AR933X_UART_MAX_STEP)
			break;

		diff = abs(ar933x_uart_get_baud(clk, tscale, tstep) - baud);
		if (diff < min_diff) {
			min_diff = diff;
			*scale = tscale;
			*step = tstep;
		}
	}
}

static void ar933x_uart_set_termios(struct uart_port *port,
				    struct ktermios *new,
				    struct ktermios *old)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;
	unsigned int cs;
	unsigned long flags;
	unsigned int baud, scale, step;

	/* Only CS8 is supported */
	new->c_cflag &= ~CSIZE;
	new->c_cflag |= CS8;

	/* Only one stop bit is supported */
	new->c_cflag &= ~CSTOPB;

	cs = 0;
	if (new->c_cflag & PARENB) {
		if (!(new->c_cflag & PARODD))
			cs |= AR933X_UART_CS_PARITY_EVEN;
		else
			cs |= AR933X_UART_CS_PARITY_ODD;
	} else {
		cs |= AR933X_UART_CS_PARITY_NONE;
	}

	/* Mark/space parity is not supported */
	new->c_cflag &= ~CMSPAR;

	baud = uart_get_baud_rate(port, new, old, up->min_baud, up->max_baud);
	ar933x_uart_get_scale_step(port->uartclk, baud, &scale, &step);

	/*
	 * Ok, we're now changing the port state. Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	/* disable the UART */
	ar933x_uart_rmw_clear(up, AR933X_UART_CS_REG,
		      AR933X_UART_CS_IF_MODE_M << AR933X_UART_CS_IF_MODE_S);

	/* Update the per-port timeout. */
	uart_update_timeout(port, new->c_cflag, baud);

	up->port.ignore_status_mask = 0;

	/* ignore all characters if CREAD is not set */
	if ((new->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= AR933X_DUMMY_STATUS_RD;

	ar933x_uart_write(up, AR933X_UART_CLOCK_REG,
			  scale << AR933X_UART_CLOCK_SCALE_S | step);

	/* setup configuration register */
	ar933x_uart_rmw(up, AR933X_UART_CS_REG, AR933X_UART_CS_PARITY_M, cs);

	/* enable host interrupt */
	ar933x_uart_rmw_set(up, AR933X_UART_CS_REG,
			    AR933X_UART_CS_HOST_INT_EN);

	/* reenable the UART */
	ar933x_uart_rmw(up, AR933X_UART_CS_REG,
			AR933X_UART_CS_IF_MODE_M << AR933X_UART_CS_IF_MODE_S,
			AR933X_UART_CS_IF_MODE_DCE << AR933X_UART_CS_IF_MODE_S);

	spin_unlock_irqrestore(&up->port.lock, flags);

	if (tty_termios_baud_rate(new))
		tty_termios_encode_baud_rate(new, baud, baud);
}

static void ar933x_uart_rx_chars(struct ar933x_uart_port *up)
{
	struct tty_struct *tty;
	int max_count = 256;

	tty = tty_port_tty_get(&up->port.state->port);
	do {
		unsigned int rdata;
		unsigned char ch;

		rdata = ar933x_uart_read(up, AR933X_UART_DATA_REG);
		if ((rdata & AR933X_UART_DATA_RX_CSR) == 0)
			break;

		/* remove the character from the FIFO */
		ar933x_uart_write(up, AR933X_UART_DATA_REG,
				  AR933X_UART_DATA_RX_CSR);

		if (!tty) {
			/* discard the data if no tty available */
			continue;
		}

		up->port.icount.rx++;
		ch = rdata & AR933X_UART_DATA_TX_RX_MASK;

		if (uart_handle_sysrq_char(&up->port, ch))
			continue;

		if ((up->port.ignore_status_mask & AR933X_DUMMY_STATUS_RD) == 0)
			tty_insert_flip_char(tty, ch, TTY_NORMAL);
	} while (max_count-- > 0);

	if (tty) {
		tty_flip_buffer_push(tty);
		tty_kref_put(tty);
	}
}

static void ar933x_uart_tx_chars(struct ar933x_uart_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count;

	if (uart_tx_stopped(&up->port))
		return;

	count = up->port.fifosize;
	do {
		unsigned int rdata;

		rdata = ar933x_uart_read(up, AR933X_UART_DATA_REG);
		if ((rdata & AR933X_UART_DATA_TX_CSR) == 0)
			break;

		if (up->port.x_char) {
			ar933x_uart_putc(up, up->port.x_char);
			up->port.icount.tx++;
			up->port.x_char = 0;
			continue;
		}

		if (uart_circ_empty(xmit))
			break;

		ar933x_uart_putc(up, xmit->buf[xmit->tail]);

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (!uart_circ_empty(xmit))
		ar933x_uart_start_tx_interrupt(up);
}

static irqreturn_t ar933x_uart_interrupt(int irq, void *dev_id)
{
	struct ar933x_uart_port *up = dev_id;
	unsigned int status;

	status = ar933x_uart_read(up, AR933X_UART_CS_REG);
	if ((status & AR933X_UART_CS_HOST_INT) == 0)
		return IRQ_NONE;

	spin_lock(&up->port.lock);

	status = ar933x_uart_read(up, AR933X_UART_INT_REG);
	status &= ar933x_uart_read(up, AR933X_UART_INT_EN_REG);

	if (status & AR933X_UART_INT_RX_VALID) {
		ar933x_uart_write(up, AR933X_UART_INT_REG,
				  AR933X_UART_INT_RX_VALID);
		ar933x_uart_rx_chars(up);
	}

	if (status & AR933X_UART_INT_TX_EMPTY) {
		ar933x_uart_write(up, AR933X_UART_INT_REG,
				  AR933X_UART_INT_TX_EMPTY);
		ar933x_uart_stop_tx_interrupt(up);
		ar933x_uart_tx_chars(up);
	}

	spin_unlock(&up->port.lock);

	return IRQ_HANDLED;
}

static int ar933x_uart_startup(struct uart_port *port)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;
	unsigned long flags;
	int ret;

	ret = request_irq(up->port.irq, ar933x_uart_interrupt,
			  up->port.irqflags, dev_name(up->port.dev), up);
	if (ret)
		return ret;

	spin_lock_irqsave(&up->port.lock, flags);

	/* Enable HOST interrupts */
	ar933x_uart_rmw_set(up, AR933X_UART_CS_REG,
			    AR933X_UART_CS_HOST_INT_EN);

	/* Enable RX interrupts */
	up->ier = AR933X_UART_INT_RX_VALID;
	ar933x_uart_write(up, AR933X_UART_INT_EN_REG, up->ier);

	spin_unlock_irqrestore(&up->port.lock, flags);

	return 0;
}

static void ar933x_uart_shutdown(struct uart_port *port)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;

	/* Disable all interrupts */
	up->ier = 0;
	ar933x_uart_write(up, AR933X_UART_INT_EN_REG, up->ier);

	/* Disable break condition */
	ar933x_uart_rmw_clear(up, AR933X_UART_CS_REG,
			      AR933X_UART_CS_TX_BREAK);

	free_irq(up->port.irq, up);
}

static const char *ar933x_uart_type(struct uart_port *port)
{
	return (port->type == PORT_AR933X) ? "AR933X UART" : NULL;
}

static void ar933x_uart_release_port(struct uart_port *port)
{
	/* Nothing to release ... */
}

static int ar933x_uart_request_port(struct uart_port *port)
{
	/* UARTs always present */
	return 0;
}

static void ar933x_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_AR933X;
}

static int ar933x_uart_verify_port(struct uart_port *port,
				   struct serial_struct *ser)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;

	if (ser->type != PORT_UNKNOWN &&
	    ser->type != PORT_AR933X)
		return -EINVAL;

	if (ser->irq < 0 || ser->irq >= NR_IRQS)
		return -EINVAL;

	if (ser->baud_base < up->min_baud ||
	    ser->baud_base > up->max_baud)
		return -EINVAL;

	return 0;
}

static struct uart_ops ar933x_uart_ops = {
	.tx_empty	= ar933x_uart_tx_empty,
	.set_mctrl	= ar933x_uart_set_mctrl,
	.get_mctrl	= ar933x_uart_get_mctrl,
	.stop_tx	= ar933x_uart_stop_tx,
	.start_tx	= ar933x_uart_start_tx,
	.stop_rx	= ar933x_uart_stop_rx,
	.enable_ms	= ar933x_uart_enable_ms,
	.break_ctl	= ar933x_uart_break_ctl,
	.startup	= ar933x_uart_startup,
	.shutdown	= ar933x_uart_shutdown,
	.set_termios	= ar933x_uart_set_termios,
	.type		= ar933x_uart_type,
	.release_port	= ar933x_uart_release_port,
	.request_port	= ar933x_uart_request_port,
	.config_port	= ar933x_uart_config_port,
	.verify_port	= ar933x_uart_verify_port,
};

#ifdef CONFIG_SERIAL_AR933X_CONSOLE

static struct ar933x_uart_port *
ar933x_console_ports[CONFIG_SERIAL_AR933X_NR_UARTS];

static void ar933x_uart_wait_xmitr(struct ar933x_uart_port *up)
{
	unsigned int status;
	unsigned int timeout = 60000;

	/* Wait up to 60ms for the character(s) to be sent. */
	do {
		status = ar933x_uart_read(up, AR933X_UART_DATA_REG);
		if (--timeout == 0)
			break;
		udelay(1);
	} while ((status & AR933X_UART_DATA_TX_CSR) == 0);
}

static void ar933x_uart_console_putchar(struct uart_port *port, int ch)
{
	struct ar933x_uart_port *up = (struct ar933x_uart_port *) port;

	ar933x_uart_wait_xmitr(up);
	ar933x_uart_putc(up, ch);
}

static void ar933x_uart_console_write(struct console *co, const char *s,
				      unsigned int count)
{
	struct ar933x_uart_port *up = ar933x_console_ports[co->index];
	unsigned long flags;
	unsigned int int_en;
	int locked = 1;

	local_irq_save(flags);

	if (up->port.sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&up->port.lock);
	else
		spin_lock(&up->port.lock);

	/*
	 * First save the IER then disable the interrupts
	 */
	int_en = ar933x_uart_read(up, AR933X_UART_INT_EN_REG);
	ar933x_uart_write(up, AR933X_UART_INT_EN_REG, 0);

	uart_console_write(&up->port, s, count, ar933x_uart_console_putchar);

	/*
	 * Finally, wait for transmitter to become empty
	 * and restore the IER
	 */
	ar933x_uart_wait_xmitr(up);
	ar933x_uart_write(up, AR933X_UART_INT_EN_REG, int_en);

	ar933x_uart_write(up, AR933X_UART_INT_REG, AR933X_UART_INT_ALLINTS);

	if (locked)
		spin_unlock(&up->port.lock);

	local_irq_restore(flags);
}

static int ar933x_uart_console_setup(struct console *co, char *options)
{
	struct ar933x_uart_port *up;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= CONFIG_SERIAL_AR933X_NR_UARTS)
		return -EINVAL;

	up = ar933x_console_ports[co->index];
	if (!up)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&up->port, co, baud, parity, bits, flow);
}

static struct console ar933x_uart_console = {
	.name		= "ttyATH",
	.write		= ar933x_uart_console_write,
	.device		= uart_console_device,
	.setup		= ar933x_uart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &ar933x_uart_driver,
};

static void ar933x_uart_add_console_port(struct ar933x_uart_port *up)
{
	ar933x_console_ports[up->port.line] = up;
}

#define AR933X_SERIAL_CONSOLE	(&ar933x_uart_console)

#else

static inline void ar933x_uart_add_console_port(struct ar933x_uart_port *up) {}

#define AR933X_SERIAL_CONSOLE	NULL

#endif /* CONFIG_SERIAL_AR933X_CONSOLE */

static struct uart_driver ar933x_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= "ttyATH",
	.nr		= CONFIG_SERIAL_AR933X_NR_UARTS,
	.cons		= AR933X_SERIAL_CONSOLE,
};

static int ar933x_uart_probe(struct platform_device *pdev)
{
	struct ar933x_uart_platform_data *pdata;
	struct ar933x_uart_port *up;
	struct uart_port *port;
	struct resource *mem_res;
	struct resource *irq_res;
	unsigned int baud;
	int id;
	int ret;

	pdata = pdev->dev.platform_data;
	if (!pdata)
		return -EINVAL;

	id = pdev->id;
	if (id == -1)
		id = 0;

	if (id > CONFIG_SERIAL_AR933X_NR_UARTS)
		return -EINVAL;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "no MEM resource\n");
		return -EINVAL;
	}

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_res) {
		dev_err(&pdev->dev, "no IRQ resource\n");
		return -EINVAL;
	}

	up = kzalloc(sizeof(struct ar933x_uart_port), GFP_KERNEL);
	if (!up)
		return -ENOMEM;

	port = &up->port;
	port->mapbase = mem_res->start;

	port->membase = ioremap(mem_res->start, AR933X_UART_REGS_SIZE);
	if (!port->membase) {
		ret = -ENOMEM;
		goto err_free_up;
	}

	port->line = id;
	port->irq = irq_res->start;
	port->dev = &pdev->dev;
	port->type = PORT_AR933X;
	port->iotype = UPIO_MEM32;
	port->uartclk = pdata->uartclk;

	port->regshift = 2;
	port->fifosize = AR933X_UART_FIFO_SIZE;
	port->ops = &ar933x_uart_ops;

	baud = ar933x_uart_get_baud(port->uartclk, AR933X_UART_MAX_SCALE, 1);
	up->min_baud = max_t(unsigned int, baud, AR933X_UART_MIN_BAUD);

	baud = ar933x_uart_get_baud(port->uartclk, 0, AR933X_UART_MAX_STEP);
	up->max_baud = min_t(unsigned int, baud, AR933X_UART_MAX_BAUD);

	ar933x_uart_add_console_port(up);

	ret = uart_add_one_port(&ar933x_uart_driver, &up->port);
	if (ret)
		goto err_unmap;

	platform_set_drvdata(pdev, up);
	return 0;

err_unmap:
	iounmap(up->port.membase);
err_free_up:
	kfree(up);
	return ret;
}

static int __devexit ar933x_uart_remove(struct platform_device *pdev)
{
	struct ar933x_uart_port *up;

	up = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, NULL);

	if (up) {
		uart_remove_one_port(&ar933x_uart_driver, &up->port);
		iounmap(up->port.membase);
		kfree(up);
	}

	return 0;
}

static struct platform_driver ar933x_uart_platform_driver = {
	.probe		= ar933x_uart_probe,
	.remove		= ar933x_uart_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
	},
};

static int __init ar933x_uart_init(void)
{
	int ret;

	ar933x_uart_driver.nr = CONFIG_SERIAL_AR933X_NR_UARTS;
	ret = uart_register_driver(&ar933x_uart_driver);
	if (ret)
		goto err_out;

	ret = platform_driver_register(&ar933x_uart_platform_driver);
	if (ret)
		goto err_unregister_uart_driver;

	return 0;

err_unregister_uart_driver:
	uart_unregister_driver(&ar933x_uart_driver);
err_out:
	return ret;
}

static void __exit ar933x_uart_exit(void)
{
	platform_driver_unregister(&ar933x_uart_platform_driver);
	uart_unregister_driver(&ar933x_uart_driver);
}

module_init(ar933x_uart_init);
module_exit(ar933x_uart_exit);

MODULE_DESCRIPTION("Atheros AR933X UART driver");
MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
