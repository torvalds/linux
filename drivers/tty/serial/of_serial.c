/*
 *  Serial Port driver for Open Firmware platform devices
 *
 *    Copyright (C) 2006 Arnd Bergmann <arnd@arndb.de>, IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_serial.h>
#include <linux/of_platform.h>
#include <linux/nwpserial.h>
#include <linux/clk.h>

struct of_serial_info {
	struct clk *clk;
	int type;
	int line;
};

#ifdef CONFIG_ARCH_TEGRA
void tegra_serial_handle_break(struct uart_port *p)
{
	unsigned int status, tmout = 10000;

	do {
		status = p->serial_in(p, UART_LSR);
		if (status & (UART_LSR_FIFOE | UART_LSR_BRK_ERROR_BITS))
			status = p->serial_in(p, UART_RX);
		else
			break;
		if (--tmout == 0)
			break;
		udelay(1);
	} while (1);
}
/* FIXME remove this export when tegra finishes conversion to open firmware */
EXPORT_SYMBOL_GPL(tegra_serial_handle_break);
#endif

/*
 * Fill a struct uart_port for a given device node
 */
static int of_platform_serial_setup(struct platform_device *ofdev,
			int type, struct uart_port *port,
			struct of_serial_info *info)
{
	struct resource resource;
	struct device_node *np = ofdev->dev.of_node;
	u32 clk, spd, prop;
	int ret;

	memset(port, 0, sizeof *port);
	if (of_property_read_u32(np, "clock-frequency", &clk)) {

		/* Get clk rate through clk driver if present */
		info->clk = clk_get(&ofdev->dev, NULL);
		if (IS_ERR(info->clk)) {
			dev_warn(&ofdev->dev,
				"clk or clock-frequency not defined\n");
			return PTR_ERR(info->clk);
		}

		clk_prepare_enable(info->clk);
		clk = clk_get_rate(info->clk);
	}
	/* If current-speed was set, then try not to change it. */
	if (of_property_read_u32(np, "current-speed", &spd) == 0)
		port->custom_divisor = clk / (16 * spd);

	ret = of_address_to_resource(np, 0, &resource);
	if (ret) {
		dev_warn(&ofdev->dev, "invalid address\n");
		goto out;
	}

	spin_lock_init(&port->lock);
	port->mapbase = resource.start;

	/* Check for shifted address mapping */
	if (of_property_read_u32(np, "reg-offset", &prop) == 0)
		port->mapbase += prop;

	/* Check for registers offset within the devices address range */
	if (of_property_read_u32(np, "reg-shift", &prop) == 0)
		port->regshift = prop;

	port->irq = irq_of_parse_and_map(np, 0);
	port->iotype = UPIO_MEM;
	if (of_property_read_u32(np, "reg-io-width", &prop) == 0) {
		switch (prop) {
		case 1:
			port->iotype = UPIO_MEM;
			break;
		case 4:
			port->iotype = UPIO_MEM32;
			break;
		default:
			dev_warn(&ofdev->dev, "unsupported reg-io-width (%d)\n",
				 prop);
			ret = -EINVAL;
			goto out;
		}
	}

	port->type = type;
	port->uartclk = clk;
	port->flags = UPF_SHARE_IRQ | UPF_BOOT_AUTOCONF | UPF_IOREMAP
		| UPF_FIXED_PORT | UPF_FIXED_TYPE;

	if (of_find_property(np, "no-loopback-test", NULL))
		port->flags |= UPF_SKIP_TEST;

	port->dev = &ofdev->dev;

	if (type == PORT_TEGRA)
		port->handle_break = tegra_serial_handle_break;

	return 0;
out:
	if (info->clk)
		clk_disable_unprepare(info->clk);
	return ret;
}

/*
 * Try to register a serial port
 */
static struct of_device_id of_platform_serial_table[];
static int of_platform_serial_probe(struct platform_device *ofdev)
{
	const struct of_device_id *match;
	struct of_serial_info *info;
	struct uart_port port;
	int port_type;
	int ret;

	match = of_match_device(of_platform_serial_table, &ofdev->dev);
	if (!match)
		return -EINVAL;

	if (of_find_property(ofdev->dev.of_node, "used-by-rtas", NULL))
		return -EBUSY;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	port_type = (unsigned long)match->data;
	ret = of_platform_serial_setup(ofdev, port_type, &port, info);
	if (ret)
		goto out;

	switch (port_type) {
#ifdef CONFIG_SERIAL_8250
	case PORT_8250 ... PORT_MAX_8250:
	{
		/* For now the of bindings don't support the extra
		   8250 specific bits */
		struct uart_8250_port port8250;
		memset(&port8250, 0, sizeof(port8250));
		port8250.port = port;
		ret = serial8250_register_8250_port(&port8250);
		break;
	}
#endif
#ifdef CONFIG_SERIAL_OF_PLATFORM_NWPSERIAL
	case PORT_NWPSERIAL:
		ret = nwpserial_register_port(&port);
		break;
#endif
	default:
		/* need to add code for these */
	case PORT_UNKNOWN:
		dev_info(&ofdev->dev, "Unknown serial port found, ignored\n");
		ret = -ENODEV;
		break;
	}
	if (ret < 0)
		goto out;

	info->type = port_type;
	info->line = ret;
	dev_set_drvdata(&ofdev->dev, info);
	return 0;
out:
	kfree(info);
	irq_dispose_mapping(port.irq);
	return ret;
}

/*
 * Release a line
 */
static int of_platform_serial_remove(struct platform_device *ofdev)
{
	struct of_serial_info *info = dev_get_drvdata(&ofdev->dev);
	switch (info->type) {
#ifdef CONFIG_SERIAL_8250
	case PORT_8250 ... PORT_MAX_8250:
		serial8250_unregister_port(info->line);
		break;
#endif
#ifdef CONFIG_SERIAL_OF_PLATFORM_NWPSERIAL
	case PORT_NWPSERIAL:
		nwpserial_unregister_port(info->line);
		break;
#endif
	default:
		/* need to add code for these */
		break;
	}

	if (info->clk)
		clk_disable_unprepare(info->clk);
	kfree(info);
	return 0;
}

/*
 * A few common types, add more as needed.
 */
static struct of_device_id of_platform_serial_table[] = {
	{ .compatible = "ns8250",   .data = (void *)PORT_8250, },
	{ .compatible = "ns16450",  .data = (void *)PORT_16450, },
	{ .compatible = "ns16550a", .data = (void *)PORT_16550A, },
	{ .compatible = "ns16550",  .data = (void *)PORT_16550, },
	{ .compatible = "ns16750",  .data = (void *)PORT_16750, },
	{ .compatible = "ns16850",  .data = (void *)PORT_16850, },
	{ .compatible = "nvidia,tegra20-uart", .data = (void *)PORT_TEGRA, },
	{ .compatible = "nxp,lpc3220-uart", .data = (void *)PORT_LPC3220, },
#ifdef CONFIG_SERIAL_OF_PLATFORM_NWPSERIAL
	{ .compatible = "ibm,qpace-nwp-serial",
		.data = (void *)PORT_NWPSERIAL, },
#endif
	{ .type = "serial",         .data = (void *)PORT_UNKNOWN, },
	{ /* end of list */ },
};

static struct platform_driver of_platform_serial_driver = {
	.driver = {
		.name = "of_serial",
		.owner = THIS_MODULE,
		.of_match_table = of_platform_serial_table,
	},
	.probe = of_platform_serial_probe,
	.remove = of_platform_serial_remove,
};

module_platform_driver(of_platform_serial_driver);

MODULE_AUTHOR("Arnd Bergmann <arnd@arndb.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Serial Port driver for Open Firmware platform devices");
