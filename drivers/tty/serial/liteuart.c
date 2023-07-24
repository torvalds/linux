// SPDX-License-Identifier: GPL-2.0
/*
 * LiteUART serial controller (LiteX) Driver
 *
 * Copyright (C) 2019-2020 Antmicro <www.antmicro.com>
 */

#include <linux/bits.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/litex.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/tty_flip.h>
#include <linux/xarray.h>

/*
 * CSRs definitions (base address offsets + width)
 *
 * The definitions below are true for LiteX SoC configured for 8-bit CSR Bus,
 * 32-bit aligned.
 *
 * Supporting other configurations might require new definitions or a more
 * generic way of indexing the LiteX CSRs.
 *
 * For more details on how CSRs are defined and handled in LiteX, see comments
 * in the LiteX SoC Driver: drivers/soc/litex/litex_soc_ctrl.c
 */
#define OFF_RXTX	0x00
#define OFF_TXFULL	0x04
#define OFF_RXEMPTY	0x08
#define OFF_EV_STATUS	0x0c
#define OFF_EV_PENDING	0x10
#define OFF_EV_ENABLE	0x14

/* events */
#define EV_TX		BIT(0)
#define EV_RX		BIT(1)

struct liteuart_port {
	struct uart_port port;
	struct timer_list timer;
	u8 irq_reg;
};

#define to_liteuart_port(port)	container_of(port, struct liteuart_port, port)

static DEFINE_XARRAY_FLAGS(liteuart_array, XA_FLAGS_ALLOC);

#ifdef CONFIG_SERIAL_LITEUART_CONSOLE
static struct console liteuart_console;
#endif

static struct uart_driver liteuart_driver = {
	.owner = THIS_MODULE,
	.driver_name = KBUILD_MODNAME,
	.dev_name = "ttyLXU",
	.major = 0,
	.minor = 0,
	.nr = CONFIG_SERIAL_LITEUART_MAX_PORTS,
#ifdef CONFIG_SERIAL_LITEUART_CONSOLE
	.cons = &liteuart_console,
#endif
};

static void liteuart_update_irq_reg(struct uart_port *port, bool set, u8 mask)
{
	struct liteuart_port *uart = to_liteuart_port(port);

	if (set)
		uart->irq_reg |= mask;
	else
		uart->irq_reg &= ~mask;

	if (port->irq)
		litex_write8(port->membase + OFF_EV_ENABLE, uart->irq_reg);
}

static void liteuart_stop_tx(struct uart_port *port)
{
	liteuart_update_irq_reg(port, false, EV_TX);
}

static void liteuart_start_tx(struct uart_port *port)
{
	liteuart_update_irq_reg(port, true, EV_TX);
}

static void liteuart_stop_rx(struct uart_port *port)
{
	struct liteuart_port *uart = to_liteuart_port(port);

	/* just delete timer */
	del_timer(&uart->timer);
}

static void liteuart_rx_chars(struct uart_port *port)
{
	unsigned char __iomem *membase = port->membase;
	u8 ch;

	while (!litex_read8(membase + OFF_RXEMPTY)) {
		ch = litex_read8(membase + OFF_RXTX);
		port->icount.rx++;

		/* necessary for RXEMPTY to refresh its value */
		litex_write8(membase + OFF_EV_PENDING, EV_RX);

		/* no overflow bits in status */
		if (!(uart_handle_sysrq_char(port, ch)))
			uart_insert_char(port, 1, 0, ch, TTY_NORMAL);
	}

	tty_flip_buffer_push(&port->state->port);
}

static void liteuart_tx_chars(struct uart_port *port)
{
	u8 ch;

	uart_port_tx(port, ch,
		!litex_read8(port->membase + OFF_TXFULL),
		litex_write8(port->membase + OFF_RXTX, ch));
}

static irqreturn_t liteuart_interrupt(int irq, void *data)
{
	struct liteuart_port *uart = data;
	struct uart_port *port = &uart->port;
	unsigned long flags;
	u8 isr;

	/*
	 * if polling, the context would be "in_serving_softirq", so use
	 * irq[save|restore] spin_lock variants to cover all possibilities
	 */
	spin_lock_irqsave(&port->lock, flags);
	isr = litex_read8(port->membase + OFF_EV_PENDING) & uart->irq_reg;
	if (isr & EV_RX)
		liteuart_rx_chars(port);
	if (isr & EV_TX)
		liteuart_tx_chars(port);
	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_RETVAL(isr);
}

static void liteuart_timer(struct timer_list *t)
{
	struct liteuart_port *uart = from_timer(uart, t, timer);
	struct uart_port *port = &uart->port;

	liteuart_interrupt(0, port);
	mod_timer(&uart->timer, jiffies + uart_poll_timeout(port));
}

static unsigned int liteuart_tx_empty(struct uart_port *port)
{
	/* not really tx empty, just checking if tx is not full */
	if (!litex_read8(port->membase + OFF_TXFULL))
		return TIOCSER_TEMT;

	return 0;
}

static void liteuart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* modem control register is not present in LiteUART */
}

static unsigned int liteuart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static int liteuart_startup(struct uart_port *port)
{
	struct liteuart_port *uart = to_liteuart_port(port);
	unsigned long flags;
	int ret;

	if (port->irq) {
		ret = request_irq(port->irq, liteuart_interrupt, 0,
				  KBUILD_MODNAME, uart);
		if (ret) {
			dev_warn(port->dev,
				"line %d irq %d failed: switch to polling\n",
				port->line, port->irq);
			port->irq = 0;
		}
	}

	spin_lock_irqsave(&port->lock, flags);
	/* only enabling rx irqs during startup */
	liteuart_update_irq_reg(port, true, EV_RX);
	spin_unlock_irqrestore(&port->lock, flags);

	if (!port->irq) {
		timer_setup(&uart->timer, liteuart_timer, 0);
		mod_timer(&uart->timer, jiffies + uart_poll_timeout(port));
	}

	return 0;
}

static void liteuart_shutdown(struct uart_port *port)
{
	struct liteuart_port *uart = to_liteuart_port(port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	liteuart_update_irq_reg(port, false, EV_RX | EV_TX);
	spin_unlock_irqrestore(&port->lock, flags);

	if (port->irq)
		free_irq(port->irq, port);
	else
		del_timer_sync(&uart->timer);
}

static void liteuart_set_termios(struct uart_port *port, struct ktermios *new,
				 const struct ktermios *old)
{
	unsigned int baud;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* update baudrate */
	baud = uart_get_baud_rate(port, new, old, 0, 460800);
	uart_update_timeout(port, new->c_cflag, baud);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *liteuart_type(struct uart_port *port)
{
	return "liteuart";
}

static void liteuart_config_port(struct uart_port *port, int flags)
{
	/*
	 * Driver core for serial ports forces a non-zero value for port type.
	 * Write an arbitrary value here to accommodate the serial core driver,
	 * as ID part of UAPI is redundant.
	 */
	port->type = 1;
}

static int liteuart_verify_port(struct uart_port *port,
				struct serial_struct *ser)
{
	if (port->type != PORT_UNKNOWN && ser->type != 1)
		return -EINVAL;

	return 0;
}

static const struct uart_ops liteuart_ops = {
	.tx_empty	= liteuart_tx_empty,
	.set_mctrl	= liteuart_set_mctrl,
	.get_mctrl	= liteuart_get_mctrl,
	.stop_tx	= liteuart_stop_tx,
	.start_tx	= liteuart_start_tx,
	.stop_rx	= liteuart_stop_rx,
	.startup	= liteuart_startup,
	.shutdown	= liteuart_shutdown,
	.set_termios	= liteuart_set_termios,
	.type		= liteuart_type,
	.config_port	= liteuart_config_port,
	.verify_port	= liteuart_verify_port,
};

static int liteuart_probe(struct platform_device *pdev)
{
	struct liteuart_port *uart;
	struct uart_port *port;
	struct xa_limit limit;
	int dev_id, ret;

	uart = devm_kzalloc(&pdev->dev, sizeof(struct liteuart_port), GFP_KERNEL);
	if (!uart)
		return -ENOMEM;

	port = &uart->port;

	/* get membase */
	port->membase = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);

	ret = platform_get_irq_optional(pdev, 0);
	if (ret < 0 && ret != -ENXIO)
		return ret;
	if (ret > 0)
		port->irq = ret;

	/* look for aliases; auto-enumerate for free index if not found */
	dev_id = of_alias_get_id(pdev->dev.of_node, "serial");
	if (dev_id < 0)
		limit = XA_LIMIT(0, CONFIG_SERIAL_LITEUART_MAX_PORTS);
	else
		limit = XA_LIMIT(dev_id, dev_id);

	ret = xa_alloc(&liteuart_array, &dev_id, uart, limit, GFP_KERNEL);
	if (ret)
		return ret;

	/* values not from device tree */
	port->dev = &pdev->dev;
	port->iotype = UPIO_MEM;
	port->flags = UPF_BOOT_AUTOCONF;
	port->ops = &liteuart_ops;
	port->fifosize = 16;
	port->type = PORT_UNKNOWN;
	port->line = dev_id;
	spin_lock_init(&port->lock);

	platform_set_drvdata(pdev, port);

	ret = uart_add_one_port(&liteuart_driver, &uart->port);
	if (ret)
		goto err_erase_id;

	return 0;

err_erase_id:
	xa_erase(&liteuart_array, dev_id);

	return ret;
}

static int liteuart_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	unsigned int line = port->line;

	uart_remove_one_port(&liteuart_driver, port);
	xa_erase(&liteuart_array, line);

	return 0;
}

static const struct of_device_id liteuart_of_match[] = {
	{ .compatible = "litex,liteuart" },
	{}
};
MODULE_DEVICE_TABLE(of, liteuart_of_match);

static struct platform_driver liteuart_platform_driver = {
	.probe = liteuart_probe,
	.remove = liteuart_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = liteuart_of_match,
	},
};

#ifdef CONFIG_SERIAL_LITEUART_CONSOLE

static void liteuart_putchar(struct uart_port *port, unsigned char ch)
{
	while (litex_read8(port->membase + OFF_TXFULL))
		cpu_relax();

	litex_write8(port->membase + OFF_RXTX, ch);
}

static void liteuart_console_write(struct console *co, const char *s,
	unsigned int count)
{
	struct liteuart_port *uart;
	struct uart_port *port;
	unsigned long flags;

	uart = (struct liteuart_port *)xa_load(&liteuart_array, co->index);
	port = &uart->port;

	spin_lock_irqsave(&port->lock, flags);
	uart_console_write(port, s, count, liteuart_putchar);
	spin_unlock_irqrestore(&port->lock, flags);
}

static int liteuart_console_setup(struct console *co, char *options)
{
	struct liteuart_port *uart;
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	uart = (struct liteuart_port *)xa_load(&liteuart_array, co->index);
	if (!uart)
		return -ENODEV;

	port = &uart->port;
	if (!port->membase)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console liteuart_console = {
	.name = KBUILD_MODNAME,
	.write = liteuart_console_write,
	.device = uart_console_device,
	.setup = liteuart_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &liteuart_driver,
};

static int __init liteuart_console_init(void)
{
	register_console(&liteuart_console);

	return 0;
}
console_initcall(liteuart_console_init);

static void early_liteuart_write(struct console *console, const char *s,
				    unsigned int count)
{
	struct earlycon_device *device = console->data;
	struct uart_port *port = &device->port;

	uart_console_write(port, s, count, liteuart_putchar);
}

static int __init early_liteuart_setup(struct earlycon_device *device,
				       const char *options)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = early_liteuart_write;
	return 0;
}

OF_EARLYCON_DECLARE(liteuart, "litex,liteuart", early_liteuart_setup);
#endif /* CONFIG_SERIAL_LITEUART_CONSOLE */

static int __init liteuart_init(void)
{
	int res;

	res = uart_register_driver(&liteuart_driver);
	if (res)
		return res;

	res = platform_driver_register(&liteuart_platform_driver);
	if (res)
		uart_unregister_driver(&liteuart_driver);

	return res;
}

static void __exit liteuart_exit(void)
{
	platform_driver_unregister(&liteuart_platform_driver);
	uart_unregister_driver(&liteuart_driver);
}

module_init(liteuart_init);
module_exit(liteuart_exit);

MODULE_AUTHOR("Antmicro <www.antmicro.com>");
MODULE_DESCRIPTION("LiteUART serial driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:liteuart");
