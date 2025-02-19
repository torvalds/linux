// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 *
 * Author: Vitor Soares <vitor.soares@synopsys.com>
 */

#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/i3c/device.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "st_lsm6dsx.h"

static const struct i3c_device_id st_lsm6dsx_i3c_ids[] = {
	I3C_DEVICE(0x0104, 0x006C, (void *)ST_LSM6DSO_ID),
	I3C_DEVICE(0x0104, 0x006B, (void *)ST_LSM6DSR_ID),
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i3c, st_lsm6dsx_i3c_ids);

static int st_lsm6dsx_i3c_probe(struct i3c_device *i3cdev)
{
	struct regmap_config st_lsm6dsx_i3c_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};
	const struct i3c_device_id *id = i3c_device_match_id(i3cdev,
							    st_lsm6dsx_i3c_ids);
	struct device *dev = i3cdev_to_dev(i3cdev);
	struct regmap *regmap;

	regmap = devm_regmap_init_i3c(i3cdev, &st_lsm6dsx_i3c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to register i3c regmap %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_lsm6dsx_probe(dev, 0, (uintptr_t)id->data, regmap);
}

static struct i3c_driver st_lsm6dsx_driver = {
	.driver = {
		.name = "st_lsm6dsx_i3c",
		.pm = pm_sleep_ptr(&st_lsm6dsx_pm_ops),
	},
	.probe = st_lsm6dsx_i3c_probe,
	.id_table = st_lsm6dsx_i3c_ids,
};
module_i3c_driver(st_lsm6dsx_driver);

MODULE_AUTHOR("Vitor Soares <vitor.soares@synopsys.com>");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsx i3c driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_LSM6DSX");
