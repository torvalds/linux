/*
 * Copyright (C) 2014 STMicroelectronics
 *
 * Power off Restart driver, used in STMicroelectronics devices.
 *
 * Author: Christophe Kerello <christophe.kerello@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

struct reset_syscfg {
	struct regmap *regmap;
	/* syscfg used for reset */
	unsigned int offset_rst;
	unsigned int mask_rst;
	/* syscfg used for unmask the reset */
	unsigned int offset_rst_msk;
	unsigned int mask_rst_msk;
};

/* STiH415 */
#define STIH415_SYSCFG_11	0x2c
#define STIH415_SYSCFG_15	0x3c

static struct reset_syscfg stih415_reset = {
	.offset_rst = STIH415_SYSCFG_11,
	.mask_rst = BIT(0),
	.offset_rst_msk = STIH415_SYSCFG_15,
	.mask_rst_msk = BIT(0)
};

/* STiH416 */
#define STIH416_SYSCFG_500	0x7d0
#define STIH416_SYSCFG_504	0x7e0

static struct reset_syscfg stih416_reset = {
	.offset_rst = STIH416_SYSCFG_500,
	.mask_rst = BIT(0),
	.offset_rst_msk = STIH416_SYSCFG_504,
	.mask_rst_msk = BIT(0)
};

/* STiH407 */
#define STIH407_SYSCFG_4000	0x0
#define STIH407_SYSCFG_4008	0x20

static struct reset_syscfg stih407_reset = {
	.offset_rst = STIH407_SYSCFG_4000,
	.mask_rst = BIT(0),
	.offset_rst_msk = STIH407_SYSCFG_4008,
	.mask_rst_msk = BIT(0)
};

/* STiD127 */
#define STID127_SYSCFG_700	0x0
#define STID127_SYSCFG_773	0x124

static struct reset_syscfg stid127_reset = {
	.offset_rst = STID127_SYSCFG_773,
	.mask_rst = BIT(0),
	.offset_rst_msk = STID127_SYSCFG_700,
	.mask_rst_msk = BIT(8)
};

static struct reset_syscfg *st_restart_syscfg;

static int st_restart(struct notifier_block *this, unsigned long mode,
		      void *cmd)
{
	/* reset syscfg updated */
	regmap_update_bits(st_restart_syscfg->regmap,
			   st_restart_syscfg->offset_rst,
			   st_restart_syscfg->mask_rst,
			   0);

	/* unmask the reset */
	regmap_update_bits(st_restart_syscfg->regmap,
			   st_restart_syscfg->offset_rst_msk,
			   st_restart_syscfg->mask_rst_msk,
			   0);

	return NOTIFY_DONE;
}

static struct notifier_block st_restart_nb = {
	.notifier_call = st_restart,
	.priority = 192,
};

static const struct of_device_id st_reset_of_match[] = {
	{
		.compatible = "st,stih415-restart",
		.data = (void *)&stih415_reset,
	}, {
		.compatible = "st,stih416-restart",
		.data = (void *)&stih416_reset,
	}, {
		.compatible = "st,stih407-restart",
		.data = (void *)&stih407_reset,
	}, {
		.compatible = "st,stid127-restart",
		.data = (void *)&stid127_reset,
	},
	{}
};

static int st_reset_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;

	match = of_match_device(st_reset_of_match, dev);
	if (!match)
		return -ENODEV;

	st_restart_syscfg = (struct reset_syscfg *)match->data;

	st_restart_syscfg->regmap =
		syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(st_restart_syscfg->regmap)) {
		dev_err(dev, "No syscfg phandle specified\n");
		return PTR_ERR(st_restart_syscfg->regmap);
	}

	return register_restart_handler(&st_restart_nb);
}

static struct platform_driver st_reset_driver = {
	.probe = st_reset_probe,
	.driver = {
		.name = "st_reset",
		.of_match_table = st_reset_of_match,
	},
};

static int __init st_reset_init(void)
{
	return platform_driver_register(&st_reset_driver);
}

device_initcall(st_reset_init);

MODULE_AUTHOR("Christophe Kerello <christophe.kerello@st.com>");
MODULE_DESCRIPTION("STMicroelectronics Power off Restart driver");
MODULE_LICENSE("GPL v2");
