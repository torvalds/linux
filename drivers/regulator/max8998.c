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
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
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

/* Voltage maps */
static const struct voltage_map_desc ldo23_voltage_map_desc = {
	.min = 800,	.step = 50,	.max = 1300,
};
static const struct voltage_map_desc ldo456711_voltage_map_desc = {
	.min = 1600,	.step = 100,	.max = 3600,
};
static const struct voltage_map_desc ldo8_voltage_map_desc = {
	.min = 3000,	.step = 100,	.max = 3600,
};
static const struct voltage_map_desc ldo9_voltage_map_desc = {
	.min = 2800,	.step = 100,	.max = 3100,
};
static const struct voltage_map_desc ldo10_voltage_map_desc = {
	.min = 950,	.step = 50,	.max = 1300,
};
static const struct voltage_map_desc ldo1213_voltage_map_desc = {
	.min = 800,	.step = 100,	.max = 3300,
};
static const struct voltage_map_desc ldo1415_voltage_map_desc = {
	.min = 1200,	.step = 100,	.max = 3300,
};
static const struct voltage_map_desc ldo1617_voltage_map_desc = {
	.min = 1600,	.step = 100,	.max = 3600,
};
static const struct voltage_map_desc buck12_voltage_map_desc = {
	.min = 750,	.step = 25,	.max = 1525,
};
static const struct voltage_map_desc buck3_voltage_map_desc = {
	.min = 1600,	.step = 100,	.max = 3600,
};
static const struct voltage_map_desc buck4_voltage_map_desc = {
	.min = 800,	.step = 100,	.max = 2300,
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

static inline int max8998_get_ldo(struct regulator_dev *rdev)
{
	return rdev_get_id(rdev);
}

static int max8998_list_voltage(struct regulator_dev *rdev,
				unsigned int selector)
{
	const struct voltage_map_desc *desc;
	int ldo = max8998_get_ldo(rdev);
	int val;

	if (ldo >= ARRAY_SIZE(ldo_voltage_map))
		return -EINVAL;

	desc = ldo_voltage_map[ldo];
	if (desc == NULL)
		return -EINVAL;

	val = desc->min + desc->step * selector;
	if (val > desc->max)
		return -EINVAL;

	return val * 1000;
}

static int max8998_get_enable_register(struct regulator_dev *rdev,
					int *reg, int *shift)
{
	int ldo = max8998_get_ldo(rdev);

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
	int ldo = max8998_get_ldo(rdev);
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

static int max8998_get_voltage(struct regulator_dev *rdev)
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

	return max8998_list_voltage(rdev, val);
}

static int max8998_set_voltage_ldo(struct regulator_dev *rdev,
				int min_uV, int max_uV)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const struct voltage_map_desc *desc;
	int ldo = max8998_get_ldo(rdev);
	int reg, shift = 0, mask, ret;
	int i = 0;

	if (ldo >= ARRAY_SIZE(ldo_voltage_map))
		return -EINVAL;

	desc = ldo_voltage_map[ldo];
	if (desc == NULL)
		return -EINVAL;

	if (max_vol < desc->min || min_vol > desc->max)
		return -EINVAL;

	while (desc->min + desc->step*i < min_vol &&
	       desc->min + desc->step*i < desc->max)
		i++;

	if (desc->min + desc->step*i > max_vol)
		return -EINVAL;

	ret = max8998_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	ret = max8998_update_reg(i2c, reg, i<<shift, mask<<shift);

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

static int max8998_set_voltage_buck(struct regulator_dev *rdev,
				    int min_uV, int max_uV)
{
	struct max8998_data *max8998 = rdev_get_drvdata(rdev);
	struct max8998_platform_data *pdata =
		dev_get_platdata(max8998->iodev->dev);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const struct voltage_map_desc *desc;
	int buck = max8998_get_ldo(rdev);
	int reg, shift = 0, mask, ret;
	int difference = 0, i = 0, j = 0, previous_vol = 0;
	u8 val = 0;
	static u8 buck1_last_val;

	if (buck >= ARRAY_SIZE(ldo_voltage_map))
		return -EINVAL;

	desc = ldo_voltage_map[buck];

	if (desc == NULL)
		return -EINVAL;

	if (max_vol < desc->min || min_vol > desc->max)
		return -EINVAL;

	while (desc->min + desc->step*i < min_vol &&
	       desc->min + desc->step*i < desc->max)
		i++;

	if (desc->min + desc->step*i > max_vol)
		return -EINVAL;

	ret = max8998_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	previous_vol = max8998_get_voltage(rdev);

	/* Check if voltage needs to be changed */
	/* if previous_voltage equal new voltage, return */
	if (previous_vol == max8998_list_voltage(rdev, i)) {
		dev_dbg(max8998->dev, "No voltage change, old:%d, new:%d\n",
			previous_vol, max8998_list_voltage(rdev, i));
		return ret;
	}

	switch (buck) {
	case MAX8998_BUCK1:
		dev_dbg(max8998->dev,
			"BUCK1, i:%d, buck1_vol1:%d, buck1_vol2:%d\n\
			 buck1_vol3:%d, buck1_vol4:%d\n",
			i, max8998->buck1_vol[0], max8998->buck1_vol[1],
			max8998->buck1_vol[2], max8998->buck1_vol[3]);

		if (gpio_is_valid(pdata->buck1_set1) &&
		    gpio_is_valid(pdata->buck1_set2)) {

			/* check if requested voltage */
			/* value is already defined */
			for (j = 0; j < ARRAY_SIZE(max8998->buck1_vol); j++) {
				if (max8998->buck1_vol[j] == i) {
					max8998->buck1_idx = j;
					buck1_gpio_set(pdata->buck1_set1,
						       pdata->buck1_set2, j);
					goto buck1_exit;
				}
			}

			/* no predefine regulator found */
			max8998->buck1_idx = (buck1_last_val % 2) + 2;
			dev_dbg(max8998->dev, "max8998->buck1_idx:%d\n",
				max8998->buck1_idx);
			max8998->buck1_vol[max8998->buck1_idx] = i;
			ret = max8998_get_voltage_register(rdev, &reg,
							   &shift,
							   &mask);
			ret = max8998_write_reg(i2c, reg, i);
			buck1_gpio_set(pdata->buck1_set1,
				       pdata->buck1_set2, max8998->buck1_idx);
			buck1_last_val++;
buck1_exit:
			dev_dbg(max8998->dev, "%s: SET1:%d, SET2:%d\n",
				i2c->name, gpio_get_value(pdata->buck1_set1),
				gpio_get_value(pdata->buck1_set2));
			break;
		} else {
			ret = max8998_write_reg(i2c, reg, i);
		}
		break;

	case MAX8998_BUCK2:
		dev_dbg(max8998->dev,
			"BUCK2, i:%d buck2_vol1:%d, buck2_vol2:%d\n"
			, i, max8998->buck2_vol[0], max8998->buck2_vol[1]);
		if (gpio_is_valid(pdata->buck2_set3)) {
			if (max8998->buck2_vol[0] == i) {
				max8998->buck1_idx = 0;
				buck2_gpio_set(pdata->buck2_set3, 0);
			} else {
				max8998->buck1_idx = 1;
				ret = max8998_get_voltage_register(rdev, &reg,
								   &shift,
								   &mask);
				ret = max8998_write_reg(i2c, reg, i);
				max8998->buck2_vol[1] = i;
				buck2_gpio_set(pdata->buck2_set3, 1);
			}
			dev_dbg(max8998->dev, "%s: SET3:%d\n", i2c->name,
				gpio_get_value(pdata->buck2_set3));
		} else {
			ret = max8998_write_reg(i2c, reg, i);
		}
		break;

	case MAX8998_BUCK3:
	case MAX8998_BUCK4:
		ret = max8998_update_reg(i2c, reg, i<<shift, mask<<shift);
		break;
	}

	/* Voltage stabilization */
	max8998_read_reg(i2c, MAX8998_REG_ONOFF4, &val);

	/* lp3974 hasn't got ENRAMP bit - ramp is assumed as true */
	/* MAX8998 has ENRAMP bit implemented, so test it*/
	if (max8998->iodev->type == TYPE_MAX8998 && !(val & MAX8998_ENRAMP))
		return ret;

	difference = desc->min + desc->step*i - previous_vol/1000;
	if (difference > 0)
		udelay(difference / ((val & 0x0f) + 1));

	return ret;
}

static struct regulator_ops max8998_ldo_ops = {
	.list_voltage		= max8998_list_voltage,
	.is_enabled		= max8998_ldo_is_enabled,
	.enable			= max8998_ldo_enable,
	.disable		= max8998_ldo_disable,
	.get_voltage		= max8998_get_voltage,
	.set_voltage		= max8998_set_voltage_ldo,
	.set_suspend_enable	= max8998_ldo_enable,
	.set_suspend_disable	= max8998_ldo_disable,
};

static struct regulator_ops max8998_buck_ops = {
	.list_voltage		= max8998_list_voltage,
	.is_enabled		= max8998_ldo_is_enabled,
	.enable			= max8998_ldo_enable,
	.disable		= max8998_ldo_disable,
	.get_voltage		= max8998_get_voltage,
	.set_voltage		= max8998_set_voltage_buck,
	.set_suspend_enable	= max8998_ldo_enable,
	.set_suspend_disable	= max8998_ldo_disable,
};

static struct regulator_ops max8998_others_ops = {
	.is_enabled		= max8998_ldo_is_enabled,
	.enable			= max8998_ldo_enable,
	.disable		= max8998_ldo_disable,
	.set_suspend_enable	= max8998_ldo_enable,
	.set_suspend_disable	= max8998_ldo_disable,
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
		.name		= "EN32KHz AP",
		.id		= MAX8998_EN32KHZ_AP,
		.ops		= &max8998_others_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}, {
		.name		= "EN32KHz CP",
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

static __devinit int max8998_pmic_probe(struct platform_device *pdev)
{
	struct max8998_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max8998_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct regulator_dev **rdev;
	struct max8998_data *max8998;
	struct i2c_client *i2c;
	int i, ret, size;

	if (!pdata) {
		dev_err(pdev->dev.parent, "No platform init data supplied\n");
		return -ENODEV;
	}

	max8998 = kzalloc(sizeof(struct max8998_data), GFP_KERNEL);
	if (!max8998)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	max8998->rdev = kzalloc(size, GFP_KERNEL);
	if (!max8998->rdev) {
		kfree(max8998);
		return -ENOMEM;
	}

	rdev = max8998->rdev;
	max8998->dev = &pdev->dev;
	max8998->iodev = iodev;
	max8998->num_regulators = pdata->num_regulators;
	platform_set_drvdata(pdev, max8998);
	i2c = max8998->iodev->i2c;

	/* NOTE: */
	/* For unused GPIO NOT marked as -1 (thereof equal to 0)  WARN_ON */
	/* will be displayed */

	/* Check if MAX8998 voltage selection GPIOs are defined */
	if (gpio_is_valid(pdata->buck1_set1) &&
	    gpio_is_valid(pdata->buck1_set2)) {
		/* Check if SET1 is not equal to 0 */
		if (!pdata->buck1_set1) {
			printk(KERN_ERR "MAX8998 SET1 GPIO defined as 0 !\n");
			WARN_ON(!pdata->buck1_set1);
			return -EIO;
		}
		/* Check if SET2 is not equal to 0 */
		if (!pdata->buck1_set2) {
			printk(KERN_ERR "MAX8998 SET2 GPIO defined as 0 !\n");
			WARN_ON(!pdata->buck1_set2);
			return -EIO;
		}

		gpio_request(pdata->buck1_set1, "MAX8998 BUCK1_SET1");
		gpio_direction_output(pdata->buck1_set1,
				      max8998->buck1_idx & 0x1);


		gpio_request(pdata->buck1_set2, "MAX8998 BUCK1_SET2");
		gpio_direction_output(pdata->buck1_set2,
				      (max8998->buck1_idx >> 1) & 0x1);
		/* Set predefined value for BUCK1 register 1 */
		i = 0;
		while (buck12_voltage_map_desc.min +
		       buck12_voltage_map_desc.step*i
		       != (pdata->buck1_max_voltage1 / 1000))
			i++;
		printk(KERN_ERR "i:%d, buck1_idx:%d\n", i, max8998->buck1_idx);
		max8998->buck1_vol[0] = i;
		ret = max8998_write_reg(i2c, MAX8998_REG_BUCK1_VOLTAGE1, i);

		/* Set predefined value for BUCK1 register 2 */
		i = 0;
		while (buck12_voltage_map_desc.min +
		       buck12_voltage_map_desc.step*i
		       != (pdata->buck1_max_voltage2 / 1000))
			i++;

		max8998->buck1_vol[1] = i;
		printk(KERN_ERR "i:%d, buck1_idx:%d\n", i, max8998->buck1_idx);
		ret = max8998_write_reg(i2c, MAX8998_REG_BUCK1_VOLTAGE2, i)
			+ ret;
		if (ret)
			return ret;

	}

	if (gpio_is_valid(pdata->buck2_set3)) {
		/* Check if SET3 is not equal to 0 */
		if (!pdata->buck2_set3) {
			printk(KERN_ERR "MAX8998 SET3 GPIO defined as 0 !\n");
			WARN_ON(!pdata->buck2_set3);
			return -EIO;
		}
		gpio_request(pdata->buck2_set3, "MAX8998 BUCK2_SET3");
		gpio_direction_output(pdata->buck2_set3,
				      max8998->buck2_idx & 0x1);

		/* BUCK2 - set preset default voltage value to buck2_vol[0] */
		i = 0;
		while (buck12_voltage_map_desc.min +
		       buck12_voltage_map_desc.step*i
		       != (pdata->buck2_max_voltage / 1000))
			i++;
		printk(KERN_ERR "i:%d, buck2_idx:%d\n", i, max8998->buck2_idx);
		max8998->buck2_vol[0] = i;
		ret = max8998_write_reg(i2c, MAX8998_REG_BUCK2_VOLTAGE1, i);
		if (ret)
			return ret;

	}

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct voltage_map_desc *desc;
		int id = pdata->regulators[i].id;
		int index = id - MAX8998_LDO2;

		desc = ldo_voltage_map[id];
		if (desc && regulators[index].ops != &max8998_others_ops) {
			int count = (desc->max - desc->min) / desc->step + 1;
			regulators[index].n_voltages = count;
		}
		rdev[i] = regulator_register(&regulators[index], max8998->dev,
				pdata->regulators[i].initdata, max8998);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(max8998->dev, "regulator init failed\n");
			rdev[i] = NULL;
			goto err;
		}
	}


	return 0;
err:
	for (i = 0; i < max8998->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max8998->rdev);
	kfree(max8998);

	return ret;
}

static int __devexit max8998_pmic_remove(struct platform_device *pdev)
{
	struct max8998_data *max8998 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max8998->rdev;
	int i;

	for (i = 0; i < max8998->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max8998->rdev);
	kfree(max8998);

	return 0;
}

static struct platform_driver max8998_pmic_driver = {
	.driver = {
		.name = "max8998-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max8998_pmic_probe,
	.remove = __devexit_p(max8998_pmic_remove),
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
