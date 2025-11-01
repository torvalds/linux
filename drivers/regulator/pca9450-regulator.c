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
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/pca9450.h>
#include <dt-bindings/regulator/nxp,pca9450-regulator.h>

static unsigned int pca9450_buck_get_mode(struct regulator_dev *rdev);
static int pca9450_buck_set_mode(struct regulator_dev *rdev, unsigned int mode);

struct pc9450_dvs_config {
	unsigned int run_reg; /* dvs0 */
	unsigned int run_mask;
	unsigned int standby_reg; /* dvs1 */
	unsigned int standby_mask;
	unsigned int mode_reg; /* ctrl */
	unsigned int mode_mask;
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
	bool sd_vsel_fixed_low;
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
	.cache_type = REGCACHE_MAPLE,
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
	.set_mode = pca9450_buck_set_mode,
	.get_mode = pca9450_buck_get_mode,
};

static const struct regulator_ops pca9450_buck_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_mode = pca9450_buck_set_mode,
	.get_mode = pca9450_buck_get_mode,
};

static const struct regulator_ops pca9450_ldo_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static unsigned int pca9450_ldo5_get_reg_voltage_sel(struct regulator_dev *rdev)
{
	struct pca9450 *pca9450 = rdev_get_drvdata(rdev);

	if (pca9450->sd_vsel_fixed_low)
		return PCA9450_REG_LDO5CTRL_L;

	if (pca9450->sd_vsel_gpio && !gpiod_get_value(pca9450->sd_vsel_gpio))
		return PCA9450_REG_LDO5CTRL_L;

	return rdev->desc->vsel_reg;
}

static int pca9450_ldo5_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, pca9450_ldo5_get_reg_voltage_sel(rdev), &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	return val;
}

static int pca9450_ldo5_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned int sel)
{
	int ret;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;

	ret = regmap_update_bits(rdev->regmap, pca9450_ldo5_get_reg_voltage_sel(rdev),
				  rdev->desc->vsel_mask, sel);
	if (ret)
		return ret;

	if (rdev->desc->apply_bit)
		ret = regmap_update_bits(rdev->regmap, rdev->desc->apply_reg,
					 rdev->desc->apply_bit,
					 rdev->desc->apply_bit);
	return ret;
}

static const struct regulator_ops pca9450_ldo5_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = pca9450_ldo5_set_voltage_sel_regmap,
	.get_voltage_sel = pca9450_ldo5_get_voltage_sel_regmap,
};

/*
 * BUCK1/2/3
 * 0.60 to 2.1875V (12.5mV step)
 */
static const struct linear_range pca9450_dvs_buck_volts[] = {
	REGULATOR_LINEAR_RANGE(600000,  0x00, 0x7F, 12500),
};

/*
 * BUCK1/3
 * 0.65 to 2.2375V (12.5mV step)
 */
static const struct linear_range pca9451a_dvs_buck_volts[] = {
	REGULATOR_LINEAR_RANGE(650000, 0x00, 0x7F, 12500),
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

static inline unsigned int pca9450_map_mode(unsigned int mode)
{
	switch (mode) {
	case PCA9450_BUCK_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	case PCA9450_BUCK_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int pca9450_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct pca9450_regulator_desc *desc = container_of(rdev->desc,
					struct pca9450_regulator_desc, desc);
	const struct pc9450_dvs_config *dvs = &desc->dvs;
	int val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = dvs->mode_mask;
		break;
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(&rdev->dev, "pca9450 buck set_mode %#x, %#x, %#x\n",
		dvs->mode_reg, dvs->mode_mask, val);

	return regmap_update_bits(rdev->regmap, dvs->mode_reg,
				  dvs->mode_mask, val);
}

static unsigned int pca9450_buck_get_mode(struct regulator_dev *rdev)
{
	struct pca9450_regulator_desc *desc = container_of(rdev->desc,
					struct pca9450_regulator_desc, desc);
	const struct pc9450_dvs_config *dvs = &desc->dvs;
	int ret = 0, regval;

	ret = regmap_read(rdev->regmap, dvs->mode_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to get pca9450 buck mode: %d\n", ret);
		return ret;
	}

	if ((regval & dvs->mode_mask) == dvs->mode_mask)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}

static struct pca9450_regulator_desc pca9450a_regulators[] = {
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK1OUT_DVS0,
			.run_mask = BUCK1OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK1OUT_DVS1,
			.standby_mask = BUCK1OUT_DVS1_MASK,
			.mode_reg = PCA9450_REG_BUCK1CTRL,
			.mode_mask = BUCK1_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ_STBYREQ,
			.ramp_reg = PCA9450_REG_BUCK2CTRL,
			.ramp_mask = BUCK2_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK2OUT_DVS0,
			.run_mask = BUCK2OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK2OUT_DVS1,
			.standby_mask = BUCK2OUT_DVS1_MASK,
			.mode_reg = PCA9450_REG_BUCK2CTRL,
			.mode_mask = BUCK2_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.ramp_reg = PCA9450_REG_BUCK3CTRL,
			.ramp_mask = BUCK3_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK3OUT_DVS0,
			.run_mask = BUCK3OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK3OUT_DVS1,
			.standby_mask = BUCK3OUT_DVS1_MASK,
			.mode_reg = PCA9450_REG_BUCK3CTRL,
			.mode_mask = BUCK3_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK4CTRL,
			.mode_mask = BUCK4_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK5CTRL,
			.mode_mask = BUCK5_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK6CTRL,
			.mode_mask = BUCK6_FPWM,
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
			.ops = &pca9450_ldo5_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO5_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo5_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo5_volts),
			.vsel_reg = PCA9450_REG_LDO5CTRL_H,
			.vsel_mask = LDO5HOUT_MASK,
			.enable_reg = PCA9450_REG_LDO5CTRL_L,
			.enable_mask = LDO5H_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
};

/*
 * Buck3 removed on PCA9450B and connected with Buck1 internal for dual phase
 * on PCA9450C as no Buck3.
 */
static struct pca9450_regulator_desc pca9450bc_regulators[] = {
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.ramp_reg = PCA9450_REG_BUCK1CTRL,
			.ramp_mask = BUCK1_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK1OUT_DVS0,
			.run_mask = BUCK1OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK1OUT_DVS1,
			.standby_mask = BUCK1OUT_DVS1_MASK,
			.mode_reg = PCA9450_REG_BUCK1CTRL,
			.mode_mask = BUCK1_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ_STBYREQ,
			.ramp_reg = PCA9450_REG_BUCK2CTRL,
			.ramp_mask = BUCK2_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK2OUT_DVS0,
			.run_mask = BUCK2OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK2OUT_DVS1,
			.standby_mask = BUCK2OUT_DVS1_MASK,
			.mode_reg = PCA9450_REG_BUCK2CTRL,
			.mode_mask = BUCK2_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK4CTRL,
			.mode_mask = BUCK4_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK5CTRL,
			.mode_mask = BUCK5_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK6CTRL,
			.mode_mask = BUCK6_FPWM,
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
			.ops = &pca9450_ldo5_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO5_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo5_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo5_volts),
			.vsel_reg = PCA9450_REG_LDO5CTRL_H,
			.vsel_mask = LDO5HOUT_MASK,
			.enable_reg = PCA9450_REG_LDO5CTRL_L,
			.enable_mask = LDO5H_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
};

static struct pca9450_regulator_desc pca9451a_regulators[] = {
	{
		.desc = {
			.name = "buck1",
			.of_match = of_match_ptr("BUCK1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PCA9450_BUCK1,
			.ops = &pca9450_dvs_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_BUCK1_VOLTAGE_NUM,
			.linear_ranges = pca9451a_dvs_buck_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9451a_dvs_buck_volts),
			.vsel_reg = PCA9450_REG_BUCK1OUT_DVS0,
			.vsel_mask = BUCK1OUT_DVS0_MASK,
			.enable_reg = PCA9450_REG_BUCK1CTRL,
			.enable_mask = BUCK1_ENMODE_MASK,
			.enable_val = BUCK_ENMODE_ONREQ,
			.ramp_mask = BUCK1_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK1OUT_DVS0,
			.run_mask = BUCK1OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK1OUT_DVS1,
			.standby_mask = BUCK1OUT_DVS1_MASK,
			.mode_reg = PCA9450_REG_BUCK1CTRL,
			.mode_mask = BUCK1_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ_STBYREQ,
			.ramp_mask = BUCK2_RAMP_MASK,
			.ramp_delay_table = pca9450_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pca9450_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pca9450_set_dvs_levels,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.run_reg = PCA9450_REG_BUCK2OUT_DVS0,
			.run_mask = BUCK2OUT_DVS0_MASK,
			.standby_reg = PCA9450_REG_BUCK2OUT_DVS1,
			.standby_mask = BUCK2OUT_DVS1_MASK,
			.mode_reg = PCA9450_REG_BUCK2CTRL,
			.mode_mask = BUCK2_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK4CTRL,
			.mode_mask = BUCK4_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK5CTRL,
			.mode_mask = BUCK5_FPWM,
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
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
			.of_map_mode = pca9450_map_mode,
		},
		.dvs = {
			.mode_reg = PCA9450_REG_BUCK6CTRL,
			.mode_mask = BUCK6_FPWM,
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
			.ops = &pca9450_ldo5_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PCA9450_LDO5_VOLTAGE_NUM,
			.linear_ranges = pca9450_ldo5_volts,
			.n_linear_ranges = ARRAY_SIZE(pca9450_ldo5_volts),
			.vsel_reg = PCA9450_REG_LDO5CTRL_H,
			.vsel_mask = LDO5HOUT_MASK,
			.enable_reg = PCA9450_REG_LDO5CTRL_L,
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

static int pca9450_i2c_restart_handler(struct sys_off_data *data)
{
	struct pca9450 *pca9450 = data->cb_data;
	struct i2c_client *i2c = container_of(pca9450->dev, struct i2c_client, dev);

	dev_dbg(&i2c->dev, "Restarting device..\n");
	if (i2c_smbus_write_byte_data(i2c, PCA9450_REG_SWRST, SW_RST_COMMAND) == 0) {
		/* tRESTART is 250ms, so 300 should be enough to make sure it happened */
		mdelay(300);
		/* When we get here, the PMIC didn't power cycle for some reason. so warn.*/
		dev_warn(&i2c->dev, "Device didn't respond to restart command\n");
	} else {
		dev_err(&i2c->dev, "Restart command failed\n");
	}

	return 0;
}

static int pca9450_i2c_probe(struct i2c_client *i2c)
{
	enum pca9450_chip_type type = (unsigned int)(uintptr_t)
				      of_device_get_match_data(&i2c->dev);
	const struct pca9450_regulator_desc *regulator_desc;
	struct regulator_config config = { };
	struct regulator_dev *ldo5;
	struct pca9450 *pca9450;
	unsigned int device_id, i;
	unsigned int reset_ctrl;
	int ret;

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
	case PCA9450_TYPE_PCA9451A:
	case PCA9450_TYPE_PCA9452:
		regulator_desc = pca9451a_regulators;
		pca9450->rcnt = ARRAY_SIZE(pca9451a_regulators);
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
	if (IS_ERR(pca9450->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(pca9450->regmap),
				     "regmap initialization failed\n");

	ret = regmap_read(pca9450->regmap, PCA9450_REG_DEV_ID, &device_id);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Read device id error\n");

	/* Check your board and dts for match the right pmic */
	if (((device_id >> 4) != 0x1 && type == PCA9450_TYPE_PCA9450A) ||
	    ((device_id >> 4) != 0x3 && type == PCA9450_TYPE_PCA9450BC) ||
	    ((device_id >> 4) != 0x9 && type == PCA9450_TYPE_PCA9451A) ||
	    ((device_id >> 4) != 0x9 && type == PCA9450_TYPE_PCA9452))
		return dev_err_probe(&i2c->dev, -EINVAL,
				     "Device id(%x) mismatched\n", device_id >> 4);

	for (i = 0; i < pca9450->rcnt; i++) {
		const struct regulator_desc *desc;
		struct regulator_dev *rdev;
		const struct pca9450_regulator_desc *r;

		r = &regulator_desc[i];
		desc = &r->desc;

		if (type == PCA9450_TYPE_PCA9451A && !strcmp(desc->name, "ldo3"))
			continue;

		config.regmap = pca9450->regmap;
		config.dev = pca9450->dev;
		config.driver_data = pca9450;

		rdev = devm_regulator_register(pca9450->dev, desc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(pca9450->dev, PTR_ERR(rdev),
					     "Failed to register regulator(%s)\n", desc->name);

		if (!strcmp(desc->name, "ldo5"))
			ldo5 = rdev;
	}

	if (pca9450->irq) {
		ret = devm_request_threaded_irq(pca9450->dev, pca9450->irq, NULL,
						pca9450_irq_handler,
						(IRQF_TRIGGER_FALLING | IRQF_ONESHOT),
						"pca9450-irq", pca9450);
		if (ret != 0)
			return dev_err_probe(pca9450->dev, ret, "Failed to request IRQ: %d\n",
					     pca9450->irq);

		/* Unmask all interrupt except PWRON/WDOG/RSVD */
		ret = regmap_update_bits(pca9450->regmap, PCA9450_REG_INT1_MSK,
					IRQ_VR_FLT1 | IRQ_VR_FLT2 | IRQ_LOWVSYS |
					IRQ_THERM_105 | IRQ_THERM_125,
					IRQ_PWRON | IRQ_WDOGB | IRQ_RSVD);
		if (ret)
			return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");
	}

	/* Clear PRESET_EN bit in BUCK123_DVS to use DVS registers */
	ret = regmap_clear_bits(pca9450->regmap, PCA9450_REG_BUCK123_DVS,
				BUCK123_PRESET_EN);
	if (ret)
		return dev_err_probe(&i2c->dev, ret,  "Failed to clear PRESET_EN bit\n");

	if (of_property_read_bool(i2c->dev.of_node, "nxp,wdog_b-warm-reset"))
		reset_ctrl = WDOG_B_CFG_WARM;
	else
		reset_ctrl = WDOG_B_CFG_COLD_LDO12;

	/* Set reset behavior on assertion of WDOG_B signal */
	ret = regmap_update_bits(pca9450->regmap, PCA9450_REG_RESET_CTRL,
				 WDOG_B_CFG_MASK, reset_ctrl);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Failed to set WDOG_B reset behavior\n");

	if (of_property_read_bool(i2c->dev.of_node, "nxp,i2c-lt-enable")) {
		/* Enable I2C Level Translator */
		ret = regmap_update_bits(pca9450->regmap, PCA9450_REG_CONFIG2,
					 I2C_LT_MASK, I2C_LT_ON_STANDBY_RUN);
		if (ret)
			return dev_err_probe(&i2c->dev, ret,
					     "Failed to enable I2C level translator\n");
	}

	/*
	 * For LDO5 we need to be able to check the status of the SD_VSEL input in
	 * order to know which control register is used. Most boards connect SD_VSEL
	 * to the VSELECT signal, so we can use the GPIO that is internally routed
	 * to this signal (if SION bit is set in IOMUX).
	 */
	pca9450->sd_vsel_gpio = gpiod_get_optional(&ldo5->dev, "sd-vsel", GPIOD_IN);
	if (IS_ERR(pca9450->sd_vsel_gpio)) {
		dev_err(&i2c->dev, "Failed to get SD_VSEL GPIO\n");
		return ret;
	}

	pca9450->sd_vsel_fixed_low =
		of_property_read_bool(ldo5->dev.of_node, "nxp,sd-vsel-fixed-low");

	if (devm_register_sys_off_handler(&i2c->dev, SYS_OFF_MODE_RESTART,
					  PCA9450_RESTART_HANDLER_PRIORITY,
					  pca9450_i2c_restart_handler, pca9450))
		dev_warn(&i2c->dev, "Failed to register restart handler\n");

	dev_info(&i2c->dev, "%s probed.\n",
		type == PCA9450_TYPE_PCA9450A ? "pca9450a" :
		(type == PCA9450_TYPE_PCA9451A ? "pca9451a" : "pca9450bc"));

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
	{
		.compatible = "nxp,pca9451a",
		.data = (void *)PCA9450_TYPE_PCA9451A,
	},
	{
		.compatible = "nxp,pca9452",
		.data = (void *)PCA9450_TYPE_PCA9452,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pca9450_of_match);

static struct i2c_driver pca9450_i2c_driver = {
	.driver = {
		.name = "nxp-pca9450",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = pca9450_of_match,
	},
	.probe = pca9450_i2c_probe,
};

module_i2c_driver(pca9450_i2c_driver);

MODULE_AUTHOR("Robin Gong <yibin.gong@nxp.com>");
MODULE_DESCRIPTION("NXP PCA9450 Power Management IC driver");
MODULE_LICENSE("GPL");
