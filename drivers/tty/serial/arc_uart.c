/*
 * ARC On-Chip(fpga) UART Driver
 *
 * Copyright (C) 2010-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: July 10th 2012
 *  -Decoupled the driver from arch/arc
 *    +Using platform_get_resource() for irq/membase (thx to bfin_uart.c)
 *    +Using early_platform_xxx() for early console (thx to mach-shmobile/xxx)
 *
 * Vineetg: Aug 21st 2010
 *  -Is uart_tx_stopped() not done in tty write path as it has already been
 *   taken care of, in serial core
 *
 * Vineetg: Aug 18th 2010
 *  -New Serial Core based ARC UART driver
 *  -Derived largely from blackfin driver albiet with some major tweaks
 *
 * TODO:
 *  -check if sysreq works
 */

#if defined(CONFIG_SERIAL_ARC_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

/*************************************
 * ARC UART Hardware Specs
 ************************************/
#define ARC_UART_TX_FIFO_SIZE  1

/*
 * UART Register set (this is not a Standards Compliant IP)
 * Also each reg is Word aligned, but only 8 bits wide
 */
#define R_ID0	0
#define R_ID1	4
#define R_ID2	8
#define R_ID3	12
#define R_DATA	16
#define R_STS	20
#define R_BAUDL	24
#define R_BAUDH	28

/* Bits for UART Status Reg (R/W) */
#define RXIENB  0x04	/* Receive Interrupt Enable */
#define TXIENB  0x40	/* Transmit Interrupt Enable */

#define RXEMPTY 0x20	/* Receive FIFO Empty: No char receivede */
#define TXEMPTY 0x80	/* Transmit FIFO Empty, thus char can be written into */

#define RXFULL  0x08	/* Receive FIFO full */
#define RXFULL1 0x10	/* Receive FIFO has space for 1 char (tot space=4) */

#define RXFERR  0x01	/* Frame Error: Stop Bit not detected */
#define RXOERR  0x02	/* OverFlow Err: Char recv but RXFULL still set */

/* Uart bit fiddling helpers: lowest level */
#define RBASE(port, reg)      (port->membase + reg)
#define UART_REG_SET(u, r, v) writeb((v), RBASE(u, r))
#define UART_REG_GET(u, r)    readb(RBASE(u, r))

#define UART_REG_OR(u, r, v)  UART_REG_SET(u, r, UART_REG_GET(u, r) | (v))
#define UART_REG_CLR(u, r, v) UART_REG_SET(u, r, UART_REG_GET(u, r) & ~(v))

/* Uart bit fiddling helpers: API level */
#define UART_SET_DATA(uart, val)   UART_REG_SET(uart, R_DATA, val)
#define UART_GET_DATA(uart)        UART_REG_GET(uart, R_DATA)

#define UART_SET_BAUDH(uart, val)  UART_REG_SET(uart, R_BAUDH, val)
#define UART_SET_BAUDL(uart, val)  UART_REG_SET(uart, R_BAUDL, val)

#define UART_CLR_STATUS(uart, val) UART_REG_CLR(uart, R_STS, val)
#define UART_GET_STATUS(uart)      UART_REG_GET(uart, R_STS)

#define UART_ALL_IRQ_DISABLE(uart) UART_REG_CLR(uart, R_STS, RXIENB|TXIENB)
#define UART_RX_IRQ_DISABLE(uart)  UART_REG_CLR(uart, R_STS, RXIENB)
#define UART_TX_IRQ_DISABLE(uart)  UART_REG_CLR(uart, R_STS, TXIENB)

#define UART_ALL_IRQ_ENABLE(uart)  UART_REG_OR(uart, R_STS, RXIENB|TXIENB)
#define UART_RX_IRQ_ENABLE(uart)   UART_REG_OR(uart, R_STS, RXIENB)
#define UART_TX_IRQ_ENABLE(uart)   UART_REG_OR(uart, R_STS, TXIENB)

#define ARC_SERIAL_DEV_NAME	"ttyARC"

struct arc_uart_port {
	struct uart_port port;
	unsigned long baud;
};

#define to_arc_port(uport)  container_of(uport, struct arc_uart_port, port)

static struct arc_uart_port arc_uart_ports[CONFIG_SERIAL_ARC_NR_PORTS];

#ifdef CONFIG_SERIAL_ARC_CONSOLE
static struct console arc_console;
#endif

#define DRIVER_NAME	"arc-uart"

static struct uart_driver arc_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= ARC_SERIAL_DEV_NAME,
	.major		= 0,
	.minor		= 0,
	.nr		= CONFIG_SERIAL_ARC_NR_PORTS,
#ifdef CONFIG_SERIAL_ARC_CONSOLE
	.cons		= &arc_console,
#endif
};

static void arc_serial_stop_rx(struct uart_port *port)
{
	UART_RX_IRQ_DISABLE(port);
}

static void arc_serial_stop_tx(struct uart_port *port)
{
	while (!(UART_GET_STATUS(port) & TXEMPTY))
		cpu_relax();

	UART_TX_IRQ_DISABLE(port);
}

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static unsigned int arc_serial_tx_empty(struct uart_port *port)
{
	unsigned int stat;

	stat = UART_GET_STATUS(port);
	if (stat & TXEMPTY)
		return TIOCSER_TEMT;

	return 0;
}

/*
 * Driver internal routine, used by both tty(serial core) as well as tx-isr
 *  -Called under spinlock in either cases
 *  -also tty->stopped has already been checked
 *     = by uart_start( ) before calling us
 *     = tx_ist checks that too before calling
 */
static void arc_serial_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	int sent = 0;
	unsigned char ch;

	if (unlikely(port->x_char)) {
		UART_SET_DATA(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		sent = 1;
	} else if (!uart_circ_empty(xmit)) {
		ch = xmit->buf[xmit->tail];
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		while (!(UART_GET_STATUS(port) & TXEMPTY))
			cpu_relax();
		UART_SET_DATA(port, ch);
		sent = 1;
	}

	/*
	 * If num chars in xmit buffer are too few, ask tty layer for more.
	 * By Hard ISR to schedule processing in software interrupt part
	 */
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (sent)
		UART_TX_IRQ_ENABLE(port);
}

/*
 * port is locked and interrupts are disabled
 * uart_start( ) calls us under the port spinlock irqsave
 */
static void arc_serial_start_tx(struct uart_port *port)
{
	arc_serial_tx_chars(port);
}

static void arc_serial_rx_chars(struct uart_port *port, unsigned int status)
{
	unsigned int ch, flg = 0;

	/*
	 * UART has 4 deep RX-FIFO. Driver's recongnition of this fact
	 * is very subtle. Here's how ...
	 * Upon getting a RX-Intr, such that RX-EMPTY=0, meaning data available,
	 * driver reads the DATA Reg and keeps doing that in a loop, until
	 * RX-EMPTY=1. Multiple chars being avail, with a single Interrupt,
	 * before RX-EMPTY=0, implies some sort of buffering going on in the
	 * controller, which is indeed the Rx-FIFO.
	 */
	do {
		/*
		 * This could be an Rx Intr for err (no data),
		 * so check err and clear that Intr first
		 */
		if (unlikely(status & (RXOERR | RXFERR))) {
			if (status & RXOERR) {
				port->icount.overrun++;
				flg = TTY_OVERRUN;
				UART_CLR_STATUS(port, RXOERR);
			}

			if (status & RXFERR) {
				port->icount.frame++;
				flg = TTY_FRAME;
				UART_CLR_STATUS(port, RXFERR);
			}
		} else
			flg = TTY_NORMAL;

		if (status & RXEMPTY)
			continue;

		ch = UART_GET_DATA(port);
		port->icount.rx++;

		if (!(uart_handle_sysrq_char(port, ch)))
			uart_insert_char(port, status, RXOERR, ch, flg);

		spin_unlock(&port->lock);
		tty_flip_buffer_push(&port->state->port);
		spin_lock(&port->lock);
	} while (!((status = UART_GET_STATUS(port)) & RXEMPTY));
}

/*
 * A note on the Interrupt handling state machine of this driver
 *
 * kernel printk writes funnel thru the console driver framework and in order
 * to keep things simple as well as efficient, it writes to UART in polled
 * mode, in one shot, and exits.
 *
 * OTOH, Userland output (via tty layer), uses interrupt based writes as there
 * can be undeterministic delay between char writes.
 *
 * Thus Rx-interrupts are always enabled, while tx-interrupts are by default
 * disabled.
 *
 * When tty has some data to send out, serial core calls driver's start_tx
 * which
 *   -checks-if-tty-buffer-has-char-to-send
 *   -writes-data-to-uart
 *   -enable-tx-intr
 *
 * Once data bits are pushed out, controller raises the Tx-room-avail-Interrupt.
 * The first thing Tx ISR does is disable further Tx interrupts (as this could
 * be the last char to send, before settling down into the quiet polled mode).
 * It then calls the exact routine used by tty layer write to send out any
 * more char in tty buffer. In case of sending, it re-enables Tx-intr. In case
 * of no data, it remains disabled.
 * This is how the transmit state machine is dynamically switched on/off
 */

static irqreturn_t arc_serial_isr(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned int status;

	status = UART_GET_STATUS(port);

	/*
	 * Single IRQ for both Rx (data available) Tx (room available) Interrupt
	 * notifications from the UART Controller.
	 * To demultiplex between the two, we check the relevant bits
	 */
	if (status & RXIENB) {

		/* already in ISR, no need of xx_irqsave */
		spin_lock(&port->lock);
		arc_serial_rx_chars(port, status);
		spin_unlock(&port->lock);
	}

	if ((status & TXIENB) && (status & TXEMPTY)) {

		/* Unconditionally disable further Tx-Interrupts.
		 * will be enabled by tx_chars() if needed.
		 */
		UART_TX_IRQ_DISABLE(port);

		spin_lock(&port->lock);

		if (!uart_tx_stopped(port))
			arc_serial_tx_chars(port);

		spin_unlock(&port->lock);
	}

	return IRQ_HANDLED;
}

static unsigned int arc_serial_get_mctrl(struct uart_port *port)
{
	/*
	 * Pretend we have a Modem status reg and following bits are
	 *  always set, to satify the serial core state machine
	 *  (DSR) Data Set Ready
	 *  (CTS) Clear To Send
	 *  (CAR) Carrier Detect
	 */
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void arc_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* MCR not present */
}

static void arc_serial_break_ctl(struct uart_port *port, int break_state)
{
	/* ARC UART doesn't support sending Break signal */
}

static int arc_serial_startup(struct uart_port *port)
{
	/* Before we hook up the ISR, Disable all UART Interrupts */
	UART_ALL_IRQ_DISABLE(port);

	if (request_irq(port->irq, arc_serial_isr, 0, "arc uart rx-tx", port)) {
		dev_warn(port->dev, "Unable to attach ARC UART intr\n");
		return -EBUSY;
	}

	UART_RX_IRQ_ENABLE(port); /* Only Rx IRQ enabled to begin with */

	return 0;
}

/* This is not really needed */
static void arc_serial_shutdown(struct uart_port *port)
{
	free_irq(port->irq, port);
}

static void
arc_serial_set_termios(struct uart_port *port, struct ktermios *new,
		       struct ktermios *old)
{
	struct arc_uart_port *uart = to_arc_port(port);
	unsigned int baud, uartl, uarth, hw_val;
	unsigned long flags;

	/*
	 * Use the generic handler so that any specially encoded baud rates
	 * such as SPD_xx flags or "%B0" can be handled
	 * Max Baud I suppose will not be more than current 115K * 4
	 * Formula for ARC UART is: hw-val = ((CLK/(BAUD*4)) -1)
	 * spread over two 8-bit registers
	 */
	baud = uart_get_baud_rate(port, new, old, 0, 460800);

	hw_val = port->uartclk / (uart->baud * 4) - 1;
	uartl = hw_val & 0xFF;
	uarth = (hw_val >> 8) & 0xFF;

	spin_lock_irqsave(&port->lock, flags);

	UART_ALL_IRQ_DISABLE(port);

	UART_SET_BAUDL(port, uartl);
	UART_SET_BAUDH(port, uarth);

	UART_RX_IRQ_ENABLE(port);

	/*
	 * UART doesn't support Parity/Hardware Flow Control;
	 * Only supports 8N1 character size
	 */
	new->c_cflag &= ~(CMSPAR|CRTSCTS|CSIZE);
	new->c_cflag |= CS8;

	if (old)
		tty_termios_copy_hw(new, old);

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(new))
		tty_termios_encode_baud_rate(new, baud, baud);

	uart_update_timeout(port, new->c_cflag, baud);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *arc_serial_type(struct uart_port *port)
{
	return port->type == PORT_ARC ? DRIVER_NAME : NULL;
}

static void arc_serial_release_port(struct uart_port *port)
{
}

static int arc_serial_request_port(struct uart_port *port)
{
	return 0;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 */
static int
arc_serial_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (port->type != PORT_UNKNOWN && ser->type != PORT_ARC)
		return -EINVAL;

	return 0;
}

/*
 * Configure/autoconfigure the port.
 */
static void arc_serial_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_ARC;
}

#ifdef CONFIG_CONSOLE_POLL

static void arc_serial_poll_putchar(struct uart_port *port, unsigned char chr)
{
	while (!(UART_GET_STATUS(port) & TXEMPTY))
		cpu_relax();

	UART_SET_DATA(port, chr);
}

static int arc_serial_poll_getchar(struct uart_port *port)
{
	unsigned char chr;

	while (!(UART_GET_STATUS(port) & RXEMPTY))
		cpu_relax();

	chr = UART_GET_DATA(port);
	return chr;
}
#endif

static const struct uart_ops arc_serial_pops = {
	.tx_empty	= arc_serial_tx_empty,
	.set_mctrl	= arc_serial_set_mctrl,
	.get_mctrl	= arc_serial_get_mctrl,
	.stop_tx	= arc_serial_stop_tx,
	.start_tx	= arc_serial_start_tx,
	.stop_rx	= arc_serial_stop_rx,
	.break_ctl	= arc_serial_break_ctl,
	.startup	= arc_serial_startup,
	.shutdown	= arc_serial_shutdown,
	.set_termios	= arc_serial_set_termios,
	.type		= arc_serial_type,
	.release_port	= arc_serial_release_port,
	.request_port	= arc_serial_request_port,
	.config_port	= arc_serial_config_port,
	.verify_port	= arc_serial_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_put_char = arc_serial_poll_putchar,
	.poll_get_char = arc_serial_poll_getchar,
#endif
};

#ifdef CONFIG_SERIAL_ARC_CONSOLE

static int arc_serial_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= CONFIG_SERIAL_ARC_NR_PORTS)
		return -ENODEV;

	/*
	 * The uart port backing the console (e.g. ttyARC1) might not have been
	 * init yet. If so, defer the console setup to after the port.
	 */
	port = &arc_uart_ports[co->index].port;
	if (!port->membase)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	/*
	 * Serial core will call port->ops->set_termios( )
	 * which will set the baud reg
	 */
	return uart_set_options(port, co, baud, parity, bits, flow);
}

static void arc_serial_console_putchar(struct uart_port *port, int ch)
{
	while (!(UART_GET_STATUS(port) & TXEMPTY))
		cpu_relax();

	UART_SET_DATA(port, (unsigned char)ch);
}

/*
 * Interrupts are disabled on entering
 */
static void arc_serial_console_write(struct console *co, const char *s,
				     unsigned int count)
{
	struct uart_port *port = &arc_uart_ports[co->index].port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	uart_console_write(port, s, count, arc_serial_console_putchar);
	spin_unlock_irqrestore(&port->lock, flags);
}

static struct console arc_console = {
	.name	= ARC_SERIAL_DEV_NAME,
	.write	= arc_serial_console_write,
	.device	= uart_console_device,
	.setup	= arc_serial_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &arc_uart_driver
};

static void arc_early_serial_write(struct console *con, const char *s,
				   unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, arc_serial_console_putchar);
}

static int __init arc_early_console_setup(struct earlycon_device *dev,
					  const char *opt)
{
	struct uart_port *port = &dev->port;
	unsigned int l, h, hw_val;

	if (!dev->port.membase)
		return -ENODEV;

	hw_val = port->uartclk / (dev->baud * 4) - 1;
	l = hw_val & 0xFF;
	h = (hw_val >> 8) & 0xFF;

	UART_SET_BAUDL(port, l);
	UART_SET_BAUDH(port, h);

	dev->con->write = arc_early_serial_write;
	return 0;
}
OF_EARLYCON_DECLARE(arc_uart, "snps,arc-uart", arc_early_console_setup);

#endif	/* CONFIG_SERIAL_ARC_CONSOLE */

static int arc_serial_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct arc_uart_port *uart;
	struct uart_port *port;
	int dev_id;
	u32 val;

	/* no device tree device */
	if (!np)
		return -ENODEV;

	dev_id = of_alias_get_id(np, "serial");
	if (dev_id < 0)
		dev_id = 0;

	uart = &arc_uart_ports[dev_id];
	port = &uart->port;

	if (of_property_read_u32(np, "clock-frequency", &val)) {
		dev_err(&pdev->dev, "clock-frequency property NOTset\n");
		return -EINVAL;
	}
	port->uartclk = val;

	if (of_property_read_u32(np, "current-speed", &val)) {
		dev_err(&pdev->dev, "current-speed property NOT set\n");
		return -EINVAL;
	}
	uart->baud = val;

	port->membase = of_iomap(np, 0);
	if (!port->membase)
		/* No point of dev_err since UART itself is hosed here */
		return -ENXIO;

	port->irq = irq_of_parse_and_map(np, 0);

	port->dev = &pdev->dev;
	port->iotype = UPIO_MEM;
	port->flags = UPF_BOOT_AUTOCONF;
	port->line = dev_id;
	port->ops = &arc_serial_pops;

	port->fifosize = ARC_UART_TX_FIFO_SIZE;

	/*
	 * uart_insert_char( ) uses it in decideding whether to ignore a
	 * char or not. Explicitly setting it here, removes the subtelty
	 */
	port->ignore_status_mask = 0;

	return uart_add_one_port(&arc_uart_driver, &arc_uart_ports[dev_id].port);
}

static int arc_serial_remove(struct platform_device *pdev)
{
	/* This will never be called */
	return 0;
}

static const struct of_device_id arc_uart_dt_ids[] = {
	{ .compatible = "snps,arc-uart" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, arc_uart_dt_ids);

static struct platform_driver arc_platform_driver = {
	.probe = arc_serial_probe,
	.remove = arc_serial_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table  = arc_uart_dt_ids,
	 },
};

static int __init arc_serial_init(void)
{
	int ret;

	ret = uart_register_driver(&arc_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&arc_platform_driver);
	if (ret)
		uart_unregister_driver(&arc_uart_driver);

	return ret;
}

static void __exit arc_serial_exit(void)
{
	platform_driver_unregister(&arc_platform_driver);
	uart_unregister_driver(&arc_uart_driver);
}

module_init(arc_serial_init);
module_exit(arc_serial_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Vineet Gupta");
MODULE_DESCRIPTION("ARC(Synopsys) On-Chip(fpga) serial driver");
