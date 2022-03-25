// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Starfive Technology Co., Ltd.
 * Author: Mason Huo <mason.huo@starfivetech.com>
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/stf7110.h>
#include <linux/slab.h>

#define STF7110_PM_POWER_SW_0		0x80
#define STF7110_PM_POWER_SW_1		0x81
#define ENABLE_MASK(id)			BIT(id)


static const struct regmap_config stf7110_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = STF7110_PM_POWER_SW_1,
	.cache_type = REGCACHE_FLAT,
};

static const struct regulator_ops stf7110_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

#define STF7110_LDO(_id, _name, en_reg, en_mask) \
{\
	.name = (_name),\
	.ops = &stf7110_ldo_ops,\
	.of_match = of_match_ptr(_name),\
	.regulators_node = of_match_ptr("regulators"),\
	.type = REGULATOR_VOLTAGE,\
	.id = STF7110_ID_##_id,\
	.owner = THIS_MODULE,\
	.enable_reg = STF7110_PM_POWER_SW_##en_reg,\
	.enable_mask = ENABLE_MASK(en_mask),\
}

static const struct regulator_desc stf7110_regulators[] = {
	STF7110_LDO(LDO_REG1, "hdmi_1p8", 0, 0),
	STF7110_LDO(LDO_REG2, "mipitx_1p8", 0, 1),
	STF7110_LDO(LDO_REG3, "mipirx_1p8", 0, 2),
	STF7110_LDO(LDO_REG4, "hdmi_0p9", 0, 3),
	STF7110_LDO(LDO_REG5, "mipitx_0p9", 0, 4),
	STF7110_LDO(LDO_REG6, "mipirx_0p9", 0, 5),
	STF7110_LDO(LDO_REG7, "sdio_vdd", 1, 0),
};

static int stf7110_i2c_probe(struct i2c_client *i2c)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regulator_init_data *init_data;
	struct regmap *regmap;
	int i, ret;

	regmap = devm_regmap_init_i2c(i2c, &stf7110_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	init_data = of_get_regulator_init_data(&i2c->dev, i2c->dev.of_node, NULL);
	if (!init_data)
		return -ENOMEM;
	config.init_data = init_data;

	for (i = 0; i < STF7110_MAX_REGULATORS; i++) {
		config.dev = &i2c->dev;
		config.regmap = regmap;

		rdev = devm_regulator_register(&i2c->dev,
			&stf7110_regulators[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&i2c->dev,
				"Failed to register STF7110 regulator\n");
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct i2c_device_id stf7110_i2c_id[] = {
	{"stf7110_evb_reg", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, stf7110_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id stf7110_dt_ids[] = {
	{ .compatible = "stf,jh7110-evb-regulator",
	  .data = &stf7110_i2c_id[0] },
	{},
};
MODULE_DEVICE_TABLE(of, stf7110_dt_ids);
#endif

static struct i2c_driver stf7110_regulator_driver = {
	.driver = {
		.name = "stf7110-evb-regulator",
		.of_match_table = of_match_ptr(stf7110_dt_ids),
	},
	.probe_new = stf7110_i2c_probe,
	.id_table = stf7110_i2c_id,
};

module_i2c_driver(stf7110_regulator_driver);

MODULE_AUTHOR("Mason Huo <mason.huo@starfivetech.com>");
MODULE_DESCRIPTION("Regulator device driver for Starfive STF7110");
MODULE_LICENSE("GPL v2");
