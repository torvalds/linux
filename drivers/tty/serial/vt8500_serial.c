// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * Based on msm_serial.c, which is:
 * Copyright (C) 2007 Google, Inc.
 * Author: Robert Love <rlove@google.com>
 */

#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>

/*
 * UART Register offsets
 */

#define VT8500_URTDR		0x0000	/* Transmit data */
#define VT8500_URRDR		0x0004	/* Receive data */
#define VT8500_URDIV		0x0008	/* Clock/Baud rate divisor */
#define VT8500_URLCR		0x000C	/* Line control */
#define VT8500_URICR		0x0010	/* IrDA control */
#define VT8500_URIER		0x0014	/* Interrupt enable */
#define VT8500_URISR		0x0018	/* Interrupt status */
#define VT8500_URUSR		0x001c	/* UART status */
#define VT8500_URFCR		0x0020	/* FIFO control */
#define VT8500_URFIDX		0x0024	/* FIFO index */
#define VT8500_URBKR		0x0028	/* Break signal count */
#define VT8500_URTOD		0x002c	/* Time out divisor */
#define VT8500_TXFIFO		0x1000	/* Transmit FIFO (16x8) */
#define VT8500_RXFIFO		0x1020	/* Receive FIFO (16x10) */

/*
 * Interrupt enable and status bits
 */

#define TXDE	(1 << 0)	/* Tx Data empty */
#define RXDF	(1 << 1)	/* Rx Data full */
#define TXFAE	(1 << 2)	/* Tx FIFO almost empty */
#define TXFE	(1 << 3)	/* Tx FIFO empty */
#define RXFAF	(1 << 4)	/* Rx FIFO almost full */
#define RXFF	(1 << 5)	/* Rx FIFO full */
#define TXUDR	(1 << 6)	/* Tx underrun */
#define RXOVER	(1 << 7)	/* Rx overrun */
#define PER	(1 << 8)	/* Parity error */
#define FER	(1 << 9)	/* Frame error */
#define TCTS	(1 << 10)	/* Toggle of CTS */
#define RXTOUT	(1 << 11)	/* Rx timeout */
#define BKDONE	(1 << 12)	/* Break signal done */
#define ERR	(1 << 13)	/* AHB error response */

#define RX_FIFO_INTS	(RXFAF | RXFF | RXOVER | PER | FER | RXTOUT)
#define TX_FIFO_INTS	(TXFAE | TXFE | TXUDR)

/*
 * Line control bits
 */

#define VT8500_TXEN	(1 << 0)	/* Enable transmit logic */
#define VT8500_RXEN	(1 << 1)	/* Enable receive logic */
#define VT8500_CS8	(1 << 2)	/* 8-bit data length (vs. 7-bit) */
#define VT8500_CSTOPB	(1 << 3)	/* 2 stop bits (vs. 1) */
#define VT8500_PARENB	(1 << 4)	/* Enable parity */
#define VT8500_PARODD	(1 << 5)	/* Odd parity (vs. even) */
#define VT8500_RTS	(1 << 6)	/* Ready to send */
#define VT8500_LOOPBK	(1 << 7)	/* Enable internal loopback */
#define VT8500_DMA	(1 << 8)	/* Enable DMA mode (needs FIFO) */
#define VT8500_BREAK	(1 << 9)	/* Initiate break signal */
#define VT8500_PSLVERR	(1 << 10)	/* APB error upon empty RX FIFO read */
#define VT8500_SWRTSCTS	(1 << 11)	/* Software-controlled RTS/CTS */

/*
 * Capability flags (driver-internal)
 */

#define VT8500_HAS_SWRTSCTS_SWITCH	(1 << 1)

#define VT8500_RECOMMENDED_CLK		12000000
#define VT8500_OVERSAMPLING_DIVISOR	13
#define VT8500_MAX_PORTS	6

struct vt8500_port {
	struct uart_port	uart;
	char			name[16];
	struct clk		*clk;
	unsigned int		clk_predivisor;
	unsigned int		ier;
	unsigned int		vt8500_uart_flags;
};

/*
 * we use this variable to keep track of which ports
 * have been allocated as we can't use pdev->id in
 * devicetree
 */
static DECLARE_BITMAP(vt8500_ports_in_use, VT8500_MAX_PORTS);

static inline void vt8500_write(struct uart_port *port, unsigned int val,
			     unsigned int off)
{
	writel(val, port->membase + off);
}

static inline unsigned int vt8500_read(struct uart_port *port, unsigned int off)
{
	return readl(port->membase + off);
}

static void vt8500_stop_tx(struct uart_port *port)
{
	struct vt8500_port *vt8500_port = container_of(port,
						       struct vt8500_port,
						       uart);

	vt8500_port->ier &= ~TX_FIFO_INTS;
	vt8500_write(port, vt8500_port->ier, VT8500_URIER);
}

static void vt8500_stop_rx(struct uart_port *port)
{
	struct vt8500_port *vt8500_port = container_of(port,
						       struct vt8500_port,
						       uart);

	vt8500_port->ier &= ~RX_FIFO_INTS;
	vt8500_write(port, vt8500_port->ier, VT8500_URIER);
}

static void vt8500_enable_ms(struct uart_port *port)
{
	struct vt8500_port *vt8500_port = container_of(port,
						       struct vt8500_port,
						       uart);

	vt8500_port->ier |= TCTS;
	vt8500_write(port, vt8500_port->ier, VT8500_URIER);
}

static void handle_rx(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;

	/*
	 * Handle overrun
	 */
	if ((vt8500_read(port, VT8500_URISR) & RXOVER)) {
		port->icount.overrun++;
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
	}

	/* and now the main RX loop */
	while (vt8500_read(port, VT8500_URFIDX) & 0x1f00) {
		unsigned int c;
		char flag = TTY_NORMAL;

		c = readw(port->membase + VT8500_RXFIFO) & 0x3ff;

		/* Mask conditions we're ignorning. */
		c &= ~port->read_status_mask;

		if (c & FER) {
			port->icount.frame++;
			flag = TTY_FRAME;
		} else if (c & PER) {
			port->icount.parity++;
			flag = TTY_PARITY;
		}
		port->icount.rx++;

		if (!uart_handle_sysrq_char(port, c))
			tty_insert_flip_char(tport, c, flag);
	}

	tty_flip_buffer_push(tport);
}

static unsigned int vt8500_tx_empty(struct uart_port *port)
{
	unsigned int idx = vt8500_read(port, VT8500_URFIDX) & 0x1f;

	return idx < 16 ? TIOCSER_TEMT : 0;
}

static void handle_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		writeb(port->x_char, port->membase + VT8500_TXFIFO);
		port->icount.tx++;
		port->x_char = 0;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		vt8500_stop_tx(port);
		return;
	}

	while (vt8500_tx_empty(port)) {
		if (uart_circ_empty(xmit))
			break;

		writeb(xmit->buf[xmit->tail], port->membase + VT8500_TXFIFO);

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		vt8500_stop_tx(port);
}

static void vt8500_start_tx(struct uart_port *port)
{
	struct vt8500_port *vt8500_port = container_of(port,
						       struct vt8500_port,
						       uart);

	vt8500_port->ier &= ~TX_FIFO_INTS;
	vt8500_write(port, vt8500_port->ier, VT8500_URIER);
	handle_tx(port);
	vt8500_port->ier |= TX_FIFO_INTS;
	vt8500_write(port, vt8500_port->ier, VT8500_URIER);
}

static void handle_delta_cts(struct uart_port *port)
{
	port->icount.cts++;
	wake_up_interruptible(&port->state->port.delta_msr_wait);
}

static irqreturn_t vt8500_irq(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned long isr;

	spin_lock(&port->lock);
	isr = vt8500_read(port, VT8500_URISR);

	/* Acknowledge active status bits */
	vt8500_write(port, isr, VT8500_URISR);

	if (isr & RX_FIFO_INTS)
		handle_rx(port);
	if (isr & TX_FIFO_INTS)
		handle_tx(port);
	if (isr & TCTS)
		handle_delta_cts(port);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static unsigned int vt8500_get_mctrl(struct uart_port *port)
{
	unsigned int usr;

	usr = vt8500_read(port, VT8500_URUSR);
	if (usr & (1 << 4))
		return TIOCM_CTS;
	else
		return 0;
}

static void vt8500_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned int lcr = vt8500_read(port, VT8500_URLCR);

	if (mctrl & TIOCM_RTS)
		lcr |= VT8500_RTS;
	else
		lcr &= ~VT8500_RTS;

	vt8500_write(port, lcr, VT8500_URLCR);
}

static void vt8500_break_ctl(struct uart_port *port, int break_ctl)
{
	if (break_ctl)
		vt8500_write(port,
			     vt8500_read(port, VT8500_URLCR) | VT8500_BREAK,
			     VT8500_URLCR);
}

static int vt8500_set_baud_rate(struct uart_port *port, unsigned int baud)
{
	struct vt8500_port *vt8500_port =
			container_of(port, struct vt8500_port, uart);
	unsigned long div;
	unsigned int loops = 1000;

	div = ((vt8500_port->clk_predivisor - 1) & 0xf) << 16;
	div |= (uart_get_divisor(port, baud) - 1) & 0x3ff;

	/* Effective baud rate */
	baud = port->uartclk / 16 / ((div & 0x3ff) + 1);

	while ((vt8500_read(port, VT8500_URUSR) & (1 << 5)) && --loops)
		cpu_relax();

	vt8500_write(port, div, VT8500_URDIV);

	/* Break signal timing depends on baud rate, update accordingly */
	vt8500_write(port, mult_frac(baud, 4096, 1000000), VT8500_URBKR);

	return baud;
}

static int vt8500_startup(struct uart_port *port)
{
	struct vt8500_port *vt8500_port =
			container_of(port, struct vt8500_port, uart);
	int ret;

	snprintf(vt8500_port->name, sizeof(vt8500_port->name),
		 "vt8500_serial%d", port->line);

	ret = request_irq(port->irq, vt8500_irq, IRQF_TRIGGER_HIGH,
			  vt8500_port->name, port);
	if (unlikely(ret))
		return ret;

	vt8500_write(port, 0x03, VT8500_URLCR);	/* enable TX & RX */

	return 0;
}

static void vt8500_shutdown(struct uart_port *port)
{
	struct vt8500_port *vt8500_port =
			container_of(port, struct vt8500_port, uart);

	vt8500_port->ier = 0;

	/* disable interrupts and FIFOs */
	vt8500_write(&vt8500_port->uart, 0, VT8500_URIER);
	vt8500_write(&vt8500_port->uart, 0x880, VT8500_URFCR);
	free_irq(port->irq, port);
}

static void vt8500_set_termios(struct uart_port *port,
			       struct ktermios *termios,
			       const struct ktermios *old)
{
	struct vt8500_port *vt8500_port =
			container_of(port, struct vt8500_port, uart);
	unsigned long flags;
	unsigned int baud, lcr;
	unsigned int loops = 1000;

	spin_lock_irqsave(&port->lock, flags);

	/* calculate and set baud rate */
	baud = uart_get_baud_rate(port, termios, old, 900, 921600);
	baud = vt8500_set_baud_rate(port, baud);
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	/* calculate parity */
	lcr = vt8500_read(&vt8500_port->uart, VT8500_URLCR);
	lcr &= ~(VT8500_PARENB | VT8500_PARODD);
	if (termios->c_cflag & PARENB) {
		lcr |= VT8500_PARENB;
		termios->c_cflag &= ~CMSPAR;
		if (termios->c_cflag & PARODD)
			lcr |= VT8500_PARODD;
	}

	/* calculate bits per char */
	lcr &= ~VT8500_CS8;
	switch (termios->c_cflag & CSIZE) {
	case CS7:
		break;
	case CS8:
	default:
		lcr |= VT8500_CS8;
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
		break;
	}

	/* calculate stop bits */
	lcr &= ~VT8500_CSTOPB;
	if (termios->c_cflag & CSTOPB)
		lcr |= VT8500_CSTOPB;

	lcr &= ~VT8500_SWRTSCTS;
	if (vt8500_port->vt8500_uart_flags & VT8500_HAS_SWRTSCTS_SWITCH)
		lcr |= VT8500_SWRTSCTS;

	/* set parity, bits per char, and stop bit */
	vt8500_write(&vt8500_port->uart, lcr, VT8500_URLCR);

	/* Configure status bits to ignore based on termio flags. */
	port->read_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->read_status_mask = FER | PER;

	uart_update_timeout(port, termios->c_cflag, baud);

	/* Reset FIFOs */
	vt8500_write(&vt8500_port->uart, 0x88c, VT8500_URFCR);
	while ((vt8500_read(&vt8500_port->uart, VT8500_URFCR) & 0xc)
							&& --loops)
		cpu_relax();

	/* Every possible FIFO-related interrupt */
	vt8500_port->ier = RX_FIFO_INTS | TX_FIFO_INTS;

	/*
	 * CTS flow control
	 */
	if (UART_ENABLE_MS(&vt8500_port->uart, termios->c_cflag))
		vt8500_port->ier |= TCTS;

	vt8500_write(&vt8500_port->uart, 0x881, VT8500_URFCR);
	vt8500_write(&vt8500_port->uart, vt8500_port->ier, VT8500_URIER);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *vt8500_type(struct uart_port *port)
{
	struct vt8500_port *vt8500_port =
			container_of(port, struct vt8500_port, uart);
	return vt8500_port->name;
}

static void vt8500_release_port(struct uart_port *port)
{
}

static int vt8500_request_port(struct uart_port *port)
{
	return 0;
}

static void vt8500_config_port(struct uart_port *port, int flags)
{
	port->type = PORT_VT8500;
}

static int vt8500_verify_port(struct uart_port *port,
			      struct serial_struct *ser)
{
	if (unlikely(ser->type != PORT_UNKNOWN && ser->type != PORT_VT8500))
		return -EINVAL;
	if (unlikely(port->irq != ser->irq))
		return -EINVAL;
	return 0;
}

static struct vt8500_port *vt8500_uart_ports[VT8500_MAX_PORTS];
static struct uart_driver vt8500_uart_driver;

#ifdef CONFIG_SERIAL_VT8500_CONSOLE

static void wait_for_xmitr(struct uart_port *port)
{
	unsigned int status, tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	do {
		status = vt8500_read(port, VT8500_URFIDX);

		if (--tmout == 0)
			break;
		udelay(1);
	} while (status & 0x10);
}

static void vt8500_console_putchar(struct uart_port *port, unsigned char c)
{
	wait_for_xmitr(port);
	writeb(c, port->membase + VT8500_TXFIFO);
}

static void vt8500_console_write(struct console *co, const char *s,
			      unsigned int count)
{
	struct vt8500_port *vt8500_port = vt8500_uart_ports[co->index];
	unsigned long ier;

	BUG_ON(co->index < 0 || co->index >= vt8500_uart_driver.nr);

	ier = vt8500_read(&vt8500_port->uart, VT8500_URIER);
	vt8500_write(&vt8500_port->uart, VT8500_URIER, 0);

	uart_console_write(&vt8500_port->uart, s, count,
			   vt8500_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and switch back to FIFO
	 */
	wait_for_xmitr(&vt8500_port->uart);
	vt8500_write(&vt8500_port->uart, VT8500_URIER, ier);
}

static int __init vt8500_console_setup(struct console *co, char *options)
{
	struct vt8500_port *vt8500_port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (unlikely(co->index >= vt8500_uart_driver.nr || co->index < 0))
		return -ENXIO;

	vt8500_port = vt8500_uart_ports[co->index];

	if (!vt8500_port)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&vt8500_port->uart,
				 co, baud, parity, bits, flow);
}

static struct console vt8500_console = {
	.name = "ttyWMT",
	.write = vt8500_console_write,
	.device = uart_console_device,
	.setup = vt8500_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &vt8500_uart_driver,
};

#define VT8500_CONSOLE	(&vt8500_console)

#else
#define VT8500_CONSOLE	NULL
#endif

#ifdef CONFIG_CONSOLE_POLL
static int vt8500_get_poll_char(struct uart_port *port)
{
	unsigned int status = vt8500_read(port, VT8500_URFIDX);

	if (!(status & 0x1f00))
		return NO_POLL_CHAR;

	return vt8500_read(port, VT8500_RXFIFO) & 0xff;
}

static void vt8500_put_poll_char(struct uart_port *port, unsigned char c)
{
	unsigned int status, tmout = 10000;

	do {
		status = vt8500_read(port, VT8500_URFIDX);

		if (--tmout == 0)
			break;
		udelay(1);
	} while (status & 0x10);

	vt8500_write(port, c, VT8500_TXFIFO);
}
#endif

static const struct uart_ops vt8500_uart_pops = {
	.tx_empty	= vt8500_tx_empty,
	.set_mctrl	= vt8500_set_mctrl,
	.get_mctrl	= vt8500_get_mctrl,
	.stop_tx	= vt8500_stop_tx,
	.start_tx	= vt8500_start_tx,
	.stop_rx	= vt8500_stop_rx,
	.enable_ms	= vt8500_enable_ms,
	.break_ctl	= vt8500_break_ctl,
	.startup	= vt8500_startup,
	.shutdown	= vt8500_shutdown,
	.set_termios	= vt8500_set_termios,
	.type		= vt8500_type,
	.release_port	= vt8500_release_port,
	.request_port	= vt8500_request_port,
	.config_port	= vt8500_config_port,
	.verify_port	= vt8500_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= vt8500_get_poll_char,
	.poll_put_char	= vt8500_put_poll_char,
#endif
};

static struct uart_driver vt8500_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "vt8500_serial",
	.dev_name	= "ttyWMT",
	.nr		= 6,
	.cons		= VT8500_CONSOLE,
};

static unsigned int vt8500_flags; /* none required so far */
static unsigned int wm8880_flags = VT8500_HAS_SWRTSCTS_SWITCH;

static const struct of_device_id wmt_dt_ids[] = {
	{ .compatible = "via,vt8500-uart", .data = &vt8500_flags},
	{ .compatible = "wm,wm8880-uart", .data = &wm8880_flags},
	{}
};

static int vt8500_serial_probe(struct platform_device *pdev)
{
	struct vt8500_port *vt8500_port;
	struct resource *mmres;
	struct device_node *np = pdev->dev.of_node;
	const unsigned int *flags;
	int ret;
	int port;
	int irq;

	flags = of_device_get_match_data(&pdev->dev);
	if (!flags)
		return -EINVAL;

	mmres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mmres)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (np) {
		port = of_alias_get_id(np, "serial");
		if (port >= VT8500_MAX_PORTS)
			port = -1;
	} else {
		port = -1;
	}

	if (port < 0) {
		/* calculate the port id */
		port = find_first_zero_bit(vt8500_ports_in_use,
					   VT8500_MAX_PORTS);
	}

	if (port >= VT8500_MAX_PORTS)
		return -ENODEV;

	/* reserve the port id */
	if (test_and_set_bit(port, vt8500_ports_in_use)) {
		/* port already in use - shouldn't really happen */
		return -EBUSY;
	}

	vt8500_port = devm_kzalloc(&pdev->dev, sizeof(struct vt8500_port),
				   GFP_KERNEL);
	if (!vt8500_port)
		return -ENOMEM;

	vt8500_port->uart.membase = devm_ioremap_resource(&pdev->dev, mmres);
	if (IS_ERR(vt8500_port->uart.membase))
		return PTR_ERR(vt8500_port->uart.membase);

	vt8500_port->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(vt8500_port->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return  -EINVAL;
	}

	ret = clk_prepare_enable(vt8500_port->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		return ret;
	}

	vt8500_port->vt8500_uart_flags = *flags;
	vt8500_port->clk_predivisor = DIV_ROUND_CLOSEST(
					clk_get_rate(vt8500_port->clk),
					VT8500_RECOMMENDED_CLK
				      );
	vt8500_port->uart.type = PORT_VT8500;
	vt8500_port->uart.iotype = UPIO_MEM;
	vt8500_port->uart.mapbase = mmres->start;
	vt8500_port->uart.irq = irq;
	vt8500_port->uart.fifosize = 16;
	vt8500_port->uart.ops = &vt8500_uart_pops;
	vt8500_port->uart.line = port;
	vt8500_port->uart.dev = &pdev->dev;
	vt8500_port->uart.flags = UPF_IOREMAP | UPF_BOOT_AUTOCONF;
	vt8500_port->uart.has_sysrq = IS_ENABLED(CONFIG_SERIAL_VT8500_CONSOLE);

	/* Serial core uses the magic "16" everywhere - adjust for it */
	vt8500_port->uart.uartclk = 16 * clk_get_rate(vt8500_port->clk) /
					vt8500_port->clk_predivisor /
					VT8500_OVERSAMPLING_DIVISOR;

	snprintf(vt8500_port->name, sizeof(vt8500_port->name),
		 "VT8500 UART%d", pdev->id);

	vt8500_uart_ports[port] = vt8500_port;

	uart_add_one_port(&vt8500_uart_driver, &vt8500_port->uart);

	platform_set_drvdata(pdev, vt8500_port);

	return 0;
}

static struct platform_driver vt8500_platform_driver = {
	.probe  = vt8500_serial_probe,
	.driver = {
		.name = "vt8500_serial",
		.of_match_table = wmt_dt_ids,
		.suppress_bind_attrs = true,
	},
};

static int __init vt8500_serial_init(void)
{
	int ret;

	ret = uart_register_driver(&vt8500_uart_driver);
	if (unlikely(ret))
		return ret;

	ret = platform_driver_register(&vt8500_platform_driver);

	if (unlikely(ret))
		uart_unregister_driver(&vt8500_uart_driver);

	return ret;
}
device_initcall(vt8500_serial_init);
