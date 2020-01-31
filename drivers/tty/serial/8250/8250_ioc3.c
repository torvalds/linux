// SPDX-License-Identifier: GPL-2.0
/*
 * SGI IOC3 8250 UART driver
 *
 * Copyright (C) 2019 Thomas Bogendoerfer <tbogendoerfer@suse.de>
 *
 * based on code Copyright (C) 2005 Stanislaw Skowronek <skylark@unaligned.org>
 *               Copyright (C) 2014 Joshua Kinard <kumba@gentoo.org>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include "8250.h"

#define IOC3_UARTCLK (22000000 / 3)

struct ioc3_8250_data {
	int line;
};

static unsigned int ioc3_serial_in(struct uart_port *p, int offset)
{
	return readb(p->membase + (offset ^ 3));
}

static void ioc3_serial_out(struct uart_port *p, int offset, int value)
{
	writeb(value, p->membase + (offset ^ 3));
}

static int serial8250_ioc3_probe(struct platform_device *pdev)
{
	struct ioc3_8250_data *data;
	struct uart_8250_port up;
	struct resource *r;
	void __iomem *membase;
	int irq, line;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	membase = devm_ioremap_nocache(&pdev->dev, r->start, resource_size(r));
	if (!membase)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		irq = 0; /* no interrupt -> use polling */

	/* Register serial ports with 8250.c */
	memset(&up, 0, sizeof(struct uart_8250_port));
	up.port.iotype = UPIO_MEM;
	up.port.uartclk = IOC3_UARTCLK;
	up.port.type = PORT_16550A;
	up.port.irq = irq;
	up.port.flags = (UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ);
	up.port.dev = &pdev->dev;
	up.port.membase = membase;
	up.port.mapbase = r->start;
	up.port.serial_in = ioc3_serial_in;
	up.port.serial_out = ioc3_serial_out;
	line = serial8250_register_8250_port(&up);
	if (line < 0)
		return line;

	platform_set_drvdata(pdev, data);
	return 0;
}

static int serial8250_ioc3_remove(struct platform_device *pdev)
{
	struct ioc3_8250_data *data = platform_get_drvdata(pdev);

	serial8250_unregister_port(data->line);
	return 0;
}

static struct platform_driver serial8250_ioc3_driver = {
	.probe  = serial8250_ioc3_probe,
	.remove = serial8250_ioc3_remove,
	.driver = {
		.name = "ioc3-serial8250",
	}
};

module_platform_driver(serial8250_ioc3_driver);

MODULE_AUTHOR("Thomas Bogendoerfer <tbogendoerfer@suse.de>");
MODULE_DESCRIPTION("SGI IOC3 8250 UART driver");
MODULE_LICENSE("GPL");
