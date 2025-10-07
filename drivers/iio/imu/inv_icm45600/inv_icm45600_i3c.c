// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2025 InvenSense, Inc. */

#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/i3c/device.h>
#include <linux/i3c/master.h>

#include "inv_icm45600.h"

static const struct regmap_config inv_icm45600_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct i3c_device_id inv_icm45600_i3c_ids[] = {
	I3C_DEVICE_EXTRA_INFO(0x0235, 0x0000, 0x0011, (void *)NULL),
	I3C_DEVICE_EXTRA_INFO(0x0235, 0x0000, 0x0084, (void *)NULL),
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i3c, inv_icm45600_i3c_ids);

static const struct inv_icm45600_chip_info *i3c_chip_info[] = {
	&inv_icm45605_chip_info,
	&inv_icm45606_chip_info,
	&inv_icm45608_chip_info,
	&inv_icm45634_chip_info,
	&inv_icm45686_chip_info,
	&inv_icm45687_chip_info,
	&inv_icm45688p_chip_info,
	&inv_icm45689_chip_info,
};

static int inv_icm45600_i3c_probe(struct i3c_device *i3cdev)
{
	int ret;
	unsigned int whoami;
	struct regmap *regmap;
	const int nb_chip = ARRAY_SIZE(i3c_chip_info);
	int chip;

	regmap = devm_regmap_init_i3c(i3cdev, &inv_icm45600_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&i3cdev->dev, PTR_ERR(regmap),
					"Failed to register i3c regmap %ld\n", PTR_ERR(regmap));

	ret = regmap_read(regmap, INV_ICM45600_REG_WHOAMI, &whoami);
	if (ret)
		return dev_err_probe(&i3cdev->dev, ret, "Failed to read part id %d\n", whoami);

	for (chip = 0; chip < nb_chip; chip++) {
		if (whoami == i3c_chip_info[chip]->whoami)
			break;
	}

	if (chip == nb_chip)
		dev_err_probe(&i3cdev->dev, -ENODEV, "Failed to match part id %d\n", whoami);

	return inv_icm45600_core_probe(regmap, i3c_chip_info[chip], false, NULL);
}

static struct i3c_driver inv_icm45600_driver = {
	.driver = {
		.name = "inv_icm45600_i3c",
		.pm = pm_sleep_ptr(&inv_icm45600_pm_ops),
	},
	.probe = inv_icm45600_i3c_probe,
	.id_table = inv_icm45600_i3c_ids,
};
module_i3c_driver(inv_icm45600_driver);

MODULE_AUTHOR("Remi Buisson <remi.buisson@tdk.com>");
MODULE_DESCRIPTION("InvenSense ICM-456xx i3c driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_ICM45600");
