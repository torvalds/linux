/*
 * Copyright (C) 2014 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <asm/system_misc.h>

#define REALVIEW_SYS_LOCK_OFFSET	0x20
#define REALVIEW_SYS_LOCK_VAL		0xA05F
#define REALVIEW_SYS_RESETCTL_OFFSET	0x40

/*
 * We detect the different syscon types from the compatible strings.
 */
enum versatile_reboot {
	REALVIEW_REBOOT_EB,
	REALVIEW_REBOOT_PB1176,
	REALVIEW_REBOOT_PB11MP,
	REALVIEW_REBOOT_PBA8,
	REALVIEW_REBOOT_PBX,
};

/* Pointer to the system controller */
static struct regmap *syscon_regmap;
static enum versatile_reboot versatile_reboot_type;

static const struct of_device_id versatile_reboot_of_match[] = {
	{
		.compatible = "arm,realview-eb-syscon",
		.data = (void *)REALVIEW_REBOOT_EB,
	},
	{
		.compatible = "arm,realview-pb1176-syscon",
		.data = (void *)REALVIEW_REBOOT_PB1176,
	},
	{
		.compatible = "arm,realview-pb11mp-syscon",
		.data = (void *)REALVIEW_REBOOT_PB11MP,
	},
	{
		.compatible = "arm,realview-pba8-syscon",
		.data = (void *)REALVIEW_REBOOT_PBA8,
	},
	{
		.compatible = "arm,realview-pbx-syscon",
		.data = (void *)REALVIEW_REBOOT_PBX,
	},
};

static void versatile_reboot(enum reboot_mode mode, const char *cmd)
{
	/* Unlock the reset register */
	regmap_write(syscon_regmap, REALVIEW_SYS_LOCK_OFFSET,
		     REALVIEW_SYS_LOCK_VAL);
	/* Then hit reset on the different machines */
	switch (versatile_reboot_type) {
	case REALVIEW_REBOOT_EB:
		regmap_write(syscon_regmap,
			     REALVIEW_SYS_RESETCTL_OFFSET, 0x0008);
		break;
	case REALVIEW_REBOOT_PB1176:
		regmap_write(syscon_regmap,
			     REALVIEW_SYS_RESETCTL_OFFSET, 0x0100);
		break;
	case REALVIEW_REBOOT_PB11MP:
	case REALVIEW_REBOOT_PBA8:
		regmap_write(syscon_regmap, REALVIEW_SYS_RESETCTL_OFFSET,
			     0x0000);
		regmap_write(syscon_regmap, REALVIEW_SYS_RESETCTL_OFFSET,
			     0x0004);
		break;
	case REALVIEW_REBOOT_PBX:
		regmap_write(syscon_regmap, REALVIEW_SYS_RESETCTL_OFFSET,
			     0x00f0);
		regmap_write(syscon_regmap, REALVIEW_SYS_RESETCTL_OFFSET,
			     0x00f4);
		break;
	}
	dsb();
}

static int __init versatile_reboot_probe(void)
{
	const struct of_device_id *reboot_id;
	struct device_node *np;

	np = of_find_matching_node_and_match(NULL, versatile_reboot_of_match,
						 &reboot_id);
	if (!np)
		return -ENODEV;
	versatile_reboot_type = (enum versatile_reboot)reboot_id->data;

	syscon_regmap = syscon_node_to_regmap(np);
	if (IS_ERR(syscon_regmap))
		return PTR_ERR(syscon_regmap);

	arm_pm_restart = versatile_reboot;
	pr_info("versatile reboot driver registered\n");
	return 0;
}
device_initcall(versatile_reboot_probe);
