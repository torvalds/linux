/*
 *  Driver for GRLIB serial ports (APBUART)
 *
 *  Based on linux/drivers/serial/amba.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *  Copyright (C) 2003 Konrad Eisele <eiselekd@web.de>
 *  Copyright (C) 2006 Daniel Hellstrom <daniel@gaisler.com>, Aeroflex Gaisler AB
 *  Copyright (C) 2008 Gilead Kutnick <kutnickg@zin-tech.com>
 *  Copyright (C) 2009 Kristoffer Glembo <kristoffer@gaisler.com>, Aeroflex Gaisler AB
 */

#if defined(CONFIG_SERIAL_GRLIB_GAISLER_APBUART_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/serial_core.h>
#include <asm/irq.h>

#include "apbuart.h"

#define SERIAL_APBUART_MAJOR	TTY_MAJOR
#define SERIAL_APBUART_MINOR	64
#define UART_DUMMY_RSR_RX	0x8000	/* for ignore all read */

static void apbuart_tx_chars(struct uart_port *port);

static void apbuart_stop_tx(struct uart_port *port)
{
	unsigned int cr;

	cr = UART_GET_CTRL(port);
	cr &= ~UART_CTRL_TI;
	UART_PUT_CTRL(port, cr);
}

static void apbuart_start_tx(struct uart_port *port)
{
	unsigned int cr;

	cr = UART_GET_CTRL(port);
	cr |= UART_CTRL_TI;
	UART_PUT_CTRL(port, cr);

	if (UART_GET_STATUS(port) & UART_STATUS_THE)
		apbuart_tx_chars(port);
}

static void apbuart_stop_rx(struct uart_port *port)
{
	unsigned int cr;

	cr = UART_GET_CTRL(port);
	cr &= ~(UART_CTRL_RI);
	UART_PUT_CTRL(port, cr);
}

static void apbuart_enable_ms(struct uart_port *port)
{
	/* No modem status change interrupts for APBUART */
}

static void apbuart_rx_chars(struct uart_port *port)
{
	struct tty_struct *tty = port->state->port.tty;
	unsigned int status, ch, rsr, flag;
	unsigned int max_chars = port->fifosize;

	status = UART_GET_STATUS(port);

	while (UART_RX_DATA(status) && (max_chars--)) {

		ch = UART_GET_CHAR(port);
		flag = TTY_NORMAL;

		port->icount.rx++;

		rsr = UART_GET_STATUS(port) | UART_DUMMY_RSR_RX;
		UART_PUT_STATUS(port, 0);
		if (rsr & UART_STATUS_ERR) {

			if (rsr & UART_STATUS_BR) {
				rsr &= ~(UART_STATUS_FE | UART_STATUS_PE);
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			} else if (rsr & UART_STATUS_PE) {
				port->icount.parity++;
			} else if (rsr & UART_STATUS_FE) {
				port->icount.frame++;
			}
			if (rsr & UART_STATUS_OE)
				port->icount.overrun++;

			rsr &= port->read_status_mask;

			if (rsr & UART_STATUS_PE)
				flag = TTY_PARITY;
			else if (rsr & UART_STATUS_FE)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		uart_insert_char(port, rsr, UART_STATUS_OE, ch, flag);


	      ignore_char:
		status = UART_GET_STATUS(port);
	}

	tty_flip_buffer_push(tty);
}

static void apbuart_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	int count;

	if (port->x_char) {
		UART_PUT_CHAR(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		apbuart_stop_tx(port);
		return;
	}

	/* amba: fill FIFO */
	count = port->fifosize >> 1;
	do {
		UART_PUT_CHAR(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		apbuart_stop_tx(port);
}

static irqreturn_t apbuart_int(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned int status;

	spin_lock(&port->lock);

	status = UART_GET_STATUS(port);
	if (status & UART_STATUS_DR)
		apbuart_rx_chars(port);
	if (status & UART_STATUS_THE)
		apbuart_tx_chars(port);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static unsigned int apbuart_tx_empty(struct uart_port *port)
{
	unsigned int status = UART_GET_STATUS(port);
	return status & UART_STATUS_THE ? TIOCSER_TEMT : 0;
}

static unsigned int apbuart_get_mctrl(struct uart_port *port)
{
	/* The GRLIB APBUART handles flow control in hardware */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

static void apbuart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* The GRLIB APBUART handles flow control in hardware */
}

static void apbuart_break_ctl(struct uart_port *port, int break_state)
{
	/* We don't support sending break */
}

static int apbuart_startup(struct uart_port *port)
{
	int retval;
	unsigned int cr;

	/* Allocate the IRQ */
	retval = request_irq(port->irq, apbuart_int, 0, "apbuart", port);
	if (retval)
		return retval;

	/* Finally, enable interrupts */
	cr = UART_GET_CTRL(port);
	UART_PUT_CTRL(port,
		      cr | UART_CTRL_RE | UART_CTRL_TE |
		      UART_CTRL_RI | UART_CTRL_TI);

	return 0;
}

static void apbuart_shutdown(struct uart_port *port)
{
	unsigned int cr;

	/* disable all interrupts, disable the port */
	cr = UART_GET_CTRL(port);
	UART_PUT_CTRL(port,
		      cr & ~(UART_CTRL_RE | UART_CTRL_TE |
			     UART_CTRL_RI | UART_CTRL_TI));

	/* Free the interrupt */
	free_irq(port->irq, port);
}

static void apbuart_set_termios(struct uart_port *port,
				struct ktermios *termios, struct ktermios *old)
{
	unsigned int cr;
	unsigned long flags;
	unsigned int baud, quot;

	/* Ask the core to calculate the divisor for us. */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk / 16);
	if (baud == 0)
		panic("invalid baudrate %i\n", port->uartclk / 16);

	/* uart_get_divisor calc a *16 uart freq, apbuart is *8 */
	quot = (uart_get_divisor(port, baud)) * 2;
	cr = UART_GET_CTRL(port);
	cr &= ~(UART_CTRL_PE | UART_CTRL_PS);

	if (termios->c_cflag & PARENB) {
		cr |= UART_CTRL_PE;
		if ((termios->c_cflag & PARODD))
			cr |= UART_CTRL_PS;
	}

	/* Enable flow control. */
	if (termios->c_cflag & CRTSCTS)
		cr |= UART_CTRL_FL;

	spin_lock_irqsave(&port->lock, flags);

	/* Update the per-port timeout. */
	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = UART_STATUS_OE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UART_STATUS_FE | UART_STATUS_PE;

	/* Characters to ignore */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= UART_STATUS_FE | UART_STATUS_PE;

	/* Ignore all characters if CREAD is not set. */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= UART_DUMMY_RSR_RX;

	/* Set baud rate */
	quot -= 1;
	UART_PUT_SCAL(port, quot);
	UART_PUT_CTRL(port, cr);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *apbuart_type(struct uart_port *port)
{
	return port->type == PORT_APBUART ? "GRLIB/APBUART" : NULL;
}

static void apbuart_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, 0x100);
}

static int apbuart_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, 0x100, "grlib-apbuart")
	    != NULL ? 0 : -EBUSY;
	return 0;
}

/* Configure/autoconfigure the port */
static void apbuart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_APBUART;
		apbuart_request_port(port);
	}
}

/* Verify the new serial_struct (for TIOCSSERIAL) */
static int apbuart_verify_port(struct uart_port *port,
			       struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_APBUART)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= NR_IRQS)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops grlib_apbuart_ops = {
	.tx_empty = apbuart_tx_empty,
	.set_mctrl = apbuart_set_mctrl,
	.get_mctrl = apbuart_get_mctrl,
	.stop_tx = apbuart_stop_tx,
	.start_tx = apbuart_start_tx,
	.stop_rx = apbuart_stop_rx,
	.enable_ms = apbuart_enable_ms,
	.break_ctl = apbuart_break_ctl,
	.startup = apbuart_startup,
	.shutdown = apbuart_shutdown,
	.set_termios = apbuart_set_termios,
	.type = apbuart_type,
	.release_port = apbuart_release_port,
	.request_port = apbuart_request_port,
	.config_port = apbuart_config_port,
	.verify_port = apbuart_verify_port,
};

static struct uart_port grlib_apbuart_ports[UART_NR];
static struct device_node *grlib_apbuart_nodes[UART_NR];

static int apbuart_scan_fifo_size(struct uart_port *port, int portnumber)
{
	int ctrl, loop = 0;
	int status;
	int fifosize;
	unsigned long flags;

	ctrl = UART_GET_CTRL(port);

	/*
	 * Enable the transceiver and wait for it to be ready to send data.
	 * Clear interrupts so that this process will not be externally
	 * interrupted in the middle (which can cause the transceiver to
	 * drain prematurely).
	 */

	local_irq_save(flags);

	UART_PUT_CTRL(port, ctrl | UART_CTRL_TE);

	while (!UART_TX_READY(UART_GET_STATUS(port)))
		loop++;

	/*
	 * Disable the transceiver so data isn't actually sent during the
	 * actual test.
	 */

	UART_PUT_CTRL(port, ctrl & ~(UART_CTRL_TE));

	fifosize = 1;
	UART_PUT_CHAR(port, 0);

	/*
	 * So long as transmitting a character increments the tranceivier FIFO
	 * length the FIFO must be at least that big. These bytes will
	 * automatically drain off of the FIFO.
	 */

	status = UART_GET_STATUS(port);
	while (((status >> 20) & 0x3F) == fifosize) {
		fifosize++;
		UART_PUT_CHAR(port, 0);
		status = UART_GET_STATUS(port);
	}

	fifosize--;

	UART_PUT_CTRL(port, ctrl);
	local_irq_restore(flags);

	if (fifosize == 0)
		fifosize = 1;

	return fifosize;
}

static void apbuart_flush_fifo(struct uart_port *port)
{
	int i;

	for (i = 0; i < port->fifosize; i++)
		UART_GET_CHAR(port);
}


/* ======================================================================== */
/* Console driver, if enabled                                               */
/* ======================================================================== */

#ifdef CONFIG_SERIAL_GRLIB_GAISLER_APBUART_CONSOLE

static void apbuart_console_putchar(struct uart_port *port, int ch)
{
	unsigned int status;
	do {
		status = UART_GET_STATUS(port);
	} while (!UART_TX_READY(status));
	UART_PUT_CHAR(port, ch);
}

static void
apbuart_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *port = &grlib_apbuart_ports[co->index];
	unsigned int status, old_cr, new_cr;

	/* First save the CR then disable the interrupts */
	old_cr = UART_GET_CTRL(port);
	new_cr = old_cr & ~(UART_CTRL_RI | UART_CTRL_TI);
	UART_PUT_CTRL(port, new_cr);

	uart_console_write(port, s, count, apbuart_console_putchar);

	/*
	 *      Finally, wait for transmitter to become empty
	 *      and restore the TCR
	 */
	do {
		status = UART_GET_STATUS(port);
	} while (!UART_TX_READY(status));
	UART_PUT_CTRL(port, old_cr);
}

static void __init
apbuart_console_get_options(struct uart_port *port, int *baud,
			    int *parity, int *bits)
{
	if (UART_GET_CTRL(port) & (UART_CTRL_RE | UART_CTRL_TE)) {

		unsigned int quot, status;
		status = UART_GET_STATUS(port);

		*parity = 'n';
		if (status & UART_CTRL_PE) {
			if ((status & UART_CTRL_PS) == 0)
				*parity = 'e';
			else
				*parity = 'o';
		}

		*bits = 8;
		quot = UART_GET_SCAL(port) / 8;
		*baud = port->uartclk / (16 * (quot + 1));
	}
}

static int __init apbuart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	pr_debug("apbuart_console_setup co=%p, co->index=%i, options=%s\n",
		 co, co->index, options);

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= grlib_apbuart_port_nr)
		co->index = 0;

	port = &grlib_apbuart_ports[co->index];

	spin_lock_init(&port->lock);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		apbuart_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver grlib_apbuart_driver;

static struct console grlib_apbuart_console = {
	.name = "ttyS",
	.write = apbuart_console_write,
	.device = uart_console_device,
	.setup = apbuart_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &grlib_apbuart_driver,
};


static void grlib_apbuart_configure(void);

static int __init apbuart_console_init(void)
{
	grlib_apbuart_configure();
	register_console(&grlib_apbuart_console);
	return 0;
}

console_initcall(apbuart_console_init);

#define APBUART_CONSOLE	(&grlib_apbuart_console)
#else
#define APBUART_CONSOLE	NULL
#endif

static struct uart_driver grlib_apbuart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "serial",
	.dev_name = "ttyS",
	.major = SERIAL_APBUART_MAJOR,
	.minor = SERIAL_APBUART_MINOR,
	.nr = UART_NR,
	.cons = APBUART_CONSOLE,
};


/* ======================================================================== */
/* OF Platform Driver                                                       */
/* ======================================================================== */

static int __devinit apbuart_probe(struct of_device *op,
				   const struct of_device_id *match)
{
	int i = -1;
	struct uart_port *port = NULL;

	i = 0;
	for (i = 0; i < grlib_apbuart_port_nr; i++) {
		if (op->dev.of_node == grlib_apbuart_nodes[i])
			break;
	}

	port = &grlib_apbuart_ports[i];
	port->dev = &op->dev;

	uart_add_one_port(&grlib_apbuart_driver, (struct uart_port *) port);

	apbuart_flush_fifo((struct uart_port *) port);

	printk(KERN_INFO "grlib-apbuart at 0x%llx, irq %d\n",
	       (unsigned long long) port->mapbase, port->irq);
	return 0;

}

static struct of_device_id __initdata apbuart_match[] = {
	{
	 .name = "GAISLER_APBUART",
	 },
	{},
};

static struct of_platform_driver grlib_apbuart_of_driver = {
	.probe = apbuart_probe,
	.driver = {
		.owner = THIS_MODULE,
		.name = "grlib-apbuart",
		.of_match_table = apbuart_match,
	},
};


static void grlib_apbuart_configure(void)
{
	static int enum_done;
	struct device_node *np, *rp;
	struct uart_port *port = NULL;
	const u32 *prop;
	int freq_khz;
	int v = 0, d = 0;
	unsigned int addr;
	int irq, line;
	struct amba_prom_registers *regs;

	if (enum_done)
		return;

	/* Get bus frequency */
	rp = of_find_node_by_path("/");
	rp = of_get_next_child(rp, NULL);
	prop = of_get_property(rp, "clock-frequency", NULL);
	freq_khz = *prop;

	line = 0;
	for_each_matching_node(np, apbuart_match) {

		int *vendor = (int *) of_get_property(np, "vendor", NULL);
		int *device = (int *) of_get_property(np, "device", NULL);
		int *irqs = (int *) of_get_property(np, "interrupts", NULL);
		regs = (struct amba_prom_registers *)
		    of_get_property(np, "reg", NULL);

		if (vendor)
			v = *vendor;
		if (device)
			d = *device;

		if (!irqs || !regs)
			return;

		grlib_apbuart_nodes[line] = np;

		addr = regs->phys_addr;
		irq = *irqs;

		port = &grlib_apbuart_ports[line];

		port->mapbase = addr;
		port->membase = ioremap(addr, sizeof(struct grlib_apbuart_regs_map));
		port->irq = irq;
		port->iotype = UPIO_MEM;
		port->ops = &grlib_apbuart_ops;
		port->flags = UPF_BOOT_AUTOCONF;
		port->line = line;
		port->uartclk = freq_khz * 1000;
		port->fifosize = apbuart_scan_fifo_size((struct uart_port *) port, line);
		line++;

		/* We support maximum UART_NR uarts ... */
		if (line == UART_NR)
			break;

	}

	enum_done = 1;

	grlib_apbuart_driver.nr = grlib_apbuart_port_nr = line;
}

static int __init grlib_apbuart_init(void)
{
	int ret;

	/* Find all APBUARTS in device the tree and initialize their ports */
	grlib_apbuart_configure();

	printk(KERN_INFO "Serial: GRLIB APBUART driver\n");

	ret = uart_register_driver(&grlib_apbuart_driver);

	if (ret) {
		printk(KERN_ERR "%s: uart_register_driver failed (%i)\n",
		       __FILE__, ret);
		return ret;
	}

	ret = of_register_platform_driver(&grlib_apbuart_of_driver);
	if (ret) {
		printk(KERN_ERR
		       "%s: of_register_platform_driver failed (%i)\n",
		       __FILE__, ret);
		uart_unregister_driver(&grlib_apbuart_driver);
		return ret;
	}

	return ret;
}

static void __exit grlib_apbuart_exit(void)
{
	int i;

	for (i = 0; i < grlib_apbuart_port_nr; i++)
		uart_remove_one_port(&grlib_apbuart_driver,
				     &grlib_apbuart_ports[i]);

	uart_unregister_driver(&grlib_apbuart_driver);
	of_unregister_platform_driver(&grlib_apbuart_of_driver);
}

module_init(grlib_apbuart_init);
module_exit(grlib_apbuart_exit);

MODULE_AUTHOR("Aeroflex Gaisler AB");
MODULE_DESCRIPTION("GRLIB APBUART serial driver");
MODULE_VERSION("2.1");
MODULE_LICENSE("GPL");
