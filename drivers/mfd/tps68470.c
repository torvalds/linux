// SPDX-License-Identifier: GPL-2.0
/*
 * TPS68470 chip Parent driver
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Authors:
 *	Rajmohan Mani <rajmohan.mani@intel.com>
 *	Tianshu Qiu <tian.shu.qiu@intel.com>
 *	Jian Xu Zheng <jian.xu.zheng@intel.com>
 *	Yuning Pu <yuning.pu@intel.com>
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps68470.h>
#include <linux/regmap.h>

static const struct mfd_cell tps68470s[] = {
	{ .name = "tps68470-gpio" },
	{ .name = "tps68470_pmic_opregion" },
};

static const struct regmap_config tps68470_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TPS68470_REG_MAX,
};

static int tps68470_chip_init(struct device *dev, struct regmap *regmap)
{
	unsigned int version;
	int ret;

	/* Force software reset */
	ret = regmap_write(regmap, TPS68470_REG_RESET, TPS68470_REG_RESET_MASK);
	if (ret)
		return ret;

	ret = regmap_read(regmap, TPS68470_REG_REVID, &version);
	if (ret) {
		dev_err(dev, "Failed to read revision register: %d\n", ret);
		return ret;
	}

	dev_info(dev, "TPS68470 REVID: 0x%x\n", version);

	return 0;
}

static int tps68470_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &tps68470_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "devm_regmap_init_i2c Error %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	i2c_set_clientdata(client, regmap);

	ret = tps68470_chip_init(dev, regmap);
	if (ret < 0) {
		dev_err(dev, "TPS68470 Init Error %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, tps68470s,
			      ARRAY_SIZE(tps68470s), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(dev, "devm_mfd_add_devices failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct acpi_device_id tps68470_acpi_ids[] = {
	{"INT3472"},
	{},
};
MODULE_DEVICE_TABLE(acpi, tps68470_acpi_ids);

static struct i2c_driver tps68470_driver = {
	.driver = {
		   .name = "tps68470",
		   .acpi_match_table = tps68470_acpi_ids,
	},
	.probe_new = tps68470_probe,
};
builtin_i2c_driver(tps68470_driver);
