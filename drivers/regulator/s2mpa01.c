/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd
 *		http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mpa01.h>

#define S2MPA01_REGULATOR_CNT ARRAY_SIZE(regulators)

struct s2mpa01_info {
	struct of_regulator_match rdata[S2MPA01_REGULATOR_MAX];
	int ramp_delay24;
	int ramp_delay3;
	int ramp_delay5;
	int ramp_delay16;
	int ramp_delay7;
	int ramp_delay8910;
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

static int s2mpa01_regulator_set_voltage_time_sel(struct regulator_dev *rdev,
				   unsigned int old_selector,
				   unsigned int new_selector)
{
	struct s2mpa01_info *s2mpa01 = rdev_get_drvdata(rdev);
	unsigned int ramp_delay = 0;
	int old_volt, new_volt;

	switch (rdev_get_id(rdev)) {
	case S2MPA01_BUCK2:
	case S2MPA01_BUCK4:
		ramp_delay = s2mpa01->ramp_delay24;
		break;
	case S2MPA01_BUCK3:
		ramp_delay = s2mpa01->ramp_delay3;
		break;
	case S2MPA01_BUCK5:
		ramp_delay = s2mpa01->ramp_delay5;
		break;
	case S2MPA01_BUCK1:
	case S2MPA01_BUCK6:
		ramp_delay = s2mpa01->ramp_delay16;
		break;
	case S2MPA01_BUCK7:
		ramp_delay = s2mpa01->ramp_delay7;
		break;
	case S2MPA01_BUCK8:
	case S2MPA01_BUCK9:
	case S2MPA01_BUCK10:
		ramp_delay = s2mpa01->ramp_delay8910;
		break;
	}

	if (ramp_delay == 0)
		ramp_delay = rdev->desc->ramp_delay;

	old_volt = rdev->desc->min_uV + (rdev->desc->uV_step * old_selector);
	new_volt = rdev->desc->min_uV + (rdev->desc->uV_step * new_selector);

	return DIV_ROUND_UP(abs(new_volt - old_volt), ramp_delay);
}

static int s2mpa01_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct s2mpa01_info *s2mpa01 = rdev_get_drvdata(rdev);
	unsigned int ramp_val, ramp_shift, ramp_reg = S2MPA01_REG_RAMP2;
	unsigned int ramp_enable = 1, enable_shift = 0;
	int ret;

	switch (rdev_get_id(rdev)) {
	case S2MPA01_BUCK1:
		enable_shift = S2MPA01_BUCK1_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mpa01->ramp_delay16)
			s2mpa01->ramp_delay16 = ramp_delay;
		else
			ramp_delay = s2mpa01->ramp_delay16;

		ramp_shift = S2MPA01_BUCK16_RAMP_SHIFT;
		break;
	case S2MPA01_BUCK2:
		enable_shift = S2MPA01_BUCK2_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mpa01->ramp_delay24)
			s2mpa01->ramp_delay24 = ramp_delay;
		else
			ramp_delay = s2mpa01->ramp_delay24;

		ramp_shift = S2MPA01_BUCK24_RAMP_SHIFT;
		ramp_reg = S2MPA01_REG_RAMP1;
		break;
	case S2MPA01_BUCK3:
		enable_shift = S2MPA01_BUCK3_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		s2mpa01->ramp_delay3 = ramp_delay;
		ramp_shift = S2MPA01_BUCK3_RAMP_SHIFT;
		ramp_reg = S2MPA01_REG_RAMP1;
		break;
	case S2MPA01_BUCK4:
		enable_shift = S2MPA01_BUCK4_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mpa01->ramp_delay24)
			s2mpa01->ramp_delay24 = ramp_delay;
		else
			ramp_delay = s2mpa01->ramp_delay24;

		ramp_shift = S2MPA01_BUCK24_RAMP_SHIFT;
		ramp_reg = S2MPA01_REG_RAMP1;
		break;
	case S2MPA01_BUCK5:
		s2mpa01->ramp_delay5 = ramp_delay;
		ramp_shift = S2MPA01_BUCK5_RAMP_SHIFT;
		break;
	case S2MPA01_BUCK6:
		if (ramp_delay > s2mpa01->ramp_delay16)
			s2mpa01->ramp_delay16 = ramp_delay;
		else
			ramp_delay = s2mpa01->ramp_delay16;

		ramp_shift = S2MPA01_BUCK16_RAMP_SHIFT;
		break;
	case S2MPA01_BUCK7:
		s2mpa01->ramp_delay7 = ramp_delay;
		ramp_shift = S2MPA01_BUCK7_RAMP_SHIFT;
		break;
	case S2MPA01_BUCK8:
	case S2MPA01_BUCK9:
	case S2MPA01_BUCK10:
		if (ramp_delay > s2mpa01->ramp_delay8910)
			s2mpa01->ramp_delay8910 = ramp_delay;
		else
			ramp_delay = s2mpa01->ramp_delay8910;

		ramp_shift = S2MPA01_BUCK8910_RAMP_SHIFT;
		break;
	default:
		return 0;
	}

	if (!ramp_enable)
		goto ramp_disable;

	/* Ramp delay can be enabled/disabled only for buck[1234] */
	if (rdev_get_id(rdev) >= S2MPA01_BUCK1 &&
			rdev_get_id(rdev) <= S2MPA01_BUCK4) {
		ret = regmap_update_bits(rdev->regmap, S2MPA01_REG_RAMP1,
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
	return regmap_update_bits(rdev->regmap, S2MPA01_REG_RAMP1,
				  1 << enable_shift, 0);
}

static struct regulator_ops s2mpa01_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
};

static struct regulator_ops s2mpa01_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= s2mpa01_regulator_set_voltage_time_sel,
	.set_ramp_delay		= s2mpa01_set_ramp_delay,
};

#define regulator_desc_ldo(num, step) {			\
	.name		= "LDO"#num,			\
	.id		= S2MPA01_LDO##num,		\
	.ops		= &s2mpa01_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= MIN_800_MV,			\
	.uV_step	= step,				\
	.n_voltages	= S2MPA01_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPA01_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPA01_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPA01_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPA01_ENABLE_MASK		\
}

#define regulator_desc_buck1_4(num)	{			\
	.name		= "BUCK"#num,				\
	.id		= S2MPA01_BUCK##num,			\
	.ops		= &s2mpa01_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= MIN_600_MV,				\
	.uV_step	= STEP_6_25_MV,				\
	.n_voltages	= S2MPA01_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPA01_RAMP_DELAY,			\
	.vsel_reg	= S2MPA01_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPA01_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPA01_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPA01_ENABLE_MASK			\
}

#define regulator_desc_buck5	{				\
	.name		= "BUCK5",				\
	.id		= S2MPA01_BUCK5,			\
	.ops		= &s2mpa01_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= MIN_800_MV,				\
	.uV_step	= STEP_6_25_MV,				\
	.n_voltages	= S2MPA01_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPA01_RAMP_DELAY,			\
	.vsel_reg	= S2MPA01_REG_B5CTRL2,			\
	.vsel_mask	= S2MPA01_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPA01_REG_B5CTRL1,			\
	.enable_mask	= S2MPA01_ENABLE_MASK			\
}

#define regulator_desc_buck6_10(num, min, step) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPA01_BUCK##num,			\
	.ops		= &s2mpa01_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.n_voltages	= S2MPA01_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPA01_RAMP_DELAY,			\
	.vsel_reg	= S2MPA01_REG_B6CTRL2 + (num - 6) * 2,	\
	.vsel_mask	= S2MPA01_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPA01_REG_B6CTRL1 + (num - 6) * 2,	\
	.enable_mask	= S2MPA01_ENABLE_MASK			\
}

static const struct regulator_desc regulators[] = {
	regulator_desc_ldo(1, STEP_25_MV),
	regulator_desc_ldo(2, STEP_50_MV),
	regulator_desc_ldo(3, STEP_50_MV),
	regulator_desc_ldo(4, STEP_50_MV),
	regulator_desc_ldo(5, STEP_50_MV),
	regulator_desc_ldo(6, STEP_25_MV),
	regulator_desc_ldo(7, STEP_50_MV),
	regulator_desc_ldo(8, STEP_50_MV),
	regulator_desc_ldo(9, STEP_50_MV),
	regulator_desc_ldo(10, STEP_50_MV),
	regulator_desc_ldo(11, STEP_25_MV),
	regulator_desc_ldo(12, STEP_50_MV),
	regulator_desc_ldo(13, STEP_50_MV),
	regulator_desc_ldo(14, STEP_50_MV),
	regulator_desc_ldo(15, STEP_50_MV),
	regulator_desc_ldo(16, STEP_50_MV),
	regulator_desc_ldo(17, STEP_50_MV),
	regulator_desc_ldo(18, STEP_50_MV),
	regulator_desc_ldo(19, STEP_50_MV),
	regulator_desc_ldo(20, STEP_50_MV),
	regulator_desc_ldo(21, STEP_50_MV),
	regulator_desc_ldo(22, STEP_25_MV),
	regulator_desc_ldo(23, STEP_25_MV),
	regulator_desc_ldo(24, STEP_50_MV),
	regulator_desc_ldo(25, STEP_50_MV),
	regulator_desc_ldo(26, STEP_50_MV),
	regulator_desc_buck1_4(1),
	regulator_desc_buck1_4(2),
	regulator_desc_buck1_4(3),
	regulator_desc_buck1_4(4),
	regulator_desc_buck5,
	regulator_desc_buck6_10(6, MIN_600_MV, STEP_6_25_MV),
	regulator_desc_buck6_10(7, MIN_600_MV, STEP_6_25_MV),
	regulator_desc_buck6_10(8, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_buck6_10(9, MIN_1500_MV, STEP_12_5_MV),
	regulator_desc_buck6_10(10, MIN_1000_MV, STEP_12_5_MV),
};

static int s2mpa01_pmic_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct sec_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct device_node *reg_np = NULL;
	struct regulator_config config = { };
	struct of_regulator_match *rdata;
	struct s2mpa01_info *s2mpa01;
	int i;

	s2mpa01 = devm_kzalloc(&pdev->dev, sizeof(*s2mpa01), GFP_KERNEL);
	if (!s2mpa01)
		return -ENOMEM;

	rdata = s2mpa01->rdata;
	for (i = 0; i < S2MPA01_REGULATOR_CNT; i++)
		rdata[i].name = regulators[i].name;

	if (iodev->dev->of_node) {
		reg_np = of_get_child_by_name(iodev->dev->of_node,
							"regulators");
			if (!reg_np) {
				dev_err(&pdev->dev,
					"could not find regulators sub-node\n");
				return -EINVAL;
			}

		of_regulator_match(&pdev->dev, reg_np, rdata,
						S2MPA01_REGULATOR_MAX);
		of_node_put(reg_np);
	}

	platform_set_drvdata(pdev, s2mpa01);

	config.dev = &pdev->dev;
	config.regmap = iodev->regmap_pmic;
	config.driver_data = s2mpa01;

	for (i = 0; i < S2MPA01_REGULATOR_MAX; i++) {
		struct regulator_dev *rdev;
		if (pdata)
			config.init_data = pdata->regulators[i].initdata;
		else
			config.init_data = rdata[i].init_data;

		if (reg_np)
			config.of_node = rdata[i].of_node;

		rdev = devm_regulator_register(&pdev->dev,
						&regulators[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "regulator init failed for %d\n",
				i);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id s2mpa01_pmic_id[] = {
	{ "s2mpa01-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mpa01_pmic_id);

static struct platform_driver s2mpa01_pmic_driver = {
	.driver = {
		.name = "s2mpa01-pmic",
	},
	.probe = s2mpa01_pmic_probe,
	.id_table = s2mpa01_pmic_id,
};

module_platform_driver(s2mpa01_pmic_driver);

/* Module information */
MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_AUTHOR("Sachin Kamat <sachin.kamat@samsung.com>");
MODULE_DESCRIPTION("SAMSUNG S2MPA01 Regulator Driver");
MODULE_LICENSE("GPL");
