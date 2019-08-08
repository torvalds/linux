// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/of.h>

#define INTEGRATOR_HDR_CTRL_OFFSET	0x0C
#define INTEGRATOR_HDR_LOCK_OFFSET	0x14
#define INTEGRATOR_CM_CTRL_RESET	(1 << 3)

#define VERSATILE_SYS_LOCK_OFFSET	0x20
#define VERSATILE_SYS_RESETCTL_OFFSET	0x40

/* Magic unlocking token used on all Versatile boards */
#define VERSATILE_LOCK_VAL		0xA05F

/*
 * We detect the different syscon types from the compatible strings.
 */
enum versatile_reboot {
	INTEGRATOR_REBOOT_CM,
	VERSATILE_REBOOT_CM,
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
		.compatible = "arm,core-module-integrator",
		.data = (void *)INTEGRATOR_REBOOT_CM
	},
	{
		.compatible = "arm,core-module-versatile",
		.data = (void *)VERSATILE_REBOOT_CM,
	},
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
	{},
};

static int versatile_reboot(struct notifier_block *this, unsigned long mode,
			    void *cmd)
{
	/* Unlock the reset register */
	/* Then hit reset on the different machines */
	switch (versatile_reboot_type) {
	case INTEGRATOR_REBOOT_CM:
		regmap_write(syscon_regmap, INTEGRATOR_HDR_LOCK_OFFSET,
			     VERSATILE_LOCK_VAL);
		regmap_update_bits(syscon_regmap,
				   INTEGRATOR_HDR_CTRL_OFFSET,
				   INTEGRATOR_CM_CTRL_RESET,
				   INTEGRATOR_CM_CTRL_RESET);
		break;
	case VERSATILE_REBOOT_CM:
		regmap_write(syscon_regmap, VERSATILE_SYS_LOCK_OFFSET,
			     VERSATILE_LOCK_VAL);
		regmap_update_bits(syscon_regmap,
				   VERSATILE_SYS_RESETCTL_OFFSET,
				   0x0107,
				   0x0105);
		regmap_write(syscon_regmap, VERSATILE_SYS_LOCK_OFFSET,
			     0);
		break;
	case REALVIEW_REBOOT_EB:
		regmap_write(syscon_regmap, VERSATILE_SYS_LOCK_OFFSET,
			     VERSATILE_LOCK_VAL);
		regmap_write(syscon_regmap,
			     VERSATILE_SYS_RESETCTL_OFFSET, 0x0008);
		break;
	case REALVIEW_REBOOT_PB1176:
		regmap_write(syscon_regmap, VERSATILE_SYS_LOCK_OFFSET,
			     VERSATILE_LOCK_VAL);
		regmap_write(syscon_regmap,
			     VERSATILE_SYS_RESETCTL_OFFSET, 0x0100);
		break;
	case REALVIEW_REBOOT_PB11MP:
	case REALVIEW_REBOOT_PBA8:
		regmap_write(syscon_regmap, VERSATILE_SYS_LOCK_OFFSET,
			     VERSATILE_LOCK_VAL);
		regmap_write(syscon_regmap, VERSATILE_SYS_RESETCTL_OFFSET,
			     0x0000);
		regmap_write(syscon_regmap, VERSATILE_SYS_RESETCTL_OFFSET,
			     0x0004);
		break;
	case REALVIEW_REBOOT_PBX:
		regmap_write(syscon_regmap, VERSATILE_SYS_LOCK_OFFSET,
			     VERSATILE_LOCK_VAL);
		regmap_write(syscon_regmap, VERSATILE_SYS_RESETCTL_OFFSET,
			     0x00f0);
		regmap_write(syscon_regmap, VERSATILE_SYS_RESETCTL_OFFSET,
			     0x00f4);
		break;
	}
	dsb();

	return NOTIFY_DONE;
}

static struct notifier_block versatile_reboot_nb = {
	.notifier_call = versatile_reboot,
	.priority = 192,
};

static int __init versatile_reboot_probe(void)
{
	const struct of_device_id *reboot_id;
	struct device_node *np;
	int err;

	np = of_find_matching_node_and_match(NULL, versatile_reboot_of_match,
						 &reboot_id);
	if (!np)
		return -ENODEV;
	versatile_reboot_type = (enum versatile_reboot)reboot_id->data;

	syscon_regmap = syscon_node_to_regmap(np);
	if (IS_ERR(syscon_regmap))
		return PTR_ERR(syscon_regmap);

	err = register_restart_handler(&versatile_reboot_nb);
	if (err)
		return err;

	pr_info("versatile reboot driver registered\n");
	return 0;
}
device_initcall(versatile_reboot_probe);
