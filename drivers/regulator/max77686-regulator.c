/*
 * max77686.c - Regulator driver for the Maxim 77686
 *
 * Copyright (C) 2012 Samsung Electronics
 * Chiwoong Byun <woong.byun@samsung.com>
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
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>

#define MAX77686_LDO_MINUV	800000
#define MAX77686_LDO_UVSTEP	50000
#define MAX77686_LDO_LOW_MINUV	800000
#define MAX77686_LDO_LOW_UVSTEP	25000
#define MAX77686_BUCK_MINUV	750000
#define MAX77686_BUCK_UVSTEP	50000
#define MAX77686_BUCK_ENABLE_TIME	40		/* us */
#define MAX77686_DVS_ENABLE_TIME	22		/* us */
#define MAX77686_RAMP_DELAY	100000			/* uV/us */
#define MAX77686_DVS_RAMP_DELAY	27500			/* uV/us */
#define MAX77686_DVS_MINUV	600000
#define MAX77686_DVS_UVSTEP	12500

/*
 * Value for configuring buck[89] and LDO{20,21,22} as GPIO control.
 * It is the same as 'off' for other regulators.
 */
#define MAX77686_GPIO_CONTROL		0x0
/*
 * Values used for configuring LDOs and bucks.
 * Forcing low power mode: LDO1, 3-5, 9, 13, 17-26
 */
#define MAX77686_LDO_LOWPOWER		0x1
/*
 * On/off controlled by PWRREQ:
 *  - LDO2, 6-8, 10-12, 14-16
 *  - buck[1234]
 */
#define MAX77686_OFF_PWRREQ		0x1
/* Low power mode controlled by PWRREQ: All LDOs */
#define MAX77686_LDO_LOWPOWER_PWRREQ	0x2
/* Forcing low power mode: buck[234] */
#define MAX77686_BUCK_LOWPOWER		0x2
#define MAX77686_NORMAL			0x3

#define MAX77686_OPMODE_SHIFT	6
#define MAX77686_OPMODE_BUCK234_SHIFT	4
#define MAX77686_OPMODE_MASK	0x3

#define MAX77686_VSEL_MASK	0x3F
#define MAX77686_DVS_VSEL_MASK	0xFF

#define MAX77686_RAMP_RATE_MASK	0xC0

#define MAX77686_REGULATORS	MAX77686_REG_MAX
#define MAX77686_LDOS		26

enum max77686_ramp_rate {
	RAMP_RATE_13P75MV,
	RAMP_RATE_27P5MV,
	RAMP_RATE_55MV,
	RAMP_RATE_NO_CTRL,	/* 100mV/us */
};

struct max77686_data {
	DECLARE_BITMAP(gpio_enabled, MAX77686_REGULATORS);

	/* Array indexed by regulator id */
	unsigned int opmode[MAX77686_REGULATORS];
};

static unsigned int max77686_get_opmode_shift(int id)
{
	switch (id) {
	case MAX77686_BUCK1:
	case MAX77686_BUCK5 ... MAX77686_BUCK9:
		return 0;
	case MAX77686_BUCK2 ... MAX77686_BUCK4:
		return MAX77686_OPMODE_BUCK234_SHIFT;
	default:
		/* all LDOs */
		return MAX77686_OPMODE_SHIFT;
	}
}

/*
 * When regulator is configured for GPIO control then it
 * replaces "normal" mode. Any change from low power mode to normal
 * should actually change to GPIO control.
 * Map normal mode to proper value for such regulators.
 */
static unsigned int max77686_map_normal_mode(struct max77686_data *max77686,
					     int id)
{
	switch (id) {
	case MAX77686_BUCK8:
	case MAX77686_BUCK9:
	case MAX77686_LDO20 ... MAX77686_LDO22:
		if (test_bit(id, max77686->gpio_enabled))
			return MAX77686_GPIO_CONTROL;
	}

	return MAX77686_NORMAL;
}

/* Some BUCKs and LDOs supports Normal[ON/OFF] mode during suspend */
static int max77686_set_suspend_disable(struct regulator_dev *rdev)
{
	unsigned int val, shift;
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	int ret, id = rdev_get_id(rdev);

	shift = max77686_get_opmode_shift(id);
	val = MAX77686_OFF_PWRREQ;

	ret = regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				 rdev->desc->enable_mask, val << shift);
	if (ret)
		return ret;

	max77686->opmode[id] = val;
	return 0;
}

/* Some LDOs supports [LPM/Normal]ON mode during suspend state */
static int max77686_set_suspend_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret, id = rdev_get_id(rdev);

	/* BUCK[5-9] doesn't support this feature */
	if (id >= MAX77686_BUCK5)
		return 0;

	switch (mode) {
	case REGULATOR_MODE_IDLE:			/* ON in LP Mode */
		val = MAX77686_LDO_LOWPOWER_PWRREQ;
		break;
	case REGULATOR_MODE_NORMAL:			/* ON in Normal Mode */
		val = max77686_map_normal_mode(max77686, id);
		break;
	default:
		pr_warn("%s: regulator_suspend_mode : 0x%x not supported\n",
			rdev->desc->name, mode);
		return -EINVAL;
	}

	ret = regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask,
				  val << MAX77686_OPMODE_SHIFT);
	if (ret)
		return ret;

	max77686->opmode[id] = val;
	return 0;
}

/* Some LDOs supports LPM-ON/OFF/Normal-ON mode during suspend state */
static int max77686_ldo_set_suspend_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	unsigned int val;
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	int ret, id = rdev_get_id(rdev);

	switch (mode) {
	case REGULATOR_MODE_STANDBY:			/* switch off */
		val = MAX77686_OFF_PWRREQ;
		break;
	case REGULATOR_MODE_IDLE:			/* ON in LP Mode */
		val = MAX77686_LDO_LOWPOWER_PWRREQ;
		break;
	case REGULATOR_MODE_NORMAL:			/* ON in Normal Mode */
		val = max77686_map_normal_mode(max77686, id);
		break;
	default:
		pr_warn("%s: regulator_suspend_mode : 0x%x not supported\n",
			rdev->desc->name, mode);
		return -EINVAL;
	}

	ret = regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				 rdev->desc->enable_mask,
				 val << MAX77686_OPMODE_SHIFT);
	if (ret)
		return ret;

	max77686->opmode[id] = val;
	return 0;
}

static int max77686_enable(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	unsigned int shift;
	int id = rdev_get_id(rdev);

	shift = max77686_get_opmode_shift(id);

	if (max77686->opmode[id] == MAX77686_OFF_PWRREQ)
		max77686->opmode[id] = max77686_map_normal_mode(max77686, id);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask,
				  max77686->opmode[id] << shift);
}

static int max77686_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	unsigned int ramp_value = RAMP_RATE_NO_CTRL;

	switch (ramp_delay) {
	case 1 ... 13750:
		ramp_value = RAMP_RATE_13P75MV;
		break;
	case 13751 ... 27500:
		ramp_value = RAMP_RATE_27P5MV;
		break;
	case 27501 ... 55000:
		ramp_value = RAMP_RATE_55MV;
		break;
	case 55001 ... 100000:
		break;
	default:
		pr_warn("%s: ramp_delay: %d not supported, setting 100000\n",
			rdev->desc->name, ramp_delay);
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  MAX77686_RAMP_RATE_MASK, ramp_value << 6);
}

static int max77686_of_parse_cb(struct device_node *np,
		const struct regulator_desc *desc,
		struct regulator_config *config)
{
	struct max77686_data *max77686 = config->driver_data;

	switch (desc->id) {
	case MAX77686_BUCK8:
	case MAX77686_BUCK9:
	case MAX77686_LDO20 ... MAX77686_LDO22:
		config->ena_gpio = of_get_named_gpio(np,
					"maxim,ena-gpios", 0);
		config->ena_gpio_flags = GPIOF_OUT_INIT_HIGH;
		config->ena_gpio_initialized = true;
		break;
	default:
		return 0;
	}

	if (gpio_is_valid(config->ena_gpio)) {
		set_bit(desc->id, max77686->gpio_enabled);

		return regmap_update_bits(config->regmap, desc->enable_reg,
					  desc->enable_mask,
					  MAX77686_GPIO_CONTROL);
	}

	return 0;
}

static struct regulator_ops max77686_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77686_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_mode	= max77686_set_suspend_mode,
};

static struct regulator_ops max77686_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77686_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_mode	= max77686_ldo_set_suspend_mode,
	.set_suspend_disable	= max77686_set_suspend_disable,
};

static struct regulator_ops max77686_buck1_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77686_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= max77686_set_suspend_disable,
};

static struct regulator_ops max77686_buck_dvs_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77686_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77686_set_ramp_delay,
	.set_suspend_disable	= max77686_set_suspend_disable,
};

#define regulator_desc_ldo(num)		{				\
	.name		= "LDO"#num,					\
	.of_match	= of_match_ptr("LDO"#num),			\
	.regulators_node	= of_match_ptr("voltage-regulators"),	\
	.of_parse_cb	= max77686_of_parse_cb,				\
	.id		= MAX77686_LDO##num,				\
	.ops		= &max77686_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= MAX77686_LDO_MINUV,				\
	.uV_step	= MAX77686_LDO_UVSTEP,				\
	.ramp_delay	= MAX77686_RAMP_DELAY,				\
	.n_voltages	= MAX77686_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77686_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77686_OPMODE_MASK				\
			<< MAX77686_OPMODE_SHIFT,			\
}
#define regulator_desc_lpm_ldo(num)	{				\
	.name		= "LDO"#num,					\
	.of_match	= of_match_ptr("LDO"#num),			\
	.regulators_node	= of_match_ptr("voltage-regulators"),	\
	.id		= MAX77686_LDO##num,				\
	.ops		= &max77686_ldo_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= MAX77686_LDO_MINUV,				\
	.uV_step	= MAX77686_LDO_UVSTEP,				\
	.ramp_delay	= MAX77686_RAMP_DELAY,				\
	.n_voltages	= MAX77686_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77686_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77686_OPMODE_MASK				\
			<< MAX77686_OPMODE_SHIFT,			\
}
#define regulator_desc_ldo_low(num)		{			\
	.name		= "LDO"#num,					\
	.of_match	= of_match_ptr("LDO"#num),			\
	.regulators_node	= of_match_ptr("voltage-regulators"),	\
	.id		= MAX77686_LDO##num,				\
	.ops		= &max77686_ldo_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= MAX77686_LDO_LOW_MINUV,			\
	.uV_step	= MAX77686_LDO_LOW_UVSTEP,			\
	.ramp_delay	= MAX77686_RAMP_DELAY,				\
	.n_voltages	= MAX77686_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77686_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77686_OPMODE_MASK				\
			<< MAX77686_OPMODE_SHIFT,			\
}
#define regulator_desc_ldo1_low(num)		{			\
	.name		= "LDO"#num,					\
	.of_match	= of_match_ptr("LDO"#num),			\
	.regulators_node	= of_match_ptr("voltage-regulators"),	\
	.id		= MAX77686_LDO##num,				\
	.ops		= &max77686_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= MAX77686_LDO_LOW_MINUV,			\
	.uV_step	= MAX77686_LDO_LOW_UVSTEP,			\
	.ramp_delay	= MAX77686_RAMP_DELAY,				\
	.n_voltages	= MAX77686_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77686_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77686_OPMODE_MASK				\
			<< MAX77686_OPMODE_SHIFT,			\
}
#define regulator_desc_buck(num)		{			\
	.name		= "BUCK"#num,					\
	.of_match	= of_match_ptr("BUCK"#num),			\
	.regulators_node	= of_match_ptr("voltage-regulators"),	\
	.of_parse_cb	= max77686_of_parse_cb,				\
	.id		= MAX77686_BUCK##num,				\
	.ops		= &max77686_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= MAX77686_BUCK_MINUV,				\
	.uV_step	= MAX77686_BUCK_UVSTEP,				\
	.ramp_delay	= MAX77686_RAMP_DELAY,				\
	.enable_time	= MAX77686_BUCK_ENABLE_TIME,			\
	.n_voltages	= MAX77686_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_BUCK5OUT + (num - 5) * 2,	\
	.vsel_mask	= MAX77686_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_BUCK5CTRL + (num - 5) * 2,	\
	.enable_mask	= MAX77686_OPMODE_MASK,				\
}
#define regulator_desc_buck1(num)		{			\
	.name		= "BUCK"#num,					\
	.of_match	= of_match_ptr("BUCK"#num),			\
	.regulators_node	= of_match_ptr("voltage-regulators"),	\
	.id		= MAX77686_BUCK##num,				\
	.ops		= &max77686_buck1_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= MAX77686_BUCK_MINUV,				\
	.uV_step	= MAX77686_BUCK_UVSTEP,				\
	.ramp_delay	= MAX77686_RAMP_DELAY,				\
	.enable_time	= MAX77686_BUCK_ENABLE_TIME,			\
	.n_voltages	= MAX77686_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_BUCK1OUT,			\
	.vsel_mask	= MAX77686_VSEL_MASK,				\
	.enable_reg	= MAX77686_REG_BUCK1CTRL,			\
	.enable_mask	= MAX77686_OPMODE_MASK,				\
}
#define regulator_desc_buck_dvs(num)		{			\
	.name		= "BUCK"#num,					\
	.of_match	= of_match_ptr("BUCK"#num),			\
	.regulators_node	= of_match_ptr("voltage-regulators"),	\
	.id		= MAX77686_BUCK##num,				\
	.ops		= &max77686_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= MAX77686_DVS_MINUV,				\
	.uV_step	= MAX77686_DVS_UVSTEP,				\
	.ramp_delay	= MAX77686_DVS_RAMP_DELAY,			\
	.enable_time	= MAX77686_DVS_ENABLE_TIME,			\
	.n_voltages	= MAX77686_DVS_VSEL_MASK + 1,			\
	.vsel_reg	= MAX77686_REG_BUCK2DVS1 + (num - 2) * 10,	\
	.vsel_mask	= MAX77686_DVS_VSEL_MASK,			\
	.enable_reg	= MAX77686_REG_BUCK2CTRL1 + (num - 2) * 10,	\
	.enable_mask	= MAX77686_OPMODE_MASK				\
			<< MAX77686_OPMODE_BUCK234_SHIFT,		\
}

static const struct regulator_desc regulators[] = {
	regulator_desc_ldo1_low(1),
	regulator_desc_ldo_low(2),
	regulator_desc_ldo(3),
	regulator_desc_ldo(4),
	regulator_desc_ldo(5),
	regulator_desc_ldo_low(6),
	regulator_desc_ldo_low(7),
	regulator_desc_ldo_low(8),
	regulator_desc_ldo(9),
	regulator_desc_lpm_ldo(10),
	regulator_desc_lpm_ldo(11),
	regulator_desc_lpm_ldo(12),
	regulator_desc_ldo(13),
	regulator_desc_lpm_ldo(14),
	regulator_desc_ldo_low(15),
	regulator_desc_lpm_ldo(16),
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
	regulator_desc_buck1(1),
	regulator_desc_buck_dvs(2),
	regulator_desc_buck_dvs(3),
	regulator_desc_buck_dvs(4),
	regulator_desc_buck(5),
	regulator_desc_buck(6),
	regulator_desc_buck(7),
	regulator_desc_buck(8),
	regulator_desc_buck(9),
};

static int max77686_pmic_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77686_data *max77686;
	int i;
	struct regulator_config config = { };

	dev_dbg(&pdev->dev, "%s\n", __func__);

	max77686 = devm_kzalloc(&pdev->dev, sizeof(struct max77686_data),
				GFP_KERNEL);
	if (!max77686)
		return -ENOMEM;

	config.dev = iodev->dev;
	config.regmap = iodev->regmap;
	config.driver_data = max77686;
	platform_set_drvdata(pdev, max77686);

	for (i = 0; i < MAX77686_REGULATORS; i++) {
		struct regulator_dev *rdev;
		int id = regulators[i].id;

		max77686->opmode[id] = MAX77686_NORMAL;
		rdev = devm_regulator_register(&pdev->dev,
						&regulators[i], &config);
		if (IS_ERR(rdev)) {
			int ret = PTR_ERR(rdev);
			dev_err(&pdev->dev,
				"regulator init failed for %d: %d\n", i, ret);
			return ret;
		}
	}

	return 0;
}

static const struct platform_device_id max77686_pmic_id[] = {
	{"max77686-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77686_pmic_id);

static struct platform_driver max77686_pmic_driver = {
	.driver = {
		.name = "max77686-pmic",
	},
	.probe = max77686_pmic_probe,
	.id_table = max77686_pmic_id,
};

module_platform_driver(max77686_pmic_driver);

MODULE_DESCRIPTION("MAXIM 77686 Regulator Driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_LICENSE("GPL");
