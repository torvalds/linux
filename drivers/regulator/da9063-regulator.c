// SPDX-License-Identifier: GPL-2.0+
//
// Regulator driver for DA9063 PMIC series
//
// Copyright 2012 Dialog Semiconductors Ltd.
// Copyright 2013 Philipp Zabel, Pengutronix
//
// Author: Krystian Garbaciak <krystian.garbaciak@diasemi.com>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/da9063/core.h>
#include <linux/mfd/da9063/registers.h>


/* Definition for registering regmap bit fields using a mask */
#define BFIELD(_reg, _mask) \
	REG_FIELD(_reg, __builtin_ffs((int)_mask) - 1, \
		sizeof(unsigned int) * 8 - __builtin_clz((_mask)) - 1)

/* DA9063 and DA9063L regulator IDs */
enum {
	/* BUCKs */
	DA9063_ID_BCORE1,
	DA9063_ID_BCORE2,
	DA9063_ID_BPRO,
	DA9063_ID_BMEM,
	DA9063_ID_BIO,
	DA9063_ID_BPERI,

	/* BCORE1 and BCORE2 in merged mode */
	DA9063_ID_BCORES_MERGED,
	/* BMEM and BIO in merged mode */
	DA9063_ID_BMEM_BIO_MERGED,
	/* When two BUCKs are merged, they cannot be reused separately */

	/* LDOs on both DA9063 and DA9063L */
	DA9063_ID_LDO3,
	DA9063_ID_LDO7,
	DA9063_ID_LDO8,
	DA9063_ID_LDO9,
	DA9063_ID_LDO11,

	/* DA9063-only LDOs */
	DA9063_ID_LDO1,
	DA9063_ID_LDO2,
	DA9063_ID_LDO4,
	DA9063_ID_LDO5,
	DA9063_ID_LDO6,
	DA9063_ID_LDO10,
};

/* Old regulator platform data */
struct da9063_regulator_data {
	int				id;
	struct regulator_init_data	*initdata;
};

struct da9063_regulators_pdata {
	unsigned int			n_regulators;
	struct da9063_regulator_data	*regulator_data;
};

/* Regulator capabilities and registers description */
struct da9063_regulator_info {
	struct regulator_desc desc;

	/* DA9063 main register fields */
	struct reg_field mode;		/* buck mode of operation */
	struct reg_field suspend;
	struct reg_field sleep;
	struct reg_field suspend_sleep;
	unsigned int suspend_vsel_reg;

	/* DA9063 event detection bit */
	struct reg_field oc_event;
};

/* Macros for LDO */
#define DA9063_LDO(chip, regl_name, min_mV, step_mV, max_mV) \
	.desc.id = chip##_ID_##regl_name, \
	.desc.name = __stringify(chip##_##regl_name), \
	.desc.ops = &da9063_ldo_ops, \
	.desc.min_uV = (min_mV) * 1000, \
	.desc.uV_step = (step_mV) * 1000, \
	.desc.n_voltages = (((max_mV) - (min_mV))/(step_mV) + 1 \
		+ (DA9063_V##regl_name##_BIAS)), \
	.desc.enable_reg = DA9063_REG_##regl_name##_CONT, \
	.desc.enable_mask = DA9063_LDO_EN, \
	.desc.vsel_reg = DA9063_REG_V##regl_name##_A, \
	.desc.vsel_mask = DA9063_V##regl_name##_MASK, \
	.desc.linear_min_sel = DA9063_V##regl_name##_BIAS, \
	.sleep = BFIELD(DA9063_REG_V##regl_name##_A, DA9063_LDO_SL), \
	.suspend_sleep = BFIELD(DA9063_REG_V##regl_name##_B, DA9063_LDO_SL), \
	.suspend_vsel_reg = DA9063_REG_V##regl_name##_B

/* Macros for voltage DC/DC converters (BUCKs) */
#define DA9063_BUCK(chip, regl_name, min_mV, step_mV, max_mV, limits_array, \
		    creg, cmask) \
	.desc.id = chip##_ID_##regl_name, \
	.desc.name = __stringify(chip##_##regl_name), \
	.desc.ops = &da9063_buck_ops, \
	.desc.min_uV = (min_mV) * 1000, \
	.desc.uV_step = (step_mV) * 1000, \
	.desc.n_voltages = ((max_mV) - (min_mV))/(step_mV) + 1, \
	.desc.csel_reg = (creg), \
	.desc.csel_mask = (cmask), \
	.desc.curr_table = limits_array, \
	.desc.n_current_limits = ARRAY_SIZE(limits_array)

#define DA9063_BUCK_COMMON_FIELDS(regl_name) \
	.desc.enable_reg = DA9063_REG_##regl_name##_CONT, \
	.desc.enable_mask = DA9063_BUCK_EN, \
	.desc.vsel_reg = DA9063_REG_V##regl_name##_A, \
	.desc.vsel_mask = DA9063_VBUCK_MASK, \
	.desc.linear_min_sel = DA9063_VBUCK_BIAS, \
	.sleep = BFIELD(DA9063_REG_V##regl_name##_A, DA9063_BUCK_SL), \
	.suspend_sleep = BFIELD(DA9063_REG_V##regl_name##_B, DA9063_BUCK_SL), \
	.suspend_vsel_reg = DA9063_REG_V##regl_name##_B, \
	.mode = BFIELD(DA9063_REG_##regl_name##_CFG, DA9063_BUCK_MODE_MASK)

/* Defines asignment of regulators info table to chip model */
struct da9063_dev_model {
	const struct da9063_regulator_info	*regulator_info;
	unsigned int				n_regulators;
	enum da9063_type			type;
};

/* Single regulator settings */
struct da9063_regulator {
	struct regulator_desc			desc;
	struct regulator_dev			*rdev;
	struct da9063				*hw;
	const struct da9063_regulator_info	*info;

	struct regmap_field			*mode;
	struct regmap_field			*suspend;
	struct regmap_field			*sleep;
	struct regmap_field			*suspend_sleep;
};

/* Encapsulates all information for the regulators driver */
struct da9063_regulators {
	unsigned int				n_regulators;
	/* Array size to be defined during init. Keep at end. */
	struct da9063_regulator			regulator[];
};

/* BUCK modes for DA9063 */
enum {
	BUCK_MODE_MANUAL,	/* 0 */
	BUCK_MODE_SLEEP,	/* 1 */
	BUCK_MODE_SYNC,		/* 2 */
	BUCK_MODE_AUTO		/* 3 */
};

/* Regulator operations */

/*
 * Current limits array (in uA) for BCORE1, BCORE2, BPRO.
 * Entry indexes corresponds to register values.
 */
static const unsigned int da9063_buck_a_limits[] = {
	 500000,  600000,  700000,  800000,  900000, 1000000, 1100000, 1200000,
	1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1900000, 2000000
};

/*
 * Current limits array (in uA) for BMEM, BIO, BPERI.
 * Entry indexes corresponds to register values.
 */
static const unsigned int da9063_buck_b_limits[] = {
	1500000, 1600000, 1700000, 1800000, 1900000, 2000000, 2100000, 2200000,
	2300000, 2400000, 2500000, 2600000, 2700000, 2800000, 2900000, 3000000
};

/*
 * Current limits array (in uA) for merged BCORE1 and BCORE2.
 * Entry indexes corresponds to register values.
 */
static const unsigned int da9063_bcores_merged_limits[] = {
	1000000, 1200000, 1400000, 1600000, 1800000, 2000000, 2200000, 2400000,
	2600000, 2800000, 3000000, 3200000, 3400000, 3600000, 3800000, 4000000
};

/*
 * Current limits array (in uA) for merged BMEM and BIO.
 * Entry indexes corresponds to register values.
 */
static const unsigned int da9063_bmem_bio_merged_limits[] = {
	3000000, 3200000, 3400000, 3600000, 3800000, 4000000, 4200000, 4400000,
	4600000, 4800000, 5000000, 5200000, 5400000, 5600000, 5800000, 6000000
};

static int da9063_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);
	unsigned int val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = BUCK_MODE_SYNC;
		break;
	case REGULATOR_MODE_NORMAL:
		val = BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = BUCK_MODE_SLEEP;
		break;
	default:
		return -EINVAL;
	}

	return regmap_field_write(regl->mode, val);
}

/*
 * Bucks use single mode register field for normal operation
 * and suspend state.
 * There are 3 modes to map to: FAST, NORMAL, and STANDBY.
 */

static unsigned int da9063_buck_get_mode(struct regulator_dev *rdev)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);
	struct regmap_field *field;
	unsigned int val;
	int ret;

	ret = regmap_field_read(regl->mode, &val);
	if (ret < 0)
		return ret;

	switch (val) {
	default:
	case BUCK_MODE_MANUAL:
		/* Sleep flag bit decides the mode */
		break;
	case BUCK_MODE_SLEEP:
		return REGULATOR_MODE_STANDBY;
	case BUCK_MODE_SYNC:
		return REGULATOR_MODE_FAST;
	case BUCK_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	}

	/* Detect current regulator state */
	ret = regmap_field_read(regl->suspend, &val);
	if (ret < 0)
		return 0;

	/* Read regulator mode from proper register, depending on state */
	if (val)
		field = regl->suspend_sleep;
	else
		field = regl->sleep;

	ret = regmap_field_read(field, &val);
	if (ret < 0)
		return 0;

	if (val)
		return REGULATOR_MODE_STANDBY;
	else
		return REGULATOR_MODE_FAST;
}

/*
 * LDOs use sleep flags - one for normal and one for suspend state.
 * There are 2 modes to map to: NORMAL and STANDBY (sleep) for each state.
 */

static int da9063_ldo_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);
	unsigned int val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_STANDBY:
		val = 1;
		break;
	default:
		return -EINVAL;
	}

	return regmap_field_write(regl->sleep, val);
}

static unsigned int da9063_ldo_get_mode(struct regulator_dev *rdev)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);
	struct regmap_field *field;
	int ret, val;

	/* Detect current regulator state */
	ret = regmap_field_read(regl->suspend, &val);
	if (ret < 0)
		return 0;

	/* Read regulator mode from proper register, depending on state */
	if (val)
		field = regl->suspend_sleep;
	else
		field = regl->sleep;

	ret = regmap_field_read(field, &val);
	if (ret < 0)
		return 0;

	if (val)
		return REGULATOR_MODE_STANDBY;
	else
		return REGULATOR_MODE_NORMAL;
}

static int da9063_buck_get_status(struct regulator_dev *rdev)
{
	int ret = regulator_is_enabled_regmap(rdev);

	if (ret == 0) {
		ret = REGULATOR_STATUS_OFF;
	} else if (ret > 0) {
		ret = da9063_buck_get_mode(rdev);
		if (ret > 0)
			ret = regulator_mode_to_status(ret);
		else if (ret == 0)
			ret = -EIO;
	}

	return ret;
}

static int da9063_ldo_get_status(struct regulator_dev *rdev)
{
	int ret = regulator_is_enabled_regmap(rdev);

	if (ret == 0) {
		ret = REGULATOR_STATUS_OFF;
	} else if (ret > 0) {
		ret = da9063_ldo_get_mode(rdev);
		if (ret > 0)
			ret = regulator_mode_to_status(ret);
		else if (ret == 0)
			ret = -EIO;
	}

	return ret;
}

static int da9063_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);
	const struct da9063_regulator_info *rinfo = regl->info;
	int ret, sel;

	sel = regulator_map_voltage_linear(rdev, uV, uV);
	if (sel < 0)
		return sel;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;

	ret = regmap_update_bits(regl->hw->regmap, rinfo->suspend_vsel_reg,
				 rdev->desc->vsel_mask, sel);

	return ret;
}

static int da9063_suspend_enable(struct regulator_dev *rdev)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);

	return regmap_field_write(regl->suspend, 1);
}

static int da9063_suspend_disable(struct regulator_dev *rdev)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);

	return regmap_field_write(regl->suspend, 0);
}

static int da9063_buck_set_suspend_mode(struct regulator_dev *rdev,
				unsigned int mode)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);
	int val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = BUCK_MODE_SYNC;
		break;
	case REGULATOR_MODE_NORMAL:
		val = BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = BUCK_MODE_SLEEP;
		break;
	default:
		return -EINVAL;
	}

	return regmap_field_write(regl->mode, val);
}

static int da9063_ldo_set_suspend_mode(struct regulator_dev *rdev,
				unsigned int mode)
{
	struct da9063_regulator *regl = rdev_get_drvdata(rdev);
	unsigned int val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_STANDBY:
		val = 1;
		break;
	default:
		return -EINVAL;
	}

	return regmap_field_write(regl->suspend_sleep, val);
}

static const struct regulator_ops da9063_buck_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.set_current_limit	= regulator_set_current_limit_regmap,
	.get_current_limit	= regulator_get_current_limit_regmap,
	.set_mode		= da9063_buck_set_mode,
	.get_mode		= da9063_buck_get_mode,
	.get_status		= da9063_buck_get_status,
	.set_suspend_voltage	= da9063_set_suspend_voltage,
	.set_suspend_enable	= da9063_suspend_enable,
	.set_suspend_disable	= da9063_suspend_disable,
	.set_suspend_mode	= da9063_buck_set_suspend_mode,
};

static const struct regulator_ops da9063_ldo_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.set_mode		= da9063_ldo_set_mode,
	.get_mode		= da9063_ldo_get_mode,
	.get_status		= da9063_ldo_get_status,
	.set_suspend_voltage	= da9063_set_suspend_voltage,
	.set_suspend_enable	= da9063_suspend_enable,
	.set_suspend_disable	= da9063_suspend_disable,
	.set_suspend_mode	= da9063_ldo_set_suspend_mode,
};

/* Info of regulators for DA9063 */
static const struct da9063_regulator_info da9063_regulator_info[] = {
	{
		DA9063_BUCK(DA9063, BCORE1, 300, 10, 1570,
			    da9063_buck_a_limits,
			    DA9063_REG_BUCK_ILIM_C, DA9063_BCORE1_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BCORE1),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VBCORE1_SEL),
	},
	{
		DA9063_BUCK(DA9063, BCORE2, 300, 10, 1570,
			    da9063_buck_a_limits,
			    DA9063_REG_BUCK_ILIM_C, DA9063_BCORE2_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BCORE2),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VBCORE2_SEL),
	},
	{
		DA9063_BUCK(DA9063, BPRO, 530, 10, 1800,
			    da9063_buck_a_limits,
			    DA9063_REG_BUCK_ILIM_B, DA9063_BPRO_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BPRO),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VBPRO_SEL),
	},
	{
		DA9063_BUCK(DA9063, BMEM, 800, 20, 3340,
			    da9063_buck_b_limits,
			    DA9063_REG_BUCK_ILIM_A, DA9063_BMEM_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BMEM),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VBMEM_SEL),
	},
	{
		DA9063_BUCK(DA9063, BIO, 800, 20, 3340,
			    da9063_buck_b_limits,
			    DA9063_REG_BUCK_ILIM_A, DA9063_BIO_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BIO),
		.suspend = BFIELD(DA9063_REG_DVC_2, DA9063_VBIO_SEL),
	},
	{
		DA9063_BUCK(DA9063, BPERI, 800, 20, 3340,
			    da9063_buck_b_limits,
			    DA9063_REG_BUCK_ILIM_B, DA9063_BPERI_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BPERI),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VBPERI_SEL),
	},
	{
		DA9063_BUCK(DA9063, BCORES_MERGED, 300, 10, 1570,
			    da9063_bcores_merged_limits,
			    DA9063_REG_BUCK_ILIM_C, DA9063_BCORE1_ILIM_MASK),
		/* BCORES_MERGED uses the same register fields as BCORE1 */
		DA9063_BUCK_COMMON_FIELDS(BCORE1),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VBCORE1_SEL),
	},
	{
		DA9063_BUCK(DA9063, BMEM_BIO_MERGED, 800, 20, 3340,
			    da9063_bmem_bio_merged_limits,
			    DA9063_REG_BUCK_ILIM_A, DA9063_BMEM_ILIM_MASK),
		/* BMEM_BIO_MERGED uses the same register fields as BMEM */
		DA9063_BUCK_COMMON_FIELDS(BMEM),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VBMEM_SEL),
	},
	{
		DA9063_LDO(DA9063, LDO3, 900, 20, 3440),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VLDO3_SEL),
		.oc_event = BFIELD(DA9063_REG_STATUS_D, DA9063_LDO3_LIM),
	},
	{
		DA9063_LDO(DA9063, LDO7, 900, 50, 3600),
		.suspend = BFIELD(DA9063_REG_LDO7_CONT, DA9063_VLDO7_SEL),
		.oc_event = BFIELD(DA9063_REG_STATUS_D, DA9063_LDO7_LIM),
	},
	{
		DA9063_LDO(DA9063, LDO8, 900, 50, 3600),
		.suspend = BFIELD(DA9063_REG_LDO8_CONT, DA9063_VLDO8_SEL),
		.oc_event = BFIELD(DA9063_REG_STATUS_D, DA9063_LDO8_LIM),
	},
	{
		DA9063_LDO(DA9063, LDO9, 950, 50, 3600),
		.suspend = BFIELD(DA9063_REG_LDO9_CONT, DA9063_VLDO9_SEL),
	},
	{
		DA9063_LDO(DA9063, LDO11, 900, 50, 3600),
		.suspend = BFIELD(DA9063_REG_LDO11_CONT, DA9063_VLDO11_SEL),
		.oc_event = BFIELD(DA9063_REG_STATUS_D, DA9063_LDO11_LIM),
	},

	/* The following LDOs are present only on DA9063, not on DA9063L */
	{
		DA9063_LDO(DA9063, LDO1, 600, 20, 1860),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VLDO1_SEL),
	},
	{
		DA9063_LDO(DA9063, LDO2, 600, 20, 1860),
		.suspend = BFIELD(DA9063_REG_DVC_1, DA9063_VLDO2_SEL),
	},
	{
		DA9063_LDO(DA9063, LDO4, 900, 20, 3440),
		.suspend = BFIELD(DA9063_REG_DVC_2, DA9063_VLDO4_SEL),
		.oc_event = BFIELD(DA9063_REG_STATUS_D, DA9063_LDO4_LIM),
	},
	{
		DA9063_LDO(DA9063, LDO5, 900, 50, 3600),
		.suspend = BFIELD(DA9063_REG_LDO5_CONT, DA9063_VLDO5_SEL),
	},
	{
		DA9063_LDO(DA9063, LDO6, 900, 50, 3600),
		.suspend = BFIELD(DA9063_REG_LDO6_CONT, DA9063_VLDO6_SEL),
	},

	{
		DA9063_LDO(DA9063, LDO10, 900, 50, 3600),
		.suspend = BFIELD(DA9063_REG_LDO10_CONT, DA9063_VLDO10_SEL),
	},
};

/* Link chip model with regulators info table */
static struct da9063_dev_model regulators_models[] = {
	{
		.regulator_info = da9063_regulator_info,
		.n_regulators = ARRAY_SIZE(da9063_regulator_info),
		.type = PMIC_TYPE_DA9063,
	},
	{
		.regulator_info = da9063_regulator_info,
		.n_regulators = ARRAY_SIZE(da9063_regulator_info) - 6,
		.type = PMIC_TYPE_DA9063L,
	},
	{ }
};

/* Regulator interrupt handlers */
static irqreturn_t da9063_ldo_lim_event(int irq, void *data)
{
	struct da9063_regulators *regulators = data;
	struct da9063 *hw = regulators->regulator[0].hw;
	struct da9063_regulator *regl;
	int bits, i, ret;

	ret = regmap_read(hw->regmap, DA9063_REG_STATUS_D, &bits);
	if (ret < 0)
		return IRQ_NONE;

	for (i = regulators->n_regulators - 1; i >= 0; i--) {
		regl = &regulators->regulator[i];
		if (regl->info->oc_event.reg != DA9063_REG_STATUS_D)
			continue;

		if (BIT(regl->info->oc_event.lsb) & bits) {
			regulator_lock(regl->rdev);
			regulator_notifier_call_chain(regl->rdev,
					REGULATOR_EVENT_OVER_CURRENT, NULL);
			regulator_unlock(regl->rdev);
		}
	}

	return IRQ_HANDLED;
}

/*
 * Probing and Initialisation functions
 */
static const struct regulator_init_data *da9063_get_regulator_initdata(
		const struct da9063_regulators_pdata *regl_pdata, int id)
{
	int i;

	for (i = 0; i < regl_pdata->n_regulators; i++) {
		if (id == regl_pdata->regulator_data[i].id)
			return regl_pdata->regulator_data[i].initdata;
	}

	return NULL;
}

static struct of_regulator_match da9063_matches[] = {
	[DA9063_ID_BCORE1]           = { .name = "bcore1"           },
	[DA9063_ID_BCORE2]           = { .name = "bcore2"           },
	[DA9063_ID_BPRO]             = { .name = "bpro",            },
	[DA9063_ID_BMEM]             = { .name = "bmem",            },
	[DA9063_ID_BIO]              = { .name = "bio",             },
	[DA9063_ID_BPERI]            = { .name = "bperi",           },
	[DA9063_ID_BCORES_MERGED]    = { .name = "bcores-merged"    },
	[DA9063_ID_BMEM_BIO_MERGED]  = { .name = "bmem-bio-merged", },
	[DA9063_ID_LDO3]             = { .name = "ldo3",            },
	[DA9063_ID_LDO7]             = { .name = "ldo7",            },
	[DA9063_ID_LDO8]             = { .name = "ldo8",            },
	[DA9063_ID_LDO9]             = { .name = "ldo9",            },
	[DA9063_ID_LDO11]            = { .name = "ldo11",           },
	/* The following LDOs are present only on DA9063, not on DA9063L */
	[DA9063_ID_LDO1]             = { .name = "ldo1",            },
	[DA9063_ID_LDO2]             = { .name = "ldo2",            },
	[DA9063_ID_LDO4]             = { .name = "ldo4",            },
	[DA9063_ID_LDO5]             = { .name = "ldo5",            },
	[DA9063_ID_LDO6]             = { .name = "ldo6",            },
	[DA9063_ID_LDO10]            = { .name = "ldo10",           },
};

static struct da9063_regulators_pdata *da9063_parse_regulators_dt(
		struct platform_device *pdev,
		struct of_regulator_match **da9063_reg_matches)
{
	struct da9063 *da9063 = dev_get_drvdata(pdev->dev.parent);
	struct da9063_regulators_pdata *pdata;
	struct da9063_regulator_data *rdata;
	struct device_node *node;
	int da9063_matches_len = ARRAY_SIZE(da9063_matches);
	int i, n, num;

	if (da9063->type == PMIC_TYPE_DA9063L)
		da9063_matches_len -= 6;

	node = of_get_child_by_name(pdev->dev.parent->of_node, "regulators");
	if (!node) {
		dev_err(&pdev->dev, "Regulators device node not found\n");
		return ERR_PTR(-ENODEV);
	}

	num = of_regulator_match(&pdev->dev, node, da9063_matches,
				 da9063_matches_len);
	of_node_put(node);
	if (num < 0) {
		dev_err(&pdev->dev, "Failed to match regulators\n");
		return ERR_PTR(-EINVAL);
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->regulator_data = devm_kcalloc(&pdev->dev,
					num, sizeof(*pdata->regulator_data),
					GFP_KERNEL);
	if (!pdata->regulator_data)
		return ERR_PTR(-ENOMEM);
	pdata->n_regulators = num;

	n = 0;
	for (i = 0; i < da9063_matches_len; i++) {
		if (!da9063_matches[i].init_data)
			continue;

		rdata = &pdata->regulator_data[n];
		rdata->id = i;
		rdata->initdata = da9063_matches[i].init_data;

		n++;
	}

	*da9063_reg_matches = da9063_matches;
	return pdata;
}

static int da9063_regulator_probe(struct platform_device *pdev)
{
	struct da9063 *da9063 = dev_get_drvdata(pdev->dev.parent);
	struct of_regulator_match *da9063_reg_matches = NULL;
	struct da9063_regulators_pdata *regl_pdata;
	const struct da9063_dev_model *model;
	struct da9063_regulators *regulators;
	struct da9063_regulator *regl;
	struct regulator_config config;
	bool bcores_merged, bmem_bio_merged;
	int id, irq, n, n_regulators, ret, val;

	regl_pdata = da9063_parse_regulators_dt(pdev, &da9063_reg_matches);

	if (IS_ERR(regl_pdata) || regl_pdata->n_regulators == 0) {
		dev_err(&pdev->dev,
			"No regulators defined for the platform\n");
		return -ENODEV;
	}

	/* Find regulators set for particular device model */
	for (model = regulators_models; model->regulator_info; model++) {
		if (model->type == da9063->type)
			break;
	}
	if (!model->regulator_info) {
		dev_err(&pdev->dev, "Chip model not recognised (%u)\n",
			da9063->type);
		return -ENODEV;
	}

	ret = regmap_read(da9063->regmap, DA9063_REG_CONFIG_H, &val);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Error while reading BUCKs configuration\n");
		return ret;
	}
	bcores_merged = val & DA9063_BCORE_MERGE;
	bmem_bio_merged = val & DA9063_BUCK_MERGE;

	n_regulators = model->n_regulators;
	if (bcores_merged)
		n_regulators -= 2; /* remove BCORE1, BCORE2 */
	else
		n_regulators--;    /* remove BCORES_MERGED */
	if (bmem_bio_merged)
		n_regulators -= 2; /* remove BMEM, BIO */
	else
		n_regulators--;    /* remove BMEM_BIO_MERGED */

	/* Allocate memory required by usable regulators */
	regulators = devm_kzalloc(&pdev->dev, struct_size(regulators,
				  regulator, n_regulators), GFP_KERNEL);
	if (!regulators)
		return -ENOMEM;

	regulators->n_regulators = n_regulators;
	platform_set_drvdata(pdev, regulators);

	/* Register all regulators declared in platform information */
	n = 0;
	id = 0;
	while (n < regulators->n_regulators) {
		/* Skip regulator IDs depending on merge mode configuration */
		switch (id) {
		case DA9063_ID_BCORE1:
		case DA9063_ID_BCORE2:
			if (bcores_merged) {
				id++;
				continue;
			}
			break;
		case DA9063_ID_BMEM:
		case DA9063_ID_BIO:
			if (bmem_bio_merged) {
				id++;
				continue;
			}
			break;
		case DA9063_ID_BCORES_MERGED:
			if (!bcores_merged) {
				id++;
				continue;
			}
			break;
		case DA9063_ID_BMEM_BIO_MERGED:
			if (!bmem_bio_merged) {
				id++;
				continue;
			}
			break;
		}

		/* Initialise regulator structure */
		regl = &regulators->regulator[n];
		regl->hw = da9063;
		regl->info = &model->regulator_info[id];
		regl->desc = regl->info->desc;
		regl->desc.type = REGULATOR_VOLTAGE;
		regl->desc.owner = THIS_MODULE;

		if (regl->info->mode.reg) {
			regl->mode = devm_regmap_field_alloc(&pdev->dev,
					da9063->regmap, regl->info->mode);
			if (IS_ERR(regl->mode))
				return PTR_ERR(regl->mode);
		}

		if (regl->info->suspend.reg) {
			regl->suspend = devm_regmap_field_alloc(&pdev->dev,
					da9063->regmap, regl->info->suspend);
			if (IS_ERR(regl->suspend))
				return PTR_ERR(regl->suspend);
		}

		if (regl->info->sleep.reg) {
			regl->sleep = devm_regmap_field_alloc(&pdev->dev,
					da9063->regmap, regl->info->sleep);
			if (IS_ERR(regl->sleep))
				return PTR_ERR(regl->sleep);
		}

		if (regl->info->suspend_sleep.reg) {
			regl->suspend_sleep = devm_regmap_field_alloc(&pdev->dev,
				da9063->regmap, regl->info->suspend_sleep);
			if (IS_ERR(regl->suspend_sleep))
				return PTR_ERR(regl->suspend_sleep);
		}

		/* Register regulator */
		memset(&config, 0, sizeof(config));
		config.dev = &pdev->dev;
		config.init_data = da9063_get_regulator_initdata(regl_pdata, id);
		config.driver_data = regl;
		if (da9063_reg_matches)
			config.of_node = da9063_reg_matches[id].of_node;
		config.regmap = da9063->regmap;
		regl->rdev = devm_regulator_register(&pdev->dev, &regl->desc,
						     &config);
		if (IS_ERR(regl->rdev)) {
			dev_err(&pdev->dev,
				"Failed to register %s regulator\n",
				regl->desc.name);
			return PTR_ERR(regl->rdev);
		}
		id++;
		n++;
	}

	/* LDOs overcurrent event support */
	irq = platform_get_irq_byname(pdev, "LDO_LIM");
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(&pdev->dev, irq,
				NULL, da9063_ldo_lim_event,
				IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				"LDO_LIM", regulators);
	if (ret)
		dev_err(&pdev->dev, "Failed to request LDO_LIM IRQ.\n");

	return ret;
}

static struct platform_driver da9063_regulator_driver = {
	.driver = {
		.name = DA9063_DRVNAME_REGULATORS,
	},
	.probe = da9063_regulator_probe,
};

static int __init da9063_regulator_init(void)
{
	return platform_driver_register(&da9063_regulator_driver);
}
subsys_initcall(da9063_regulator_init);

static void __exit da9063_regulator_cleanup(void)
{
	platform_driver_unregister(&da9063_regulator_driver);
}
module_exit(da9063_regulator_cleanup);


/* Module information */
MODULE_AUTHOR("Krystian Garbaciak <krystian.garbaciak@diasemi.com>");
MODULE_DESCRIPTION("DA9063 regulators driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DA9063_DRVNAME_REGULATORS);
