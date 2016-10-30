/*
 * ZTE zx296702 SoC reset code
 *
 * Copyright (c) 2015 Linaro Ltd.
 *
 * Author: Jun Nie <jun.nie@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

static void __iomem *base;
static void __iomem *pcu_base;

static int zx_restart_handler(struct notifier_block *this,
			      unsigned long mode, void *cmd)
{
	writel_relaxed(1, base + 0xb0);
	writel_relaxed(1, pcu_base + 0x34);

	mdelay(50);
	pr_emerg("Unable to restart system\n");

	return NOTIFY_DONE;
}

static struct notifier_block zx_restart_nb = {
	.notifier_call = zx_restart_handler,
	.priority = 128,
};

static int zx_reboot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int err;

	base = of_iomap(np, 0);
	if (!base) {
		WARN(1, "failed to map base address");
		return -ENODEV;
	}

	np = of_find_compatible_node(NULL, NULL, "zte,zx296702-pcu");
	pcu_base = of_iomap(np, 0);
	if (!pcu_base) {
		iounmap(base);
		WARN(1, "failed to map pcu_base address");
		return -ENODEV;
	}

	err = register_restart_handler(&zx_restart_nb);
	if (err) {
		iounmap(base);
		iounmap(pcu_base);
		dev_err(&pdev->dev, "Register restart handler failed(err=%d)\n",
			err);
	}

	return err;
}

static const struct of_device_id zx_reboot_of_match[] = {
	{ .compatible = "zte,sysctrl" },
	{}
};

static struct platform_driver zx_reboot_driver = {
	.probe = zx_reboot_probe,
	.driver = {
		.name = "zx-reboot",
		.of_match_table = zx_reboot_of_match,
	},
};
module_platform_driver(zx_reboot_driver);
