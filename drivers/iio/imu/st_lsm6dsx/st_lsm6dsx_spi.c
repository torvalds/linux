// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsx spi driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "st_lsm6dsx.h"

static const struct regmap_config st_lsm6dsx_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_lsm6dsx_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	int hw_id = id->driver_data;
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &st_lsm6dsx_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_lsm6dsx_probe(&spi->dev, spi->irq, hw_id, regmap);
}

static const struct of_device_id st_lsm6dsx_spi_of_match[] = {
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
	{},
};
MODULE_DEVICE_TABLE(of, st_lsm6dsx_spi_of_match);

static const struct spi_device_id st_lsm6dsx_spi_id_table[] = {
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
	{},
};
MODULE_DEVICE_TABLE(spi, st_lsm6dsx_spi_id_table);

static struct spi_driver st_lsm6dsx_driver = {
	.driver = {
		.name = "st_lsm6dsx_spi",
		.pm = &st_lsm6dsx_pm_ops,
		.of_match_table = st_lsm6dsx_spi_of_match,
	},
	.probe = st_lsm6dsx_spi_probe,
	.id_table = st_lsm6dsx_spi_id_table,
};
module_spi_driver(st_lsm6dsx_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsx spi driver");
MODULE_LICENSE("GPL v2");
