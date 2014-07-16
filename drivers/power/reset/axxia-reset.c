/*
 * Reset driver for Axxia devices
 *
 * Copyright (C) 2014 LSI
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
#include <linux/init.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#include <asm/system_misc.h>


#define SC_CRIT_WRITE_KEY	0x1000
#define SC_LATCH_ON_RESET	0x1004
#define SC_RESET_CONTROL	0x1008
#define   RSTCTL_RST_ZERO	(1<<3)
#define   RSTCTL_RST_FAB	(1<<2)
#define   RSTCTL_RST_CHIP	(1<<1)
#define   RSTCTL_RST_SYS	(1<<0)
#define SC_EFUSE_INT_STATUS	0x180c
#define   EFUSE_READ_DONE	(1<<31)

static struct regmap *syscon;

static void do_axxia_restart(enum reboot_mode reboot_mode, const char *cmd)
{
	/* Access Key (0xab) */
	regmap_write(syscon, SC_CRIT_WRITE_KEY, 0xab);
	/* Select internal boot from 0xffff0000 */
	regmap_write(syscon, SC_LATCH_ON_RESET, 0x00000040);
	/* Assert ResetReadDone (to avoid hanging in boot ROM) */
	regmap_write(syscon, SC_EFUSE_INT_STATUS, EFUSE_READ_DONE);
	/* Assert chip reset */
	regmap_update_bits(syscon, SC_RESET_CONTROL,
			   RSTCTL_RST_CHIP, RSTCTL_RST_CHIP);
}

static int axxia_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	syscon = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
	if (IS_ERR(syscon)) {
		pr_err("%s: syscon lookup failed\n", dev->of_node->name);
		return PTR_ERR(syscon);
	}

	arm_pm_restart = do_axxia_restart;

	return 0;
}

static const struct of_device_id of_axxia_reset_match[] = {
	{ .compatible = "lsi,axm55xx-reset", },
	{},
};
MODULE_DEVICE_TABLE(of, of_axxia_reset_match);

static struct platform_driver axxia_reset_driver = {
	.probe = axxia_reset_probe,
	.driver = {
		.name = "axxia-reset",
		.of_match_table = of_match_ptr(of_axxia_reset_match),
	},
};

static int __init axxia_reset_init(void)
{
	return platform_driver_register(&axxia_reset_driver);
}
device_initcall(axxia_reset_init);
