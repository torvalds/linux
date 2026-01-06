// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Serial Port driver for Loongson family chips
 *
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/property.h>
#include <linux/math.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>
#include <linux/reset.h>

#include "8250.h"

/* Divisor Latch Fraction Register */
#define LOONGSON_UART_DLF		0x2

#define LOONGSON_QUOT_FRAC_MASK		GENMASK(7, 0)
#define LOONGSON_QUOT_DIV_MASK		GENMASK(15, 8)

struct loongson_uart_ddata {
	bool has_frac;
	u8 mcr_invert;
	u8 msr_invert;
};

static const struct loongson_uart_ddata ls2k0500_uart_data = {
	.has_frac = false,
	.mcr_invert = UART_MCR_RTS | UART_MCR_DTR,
	.msr_invert = UART_MSR_CTS | UART_MSR_DSR,
};

static const struct loongson_uart_ddata ls2k1500_uart_data = {
	.has_frac = true,
	.mcr_invert = UART_MCR_RTS | UART_MCR_DTR,
	.msr_invert = 0,
};

struct loongson_uart_priv {
	int line;
	struct clk *clk;
	struct resource *res;
	struct reset_control *rst;
	const struct loongson_uart_ddata *ddata;
};

static u8 serial_fixup(struct uart_port *p, unsigned int offset, u8 val)
{
	struct loongson_uart_priv *priv = p->private_data;

	switch (offset) {
	case UART_MCR:
		return val ^ priv->ddata->mcr_invert;
	case UART_MSR:
		return val ^ priv->ddata->msr_invert;
	default:
		return val;
	}
}

static u32 loongson_serial_in(struct uart_port *p, unsigned int offset)
{
	u8 val;

	val = readb(p->membase + (offset << p->regshift));

	return serial_fixup(p, offset, val);
}

static void loongson_serial_out(struct uart_port *p, unsigned int offset, unsigned int value)
{
	u8 val;

	offset <<= p->regshift;
	val = serial_fixup(p, offset, value);
	writeb(val, p->membase + offset);
}

static unsigned int loongson_frac_get_divisor(struct uart_port *port, unsigned int baud,
					      unsigned int *frac)
{
	unsigned int quot;

	quot = DIV_ROUND_CLOSEST((port->uartclk << 4), baud);
	*frac = FIELD_GET(LOONGSON_QUOT_FRAC_MASK, quot);

	return FIELD_GET(LOONGSON_QUOT_DIV_MASK, quot);
}

static void loongson_frac_set_divisor(struct uart_port *port, unsigned int baud,
				      unsigned int quot, unsigned int quot_frac)
{
	struct uart_8250_port *up = up_to_u8250p(port);

	serial_port_out(port, UART_LCR, up->lcr | UART_LCR_DLAB);
	serial_dl_write(up, quot);
	serial_port_out(port, LOONGSON_UART_DLF, quot_frac);
}

static int loongson_uart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uart_8250_port uart = {};
	struct loongson_uart_priv *priv;
	struct uart_port *port;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ddata = device_get_match_data(dev);

	port = &uart.port;
	spin_lock_init(&port->lock);
	port->flags = UPF_SHARE_IRQ | UPF_FIXED_PORT | UPF_FIXED_TYPE | UPF_IOREMAP;
	port->iotype = UPIO_MEM;
	port->regshift = 0;
	port->dev = dev;
	port->type = PORT_16550A;
	port->private_data = priv;

	port->membase = devm_platform_get_and_ioremap_resource(pdev, 0, &priv->res);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);

	port->mapbase = priv->res->start;
	port->mapsize = resource_size(priv->res);
	port->serial_in = loongson_serial_in;
	port->serial_out = loongson_serial_out;

	if (priv->ddata->has_frac) {
		port->get_divisor = loongson_frac_get_divisor;
		port->set_divisor = loongson_frac_set_divisor;
	}

	ret = uart_read_port_properties(port);
	if (ret)
		return ret;

	if (!port->uartclk) {
		priv->clk = devm_clk_get_enabled(dev, NULL);
		if (IS_ERR(priv->clk))
			return dev_err_probe(dev, PTR_ERR(priv->clk),
					     "Unable to determine clock frequency!\n");
		port->uartclk = clk_get_rate(priv->clk);
	}

	priv->rst = devm_reset_control_get_optional_shared(dev, NULL);
	if (IS_ERR(priv->rst))
		return PTR_ERR(priv->rst);

	ret = reset_control_deassert(priv->rst);
	if (ret)
		return ret;

	ret = serial8250_register_8250_port(&uart);
	if (ret < 0) {
		reset_control_assert(priv->rst);
		return ret;
	}

	priv->line = ret;
	platform_set_drvdata(pdev, priv);

	return 0;
}

static void loongson_uart_remove(struct platform_device *pdev)
{
	struct loongson_uart_priv *priv = platform_get_drvdata(pdev);

	serial8250_unregister_port(priv->line);
	reset_control_assert(priv->rst);
}

static int loongson_uart_suspend(struct device *dev)
{
	struct loongson_uart_priv *priv = dev_get_drvdata(dev);
	struct uart_8250_port *up = serial8250_get_port(priv->line);

	serial8250_suspend_port(priv->line);

	if (!uart_console(&up->port) || console_suspend_enabled)
		clk_disable_unprepare(priv->clk);

	return 0;
}

static int loongson_uart_resume(struct device *dev)
{
	struct loongson_uart_priv *priv = dev_get_drvdata(dev);
	struct uart_8250_port *up = serial8250_get_port(priv->line);
	int ret;

	if (!uart_console(&up->port) || console_suspend_enabled) {
		ret = clk_prepare_enable(priv->clk);
		if (ret)
			return ret;
	}

	serial8250_resume_port(priv->line);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(loongson_uart_pm_ops, loongson_uart_suspend,
				loongson_uart_resume);

static const struct of_device_id loongson_uart_of_ids[] = {
	{ .compatible = "loongson,ls2k0500-uart", .data = &ls2k0500_uart_data },
	{ .compatible = "loongson,ls2k1500-uart", .data = &ls2k1500_uart_data },
	{ },
};
MODULE_DEVICE_TABLE(of, loongson_uart_of_ids);

static struct platform_driver loongson_uart_driver = {
	.probe = loongson_uart_probe,
	.remove = loongson_uart_remove,
	.driver = {
		.name = "loongson-uart",
		.pm = pm_ptr(&loongson_uart_pm_ops),
		.of_match_table = loongson_uart_of_ids,
	},
};

module_platform_driver(loongson_uart_driver);

MODULE_DESCRIPTION("Loongson UART driver");
MODULE_AUTHOR("Loongson Technology Corporation Limited.");
MODULE_LICENSE("GPL");
