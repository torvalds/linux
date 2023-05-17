// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsx i2c driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "st_lsm6dsx.h"

static const struct regmap_config st_lsm6dsx_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_lsm6dsx_i2c_probe(struct i2c_client *client)
{
	int hw_id;
	struct regmap *regmap;

	hw_id = (kernel_ulong_t)device_get_match_data(&client->dev);
	if (!hw_id)
		hw_id = i2c_client_get_device_id(client)->driver_data;
	if (!hw_id)
		return -EINVAL;

	regmap = devm_regmap_init_i2c(client, &st_lsm6dsx_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_lsm6dsx_probe(&client->dev, client->irq, hw_id, regmap);
}

static const struct of_device_id st_lsm6dsx_i2c_of_match[] = {
	{
		.compatible = "st,lsm6ds3",
		.data = (void *)ST_LSM6DS3_ID,
	},
	{
		.compatible = "st,lsm6ds3h",
		.data = (void *)ST_LSM6DS3H_ID,
	},
	{
		.compatible = "st,lsm6dsl",
		.data = (void *)ST_LSM6DSL_ID,
	},
	{
		.compatible = "st,lsm6dsm",
		.data = (void *)ST_LSM6DSM_ID,
	},
	{
		.compatible = "st,ism330dlc",
		.data = (void *)ST_ISM330DLC_ID,
	},
	{
		.compatible = "st,lsm6dso",
		.data = (void *)ST_LSM6DSO_ID,
	},
	{
		.compatible = "st,asm330lhh",
		.data = (void *)ST_ASM330LHH_ID,
	},
	{
		.compatible = "st,lsm6dsox",
		.data = (void *)ST_LSM6DSOX_ID,
	},
	{
		.compatible = "st,lsm6dsr",
		.data = (void *)ST_LSM6DSR_ID,
	},
	{
		.compatible = "st,lsm6ds3tr-c",
		.data = (void *)ST_LSM6DS3TRC_ID,
	},
	{
		.compatible = "st,ism330dhcx",
		.data = (void *)ST_ISM330DHCX_ID,
	},
	{
		.compatible = "st,lsm9ds1-imu",
		.data = (void *)ST_LSM9DS1_ID,
	},
	{
		.compatible = "st,lsm6ds0",
		.data = (void *)ST_LSM6DS0_ID,
	},
	{
		.compatible = "st,lsm6dsrx",
		.data = (void *)ST_LSM6DSRX_ID,
	},
	{
		.compatible = "st,lsm6dst",
		.data = (void *)ST_LSM6DST_ID,
	},
	{
		.compatible = "st,lsm6dsop",
		.data = (void *)ST_LSM6DSOP_ID,
	},
	{
		.compatible = "st,asm330lhhx",
		.data = (void *)ST_ASM330LHHX_ID,
	},
	{
		.compatible = "st,lsm6dstx",
		.data = (void *)ST_LSM6DSTX_ID,
	},
	{
		.compatible = "st,lsm6dsv",
		.data = (void *)ST_LSM6DSV_ID,
	},
	{
		.compatible = "st,lsm6dsv16x",
		.data = (void *)ST_LSM6DSV16X_ID,
	},
	{
		.compatible = "st,lsm6dso16is",
		.data = (void *)ST_LSM6DSO16IS_ID,
	},
	{
		.compatible = "st,ism330is",
		.data = (void *)ST_ISM330IS_ID,
	},
	{
		.compatible = "st,asm330lhb",
		.data = (void *)ST_ASM330LHB_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lsm6dsx_i2c_of_match);

static const struct acpi_device_id st_lsm6dsx_i2c_acpi_match[] = {
	{ "SMO8B30", ST_LSM6DS3TRC_ID, },
	{}
};
MODULE_DEVICE_TABLE(acpi, st_lsm6dsx_i2c_acpi_match);

static const struct i2c_device_id st_lsm6dsx_i2c_id_table[] = {
	{ ST_LSM6DS3_DEV_NAME, ST_LSM6DS3_ID },
	{ ST_LSM6DS3H_DEV_NAME, ST_LSM6DS3H_ID },
	{ ST_LSM6DSL_DEV_NAME, ST_LSM6DSL_ID },
	{ ST_LSM6DSM_DEV_NAME, ST_LSM6DSM_ID },
	{ ST_ISM330DLC_DEV_NAME, ST_ISM330DLC_ID },
	{ ST_LSM6DSO_DEV_NAME, ST_LSM6DSO_ID },
	{ ST_ASM330LHH_DEV_NAME, ST_ASM330LHH_ID },
	{ ST_LSM6DSOX_DEV_NAME, ST_LSM6DSOX_ID },
	{ ST_LSM6DSR_DEV_NAME, ST_LSM6DSR_ID },
	{ ST_LSM6DS3TRC_DEV_NAME, ST_LSM6DS3TRC_ID },
	{ ST_ISM330DHCX_DEV_NAME, ST_ISM330DHCX_ID },
	{ ST_LSM9DS1_DEV_NAME, ST_LSM9DS1_ID },
	{ ST_LSM6DS0_DEV_NAME, ST_LSM6DS0_ID },
	{ ST_LSM6DSRX_DEV_NAME, ST_LSM6DSRX_ID },
	{ ST_LSM6DST_DEV_NAME, ST_LSM6DST_ID },
	{ ST_LSM6DSOP_DEV_NAME, ST_LSM6DSOP_ID },
	{ ST_ASM330LHHX_DEV_NAME, ST_ASM330LHHX_ID },
	{ ST_LSM6DSTX_DEV_NAME, ST_LSM6DSTX_ID },
	{ ST_LSM6DSV_DEV_NAME, ST_LSM6DSV_ID },
	{ ST_LSM6DSV16X_DEV_NAME, ST_LSM6DSV16X_ID },
	{ ST_LSM6DSO16IS_DEV_NAME, ST_LSM6DSO16IS_ID },
	{ ST_ISM330IS_DEV_NAME, ST_ISM330IS_ID },
	{ ST_ASM330LHB_DEV_NAME, ST_ASM330LHB_ID },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_lsm6dsx_i2c_id_table);

static struct i2c_driver st_lsm6dsx_driver = {
	.driver = {
		.name = "st_lsm6dsx_i2c",
		.pm = pm_sleep_ptr(&st_lsm6dsx_pm_ops),
		.of_match_table = st_lsm6dsx_i2c_of_match,
		.acpi_match_table = st_lsm6dsx_i2c_acpi_match,
	},
	.probe_new = st_lsm6dsx_i2c_probe,
	.id_table = st_lsm6dsx_i2c_id_table,
};
module_i2c_driver(st_lsm6dsx_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsx i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_LSM6DSX);
