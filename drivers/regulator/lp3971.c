/*
 * Regulator driver for National Semiconductors LP3971 PMIC chip
 *
 *  Copyright (C) 2009 Samsung Electronics
 *  Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Based on wm8350.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/lp3971.h>
#include <linux/slab.h>

struct lp3971 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
};

static u8 lp3971_reg_read(struct lp3971 *lp3971, u8 reg);
static int lp3971_set_bits(struct lp3971 *lp3971, u8 reg, u16 mask, u16 val);

#define LP3971_SYS_CONTROL1_REG 0x07

/* System control register 1 initial value,
   bits 4 and 5 are EPROM programmable */
#define SYS_CONTROL1_INIT_VAL 0x40
#define SYS_CONTROL1_INIT_MASK 0xCF

#define LP3971_BUCK_VOL_ENABLE_REG 0x10
#define LP3971_BUCK_VOL_CHANGE_REG 0x20

/*	Voltage control registers shift:
	LP3971_BUCK1 -> 0
	LP3971_BUCK2 -> 4
	LP3971_BUCK3 -> 6
*/
#define BUCK_VOL_CHANGE_SHIFT(x) (((!!x) << 2) | (x & ~0x01))
#define BUCK_VOL_CHANGE_FLAG_GO 0x01
#define BUCK_VOL_CHANGE_FLAG_TARGET 0x02
#define BUCK_VOL_CHANGE_FLAG_MASK 0x03

#define LP3971_BUCK1_BASE 0x23
#define LP3971_BUCK2_BASE 0x29
#define LP3971_BUCK3_BASE 0x32

static const int buck_base_addr[] = {
	LP3971_BUCK1_BASE,
	LP3971_BUCK2_BASE,
	LP3971_BUCK3_BASE,
};

#define LP3971_BUCK_TARGET_VOL1_REG(x) (buck_base_addr[x])
#define LP3971_BUCK_TARGET_VOL2_REG(x) (buck_base_addr[x]+1)

static const unsigned int buck_voltage_map[] = {
	      0,  800000,  850000,  900000,  950000, 1000000, 1050000, 1100000,
	1150000, 1200000, 1250000, 1300000, 1350000, 1400000, 1450000, 1500000,
	1550000, 1600000, 1650000, 1700000, 1800000, 1900000, 2500000, 2800000,
	3000000, 3300000,
};

#define BUCK_TARGET_VOL_MASK 0x3f

#define LP3971_BUCK_RAMP_REG(x)	(buck_base_addr[x]+2)

#define LP3971_LDO_ENABLE_REG 0x12
#define LP3971_LDO_VOL_CONTR_BASE 0x39

/*	Voltage control registers:
	LP3971_LDO1 -> LP3971_LDO_VOL_CONTR_BASE + 0
	LP3971_LDO2 -> LP3971_LDO_VOL_CONTR_BASE + 0
	LP3971_LDO3 -> LP3971_LDO_VOL_CONTR_BASE + 1
	LP3971_LDO4 -> LP3971_LDO_VOL_CONTR_BASE + 1
	LP3971_LDO5 -> LP3971_LDO_VOL_CONTR_BASE + 2
*/
#define LP3971_LDO_VOL_CONTR_REG(x)	(LP3971_LDO_VOL_CONTR_BASE + (x >> 1))

/*	Voltage control registers shift:
	LP3971_LDO1 -> 0, LP3971_LDO2 -> 4
	LP3971_LDO3 -> 0, LP3971_LDO4 -> 4
	LP3971_LDO5 -> 0
*/
#define LDO_VOL_CONTR_SHIFT(x) ((x & 1) << 2)
#define LDO_VOL_CONTR_MASK 0x0f

static const unsigned int ldo45_voltage_map[] = {
	1000000, 1050000, 1100000, 1150000, 1200000, 1250000, 1300000, 1350000,
	1400000, 1500000, 1800000, 1900000, 2500000, 2800000, 3000000, 3300000,
};

static const unsigned int ldo123_voltage_map[] = {
	1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000,
	2600000, 2700000, 2800000, 2900000, 3000000, 3100000, 3200000, 3300000,
};

#define LDO_VOL_MIN_IDX 0x00
#define LDO_VOL_MAX_IDX 0x0f

static int lp3971_ldo_is_enabled(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3971_LDO1;
	u16 mask = 1 << (1 + ldo);
	u16 val;

	val = lp3971_reg_read(lp3971, LP3971_LDO_ENABLE_REG);
	return (val & mask) != 0;
}

static int lp3971_ldo_enable(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3971_LDO1;
	u16 mask = 1 << (1 + ldo);

	return lp3971_set_bits(lp3971, LP3971_LDO_ENABLE_REG, mask, mask);
}

static int lp3971_ldo_disable(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3971_LDO1;
	u16 mask = 1 << (1 + ldo);

	return lp3971_set_bits(lp3971, LP3971_LDO_ENABLE_REG, mask, 0);
}

static int lp3971_ldo_get_voltage_sel(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3971_LDO1;
	u16 val, reg;

	reg = lp3971_reg_read(lp3971, LP3971_LDO_VOL_CONTR_REG(ldo));
	val = (reg >> LDO_VOL_CONTR_SHIFT(ldo)) & LDO_VOL_CONTR_MASK;

	return val;
}

static int lp3971_ldo_set_voltage_sel(struct regulator_dev *dev,
				      unsigned int selector)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3971_LDO1;

	return lp3971_set_bits(lp3971, LP3971_LDO_VOL_CONTR_REG(ldo),
			LDO_VOL_CONTR_MASK << LDO_VOL_CONTR_SHIFT(ldo),
			selector << LDO_VOL_CONTR_SHIFT(ldo));
}

static const struct regulator_ops lp3971_ldo_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.is_enabled = lp3971_ldo_is_enabled,
	.enable = lp3971_ldo_enable,
	.disable = lp3971_ldo_disable,
	.get_voltage_sel = lp3971_ldo_get_voltage_sel,
	.set_voltage_sel = lp3971_ldo_set_voltage_sel,
};

static int lp3971_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3971_DCDC1;
	u16 mask = 1 << (buck * 2);
	u16 val;

	val = lp3971_reg_read(lp3971, LP3971_BUCK_VOL_ENABLE_REG);
	return (val & mask) != 0;
}

static int lp3971_dcdc_enable(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3971_DCDC1;
	u16 mask = 1 << (buck * 2);

	return lp3971_set_bits(lp3971, LP3971_BUCK_VOL_ENABLE_REG, mask, mask);
}

static int lp3971_dcdc_disable(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3971_DCDC1;
	u16 mask = 1 << (buck * 2);

	return lp3971_set_bits(lp3971, LP3971_BUCK_VOL_ENABLE_REG, mask, 0);
}

static int lp3971_dcdc_get_voltage_sel(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3971_DCDC1;
	u16 reg;

	reg = lp3971_reg_read(lp3971, LP3971_BUCK_TARGET_VOL1_REG(buck));
	reg &= BUCK_TARGET_VOL_MASK;

	return reg;
}

static int lp3971_dcdc_set_voltage_sel(struct regulator_dev *dev,
				       unsigned int selector)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3971_DCDC1;
	int ret;

	ret = lp3971_set_bits(lp3971, LP3971_BUCK_TARGET_VOL1_REG(buck),
	       BUCK_TARGET_VOL_MASK, selector);
	if (ret)
		return ret;

	ret = lp3971_set_bits(lp3971, LP3971_BUCK_VOL_CHANGE_REG,
	       BUCK_VOL_CHANGE_FLAG_MASK << BUCK_VOL_CHANGE_SHIFT(buck),
	       BUCK_VOL_CHANGE_FLAG_GO << BUCK_VOL_CHANGE_SHIFT(buck));
	if (ret)
		return ret;

	return lp3971_set_bits(lp3971, LP3971_BUCK_VOL_CHANGE_REG,
	       BUCK_VOL_CHANGE_FLAG_MASK << BUCK_VOL_CHANGE_SHIFT(buck),
	       0 << BUCK_VOL_CHANGE_SHIFT(buck));
}

static const struct regulator_ops lp3971_dcdc_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.is_enabled = lp3971_dcdc_is_enabled,
	.enable = lp3971_dcdc_enable,
	.disable = lp3971_dcdc_disable,
	.get_voltage_sel = lp3971_dcdc_get_voltage_sel,
	.set_voltage_sel = lp3971_dcdc_set_voltage_sel,
};

static const struct regulator_desc regulators[] = {
	{
		.name = "LDO1",
		.id = LP3971_LDO1,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo123_voltage_map),
		.volt_table = ldo123_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO2",
		.id = LP3971_LDO2,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo123_voltage_map),
		.volt_table = ldo123_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO3",
		.id = LP3971_LDO3,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo123_voltage_map),
		.volt_table = ldo123_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO4",
		.id = LP3971_LDO4,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo45_voltage_map),
		.volt_table = ldo45_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO5",
		.id = LP3971_LDO5,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo45_voltage_map),
		.volt_table = ldo45_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC1",
		.id = LP3971_DCDC1,
		.ops = &lp3971_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.volt_table = buck_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC2",
		.id = LP3971_DCDC2,
		.ops = &lp3971_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.volt_table = buck_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC3",
		.id = LP3971_DCDC3,
		.ops = &lp3971_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.volt_table = buck_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

static int lp3971_i2c_read(struct i2c_client *i2c, char reg, int count,
	u16 *dest)
{
	int ret;

	if (count != 1)
		return -EIO;
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret < 0)
		return ret;

	*dest = ret;
	return 0;
}

static int lp3971_i2c_write(struct i2c_client *i2c, char reg, int count,
	const u16 *src)
{
	if (count != 1)
		return -EIO;
	return i2c_smbus_write_byte_data(i2c, reg, *src);
}

static u8 lp3971_reg_read(struct lp3971 *lp3971, u8 reg)
{
	u16 val = 0;

	mutex_lock(&lp3971->io_lock);

	lp3971_i2c_read(lp3971->i2c, reg, 1, &val);

	dev_dbg(lp3971->dev, "reg read 0x%02x -> 0x%02x\n", (int)reg,
		(unsigned)val&0xff);

	mutex_unlock(&lp3971->io_lock);

	return val & 0xff;
}

static int lp3971_set_bits(struct lp3971 *lp3971, u8 reg, u16 mask, u16 val)
{
	u16 tmp;
	int ret;

	mutex_lock(&lp3971->io_lock);

	ret = lp3971_i2c_read(lp3971->i2c, reg, 1, &tmp);
	if (ret == 0) {
		tmp = (tmp & ~mask) | val;
		ret = lp3971_i2c_write(lp3971->i2c, reg, 1, &tmp);
		dev_dbg(lp3971->dev, "reg write 0x%02x -> 0x%02x\n", (int)reg,
			(unsigned)val&0xff);
	}
	mutex_unlock(&lp3971->io_lock);

	return ret;
}

static int setup_regulators(struct lp3971 *lp3971,
				      struct lp3971_platform_data *pdata)
{
	int i, err;

	/* Instantiate the regulators */
	for (i = 0; i < pdata->num_regulators; i++) {
		struct regulator_config config = { };
		struct lp3971_regulator_subdev *reg = &pdata->regulators[i];
		struct regulator_dev *rdev;

		config.dev = lp3971->dev;
		config.init_data = reg->initdata;
		config.driver_data = lp3971;

		rdev = devm_regulator_register(lp3971->dev,
					       &regulators[reg->id], &config);
		if (IS_ERR(rdev)) {
			err = PTR_ERR(rdev);
			dev_err(lp3971->dev, "regulator init failed: %d\n",
				err);
			return err;
		}
	}

	return 0;
}

static int lp3971_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct lp3971 *lp3971;
	struct lp3971_platform_data *pdata = dev_get_platdata(&i2c->dev);
	int ret;
	u16 val;

	if (!pdata) {
		dev_dbg(&i2c->dev, "No platform init data supplied\n");
		return -ENODEV;
	}

	lp3971 = devm_kzalloc(&i2c->dev, sizeof(struct lp3971), GFP_KERNEL);
	if (lp3971 == NULL)
		return -ENOMEM;

	lp3971->i2c = i2c;
	lp3971->dev = &i2c->dev;

	mutex_init(&lp3971->io_lock);

	/* Detect LP3971 */
	ret = lp3971_i2c_read(i2c, LP3971_SYS_CONTROL1_REG, 1, &val);
	if (ret == 0 && (val & SYS_CONTROL1_INIT_MASK) != SYS_CONTROL1_INIT_VAL)
		ret = -ENODEV;
	if (ret < 0) {
		dev_err(&i2c->dev, "failed to detect device\n");
		return ret;
	}

	ret = setup_regulators(lp3971, pdata);
	if (ret < 0)
		return ret;

	i2c_set_clientdata(i2c, lp3971);
	return 0;
}

static const struct i2c_device_id lp3971_i2c_id[] = {
	{ "lp3971", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp3971_i2c_id);

static struct i2c_driver lp3971_i2c_driver = {
	.driver = {
		.name = "LP3971",
	},
	.probe    = lp3971_i2c_probe,
	.id_table = lp3971_i2c_id,
};

module_i2c_driver(lp3971_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marek Szyprowski <m.szyprowski@samsung.com>");
MODULE_DESCRIPTION("LP3971 PMIC driver");
