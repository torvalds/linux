// SPDX-License-Identifier: GPL-2.0
//
// Regulator driver for tps6594 PMIC
//
// Copyright (C) 2023 BayLibre Incorporated - https://www.baylibre.com/

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#include <linux/mfd/tps6594.h>

#define BUCK_NB		5
#define LDO_NB		4
#define MULTI_PHASE_NB	4
#define REGS_INT_NB	4

enum tps6594_regulator_id {
	/* DCDC's */
	TPS6594_BUCK_1,
	TPS6594_BUCK_2,
	TPS6594_BUCK_3,
	TPS6594_BUCK_4,
	TPS6594_BUCK_5,

	/* LDOs */
	TPS6594_LDO_1,
	TPS6594_LDO_2,
	TPS6594_LDO_3,
	TPS6594_LDO_4,
};

enum tps6594_multi_regulator_id {
	/* Multi-phase DCDC's */
	TPS6594_BUCK_12,
	TPS6594_BUCK_34,
	TPS6594_BUCK_123,
	TPS6594_BUCK_1234,
};

struct tps6594_regulator_irq_type {
	const char *irq_name;
	const char *regulator_name;
	const char *event_name;
	unsigned long event;
};

static struct tps6594_regulator_irq_type tps6594_ext_regulator_irq_types[] = {
	{ TPS6594_IRQ_NAME_VCCA_OV, "VCCA", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_VCCA_UV, "VCCA", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_VMON1_OV, "VMON1", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_VMON1_UV, "VMON1", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_VMON1_RV, "VMON1", "residual voltage",
	  REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_VMON2_OV, "VMON2", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_VMON2_UV, "VMON2", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_VMON2_RV, "VMON2", "residual voltage",
	  REGULATOR_EVENT_OVER_VOLTAGE_WARN },
};

struct tps6594_regulator_irq_data {
	struct device *dev;
	struct tps6594_regulator_irq_type *type;
	struct regulator_dev *rdev;
};

struct tps6594_ext_regulator_irq_data {
	struct device *dev;
	struct tps6594_regulator_irq_type *type;
};

#define TPS6594_REGULATOR(_name, _of, _id, _type, _ops, _n, _vr, _vm, _er, \
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
	REGULATOR_LINEAR_RANGE(300000, 0x0, 0xe, 20000),
	REGULATOR_LINEAR_RANGE(600000, 0xf, 0x72, 5000),
	REGULATOR_LINEAR_RANGE(1100000, 0x73, 0xaa, 10000),
	REGULATOR_LINEAR_RANGE(1660000, 0xab, 0xff, 20000),
};

static const struct linear_range ldos_1_2_3_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x4, 0x3a, 50000),
};

static const struct linear_range ldos_4_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0x20, 0x74, 25000),
};

/* Operations permitted on BUCK1/2/3/4/5 */
static const struct regulator_ops tps6594_bucks_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,

};

/* Operations permitted on LDO1/2/3 */
static const struct regulator_ops tps6594_ldos_1_2_3_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_bypass		= regulator_set_bypass_regmap,
	.get_bypass		= regulator_get_bypass_regmap,
};

/* Operations permitted on LDO4 */
static const struct regulator_ops tps6594_ldos_4_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_desc buck_regs[] = {
	TPS6594_REGULATOR("BUCK1", "buck1", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(0),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(0),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK2", "buck2", TPS6594_BUCK_2,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(1),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(1),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK3", "buck3", TPS6594_BUCK_3,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(2),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(2),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK4", "buck4", TPS6594_BUCK_4,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(3),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(3),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK5", "buck5", TPS6594_BUCK_5,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(4),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(4),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 0, 0, NULL, 0, 0),
};

static struct tps6594_regulator_irq_type tps6594_buck1_irq_types[] = {
	{ TPS6594_IRQ_NAME_BUCK1_OV, "BUCK1", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_BUCK1_UV, "BUCK1", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_BUCK1_SC, "BUCK1", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_BUCK1_ILIM, "BUCK1", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type tps6594_buck2_irq_types[] = {
	{ TPS6594_IRQ_NAME_BUCK2_OV, "BUCK2", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_BUCK2_UV, "BUCK2", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_BUCK2_SC, "BUCK2", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_BUCK2_ILIM, "BUCK2", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type tps6594_buck3_irq_types[] = {
	{ TPS6594_IRQ_NAME_BUCK3_OV, "BUCK3", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_BUCK3_UV, "BUCK3", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_BUCK3_SC, "BUCK3", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_BUCK3_ILIM, "BUCK3", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type tps6594_buck4_irq_types[] = {
	{ TPS6594_IRQ_NAME_BUCK4_OV, "BUCK4", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_BUCK4_UV, "BUCK4", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_BUCK4_SC, "BUCK4", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_BUCK4_ILIM, "BUCK4", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type tps6594_buck5_irq_types[] = {
	{ TPS6594_IRQ_NAME_BUCK5_OV, "BUCK5", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_BUCK5_UV, "BUCK5", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_BUCK5_SC, "BUCK5", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_BUCK5_ILIM, "BUCK5", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type tps6594_ldo1_irq_types[] = {
	{ TPS6594_IRQ_NAME_LDO1_OV, "LDO1", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_LDO1_UV, "LDO1", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_LDO1_SC, "LDO1", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_LDO1_ILIM, "LDO1", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type tps6594_ldo2_irq_types[] = {
	{ TPS6594_IRQ_NAME_LDO2_OV, "LDO2", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_LDO2_UV, "LDO2", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_LDO2_SC, "LDO2", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_LDO2_ILIM, "LDO2", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type tps6594_ldo3_irq_types[] = {
	{ TPS6594_IRQ_NAME_LDO3_OV, "LDO3", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_LDO3_UV, "LDO3", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_LDO3_SC, "LDO3", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_LDO3_ILIM, "LDO3", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type tps6594_ldo4_irq_types[] = {
	{ TPS6594_IRQ_NAME_LDO4_OV, "LDO4", "overvoltage", REGULATOR_EVENT_OVER_VOLTAGE_WARN },
	{ TPS6594_IRQ_NAME_LDO4_UV, "LDO4", "undervoltage", REGULATOR_EVENT_UNDER_VOLTAGE },
	{ TPS6594_IRQ_NAME_LDO4_SC, "LDO4", "short circuit", REGULATOR_EVENT_REGULATION_OUT },
	{ TPS6594_IRQ_NAME_LDO4_ILIM, "LDO4", "reach ilim, overcurrent",
	  REGULATOR_EVENT_OVER_CURRENT },
};

static struct tps6594_regulator_irq_type *tps6594_bucks_irq_types[] = {
	tps6594_buck1_irq_types,
	tps6594_buck2_irq_types,
	tps6594_buck3_irq_types,
	tps6594_buck4_irq_types,
	tps6594_buck5_irq_types,
};

static struct tps6594_regulator_irq_type *tps6594_ldos_irq_types[] = {
	tps6594_ldo1_irq_types,
	tps6594_ldo2_irq_types,
	tps6594_ldo3_irq_types,
	tps6594_ldo4_irq_types,
};

static const struct regulator_desc multi_regs[] = {
	TPS6594_REGULATOR("BUCK12", "buck12", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(1),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(1),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 4000, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK34", "buck34", TPS6594_BUCK_3,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(3),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(3),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK123", "buck123", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(1),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(1),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 4000, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK1234", "buck1234", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(1),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(1),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 4000, 0, NULL, 0, 0),
};

static const struct regulator_desc ldo_regs[] = {
	TPS6594_REGULATOR("LDO1", "ldo1", TPS6594_LDO_1,
			  REGULATOR_VOLTAGE, tps6594_ldos_1_2_3_ops, TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_VOUT(0),
			  TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_CTRL(0),
			  TPS6594_BIT_LDO_EN, 0, 0, ldos_1_2_3_ranges,
			  1, 0, 0, NULL, 0, TPS6594_BIT_LDO_BYPASS),
	TPS6594_REGULATOR("LDO2", "ldo2", TPS6594_LDO_2,
			  REGULATOR_VOLTAGE, tps6594_ldos_1_2_3_ops, TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_VOUT(1),
			  TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_CTRL(1),
			  TPS6594_BIT_LDO_EN, 0, 0, ldos_1_2_3_ranges,
			  1, 0, 0, NULL, 0, TPS6594_BIT_LDO_BYPASS),
	TPS6594_REGULATOR("LDO3", "ldo3", TPS6594_LDO_3,
			  REGULATOR_VOLTAGE, tps6594_ldos_1_2_3_ops, TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_VOUT(2),
			  TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_CTRL(2),
			  TPS6594_BIT_LDO_EN, 0, 0, ldos_1_2_3_ranges,
			  1, 0, 0, NULL, 0, TPS6594_BIT_LDO_BYPASS),
	TPS6594_REGULATOR("LDO4", "ldo4", TPS6594_LDO_4,
			  REGULATOR_VOLTAGE, tps6594_ldos_4_ops, TPS6594_MASK_LDO4_VSET >> 1,
			  TPS6594_REG_LDOX_VOUT(3),
			  TPS6594_MASK_LDO4_VSET,
			  TPS6594_REG_LDOX_CTRL(3),
			  TPS6594_BIT_LDO_EN, 0, 0, ldos_4_ranges,
			  1, 0, 0, NULL, 0, 0),
};

static irqreturn_t tps6594_regulator_irq_handler(int irq, void *data)
{
	struct tps6594_regulator_irq_data *irq_data = data;

	if (irq_data->type->event_name[0] == '\0') {
		/* This is the timeout interrupt no specific regulator */
		dev_err(irq_data->dev,
			"System was put in shutdown due to timeout during an active or standby transition.\n");
		return IRQ_HANDLED;
	}

	dev_err(irq_data->dev, "Error IRQ trap %s for %s\n",
		irq_data->type->event_name, irq_data->type->regulator_name);

	regulator_notifier_call_chain(irq_data->rdev,
				      irq_data->type->event, NULL);

	return IRQ_HANDLED;
}

static int tps6594_request_reg_irqs(struct platform_device *pdev,
				    struct regulator_dev *rdev,
				    struct tps6594_regulator_irq_data *irq_data,
				    struct tps6594_regulator_irq_type *tps6594_regs_irq_types,
				    int *irq_idx)
{
	struct tps6594_regulator_irq_type *irq_type;
	struct tps6594 *tps = dev_get_drvdata(pdev->dev.parent);
	int j;
	int irq;
	int error;

	for (j = 0; j < REGS_INT_NB; j++) {
		irq_type = &tps6594_regs_irq_types[j];
		irq = platform_get_irq_byname(pdev, irq_type->irq_name);
		if (irq < 0)
			return -EINVAL;

		irq_data[*irq_idx].dev = tps->dev;
		irq_data[*irq_idx].type = irq_type;
		irq_data[*irq_idx].rdev = rdev;

		error = devm_request_threaded_irq(tps->dev, irq, NULL,
						  tps6594_regulator_irq_handler, IRQF_ONESHOT,
						  irq_type->irq_name, &irq_data[*irq_idx]);
		if (error) {
			dev_err(tps->dev, "tps6594 failed to request %s IRQ %d: %d\n",
				irq_type->irq_name, irq, error);
			return error;
		}
		(*irq_idx)++;
	}
	return 0;
}

static int tps6594_regulator_probe(struct platform_device *pdev)
{
	struct tps6594 *tps = dev_get_drvdata(pdev->dev.parent);
	struct regulator_dev *rdev;
	struct device_node *np = NULL;
	struct device_node *np_pmic_parent = NULL;
	struct regulator_config config = {};
	struct tps6594_regulator_irq_data *irq_data;
	struct tps6594_ext_regulator_irq_data *irq_ext_reg_data;
	struct tps6594_regulator_irq_type *irq_type;
	u8 buck_configured[BUCK_NB] = { 0 };
	u8 buck_multi[MULTI_PHASE_NB] = { 0 };
	static const char * const multiphases[] = {"buck12", "buck123", "buck1234", "buck34"};
	static const char *npname;
	int error, i, irq, multi, delta;
	int irq_idx = 0;
	int buck_idx = 0;
	size_t ext_reg_irq_nb = 2;
	size_t reg_irq_nb;
	enum {
		MULTI_BUCK12,
		MULTI_BUCK123,
		MULTI_BUCK1234,
		MULTI_BUCK12_34,
		MULTI_FIRST = MULTI_BUCK12,
		MULTI_LAST = MULTI_BUCK12_34,
		MULTI_NUM = MULTI_LAST - MULTI_FIRST + 1
	};

	config.dev = tps->dev;
	config.driver_data = tps;
	config.regmap = tps->regmap;

	/*
	 * Switch case defines different possible multi phase config
	 * This is based on dts buck node name.
	 * Buck node name must be chosen accordingly.
	 * Default case is no Multiphase buck.
	 * In case of Multiphase configuration, value should be defined for
	 * buck_configured to avoid creating bucks for every buck in multiphase
	 */
	for (multi = MULTI_FIRST; multi < MULTI_NUM; multi++) {
		np = of_find_node_by_name(tps->dev->of_node, multiphases[multi]);
		npname = of_node_full_name(np);
		np_pmic_parent = of_get_parent(of_get_parent(np));
		if (of_node_cmp(of_node_full_name(np_pmic_parent), tps->dev->of_node->full_name))
			continue;
		delta = strcmp(npname, multiphases[multi]);
		if (!delta) {
			switch (multi) {
			case MULTI_BUCK12:
				buck_multi[0] = 1;
				buck_configured[0] = 1;
				buck_configured[1] = 1;
				break;
			/* multiphase buck34 is supported only with buck12 */
			case MULTI_BUCK12_34:
				buck_multi[0] = 1;
				buck_multi[1] = 1;
				buck_configured[0] = 1;
				buck_configured[1] = 1;
				buck_configured[2] = 1;
				buck_configured[3] = 1;
				break;
			case MULTI_BUCK123:
				buck_multi[2] = 1;
				buck_configured[0] = 1;
				buck_configured[1] = 1;
				buck_configured[2] = 1;
				break;
			case MULTI_BUCK1234:
				buck_multi[3] = 1;
				buck_configured[0] = 1;
				buck_configured[1] = 1;
				buck_configured[2] = 1;
				buck_configured[3] = 1;
				break;
			}
		}
	}

	if (tps->chip_id == LP8764) {
		/* There is only 4 buck on LP8764 */
		buck_configured[4] = 1;
		reg_irq_nb = size_mul(REGS_INT_NB, (BUCK_NB - 1));
	} else {
		reg_irq_nb = size_mul(REGS_INT_NB, (size_add(BUCK_NB, LDO_NB)));
	}

	irq_data = devm_kmalloc_array(tps->dev, reg_irq_nb,
				      sizeof(struct tps6594_regulator_irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	for (i = 0; i < MULTI_PHASE_NB; i++) {
		if (buck_multi[i] == 0)
			continue;

		rdev = devm_regulator_register(&pdev->dev, &multi_regs[i], &config);
		if (IS_ERR(rdev))
			return dev_err_probe(tps->dev, PTR_ERR(rdev),
					     "failed to register %s regulator\n",
					     pdev->name);

		/* config multiphase buck12+buck34 */
		if (i == 1)
			buck_idx = 2;
		error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
						 tps6594_bucks_irq_types[buck_idx], &irq_idx);
		if (error)
			return error;
		error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
						 tps6594_bucks_irq_types[buck_idx + 1], &irq_idx);
		if (error)
			return error;

		if (i == 2 || i == 3) {
			error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
							 tps6594_bucks_irq_types[buck_idx + 2],
							 &irq_idx);
			if (error)
				return error;
		}
		if (i == 3) {
			error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
							 tps6594_bucks_irq_types[buck_idx + 3],
							 &irq_idx);
			if (error)
				return error;
		}
	}

	for (i = 0; i < BUCK_NB; i++) {
		if (buck_configured[i] == 1)
			continue;

		rdev = devm_regulator_register(&pdev->dev, &buck_regs[i], &config);
		if (IS_ERR(rdev))
			return dev_err_probe(tps->dev, PTR_ERR(rdev),
					     "failed to register %s regulator\n",
					     pdev->name);

		error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
						 tps6594_bucks_irq_types[i], &irq_idx);
		if (error)
			return error;
	}

	/* LP8764 dosen't have LDO */
	if (tps->chip_id != LP8764) {
		for (i = 0; i < ARRAY_SIZE(ldo_regs); i++) {
			rdev = devm_regulator_register(&pdev->dev, &ldo_regs[i], &config);
			if (IS_ERR(rdev))
				return dev_err_probe(tps->dev, PTR_ERR(rdev),
						     "failed to register %s regulator\n",
						     pdev->name);

			error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
							 tps6594_ldos_irq_types[i],
							 &irq_idx);
			if (error)
				return error;
		}
	}

	if (tps->chip_id == LP8764)
		ext_reg_irq_nb = ARRAY_SIZE(tps6594_ext_regulator_irq_types);

	irq_ext_reg_data = devm_kmalloc_array(tps->dev,
					ext_reg_irq_nb,
					sizeof(struct tps6594_ext_regulator_irq_data),
					GFP_KERNEL);
	if (!irq_ext_reg_data)
		return -ENOMEM;

	for (i = 0; i < ext_reg_irq_nb; ++i) {
		irq_type = &tps6594_ext_regulator_irq_types[i];

		irq = platform_get_irq_byname(pdev, irq_type->irq_name);
		if (irq < 0)
			return -EINVAL;

		irq_ext_reg_data[i].dev = tps->dev;
		irq_ext_reg_data[i].type = irq_type;

		error = devm_request_threaded_irq(tps->dev, irq, NULL,
						  tps6594_regulator_irq_handler,
						  IRQF_ONESHOT,
						  irq_type->irq_name,
						  &irq_ext_reg_data[i]);
		if (error)
			return dev_err_probe(tps->dev, error,
					     "failed to request %s IRQ %d\n",
					     irq_type->irq_name, irq);
	}
	return 0;
}

static struct platform_driver tps6594_regulator_driver = {
	.driver = {
		.name = "tps6594-regulator",
	},
	.probe = tps6594_regulator_probe,
};

module_platform_driver(tps6594_regulator_driver);

MODULE_ALIAS("platform:tps6594-regulator");
MODULE_AUTHOR("Jerome Neanne <jneanne@baylibre.com>");
MODULE_DESCRIPTION("TPS6594 voltage regulator driver");
MODULE_LICENSE("GPL");
