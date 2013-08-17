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
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>

#define MAX77686_OPMODE_SHIFT 6
#define MAX77686_OPMODE_BUCK234_SHIFT 4
#define MAX77686_OPMODE_MASK 0x3

#define MAX77686_DVS_VOL_COMP 50000

#define MAX77686_CHIPREV_MASK		0x7
#define MAX77686_VERSION_MASK		0x78

enum MAX77686_CHIPREV {
	CHIPREV_MAX77686_PASS1 = 0x1,
	CHIPREV_MAX77686_PASS2 = 0x2,
};

enum MAX77686_VERSION {
	VERSION_MAX77686,
	VERSION_MAX77686A,
};

struct max77686_data {
	struct device *dev;
	struct max77686_dev *iodev;
	int num_regulators;
	struct regulator_dev **rdev;
	int ramp_delay; /* in mV/us */
	u8 version;
	u8 chip_rev;

	struct max77686_opmode_data *opmode_data;

	bool buck2_gpiodvs;
	bool buck3_gpiodvs;
	bool buck4_gpiodvs;
	u8 buck2_vol[8];
	u8 buck3_vol[8];
	u8 buck4_vol[8];
	int buck234_gpios_dvs[3];
	char *buck234_gpios_dvs_label[3];
	int buck234_gpios_selb[3];
	char *buck234_gpios_selb_label[3];
	int buck234_gpioindex;
	bool ignore_gpiodvs_side_effect;

	u8 saved_states[MAX77686_REG_MAX];
};

static inline void max77686_set_gpio(struct max77686_data *max77686)
{
	int set3 = (max77686->buck234_gpioindex) & 0x1;
	int set2 = ((max77686->buck234_gpioindex) >> 1) & 0x1;
	int set1 = ((max77686->buck234_gpioindex) >> 2) & 0x1;

	if (max77686->buck234_gpios_dvs[0])
		gpio_set_value(max77686->buck234_gpios_dvs[0], set1);
	if (max77686->buck234_gpios_dvs[1])
		gpio_set_value(max77686->buck234_gpios_dvs[1], set2);
	if (max77686->buck234_gpios_dvs[2])
		gpio_set_value(max77686->buck234_gpios_dvs[2], set3);
}

struct voltage_map_desc {
	int min;
	int max;
	int step;
	unsigned int n_bits;
};

/* LDO3 ~ 5, 9 ~ 14, 16 ~ 26 (uV) */
static struct voltage_map_desc ldo_voltage_map_desc = {
	.min = 800000,	.max = 3950000,	.step = 50000,	.n_bits = 6,
};

/* LDO1 ~ 2, 6 ~ 8, 15 (uV) */
static struct voltage_map_desc ldo_low_voltage_map_desc = {
	.min = 800000,	.max = 2375000,	.step = 25000,	.n_bits = 6,
};

/* Buck2, 3, 4 (uV) */
static struct voltage_map_desc buck_dvs_voltage_map_desc = {
	.min = 600000,	.max = 3787500,	.step = 12500,	.n_bits = 8,
};

/* Buck1, 5 ~ 9 (uV) */
static struct voltage_map_desc buck_voltage_map_desc = {
	.min = 750000,	.max = 3900000,	.step = 50000,	.n_bits = 6,
};

/* Buck1 (uV) for MAX77686A */
static struct voltage_map_desc buck1_voltage_map_desc = {
	.min = 750000,	.max = 1537500,	.step = 12500,	.n_bits = 6,
};

static struct voltage_map_desc *reg_voltage_map[] = {
	[MAX77686_LDO1] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO2] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO3] = &ldo_voltage_map_desc,
	[MAX77686_LDO4] = &ldo_voltage_map_desc,
	[MAX77686_LDO5] = &ldo_voltage_map_desc,
	[MAX77686_LDO6] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO7] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO8] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO9] = &ldo_voltage_map_desc,
	[MAX77686_LDO10] = &ldo_voltage_map_desc,
	[MAX77686_LDO11] = &ldo_voltage_map_desc,
	[MAX77686_LDO12] = &ldo_voltage_map_desc,
	[MAX77686_LDO13] = &ldo_voltage_map_desc,
	[MAX77686_LDO14] = &ldo_voltage_map_desc,
	[MAX77686_LDO15] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO16] = &ldo_voltage_map_desc,
	[MAX77686_LDO17] = &ldo_voltage_map_desc,
	[MAX77686_LDO18] = &ldo_voltage_map_desc,
	[MAX77686_LDO19] = &ldo_voltage_map_desc,
	[MAX77686_LDO20] = &ldo_voltage_map_desc,
	[MAX77686_LDO21] = &ldo_voltage_map_desc,
	[MAX77686_LDO22] = &ldo_voltage_map_desc,
	[MAX77686_LDO23] = &ldo_voltage_map_desc,
	[MAX77686_LDO24] = &ldo_voltage_map_desc,
	[MAX77686_LDO25] = &ldo_voltage_map_desc,
	[MAX77686_LDO26] = &ldo_voltage_map_desc,
	[MAX77686_BUCK1] = &buck_voltage_map_desc,
	[MAX77686_BUCK2] = &buck_dvs_voltage_map_desc,
	[MAX77686_BUCK3] = &buck_dvs_voltage_map_desc,
	[MAX77686_BUCK4] = &buck_dvs_voltage_map_desc,
	[MAX77686_BUCK5] = &buck_voltage_map_desc,
	[MAX77686_BUCK6] = &buck_voltage_map_desc,
	[MAX77686_BUCK7] = &buck_voltage_map_desc,
	[MAX77686_BUCK8] = &buck_voltage_map_desc,
	[MAX77686_BUCK9] = &buck_voltage_map_desc,
	[MAX77686_EN32KHZ_AP] = NULL,
	[MAX77686_EN32KHZ_CP] = NULL,
	[MAX77686_P32KH] = NULL,
};

static int max77686_list_voltage(struct regulator_dev *rdev,
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
unsigned int max77686_opmode_reg[][3] = {
	/* LDO1 ... LDO26 */
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
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0}, /* LDO21 */
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	{0x3, 0x2, 0x0},
	/* BUCK1 ... BUCK9 */
	{0x3, 0x0, 0x1}, /* BUCK1 */
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x2, 0x1},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0},
	{0x3, 0x0, 0x0},
	/* 32KHZ */
	{0x1, 0x0, 0x0},
	{0x1, 0x0, 0x0},
	{0x1, 0x0, 0x0},
};

static int max77686_get_enable_register(struct regulator_dev *rdev,
		int *reg, int *mask, int *pattern)
{
	unsigned int rid = rdev_get_id(rdev);
	unsigned int mode;
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);

	if (rid >= ARRAY_SIZE(max77686_opmode_reg))
		return -EINVAL;

	mode = max77686->opmode_data[rid].mode;
	pr_debug("%s: rid=%d, mode=%d, size=%d\n",
		__func__, rid, mode, ARRAY_SIZE(max77686_opmode_reg));

	if (max77686_opmode_reg[rid][mode] == 0x0)
		WARN(1, "Not supported opmode\n");

	switch (rid) {
	case MAX77686_LDO1 ... MAX77686_LDO26:
		*reg = MAX77686_REG_LDO1CTRL1 + (rid - MAX77686_LDO1);
		*mask = MAX77686_OPMODE_MASK << MAX77686_OPMODE_SHIFT;
		*pattern = max77686_opmode_reg[rid][mode]
				<< MAX77686_OPMODE_SHIFT;
		break;
	case MAX77686_BUCK1:
		*reg = MAX77686_REG_BUCK1CTRL;
		*mask = MAX77686_OPMODE_MASK;
		*pattern = max77686_opmode_reg[rid][mode];
		break;
	case MAX77686_BUCK2:
	case MAX77686_BUCK3:
	case MAX77686_BUCK4:
		*reg = MAX77686_REG_BUCK2CTRL1 + (rid - MAX77686_BUCK2)*10;
		*mask = MAX77686_OPMODE_MASK << MAX77686_OPMODE_BUCK234_SHIFT;
		*pattern = max77686_opmode_reg[rid][mode]
				<< MAX77686_OPMODE_BUCK234_SHIFT;
		break;
	case MAX77686_BUCK5 ... MAX77686_BUCK9:
		*reg = MAX77686_REG_BUCK5CTRL + (rid - MAX77686_BUCK5) * 2;
		*mask = MAX77686_OPMODE_MASK;
		*pattern = max77686_opmode_reg[rid][mode];
		break;
	case MAX77686_EN32KHZ_AP ... MAX77686_P32KH:
		*reg = MAX77686_REG_32KHZ;
		*mask = 0x01 << (rid - MAX77686_EN32KHZ_AP);
		*pattern = 0x01 << (rid - MAX77686_EN32KHZ_AP);
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

	pr_debug("%s: id=%d, ret=%d, val=%x, mask=%x, pattern=%x\n",
		__func__, rdev_get_id(rdev), (val & mask) == pattern,
		val, mask, pattern);

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

	pr_debug("%s: id=%d, reg=%x, mask=%x, pattern=%x\n",
		__func__, rdev_get_id(rdev), reg, mask, pattern);

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

	pr_debug("%s: id=%d, reg=%x, mask=%x, pattern=%x\n",
		__func__, rdev_get_id(rdev), reg, mask, pattern);

	return max77686_update_reg(i2c, reg, ~mask, mask);
}

static int max77686_get_voltage_register(struct regulator_dev *rdev,
		int *_reg, int *_shift, int *_mask)
{
	int rid = rdev_get_id(rdev);
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
	int rid = rdev_get_id(rdev);
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

	pr_debug("%s: id=%d, reg=%x, mask=%x, val=%x\n",
		__func__, rid, reg, mask, val);

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

static int max77686_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	const struct voltage_map_desc *desc;
	int rid = rdev_get_id(rdev);
	int reg, shift = 0, mask, ret;
	int i;
	u8 org;

	desc = reg_voltage_map[rid];

	/* W/A code about voltage drop of PASS1 */
	if (max77686->version < VERSION_MAX77686A &&
			max77686->chip_rev <= CHIPREV_MAX77686_PASS1) {
		if (rid >= MAX77686_BUCK1 && rid <= MAX77686_BUCK4) {
			min_uV = min_uV + MAX77686_DVS_VOL_COMP;
			max_uV = max_uV + MAX77686_DVS_VOL_COMP;
		}
	}

	i = max77686_get_voltage_proper_val(desc, min_uV, max_uV);
	if (i < 0)
		return i;

	ret = max77686_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	/* TODO: If GPIO-DVS is being used, this won't work. */

	max77686_read_reg(i2c, reg, &org);
	org = (org & mask) >> shift;

	pr_debug("%s: id=%d, reg=%x, mask=%x, org=%x, val=%x\n",
		__func__, rdev_get_id(rdev), reg, mask, org, i);

	ret = max77686_update_reg(i2c, reg, i << shift, mask << shift);
	*selector = i;

	switch (rid) {
	case MAX77686_BUCK2 ... MAX77686_BUCK4:
		if (org < i)
			udelay(DIV_ROUND_UP(desc->step * (i - org),
						max77686->ramp_delay * 1000));
		break;
	case MAX77686_BUCK1:
	case MAX77686_BUCK5 ... MAX77686_BUCK9:
		/* Unconditionally 100 mV/us */
		if (org < i)
			udelay(DIV_ROUND_UP(desc->step * (i - org), 100000));
		break;
	default:
		break;
	}

	return ret;
}

static int max77686_reg_do_nothing(struct regulator_dev *rdev)
{
	return 0;
}

static struct regulator_ops max77686_ldo_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	.get_voltage		= max77686_get_voltage,
	.set_voltage		= max77686_set_voltage,
	/* TODO: set 0xC0 -> 0x40 with suspend_enable. for 0x0, ->0x0 */
	.set_suspend_enable	= max77686_reg_do_nothing,
	/* LDO's ON(0xC0) means "ON at normal, OFF at suspend" */
	.set_suspend_disable	= max77686_reg_do_nothing,
};

static struct regulator_ops max77686_buck_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	.get_voltage		= max77686_get_voltage,
	.set_voltage		= max77686_set_voltage,
	/* Interpret suspend_enable as "keep on if it was enabled." */
	.set_suspend_enable	= max77686_reg_do_nothing,
	.set_suspend_disable	= max77686_reg_disable,
};

static struct regulator_ops max77686_fixedvolt_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	/* Interpret suspend_enable as "keep on if it was enabled." */
	.set_suspend_enable	= max77686_reg_do_nothing,
	.set_suspend_disable	= max77686_reg_disable,
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
	}, {
		.name	= "EN32KHz PMIC",
		.id	= MAX77686_P32KH,
		.ops	= &max77686_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	},
};

static int max77686_set_ramp_rate(struct i2c_client *i2c, int rate)
{
	int ramp_delay = 0;
	u8 data = 0;

	switch (rate) {
	case MAX77686_RAMP_RATE_100MV:
		ramp_delay = 100;
		data = MAX77686_REG_RAMP_RATE_100MV;
		break;
	case MAX77686_RAMP_RATE_13MV:
		ramp_delay = 14;
		data = MAX77686_REG_RAMP_RATE_13MV;
		break;
	case MAX77686_RAMP_RATE_27MV:
		ramp_delay = 28;
		data = MAX77686_REG_RAMP_RATE_27MV;
		break;
	case MAX77686_RAMP_RATE_55MV:
		ramp_delay = 55;
		data = MAX77686_REG_RAMP_RATE_55MV;
		break;
	}

	pr_debug("%s: ramp_delay=%d, data=0x%x\n", __func__,
			ramp_delay, data);

	max77686_update_reg(i2c, MAX77686_REG_BUCK2CTRL1, data, 0xC0);
	max77686_update_reg(i2c, MAX77686_REG_BUCK3CTRL1, data, 0xC0);
	max77686_update_reg(i2c, MAX77686_REG_BUCK4CTRL1, data, 0xC0);

	return ramp_delay;
}

static __devinit void max77686_show_pwron_src(struct max77686_data *max77686)
{
	const char *src[] = {
		"PWRON=High", "JIGONB=Low", "ACOKB=Low", "Manual Reset Event",
		"ALARM1", "ALARM2", "SMPL Event", "WTSR Event"
	};
	struct i2c_client *i2c = max77686->iodev->i2c;
	int i, ret;
	u8 data = 0;

	ret = max77686_read_reg(i2c, MAX77686_REG_PWRON, &data);
	if (ret < 0) {
		dev_err(max77686->dev, "%s: fail to read PWRON reg(%d)\n",
				__func__, ret);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(src); i++)
		if (data & 1 << i)
			dev_info(max77686->dev, "Power on triggered by %s\n",
					src[i]);
}

static __devinit int max77686_pmic_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77686_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct regulator_dev **rdev;
	struct max77686_data *max77686;
	struct i2c_client *i2c;
	int i, ret, size;
	u8 data = 0;

	pr_debug("%s\n", __func__);

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
	platform_set_drvdata(pdev, max77686);
	i2c = max77686->iodev->i2c;

	max77686->opmode_data = pdata->opmode_data;
	max77686->ramp_delay = max77686_set_ramp_rate(i2c, pdata->ramp_rate);

	max77686_read_reg(i2c, MAX77686_REG_DEVICE_ID, &data);
	max77686->version =
		(data & MAX77686_VERSION_MASK) >> __ffs(MAX77686_VERSION_MASK);
	max77686->chip_rev = data & MAX77686_CHIPREV_MASK;
	pr_info("%s: Device ID=0x%02x, (version:%d, chip_rev:%d)\n", __func__,
			data, max77686->version, max77686->chip_rev);

	max77686_show_pwron_src(max77686);

	/*
	 * TODO
	 * This disables GPIO-DVS. Later we may need to implement GPIO-DVS..
	 * or we do not?
	 */
	max77686->buck2_gpiodvs = false;
	max77686->buck3_gpiodvs = false;
	max77686->buck4_gpiodvs = false;
	for (i = 0; i < 3; i++) {
		if (gpio_is_valid(pdata->buck234_gpio_dvs[i])) {
			max77686->buck234_gpios_dvs_label[i] =
				kasprintf(GFP_KERNEL, "MAX77686 DVS%d", i);
			max77686->buck234_gpios_dvs[i] =
				pdata->buck234_gpio_dvs[i];
			gpio_request(pdata->buck234_gpio_dvs[i],
				max77686->buck234_gpios_dvs_label[i]);
			gpio_direction_output(pdata->buck234_gpio_dvs[i], 0);
		} else {
			dev_info(&pdev->dev, "GPIO MAX77686 DVS%d ignored (%d)\n",
				 i, pdata->buck234_gpio_dvs[i]);
		}

		if (gpio_is_valid(pdata->buck234_gpio_selb[i])) {
			max77686->buck234_gpios_selb_label[i] =
				kasprintf(GFP_KERNEL, "MAX77686 SELB%d", i);
			max77686->buck234_gpios_selb[i] =
				pdata->buck234_gpio_selb[i];
			gpio_request(pdata->buck234_gpio_selb[i],
				max77686->buck234_gpios_selb_label[i]);
			gpio_direction_output(pdata->buck234_gpio_selb[i], 0);
		} else {
			dev_info(&pdev->dev, "GPIO MAX77686 SELB%d ignored (%d)\n",
				 i, pdata->buck234_gpio_selb[i]);
		}
	}
	max77686->buck234_gpioindex = 0;

	for (i = 0; i < 8; i++) {
		ret = max77686_get_voltage_proper_val(
				&buck_dvs_voltage_map_desc,
				pdata->buck2_voltage[i],
				pdata->buck2_voltage[i]
					+ buck_dvs_voltage_map_desc.step);
		/* 1.1V as default for safety */
		if (ret < 0)
			max77686->buck2_vol[i] = 0x28;
		else
			max77686->buck2_vol[i] = ret;
		max77686_write_reg(i2c, MAX77686_REG_BUCK2DVS1 + i,
				   max77686->buck2_vol[i]);

		ret = max77686_get_voltage_proper_val(
				&buck_dvs_voltage_map_desc,
				pdata->buck3_voltage[i],
				pdata->buck3_voltage[i]
					+ buck_dvs_voltage_map_desc.step);
		/* 1.1V as default for safety */
		if (ret < 0)
			max77686->buck3_vol[i] = 0x28;
		else
			max77686->buck3_vol[i] = ret;
		max77686_write_reg(i2c, MAX77686_REG_BUCK3DVS1 + i,
				   max77686->buck3_vol[i]);

		ret = max77686_get_voltage_proper_val(
				&buck_dvs_voltage_map_desc,
				pdata->buck4_voltage[i],
				pdata->buck4_voltage[i]
					+ buck_dvs_voltage_map_desc.step);
		/* 1.1V as default for safety */
		if (ret < 0)
			max77686->buck4_vol[i] = 0x28;
		else
			max77686->buck4_vol[i] = ret;
		max77686_write_reg(i2c, MAX77686_REG_BUCK4DVS1 + i,
				   max77686->buck4_vol[i]);
	}

	if (pdata->has_full_constraints)
		regulator_has_full_constraints();

	if (max77686->version > VERSION_MAX77686)
		reg_voltage_map[MAX77686_BUCK1] = &buck1_voltage_map_desc;

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct voltage_map_desc *desc;
		int id = pdata->regulators[i].id;

		desc = reg_voltage_map[id];
		if (desc) {
			regulators[id].n_voltages =
				(desc->max - desc->min) / desc->step + 1;

			pr_debug("%s: desc=%p, id=%d, n_vol=%d, "
					"max=%d, min=%d, step=%d\n", __func__,
					desc, id, regulators[id].n_voltages,
					desc->max, desc->min, desc->step);
		}

		rdev[i] = regulator_register(&regulators[id], max77686->dev,
				pdata->regulators[i].initdata, max77686, NULL);
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
	for (i = 0; i < 3; i++) {
		if (max77686->buck234_gpios_dvs[i])
			gpio_free(max77686->buck234_gpios_dvs[i]);
		if (max77686->buck234_gpios_dvs[i])
			gpio_free(max77686->buck234_gpios_selb[i]);
		kfree(max77686->buck234_gpios_dvs_label[i]);
		kfree(max77686->buck234_gpios_selb_label[i]);
	}

	for (i = 0; i < max77686->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max77686->rdev);
	kfree(max77686);

	return ret;
}

static int __devexit max77686_pmic_remove(struct platform_device *pdev)
{
	struct max77686_data *max77686 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max77686->rdev;
	int i;

	for (i = 0; i < 3; i++) {
		if (max77686->buck234_gpios_dvs[i])
			gpio_free(max77686->buck234_gpios_dvs[i]);
		if (max77686->buck234_gpios_dvs[i])
			gpio_free(max77686->buck234_gpios_selb[i]);
		kfree(max77686->buck234_gpios_dvs_label[i]);
		kfree(max77686->buck234_gpios_selb_label[i]);
	}

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
	pr_debug("%s\n", __func__);

	return platform_driver_register(&max77686_pmic_driver);
}
subsys_initcall(max77686_pmic_init);

static void __exit max77686_pmic_cleanup(void)
{
	platform_driver_unregister(&max77686_pmic_driver);
}
module_exit(max77686_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 77686 Regulator Driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_LICENSE("GPL");
