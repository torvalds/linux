// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright 2014 Embest Technology Co. Ltd. Inc.
// bd71815-regulator.c ROHM BD71815 regulator driver
//
// Author: Tony Luo <luofc@embedinfo.com>
//
// Partially rewritten at 2021 by
// Matti Vaittinen <matti.vaitinen@fi.rohmeurope.com>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/mfd/rohm-bd71815.h>
#include <linux/regulator/of_regulator.h>

struct bd71815_regulator {
	struct regulator_desc desc;
	const struct rohm_dvs_config *dvs;
};

static const int bd7181x_wled_currents[] = {
	10, 20, 30, 50, 70, 100, 200, 300, 500, 700, 1000, 2000, 3000, 4000,
	5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 13000, 14000, 15000,
	16000, 17000, 18000, 19000, 20000, 21000, 22000, 23000, 24000, 25000,
};

static const struct rohm_dvs_config buck1_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_BUCK1_VOLT_H,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= BD71815_BUCK_RUN_ON,
	.snvs_on_mask		= BD71815_BUCK_SNVS_ON,
	.suspend_reg		= BD71815_REG_BUCK1_VOLT_L,
	.suspend_mask		= BD71815_VOLT_MASK,
	.suspend_on_mask	= BD71815_BUCK_SUSP_ON,
	.lpsr_reg		= BD71815_REG_BUCK1_VOLT_L,
	.lpsr_mask		= BD71815_VOLT_MASK,
	.lpsr_on_mask		= BD71815_BUCK_LPSR_ON,
};

static const struct rohm_dvs_config buck2_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_BUCK2_VOLT_H,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= BD71815_BUCK_RUN_ON,
	.snvs_on_mask		= BD71815_BUCK_SNVS_ON,
	.suspend_reg		= BD71815_REG_BUCK2_VOLT_L,
	.suspend_mask		= BD71815_VOLT_MASK,
	.suspend_on_mask	= BD71815_BUCK_SUSP_ON,
	.lpsr_reg		= BD71815_REG_BUCK2_VOLT_L,
	.lpsr_mask		= BD71815_VOLT_MASK,
	.lpsr_on_mask		= BD71815_BUCK_LPSR_ON,
};

static const struct rohm_dvs_config buck3_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_BUCK3_VOLT,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= BD71815_BUCK_RUN_ON,
	.snvs_on_mask		= BD71815_BUCK_SNVS_ON,
	.suspend_on_mask	= BD71815_BUCK_SUSP_ON,
	.lpsr_on_mask		= BD71815_BUCK_LPSR_ON,
};

static const struct rohm_dvs_config buck4_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_BUCK4_VOLT,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= BD71815_BUCK_RUN_ON,
	.snvs_on_mask		= BD71815_BUCK_SNVS_ON,
	.suspend_on_mask	= BD71815_BUCK_SUSP_ON,
	.lpsr_on_mask		= BD71815_BUCK_LPSR_ON,
};

static const struct rohm_dvs_config ldo1_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_LDO_MODE1,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= LDO1_RUN_ON,
	.snvs_on_mask		= LDO1_SNVS_ON,
	.suspend_on_mask	= LDO1_SUSP_ON,
	.lpsr_on_mask		= LDO1_LPSR_ON,
};

static const struct rohm_dvs_config ldo2_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_LDO_MODE2,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= LDO2_RUN_ON,
	.snvs_on_mask		= LDO2_SNVS_ON,
	.suspend_on_mask	= LDO2_SUSP_ON,
	.lpsr_on_mask		= LDO2_LPSR_ON,
};

static const struct rohm_dvs_config ldo3_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_LDO_MODE2,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= LDO3_RUN_ON,
	.snvs_on_mask		= LDO3_SNVS_ON,
	.suspend_on_mask	= LDO3_SUSP_ON,
	.lpsr_on_mask		= LDO3_LPSR_ON,
};

static const struct rohm_dvs_config ldo4_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_LDO_MODE3,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= LDO4_RUN_ON,
	.snvs_on_mask		= LDO4_SNVS_ON,
	.suspend_on_mask	= LDO4_SUSP_ON,
	.lpsr_on_mask		= LDO4_LPSR_ON,
};

static const struct rohm_dvs_config ldo5_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_LDO_MODE3,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= LDO5_RUN_ON,
	.snvs_on_mask		= LDO5_SNVS_ON,
	.suspend_on_mask	= LDO5_SUSP_ON,
	.lpsr_on_mask		= LDO5_LPSR_ON,
};

static const struct rohm_dvs_config dvref_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_on_mask		= DVREF_RUN_ON,
	.snvs_on_mask		= DVREF_SNVS_ON,
	.suspend_on_mask	= DVREF_SUSP_ON,
	.lpsr_on_mask		= DVREF_LPSR_ON,
};

static const struct rohm_dvs_config ldolpsr_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_on_mask		= DVREF_RUN_ON,
	.snvs_on_mask		= DVREF_SNVS_ON,
	.suspend_on_mask	= DVREF_SUSP_ON,
	.lpsr_on_mask		= DVREF_LPSR_ON,
};

static const struct rohm_dvs_config buck5_dvs = {
	.level_map		= ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_SNVS |
				  ROHM_DVS_LEVEL_SUSPEND | ROHM_DVS_LEVEL_LPSR,
	.run_reg		= BD71815_REG_BUCK5_VOLT,
	.run_mask		= BD71815_VOLT_MASK,
	.run_on_mask		= BD71815_BUCK_RUN_ON,
	.snvs_on_mask		= BD71815_BUCK_SNVS_ON,
	.suspend_on_mask	= BD71815_BUCK_SUSP_ON,
	.lpsr_on_mask		= BD71815_BUCK_LPSR_ON,
};

static int set_hw_dvs_levels(struct device_node *np,
			     const struct regulator_desc *desc,
			     struct regulator_config *cfg)
{
	struct bd71815_regulator *data;

	data = container_of(desc, struct bd71815_regulator, desc);
	return rohm_regulator_set_dvs_levels(data->dvs, np, desc, cfg->regmap);
}

/*
 * Bucks 1 and 2 have two voltage selection registers where selected
 * voltage can be set. Which of the registers is used can be either controlled
 * by a control bit in register - or by HW state. If HW state specific voltages
 * are given - then we assume HW state based control should be used.
 *
 * If volatge value is updated to currently selected register - then output
 * voltage is immediately changed no matter what is set as ramp rate. Thus we
 * default changing voltage by writing new value to inactive register and
 * then updating the 'register selection' bit. This naturally only works when
 * HW state machine is not used to select the voltage.
 */
static int buck12_set_hw_dvs_levels(struct device_node *np,
				    const struct regulator_desc *desc,
				    struct regulator_config *cfg)
{
	struct bd71815_regulator *data;
	int ret = 0, val;

	data = container_of(desc, struct bd71815_regulator, desc);

	if (of_find_property(np, "rohm,dvs-run-voltage", NULL) ||
	    of_find_property(np, "rohm,dvs-suspend-voltage", NULL) ||
	    of_find_property(np, "rohm,dvs-lpsr-voltage", NULL) ||
	    of_find_property(np, "rohm,dvs-snvs-voltage", NULL)) {
		ret = regmap_read(cfg->regmap, desc->vsel_reg, &val);
		if (ret)
			return ret;

		if (!(BD71815_BUCK_STBY_DVS & val) &&
		    !(BD71815_BUCK_DVSSEL & val)) {
			int val2;

			/*
			 * We are currently using voltage from _L.
			 * We'd better copy it to _H and switch to it to
			 * avoid shutting us down if LPSR or SUSPEND is set to
			 * disabled. _L value is at reg _H + 1
			 */
			ret = regmap_read(cfg->regmap, desc->vsel_reg + 1,
					  &val2);
			if (ret)
				return ret;

			ret = regmap_update_bits(cfg->regmap, desc->vsel_reg,
						 BD71815_VOLT_MASK |
						 BD71815_BUCK_DVSSEL,
						 val2 | BD71815_BUCK_DVSSEL);
			if (ret)
				return ret;
		}
		ret = rohm_regulator_set_dvs_levels(data->dvs, np, desc,
						    cfg->regmap);
		if (ret)
			return ret;
		/*
		 * DVS levels were given => use HW-state machine for voltage
		 * controls. NOTE: AFAIK, This means that if voltage is changed
		 * by SW the ramp-rate is not respected. Should we disable
		 * SW voltage control when the HW state machine is used?
		 */
		ret = regmap_update_bits(cfg->regmap, desc->vsel_reg,
					 BD71815_BUCK_STBY_DVS,
					 BD71815_BUCK_STBY_DVS);
	}

	return ret;
}

/*
 * BUCK1/2
 * BUCK1RAMPRATE[1:0] BUCK1 DVS ramp rate setting
 * 00: 10.00mV/usec	10mV 1uS
 * 01: 5.00mV/usec	10mV 2uS
 * 10: 2.50mV/usec	10mV 4uS
 * 11: 1.25mV/usec	10mV 8uS
 */
static const unsigned int bd7181x_ramp_table[] = { 1250, 2500, 5000, 10000 };

static int bd7181x_led_set_current_limit(struct regulator_dev *rdev,
					int min_uA, int max_uA)
{
	int ret;
	int onstatus;

	onstatus = regulator_is_enabled_regmap(rdev);

	ret = regulator_set_current_limit_regmap(rdev, min_uA, max_uA);
	if (!ret) {
		int newstatus;

		newstatus = regulator_is_enabled_regmap(rdev);
		if (onstatus != newstatus) {
			/*
			 * HW FIX: spurious led status change detected. Toggle
			 * state as a workaround
			 */
			if (onstatus)
				ret = regulator_enable_regmap(rdev);
			else
				ret = regulator_disable_regmap(rdev);

			if (ret)
				dev_err(rdev_get_dev(rdev),
					"failed to revert the LED state (%d)\n",
					ret);
		}
	}

	return ret;
}

static int bd7181x_buck12_get_voltage_sel(struct regulator_dev *rdev)
{
	int rid = rdev_get_id(rdev);
	int ret, regh, regl, val;

	regh = BD71815_REG_BUCK1_VOLT_H + rid * 0x2;
	regl = BD71815_REG_BUCK1_VOLT_L + rid * 0x2;

	ret = regmap_read(rdev->regmap, regh, &val);
	if (ret)
		return ret;

	/*
	 * If we use HW state machine based voltage reg selection - then we
	 * return BD71815_REG_BUCK1_VOLT_H which is used at RUN.
	 * Else we do return the BD71815_REG_BUCK1_VOLT_H or
	 * BD71815_REG_BUCK1_VOLT_L depending on which is selected to be used
	 * by BD71815_BUCK_DVSSEL bit
	 */
	if ((!(val & BD71815_BUCK_STBY_DVS)) && (!(val & BD71815_BUCK_DVSSEL)))
		ret = regmap_read(rdev->regmap, regl, &val);

	if (ret)
		return ret;

	return val & BD71815_VOLT_MASK;
}

/*
 * For Buck 1/2.
 */
static int bd7181x_buck12_set_voltage_sel(struct regulator_dev *rdev,
					  unsigned int sel)
{
	int rid = rdev_get_id(rdev);
	int ret, val, reg, regh, regl;

	regh = BD71815_REG_BUCK1_VOLT_H + rid*0x2;
	regl = BD71815_REG_BUCK1_VOLT_L + rid*0x2;

	ret = regmap_read(rdev->regmap, regh, &val);
	if (ret)
		return ret;

	/*
	 * If bucks 1 & 2 are controlled by state machine - then the RUN state
	 * voltage is set to BD71815_REG_BUCK1_VOLT_H. Changing SUSPEND/LPSR
	 * voltages at runtime is not supported by this driver.
	 */
	if (((val & BD71815_BUCK_STBY_DVS))) {
		return regmap_update_bits(rdev->regmap, regh, BD71815_VOLT_MASK,
					  sel);
	}
	/* Update new voltage to the register which is not selected now */
	if (val & BD71815_BUCK_DVSSEL)
		reg = regl;
	else
		reg = regh;

	ret = regmap_update_bits(rdev->regmap, reg, BD71815_VOLT_MASK, sel);
	if (ret)
		return ret;

	/* Select the other DVS register to be used */
	return regmap_update_bits(rdev->regmap, regh, BD71815_BUCK_DVSSEL,
				  ~val);
}

static const struct regulator_ops bd7181x_ldo_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops bd7181x_fixed_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

static const struct regulator_ops bd7181x_buck_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static const struct regulator_ops bd7181x_buck12_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = bd7181x_buck12_set_voltage_sel,
	.get_voltage_sel = bd7181x_buck12_get_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
};

static const struct regulator_ops bd7181x_led_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_current_limit = bd7181x_led_set_current_limit,
	.get_current_limit = regulator_get_current_limit_regmap,
};

#define BD71815_FIXED_REG(_name, _id, ereg, emsk, voltage, _dvs)	\
	[(_id)] = {							\
		.desc = {						\
			.name = #_name,					\
			.of_match = of_match_ptr(#_name),		\
			.regulators_node = of_match_ptr("regulators"),	\
			.n_voltages = 1,				\
			.ops = &bd7181x_fixed_regulator_ops,		\
			.type = REGULATOR_VOLTAGE,			\
			.id = (_id),					\
			.owner = THIS_MODULE,				\
			.min_uV = (voltage),				\
			.enable_reg = (ereg),				\
			.enable_mask = (emsk),				\
			.of_parse_cb = set_hw_dvs_levels,		\
		},							\
		.dvs = (_dvs),						\
	}

#define BD71815_BUCK_REG(_name, _id, vsel, ereg, min, max, step, _dvs)	\
	[(_id)] = {							\
		.desc = {						\
			.name = #_name,					\
			.of_match = of_match_ptr(#_name),		\
			.regulators_node = of_match_ptr("regulators"),	\
			.n_voltages = ((max) - (min)) / (step) + 1,	\
			.ops = &bd7181x_buck_regulator_ops,		\
			.type = REGULATOR_VOLTAGE,			\
			.id = (_id),					\
			.owner = THIS_MODULE,				\
			.min_uV = (min),				\
			.uV_step = (step),				\
			.vsel_reg = (vsel),				\
			.vsel_mask = BD71815_VOLT_MASK,			\
			.enable_reg = (ereg),				\
			.enable_mask = BD71815_BUCK_RUN_ON,		\
			.of_parse_cb = set_hw_dvs_levels,		\
		},							\
		.dvs = (_dvs),						\
	}

#define BD71815_BUCK12_REG(_name, _id, vsel, ereg, min, max, step,	\
			   _dvs)					\
	[(_id)] = {							\
		.desc = {						\
			.name = #_name,					\
			.of_match = of_match_ptr(#_name),		\
			.regulators_node = of_match_ptr("regulators"),	\
			.n_voltages = ((max) - (min)) / (step) + 1,	\
			.ops = &bd7181x_buck12_regulator_ops,		\
			.type = REGULATOR_VOLTAGE,			\
			.id = (_id),					\
			.owner = THIS_MODULE,				\
			.min_uV = (min),				\
			.uV_step = (step),				\
			.vsel_reg = (vsel),				\
			.vsel_mask = BD71815_VOLT_MASK,			\
			.enable_reg = (ereg),				\
			.enable_mask = BD71815_BUCK_RUN_ON,		\
			.ramp_reg = (ereg),				\
			.ramp_mask = BD71815_BUCK_RAMPRATE_MASK,	\
			.ramp_delay_table = bd7181x_ramp_table,		\
			.n_ramp_values = ARRAY_SIZE(bd7181x_ramp_table),\
			.of_parse_cb = buck12_set_hw_dvs_levels,	\
		},							\
		.dvs = (_dvs),						\
	}

#define BD71815_LED_REG(_name, _id, csel, mask, ereg, emsk, currents)	\
	[(_id)] = {							\
		.desc = {						\
			.name = #_name,					\
			.of_match = of_match_ptr(#_name),		\
			.regulators_node = of_match_ptr("regulators"),	\
			.n_current_limits = ARRAY_SIZE(currents),	\
			.ops = &bd7181x_led_regulator_ops,		\
			.type = REGULATOR_CURRENT,			\
			.id = (_id),					\
			.owner = THIS_MODULE,				\
			.curr_table = currents,				\
			.csel_reg = (csel),				\
			.csel_mask = (mask),				\
			.enable_reg = (ereg),				\
			.enable_mask = (emsk),				\
		},							\
	}

#define BD71815_LDO_REG(_name, _id, vsel, ereg, emsk, min, max, step,	\
			_dvs)						\
	[(_id)] = {							\
		.desc = {						\
			.name = #_name,					\
			.of_match = of_match_ptr(#_name),		\
			.regulators_node = of_match_ptr("regulators"),	\
			.n_voltages = ((max) - (min)) / (step) + 1,	\
			.ops = &bd7181x_ldo_regulator_ops,		\
			.type = REGULATOR_VOLTAGE,			\
			.id = (_id),					\
			.owner = THIS_MODULE,				\
			.min_uV = (min),				\
			.uV_step = (step),				\
			.vsel_reg = (vsel),				\
			.vsel_mask = BD71815_VOLT_MASK,			\
			.enable_reg = (ereg),				\
			.enable_mask = (emsk),				\
			.of_parse_cb = set_hw_dvs_levels,		\
		},							\
		.dvs = (_dvs),						\
	}

static const struct bd71815_regulator bd71815_regulators[] = {
	BD71815_BUCK12_REG(buck1, BD71815_BUCK1, BD71815_REG_BUCK1_VOLT_H,
			   BD71815_REG_BUCK1_MODE, 800000, 2000000, 25000,
			   &buck1_dvs),
	BD71815_BUCK12_REG(buck2, BD71815_BUCK2, BD71815_REG_BUCK2_VOLT_H,
			   BD71815_REG_BUCK2_MODE, 800000, 2000000, 25000,
			   &buck2_dvs),
	BD71815_BUCK_REG(buck3, BD71815_BUCK3, BD71815_REG_BUCK3_VOLT,
			 BD71815_REG_BUCK3_MODE,  1200000, 2700000, 50000,
			 &buck3_dvs),
	BD71815_BUCK_REG(buck4, BD71815_BUCK4, BD71815_REG_BUCK4_VOLT,
			 BD71815_REG_BUCK4_MODE,  1100000, 1850000, 25000,
			 &buck4_dvs),
	BD71815_BUCK_REG(buck5, BD71815_BUCK5, BD71815_REG_BUCK5_VOLT,
			 BD71815_REG_BUCK5_MODE,  1800000, 3300000, 50000,
			 &buck5_dvs),
	BD71815_LDO_REG(ldo1, BD71815_LDO1, BD71815_REG_LDO1_VOLT,
			BD71815_REG_LDO_MODE1, LDO1_RUN_ON, 800000, 3300000,
			50000, &ldo1_dvs),
	BD71815_LDO_REG(ldo2, BD71815_LDO2, BD71815_REG_LDO2_VOLT,
			BD71815_REG_LDO_MODE2, LDO2_RUN_ON, 800000, 3300000,
			50000, &ldo2_dvs),
	/*
	 * Let's default LDO3 to be enabled by SW. We can override ops if DT
	 * says LDO3 should be enabled by HW when DCIN is connected.
	 */
	BD71815_LDO_REG(ldo3, BD71815_LDO3, BD71815_REG_LDO3_VOLT,
			BD71815_REG_LDO_MODE2, LDO3_RUN_ON, 800000, 3300000,
			50000, &ldo3_dvs),
	BD71815_LDO_REG(ldo4, BD71815_LDO4, BD71815_REG_LDO4_VOLT,
			BD71815_REG_LDO_MODE3, LDO4_RUN_ON, 800000, 3300000,
			50000, &ldo4_dvs),
	BD71815_LDO_REG(ldo5, BD71815_LDO5, BD71815_REG_LDO5_VOLT_H,
			BD71815_REG_LDO_MODE3, LDO5_RUN_ON, 800000, 3300000,
			50000, &ldo5_dvs),
	BD71815_FIXED_REG(ldodvref, BD71815_LDODVREF, BD71815_REG_LDO_MODE4,
			  DVREF_RUN_ON, 3000000, &dvref_dvs),
	BD71815_FIXED_REG(ldolpsr, BD71815_LDOLPSR, BD71815_REG_LDO_MODE4,
			  LDO_LPSR_RUN_ON, 1800000, &ldolpsr_dvs),
	BD71815_LED_REG(wled, BD71815_WLED, BD71815_REG_LED_DIMM, LED_DIMM_MASK,
			BD71815_REG_LED_CTRL, LED_RUN_ON,
			bd7181x_wled_currents),
};

static int bd7181x_probe(struct platform_device *pdev)
{
	struct regulator_config config = {};
	int i, ret;
	struct gpio_desc *ldo4_en;
	struct regmap *regmap;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		dev_err(&pdev->dev, "No parent regmap\n");
		return -ENODEV;
	}

	ldo4_en = devm_fwnode_gpiod_get(&pdev->dev,
					dev_fwnode(pdev->dev.parent),
					"rohm,vsel", GPIOD_ASIS, "ldo4-en");
	if (IS_ERR(ldo4_en)) {
		ret = PTR_ERR(ldo4_en);
		if (ret != -ENOENT)
			return ret;
		ldo4_en = NULL;
	}

	/* Disable to go to ship-mode */
	ret = regmap_update_bits(regmap, BD71815_REG_PWRCTRL, RESTARTEN, 0);
	if (ret)
		return ret;

	config.dev = pdev->dev.parent;
	config.regmap = regmap;

	for (i = 0; i < BD71815_REGULATOR_CNT; i++) {
		const struct regulator_desc *desc;
		struct regulator_dev *rdev;

		desc = &bd71815_regulators[i].desc;

		if (i == BD71815_LDO4)
			config.ena_gpiod = ldo4_en;
		else
			config.ena_gpiod = NULL;

		rdev = devm_regulator_register(&pdev->dev, desc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(&pdev->dev, PTR_ERR(rdev),
					     "failed to register %s regulator\n",
					     desc->name);
	}
	return 0;
}

static const struct platform_device_id bd7181x_pmic_id[] = {
	{ "bd71815-pmic", ROHM_CHIP_TYPE_BD71815 },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd7181x_pmic_id);

static struct platform_driver bd7181x_regulator = {
	.driver = {
		.name = "bd7181x-pmic",
	},
	.probe = bd7181x_probe,
	.id_table = bd7181x_pmic_id,
};
module_platform_driver(bd7181x_regulator);

MODULE_AUTHOR("Tony Luo <luofc@embedinfo.com>");
MODULE_DESCRIPTION("BD71815 voltage regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bd7181x-pmic");
