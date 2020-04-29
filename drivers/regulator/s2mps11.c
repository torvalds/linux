// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2012-2014 Samsung Electronics Co., Ltd
//              http://www.samsung.com

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s2mps13.h>
#include <linux/mfd/samsung/s2mps14.h>
#include <linux/mfd/samsung/s2mps15.h>
#include <linux/mfd/samsung/s2mpu02.h>

/* The highest number of possible regulators for supported devices. */
#define S2MPS_REGULATOR_MAX		S2MPS13_REGULATOR_MAX
struct s2mps11_info {
	int ramp_delay2;
	int ramp_delay34;
	int ramp_delay5;
	int ramp_delay16;
	int ramp_delay7810;
	int ramp_delay9;

	enum sec_device_type dev_type;

	/*
	 * One bit for each S2MPS11/S2MPS13/S2MPS14/S2MPU02 regulator whether
	 * the suspend mode was enabled.
	 */
	DECLARE_BITMAP(suspend_state, S2MPS_REGULATOR_MAX);

	/*
	 * Array (size: number of regulators) with GPIO-s for external
	 * sleep control.
	 */
	struct gpio_desc **ext_control_gpiod;
};

static int get_ramp_delay(int ramp_delay)
{
	unsigned char cnt = 0;

	ramp_delay /= 6250;

	while (true) {
		ramp_delay = ramp_delay >> 1;
		if (ramp_delay == 0)
			break;
		cnt++;
	}

	if (cnt > 3)
		cnt = 3;

	return cnt;
}

static int s2mps11_regulator_set_voltage_time_sel(struct regulator_dev *rdev,
				   unsigned int old_selector,
				   unsigned int new_selector)
{
	struct s2mps11_info *s2mps11 = rdev_get_drvdata(rdev);
	int rdev_id = rdev_get_id(rdev);
	unsigned int ramp_delay = 0;
	int old_volt, new_volt;

	switch (rdev_id) {
	case S2MPS11_BUCK2:
		ramp_delay = s2mps11->ramp_delay2;
		break;
	case S2MPS11_BUCK3:
	case S2MPS11_BUCK4:
		ramp_delay = s2mps11->ramp_delay34;
		break;
	case S2MPS11_BUCK5:
		ramp_delay = s2mps11->ramp_delay5;
		break;
	case S2MPS11_BUCK6:
	case S2MPS11_BUCK1:
		ramp_delay = s2mps11->ramp_delay16;
		break;
	case S2MPS11_BUCK7:
	case S2MPS11_BUCK8:
	case S2MPS11_BUCK10:
		ramp_delay = s2mps11->ramp_delay7810;
		break;
	case S2MPS11_BUCK9:
		ramp_delay = s2mps11->ramp_delay9;
	}

	if (ramp_delay == 0)
		ramp_delay = rdev->desc->ramp_delay;

	old_volt = rdev->desc->min_uV + (rdev->desc->uV_step * old_selector);
	new_volt = rdev->desc->min_uV + (rdev->desc->uV_step * new_selector);

	return DIV_ROUND_UP(abs(new_volt - old_volt), ramp_delay);
}

static int s2mps11_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct s2mps11_info *s2mps11 = rdev_get_drvdata(rdev);
	unsigned int ramp_val, ramp_shift, ramp_reg = S2MPS11_REG_RAMP_BUCK;
	unsigned int ramp_enable = 1, enable_shift = 0;
	int rdev_id = rdev_get_id(rdev);
	int ret;

	switch (rdev_id) {
	case S2MPS11_BUCK1:
		if (ramp_delay > s2mps11->ramp_delay16)
			s2mps11->ramp_delay16 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay16;

		ramp_shift = S2MPS11_BUCK16_RAMP_SHIFT;
		break;
	case S2MPS11_BUCK2:
		enable_shift = S2MPS11_BUCK2_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		s2mps11->ramp_delay2 = ramp_delay;
		ramp_shift = S2MPS11_BUCK2_RAMP_SHIFT;
		ramp_reg = S2MPS11_REG_RAMP;
		break;
	case S2MPS11_BUCK3:
		enable_shift = S2MPS11_BUCK3_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mps11->ramp_delay34)
			s2mps11->ramp_delay34 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay34;

		ramp_shift = S2MPS11_BUCK34_RAMP_SHIFT;
		ramp_reg = S2MPS11_REG_RAMP;
		break;
	case S2MPS11_BUCK4:
		enable_shift = S2MPS11_BUCK4_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mps11->ramp_delay34)
			s2mps11->ramp_delay34 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay34;

		ramp_shift = S2MPS11_BUCK34_RAMP_SHIFT;
		ramp_reg = S2MPS11_REG_RAMP;
		break;
	case S2MPS11_BUCK5:
		s2mps11->ramp_delay5 = ramp_delay;
		ramp_shift = S2MPS11_BUCK5_RAMP_SHIFT;
		break;
	case S2MPS11_BUCK6:
		enable_shift = S2MPS11_BUCK6_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mps11->ramp_delay16)
			s2mps11->ramp_delay16 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay16;

		ramp_shift = S2MPS11_BUCK16_RAMP_SHIFT;
		break;
	case S2MPS11_BUCK7:
	case S2MPS11_BUCK8:
	case S2MPS11_BUCK10:
		if (ramp_delay > s2mps11->ramp_delay7810)
			s2mps11->ramp_delay7810 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay7810;

		ramp_shift = S2MPS11_BUCK7810_RAMP_SHIFT;
		break;
	case S2MPS11_BUCK9:
		s2mps11->ramp_delay9 = ramp_delay;
		ramp_shift = S2MPS11_BUCK9_RAMP_SHIFT;
		break;
	default:
		return 0;
	}

	if (!ramp_enable)
		goto ramp_disable;

	/* Ramp delay can be enabled/disabled only for buck[2346] */
	if ((rdev_id >= S2MPS11_BUCK2 && rdev_id <= S2MPS11_BUCK4) ||
	    rdev_id == S2MPS11_BUCK6)  {
		ret = regmap_update_bits(rdev->regmap, S2MPS11_REG_RAMP,
					 1 << enable_shift, 1 << enable_shift);
		if (ret) {
			dev_err(&rdev->dev, "failed to enable ramp rate\n");
			return ret;
		}
	}

	ramp_val = get_ramp_delay(ramp_delay);

	return regmap_update_bits(rdev->regmap, ramp_reg, 0x3 << ramp_shift,
				  ramp_val << ramp_shift);

ramp_disable:
	return regmap_update_bits(rdev->regmap, S2MPS11_REG_RAMP,
				  1 << enable_shift, 0);
}

static int s2mps11_regulator_enable(struct regulator_dev *rdev)
{
	struct s2mps11_info *s2mps11 = rdev_get_drvdata(rdev);
	int rdev_id = rdev_get_id(rdev);
	unsigned int val;

	switch (s2mps11->dev_type) {
	case S2MPS11X:
		if (test_bit(rdev_id, s2mps11->suspend_state))
			val = S2MPS14_ENABLE_SUSPEND;
		else
			val = rdev->desc->enable_mask;
		break;
	case S2MPS13X:
	case S2MPS14X:
		if (test_bit(rdev_id, s2mps11->suspend_state))
			val = S2MPS14_ENABLE_SUSPEND;
		else if (s2mps11->ext_control_gpiod[rdev_id])
			val = S2MPS14_ENABLE_EXT_CONTROL;
		else
			val = rdev->desc->enable_mask;
		break;
	case S2MPU02:
		if (test_bit(rdev_id, s2mps11->suspend_state))
			val = S2MPU02_ENABLE_SUSPEND;
		else
			val = rdev->desc->enable_mask;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
			rdev->desc->enable_mask, val);
}

static int s2mps11_regulator_set_suspend_disable(struct regulator_dev *rdev)
{
	int ret;
	unsigned int val, state;
	struct s2mps11_info *s2mps11 = rdev_get_drvdata(rdev);
	int rdev_id = rdev_get_id(rdev);

	/* Below LDO should be always on or does not support suspend mode. */
	switch (s2mps11->dev_type) {
	case S2MPS11X:
		switch (rdev_id) {
		case S2MPS11_LDO2:
		case S2MPS11_LDO36:
		case S2MPS11_LDO37:
		case S2MPS11_LDO38:
			return 0;
		default:
			state = S2MPS14_ENABLE_SUSPEND;
			break;
		}
		break;
	case S2MPS13X:
	case S2MPS14X:
		switch (rdev_id) {
		case S2MPS14_LDO3:
			return 0;
		default:
			state = S2MPS14_ENABLE_SUSPEND;
			break;
		}
		break;
	case S2MPU02:
		switch (rdev_id) {
		case S2MPU02_LDO13:
		case S2MPU02_LDO14:
		case S2MPU02_LDO15:
		case S2MPU02_LDO17:
		case S2MPU02_BUCK7:
			state = S2MPU02_DISABLE_SUSPEND;
			break;
		default:
			state = S2MPU02_ENABLE_SUSPEND;
			break;
		}
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(rdev->regmap, rdev->desc->enable_reg, &val);
	if (ret < 0)
		return ret;

	set_bit(rdev_id, s2mps11->suspend_state);
	/*
	 * Don't enable suspend mode if regulator is already disabled because
	 * this would effectively for a short time turn on the regulator after
	 * resuming.
	 * However we still want to toggle the suspend_state bit for regulator
	 * in case if it got enabled before suspending the system.
	 */
	if (!(val & rdev->desc->enable_mask))
		return 0;

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, state);
}

static const struct regulator_ops s2mps11_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
};

static const struct regulator_ops s2mps11_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= s2mps11_regulator_set_voltage_time_sel,
	.set_ramp_delay		= s2mps11_set_ramp_delay,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
};

#define regulator_desc_s2mps11_ldo(num, step) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPS11_LDO##num,		\
	.ops		= &s2mps11_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.ramp_delay	= RAMP_DELAY_12_MVUS,		\
	.min_uV		= MIN_800_MV,			\
	.uV_step	= step,				\
	.n_voltages	= S2MPS11_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS11_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS11_ENABLE_MASK		\
}

#define regulator_desc_s2mps11_buck1_4(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPS11_BUCK##num,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= MIN_650_MV,				\
	.uV_step	= STEP_6_25_MV,				\
	.linear_min_sel	= 8,					\
	.n_voltages	= S2MPS11_BUCK12346_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_s2mps11_buck5 {				\
	.name		= "BUCK5",				\
	.id		= S2MPS11_BUCK5,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= MIN_650_MV,				\
	.uV_step	= STEP_6_25_MV,				\
	.linear_min_sel	= 8,					\
	.n_voltages	= S2MPS11_BUCK5_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B5CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B5CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_s2mps11_buck67810(num, min, step, min_sel, voltages) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS11_BUCK##num,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= voltages,				\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B6CTRL2 + (num - 6) * 2,	\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B6CTRL1 + (num - 6) * 2,	\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_s2mps11_buck9 {				\
	.name		= "BUCK9",				\
	.id		= S2MPS11_BUCK9,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= MIN_3000_MV,				\
	.uV_step	= STEP_25_MV,				\
	.n_voltages	= S2MPS11_BUCK9_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B9CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK9_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B9CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

static const struct regulator_desc s2mps11_regulators[] = {
	regulator_desc_s2mps11_ldo(1, STEP_25_MV),
	regulator_desc_s2mps11_ldo(2, STEP_50_MV),
	regulator_desc_s2mps11_ldo(3, STEP_50_MV),
	regulator_desc_s2mps11_ldo(4, STEP_50_MV),
	regulator_desc_s2mps11_ldo(5, STEP_50_MV),
	regulator_desc_s2mps11_ldo(6, STEP_25_MV),
	regulator_desc_s2mps11_ldo(7, STEP_50_MV),
	regulator_desc_s2mps11_ldo(8, STEP_50_MV),
	regulator_desc_s2mps11_ldo(9, STEP_50_MV),
	regulator_desc_s2mps11_ldo(10, STEP_50_MV),
	regulator_desc_s2mps11_ldo(11, STEP_25_MV),
	regulator_desc_s2mps11_ldo(12, STEP_50_MV),
	regulator_desc_s2mps11_ldo(13, STEP_50_MV),
	regulator_desc_s2mps11_ldo(14, STEP_50_MV),
	regulator_desc_s2mps11_ldo(15, STEP_50_MV),
	regulator_desc_s2mps11_ldo(16, STEP_50_MV),
	regulator_desc_s2mps11_ldo(17, STEP_50_MV),
	regulator_desc_s2mps11_ldo(18, STEP_50_MV),
	regulator_desc_s2mps11_ldo(19, STEP_50_MV),
	regulator_desc_s2mps11_ldo(20, STEP_50_MV),
	regulator_desc_s2mps11_ldo(21, STEP_50_MV),
	regulator_desc_s2mps11_ldo(22, STEP_25_MV),
	regulator_desc_s2mps11_ldo(23, STEP_25_MV),
	regulator_desc_s2mps11_ldo(24, STEP_50_MV),
	regulator_desc_s2mps11_ldo(25, STEP_50_MV),
	regulator_desc_s2mps11_ldo(26, STEP_50_MV),
	regulator_desc_s2mps11_ldo(27, STEP_25_MV),
	regulator_desc_s2mps11_ldo(28, STEP_50_MV),
	regulator_desc_s2mps11_ldo(29, STEP_50_MV),
	regulator_desc_s2mps11_ldo(30, STEP_50_MV),
	regulator_desc_s2mps11_ldo(31, STEP_50_MV),
	regulator_desc_s2mps11_ldo(32, STEP_50_MV),
	regulator_desc_s2mps11_ldo(33, STEP_50_MV),
	regulator_desc_s2mps11_ldo(34, STEP_50_MV),
	regulator_desc_s2mps11_ldo(35, STEP_25_MV),
	regulator_desc_s2mps11_ldo(36, STEP_50_MV),
	regulator_desc_s2mps11_ldo(37, STEP_50_MV),
	regulator_desc_s2mps11_ldo(38, STEP_50_MV),
	regulator_desc_s2mps11_buck1_4(1),
	regulator_desc_s2mps11_buck1_4(2),
	regulator_desc_s2mps11_buck1_4(3),
	regulator_desc_s2mps11_buck1_4(4),
	regulator_desc_s2mps11_buck5,
	regulator_desc_s2mps11_buck67810(6, MIN_650_MV, STEP_6_25_MV, 8,
					 S2MPS11_BUCK12346_N_VOLTAGES),
	regulator_desc_s2mps11_buck67810(7, MIN_750_MV, STEP_12_5_MV, 0,
					 S2MPS11_BUCK7810_N_VOLTAGES),
	regulator_desc_s2mps11_buck67810(8, MIN_750_MV, STEP_12_5_MV, 0,
					 S2MPS11_BUCK7810_N_VOLTAGES),
	regulator_desc_s2mps11_buck9,
	regulator_desc_s2mps11_buck67810(10, MIN_750_MV, STEP_12_5_MV, 0,
					 S2MPS11_BUCK7810_N_VOLTAGES),
};

static const struct regulator_ops s2mps14_reg_ops;

#define regulator_desc_s2mps13_ldo(num, min, step, min_sel) {	\
	.name		= "LDO"#num,				\
	.id		= S2MPS13_LDO##num,			\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= S2MPS14_LDO_N_VOLTAGES,		\
	.vsel_reg	= S2MPS13_REG_L1CTRL + num - 1,		\
	.vsel_mask	= S2MPS14_LDO_VSEL_MASK,		\
	.enable_reg	= S2MPS13_REG_L1CTRL + num - 1,		\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

#define regulator_desc_s2mps13_buck(num, min, step, min_sel) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS13_BUCK##num,			\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= S2MPS14_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS13_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPS13_REG_B1OUT + (num - 1) * 2,	\
	.vsel_mask	= S2MPS14_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS13_REG_B1CTRL + (num - 1) * 2,	\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

#define regulator_desc_s2mps13_buck7(num, min, step, min_sel) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS13_BUCK##num,			\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= S2MPS14_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS13_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPS13_REG_B1OUT + (num) * 2 - 1,	\
	.vsel_mask	= S2MPS14_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS13_REG_B1CTRL + (num - 1) * 2,	\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

#define regulator_desc_s2mps13_buck8_10(num, min, step, min_sel) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS13_BUCK##num,			\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= S2MPS14_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS13_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPS13_REG_B1OUT + (num) * 2 - 1,	\
	.vsel_mask	= S2MPS14_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS13_REG_B1CTRL + (num) * 2 - 1,	\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

static const struct regulator_desc s2mps13_regulators[] = {
	regulator_desc_s2mps13_ldo(1,  MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(2,  MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(3,  MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(4,  MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(5,  MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(6,  MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(7,  MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(8,  MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(9,  MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(10, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(11, MIN_800_MV,  STEP_25_MV,   0x10),
	regulator_desc_s2mps13_ldo(12, MIN_800_MV,  STEP_25_MV,   0x10),
	regulator_desc_s2mps13_ldo(13, MIN_800_MV,  STEP_25_MV,   0x10),
	regulator_desc_s2mps13_ldo(14, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(15, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(16, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(17, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(18, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(19, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(20, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(21, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(22, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(23, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(24, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(25, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(26, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(27, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(28, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(29, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(30, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(31, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(32, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(33, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(34, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(35, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(36, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(37, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(38, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(39, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(40, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_buck(1,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(2,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(3,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(4,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(5,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(6,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck7(7,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck8_10(8,  MIN_1000_MV, STEP_12_5_MV, 0x20),
	regulator_desc_s2mps13_buck8_10(9,  MIN_1000_MV, STEP_12_5_MV, 0x20),
	regulator_desc_s2mps13_buck8_10(10, MIN_500_MV,  STEP_6_25_MV, 0x10),
};

static const struct regulator_ops s2mps14_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
};

#define regulator_desc_s2mps14_ldo(num, min, step) {	\
	.name		= "LDO"#num,			\
	.id		= S2MPS14_LDO##num,		\
	.ops		= &s2mps14_reg_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= min,				\
	.uV_step	= step,				\
	.n_voltages	= S2MPS14_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS14_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS14_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS14_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS14_ENABLE_MASK		\
}

#define regulator_desc_s2mps14_buck(num, min, step, min_sel) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS14_BUCK##num,			\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.n_voltages	= S2MPS14_BUCK_N_VOLTAGES,		\
	.linear_min_sel = min_sel,				\
	.ramp_delay	= S2MPS14_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPS14_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPS14_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS14_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

static const struct regulator_desc s2mps14_regulators[] = {
	regulator_desc_s2mps14_ldo(1, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(2, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(3, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(4, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(5, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(6, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(7, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(8, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(9, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(10, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(11, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(12, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(13, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(14, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(15, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(16, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(17, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(18, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(19, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(20, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(21, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(22, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(23, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(24, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(25, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_buck(1, MIN_600_MV, STEP_6_25_MV,
				    S2MPS14_BUCK1235_START_SEL),
	regulator_desc_s2mps14_buck(2, MIN_600_MV, STEP_6_25_MV,
				    S2MPS14_BUCK1235_START_SEL),
	regulator_desc_s2mps14_buck(3, MIN_600_MV, STEP_6_25_MV,
				    S2MPS14_BUCK1235_START_SEL),
	regulator_desc_s2mps14_buck(4, MIN_1400_MV, STEP_12_5_MV,
				    S2MPS14_BUCK4_START_SEL),
	regulator_desc_s2mps14_buck(5, MIN_600_MV, STEP_6_25_MV,
				    S2MPS14_BUCK1235_START_SEL),
};

static const struct regulator_ops s2mps15_reg_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
};

static const struct regulator_ops s2mps15_reg_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
};

#define regulator_desc_s2mps15_ldo(num, range) {	\
	.name		= "LDO"#num,			\
	.id		= S2MPS15_LDO##num,		\
	.ops		= &s2mps15_reg_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.linear_ranges	= range,			\
	.n_linear_ranges = ARRAY_SIZE(range),		\
	.n_voltages	= S2MPS15_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS15_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS15_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS15_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS15_ENABLE_MASK		\
}

#define regulator_desc_s2mps15_buck(num, range) {			\
	.name		= "BUCK"#num,					\
	.id		= S2MPS15_BUCK##num,				\
	.ops		= &s2mps15_reg_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.linear_ranges	= range,					\
	.n_linear_ranges = ARRAY_SIZE(range),				\
	.ramp_delay	= 12500,					\
	.n_voltages	= S2MPS15_BUCK_N_VOLTAGES,			\
	.vsel_reg	= S2MPS15_REG_B1CTRL2 + ((num - 1) * 2),	\
	.vsel_mask	= S2MPS15_BUCK_VSEL_MASK,			\
	.enable_reg	= S2MPS15_REG_B1CTRL1 + ((num - 1) * 2),	\
	.enable_mask	= S2MPS15_ENABLE_MASK				\
}

/* voltage range for s2mps15 LDO 3, 5, 15, 16, 18, 20, 23 and 27 */
static const struct regulator_linear_range s2mps15_ldo_voltage_ranges1[] = {
	REGULATOR_LINEAR_RANGE(1000000, 0xc, 0x38, 25000),
};

/* voltage range for s2mps15 LDO 2, 6, 14, 17, 19, 21, 24 and 25 */
static const struct regulator_linear_range s2mps15_ldo_voltage_ranges2[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0x0, 0x3f, 25000),
};

/* voltage range for s2mps15 LDO 4, 11, 12, 13, 22 and 26 */
static const struct regulator_linear_range s2mps15_ldo_voltage_ranges3[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x0, 0x34, 12500),
};

/* voltage range for s2mps15 LDO 7, 8, 9 and 10 */
static const struct regulator_linear_range s2mps15_ldo_voltage_ranges4[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x10, 0x20, 25000),
};

/* voltage range for s2mps15 LDO 1 */
static const struct regulator_linear_range s2mps15_ldo_voltage_ranges5[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x20, 12500),
};

/* voltage range for s2mps15 BUCK 1, 2, 3, 4, 5, 6 and 7 */
static const struct regulator_linear_range s2mps15_buck_voltage_ranges1[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x20, 0xc0, 6250),
};

/* voltage range for s2mps15 BUCK 8, 9 and 10 */
static const struct regulator_linear_range s2mps15_buck_voltage_ranges2[] = {
	REGULATOR_LINEAR_RANGE(1000000, 0x20, 0x78, 12500),
};

static const struct regulator_desc s2mps15_regulators[] = {
	regulator_desc_s2mps15_ldo(1, s2mps15_ldo_voltage_ranges5),
	regulator_desc_s2mps15_ldo(2, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(3, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(4, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(5, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(6, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(7, s2mps15_ldo_voltage_ranges4),
	regulator_desc_s2mps15_ldo(8, s2mps15_ldo_voltage_ranges4),
	regulator_desc_s2mps15_ldo(9, s2mps15_ldo_voltage_ranges4),
	regulator_desc_s2mps15_ldo(10, s2mps15_ldo_voltage_ranges4),
	regulator_desc_s2mps15_ldo(11, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(12, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(13, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(14, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(15, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(16, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(17, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(18, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(19, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(20, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(21, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(22, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(23, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(24, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(25, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(26, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(27, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_buck(1, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(2, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(3, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(4, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(5, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(6, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(7, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(8, s2mps15_buck_voltage_ranges2),
	regulator_desc_s2mps15_buck(9, s2mps15_buck_voltage_ranges2),
	regulator_desc_s2mps15_buck(10, s2mps15_buck_voltage_ranges2),
};

static int s2mps14_pmic_enable_ext_control(struct s2mps11_info *s2mps11,
		struct regulator_dev *rdev)
{
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
			rdev->desc->enable_mask, S2MPS14_ENABLE_EXT_CONTROL);
}

static void s2mps14_pmic_dt_parse_ext_control_gpio(struct platform_device *pdev,
		struct of_regulator_match *rdata, struct s2mps11_info *s2mps11)
{
	struct gpio_desc **gpio = s2mps11->ext_control_gpiod;
	unsigned int i;
	unsigned int valid_regulators[3] = { S2MPS14_LDO10, S2MPS14_LDO11,
		S2MPS14_LDO12 };

	for (i = 0; i < ARRAY_SIZE(valid_regulators); i++) {
		unsigned int reg = valid_regulators[i];

		if (!rdata[reg].init_data || !rdata[reg].of_node)
			continue;

		gpio[reg] = devm_fwnode_gpiod_get(&pdev->dev,
				of_fwnode_handle(rdata[reg].of_node),
				"samsung,ext-control",
				GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE,
				"s2mps11-regulator");
		if (PTR_ERR(gpio[reg]) == -ENOENT)
			gpio[reg] = NULL;
		else if (IS_ERR(gpio[reg])) {
			dev_err(&pdev->dev, "Failed to get control GPIO for %d/%s\n",
				reg, rdata[reg].name);
			gpio[reg] = NULL;
			continue;
		}
		if (gpio[reg])
			dev_dbg(&pdev->dev, "Using GPIO for ext-control over %d/%s\n",
				reg, rdata[reg].name);
	}
}

static int s2mps11_pmic_dt_parse(struct platform_device *pdev,
		struct of_regulator_match *rdata, struct s2mps11_info *s2mps11,
		unsigned int rdev_num)
{
	struct device_node *reg_np;

	reg_np = of_get_child_by_name(pdev->dev.parent->of_node, "regulators");
	if (!reg_np) {
		dev_err(&pdev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	of_regulator_match(&pdev->dev, reg_np, rdata, rdev_num);
	if (s2mps11->dev_type == S2MPS14X)
		s2mps14_pmic_dt_parse_ext_control_gpio(pdev, rdata, s2mps11);

	of_node_put(reg_np);

	return 0;
}

static int s2mpu02_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	unsigned int ramp_val, ramp_shift, ramp_reg;
	int rdev_id = rdev_get_id(rdev);

	switch (rdev_id) {
	case S2MPU02_BUCK1:
		ramp_shift = S2MPU02_BUCK1_RAMP_SHIFT;
		break;
	case S2MPU02_BUCK2:
		ramp_shift = S2MPU02_BUCK2_RAMP_SHIFT;
		break;
	case S2MPU02_BUCK3:
		ramp_shift = S2MPU02_BUCK3_RAMP_SHIFT;
		break;
	case S2MPU02_BUCK4:
		ramp_shift = S2MPU02_BUCK4_RAMP_SHIFT;
		break;
	default:
		return 0;
	}
	ramp_reg = S2MPU02_REG_RAMP1;
	ramp_val = get_ramp_delay(ramp_delay);

	return regmap_update_bits(rdev->regmap, ramp_reg,
				  S2MPU02_BUCK1234_RAMP_MASK << ramp_shift,
				  ramp_val << ramp_shift);
}

static const struct regulator_ops s2mpu02_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
};

static const struct regulator_ops s2mpu02_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
	.set_ramp_delay		= s2mpu02_set_ramp_delay,
};

#define regulator_desc_s2mpu02_ldo1(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_900MV,	\
	.uV_step	= S2MPU02_LDO_STEP_12_5MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP1_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L1CTRL,		\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L1CTRL,		\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}
#define regulator_desc_s2mpu02_ldo2(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_1050MV,	\
	.uV_step	= S2MPU02_LDO_STEP_25MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP2_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L2CTRL1,		\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L2CTRL1,		\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}
#define regulator_desc_s2mpu02_ldo3(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_900MV,	\
	.uV_step	= S2MPU02_LDO_STEP_12_5MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP1_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}
#define regulator_desc_s2mpu02_ldo4(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_1050MV,	\
	.uV_step	= S2MPU02_LDO_STEP_25MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP2_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}
#define regulator_desc_s2mpu02_ldo5(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_1600MV,	\
	.uV_step	= S2MPU02_LDO_STEP_50MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP3_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}

#define regulator_desc_s2mpu02_buck1234(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPU02_BUCK##num,			\
	.ops		= &s2mpu02_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPU02_BUCK1234_MIN_600MV,		\
	.uV_step	= S2MPU02_BUCK1234_STEP_6_25MV,		\
	.n_voltages	= S2MPU02_BUCK_N_VOLTAGES,		\
	.linear_min_sel = S2MPU02_BUCK1234_START_SEL,		\
	.ramp_delay	= S2MPU02_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPU02_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPU02_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPU02_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPU02_ENABLE_MASK			\
}
#define regulator_desc_s2mpu02_buck5(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPU02_BUCK##num,			\
	.ops		= &s2mpu02_ldo_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPU02_BUCK5_MIN_1081_25MV,		\
	.uV_step	= S2MPU02_BUCK5_STEP_6_25MV,		\
	.n_voltages	= S2MPU02_BUCK_N_VOLTAGES,		\
	.linear_min_sel = S2MPU02_BUCK5_START_SEL,		\
	.ramp_delay	= S2MPU02_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPU02_REG_B5CTRL2,			\
	.vsel_mask	= S2MPU02_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPU02_REG_B5CTRL1,			\
	.enable_mask	= S2MPU02_ENABLE_MASK			\
}
#define regulator_desc_s2mpu02_buck6(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPU02_BUCK##num,			\
	.ops		= &s2mpu02_ldo_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPU02_BUCK6_MIN_1700MV,		\
	.uV_step	= S2MPU02_BUCK6_STEP_2_50MV,		\
	.n_voltages	= S2MPU02_BUCK_N_VOLTAGES,		\
	.linear_min_sel = S2MPU02_BUCK6_START_SEL,		\
	.ramp_delay	= S2MPU02_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPU02_REG_B6CTRL2,			\
	.vsel_mask	= S2MPU02_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPU02_REG_B6CTRL1,			\
	.enable_mask	= S2MPU02_ENABLE_MASK			\
}
#define regulator_desc_s2mpu02_buck7(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPU02_BUCK##num,			\
	.ops		= &s2mpu02_ldo_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPU02_BUCK7_MIN_900MV,		\
	.uV_step	= S2MPU02_BUCK7_STEP_6_25MV,		\
	.n_voltages	= S2MPU02_BUCK_N_VOLTAGES,		\
	.linear_min_sel = S2MPU02_BUCK7_START_SEL,		\
	.ramp_delay	= S2MPU02_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPU02_REG_B7CTRL2,			\
	.vsel_mask	= S2MPU02_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPU02_REG_B7CTRL1,			\
	.enable_mask	= S2MPU02_ENABLE_MASK			\
}

static const struct regulator_desc s2mpu02_regulators[] = {
	regulator_desc_s2mpu02_ldo1(1),
	regulator_desc_s2mpu02_ldo2(2),
	regulator_desc_s2mpu02_ldo4(3),
	regulator_desc_s2mpu02_ldo5(4),
	regulator_desc_s2mpu02_ldo4(5),
	regulator_desc_s2mpu02_ldo3(6),
	regulator_desc_s2mpu02_ldo3(7),
	regulator_desc_s2mpu02_ldo4(8),
	regulator_desc_s2mpu02_ldo5(9),
	regulator_desc_s2mpu02_ldo3(10),
	regulator_desc_s2mpu02_ldo4(11),
	regulator_desc_s2mpu02_ldo5(12),
	regulator_desc_s2mpu02_ldo5(13),
	regulator_desc_s2mpu02_ldo5(14),
	regulator_desc_s2mpu02_ldo5(15),
	regulator_desc_s2mpu02_ldo5(16),
	regulator_desc_s2mpu02_ldo4(17),
	regulator_desc_s2mpu02_ldo5(18),
	regulator_desc_s2mpu02_ldo3(19),
	regulator_desc_s2mpu02_ldo4(20),
	regulator_desc_s2mpu02_ldo5(21),
	regulator_desc_s2mpu02_ldo5(22),
	regulator_desc_s2mpu02_ldo5(23),
	regulator_desc_s2mpu02_ldo4(24),
	regulator_desc_s2mpu02_ldo5(25),
	regulator_desc_s2mpu02_ldo4(26),
	regulator_desc_s2mpu02_ldo5(27),
	regulator_desc_s2mpu02_ldo5(28),
	regulator_desc_s2mpu02_buck1234(1),
	regulator_desc_s2mpu02_buck1234(2),
	regulator_desc_s2mpu02_buck1234(3),
	regulator_desc_s2mpu02_buck1234(4),
	regulator_desc_s2mpu02_buck5(5),
	regulator_desc_s2mpu02_buck6(6),
	regulator_desc_s2mpu02_buck7(7),
};

static int s2mps11_pmic_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct sec_platform_data *pdata = NULL;
	struct of_regulator_match *rdata = NULL;
	struct regulator_config config = { };
	struct s2mps11_info *s2mps11;
	unsigned int rdev_num = 0;
	int i, ret = 0;
	const struct regulator_desc *regulators;

	s2mps11 = devm_kzalloc(&pdev->dev, sizeof(struct s2mps11_info),
				GFP_KERNEL);
	if (!s2mps11)
		return -ENOMEM;

	s2mps11->dev_type = platform_get_device_id(pdev)->driver_data;
	switch (s2mps11->dev_type) {
	case S2MPS11X:
		rdev_num = ARRAY_SIZE(s2mps11_regulators);
		regulators = s2mps11_regulators;
		BUILD_BUG_ON(S2MPS_REGULATOR_MAX < ARRAY_SIZE(s2mps11_regulators));
		break;
	case S2MPS13X:
		rdev_num = ARRAY_SIZE(s2mps13_regulators);
		regulators = s2mps13_regulators;
		BUILD_BUG_ON(S2MPS_REGULATOR_MAX < ARRAY_SIZE(s2mps13_regulators));
		break;
	case S2MPS14X:
		rdev_num = ARRAY_SIZE(s2mps14_regulators);
		regulators = s2mps14_regulators;
		BUILD_BUG_ON(S2MPS_REGULATOR_MAX < ARRAY_SIZE(s2mps14_regulators));
		break;
	case S2MPS15X:
		rdev_num = ARRAY_SIZE(s2mps15_regulators);
		regulators = s2mps15_regulators;
		BUILD_BUG_ON(S2MPS_REGULATOR_MAX < ARRAY_SIZE(s2mps15_regulators));
		break;
	case S2MPU02:
		rdev_num = ARRAY_SIZE(s2mpu02_regulators);
		regulators = s2mpu02_regulators;
		BUILD_BUG_ON(S2MPS_REGULATOR_MAX < ARRAY_SIZE(s2mpu02_regulators));
		break;
	default:
		dev_err(&pdev->dev, "Invalid device type: %u\n",
				    s2mps11->dev_type);
		return -EINVAL;
	}

	s2mps11->ext_control_gpiod = devm_kcalloc(&pdev->dev, rdev_num,
			       sizeof(*s2mps11->ext_control_gpiod), GFP_KERNEL);
	if (!s2mps11->ext_control_gpiod)
		return -ENOMEM;

	if (!iodev->dev->of_node) {
		if (iodev->pdata) {
			pdata = iodev->pdata;
			goto common_reg;
		} else {
			dev_err(pdev->dev.parent,
				"Platform data or DT node not supplied\n");
			return -ENODEV;
		}
	}

	rdata = kcalloc(rdev_num, sizeof(*rdata), GFP_KERNEL);
	if (!rdata)
		return -ENOMEM;

	for (i = 0; i < rdev_num; i++)
		rdata[i].name = regulators[i].name;

	ret = s2mps11_pmic_dt_parse(pdev, rdata, s2mps11, rdev_num);
	if (ret)
		goto out;

common_reg:
	platform_set_drvdata(pdev, s2mps11);

	config.dev = &pdev->dev;
	config.regmap = iodev->regmap_pmic;
	config.driver_data = s2mps11;
	for (i = 0; i < rdev_num; i++) {
		struct regulator_dev *regulator;

		if (pdata) {
			config.init_data = pdata->regulators[i].initdata;
			config.of_node = pdata->regulators[i].reg_node;
		} else {
			config.init_data = rdata[i].init_data;
			config.of_node = rdata[i].of_node;
		}
		config.ena_gpiod = s2mps11->ext_control_gpiod[i];
		/*
		 * Hand the GPIO descriptor management over to the regulator
		 * core, remove it from devres management.
		 */
		if (config.ena_gpiod)
			devm_gpiod_unhinge(&pdev->dev, config.ena_gpiod);
		regulator = devm_regulator_register(&pdev->dev,
						&regulators[i], &config);
		if (IS_ERR(regulator)) {
			ret = PTR_ERR(regulator);
			dev_err(&pdev->dev, "regulator init failed for %d\n",
				i);
			goto out;
		}

		if (config.ena_gpiod) {
			ret = s2mps14_pmic_enable_ext_control(s2mps11,
					regulator);
			if (ret < 0) {
				dev_err(&pdev->dev,
						"failed to enable GPIO control over %s: %d\n",
						regulator->desc->name, ret);
				goto out;
			}
		}
	}

out:
	kfree(rdata);

	return ret;
}

static const struct platform_device_id s2mps11_pmic_id[] = {
	{ "s2mps11-regulator", S2MPS11X},
	{ "s2mps13-regulator", S2MPS13X},
	{ "s2mps14-regulator", S2MPS14X},
	{ "s2mps15-regulator", S2MPS15X},
	{ "s2mpu02-regulator", S2MPU02},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mps11_pmic_id);

static struct platform_driver s2mps11_pmic_driver = {
	.driver = {
		.name = "s2mps11-pmic",
	},
	.probe = s2mps11_pmic_probe,
	.id_table = s2mps11_pmic_id,
};

module_platform_driver(s2mps11_pmic_driver);

/* Module information */
MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("Samsung S2MPS11/S2MPS14/S2MPS15/S2MPU02 Regulator Driver");
MODULE_LICENSE("GPL");
