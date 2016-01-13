/*
 * Mediatek 8250 driver.
 *
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Matthias Brugger <matthias.bgg@gmail.com>
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
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>

#include "8250.h"

#define UART_MTK_HIGHS		0x09	/* Highspeed register */
#define UART_MTK_SAMPLE_COUNT	0x0a	/* Sample count register */
#define UART_MTK_SAMPLE_POINT	0x0b	/* Sample point register */
#define MTK_UART_RATE_FIX	0x0d	/* UART Rate Fix Register */

struct mtk8250_data {
	int			line;
	struct clk		*uart_clk;
	struct clk		*bus_clk;
};

static void
mtk8250_set_termios(struct uart_port *port, struct ktermios *termios,
			struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud, quot;

	struct uart_8250_port *up =
		container_of(port, struct uart_8250_port, port);

	serial8250_do_set_termios(port, termios, old);

	/*
	 * Mediatek UARTs use an extra highspeed register (UART_MTK_HIGHS)
	 *
	 * We need to recalcualte the quot register, as the claculation depends
	 * on the vaule in the highspeed register.
	 *
	 * Some baudrates are not supported by the chip, so we use the next
	 * lower rate supported and update termios c_flag.
	 *
	 * If highspeed register is set to 3, we need to specify sample count
	 * and sample point to increase accuracy. If not, we reset the
	 * registers to their default values.
	 */
	baud = uart_get_baud_rate(port, termios, old,
				  port->uartclk / 16 / 0xffff,
				  port->uartclk / 16);

	if (baud <= 115200) {
		serial_port_out(port, UART_MTK_HIGHS, 0x0);
		quot = uart_get_divisor(port, baud);
	} else if (baud <= 576000) {
		serial_port_out(port, UART_MTK_HIGHS, 0x2);

		/* Set to next lower baudrate supported */
		if ((baud == 500000) || (baud == 576000))
			baud = 460800;
		quot = DIV_ROUND_UP(port->uartclk, 4 * baud);
	} else {
		serial_port_out(port, UART_MTK_HIGHS, 0x3);

		/* Set to highest baudrate supported */
		if (baud >= 1152000)
			baud = 921600;
		quot = DIV_ROUND_UP(port->uartclk, 256 * baud);
	}

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&port->lock, flags);

	/* set DLAB we have cval saved in up->lcr from the call to the core */
	serial_port_out(port, UART_LCR, up->lcr | UART_LCR_DLAB);
	serial_dl_write(up, quot);

	/* reset DLAB */
	serial_port_out(port, UART_LCR, up->lcr);

	if (baud > 460800) {
		unsigned int tmp;

		tmp = DIV_ROUND_CLOSEST(port->uartclk, quot * baud);
		serial_port_out(port, UART_MTK_SAMPLE_COUNT, tmp - 1);
		serial_port_out(port, UART_MTK_SAMPLE_POINT,
					(tmp - 2) >> 1);
	} else {
		serial_port_out(port, UART_MTK_SAMPLE_COUNT, 0x00);
		serial_port_out(port, UART_MTK_SAMPLE_POINT, 0xff);
	}

	spin_unlock_irqrestore(&port->lock, flags);
	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);
}

static int mtk8250_runtime_suspend(struct device *dev)
{
	struct mtk8250_data *data = dev_get_drvdata(dev);

	clk_disable_unprepare(data->uart_clk);
	clk_disable_unprepare(data->bus_clk);

	return 0;
}

static int mtk8250_runtime_resume(struct device *dev)
{
	struct mtk8250_data *data = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(data->uart_clk);
	if (err) {
		dev_warn(dev, "Can't enable clock\n");
		return err;
	}

	err = clk_prepare_enable(data->bus_clk);
	if (err) {
		dev_warn(dev, "Can't enable bus clock\n");
		return err;
	}

	return 0;
}

static void
mtk8250_do_pm(struct uart_port *port, unsigned int state, unsigned int old)
{
	if (!state)
		pm_runtime_get_sync(port->dev);

	serial8250_do_pm(port, state, old);

	if (state)
		pm_runtime_put_sync_suspend(port->dev);
}

static int mtk8250_probe_of(struct platform_device *pdev, struct uart_port *p,
			   struct mtk8250_data *data)
{
	data->uart_clk = devm_clk_get(&pdev->dev, "baud");
	if (IS_ERR(data->uart_clk)) {
		/*
		 * For compatibility with older device trees try unnamed
		 * clk when no baud clk can be found.
		 */
		data->uart_clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(data->uart_clk)) {
			dev_warn(&pdev->dev, "Can't get uart clock\n");
			return PTR_ERR(data->uart_clk);
		}

		return 0;
	}

	data->bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(data->bus_clk))
		return PTR_ERR(data->bus_clk);

	return 0;
}

static int mtk8250_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {};
	struct resource *regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	struct mtk8250_data *data;
	int err;

	if (!regs || !irq) {
		dev_err(&pdev->dev, "no registers/irq defined\n");
		return -EINVAL;
	}

	uart.port.membase = devm_ioremap(&pdev->dev, regs->start,
					 resource_size(regs));
	if (!uart.port.membase)
		return -ENOMEM;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		err = mtk8250_probe_of(pdev, &uart.port, data);
		if (err)
			return err;
	} else
		return -ENODEV;

	spin_lock_init(&uart.port.lock);
	uart.port.mapbase = regs->start;
	uart.port.irq = irq->start;
	uart.port.pm = mtk8250_do_pm;
	uart.port.type = PORT_16550;
	uart.port.flags = UPF_BOOT_AUTOCONF | UPF_FIXED_PORT;
	uart.port.dev = &pdev->dev;
	uart.port.iotype = UPIO_MEM32;
	uart.port.regshift = 2;
	uart.port.private_data = data;
	uart.port.set_termios = mtk8250_set_termios;
	uart.port.uartclk = clk_get_rate(data->uart_clk);

	/* Disable Rate Fix function */
	writel(0x0, uart.port.membase +
			(MTK_UART_RATE_FIX << uart.port.regshift));

	platform_set_drvdata(pdev, data);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		err = mtk8250_runtime_resume(&pdev->dev);
		if (err)
			return err;
	}

	data->line = serial8250_register_8250_port(&uart);
	if (data->line < 0)
		return data->line;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk8250_suspend(struct device *dev)
{
	struct mtk8250_data *data = dev_get_drvdata(dev);

	serial8250_suspend_port(data->line);

	return 0;
}

static int mtk8250_resume(struct device *dev)
{
	struct mtk8250_data *data = dev_get_drvdata(dev);

	serial8250_resume_port(data->line);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops mtk8250_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk8250_suspend, mtk8250_resume)
	SET_RUNTIME_PM_OPS(mtk8250_runtime_suspend, mtk8250_runtime_resume,
				NULL)
};

static const struct of_device_id mtk8250_of_match[] = {
	{ .compatible = "mediatek,mt6577-uart" },
	{ /* Sentinel */ }
};

static struct platform_driver mtk8250_platform_driver = {
	.driver = {
		.name			= "mt6577-uart",
		.pm			= &mtk8250_pm_ops,
		.of_match_table		= mtk8250_of_match,
		.suppress_bind_attrs	= true,

	},
	.probe			= mtk8250_probe,
};
builtin_platform_driver(mtk8250_platform_driver);

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init early_mtk8250_setup(struct earlycon_device *device,
					const char *options)
{
	if (!device->port.membase)
		return -ENODEV;

	device->port.iotype = UPIO_MEM32;

	return early_serial8250_setup(device, NULL);
}

OF_EARLYCON_DECLARE(mtk8250, "mediatek,mt6577-uart", early_mtk8250_setup);
#endif
