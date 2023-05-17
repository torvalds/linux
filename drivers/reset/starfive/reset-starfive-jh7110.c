// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Reset driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/auxiliary_bus.h>

#include <soc/starfive/reset-starfive-jh71x0.h>

#include "reset-starfive-jh71x0.h"

#include <dt-bindings/reset/starfive,jh7110-crg.h>

struct jh7110_reset_info {
	unsigned int nr_resets;
	unsigned int assert_offset;
	unsigned int status_offset;
};

static const struct jh7110_reset_info jh7110_sys_info = {
	.nr_resets = JH7110_SYSRST_END,
	.assert_offset = 0x2F8,
	.status_offset = 0x308,
};

static const struct jh7110_reset_info jh7110_aon_info = {
	.nr_resets = JH7110_AONRST_END,
	.assert_offset = 0x38,
	.status_offset = 0x3C,
};

static int jh7110_reset_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	struct jh7110_reset_info *info = (struct jh7110_reset_info *)(id->driver_data);
	struct jh71x0_reset_adev *rdev = to_jh71x0_reset_adev(adev);
	void __iomem *base = rdev->base;

	if (!info || !base)
		return -ENODEV;

	return reset_starfive_jh71x0_register(&adev->dev, adev->dev.parent->of_node,
					      base + info->assert_offset,
					      base + info->status_offset,
					      NULL,
					      info->nr_resets,
					      NULL);
}

static const struct auxiliary_device_id jh7110_reset_ids[] = {
	{
		.name = "clk_starfive_jh7110_sys.rst-sys",
		.driver_data = (kernel_ulong_t)&jh7110_sys_info,
	},
	{
		.name = "clk_starfive_jh7110_sys.rst-aon",
		.driver_data = (kernel_ulong_t)&jh7110_aon_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(auxiliary, jh7110_reset_ids);

static struct auxiliary_driver jh7110_reset_driver = {
	.probe		= jh7110_reset_probe,
	.id_table	= jh7110_reset_ids,
};
module_auxiliary_driver(jh7110_reset_driver);

MODULE_AUTHOR("Hal Feng <hal.feng@starfivetech.com>");
MODULE_DESCRIPTION("StarFive JH7110 reset driver");
MODULE_LICENSE("GPL");
