/*
 * uartlite.c: Serial driver for Xilinx uartlite serial controller
 *
 * Copyright (C) 2006 Peter Korsgaard <jacmet@sunsite.dk>
 * Copyright (C) 2007 Secret Lab Technologies Ltd.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/io.h>
#if defined(CONFIG_OF) && (defined(CONFIG_PPC32) || defined(CONFIG_MICROBLAZE))
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

/* Match table for of_platform binding */
static struct of_device_id ulite_of_match[] __devinitdata = {
	{ .compatible = "xlnx,opb-uartlite-1.00.b", },
	{ .compatible = "xlnx,xps-uartlite-1.00.a", },
	{}
};
MODULE_DEVICE_TABLE(of, ulite_of_match);

#endif

#define ULITE_NAME		"ttyUL"
#define ULITE_MAJOR		204
#define ULITE_MINOR		187
#define ULITE_NR_UARTS		4

/* ---------------------------------------------------------------------
 * Register definitions
 *
 * For register details see datasheet:
 * http://www.xilinx.com/bvdocs/ipcenter/data_sheet/opb_uartlite.pdf
 */

#define ULITE_RX		0x00
#define ULITE_TX		0x04
#define ULITE_STATUS		0x08
#define ULITE_CONTROL		0x0c

#define ULITE_REGION		16

#define ULITE_STATUS_RXVALID	0x01
#define ULITE_STATUS_RXFULL	0x02
#define ULITE_STATUS_TXEMPTY	0x04
#define ULITE_STATUS_TXFULL	0x08
#define ULITE_STATUS_IE		0x10
#define ULITE_STATUS_OVERRUN	0x20
#define ULITE_STATUS_FRAME	0x40
#define ULITE_STATUS_PARITY	0x80

#define ULITE_CONTROL_RST_TX	0x01
#define ULITE_CONTROL_RST_RX	0x02
#define ULITE_CONTROL_IE	0x10


static struct uart_port ulite_ports[ULITE_NR_UARTS];

/* ---------------------------------------------------------------------
 * Core UART driver operations
 */

static int ulite_receive(struct uart_port *port, int stat)
{
	struct tty_struct *tty = port->state->port.tty;
	unsigned char ch = 0;
	char flag = TTY_NORMAL;

	if ((stat & (ULITE_STATUS_RXVALID | ULITE_STATUS_OVERRUN
		     | ULITE_STATUS_FRAME)) == 0)
		return 0;

	/* stats */
	if (stat & ULITE_STATUS_RXVALID) {
		port->icount.rx++;
		ch = ioread32be(port->membase + ULITE_RX);

		if (stat & ULITE_STATUS_PARITY)
			port->icount.parity++;
	}

	if (stat & ULITE_STATUS_OVERRUN)
		port->icount.overrun++;

	if (stat & ULITE_STATUS_FRAME)
		port->icount.frame++;


	/* drop byte with parity error if IGNPAR specificed */
	if (stat & port->ignore_status_mask & ULITE_STATUS_PARITY)
		stat &= ~ULITE_STATUS_RXVALID;

	stat &= port->read_status_mask;

	if (stat & ULITE_STATUS_PARITY)
		flag = TTY_PARITY;


	stat &= ~port->ignore_status_mask;

	if (stat & ULITE_STATUS_RXVALID)
		tty_insert_flip_char(tty, ch, flag);

	if (stat & ULITE_STATUS_FRAME)
		tty_insert_flip_char(tty, 0, TTY_FRAME);

	if (stat & ULITE_STATUS_OVERRUN)
		tty_insert_flip_char(tty, 0, TTY_OVERRUN);

	return 1;
}

static int ulite_transmit(struct uart_port *port, int stat)
{
	struct circ_buf *xmit  = &port->state->xmit;

	if (stat & ULITE_STATUS_TXFULL)
		return 0;

	if (port->x_char) {
		iowrite32be(port->x_char, port->membase + ULITE_TX);
		port->x_char = 0;
		port->icount.tx++;
		return 1;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return 0;

	iowrite32be(xmit->buf[xmit->tail], port->membase + ULITE_TX);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE-1);
	port->icount.tx++;

	/* wake up */
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	return 1;
}

static irqreturn_t ulite_isr(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	int busy, n = 0;

	do {
		int stat = ioread32be(port->membase + ULITE_STATUS);
		busy  = ulite_receive(port, stat);
		busy |= ulite_transmit(port, stat);
		n++;
	} while (busy);

	/* work done? */
	if (n > 1) {
		tty_flip_buffer_push(port->state->port.tty);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static unsigned int ulite_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&port->lock, flags);
	ret = ioread32be(port->membase + ULITE_STATUS);
	spin_unlock_irqrestore(&port->lock, flags);

	return ret & ULITE_STATUS_TXEMPTY ? TIOCSER_TEMT : 0;
}

static unsigned int ulite_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void ulite_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* N/A */
}

static void ulite_stop_tx(struct uart_port *port)
{
	/* N/A */
}

static void ulite_start_tx(struct uart_port *port)
{
	ulite_transmit(port, ioread32be(port->membase + ULITE_STATUS));
}

static void ulite_stop_rx(struct uart_port *port)
{
	/* don't forward any more data (like !CREAD) */
	port->ignore_status_mask = ULITE_STATUS_RXVALID | ULITE_STATUS_PARITY
		| ULITE_STATUS_FRAME | ULITE_STATUS_OVERRUN;
}

static void ulite_enable_ms(struct uart_port *port)
{
	/* N/A */
}

static void ulite_break_ctl(struct uart_port *port, int ctl)
{
	/* N/A */
}

static int ulite_startup(struct uart_port *port)
{
	int ret;

	ret = request_irq(port->irq, ulite_isr,
			  IRQF_SHARED | IRQF_SAMPLE_RANDOM, "uartlite", port);
	if (ret)
		return ret;

	iowrite32be(ULITE_CONTROL_RST_RX | ULITE_CONTROL_RST_TX,
	       port->membase + ULITE_CONTROL);
	iowrite32be(ULITE_CONTROL_IE, port->membase + ULITE_CONTROL);

	return 0;
}

static void ulite_shutdown(struct uart_port *port)
{
	iowrite32be(0, port->membase + ULITE_CONTROL);
	ioread32be(port->membase + ULITE_CONTROL); /* dummy */
	free_irq(port->irq, port);
}

static void ulite_set_termios(struct uart_port *port, struct ktermios *termios,
			      struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud;

	spin_lock_irqsave(&port->lock, flags);

	port->read_status_mask = ULITE_STATUS_RXVALID | ULITE_STATUS_OVERRUN
		| ULITE_STATUS_TXFULL;

	if (termios->c_iflag & INPCK)
		port->read_status_mask |=
			ULITE_STATUS_PARITY | ULITE_STATUS_FRAME;

	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= ULITE_STATUS_PARITY
			| ULITE_STATUS_FRAME | ULITE_STATUS_OVERRUN;

	/* ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |=
			ULITE_STATUS_RXVALID | ULITE_STATUS_PARITY
			| ULITE_STATUS_FRAME | ULITE_STATUS_OVERRUN;

	/* update timeout */
	baud = uart_get_baud_rate(port, termios, old, 0, 460800);
	uart_update_timeout(port, termios->c_cflag, baud);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *ulite_type(struct uart_port *port)
{
	return port->type == PORT_UARTLITE ? "uartlite" : NULL;
}

static void ulite_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, ULITE_REGION);
	iounmap(port->membase);
	port->membase = NULL;
}

static int ulite_request_port(struct uart_port *port)
{
	pr_debug("ulite console: port=%p; port->mapbase=%llx\n",
		 port, (unsigned long long) port->mapbase);

	if (!request_mem_region(port->mapbase, ULITE_REGION, "uartlite")) {
		dev_err(port->dev, "Memory region busy\n");
		return -EBUSY;
	}

	port->membase = ioremap(port->mapbase, ULITE_REGION);
	if (!port->membase) {
		dev_err(port->dev, "Unable to map registers\n");
		release_mem_region(port->mapbase, ULITE_REGION);
		return -EBUSY;
	}

	return 0;
}

static void ulite_config_port(struct uart_port *port, int flags)
{
	if (!ulite_request_port(port))
		port->type = PORT_UARTLITE;
}

static int ulite_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	return -EINVAL;
}

static struct uart_ops ulite_ops = {
	.tx_empty	= ulite_tx_empty,
	.set_mctrl	= ulite_set_mctrl,
	.get_mctrl	= ulite_get_mctrl,
	.stop_tx	= ulite_stop_tx,
	.start_tx	= ulite_start_tx,
	.stop_rx	= ulite_stop_rx,
	.enable_ms	= ulite_enable_ms,
	.break_ctl	= ulite_break_ctl,
	.startup	= ulite_startup,
	.shutdown	= ulite_shutdown,
	.set_termios	= ulite_set_termios,
	.type		= ulite_type,
	.release_port	= ulite_release_port,
	.request_port	= ulite_request_port,
	.config_port	= ulite_config_port,
	.verify_port	= ulite_verify_port
};

/* ---------------------------------------------------------------------
 * Console driver operations
 */

#ifdef CONFIG_SERIAL_UARTLITE_CONSOLE
static void ulite_console_wait_tx(struct uart_port *port)
{
	int i;
	u8 val;

	/* Spin waiting for TX fifo to have space available */
	for (i = 0; i < 100000; i++) {
		val = ioread32be(port->membase + ULITE_STATUS);
		if ((val & ULITE_STATUS_TXFULL) == 0)
			break;
		cpu_relax();
	}
}

static void ulite_console_putchar(struct uart_port *port, int ch)
{
	ulite_console_wait_tx(port);
	iowrite32be(ch, port->membase + ULITE_TX);
}

static void ulite_console_write(struct console *co, const char *s,
				unsigned int count)
{
	struct uart_port *port = &ulite_ports[co->index];
	unsigned long flags;
	unsigned int ier;
	int locked = 1;

	if (oops_in_progress) {
		locked = spin_trylock_irqsave(&port->lock, flags);
	} else
		spin_lock_irqsave(&port->lock, flags);

	/* save and disable interrupt */
	ier = ioread32be(port->membase + ULITE_STATUS) & ULITE_STATUS_IE;
	iowrite32be(0, port->membase + ULITE_CONTROL);

	uart_console_write(port, s, count, ulite_console_putchar);

	ulite_console_wait_tx(port);

	/* restore interrupt state */
	if (ier)
		iowrite32be(ULITE_CONTROL_IE, port->membase + ULITE_CONTROL);

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static int __devinit ulite_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= ULITE_NR_UARTS)
		return -EINVAL;

	port = &ulite_ports[co->index];

	/* Has the device been initialized yet? */
	if (!port->mapbase) {
		pr_debug("console on ttyUL%i not present\n", co->index);
		return -ENODEV;
	}

	/* not initialized yet? */
	if (!port->membase) {
		if (ulite_request_port(port))
			return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver ulite_uart_driver;

static struct console ulite_console = {
	.name	= ULITE_NAME,
	.write	= ulite_console_write,
	.device	= uart_console_device,
	.setup	= ulite_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1, /* Specified on the cmdline (e.g. console=ttyUL0 ) */
	.data	= &ulite_uart_driver,
};

static int __init ulite_console_init(void)
{
	register_console(&ulite_console);
	return 0;
}

console_initcall(ulite_console_init);

#endif /* CONFIG_SERIAL_UARTLITE_CONSOLE */

static struct uart_driver ulite_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "uartlite",
	.dev_name	= ULITE_NAME,
	.major		= ULITE_MAJOR,
	.minor		= ULITE_MINOR,
	.nr		= ULITE_NR_UARTS,
#ifdef CONFIG_SERIAL_UARTLITE_CONSOLE
	.cons		= &ulite_console,
#endif
};

/* ---------------------------------------------------------------------
 * Port assignment functions (mapping devices to uart_port structures)
 */

/** ulite_assign: register a uartlite device with the driver
 *
 * @dev: pointer to device structure
 * @id: requested id number.  Pass -1 for automatic port assignment
 * @base: base address of uartlite registers
 * @irq: irq number for uartlite
 *
 * Returns: 0 on success, <0 otherwise
 */
static int __devinit ulite_assign(struct device *dev, int id, u32 base, int irq)
{
	struct uart_port *port;
	int rc;

	/* if id = -1; then scan for a free id and use that */
	if (id < 0) {
		for (id = 0; id < ULITE_NR_UARTS; id++)
			if (ulite_ports[id].mapbase == 0)
				break;
	}
	if (id < 0 || id >= ULITE_NR_UARTS) {
		dev_err(dev, "%s%i too large\n", ULITE_NAME, id);
		return -EINVAL;
	}

	if ((ulite_ports[id].mapbase) && (ulite_ports[id].mapbase != base)) {
		dev_err(dev, "cannot assign to %s%i; it is already in use\n",
			ULITE_NAME, id);
		return -EBUSY;
	}

	port = &ulite_ports[id];

	spin_lock_init(&port->lock);
	port->fifosize = 16;
	port->regshift = 2;
	port->iotype = UPIO_MEM;
	port->iobase = 1; /* mark port in use */
	port->mapbase = base;
	port->membase = NULL;
	port->ops = &ulite_ops;
	port->irq = irq;
	port->flags = UPF_BOOT_AUTOCONF;
	port->dev = dev;
	port->type = PORT_UNKNOWN;
	port->line = id;

	dev_set_drvdata(dev, port);

	/* Register the port */
	rc = uart_add_one_port(&ulite_uart_driver, port);
	if (rc) {
		dev_err(dev, "uart_add_one_port() failed; err=%i\n", rc);
		port->mapbase = 0;
		dev_set_drvdata(dev, NULL);
		return rc;
	}

	return 0;
}

/** ulite_release: register a uartlite device with the driver
 *
 * @dev: pointer to device structure
 */
static int __devexit ulite_release(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);
	int rc = 0;

	if (port) {
		rc = uart_remove_one_port(&ulite_uart_driver, port);
		dev_set_drvdata(dev, NULL);
		port->mapbase = 0;
	}

	return rc;
}

/* ---------------------------------------------------------------------
 * Platform bus binding
 */

static int __devinit ulite_probe(struct platform_device *pdev)
{
	struct resource *res, *res2;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	res2 = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res2)
		return -ENODEV;

	return ulite_assign(&pdev->dev, pdev->id, res->start, res2->start);
}

static int __devexit ulite_remove(struct platform_device *pdev)
{
	return ulite_release(&pdev->dev);
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:uartlite");

static struct platform_driver ulite_platform_driver = {
	.probe	= ulite_probe,
	.remove	= __devexit_p(ulite_remove),
	.driver	= {
		   .owner = THIS_MODULE,
		   .name  = "uartlite",
		   },
};

/* ---------------------------------------------------------------------
 * OF bus bindings
 */
#if defined(CONFIG_OF) && (defined(CONFIG_PPC32) || defined(CONFIG_MICROBLAZE))
static int __devinit
ulite_of_probe(struct of_device *op, const struct of_device_id *match)
{
	struct resource res;
	const unsigned int *id;
	int irq, rc;

	dev_dbg(&op->dev, "%s(%p, %p)\n", __func__, op, match);

	rc = of_address_to_resource(op->dev.of_node, 0, &res);
	if (rc) {
		dev_err(&op->dev, "invalid address\n");
		return rc;
	}

	irq = irq_of_parse_and_map(op->dev.of_node, 0);

	id = of_get_property(op->dev.of_node, "port-number", NULL);

	return ulite_assign(&op->dev, id ? *id : -1, res.start, irq);
}

static int __devexit ulite_of_remove(struct of_device *op)
{
	return ulite_release(&op->dev);
}

static struct of_platform_driver ulite_of_driver = {
	.probe = ulite_of_probe,
	.remove = __devexit_p(ulite_of_remove),
	.driver = {
		.name = "uartlite",
		.owner = THIS_MODULE,
		.of_match_table = ulite_of_match,
	},
};

/* Registration helpers to keep the number of #ifdefs to a minimum */
static inline int __init ulite_of_register(void)
{
	pr_debug("uartlite: calling of_register_platform_driver()\n");
	return of_register_platform_driver(&ulite_of_driver);
}

static inline void __exit ulite_of_unregister(void)
{
	of_unregister_platform_driver(&ulite_of_driver);
}
#else /* CONFIG_OF && (CONFIG_PPC32 || CONFIG_MICROBLAZE) */
/* Appropriate config not enabled; do nothing helpers */
static inline int __init ulite_of_register(void) { return 0; }
static inline void __exit ulite_of_unregister(void) { }
#endif /* CONFIG_OF && (CONFIG_PPC32 || CONFIG_MICROBLAZE) */

/* ---------------------------------------------------------------------
 * Module setup/teardown
 */

int __init ulite_init(void)
{
	int ret;

	pr_debug("uartlite: calling uart_register_driver()\n");
	ret = uart_register_driver(&ulite_uart_driver);
	if (ret)
		goto err_uart;

	ret = ulite_of_register();
	if (ret)
		goto err_of;

	pr_debug("uartlite: calling platform_driver_register()\n");
	ret = platform_driver_register(&ulite_platform_driver);
	if (ret)
		goto err_plat;

	return 0;

err_plat:
	ulite_of_unregister();
err_of:
	uart_unregister_driver(&ulite_uart_driver);
err_uart:
	printk(KERN_ERR "registering uartlite driver failed: err=%i", ret);
	return ret;
}

void __exit ulite_exit(void)
{
	platform_driver_unregister(&ulite_platform_driver);
	ulite_of_unregister();
	uart_unregister_driver(&ulite_uart_driver);
}

module_init(ulite_init);
module_exit(ulite_exit);

MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("Xilinx uartlite serial driver");
MODULE_LICENSE("GPL");
