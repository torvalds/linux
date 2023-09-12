// SPDX-License-Identifier: GPL-2.0
/*
 *  Atheros AR933X SoC built-in UART driver
 *
 *  Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>

#include <asm/div64.h>

#include <asm/mach-ath79/ar933x_uart.h>

#include "serial_mctrl_gpio.h"

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
	struct clk		*clk;
	struct mctrl_gpios	*gpios;
	struct gpio_desc	*rts_gpiod;
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

static inline void ar933x_uart_start_rx_interrupt(struct ar933x_uart_port *up)
{
	up->ier |= AR933X_UART_INT_RX_VALID;
	ar933x_uart_write(up, AR933X_UART_INT_EN_REG, up->ier);
}

static inline void ar933x_uart_stop_rx_interrupt(struct ar933x_uart_port *up)
{
	up->ier &= ~AR933X_UART_INT_RX_VALID;
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
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);
	unsigned long flags;
	unsigned int rdata;

	spin_lock_irqsave(&up->port.lock, flags);
	rdata = ar933x_uart_read(up, AR933X_UART_DATA_REG);
	spin_unlock_irqrestore(&up->port.lock, flags);

	return (rdata & AR933X_UART_DATA_TX_CSR) ? 0 : TIOCSER_TEMT;
}

static unsigned int ar933x_uart_get_mctrl(struct uart_port *port)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);
	int ret = TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;

	mctrl_gpio_get(up->gpios, &ret);

	return ret;
}

static void ar933x_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);

	mctrl_gpio_set(up->gpios, mctrl);
}

static void ar933x_uart_start_tx(struct uart_port *port)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);

	ar933x_uart_start_tx_interrupt(up);
}

static void ar933x_uart_wait_tx_complete(struct ar933x_uart_port *up)
{
	unsigned int status;
	unsigned int timeout = 60000;

	/* Wait up to 60ms for the character(s) to be sent. */
	do {
		status = ar933x_uart_read(up, AR933X_UART_CS_REG);
		if (--timeout == 0)
			break;
		udelay(1);
	} while (status & AR933X_UART_CS_TX_BUSY);

	if (timeout == 0)
		dev_err(up->port.dev, "waiting for TX timed out\n");
}

static void ar933x_uart_rx_flush(struct ar933x_uart_port *up)
{
	unsigned int status;

	/* clear RX_VALID interrupt */
	ar933x_uart_write(up, AR933X_UART_INT_REG, AR933X_UART_INT_RX_VALID);

	/* remove characters from the RX FIFO */
	do {
		ar933x_uart_write(up, AR933X_UART_DATA_REG, AR933X_UART_DATA_RX_CSR);
		status = ar933x_uart_read(up, AR933X_UART_DATA_REG);
	} while (status & AR933X_UART_DATA_RX_CSR);
}

static void ar933x_uart_stop_tx(struct uart_port *port)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);

	ar933x_uart_stop_tx_interrupt(up);
}

static void ar933x_uart_stop_rx(struct uart_port *port)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);

	ar933x_uart_stop_rx_interrupt(up);
}

static void ar933x_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);
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
				    const struct ktermios *old)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);
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

	/* enable RX and TX ready overide */
	ar933x_uart_rmw_set(up, AR933X_UART_CS_REG,
		AR933X_UART_CS_TX_READY_ORIDE | AR933X_UART_CS_RX_READY_ORIDE);

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
	struct tty_port *port = &up->port.state->port;
	int max_count = 256;

	do {
		unsigned int rdata;
		unsigned char ch;

		rdata = ar933x_uart_read(up, AR933X_UART_DATA_REG);
		if ((rdata & AR933X_UART_DATA_RX_CSR) == 0)
			break;

		/* remove the character from the FIFO */
		ar933x_uart_write(up, AR933X_UART_DATA_REG,
				  AR933X_UART_DATA_RX_CSR);

		up->port.icount.rx++;
		ch = rdata & AR933X_UART_DATA_TX_RX_MASK;

		if (uart_handle_sysrq_char(&up->port, ch))
			continue;

		if ((up->port.ignore_status_mask & AR933X_DUMMY_STATUS_RD) == 0)
			tty_insert_flip_char(port, ch, TTY_NORMAL);
	} while (max_count-- > 0);

	tty_flip_buffer_push(port);
}

static void ar933x_uart_tx_chars(struct ar933x_uart_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	struct serial_rs485 *rs485conf = &up->port.rs485;
	int count;
	bool half_duplex_send = false;

	if (uart_tx_stopped(&up->port))
		return;

	if ((rs485conf->flags & SER_RS485_ENABLED) &&
	    (up->port.x_char || !uart_circ_empty(xmit))) {
		ar933x_uart_stop_rx_interrupt(up);
		gpiod_set_value(up->rts_gpiod, !!(rs485conf->flags & SER_RS485_RTS_ON_SEND));
		half_duplex_send = true;
	}

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

		uart_xmit_advance(&up->port, 1);
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (!uart_circ_empty(xmit)) {
		ar933x_uart_start_tx_interrupt(up);
	} else if (half_duplex_send) {
		ar933x_uart_wait_tx_complete(up);
		ar933x_uart_rx_flush(up);
		ar933x_uart_start_rx_interrupt(up);
		gpiod_set_value(up->rts_gpiod, !!(rs485conf->flags & SER_RS485_RTS_AFTER_SEND));
	}
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
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);
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

	/* enable RX and TX ready overide */
	ar933x_uart_rmw_set(up, AR933X_UART_CS_REG,
		AR933X_UART_CS_TX_READY_ORIDE | AR933X_UART_CS_RX_READY_ORIDE);

	/* Enable RX interrupts */
	ar933x_uart_start_rx_interrupt(up);

	spin_unlock_irqrestore(&up->port.lock, flags);

	return 0;
}

static void ar933x_uart_shutdown(struct uart_port *port)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);

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
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);

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

static const struct uart_ops ar933x_uart_ops = {
	.tx_empty	= ar933x_uart_tx_empty,
	.set_mctrl	= ar933x_uart_set_mctrl,
	.get_mctrl	= ar933x_uart_get_mctrl,
	.stop_tx	= ar933x_uart_stop_tx,
	.start_tx	= ar933x_uart_start_tx,
	.stop_rx	= ar933x_uart_stop_rx,
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

static int ar933x_config_rs485(struct uart_port *port, struct ktermios *termios,
				struct serial_rs485 *rs485conf)
{
	struct ar933x_uart_port *up =
			container_of(port, struct ar933x_uart_port, port);

	if (port->rs485.flags & SER_RS485_ENABLED)
		gpiod_set_value(up->rts_gpiod,
			!!(rs485conf->flags & SER_RS485_RTS_AFTER_SEND));

	return 0;
}

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

static void ar933x_uart_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct ar933x_uart_port *up =
		container_of(port, struct ar933x_uart_port, port);

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
#endif /* CONFIG_SERIAL_AR933X_CONSOLE */

static struct uart_driver ar933x_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= "ttyATH",
	.nr		= CONFIG_SERIAL_AR933X_NR_UARTS,
	.cons		= NULL, /* filled in runtime */
};

static const struct serial_rs485 ar933x_no_rs485 = {};
static const struct serial_rs485 ar933x_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND,
};

static int ar933x_uart_probe(struct platform_device *pdev)
{
	struct ar933x_uart_port *up;
	struct uart_port *port;
	struct resource *mem_res;
	struct device_node *np;
	unsigned int baud;
	int id;
	int ret;
	int irq;

	np = pdev->dev.of_node;
	if (IS_ENABLED(CONFIG_OF) && np) {
		id = of_alias_get_id(np, "serial");
		if (id < 0) {
			dev_err(&pdev->dev, "unable to get alias id, err=%d\n",
				id);
			return id;
		}
	} else {
		id = pdev->id;
		if (id == -1)
			id = 0;
	}

	if (id >= CONFIG_SERIAL_AR933X_NR_UARTS)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	up = devm_kzalloc(&pdev->dev, sizeof(struct ar933x_uart_port),
			  GFP_KERNEL);
	if (!up)
		return -ENOMEM;

	up->clk = devm_clk_get(&pdev->dev, "uart");
	if (IS_ERR(up->clk)) {
		dev_err(&pdev->dev, "unable to get UART clock\n");
		return PTR_ERR(up->clk);
	}

	port = &up->port;

	port->membase = devm_platform_get_and_ioremap_resource(pdev, 0, &mem_res);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);

	ret = clk_prepare_enable(up->clk);
	if (ret)
		return ret;

	port->uartclk = clk_get_rate(up->clk);
	if (!port->uartclk) {
		ret = -EINVAL;
		goto err_disable_clk;
	}

	port->mapbase = mem_res->start;
	port->line = id;
	port->irq = irq;
	port->dev = &pdev->dev;
	port->type = PORT_AR933X;
	port->iotype = UPIO_MEM32;

	port->regshift = 2;
	port->fifosize = AR933X_UART_FIFO_SIZE;
	port->ops = &ar933x_uart_ops;
	port->rs485_config = ar933x_config_rs485;
	port->rs485_supported = ar933x_rs485_supported;

	baud = ar933x_uart_get_baud(port->uartclk, AR933X_UART_MAX_SCALE, 1);
	up->min_baud = max_t(unsigned int, baud, AR933X_UART_MIN_BAUD);

	baud = ar933x_uart_get_baud(port->uartclk, 0, AR933X_UART_MAX_STEP);
	up->max_baud = min_t(unsigned int, baud, AR933X_UART_MAX_BAUD);

	ret = uart_get_rs485_mode(port);
	if (ret)
		goto err_disable_clk;

	up->gpios = mctrl_gpio_init(port, 0);
	if (IS_ERR(up->gpios) && PTR_ERR(up->gpios) != -ENOSYS) {
		ret = PTR_ERR(up->gpios);
		goto err_disable_clk;
	}

	up->rts_gpiod = mctrl_gpio_to_gpiod(up->gpios, UART_GPIO_RTS);

	if (!up->rts_gpiod) {
		port->rs485_supported = ar933x_no_rs485;
		if (port->rs485.flags & SER_RS485_ENABLED) {
			dev_err(&pdev->dev, "lacking rts-gpio, disabling RS485\n");
			port->rs485.flags &= ~SER_RS485_ENABLED;
		}
	}

#ifdef CONFIG_SERIAL_AR933X_CONSOLE
	ar933x_console_ports[up->port.line] = up;
#endif

	ret = uart_add_one_port(&ar933x_uart_driver, &up->port);
	if (ret)
		goto err_disable_clk;

	platform_set_drvdata(pdev, up);
	return 0;

err_disable_clk:
	clk_disable_unprepare(up->clk);
	return ret;
}

static int ar933x_uart_remove(struct platform_device *pdev)
{
	struct ar933x_uart_port *up;

	up = platform_get_drvdata(pdev);

	if (up) {
		uart_remove_one_port(&ar933x_uart_driver, &up->port);
		clk_disable_unprepare(up->clk);
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ar933x_uart_of_ids[] = {
	{ .compatible = "qca,ar9330-uart" },
	{},
};
MODULE_DEVICE_TABLE(of, ar933x_uart_of_ids);
#endif

static struct platform_driver ar933x_uart_platform_driver = {
	.probe		= ar933x_uart_probe,
	.remove		= ar933x_uart_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table = of_match_ptr(ar933x_uart_of_ids),
	},
};

static int __init ar933x_uart_init(void)
{
	int ret;

#ifdef CONFIG_SERIAL_AR933X_CONSOLE
	ar933x_uart_driver.cons = &ar933x_uart_console;
#endif

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
