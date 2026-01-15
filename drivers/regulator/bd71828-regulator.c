// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019 ROHM Semiconductors
// bd71828-regulator.c ROHM BD71828GW-DS1 regulator driver
//

#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/rohm-bd71828.h>
#include <linux/mfd/rohm-bd72720.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define BD72720_MASK_LDON_HEAD GENMASK(2, 0)
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

static const struct reg_init bd71828_buck1_inits[] = {
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

static const struct reg_init bd71828_buck2_inits[] = {
	{
		.reg = BD71828_REG_PS_CTRL_1,
		.mask = BD71828_MASK_DVS_BUCK2_CTRL,
		.val = BD71828_DVS_BUCK2_CTRL_I2C,
	},
};

static const struct reg_init bd71828_buck6_inits[] = {
	{
		.reg = BD71828_REG_PS_CTRL_1,
		.mask = BD71828_MASK_DVS_BUCK6_CTRL,
		.val = BD71828_DVS_BUCK6_CTRL_I2C,
	},
};

static const struct reg_init bd71828_buck7_inits[] = {
	{
		.reg = BD71828_REG_PS_CTRL_1,
		.mask = BD71828_MASK_DVS_BUCK7_CTRL,
		.val = BD71828_DVS_BUCK7_CTRL_I2C,
	},
};

#define BD72720_MASK_DVS_BUCK1_CTRL BIT(4)
#define BD72720_MASK_DVS_LDO1_CTRL BIT(5)

static const struct reg_init bd72720_buck1_inits[] = {
	{
		.reg = BD72720_REG_PS_CTRL_2,
		.mask = BD72720_MASK_DVS_BUCK1_CTRL,
		.val = 0, /* Disable "run-level" control */
	},
};

static const struct reg_init bd72720_ldo1_inits[] = {
	{
		.reg = BD72720_REG_PS_CTRL_2,
		.mask = BD72720_MASK_DVS_LDO1_CTRL,
		.val = 0, /* Disable "run-level" control */
	},
};

/* BD71828 Buck voltages */
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

/* BD71828 LDO voltages */
static const struct linear_range bd71828_ldo_volts[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x00, 0x31, 50000),
	REGULATOR_LINEAR_RANGE(3300000, 0x32, 0x3f, 0),
};

/* BD72720 Buck voltages */
static const struct linear_range bd72720_buck1234_volts[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x00, 0xc0, 6250),
	REGULATOR_LINEAR_RANGE(1700000, 0xc1, 0xff, 0),
};

static const struct linear_range bd72720_buck589_volts[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x00, 0x78, 10000),
	REGULATOR_LINEAR_RANGE(1700000, 0x79, 0xff, 0),
};

static const struct linear_range bd72720_buck67_volts[] = {
	REGULATOR_LINEAR_RANGE(1500000, 0x00, 0xb4, 10000),
	REGULATOR_LINEAR_RANGE(3300000, 0xb5, 0xff, 0),
};

/*
 * The BUCK10 on BD72720 has two modes of operation, depending on a LDON_HEAD
 * setting. When LDON_HEAD is 0x0, the behaviour is as with other bucks, eg.
 * voltage can be set to a values indicated below using the VSEL register.
 *
 * However, when LDON_HEAD is set to 0x1 ... 0x7, BUCK 10 voltage is, according
 * to the data-sheet, "automatically adjusted following LDON_HEAD setting and
 * clamped to BUCK10_VID setting".
 *
 * Again, reading the data-sheet shows a "typical connection" where the BUCK10
 * is used to supply the LDOs 1-4. My assumption is that in practice, this
 * means that the BUCK10 voltage will be adjusted based on the maximum output
 * of the LDO 1-4 (to minimize power loss). This makes sense.
 *
 * Auto-adjusting regulators aren't something I really like to model in the
 * driver though - and, if the auto-adjustment works as intended, then there
 * should really be no need to software to care about the buck10 voltages.
 * If enable/disable control is still needed, we can implement buck10 as a
 * regulator with only the enable/disable ops - and device-tree can be used
 * to model the supply-relations. I believe this could allow the regulator
 * framework to automagically disable the BUCK10 if all LDOs that are being
 * supplied by it are disabled.
 */
static const struct linear_range bd72720_buck10_volts[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x00, 0xc0, 6250),
	REGULATOR_LINEAR_RANGE(1700000, 0xc1, 0xff, 0),
};

/* BD72720 LDO voltages */
static const struct linear_range bd72720_ldo1234_volts[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x00, 0x50, 6250),
	REGULATOR_LINEAR_RANGE(1000000, 0x51, 0x7f, 0),
};

static const struct linear_range bd72720_ldo57891011_volts[] = {
	REGULATOR_LINEAR_RANGE(750000, 0x00, 0xff, 10000),
};

static const struct linear_range bd72720_ldo6_volts[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x00, 0x78, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0x79, 0x7f, 0),
};

static const unsigned int bd71828_ramp_delay[] = { 2500, 5000, 10000, 20000 };

/*
 * BD72720 supports setting both the ramp-up and ramp-down values
 * separately. Do we need to support ramp-down setting?
 */
static const unsigned int bd72720_ramp_delay[] = { 5000, 7500, 10000, 12500 };

static int buck_set_hw_dvs_levels(struct device_node *np,
				  const struct regulator_desc *desc,
				  struct regulator_config *cfg)
{
	const struct bd71828_regulator_data *data;

	data = container_of_const(desc, struct bd71828_regulator_data, desc);

	return rohm_regulator_set_dvs_levels(&data->dvs, np, desc, cfg->regmap);
}

static int bd71828_ldo6_parse_dt(struct device_node *np,
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

static const struct regulator_ops bd72720_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
};

static const struct regulator_ops bd72720_buck10_ldon_head_op = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
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
			/*
			 * LPSR voltage is same as SUSPEND voltage. Allow
			 * only enabling/disabling regulator for LPSR state
			 */
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},
		.reg_inits = bd71828_buck1_inits,
		.reg_init_amnt = ARRAY_SIZE(bd71828_buck1_inits),
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
		.reg_inits = bd71828_buck2_inits,
		.reg_init_amnt = ARRAY_SIZE(bd71828_buck2_inits),
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
			.run_mask = BD71828_MASK_BUCK3_VOLT,
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
			.run_mask = BD71828_MASK_BUCK4_VOLT,
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
			.run_mask = BD71828_MASK_BUCK5_VOLT,
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
		.reg_inits = bd71828_buck6_inits,
		.reg_init_amnt = ARRAY_SIZE(bd71828_buck6_inits),
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
		.reg_inits = bd71828_buck7_inits,
		.reg_init_amnt = ARRAY_SIZE(bd71828_buck7_inits),
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
			.run_mask = BD71828_MASK_LDO_VOLT,
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
			.run_mask = BD71828_MASK_LDO_VOLT,
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
			.run_mask = BD71828_MASK_LDO_VOLT,
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
			.run_mask = BD71828_MASK_LDO_VOLT,
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
			.run_mask = BD71828_MASK_LDO_VOLT,
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
			.of_parse_cb = bd71828_ldo6_parse_dt,
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
			.idle_on_mask = BD71828_MASK_IDLE_EN,
			.suspend_on_mask = BD71828_MASK_SUSP_EN,
			.lpsr_on_mask = BD71828_MASK_LPSR_EN,
		},

	},
};

#define BD72720_BUCK10_DESC_INDEX 10
#define BD72720_NUM_BUCK_VOLTS 0x100
#define BD72720_NUM_LDO_VOLTS 0x100
#define BD72720_NUM_LDO12346_VOLTS 0x80

static const struct bd71828_regulator_data bd72720_rdata[] = {
	{
		.desc = {
			.name = "buck1",
			.of_match = of_match_ptr("buck1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK1,
			.type = REGULATOR_VOLTAGE,

			/*
			 * The BD72720 BUCK1 and LDO1 support GPIO toggled
			 * sub-RUN states called RUN0, RUN1, RUN2 and RUN3.
			 * The "operating mode" (sub-RUN states or normal)
			 * can be changed by a register.
			 *
			 * When the sub-RUN states are used, the voltage and
			 * enable state depend on a state specific
			 * configuration. The voltage and enable configuration
			 * for BUCK1 and LDO1 can be defined for each sub-RUN
			 * state using BD72720_REG_[BUCK,LDO]1_VSEL_R[0,1,2,3]
			 * voltage selection registers and the bits
			 * BD72720_MASK_RUN_[0,1,2,3]_EN in the enable registers.
			 * The PMIC will change both the BUCK1 and LDO1 voltages
			 * to the states defined in these registers when
			 * "DVS GPIOs" are toggled.
			 *
			 * If RUN 0 .. RUN 4 states are to be used, the normal
			 * voltage configuration mechanisms do not apply
			 * and we should overwrite the ops and ignore the
			 * voltage setting/getting registers which are setup
			 * here. This is not supported for now. If you need
			 * this functionality, you may try merging functionality
			 * from a downstream driver:
			 * https://rohmsemiconductor.github.io/Linux-Kernel-PMIC-Drivers/BD72720/
			 */
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck1234_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck1234_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK1_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK1_VSEL_RB,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK1_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR, /* Deep idle in data-sheet */
			.run_reg = BD72720_REG_BUCK1_VSEL_RB,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_reg = BD72720_REG_BUCK1_VSEL_I,
			.idle_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_reg = BD72720_REG_BUCK1_VSEL_S,
			.suspend_mask = BD72720_MASK_BUCK_VSEL,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_reg = BD72720_REG_BUCK1_VSEL_DI,
			.lpsr_mask = BD72720_MASK_BUCK_VSEL,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
		.reg_inits = bd72720_buck1_inits,
		.reg_init_amnt = ARRAY_SIZE(bd72720_buck1_inits),
	}, {
		.desc = {
			.name = "buck2",
			.of_match = of_match_ptr("buck2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK2,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck1234_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck1234_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK2_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK2_VSEL_R,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK2_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK2_VSEL_R,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_reg = BD72720_REG_BUCK2_VSEL_I,
			.idle_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_reg = BD72720_REG_BUCK2_VSEL_S,
			.suspend_mask = BD72720_MASK_BUCK_VSEL,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_reg = BD72720_REG_BUCK2_VSEL_DI,
			.lpsr_mask = BD72720_MASK_BUCK_VSEL,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "buck3",
			.of_match = of_match_ptr("buck3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK3,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck1234_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck1234_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK3_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK3_VSEL_R,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK3_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK3_VSEL_R,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_reg = BD72720_REG_BUCK3_VSEL_I,
			.idle_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_reg = BD72720_REG_BUCK3_VSEL_S,
			.suspend_mask = BD72720_MASK_BUCK_VSEL,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_reg = BD72720_REG_BUCK3_VSEL_DI,
			.lpsr_mask = BD72720_MASK_BUCK_VSEL,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "buck4",
			.of_match = of_match_ptr("buck4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK4,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck1234_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck1234_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK4_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK4_VSEL_R,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK4_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK4_VSEL_R,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_reg = BD72720_REG_BUCK4_VSEL_I,
			.idle_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_reg = BD72720_REG_BUCK4_VSEL_S,
			.suspend_mask = BD72720_MASK_BUCK_VSEL,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_reg = BD72720_REG_BUCK4_VSEL_DI,
			.lpsr_mask = BD72720_MASK_BUCK_VSEL,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "buck5",
			.of_match = of_match_ptr("buck5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK5,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck589_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck589_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK5_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK5_VSEL,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK5_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK5_VSEL,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "buck6",
			.of_match = of_match_ptr("buck6"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK6,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck67_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck67_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK6_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK6_VSEL,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK6_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK6_VSEL,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "buck7",
			.of_match = of_match_ptr("buck7"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK7,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck67_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck67_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK7_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK7_VSEL,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK7_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK7_VSEL,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "buck8",
			.of_match = of_match_ptr("buck8"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK8,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck589_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck589_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK8_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK8_VSEL,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK8_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK8_VSEL,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "buck9",
			.of_match = of_match_ptr("buck9"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK9,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck589_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck589_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK9_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK9_VSEL,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK9_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK9_VSEL,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "buck10",
			.of_match = of_match_ptr("buck10"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_BUCK10,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_buck10_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_buck10_volts),
			.n_voltages = BD72720_NUM_BUCK_VOLTS,
			.enable_reg = BD72720_REG_BUCK10_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_BUCK10_VSEL,
			.vsel_mask = BD72720_MASK_BUCK_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_BUCK10_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_BUCK10_VSEL,
			.run_mask = BD72720_MASK_BUCK_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo1",
			.of_match = of_match_ptr("ldo1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO1,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo1234_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo1234_volts),
			.n_voltages = BD72720_NUM_LDO12346_VOLTS,
			.enable_reg = BD72720_REG_LDO1_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO1_VSEL_RB,
			.vsel_mask = BD72720_MASK_LDO12346_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO1_MODE1,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO1_VSEL_RB,
			.run_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_reg = BD72720_REG_LDO1_VSEL_I,
			.idle_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_reg = BD72720_REG_LDO1_VSEL_S,
			.suspend_mask = BD72720_MASK_LDO12346_VSEL,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_reg = BD72720_REG_LDO1_VSEL_DI,
			.lpsr_mask = BD72720_MASK_LDO12346_VSEL,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
		.reg_inits = bd72720_ldo1_inits,
		.reg_init_amnt = ARRAY_SIZE(bd72720_ldo1_inits),
	}, {
		.desc = {
			.name = "ldo2",
			.of_match = of_match_ptr("ldo2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO2,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo1234_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo1234_volts),
			.n_voltages = BD72720_NUM_LDO12346_VOLTS,
			.enable_reg = BD72720_REG_LDO2_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO2_VSEL_R,
			.vsel_mask = BD72720_MASK_LDO12346_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO2_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO2_VSEL_R,
			.run_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_reg = BD72720_REG_LDO2_VSEL_I,
			.idle_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_reg = BD72720_REG_LDO2_VSEL_S,
			.suspend_mask = BD72720_MASK_LDO12346_VSEL,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_reg = BD72720_REG_LDO2_VSEL_DI,
			.lpsr_mask = BD72720_MASK_LDO12346_VSEL,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo3",
			.of_match = of_match_ptr("ldo3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO3,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo1234_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo1234_volts),
			.n_voltages = BD72720_NUM_LDO12346_VOLTS,
			.enable_reg = BD72720_REG_LDO3_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO3_VSEL_R,
			.vsel_mask = BD72720_MASK_LDO12346_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO3_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO3_VSEL_R,
			.run_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_reg = BD72720_REG_LDO3_VSEL_I,
			.idle_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_reg = BD72720_REG_LDO3_VSEL_S,
			.suspend_mask = BD72720_MASK_LDO12346_VSEL,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_reg = BD72720_REG_LDO3_VSEL_DI,
			.lpsr_mask = BD72720_MASK_LDO12346_VSEL,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo4",
			.of_match = of_match_ptr("ldo4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO4,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo1234_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo1234_volts),
			.n_voltages = BD72720_NUM_LDO12346_VOLTS,
			.enable_reg = BD72720_REG_LDO4_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO4_VSEL_R,
			.vsel_mask = BD72720_MASK_LDO12346_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO4_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO4_VSEL_R,
			.run_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_reg = BD72720_REG_LDO4_VSEL_I,
			.idle_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_reg = BD72720_REG_LDO4_VSEL_S,
			.suspend_mask = BD72720_MASK_LDO12346_VSEL,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_reg = BD72720_REG_LDO4_VSEL_DI,
			.lpsr_mask = BD72720_MASK_LDO12346_VSEL,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo5",
			.of_match = of_match_ptr("ldo5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO5,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo57891011_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo57891011_volts),
			.n_voltages = BD72720_NUM_LDO_VOLTS,
			.enable_reg = BD72720_REG_LDO5_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO5_VSEL,
			.vsel_mask = BD72720_MASK_LDO_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO5_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO5_VSEL,
			.run_mask = BD72720_MASK_LDO_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo6",
			.of_match = of_match_ptr("ldo6"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO6,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo6_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo6_volts),
			.n_voltages = BD72720_NUM_LDO12346_VOLTS,
			.enable_reg = BD72720_REG_LDO6_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO6_VSEL,
			.vsel_mask = BD72720_MASK_LDO12346_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO6_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO6_VSEL,
			.run_mask = BD72720_MASK_LDO12346_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo7",
			.of_match = of_match_ptr("ldo7"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO7,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo57891011_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo57891011_volts),
			.n_voltages = BD72720_NUM_LDO_VOLTS,
			.enable_reg = BD72720_REG_LDO7_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO7_VSEL,
			.vsel_mask = BD72720_MASK_LDO_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO7_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO7_VSEL,
			.run_mask = BD72720_MASK_LDO_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo8",
			.of_match = of_match_ptr("ldo8"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO8,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo57891011_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo57891011_volts),
			.n_voltages = BD72720_NUM_LDO_VOLTS,
			.enable_reg = BD72720_REG_LDO8_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO8_VSEL,
			.vsel_mask = BD72720_MASK_LDO_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO8_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO8_VSEL,
			.run_mask = BD72720_MASK_LDO_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo9",
			.of_match = of_match_ptr("ldo9"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO9,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo57891011_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo57891011_volts),
			.n_voltages = BD72720_NUM_LDO_VOLTS,
			.enable_reg = BD72720_REG_LDO9_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO9_VSEL,
			.vsel_mask = BD72720_MASK_LDO_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO9_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO9_VSEL,
			.run_mask = BD72720_MASK_LDO_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo10",
			.of_match = of_match_ptr("ldo10"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO10,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo57891011_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo57891011_volts),
			.n_voltages = BD72720_NUM_LDO_VOLTS,
			.enable_reg = BD72720_REG_LDO10_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO10_VSEL,
			.vsel_mask = BD72720_MASK_LDO_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO10_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO10_VSEL,
			.run_mask = BD72720_MASK_LDO_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	}, {
		.desc = {
			.name = "ldo11",
			.of_match = of_match_ptr("ldo11"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD72720_LDO11,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd72720_regulator_ops,
			.linear_ranges = bd72720_ldo57891011_volts,
			.n_linear_ranges = ARRAY_SIZE(bd72720_ldo57891011_volts),
			.n_voltages = BD72720_NUM_LDO_VOLTS,
			.enable_reg = BD72720_REG_LDO11_ON,
			.enable_mask = BD72720_MASK_RUN_B_EN,
			.vsel_reg = BD72720_REG_LDO11_VSEL,
			.vsel_mask = BD72720_MASK_LDO_VSEL,

			.ramp_delay_table = bd72720_ramp_delay,
			.n_ramp_values = ARRAY_SIZE(bd72720_ramp_delay),
			.ramp_reg = BD72720_REG_LDO11_MODE,
			.ramp_mask = BD72720_MASK_RAMP_UP_DELAY,
			.owner = THIS_MODULE,
			.of_parse_cb = buck_set_hw_dvs_levels,
		},
		.dvs = {
			.level_map = ROHM_DVS_LEVEL_RUN | ROHM_DVS_LEVEL_IDLE |
				     ROHM_DVS_LEVEL_SUSPEND |
				     ROHM_DVS_LEVEL_LPSR,
			.run_reg = BD72720_REG_LDO11_VSEL,
			.run_mask = BD72720_MASK_LDO_VSEL,
			.idle_on_mask = BD72720_MASK_IDLE_EN,
			.suspend_on_mask = BD72720_MASK_SUSPEND_EN,
			.lpsr_on_mask = BD72720_MASK_DEEP_IDLE_EN,
		},
	},
};

static int bd72720_buck10_ldon_head_mode(struct device *dev,
					 struct device_node *npreg,
					 struct regmap *regmap,
					 struct regulator_desc *buck10_desc)
{
	struct device_node *np __free(device_node) =
		of_get_child_by_name(npreg, "buck10");
	uint32_t ldon_head;
	int ldon_val;
	int ret;

	if (!np) {
		dev_err(dev, "failed to find buck10 regulator node\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "rohm,ldon-head-microvolt", &ldon_head);
	if (ret == -EINVAL)
		return 0;
	if (ret)
		return ret;

	/*
	 * LDON_HEAD mode means the BUCK10 is used to supply LDOs 1-4 and
	 * the BUCK 10 voltage is automatically set to follow LDO 1-4
	 * settings. Thus the BUCK10 should not allow voltage [g/s]etting.
	 */
	buck10_desc->ops = &bd72720_buck10_ldon_head_op;

	ldon_val = ldon_head / 50000 + 1;
	if (ldon_head > 300000) {
		dev_warn(dev, "Unsupported LDON_HEAD, clamping to 300 mV\n");
		ldon_val = 7;
	}

	return regmap_update_bits(regmap, BD72720_REG_LDO1_MODE2,
				  BD72720_MASK_LDON_HEAD, ldon_val);
}

static int bd72720_dt_parse(struct device *dev,
			    struct regulator_desc *buck10_desc,
			    struct regmap *regmap)
{
	struct device_node *nproot __free(device_node) =
		of_get_child_by_name(dev->parent->of_node, "regulators");

	if (!nproot) {
		dev_err(dev, "failed to find regulators node\n");
		return -ENODEV;
	}

	return bd72720_buck10_ldon_head_mode(dev, nproot, regmap, buck10_desc);
}

static int bd71828_probe(struct platform_device *pdev)
{
	int i, j, ret, num_regulators;
	struct regulator_config config = {
		.dev = pdev->dev.parent,
	};
	enum rohm_chip_type chip = platform_get_device_id(pdev)->driver_data;
	struct bd71828_regulator_data *rdata;

	config.regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!config.regmap)
		return -ENODEV;

	switch (chip) {
	case ROHM_CHIP_TYPE_BD72720:
		rdata = devm_kmemdup(&pdev->dev, bd72720_rdata,
				     sizeof(bd72720_rdata), GFP_KERNEL);
		if (!rdata)
			return -ENOMEM;

		ret = bd72720_dt_parse(&pdev->dev, &rdata[BD72720_BUCK10_DESC_INDEX].desc,
				       config.regmap);
		if (ret)
			return ret;

		num_regulators = ARRAY_SIZE(bd72720_rdata);
		break;

	case ROHM_CHIP_TYPE_BD71828:
		rdata = devm_kmemdup(&pdev->dev, bd71828_rdata,
				     sizeof(bd71828_rdata), GFP_KERNEL);
		if (!rdata)
			return -ENOMEM;

		num_regulators = ARRAY_SIZE(bd71828_rdata);

		break;
	default:
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Unsupported device\n");
	}

	for (i = 0; i < num_regulators; i++) {
		struct regulator_dev *rdev;
		struct bd71828_regulator_data *rd;

		rd = &rdata[i];

		config.driver_data = rd;
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

static const struct platform_device_id bd71828_pmic_id[] = {
	{ "bd71828-pmic", ROHM_CHIP_TYPE_BD71828 },
	{ "bd72720-pmic", ROHM_CHIP_TYPE_BD72720 },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd71828_pmic_id);

static struct platform_driver bd71828_regulator = {
	.driver = {
		.name = "bd71828-pmic",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = bd71828_probe,
	.id_table = bd71828_pmic_id,
};

module_platform_driver(bd71828_regulator);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD71828 voltage regulator driver");
MODULE_LICENSE("GPL");
