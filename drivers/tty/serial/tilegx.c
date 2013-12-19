/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * TILEGx UART driver.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <gxio/common.h>
#include <gxio/iorpc_globals.h>
#include <gxio/iorpc_uart.h>
#include <gxio/kiorpc.h>

#include <hv/drv_uart_intf.h>

/*
 * Use device name ttyS, major 4, minor 64-65.
 * This is the usual serial port name, 8250 conventional range.
 */
#define TILEGX_UART_MAJOR	TTY_MAJOR
#define TILEGX_UART_MINOR	64
#define TILEGX_UART_NAME	"ttyS"
#define DRIVER_NAME_STRING	"TILEGx_Serial"
#define TILEGX_UART_REF_CLK	125000000; /* REF_CLK is always 125 MHz. */

struct tile_uart_port {
	/* UART port. */
	struct uart_port	uart;

	/* GXIO device context. */
	gxio_uart_context_t	context;

	/* UART access mutex. */
	struct mutex		mutex;

	/* CPU receiving interrupts. */
	int			irq_cpu;
};

static struct tile_uart_port tile_uart_ports[TILEGX_UART_NR];
static struct uart_driver tilegx_uart_driver;


/*
 * Read UART rx fifo, and insert the chars into tty buffer.
 */
static void receive_chars(struct tile_uart_port *tile_uart,
			  struct tty_struct *tty)
{
	int i;
	char c;
	UART_FIFO_COUNT_t count;
	gxio_uart_context_t *context = &tile_uart->context;
	struct tty_port *port = tty->port;

	count.word = gxio_uart_read(context, UART_FIFO_COUNT);
	for (i = 0; i < count.rfifo_count; i++) {
		c = (char)gxio_uart_read(context, UART_RECEIVE_DATA);
		tty_insert_flip_char(port, c, TTY_NORMAL);
	}
}


/*
 * Drain the Rx FIFO, called by interrupt handler.
 */
static void handle_receive(struct tile_uart_port *tile_uart)
{
	struct tty_port *port = &tile_uart->uart.state->port;
	struct tty_struct *tty = tty_port_tty_get(port);
	gxio_uart_context_t *context = &tile_uart->context;

	if (!tty)
		return;

	/* First read UART rx fifo. */
	receive_chars(tile_uart, tty);

	/* Reset RFIFO_WE interrupt. */
	gxio_uart_write(context, UART_INTERRUPT_STATUS,
			UART_INTERRUPT_MASK__RFIFO_WE_MASK);

	/* Final read, if any chars comes between the first read and
	 * the interrupt reset.
	 */
	receive_chars(tile_uart, tty);

	spin_unlock(&tile_uart->uart.lock);
	tty_flip_buffer_push(port);
	spin_lock(&tile_uart->uart.lock);
	tty_kref_put(tty);
}


/*
 * Push one char to UART Write FIFO.
 * Return 0 on success, -1 if write filo is full.
 */
static int tilegx_putchar(gxio_uart_context_t *context, char c)
{
	UART_FLAG_t flag;
	flag.word = gxio_uart_read(context, UART_FLAG);
	if (flag.wfifo_full)
		return -1;

	gxio_uart_write(context, UART_TRANSMIT_DATA, (unsigned long)c);
	return 0;
}


/*
 * Send chars to UART Write FIFO; called by interrupt handler.
 */
static void handle_transmit(struct tile_uart_port *tile_uart)
{
	unsigned char ch;
	struct uart_port *port;
	struct circ_buf *xmit;
	gxio_uart_context_t *context = &tile_uart->context;

	/* First reset WFIFO_RE interrupt. */
	gxio_uart_write(context, UART_INTERRUPT_STATUS,
			UART_INTERRUPT_MASK__WFIFO_RE_MASK);

	port = &tile_uart->uart;
	xmit = &port->state->xmit;
	if (port->x_char) {
		if (tilegx_putchar(context, port->x_char))
			return;
		port->x_char = 0;
		port->icount.tx++;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return;

	while (!uart_circ_empty(xmit)) {
		ch = xmit->buf[xmit->tail];
		if (tilegx_putchar(context, ch))
			break;
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	/* Reset WFIFO_RE interrupt. */
	gxio_uart_write(context, UART_INTERRUPT_STATUS,
			UART_INTERRUPT_MASK__WFIFO_RE_MASK);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}


/*
 * UART Interrupt handler.
 */
static irqreturn_t tilegx_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	UART_INTERRUPT_STATUS_t intr_stat;
	struct tile_uart_port *tile_uart;
	gxio_uart_context_t *context;
	struct uart_port *port = dev_id;
	irqreturn_t ret = IRQ_NONE;

	spin_lock_irqsave(&port->lock, flags);

	tile_uart = container_of(port, struct tile_uart_port, uart);
	context = &tile_uart->context;
	intr_stat.word = gxio_uart_read(context, UART_INTERRUPT_STATUS);

	if (intr_stat.rfifo_we) {
		handle_receive(tile_uart);
		ret = IRQ_HANDLED;
	}
	if (intr_stat.wfifo_re) {
		handle_transmit(tile_uart);
		ret = IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}


/*
 * Return TIOCSER_TEMT when transmitter FIFO is empty.
 */
static u_int tilegx_tx_empty(struct uart_port *port)
{
	int ret;
	UART_FLAG_t flag;
	struct tile_uart_port *tile_uart;
	gxio_uart_context_t *context;

	tile_uart = container_of(port, struct tile_uart_port, uart);
	if (!mutex_trylock(&tile_uart->mutex))
		return 0;
	context = &tile_uart->context;

	flag.word = gxio_uart_read(context, UART_FLAG);
	ret = (flag.wfifo_empty) ? TIOCSER_TEMT : 0;
	mutex_unlock(&tile_uart->mutex);

	return ret;
}


/*
 * Set state of the modem control output lines.
 */
static void tilegx_set_mctrl(struct uart_port *port, u_int mctrl)
{
	/* N/A */
}


/*
 * Get state of the modem control input lines.
 */
static u_int tilegx_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}


/*
 * Stop transmitting.
 */
static void tilegx_stop_tx(struct uart_port *port)
{
	/* N/A */
}


/*
 * Start transmitting.
 */
static void tilegx_start_tx(struct uart_port *port)
{
	unsigned char ch;
	struct circ_buf *xmit;
	struct tile_uart_port *tile_uart;
	gxio_uart_context_t *context;

	tile_uart = container_of(port, struct tile_uart_port, uart);
	if (!mutex_trylock(&tile_uart->mutex))
		return;
	context = &tile_uart->context;
	xmit = &port->state->xmit;
	if (port->x_char) {
		if (tilegx_putchar(context, port->x_char))
			return;
		port->x_char = 0;
		port->icount.tx++;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		mutex_unlock(&tile_uart->mutex);
		return;
	}

	while (!uart_circ_empty(xmit)) {
		ch = xmit->buf[xmit->tail];
		if (tilegx_putchar(context, ch))
			break;
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	mutex_unlock(&tile_uart->mutex);
}


/*
 * Stop receiving - port is in process of being closed.
 */
static void tilegx_stop_rx(struct uart_port *port)
{
	int err;
	struct tile_uart_port *tile_uart;
	gxio_uart_context_t *context;
	int cpu;

	tile_uart = container_of(port, struct tile_uart_port, uart);
	if (!mutex_trylock(&tile_uart->mutex))
		return;

	context = &tile_uart->context;
	cpu = tile_uart->irq_cpu;
	err = gxio_uart_cfg_interrupt(context, cpu_x(cpu), cpu_y(cpu),
				      KERNEL_PL, -1);
	mutex_unlock(&tile_uart->mutex);
}


/*
 * Enable modem status interrupts.
 */
static void tilegx_enable_ms(struct uart_port *port)
{
	/* N/A */
}

/*
 * Control the transmission of a break signal.
 */
static void tilegx_break_ctl(struct uart_port *port, int break_state)
{
	/* N/A */
}


/*
 * Perform initialization and enable port for reception.
 */
static int tilegx_startup(struct uart_port *port)
{
	struct tile_uart_port *tile_uart;
	gxio_uart_context_t *context;
	int ret = 0;
	int cpu = raw_smp_processor_id();  /* pick an arbitrary cpu */

	tile_uart = container_of(port, struct tile_uart_port, uart);
	if (mutex_lock_interruptible(&tile_uart->mutex))
		return -EBUSY;
	context = &tile_uart->context;

	/* Now open the hypervisor device if we haven't already. */
	if (context->fd < 0) {
		UART_INTERRUPT_MASK_t intr_mask;

		/* Initialize UART device. */
		ret = gxio_uart_init(context, port->line);
		if (ret) {
			ret = -ENXIO;
			goto err;
		}

		/* Create our IRQs. */
		port->irq = create_irq();
		if (port->irq < 0)
			goto err_uart_dest;
		tile_irq_activate(port->irq, TILE_IRQ_PERCPU);

		/* Register our IRQs. */
		ret = request_irq(port->irq, tilegx_interrupt, 0,
				  tilegx_uart_driver.driver_name, port);
		if (ret)
			goto err_dest_irq;

		/* Request that the hardware start sending us interrupts. */
		tile_uart->irq_cpu = cpu;
		ret = gxio_uart_cfg_interrupt(context, cpu_x(cpu), cpu_y(cpu),
					      KERNEL_PL, port->irq);
		if (ret)
			goto err_free_irq;

		/* Enable UART Tx/Rx Interrupt. */
		intr_mask.word = gxio_uart_read(context, UART_INTERRUPT_MASK);
		intr_mask.wfifo_re = 0;
		intr_mask.rfifo_we = 0;
		gxio_uart_write(context, UART_INTERRUPT_MASK, intr_mask.word);

		/* Reset the Tx/Rx interrupt in case it's set. */
		gxio_uart_write(context, UART_INTERRUPT_STATUS,
				UART_INTERRUPT_MASK__WFIFO_RE_MASK |
				UART_INTERRUPT_MASK__RFIFO_WE_MASK);
	}

	mutex_unlock(&tile_uart->mutex);
	return ret;

err_free_irq:
	free_irq(port->irq, port);
err_dest_irq:
	destroy_irq(port->irq);
err_uart_dest:
	gxio_uart_destroy(context);
	ret = -ENXIO;
err:
	mutex_unlock(&tile_uart->mutex);
	return ret;
}


/*
 * Release kernel resources if it is the last close, disable the port,
 * free IRQ and close the port.
 */
static void tilegx_shutdown(struct uart_port *port)
{
	int err;
	UART_INTERRUPT_MASK_t intr_mask;
	struct tile_uart_port *tile_uart;
	gxio_uart_context_t *context;
	int cpu;

	tile_uart = container_of(port, struct tile_uart_port, uart);
	if (mutex_lock_interruptible(&tile_uart->mutex))
		return;
	context = &tile_uart->context;

	/* Disable UART Tx/Rx Interrupt. */
	intr_mask.word = gxio_uart_read(context, UART_INTERRUPT_MASK);
	intr_mask.wfifo_re = 1;
	intr_mask.rfifo_we = 1;
	gxio_uart_write(context, UART_INTERRUPT_MASK, intr_mask.word);

	/* Request that the hardware stop sending us interrupts. */
	cpu = tile_uart->irq_cpu;
	err = gxio_uart_cfg_interrupt(context, cpu_x(cpu), cpu_y(cpu),
				      KERNEL_PL, -1);

	if (port->irq > 0) {
		free_irq(port->irq, port);
		destroy_irq(port->irq);
		port->irq = 0;
	}

	gxio_uart_destroy(context);

	mutex_unlock(&tile_uart->mutex);
}


/*
 * Flush the buffer.
 */
static void tilegx_flush_buffer(struct uart_port *port)
{
	/* N/A */
}


/*
 * Change the port parameters.
 */
static void tilegx_set_termios(struct uart_port *port,
			       struct ktermios *termios, struct ktermios *old)
{
	int err;
	UART_DIVISOR_t divisor;
	UART_TYPE_t type;
	unsigned int baud;
	struct tile_uart_port *tile_uart;
	gxio_uart_context_t *context;

	tile_uart = container_of(port, struct tile_uart_port, uart);
	if (!mutex_trylock(&tile_uart->mutex))
		return;
	context = &tile_uart->context;

	/* Open the hypervisor device if we haven't already. */
	if (context->fd < 0) {
		err = gxio_uart_init(context, port->line);
		if (err) {
			mutex_unlock(&tile_uart->mutex);
			return;
		}
	}

	divisor.word = gxio_uart_read(context, UART_DIVISOR);
	type.word = gxio_uart_read(context, UART_TYPE);

	/* Divisor. */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk / 16);
	divisor.divisor = uart_get_divisor(port, baud);

	/* Byte size. */
	if ((termios->c_cflag & CSIZE) == CS7)
		type.dbits = UART_TYPE__DBITS_VAL_SEVEN_DBITS;
	else
		type.dbits = UART_TYPE__DBITS_VAL_EIGHT_DBITS;

	/* Parity. */
	if (termios->c_cflag & PARENB) {
		/* Mark or Space parity. */
		if (termios->c_cflag & CMSPAR)
			if (termios->c_cflag & PARODD)
				type.ptype = UART_TYPE__PTYPE_VAL_MARK;
			else
				type.ptype = UART_TYPE__PTYPE_VAL_SPACE;
		else if (termios->c_cflag & PARODD)
			type.ptype = UART_TYPE__PTYPE_VAL_ODD;
		else
			type.ptype = UART_TYPE__PTYPE_VAL_EVEN;
	} else
		type.ptype = UART_TYPE__PTYPE_VAL_NONE;

	/* Stop bits. */
	if (termios->c_cflag & CSTOPB)
		type.sbits = UART_TYPE__SBITS_VAL_TWO_SBITS;
	else
		type.sbits = UART_TYPE__SBITS_VAL_ONE_SBITS;

	/* Set the uart paramters. */
	gxio_uart_write(context, UART_DIVISOR, divisor.word);
	gxio_uart_write(context, UART_TYPE, type.word);

	mutex_unlock(&tile_uart->mutex);
}


/*
 * Return string describing the specified port.
 */
static const char *tilegx_type(struct uart_port *port)
{
	return port->type == PORT_TILEGX ? DRIVER_NAME_STRING : NULL;
}


/*
 * Release the resources being used by 'port'.
 */
static void tilegx_release_port(struct uart_port *port)
{
	/* Nothing to release. */
}


/*
 * Request the resources being used by 'port'.
 */
static int tilegx_request_port(struct uart_port *port)
{
	/* Always present. */
	return 0;
}


/*
 * Configure/autoconfigure the port.
 */
static void tilegx_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_TILEGX;
}


/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 */
static int tilegx_verify_port(struct uart_port *port,
			      struct serial_struct *ser)
{
	if ((ser->type != PORT_UNKNOWN) && (ser->type != PORT_TILEGX))
		return -EINVAL;

	return 0;
}

#ifdef CONFIG_CONSOLE_POLL

/*
 * Console polling routines for writing and reading from the uart while
 * in an interrupt or debug context.
 */

static int tilegx_poll_get_char(struct uart_port *port)
{
	UART_FIFO_COUNT_t count;
	gxio_uart_context_t *context;
	struct tile_uart_port *tile_uart;

	tile_uart = container_of(port, struct tile_uart_port, uart);
	context = &tile_uart->context;
	count.word = gxio_uart_read(context, UART_FIFO_COUNT);
	if (count.rfifo_count == 0)
		return NO_POLL_CHAR;
	return (char)gxio_uart_read(context, UART_RECEIVE_DATA);
}

static void tilegx_poll_put_char(struct uart_port *port, unsigned char c)
{
	gxio_uart_context_t *context;
	struct tile_uart_port *tile_uart;

	tile_uart = container_of(port, struct tile_uart_port, uart);
	context = &tile_uart->context;
	gxio_uart_write(context, UART_TRANSMIT_DATA, (unsigned long)c);
}

#endif /* CONFIG_CONSOLE_POLL */


static const struct uart_ops tilegx_ops = {
	.tx_empty	= tilegx_tx_empty,
	.set_mctrl	= tilegx_set_mctrl,
	.get_mctrl	= tilegx_get_mctrl,
	.stop_tx	= tilegx_stop_tx,
	.start_tx	= tilegx_start_tx,
	.stop_rx	= tilegx_stop_rx,
	.enable_ms	= tilegx_enable_ms,
	.break_ctl	= tilegx_break_ctl,
	.startup	= tilegx_startup,
	.shutdown	= tilegx_shutdown,
	.flush_buffer	= tilegx_flush_buffer,
	.set_termios	= tilegx_set_termios,
	.type		= tilegx_type,
	.release_port	= tilegx_release_port,
	.request_port	= tilegx_request_port,
	.config_port	= tilegx_config_port,
	.verify_port	= tilegx_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= tilegx_poll_get_char,
	.poll_put_char	= tilegx_poll_put_char,
#endif
};


static void tilegx_init_ports(void)
{
	int i;
	struct uart_port *port;

	for (i = 0; i < TILEGX_UART_NR; i++) {
		port = &tile_uart_ports[i].uart;
		port->ops = &tilegx_ops;
		port->line = i;
		port->type = PORT_TILEGX;
		port->uartclk = TILEGX_UART_REF_CLK;
		port->flags = UPF_BOOT_AUTOCONF;

		tile_uart_ports[i].context.fd = -1;
		mutex_init(&tile_uart_ports[i].mutex);
	}
}


static struct uart_driver tilegx_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME_STRING,
	.dev_name	= TILEGX_UART_NAME,
	.major		= TILEGX_UART_MAJOR,
	.minor		= TILEGX_UART_MINOR,
	.nr		= TILEGX_UART_NR,
};


static int __init tilegx_init(void)
{
	int i;
	int ret;
	struct tty_driver *tty_drv;

	ret = uart_register_driver(&tilegx_uart_driver);
	if (ret)
		return ret;
	tty_drv = tilegx_uart_driver.tty_driver;
	tty_drv->init_termios.c_cflag = B115200 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_drv->init_termios.c_ispeed = 115200;
	tty_drv->init_termios.c_ospeed = 115200;

	tilegx_init_ports();

	for (i = 0; i < TILEGX_UART_NR; i++) {
		struct uart_port *port = &tile_uart_ports[i].uart;
		ret = uart_add_one_port(&tilegx_uart_driver, port);
	}

	return 0;
}


static void __exit tilegx_exit(void)
{
	int i;
	struct uart_port *port;

	for (i = 0; i < TILEGX_UART_NR; i++) {
		port = &tile_uart_ports[i].uart;
		uart_remove_one_port(&tilegx_uart_driver, port);
	}

	uart_unregister_driver(&tilegx_uart_driver);
}


module_init(tilegx_init);
module_exit(tilegx_exit);

MODULE_AUTHOR("Tilera Corporation");
MODULE_DESCRIPTION("TILEGx serial port driver");
MODULE_LICENSE("GPL");
