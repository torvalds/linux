// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 NXP.
 * NXP PCA9450 pmic driver
 */

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/pca9450.h>

struct pc9450_dvs_config {
	unsigned int run_reg; /* dvs0 */
	unsigned int run_mask;
	unsigned int standby_reg; /* dvs1 */
	unsigned int standby_mask;
};

struct pca9450_regulator_desc {
	struct regulator_desc desc;
	const struct pc9450_dvs_config dvs;
};

struct pca9450 {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *sd_vsel_gpio;
	enum pca9450_chip_type type;
	unsigned int rcnt;
	int irq;
};

static const struct regmap_range pca9450_status_range = {
	.range_min = PCA9450_REG_INT1,
	.range_max = PCA9450_REG_PWRON_STAT,
};

static const struct regmap_access_table pca9450_volatile_regs = {
	.yes_ranges = &pca9450_status_range,
	.n_yes_ranges = 1,
};

static const struct regmap_config pca9450_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &pca9450_volatile_regs,
	.max_register = PCA9450_MAX_REGISTER - 1,
	.cache_type = REGCACHE_RBTREE,
};

/*
 * BUCK1/2/3
 * BUCK1RAM[1:0] BUCK1 DVS ramp rate setting
 * 00: 25mV/1usec
 * 01: 25mV/2usec
 * 10: 25mV/4usec
 * 11: 25mV/8usec
 */
static const unsigned int pca9450_dvs_buck_ramp_table[] = {
	25000, 12500, 6250, 3125
};

static const struct regulator_ops pca9450_dvs_buck_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay	= regulator_set_ramp_delay_regmap,
};

static const struct regulator_ops pca9450_buck_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static const struct regulator_ops pca9450_ldo_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

/*
 * BUCK1/2/3
 * 0.60 to 2.1875V (12.5mV step)
 */
static const struct linear_range pca9450_dvs_buck_volts[] = {
	REGULATOR_LINEAR_RANGE(600000,  0x00, 0x7F, 12500),
};

/*
 * BUCK4/5/6
 * 0.6V to 3.4V (25mV step)
 */
static const struct linear_range pca9450_buck_volts[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x00, 0x70, 25000),
	REGULATOR_LINEAR_RANGE(3400000, 0x71, 0x7F, 0),
};

/*
 * LDO1
 * 1.6 to 3.3V ()
 */
static const struct linear_range pca9450_ldo1_volts[] = {
	REGULATOR_LINEAR_RANGE(1600000, 0x00, 0x03, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 0x04, 0x07, 100000),
};

/*
 * LDO2
 * 0.8 to 1.15V (50mV step)
 */
static const struct linear_range pca9450_ldo2_volts[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x00, 0x07, 50000),
};

/*
 * LDO3/4
 * 0.8 to 3.3V (100mV step)
 */
static const struct linear_range pca9450_ldo34_volts[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x00, 0x19, 100000),
	REGULATOR_LINEAR_RANGE(3300000, 0x1A, 0x1F, 0),
};

/*
 * LDO5
 * 1.8 to 3.3V (100mV step)
 */
static const struct linear_range pca9450_ldo5_volts[] = {
	REGULATOR_LINEAR_RANGE(1800000,  0x00, 0x0F, 100000),
};

static int buck_set_dvs(const struct regulator_desc *desc,
			struct device_node *np, struct regmap *regmap,
			char *prop, unsigned int reg, unsigned int mask)
{
	int ret, i;
	uint32_t uv;

	ret = of_property_read_u32(np, prop, &uv);
	if (ret == -EINVAL)
		return 0;
	else if (ret)
		return ret;

	for (i = 0; i < desc->n_voltages; i++) {
		ret = regulator_desc_list_voltage_linear_range(desc, i);
		if (ret < 0)
			continue;
		if (ret == uv) {
			i <<= ffs(desc->vsel_mask) - 1;
			ret = regmap_update_bits(regmap, reg, mask, i);
			break;
		}
	}

	if (ret == 0) {
		struct pca9450_regulator_desc *regulator = container_of(desc,
					struct pca9450_regulator_desc, desc);

		/* Enable DVS control through PMIC_STBY_REQ for this BUCK */
		ret = regmap_update_bits(regmap, regulator->desc.enable_reg,
					 BUCK1_DVS_CTRL, BUCK1_DVS_CTRL);
	}
	return ret;
}

static int pca9450_set_dvs_levels(struct device_node *np,
			    const struct regulator_desc *desc,
			    struct regulator_config *cfg)
{
	struct pca9450_regulator_desc *data = container_of(desc,
					struct pca9450_regulator_desc, desc);
	const struct pc9450_dvs_config *dvs = &data->dvs;
	unsigned int reg, mask;
	char *prop;
	int i, ret = 0;

	for (i = 0; i < PCA9450_DVS_LEVEL_MAX; i++) {
		switch (i) {
		case PCA9450_DVS_LEVEL_RUN:
			prop = "nxp,dvs-run-voltage";
			reg = dvs->run_reg;
			mask = dvs->run_mask;
			break;
		case PCA9450_DVS_LEVEL_STANDBY:
			prop = "nxp,dvs-standby-voltage";
			reg = dvs->standby_reg;
			mask = dvs->standby_mask;
			break;
		default:
			return -EINVAL;
		}

		ret = buck_set_dvs(desc, np, cfg->regmap, prop, reg, mask);
		if (ret)
			break;
	}

	return ret;
}

static const struct pca9450_regulator_desc pca9450a_regulators[] = {
	{
		.desc = {
			.name = "buck1",
			.of_match = of_match_ptr("BUCK1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK1,
			.ops = &pca9450_dvs_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK1_VOLTAGE_NUM,
			.linear_ranges = pca9450_dvs_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_dvs_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK1OUT_DVS0,
			.vsel_mask = BUCK1OUT_DVS0_MASK,
			.enable_reg = PCA9450_REG_BUCK1CTRL,
			.enable_mask = BUCK1_ENMODE_MASK,
			.ramp_reg = PCA9450_REG_BUCK1CTRL,
			.ramp_mask = BUCK1_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK1OUT_DVS0,
			.run_mask = BUCK1OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK1OUT_DVS1,
			.standby_mask = BUCK1OUT_DVS1_MASK,
		},
	},
	{
		.desc = {
			.name = "buck2",
			.of_match = of_match_ptr("BUCK2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK2,
			.ops = &pca9450_dvs_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK2_VOLTAGE_NUM,
			.linear_ranges = pca9450_dvs_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_dvs_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK2OUT_DVS0,
			.vsel_mask = BUCK2OUT_DVS0_MASK,
			.enable_reg = PCA9450_REG_BUCK2CTRL,
			.enable_mask = BUCK2_ENMODE_MASK,
			.ramp_reg = PCA9450_REG_BUCK2CTRL,
			.ramp_mask = BUCK2_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK2OUT_DVS0,
			.run_mask = BUCK2OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK2OUT_DVS1,
			.standby_mask = BUCK2OUT_DVS1_MASK,
		},
	},
	{
		.desc = {
			.name = "buck3",
			.of_match = of_match_ptr("BUCK3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK3,
			.ops = &pca9450_dvs_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK3_VOLTAGE_NUM,
			.linear_ranges = pca9450_dvs_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_dvs_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK3OUT_DVS0,
			.vsel_mask = BUCK3OUT_DVS0_MASK,
			.enable_reg = PCA9450_REG_BUCK3CTRL,
			.enable_mask = BUCK3_ENMODE_MASK,
			.ramp_reg = PCA9450_REG_BUCK3CTRL,
			.ramp_mask = BUCK3_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK3OUT_DVS0,
			.run_mask = BUCK3OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK3OUT_DVS1,
			.standby_mask = BUCK3OUT_DVS1_MASK,
		},
	},
	{
		.desc = {
			.name = "buck4",
			.of_match = of_match_ptr("BUCK4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK4,
			.ops = &pca9450_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK4_VOLTAGE_NUM,
			.linear_ranges = pca9450_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK4OUT,
			.vsel_mask = BUCK4OUT_MASK,
			.enable_reg = PCA9450_REG_BUCK4CTRL,
			.enable_mask = BUCK4_ENMODE_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "buck5",
			.of_match = of_match_ptr("BUCK5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK5,
			.ops = &pca9450_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK5_VOLTAGE_NUM,
			.linear_ranges = pca9450_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK5OUT,
			.vsel_mask = BUCK5OUT_MASK,
			.enable_reg = PCA9450_REG_BUCK5CTRL,
			.enable_mask = BUCK5_ENMODE_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "buck6",
			.of_match = of_match_ptr("BUCK6"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK6,
			.ops = &pca9450_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK6_VOLTAGE_NUM,
			.linear_ranges = pca9450_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK6OUT,
			.vsel_mask = BUCK6OUT_MASK,
			.enable_reg = PCA9450_REG_BUCK6CTRL,
			.enable_mask = BUCK6_ENMODE_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo1",
			.of_match = of_match_ptr("LDO1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO1,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO1_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo1_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo1_volts),
			.vsel_reg = PCA9450_REG_LDO1CTRL,
			.vsel_mask = LDO1OUT_MASK,
			.enable_reg = PCA9450_REG_LDO1CTRL,
			.enable_mask = LDO1_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo2",
			.of_match = of_match_ptr("LDO2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO2,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO2_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo2_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo2_volts),
			.vsel_reg = PCA9450_REG_LDO2CTRL,
			.vsel_mask = LDO2OUT_MASK,
			.enable_reg = PCA9450_REG_LDO2CTRL,
			.enable_mask = LDO2_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo3",
			.of_match = of_match_ptr("LDO3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO3,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO3_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo34_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo34_volts),
			.vsel_reg = PCA9450_REG_LDO3CTRL,
			.vsel_mask = LDO3OUT_MASK,
			.enable_reg = PCA9450_REG_LDO3CTRL,
			.enable_mask = LDO3_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo4",
			.of_match = of_match_ptr("LDO4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO4,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO4_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo34_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo34_volts),
			.vsel_reg = PCA9450_REG_LDO4CTRL,
			.vsel_mask = LDO4OUT_MASK,
			.enable_reg = PCA9450_REG_LDO4CTRL,
			.enable_mask = LDO4_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo5",
			.of_match = of_match_ptr("LDO5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO5,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO5_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo5_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo5_volts),
			.vsel_reg = PCA9450_REG_LDO5CTRL_H,
			.vsel_mask = LDO5HOUT_MASK,
			.enable_reg = PCA9450_REG_LDO5CTRL_H,
			.enable_mask = LDO5H_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
};

/*
 * Buck3 removed on PCA9450B and connected with Buck1 internal for dual phase
 * on PCA9450C as no Buck3.
 */
static const struct pca9450_regulator_desc pca9450bc_regulators[] = {
	{
		.desc = {
			.name = "buck1",
			.of_match = of_match_ptr("BUCK1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK1,
			.ops = &pca9450_dvs_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK1_VOLTAGE_NUM,
			.linear_ranges = pca9450_dvs_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_dvs_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK1OUT_DVS0,
			.vsel_mask = BUCK1OUT_DVS0_MASK,
			.enable_reg = PCA9450_REG_BUCK1CTRL,
			.enable_mask = BUCK1_ENMODE_MASK,
			.ramp_reg = PCA9450_REG_BUCK1CTRL,
			.ramp_mask = BUCK1_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK1OUT_DVS0,
			.run_mask = BUCK1OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK1OUT_DVS1,
			.standby_mask = BUCK1OUT_DVS1_MASK,
		},
	},
	{
		.desc = {
			.name = "buck2",
			.of_match = of_match_ptr("BUCK2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK2,
			.ops = &pca9450_dvs_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK2_VOLTAGE_NUM,
			.linear_ranges = pca9450_dvs_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_dvs_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK2OUT_DVS0,
			.vsel_mask = BUCK2OUT_DVS0_MASK,
			.enable_reg = PCA9450_REG_BUCK2CTRL,
			.enable_mask = BUCK2_ENMODE_MASK,
			.ramp_reg = PCA9450_REG_BUCK2CTRL,
			.ramp_mask = BUCK2_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK2OUT_DVS0,
			.run_mask = BUCK2OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK2OUT_DVS1,
			.standby_mask = BUCK2OUT_DVS1_MASK,
		},
	},
	{
		.desc = {
			.name = "buck4",
			.of_match = of_match_ptr("BUCK4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK4,
			.ops = &pca9450_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK4_VOLTAGE_NUM,
			.linear_ranges = pca9450_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK4OUT,
			.vsel_mask = BUCK4OUT_MASK,
			.enable_reg = PCA9450_REG_BUCK4CTRL,
			.enable_mask = BUCK4_ENMODE_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "buck5",
			.of_match = of_match_ptr("BUCK5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK5,
			.ops = &pca9450_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK5_VOLTAGE_NUM,
			.linear_ranges = pca9450_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK5OUT,
			.vsel_mask = BUCK5OUT_MASK,
			.enable_reg = PCA9450_REG_BUCK5CTRL,
			.enable_mask = BUCK5_ENMODE_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "buck6",
			.of_match = of_match_ptr("BUCK6"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK6,
			.ops = &pca9450_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK6_VOLTAGE_NUM,
			.linear_ranges = pca9450_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK6OUT,
			.vsel_mask = BUCK6OUT_MASK,
			.enable_reg = PCA9450_REG_BUCK6CTRL,
			.enable_mask = BUCK6_ENMODE_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo1",
			.of_match = of_match_ptr("LDO1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO1,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO1_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo1_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo1_volts),
			.vsel_reg = PCA9450_REG_LDO1CTRL,
			.vsel_mask = LDO1OUT_MASK,
			.enable_reg = PCA9450_REG_LDO1CTRL,
			.enable_mask = LDO1_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo2",
			.of_match = of_match_ptr("LDO2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO2,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO2_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo2_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo2_volts),
			.vsel_reg = PCA9450_REG_LDO2CTRL,
			.vsel_mask = LDO2OUT_MASK,
			.enable_reg = PCA9450_REG_LDO2CTRL,
			.enable_mask = LDO2_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo3",
			.of_match = of_match_ptr("LDO3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO3,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO3_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo34_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo34_volts),
			.vsel_reg = PCA9450_REG_LDO3CTRL,
			.vsel_mask = LDO3OUT_MASK,
			.enable_reg = PCA9450_REG_LDO3CTRL,
			.enable_mask = LDO3_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo4",
			.of_match = of_match_ptr("LDO4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO4,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO4_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo34_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo34_volts),
			.vsel_reg = PCA9450_REG_LDO4CTRL,
			.vsel_mask = LDO4OUT_MASK,
			.enable_reg = PCA9450_REG_LDO4CTRL,
			.enable_mask = LDO4_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo5",
			.of_match = of_match_ptr("LDO5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_LDO5,
			.ops = &pca9450_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO5_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo5_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo5_volts),
			.vsel_reg = PCA9450_REG_LDO5CTRL_H,
			.vsel_mask = LDO5HOUT_MASK,
			.enable_reg = PCA9450_REG_LDO5CTRL_H,
			.enable_mask = LDO5H_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
};

static irqreturn_t pca9450_irq_handler(int irq, void *data)
{
	struct pca9450 *pca9450 = data;
	struct regmap *regmap = pca9450->regmap;
	unsigned int status;
	int ret;

	ret = regmap_read(regmap, PCA9450_REG_INT1, &status);
	if (ret < 0) {
		dev_err(pca9450->dev,
			"Failed to read INT1(%d)\n", ret);
		return IRQ_NONE;
	}

	if (status & IRQ_PWRON)
		dev_warn(pca9450->dev, "PWRON interrupt.\n");

	if (status & IRQ_WDOGB)
		dev_warn(pca9450->dev, "WDOGB interrupt.\n");

	if (status & IRQ_VR_FLT1)
		dev_warn(pca9450->dev, "VRFLT1 interrupt.\n");

	if (status & IRQ_VR_FLT2)
		dev_warn(pca9450->dev, "VRFLT2 interrupt.\n");

	if (status & IRQ_LOWVSYS)
		dev_warn(pca9450->dev, "LOWVSYS interrupt.\n");

	if (status & IRQ_THERM_105)
		dev_warn(pca9450->dev, "IRQ_THERM_105 interrupt.\n");

	if (status & IRQ_THERM_125)
		dev_warn(pca9450->dev, "IRQ_THERM_125 interrupt.\n");

	return IRQ_HANDLED;
}

static int pca9450_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	enum pca9450_chip_type type = (unsigned int)(uintptr_t)
				      of_device_get_match_data(&i2c->dev);
	const struct pca9450_regulator_desc	*regulator_desc;
	struct regulator_config config = { };
	struct pca9450 *pca9450;
	unsigned int device_id, i;
	unsigned int reset_ctrl;
	int ret;

	if (!i2c->irq) {
		dev_err(&i2c->dev, "No IRQ configured?\n");
		return -EINVAL;
	}

	pca9450 = devm_kzalloc(&i2c->dev, sizeof(struct pca9450), GFP_KERNEL);
	if (!pca9450)
		return -ENOMEM;

	switch (type) {
	case PCA9450_TYPE_PCA9450A:
		regulator_desc = pca9450a_regulators;
		pca9450->rcnt = ARRAY_SIZE(pca9450a_regulators);
		break;
	case PCA9450_TYPE_PCA9450BC:
		regulator_desc = pca9450bc_regulators;
		pca9450->rcnt = ARRAY_SIZE(pca9450bc_regulators);
		break;
	default:
		dev_err(&i2c->dev, "Unknown device type");
		return -EINVAL;
	}

	pca9450->irq = i2c->irq;
	pca9450->type = type;
	pca9450->dev = &i2c->dev;

	dev_set_drvdata(&i2c->dev, pca9450);

	pca9450->regmap = devm_regmap_init_i2c(i2c,
					       &pca9450_regmap_config);
	if (IS_ERR(pca9450->regmap)) {
		dev_err(&i2c->dev, "regmap initialization failed\n");
		return PTR_ERR(pca9450->regmap);
	}

	ret = regmap_read(pca9450->regmap, PCA9450_REG_DEV_ID, &device_id);
	if (ret) {
		dev_err(&i2c->dev, "Read device id error\n");
		return ret;
	}

	/* Check your board and dts for match the right pmic */
	if (((device_id >> 4) != 0x1 && type == PCA9450_TYPE_PCA9450A) ||
	    ((device_id >> 4) != 0x3 && type == PCA9450_TYPE_PCA9450BC)) {
		dev_err(&i2c->dev, "Device id(%x) mismatched\n",
			device_id >> 4);
		return -EINVAL;
	}

	for (i = 0; i < pca9450->rcnt; i++) {
		const struct regulator_desc *desc;
		struct regulator_dev *rdev;
		const struct pca9450_regulator_desc *r;

		r = &regulator_desc[i];
		desc = &r->desc;

		config.regmap = pca9450->regmap;
		config.dev = pca9450->dev;

		rdev = devm_regulator_register(pca9450->dev, desc, &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(pca9450->dev,
				"Failed to register regulator(%s): %d\n",
				desc->name, ret);
			return ret;
		}
	}

	ret = devm_request_threaded_irq(pca9450->dev, pca9450->irq, NULL,
					pca9450_irq_handler,
					(IRQF_TRIGGER_FALLING | IRQF_ONESHOT),
					"pca9450-irq", pca9450);
	if (ret != 0) {
		dev_err(pca9450->dev, "Failed to request IRQ: %d\n",
			pca9450->irq);
		return ret;
	}
	/* Unmask all interrupt except PWRON/WDOG/RSVD */
	ret = regmap_update_bits(pca9450->regmap, PCA9450_REG_INT1_MSK,
				IRQ_VR_FLT1 | IRQ_VR_FLT2 | IRQ_LOWVSYS |
				IRQ_THERM_105 | IRQ_THERM_125,
				IRQ_PWRON | IRQ_WDOGB | IRQ_RSVD);
	if (ret) {
		dev_err(&i2c->dev, "Unmask irq error\n");
		return ret;
	}

	/* Clear PRESET_EN bit in BUCK123_DVS to use DVS registers */
	ret = regmap_clear_bits(pca9450->regmap, PCA9450_REG_BUCK123_DVS,
				BUCK123_PRESET_EN);
	if (ret) {
		dev_err(&i2c->dev, "Failed to clear PRESET_EN bit: %d\n", ret);
		return ret;
	}

	if (of_property_read_bool(i2c->dev.of_node, "nxp,wdog_b-warm-reset"))
		reset_ctrl = WDOG_B_CFG_WARM;
	else
		reset_ctrl = WDOG_B_CFG_COLD_LDO12;

	/* Set reset behavior on assertion of WDOG_B signal */
	ret = regmap_update_bits(pca9450->regmap, PCA9450_REG_RESET_CTRL,
				 WDOG_B_CFG_MASK, reset_ctrl);
	if (ret) {
		dev_err(&i2c->dev, "Failed to set WDOG_B reset behavior\n");
		return ret;
	}

	if (of_property_read_bool(i2c->dev.of_node, "nxp,i2c-lt-enable")) {
		/* Enable I2C Level Translator */
		ret = regmap_update_bits(pca9450->regmap, PCA9450_REG_CONFIG2,
					 I2C_LT_MASK, I2C_LT_ON_STANDBY_RUN);
		if (ret) {
			dev_err(&i2c->dev,
				"Failed to enable I2C level translator\n");
			return ret;
		}
	}

	/*
	 * The driver uses the LDO5CTRL_H register to control the LDO5 regulator.
	 * This is only valid if the SD_VSEL input of the PMIC is high. Let's
	 * check if the pin is available as GPIO and set it to high.
	 */
	pca9450->sd_vsel_gpio = gpiod_get_optional(pca9450->dev, "sd-vsel", GPIOD_OUT_HIGH);

	if (IS_ERR(pca9450->sd_vsel_gpio)) {
		dev_err(&i2c->dev, "Failed to get SD_VSEL GPIO\n");
		return PTR_ERR(pca9450->sd_vsel_gpio);
	}

	dev_info(&i2c->dev, "%s probed.\n",
		type == PCA9450_TYPE_PCA9450A ? "pca9450a" : "pca9450bc");

	return 0;
}

static const struct of_device_id pca9450_of_match[] = {
	{
		.compatible = "nxp,pca9450a",
		.data = (void *)PCA9450_TYPE_PCA9450A,
	},
	{
		.compatible = "nxp,pca9450b",
		.data = (void *)PCA9450_TYPE_PCA9450BC,
	},
	{
		.compatible = "nxp,pca9450c",
		.data = (void *)PCA9450_TYPE_PCA9450BC,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pca9450_of_match);

static struct i2c_driver pca9450_i2c_driver = {
	.driver = {
		.name = "nxp-pca9450",
		.of_match_table = pca9450_of_match,
	},
	.probe = pca9450_i2c_probe,
};

module_i2c_driver(pca9450_i2c_driver);

MODULE_AUTHOR("Robin Gong <yibin.gong@nxp.com>");
MODULE_DESCRIPTION("NXP PCA9450 Power Management IC driver");
MODULE_LICENSE("GPL");
