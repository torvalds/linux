// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>
#include <linux/regmap.h>

#include "bmi270.h"

static const struct regmap_config bmi270_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int bmi270_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;
	struct device *dev = &client->dev;
	const struct bmi270_chip_info *chip_info;

	chip_info = i2c_get_match_data(client);
	if (!chip_info)
		return -ENODEV;

	regmap = devm_regmap_init_i2c(client, &bmi270_i2c_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to init i2c regmap");

	return bmi270_core_probe(dev, regmap, chip_info);
}

static const struct i2c_device_id bmi270_i2c_id[] = {
	{ "bmi260", (kernel_ulong_t)&bmi260_chip_info },
	{ "bmi270", (kernel_ulong_t)&bmi270_chip_info },
	{ }
};

static const struct acpi_device_id bmi270_acpi_match[] = {
	/* GPD Win Mini, Aya Neo AIR Pro, OXP Mini Pro, etc. */
	{ "BMI0160",  (kernel_ulong_t)&bmi260_chip_info },
	/* GPD Win Max 2 2023(sincice BIOS v0.40), etc. */
	{ "BMI0260",  (kernel_ulong_t)&bmi260_chip_info },
	{ }
};

static const struct of_device_id bmi270_of_match[] = {
	{ .compatible = "bosch,bmi260", .data = &bmi260_chip_info },
	{ .compatible = "bosch,bmi270", .data = &bmi270_chip_info },
	{ }
};

static struct i2c_driver bmi270_i2c_driver = {
	.driver = {
		.name = "bmi270_i2c",
		.pm = pm_ptr(&bmi270_core_pm_ops),
		.acpi_match_table = bmi270_acpi_match,
		.of_match_table = bmi270_of_match,
	},
	.probe = bmi270_i2c_probe,
	.id_table = bmi270_i2c_id,
};
module_i2c_driver(bmi270_i2c_driver);

MODULE_AUTHOR("Alex Lanzano");
MODULE_DESCRIPTION("BMI270 driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BMI270");
