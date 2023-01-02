// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019 ROHM Semiconductors
// bd71828-regulator.c ROHM BD71828GW-DS1 regulator driver
//

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/rohm-bd71828.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

struct reg_init {
	unsigned int reg;
	unsigned int mask;
	unsigned int val;
};
struct bd71828_regulator_data {
	struct regulator_desc desc;
	const struct rohm_dvs_config dvs;
	const struct reg_init *reg_inits;
	int reg_init_amnt;
};

static const struct reg_init buck1_inits[] = {
	/*
	 * DVS Buck voltages can be changed by register values or via GPIO.
	 * Use register accesses by default.
	 */
	{
		.reg = BD71828_REG_PS_CTRL_1,
		.mask = BD71828_MASK_DVS_BUCK1_CTRL,
		.val = BD71828_DVS_BUCK1_CTRL_I2C,
	},
};

static const struct reg_init buck2_inits[] = {
	{
		.reg = BD71828_REG_PS_CTRL_1,
		.mask = BD71828_MASK_DVS_BUCK2_CTRL,
		.val = BD71828_DVS_BUCK2_CTRL_I2C,
	},
};

static const struct reg_init buck6_inits[] = {
	{
		.reg = BD71828_REG_PS_CTRL_1,
		.mask = BD71828_MASK_DVS_BUCK6_CTRL,
		.val = BD71828_DVS_BUCK6_CTRL_I2C,
	},
};

static const struct reg_init buck7_inits[] = {
	{
		.reg = BD71828_REG_PS_CTRL_1,
		.mask = BD71828_MASK_DVS_BUCK7_CTRL,
		.val = BD71828_DVS_BUCK7_CTRL_I2C,
	},
};

static const struct linear_range bd71828_buck1267_volts[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x00, 0xef, 6250),
	REGULATOR_LINEAR_RANGE(2000000, 0xf0, 0xff, 0),
};

static const struct linear_range bd71828_buck3_volts[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0x00, 0x0f, 50000),
	REGULATOR_LINEAR_RANGE(2000000, 0x10, 0x1f, 0),
};

static const struct linear_range bd71828_buck4_volts[] = {
	REGULATOR_LINEAR_RANGE(1000000, 0x00, 0x1f, 25000),
	REGULATOR_LINEAR_RANGE(1800000, 0x20, 0x3f, 0),
};

static const struct linear_range bd71828_buck5_volts[] = {
	REGULATOR_LINEAR_RANGE(2500000, 0x00, 0x0f, 50000),
	REGULATOR_LINEAR_RANGE(3300000, 0x10, 0x1f, 0),
};

static const struct linear_range bd71828_ldo_volts[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x00, 0x31, 50000),
	REGULATOR_LINEAR_RANGE(3300000, 0x32, 0x3f, 0),
};

static const unsigned int bd71828_ramp_delay[] = { 2500, 5000, 10000, 20000 };

static int buck_set_hw_dvs_levels(struct device_node *np,
				  const struct regulator_desc *desc,
				  struct regulator_config *cfg)
{
	struct bd71828_regulator_data *data;

	data = container_of(desc, struct bd71828_regulator_data, desc);

	return rohm_regulator_set_dvs_levels(&data->dvs, np, desc, cfg->regmap);
}

static int ldo6_parse_dt(struct device_node *np,
			 const struct regulator_desc *desc,
			 struct regulator_config *cfg)
{
	int ret, i;
	uint32_t uv = 0;
	unsigned int en;
	struct regmap *regmap = cfg->regmap;
	static const char * const props[] = { "rohm,dvs-run-voltage",
					      "rohm,dvs-idle-voltage",
					      "rohm,dvs-suspend-voltage",
					      "rohm,dvs-lpsr-voltage" };
	unsigned int mask[] = { BD71828_MASK_RUN_EN, BD71828_MASK_IDLE_EN,
			       BD71828_MASK_SUSP_EN, BD71828_MASK_LPSR_EN };

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = of_property_read_u32(np, props[i], &uv);
		if (ret) {
			if (ret != -EINVAL)
				return ret;
			continue;
		}
		if (uv)
			en = 0xffffffff;
		else
			en = 0;

		ret = regmap_update_bits(regmap, desc->enable_reg, mask[i], en);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct regulator_ops bd71828_buck_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops bd71828_dvs_buck_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
};

static const struct regulator_ops bd71828_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops bd71828_ldo6_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct bd71828_regulator_data bd71828_rdata[] = {
	{
		.desc = {
			.name = "buck1",
			.of_match = of_match_ptr("BUCK1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_BUCK1,
			.ops = &bd71828_dvs_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_buck1267_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_buck1267_volts),
			.n_voltages = BD71828_BUCK1267_VOLTS,
			.enable_reg = BD71828_REG_BUCK1_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_BUCK1_VOLT,
			.vsel_mask = BD71828_MASK_BUCK1267_VOLT,
			.ramp_delay_table = bd71828_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd71828_ramp_delay),
			.ramp_reg = BD71828_REG_BUCK1_MODE,
			.ramp_mask = BD71828_MASK_RAMP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_BUCK1_VOLT,
			.run_mask = BD71828_MASK_BUCK1267_VOLT,
			.idle_reg = BD71828_REG_BUCK1_IDLE_VOLT,
			.idle_mask = BD71828_MASK_BUCK1267_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_reg = BD71828_REG_BUCK1_SUSP_VOLT,
			.suspend_mask = BD71828_MASK_BUCK1267_VOLT,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
			/*
			 * LPSR voltage is same as SUSPEND voltage. Allow
			 * setting it so that regulator can be set enabled at
			 * LPSR state
			 */
			.lpsr_reg = BD71828_REG_BUCK1_SUSP_VOLT,
			.lpsr_mask = BD71828_MASK_BUCK1267_VOLT,
		},
		.reg_inits = buck1_inits,
		.reg_init_amnt = ARRAY_SIZE(buck1_inits),
	},
	{
		.desc = {
			.name = "buck2",
			.of_match = of_match_ptr("BUCK2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_BUCK2,
			.ops = &bd71828_dvs_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_buck1267_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_buck1267_volts),
			.n_voltages = BD71828_BUCK1267_VOLTS,
			.enable_reg = BD71828_REG_BUCK2_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_BUCK2_VOLT,
			.vsel_mask = BD71828_MASK_BUCK1267_VOLT,
			.ramp_delay_table = bd71828_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd71828_ramp_delay),
			.ramp_reg = BD71828_REG_BUCK2_MODE,
			.ramp_mask = BD71828_MASK_RAMP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_BUCK2_VOLT,
			.run_mask = BD71828_MASK_BUCK1267_VOLT,
			.idle_reg = BD71828_REG_BUCK2_IDLE_VOLT,
			.idle_mask = BD71828_MASK_BUCK1267_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_reg = BD71828_REG_BUCK2_SUSP_VOLT,
			.suspend_mask = BD71828_MASK_BUCK1267_VOLT,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
			.lpsr_reg = BD71828_REG_BUCK2_SUSP_VOLT,
			.lpsr_mask = BD71828_MASK_BUCK1267_VOLT,
		},
		.reg_inits = buck2_inits,
		.reg_init_amnt = ARRAY_SIZE(buck2_inits),
	},
	{
		.desc = {
			.name = "buck3",
			.of_match = of_match_ptr("BUCK3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_BUCK3,
			.ops = &bd71828_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_buck3_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_buck3_volts),
			.n_voltages = BD71828_BUCK3_VOLTS,
			.enable_reg = BD71828_REG_BUCK3_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_BUCK3_VOLT,
			.vsel_mask = BD71828_MASK_BUCK3_VOLT,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			/*
			 * BUCK3 only supports single voltage for all states.
			 * voltage can be individually enabled for each state
			 * though => allow setting all states to support
			 * enabling power rail on different states.
			 */
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_BUCK3_VOLT,
			.idle_reg = BD71828_REG_BUCK3_VOLT,
			.suspend_reg = BD71828_REG_BUCK3_VOLT,
			.lpsr_reg = BD71828_REG_BUCK3_VOLT,
			.run_mask = BD71828_MASK_BUCK3_VOLT,
			.idle_mask = BD71828_MASK_BUCK3_VOLT,
			.suspend_mask = BD71828_MASK_BUCK3_VOLT,
			.lpsr_mask = BD71828_MASK_BUCK3_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},
	},
	{
		.desc = {
			.name = "buck4",
			.of_match = of_match_ptr("BUCK4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_BUCK4,
			.ops = &bd71828_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_buck4_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_buck4_volts),
			.n_voltages = BD71828_BUCK4_VOLTS,
			.enable_reg = BD71828_REG_BUCK4_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_BUCK4_VOLT,
			.vsel_mask = BD71828_MASK_BUCK4_VOLT,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			/*
			 * BUCK4 only supports single voltage for all states.
			 * voltage can be individually enabled for each state
			 * though => allow setting all states to support
			 * enabling power rail on different states.
			 */
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_BUCK4_VOLT,
			.idle_reg = BD71828_REG_BUCK4_VOLT,
			.suspend_reg = BD71828_REG_BUCK4_VOLT,
			.lpsr_reg = BD71828_REG_BUCK4_VOLT,
			.run_mask = BD71828_MASK_BUCK4_VOLT,
			.idle_mask = BD71828_MASK_BUCK4_VOLT,
			.suspend_mask = BD71828_MASK_BUCK4_VOLT,
			.lpsr_mask = BD71828_MASK_BUCK4_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},
	},
	{
		.desc = {
			.name = "buck5",
			.of_match = of_match_ptr("BUCK5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_BUCK5,
			.ops = &bd71828_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_buck5_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_buck5_volts),
			.n_voltages = BD71828_BUCK5_VOLTS,
			.enable_reg = BD71828_REG_BUCK5_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_BUCK5_VOLT,
			.vsel_mask = BD71828_MASK_BUCK5_VOLT,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			/*
			 * BUCK5 only supports single voltage for all states.
			 * voltage can be individually enabled for each state
			 * though => allow setting all states to support
			 * enabling power rail on different states.
			 */
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_BUCK5_VOLT,
			.idle_reg = BD71828_REG_BUCK5_VOLT,
			.suspend_reg = BD71828_REG_BUCK5_VOLT,
			.lpsr_reg = BD71828_REG_BUCK5_VOLT,
			.run_mask = BD71828_MASK_BUCK5_VOLT,
			.idle_mask = BD71828_MASK_BUCK5_VOLT,
			.suspend_mask = BD71828_MASK_BUCK5_VOLT,
			.lpsr_mask = BD71828_MASK_BUCK5_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},
	},
	{
		.desc = {
			.name = "buck6",
			.of_match = of_match_ptr("BUCK6"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_BUCK6,
			.ops = &bd71828_dvs_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_buck1267_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_buck1267_volts),
			.n_voltages = BD71828_BUCK1267_VOLTS,
			.enable_reg = BD71828_REG_BUCK6_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_BUCK6_VOLT,
			.vsel_mask = BD71828_MASK_BUCK1267_VOLT,
			.ramp_delay_table = bd71828_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd71828_ramp_delay),
			.ramp_reg = BD71828_REG_BUCK6_MODE,
			.ramp_mask = BD71828_MASK_RAMP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_BUCK6_VOLT,
			.run_mask = BD71828_MASK_BUCK1267_VOLT,
			.idle_reg = BD71828_REG_BUCK6_IDLE_VOLT,
			.idle_mask = BD71828_MASK_BUCK1267_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_reg = BD71828_REG_BUCK6_SUSP_VOLT,
			.suspend_mask = BD71828_MASK_BUCK1267_VOLT,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
			.lpsr_reg = BD71828_REG_BUCK6_SUSP_VOLT,
			.lpsr_mask = BD71828_MASK_BUCK1267_VOLT,
		},
		.reg_inits = buck6_inits,
		.reg_init_amnt = ARRAY_SIZE(buck6_inits),
	},
	{
		.desc = {
			.name = "buck7",
			.of_match = of_match_ptr("BUCK7"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_BUCK7,
			.ops = &bd71828_dvs_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_buck1267_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_buck1267_volts),
			.n_voltages = BD71828_BUCK1267_VOLTS,
			.enable_reg = BD71828_REG_BUCK7_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_BUCK7_VOLT,
			.vsel_mask = BD71828_MASK_BUCK1267_VOLT,
			.ramp_delay_table = bd71828_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd71828_ramp_delay),
			.ramp_reg = BD71828_REG_BUCK7_MODE,
			.ramp_mask = BD71828_MASK_RAMP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_BUCK7_VOLT,
			.run_mask = BD71828_MASK_BUCK1267_VOLT,
			.idle_reg = BD71828_REG_BUCK7_IDLE_VOLT,
			.idle_mask = BD71828_MASK_BUCK1267_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_reg = BD71828_REG_BUCK7_SUSP_VOLT,
			.suspend_mask = BD71828_MASK_BUCK1267_VOLT,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
			.lpsr_reg = BD71828_REG_BUCK7_SUSP_VOLT,
			.lpsr_mask = BD71828_MASK_BUCK1267_VOLT,
		},
		.reg_inits = buck7_inits,
		.reg_init_amnt = ARRAY_SIZE(buck7_inits),
	},
	{
		.desc = {
			.name = "ldo1",
			.of_match = of_match_ptr("LDO1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_LDO1,
			.ops = &bd71828_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_ldo_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_ldo_volts),
			.n_voltages = BD71828_LDO_VOLTS,
			.enable_reg = BD71828_REG_LDO1_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_LDO1_VOLT,
			.vsel_mask = BD71828_MASK_LDO_VOLT,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			/*
			 * LDO1 only supports single voltage for all states.
			 * voltage can be individually enabled for each state
			 * though => allow setting all states to support
			 * enabling power rail on different states.
			 */
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_LDO1_VOLT,
			.idle_reg = BD71828_REG_LDO1_VOLT,
			.suspend_reg = BD71828_REG_LDO1_VOLT,
			.lpsr_reg = BD71828_REG_LDO1_VOLT,
			.run_mask = BD71828_MASK_LDO_VOLT,
			.idle_mask = BD71828_MASK_LDO_VOLT,
			.suspend_mask = BD71828_MASK_LDO_VOLT,
			.lpsr_mask = BD71828_MASK_LDO_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},
	}, {
		.desc = {
			.name = "ldo2",
			.of_match = of_match_ptr("LDO2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_LDO2,
			.ops = &bd71828_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_ldo_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_ldo_volts),
			.n_voltages = BD71828_LDO_VOLTS,
			.enable_reg = BD71828_REG_LDO2_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_LDO2_VOLT,
			.vsel_mask = BD71828_MASK_LDO_VOLT,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			/*
			 * LDO2 only supports single voltage for all states.
			 * voltage can be individually enabled for each state
			 * though => allow setting all states to support
			 * enabling power rail on different states.
			 */
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_LDO2_VOLT,
			.idle_reg = BD71828_REG_LDO2_VOLT,
			.suspend_reg = BD71828_REG_LDO2_VOLT,
			.lpsr_reg = BD71828_REG_LDO2_VOLT,
			.run_mask = BD71828_MASK_LDO_VOLT,
			.idle_mask = BD71828_MASK_LDO_VOLT,
			.suspend_mask = BD71828_MASK_LDO_VOLT,
			.lpsr_mask = BD71828_MASK_LDO_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},
	}, {
		.desc = {
			.name = "ldo3",
			.of_match = of_match_ptr("LDO3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_LDO3,
			.ops = &bd71828_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_ldo_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_ldo_volts),
			.n_voltages = BD71828_LDO_VOLTS,
			.enable_reg = BD71828_REG_LDO3_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_LDO3_VOLT,
			.vsel_mask = BD71828_MASK_LDO_VOLT,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			/*
			 * LDO3 only supports single voltage for all states.
			 * voltage can be individually enabled for each state
			 * though => allow setting all states to support
			 * enabling power rail on different states.
			 */
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_LDO3_VOLT,
			.idle_reg = BD71828_REG_LDO3_VOLT,
			.suspend_reg = BD71828_REG_LDO3_VOLT,
			.lpsr_reg = BD71828_REG_LDO3_VOLT,
			.run_mask = BD71828_MASK_LDO_VOLT,
			.idle_mask = BD71828_MASK_LDO_VOLT,
			.suspend_mask = BD71828_MASK_LDO_VOLT,
			.lpsr_mask = BD71828_MASK_LDO_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},

	}, {
		.desc = {
			.name = "ldo4",
			.of_match = of_match_ptr("LDO4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_LDO4,
			.ops = &bd71828_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_ldo_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_ldo_volts),
			.n_voltages = BD71828_LDO_VOLTS,
			.enable_reg = BD71828_REG_LDO4_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_LDO4_VOLT,
			.vsel_mask = BD71828_MASK_LDO_VOLT,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			/*
			 * LDO1 only supports single voltage for all states.
			 * voltage can be individually enabled for each state
			 * though => allow setting all states to support
			 * enabling power rail on different states.
			 */
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_LDO4_VOLT,
			.idle_reg = BD71828_REG_LDO4_VOLT,
			.suspend_reg = BD71828_REG_LDO4_VOLT,
			.lpsr_reg = BD71828_REG_LDO4_VOLT,
			.run_mask = BD71828_MASK_LDO_VOLT,
			.idle_mask = BD71828_MASK_LDO_VOLT,
			.suspend_mask = BD71828_MASK_LDO_VOLT,
			.lpsr_mask = BD71828_MASK_LDO_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},
	}, {
		.desc = {
			.name = "ldo5",
			.of_match = of_match_ptr("LDO5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_LDO5,
			.ops = &bd71828_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_ldo_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_ldo_volts),
			.n_voltages = BD71828_LDO_VOLTS,
			.enable_reg = BD71828_REG_LDO5_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_LDO5_VOLT,
			.vsel_mask = BD71828_MASK_LDO_VOLT,
			.of_parse_cb = buck_set_hw_dvs_levels,
			.owner = THIS_MODULE,
		},
		/*
		 * LDO5 is special. It can choose vsel settings to be configured
		 * from 2 different registers (by GPIO).
		 *
		 * This driver supports only configuration where
		 * BD71828_REG_LDO5_VOLT_L is used.
		 */
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_LDO5_VOLT,
			.idle_reg = BD71828_REG_LDO5_VOLT,
			.suspend_reg = BD71828_REG_LDO5_VOLT,
			.lpsr_reg = BD71828_REG_LDO5_VOLT,
			.run_mask = BD71828_MASK_LDO_VOLT,
			.idle_mask = BD71828_MASK_LDO_VOLT,
			.suspend_mask = BD71828_MASK_LDO_VOLT,
			.lpsr_mask = BD71828_MASK_LDO_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},

	}, {
		.desc = {
			.name = "ldo6",
			.of_match = of_match_ptr("LDO6"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_LDO6,
			.ops = &bd71828_ldo6_ops,
			.type = REGULATOR_VOLTAGE,
			.fixed_uV = BD71828_LDO_6_VOLTAGE,
			.n_voltages = 1,
			.enable_reg = BD71828_REG_LDO6_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.owner = THIS_MODULE,
			/*
			 * LDO6 only supports enable/disable for all states.
			 * Voltage for LDO6 is fixed.
			 */
			.of_parse_cb = ldo6_parse_dt,
		},
	}, {
		.desc = {
			/* SNVS LDO in data-sheet */
			.name = "ldo7",
			.of_match = of_match_ptr("LDO7"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD71828_LDO_SNVS,
			.ops = &bd71828_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd71828_ldo_volts,
			.n_linear_ranges = ARRAY_SIZE(bd71828_ldo_volts),
			.n_voltages = BD71828_LDO_VOLTS,
			.enable_reg = BD71828_REG_LDO7_EN,
			.enable_mask = BD71828_MASK_RUN_EN,
			.vsel_reg = BD71828_REG_LDO7_VOLT,
			.vsel_mask = BD71828_MASK_LDO_VOLT,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			/*
			 * LDO7 only supports single voltage for all states.
			 * voltage can be individually enabled for each state
			 * though => allow setting all states to support
			 * enabling power rail on different states.
			 */
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD71828_REG_LDO7_VOLT,
			.idle_reg = BD71828_REG_LDO7_VOLT,
			.suspend_reg = BD71828_REG_LDO7_VOLT,
			.lpsr_reg = BD71828_REG_LDO7_VOLT,
			.run_mask = BD71828_MASK_LDO_VOLT,
			.idle_mask = BD71828_MASK_LDO_VOLT,
			.suspend_mask = BD71828_MASK_LDO_VOLT,
			.lpsr_mask = BD71828_MASK_LDO_VOLT,
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},

	},
};

static int bd71828_probe(struct platform_device *pdev)
{
	int i, j, ret;
	struct regulator_config config = {
		.dev = pdev->dev.parent,
	};

	config.regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!config.regmap)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(bd71828_rdata); i++) {
		struct regulator_dev *rdev;
		const struct bd71828_regulator_data *rd;

		rd = &bd71828_rdata[i];
		rdev = devm_regulator_register(&pdev->dev,
					       &rd->desc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(&pdev->dev, PTR_ERR(rdev),
					     "failed to register %s regulator\n",
					     rd->desc.name);

		for (j = 0; j < rd->reg_init_amnt; j++) {
			ret = regmap_update_bits(config.regmap,
						 rd->reg_inits[j].reg,
						 rd->reg_inits[j].mask,
						 rd->reg_inits[j].val);
			if (ret)
				return dev_err_probe(&pdev->dev, ret,
						     "regulator %s init failed\n",
						     rd->desc.name);
		}
	}
	return 0;
}

static struct platform_driver bd71828_regulator = {
	.driver = {
		.name = "bd71828-pmic"
	},
	.probe = bd71828_probe,
};

module_platform_driver(bd71828_regulator);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD71828 voltage regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bd71828-pmic");
