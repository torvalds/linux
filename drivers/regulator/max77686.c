/*
 * max77686.c - Regulator driver for the Maxim 77686
 *
 * Copyright (C) 2011 Samsung Electronics
 * Chiwoong Byun <woong.byun@smasung.com>
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
 *
 * This driver is based on max8997.c
 */

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>

struct max77686_data {
	struct device *dev;
	struct max77686_dev *iodev;
	int num_regulators;
	struct regulator_dev **rdev;
	int ramp_delay; /* in mV/us */

	bool buck2_gpiodvs;
	bool buck3_gpiodvs;
	bool buck4_gpiodvs;
	u8 buck2_vol[8];
	u8 buck3_vol[8];
	u8 buck4_vol[8];
	int buck234_gpios[3];
	int buck234_gpioindex;
	bool ignore_gpiodvs_side_effect;

	u8 saved_states[MAX77686_REG_MAX];
};

static inline void max77686_set_gpio(struct max77686_data *max77686)
{
	int set3 = (max77686->buck234_gpioindex) & 0x1;
	int set2 = ((max77686->buck234_gpioindex) >> 1) & 0x1;
	int set1 = ((max77686->buck234_gpioindex) >> 2) & 0x1;

	gpio_set_value(max77686->buck234_gpios[0], set1);
	gpio_set_value(max77686->buck234_gpios[1], set2);
	gpio_set_value(max77686->buck234_gpios[2], set3);
}

struct voltage_map_desc {
	int min;
	int max;
	int step;
	unsigned int n_bits;
};

/* Voltage maps in mV */
static const struct voltage_map_desc ldo2375_voltage_map_desc = {
	.min = 8000,	.max = 23750,	.step = 250,	.n_bits = 6,
}; /* LDO1 ~ 2, 6 ~ 8, 15 */

/* Voltage maps in mV */
static const struct voltage_map_desc ldo3950_voltage_map_desc = {
	.min = 8000,	.max = 39500,	.step = 500,	.n_bits = 6,
}; /* LDO3 ~ 5, 9 ~ 14, 16 ~ 26 */


static const struct voltage_map_desc buck234_voltage_map_desc = {
	.min = 6000,	.max = 37875,	.step = 125,	.n_bits = 8,
}; /* Buck2, 3, 4 */

static const struct voltage_map_desc buck56789_voltage_map_desc = {
	.min = 7500,	.max = 39000,	.step = 500,	.n_bits = 6,
}; /* Buck1, 5, 6, 7, 8, 9 */

static const struct voltage_map_desc *reg_voltage_map[] = {
	[MAX77686_LDO1] = &ldo2375_voltage_map_desc,
	[MAX77686_LDO2] = &ldo2375_voltage_map_desc,
	[MAX77686_LDO3] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO4] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO5] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO6] = &ldo2375_voltage_map_desc,
	[MAX77686_LDO7] = &ldo2375_voltage_map_desc,
	[MAX77686_LDO8] = &ldo2375_voltage_map_desc,
	[MAX77686_LDO9] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO10] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO11] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO12] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO13] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO14] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO15] = &ldo2375_voltage_map_desc,
	[MAX77686_LDO16] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO17] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO18] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO19] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO20] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO21] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO22] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO23] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO24] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO25] = &ldo3950_voltage_map_desc,
	[MAX77686_LDO26] = &ldo3950_voltage_map_desc,
	[MAX77686_BUCK1] = &buck56789_voltage_map_desc,
	[MAX77686_BUCK2] = &buck234_voltage_map_desc,
	[MAX77686_BUCK3] = &buck234_voltage_map_desc,
	[MAX77686_BUCK4] = &buck234_voltage_map_desc,
	[MAX77686_BUCK5] = &buck56789_voltage_map_desc,
	[MAX77686_BUCK6] = &buck56789_voltage_map_desc,
	[MAX77686_BUCK7] = &buck56789_voltage_map_desc,
	[MAX77686_BUCK8] = &buck56789_voltage_map_desc,
	[MAX77686_BUCK9] = &buck56789_voltage_map_desc,
	[MAX77686_EN32KHZ_AP] = NULL,
	[MAX77686_EN32KHZ_CP] = NULL,
};

static inline int max77686_get_rid(struct regulator_dev *rdev)
{
	return rdev_get_id(rdev);
}

static int max77686_list_voltage(struct regulator_dev *rdev,
		unsigned int selector)
{
	const struct voltage_map_desc *desc;
	int rid = max77686_get_rid(rdev);
	int val;

	if (rid >= ARRAY_SIZE(reg_voltage_map) ||
			rid < 0)
		return -EINVAL;

	desc = reg_voltage_map[rid];
	if (desc == NULL)
		return -EINVAL;

	val = desc->min + desc->step * selector;
	if (val > desc->max)
		return -EINVAL;

	return val * 100;
}

static int max77686_get_enable_register(struct regulator_dev *rdev,
		int *reg, int *mask, int *pattern)
{
	int rid = max77686_get_rid(rdev);

	switch (rid) {
	case MAX77686_LDO1 ... MAX77686_LDO26:
		*reg = MAX77686_REG_LDO1CTRL1 + (rid - MAX77686_LDO1);
		*mask = 0xC0;
		*pattern = 0xC0;
		break;
	case MAX77686_BUCK1:
		*reg = MAX77686_REG_BUCK1CTRL;
		*mask = 0x03;
		*pattern = 0x03;
		break;
	case MAX77686_BUCK2:
		*reg = MAX77686_REG_BUCK2CTRL1;
		*mask = 0x30;
		*pattern = 0x10;
		break;
	case MAX77686_BUCK3:
		*reg = MAX77686_REG_BUCK3CTRL1;
		*mask = 0x30;
		*pattern = 0x10;
		break;
	case MAX77686_BUCK4:
		*reg = MAX77686_REG_BUCK4CTRL1;
		*mask = 0x30;
		*pattern = 0x10;
		break;
	case MAX77686_BUCK5 ... MAX77686_BUCK9:
		*reg = MAX77686_REG_BUCK5CTRL + (rid - MAX77686_BUCK5)*2;
		*mask = 0x03;
		*pattern = 0x03;
		break;
	case MAX77686_EN32KHZ_AP ... MAX77686_EN32KHZ_CP:
		*reg = MAX77686_REG_32KHZ;
		*mask = 0x07;
		*pattern = 0x07;
		break;
	default:
		/* Not controllable or not exists */
		return -EINVAL;
	}

	return 0;
}

static int max77686_reg_is_enabled(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int ret, reg, mask, pattern;
	u8 val;

	ret = max77686_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret == -EINVAL)
		return 1; /* "not controllable" */
	else if (ret)
		return ret;

	ret = max77686_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	return (val & mask) == pattern;
}

static int max77686_reg_enable(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int ret, reg, mask, pattern;

	ret = max77686_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

/*	printk(PMIC_DEBUG "%s: id=%d, reg=%x, mask=%x, pattern=%x\n",
		__func__, max77686_get_rid(rdev), reg, mask, pattern);
*/
	return max77686_update_reg(i2c, reg, pattern, mask);
}

static int max77686_reg_disable(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int ret, reg, mask, pattern;

	ret = max77686_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	return max77686_update_reg(i2c, reg, ~mask, mask);
}

static int max77686_get_voltage_register(struct regulator_dev *rdev,
		int *_reg, int *_shift, int *_mask)
{
	int rid = max77686_get_rid(rdev);
	int reg, shift = 0, mask = 0x3f;

	switch (rid) {
	case MAX77686_LDO1 ... MAX77686_LDO26:
		reg = MAX77686_REG_LDO1CTRL1 + (rid - MAX77686_LDO1);
		break;
	case MAX77686_BUCK1:
		reg = MAX77686_REG_BUCK1OUT;
		break;
	case MAX77686_BUCK2:
		reg = MAX77686_REG_BUCK2DVS1;
		mask = 0xff;
		break;
	case MAX77686_BUCK3:
		reg = MAX77686_REG_BUCK3DVS1;
		mask = 0xff;
		break;
	case MAX77686_BUCK4:
		reg = MAX77686_REG_BUCK4DVS1;
		mask = 0xff;
		break;
	case MAX77686_BUCK5 ... MAX77686_BUCK9:
		reg = MAX77686_REG_BUCK5OUT + (rid - MAX77686_BUCK5) * 2;
		break;
	default:
		return -EINVAL;
	}

	*_reg = reg;
	*_shift = shift;
	*_mask = mask;

	return 0;
}

static int max77686_get_voltage(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int reg, shift, mask, ret;
	int rid = max77686_get_rid(rdev);
	u8 val;

	ret = max77686_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	if ((rid == MAX77686_BUCK2 && max77686->buck2_gpiodvs) ||
			(rid == MAX77686_BUCK3 && max77686->buck3_gpiodvs) ||
			(rid == MAX77686_BUCK4 && max77686->buck4_gpiodvs))
		reg += max77686->buck234_gpioindex;

	ret = max77686_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	val >>= shift;
	val &= mask;

	if (rdev->desc && rdev->desc->ops && rdev->desc->ops->list_voltage)
		return rdev->desc->ops->list_voltage(rdev, val);

	/*
	 * max77686_list_voltage returns value for any rdev with voltage_map,
	 * which works for "CHARGER" and "CHARGER TOPOFF" that do not have
	 * list_voltage ops (they are current regulators).
	 */
	return max77686_list_voltage(rdev, val);
}

static inline int max77686_get_voltage_proper_val(
		const struct voltage_map_desc *desc,
		int min_vol, int max_vol)
{
	int i = 0;

	if (desc == NULL)
		return -EINVAL;

	if (max_vol < desc->min || min_vol > desc->max)
		return -EINVAL;

	while (desc->min + desc->step * i < min_vol &&
			desc->min + desc->step * i < desc->max)
		i++;

	if (desc->min + desc->step * i > max_vol)
		return -EINVAL;

	if (i >= (1 << desc->n_bits))
		return -EINVAL;

	return i;
}

/*
 * For LDO1 ~ LDO21, BUCK1~5, BUCK7, CHARGER, CHARGER_TOPOFF
 * BUCK1, 2, and 5 are available if they are not controlled by gpio
 */
static int max77686_set_voltage_ldobuck(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int min_vol = min_uV / 100, max_vol = max_uV / 100;
	const struct voltage_map_desc *desc;
	int rid = max77686_get_rid(rdev);
	int reg, shift = 0, mask, ret;
	int i;
	u8 org;

	switch (rid) {
	case MAX77686_LDO1 ... MAX77686_LDO26:
		break;
	case MAX77686_BUCK1 ... MAX77686_BUCK9:
		break;
	default:
		return -EINVAL;
	}

	desc = reg_voltage_map[rid];

#if defined(CONFIG_BOARD_ODROID_U2) || defined(CONFIG_BOARD_ODROID_U)
	if((rid == MAX77686_BUCK8) && (min_vol > max_vol))	i = 0;
	else
		i = max77686_get_voltage_proper_val(desc, min_vol, max_vol);
#else
	i = max77686_get_voltage_proper_val(desc, min_vol, max_vol);
#endif

	if (i < 0)
		return i;

	ret = max77686_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	max77686_read_reg(i2c, reg, &org);
	org = (org & mask) >> shift;

	ret = max77686_update_reg(i2c, reg, i << shift, mask << shift);
	*selector = i;

	if (rid == MAX77686_BUCK2 || rid == MAX77686_BUCK2 || rid == MAX77686_BUCK3 || rid == MAX77686_BUCK4) {
		/* If the voltage is increasing */
		//if (org < i)
		//	udelay(DIV_ROUND_UP(desc->step * (i - org), max77686->ramp_delay));
		udelay(100);
	}

	return ret;
}

/*
 * Assess the damage on the voltage setting of BUCK1,2,5 by the change.
 *
 * When GPIO-DVS mode is used for multiple bucks, changing the voltage value
 * of one of the bucks may affect that of another buck, which is the side
 * effect of the change (set_voltage). This function examines the GPIO-DVS
 * configurations and checks whether such side-effect exists.
 */
static int max77686_assess_side_effect(struct regulator_dev *rdev,
		u8 new_val, int *best)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	int rid = max77686_get_rid(rdev);
	u8 *buckx_val[3];
	bool buckx_gpiodvs[3];
	int side_effect[8];
	int min_side_effect = INT_MAX;
	int i;

	*best = -1;

	switch (rid) {
	case MAX77686_BUCK2:
		rid = 0;
		break;
	case MAX77686_BUCK3:
		rid = 1;
		break;
	case MAX77686_BUCK4:
		rid = 2;
		break;
	default:
		return -EINVAL;
	}

	buckx_val[0] = max77686->buck2_vol;
	buckx_val[1] = max77686->buck3_vol;
	buckx_val[2] = max77686->buck4_vol;
	buckx_gpiodvs[0] = max77686->buck2_gpiodvs;
	buckx_gpiodvs[1] = max77686->buck3_gpiodvs;
	buckx_gpiodvs[2] = max77686->buck4_gpiodvs;

	for (i = 0; i < 8; i++) {
		int others;

		if (new_val != (buckx_val[rid])[i]) {
			side_effect[i] = -1;
			continue;
		}

		side_effect[i] = 0;
		for (others = 0; others < 3; others++) {
			int diff;

			if (others == rid)
				continue;
			if (buckx_gpiodvs[others] == false)
				continue; /* Not affected */
			diff = (buckx_val[others])[i] -
				(buckx_val[others])[max77686->buck234_gpioindex];
			if (diff > 0)
				side_effect[i] += diff;
			else if (diff < 0)
				side_effect[i] -= diff;
		}
		if (side_effect[i] == 0) {
			*best = i;
			return 0; /* NO SIDE EFFECT! Use This! */
		}
		if (side_effect[i] < min_side_effect) {
			min_side_effect = side_effect[i];
			*best = i;
		}
	}

	if (*best == -1)
		return -EINVAL;

	return side_effect[*best];
}

/*
 * For Buck 1 ~ 5 and 7. If it is not controlled by GPIO, this calls
 * max77686_set_voltage_ldobuck to do the job.
 */
static int max77686_set_voltage_buck(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	int rid = max77686_get_rid(rdev);
	const struct voltage_map_desc *desc;
	int new_val, new_idx, damage, tmp_val, tmp_idx, tmp_dmg;
	bool gpio_dvs_mode = false;
	int min_vol = min_uV / 100, max_vol = max_uV / 100;

	if (rid < MAX77686_BUCK1 || rid > MAX77686_BUCK9)
		return -EINVAL;

	switch (rid) {
	case MAX77686_BUCK2:
		if (max77686->buck2_gpiodvs)
			gpio_dvs_mode = true;
		break;
	case MAX77686_BUCK3:
		if (max77686->buck3_gpiodvs)
			gpio_dvs_mode = true;
		break;
	case MAX77686_BUCK4:
		if (max77686->buck4_gpiodvs)
			gpio_dvs_mode = true;
		break;
#if defined(CONFIG_BOARD_ODROID_U2) || defined(CONFIG_BOARD_ODROID_U)
	case MAX77686_BUCK8:	
		max77686_set_voltage_ldobuck(rdev, min_uV, -1, selector);
		mdelay(100);
		break;
#endif		
	}
	max77686_set_gpio(max77686);

	if (!gpio_dvs_mode)
		return max77686_set_voltage_ldobuck(rdev, min_uV, max_uV,
						selector);

	desc = reg_voltage_map[rid];
	new_val = max77686_get_voltage_proper_val(desc, min_vol, max_vol);
	if (new_val < 0)
		return new_val;

	tmp_dmg = INT_MAX;
	tmp_idx = -1;
	tmp_val = -1;
	do {
		damage = max77686_assess_side_effect(rdev, new_val, &new_idx);
		if (damage == 0)
			goto out;

		if (tmp_dmg > damage) {
			tmp_idx = new_idx;
			tmp_val = new_val;
			tmp_dmg = damage;
		}

		new_val++;
	} while (desc->min + desc->step + new_val <= desc->max);

	new_idx = tmp_idx;
	new_val = tmp_val;

	if (max77686->ignore_gpiodvs_side_effect == false)
		return -EINVAL;

	dev_warn(&rdev->dev, "MAX77686 GPIO-DVS Side Effect Warning: GPIO SET:"
			" %d -> %d\n", max77686->buck234_gpioindex, tmp_idx);

out:
	if (new_idx < 0 || new_val < 0)
		return -EINVAL;

	max77686->buck234_gpioindex = new_idx;
	max77686_set_gpio(max77686);
	*selector = new_val;

	return 0;
}

static int max77686_reg_enable_suspend(struct regulator_dev *rdev)
{
	return 0;
}

static int max77686_reg_disable_suspend(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int ret, reg, mask, pattern;
	int rid = max77686_get_rid(rdev);

	ret = max77686_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	max77686_read_reg(i2c, reg, &max77686->saved_states[rid]);

	if (rid == MAX77686_LDO1 ||
			rid == MAX77686_LDO10 ||
			rid == MAX77686_LDO21) {
		dev_dbg(&rdev->dev, "Conditional Power-Off for %s\n",
				rdev->desc->name);
		return max77686_update_reg(i2c, reg, 0x40, mask);
	}

	dev_dbg(&rdev->dev, "Full Power-Off for %s (%xh -> %xh)\n",
			rdev->desc->name, max77686->saved_states[rid] & mask,
			(~pattern) & mask);
	return max77686_update_reg(i2c, reg, ~pattern, mask);
}

static struct regulator_ops max77686_ldo_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	.get_voltage		= max77686_get_voltage,
	.set_voltage		= max77686_set_voltage_ldobuck,
	.set_suspend_enable	= max77686_reg_enable_suspend,
	.set_suspend_disable	= max77686_reg_disable_suspend,
};

static struct regulator_ops max77686_buck_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	.get_voltage		= max77686_get_voltage,
	.set_voltage		= max77686_set_voltage_buck,
	.set_suspend_enable	= max77686_reg_enable_suspend,
	.set_suspend_disable	= max77686_reg_disable_suspend,
};

static struct regulator_ops max77686_fixedvolt_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	.set_suspend_enable	= max77686_reg_enable_suspend,
	.set_suspend_disable	= max77686_reg_disable_suspend,
};

#define regulator_desc_ldo(num)		{	\
	.name		= "LDO"#num,		\
	.id		= MAX77686_LDO##num,	\
	.ops		= &max77686_ldo_ops,	\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,		\
}
#define regulator_desc_buck(num)		{	\
	.name		= "BUCK"#num,		\
	.id		= MAX77686_BUCK##num,	\
	.ops		= &max77686_buck_ops,	\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,		\
}

static struct regulator_desc regulators[] = {
	regulator_desc_ldo(1),
	regulator_desc_ldo(2),
	regulator_desc_ldo(3),
	regulator_desc_ldo(4),
	regulator_desc_ldo(5),
	regulator_desc_ldo(6),
	regulator_desc_ldo(7),
	regulator_desc_ldo(8),
	regulator_desc_ldo(9),
	regulator_desc_ldo(10),
	regulator_desc_ldo(11),
	regulator_desc_ldo(12),
	regulator_desc_ldo(13),
	regulator_desc_ldo(14),
	regulator_desc_ldo(15),
	regulator_desc_ldo(16),
	regulator_desc_ldo(17),
	regulator_desc_ldo(18),
	regulator_desc_ldo(19),
	regulator_desc_ldo(20),
	regulator_desc_ldo(21),
	regulator_desc_ldo(22),
	regulator_desc_ldo(23),
	regulator_desc_ldo(24),
	regulator_desc_ldo(25),
	regulator_desc_ldo(26),
	regulator_desc_buck(1),
	regulator_desc_buck(2),
	regulator_desc_buck(3),
	regulator_desc_buck(4),
	regulator_desc_buck(5),
	regulator_desc_buck(6),
	regulator_desc_buck(7),
	regulator_desc_buck(8),
	regulator_desc_buck(9),
	{
		.name	= "EN32KHz AP",
		.id	= MAX77686_EN32KHZ_AP,
		.ops	= &max77686_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "EN32KHz CP",
		.id	= MAX77686_EN32KHZ_CP,
		.ops	= &max77686_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	},
};

static __devinit int max77686_pmic_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77686_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct regulator_dev **rdev;
	struct max77686_data *max77686;
	struct i2c_client *i2c;
	int i, ret, size;
	u8 max_buck2 = 0, max_buck3 = 0, max_buck4 = 0;

	if (!pdata) {
		dev_err(pdev->dev.parent, "No platform init data supplied.\n");
		return -ENODEV;
	}

	max77686 = kzalloc(sizeof(struct max77686_data), GFP_KERNEL);
	if (!max77686)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	max77686->rdev = kzalloc(size, GFP_KERNEL);
	if (!max77686->rdev) {
		kfree(max77686);
		return -ENOMEM;
	}

	rdev = max77686->rdev;
	max77686->dev = &pdev->dev;
	max77686->iodev = iodev;
	max77686->num_regulators = pdata->num_regulators;
	max77686->ramp_delay = 20;
	platform_set_drvdata(pdev, max77686);
	i2c = max77686->iodev->i2c;

	max77686->buck234_gpioindex = pdata->buck234_default_idx;
	max77686->buck2_gpiodvs = pdata->buck2_gpiodvs;
	max77686->buck3_gpiodvs = pdata->buck3_gpiodvs;
	max77686->buck4_gpiodvs = pdata->buck4_gpiodvs;
	memcpy(max77686->buck234_gpios, pdata->buck234_gpios, sizeof(int) * 3);
	max77686->ignore_gpiodvs_side_effect = pdata->ignore_gpiodvs_side_effect;

	for (i = 0; i < 8; i++) {
		max77686->buck2_vol[i] = ret =
			max77686_get_voltage_proper_val(
					&buck234_voltage_map_desc,
					pdata->buck2_voltage[i] / 100,
					pdata->buck2_voltage[i] / 100 +
					buck234_voltage_map_desc.step);
		if (ret < 0)
			goto err_alloc;

		max77686->buck3_vol[i] = ret =
			max77686_get_voltage_proper_val(
					&buck234_voltage_map_desc,
					pdata->buck3_voltage[i] / 100,
					pdata->buck3_voltage[i] / 100 +
					buck234_voltage_map_desc.step);
		if (ret < 0)
			goto err_alloc;

		max77686->buck4_vol[i] = ret =
			max77686_get_voltage_proper_val(
					&buck234_voltage_map_desc,
					pdata->buck4_voltage[i] / 100,
					pdata->buck4_voltage[i] / 100 +
					buck234_voltage_map_desc.step);
		if (ret < 0)
			goto err_alloc;

		if (max_buck2 < max77686->buck2_vol[i])
			max_buck2 = max77686->buck2_vol[i];
		if (max_buck3 < max77686->buck3_vol[i])
			max_buck3 = max77686->buck3_vol[i];
		if (max_buck4 < max77686->buck4_vol[i])
			max_buck4 = max77686->buck4_vol[i];
	}

	/* For the safety, set max voltage before setting up */
	for (i = 0; i < 8; i++) {
		max77686_update_reg(i2c, MAX77686_REG_BUCK2DVS1 + i,
				max_buck2, 0xff);
		max77686_update_reg(i2c, MAX77686_REG_BUCK3DVS1 + i,
				max_buck3, 0xff);
		max77686_update_reg(i2c, MAX77686_REG_BUCK4DVS1 + i,
				max_buck4, 0xff);
	}

	/*
	 * If buck 1, 2, and 5 do not care DVS GPIO settings, ignore them.
	 * If at least one of them cares, set gpios.
	 */
	if (pdata->buck2_gpiodvs || pdata->buck3_gpiodvs ||
			pdata->buck4_gpiodvs) {
		bool gpio1set = false, gpio2set = false;

		if (!gpio_is_valid(pdata->buck234_gpios[0]) ||
				!gpio_is_valid(pdata->buck234_gpios[1]) ||
				!gpio_is_valid(pdata->buck234_gpios[2])) {
			dev_err(&pdev->dev, "GPIO NOT VALID\n");
			ret = -EINVAL;
			goto err_alloc;
		}

		ret = gpio_request(pdata->buck234_gpios[0],
				"MAX77686 SET1");
		if (ret == -EBUSY)
			dev_warn(&pdev->dev, "Duplicated gpio request"
					" on SET1\n");
		else if (ret)
			goto err_alloc;
		else
			gpio1set = true;

		ret = gpio_request(pdata->buck234_gpios[1],
				"MAX77686 SET2");
		if (ret == -EBUSY)
			dev_warn(&pdev->dev, "Duplicated gpio request"
					" on SET2\n");
		else if (ret) {
			if (gpio1set)
				gpio_free(pdata->buck234_gpios[0]);
			goto err_alloc;
		} else
			gpio2set = true;

		ret = gpio_request(pdata->buck234_gpios[2],
				"MAX77686 SET3");
		if (ret == -EBUSY)
			dev_warn(&pdev->dev, "Duplicated gpio request"
					" on SET3\n");
		else if (ret) {
			if (gpio1set)
				gpio_free(pdata->buck234_gpios[0]);
			if (gpio2set)
				gpio_free(pdata->buck234_gpios[1]);
			goto err_alloc;
		}

		gpio_direction_output(pdata->buck234_gpios[0],
				(max77686->buck234_gpioindex >> 2)
				& 0x1); /* SET1 */
		gpio_direction_output(pdata->buck234_gpios[1],
				(max77686->buck234_gpioindex >> 1)
				& 0x1); /* SET2 */
		gpio_direction_output(pdata->buck234_gpios[2],
				(max77686->buck234_gpioindex >> 0)
				& 0x1); /* SET3 */
		ret = 0;
	}

	/* Initialize all the DVS related BUCK registers */
	for (i = 0; i < 8; i++) {
		max77686_update_reg(i2c, MAX77686_REG_BUCK2DVS1 + i,
				max77686->buck2_vol[i],
				0xff);
		max77686_update_reg(i2c, MAX77686_REG_BUCK3DVS1 + i,
				max77686->buck3_vol[i],
				0xff);
		max77686_update_reg(i2c, MAX77686_REG_BUCK4DVS1 + i,
				max77686->buck4_vol[i],
				0xff);
	}

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct voltage_map_desc *desc;
		int id = pdata->regulators[i].id;

		desc = reg_voltage_map[id];
		if (desc)
			regulators[id].n_voltages = 
				(desc->max - desc->min) / desc->step + 1;

		rdev[i] = regulator_register(&regulators[id], max77686->dev,
				pdata->regulators[i].initdata, max77686);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(max77686->dev, "regulator init failed for %d\n",
					id);
			rdev[i] = NULL;
			goto err;
		}
	}

	return 0;
err:
	for (i = 0; i < max77686->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);
err_alloc:
	kfree(max77686->rdev);
	kfree(max77686);

	return ret;
}

static int __devexit max77686_pmic_remove(struct platform_device *pdev)
{
	struct max77686_data *max77686 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max77686->rdev;
	int i;

	for (i = 0; i < max77686->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max77686->rdev);
	kfree(max77686);

	return 0;
}

static const struct platform_device_id max77686_pmic_id[] = {
	{ "max77686-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77686_pmic_id);

static struct platform_driver max77686_pmic_driver = {
	.driver = {
		.name = "max77686-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max77686_pmic_probe,
	.remove = __devexit_p(max77686_pmic_remove),
	.id_table = max77686_pmic_id,
};

static int __init max77686_pmic_init(void)
{
	return platform_driver_register(&max77686_pmic_driver);
}
subsys_initcall(max77686_pmic_init);

static void __exit max77686_pmic_cleanup(void)
{
	platform_driver_unregister(&max77686_pmic_driver);
}
module_exit(max77686_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 8997/8966 Regulator Driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
