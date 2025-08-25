// SPDX-License-Identifier: GPL-2.0
//
// TPS65214/TPS65215/TPS65219 PMIC Regulator Driver
//
// Copyright (C) 2022 BayLibre Incorporated - https://www.baylibre.com/
// Copyright (C) 2024 Texas Instruments Incorporated - https://www.ti.com/
//
// This implementation derived from tps65218 authored by
// "J Keerthy <j-keerthy@ti.com>"
//

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps65219.h>

struct tps65219_regulator_irq_type {
	const char *irq_name;
	const char *regulator_name;
	const char *event_name;
	unsigned long event;
};

static struct tps65219_regulator_irq_type tps65215_regulator_irq_types[] = {
	{ "SENSOR_3_WARM", "SENSOR3", "warm temperature", REGULATOR_EVENT_OVER_TEMP_WARN},
	{ "SENSOR_3_HOT", "SENSOR3", "hot temperature", REGULATOR_EVENT_OVER_TEMP},
};

static struct tps65219_regulator_irq_type tps65219_regulator_irq_types[] = {
	{ "LDO3_SCG", "LDO3", "short circuit to ground", REGULATOR_EVENT_REGULATION_OUT },
	{ "LDO3_OC", "LDO3", "overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "LDO3_UV", "LDO3", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ "LDO4_SCG", "LDO4", "short circuit to ground", REGULATOR_EVENT_REGULATION_OUT },
	{ "LDO4_OC", "LDO4", "overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "LDO4_UV", "LDO4", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ "LDO3_RV", "LDO3", "residual voltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "LDO4_RV", "LDO4", "residual voltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "LDO3_RV_SD", "LDO3", "residual voltage on shutdown", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "LDO4_RV_SD", "LDO4", "residual voltage on shutdown", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "SENSOR_3_WARM", "SENSOR3", "warm temperature", REGULATOR_EVENT_OVER_TEMP_WARN},
	{ "SENSOR_3_HOT", "SENSOR3", "hot temperature", REGULATOR_EVENT_OVER_TEMP},
};

/*  All of TPS65214's irq types are the same as common_regulator_irq_types */
static struct tps65219_regulator_irq_type common_regulator_irq_types[] = {
	{ "LDO1_SCG", "LDO1", "short circuit to ground", REGULATOR_EVENT_REGULATION_OUT },
	{ "LDO1_OC", "LDO1", "overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "LDO1_UV", "LDO1", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ "LDO2_SCG", "LDO2", "short circuit to ground", REGULATOR_EVENT_REGULATION_OUT },
	{ "LDO2_OC", "LDO2", "overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "LDO2_UV", "LDO2", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ "BUCK3_SCG", "BUCK3", "short circuit to ground", REGULATOR_EVENT_REGULATION_OUT },
	{ "BUCK3_OC", "BUCK3", "overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "BUCK3_NEG_OC", "BUCK3", "negative overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "BUCK3_UV", "BUCK3", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ "BUCK1_SCG", "BUCK1", "short circuit to ground", REGULATOR_EVENT_REGULATION_OUT },
	{ "BUCK1_OC", "BUCK1", "overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "BUCK1_NEG_OC", "BUCK1", "negative overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "BUCK1_UV", "BUCK1", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ "BUCK2_SCG", "BUCK2", "short circuit to ground", REGULATOR_EVENT_REGULATION_OUT },
	{ "BUCK2_OC", "BUCK2", "overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "BUCK2_NEG_OC", "BUCK2", "negative overcurrent", REGULATOR_EVENT_OVER_CURRENT },
	{ "BUCK2_UV", "BUCK2", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ "BUCK1_RV", "BUCK1", "residual voltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "BUCK2_RV", "BUCK2", "residual voltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "BUCK3_RV", "BUCK3", "residual voltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "LDO1_RV", "LDO1", "residual voltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "LDO2_RV", "LDO2", "residual voltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "BUCK1_RV_SD", "BUCK1", "residual voltage on shutdown",
	 REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "BUCK2_RV_SD", "BUCK2", "residual voltage on shutdown",
	 REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "BUCK3_RV_SD", "BUCK3", "residual voltage on shutdown",
	 REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "LDO1_RV_SD", "LDO1", "residual voltage on shutdown", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "LDO2_RV_SD", "LDO2", "residual voltage on shutdown", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ "SENSOR_2_WARM", "SENSOR2", "warm temperature", REGULATOR_EVENT_OVER_TEMP_WARN },
	{ "SENSOR_1_WARM", "SENSOR1", "warm temperature", REGULATOR_EVENT_OVER_TEMP_WARN },
	{ "SENSOR_0_WARM", "SENSOR0", "warm temperature", REGULATOR_EVENT_OVER_TEMP_WARN },
	{ "SENSOR_2_HOT", "SENSOR2", "hot temperature", REGULATOR_EVENT_OVER_TEMP },
	{ "SENSOR_1_HOT", "SENSOR1", "hot temperature", REGULATOR_EVENT_OVER_TEMP },
	{ "SENSOR_0_HOT", "SENSOR0", "hot temperature", REGULATOR_EVENT_OVER_TEMP },
	{ "TIMEOUT", "", "", REGULATOR_EVENT_ABORT_VOLTAGE_CHANGE },
};

struct tps65219_regulator_irq_data {
	struct device *dev;
	struct tps65219_regulator_irq_type *type;
	struct regulator_dev *rdev;
};

#define TPS65219_REGULATOR(_name, _of, _id, _type, _ops, _n, _vr, _vm, _er, \
			   _em, _cr, _cm, _lr, _nlr, _delay, _fuv, \
			   _ct, _ncl, _bpm) \
	{								\
		.name			= _name,			\
		.of_match		= _of,				\
		.regulators_node	= of_match_ptr("regulators"),	\
		.supply_name		= _of,				\
		.id			= _id,				\
		.ops			= &(_ops),			\
		.n_voltages		= _n,				\
		.type			= _type,			\
		.owner			= THIS_MODULE,			\
		.vsel_reg		= _vr,				\
		.vsel_mask		= _vm,				\
		.csel_reg		= _cr,				\
		.csel_mask		= _cm,				\
		.curr_table		= _ct,				\
		.n_current_limits	= _ncl,				\
		.enable_reg		= _er,				\
		.enable_mask		= _em,				\
		.volt_table		= NULL,				\
		.linear_ranges		= _lr,				\
		.n_linear_ranges	= _nlr,				\
		.ramp_delay		= _delay,			\
		.fixed_uV		= _fuv,				\
		.bypass_reg		= _vr,				\
		.bypass_mask		= _bpm,				\
	}								\

static const struct linear_range bucks_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x1f, 25000),
	REGULATOR_LINEAR_RANGE(1400000, 0x20, 0x33, 100000),
	REGULATOR_LINEAR_RANGE(3400000, 0x34, 0x3f, 0),
};

static const struct linear_range ldo_1_range[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x37, 50000),
	REGULATOR_LINEAR_RANGE(3400000, 0x38, 0x3f, 0),
};

static const struct linear_range tps65214_ldo_1_2_range[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x2, 0),
	REGULATOR_LINEAR_RANGE(650000, 0x3, 0x37, 50000),
	REGULATOR_LINEAR_RANGE(3300000, 0x38, 0x3F, 0),
};

static const struct linear_range tps65215_ldo_2_range[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0x0, 0xC, 50000),
	REGULATOR_LINEAR_RANGE(3300000, 0x36, 0x3F, 0),
};

static const struct linear_range tps65219_ldo_2_range[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x37, 50000),
	REGULATOR_LINEAR_RANGE(3400000, 0x38, 0x3f, 0),
};

static const struct linear_range tps65219_ldos_3_4_range[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0x0, 0xC, 0),
	REGULATOR_LINEAR_RANGE(1250000, 0xD, 0x35, 50000),
	REGULATOR_LINEAR_RANGE(3300000, 0x36, 0x3F, 0),
};

static int tps65219_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct tps65219 *tps = rdev_get_drvdata(dev);

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		return regmap_set_bits(tps->regmap, TPS65219_REG_STBY_1_CONFIG,
				       dev->desc->enable_mask);

	case REGULATOR_MODE_STANDBY:
		return regmap_clear_bits(tps->regmap,
					 TPS65219_REG_STBY_1_CONFIG,
					 dev->desc->enable_mask);
	default:
		return -EINVAL;
	}
}

static unsigned int tps65219_get_mode(struct regulator_dev *dev)
{
	struct tps65219 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);
	int ret, value = 0;

	ret = regmap_read(tps->regmap, TPS65219_REG_STBY_1_CONFIG, &value);
	if (ret) {
		dev_dbg(tps->dev, "%s failed for regulator %s: %d ",
			__func__, dev->desc->name, ret);
		return ret;
	}
	value = (value & BIT(rid)) >> rid;
	if (value)
		return REGULATOR_MODE_STANDBY;
	else
		return REGULATOR_MODE_NORMAL;
}

/* Operations permitted on BUCK1/2/3 */
static const struct regulator_ops bucks_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.set_mode		= tps65219_set_mode,
	.get_mode		= tps65219_get_mode,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,

};

/* Operations permitted on LDO1/2 */
static const struct regulator_ops ldos_1_2_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.set_mode		= tps65219_set_mode,
	.get_mode		= tps65219_get_mode,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_bypass		= regulator_set_bypass_regmap,
	.get_bypass		= regulator_get_bypass_regmap,
};

/* Operations permitted on LDO3/4 */
static const struct regulator_ops ldos_3_4_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.set_mode		= tps65219_set_mode,
	.get_mode		= tps65219_get_mode,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_desc common_regs[] = {
	TPS65219_REGULATOR("BUCK1", "buck1", TPS65219_BUCK_1,
			   REGULATOR_VOLTAGE, bucks_ops, 64,
			   TPS65219_REG_BUCK1_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_BUCK1_EN_MASK, 0, 0, bucks_ranges,
			   3, 4000, 0, NULL, 0, 0),
	TPS65219_REGULATOR("BUCK2", "buck2", TPS65219_BUCK_2,
			   REGULATOR_VOLTAGE, bucks_ops, 64,
			   TPS65219_REG_BUCK2_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_BUCK2_EN_MASK, 0, 0, bucks_ranges,
			   3, 4000, 0, NULL, 0, 0),
	TPS65219_REGULATOR("BUCK3", "buck3", TPS65219_BUCK_3,
			   REGULATOR_VOLTAGE, bucks_ops, 64,
			   TPS65219_REG_BUCK3_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_BUCK3_EN_MASK, 0, 0, bucks_ranges,
			   3, 0, 0, NULL, 0, 0),
};

static const struct regulator_desc tps65214_regs[] = {
	// TPS65214's LDO3 pin maps to TPS65219's LDO3 pin
	TPS65219_REGULATOR("LDO1", "ldo1", TPS65214_LDO_1,
			   REGULATOR_VOLTAGE, ldos_3_4_ops, 64,
			   TPS65214_REG_LDO1_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_LDO3_EN_MASK, 0, 0, tps65214_ldo_1_2_range,
			   3, 0, 0, NULL, 0, 0),
	TPS65219_REGULATOR("LDO2", "ldo2", TPS65214_LDO_2,
			   REGULATOR_VOLTAGE, ldos_3_4_ops, 64,
			   TPS65214_REG_LDO2_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_LDO2_EN_MASK, 0, 0, tps65214_ldo_1_2_range,
			   3, 0, 0, NULL, 0, 0),
};

static const struct regulator_desc tps65215_regs[] = {
	/*
	 *  TPS65215's LDO1 is the same as TPS65219's LDO1. LDO1 is
	 *  configurable as load switch and bypass-mode.
	 *  TPS65215's LDO2 is the same as TPS65219's LDO3
	 */
	TPS65219_REGULATOR("LDO1", "ldo1", TPS65219_LDO_1,
			   REGULATOR_VOLTAGE, ldos_1_2_ops, 64,
			   TPS65219_REG_LDO1_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_LDO1_EN_MASK, 0, 0, ldo_1_range,
			   2, 0, 0, NULL, 0, TPS65219_LDOS_BYP_CONFIG_MASK),
	TPS65219_REGULATOR("LDO2", "ldo2", TPS65215_LDO_2,
			   REGULATOR_VOLTAGE, ldos_3_4_ops, 64,
			   TPS65215_REG_LDO2_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65215_ENABLE_LDO2_EN_MASK, 0, 0, tps65215_ldo_2_range,
			   2, 0, 0, NULL, 0, 0),
};

static const struct regulator_desc tps65219_regs[] = {
	TPS65219_REGULATOR("LDO1", "ldo1", TPS65219_LDO_1,
			   REGULATOR_VOLTAGE, ldos_1_2_ops, 64,
			   TPS65219_REG_LDO1_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_LDO1_EN_MASK, 0, 0, ldo_1_range,
			   2, 0, 0, NULL, 0, TPS65219_LDOS_BYP_CONFIG_MASK),
	TPS65219_REGULATOR("LDO2", "ldo2", TPS65219_LDO_2,
			   REGULATOR_VOLTAGE, ldos_1_2_ops, 64,
			   TPS65219_REG_LDO2_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_LDO2_EN_MASK, 0, 0, tps65219_ldo_2_range,
			   2, 0, 0, NULL, 0, TPS65219_LDOS_BYP_CONFIG_MASK),
	TPS65219_REGULATOR("LDO3", "ldo3", TPS65219_LDO_3,
			   REGULATOR_VOLTAGE, ldos_3_4_ops, 64,
			   TPS65219_REG_LDO3_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_LDO3_EN_MASK, 0, 0, tps65219_ldos_3_4_range,
			   3, 0, 0, NULL, 0, 0),
	TPS65219_REGULATOR("LDO4", "ldo4", TPS65219_LDO_4,
			   REGULATOR_VOLTAGE, ldos_3_4_ops, 64,
			   TPS65219_REG_LDO4_VOUT,
			   TPS65219_BUCKS_LDOS_VOUT_VSET_MASK,
			   TPS65219_REG_ENABLE_CTRL,
			   TPS65219_ENABLE_LDO4_EN_MASK, 0, 0, tps65219_ldos_3_4_range,
			   3, 0, 0, NULL, 0, 0),
};

static irqreturn_t tps65219_regulator_irq_handler(int irq, void *data)
{
	struct tps65219_regulator_irq_data *irq_data = data;

	if (irq_data->type->event_name[0] == '\0') {
		/* This is the timeout interrupt no specific regulator */
		dev_err(irq_data->dev,
			"System was put in shutdown due to timeout during an active or standby transition.\n");
		return IRQ_HANDLED;
	}

	regulator_notifier_call_chain(irq_data->rdev,
				      irq_data->type->event, NULL);

	dev_err(irq_data->dev, "Error IRQ trap %s for %s\n",
		irq_data->type->event_name, irq_data->type->regulator_name);
	return IRQ_HANDLED;
}

struct tps65219_chip_data {
	size_t rdesc_size;
	size_t common_rdesc_size;
	size_t dev_irq_size;
	size_t common_irq_size;
	const struct regulator_desc *rdesc;
	const struct regulator_desc *common_rdesc;
	struct tps65219_regulator_irq_type *irq_types;
	struct tps65219_regulator_irq_type *common_irq_types;
};

static struct tps65219_chip_data chip_info_table[] = {
	[TPS65214] = {
		.rdesc = tps65214_regs,
		.rdesc_size = ARRAY_SIZE(tps65214_regs),
		.common_rdesc = common_regs,
		.common_rdesc_size = ARRAY_SIZE(common_regs),
		.irq_types = NULL,
		.dev_irq_size = 0,
		.common_irq_types = common_regulator_irq_types,
		.common_irq_size = ARRAY_SIZE(common_regulator_irq_types),
	},
	[TPS65215] = {
		.rdesc = tps65215_regs,
		.rdesc_size = ARRAY_SIZE(tps65215_regs),
		.common_rdesc = common_regs,
		.common_rdesc_size = ARRAY_SIZE(common_regs),
		.irq_types = tps65215_regulator_irq_types,
		.dev_irq_size = ARRAY_SIZE(tps65215_regulator_irq_types),
		.common_irq_types = common_regulator_irq_types,
		.common_irq_size = ARRAY_SIZE(common_regulator_irq_types),
	},
	[TPS65219] = {
		.rdesc = tps65219_regs,
		.rdesc_size = ARRAY_SIZE(tps65219_regs),
		.common_rdesc = common_regs,
		.common_rdesc_size = ARRAY_SIZE(common_regs),
		.irq_types = tps65219_regulator_irq_types,
		.dev_irq_size = ARRAY_SIZE(tps65219_regulator_irq_types),
		.common_irq_types = common_regulator_irq_types,
		.common_irq_size = ARRAY_SIZE(common_regulator_irq_types),
	},
};

static int tps65219_regulator_probe(struct platform_device *pdev)
{
	struct tps65219_regulator_irq_data *irq_data;
	struct tps65219_regulator_irq_type *irq_type;
	struct tps65219_chip_data *pmic;
	struct regulator_dev *rdev;
	int error;
	int irq;
	int i;

	struct tps65219 *tps = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	enum pmic_id chip = platform_get_device_id(pdev)->driver_data;

	pmic = &chip_info_table[chip];

	config.dev = tps->dev;
	config.driver_data = tps;
	config.regmap = tps->regmap;

	for (i = 0; i <  pmic->common_rdesc_size; i++) {
		rdev = devm_regulator_register(&pdev->dev, &pmic->common_rdesc[i],
					       &config);
		if (IS_ERR(rdev))
			return dev_err_probe(tps->dev, PTR_ERR(rdev),
					      "Failed to register %s regulator\n",
					      pmic->common_rdesc[i].name);
	}

	for (i = 0; i <  pmic->rdesc_size; i++) {
		rdev = devm_regulator_register(&pdev->dev, &pmic->rdesc[i],
					       &config);
		if (IS_ERR(rdev))
			return dev_err_probe(tps->dev, PTR_ERR(rdev),
					     "Failed to register %s regulator\n",
					     pmic->rdesc[i].name);
	}

	for (i = 0; i < pmic->common_irq_size; ++i) {
		irq_type = &pmic->common_irq_types[i];
		irq = platform_get_irq_byname(pdev, irq_type->irq_name);
		if (irq < 0)
			return -EINVAL;

		irq_data = devm_kmalloc(tps->dev, sizeof(*irq_data), GFP_KERNEL);
		if (!irq_data)
			return -ENOMEM;

		irq_data->dev = tps->dev;
		irq_data->type = irq_type;
		error = devm_request_threaded_irq(tps->dev, irq, NULL,
						  tps65219_regulator_irq_handler,
						  IRQF_ONESHOT,
						  irq_type->irq_name,
						  irq_data);
		if (error)
			return dev_err_probe(tps->dev, error,
					     "Failed to request %s IRQ %d\n",
					     irq_type->irq_name, irq);
	}

	for (i = 0; i < pmic->dev_irq_size; ++i) {
		irq_type = &pmic->irq_types[i];
		irq = platform_get_irq_byname(pdev, irq_type->irq_name);
		if (irq < 0)
			return -EINVAL;

		irq_data = devm_kmalloc(tps->dev, sizeof(*irq_data), GFP_KERNEL);
		if (!irq_data)
			return -ENOMEM;

		irq_data->dev = tps->dev;
		irq_data->type = irq_type;
		error = devm_request_threaded_irq(tps->dev, irq, NULL,
						  tps65219_regulator_irq_handler,
						  IRQF_ONESHOT,
						  irq_type->irq_name,
						  irq_data);
		if (error)
			return dev_err_probe(tps->dev, error,
					     "Failed to request %s IRQ %d\n",
					     irq_type->irq_name, irq);
	}

	return 0;
}

static const struct platform_device_id tps65219_regulator_id_table[] = {
	{ "tps65214-regulator", TPS65214 },
	{ "tps65215-regulator", TPS65215 },
	{ "tps65219-regulator", TPS65219 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps65219_regulator_id_table);

static struct platform_driver tps65219_regulator_driver = {
	.driver = {
		.name = "tps65219-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = tps65219_regulator_probe,
	.id_table = tps65219_regulator_id_table,
};

module_platform_driver(tps65219_regulator_driver);

MODULE_AUTHOR("Jerome Neanne <j-neanne@baylibre.com>");
MODULE_DESCRIPTION("TPS65214/TPS65215/TPS65219 Regulator driver");
MODULE_LICENSE("GPL");
