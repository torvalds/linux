/* Power off driver for i.mx6
 * Copyright (c) 2014, FREESCALE CORPORATION.  All rights reserved.
 *
 * based on msm-poweroff.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

static void __iomem *snvs_base;

static void do_imx_poweroff(void)
{
	u32 value = readl(snvs_base);

	/* set TOP and DP_EN bit */
	writel(value | 0x60, snvs_base);
}

static int imx_poweroff_probe(struct platform_device *pdev)
{
	snvs_base = of_iomap(pdev->dev.of_node, 0);
	if (!snvs_base) {
		dev_err(&pdev->dev, "failed to get memory\n");
		return -ENODEV;
	}

	pm_power_off = do_imx_poweroff;
	return 0;
}

static const struct of_device_id of_imx_poweroff_match[] = {
	{ .compatible = "fsl,sec-v4.0-poweroff", },
	{},
};
MODULE_DEVICE_TABLE(of, of_imx_poweroff_match);

static struct platform_driver imx_poweroff_driver = {
	.probe = imx_poweroff_probe,
	.driver = {
		.name = "imx-snvs-poweroff",
		.of_match_table = of_match_ptr(of_imx_poweroff_match),
	},
};

static int __init imx_poweroff_init(void)
{
	return platform_driver_register(&imx_poweroff_driver);
}
device_initcall(imx_poweroff_init);
