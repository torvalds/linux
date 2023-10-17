// SPDX-License-Identifier: GPL-2.0+
/*
 * mpq7932.c - hwmon with optional regulator driver for mps mpq7932
 * Copyright 2022 Monolithic Power Systems, Inc
 *
 * Author: Saravanan Sekar <saravanan@linumiz.com>
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pmbus.h>
#include "pmbus.h"

#define MPQ7932_BUCK_UV_MIN		206250
#define MPQ7932_UV_STEP			6250
#define MPQ7932_N_VOLTAGES		256
#define MPQ7932_VOUT_MAX		0xFF
#define MPQ7932_NUM_PAGES		6

#define MPQ7932_TON_DELAY		0x60
#define MPQ7932_VOUT_STARTUP_SLEW	0xA3
#define MPQ7932_VOUT_SHUTDOWN_SLEW	0xA5
#define MPQ7932_VOUT_SLEW_MASK		GENMASK(1, 0)
#define MPQ7932_TON_DELAY_MASK		GENMASK(4, 0)

struct mpq7932_data {
	struct pmbus_driver_info info;
	struct pmbus_platform_data pdata;
};

#if IS_ENABLED(CONFIG_SENSORS_MPQ7932_REGULATOR)
static struct regulator_desc mpq7932_regulators_desc[] = {
	PMBUS_REGULATOR_STEP("buck", 0, MPQ7932_N_VOLTAGES,
			     MPQ7932_UV_STEP, MPQ7932_BUCK_UV_MIN),
	PMBUS_REGULATOR_STEP("buck", 1, MPQ7932_N_VOLTAGES,
			     MPQ7932_UV_STEP, MPQ7932_BUCK_UV_MIN),
	PMBUS_REGULATOR_STEP("buck", 2, MPQ7932_N_VOLTAGES,
			     MPQ7932_UV_STEP, MPQ7932_BUCK_UV_MIN),
	PMBUS_REGULATOR_STEP("buck", 3, MPQ7932_N_VOLTAGES,
			     MPQ7932_UV_STEP, MPQ7932_BUCK_UV_MIN),
	PMBUS_REGULATOR_STEP("buck", 4, MPQ7932_N_VOLTAGES,
			     MPQ7932_UV_STEP, MPQ7932_BUCK_UV_MIN),
	PMBUS_REGULATOR_STEP("buck", 5, MPQ7932_N_VOLTAGES,
			     MPQ7932_UV_STEP, MPQ7932_BUCK_UV_MIN),
};
#endif

static int mpq7932_write_word_data(struct i2c_client *client, int page, int reg,
				   u16 word)
{
	switch (reg) {
	/*
	 * chip supports only byte access for VOUT_COMMAND otherwise
	 * access results -EREMOTEIO
	 */
	case PMBUS_VOUT_COMMAND:
		return pmbus_write_byte_data(client, page, reg, word & 0xFF);

	default:
		return -ENODATA;
	}
}

static int mpq7932_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	switch (reg) {
	/*
	 * chip supports neither (PMBUS_VOUT_MARGIN_HIGH, PMBUS_VOUT_MARGIN_LOW)
	 * nor (PMBUS_MFR_VOUT_MIN, PMBUS_MFR_VOUT_MAX). As a result set voltage
	 * fails due to error in pmbus_regulator_get_low_margin, so faked.
	 */
	case PMBUS_MFR_VOUT_MIN:
		return 0;

	case PMBUS_MFR_VOUT_MAX:
		return MPQ7932_VOUT_MAX;

	/*
	 * chip supports only byte access for VOUT_COMMAND otherwise
	 * access results in -EREMOTEIO
	 */
	case PMBUS_READ_VOUT:
		return pmbus_read_byte_data(client, page, PMBUS_VOUT_COMMAND);

	default:
		return -ENODATA;
	}
}

static int mpq7932_probe(struct i2c_client *client)
{
	struct mpq7932_data *data;
	struct pmbus_driver_info *info;
	struct device *dev = &client->dev;
	int i;

	data = devm_kzalloc(dev, sizeof(struct mpq7932_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	info = &data->info;
	info->pages = MPQ7932_NUM_PAGES;
	info->format[PSC_VOLTAGE_OUT] = direct;
	info->m[PSC_VOLTAGE_OUT] = 160;
	info->b[PSC_VOLTAGE_OUT] = -33;
	for (i = 0; i < info->pages; i++) {
		info->func[i] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
				| PMBUS_HAVE_STATUS_TEMP;
	}

#if IS_ENABLED(CONFIG_SENSORS_MPQ7932_REGULATOR)
	info->num_regulators = ARRAY_SIZE(mpq7932_regulators_desc);
	info->reg_desc = mpq7932_regulators_desc;
#endif

	info->read_word_data = mpq7932_read_word_data;
	info->write_word_data = mpq7932_write_word_data;

	data->pdata.flags = PMBUS_NO_CAPABILITY;
	dev->platform_data = &data->pdata;

	return pmbus_do_probe(client, info);
}

static const struct of_device_id mpq7932_of_match[] = {
	{ .compatible = "mps,mpq7932"},
	{},
};
MODULE_DEVICE_TABLE(of, mpq7932_of_match);

static const struct i2c_device_id mpq7932_id[] = {
	{ "mpq7932", },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mpq7932_id);

static struct i2c_driver mpq7932_regulator_driver = {
	.driver = {
		.name = "mpq7932",
		.of_match_table = mpq7932_of_match,
	},
	.probe = mpq7932_probe,
	.id_table = mpq7932_id,
};
module_i2c_driver(mpq7932_regulator_driver);

MODULE_AUTHOR("Saravanan Sekar <saravanan@linumiz.com>");
MODULE_DESCRIPTION("MPQ7932 PMIC regulator driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
