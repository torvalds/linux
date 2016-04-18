/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "8250.h"

/* Most (but not all) of UniPhier UART devices have 64-depth FIFO. */
#define UNIPHIER_UART_DEFAULT_FIFO_SIZE	64

#define UNIPHIER_UART_CHAR_FCR	3	/* Character / FIFO Control Register */
#define UNIPHIER_UART_LCR_MCR	4	/* Line/Modem Control Register */
#define   UNIPHIER_UART_LCR_SHIFT	8
#define UNIPHIER_UART_DLR	9	/* Divisor Latch Register */

struct uniphier8250_priv {
	int line;
	struct clk *clk;
	spinlock_t atomic_write_lock;
};

#if defined(CONFIG_SERIAL_8250_CONSOLE) && !defined(MODULE)
static int __init uniphier_early_console_setup(struct earlycon_device *device,
					       const char *options)
{
	if (!device->port.membase)
		return -ENODEV;

	/* This hardware always expects MMIO32 register interface. */
	device->port.iotype = UPIO_MEM32;
	device->port.regshift = 2;

	/*
	 * Do not touch the divisor register in early_serial8250_setup();
	 * we assume it has been initialized by a boot loader.
	 */
	device->baud = 0;

	return early_serial8250_setup(device, options);
}
OF_EARLYCON_DECLARE(uniphier, "socionext,uniphier-uart",
		    uniphier_early_console_setup);
#endif

/*
 * The register map is slightly different from that of 8250.
 * IO callbacks must be overridden for correct access to FCR, LCR, and MCR.
 */
static unsigned int uniphier_serial_in(struct uart_port *p, int offset)
{
	unsigned int valshift = 0;

	switch (offset) {
	case UART_LCR:
		valshift = UNIPHIER_UART_LCR_SHIFT;
		/* fall through */
	case UART_MCR:
		offset = UNIPHIER_UART_LCR_MCR;
		break;
	default:
		break;
	}

	offset <<= p->regshift;

	/*
	 * The return value must be masked with 0xff because LCR and MCR reside
	 * in the same register that must be accessed by 32-bit write/read.
	 * 8 or 16 bit access to this hardware result in unexpected behavior.
	 */
	return (readl(p->membase + offset) >> valshift) & 0xff;
}

static void uniphier_serial_out(struct uart_port *p, int offset, int value)
{
	unsigned int valshift = 0;
	bool normal = false;

	switch (offset) {
	case UART_FCR:
		offset = UNIPHIER_UART_CHAR_FCR;
		break;
	case UART_LCR:
		valshift = UNIPHIER_UART_LCR_SHIFT;
		/* Divisor latch access bit does not exist. */
		value &= ~(UART_LCR_DLAB << valshift);
		/* fall through */
	case UART_MCR:
		offset = UNIPHIER_UART_LCR_MCR;
		break;
	default:
		normal = true;
		break;
	}

	offset <<= p->regshift;

	if (normal) {
		writel(value, p->membase + offset);
	} else {
		/*
		 * Special case: two registers share the same address that
		 * must be 32-bit accessed.  As this is not longer atomic safe,
		 * take a lock just in case.
		 */
		struct uniphier8250_priv *priv = p->private_data;
		unsigned long flags;
		u32 tmp;

		spin_lock_irqsave(&priv->atomic_write_lock, flags);
		tmp = readl(p->membase + offset);
		tmp &= ~(0xff << valshift);
		tmp |= value << valshift;
		writel(tmp, p->membase + offset);
		spin_unlock_irqrestore(&priv->atomic_write_lock, flags);
	}
}

/*
 * This hardware does not have the divisor latch access bit.
 * The divisor latch register exists at different address.
 * Override dl_read/write callbacks.
 */
static int uniphier_serial_dl_read(struct uart_8250_port *up)
{
	int offset = UNIPHIER_UART_DLR << up->port.regshift;

	return readl(up->port.membase + offset);
}

static void uniphier_serial_dl_write(struct uart_8250_port *up, int value)
{
	int offset = UNIPHIER_UART_DLR << up->port.regshift;

	writel(value, up->port.membase + offset);
}

static int uniphier_of_serial_setup(struct device *dev, struct uart_port *port,
				    struct uniphier8250_priv *priv)
{
	int ret;
	u32 prop;
	struct device_node *np = dev->of_node;

	ret = of_alias_get_id(np, "serial");
	if (ret < 0) {
		dev_err(dev, "failed to get alias id\n");
		return ret;
	}
	port->line = priv->line = ret;

	/* Get clk rate through clk driver */
	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret < 0)
		return ret;

	port->uartclk = clk_get_rate(priv->clk);

	/* Check for fifo size */
	if (of_property_read_u32(np, "fifo-size", &prop) == 0)
		port->fifosize = prop;
	else
		port->fifosize = UNIPHIER_UART_DEFAULT_FIFO_SIZE;

	return 0;
}

static int uniphier_uart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uart_8250_port up;
	struct uniphier8250_priv *priv;
	struct resource *regs;
	void __iomem *membase;
	int irq;
	int ret;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(dev, "failed to get memory resource");
		return -EINVAL;
	}

	membase = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!membase)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get IRQ number");
		return irq;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	memset(&up, 0, sizeof(up));

	ret = uniphier_of_serial_setup(dev, &up.port, priv);
	if (ret < 0)
		return ret;

	spin_lock_init(&priv->atomic_write_lock);

	up.port.dev = dev;
	up.port.private_data = priv;
	up.port.mapbase = regs->start;
	up.port.mapsize = resource_size(regs);
	up.port.membase = membase;
	up.port.irq = irq;

	up.port.type = PORT_16550A;
	up.port.iotype = UPIO_MEM32;
	up.port.regshift = 2;
	up.port.flags = UPF_FIXED_PORT | UPF_FIXED_TYPE;
	up.capabilities = UART_CAP_FIFO;

	up.port.serial_in = uniphier_serial_in;
	up.port.serial_out = uniphier_serial_out;
	up.dl_read = uniphier_serial_dl_read;
	up.dl_write = uniphier_serial_dl_write;

	ret = serial8250_register_8250_port(&up);
	if (ret < 0) {
		dev_err(dev, "failed to register 8250 port\n");
		clk_disable_unprepare(priv->clk);
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int uniphier_uart_remove(struct platform_device *pdev)
{
	struct uniphier8250_priv *priv = platform_get_drvdata(pdev);

	serial8250_unregister_port(priv->line);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id uniphier_uart_match[] = {
	{ .compatible = "socionext,uniphier-uart" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_uart_match);

static struct platform_driver uniphier_uart_platform_driver = {
	.probe		= uniphier_uart_probe,
	.remove		= uniphier_uart_remove,
	.driver = {
		.name	= "uniphier-uart",
		.of_match_table = uniphier_uart_match,
	},
};
module_platform_driver(uniphier_uart_platform_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier UART driver");
MODULE_LICENSE("GPL");
