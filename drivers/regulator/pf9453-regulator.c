// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 NXP.
 * NXP PF9453 pmic driver
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

struct pf9453_dvs_config {
	unsigned int run_reg; /* dvs0 */
	unsigned int run_mask;
	unsigned int standby_reg; /* dvs1 */
	unsigned int standby_mask;
};

struct pf9453_regulator_desc {
	struct regulator_desc desc;
	const struct pf9453_dvs_config dvs;
};

struct pf9453 {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *sd_vsel_gpio;
	int irq;
};

enum {
	PF9453_BUCK1 = 0,
	PF9453_BUCK2,
	PF9453_BUCK3,
	PF9453_BUCK4,
	PF9453_LDO1,
	PF9453_LDO2,
	PF9453_LDOSNVS,
	PF9453_REGULATOR_CNT
};

enum {
	PF9453_DVS_LEVEL_RUN = 0,
	PF9453_DVS_LEVEL_STANDBY,
	PF9453_DVS_LEVEL_DPSTANDBY,
	PF9453_DVS_LEVEL_MAX
};

#define PF9453_BUCK1_VOLTAGE_NUM	0x80
#define PF9453_BUCK2_VOLTAGE_NUM	0x80
#define PF9453_BUCK3_VOLTAGE_NUM	0x80
#define PF9453_BUCK4_VOLTAGE_NUM	0x80

#define PF9453_LDO1_VOLTAGE_NUM		0x65
#define PF9453_LDO2_VOLTAGE_NUM		0x3b
#define PF9453_LDOSNVS_VOLTAGE_NUM	0x59

enum {
	PF9453_REG_DEV_ID		= 0x00,
	PF9453_REG_OTP_VER		= 0x01,
	PF9453_REG_INT1			= 0x02,
	PF9453_REG_INT1_MASK		= 0x03,
	PF9453_REG_INT1_STATUS		= 0x04,
	PF9453_REG_VRFLT1_INT		= 0x05,
	PF9453_REG_VRFLT1_MASK		= 0x06,
	PF9453_REG_PWRON_STAT		= 0x07,
	PF9453_REG_RESET_CTRL		= 0x08,
	PF9453_REG_SW_RST		= 0x09,
	PF9453_REG_PWR_CTRL		= 0x0a,
	PF9453_REG_CONFIG1		= 0x0b,
	PF9453_REG_CONFIG2		= 0x0c,
	PF9453_REG_32K_CONFIG		= 0x0d,
	PF9453_REG_BUCK1CTRL		= 0x10,
	PF9453_REG_BUCK1OUT		= 0x11,
	PF9453_REG_BUCK2CTRL		= 0x14,
	PF9453_REG_BUCK2OUT		= 0x15,
	PF9453_REG_BUCK2OUT_STBY	= 0x1d,
	PF9453_REG_BUCK2OUT_MAX_LIMIT	= 0x1f,
	PF9453_REG_BUCK2OUT_MIN_LIMIT	= 0x20,
	PF9453_REG_BUCK3CTRL		= 0x21,
	PF9453_REG_BUCK3OUT		= 0x22,
	PF9453_REG_BUCK4CTRL		= 0x2e,
	PF9453_REG_BUCK4OUT		= 0x2f,
	PF9453_REG_LDO1OUT_L		= 0x36,
	PF9453_REG_LDO1CFG		= 0x37,
	PF9453_REG_LDO1OUT_H		= 0x38,
	PF9453_REG_LDOSNVS_CFG1		= 0x39,
	PF9453_REG_LDOSNVS_CFG2		= 0x3a,
	PF9453_REG_LDO2CFG		= 0x3b,
	PF9453_REG_LDO2OUT		= 0x3c,
	PF9453_REG_BUCK_POK		= 0x3d,
	PF9453_REG_LSW_CTRL1		= 0x40,
	PF9453_REG_LSW_CTRL2		= 0x41,
	PF9453_REG_LOCK			= 0x4e,
	PF9453_MAX_REG
};

#define PF9453_UNLOCK_KEY		0x5c
#define PF9453_LOCK_KEY			0x0

/* PF9453 BUCK ENMODE bits */
#define BUCK_ENMODE_OFF			0x00
#define BUCK_ENMODE_ONREQ		0x01
#define BUCK_ENMODE_ONREQ_STBY		0x02
#define BUCK_ENMODE_ONREQ_STBY_DPSTBY	0x03

/* PF9453 BUCK ENMODE bits */
#define LDO_ENMODE_OFF			0x00
#define LDO_ENMODE_ONREQ		0x01
#define LDO_ENMODE_ONREQ_STBY		0x02
#define LDO_ENMODE_ONREQ_STBY_DPSTBY	0x03

/* PF9453_REG_BUCK1_CTRL bits */
#define BUCK1_LPMODE			0x30
#define BUCK1_AD			0x08
#define BUCK1_FPWM			0x04
#define BUCK1_ENMODE_MASK		GENMASK(1, 0)

/* PF9453_REG_BUCK2_CTRL bits */
#define BUCK2_RAMP_MASK			GENMASK(7, 6)
#define BUCK2_RAMP_25MV			0x0
#define BUCK2_RAMP_12P5MV		0x1
#define BUCK2_RAMP_6P25MV		0x2
#define BUCK2_RAMP_3P125MV		0x3
#define BUCK2_LPMODE			0x30
#define BUCK2_AD			0x08
#define BUCK2_FPWM			0x04
#define BUCK2_ENMODE_MASK		GENMASK(1, 0)

/* PF9453_REG_BUCK3_CTRL bits */
#define BUCK3_LPMODE			0x30
#define BUCK3_AD			0x08
#define BUCK3_FPWM			0x04
#define BUCK3_ENMODE_MASK		GENMASK(1, 0)

/* PF9453_REG_BUCK4_CTRL bits */
#define BUCK4_LPMODE			0x30
#define BUCK4_AD			0x08
#define BUCK4_FPWM			0x04
#define BUCK4_ENMODE_MASK		GENMASK(1, 0)

/* PF9453_REG_BUCK123_PRESET_EN bit */
#define BUCK123_PRESET_EN		0x80

/* PF9453_BUCK1OUT bits */
#define BUCK1OUT_MASK			GENMASK(6, 0)

/* PF9453_BUCK2OUT bits */
#define BUCK2OUT_MASK			GENMASK(6, 0)
#define BUCK2OUT_STBY_MASK		GENMASK(6, 0)

/* PF9453_REG_BUCK3OUT bits */
#define BUCK3OUT_MASK			GENMASK(6, 0)

/* PF9453_REG_BUCK4OUT bits */
#define BUCK4OUT_MASK			GENMASK(6, 0)

/* PF9453_REG_LDO1_VOLT bits */
#define LDO1_EN_MASK			GENMASK(1, 0)
#define LDO1OUT_MASK			GENMASK(6, 0)

/* PF9453_REG_LDO2_VOLT bits */
#define LDO2_EN_MASK			GENMASK(1, 0)
#define LDO2OUT_MASK			GENMASK(6, 0)

/* PF9453_REG_LDOSNVS_VOLT bits */
#define LDOSNVS_EN_MASK			GENMASK(0, 0)
#define LDOSNVSCFG1_MASK		GENMASK(6, 0)

/* PF9453_REG_IRQ bits */
#define IRQ_RSVD			0x80
#define IRQ_RSTB			0x40
#define IRQ_ONKEY			0x20
#define IRQ_RESETKEY			0x10
#define IRQ_VR_FLT1			0x08
#define IRQ_LOWVSYS			0x04
#define IRQ_THERM_100			0x02
#define IRQ_THERM_80			0x01

/* PF9453_REG_RESET_CTRL bits */
#define WDOG_B_CFG_MASK			GENMASK(7, 6)
#define WDOG_B_CFG_NONE			0x00
#define WDOG_B_CFG_WARM			0x40
#define WDOG_B_CFG_COLD			0x80

/* PF9453_REG_CONFIG2 bits */
#define I2C_LT_MASK			GENMASK(1, 0)
#define I2C_LT_FORCE_DISABLE		0x00
#define I2C_LT_ON_STANDBY_RUN		0x01
#define I2C_LT_ON_RUN			0x02
#define I2C_LT_FORCE_ENABLE		0x03

static const struct regmap_range pf9453_status_range = {
	.range_min = PF9453_REG_INT1,
	.range_max = PF9453_REG_PWRON_STAT,
};

static const struct regmap_access_table pf9453_volatile_regs = {
	.yes_ranges = &pf9453_status_range,
	.n_yes_ranges = 1,
};

static const struct regmap_config pf9453_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &pf9453_volatile_regs,
	.max_register = PF9453_MAX_REG - 1,
	.cache_type = REGCACHE_RBTREE,
};

/*
 * BUCK2
 * BUCK2RAM[1:0] BUCK2 DVS ramp rate setting
 * 00: 25mV/1usec
 * 01: 25mV/2usec
 * 10: 25mV/4usec
 * 11: 25mV/8usec
 */
static const unsigned int pf9453_dvs_buck_ramp_table[] = {
	25000, 12500, 6250, 3125
};

static bool is_reg_protect(uint reg)
{
	switch (reg) {
	case PF9453_REG_BUCK1OUT:
	case PF9453_REG_BUCK2OUT:
	case PF9453_REG_BUCK3OUT:
	case PF9453_REG_BUCK4OUT:
	case PF9453_REG_LDO1OUT_L:
	case PF9453_REG_LDO1OUT_H:
	case PF9453_REG_LDO2OUT:
	case PF9453_REG_LDOSNVS_CFG1:
	case PF9453_REG_BUCK2OUT_MAX_LIMIT:
	case PF9453_REG_BUCK2OUT_MIN_LIMIT:
		return true;
	default:
		return false;
	}
}

static int pf9453_pmic_write(struct pf9453 *pf9453, unsigned int reg, u8 mask, unsigned int val)
{
	int ret = -EINVAL;
	u8 data, key;
	u32 rxBuf;

	/* If not updating entire register, perform a read-mod-write */
	data = val;
	key = PF9453_UNLOCK_KEY;

	if (mask != 0xffU) {
		/* Read data */
		ret = regmap_read(pf9453->regmap, reg, &rxBuf);
		if (ret) {
			dev_err(pf9453->dev, "Read reg=%0x error!\n", reg);
			return ret;
		}
		data = (val & mask) | (rxBuf & (~mask));
	}

	if (reg < PF9453_MAX_REG) {
		if (is_reg_protect(reg)) {
			ret = regmap_raw_write(pf9453->regmap, PF9453_REG_LOCK, &key, 1U);
			if (ret) {
				dev_err(pf9453->dev, "Write reg=%0x error!\n", reg);
				return ret;
			}

			ret = regmap_raw_write(pf9453->regmap, reg, &data, 1U);
			if (ret) {
				dev_err(pf9453->dev, "Write reg=%0x error!\n", reg);
				return ret;
			}

			key = PF9453_LOCK_KEY;
			ret = regmap_raw_write(pf9453->regmap, PF9453_REG_LOCK, &key, 1U);
			if (ret) {
				dev_err(pf9453->dev, "Write reg=%0x error!\n", reg);
				return ret;
			}
		} else {
			ret = regmap_raw_write(pf9453->regmap, reg, &data, 1U);
			if (ret) {
				dev_err(pf9453->dev, "Write reg=%0x error!\n", reg);
				return ret;
			}
		}
	}

	return ret;
}

/**
 * pf9453_regulator_enable_regmap for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their enable() operation, saving some code.
 */
static int pf9453_regulator_enable_regmap(struct regulator_dev *rdev)
{
	struct pf9453 *pf9453 = dev_get_drvdata(rdev->dev.parent);
	unsigned int val;

	if (rdev->desc->enable_is_inverted) {
		val = rdev->desc->disable_val;
	} else {
		val = rdev->desc->enable_val;
		if (!val)
			val = rdev->desc->enable_mask;
	}

	return pf9453_pmic_write(pf9453, rdev->desc->enable_reg, rdev->desc->enable_mask, val);
}

/**
 * pf9453_regulator_disable_regmap for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their disable() operation, saving some code.
 */
static int pf9453_regulator_disable_regmap(struct regulator_dev *rdev)
{
	struct pf9453 *pf9453 = dev_get_drvdata(rdev->dev.parent);
	unsigned int val;

	if (rdev->desc->enable_is_inverted) {
		val = rdev->desc->enable_val;
		if (!val)
			val = rdev->desc->enable_mask;
	} else {
		val = rdev->desc->disable_val;
	}

	return pf9453_pmic_write(pf9453, rdev->desc->enable_reg, rdev->desc->enable_mask, val);
}

/**
 * pf9453_regulator_set_voltage_sel_regmap for regmap users
 *
 * @rdev: regulator to operate on
 * @sel: Selector to set
 *
 * Regulators that use regmap for their register I/O can set the
 * vsel_reg and vsel_mask fields in their descriptor and then use this
 * as their set_voltage_vsel operation, saving some code.
 */
static int pf9453_regulator_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned int sel)
{
	struct pf9453 *pf9453 = dev_get_drvdata(rdev->dev.parent);
	int ret;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;
	ret = pf9453_pmic_write(pf9453, rdev->desc->vsel_reg, rdev->desc->vsel_mask, sel);
	if (ret)
		return ret;

	if (rdev->desc->apply_bit)
		ret = pf9453_pmic_write(pf9453, rdev->desc->apply_reg,
					rdev->desc->apply_bit, rdev->desc->apply_bit);
	return ret;
}

static int find_closest_bigger(unsigned int target, const unsigned int *table,
			       unsigned int num_sel, unsigned int *sel)
{
	unsigned int s, tmp, max, maxsel = 0;
	bool found = false;

	max = table[0];

	for (s = 0; s < num_sel; s++) {
		if (table[s] > max) {
			max = table[s];
			maxsel = s;
		}
		if (table[s] >= target) {
			if (!found || table[s] - target < tmp - target) {
				tmp = table[s];
				*sel = s;
				found = true;
				if (tmp == target)
					break;
			}
		}
	}

	if (!found) {
		*sel = maxsel;
		return -EINVAL;
	}

	return 0;
}

/**
 * pf9453_regulator_set_ramp_delay_regmap
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the ramp_reg
 * and ramp_mask fields in their descriptor and then use this as their
 * set_ramp_delay operation, saving some code.
 */
static int pf9453_regulator_set_ramp_delay_regmap(struct regulator_dev *rdev, int ramp_delay)
{
	struct pf9453 *pf9453 = dev_get_drvdata(rdev->dev.parent);
	unsigned int sel;
	int ret;

	if (WARN_ON(!rdev->desc->n_ramp_values || !rdev->desc->ramp_delay_table))
		return -EINVAL;

	ret = find_closest_bigger(ramp_delay, rdev->desc->ramp_delay_table,
				  rdev->desc->n_ramp_values, &sel);

	if (ret) {
		dev_warn(rdev_get_dev(rdev),
			 "Can't set ramp-delay %u, setting %u\n", ramp_delay,
			 rdev->desc->ramp_delay_table[sel]);
	}

	sel <<= ffs(rdev->desc->ramp_mask) - 1;

	return pf9453_pmic_write(pf9453, rdev->desc->ramp_reg,
				 rdev->desc->ramp_mask, sel);
}

static const struct regulator_ops pf9453_dvs_buck_regulator_ops = {
	.enable = pf9453_regulator_enable_regmap,
	.disable = pf9453_regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = pf9453_regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay	= pf9453_regulator_set_ramp_delay_regmap,
};

static const struct regulator_ops pf9453_buck_regulator_ops = {
	.enable = pf9453_regulator_enable_regmap,
	.disable = pf9453_regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = pf9453_regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static const struct regulator_ops pf9453_ldo_regulator_ops = {
	.enable = pf9453_regulator_enable_regmap,
	.disable = pf9453_regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = pf9453_regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

/*
 * BUCK1/3/4
 * 0.60 to 3.775V (25mV step)
 */
static const struct linear_range pf9453_buck134_volts[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x00, 0x7F, 25000),
};

/*
 * BUCK2
 * 0.60 to 2.1875V (12.5mV step)
 */
static const struct linear_range pf9453_buck2_volts[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x00, 0x7F, 12500),
};

/*
 * LDO1
 * 0.8 to 3.3V (25mV step)
 */
static const struct linear_range pf9453_ldo1_volts[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x00, 0x64, 25000),
};

/*
 * LDO2
 * 0.5 to 1.95V (25mV step)
 */
static const struct linear_range pf9453_ldo2_volts[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x00, 0x3A, 25000),
};

/*
 * LDOSNVS
 * 1.2 to 3.4V (25mV step)
 */
static const struct linear_range pf9453_ldosnvs_volts[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0x00, 0x58, 25000),
};

static int buck_set_dvs(const struct regulator_desc *desc,
			struct device_node *np, struct pf9453 *pf9453,
			char *prop, unsigned int reg, unsigned int mask)
{
	int ret, i;
	u32 uv;

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
			ret = pf9453_pmic_write(pf9453, reg, mask, i);
			break;
		}
	}

	if (ret == 0) {
		struct pf9453_regulator_desc *regulator = container_of(desc,
					struct pf9453_regulator_desc, desc);

		/* Enable DVS control through PMIC_STBY_REQ for this BUCK */
		ret = pf9453_pmic_write(pf9453, regulator->desc.enable_reg,
					BUCK2_LPMODE, BUCK2_LPMODE);
	}
	return ret;
}

static int pf9453_set_dvs_levels(struct device_node *np, const struct regulator_desc *desc,
				 struct regulator_config *cfg)
{
	struct pf9453_regulator_desc *data = container_of(desc, struct pf9453_regulator_desc, desc);
	struct pf9453 *pf9453 = dev_get_drvdata(cfg->dev);
	const struct pf9453_dvs_config *dvs = &data->dvs;
	unsigned int reg, mask;
	int i, ret = 0;
	char *prop;

	for (i = 0; i < PF9453_DVS_LEVEL_MAX; i++) {
		switch (i) {
		case PF9453_DVS_LEVEL_RUN:
			prop = "nxp,dvs-run-voltage";
			reg = dvs->run_reg;
			mask = dvs->run_mask;
			break;
		case PF9453_DVS_LEVEL_DPSTANDBY:
		case PF9453_DVS_LEVEL_STANDBY:
			prop = "nxp,dvs-standby-voltage";
			reg = dvs->standby_reg;
			mask = dvs->standby_mask;
			break;
		default:
			return -EINVAL;
		}

		ret = buck_set_dvs(desc, np, pf9453, prop, reg, mask);
		if (ret)
			break;
	}

	return ret;
}

static const struct pf9453_regulator_desc pf9453_regulators[] = {
	{
		.desc = {
			.name = "buck1",
			.of_match = of_match_ptr("BUCK1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF9453_BUCK1,
			.ops = &pf9453_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF9453_BUCK1_VOLTAGE_NUM,
			.linear_ranges = pf9453_buck134_volts,
			.n_linear_ranges = ARRAY_SIZE(pf9453_buck134_volts),
			.vsel_reg = PF9453_REG_BUCK1OUT,
			.vsel_mask = BUCK1OUT_MASK,
			.enable_reg = PF9453_REG_BUCK1CTRL,
			.enable_mask = BUCK1_ENMODE_MASK,
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "buck2",
			.of_match = of_match_ptr("BUCK2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF9453_BUCK2,
			.ops = &pf9453_dvs_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF9453_BUCK2_VOLTAGE_NUM,
			.linear_ranges = pf9453_buck2_volts,
			.n_linear_ranges = ARRAY_SIZE(pf9453_buck2_volts),
			.vsel_reg = PF9453_REG_BUCK2OUT,
			.vsel_mask = BUCK2OUT_MASK,
			.enable_reg = PF9453_REG_BUCK2CTRL,
			.enable_mask = BUCK2_ENMODE_MASK,
			.enable_val = BUCK_ENMODE_ONREQ,
			.ramp_reg = PF9453_REG_BUCK2CTRL,
			.ramp_mask = BUCK2_RAMP_MASK,
			.ramp_delay_table = pf9453_dvs_buck_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pf9453_dvs_buck_ramp_table),
			.owner = THIS_MODULE,
			.of_parse_cb = pf9453_set_dvs_levels,
		},
		.dvs = {
			.run_reg = PF9453_REG_BUCK2OUT,
			.run_mask = BUCK2OUT_MASK,
			.standby_reg = PF9453_REG_BUCK2OUT_STBY,
			.standby_mask = BUCK2OUT_STBY_MASK,
		},
	},
	{
		.desc = {
			.name = "buck3",
			.of_match = of_match_ptr("BUCK3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF9453_BUCK3,
			.ops = &pf9453_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF9453_BUCK3_VOLTAGE_NUM,
			.linear_ranges = pf9453_buck134_volts,
			.n_linear_ranges = ARRAY_SIZE(pf9453_buck134_volts),
			.vsel_reg = PF9453_REG_BUCK3OUT,
			.vsel_mask = BUCK3OUT_MASK,
			.enable_reg = PF9453_REG_BUCK3CTRL,
			.enable_mask = BUCK3_ENMODE_MASK,
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "buck4",
			.of_match = of_match_ptr("BUCK4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF9453_BUCK4,
			.ops = &pf9453_buck_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF9453_BUCK4_VOLTAGE_NUM,
			.linear_ranges = pf9453_buck134_volts,
			.n_linear_ranges = ARRAY_SIZE(pf9453_buck134_volts),
			.vsel_reg = PF9453_REG_BUCK4OUT,
			.vsel_mask = BUCK4OUT_MASK,
			.enable_reg = PF9453_REG_BUCK4CTRL,
			.enable_mask = BUCK4_ENMODE_MASK,
			.enable_val = BUCK_ENMODE_ONREQ,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo1",
			.of_match = of_match_ptr("LDO1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF9453_LDO1,
			.ops = &pf9453_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF9453_LDO1_VOLTAGE_NUM,
			.linear_ranges = pf9453_ldo1_volts,
			.n_linear_ranges = ARRAY_SIZE(pf9453_ldo1_volts),
			.vsel_reg = PF9453_REG_LDO1OUT_H,
			.vsel_mask = LDO1OUT_MASK,
			.enable_reg = PF9453_REG_LDO1CFG,
			.enable_mask = LDO1_EN_MASK,
			.enable_val = LDO_ENMODE_ONREQ,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo2",
			.of_match = of_match_ptr("LDO2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF9453_LDO2,
			.ops = &pf9453_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF9453_LDO2_VOLTAGE_NUM,
			.linear_ranges = pf9453_ldo2_volts,
			.n_linear_ranges = ARRAY_SIZE(pf9453_ldo2_volts),
			.vsel_reg = PF9453_REG_LDO2OUT,
			.vsel_mask = LDO2OUT_MASK,
			.enable_reg = PF9453_REG_LDO2CFG,
			.enable_mask = LDO2_EN_MASK,
			.enable_val = LDO_ENMODE_ONREQ,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldosnvs",
			.of_match = of_match_ptr("LDO-SNVS"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF9453_LDOSNVS,
			.ops = &pf9453_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF9453_LDOSNVS_VOLTAGE_NUM,
			.linear_ranges = pf9453_ldosnvs_volts,
			.n_linear_ranges = ARRAY_SIZE(pf9453_ldosnvs_volts),
			.vsel_reg = PF9453_REG_LDOSNVS_CFG1,
			.vsel_mask = LDOSNVSCFG1_MASK,
			.enable_reg = PF9453_REG_LDOSNVS_CFG2,
			.enable_mask = LDOSNVS_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{ }
};

static irqreturn_t pf9453_irq_handler(int irq, void *data)
{
	struct pf9453 *pf9453 = data;
	struct regmap *regmap = pf9453->regmap;
	unsigned int status;
	int ret;

	ret = regmap_read(regmap, PF9453_REG_INT1, &status);
	if (ret < 0) {
		dev_err(pf9453->dev, "Failed to read INT1(%d)\n", ret);
		return IRQ_NONE;
	}

	if (status & IRQ_RSTB)
		dev_warn(pf9453->dev, "IRQ_RSTB interrupt.\n");

	if (status & IRQ_ONKEY)
		dev_warn(pf9453->dev, "IRQ_ONKEY interrupt.\n");

	if (status & IRQ_VR_FLT1)
		dev_warn(pf9453->dev, "VRFLT1 interrupt.\n");

	if (status & IRQ_RESETKEY)
		dev_warn(pf9453->dev, "IRQ_RESETKEY interrupt.\n");

	if (status & IRQ_LOWVSYS)
		dev_warn(pf9453->dev, "LOWVSYS interrupt.\n");

	if (status & IRQ_THERM_100)
		dev_warn(pf9453->dev, "IRQ_THERM_100 interrupt.\n");

	if (status & IRQ_THERM_80)
		dev_warn(pf9453->dev, "IRQ_THERM_80 interrupt.\n");

	return IRQ_HANDLED;
}

static int pf9453_i2c_probe(struct i2c_client *i2c)
{
	const struct pf9453_regulator_desc *regulator_desc = of_device_get_match_data(&i2c->dev);
	struct regulator_config config = { };
	unsigned int reset_ctrl;
	unsigned int device_id;
	struct pf9453 *pf9453;
	int ret;

	if (!i2c->irq)
		return dev_err_probe(&i2c->dev, -EINVAL, "No IRQ configured?\n");

	pf9453 = devm_kzalloc(&i2c->dev, sizeof(struct pf9453), GFP_KERNEL);
	if (!pf9453)
		return -ENOMEM;

	pf9453->regmap = devm_regmap_init_i2c(i2c, &pf9453_regmap_config);
	if (IS_ERR(pf9453->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(pf9453->regmap),
				     "regmap initialization failed\n");

	pf9453->irq = i2c->irq;
	pf9453->dev = &i2c->dev;

	dev_set_drvdata(&i2c->dev, pf9453);

	ret = regmap_read(pf9453->regmap, PF9453_REG_DEV_ID, &device_id);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Read device id error\n");

	/* Check your board and dts for match the right pmic */
	if ((device_id >> 4) != 0xb)
		return dev_err_probe(&i2c->dev, -EINVAL, "Device id(%x) mismatched\n",
				     device_id >> 4);

	while (regulator_desc->desc.name) {
		const struct regulator_desc *desc;
		struct regulator_dev *rdev;

		desc = &regulator_desc->desc;

		config.regmap = pf9453->regmap;
		config.dev = pf9453->dev;

		rdev = devm_regulator_register(pf9453->dev, desc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(pf9453->dev, PTR_ERR(rdev),
					     "Failed to register regulator(%s)\n", desc->name);

		regulator_desc++;
	}

	ret = devm_request_threaded_irq(pf9453->dev, pf9453->irq, NULL, pf9453_irq_handler,
					(IRQF_TRIGGER_FALLING | IRQF_ONESHOT),
					"pf9453-irq", pf9453);
	if (ret)
		return dev_err_probe(pf9453->dev, ret, "Failed to request IRQ: %d\n", pf9453->irq);

	/* Unmask all interrupt except PWRON/WDOG/RSVD */
	ret = pf9453_pmic_write(pf9453, PF9453_REG_INT1_MASK,
				IRQ_ONKEY | IRQ_RESETKEY | IRQ_RSTB | IRQ_VR_FLT1
				| IRQ_LOWVSYS | IRQ_THERM_100 | IRQ_THERM_80, IRQ_RSVD);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");

	if (of_property_read_bool(i2c->dev.of_node, "nxp,wdog_b-warm-reset"))
		reset_ctrl = WDOG_B_CFG_WARM;
	else
		reset_ctrl = WDOG_B_CFG_COLD;

	/* Set reset behavior on assertion of WDOG_B signal */
	ret = pf9453_pmic_write(pf9453, PF9453_REG_RESET_CTRL, WDOG_B_CFG_MASK, reset_ctrl);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Failed to set WDOG_B reset behavior\n");

	/*
	 * The driver uses the LDO1OUT_H register to control the LDO1 regulator.
	 * This is only valid if the SD_VSEL input of the PMIC is high. Let's
	 * check if the pin is available as GPIO and set it to high.
	 */
	pf9453->sd_vsel_gpio = gpiod_get_optional(pf9453->dev, "sd-vsel", GPIOD_OUT_HIGH);

	if (IS_ERR(pf9453->sd_vsel_gpio))
		return dev_err_probe(&i2c->dev, PTR_ERR(pf9453->sd_vsel_gpio),
				     "Failed to get SD_VSEL GPIO\n");

	return 0;
}

static const struct of_device_id pf9453_of_match[] = {
	{
		.compatible = "nxp,pf9453",
		.data = pf9453_regulators,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pf9453_of_match);

static struct i2c_driver pf9453_i2c_driver = {
	.driver = {
		.name = "nxp-pf9453",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = pf9453_of_match,
	},
	.probe = pf9453_i2c_probe,
};

module_i2c_driver(pf9453_i2c_driver);

MODULE_AUTHOR("Joy Zou <joy.zou@nxp.com>");
MODULE_DESCRIPTION("NXP PF9453 Power Management IC driver");
MODULE_LICENSE("GPL");
