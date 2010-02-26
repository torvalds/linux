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
#include <linux/regulator/driver.h>
#include <linux/regulator/lp3971.h>

struct lp3971 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
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
#define BUCK_VOL_CHANGE_SHIFT(x) (((1 << x) & ~0x01) << 1)
#define BUCK_VOL_CHANGE_FLAG_GO 0x01
#define BUCK_VOL_CHANGE_FLAG_TARGET 0x02
#define BUCK_VOL_CHANGE_FLAG_MASK 0x03

#define LP3971_BUCK1_BASE 0x23
#define LP3971_BUCK2_BASE 0x29
#define LP3971_BUCK3_BASE 0x32

const static int buck_base_addr[] = {
	LP3971_BUCK1_BASE,
	LP3971_BUCK2_BASE,
	LP3971_BUCK3_BASE,
};

#define LP3971_BUCK_TARGET_VOL1_REG(x) (buck_base_addr[x])
#define LP3971_BUCK_TARGET_VOL2_REG(x) (buck_base_addr[x]+1)

const static int buck_voltage_map[] = {
	   0,  800,  850,  900,  950, 1000, 1050, 1100,
	1150, 1200, 1250, 1300, 1350, 1400, 1450, 1500,
	1550, 1600, 1650, 1700, 1800, 1900, 2500, 2800,
	3000, 3300,
};

#define BUCK_TARGET_VOL_MASK 0x3f
#define BUCK_TARGET_VOL_MIN_IDX 0x01
#define BUCK_TARGET_VOL_MAX_IDX 0x19

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

const static int ldo45_voltage_map[] = {
	1000, 1050, 1100, 1150, 1200, 1250, 1300, 1350,
	1400, 1500, 1800, 1900, 2500, 2800, 3000, 3300,
};

const static int ldo123_voltage_map[] = {
	1800, 1900, 2000, 2100, 2200, 2300, 2400, 2500,
	2600, 2700, 2800, 2900, 3000, 3100, 3200, 3300,
};

const static int *ldo_voltage_map[] = {
	ldo123_voltage_map, /* LDO1 */
	ldo123_voltage_map, /* LDO2 */
	ldo123_voltage_map, /* LDO3 */
	ldo45_voltage_map, /* LDO4 */
	ldo45_voltage_map, /* LDO5 */
};

#define LDO_VOL_VALUE_MAP(x) (ldo_voltage_map[(x - LP3971_LDO1)])

#define LDO_VOL_MIN_IDX 0x00
#define LDO_VOL_MAX_IDX 0x0f

static int lp3971_ldo_list_voltage(struct regulator_dev *dev, unsigned index)
{
	int ldo = rdev_get_id(dev) - LP3971_LDO1;
	return 1000 * LDO_VOL_VALUE_MAP(ldo)[index];
}

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

static int lp3971_ldo_get_voltage(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3971_LDO1;
	u16 val, reg;

	reg = lp3971_reg_read(lp3971, LP3971_LDO_VOL_CONTR_REG(ldo));
	val = (reg >> LDO_VOL_CONTR_SHIFT(ldo)) & LDO_VOL_CONTR_MASK;

	return 1000 * LDO_VOL_VALUE_MAP(ldo)[val];
}

static int lp3971_ldo_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3971_LDO1;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map = LDO_VOL_VALUE_MAP(ldo);
	u16 val;

	if (min_vol < vol_map[LDO_VOL_MIN_IDX] ||
	    min_vol > vol_map[LDO_VOL_MAX_IDX])
		return -EINVAL;

	for (val = LDO_VOL_MIN_IDX; val <= LDO_VOL_MAX_IDX; val++)
		if (vol_map[val] >= min_vol)
			break;

	if (val > LDO_VOL_MAX_IDX || vol_map[val] > max_vol)
		return -EINVAL;

	return lp3971_set_bits(lp3971, LP3971_LDO_VOL_CONTR_REG(ldo),
		LDO_VOL_CONTR_MASK << LDO_VOL_CONTR_SHIFT(ldo), val);
}

static struct regulator_ops lp3971_ldo_ops = {
	.list_voltage = lp3971_ldo_list_voltage,
	.is_enabled = lp3971_ldo_is_enabled,
	.enable = lp3971_ldo_enable,
	.disable = lp3971_ldo_disable,
	.get_voltage = lp3971_ldo_get_voltage,
	.set_voltage = lp3971_ldo_set_voltage,
};

static int lp3971_dcdc_list_voltage(struct regulator_dev *dev, unsigned index)
{
	return 1000 * buck_voltage_map[index];
}

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

static int lp3971_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3971_DCDC1;
	u16 reg;
	int val;

	reg = lp3971_reg_read(lp3971, LP3971_BUCK_TARGET_VOL1_REG(buck));
	reg &= BUCK_TARGET_VOL_MASK;

	if (reg <= BUCK_TARGET_VOL_MAX_IDX)
		val = 1000 * buck_voltage_map[reg];
	else {
		val = 0;
		dev_warn(&dev->dev, "chip reported incorrect voltage value.\n");
	}

	return val;
}

static int lp3971_dcdc_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV)
{
	struct lp3971 *lp3971 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3971_DCDC1;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret;

	if (min_vol < vol_map[BUCK_TARGET_VOL_MIN_IDX] ||
	    min_vol > vol_map[BUCK_TARGET_VOL_MAX_IDX])
		return -EINVAL;

	for (val = BUCK_TARGET_VOL_MIN_IDX; val <= BUCK_TARGET_VOL_MAX_IDX;
	     val++)
		if (vol_map[val] >= min_vol)
			break;

	if (val > BUCK_TARGET_VOL_MAX_IDX || vol_map[val] > max_vol)
		return -EINVAL;

	ret = lp3971_set_bits(lp3971, LP3971_BUCK_TARGET_VOL1_REG(buck),
	       BUCK_TARGET_VOL_MASK, val);
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

static struct regulator_ops lp3971_dcdc_ops = {
	.list_voltage = lp3971_dcdc_list_voltage,
	.is_enabled = lp3971_dcdc_is_enabled,
	.enable = lp3971_dcdc_enable,
	.disable = lp3971_dcdc_disable,
	.get_voltage = lp3971_dcdc_get_voltage,
	.set_voltage = lp3971_dcdc_set_voltage,
};

static struct regulator_desc regulators[] = {
	{
		.name = "LDO1",
		.id = LP3971_LDO1,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo123_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO2",
		.id = LP3971_LDO2,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo123_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO3",
		.id = LP3971_LDO3,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo123_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO4",
		.id = LP3971_LDO4,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo45_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO5",
		.id = LP3971_LDO5,
		.ops = &lp3971_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo45_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC1",
		.id = LP3971_DCDC1,
		.ops = &lp3971_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC2",
		.id = LP3971_DCDC2,
		.ops = &lp3971_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC3",
		.id = LP3971_DCDC3,
		.ops = &lp3971_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
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
	if (ret < 0 || count != 1)
		return -EIO;

	*dest = ret;
	return 0;
}

static int lp3971_i2c_write(struct i2c_client *i2c, char reg, int count,
	const u16 *src)
{
	int ret;

	if (count != 1)
		return -EIO;
	ret = i2c_smbus_write_byte_data(i2c, reg, *src);
	if (ret >= 0)
		return 0;

	return ret;
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
	tmp = (tmp & ~mask) | val;
	if (ret == 0) {
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
	int num_regulators = pdata->num_regulators;
	lp3971->num_regulators = num_regulators;
	lp3971->rdev = kzalloc(sizeof(struct regulator_dev *) * num_regulators,
		GFP_KERNEL);

	/* Instantiate the regulators */
	for (i = 0; i < num_regulators; i++) {
		int id = pdata->regulators[i].id;
		lp3971->rdev[i] = regulator_register(&regulators[id],
			lp3971->dev, pdata->regulators[i].initdata, lp3971);

		if (IS_ERR(lp3971->rdev[i])) {
			err = PTR_ERR(lp3971->rdev[i]);
			dev_err(lp3971->dev, "regulator init failed: %d\n",
				err);
			goto error;
		}
	}

	return 0;
error:
	for (i = 0; i < num_regulators; i++)
		if (lp3971->rdev[i])
			regulator_unregister(lp3971->rdev[i]);
	kfree(lp3971->rdev);
	lp3971->rdev = NULL;
	return err;
}

static int __devinit lp3971_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct lp3971 *lp3971;
	struct lp3971_platform_data *pdata = i2c->dev.platform_data;
	int ret;
	u16 val;

	lp3971 = kzalloc(sizeof(struct lp3971), GFP_KERNEL);
	if (lp3971 == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	lp3971->i2c = i2c;
	lp3971->dev = &i2c->dev;
	i2c_set_clientdata(i2c, lp3971);

	mutex_init(&lp3971->io_lock);

	/* Detect LP3971 */
	ret = lp3971_i2c_read(i2c, LP3971_SYS_CONTROL1_REG, 1, &val);
	if (ret == 0 && (val & SYS_CONTROL1_INIT_MASK) != SYS_CONTROL1_INIT_VAL)
		ret = -ENODEV;
	if (ret < 0) {
		dev_err(&i2c->dev, "failed to detect device\n");
		goto err_detect;
	}

	if (pdata) {
		ret = setup_regulators(lp3971, pdata);
		if (ret < 0)
			goto err_detect;
	} else
		dev_warn(lp3971->dev, "No platform init data supplied\n");

	return 0;

err_detect:
	i2c_set_clientdata(i2c, NULL);
	kfree(lp3971);
err:
	return ret;
}

static int __devexit lp3971_i2c_remove(struct i2c_client *i2c)
{
	struct lp3971 *lp3971 = i2c_get_clientdata(i2c);
	int i;
	for (i = 0; i < lp3971->num_regulators; i++)
		if (lp3971->rdev[i])
			regulator_unregister(lp3971->rdev[i]);
	kfree(lp3971->rdev);
	i2c_set_clientdata(i2c, NULL);
	kfree(lp3971);

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
		.owner = THIS_MODULE,
	},
	.probe    = lp3971_i2c_probe,
	.remove   = __devexit_p(lp3971_i2c_remove),
	.id_table = lp3971_i2c_id,
};

static int __init lp3971_module_init(void)
{
	int ret;

	ret = i2c_add_driver(&lp3971_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);

	return ret;
}
module_init(lp3971_module_init);

static void __exit lp3971_module_exit(void)
{
	i2c_del_driver(&lp3971_i2c_driver);
}
module_exit(lp3971_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marek Szyprowski <m.szyprowski@samsung.com>");
MODULE_DESCRIPTION("LP3971 PMIC driver");
