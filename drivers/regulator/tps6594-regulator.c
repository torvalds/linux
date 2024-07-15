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

#define BUCK_NB			5
#define LDO_NB			4
#define MULTI_PHASE_NB		4
/* TPS6593 and LP8764 supports OV, UV, SC, ILIM */
#define REGS_INT_NB		4
/* TPS65224 supports OV or UV */
#define TPS65224_REGS_INT_NB	1

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

static struct tps6594_regulator_irq_type tps65224_ext_regulator_irq_types[] = {
	{ TPS65224_IRQ_NAME_VCCA_UVOV, "VCCA", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
	{ TPS65224_IRQ_NAME_VMON1_UVOV, "VMON1", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
	{ TPS65224_IRQ_NAME_VMON2_UVOV, "VMON2", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
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

/* Voltage range for TPS65224 Bucks and LDOs */
static const struct linear_range tps65224_bucks_1_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0a, 0x0e, 20000),
	REGULATOR_LINEAR_RANGE(600000, 0x0f, 0x72, 5000),
	REGULATOR_LINEAR_RANGE(1100000, 0x73, 0xaa, 10000),
	REGULATOR_LINEAR_RANGE(1660000, 0xab, 0xfd, 20000),
};

static const struct linear_range tps65224_bucks_2_3_4_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x1a, 25000),
	REGULATOR_LINEAR_RANGE(1200000, 0x1b, 0x45, 50000),
};

static const struct linear_range tps65224_ldos_1_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0xC, 0x36, 50000),
};

static const struct linear_range tps65224_ldos_2_3_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x38, 50000),
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

/* Buck configuration for TPS65224 */
static const struct regulator_desc tps65224_buck_regs[] = {
	TPS6594_REGULATOR("BUCK1", "buck1", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS65224_MASK_BUCK1_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(0),
			  TPS65224_MASK_BUCK1_VSET,
			  TPS6594_REG_BUCKX_CTRL(0),
			  TPS6594_BIT_BUCK_EN, 0, 0, tps65224_bucks_1_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK2", "buck2", TPS6594_BUCK_2,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS65224_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(1),
			  TPS65224_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(1),
			  TPS6594_BIT_BUCK_EN, 0, 0, tps65224_bucks_2_3_4_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK3", "buck3", TPS6594_BUCK_3,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS65224_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(2),
			  TPS65224_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(2),
			  TPS6594_BIT_BUCK_EN, 0, 0, tps65224_bucks_2_3_4_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK4", "buck4", TPS6594_BUCK_4,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS65224_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(3),
			  TPS65224_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(3),
			  TPS6594_BIT_BUCK_EN, 0, 0, tps65224_bucks_2_3_4_ranges,
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

static struct tps6594_regulator_irq_type tps65224_buck1_irq_types[] = {
	{ TPS65224_IRQ_NAME_BUCK1_UVOV, "BUCK1", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
};

static struct tps6594_regulator_irq_type tps65224_buck2_irq_types[] = {
	{ TPS65224_IRQ_NAME_BUCK2_UVOV, "BUCK2", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
};

static struct tps6594_regulator_irq_type tps65224_buck3_irq_types[] = {
	{ TPS65224_IRQ_NAME_BUCK3_UVOV, "BUCK3", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
};

static struct tps6594_regulator_irq_type tps65224_buck4_irq_types[] = {
	{ TPS65224_IRQ_NAME_BUCK4_UVOV, "BUCK4", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
};

static struct tps6594_regulator_irq_type tps65224_ldo1_irq_types[] = {
	{ TPS65224_IRQ_NAME_LDO1_UVOV, "LDO1", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
};

static struct tps6594_regulator_irq_type tps65224_ldo2_irq_types[] = {
	{ TPS65224_IRQ_NAME_LDO2_UVOV, "LDO2", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
};

static struct tps6594_regulator_irq_type tps65224_ldo3_irq_types[] = {
	{ TPS65224_IRQ_NAME_LDO3_UVOV, "LDO3", "voltage out of range",
	  REGULATOR_EVENT_REGULATION_OUT },
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

static struct tps6594_regulator_irq_type *tps65224_bucks_irq_types[] = {
	tps65224_buck1_irq_types,
	tps65224_buck2_irq_types,
	tps65224_buck3_irq_types,
	tps65224_buck4_irq_types,
};

static struct tps6594_regulator_irq_type *tps65224_ldos_irq_types[] = {
	tps65224_ldo1_irq_types,
	tps65224_ldo2_irq_types,
	tps65224_ldo3_irq_types,
};

static const struct regulator_desc tps6594_multi_regs[] = {
	TPS6594_REGULATOR("BUCK12", "buck12", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(0),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(0),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 4000, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK34", "buck34", TPS6594_BUCK_3,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(2),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(2),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 0, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK123", "buck123", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(0),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(0),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 4000, 0, NULL, 0, 0),
	TPS6594_REGULATOR("BUCK1234", "buck1234", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(0),
			  TPS6594_MASK_BUCKS_VSET,
			  TPS6594_REG_BUCKX_CTRL(0),
			  TPS6594_BIT_BUCK_EN, 0, 0, bucks_ranges,
			  4, 4000, 0, NULL, 0, 0),
};

static const struct regulator_desc tps65224_multi_regs[] = {
	TPS6594_REGULATOR("BUCK12", "buck12", TPS6594_BUCK_1,
			  REGULATOR_VOLTAGE, tps6594_bucks_ops, TPS65224_MASK_BUCK1_VSET,
			  TPS6594_REG_BUCKX_VOUT_1(0),
			  TPS65224_MASK_BUCK1_VSET,
			  TPS6594_REG_BUCKX_CTRL(0),
			  TPS6594_BIT_BUCK_EN, 0, 0, tps65224_bucks_1_ranges,
			  4, 4000, 0, NULL, 0, 0),
};

static const struct regulator_desc tps6594_ldo_regs[] = {
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

static const struct regulator_desc tps65224_ldo_regs[] = {
	TPS6594_REGULATOR("LDO1", "ldo1", TPS6594_LDO_1,
			  REGULATOR_VOLTAGE, tps6594_ldos_1_2_3_ops, TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_VOUT(0),
			  TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_CTRL(0),
			  TPS6594_BIT_LDO_EN, 0, 0, tps65224_ldos_1_ranges,
			  1, 0, 0, NULL, 0, TPS6594_BIT_LDO_BYPASS),
	TPS6594_REGULATOR("LDO2", "ldo2", TPS6594_LDO_2,
			  REGULATOR_VOLTAGE, tps6594_ldos_1_2_3_ops, TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_VOUT(1),
			  TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_CTRL(1),
			  TPS6594_BIT_LDO_EN, 0, 0, tps65224_ldos_2_3_ranges,
			  1, 0, 0, NULL, 0, TPS6594_BIT_LDO_BYPASS),
	TPS6594_REGULATOR("LDO3", "ldo3", TPS6594_LDO_3,
			  REGULATOR_VOLTAGE, tps6594_ldos_1_2_3_ops, TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_VOUT(2),
			  TPS6594_MASK_LDO123_VSET,
			  TPS6594_REG_LDOX_CTRL(2),
			  TPS6594_BIT_LDO_EN, 0, 0, tps65224_ldos_2_3_ranges,
			  1, 0, 0, NULL, 0, TPS6594_BIT_LDO_BYPASS),
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
				    struct tps6594_regulator_irq_type *regs_irq_types,
				    size_t interrupt_cnt,
				    int *irq_idx)
{
	struct tps6594_regulator_irq_type *irq_type;
	struct tps6594 *tps = dev_get_drvdata(pdev->dev.parent);
	size_t j;
	int irq;
	int error;

	for (j = 0; j < interrupt_cnt; j++) {
		irq_type = &regs_irq_types[j];
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
	struct tps6594_regulator_irq_type *irq_types;
	bool buck_configured[BUCK_NB] = { false };
	bool buck_multi[MULTI_PHASE_NB] = { false };

	static const char *npname;
	int error, i, irq, multi;
	int irq_idx = 0;
	int buck_idx = 0;
	int nr_ldo;
	int nr_buck;
	int nr_types;
	unsigned int irq_count;
	unsigned int multi_phase_cnt;
	size_t reg_irq_nb;
	struct tps6594_regulator_irq_type **bucks_irq_types;
	const struct regulator_desc *multi_regs;
	struct tps6594_regulator_irq_type **ldos_irq_types;
	const struct regulator_desc *ldo_regs;
	size_t interrupt_count;

	if (tps->chip_id == TPS65224) {
		bucks_irq_types = tps65224_bucks_irq_types;
		interrupt_count = ARRAY_SIZE(tps65224_buck1_irq_types);
		multi_regs = tps65224_multi_regs;
		ldos_irq_types = tps65224_ldos_irq_types;
		ldo_regs = tps65224_ldo_regs;
		multi_phase_cnt = ARRAY_SIZE(tps65224_multi_regs);
	} else {
		bucks_irq_types = tps6594_bucks_irq_types;
		interrupt_count = ARRAY_SIZE(tps6594_buck1_irq_types);
		multi_regs = tps6594_multi_regs;
		ldos_irq_types = tps6594_ldos_irq_types;
		ldo_regs = tps6594_ldo_regs;
		multi_phase_cnt = ARRAY_SIZE(tps6594_multi_regs);
	}

	enum {
		MULTI_BUCK12,
		MULTI_BUCK12_34,
		MULTI_BUCK123,
		MULTI_BUCK1234,
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
	for (multi = 0; multi < multi_phase_cnt; multi++) {
		np = of_find_node_by_name(tps->dev->of_node, multi_regs[multi].supply_name);
		npname = of_node_full_name(np);
		np_pmic_parent = of_get_parent(of_get_parent(np));
		if (of_node_cmp(of_node_full_name(np_pmic_parent), tps->dev->of_node->full_name))
			continue;
		if (strcmp(npname, multi_regs[multi].supply_name) == 0) {
			switch (multi) {
			case MULTI_BUCK12:
				buck_multi[0] = true;
				buck_configured[0] = true;
				buck_configured[1] = true;
				break;
			/* multiphase buck34 is supported only with buck12 */
			case MULTI_BUCK12_34:
				buck_multi[0] = true;
				buck_multi[1] = true;
				buck_configured[0] = true;
				buck_configured[1] = true;
				buck_configured[2] = true;
				buck_configured[3] = true;
				break;
			case MULTI_BUCK123:
				buck_multi[2] = true;
				buck_configured[0] = true;
				buck_configured[1] = true;
				buck_configured[2] = true;
				break;
			case MULTI_BUCK1234:
				buck_multi[3] = true;
				buck_configured[0] = true;
				buck_configured[1] = true;
				buck_configured[2] = true;
				buck_configured[3] = true;
				break;
			}
		}
	}

	if (tps->chip_id == TPS65224) {
		nr_buck = ARRAY_SIZE(tps65224_buck_regs);
		nr_ldo = ARRAY_SIZE(tps65224_ldo_regs);
		nr_types = TPS65224_REGS_INT_NB;
	} else {
		nr_buck = ARRAY_SIZE(buck_regs);
		nr_ldo = (tps->chip_id == LP8764) ? 0 : ARRAY_SIZE(tps6594_ldo_regs);
		nr_types = REGS_INT_NB;
	}

	reg_irq_nb = nr_types * (nr_buck + nr_ldo);

	irq_data = devm_kmalloc_array(tps->dev, reg_irq_nb,
				      sizeof(struct tps6594_regulator_irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	for (i = 0; i < multi_phase_cnt; i++) {
		if (!buck_multi[i])
			continue;

		rdev = devm_regulator_register(&pdev->dev, &multi_regs[i], &config);
		if (IS_ERR(rdev))
			return dev_err_probe(tps->dev, PTR_ERR(rdev),
					     "failed to register %s regulator\n",
					     pdev->name);

		/* config multiphase buck12+buck34 */
		if (i == MULTI_BUCK12_34)
			buck_idx = 2;

		error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
						 bucks_irq_types[buck_idx],
						 interrupt_count, &irq_idx);
		if (error)
			return error;

		error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
						 bucks_irq_types[buck_idx + 1],
						 interrupt_count, &irq_idx);
		if (error)
			return error;

		if (i == MULTI_BUCK123 || i == MULTI_BUCK1234) {
			error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
							 tps6594_bucks_irq_types[buck_idx + 2],
							 interrupt_count,
							 &irq_idx);
			if (error)
				return error;
		}
		if (i == MULTI_BUCK1234) {
			error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
							 tps6594_bucks_irq_types[buck_idx + 3],
							 interrupt_count,
							 &irq_idx);
			if (error)
				return error;
		}
	}

	for (i = 0; i < nr_buck; i++) {
		if (buck_configured[i])
			continue;

		const struct regulator_desc *buck_cfg = (tps->chip_id == TPS65224) ?
							 tps65224_buck_regs : buck_regs;

		rdev = devm_regulator_register(&pdev->dev, &buck_cfg[i], &config);
		if (IS_ERR(rdev))
			return dev_err_probe(tps->dev, PTR_ERR(rdev),
					     "failed to register %s regulator\n", pdev->name);

		error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
						 bucks_irq_types[i], interrupt_count, &irq_idx);
		if (error)
			return error;
	}

	/* LP8764 doesn't have LDO */
	if (tps->chip_id != LP8764) {
		for (i = 0; i < nr_ldo; i++) {
			rdev = devm_regulator_register(&pdev->dev, &ldo_regs[i], &config);
			if (IS_ERR(rdev))
				return dev_err_probe(tps->dev, PTR_ERR(rdev),
						     "failed to register %s regulator\n",
						     pdev->name);

			error = tps6594_request_reg_irqs(pdev, rdev, irq_data,
							 ldos_irq_types[i], interrupt_count,
							 &irq_idx);
			if (error)
				return error;
		}
	}

	if (tps->chip_id == TPS65224) {
		irq_types = tps65224_ext_regulator_irq_types;
		irq_count = ARRAY_SIZE(tps65224_ext_regulator_irq_types);
	} else {
		irq_types = tps6594_ext_regulator_irq_types;
		if (tps->chip_id == LP8764)
			irq_count = ARRAY_SIZE(tps6594_ext_regulator_irq_types);
		else
			/* TPS6593 supports only VCCA OV and UV */
			irq_count = 2;
	}

	irq_ext_reg_data = devm_kmalloc_array(tps->dev,
					      irq_count,
					      sizeof(struct tps6594_ext_regulator_irq_data),
					      GFP_KERNEL);
	if (!irq_ext_reg_data)
		return -ENOMEM;

	for (i = 0; i < irq_count; ++i) {
		irq_type = &irq_types[i];
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
MODULE_AUTHOR("Nirmala Devi Mal Nadar <m.nirmaladevi@ltts.com>");
MODULE_DESCRIPTION("TPS6594 voltage regulator driver");
MODULE_LICENSE("GPL");
