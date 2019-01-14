// SPDX-License-Identifier: GPL-2.0+
//
// max77802.c - Regulator driver for the Maxim 77802
//
// Copyright (C) 2013-2014 Google, Inc
// Simon Glass <sjg@chromium.org>
//
// Copyright (C) 2012 Samsung Electronics
// Chiwoong Byun <woong.byun@samsung.com>
// Jonghwa Lee <jonghwa3.lee@samsung.com>
//
// This driver is based on max8997.c

#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>
#include <dt-bindings/regulator/maxim,max77802.h>

/* Default ramp delay in case it is not manually set */
#define MAX77802_RAMP_DELAY		100000		/* uV/us */

#define MAX77802_OPMODE_SHIFT_LDO	6
#define MAX77802_OPMODE_BUCK234_SHIFT	4
#define MAX77802_OPMODE_MASK		0x3

#define MAX77802_VSEL_MASK		0x3F
#define MAX77802_DVS_VSEL_MASK		0xFF

#define MAX77802_RAMP_RATE_MASK_2BIT	0xC0
#define MAX77802_RAMP_RATE_SHIFT_2BIT	6
#define MAX77802_RAMP_RATE_MASK_4BIT	0xF0
#define MAX77802_RAMP_RATE_SHIFT_4BIT	4

#define MAX77802_STATUS_OFF		0x0
#define MAX77802_OFF_PWRREQ		0x1
#define MAX77802_LP_PWRREQ		0x2

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

struct max77802_regulator_prv {
	/* Array indexed by regulator id */
	unsigned int opmode[MAX77802_REG_MAX];
};

static inline unsigned int max77802_map_mode(unsigned int mode)
{
	return mode == MAX77802_OPMODE_NORMAL ?
		REGULATOR_MODE_NORMAL : REGULATOR_MODE_STANDBY;
}

static int max77802_get_opmode_shift(int id)
{
	if (id == MAX77802_BUCK1 || (id >= MAX77802_BUCK5 &&
				     id <= MAX77802_BUCK10))
		return 0;

	if (id >= MAX77802_BUCK2 && id <= MAX77802_BUCK4)
		return MAX77802_OPMODE_BUCK234_SHIFT;

	if (id >= MAX77802_LDO1 && id <= MAX77802_LDO35)
		return MAX77802_OPMODE_SHIFT_LDO;

	return -EINVAL;
}

/**
 * max77802_set_suspend_disable - Disable the regulator during system suspend
 * @rdev: regulator to mark as disabled
 *
 * All regulators expect LDO 1, 3, 20 and 21 support OFF by PWRREQ.
 * Configure the regulator so the PMIC will turn it OFF during system suspend.
 */
static int max77802_set_suspend_disable(struct regulator_dev *rdev)
{
	unsigned int val = MAX77802_OFF_PWRREQ;
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int shift = max77802_get_opmode_shift(id);

	max77802->opmode[id] = val;
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

/*
 * Some LDOs support Low Power Mode while the system is running.
 *
 * LDOs 1, 3, 20, 21.
 */
static int max77802_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int val;
	int shift = max77802_get_opmode_shift(id);

	switch (mode) {
	case REGULATOR_MODE_STANDBY:
		val = MAX77802_OPMODE_LP;	/* ON in Low Power Mode */
		break;
	case REGULATOR_MODE_NORMAL:
		val = MAX77802_OPMODE_NORMAL;	/* ON in Normal Mode */
		break;
	default:
		dev_warn(&rdev->dev, "%s: regulator mode: 0x%x not supported\n",
			 rdev->desc->name, mode);
		return -EINVAL;
	}

	max77802->opmode[id] = val;
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

static unsigned max77802_get_mode(struct regulator_dev *rdev)
{
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);

	return max77802_map_mode(max77802->opmode[id]);
}

/**
 * max77802_set_suspend_mode - set regulator opmode when the system is suspended
 * @rdev: regulator to change mode
 * @mode: operating mode to be set
 *
 * Will set the operating mode for the regulators during system suspend.
 * This function is valid for the three different enable control logics:
 *
 * Enable Control Logic1 by PWRREQ (BUCK 2-4 and LDOs 2, 4-19, 22-35)
 * Enable Control Logic2 by PWRREQ (LDOs 1, 20, 21)
 * Enable Control Logic3 by PWRREQ (LDO 3)
 *
 * If setting the regulator mode fails, the function only warns but does
 * not return an error code to avoid the regulator core to stop setting
 * the operating mode for the remaining regulators.
 */
static int max77802_set_suspend_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int val;
	int shift = max77802_get_opmode_shift(id);

	/*
	 * If the regulator has been disabled for suspend
	 * then is invalid to try setting a suspend mode.
	 */
	if (max77802->opmode[id] == MAX77802_OFF_PWRREQ) {
		dev_warn(&rdev->dev, "%s: is disabled, mode: 0x%x not set\n",
			 rdev->desc->name, mode);
		return 0;
	}

	switch (mode) {
	case REGULATOR_MODE_STANDBY:
		/*
		 * If the regulator opmode is normal then enable
		 * ON in Low Power Mode by PWRREQ. If the mode is
		 * already Low Power then no action is required.
		 */
		if (max77802->opmode[id] == MAX77802_OPMODE_NORMAL)
			val = MAX77802_LP_PWRREQ;
		else
			return 0;
		break;
	case REGULATOR_MODE_NORMAL:
		/*
		 * If the regulator operating mode is Low Power then
		 * normal is not a valid opmode in suspend. If the
		 * mode is already normal then no action is required.
		 */
		if (max77802->opmode[id] == MAX77802_OPMODE_LP)
			dev_warn(&rdev->dev, "%s: in Low Power: 0x%x invalid\n",
				 rdev->desc->name, mode);
		return 0;
	default:
		dev_warn(&rdev->dev, "%s: regulator mode: 0x%x not supported\n",
			 rdev->desc->name, mode);
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

static int max77802_enable(struct regulator_dev *rdev)
{
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int shift = max77802_get_opmode_shift(id);

	if (max77802->opmode[id] == MAX77802_OFF_PWRREQ)
		max77802->opmode[id] = MAX77802_OPMODE_NORMAL;

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask,
				  max77802->opmode[id] << shift);
}

static int max77802_find_ramp_value(struct regulator_dev *rdev,
				    const unsigned int limits[], int size,
				    unsigned int ramp_delay)
{
	int i;

	for (i = 0; i < size; i++) {
		if (ramp_delay <= limits[i])
			return i;
	}

	/* Use maximum value for no ramp control */
	dev_warn(&rdev->dev, "%s: ramp_delay: %d not supported, setting 100000\n",
		 rdev->desc->name, ramp_delay);
	return size - 1;
}

/* Used for BUCKs 2-4 */
static int max77802_set_ramp_delay_2bit(struct regulator_dev *rdev,
					int ramp_delay)
{
	int id = rdev_get_id(rdev);
	unsigned int ramp_value;

	if (id > MAX77802_BUCK4) {
			dev_warn(&rdev->dev,
				 "%s: regulator: ramp delay not supported\n",
				 rdev->desc->name);
		return -EINVAL;
	}
	ramp_value = max77802_find_ramp_value(rdev, ramp_table_77802_2bit,
				ARRAY_SIZE(ramp_table_77802_2bit), ramp_delay);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  MAX77802_RAMP_RATE_MASK_2BIT,
				  ramp_value << MAX77802_RAMP_RATE_SHIFT_2BIT);
}

/* For BUCK1, 6 */
static int max77802_set_ramp_delay_4bit(struct regulator_dev *rdev,
					    int ramp_delay)
{
	unsigned int ramp_value;

	ramp_value = max77802_find_ramp_value(rdev, ramp_table_77802_4bit,
				ARRAY_SIZE(ramp_table_77802_4bit), ramp_delay);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  MAX77802_RAMP_RATE_MASK_4BIT,
				  ramp_value << MAX77802_RAMP_RATE_SHIFT_4BIT);
}

/*
 * LDOs 2, 4-19, 22-35
 */
static const struct regulator_ops max77802_ldo_ops_logic1 = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= max77802_set_suspend_disable,
	.set_suspend_mode	= max77802_set_suspend_mode,
};

/*
 * LDOs 1, 20, 21, 3
 */
static const struct regulator_ops max77802_ldo_ops_logic2 = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_mode		= max77802_set_mode,
	.get_mode		= max77802_get_mode,
	.set_suspend_mode	= max77802_set_suspend_mode,
};

/* BUCKS 1, 6 */
static const struct regulator_ops max77802_buck_16_dvs_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77802_set_ramp_delay_4bit,
	.set_suspend_disable	= max77802_set_suspend_disable,
};

/* BUCKs 2-4 */
static const struct regulator_ops max77802_buck_234_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77802_set_ramp_delay_2bit,
	.set_suspend_disable	= max77802_set_suspend_disable,
	.set_suspend_mode	= max77802_set_suspend_mode,
};

/* BUCKs 5, 7-10 */
static const struct regulator_ops max77802_buck_dvs_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77802_set_ramp_delay_2bit,
	.set_suspend_disable	= max77802_set_suspend_disable,
};

/* LDOs 3-7, 9-14, 18-26, 28, 29, 32-34 */
#define regulator_77802_desc_p_ldo(num, supply, log)	{		\
	.name		= "LDO"#num,					\
	.of_match	= of_match_ptr("LDO"#num),			\
	.regulators_node	= of_match_ptr("regulators"),		\
	.id		= MAX77802_LDO##num,				\
	.supply_name	= "inl"#supply,					\
	.ops		= &max77802_ldo_ops_logic##log,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77802_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77802_OPMODE_MASK << MAX77802_OPMODE_SHIFT_LDO, \
	.of_map_mode	= max77802_map_mode,				\
}

/* LDOs 1, 2, 8, 15, 17, 27, 30, 35 */
#define regulator_77802_desc_n_ldo(num, supply, log)   {		\
	.name		= "LDO"#num,					\
	.of_match	= of_match_ptr("LDO"#num),			\
	.regulators_node	= of_match_ptr("regulators"),		\
	.id		= MAX77802_LDO##num,				\
	.supply_name	= "inl"#supply,					\
	.ops		= &max77802_ldo_ops_logic##log,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 25000,					\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77802_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77802_OPMODE_MASK << MAX77802_OPMODE_SHIFT_LDO, \
	.of_map_mode	= max77802_map_mode,				\
}

/* BUCKs 1, 6 */
#define regulator_77802_desc_16_buck(num)	{		\
	.name		= "BUCK"#num,					\
	.of_match	= of_match_ptr("BUCK"#num),			\
	.regulators_node	= of_match_ptr("regulators"),		\
	.id		= MAX77802_BUCK##num,				\
	.supply_name	= "inb"#num,					\
	.ops		= &max77802_buck_16_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 612500,					\
	.uV_step	= 6250,						\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 8,					\
	.vsel_reg	= MAX77802_REG_BUCK ## num ## DVS1,		\
	.vsel_mask	= MAX77802_DVS_VSEL_MASK,			\
	.enable_reg	= MAX77802_REG_BUCK ## num ## CTRL,		\
	.enable_mask	= MAX77802_OPMODE_MASK,				\
	.of_map_mode	= max77802_map_mode,				\
}

/* BUCKS 2-4 */
#define regulator_77802_desc_234_buck(num)	{		\
	.name		= "BUCK"#num,					\
	.of_match	= of_match_ptr("BUCK"#num),			\
	.regulators_node	= of_match_ptr("regulators"),		\
	.id		= MAX77802_BUCK##num,				\
	.supply_name	= "inb"#num,					\
	.ops		= &max77802_buck_234_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 600000,					\
	.uV_step	= 6250,						\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 0x91,						\
	.vsel_reg	= MAX77802_REG_BUCK ## num ## DVS1,		\
	.vsel_mask	= MAX77802_DVS_VSEL_MASK,			\
	.enable_reg	= MAX77802_REG_BUCK ## num ## CTRL1,		\
	.enable_mask	= MAX77802_OPMODE_MASK <<			\
				MAX77802_OPMODE_BUCK234_SHIFT,		\
	.of_map_mode	= max77802_map_mode,				\
}

/* BUCK 5 */
#define regulator_77802_desc_buck5(num)		{		\
	.name		= "BUCK"#num,					\
	.of_match	= of_match_ptr("BUCK"#num),			\
	.regulators_node	= of_match_ptr("regulators"),		\
	.id		= MAX77802_BUCK##num,				\
	.supply_name	= "inb"#num,					\
	.ops		= &max77802_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 750000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_BUCK5OUT,			\
	.vsel_mask	= MAX77802_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_BUCK5CTRL,			\
	.enable_mask	= MAX77802_OPMODE_MASK,				\
	.of_map_mode	= max77802_map_mode,				\
}

/* BUCKs 7-10 */
#define regulator_77802_desc_buck7_10(num)	{		\
	.name		= "BUCK"#num,					\
	.of_match	= of_match_ptr("BUCK"#num),			\
	.regulators_node	= of_match_ptr("regulators"),		\
	.id		= MAX77802_BUCK##num,				\
	.supply_name	= "inb"#num,					\
	.ops		= &max77802_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 750000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_BUCK7OUT + (num - 7) * 3,	\
	.vsel_mask	= MAX77802_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_BUCK7CTRL + (num - 7) * 3,	\
	.enable_mask	= MAX77802_OPMODE_MASK,				\
	.of_map_mode	= max77802_map_mode,				\
}

static const struct regulator_desc regulators[] = {
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
	regulator_77802_desc_n_ldo(1, 10, 2),
	regulator_77802_desc_n_ldo(2, 10, 1),
	regulator_77802_desc_p_ldo(3, 3, 2),
	regulator_77802_desc_p_ldo(4, 6, 1),
	regulator_77802_desc_p_ldo(5, 3, 1),
	regulator_77802_desc_p_ldo(6, 3, 1),
	regulator_77802_desc_p_ldo(7, 3, 1),
	regulator_77802_desc_n_ldo(8, 1, 1),
	regulator_77802_desc_p_ldo(9, 5, 1),
	regulator_77802_desc_p_ldo(10, 4, 1),
	regulator_77802_desc_p_ldo(11, 4, 1),
	regulator_77802_desc_p_ldo(12, 9, 1),
	regulator_77802_desc_p_ldo(13, 4, 1),
	regulator_77802_desc_p_ldo(14, 4, 1),
	regulator_77802_desc_n_ldo(15, 1, 1),
	regulator_77802_desc_n_ldo(17, 2, 1),
	regulator_77802_desc_p_ldo(18, 7, 1),
	regulator_77802_desc_p_ldo(19, 5, 1),
	regulator_77802_desc_p_ldo(20, 7, 2),
	regulator_77802_desc_p_ldo(21, 6, 2),
	regulator_77802_desc_p_ldo(23, 9, 1),
	regulator_77802_desc_p_ldo(24, 6, 1),
	regulator_77802_desc_p_ldo(25, 9, 1),
	regulator_77802_desc_p_ldo(26, 9, 1),
	regulator_77802_desc_n_ldo(27, 2, 1),
	regulator_77802_desc_p_ldo(28, 7, 1),
	regulator_77802_desc_p_ldo(29, 7, 1),
	regulator_77802_desc_n_ldo(30, 2, 1),
	regulator_77802_desc_p_ldo(32, 9, 1),
	regulator_77802_desc_p_ldo(33, 6, 1),
	regulator_77802_desc_p_ldo(34, 9, 1),
	regulator_77802_desc_n_ldo(35, 2, 1),
};

static int max77802_pmic_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77802_regulator_prv *max77802;
	int i, val;
	struct regulator_config config = { };

	max77802 = devm_kzalloc(&pdev->dev,
				sizeof(struct max77802_regulator_prv),
				GFP_KERNEL);
	if (!max77802)
		return -ENOMEM;

	config.dev = iodev->dev;
	config.regmap = iodev->regmap;
	config.driver_data = max77802;
	platform_set_drvdata(pdev, max77802);

	for (i = 0; i < MAX77802_REG_MAX; i++) {
		struct regulator_dev *rdev;
		int id = regulators[i].id;
		int shift = max77802_get_opmode_shift(id);
		int ret;

		ret = regmap_read(iodev->regmap, regulators[i].enable_reg, &val);
		if (ret < 0) {
			dev_warn(&pdev->dev,
				"cannot read current mode for %d\n", i);
			val = MAX77802_OPMODE_NORMAL;
		} else {
			val = val >> shift & MAX77802_OPMODE_MASK;
		}

		/*
		 * If the regulator is disabled and the system warm rebooted,
		 * the hardware reports OFF as the regulator operating mode.
		 * Default to operating mode NORMAL in that case.
		 */
		if (val == MAX77802_STATUS_OFF)
			max77802->opmode[id] = MAX77802_OPMODE_NORMAL;
		else
			max77802->opmode[id] = val;

		rdev = devm_regulator_register(&pdev->dev,
					       &regulators[i], &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(&pdev->dev,
				"regulator init failed for %d: %d\n", i, ret);
			return ret;
		}
	}

	return 0;
}

static const struct platform_device_id max77802_pmic_id[] = {
	{"max77802-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77802_pmic_id);

static struct platform_driver max77802_pmic_driver = {
	.driver = {
		.name = "max77802-pmic",
	},
	.probe = max77802_pmic_probe,
	.id_table = max77802_pmic_id,
};

module_platform_driver(max77802_pmic_driver);

MODULE_DESCRIPTION("MAXIM 77802 Regulator Driver");
MODULE_AUTHOR("Simon Glass <sjg@chromium.org>");
MODULE_LICENSE("GPL");
