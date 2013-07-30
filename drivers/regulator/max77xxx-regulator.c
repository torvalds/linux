/*
 * max77xxx.c - Regulator driver for the Maxim 77686/802
 *
 * Copyright (C) 2013 Google, Inc
 *
 * Copyright (C) 2012 Samsung Electronics
 * Chiwoong Byun <woong.byun@smasung.com>
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
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

#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/max77xxx.h>
#include <linux/mfd/max77xxx-private.h>

/* Default ramp delay in case it is not manually set */
#define MAX77XXX_RAMP_DELAY		100000		/* uV/us */

#define MAX77XXX_OPMODE_SHIFT_LDO	6
#define MAX77XXX_OPMODE_BUCK234_SHIFT	4
#define MAX77XXX_OPMODE_MASK		0x3

#define MAX77XXX_VSEL_MASK		0x3F
#define MAX77XXX_DVS_VSEL_MASK		0xFF

#define MAX77XXX_RAMP_RATE_MASK_2BIT	0xC0
#define MAX77XXX_RAMP_RATE_SHIFT_2BIT	6
#define MAX77XXX_RAMP_RATE_MASK_4BIT	0xF0
#define MAX77XXX_RAMP_RATE_SHIFT_4BIT	4

/*
 * This is the number of regulators that we support, across all PMICs supported
 * by this driver.
 */
#define MAX77XXX_MAX_REGULATORS		MAX77XXX_REG_MAX

#define MAX77XXX_CLOCK_OPMODE_MASK	0x1
#define MAX77XXX_CLOCK_EN32KHZ_AP_SHIFT	0x0
#define MAX77XXX_CLOCK_EN32KHZ_CP_SHIFT	0x1
#define MAX77XXX_CLOCK_P32KHZ_SHIFT	0x2
#define MAX77XXX_CLOCK_LOW_JITTER_SHIFT 0x3

/* Voltage ramp thresholds for MAX77686 in mV/uS */
static const unsigned int ramp_table_77686[] = {
	13750,
	27500,
	55000,
	100000,
};

/* MAX77802 has two register formats: 2-bit and 4-bit */
static const unsigned int ramp_table_77802_2bit[] = {
	12500,
	25000,
	50000,
	100000,
};

static unsigned int ramp_table_77802_4bit[] = {
	1000,	2000,	3030,	4000,
	5000,	5880,	7140,	8330,
	9090,	10000,	11110,	12500,
	16670,	25000,	50000,	100000,
};

struct max77xxx_regulator_prv {
	int num_regulators;
	enum max77xxx_types type;
	struct regulator_dev *rdev[MAX77XXX_MAX_REGULATORS];
	unsigned int opmode[MAX77XXX_MAX_REGULATORS];
};

static int max77xxx_get_opmode_shift(int id)
{
	if (id >= MAX77XXX_LDO1 && id <= MAX77XXX_LDO35)
		return MAX77XXX_OPMODE_SHIFT_LDO;
	else if (id == MAX77XXX_BUCK1 || (id >= MAX77XXX_BUCK5 &&
					  id <= MAX77XXX_BUCK10))
		return 0;
	else if (id >= MAX77XXX_BUCK2 && id <= MAX77XXX_BUCK4)
		return MAX77XXX_OPMODE_BUCK234_SHIFT;
	else if (id == MAX77XXX_EN32KHZ_AP)
		return MAX77XXX_CLOCK_EN32KHZ_AP_SHIFT;
	else if (id == MAX77XXX_EN32KHZ_CP)
		return MAX77XXX_CLOCK_EN32KHZ_CP_SHIFT;
	else if (id == MAX77XXX_P32KHZ)
		return MAX77XXX_CLOCK_P32KHZ_SHIFT;
	else
		return -EINVAL;
}

/*
 * Some BUCKS supports Normal[ON/OFF] mode during suspend
 *
 * 686: BUCK 1, 2-4
 * 802: BUCK 1, 6, 2-4, 5, 7-10 (all)
 *
 * On 686 Bucks 2/3/4 also have another mode (0x02) that will force to low
 * power. On 802 the other mode (0x02) will make pwrreq switch between normal
 * and low power.
 */
static int max77xxx_buck_set_suspend_disable(struct regulator_dev *rdev)
{
	unsigned int val = MAX77XXX_OPMODE_STANDBY;
	struct max77xxx_regulator_prv *max77xxx = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int shift = max77xxx_get_opmode_shift(id);

	max77xxx->opmode[id] = val;
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

/*
 * Some LDOs support [LPM/Normal]ON mode during suspend state. This function
 * does not deal with BUCKs.
 *
 * Used on 686 for LDOs 3-5, 9, 13, 17-26
 */
static int max77686_set_suspend_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct max77xxx_regulator_prv *max77xxx = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int val;
	int shift = max77xxx_get_opmode_shift(id);

	/* BUCKs don't support this feature */
	if (id >= MAX77XXX_BUCK1)
		return 0;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:			/* ON in Normal Mode */
		val = MAX77XXX_OPMODE_NORMAL;
		break;
	case REGULATOR_MODE_IDLE:			/* ON in LP Mode */
		if (id != MAX77XXX_LDO5) {
			val = MAX77XXX_OPMODE_LP;
			break;
		}
		/* no break */
	default:
		pr_warn("%s: regulator_suspend_mode : 0x%x not supported\n",
			rdev->desc->name, mode);
		return -EINVAL;
	}

	max77xxx->opmode[id] = val;
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

/*
 * Some LDOs supports LPM-ON/OFF/Normal-ON mode during suspend state
 *
 * 686: LDOs 10-12, 14, 16, 2, 6-8, 15
 * 802: LDOs 3-7, 9-14, 18-26, 28, 29, 32-34, 1, 2, 8, 15, 17, 27, 30, 35 (all)
 *
 * For 802 LDOs 1, 20, 21, and 3, mode 1 is not supported
 */
static int max77xxx_ldo_set_suspend_mode(struct regulator_dev *rdev,
					 unsigned int mode)
{
	struct max77xxx_regulator_prv *max77xxx = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int val;
	int shift = max77xxx_get_opmode_shift(id);

	switch (mode) {
	case REGULATOR_MODE_IDLE:			/* ON in LP Mode */
		val = MAX77XXX_OPMODE_LP;
		break;
	case REGULATOR_MODE_NORMAL:			/* ON in Normal Mode */
		val = MAX77XXX_OPMODE_NORMAL;
		break;
	case REGULATOR_MODE_STANDBY:			/* switch off */
		if (id != MAX77XXX_LDO1 && id != MAX77XXX_LDO20 &&
			id != MAX77XXX_LDO21 && id != MAX77XXX_LDO3) {
			val = MAX77XXX_OPMODE_STANDBY;
			break;
		}
		/* no break */
	default:
		pr_warn("%s: regulator_suspend_mode : 0x%x not supported\n",
			rdev->desc->name, mode);
		return -EINVAL;
	}

	max77xxx->opmode[rdev_get_id(rdev)] = val;
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

static int max77xxx_enable(struct regulator_dev *rdev)
{
	struct max77xxx_regulator_prv *max77xxx = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int shift = max77xxx_get_opmode_shift(id);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask,
				  max77xxx->opmode[id] << shift);
}

static int max77xxx_find_ramp_value(struct regulator_dev *rdev,
				    const unsigned int limits[], int size,
				    unsigned int ramp_delay)
{
	int i;

	for (i = 0; i < size; i++) {
		if (ramp_delay <= limits[i])
			return i;
	}

	/* Use maximum value for no ramp control */
	pr_warn("%s: ramp_delay: %d not supported, setting 100000\n",
		rdev->desc->name, ramp_delay);
	return size - 1;
}

static int max77686_set_ramp_delay(struct regulator_dev *rdev,
				   int ramp_delay)
{
	unsigned int ramp_value;

	ramp_value = max77xxx_find_ramp_value(rdev, ramp_table_77686,
				ARRAY_SIZE(ramp_table_77686), ramp_delay);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  MAX77XXX_RAMP_RATE_MASK_2BIT,
				  ramp_value << MAX77XXX_RAMP_RATE_SHIFT_2BIT);
}

/* Used for BUCKs 2-4 */
static int max77802_set_ramp_delay_2bit(struct regulator_dev *rdev,
					int ramp_delay)
{
	int id = rdev_get_id(rdev);
	unsigned int ramp_value;

	if (id > MAX77XXX_BUCK4) {
		pr_warn("%s: regulator_suspend_mode: ramp delay not supported\n",
			rdev->desc->name);
		return -EINVAL;
	}
	ramp_value = max77xxx_find_ramp_value(rdev, ramp_table_77802_2bit,
				ARRAY_SIZE(ramp_table_77802_2bit), ramp_delay);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  MAX77XXX_RAMP_RATE_MASK_2BIT,
				  ramp_value << MAX77XXX_RAMP_RATE_SHIFT_2BIT);
}

/* For BUCK1, 6 */
static int max77802_set_ramp_delay_4bit(struct regulator_dev *rdev,
					    int ramp_delay)
{
	unsigned int ramp_value;

	ramp_value = max77xxx_find_ramp_value(rdev, ramp_table_77802_4bit,
				ARRAY_SIZE(ramp_table_77802_4bit), ramp_delay);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  MAX77XXX_RAMP_RATE_MASK_4BIT,
				  ramp_value << MAX77XXX_RAMP_RATE_SHIFT_4BIT);
}

/* Used for LDOs 3-5, 9, 13, 17-26, 1, BUCKs 5-9 */
static struct regulator_ops max77686_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77xxx_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_mode	= max77686_set_suspend_mode,
};

/*
 * 686: LDOs 10-12, 14, 16, 2, 6-8, 15
 * 802: LDOs 3-7, 9-14, 18-26, 28, 29, 32-34, 1, 2, 8, 15, 17, 27, 30, 35 (all)
 */
static struct regulator_ops max77xxx_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77xxx_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_mode	= max77xxx_ldo_set_suspend_mode,
};

/* 686: BUCK1 */
static struct regulator_ops max77686_buck1_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77xxx_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= max77xxx_buck_set_suspend_disable,
};

/* 686: BUCK 2-4 */
static struct regulator_ops max77686_buck_dvs_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77xxx_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77686_set_ramp_delay,
	.set_suspend_disable	= max77xxx_buck_set_suspend_disable,
};

static struct regulator_ops max77xxx_fixedvolt_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77xxx_enable,
	.disable		= regulator_disable_regmap,
};

/* LDOs 3-5, 9, 13, 17-26 */
#define regulator_77686_desc_ldo(num)		{			\
	.name		= "LDO"#num,					\
	.id		= MAX77XXX_LDO##num,				\
	.ops		= &max77686_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= MAX77XXX_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77XXX_OPMODE_MASK				\
			<< MAX77XXX_OPMODE_SHIFT_LDO,			\
}

/* LDOs 10-12, 14, 16 */
#define regulator_77686_desc_lpm_ldo(num)	{			\
	.name		= "LDO"#num,					\
	.id		= MAX77XXX_LDO##num,				\
	.ops		= &max77xxx_ldo_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= MAX77XXX_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77XXX_OPMODE_MASK				\
			<< MAX77XXX_OPMODE_SHIFT_LDO,			\
}

/* LDOs 2, 6-8, 15 */
#define regulator_77686_desc_ldo_low(num)	{			\
	.name		= "LDO"#num,					\
	.id		= MAX77XXX_LDO##num,				\
	.ops		= &max77xxx_ldo_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 25000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= MAX77XXX_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77XXX_OPMODE_MASK				\
			<< MAX77XXX_OPMODE_SHIFT_LDO,			\
}

/* LDO1 */
#define regulator_77686_desc_ldo1_low(num)	{			\
	.name		= "LDO"#num,					\
	.id		= MAX77XXX_LDO##num,				\
	.ops		= &max77686_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 25000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= MAX77XXX_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77XXX_OPMODE_MASK				\
			<< MAX77XXX_OPMODE_SHIFT_LDO,			\
}

/* BUCK 5-9 */
#define regulator_77686_desc_buck(num)		{			\
	.name		= "BUCK"#num,					\
	.id		= MAX77XXX_BUCK##num,				\
	.ops		= &max77686_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 750000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= MAX77XXX_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_BUCK5OUT + (num - 5) * 2,	\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_BUCK5CTRL + (num - 5) * 2,	\
	.enable_mask	= MAX77XXX_OPMODE_MASK,				\
}

/* BUCK1 */
#define regulator_77686_desc_buck1(num)	{				\
	.name		= "BUCK"#num,					\
	.id		= MAX77XXX_BUCK##num,				\
	.ops		= &max77686_buck1_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 750000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= MAX77XXX_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_BUCK1OUT,			\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_BUCK1CTRL,			\
	.enable_mask	= MAX77XXX_OPMODE_MASK,				\
}

/* BUCK 2-4 */
#define regulator_77686_desc_buck_dvs(num)	{			\
	.name		= "BUCK"#num,					\
	.id		= MAX77XXX_BUCK##num,				\
	.ops		= &max77686_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 600000,					\
	.uV_step	= 12500,					\
	.ramp_delay	= 27500,					\
	.n_voltages	= MAX77XXX_DVS_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_BUCK2DVS1 + (num - 2) * 10,	\
	.vsel_mask	= MAX77XXX_DVS_VSEL_MASK,			\
	.enable_reg	= MAX77686_REG_BUCK2CTRL1 + (num - 2) * 10,	\
	.enable_mask	= MAX77XXX_OPMODE_MASK				\
			<< MAX77XXX_OPMODE_BUCK234_SHIFT,		\
}

static struct regulator_desc regulators_77686[] = {
	regulator_77686_desc_ldo1_low(1),
	regulator_77686_desc_ldo_low(2),
	regulator_77686_desc_ldo(3),
	regulator_77686_desc_ldo(4),
	regulator_77686_desc_ldo(5),
	regulator_77686_desc_ldo_low(6),
	regulator_77686_desc_ldo_low(7),
	regulator_77686_desc_ldo_low(8),
	regulator_77686_desc_ldo(9),
	regulator_77686_desc_lpm_ldo(10),
	regulator_77686_desc_lpm_ldo(11),
	regulator_77686_desc_lpm_ldo(12),
	regulator_77686_desc_ldo(13),
	regulator_77686_desc_lpm_ldo(14),
	regulator_77686_desc_ldo_low(15),
	regulator_77686_desc_lpm_ldo(16),
	regulator_77686_desc_ldo(17),
	regulator_77686_desc_ldo(18),
	regulator_77686_desc_ldo(19),
	regulator_77686_desc_ldo(20),
	regulator_77686_desc_ldo(21),
	regulator_77686_desc_ldo(22),
	regulator_77686_desc_ldo(23),
	regulator_77686_desc_ldo(24),
	regulator_77686_desc_ldo(25),
	regulator_77686_desc_ldo(26),
	regulator_77686_desc_buck1(1),
	regulator_77686_desc_buck_dvs(2),
	regulator_77686_desc_buck_dvs(3),
	regulator_77686_desc_buck_dvs(4),
	regulator_77686_desc_buck(5),
	regulator_77686_desc_buck(6),
	regulator_77686_desc_buck(7),
	regulator_77686_desc_buck(8),
	regulator_77686_desc_buck(9),
	{
		.name = "EN32KHZ_AP",
		.id = MAX77XXX_EN32KHZ_AP,
		.ops = &max77xxx_fixedvolt_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = MAX77686_REG_32KHZ,
		.enable_mask = MAX77XXX_CLOCK_OPMODE_MASK
			       << MAX77XXX_CLOCK_EN32KHZ_AP_SHIFT,
	}, {
		.name = "EN32KHZ_CP",
		.id = MAX77XXX_EN32KHZ_CP,
		.ops = &max77xxx_fixedvolt_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = MAX77686_REG_32KHZ,
		.enable_mask = MAX77XXX_CLOCK_OPMODE_MASK
			       << MAX77XXX_CLOCK_EN32KHZ_CP_SHIFT,
	}, {
		.name = "P32KHZ",
		.id = MAX77XXX_P32KHZ,
		.ops = &max77xxx_fixedvolt_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = MAX77686_REG_32KHZ,
		.enable_mask = MAX77XXX_CLOCK_OPMODE_MASK
			       << MAX77XXX_CLOCK_P32KHZ_SHIFT,
	},
};

/* BUCKS 1, 6 */
static struct regulator_ops max77802_buck_16_dvs_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77xxx_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77802_set_ramp_delay_4bit,
	.set_suspend_disable	= max77xxx_buck_set_suspend_disable,
};

/* BUCKs 2-4, 5, 7-10 */
static struct regulator_ops max77802_buck_dvs_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77xxx_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77802_set_ramp_delay_2bit,
	.set_suspend_disable	= max77xxx_buck_set_suspend_disable,
};

/* LDOs 3-7, 9-14, 18-26, 28, 29, 32-34 */
#define regulator_77802_desc_p_ldo(num)	{				\
	.name		= "LDO"#num,					\
	.id		= MAX77XXX_LDO##num,				\
	.ops		= &max77xxx_ldo_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77XXX_OPMODE_MASK << MAX77XXX_OPMODE_SHIFT_LDO, \
}

/* LDOs 1, 2, 8, 15, 17, 27, 30, 35 */
#define regulator_77802_desc_n_ldo(num)	{				\
	.name		= "LDO"#num,					\
	.id		= MAX77XXX_LDO##num,				\
	.ops		= &max77xxx_ldo_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 25000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77XXX_OPMODE_MASK << MAX77XXX_OPMODE_SHIFT_LDO, \
}

/* BUCKs 1, 6 */
#define regulator_77802_desc_16_buck(num)	{			\
	.name		= "BUCK"#num,					\
	.id		= MAX77XXX_BUCK##num,				\
	.ops		= &max77802_buck_16_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 612500,					\
	.uV_step	= 6250,						\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= 1 << 8,					\
	.vsel_reg	= MAX77802_REG_BUCK ## num ## DVS1,		\
	.vsel_mask	= MAX77XXX_DVS_VSEL_MASK,			\
	.enable_reg	= MAX77802_REG_BUCK ## num ## CTRL,		\
	.enable_mask	= MAX77XXX_OPMODE_MASK,				\
}

/* BUCKS 2-4 */
#define regulator_77802_desc_234_buck(num)	{			\
	.name		= "BUCK"#num,					\
	.id		= MAX77XXX_BUCK##num,				\
	.ops		= &max77802_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 600000,					\
	.uV_step	= 6250,						\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= 0x91,						\
	.vsel_reg	= MAX77802_REG_BUCK ## num ## DVS1,		\
	.vsel_mask	= MAX77XXX_DVS_VSEL_MASK,			\
	.enable_reg	= MAX77802_REG_BUCK ## num ## CTRL1,		\
	.enable_mask	= MAX77XXX_OPMODE_MASK <<			\
				MAX77XXX_OPMODE_BUCK234_SHIFT,		\
}

/* BUCK 5 */
#define regulator_77802_desc_buck5(num)		{			\
	.name		= "BUCK"#num,					\
	.id		= MAX77XXX_BUCK##num,				\
	.ops		= &max77802_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 750000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_BUCK5OUT,			\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_BUCK5CTRL,			\
	.enable_mask	= MAX77XXX_OPMODE_MASK,				\
}

/* BUCKs 7-10 */
#define regulator_77802_desc_buck7_10(num)	{			\
	.name		= "BUCK"#num,					\
	.id		= MAX77XXX_BUCK##num,				\
	.ops		= &max77802_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 750000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77XXX_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_BUCK7OUT + (num - 7) * 3,	\
	.vsel_mask	= MAX77XXX_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_BUCK7CTRL + (num - 7) * 3,	\
	.enable_mask	= MAX77XXX_OPMODE_MASK,				\
}

static struct regulator_desc regulators_77802[] = {
	regulator_77802_desc_n_ldo(1),
	regulator_77802_desc_n_ldo(2),
	regulator_77802_desc_p_ldo(3),
	regulator_77802_desc_p_ldo(4),
	regulator_77802_desc_p_ldo(5),
	regulator_77802_desc_p_ldo(6),
	regulator_77802_desc_p_ldo(7),
	regulator_77802_desc_n_ldo(8),
	regulator_77802_desc_p_ldo(9),
	regulator_77802_desc_p_ldo(10),
	regulator_77802_desc_p_ldo(11),
	regulator_77802_desc_p_ldo(12),
	regulator_77802_desc_p_ldo(13),
	regulator_77802_desc_p_ldo(14),
	regulator_77802_desc_n_ldo(15),
	regulator_77802_desc_n_ldo(17),
	regulator_77802_desc_p_ldo(18),
	regulator_77802_desc_p_ldo(19),
	regulator_77802_desc_p_ldo(20),
	regulator_77802_desc_p_ldo(21),
	regulator_77802_desc_p_ldo(23),
	regulator_77802_desc_p_ldo(24),
	regulator_77802_desc_p_ldo(25),
	regulator_77802_desc_p_ldo(26),
	regulator_77802_desc_n_ldo(27),
	regulator_77802_desc_p_ldo(28),
	regulator_77802_desc_p_ldo(29),
	regulator_77802_desc_n_ldo(30),
	regulator_77802_desc_p_ldo(32),
	regulator_77802_desc_p_ldo(33),
	regulator_77802_desc_p_ldo(34),
	regulator_77802_desc_n_ldo(35),
	regulator_77802_desc_16_buck(1),
	regulator_77802_desc_234_buck(2),
	regulator_77802_desc_234_buck(3),
	regulator_77802_desc_234_buck(4),
	regulator_77802_desc_buck5(5),
	regulator_77802_desc_16_buck(6),
	regulator_77802_desc_buck7_10(7),
	regulator_77802_desc_buck7_10(8),
	regulator_77802_desc_buck7_10(9),
	regulator_77802_desc_buck7_10(10),
	{
		.name	= "EN32KHZ_AP",
		.id	= MAX77XXX_EN32KHZ_AP,
		.ops	= &max77xxx_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
		.enable_reg	= MAX77802_REG_32KHZ,
		.enable_mask	= 1 << 0,
	}, {
		.name	= "EN32KHZ_CP",
		.id	= MAX77XXX_EN32KHZ_CP,
		.ops	= &max77xxx_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
		.enable_reg	= MAX77802_REG_32KHZ,
		.enable_mask	= 1 << 1,
	},
};

static struct max77xxx_info {
	int num_regulators;
	struct regulator_desc *regulators;
} max77xxx_info[TYPE_COUNT] = {
	{
		.num_regulators = ARRAY_SIZE(regulators_77686),
		.regulators = regulators_77686,
	}, {
		.num_regulators = ARRAY_SIZE(regulators_77802),
		.regulators = regulators_77802
	},
};

#ifdef CONFIG_OF
static int max77xxx_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct max77xxx_platform_data *pdata,
					struct max77xxx_info *info)
{
	struct max77xxx_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct device_node *pmic_np, *regulators_np;
	struct max77xxx_regulator_data *rdata;
	struct of_regulator_match rmatch;
	enum max77xxx_types type;
	unsigned int i;

	type = pdev->id_entry->driver_data;
	pmic_np = iodev->dev->of_node;
	regulators_np = of_find_node_by_name(pmic_np, "voltage-regulators");
	if (!regulators_np) {
		dev_err(&pdev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	pdata->num_regulators = info->num_regulators;
	rdata = devm_kzalloc(&pdev->dev, sizeof(*rdata) *
			     pdata->num_regulators, GFP_KERNEL);
	if (!rdata) {
		dev_err(&pdev->dev,
			"could not allocate memory for regulator data\n");
		return -ENOMEM;
	}

	for (i = 0; i < pdata->num_regulators; i++) {
		rmatch.name = info->regulators[i].name;
		rmatch.init_data = NULL;
		rmatch.of_node = NULL;
		if (of_regulator_match(&pdev->dev, regulators_np, &rmatch,
				       1) != 1) {
			dev_warn(&pdev->dev, "No matching regulator for '%s'\n",
				 rmatch.name);
			continue;
		}
		rdata[i].initdata = rmatch.init_data;
		rdata[i].of_node = rmatch.of_node;
		rdata[i].id = info->regulators[i].id;
		if (of_property_read_u32(rdata[i].of_node, "regulator-op-mode",
			&rdata[i].opmode)) {
			dev_warn(iodev->dev, "no regulator-op-mode property property at %s\n",
				 rmatch.name);
			rdata[i].opmode = MAX77XXX_OPMODE_NORMAL;
		}
	}

	pdata->regulators = rdata;

	return 0;
}
#else
static int max77xxx_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct max77xxx_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

/**
 * max77xxx_setup_gpios - init DVS-related GPIOs
 *
 * This function claims / initalizations GPIOs related to DVS if they are
 * defined.  This may have the effect of switching voltages if the
 * pdata->buck_default_idx does not match the boot time state of pins.
 */
static int max77xxx_setup_gpios(struct device *dev,
				struct max77xxx_platform_data *pdata)
{
	int buck_default_idx = pdata->buck_default_idx;
	int ret;
	int i;

	/* Set all SELB high to avoid glitching while DVS is changing */
	for (i = 0; i < ARRAY_SIZE(pdata->buck_gpio_selb); i++) {
		int gpio = pdata->buck_gpio_selb[i];

		/* OK if some GPIOs aren't defined */
		if (!gpio_is_valid(gpio))
			continue;

		/* If a GPIO is valid, we'd better be able to claim it */
		ret = devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_HIGH,
					    "max77xxx selb");
		if (ret) {
			dev_err(dev, "can't claim gpio[%d]: %d\n", i, ret);
			return ret;
		}
	}

	/* Set our initial setting */
	for (i = 0; i < ARRAY_SIZE(pdata->buck_gpio_dvs); i++) {
		int gpio = pdata->buck_gpio_dvs[i];

		/* OK if some GPIOs aren't defined */
		if (!gpio_is_valid(gpio))
			continue;

		/* If a GPIO is valid, we'd better be able to claim it */
		ret = devm_gpio_request(dev, gpio, "max77xxx dvs");
		if (ret) {
			dev_err(dev, "can't claim gpio[%d]: %d\n", i, ret);
			return ret;
		}
		gpio_direction_output(gpio, (buck_default_idx >> i) & 1);
	}

	/* Now set SELB low to take effect */
	for (i = 0; i < ARRAY_SIZE(pdata->buck_gpio_selb); i++) {
		int gpio = pdata->buck_gpio_selb[i];

		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, 0);
	}

	return 0;
}

static inline bool max77xxx_is_dvs_buck(int id, int type)
{
	/* On 77686 bucks 2-4 are DVS; on 77802 bucks 1-4, 6 are */
	return (id >= MAX77XXX_BUCK2 && id <= MAX77XXX_BUCK4) ||
	       (type == TYPE_MAX77802 && (id == MAX77XXX_BUCK1 ||
					  id == MAX77XXX_BUCK6));
}

static int max77xxx_pmic_probe(struct platform_device *pdev)
{
	struct max77xxx_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77xxx_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct max77xxx_regulator_prv *max77xxx;
	int i, ret = 0;
	struct regulator_config config = { };
	struct max77xxx_info *info;
	unsigned int reg;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	/* This is allocated by the MFD driver */
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data found for regulator\n");
		return -ENODEV;
	}

	max77xxx = devm_kzalloc(&pdev->dev,
				sizeof(struct max77xxx_regulator_prv),
				GFP_KERNEL);
	if (!max77xxx)
		return -ENOMEM;
	max77xxx->type = pdev->id_entry->driver_data;
	if (max77xxx->type < 0 || max77xxx->type >= TYPE_COUNT) {
		dev_err(&pdev->dev, "unknown MAX77xxx type\n");
		return -EINVAL;
	}
	info = &max77xxx_info[max77xxx->type];
	max77xxx->num_regulators = info->num_regulators;

	if (iodev->dev->of_node) {
		ret = max77xxx_pmic_dt_parse_pdata(pdev, pdata, info);
		if (ret)
			return ret;
	}

	if (pdata->num_regulators != info->num_regulators) {
		dev_err(&pdev->dev,
			"Invalid initial data for regulator's initialiation: expected %d, pdata/dt provided %d\n",
			info->num_regulators, pdata->num_regulators);
		return -EINVAL;
	}

	config.dev = &pdev->dev;
	config.regmap = iodev->regmap;
	config.driver_data = max77xxx;
	platform_set_drvdata(pdev, max77xxx);

	for (i = 0; i < max77xxx->num_regulators; i++) {
		int id = pdata->regulators[i].id;

		config.init_data = pdata->regulators[i].initdata;
		config.of_node = pdata->regulators[i].of_node;

		if (config.of_node)
			max77xxx->opmode[id] = pdata->regulators[i].opmode;
		else
			max77xxx->opmode[id] = MAX77XXX_OPMODE_NORMAL;

		if (max77xxx_is_dvs_buck(id, max77xxx->type))
			info->regulators[i].vsel_reg += pdata->buck_default_idx;

		max77xxx->rdev[i] = regulator_register(&info->regulators[i],
						       &config);
		if (IS_ERR(max77xxx->rdev[i])) {
			ret = PTR_ERR(max77xxx->rdev[i]);
			dev_err(&pdev->dev,
				"regulator init failed for %d\n", i);
			max77xxx->rdev[i] = NULL;
			goto err;
		}
	}

	/* Enable low-jitter mode on the 32khz clocks. */
	if (max77xxx->type == TYPE_MAX77686)
		reg = MAX77686_REG_32KHZ;
	else
		reg = MAX77802_REG_32KHZ;
	ret = regmap_update_bits(iodev->regmap, reg,
				 1 << MAX77XXX_CLOCK_LOW_JITTER_SHIFT,
				 1 << MAX77XXX_CLOCK_LOW_JITTER_SHIFT);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable low-jitter mode\n");
		goto err;
	}

	ret = max77xxx_setup_gpios(&pdev->dev, pdata);
	if (ret)
		goto err;

	return 0;
err:
	while (--i >= 0)
		regulator_unregister(max77xxx->rdev[i]);
	return ret;
}

static int max77xxx_pmic_remove(struct platform_device *pdev)
{
	struct max77xxx_regulator_prv *max77xxx = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < max77xxx->num_regulators; i++)
		regulator_unregister(max77xxx->rdev[i]);

	return 0;
}

static const struct platform_device_id max77xxx_pmic_id[] = {
	{"max77686-pmic", TYPE_MAX77686},
	{"max77802-pmic", TYPE_MAX77802},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77xxx_pmic_id);

static struct platform_driver max77xxx_pmic_driver = {
	.driver = {
		.name = "max77xxx-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max77xxx_pmic_probe,
	.remove = max77xxx_pmic_remove,
	.id_table = max77xxx_pmic_id,
};

static int __init max77xxx_pmic_init(void)
{
	return platform_driver_register(&max77xxx_pmic_driver);
}
subsys_initcall(max77xxx_pmic_init);

static void __exit max77xxx_pmic_cleanup(void)
{
	platform_driver_unregister(&max77xxx_pmic_driver);
}
module_exit(max77xxx_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 77xxx Regulator Driver");
MODULE_AUTHOR("Simon Glass <sjg@chromium.org>");
MODULE_LICENSE("GPL");
