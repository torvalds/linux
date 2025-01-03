// SPDX-License-Identifier: GPL-2.0+
/*
 *  Serial Port driver for Tegra devices
 *
 *  Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "8250.h"

struct tegra_uart {
	struct clk *clk;
	struct reset_control *rst;
	int line;
};

static void tegra_uart_handle_break(struct uart_port *p)
{
	unsigned int status, tmout = 10000;

	while (1) {
		status = p->serial_in(p, UART_LSR);
		if (!(status & (UART_LSR_FIFOE | UART_LSR_BRK_ERROR_BITS)))
			break;

		p->serial_in(p, UART_RX);

		if (--tmout == 0)
			break;
		udelay(1);
	}
}

static int tegra_uart_probe(struct platform_device *pdev)
{
	struct uart_8250_port port8250;
	struct tegra_uart *uart;
	struct uart_port *port;
	struct resource *res;
	int ret;

	uart = devm_kzalloc(&pdev->dev, sizeof(*uart), GFP_KERNEL);
	if (!uart)
		return -ENOMEM;

	memset(&port8250, 0, sizeof(port8250));

	port = &port8250.port;
	spin_lock_init(&port->lock);

	port->flags = UPF_BOOT_AUTOCONF | UPF_FIXED_PORT | UPF_FIXED_TYPE;
	port->type = PORT_TEGRA;
	port->dev = &pdev->dev;
	port->handle_break = tegra_uart_handle_break;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	port->membase = devm_ioremap(&pdev->dev, res->start,
				     resource_size(res));
	if (!port->membase)
		return -ENOMEM;

	port->mapbase = res->start;
	port->mapsize = resource_size(res);

	ret = uart_read_port_properties(port);
	if (ret)
		return ret;

	port->iotype = UPIO_MEM32;
	port->regshift = 2;

	uart->rst = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
	if (IS_ERR(uart->rst))
		return PTR_ERR(uart->rst);

	if (!port->uartclk) {
		uart->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(uart->clk)) {
			dev_err(&pdev->dev, "failed to get clock!\n");
			return -ENODEV;
		}

		ret = clk_prepare_enable(uart->clk);
		if (ret < 0)
			return ret;

		port->uartclk = clk_get_rate(uart->clk);
	}

	ret = reset_control_deassert(uart->rst);
	if (ret)
		goto err_clkdisable;

	ret = serial8250_register_8250_port(&port8250);
	if (ret < 0)
		goto err_ctrl_assert;

	platform_set_drvdata(pdev, uart);
	uart->line = ret;

	return 0;

err_ctrl_assert:
	reset_control_assert(uart->rst);
err_clkdisable:
	clk_disable_unprepare(uart->clk);

	return ret;
}

static void tegra_uart_remove(struct platform_device *pdev)
{
	struct tegra_uart *uart = platform_get_drvdata(pdev);

	serial8250_unregister_port(uart->line);
	reset_control_assert(uart->rst);
	clk_disable_unprepare(uart->clk);
}

#ifdef CONFIG_PM_SLEEP
static int tegra_uart_suspend(struct device *dev)
{
	struct tegra_uart *uart = dev_get_drvdata(dev);
	struct uart_8250_port *port8250 = serial8250_get_port(uart->line);
	struct uart_port *port = &port8250->port;

	serial8250_suspend_port(uart->line);

	if (!uart_console(port) || console_suspend_enabled)
		clk_disable_unprepare(uart->clk);

	return 0;
}

static int tegra_uart_resume(struct device *dev)
{
	struct tegra_uart *uart = dev_get_drvdata(dev);
	struct uart_8250_port *port8250 = serial8250_get_port(uart->line);
	struct uart_port *port = &port8250->port;

	if (!uart_console(port) || console_suspend_enabled)
		clk_prepare_enable(uart->clk);

	serial8250_resume_port(uart->line);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tegra_uart_pm_ops, tegra_uart_suspend,
			 tegra_uart_resume);

static const struct of_device_id tegra_uart_of_match[] = {
	{ .compatible = "nvidia,tegra20-uart", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_uart_of_match);

static const struct acpi_device_id tegra_uart_acpi_match[] __maybe_unused = {
	{ "NVDA0100", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, tegra_uart_acpi_match);

static struct platform_driver tegra_uart_driver = {
	.driver = {
		.name = "tegra-uart",
		.pm = &tegra_uart_pm_ops,
		.of_match_table = tegra_uart_of_match,
		.acpi_match_table = ACPI_PTR(tegra_uart_acpi_match),
	},
	.probe = tegra_uart_probe,
	.remove = tegra_uart_remove,
};

module_platform_driver(tegra_uart_driver);

MODULE_AUTHOR("Jeff Brasen <jbrasen@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra 8250 Driver");
MODULE_LICENSE("GPL v2");
