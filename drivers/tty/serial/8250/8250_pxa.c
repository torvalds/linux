// SPDX-License-Identifier: GPL-2.0+
/*
 *  drivers/tty/serial/8250/8250_pxa.c -- driver for PXA on-board UARTS
 *  Copyright:	(C) 2013 Sergei Ianovich <ynvich@gmail.com>
 *
 *  replaces drivers/serial/pxa.c by Nicolas Pitre
 *  Created:	Feb 20, 2003
 *  Copyright:	(C) 2003 Monta Vista Software, Inc.
 *
 *  Based on drivers/serial/8250.c by Russell King.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include "8250.h"

struct pxa8250_data {
	int			line;
	struct clk		*clk;
};

static int __maybe_unused serial_pxa_suspend(struct device *dev)
{
	struct pxa8250_data *data = dev_get_drvdata(dev);

	serial8250_suspend_port(data->line);

	return 0;
}

static int __maybe_unused serial_pxa_resume(struct device *dev)
{
	struct pxa8250_data *data = dev_get_drvdata(dev);

	serial8250_resume_port(data->line);

	return 0;
}

static const struct dev_pm_ops serial_pxa_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(serial_pxa_suspend, serial_pxa_resume)
};

static const struct of_device_id serial_pxa_dt_ids[] = {
	{ .compatible = "mrvl,pxa-uart", },
	{ .compatible = "mrvl,mmp-uart", },
	{}
};
MODULE_DEVICE_TABLE(of, serial_pxa_dt_ids);

/* Uart divisor latch write */
static void serial_pxa_dl_write(struct uart_8250_port *up, u32 value)
{
	unsigned int dll;

	serial_out(up, UART_DLL, value & 0xff);
	/*
	 * work around Erratum #74 according to Marvel(R) PXA270M Processor
	 * Specification Update (April 19, 2010)
	 */
	dll = serial_in(up, UART_DLL);
	WARN_ON(dll != (value & 0xff));

	serial_out(up, UART_DLM, value >> 8 & 0xff);
}


static void serial_pxa_pm(struct uart_port *port, unsigned int state,
	      unsigned int oldstate)
{
	struct pxa8250_data *data = port->private_data;

	if (!state)
		clk_prepare_enable(data->clk);
	else
		clk_disable_unprepare(data->clk);
}

static int serial_pxa_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {};
	struct pxa8250_data *data;
	struct resource *mmres;
	int ret;

	mmres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mmres)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(data->clk))
		return PTR_ERR(data->clk);

	ret = clk_prepare(data->clk);
	if (ret)
		return ret;

	uart.port.type = PORT_XSCALE;
	uart.port.mapbase = mmres->start;
	uart.port.flags = UPF_IOREMAP | UPF_SKIP_TEST | UPF_FIXED_TYPE;
	uart.port.dev = &pdev->dev;
	uart.port.uartclk = clk_get_rate(data->clk);
	uart.port.pm = serial_pxa_pm;
	uart.port.private_data = data;

	ret = uart_read_port_properties(&uart.port);
	if (ret)
		return ret;

	uart.port.iotype = UPIO_MEM32;
	uart.port.regshift = 2;
	uart.port.fifosize = 64;
	uart.tx_loadsz = 32;
	uart.dl_write = serial_pxa_dl_write;

	ret = serial8250_register_8250_port(&uart);
	if (ret < 0)
		goto err_clk;

	data->line = ret;

	platform_set_drvdata(pdev, data);

	return 0;

 err_clk:
	clk_unprepare(data->clk);
	return ret;
}

static void serial_pxa_remove(struct platform_device *pdev)
{
	struct pxa8250_data *data = platform_get_drvdata(pdev);

	serial8250_unregister_port(data->line);

	clk_unprepare(data->clk);
}

static struct platform_driver serial_pxa_driver = {
	.probe          = serial_pxa_probe,
	.remove         = serial_pxa_remove,

	.driver		= {
		.name	= "pxa2xx-uart",
		.pm	= &serial_pxa_pm_ops,
		.of_match_table = serial_pxa_dt_ids,
	},
};

module_platform_driver(serial_pxa_driver);

MODULE_AUTHOR("Sergei Ianovich");
MODULE_DESCRIPTION("driver for PXA on-board UARTS");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-uart");
