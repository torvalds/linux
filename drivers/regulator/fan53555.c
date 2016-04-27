/*
 * FAN53555 Fairchild Digitally Programmable TinyBuck Regulator Driver.
 *
 * Supported Part Numbers:
 * FAN53555UC00X/01X/03X/04X/05X
 *
 * Copyright (c) 2012 Marvell Technology Ltd.
 * Yunfan Zhang <yfzhang@marvell.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/param.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/regulator/fan53555.h>

/* Voltage setting */
#define FAN53555_VSEL0		0x00
#define FAN53555_VSEL1		0x01
/* Control register */
#define FAN53555_CONTROL	0x02
/* IC Type */
#define FAN53555_ID1		0x03
/* IC mask version */
#define FAN53555_ID2		0x04
/* Monitor register */
#define FAN53555_MONITOR	0x05

/* VSEL bit definitions */
#define VSEL_BUCK_EN	(1 << 7)
#define VSEL_MODE		(1 << 6)
#define VSEL_NSEL_MASK	0x3F
/* Chip ID and Verison */
#define DIE_ID		0x0F	/* ID1 */
#define DIE_REV		0x0F	/* ID2 */
/* Control bit definitions */
#define CTL_OUTPUT_DISCHG	(1 << 7)
#define CTL_SLEW_MASK		(0x7 << 4)
#define CTL_SLEW_SHIFT		4
#define CTL_RESET			(1 << 2)

#define FAN53555_NVOLTAGES	64	/* Numbers of voltages */

enum fan53555_vendor {
	FAN53555_VENDOR_FAIRCHILD = 0,
	FAN53555_VENDOR_SILERGY,
};

/* IC Type */
enum {
	FAN53555_CHIP_ID_00 = 0,
	FAN53555_CHIP_ID_01,
	FAN53555_CHIP_ID_02,
	FAN53555_CHIP_ID_03,
	FAN53555_CHIP_ID_04,
	FAN53555_CHIP_ID_05,
};

enum {
	SILERGY_SYR82X = 8,
};

struct fan53555_device_info {
	enum fan53555_vendor vendor;
	struct regmap *regmap;
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_dev *rdev;
	struct regulator_init_data *regulator;
	/* IC Type and Rev */
	int chip_id;
	int chip_rev;
	/* Voltage setting register */
	unsigned int vol_reg;
	unsigned int sleep_reg;
	/* Voltage range and step(linear) */
	unsigned int vsel_min;
	unsigned int vsel_step;
	/* Voltage slew rate limiting */
	unsigned int slew_rate;
	/* Sleep voltage cache */
	unsigned int sleep_vol_cache;
};

static int fan53555_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct fan53555_device_info *di = rdev_get_drvdata(rdev);
	int ret;

	if (di->sleep_vol_cache == uV)
		return 0;
	ret = regulator_map_voltage_linear(rdev, uV, uV);
	if (ret < 0)
		return ret;
	ret = regmap_update_bits(di->regmap, di->sleep_reg,
					VSEL_NSEL_MASK, ret);
	if (ret < 0)
		return ret;
	/* Cache the sleep voltage setting.
	 * Might not be the real voltage which is rounded */
	di->sleep_vol_cache = uV;

	return 0;
}

static int fan53555_set_suspend_enable(struct regulator_dev *rdev)
{
	struct fan53555_device_info *di = rdev_get_drvdata(rdev);

	return regmap_update_bits(di->regmap, di->sleep_reg,
				  VSEL_BUCK_EN, VSEL_BUCK_EN);
}

static int fan53555_set_suspend_disable(struct regulator_dev *rdev)
{
	struct fan53555_device_info *di = rdev_get_drvdata(rdev);

	return regmap_update_bits(di->regmap, di->sleep_reg,
				  VSEL_BUCK_EN, 0);
}

static int fan53555_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct fan53555_device_info *di = rdev_get_drvdata(rdev);

	switch (mode) {
	case REGULATOR_MODE_FAST:
		regmap_update_bits(di->regmap, di->vol_reg,
				VSEL_MODE, VSEL_MODE);
		break;
	case REGULATOR_MODE_NORMAL:
		regmap_update_bits(di->regmap, di->vol_reg, VSEL_MODE, 0);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static unsigned int fan53555_get_mode(struct regulator_dev *rdev)
{
	struct fan53555_device_info *di = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret = 0;

	ret = regmap_read(di->regmap, di->vol_reg, &val);
	if (ret < 0)
		return ret;
	if (val & VSEL_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static const int slew_rates[] = {
	64000,
	32000,
	16000,
	 8000,
	 4000,
	 2000,
	 1000,
	  500,
};

static int fan53555_set_ramp(struct regulator_dev *rdev, int ramp)
{
	struct fan53555_device_info *di = rdev_get_drvdata(rdev);
	int regval = -1, i;

	for (i = 0; i < ARRAY_SIZE(slew_rates); i++) {
		if (ramp <= slew_rates[i])
			regval = i;
		else
			break;
	}

	if (regval < 0) {
		dev_err(di->dev, "unsupported ramp value %d\n", ramp);
		return -EINVAL;
	}

	return regmap_update_bits(di->regmap, FAN53555_CONTROL,
				  CTL_SLEW_MASK, regval << CTL_SLEW_SHIFT);
}

static struct regulator_ops fan53555_regulator_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.map_voltage = regulator_map_voltage_linear,
	.list_voltage = regulator_list_voltage_linear,
	.set_suspend_voltage = fan53555_set_suspend_voltage,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = fan53555_set_mode,
	.get_mode = fan53555_get_mode,
	.set_ramp_delay = fan53555_set_ramp,
	.set_suspend_enable = fan53555_set_suspend_enable,
	.set_suspend_disable = fan53555_set_suspend_disable,
};

static int fan53555_voltages_setup_fairchild(struct fan53555_device_info *di)
{
	/* Init voltage range and step */
	switch (di->chip_id) {
	case FAN53555_CHIP_ID_00:
	case FAN53555_CHIP_ID_01:
	case FAN53555_CHIP_ID_03:
	case FAN53555_CHIP_ID_05:
		di->vsel_min = 600000;
		di->vsel_step = 10000;
		break;
	case FAN53555_CHIP_ID_04:
		di->vsel_min = 603000;
		di->vsel_step = 12826;
		break;
	default:
		dev_err(di->dev,
			"Chip ID %d not supported!\n", di->chip_id);
		return -EINVAL;
	}

	return 0;
}

static int fan53555_voltages_setup_silergy(struct fan53555_device_info *di)
{
	/* Init voltage range and step */
	switch (di->chip_id) {
	case SILERGY_SYR82X:
		di->vsel_min = 712500;
		di->vsel_step = 12500;
		break;
	default:
		dev_err(di->dev,
			"Chip ID %d not supported!\n", di->chip_id);
		return -EINVAL;
	}

	return 0;
}

/* For 00,01,03,05 options:
 * VOUT = 0.60V + NSELx * 10mV, from 0.60 to 1.23V.
 * For 04 option:
 * VOUT = 0.603V + NSELx * 12.826mV, from 0.603 to 1.411V.
 * */
static int fan53555_device_setup(struct fan53555_device_info *di,
				struct fan53555_platform_data *pdata)
{
	int ret = 0;

	/* Setup voltage control register */
	switch (pdata->sleep_vsel_id) {
	case FAN53555_VSEL_ID_0:
		di->sleep_reg = FAN53555_VSEL0;
		di->vol_reg = FAN53555_VSEL1;
		break;
	case FAN53555_VSEL_ID_1:
		di->sleep_reg = FAN53555_VSEL1;
		di->vol_reg = FAN53555_VSEL0;
		break;
	default:
		dev_err(di->dev, "Invalid VSEL ID!\n");
		return -EINVAL;
	}

	switch (di->vendor) {
	case FAN53555_VENDOR_FAIRCHILD:
		ret = fan53555_voltages_setup_fairchild(di);
		break;
	case FAN53555_VENDOR_SILERGY:
		ret = fan53555_voltages_setup_silergy(di);
		break;
	default:
		dev_err(di->dev, "vendor %d not supported!\n", di->vendor);
		return -EINVAL;
	}

	return ret;
}

static int fan53555_regulator_register(struct fan53555_device_info *di,
			struct regulator_config *config)
{
	struct regulator_desc *rdesc = &di->desc;

	rdesc->name = "fan53555-reg";
	rdesc->supply_name = "vin";
	rdesc->ops = &fan53555_regulator_ops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->n_voltages = FAN53555_NVOLTAGES;
	rdesc->enable_reg = di->vol_reg;
	rdesc->enable_mask = VSEL_BUCK_EN;
	rdesc->min_uV = di->vsel_min;
	rdesc->uV_step = di->vsel_step;
	rdesc->vsel_reg = di->vol_reg;
	rdesc->vsel_mask = VSEL_NSEL_MASK;
	rdesc->owner = THIS_MODULE;

	di->rdev = devm_regulator_register(di->dev, &di->desc, config);
	return PTR_ERR_OR_ZERO(di->rdev);
}

static const struct regmap_config fan53555_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static struct fan53555_platform_data *fan53555_parse_dt(struct device *dev,
					      struct device_node *np,
					      const struct regulator_desc *desc)
{
	struct fan53555_platform_data *pdata;
	int ret;
	u32 tmp;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->regulator = of_get_regulator_init_data(dev, np, desc);

	ret = of_property_read_u32(np, "fcs,suspend-voltage-selector",
				   &tmp);
	if (!ret)
		pdata->sleep_vsel_id = tmp;

	return pdata;
}

static const struct of_device_id fan53555_dt_ids[] = {
	{
		.compatible = "fcs,fan53555",
		.data = (void *)FAN53555_VENDOR_FAIRCHILD
	}, {
		.compatible = "silergy,syr827",
		.data = (void *)FAN53555_VENDOR_SILERGY,
	}, {
		.compatible = "silergy,syr828",
		.data = (void *)FAN53555_VENDOR_SILERGY,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, fan53555_dt_ids);

static int fan53555_regulator_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct fan53555_device_info *di;
	struct fan53555_platform_data *pdata;
	struct regulator_config config = { };
	unsigned int val;
	int ret;

	di = devm_kzalloc(&client->dev, sizeof(struct fan53555_device_info),
					GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	pdata = dev_get_platdata(&client->dev);
	if (!pdata)
		pdata = fan53555_parse_dt(&client->dev, np, &di->desc);

	if (!pdata || !pdata->regulator) {
		dev_err(&client->dev, "Platform data not found!\n");
		return -ENODEV;
	}

	di->regulator = pdata->regulator;
	if (client->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_device(of_match_ptr(fan53555_dt_ids),
					&client->dev);
		if (!match)
			return -ENODEV;

		di->vendor = (unsigned long) match->data;
	} else {
		/* if no ramp constraint set, get the pdata ramp_delay */
		if (!di->regulator->constraints.ramp_delay) {
			int slew_idx = (pdata->slew_rate & 0x7)
						? pdata->slew_rate : 0;

			di->regulator->constraints.ramp_delay
						= slew_rates[slew_idx];
		}

		di->vendor = id->driver_data;
	}

	di->regmap = devm_regmap_init_i2c(client, &fan53555_regmap_config);
	if (IS_ERR(di->regmap)) {
		dev_err(&client->dev, "Failed to allocate regmap!\n");
		return PTR_ERR(di->regmap);
	}
	di->dev = &client->dev;
	i2c_set_clientdata(client, di);
	/* Get chip ID */
	ret = regmap_read(di->regmap, FAN53555_ID1, &val);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to get chip ID!\n");
		return ret;
	}
	di->chip_id = val & DIE_ID;
	/* Get chip revision */
	ret = regmap_read(di->regmap, FAN53555_ID2, &val);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to get chip Rev!\n");
		return ret;
	}
	di->chip_rev = val & DIE_REV;
	dev_info(&client->dev, "FAN53555 Option[%d] Rev[%d] Detected!\n",
				di->chip_id, di->chip_rev);
	/* Device init */
	ret = fan53555_device_setup(di, pdata);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to setup device!\n");
		return ret;
	}
	/* Register regulator */
	config.dev = di->dev;
	config.init_data = di->regulator;
	config.regmap = di->regmap;
	config.driver_data = di;
	config.of_node = np;

	ret = fan53555_regulator_register(di, &config);
	if (ret < 0)
		dev_err(&client->dev, "Failed to register regulator!\n");
	return ret;

}

static const struct i2c_device_id fan53555_id[] = {
	{
		.name = "fan53555",
		.driver_data = FAN53555_VENDOR_FAIRCHILD
	}, {
		.name = "syr82x",
		.driver_data = FAN53555_VENDOR_SILERGY
	},
	{ },
};
MODULE_DEVICE_TABLE(i2c, fan53555_id);

static struct i2c_driver fan53555_regulator_driver = {
	.driver = {
		.name = "fan53555-regulator",
		.of_match_table = of_match_ptr(fan53555_dt_ids),
	},
	.probe = fan53555_regulator_probe,
	.id_table = fan53555_id,
};

module_i2c_driver(fan53555_regulator_driver);

MODULE_AUTHOR("Yunfan Zhang <yfzhang@marvell.com>");
MODULE_DESCRIPTION("FAN53555 regulator driver");
MODULE_LICENSE("GPL v2");
