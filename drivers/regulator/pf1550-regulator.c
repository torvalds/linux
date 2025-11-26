// SPDX-License-Identifier: GPL-2.0
//
// regulator driver for the PF1550
//
// Copyright (C) 2016 Freescale Semiconductor, Inc.
// Robin Gong <yibin.gong@freescale.com>
//
// Portions Copyright (c) 2025 Savoir-faire Linux Inc.
// Samuel Kayode <samuel.kayode@savoirfairelinux.com>
//

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/mfd/pf1550.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define PF1550_REGULATOR_IRQ_NR		11
#define PF1550_MAX_REGULATOR		7

struct pf1550_desc {
	struct regulator_desc desc;
	unsigned char stby_reg;
	unsigned char stby_mask;
	unsigned char stby_enable_reg;
	unsigned char stby_enable_mask;
};

struct pf1550_regulator_info {
	struct device *dev;
	const struct pf1550_ddata *pf1550;
	struct pf1550_desc regulator_descs[PF1550_MAX_REGULATOR];
	struct regulator_dev *rdevs[PF1550_MAX_REGULATOR];
};

static const int pf1550_sw12_volts[] = {
	1100000, 1200000, 1350000, 1500000, 1800000, 2500000, 3000000, 3300000,
};

static const int pf1550_ldo13_volts[] = {
	750000, 800000, 850000, 900000, 950000, 1000000, 1050000, 1100000,
	1150000, 1200000, 1250000, 1300000, 1350000, 1400000, 1450000, 1500000,
	1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000,
	2600000, 2700000, 2800000, 2900000, 3000000, 3100000, 3200000, 3300000,
};

static int pf1550_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	int id = rdev_get_id(rdev);
	unsigned int ramp_bits = 0;
	int ret;

	if (id > PF1550_VREFDDR)
		return -EACCES;

	if (ramp_delay < 0 || ramp_delay > 6250)
		return -EINVAL;

	ramp_delay = 6250 / ramp_delay;
	ramp_bits = ramp_delay >> 1;

	ret = regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg + 4, 0x10,
				 ramp_bits << 4);
	if (ret < 0)
		dev_err(&rdev->dev, "ramp failed, err %d\n", ret);

	return ret;
}

static int pf1550_set_suspend_enable(struct regulator_dev *rdev)
{
	const struct pf1550_desc *desc = container_of_const(rdev->desc,
							    struct pf1550_desc,
							    desc);
	unsigned int val = desc->stby_enable_mask;

	return regmap_update_bits(rdev->regmap, desc->stby_enable_reg,
				  desc->stby_enable_mask, val);
}

static int pf1550_set_suspend_disable(struct regulator_dev *rdev)
{
	const struct pf1550_desc *desc = container_of_const(rdev->desc,
							    struct pf1550_desc,
							    desc);

	return regmap_update_bits(rdev->regmap, desc->stby_enable_reg,
				  desc->stby_enable_mask, 0);
}

static int pf1550_buck_set_table_suspend_voltage(struct regulator_dev *rdev,
						 int uV)
{
	const struct pf1550_desc *desc = container_of_const(rdev->desc,
							    struct pf1550_desc,
							    desc);
	int ret;

	ret = regulator_map_voltage_ascend(rdev, uV, uV);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev), "failed to map %i uV\n", uV);
		return ret;
	}

	return regmap_update_bits(rdev->regmap, desc->stby_reg,
				  desc->stby_mask, ret);
}

static int pf1550_buck_set_linear_suspend_voltage(struct regulator_dev *rdev,
						  int uV)
{
	const struct pf1550_desc *desc = container_of_const(rdev->desc,
							    struct pf1550_desc,
							    desc);
	int ret;

	ret = regulator_map_voltage_linear(rdev, uV, uV);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev), "failed to map %i uV\n", uV);
		return ret;
	}

	return regmap_update_bits(rdev->regmap, desc->stby_reg,
				  desc->stby_mask, ret);
}

static const struct regulator_ops pf1550_sw1_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.set_suspend_enable = pf1550_set_suspend_enable,
	.set_suspend_disable = pf1550_set_suspend_disable,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_suspend_voltage = pf1550_buck_set_table_suspend_voltage,
	.map_voltage = regulator_map_voltage_ascend,
	.set_ramp_delay = pf1550_set_ramp_delay,
};

static const struct regulator_ops pf1550_sw2_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.set_suspend_enable = pf1550_set_suspend_enable,
	.set_suspend_disable = pf1550_set_suspend_disable,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_suspend_voltage = pf1550_buck_set_linear_suspend_voltage,
	.map_voltage = regulator_map_voltage_linear,
	.set_ramp_delay = pf1550_set_ramp_delay,
};

static const struct regulator_ops pf1550_ldo1_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.set_suspend_enable = pf1550_set_suspend_enable,
	.set_suspend_disable = pf1550_set_suspend_disable,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops pf1550_ldo2_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.set_suspend_enable = pf1550_set_suspend_enable,
	.set_suspend_disable = pf1550_set_suspend_disable,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.map_voltage = regulator_map_voltage_linear,
};

static const struct regulator_ops pf1550_fixed_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.set_suspend_enable = pf1550_set_suspend_enable,
	.set_suspend_disable = pf1550_set_suspend_disable,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

#define PF_VREF(_chip, match, _name, voltage)	{	\
	.desc = {	\
		.name = #_name,	\
		.of_match = of_match_ptr(match),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.n_voltages = 1,	\
		.ops = &pf1550_fixed_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = _chip ## _ ## _name,	\
		.owner = THIS_MODULE,	\
		.min_uV = (voltage),	\
		.enable_reg = _chip ## _PMIC_REG_ ## _name ## _CTRL, \
		.enable_mask = 0x1,	\
	},	\
	.stby_enable_reg = _chip ## _PMIC_REG_ ## _name ## _CTRL, \
	.stby_enable_mask = 0x2,	\
}

#define PF_SW(_chip, match, _name, min, max, mask, step)	{	\
	.desc = {	\
		.name = #_name,	\
		.of_match = of_match_ptr(match),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.ops = &pf1550_sw2_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = _chip ## _ ## _name,	\
		.owner = THIS_MODULE,	\
		.min_uV = (min),	\
		.uV_step = (step),	\
		.linear_min_sel = 0,	\
		.vsel_reg = _chip ## _PMIC_REG_ ## _name ## _VOLT, \
		.vsel_mask = (mask),	\
		.enable_reg = _chip ## _PMIC_REG_ ## _name ## _CTRL, \
		.enable_mask = 0x1,	\
	},	\
	.stby_reg = _chip ## _PMIC_REG_ ## _name ## _STBY_VOLT,	\
	.stby_mask = (mask),	\
	.stby_enable_reg = _chip ## _PMIC_REG_ ## _name ## _CTRL, \
	.stby_enable_mask = 0x2,	\
}

#define PF_LDO1(_chip, match, _name, mask, voltages)	{	\
	.desc = {	\
		.name = #_name,	\
		.of_match = of_match_ptr(match),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.n_voltages = ARRAY_SIZE(voltages),	\
		.ops = &pf1550_ldo1_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = _chip ## _ ## _name,	\
		.owner = THIS_MODULE,	\
		.volt_table = voltages, \
		.vsel_reg = _chip ## _PMIC_REG_ ## _name ## _VOLT, \
		.vsel_mask = (mask),	\
		.enable_reg = _chip ## _PMIC_REG_ ## _name ## _CTRL, \
		.enable_mask = 0x1,	\
	},	\
	.stby_enable_reg = _chip ## _PMIC_REG_ ## _name ## _CTRL, \
	.stby_enable_mask = 0x2,	\
}

#define PF_LDO2(_chip, match, _name, mask, min, max, step)	{	\
	.desc = {	\
		.name = #_name,	\
		.of_match = of_match_ptr(match),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.ops = &pf1550_ldo2_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = _chip ## _ ## _name,	\
		.owner = THIS_MODULE,	\
		.min_uV = (min),	\
		.uV_step = (step),	\
		.linear_min_sel = 0,	\
		.vsel_reg = _chip ## _PMIC_REG_ ## _name ## _VOLT, \
		.vsel_mask = (mask),	\
		.enable_reg = _chip ## _PMIC_REG_ ## _name ## _CTRL, \
		.enable_mask = 0x1,	\
	},	\
	.stby_enable_reg = _chip ## _PMIC_REG_ ## _name ## _CTRL, \
	.stby_enable_mask = 0x2,	\
}

static struct pf1550_desc pf1550_regulators[] = {
	PF_SW(PF1550, "sw1", SW1, 600000, 1387500, 0x3f, 12500),
	PF_SW(PF1550, "sw2", SW2, 600000, 1387500, 0x3f, 12500),
	PF_SW(PF1550, "sw3", SW3, 1800000, 3300000, 0xf, 100000),
	PF_VREF(PF1550, "vrefddr", VREFDDR, 1200000),
	PF_LDO1(PF1550, "ldo1", LDO1, 0x1f, pf1550_ldo13_volts),
	PF_LDO2(PF1550, "ldo2", LDO2, 0xf, 1800000, 3300000, 100000),
	PF_LDO1(PF1550, "ldo3", LDO3, 0x1f, pf1550_ldo13_volts),
};

static irqreturn_t pf1550_regulator_irq_handler(int irq, void *data)
{
	struct pf1550_regulator_info *info = data;
	struct device *dev = info->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int i, irq_type = -1;
	unsigned int event;

	for (i = 0; i < PF1550_REGULATOR_IRQ_NR; i++)
		if (irq == platform_get_irq(pdev, i))
			irq_type = i;

	switch (irq_type) {
	/* The _LS interrupts indicate over-current event. The _HS interrupts
	 * which are more accurate and can detect catastrophic faults, issue
	 * an error event. The current limit FAULT interrupt is similar to the
	 * _HS'
	 */
	case PF1550_PMIC_IRQ_SW1_LS:
	case PF1550_PMIC_IRQ_SW2_LS:
	case PF1550_PMIC_IRQ_SW3_LS:
		event = REGULATOR_EVENT_OVER_CURRENT_WARN;
		for (i = 0; i < PF1550_MAX_REGULATOR; i++)
			if (!strcmp(rdev_get_name(info->rdevs[i]), "SW3"))
				regulator_notifier_call_chain(info->rdevs[i],
							      event, NULL);
		break;
	case PF1550_PMIC_IRQ_SW1_HS:
	case PF1550_PMIC_IRQ_SW2_HS:
	case PF1550_PMIC_IRQ_SW3_HS:
		event = REGULATOR_EVENT_OVER_CURRENT;
		for (i = 0; i < PF1550_MAX_REGULATOR; i++)
			if (!strcmp(rdev_get_name(info->rdevs[i]), "SW3"))
				regulator_notifier_call_chain(info->rdevs[i],
							      event, NULL);
		break;
	case PF1550_PMIC_IRQ_LDO1_FAULT:
	case PF1550_PMIC_IRQ_LDO2_FAULT:
	case PF1550_PMIC_IRQ_LDO3_FAULT:
		event = REGULATOR_EVENT_OVER_CURRENT;
		for (i = 0; i < PF1550_MAX_REGULATOR; i++)
			if (!strcmp(rdev_get_name(info->rdevs[i]), "LDO3"))
				regulator_notifier_call_chain(info->rdevs[i],
							      event, NULL);
		break;
	case PF1550_PMIC_IRQ_TEMP_110:
	case PF1550_PMIC_IRQ_TEMP_125:
		event = REGULATOR_EVENT_OVER_TEMP;
		for (i = 0; i < PF1550_MAX_REGULATOR; i++)
			regulator_notifier_call_chain(info->rdevs[i],
						      event, NULL);
		break;
	default:
		dev_err(dev, "regulator interrupt: irq %d occurred\n",
			irq_type);
	}

	return IRQ_HANDLED;
}

static int pf1550_regulator_probe(struct platform_device *pdev)
{
	const struct pf1550_ddata *pf1550 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct pf1550_regulator_info *info;
	int i, irq = -1, ret = 0;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	config.regmap = dev_get_regmap(pf1550->dev, NULL);
	if (!config.regmap)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "failed to get parent regmap\n");

	config.dev = pf1550->dev;
	config.regmap = pf1550->regmap;
	info->dev = &pdev->dev;
	info->pf1550 = pf1550;

	memcpy(info->regulator_descs, pf1550_regulators,
	       sizeof(info->regulator_descs));

	for (i = 0; i < ARRAY_SIZE(pf1550_regulators); i++) {
		struct regulator_desc *desc;

		desc = &info->regulator_descs[i].desc;

		if ((desc->id == PF1550_SW2 && !pf1550->dvs2_enable) ||
		    (desc->id == PF1550_SW1 && !pf1550->dvs1_enable)) {
			/* OTP_SW2_DVS_ENB == 1? or OTP_SW1_DVS_ENB == 1? */
			desc->volt_table = pf1550_sw12_volts;
			desc->n_voltages = ARRAY_SIZE(pf1550_sw12_volts);
			desc->ops = &pf1550_sw1_ops;
		}

		info->rdevs[i] = devm_regulator_register(&pdev->dev, desc,
							 &config);
		if (IS_ERR(info->rdevs[i]))
			return dev_err_probe(&pdev->dev,
					     PTR_ERR(info->rdevs[i]),
					     "failed to initialize regulator-%d\n",
					     i);
	}

	platform_set_drvdata(pdev, info);

	for (i = 0; i < PF1550_REGULATOR_IRQ_NR; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						pf1550_regulator_irq_handler,
						IRQF_NO_SUSPEND,
						"pf1550-regulator", info);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "failed: irq request (IRQ: %d)\n",
					     i);
	}

	return 0;
}

static const struct platform_device_id pf1550_regulator_id[] = {
	{ "pf1550-regulator", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, pf1550_regulator_id);

static struct platform_driver pf1550_regulator_driver = {
	.driver = {
		   .name = "pf1550-regulator",
		   },
	.probe = pf1550_regulator_probe,
	.id_table = pf1550_regulator_id,
};
module_platform_driver(pf1550_regulator_driver);

MODULE_DESCRIPTION("NXP PF1550 regulator driver");
MODULE_AUTHOR("Robin Gong <yibin.gong@freescale.com>");
MODULE_LICENSE("GPL");
