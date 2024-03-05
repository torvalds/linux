// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsox i3c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2020 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i3c/device.h>
#include <linux/i3c/master.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "st_lsm6dsox.h"

static const struct i3c_device_id st_lsm6dsox_i3c_ids[] = {
	I3C_DEVICE(0x0104, ST_LSM6DSOX_WHOAMI_VAL, (void *)ST_LSM6DSO_ID),
	{},
};
MODULE_DEVICE_TABLE(i3c, st_lsm6dsox_i3c_ids);

static int st_lsm6dsox_i3c_probe(struct i3c_device *i3cdev)
{
	struct regmap_config st_lsm6dsox_i3c_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};
	const struct i3c_device_id *id =
				i3c_device_match_id(i3cdev, st_lsm6dsox_i3c_ids);
	struct regmap *regmap;

	regmap = devm_regmap_init_i3c(i3cdev, &st_lsm6dsox_i3c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&i3cdev->dev, "Failed to register i3c regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_lsm6dsox_probe(&i3cdev->dev, 0,
				(uintptr_t)id->data, regmap);
}

static struct i3c_driver st_lsm6dsox_driver = {
	.driver = {
		.name = "st_" ST_LSM6DSOX_DEV_NAME "_i3c",
		.pm = &st_lsm6dsox_pm_ops,
	},
	.probe = st_lsm6dsox_i3c_probe,
	.id_table = st_lsm6dsox_i3c_ids,
};
module_i3c_driver(st_lsm6dsox_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsox i3c driver");
MODULE_LICENSE("GPL v2");
