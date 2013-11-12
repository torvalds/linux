/*
 * s2mps11.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
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
#include <linux/mfd/samsung/s2mps11.h>

#define S2MPS11_REGULATOR_CNT ARRAY_SIZE(regulators)

struct s2mps11_info {
	struct regulator_dev *rdev[S2MPS11_REGULATOR_MAX];

	int ramp_delay2;
	int ramp_delay34;
	int ramp_delay5;
	int ramp_delay16;
	int ramp_delay7810;
	int ramp_delay9;
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
	unsigned int ramp_delay = 0;
	int old_volt, new_volt;

	switch (rdev->desc->id) {
	case S2MPS11_BUCK2:
		ramp_delay = s2mps11->ramp_delay2;
		break;
	case S2MPS11_BUCK3:
		ramp_delay = s2mps11->ramp_delay34;
		break;
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
	int ret;

	switch (rdev->desc->id) {
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

	if (enable_shift) {
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

static struct regulator_ops s2mps11_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
};

static struct regulator_ops s2mps11_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= s2mps11_regulator_set_voltage_time_sel,
	.set_ramp_delay		= s2mps11_set_ramp_delay,
};

#define regulator_desc_ldo1(num)	{		\
	.name		= "LDO"#num,			\
	.id		= S2MPS11_LDO##num,		\
	.ops		= &s2mps11_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPS11_LDO_MIN,		\
	.uV_step	= S2MPS11_LDO_STEP1,		\
	.n_voltages	= S2MPS11_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS11_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS11_ENABLE_MASK		\
}
#define regulator_desc_ldo2(num)	{		\
	.name		= "LDO"#num,			\
	.id		= S2MPS11_LDO##num,		\
	.ops		= &s2mps11_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPS11_LDO_MIN,		\
	.uV_step	= S2MPS11_LDO_STEP2,		\
	.n_voltages	= S2MPS11_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS11_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS11_ENABLE_MASK		\
}

#define regulator_desc_buck1_4(num)	{			\
	.name		= "BUCK"#num,				\
	.id		= S2MPS11_BUCK##num,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN1,			\
	.uV_step	= S2MPS11_BUCK_STEP1,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_buck5	{				\
	.name		= "BUCK5",				\
	.id		= S2MPS11_BUCK5,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN1,			\
	.uV_step	= S2MPS11_BUCK_STEP1,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B5CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B5CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_buck6_8(num)	{			\
	.name		= "BUCK"#num,				\
	.id		= S2MPS11_BUCK##num,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN1,			\
	.uV_step	= S2MPS11_BUCK_STEP1,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B6CTRL2 + (num - 6) * 2,	\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B6CTRL1 + (num - 6) * 2,	\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_buck9	{				\
	.name		= "BUCK9",				\
	.id		= S2MPS11_BUCK9,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN3,			\
	.uV_step	= S2MPS11_BUCK_STEP3,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B9CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B9CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_buck10	{				\
	.name		= "BUCK10",				\
	.id		= S2MPS11_BUCK10,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN2,			\
	.uV_step	= S2MPS11_BUCK_STEP2,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B10CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B10CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

static struct regulator_desc regulators[] = {
	regulator_desc_ldo2(1),
	regulator_desc_ldo1(2),
	regulator_desc_ldo1(3),
	regulator_desc_ldo1(4),
	regulator_desc_ldo1(5),
	regulator_desc_ldo2(6),
	regulator_desc_ldo1(7),
	regulator_desc_ldo1(8),
	regulator_desc_ldo1(9),
	regulator_desc_ldo1(10),
	regulator_desc_ldo2(11),
	regulator_desc_ldo1(12),
	regulator_desc_ldo1(13),
	regulator_desc_ldo1(14),
	regulator_desc_ldo1(15),
	regulator_desc_ldo1(16),
	regulator_desc_ldo1(17),
	regulator_desc_ldo1(18),
	regulator_desc_ldo1(19),
	regulator_desc_ldo1(20),
	regulator_desc_ldo1(21),
	regulator_desc_ldo2(22),
	regulator_desc_ldo2(23),
	regulator_desc_ldo1(24),
	regulator_desc_ldo1(25),
	regulator_desc_ldo1(26),
	regulator_desc_ldo2(27),
	regulator_desc_ldo1(28),
	regulator_desc_ldo1(29),
	regulator_desc_ldo1(30),
	regulator_desc_ldo1(31),
	regulator_desc_ldo1(32),
	regulator_desc_ldo1(33),
	regulator_desc_ldo1(34),
	regulator_desc_ldo1(35),
	regulator_desc_ldo1(36),
	regulator_desc_ldo1(37),
	regulator_desc_ldo1(38),
	regulator_desc_buck1_4(1),
	regulator_desc_buck1_4(2),
	regulator_desc_buck1_4(3),
	regulator_desc_buck1_4(4),
	regulator_desc_buck5,
	regulator_desc_buck6_8(6),
	regulator_desc_buck6_8(7),
	regulator_desc_buck6_8(8),
	regulator_desc_buck9,
	regulator_desc_buck10,
};

static int s2mps11_pmic_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct sec_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct of_regulator_match rdata[S2MPS11_REGULATOR_MAX];
	struct device_node *reg_np = NULL;
	struct regulator_config config = { };
	struct s2mps11_info *s2mps11;
	int i, ret;

	s2mps11 = devm_kzalloc(&pdev->dev, sizeof(struct s2mps11_info),
				GFP_KERNEL);
	if (!s2mps11)
		return -ENOMEM;

	if (!iodev->dev->of_node) {
		if (pdata) {
			goto common_reg;
		} else {
			dev_err(pdev->dev.parent,
				"Platform data or DT node not supplied\n");
			return -ENODEV;
		}
	}

	for (i = 0; i < S2MPS11_REGULATOR_CNT; i++)
		rdata[i].name = regulators[i].name;

	reg_np = of_find_node_by_name(iodev->dev->of_node, "regulators");
	if (!reg_np) {
		dev_err(&pdev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	of_regulator_match(&pdev->dev, reg_np, rdata, S2MPS11_REGULATOR_MAX);

common_reg:
	platform_set_drvdata(pdev, s2mps11);

	config.dev = &pdev->dev;
	config.regmap = iodev->regmap;
	config.driver_data = s2mps11;
	for (i = 0; i < S2MPS11_REGULATOR_MAX; i++) {
		if (!reg_np) {
			config.init_data = pdata->regulators[i].initdata;
		} else {
			config.init_data = rdata[i].init_data;
			config.of_node = rdata[i].of_node;
		}

		s2mps11->rdev[i] = devm_regulator_register(&pdev->dev,
						&regulators[i], &config);
		if (IS_ERR(s2mps11->rdev[i])) {
			ret = PTR_ERR(s2mps11->rdev[i]);
			dev_err(&pdev->dev, "regulator init failed for %d\n",
				i);
			return ret;
		}
	}

	return 0;
}

static const struct platform_device_id s2mps11_pmic_id[] = {
	{ "s2mps11-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mps11_pmic_id);

static struct platform_driver s2mps11_pmic_driver = {
	.driver = {
		.name = "s2mps11-pmic",
		.owner = THIS_MODULE,
	},
	.probe = s2mps11_pmic_probe,
	.id_table = s2mps11_pmic_id,
};

static int __init s2mps11_pmic_init(void)
{
	return platform_driver_register(&s2mps11_pmic_driver);
}
subsys_initcall(s2mps11_pmic_init);

static void __exit s2mps11_pmic_exit(void)
{
	platform_driver_unregister(&s2mps11_pmic_driver);
}
module_exit(s2mps11_pmic_exit);

/* Module information */
MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("SAMSUNG S2MPS11 Regulator Driver");
MODULE_LICENSE("GPL");
