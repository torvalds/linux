// SPDX-License-Identifier: GPL-2.0
/*
 * timbuart.c timberdale FPGA UART driver
 * Copyright (c) 2009 Intel Corporation
 */

/* Supports:
 * Timberdale FPGA UART
 */

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "timbuart.h"

struct timbuart_port {
	struct uart_port	port;
	struct tasklet_struct	tasklet;
	int			usedma;
	u32			last_ier;
	struct platform_device  *dev;
};

static int baudrates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800,
	921600, 1843200, 3250000};

static void timbuart_mctrl_check(struct uart_port *port, u32 isr, u32 *ier);

static irqreturn_t timbuart_handleinterrupt(int irq, void *devid);

static void timbuart_stop_rx(struct uart_port *port)
{
	/* spin lock held by upper layer, disable all RX interrupts */
	u32 ier = ioread32(port->membase + TIMBUART_IER) & ~RXFLAGS;
	iowrite32(ier, port->membase + TIMBUART_IER);
}

static void timbuart_stop_tx(struct uart_port *port)
{
	/* spinlock held by upper layer, disable TX interrupt */
	u32 ier = ioread32(port->membase + TIMBUART_IER) & ~TXBAE;
	iowrite32(ier, port->membase + TIMBUART_IER);
}

static void timbuart_start_tx(struct uart_port *port)
{
	struct timbuart_port *uart =
		container_of(port, struct timbuart_port, port);

	/* do not transfer anything here -> fire off the tasklet */
	tasklet_schedule(&uart->tasklet);
}

static unsigned int timbuart_tx_empty(struct uart_port *port)
{
	u32 isr = ioread32(port->membase + TIMBUART_ISR);

	return (isr & TXBE) ? TIOCSER_TEMT : 0;
}

static void timbuart_flush_buffer(struct uart_port *port)
{
	if (!timbuart_tx_empty(port)) {
		u8 ctl = ioread8(port->membase + TIMBUART_CTRL) |
			TIMBUART_CTRL_FLSHTX;

		iowrite8(ctl, port->membase + TIMBUART_CTRL);
		iowrite32(TXBF, port->membase + TIMBUART_ISR);
	}
}

static void timbuart_rx_chars(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;

	while (ioread32(port->membase + TIMBUART_ISR) & RXDP) {
		u8 ch = ioread8(port->membase + TIMBUART_RXFIFO);
		port->icount.rx++;
		tty_insert_flip_char(tport, ch, TTY_NORMAL);
	}

	tty_flip_buffer_push(tport);

	dev_dbg(port->dev, "%s - total read %d bytes\n",
		__func__, port->icount.rx);
}

static void timbuart_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	while (!(ioread32(port->membase + TIMBUART_ISR) & TXBF) &&
		!uart_circ_empty(xmit)) {
		iowrite8(xmit->buf[xmit->tail],
			port->membase + TIMBUART_TXFIFO);
		uart_xmit_advance(port, 1);
	}

	dev_dbg(port->dev,
		"%s - total written %d bytes, CTL: %x, RTS: %x, baud: %x\n",
		 __func__,
		port->icount.tx,
		ioread8(port->membase + TIMBUART_CTRL),
		port->mctrl & TIOCM_RTS,
		ioread8(port->membase + TIMBUART_BAUDRATE));
}

static void timbuart_handle_tx_port(struct uart_port *port, u32 isr, u32 *ier)
{
	struct timbuart_port *uart =
		container_of(port, struct timbuart_port, port);
	struct circ_buf *xmit = &port->state->xmit;

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return;

	if (port->x_char)
		return;

	if (isr & TXFLAGS) {
		timbuart_tx_chars(port);
		/* clear all TX interrupts */
		iowrite32(TXFLAGS, port->membase + TIMBUART_ISR);

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_write_wakeup(port);
	} else
		/* Re-enable any tx interrupt */
		*ier |= uart->last_ier & TXFLAGS;

	/* enable interrupts if there are chars in the transmit buffer,
	 * Or if we delivered some bytes and want the almost empty interrupt
	 * we wake up the upper layer later when we got the interrupt
	 * to give it some time to go out...
	 */
	if (!uart_circ_empty(xmit))
		*ier |= TXBAE;

	dev_dbg(port->dev, "%s - leaving\n", __func__);
}

static void timbuart_handle_rx_port(struct uart_port *port, u32 isr, u32 *ier)
{
	if (isr & RXFLAGS) {
		/* Some RX status is set */
		if (isr & RXBF) {
			u8 ctl = ioread8(port->membase + TIMBUART_CTRL) |
				TIMBUART_CTRL_FLSHRX;
			iowrite8(ctl, port->membase + TIMBUART_CTRL);
			port->icount.overrun++;
		} else if (isr & (RXDP))
			timbuart_rx_chars(port);

		/* ack all RX interrupts */
		iowrite32(RXFLAGS, port->membase + TIMBUART_ISR);
	}

	/* always have the RX interrupts enabled */
	*ier |= RXBAF | RXBF | RXTT;

	dev_dbg(port->dev, "%s - leaving\n", __func__);
}

static void timbuart_tasklet(struct tasklet_struct *t)
{
	struct timbuart_port *uart = from_tasklet(uart, t, tasklet);
	u32 isr, ier = 0;

	spin_lock(&uart->port.lock);

	isr = ioread32(uart->port.membase + TIMBUART_ISR);
	dev_dbg(uart->port.dev, "%s ISR: %x\n", __func__, isr);

	if (!uart->usedma)
		timbuart_handle_tx_port(&uart->port, isr, &ier);

	timbuart_mctrl_check(&uart->port, isr, &ier);

	if (!uart->usedma)
		timbuart_handle_rx_port(&uart->port, isr, &ier);

	iowrite32(ier, uart->port.membase + TIMBUART_IER);

	spin_unlock(&uart->port.lock);
	dev_dbg(uart->port.dev, "%s leaving\n", __func__);
}

static unsigned int timbuart_get_mctrl(struct uart_port *port)
{
	u8 cts = ioread8(port->membase + TIMBUART_CTRL);
	dev_dbg(port->dev, "%s - cts %x\n", __func__, cts);

	if (cts & TIMBUART_CTRL_CTS)
		return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
	else
		return TIOCM_DSR | TIOCM_CAR;
}

static void timbuart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	dev_dbg(port->dev, "%s - %x\n", __func__, mctrl);

	if (mctrl & TIOCM_RTS)
		iowrite8(TIMBUART_CTRL_RTS, port->membase + TIMBUART_CTRL);
	else
		iowrite8(0, port->membase + TIMBUART_CTRL);
}

static void timbuart_mctrl_check(struct uart_port *port, u32 isr, u32 *ier)
{
	unsigned int cts;

	if (isr & CTS_DELTA) {
		/* ack */
		iowrite32(CTS_DELTA, port->membase + TIMBUART_ISR);
		cts = timbuart_get_mctrl(port);
		uart_handle_cts_change(port, cts & TIOCM_CTS);
		wake_up_interruptible(&port->state->port.delta_msr_wait);
	}

	*ier |= CTS_DELTA;
}

static void timbuart_break_ctl(struct uart_port *port, int ctl)
{
	/* N/A */
}

static int timbuart_startup(struct uart_port *port)
{
	struct timbuart_port *uart =
		container_of(port, struct timbuart_port, port);

	dev_dbg(port->dev, "%s\n", __func__);

	iowrite8(TIMBUART_CTRL_FLSHRX, port->membase + TIMBUART_CTRL);
	iowrite32(0x1ff, port->membase + TIMBUART_ISR);
	/* Enable all but TX interrupts */
	iowrite32(RXBAF | RXBF | RXTT | CTS_DELTA,
		port->membase + TIMBUART_IER);

	return request_irq(port->irq, timbuart_handleinterrupt, IRQF_SHARED,
		"timb-uart", uart);
}

static void timbuart_shutdown(struct uart_port *port)
{
	struct timbuart_port *uart =
		container_of(port, struct timbuart_port, port);
	dev_dbg(port->dev, "%s\n", __func__);
	free_irq(port->irq, uart);
	iowrite32(0, port->membase + TIMBUART_IER);

	timbuart_flush_buffer(port);
}

static int get_bindex(int baud)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(baudrates); i++)
		if (baud <= baudrates[i])
			return i;

	return -1;
}

static void timbuart_set_termios(struct uart_port *port,
				 struct ktermios *termios,
				 const struct ktermios *old)
{
	unsigned int baud;
	short bindex;
	unsigned long flags;

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk / 16);
	bindex = get_bindex(baud);
	dev_dbg(port->dev, "%s - bindex %d\n", __func__, bindex);

	if (bindex < 0)
		bindex = 0;
	baud = baudrates[bindex];

	/* The serial layer calls into this once with old = NULL when setting
	   up initially */
	if (old)
		tty_termios_copy_hw(termios, old);
	tty_termios_encode_baud_rate(termios, baud, baud);

	spin_lock_irqsave(&port->lock, flags);
	iowrite8((u8)bindex, port->membase + TIMBUART_BAUDRATE);
	uart_update_timeout(port, termios->c_cflag, baud);
	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *timbuart_type(struct uart_port *port)
{
	return port->type == PORT_UNKNOWN ? "timbuart" : NULL;
}

/* We do not request/release mappings of the registers here,
 * currently it's done in the proble function.
 */
static void timbuart_release_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	int size =
		resource_size(platform_get_resource(pdev, IORESOURCE_MEM, 0));

	if (port->flags & UPF_IOREMAP) {
		iounmap(port->membase);
		port->membase = NULL;
	}

	release_mem_region(port->mapbase, size);
}

static int timbuart_request_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	int size =
		resource_size(platform_get_resource(pdev, IORESOURCE_MEM, 0));

	if (!request_mem_region(port->mapbase, size, "timb-uart"))
		return -EBUSY;

	if (port->flags & UPF_IOREMAP) {
		port->membase = ioremap(port->mapbase, size);
		if (port->membase == NULL) {
			release_mem_region(port->mapbase, size);
			return -ENOMEM;
		}
	}

	return 0;
}

static irqreturn_t timbuart_handleinterrupt(int irq, void *devid)
{
	struct timbuart_port *uart = (struct timbuart_port *)devid;

	if (ioread8(uart->port.membase + TIMBUART_IPR)) {
		uart->last_ier = ioread32(uart->port.membase + TIMBUART_IER);

		/* disable interrupts, the tasklet enables them again */
		iowrite32(0, uart->port.membase + TIMBUART_IER);

		/* fire off bottom half */
		tasklet_schedule(&uart->tasklet);

		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
}

/*
 * Configure/autoconfigure the port.
 */
static void timbuart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_TIMBUART;
		timbuart_request_port(port);
	}
}

static int timbuart_verify_port(struct uart_port *port,
	struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	return -EINVAL;
}

static const struct uart_ops timbuart_ops = {
	.tx_empty = timbuart_tx_empty,
	.set_mctrl = timbuart_set_mctrl,
	.get_mctrl = timbuart_get_mctrl,
	.stop_tx = timbuart_stop_tx,
	.start_tx = timbuart_start_tx,
	.flush_buffer = timbuart_flush_buffer,
	.stop_rx = timbuart_stop_rx,
	.break_ctl = timbuart_break_ctl,
	.startup = timbuart_startup,
	.shutdown = timbuart_shutdown,
	.set_termios = timbuart_set_termios,
	.type = timbuart_type,
	.release_port = timbuart_release_port,
	.request_port = timbuart_request_port,
	.config_port = timbuart_config_port,
	.verify_port = timbuart_verify_port
};

static struct uart_driver timbuart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "timberdale_uart",
	.dev_name = "ttyTU",
	.major = TIMBUART_MAJOR,
	.minor = TIMBUART_MINOR,
	.nr = 1
};

static int timbuart_probe(struct platform_device *dev)
{
	int err, irq;
	struct timbuart_port *uart;
	struct resource *iomem;

	dev_dbg(&dev->dev, "%s\n", __func__);

	uart = kzalloc(sizeof(*uart), GFP_KERNEL);
	if (!uart) {
		err = -EINVAL;
		goto err_mem;
	}

	uart->usedma = 0;

	uart->port.uartclk = 3250000 * 16;
	uart->port.fifosize  = TIMBUART_FIFO_SIZE;
	uart->port.regshift  = 2;
	uart->port.iotype  = UPIO_MEM;
	uart->port.ops = &timbuart_ops;
	uart->port.irq = 0;
	uart->port.flags = UPF_BOOT_AUTOCONF | UPF_IOREMAP;
	uart->port.line  = 0;
	uart->port.dev	= &dev->dev;

	iomem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!iomem) {
		err = -ENOMEM;
		goto err_register;
	}
	uart->port.mapbase = iomem->start;
	uart->port.membase = NULL;

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		err = -EINVAL;
		goto err_register;
	}
	uart->port.irq = irq;

	tasklet_setup(&uart->tasklet, timbuart_tasklet);

	err = uart_register_driver(&timbuart_driver);
	if (err)
		goto err_register;

	err = uart_add_one_port(&timbuart_driver, &uart->port);
	if (err)
		goto err_add_port;

	platform_set_drvdata(dev, uart);

	return 0;

err_add_port:
	uart_unregister_driver(&timbuart_driver);
err_register:
	kfree(uart);
err_mem:
	printk(KERN_ERR "timberdale: Failed to register Timberdale UART: %d\n",
		err);

	return err;
}

static int timbuart_remove(struct platform_device *dev)
{
	struct timbuart_port *uart = platform_get_drvdata(dev);

	tasklet_kill(&uart->tasklet);
	uart_remove_one_port(&timbuart_driver, &uart->port);
	uart_unregister_driver(&timbuart_driver);
	kfree(uart);

	return 0;
}

static struct platform_driver timbuart_platform_driver = {
	.driver = {
		.name	= "timb-uart",
	},
	.probe		= timbuart_probe,
	.remove		= timbuart_remove,
};

module_platform_driver(timbuart_platform_driver);

MODULE_DESCRIPTION("Timberdale UART driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:timb-uart");

