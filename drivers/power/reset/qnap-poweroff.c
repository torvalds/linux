// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QNAP Turbo NAS Board power off. Can also be used on Synology devices.
 *
 * Copyright (C) 2012 Andrew Lunn <andrew@lunn.ch>
 *
 * Based on the code from:
 *
 * Copyright (C) 2009  Martin Michlmayr <tbm@cyrius.com>
 * Copyright (C) 2008  Byron Bradley <byron.bbradley@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial_reg.h>
#include <linux/kallsyms.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/clk.h>

#define UART1_REG(x)	(base + ((UART_##x) << 2))

struct power_off_cfg {
	u32 baud;
	char cmd;
};

static const struct power_off_cfg qnap_power_off_cfg = {
	.baud = 19200,
	.cmd = 'A',
};

static const struct power_off_cfg synology_power_off_cfg = {
	.baud = 9600,
	.cmd = '1',
};

static const struct of_device_id qnap_power_off_of_match_table[] = {
	{ .compatible = "qnap,power-off",
	  .data = &qnap_power_off_cfg,
	},
	{ .compatible = "synology,power-off",
	  .data = &synology_power_off_cfg,
	},
	{}
};
MODULE_DEVICE_TABLE(of, qnap_power_off_of_match_table);

static void __iomem *base;
static unsigned long tclk;
static const struct power_off_cfg *cfg;

static void qnap_power_off(void)
{
	const unsigned divisor = ((tclk + (8 * cfg->baud)) / (16 * cfg->baud));

	pr_err("%s: triggering power-off...\n", __func__);

	/* hijack UART1 and reset into sane state */
	writel(0x83, UART1_REG(LCR));
	writel(divisor & 0xff, UART1_REG(DLL));
	writel((divisor >> 8) & 0xff, UART1_REG(DLM));
	writel(0x03, UART1_REG(LCR));
	writel(0x00, UART1_REG(IER));
	writel(0x00, UART1_REG(FCR));
	writel(0x00, UART1_REG(MCR));

	/* send the power-off command to PIC */
	writel(cfg->cmd, UART1_REG(TX));
}

static int qnap_power_off_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct clk *clk;
	char symname[KSYM_NAME_LEN];

	const struct of_device_id *match =
		of_match_node(qnap_power_off_of_match_table, np);
	cfg = match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Missing resource");
		return -EINVAL;
	}

	base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!base) {
		dev_err(&pdev->dev, "Unable to map resource");
		return -EINVAL;
	}

	/* We need to know tclk in order to calculate the UART divisor */
	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Clk missing");
		return PTR_ERR(clk);
	}

	tclk = clk_get_rate(clk);

	/* Check that nothing else has already setup a handler */
	if (pm_power_off) {
		lookup_symbol_name((ulong)pm_power_off, symname);
		dev_err(&pdev->dev,
			"pm_power_off already claimed %p %s",
			pm_power_off, symname);
		return -EBUSY;
	}
	pm_power_off = qnap_power_off;

	return 0;
}

static int qnap_power_off_remove(struct platform_device *pdev)
{
	pm_power_off = NULL;
	return 0;
}

static struct platform_driver qnap_power_off_driver = {
	.probe	= qnap_power_off_probe,
	.remove	= qnap_power_off_remove,
	.driver	= {
		.name	= "qnap_power_off",
		.of_match_table = of_match_ptr(qnap_power_off_of_match_table),
	},
};
module_platform_driver(qnap_power_off_driver);

MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_DESCRIPTION("QNAP Power off driver");
MODULE_LICENSE("GPL v2");
