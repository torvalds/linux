/*
 * max8998.c - Voltage regulator driver for the Maxim 8998
 *
 *  Copyright (C) 2009-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/max8998.h>
#include <linux/mfd/max8998-private.h>

struct max8998_data {
	struct device		*dev;
	struct max8998_dev	*iodev;
	int			num_regulators;
	struct regulator_dev	**rdev;
	u8                      buck1_vol[4]; /* voltages for selection */
	u8                      buck2_vol[2];
	unsigned int		buck1_idx; /* index to last changed voltage */
					   /* value in a set */
	unsigned int		buck2_idx;
};

struct voltage_map_desc {
	int min;
	int max;
	int step;
};

/* Voltage maps in uV*/
static const struct voltage_map_desc ldo23_voltage_map_desc = {
	.min = 800000,	.step = 50000,	.max = 1300000,
};
static const struct voltage_map_desc ldo456711_voltage_map_desc = {
	.min = 1600000,	.step = 100000,	.max = 3600000,
};
static const struct voltage_map_desc ldo8_voltage_map_desc = {
	.min = 3000000,	.step = 100000,	.max = 3600000,
};
static const struct voltage_map_desc ldo9_voltage_map_desc = {
	.min = 2800000,	.step = 100000,	.max = 3100000,
};
static const struct voltage_map_desc ldo10_voltage_map_desc = {
	.min = 950000,	.step = 50000,	.max = 1300000,
};
static const struct voltage_map_desc ldo1213_voltage_map_desc = {
	.min = 800000,	.step = 100000,	.max = 3300000,
};
static const struct voltage_map_desc ldo1415_voltage_map_desc = {
	.min = 1200000,	.step = 100000,	.max = 3300000,
};
static const struct voltage_map_desc ldo1617_voltage_map_desc = {
	.min = 1600000,	.step = 100000,	.max = 3600000,
};
static const struct voltage_map_desc buck12_voltage_map_desc = {
	.min = 750000,	.step = 25000,	.max = 1525000,
};
static const struct voltage_map_desc buck3_voltage_map_desc = {
	.min = 1600000,	.step = 100000,	.max = 3600000,
};
static const struct voltage_map_desc buck4_voltage_map_desc = {
	.min = 800000,	.step = 100000,	.max = 2300000,
};

static const struct voltage_map_desc *ldo_voltage_map[] = {
	NULL,
	NULL,
	&ldo23_voltage_map_desc,	/* LDO2 */
	&ldo23_voltage_map_desc,	/* LDO3 */
	&ldo456711_voltage_map_desc,	/* LDO4 */
	&ldo456711_voltage_map_desc,	/* LDO5 */
	&ldo456711_voltage_map_desc,	/* LDO6 */
	&ldo456711_voltage_map_desc,	/* LDO7 */
	&ldo8_voltage_map_desc,		/* LDO8 */
	&ldo9_voltage_map_desc,		/* LDO9 */
	&ldo10_voltage_map_desc,	/* LDO10 */
	&ldo456711_voltage_map_desc,	/* LDO11 */
	&ldo1213_voltage_map_desc,	/* LDO12 */
	&ldo1213_voltage_map_desc,	/* LDO13 */
	&ldo1415_voltage_map_desc,	/* LDO14 */
	&ldo1415_voltage_map_desc,	/* LDO15 */
	&ldo1617_voltage_map_desc,	/* LDO16 */
	&ldo1617_voltage_map_desc,	/* LDO17 */
	&buck12_voltage_map_desc,	/* BUCK1 */
	&buck12_voltage_map_desc,	/* BUCK2 */
	&buck3_voltage_map_desc,	/* BUCK3 */
	&buck4_voltage_map_desc,	/* BUCK4 */
};

static int max8998_get_enable_register(struct regulator_dev *rdev,
					int *reg, int *shift)
{
	int ldo = rdev_get_id(rdev);

	switch (ldo) {
	case MAX8998_LDO2 ... MAX8998_LDO5:
		*reg = MAX8998_REG_ONOFF1;
		*shift = 3 - (ldo - MAX8998_LDO2);
		break;
	case MAX8998_LDO6 ... MAX8998_LDO13:
		*reg = MAX8998_REG_ONOFF2;
		*shift = 7 - (ldo - MAX8998_LDO6);
		break;
	case MAX8998_LDO14 ... MAX8998_LDO17:
		*reg = MAX8998_REG_ONOFF3;
		*shift = 7 - (ldo - MAX8998_LDO14);
		break;
	case MAX8998_BUCK1 ... MAX8998_BUCK4:
		*reg = MAX8998_REG_ONOFF1;
		*shift = 7 - (ldo - MAX8998_BUCK1);
		break;
	case MAX8998_EN32KHZ_AP ... MAX8998_ENVICHG:
		*reg = MAX8998_REG_ONOFF4;
		*shift = 7 - (ldo - MAX8998_EN32KHZ_AP);
		break;
	case MAX8998_ESAFEOUT1 ... MAX8998_ESAFEOUT2:
		*reg = MAX8998_REG_CHGR2;
		*shift = 7 - (ldo - MAX8998_ESAFEOUT1);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max8998_ldo_is_enabled(struct regulator_dev *rdev)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int ret, reg, shift = 8;
	u8 val;

	ret = max8998_get_enable_register(rdev, &reg, &shift);
	if (ret)
		return ret;

	ret = max8998_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	return val & (1 << shift);
}

static int max8998_ldo_enable(struct regulator_dev *rdev)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int reg, shift = 8, ret;

	ret = max8998_get_enable_register(rdev, &reg, &shift);
	if (ret)
		return ret;

	return max8998_update_reg(i2c, reg, 1<<shift, 1<<shift);
}

static int max8998_ldo_disable(struct regulator_dev *rdev)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int reg, shift = 8, ret;

	ret = max8998_get_enable_register(rdev, &reg, &shift);
	if (ret)
		return ret;

	return max8998_update_reg(i2c, reg, 0, 1<<shift);
}

static int max8998_get_voltage_register(struct regulator_dev *rdev,
				int *_reg, int *_shift, int *_mask)
{
	int ldo = rdev_get_id(rdev);
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	int reg, shift = 0, mask = 0xff;

	switch (ldo) {
	case MAX8998_LDO2 ... MAX8998_LDO3:
		reg = MAX8998_REG_LDO2_LDO3;
		mask = 0xf;
		if (ldo == MAX8998_LDO2)
			shift = 4;
		else
			shift = 0;
		break;
	case MAX8998_LDO4 ... MAX8998_LDO7:
		reg = MAX8998_REG_LDO4 + (ldo - MAX8998_LDO4);
		break;
	case MAX8998_LDO8 ... MAX8998_LDO9:
		reg = MAX8998_REG_LDO8_LDO9;
		mask = 0xf;
		if (ldo == MAX8998_LDO8)
			shift = 4;
		else
			shift = 0;
		break;
	case MAX8998_LDO10 ... MAX8998_LDO11:
		reg = MAX8998_REG_LDO10_LDO11;
		if (ldo == MAX8998_LDO10) {
			shift = 5;
			mask = 0x7;
		} else {
			shift = 0;
			mask = 0x1f;
		}
		break;
	case MAX8998_LDO12 ... MAX8998_LDO17:
		reg = MAX8998_REG_LDO12 + (ldo - MAX8998_LDO12);
		break;
	case MAX8998_BUCK1:
		reg = MAX8998_REG_BUCK1_VOLTAGE1 + max8998->buck1_idx;
		break;
	case MAX8998_BUCK2:
		reg = MAX8998_REG_BUCK2_VOLTAGE1 + max8998->buck2_idx;
		break;
	case MAX8998_BUCK3:
		reg = MAX8998_REG_BUCK3;
		break;
	case MAX8998_BUCK4:
		reg = MAX8998_REG_BUCK4;
		break;
	default:
		return -EINVAL;
	}

	*_reg = reg;
	*_shift = shift;
	*_mask = mask;

	return 0;
}

static int max8998_get_voltage_sel(struct regulator_dev *rdev)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int reg, shift = 0, mask, ret;
	u8 val;

	ret = max8998_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	ret = max8998_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	val >>= shift;
	val &= mask;

	return val;
}

static int max8998_set_voltage_ldo_sel(struct regulator_dev *rdev,
				       unsigned selector)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int reg, shift = 0, mask, ret;

	ret = max8998_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	ret = max8998_update_reg(i2c, reg, selector<<shift, mask<<shift);

	return ret;
}

static inline void buck1_gpio_set(int gpio1, int gpio2, int v)
{
	gpio_set_value(gpio1, v & 0x1);
	gpio_set_value(gpio2, (v >> 1) & 0x1);
}

static inline void buck2_gpio_set(int gpio, int v)
{
	gpio_set_value(gpio, v & 0x1);
}

static int max8998_set_voltage_buck_sel(struct regulator_dev *rdev,
					unsigned selector)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct max8998_platform_data *pdata =
		dev_get_platdata(max8998->iodev->dev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int buck = rdev_get_id(rdev);
	int reg, shift = 0, mask, ret, j;
	static u8 buck1_last_val;

	ret = max8998_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	switch (buck) {
	case MAX8998_BUCK1:
		dev_dbg(max8998->dev,
			"BUCK1, selector:%d, buck1_vol1:%d, buck1_vol2:%d\n"
			"buck1_vol3:%d, buck1_vol4:%d\n",
			selector, max8998->buck1_vol[0], max8998->buck1_vol[1],
			max8998->buck1_vol[2], max8998->buck1_vol[3]);

		if (gpio_is_valid(pdata->buck1_set1) &&
		    gpio_is_valid(pdata->buck1_set2)) {

			/* check if requested voltage */
			/* value is already defined */
			for (j = 0; j < ARRAY_SIZE(max8998->buck1_vol); j++) {
				if (max8998->buck1_vol[j] == selector) {
					max8998->buck1_idx = j;
					buck1_gpio_set(pdata->buck1_set1,
						       pdata->buck1_set2, j);
					goto buck1_exit;
				}
			}

			if (pdata->buck_voltage_lock)
				return -EINVAL;

			/* no predefine regulator found */
			max8998->buck1_idx = (buck1_last_val % 2) + 2;
			dev_dbg(max8998->dev, "max8998->buck1_idx:%d\n",
				max8998->buck1_idx);
			max8998->buck1_vol[max8998->buck1_idx] = selector;
			ret = max8998_get_voltage_register(rdev, &reg,
							   &shift,
							   &mask);
			ret = max8998_write_reg(i2c, reg, selector);
			buck1_gpio_set(pdata->buck1_set1,
				       pdata->buck1_set2, max8998->buck1_idx);
			buck1_last_val++;
buck1_exit:
			dev_dbg(max8998->dev, "%s: SET1:%d, SET2:%d\n",
				i2c->name, gpio_get_value(pdata->buck1_set1),
				gpio_get_value(pdata->buck1_set2));
			break;
		} else {
			ret = max8998_write_reg(i2c, reg, selector);
		}
		break;

	case MAX8998_BUCK2:
		dev_dbg(max8998->dev,
			"BUCK2, selector:%d buck2_vol1:%d, buck2_vol2:%d\n",
			selector, max8998->buck2_vol[0], max8998->buck2_vol[1]);
		if (gpio_is_valid(pdata->buck2_set3)) {

			/* check if requested voltage */
			/* value is already defined */
			for (j = 0; j < ARRAY_SIZE(max8998->buck2_vol); j++) {
				if (max8998->buck2_vol[j] == selector) {
					max8998->buck2_idx = j;
					buck2_gpio_set(pdata->buck2_set3, j);
					goto buck2_exit;
				}
			}

			if (pdata->buck_voltage_lock)
				return -EINVAL;

			max8998_get_voltage_register(rdev,
					&reg, &shift, &mask);
			ret = max8998_write_reg(i2c, reg, selector);
			max8998->buck2_vol[max8998->buck2_idx] = selector;
			buck2_gpio_set(pdata->buck2_set3, max8998->buck2_idx);
buck2_exit:
			dev_dbg(max8998->dev, "%s: SET3:%d\n", i2c->name,
				gpio_get_value(pdata->buck2_set3));
		} else {
			ret = max8998_write_reg(i2c, reg, selector);
		}
		break;

	case MAX8998_BUCK3:
	case MAX8998_BUCK4:
		ret = max8998_update_reg(i2c, reg, selector<<shift,
					 mask<<shift);
		break;
	}

	return ret;
}

static int max8998_set_voltage_buck_time_sel(struct regulator_dev *rdev,
					     unsigned int old_selector,
					     unsigned int new_selector)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	const struct voltage_map_desc *desc;
	int buck = rdev_get_id(rdev);
	u8 val = 0;
	int difference, ret;

	if (buck < MAX8998_BUCK1 || buck > MAX8998_BUCK4)
		return -EINVAL;

	desc = ldo_voltage_map[buck];

	/* Voltage stabilization */
	ret = max8998_read_reg(i2c, MAX8998_REG_ONOFF4, &val);
	if (ret)
		return ret;

	/* lp3974 hasn't got ENRAMP bit - ramp is assumed as true */
	/* MAX8998 has ENRAMP bit implemented, so test it*/
	if (max8998->iodev->type == TYPE_MAX8998 && !(val & MAX8998_ENRAMP))
		return 0;

	difference = (new_selector - old_selector) * desc->step / 1000;
	if (difference > 0)
		return DIV_ROUND_UP(difference, (val & 0x0f) + 1);

	return 0;
}

static struct regulator_ops max8998_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= max8998_ldo_is_enabled,
	.enable			= max8998_ldo_enable,
	.disable		= max8998_ldo_disable,
	.get_voltage_sel	= max8998_get_voltage_sel,
	.set_voltage_sel	= max8998_set_voltage_ldo_sel,
};

static struct regulator_ops max8998_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= max8998_ldo_is_enabled,
	.enable			= max8998_ldo_enable,
	.disable		= max8998_ldo_disable,
	.get_voltage_sel	= max8998_get_voltage_sel,
	.set_voltage_sel	= max8998_set_voltage_buck_sel,
	.set_voltage_time_sel	= max8998_set_voltage_buck_time_sel,
};

static struct regulator_ops max8998_others_ops = {
	.is_enabled		= max8998_ldo_is_enabled,
	.enable			= max8998_ldo_enable,
	.disable		= max8998_ldo_disable,
};

static struct regulator_desc regulators[] = {
	{
		.name		= "LDO2",
		.id		= MAX8998_LDO2,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO3",
		.id		= MAX8998_LDO3,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO4",
		.id		= MAX8998_LDO4,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO5",
		.id		= MAX8998_LDO5,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO6",
		.id		= MAX8998_LDO6,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO7",
		.id		= MAX8998_LDO7,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO8",
		.id		= MAX8998_LDO8,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO9",
		.id		= MAX8998_LDO9,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO10",
		.id		= MAX8998_LDO10,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO11",
		.id		= MAX8998_LDO11,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO12",
		.id		= MAX8998_LDO12,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO13",
		.id		= MAX8998_LDO13,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO14",
		.id		= MAX8998_LDO14,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO15",
		.id		= MAX8998_LDO15,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO16",
		.id		= MAX8998_LDO16,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "LDO17",
		.id		= MAX8998_LDO17,
		.ops		= &max8998_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "BUCK1",
		.id		= MAX8998_BUCK1,
		.ops		= &max8998_buck_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "BUCK2",
		.id		= MAX8998_BUCK2,
		.ops		= &max8998_buck_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "BUCK3",
		.id		= MAX8998_BUCK3,
		.ops		= &max8998_buck_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "BUCK4",
		.id		= MAX8998_BUCK4,
		.ops		= &max8998_buck_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "EN32KHz-AP",
		.id		= MAX8998_EN32KHZ_AP,
		.ops		= &max8998_others_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "EN32KHz-CP",
		.id		= MAX8998_EN32KHZ_CP,
		.ops		= &max8998_others_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "ENVICHG",
		.id		= MAX8998_ENVICHG,
		.ops		= &max8998_others_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "ESAFEOUT1",
		.id		= MAX8998_ESAFEOUT1,
		.ops		= &max8998_others_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "ESAFEOUT2",
		.id		= MAX8998_ESAFEOUT2,
		.ops		= &max8998_others_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}
};

static int max8998_pmic_dt_parse_dvs_gpio(struct max8998_dev *iodev,
			struct max8998_platform_data *pdata,
			struct device_node *pmic_np)
{
	int gpio;

	gpio = of_get_named_gpio(pmic_np, "max8998,pmic-buck1-dvs-gpios", 0);
	if (!gpio_is_valid(gpio)) {
		dev_err(iodev->dev, "invalid buck1 gpio[0]: %d\n", gpio);
		return -EINVAL;
	}
	pdata->buck1_set1 = gpio;

	gpio = of_get_named_gpio(pmic_np, "max8998,pmic-buck1-dvs-gpios", 1);
	if (!gpio_is_valid(gpio)) {
		dev_err(iodev->dev, "invalid buck1 gpio[1]: %d\n", gpio);
		return -EINVAL;
	}
	pdata->buck1_set2 = gpio;

	gpio = of_get_named_gpio(pmic_np, "max8998,pmic-buck2-dvs-gpio", 0);
	if (!gpio_is_valid(gpio)) {
		dev_err(iodev->dev, "invalid buck 2 gpio: %d\n", gpio);
		return -EINVAL;
	}
	pdata->buck2_set3 = gpio;

	return 0;
}

static int max8998_pmic_dt_parse_pdata(struct max8998_dev *iodev,
					struct max8998_platform_data *pdata)
{
	struct device_node *pmic_np = iodev->dev->of_node;
	struct device_node *regulators_np, *reg_np;
	struct max8998_regulator_data *rdata;
	unsigned int i;
	int ret;

	regulators_np = of_get_child_by_name(pmic_np, "regulators");
	if (!regulators_np) {
		dev_err(iodev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	/* count the number of regulators to be supported in pmic */
	pdata->num_regulators = of_get_child_count(regulators_np);

	rdata = devm_kzalloc(iodev->dev, sizeof(*rdata) *
				pdata->num_regulators, GFP_KERNEL);
	if (!rdata)
		return -ENOMEM;

	pdata->regulators = rdata;
	for (i = 0; i < ARRAY_SIZE(regulators); ++i) {
		reg_np = of_get_child_by_name(regulators_np,
							regulators[i].name);
		if (!reg_np)
			continue;

		rdata->id = regulators[i].id;
		rdata->initdata = of_get_regulator_init_data(
							iodev->dev, reg_np);
		rdata->reg_node = reg_np;
		++rdata;
	}
	pdata->num_regulators = rdata - pdata->regulators;

	ret = max8998_pmic_dt_parse_dvs_gpio(iodev, pdata, pmic_np);
	if (ret)
		return -EINVAL;

	if (of_find_property(pmic_np, "max8998,pmic-buck-voltage-lock", NULL))
		pdata->buck_voltage_lock = true;

	ret = of_property_read_u32(pmic_np,
					"max8998,pmic-buck1-default-dvs-idx",
					&pdata->buck1_default_idx);
	if (!ret && pdata->buck1_default_idx >= 4) {
		pdata->buck1_default_idx = 0;
		dev_warn(iodev->dev, "invalid value for default dvs index, using 0 instead\n");
	}

	ret = of_property_read_u32(pmic_np,
					"max8998,pmic-buck2-default-dvs-idx",
					&pdata->buck2_default_idx);
	if (!ret && pdata->buck2_default_idx >= 2) {
		pdata->buck2_default_idx = 0;
		dev_warn(iodev->dev, "invalid value for default dvs index, using 0 instead\n");
	}

	ret = of_property_read_u32_array(pmic_np,
					"max8998,pmic-buck1-dvs-voltage",
					pdata->buck1_voltage,
					ARRAY_SIZE(pdata->buck1_voltage));
	if (ret) {
		dev_err(iodev->dev, "buck1 voltages not specified\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(pmic_np,
					"max8998,pmic-buck2-dvs-voltage",
					pdata->buck2_voltage,
					ARRAY_SIZE(pdata->buck2_voltage));
	if (ret) {
		dev_err(iodev->dev, "buck2 voltages not specified\n");
		return -EINVAL;
	}

	return 0;
}

static int max8998_pmic_probe(struct platform_device *pdev)
{
	struct max8998_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max8998_platform_data *pdata = iodev->pdata;
	struct regulator_config config = { };
	struct regulator_dev **rdev;
	struct max8998_data *max8998;
	struct i2c_client *i2c;
	int i, ret, size;
	unsigned int v;

	if (!pdata) {
		dev_err(pdev->dev.parent, "No platform init data supplied\n");
		return -ENODEV;
	}

	if (IS_ENABLED(CONFIG_OF) && iodev->dev->of_node) {
		ret = max8998_pmic_dt_parse_pdata(iodev, pdata);
		if (ret)
			return ret;
	}

	max8998 = devm_kzalloc(&pdev->dev, sizeof(struct max8998_data),
			       GFP_KERNEL);
	if (!max8998)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	max8998->rdev = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!max8998->rdev)
		return -ENOMEM;

	rdev = max8998->rdev;
	max8998->dev = &pdev->dev;
	max8998->iodev = iodev;
	max8998->num_regulators = pdata->num_regulators;
	platform_set_drvdata(pdev, max8998);
	i2c = max8998->iodev->i2c;

	max8998->buck1_idx = pdata->buck1_default_idx;
	max8998->buck2_idx = pdata->buck2_default_idx;

	/* NOTE: */
	/* For unused GPIO NOT marked as -1 (thereof equal to 0)  WARN_ON */
	/* will be displayed */

	/* Check if MAX8998 voltage selection GPIOs are defined */
	if (gpio_is_valid(pdata->buck1_set1) &&
	    gpio_is_valid(pdata->buck1_set2)) {
		/* Check if SET1 is not equal to 0 */
		if (!pdata->buck1_set1) {
			dev_err(&pdev->dev,
				"MAX8998 SET1 GPIO defined as 0 !\n");
			WARN_ON(!pdata->buck1_set1);
			ret = -EIO;
			goto err_out;
		}
		/* Check if SET2 is not equal to 0 */
		if (!pdata->buck1_set2) {
			dev_err(&pdev->dev,
				"MAX8998 SET2 GPIO defined as 0 !\n");
			WARN_ON(!pdata->buck1_set2);
			ret = -EIO;
			goto err_out;
		}

		gpio_request(pdata->buck1_set1, "MAX8998 BUCK1_SET1");
		gpio_direction_output(pdata->buck1_set1,
				      max8998->buck1_idx & 0x1);


		gpio_request(pdata->buck1_set2, "MAX8998 BUCK1_SET2");
		gpio_direction_output(pdata->buck1_set2,
				      (max8998->buck1_idx >> 1) & 0x1);

		/* Set predefined values for BUCK1 registers */
		for (v = 0; v < ARRAY_SIZE(pdata->buck1_voltage); ++v) {
			i = 0;
			while (buck12_voltage_map_desc.min +
			       buck12_voltage_map_desc.step*i
			       < pdata->buck1_voltage[v])
				i++;

			max8998->buck1_vol[v] = i;
			ret = max8998_write_reg(i2c,
					MAX8998_REG_BUCK1_VOLTAGE1 + v, i);
			if (ret)
				goto err_out;
		}
	}

	if (gpio_is_valid(pdata->buck2_set3)) {
		/* Check if SET3 is not equal to 0 */
		if (!pdata->buck2_set3) {
			dev_err(&pdev->dev,
				"MAX8998 SET3 GPIO defined as 0 !\n");
			WARN_ON(!pdata->buck2_set3);
			ret = -EIO;
			goto err_out;
		}
		gpio_request(pdata->buck2_set3, "MAX8998 BUCK2_SET3");
		gpio_direction_output(pdata->buck2_set3,
				      max8998->buck2_idx & 0x1);

		/* Set predefined values for BUCK2 registers */
		for (v = 0; v < ARRAY_SIZE(pdata->buck2_voltage); ++v) {
			i = 0;
			while (buck12_voltage_map_desc.min +
			       buck12_voltage_map_desc.step*i
			       < pdata->buck2_voltage[v])
				i++;

			max8998->buck2_vol[v] = i;
			ret = max8998_write_reg(i2c,
					MAX8998_REG_BUCK2_VOLTAGE1 + v, i);
			if (ret)
				goto err_out;
		}
	}

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct voltage_map_desc *desc;
		int id = pdata->regulators[i].id;
		int index = id - MAX8998_LDO2;

		desc = ldo_voltage_map[id];
		if (desc && regulators[index].ops != &max8998_others_ops) {
			int count = (desc->max - desc->min) / desc->step + 1;

			regulators[index].n_voltages = count;
			regulators[index].min_uV = desc->min;
			regulators[index].uV_step = desc->step;
		}

		config.dev = max8998->dev;
		config.of_node = pdata->regulators[i].reg_node;
		config.init_data = pdata->regulators[i].initdata;
		config.driver_data = max8998;

		rdev[i] = regulator_register(&regulators[index], &config);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(max8998->dev, "regulator %s init failed (%d)\n",
						regulators[index].name, ret);
			rdev[i] = NULL;
			goto err;
		}
	}


	return 0;
err:
	while (--i >= 0)
		regulator_unregister(rdev[i]);
err_out:
	return ret;
}

static int max8998_pmic_remove(struct platform_device *pdev)
{
	struct max8998_data *max8998 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max8998->rdev;
	int i;

	for (i = 0; i < max8998->num_regulators; i++)
		regulator_unregister(rdev[i]);
	return 0;
}

static const struct platform_device_id max8998_pmic_id[] = {
	{ "max8998-pmic", TYPE_MAX8998 },
	{ "lp3974-pmic", TYPE_LP3974 },
	{ }
};
MODULE_DEVICE_TABLE(platform, max8998_pmic_id);

static struct platform_driver max8998_pmic_driver = {
	.driver = {
		.name = "max8998-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max8998_pmic_probe,
	.remove = max8998_pmic_remove,
	.id_table = max8998_pmic_id,
};

static int __init max8998_pmic_init(void)
{
	return platform_driver_register(&max8998_pmic_driver);
}
subsys_initcall(max8998_pmic_init);

static void __exit max8998_pmic_cleanup(void)
{
	platform_driver_unregister(&max8998_pmic_driver);
}
module_exit(max8998_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 8998 voltage regulator driver");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_LICENSE("GPL");
