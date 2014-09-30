/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/smp.h>
#include <linux/mfd/syscon.h>

#define RESET_SOURCE_ENABLE_REG 1
#define SW_MASTER_RESET_REG 2

static struct regmap *regmap;
static u32 rst_src_en;
static u32 sw_mstr_rst;

static int brcmstb_restart_handler(struct notifier_block *this,
				   unsigned long mode, void *cmd)
{
	int rc;
	u32 tmp;

	rc = regmap_write(regmap, rst_src_en, 1);
	if (rc) {
		pr_err("failed to write rst_src_en (%d)\n", rc);
		return NOTIFY_DONE;
	}

	rc = regmap_read(regmap, rst_src_en, &tmp);
	if (rc) {
		pr_err("failed to read rst_src_en (%d)\n", rc);
		return NOTIFY_DONE;
	}

	rc = regmap_write(regmap, sw_mstr_rst, 1);
	if (rc) {
		pr_err("failed to write sw_mstr_rst (%d)\n", rc);
		return NOTIFY_DONE;
	}

	rc = regmap_read(regmap, sw_mstr_rst, &tmp);
	if (rc) {
		pr_err("failed to read sw_mstr_rst (%d)\n", rc);
		return NOTIFY_DONE;
	}

	while (1)
		;

	return NOTIFY_DONE;
}

static struct notifier_block brcmstb_restart_nb = {
	.notifier_call = brcmstb_restart_handler,
	.priority = 128,
};

static int brcmstb_reboot_probe(struct platform_device *pdev)
{
	int rc;
	struct device_node *np = pdev->dev.of_node;

	regmap = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(regmap)) {
		pr_err("failed to get syscon phandle\n");
		return -EINVAL;
	}

	rc = of_property_read_u32_index(np, "syscon", RESET_SOURCE_ENABLE_REG,
					&rst_src_en);
	if (rc) {
		pr_err("can't get rst_src_en offset (%d)\n", rc);
		return -EINVAL;
	}

	rc = of_property_read_u32_index(np, "syscon", SW_MASTER_RESET_REG,
					&sw_mstr_rst);
	if (rc) {
		pr_err("can't get sw_mstr_rst offset (%d)\n", rc);
		return -EINVAL;
	}

	rc = register_restart_handler(&brcmstb_restart_nb);
	if (rc)
		dev_err(&pdev->dev,
			"cannot register restart handler (err=%d)\n", rc);

	return rc;
}

static const struct of_device_id of_match[] = {
	{ .compatible = "brcm,brcmstb-reboot", },
	{},
};

static struct platform_driver brcmstb_reboot_driver = {
	.probe = brcmstb_reboot_probe,
	.driver = {
		.name = "brcmstb-reboot",
		.owner = THIS_MODULE,
		.of_match_table = of_match,
	},
};

static int __init brcmstb_reboot_init(void)
{
	return platform_driver_probe(&brcmstb_reboot_driver,
					brcmstb_reboot_probe);
}
subsys_initcall(brcmstb_reboot_init);
