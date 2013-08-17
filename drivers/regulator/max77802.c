/*
 * max77802.c - Regulator driver for the Maxim 77802
 *
 * Copyright (C) 2012 Samsung Electronics
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
 * This driver is based on max77686.c
 */

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max77802.h>
#include <linux/mfd/max77802-private.h>

#define PMIC_DEBUG KERN_DEBUG
#define PMIC_REG_DEBUG KERN_DEBUG

#define MAX77802_OPMODE_SHIFT 6
#define MAX77802_OPMODE_BUCK234_SHIFT 4
#define MAX77802_OPMODE_MASK 0x3

#undef DEBUG

enum MAX77802_DEVICE_ID {
	MAX77802_DEVICE_PASS1 = 0x1,
	MAX77802_DEVICE_PASS2 = 0x2,
};

struct max77802_data {
	struct device *dev;
	struct max77802_dev *iodev;
	int num_regulators;
	struct regulator_dev **rdev;
	int ramp_delay; /* in mV/us */
	int device_id;

	struct max77802_opmode_data *opmode_data;

	bool buck1_gpiodvs;
	bool buck2_gpiodvs;
	bool buck3_gpiodvs;
	bool buck4_gpiodvs;
	bool buck6_gpiodvs;
	u8 buck1_vol[8];
	u8 buck2_vol[8];
	u8 buck3_vol[8];
	u8 buck4_vol[8];
	u8 buck6_vol[8];
	int buck12346_gpios_dvs[3];
	int buck12346_gpios_selb[5];
	int buck12346_gpioindex;
	bool ignore_gpiodvs_side_effect;

	u8 saved_states[MAX77802_REG_MAX];
};

static inline void max77802_set_gpio(struct max77802_data *max77802)
{
	int set3 = (max77802->buck12346_gpioindex) & 0x1;
	int set2 = ((max77802->buck12346_gpioindex) >> 1) & 0x1;
	int set1 = ((max77802->buck12346_gpioindex) >> 2) & 0x1;

	if (max77802->buck12346_gpios_dvs[0])
		gpio_set_value(max77802->buck12346_gpios_dvs[0], set1);
	if (max77802->buck12346_gpios_dvs[1])
		gpio_set_value(max77802->buck12346_gpios_dvs[1], set2);
	if (max77802->buck12346_gpios_dvs[2])
		gpio_set_value(max77802->buck12346_gpios_dvs[2], set3);
}

struct voltage_map_desc {
	int min;
	int max;
	int step;
	unsigned int n_bits;
};

/* P-CH LDO - LDO3 ~ 7, 9 ~ 14, 18 ~ 26, 28, 29, 32 ~ 34 (uV) */
static const struct voltage_map_desc pldo_voltage_map_desc = {
	.min = 800000,	.max = 3950000,	.step = 50000,	.n_bits = 6,
};

/* N-CH LDO - LDO1 ~ 2, 8, 15, 17, 27, 30, 35 (uV) */
static const struct voltage_map_desc nldo_voltage_map_desc = {
	.min = 800000,	.max = 2375000,	.step = 25000,	.n_bits = 6,
};

/* Buck1, 6 (uV) */
static const struct voltage_map_desc buck16_dvs_voltage_map_desc = {
	.min = 612500,	.max = 2206250,	.step = 6250,	.n_bits = 8,
};

/* Buck2, 3, 4 (uV) */
static const struct voltage_map_desc buck234_dvs_voltage_map_desc = {
	.min = 600000,	.max = 1500000,	.step = 6250,	.n_bits = 8,
};

/* Buck5, 7 ~ 10 (uV) */
static const struct voltage_map_desc buck_voltage_map_desc = {
	.min = 750000,	.max = 3900000,	.step = 50000,	.n_bits = 6,
};

static const struct voltage_map_desc *reg_voltage_map[] = {
	[MAX77802_LDO1] = &nldo_voltage_map_desc,
	[MAX77802_LDO2] = &nldo_voltage_map_desc,
	[MAX77802_LDO3] = &pldo_voltage_map_desc,
	[MAX77802_LDO4] = &pldo_voltage_map_desc,
	[MAX77802_LDO5] = &pldo_voltage_map_desc,
	[MAX77802_LDO6] = &pldo_voltage_map_desc,
	[MAX77802_LDO7] = &pldo_voltage_map_desc,
	[MAX77802_LDO8] = &nldo_voltage_map_desc,
	[MAX77802_LDO9] = &pldo_voltage_map_desc,
	[MAX77802_LDO10] = &pldo_voltage_map_desc,
	[MAX77802_LDO11] = &pldo_voltage_map_desc,
	[MAX77802_LDO12] = &pldo_voltage_map_desc,
	[MAX77802_LDO13] = &pldo_voltage_map_desc,
	[MAX77802_LDO14] = &pldo_voltage_map_desc,
	[MAX77802_LDO15] = &nldo_voltage_map_desc,
	[MAX77802_LDO17] = &nldo_voltage_map_desc,
	[MAX77802_LDO18] = &pldo_voltage_map_desc,
	[MAX77802_LDO19] = &pldo_voltage_map_desc,
	[MAX77802_LDO20] = &pldo_voltage_map_desc,
	[MAX77802_LDO21] = &pldo_voltage_map_desc,
	[MAX77802_LDO23] = &pldo_voltage_map_desc,
	[MAX77802_LDO24] = &pldo_voltage_map_desc,
	[MAX77802_LDO25] = &pldo_voltage_map_desc,
	[MAX77802_LDO26] = &pldo_voltage_map_desc,
	[MAX77802_LDO27] = &nldo_voltage_map_desc,
	[MAX77802_LDO28] = &pldo_voltage_map_desc,
	[MAX77802_LDO29] = &pldo_voltage_map_desc,
	[MAX77802_LDO30] = &nldo_voltage_map_desc,
	[MAX77802_LDO32] = &pldo_voltage_map_desc,
	[MAX77802_LDO33] = &pldo_voltage_map_desc,
	[MAX77802_LDO34] = &pldo_voltage_map_desc,
	[MAX77802_LDO35] = &nldo_voltage_map_desc,
	[MAX77802_BUCK1] = &buck16_dvs_voltage_map_desc,
	[MAX77802_BUCK2] = &buck234_dvs_voltage_map_desc,
	[MAX77802_BUCK3] = &buck234_dvs_voltage_map_desc,
	[MAX77802_BUCK4] = &buck234_dvs_voltage_map_desc,
	[MAX77802_BUCK5] = &buck_voltage_map_desc,
	[MAX77802_BUCK6] = &buck16_dvs_voltage_map_desc,
	[MAX77802_BUCK7] = &buck_voltage_map_desc,
	[MAX77802_BUCK8] = &buck_voltage_map_desc,
	[MAX77802_BUCK9] = &buck_voltage_map_desc,
	[MAX77802_BUCK10] = &buck_voltage_map_desc,
#if 0
	[MAX77802_EN32KHZ_AP] = NULL,
	[MAX77802_EN32KHZ_CP] = NULL,
#endif
};

static int max77802_list_voltage(struct regulator_dev *rdev,
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

/*
 * TODO
 * Reaction to the LP/Standby for each regulator should be defined by
 * each consumer, not by the regulator device driver if it depends
 * on which device is attached to which regulator. Here we are
 * creating possible PM-wise regression with board changes.Also,
 * we can do the same effect without creating issues with board
 * changes by carefully defining .state_mem at bsp and suspend ops
 * callbacks.
 */
#if 0
unsigned int max77802_opmode_reg[][3] = {
	/* LDO1 ... LDO35 */
	/* {NORMAL, LP, STANDBY} */
	{0x3, 0x2, 0x0}, /* LDO1 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1}, /* LDO11 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1}, /* LDO15 */
	{0x3, 0x2, 0x1}, /* LDO17 */
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0}, /* LDO21 */
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0}, /* LDO30 */
	{0x3, 0x2, 0x0}, /* LDO32 */
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0}, /* LDO35 */
	/* BUCK1 ... BUCK10 */
	{0x3, 0x0, 0x1}, /* BUCK1 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0}, /* BUCK10 */
#if 0
	/* 32KHZ */
	{0x1, 0x0, 0x0},
	{0x1, 0x0, 0x0},
#endif
};
#else
unsigned int max77802_opmode_reg[][3] = {
	/* LDO1 ... LDO35 */
	/* {NORMAL, LP, STANDBY} */
	{0x3, 0x2, 0x1}, /* LDO1 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1}, /* LDO11 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1}, /* LDO15 */
	{0x3, 0x2, 0x1}, /* LDO17 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1}, /* LDO21 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1}, /* LDO30 */
	{0x3, 0x2, 0x1}, /* LDO32 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1}, /* LDO35 */
	/* BUCK1 ... BUCK10 */
	{0x3, 0x1, 0x1}, /* BUCK1 */
	{0x3, 0x1, 0x1},
	{0x3, 0x1, 0x1},
	{0x3, 0x1, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x1, 0x1},
	{0x3, 0x1, 0x1},
	{0x3, 0x1, 0x1},
	{0x3, 0x1, 0x1},
	{0x3, 0x1, 0x1}, /* BUCK10 */
#if 0
	/* 32KHZ */
	{0x1, 0x0, 0x0},
	{0x1, 0x0, 0x0},
#endif
};
#endif
static int max77802_get_enable_register(struct regulator_dev *rdev,
		int *reg, int *mask, int *pattern)
{
	unsigned int rid = rdev_get_id(rdev);
	unsigned int mode;
	struct max77802_data *max77802 = rdev_get_drvdata(rdev);

	if (rid >= ARRAY_SIZE(max77802_opmode_reg))
		return -EINVAL;

	mode = max77802->opmode_data[rid].mode;
	pr_debug("%s: rid=%d, mode=%d, size=%d\n",
		__func__, rid, mode, ARRAY_SIZE(max77802_opmode_reg));

	if (max77802_opmode_reg[rid][mode] == 0x0)
		WARN(1, "Not supported opmode\n");

	switch (rid) {
	case MAX77802_LDO1 ... MAX77802_LDO15:
		*reg = MAX77802_REG_LDO1CTRL1 + (rid - MAX77802_LDO1);
		*mask = MAX77802_OPMODE_MASK << MAX77802_OPMODE_SHIFT;
		*pattern = max77802_opmode_reg[rid][mode] << MAX77802_OPMODE_SHIFT;
		break;
	case MAX77802_LDO17 ... MAX77802_LDO21:
		*reg = MAX77802_REG_LDO17CTRL1 + (rid - MAX77802_LDO17);
		*mask = MAX77802_OPMODE_MASK << MAX77802_OPMODE_SHIFT;
		*pattern = max77802_opmode_reg[rid][mode] << MAX77802_OPMODE_SHIFT;
		break;
	case MAX77802_LDO23 ... MAX77802_LDO30:
		*reg = MAX77802_REG_LDO23CTRL1 + (rid - MAX77802_LDO23);
		*mask = MAX77802_OPMODE_MASK << MAX77802_OPMODE_SHIFT;
		*pattern = max77802_opmode_reg[rid][mode] << MAX77802_OPMODE_SHIFT;
		break;
	case MAX77802_LDO32 ... MAX77802_LDO35:
		*reg = MAX77802_REG_LDO32CTRL1 + (rid - MAX77802_LDO32);
		*mask = MAX77802_OPMODE_MASK << MAX77802_OPMODE_SHIFT;
		*pattern = max77802_opmode_reg[rid][mode] << MAX77802_OPMODE_SHIFT;
		break;

	case MAX77802_BUCK1:
		*reg = MAX77802_REG_BUCK1CTRL;
		*mask = MAX77802_OPMODE_MASK;
		*pattern = max77802_opmode_reg[rid][mode];
		break;
	case MAX77802_BUCK2:
		*reg = MAX77802_REG_BUCK2CTRL1;
		*mask = MAX77802_OPMODE_MASK << MAX77802_OPMODE_BUCK234_SHIFT;
		*pattern = max77802_opmode_reg[rid][mode] << MAX77802_OPMODE_BUCK234_SHIFT;
		break;
	case MAX77802_BUCK3:
		*reg = MAX77802_REG_BUCK3CTRL1;
		*mask = MAX77802_OPMODE_MASK << MAX77802_OPMODE_BUCK234_SHIFT;
		*pattern = max77802_opmode_reg[rid][mode] << MAX77802_OPMODE_BUCK234_SHIFT;
		break;
	case MAX77802_BUCK4:
		*reg = MAX77802_REG_BUCK4CTRL1;
		*mask = MAX77802_OPMODE_MASK << MAX77802_OPMODE_BUCK234_SHIFT;
		*pattern = max77802_opmode_reg[rid][mode] << MAX77802_OPMODE_BUCK234_SHIFT;
		break;
	case MAX77802_BUCK5:
		*reg = MAX77802_REG_BUCK5CTRL;
		*mask = MAX77802_OPMODE_MASK;
		*pattern = max77802_opmode_reg[rid][mode];
		break;
	case MAX77802_BUCK6:
		*reg = MAX77802_REG_BUCK6CTRL;
		*mask = MAX77802_OPMODE_MASK;
		*pattern = max77802_opmode_reg[rid][mode];
		break;
	case MAX77802_BUCK7 ... MAX77802_BUCK10:
		*reg = MAX77802_REG_BUCK7CTRL + (rid - MAX77802_BUCK7) * 3;
		*mask = MAX77802_OPMODE_MASK;
		*pattern = max77802_opmode_reg[rid][mode];
		break;
#if 0
	case MAX77802_EN32KHZ_AP:
	case MAX77802_EN32KHZ_CP:
		*reg = MAX77802_REG_32KHZ;
		*mask = 0x01 << (rid - MAX77802_EN32KHZ_AP);
		*pattern = 0x01 << (rid - MAX77802_EN32KHZ_AP);
		break;
#endif
	default:
		/* Not controllable or not exists */
		return -EINVAL;
	}

	return 0;
}

static int max77802_reg_is_enabled(struct regulator_dev *rdev)
{
	struct max77802_data *max77802 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77802->iodev->i2c;
	int ret, reg, mask, pattern;
	u8 val;

	ret = max77802_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret == -EINVAL)
		return 1; /* "not controllable" */
	else if (ret)
		return ret;

	ret = max77802_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	pr_debug("%s: id=%d, ret=%d, val=%x, mask=%x, pattern=%x\n",
		__func__, rdev_get_id(rdev), (val & mask) == pattern,
		val, mask, pattern);

	return (val & mask) == pattern;
}

static int max77802_reg_enable(struct regulator_dev *rdev)
{
	struct max77802_data *max77802 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77802->iodev->i2c;
	int ret, reg, mask, pattern;

	ret = max77802_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	pr_debug("%s: id=%d, pattern=%x\n",
		__func__, rdev_get_id(rdev), pattern);

	return max77802_update_reg(i2c, reg, pattern, mask);

}

static int max77802_reg_disable(struct regulator_dev *rdev)
{
	struct max77802_data *max77802 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77802->iodev->i2c;
	int ret, reg, mask, pattern;

	ret = max77802_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	pr_debug("%s: id=%d, pattern=%x\n",
		__func__, rdev_get_id(rdev), pattern);

	return max77802_update_reg(i2c, reg, ~mask, mask);
}

static int max77802_get_voltage_register(struct regulator_dev *rdev,
		int *_reg, int *_shift, int *_mask)
{
	int rid = rdev_get_id(rdev);
	int reg, shift = 0, mask = 0x3f;

	switch (rid) {
	case MAX77802_LDO1 ... MAX77802_LDO15:
		reg = MAX77802_REG_LDO1CTRL1 + (rid - MAX77802_LDO1);
		break;
	case MAX77802_LDO17 ... MAX77802_LDO21:
		reg = MAX77802_REG_LDO17CTRL1 + (rid - MAX77802_LDO17);
		break;
	case MAX77802_LDO23 ... MAX77802_LDO30:
		reg = MAX77802_REG_LDO23CTRL1 + (rid - MAX77802_LDO23);
		break;
	case MAX77802_LDO32 ... MAX77802_LDO35:
		reg = MAX77802_REG_LDO32CTRL1 + (rid - MAX77802_LDO32);
		break;
	case MAX77802_BUCK1:
		/* TODO: DVS1 -> DVS2 */
		reg = MAX77802_REG_BUCK1DVS1;
		mask = 0xff;
		break;
	case MAX77802_BUCK2:
		reg = MAX77802_REG_BUCK2DVS1;
		mask = 0xff;
		break;
	case MAX77802_BUCK3:
		reg = MAX77802_REG_BUCK3DVS1;
		mask = 0xff;
		break;
	case MAX77802_BUCK4:
		reg = MAX77802_REG_BUCK4DVS1;
		mask = 0xff;
		break;
	case MAX77802_BUCK5:
		reg = MAX77802_REG_BUCK5OUT;
		break;
	case MAX77802_BUCK6:
		reg = MAX77802_REG_BUCK6DVS1;
		mask = 0xff;
		break;
	case MAX77802_BUCK7 ... MAX77802_BUCK10:
		reg = MAX77802_REG_BUCK7OUT + (rid - MAX77802_BUCK7) * 3;
		break;
	default:
		return -EINVAL;
	}

	*_reg = reg;
	*_shift = shift;
	*_mask = mask;

	return 0;
}

static int max77802_get_voltage(struct regulator_dev *rdev)
{
	struct max77802_data *max77802 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77802->iodev->i2c;
	int reg, shift, mask, ret;
	int rid = rdev_get_id(rdev);
	u8 val;

	ret = max77802_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	if ((rid == MAX77802_BUCK1 && max77802->buck1_gpiodvs) ||
		(rid == MAX77802_BUCK2 && max77802->buck2_gpiodvs) ||
		(rid == MAX77802_BUCK3 && max77802->buck3_gpiodvs) ||
		(rid == MAX77802_BUCK4 && max77802->buck4_gpiodvs) ||
		(rid == MAX77802_BUCK6 && max77802->buck6_gpiodvs))
		reg += max77802->buck12346_gpioindex;

	ret = max77802_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	val >>= shift;
	val &= mask;

	pr_debug("%s: id=%d, val=%x\n",	__func__, rid, val);

	return max77802_list_voltage(rdev, val);
}

static inline int max77802_get_voltage_proper_val(
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

static int max77802_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max77802_data *max77802 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77802->iodev->i2c;
	const struct voltage_map_desc *desc;
	int rid = rdev_get_id(rdev);
	int reg, shift = 0, mask, ret;
	int i;
	u8 org;

	desc = reg_voltage_map[rid];

	i = max77802_get_voltage_proper_val(desc, min_uV, max_uV);
	if (i < 0)
		return i;

	ret = max77802_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	/* TODO: If GPIO-DVS is being used, this won't work. */

	max77802_read_reg(i2c, reg, &org);
	org = (org & mask) >> shift;

	pr_debug("max77802: id=%d, org=%x, val=%x\n", rdev_get_id(rdev), org, i);

	ret = max77802_update_reg(i2c, reg, i << shift, mask << shift);
	*selector = i;

	switch (rid) {
	case MAX77802_BUCK2 ... MAX77802_BUCK4:
		if (org < i)
			udelay(DIV_ROUND_UP(desc->step * (i - org),
						max77802->ramp_delay * 1000));
		break;
	case MAX77802_BUCK1:
	case MAX77802_BUCK6:
	if (org < i)
		udelay(DIV_ROUND_UP(desc->step * (i - org),
			max77802->ramp_delay * 1000));
	break;
	case MAX77802_BUCK5:
	case MAX77802_BUCK7 ... MAX77802_BUCK10:
		/* Unconditionally 100 mV/us */
		if (org < i)
			udelay(DIV_ROUND_UP(desc->step * (i - org), 100000));
		break;
	default:
		break;
	}
	return ret;
}

static int max77802_reg_do_nothing(struct regulator_dev *rdev)
{
	return 0;
}

static struct regulator_ops max77802_ldo_ops = {
	.list_voltage		= max77802_list_voltage,
	.is_enabled		= max77802_reg_is_enabled,
	.enable			= max77802_reg_enable,
	.disable		= max77802_reg_disable,
	.get_voltage		= max77802_get_voltage,
	.set_voltage		= max77802_set_voltage,
	.set_suspend_enable	= max77802_reg_do_nothing,
	.set_suspend_disable	= max77802_reg_do_nothing,
};

static struct regulator_ops max77802_buck_ops = {
	.list_voltage		= max77802_list_voltage,
	.is_enabled		= max77802_reg_is_enabled,
	.enable			= max77802_reg_enable,
	.disable		= max77802_reg_disable,
	.get_voltage		= max77802_get_voltage,
	.set_voltage		= max77802_set_voltage,
	.set_suspend_enable	= max77802_reg_do_nothing,
	.set_suspend_disable	= max77802_reg_disable,
};

#if 0
static struct regulator_ops max77802_fixedvolt_ops = {
	.list_voltage		= max77802_list_voltage,
	.is_enabled		= max77802_reg_is_enabled,
	.enable			= max77802_reg_enable,
	.disable		= max77802_reg_disable,
	.set_suspend_enable	= max77802_reg_do_nothing,
	.set_suspend_disable	= max77802_reg_disable,
};
#endif

#define regulator_desc_ldo(num)		{	\
	.name		= "LDO"#num,		\
	.id		= MAX77802_LDO##num,	\
	.ops		= &max77802_ldo_ops,	\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,		\
}
#define regulator_desc_buck(num)		{	\
	.name		= "BUCK"#num,		\
	.id		= MAX77802_BUCK##num,	\
	.ops		= &max77802_buck_ops,	\
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
	regulator_desc_ldo(17),
	regulator_desc_ldo(18),
	regulator_desc_ldo(19),
	regulator_desc_ldo(20),
	regulator_desc_ldo(21),
	regulator_desc_ldo(23),
	regulator_desc_ldo(24),
	regulator_desc_ldo(25),
	regulator_desc_ldo(26),
	regulator_desc_ldo(27),
	regulator_desc_ldo(28),
	regulator_desc_ldo(29),
	regulator_desc_ldo(30),
	regulator_desc_ldo(32),
	regulator_desc_ldo(33),
	regulator_desc_ldo(34),
	regulator_desc_ldo(35),
	regulator_desc_buck(1),
	regulator_desc_buck(2),
	regulator_desc_buck(3),
	regulator_desc_buck(4),
	regulator_desc_buck(5),
	regulator_desc_buck(6),
	regulator_desc_buck(7),
	regulator_desc_buck(8),
	regulator_desc_buck(9),
	regulator_desc_buck(10),
#if 0
	{
		.name	= "EN32KHz AP",
		.id	= MAX77802_EN32KHZ_AP,
		.ops	= &max77802_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "EN32KHz CP",
		.id	= MAX77802_EN32KHZ_CP,
		.ops	= &max77802_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	},
#endif
};

static int max77802_set_ramp_rate(struct i2c_client *i2c, int rate)
{
	int ramp_delay = 0;
	u8 data = 0;

	switch (rate) {
	case MAX77802_RAMP_RATE_100MV:
		ramp_delay = 100;
		data = MAX77802_REG_BUCK234_RAMP_RATE_100MV;
		break;
	case MAX77802_RAMP_RATE_50MV:
		ramp_delay = 50;
		data = MAX77802_REG_BUCK234_RAMP_RATE_50MV;
		break;
	case MAX77802_RAMP_RATE_25MV:
		ramp_delay = 25;
		data = MAX77802_REG_BUCK234_RAMP_RATE_25MV;
		break;
	case MAX77802_RAMP_RATE_12P5MV:
		ramp_delay = 13;
		data = MAX77802_REG_BUCK234_RAMP_RATE_12P5MV;
		break;
	}
	pr_debug("%s: ramp_delay=%d, data=0x%x\n", __func__, ramp_delay, data);

	max77802_update_reg(i2c, MAX77802_REG_BUCK2CTRL1, data, 0xC0);
	max77802_update_reg(i2c, MAX77802_REG_BUCK3CTRL1, data, 0xC0);
	max77802_update_reg(i2c, MAX77802_REG_BUCK4CTRL1, data, 0xC0);

	switch (rate) {
	case MAX77802_RAMP_RATE_100MV:
		ramp_delay = 100;
		data = MAX77802_REG_BUCK16_RAMP_RATE_100MV;
		break;
	case MAX77802_RAMP_RATE_50MV:
		ramp_delay = 50;
		data = MAX77802_REG_BUCK16_RAMP_RATE_50MV;
		break;
	case MAX77802_RAMP_RATE_25MV:
		ramp_delay = 25;
		data = MAX77802_REG_BUCK16_RAMP_RATE_25MV;
		break;
	case MAX77802_RAMP_RATE_12P5MV:
		ramp_delay = 13;
		data = MAX77802_REG_BUCK16_RAMP_RATE_12P5MV;
		break;
	}
	max77802_update_reg(i2c, MAX77802_REG_BUCK1CTRL, data, 0xF0);
	max77802_update_reg(i2c, MAX77802_REG_BUCK6CTRL, data, 0xF0);

	return ramp_delay;
}

static __devinit int max77802_pmic_probe(struct platform_device *pdev)
{
	struct max77802_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77802_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct regulator_dev **rdev;
	struct max77802_data *max77802;
	struct i2c_client *i2c;
	int i, ret, size;
	u8 data = 0;

	pr_debug("%s\n", __func__);

	if (!pdata) {
		dev_err(pdev->dev.parent, "No platform init data supplied.\n");
		return -ENODEV;
	}

	max77802 = kzalloc(sizeof(struct max77802_data), GFP_KERNEL);
	if (!max77802)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	max77802->rdev = kzalloc(size, GFP_KERNEL);
	if (!max77802->rdev) {
		kfree(max77802);
		return -ENOMEM;
	}

	rdev = max77802->rdev;
	max77802->dev = &pdev->dev;
	max77802->iodev = iodev;
	max77802->num_regulators = pdata->num_regulators;
	platform_set_drvdata(pdev, max77802);
	i2c = max77802->iodev->i2c;

	max77802->opmode_data = pdata->opmode_data;
	max77802->ramp_delay = max77802_set_ramp_rate(i2c, pdata->ramp_rate);

	max77802_read_reg(i2c, MAX77802_REG_DEVICE_ID, &data);
	max77802->device_id = (data & 0x7);
	pr_debug("%s: DEVICE ID=0x%x\n", __func__, data);

	/*
	 * TODO
	 * This disables GPIO-DVS. Later we may need to implement GPIO-DVS..
	 * or we do not?
	 */
	max77802->buck1_gpiodvs = false;
	max77802->buck2_gpiodvs = false;
	max77802->buck3_gpiodvs = false;
	max77802->buck4_gpiodvs = false;
	max77802->buck6_gpiodvs = false;

/* TODO */
#if 0
	for (i = 0; i < 3; i++) {
		char buf[80];

		sprintf(buf, "MAX77802 DVS%d", i);

		if (gpio_is_valid(pdata->buck12346_gpio_dvs[i].gpio)) {
			max77802->buck12346_gpios_dvs[i] =
				pdata->buck12346_gpio_dvs[i].gpio;
			gpio_request(pdata->buck12346_gpio_dvs[i].gpio, buf);
			gpio_direction_output(pdata->buck12346_gpio_dvs[i].gpio,
				pdata->buck12346_gpio_dvs[i].data);
		} else {
			dev_info(&pdev->dev, "GPIO %s ignored (%d)\n",
				 buf, pdata->buck12346_gpio_dvs[i].gpio);
		}
	}

	for (i = 0; i < 5; i++) {
		char buf[80];
		sprintf(buf, "MAX77802 SELB%d", i);

		if (gpio_is_valid(pdata->buck12346_gpio_selb[i])) {
			int data = (max77802->device_id <= MAX77802_DEVICE_PASS1) ? 1 : 0;
			max77802->buck12346_gpios_selb[i] =
				pdata->buck12346_gpio_selb[i];
			gpio_request(pdata->buck12346_gpio_selb[i], buf);
			gpio_direction_output(pdata->buck12346_gpio_selb[i], data);
		} else {
			dev_info(&pdev->dev, "GPIO %s ignored (%d)\n",
				 buf, pdata->buck12346_gpio_selb[i]);
		}
	}

	max77802->buck12346_gpioindex = 0;

	for (i = 0; i < 8; i++) {
		ret = max77802_get_voltage_proper_val(
				&buck16_dvs_voltage_map_desc,
				pdata->buck1_voltage[i],
				pdata->buck1_voltage[i]
					+ buck16_dvs_voltage_map_desc.step);
		/* 1.1V as default for safety */
		if (ret < 0)
			max77802->buck1_vol[i] = 0x4E;
		else
			max77802->buck1_vol[i] = ret;
		max77802_write_reg(i2c, MAX77802_REG_BUCK1DVS1 + i,
				   max77802->buck1_vol[i]);

		ret = max77802_get_voltage_proper_val(
				&buck234_dvs_voltage_map_desc,
				pdata->buck2_voltage[i],
				pdata->buck2_voltage[i]
					+ buck234_dvs_voltage_map_desc.step);
		/* 1.1V as default for safety */
		if (ret < 0)
			max77802->buck2_vol[i] = 0x28;
		else
			max77802->buck2_vol[i] = ret;
		max77802_write_reg(i2c, MAX77802_REG_BUCK2DVS1 + i,
				   max77802->buck2_vol[i]);

		ret = max77802_get_voltage_proper_val(
				&buck234_dvs_voltage_map_desc,
				pdata->buck3_voltage[i],
				pdata->buck3_voltage[i]
					+ buck234_dvs_voltage_map_desc.step);
		/* 1.1V as default for safety */
		if (ret < 0)
			max77802->buck3_vol[i] = 0x28;
		else
			max77802->buck3_vol[i] = ret;
		max77802_write_reg(i2c, MAX77802_REG_BUCK3DVS1 + i,
				   max77802->buck3_vol[i]);

		ret = max77802_get_voltage_proper_val(
				&buck234_dvs_voltage_map_desc,
				pdata->buck4_voltage[i],
				pdata->buck4_voltage[i]
					+ buck234_dvs_voltage_map_desc.step);
		/* 1.1V as default for safety */
		if (ret < 0)
			max77802->buck4_vol[i] = 0x28;
		else
			max77802->buck4_vol[i] = ret;
		max77802_write_reg(i2c, MAX77802_REG_BUCK4DVS1 + i,
				   max77802->buck4_vol[i]);

		ret = max77802_get_voltage_proper_val(
				&buck16_dvs_voltage_map_desc,
				pdata->buck6_voltage[i],
				pdata->buck6_voltage[i]
					+ buck16_dvs_voltage_map_desc.step);
		/* 1.1V as default for safety */
		if (ret < 0)
			max77802->buck6_vol[i] = 0x4E;
		else
			max77802->buck6_vol[i] = ret;
		max77802_write_reg(i2c, MAX77802_REG_BUCK6DVS1 + i,
				   max77802->buck6_vol[i]);
	}
#endif

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct voltage_map_desc *desc;
		int id = pdata->regulators[i].id;

		desc = reg_voltage_map[id];
		if (desc) {
			regulators[id].n_voltages =
				(desc->max - desc->min) / desc->step + 1;

			pr_debug("%s: desc=%p, id=%d, n_vol=%d, max=%d, min=%d, step=%d\n",
					__func__, desc, id, regulators[id].n_voltages,
					desc->max, desc->min, desc->step);
		}

		rdev[i] = regulator_register(&regulators[id], max77802->dev,
				pdata->regulators[i].initdata, max77802, NULL);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(max77802->dev, "regulator init failed for %d\n",
					id);
			rdev[i] = NULL;
			goto err;
		}
	}

	return 0;
err:
	for (i = 0; i < 3; i++) {
		if (max77802->buck12346_gpios_dvs[i])
			gpio_free(max77802->buck12346_gpios_dvs[i]);
	}
	for (i = 0; i < 5; i++) {
		if (max77802->buck12346_gpios_dvs[i])
			gpio_free(max77802->buck12346_gpios_selb[i]);
	}

	for (i = 0; i < max77802->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max77802->rdev);
	kfree(max77802);

	return ret;
}

static int __devexit max77802_pmic_remove(struct platform_device *pdev)
{
	struct max77802_data *max77802 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max77802->rdev;
	int i;

	for (i = 0; i < 3; i++) {
		if (max77802->buck12346_gpios_dvs[i])
			gpio_free(max77802->buck12346_gpios_dvs[i]);
	}
	for (i = 0; i < 5; i++) {
		if (max77802->buck12346_gpios_dvs[i])
			gpio_free(max77802->buck12346_gpios_selb[i]);
	}

	for (i = 0; i < max77802->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max77802->rdev);
	kfree(max77802);

	return 0;
}

static const struct platform_device_id max77802_pmic_id[] = {
	{ "max77802-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77802_pmic_id);

static struct platform_driver max77802_pmic_driver = {
	.driver = {
		.name = "max77802-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max77802_pmic_probe,
	.remove = __devexit_p(max77802_pmic_remove),
	.id_table = max77802_pmic_id,
};

static int __init max77802_pmic_init(void)
{
	pr_debug("%s\n", __func__);

	return platform_driver_register(&max77802_pmic_driver);
}
subsys_initcall(max77802_pmic_init);

static void __exit max77802_pmic_cleanup(void)
{
	platform_driver_unregister(&max77802_pmic_driver);
}
module_exit(max77802_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 77802 Regulator Driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_AUTHOR("Kangwon Lee <kw4.lee@samsung.com>");
MODULE_LICENSE("GPL");
