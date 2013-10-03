/*
 * max8997.c - Regulator driver for the Maxim 8997/8966
 *
 * Copyright (C) 2011 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@smasung.com>
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
 * This driver is based on max8998.c
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
#include <linux/regulator/of_regulator.h>

struct max8997_data {
	struct device *dev;
	struct max8997_dev *iodev;
	int num_regulators;
	struct regulator_dev **rdev;
	int ramp_delay; /* in mV/us */

	bool buck1_gpiodvs;
	bool buck2_gpiodvs;
	bool buck5_gpiodvs;
	u8 buck1_vol[8];
	u8 buck2_vol[8];
	u8 buck5_vol[8];
	int buck125_gpios[3];
	int buck125_gpioindex;
	bool ignore_gpiodvs_side_effect;

	u8 saved_states[MAX8997_REG_MAX];
};

static const unsigned int safeoutvolt[] = {
	4850000,
	4900000,
	4950000,
	3300000,
};

static inline void max8997_set_gpio(struct max8997_data *max8997)
{
	int set3 = (max8997->buck125_gpioindex) & 0x1;
	int set2 = ((max8997->buck125_gpioindex) >> 1) & 0x1;
	int set1 = ((max8997->buck125_gpioindex) >> 2) & 0x1;

	gpio_set_value(max8997->buck125_gpios[0], set1);
	gpio_set_value(max8997->buck125_gpios[1], set2);
	gpio_set_value(max8997->buck125_gpios[2], set3);
}

struct voltage_map_desc {
	int min;
	int max;
	int step;
};

/* Voltage maps in uV */
static const struct voltage_map_desc ldo_voltage_map_desc = {
	.min = 800000,	.max = 3950000,	.step = 50000,
}; /* LDO1 ~ 18, 21 all */

static const struct voltage_map_desc buck1245_voltage_map_desc = {
	.min = 650000,	.max = 2225000,	.step = 25000,
}; /* Buck1, 2, 4, 5 */

static const struct voltage_map_desc buck37_voltage_map_desc = {
	.min = 750000,	.max = 3900000,	.step = 50000,
}; /* Buck3, 7 */

/* current map in uA */
static const struct voltage_map_desc charger_current_map_desc = {
	.min = 200000,	.max = 950000,	.step = 50000,
};

static const struct voltage_map_desc topoff_current_map_desc = {
	.min = 50000,	.max = 200000,	.step = 10000,
};

static const struct voltage_map_desc *reg_voltage_map[] = {
	[MAX8997_LDO1] = &ldo_voltage_map_desc,
	[MAX8997_LDO2] = &ldo_voltage_map_desc,
	[MAX8997_LDO3] = &ldo_voltage_map_desc,
	[MAX8997_LDO4] = &ldo_voltage_map_desc,
	[MAX8997_LDO5] = &ldo_voltage_map_desc,
	[MAX8997_LDO6] = &ldo_voltage_map_desc,
	[MAX8997_LDO7] = &ldo_voltage_map_desc,
	[MAX8997_LDO8] = &ldo_voltage_map_desc,
	[MAX8997_LDO9] = &ldo_voltage_map_desc,
	[MAX8997_LDO10] = &ldo_voltage_map_desc,
	[MAX8997_LDO11] = &ldo_voltage_map_desc,
	[MAX8997_LDO12] = &ldo_voltage_map_desc,
	[MAX8997_LDO13] = &ldo_voltage_map_desc,
	[MAX8997_LDO14] = &ldo_voltage_map_desc,
	[MAX8997_LDO15] = &ldo_voltage_map_desc,
	[MAX8997_LDO16] = &ldo_voltage_map_desc,
	[MAX8997_LDO17] = &ldo_voltage_map_desc,
	[MAX8997_LDO18] = &ldo_voltage_map_desc,
	[MAX8997_LDO21] = &ldo_voltage_map_desc,
	[MAX8997_BUCK1] = &buck1245_voltage_map_desc,
	[MAX8997_BUCK2] = &buck1245_voltage_map_desc,
	[MAX8997_BUCK3] = &buck37_voltage_map_desc,
	[MAX8997_BUCK4] = &buck1245_voltage_map_desc,
	[MAX8997_BUCK5] = &buck1245_voltage_map_desc,
	[MAX8997_BUCK6] = NULL,
	[MAX8997_BUCK7] = &buck37_voltage_map_desc,
	[MAX8997_EN32KHZ_AP] = NULL,
	[MAX8997_EN32KHZ_CP] = NULL,
	[MAX8997_ENVICHG] = NULL,
	[MAX8997_ESAFEOUT1] = NULL,
	[MAX8997_ESAFEOUT2] = NULL,
	[MAX8997_CHARGER_CV] = NULL,
	[MAX8997_CHARGER] = &charger_current_map_desc,
	[MAX8997_CHARGER_TOPOFF] = &topoff_current_map_desc,
};

static int max8997_list_voltage_charger_cv(struct regulator_dev *rdev,
		unsigned int selector)
{
	int rid = rdev_get_id(rdev);

	if (rid != MAX8997_CHARGER_CV)
		goto err;

	switch (selector) {
	case 0x00:
		return 4200000;
	case 0x01 ... 0x0E:
		return 4000000 + 20000 * (selector - 0x01);
	case 0x0F:
		return 4350000;
	default:
		return -EINVAL;
	}
err:
	return -EINVAL;
}

static int max8997_list_voltage(struct regulator_dev *rdev,
		unsigned int selector)
{
	const struct voltage_map_desc *desc;
	int rid = rdev_get_id(rdev);
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

	return val;
}

static int max8997_get_enable_register(struct regulator_dev *rdev,
		int *reg, int *mask, int *pattern)
{
	int rid = rdev_get_id(rdev);

	switch (rid) {
	case MAX8997_LDO1 ... MAX8997_LDO21:
		*reg = MAX8997_REG_LDO1CTRL + (rid - MAX8997_LDO1);
		*mask = 0xC0;
		*pattern = 0xC0;
		break;
	case MAX8997_BUCK1:
		*reg = MAX8997_REG_BUCK1CTRL;
		*mask = 0x01;
		*pattern = 0x01;
		break;
	case MAX8997_BUCK2:
		*reg = MAX8997_REG_BUCK2CTRL;
		*mask = 0x01;
		*pattern = 0x01;
		break;
	case MAX8997_BUCK3:
		*reg = MAX8997_REG_BUCK3CTRL;
		*mask = 0x01;
		*pattern = 0x01;
		break;
	case MAX8997_BUCK4:
		*reg = MAX8997_REG_BUCK4CTRL;
		*mask = 0x01;
		*pattern = 0x01;
		break;
	case MAX8997_BUCK5:
		*reg = MAX8997_REG_BUCK5CTRL;
		*mask = 0x01;
		*pattern = 0x01;
		break;
	case MAX8997_BUCK6:
		*reg = MAX8997_REG_BUCK6CTRL;
		*mask = 0x01;
		*pattern = 0x01;
		break;
	case MAX8997_BUCK7:
		*reg = MAX8997_REG_BUCK7CTRL;
		*mask = 0x01;
		*pattern = 0x01;
		break;
	case MAX8997_EN32KHZ_AP ... MAX8997_EN32KHZ_CP:
		*reg = MAX8997_REG_MAINCON1;
		*mask = 0x01 << (rid - MAX8997_EN32KHZ_AP);
		*pattern = 0x01 << (rid - MAX8997_EN32KHZ_AP);
		break;
	case MAX8997_ENVICHG:
		*reg = MAX8997_REG_MBCCTRL1;
		*mask = 0x80;
		*pattern = 0x80;
		break;
	case MAX8997_ESAFEOUT1 ... MAX8997_ESAFEOUT2:
		*reg = MAX8997_REG_SAFEOUTCTRL;
		*mask = 0x40 << (rid - MAX8997_ESAFEOUT1);
		*pattern = 0x40 << (rid - MAX8997_ESAFEOUT1);
		break;
	case MAX8997_CHARGER:
		*reg = MAX8997_REG_MBCCTRL2;
		*mask = 0x40;
		*pattern = 0x40;
		break;
	default:
		/* Not controllable or not exists */
		return -EINVAL;
	}

	return 0;
}

static int max8997_reg_is_enabled(struct regulator_dev *rdev)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8997->iodev->i2c;
	int ret, reg, mask, pattern;
	u8 val;

	ret = max8997_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	ret = max8997_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	return (val & mask) == pattern;
}

static int max8997_reg_enable(struct regulator_dev *rdev)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8997->iodev->i2c;
	int ret, reg, mask, pattern;

	ret = max8997_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	return max8997_update_reg(i2c, reg, pattern, mask);
}

static int max8997_reg_disable(struct regulator_dev *rdev)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8997->iodev->i2c;
	int ret, reg, mask, pattern;

	ret = max8997_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	return max8997_update_reg(i2c, reg, ~pattern, mask);
}

static int max8997_get_voltage_register(struct regulator_dev *rdev,
		int *_reg, int *_shift, int *_mask)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	int rid = rdev_get_id(rdev);
	int reg, shift = 0, mask = 0x3f;

	switch (rid) {
	case MAX8997_LDO1 ... MAX8997_LDO21:
		reg = MAX8997_REG_LDO1CTRL + (rid - MAX8997_LDO1);
		break;
	case MAX8997_BUCK1:
		reg = MAX8997_REG_BUCK1DVS1;
		if (max8997->buck1_gpiodvs)
			reg += max8997->buck125_gpioindex;
		break;
	case MAX8997_BUCK2:
		reg = MAX8997_REG_BUCK2DVS1;
		if (max8997->buck2_gpiodvs)
			reg += max8997->buck125_gpioindex;
		break;
	case MAX8997_BUCK3:
		reg = MAX8997_REG_BUCK3DVS;
		break;
	case MAX8997_BUCK4:
		reg = MAX8997_REG_BUCK4DVS;
		break;
	case MAX8997_BUCK5:
		reg = MAX8997_REG_BUCK5DVS1;
		if (max8997->buck5_gpiodvs)
			reg += max8997->buck125_gpioindex;
		break;
	case MAX8997_BUCK7:
		reg = MAX8997_REG_BUCK7DVS;
		break;
	case MAX8997_ESAFEOUT1 ...  MAX8997_ESAFEOUT2:
		reg = MAX8997_REG_SAFEOUTCTRL;
		shift = (rid == MAX8997_ESAFEOUT2) ? 2 : 0;
		mask = 0x3;
		break;
	case MAX8997_CHARGER_CV:
		reg = MAX8997_REG_MBCCTRL3;
		shift = 0;
		mask = 0xf;
		break;
	case MAX8997_CHARGER:
		reg = MAX8997_REG_MBCCTRL4;
		shift = 0;
		mask = 0xf;
		break;
	case MAX8997_CHARGER_TOPOFF:
		reg = MAX8997_REG_MBCCTRL5;
		shift = 0;
		mask = 0xf;
		break;
	default:
		return -EINVAL;
	}

	*_reg = reg;
	*_shift = shift;
	*_mask = mask;

	return 0;
}

static int max8997_get_voltage_sel(struct regulator_dev *rdev)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8997->iodev->i2c;
	int reg, shift, mask, ret;
	u8 val;

	ret = max8997_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	ret = max8997_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	val >>= shift;
	val &= mask;

	return val;
}

static inline int max8997_get_voltage_proper_val(
		const struct voltage_map_desc *desc,
		int min_vol, int max_vol)
{
	int i;

	if (desc == NULL)
		return -EINVAL;

	if (max_vol < desc->min || min_vol > desc->max)
		return -EINVAL;

	if (min_vol < desc->min)
		min_vol = desc->min;

	i = DIV_ROUND_UP(min_vol - desc->min, desc->step);

	if (desc->min + desc->step * i > max_vol)
		return -EINVAL;

	return i;
}

static int max8997_set_voltage_charger_cv(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8997->iodev->i2c;
	int rid = rdev_get_id(rdev);
	int lb, ub;
	int reg, shift = 0, mask, ret = 0;
	u8 val = 0x0;

	if (rid != MAX8997_CHARGER_CV)
		return -EINVAL;

	ret = max8997_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	if (max_uV < 4000000 || min_uV > 4350000)
		return -EINVAL;

	if (min_uV <= 4000000) {
		if (max_uV >= 4000000)
			return -EINVAL;
		else
			val = 0x1;
	} else if (min_uV <= 4200000 && max_uV >= 4200000)
		val = 0x0;
	else {
		lb = (min_uV - 4000001) / 20000 + 2;
		ub = (max_uV - 4000000) / 20000 + 1;

		if (lb > ub)
			return -EINVAL;

		if (lb < 0xf)
			val = lb;
		else {
			if (ub >= 0xf)
				val = 0xf;
			else
				return -EINVAL;
		}
	}

	*selector = val;

	ret = max8997_update_reg(i2c, reg, val << shift, mask);

	return ret;
}

/*
 * For LDO1 ~ LDO21, BUCK1~5, BUCK7, CHARGER, CHARGER_TOPOFF
 * BUCK1, 2, and 5 are available if they are not controlled by gpio
 */
static int max8997_set_voltage_ldobuck(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8997->iodev->i2c;
	const struct voltage_map_desc *desc;
	int rid = rdev_get_id(rdev);
	int i, reg, shift, mask, ret;

	switch (rid) {
	case MAX8997_LDO1 ... MAX8997_LDO21:
		break;
	case MAX8997_BUCK1 ... MAX8997_BUCK5:
		break;
	case MAX8997_BUCK6:
		return -EINVAL;
	case MAX8997_BUCK7:
		break;
	case MAX8997_CHARGER:
		break;
	case MAX8997_CHARGER_TOPOFF:
		break;
	default:
		return -EINVAL;
	}

	desc = reg_voltage_map[rid];

	i = max8997_get_voltage_proper_val(desc, min_uV, max_uV);
	if (i < 0)
		return i;

	ret = max8997_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	ret = max8997_update_reg(i2c, reg, i << shift, mask << shift);
	*selector = i;

	return ret;
}

static int max8997_set_voltage_buck_time_sel(struct regulator_dev *rdev,
						unsigned int old_selector,
						unsigned int new_selector)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	int rid = rdev_get_id(rdev);
	const struct voltage_map_desc *desc = reg_voltage_map[rid];

	/* Delay is required only if the voltage is increasing */
	if (old_selector >= new_selector)
		return 0;

	/* No need to delay if gpio_dvs_mode */
	switch (rid) {
	case MAX8997_BUCK1:
		if (max8997->buck1_gpiodvs)
			return 0;
		break;
	case MAX8997_BUCK2:
		if (max8997->buck2_gpiodvs)
			return 0;
		break;
	case MAX8997_BUCK5:
		if (max8997->buck5_gpiodvs)
			return 0;
		break;
	}

	switch (rid) {
	case MAX8997_BUCK1:
	case MAX8997_BUCK2:
	case MAX8997_BUCK4:
	case MAX8997_BUCK5:
		return DIV_ROUND_UP(desc->step * (new_selector - old_selector),
				    max8997->ramp_delay * 1000);
	}

	return 0;
}

/*
 * Assess the damage on the voltage setting of BUCK1,2,5 by the change.
 *
 * When GPIO-DVS mode is used for multiple bucks, changing the voltage value
 * of one of the bucks may affect that of another buck, which is the side
 * effect of the change (set_voltage). This function examines the GPIO-DVS
 * configurations and checks whether such side-effect exists.
 */
static int max8997_assess_side_effect(struct regulator_dev *rdev,
		u8 new_val, int *best)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	int rid = rdev_get_id(rdev);
	u8 *buckx_val[3];
	bool buckx_gpiodvs[3];
	int side_effect[8];
	int min_side_effect = INT_MAX;
	int i;

	*best = -1;

	switch (rid) {
	case MAX8997_BUCK1:
		rid = 0;
		break;
	case MAX8997_BUCK2:
		rid = 1;
		break;
	case MAX8997_BUCK5:
		rid = 2;
		break;
	default:
		return -EINVAL;
	}

	buckx_val[0] = max8997->buck1_vol;
	buckx_val[1] = max8997->buck2_vol;
	buckx_val[2] = max8997->buck5_vol;
	buckx_gpiodvs[0] = max8997->buck1_gpiodvs;
	buckx_gpiodvs[1] = max8997->buck2_gpiodvs;
	buckx_gpiodvs[2] = max8997->buck5_gpiodvs;

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
				(buckx_val[others])[max8997->buck125_gpioindex];
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
 * max8997_set_voltage_ldobuck to do the job.
 */
static int max8997_set_voltage_buck(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	int rid = rdev_get_id(rdev);
	const struct voltage_map_desc *desc;
	int new_val, new_idx, damage, tmp_val, tmp_idx, tmp_dmg;
	bool gpio_dvs_mode = false;

	if (rid < MAX8997_BUCK1 || rid > MAX8997_BUCK7)
		return -EINVAL;

	switch (rid) {
	case MAX8997_BUCK1:
		if (max8997->buck1_gpiodvs)
			gpio_dvs_mode = true;
		break;
	case MAX8997_BUCK2:
		if (max8997->buck2_gpiodvs)
			gpio_dvs_mode = true;
		break;
	case MAX8997_BUCK5:
		if (max8997->buck5_gpiodvs)
			gpio_dvs_mode = true;
		break;
	}

	if (!gpio_dvs_mode)
		return max8997_set_voltage_ldobuck(rdev, min_uV, max_uV,
						selector);

	desc = reg_voltage_map[rid];
	new_val = max8997_get_voltage_proper_val(desc, min_uV, max_uV);
	if (new_val < 0)
		return new_val;

	tmp_dmg = INT_MAX;
	tmp_idx = -1;
	tmp_val = -1;
	do {
		damage = max8997_assess_side_effect(rdev, new_val, &new_idx);
		if (damage == 0)
			goto out;

		if (tmp_dmg > damage) {
			tmp_idx = new_idx;
			tmp_val = new_val;
			tmp_dmg = damage;
		}

		new_val++;
	} while (desc->min + desc->step * new_val <= desc->max);

	new_idx = tmp_idx;
	new_val = tmp_val;

	if (max8997->ignore_gpiodvs_side_effect == false)
		return -EINVAL;

	dev_warn(&rdev->dev, "MAX8997 GPIO-DVS Side Effect Warning: GPIO SET:"
			" %d -> %d\n", max8997->buck125_gpioindex, tmp_idx);

out:
	if (new_idx < 0 || new_val < 0)
		return -EINVAL;

	max8997->buck125_gpioindex = new_idx;
	max8997_set_gpio(max8997);
	*selector = new_val;

	return 0;
}

/* For SAFEOUT1 and SAFEOUT2 */
static int max8997_set_voltage_safeout_sel(struct regulator_dev *rdev,
					   unsigned selector)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8997->iodev->i2c;
	int rid = rdev_get_id(rdev);
	int reg, shift = 0, mask, ret;

	if (rid != MAX8997_ESAFEOUT1 && rid != MAX8997_ESAFEOUT2)
		return -EINVAL;

	ret = max8997_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	return max8997_update_reg(i2c, reg, selector << shift, mask << shift);
}

static int max8997_reg_disable_suspend(struct regulator_dev *rdev)
{
	struct max8997_data *max8997 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max8997->iodev->i2c;
	int ret, reg, mask, pattern;
	int rid = rdev_get_id(rdev);

	ret = max8997_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	max8997_read_reg(i2c, reg, &max8997->saved_states[rid]);

	if (rid == MAX8997_LDO1 ||
			rid == MAX8997_LDO10 ||
			rid == MAX8997_LDO21) {
		dev_dbg(&rdev->dev, "Conditional Power-Off for %s\n",
				rdev->desc->name);
		return max8997_update_reg(i2c, reg, 0x40, mask);
	}

	dev_dbg(&rdev->dev, "Full Power-Off for %s (%xh -> %xh)\n",
			rdev->desc->name, max8997->saved_states[rid] & mask,
			(~pattern) & mask);
	return max8997_update_reg(i2c, reg, ~pattern, mask);
}

static struct regulator_ops max8997_ldo_ops = {
	.list_voltage		= max8997_list_voltage,
	.is_enabled		= max8997_reg_is_enabled,
	.enable			= max8997_reg_enable,
	.disable		= max8997_reg_disable,
	.get_voltage_sel	= max8997_get_voltage_sel,
	.set_voltage		= max8997_set_voltage_ldobuck,
	.set_suspend_disable	= max8997_reg_disable_suspend,
};

static struct regulator_ops max8997_buck_ops = {
	.list_voltage		= max8997_list_voltage,
	.is_enabled		= max8997_reg_is_enabled,
	.enable			= max8997_reg_enable,
	.disable		= max8997_reg_disable,
	.get_voltage_sel	= max8997_get_voltage_sel,
	.set_voltage		= max8997_set_voltage_buck,
	.set_voltage_time_sel	= max8997_set_voltage_buck_time_sel,
	.set_suspend_disable	= max8997_reg_disable_suspend,
};

static struct regulator_ops max8997_fixedvolt_ops = {
	.list_voltage		= max8997_list_voltage,
	.is_enabled		= max8997_reg_is_enabled,
	.enable			= max8997_reg_enable,
	.disable		= max8997_reg_disable,
	.set_suspend_disable	= max8997_reg_disable_suspend,
};

static struct regulator_ops max8997_safeout_ops = {
	.list_voltage		= regulator_list_voltage_table,
	.is_enabled		= max8997_reg_is_enabled,
	.enable			= max8997_reg_enable,
	.disable		= max8997_reg_disable,
	.get_voltage_sel	= max8997_get_voltage_sel,
	.set_voltage_sel	= max8997_set_voltage_safeout_sel,
	.set_suspend_disable	= max8997_reg_disable_suspend,
};

static struct regulator_ops max8997_fixedstate_ops = {
	.list_voltage		= max8997_list_voltage_charger_cv,
	.get_voltage_sel	= max8997_get_voltage_sel,
	.set_voltage		= max8997_set_voltage_charger_cv,
};

static int max8997_set_current_limit(struct regulator_dev *rdev,
				     int min_uA, int max_uA)
{
	unsigned dummy;
	int rid = rdev_get_id(rdev);

	if (rid != MAX8997_CHARGER && rid != MAX8997_CHARGER_TOPOFF)
		return -EINVAL;

	/* Reuse max8997_set_voltage_ldobuck to set current_limit. */
	return max8997_set_voltage_ldobuck(rdev, min_uA, max_uA, &dummy);
}

static int max8997_get_current_limit(struct regulator_dev *rdev)
{
	int sel, rid = rdev_get_id(rdev);

	if (rid != MAX8997_CHARGER && rid != MAX8997_CHARGER_TOPOFF)
		return -EINVAL;

	sel = max8997_get_voltage_sel(rdev);
	if (sel < 0)
		return sel;

	/* Reuse max8997_list_voltage to get current_limit. */
	return max8997_list_voltage(rdev, sel);
}

static struct regulator_ops max8997_charger_ops = {
	.is_enabled		= max8997_reg_is_enabled,
	.enable			= max8997_reg_enable,
	.disable		= max8997_reg_disable,
	.get_current_limit	= max8997_get_current_limit,
	.set_current_limit	= max8997_set_current_limit,
};

static struct regulator_ops max8997_charger_fixedstate_ops = {
	.get_current_limit	= max8997_get_current_limit,
	.set_current_limit	= max8997_set_current_limit,
};

#define MAX8997_VOLTAGE_REGULATOR(_name, _ops) {\
	.name		= #_name,		\
	.id		= MAX8997_##_name,	\
	.ops		= &_ops,		\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,		\
}

#define MAX8997_CURRENT_REGULATOR(_name, _ops) {\
	.name		= #_name,		\
	.id		= MAX8997_##_name,	\
	.ops		= &_ops,		\
	.type		= REGULATOR_CURRENT,	\
	.owner		= THIS_MODULE,		\
}

static struct regulator_desc regulators[] = {
	MAX8997_VOLTAGE_REGULATOR(LDO1, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO2, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO3, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO4, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO5, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO6, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO7, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO8, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO9, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO10, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO11, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO12, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO13, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO14, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO15, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO16, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO17, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO18, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(LDO21, max8997_ldo_ops),
	MAX8997_VOLTAGE_REGULATOR(BUCK1, max8997_buck_ops),
	MAX8997_VOLTAGE_REGULATOR(BUCK2, max8997_buck_ops),
	MAX8997_VOLTAGE_REGULATOR(BUCK3, max8997_buck_ops),
	MAX8997_VOLTAGE_REGULATOR(BUCK4, max8997_buck_ops),
	MAX8997_VOLTAGE_REGULATOR(BUCK5, max8997_buck_ops),
	MAX8997_VOLTAGE_REGULATOR(BUCK6, max8997_fixedvolt_ops),
	MAX8997_VOLTAGE_REGULATOR(BUCK7, max8997_buck_ops),
	MAX8997_VOLTAGE_REGULATOR(EN32KHZ_AP, max8997_fixedvolt_ops),
	MAX8997_VOLTAGE_REGULATOR(EN32KHZ_CP, max8997_fixedvolt_ops),
	MAX8997_VOLTAGE_REGULATOR(ENVICHG, max8997_fixedvolt_ops),
	MAX8997_VOLTAGE_REGULATOR(ESAFEOUT1, max8997_safeout_ops),
	MAX8997_VOLTAGE_REGULATOR(ESAFEOUT2, max8997_safeout_ops),
	MAX8997_VOLTAGE_REGULATOR(CHARGER_CV, max8997_fixedstate_ops),
	MAX8997_CURRENT_REGULATOR(CHARGER, max8997_charger_ops),
	MAX8997_CURRENT_REGULATOR(CHARGER_TOPOFF,
				  max8997_charger_fixedstate_ops),
};

#ifdef CONFIG_OF
static int max8997_pmic_dt_parse_dvs_gpio(struct platform_device *pdev,
			struct max8997_platform_data *pdata,
			struct device_node *pmic_np)
{
	int i, gpio;

	for (i = 0; i < 3; i++) {
		gpio = of_get_named_gpio(pmic_np,
					"max8997,pmic-buck125-dvs-gpios", i);
		if (!gpio_is_valid(gpio)) {
			dev_err(&pdev->dev, "invalid gpio[%d]: %d\n", i, gpio);
			return -EINVAL;
		}
		pdata->buck125_gpios[i] = gpio;
	}
	return 0;
}

static int max8997_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct max8997_platform_data *pdata)
{
	struct max8997_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct device_node *pmic_np, *regulators_np, *reg_np;
	struct max8997_regulator_data *rdata;
	unsigned int i, dvs_voltage_nr = 1, ret;

	pmic_np = of_node_get(iodev->dev->of_node);
	if (!pmic_np) {
		dev_err(&pdev->dev, "could not find pmic sub-node\n");
		return -ENODEV;
	}

	regulators_np = of_find_node_by_name(pmic_np, "regulators");
	if (!regulators_np) {
		dev_err(&pdev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	/* count the number of regulators to be supported in pmic */
	pdata->num_regulators = of_get_child_count(regulators_np);

	rdata = devm_kzalloc(&pdev->dev, sizeof(*rdata) *
				pdata->num_regulators, GFP_KERNEL);
	if (!rdata) {
		of_node_put(regulators_np);
		dev_err(&pdev->dev, "could not allocate memory for regulator data\n");
		return -ENOMEM;
	}

	pdata->regulators = rdata;
	for_each_child_of_node(regulators_np, reg_np) {
		for (i = 0; i < ARRAY_SIZE(regulators); i++)
			if (!of_node_cmp(reg_np->name, regulators[i].name))
				break;

		if (i == ARRAY_SIZE(regulators)) {
			dev_warn(&pdev->dev, "don't know how to configure regulator %s\n",
				 reg_np->name);
			continue;
		}

		rdata->id = i;
		rdata->initdata = of_get_regulator_init_data(&pdev->dev,
							     reg_np);
		rdata->reg_node = reg_np;
		rdata++;
	}
	of_node_put(regulators_np);

	if (of_get_property(pmic_np, "max8997,pmic-buck1-uses-gpio-dvs", NULL))
		pdata->buck1_gpiodvs = true;

	if (of_get_property(pmic_np, "max8997,pmic-buck2-uses-gpio-dvs", NULL))
		pdata->buck2_gpiodvs = true;

	if (of_get_property(pmic_np, "max8997,pmic-buck5-uses-gpio-dvs", NULL))
		pdata->buck5_gpiodvs = true;

	if (pdata->buck1_gpiodvs || pdata->buck2_gpiodvs ||
						pdata->buck5_gpiodvs) {
		ret = max8997_pmic_dt_parse_dvs_gpio(pdev, pdata, pmic_np);
		if (ret)
			return -EINVAL;

		if (of_property_read_u32(pmic_np,
				"max8997,pmic-buck125-default-dvs-idx",
				&pdata->buck125_default_idx)) {
			pdata->buck125_default_idx = 0;
		} else {
			if (pdata->buck125_default_idx >= 8) {
				pdata->buck125_default_idx = 0;
				dev_info(&pdev->dev, "invalid value for default dvs index, using 0 instead\n");
			}
		}

		if (of_get_property(pmic_np,
			"max8997,pmic-ignore-gpiodvs-side-effect", NULL))
			pdata->ignore_gpiodvs_side_effect = true;

		dvs_voltage_nr = 8;
	}

	if (of_property_read_u32_array(pmic_np,
				"max8997,pmic-buck1-dvs-voltage",
				pdata->buck1_voltage, dvs_voltage_nr)) {
		dev_err(&pdev->dev, "buck1 voltages not specified\n");
		return -EINVAL;
	}

	if (of_property_read_u32_array(pmic_np,
				"max8997,pmic-buck2-dvs-voltage",
				pdata->buck2_voltage, dvs_voltage_nr)) {
		dev_err(&pdev->dev, "buck2 voltages not specified\n");
		return -EINVAL;
	}

	if (of_property_read_u32_array(pmic_np,
				"max8997,pmic-buck5-dvs-voltage",
				pdata->buck5_voltage, dvs_voltage_nr)) {
		dev_err(&pdev->dev, "buck5 voltages not specified\n");
		return -EINVAL;
	}

	return 0;
}
#else
static int max8997_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct max8997_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

static int max8997_pmic_probe(struct platform_device *pdev)
{
	struct max8997_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max8997_platform_data *pdata = iodev->pdata;
	struct regulator_config config = { };
	struct regulator_dev **rdev;
	struct max8997_data *max8997;
	struct i2c_client *i2c;
	int i, ret, size, nr_dvs;
	u8 max_buck1 = 0, max_buck2 = 0, max_buck5 = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}

	if (iodev->dev->of_node) {
		ret = max8997_pmic_dt_parse_pdata(pdev, pdata);
		if (ret)
			return ret;
	}

	max8997 = devm_kzalloc(&pdev->dev, sizeof(struct max8997_data),
			       GFP_KERNEL);
	if (!max8997)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	max8997->rdev = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!max8997->rdev)
		return -ENOMEM;

	rdev = max8997->rdev;
	max8997->dev = &pdev->dev;
	max8997->iodev = iodev;
	max8997->num_regulators = pdata->num_regulators;
	platform_set_drvdata(pdev, max8997);
	i2c = max8997->iodev->i2c;

	max8997->buck125_gpioindex = pdata->buck125_default_idx;
	max8997->buck1_gpiodvs = pdata->buck1_gpiodvs;
	max8997->buck2_gpiodvs = pdata->buck2_gpiodvs;
	max8997->buck5_gpiodvs = pdata->buck5_gpiodvs;
	memcpy(max8997->buck125_gpios, pdata->buck125_gpios, sizeof(int) * 3);
	max8997->ignore_gpiodvs_side_effect = pdata->ignore_gpiodvs_side_effect;

	nr_dvs = (pdata->buck1_gpiodvs || pdata->buck2_gpiodvs ||
			pdata->buck5_gpiodvs) ? 8 : 1;

	for (i = 0; i < nr_dvs; i++) {
		max8997->buck1_vol[i] = ret =
			max8997_get_voltage_proper_val(
					&buck1245_voltage_map_desc,
					pdata->buck1_voltage[i],
					pdata->buck1_voltage[i] +
					buck1245_voltage_map_desc.step);
		if (ret < 0)
			goto err_out;

		max8997->buck2_vol[i] = ret =
			max8997_get_voltage_proper_val(
					&buck1245_voltage_map_desc,
					pdata->buck2_voltage[i],
					pdata->buck2_voltage[i] +
					buck1245_voltage_map_desc.step);
		if (ret < 0)
			goto err_out;

		max8997->buck5_vol[i] = ret =
			max8997_get_voltage_proper_val(
					&buck1245_voltage_map_desc,
					pdata->buck5_voltage[i],
					pdata->buck5_voltage[i] +
					buck1245_voltage_map_desc.step);
		if (ret < 0)
			goto err_out;

		if (max_buck1 < max8997->buck1_vol[i])
			max_buck1 = max8997->buck1_vol[i];
		if (max_buck2 < max8997->buck2_vol[i])
			max_buck2 = max8997->buck2_vol[i];
		if (max_buck5 < max8997->buck5_vol[i])
			max_buck5 = max8997->buck5_vol[i];
	}

	/* For the safety, set max voltage before setting up */
	for (i = 0; i < 8; i++) {
		max8997_update_reg(i2c, MAX8997_REG_BUCK1DVS1 + i,
				max_buck1, 0x3f);
		max8997_update_reg(i2c, MAX8997_REG_BUCK2DVS1 + i,
				max_buck2, 0x3f);
		max8997_update_reg(i2c, MAX8997_REG_BUCK5DVS1 + i,
				max_buck5, 0x3f);
	}

	/* Initialize all the DVS related BUCK registers */
	for (i = 0; i < nr_dvs; i++) {
		max8997_update_reg(i2c, MAX8997_REG_BUCK1DVS1 + i,
				max8997->buck1_vol[i],
				0x3f);
		max8997_update_reg(i2c, MAX8997_REG_BUCK2DVS1 + i,
				max8997->buck2_vol[i],
				0x3f);
		max8997_update_reg(i2c, MAX8997_REG_BUCK5DVS1 + i,
				max8997->buck5_vol[i],
				0x3f);
	}

	/*
	 * If buck 1, 2, and 5 do not care DVS GPIO settings, ignore them.
	 * If at least one of them cares, set gpios.
	 */
	if (pdata->buck1_gpiodvs || pdata->buck2_gpiodvs ||
			pdata->buck5_gpiodvs) {

		if (!gpio_is_valid(pdata->buck125_gpios[0]) ||
				!gpio_is_valid(pdata->buck125_gpios[1]) ||
				!gpio_is_valid(pdata->buck125_gpios[2])) {
			dev_err(&pdev->dev, "GPIO NOT VALID\n");
			ret = -EINVAL;
			goto err_out;
		}

		ret = devm_gpio_request(&pdev->dev, pdata->buck125_gpios[0],
					"MAX8997 SET1");
		if (ret)
			goto err_out;

		ret = devm_gpio_request(&pdev->dev, pdata->buck125_gpios[1],
					"MAX8997 SET2");
		if (ret)
			goto err_out;

		ret = devm_gpio_request(&pdev->dev, pdata->buck125_gpios[2],
				"MAX8997 SET3");
		if (ret)
			goto err_out;

		gpio_direction_output(pdata->buck125_gpios[0],
				(max8997->buck125_gpioindex >> 2)
				& 0x1); /* SET1 */
		gpio_direction_output(pdata->buck125_gpios[1],
				(max8997->buck125_gpioindex >> 1)
				& 0x1); /* SET2 */
		gpio_direction_output(pdata->buck125_gpios[2],
				(max8997->buck125_gpioindex >> 0)
				& 0x1); /* SET3 */
	}

	/* DVS-GPIO disabled */
	max8997_update_reg(i2c, MAX8997_REG_BUCK1CTRL, (pdata->buck1_gpiodvs) ?
			(1 << 1) : (0 << 1), 1 << 1);
	max8997_update_reg(i2c, MAX8997_REG_BUCK2CTRL, (pdata->buck2_gpiodvs) ?
			(1 << 1) : (0 << 1), 1 << 1);
	max8997_update_reg(i2c, MAX8997_REG_BUCK5CTRL, (pdata->buck5_gpiodvs) ?
			(1 << 1) : (0 << 1), 1 << 1);

	/* Misc Settings */
	max8997->ramp_delay = 10; /* set 10mV/us, which is the default */
	max8997_write_reg(i2c, MAX8997_REG_BUCKRAMP, (0xf << 4) | 0x9);

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct voltage_map_desc *desc;
		int id = pdata->regulators[i].id;

		desc = reg_voltage_map[id];
		if (desc) {
			regulators[id].n_voltages =
				(desc->max - desc->min) / desc->step + 1;
		} else if (id == MAX8997_ESAFEOUT1 || id == MAX8997_ESAFEOUT2) {
			regulators[id].volt_table = safeoutvolt;
			regulators[id].n_voltages = ARRAY_SIZE(safeoutvolt);
		} else if (id == MAX8997_CHARGER_CV) {
			regulators[id].n_voltages = 16;
		}

		config.dev = max8997->dev;
		config.init_data = pdata->regulators[i].initdata;
		config.driver_data = max8997;
		config.of_node = pdata->regulators[i].reg_node;

		rdev[i] = regulator_register(&regulators[id], &config);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(max8997->dev, "regulator init failed for %d\n",
					id);
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

static int max8997_pmic_remove(struct platform_device *pdev)
{
	struct max8997_data *max8997 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max8997->rdev;
	int i;

	for (i = 0; i < max8997->num_regulators; i++)
		regulator_unregister(rdev[i]);
	return 0;
}

static const struct platform_device_id max8997_pmic_id[] = {
	{ "max8997-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max8997_pmic_id);

static struct platform_driver max8997_pmic_driver = {
	.driver = {
		.name = "max8997-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max8997_pmic_probe,
	.remove = max8997_pmic_remove,
	.id_table = max8997_pmic_id,
};

static int __init max8997_pmic_init(void)
{
	return platform_driver_register(&max8997_pmic_driver);
}
subsys_initcall(max8997_pmic_init);

static void __exit max8997_pmic_cleanup(void)
{
	platform_driver_unregister(&max8997_pmic_driver);
}
module_exit(max8997_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 8997/8966 Regulator Driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
