// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *          Daniel Willerud <daniel.willerud@stericsson.com> for ST-Ericsson
 *
 * AB8500 peripheral regulators
 *
 * AB8500 supports the following regulators:
 *   VSMPS1/2/3, VARM, VAPE, VMOD, VAUX1/2/3, VINTCORE, VTVOUT,
 *   VUSB, VAUDIO, VAMIC1/2, VDMIC, VANA
 *
 * AB8505 supports the following regulators:
 *   VSMPSA/B/C/M, VSAFE, VARM, VAUX1/2/3/4/5/6, VINTCORE,
 *   VADC, VUSB, VAUDIO, VAMIC1/2, VDMIC, VANA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>

/* AB8500 regulators */
enum ab8500_regulator_id {
	AB8500_LDO_AUX1,
	AB8500_LDO_AUX2,
	AB8500_LDO_AUX3,
	AB8500_LDO_INTCORE,
	AB8500_LDO_TVOUT,
	AB8500_LDO_AUDIO,
	AB8500_LDO_ANAMIC1,
	AB8500_LDO_ANAMIC2,
	AB8500_LDO_DMIC,
	AB8500_LDO_ANA,
	AB8500_BUCK_SMPS1,
	AB8500_BUCK_SMPS2,
	AB8500_BUCK_SMPS3,
	AB8500_BUCK_ARM,
	AB8500_BUCK_APE,
	AB8500_BUCK_MOD,
	AB8500_NUM_REGULATORS,
};

/* AB8505 regulators */
enum ab8505_regulator_id {
	AB8505_LDO_AUX1,
	AB8505_LDO_AUX2,
	AB8505_LDO_AUX3,
	AB8505_LDO_AUX4,
	AB8505_LDO_AUX5,
	AB8505_LDO_AUX6,
	AB8505_LDO_INTCORE,
	AB8505_LDO_ADC,
	AB8505_LDO_AUDIO,
	AB8505_LDO_ANAMIC1,
	AB8505_LDO_ANAMIC2,
	AB8505_LDO_AUX8,
	AB8505_LDO_ANA,
	AB8505_BUCK_SMPSA,
	AB8505_BUCK_SMPSB,
	AB8505_BUCK_SAFE,
	AB8505_BUCK_ARM,
	AB8505_BUCK_SMPSC,
	AB8505_BUCK_SMPSM,
	AB8505_NUM_REGULATORS,
};

/* AB8500 registers */
enum ab8500_regulator_reg {
	AB8500_REGUREQUESTCTRL2,
	AB8500_REGUREQUESTCTRL3,
	AB8500_REGUREQUESTCTRL4,
	AB8500_REGUSYSCLKREQ1HPVALID1,
	AB8500_REGUSYSCLKREQ1HPVALID2,
	AB8500_REGUHWHPREQ1VALID1,
	AB8500_REGUHWHPREQ1VALID2,
	AB8500_REGUHWHPREQ2VALID1,
	AB8500_REGUHWHPREQ2VALID2,
	AB8500_REGUSWHPREQVALID1,
	AB8500_REGUSWHPREQVALID2,
	AB8500_REGUSYSCLKREQVALID1,
	AB8500_REGUSYSCLKREQVALID2,
	AB8500_REGUMISC1,
	AB8500_VAUDIOSUPPLY,
	AB8500_REGUCTRL1VAMIC,
	AB8500_VPLLVANAREGU,
	AB8500_VREFDDR,
	AB8500_EXTSUPPLYREGU,
	AB8500_VAUX12REGU,
	AB8500_VRF1VAUX3REGU,
	AB8500_VAUX1SEL,
	AB8500_VAUX2SEL,
	AB8500_VRF1VAUX3SEL,
	AB8500_REGUCTRL2SPARE,
	AB8500_REGUCTRLDISCH,
	AB8500_REGUCTRLDISCH2,
	AB8500_NUM_REGULATOR_REGISTERS,
};

/* AB8505 registers */
enum ab8505_regulator_reg {
	AB8505_REGUREQUESTCTRL1,
	AB8505_REGUREQUESTCTRL2,
	AB8505_REGUREQUESTCTRL3,
	AB8505_REGUREQUESTCTRL4,
	AB8505_REGUSYSCLKREQ1HPVALID1,
	AB8505_REGUSYSCLKREQ1HPVALID2,
	AB8505_REGUHWHPREQ1VALID1,
	AB8505_REGUHWHPREQ1VALID2,
	AB8505_REGUHWHPREQ2VALID1,
	AB8505_REGUHWHPREQ2VALID2,
	AB8505_REGUSWHPREQVALID1,
	AB8505_REGUSWHPREQVALID2,
	AB8505_REGUSYSCLKREQVALID1,
	AB8505_REGUSYSCLKREQVALID2,
	AB8505_REGUVAUX4REQVALID,
	AB8505_REGUMISC1,
	AB8505_VAUDIOSUPPLY,
	AB8505_REGUCTRL1VAMIC,
	AB8505_VSMPSAREGU,
	AB8505_VSMPSBREGU,
	AB8505_VSAFEREGU, /* NOTE! PRCMU register */
	AB8505_VPLLVANAREGU,
	AB8505_EXTSUPPLYREGU,
	AB8505_VAUX12REGU,
	AB8505_VRF1VAUX3REGU,
	AB8505_VSMPSASEL1,
	AB8505_VSMPSASEL2,
	AB8505_VSMPSASEL3,
	AB8505_VSMPSBSEL1,
	AB8505_VSMPSBSEL2,
	AB8505_VSMPSBSEL3,
	AB8505_VSAFESEL1, /* NOTE! PRCMU register */
	AB8505_VSAFESEL2, /* NOTE! PRCMU register */
	AB8505_VSAFESEL3, /* NOTE! PRCMU register */
	AB8505_VAUX1SEL,
	AB8505_VAUX2SEL,
	AB8505_VRF1VAUX3SEL,
	AB8505_VAUX4REQCTRL,
	AB8505_VAUX4REGU,
	AB8505_VAUX4SEL,
	AB8505_REGUCTRLDISCH,
	AB8505_REGUCTRLDISCH2,
	AB8505_REGUCTRLDISCH3,
	AB8505_CTRLVAUX5,
	AB8505_CTRLVAUX6,
	AB8505_NUM_REGULATOR_REGISTERS,
};

/**
 * struct ab8500_shared_mode - is used when mode is shared between
 * two regulators.
 * @shared_regulator: pointer to the other sharing regulator
 * @lp_mode_req: low power mode requested by this regulator
 */
struct ab8500_shared_mode {
	struct ab8500_regulator_info *shared_regulator;
	bool lp_mode_req;
};

/**
 * struct ab8500_regulator_info - ab8500 regulator information
 * @dev: device pointer
 * @desc: regulator description
 * @shared_mode: used when mode is shared between two regulators
 * @load_lp_uA: maximum load in idle (low power) mode
 * @update_bank: bank to control on/off
 * @update_reg: register to control on/off
 * @update_mask: mask to enable/disable and set mode of regulator
 * @enable_mask: optional mask for an enable bit separate from the mode bit
 * @update_val: bits holding the regulator current mode
 * @update_val_idle: bits to enable the regulator in idle (low power) mode
 * @update_val_normal: bits to enable the regulator in normal (high power) mode
 * @mode_bank: bank with location of mode register
 * @mode_reg: mode register
 * @mode_mask: mask for setting mode
 * @mode_val_idle: mode setting for low power
 * @mode_val_normal: mode setting for normal power
 * @voltage_bank: bank to control regulator voltage
 * @voltage_reg: first register containing a selectable regulator voltage
 * @voltage_mask: mask to control regulator voltage
 * @expand_register: additional register used to select an extra voltage
 * @voltage_ctrl_bank: bank containing the voltage selector control
 * @voltage_ctrl_reg: register containing the voltage selector control
 * @voltage_ctrl_mask: mask selecting one of the first voltage registers
 * @voltage_ext_ctrl_bank: bank containing the extended selector control
 * @voltage_ext_ctrl_reg: register containing the extended selector control
 * @voltage_ext_ctrl_mask: mask selecting one of the extended voltage registers
 * @voltage_ext_reg: first extended voltage register
 * @voltage_ext_regs: number of extended voltage registers
 */
struct ab8500_regulator_info {
	struct device		*dev;
	struct regulator_desc	desc;
	struct ab8500_shared_mode *shared_mode;
	int load_lp_uA;
	u8 update_bank;
	u8 update_reg;
	u8 update_mask;
	u8 enable_mask;
	u8 update_val;
	u8 update_val_idle;
	u8 update_val_normal;
	u8 mode_bank;
	u8 mode_reg;
	u8 mode_mask;
	u8 mode_val_idle;
	u8 mode_val_normal;
	u8 voltage_bank;
	u8 voltage_reg;
	u8 voltage_mask;
	struct {
		u8 voltage_limit;
		u8 voltage_bank;
		u8 voltage_reg;
		u8 voltage_mask;
	} expand_register;
	u8 voltage_ctrl_bank;
	u8 voltage_ctrl_reg;
	u8 voltage_ctrl_mask;
	u8 voltage_ext_ctrl_bank;
	u8 voltage_ext_ctrl_reg;
	u8 voltage_ext_ctrl_mask;
	u8 voltage_ext_reg;
	u8 voltage_ext_regs;
};

/* voltage tables for the vauxn/vintcore supplies */
static const unsigned int ldo_vauxn_voltages[] = {
	1100000,
	1200000,
	1300000,
	1400000,
	1500000,
	1800000,
	1850000,
	1900000,
	2500000,
	2650000,
	2700000,
	2750000,
	2800000,
	2900000,
	3000000,
	3300000,
};

static const unsigned int ldo_vaux3_voltages[] = {
	1200000,
	1500000,
	1800000,
	2100000,
	2500000,
	2750000,
	2790000,
	2910000,
};

static const unsigned int ldo_vaux3_ab8505_voltages[] = {
	1200000,
	1500000,
	1800000,
	2100000,
	2500000,
	2750000,
	2790000,
	2910000,
	3050000,
};

static const unsigned int ldo_vaux56_voltages[] = {
	1800000,
	1050000,
	1100000,
	1200000,
	1500000,
	2200000,
	2500000,
	2790000,
};

static const struct linear_range ldo_vintcore_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 6, 25000),
};

static const struct linear_range ldo_vintcore_ab8505_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 6, 25000),
	REGULATOR_LINEAR_RANGE(1350000, 7, 7, 0),
};

static const unsigned int fixed_1200000_voltage[] = {
	1200000,
};

static const unsigned int fixed_1800000_voltage[] = {
	1800000,
};

static const unsigned int fixed_2000000_voltage[] = {
	2000000,
};

static const unsigned int fixed_2050000_voltage[] = {
	2050000,
};

static const unsigned int ldo_vana_voltages[] = {
	1200000,
	1050000,
	1075000,
	1100000,
	1125000,
	1150000,
	1175000,
	1225000,
};

static const struct linear_range ldo_vaudio_ranges[] = {
	REGULATOR_LINEAR_RANGE(2000000, 0, 6, 100000),
	/* Duplicated in Vaudio and IsoUicc Control register. */
	REGULATOR_LINEAR_RANGE(2600000, 7, 7, 0),
};

/*
 * AB8505 buck ranges except VARM are selected by OTP.  The supported
 * platforms use the AB8500-compatible profiles for VSMPSA/B and the low
 * profiles for VSAFE, VSMPSC and VSMPSM.
 */
static const struct linear_range buck_low_voltages[] = {
	REGULATOR_LINEAR_RANGE(700000, 0, 53, 12500),
	REGULATOR_LINEAR_RANGE(1362500, 54, 63, 0),
};

/* VSMPS3 and VSAFE have a 7-bit selector, but the same low range. */
static const struct linear_range buck_low_7bit_voltages[] = {
	REGULATOR_LINEAR_RANGE(700000, 0, 53, 12500),
	REGULATOR_LINEAR_RANGE(1362500, 54, 127, 0),
};

/* AB8505 VARM uses a separate 0.6 V to 1.39375 V selector range. */
static const struct linear_range ab8505_buck_arm_voltages[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 127, 6250),
};

/* VSMPS1 and the VSMPSA AB8500-compatible profile clamp to this range. */
static const struct linear_range buck_smps1_voltages[] = {
	REGULATOR_LINEAR_RANGE(1100000, 0, 32, 0),
	REGULATOR_LINEAR_RANGE(1112500, 33, 48, 12500),
	REGULATOR_LINEAR_RANGE(1300000, 49, 63, 0),
};

/* VSMPS2 and the VSMPSB AB8500-compatible profile clamp to this range. */
static const struct linear_range buck_smps2_voltages[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 57, 0),
	REGULATOR_LINEAR_RANGE(1812500, 58, 63, 12500),
};

static DEFINE_MUTEX(shared_mode_mutex);
static struct ab8500_shared_mode ldo_anamic1_shared;
static struct ab8500_shared_mode ldo_anamic2_shared;

static int ab8500_regulator_enable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, info->update_val);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't set enable bits for regulator\n");
		return ret;
	}

	dev_vdbg(rdev_get_dev(rdev),
		"%s-enable (bank, reg, mask, value): 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, info->update_val);

	return ret;
}

static int ab8500_regulator_disable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, 0x0);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't set disable bits for regulator\n");
		return ret;
	}

	dev_vdbg(rdev_get_dev(rdev),
		"%s-disable (bank, reg, mask, value): 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, 0x0);

	return ret;
}

static int ab8500_regulator_get_enable_value(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_get_register_interruptible(info->dev,
		info->update_bank, info->update_reg, &regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read 0x%x register\n", info->update_reg);
		return ret;
	}

	dev_vdbg(rdev_get_dev(rdev),
		"%s-is_enabled (bank, reg, mask, value): 0x%x, 0x%x, 0x%x,"
		" 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, regval);

	return regval & info->update_mask;
}

static int ab8500_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 enable_mask;
	int ret;

	ret = ab8500_regulator_get_enable_value(rdev);
	if (ret < 0)
		return ret;

	enable_mask = info->enable_mask ? info->enable_mask : info->update_mask;

	return !!(ret & enable_mask);
}

static int ab8500_buck_enable(struct regulator_dev *rdev)
{
	int ret;

	/* Keep an OTP-selected hardware or low-power mode intact. */
	ret = ab8500_regulator_is_enabled(rdev);
	if (ret)
		return ret < 0 ? ret : 0;

	return ab8500_regulator_enable(rdev);
}

static int ab8500_buck_init(struct regulator_dev *rdev,
			    struct regulator_config *config)
{
	struct ab8500_regulator_info *info = config->driver_data;
	int ret;

	ret = ab8500_regulator_get_enable_value(rdev);
	if (ret <= 0)
		return ret;

	/* Report forced LP accurately; HP and hardware control are normal mode. */
	if (ret == info->update_val_idle)
		info->update_val = info->update_val_idle;
	else
		info->update_val = info->update_val_normal;

	/*
	 * The SMPS enable state is selected by OTP.  An enabled rail may
	 * supply discrete board components which are not represented as
	 * regulator consumers, so keep it out of the unused-regulator sweep.
	 */
	rdev->constraints->boot_on = true;
	rdev->constraints->always_on = true;
	rdev->constraints->valid_ops_mask &= ~REGULATOR_CHANGE_STATUS;

	dev_dbg(config->dev, "%s: preserving OTP-enabled state\n",
		info->desc.name);

	return 0;
}

static unsigned int ab8500_regulator_get_optimum_mode(
		struct regulator_dev *rdev, int input_uV,
		int output_uV, int load_uA)
{
	unsigned int mode;

	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	if (load_uA <= info->load_lp_uA)
		mode = REGULATOR_MODE_IDLE;
	else
		mode = REGULATOR_MODE_NORMAL;

	return mode;
}

static int ab8500_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	int enabled, ret = 0;
	u8 bank, reg, mask, val;
	bool lp_mode_req = false;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	guard(mutex)(&shared_mode_mutex);

	if (info->mode_mask) {
		bank = info->mode_bank;
		reg = info->mode_reg;
		mask = info->mode_mask;
	} else {
		bank = info->update_bank;
		reg = info->update_reg;
		mask = info->update_mask;
	}

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		if (info->shared_mode)
			lp_mode_req = false;

		if (info->mode_mask)
			val = info->mode_val_normal;
		else
			val = info->update_val_normal;
		break;
	case REGULATOR_MODE_IDLE:
		if (info->shared_mode) {
			struct ab8500_regulator_info *shared_regulator;

			shared_regulator = info->shared_mode->shared_regulator;
			if (!shared_regulator->shared_mode->lp_mode_req) {
				/* Other regulator prevent LP mode */
				info->shared_mode->lp_mode_req = true;
				return 0;
			}

			lp_mode_req = true;
		}

		if (info->mode_mask)
			val = info->mode_val_idle;
		else
			val = info->update_val_idle;
		break;
	default:
		return -EINVAL;
	}

	if (info->mode_mask) {
		enabled = 1;
	} else {
		enabled = ab8500_regulator_is_enabled(rdev);
		if (enabled < 0)
			return enabled;
	}

	if (enabled) {
		ret = abx500_mask_and_set_register_interruptible(info->dev,
			bank, reg, mask, val);
		if (ret < 0) {
			dev_err(rdev_get_dev(rdev),
				"couldn't set regulator mode\n");
			return ret;
		}

		dev_vdbg(rdev_get_dev(rdev),
			"%s-set_mode (bank, reg, mask, value): "
			"0x%x, 0x%x, 0x%x, 0x%x\n",
			info->desc.name, bank, reg,
			mask, val);
	}

	if (!info->mode_mask)
		info->update_val = val;

	if (info->shared_mode)
		info->shared_mode->lp_mode_req = lp_mode_req;

	return ret;
}

static unsigned int ab8500_regulator_get_mode(struct regulator_dev *rdev)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;
	u8 val;
	u8 val_normal;
	u8 val_idle;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	/* Need special handling for shared mode */
	if (info->shared_mode) {
		if (info->shared_mode->lp_mode_req)
			return REGULATOR_MODE_IDLE;
		else
			return REGULATOR_MODE_NORMAL;
	}

	if (info->mode_mask) {
		/* Dedicated register for handling mode */
		ret = abx500_get_register_interruptible(info->dev,
		info->mode_bank, info->mode_reg, &val);
		val = val & info->mode_mask;

		val_normal = info->mode_val_normal;
		val_idle = info->mode_val_idle;
	} else {
		/* Mode register same as enable register */
		val = info->update_val;
		val_normal = info->update_val_normal;
		val_idle = info->update_val_idle;
	}

	if (val == val_normal)
		ret = REGULATOR_MODE_NORMAL;
	else if (val == val_idle)
		ret = REGULATOR_MODE_IDLE;
	else
		ret = -EINVAL;

	return ret;
}

static int ab8500_regulator_get_voltage_reg(struct regulator_dev *rdev,
					    u8 *voltage_reg)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;
	unsigned int selector;
	int ret;

	if (info->voltage_ext_ctrl_mask) {
		ret = abx500_get_register_interruptible(info->dev,
							info->voltage_ext_ctrl_bank,
							info->voltage_ext_ctrl_reg, &regval);
		if (ret < 0)
			return ret;

		selector = (regval & info->voltage_ext_ctrl_mask) >>
			(ffs(info->voltage_ext_ctrl_mask) - 1);
		if (selector) {
			selector = min_t(unsigned int, selector,
					 info->voltage_ext_regs);
			*voltage_reg = info->voltage_ext_reg + selector - 1;
			return 0;
		}
	}

	if (!info->voltage_ctrl_mask) {
		*voltage_reg = info->voltage_reg;
		return 0;
	}

	ret = abx500_get_register_interruptible(info->dev,
						info->voltage_ctrl_bank,
						info->voltage_ctrl_reg, &regval);
	if (ret < 0)
		return ret;

	/* The three hardware selector layouts all use consecutive registers. */
	switch (info->voltage_ctrl_mask) {
	case 0x0c:
		selector = min((unsigned int)((regval & 0x0c) >> 2), 2U);
		break;
	case 0x24:
		selector = regval & BIT(5) ? 2 : !!(regval & BIT(2));
		break;
	case 0x04:
		selector = !!(regval & BIT(2));
		break;
	default:
		return -EINVAL;
	}

	*voltage_reg = info->voltage_reg + selector;

	return 0;
}

static int ab8500_regulator_get_voltage_sel(struct regulator_dev *rdev)
{
	int ret, voltage_shift;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval, voltage_reg;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	voltage_shift = ffs(info->voltage_mask) - 1;

	ret = ab8500_regulator_get_voltage_reg(rdev, &voltage_reg);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read voltage selector control\n");
		return ret;
	}

	ret = abx500_get_register_interruptible(info->dev,
			info->voltage_bank, voltage_reg, &regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read voltage reg for regulator\n");
		return ret;
	}

	dev_vdbg(rdev_get_dev(rdev),
		"%s-get_voltage (bank, reg, mask, shift, value): "
		"0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->voltage_bank,
		voltage_reg, info->voltage_mask,
		voltage_shift, regval);

	return (regval & info->voltage_mask) >> voltage_shift;
}

static int ab8500_regulator_set_voltage_sel(struct regulator_dev *rdev,
					    unsigned selector)
{
	int ret, voltage_shift;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval, voltage_reg;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	voltage_shift = ffs(info->voltage_mask) - 1;

	ret = ab8500_regulator_get_voltage_reg(rdev, &voltage_reg);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read voltage selector control\n");
		return ret;
	}

	/* set the registers for the request */
	regval = (u8)selector << voltage_shift;
	ret = abx500_mask_and_set_register_interruptible(info->dev,
			info->voltage_bank, voltage_reg,
			info->voltage_mask, regval);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
		"couldn't set voltage reg for regulator\n");

	dev_vdbg(rdev_get_dev(rdev),
		"%s-set_voltage (bank, reg, mask, value): 0x%x, 0x%x, 0x%x,"
		" 0x%x\n",
		info->desc.name, info->voltage_bank, voltage_reg,
		info->voltage_mask, regval);

	return ret;
}

static int ab8500_regulator_get_voltage_sel_expand(struct regulator_dev *rdev)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;
	int ret;

	if (!info)
		return -EINVAL;

	ret = abx500_get_register_interruptible(info->dev,
			info->expand_register.voltage_bank,
			info->expand_register.voltage_reg, &regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read voltage expand reg for regulator\n");
		return ret;
	}

	if (regval & info->expand_register.voltage_mask)
		return info->expand_register.voltage_limit;

	return ab8500_regulator_get_voltage_sel(rdev);
}

static int ab8500_regulator_set_voltage_sel_expand(struct regulator_dev *rdev,
						   unsigned int selector)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;
	int ret;

	if (!info)
		return -EINVAL;

	if (selector > info->expand_register.voltage_limit)
		return -EINVAL;

	if (selector < info->expand_register.voltage_limit) {
		ret = ab8500_regulator_set_voltage_sel(rdev, selector);
		if (ret < 0)
			return ret;

		regval = 0;
	} else {
		regval = info->expand_register.voltage_mask;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
			info->expand_register.voltage_bank,
			info->expand_register.voltage_reg,
			info->expand_register.voltage_mask, regval);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
			"couldn't set voltage expand reg for regulator\n");

	return ret;
}

static const struct regulator_ops ab8500_regulator_volt_mode_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode		= ab8500_regulator_set_mode,
	.get_mode		= ab8500_regulator_get_mode,
	.get_voltage_sel 	= ab8500_regulator_get_voltage_sel,
	.set_voltage_sel	= ab8500_regulator_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_table,
};

static const struct regulator_ops ab8500_regulator_volt_mode_expand_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode		= ab8500_regulator_set_mode,
	.get_mode		= ab8500_regulator_get_mode,
	.get_voltage_sel	= ab8500_regulator_get_voltage_sel_expand,
	.set_voltage_sel	= ab8500_regulator_set_voltage_sel_expand,
	.list_voltage		= regulator_list_voltage_table,
};

static const struct regulator_ops ab8500_regulator_linear_range_volt_mode_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode		= ab8500_regulator_set_mode,
	.get_mode		= ab8500_regulator_get_mode,
	.get_voltage_sel	= ab8500_regulator_get_voltage_sel,
	.set_voltage_sel	= ab8500_regulator_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_ops ab8500_regulator_linear_range_volt_ops = {
	.enable		= ab8500_regulator_enable,
	.disable	= ab8500_regulator_disable,
	.is_enabled	= ab8500_regulator_is_enabled,
	.get_voltage_sel = ab8500_regulator_get_voltage_sel,
	.set_voltage_sel = ab8500_regulator_set_voltage_sel,
	.list_voltage	= regulator_list_voltage_linear_range,
	.map_voltage	= regulator_map_voltage_linear_range,
};

static const struct regulator_ops ab8500_buck_ops = {
	.enable			= ab8500_buck_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode		= ab8500_regulator_set_mode,
	.get_mode		= ab8500_regulator_get_mode,
	.get_voltage_sel	= ab8500_regulator_get_voltage_sel,
	.set_voltage_sel	= ab8500_regulator_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_ops ab8500_buck_voltage_ops = {
	.get_voltage_sel	= ab8500_regulator_get_voltage_sel,
	.set_voltage_sel	= ab8500_regulator_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_ops ab8500_regulator_mode_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode		= ab8500_regulator_set_mode,
	.get_mode		= ab8500_regulator_get_mode,
	.list_voltage		= regulator_list_voltage_table,
};

static const struct regulator_ops ab8500_regulator_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.list_voltage		= regulator_list_voltage_table,
};

static const struct regulator_ops ab8500_regulator_anamic_mode_ops = {
	.enable		= ab8500_regulator_enable,
	.disable	= ab8500_regulator_disable,
	.is_enabled	= ab8500_regulator_is_enabled,
	.set_mode	= ab8500_regulator_set_mode,
	.get_mode	= ab8500_regulator_get_mode,
	.list_voltage	= regulator_list_voltage_table,
};

/* AB8500 regulator information */
static struct ab8500_regulator_info
		ab8500_regulator_info[AB8500_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask
	 */
	[AB8500_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
			.volt_table	= ldo_vauxn_voltages,
			.enable_time	= 200,
			.supply_name    = "vin",
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
	},
	[AB8500_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
			.volt_table	= ldo_vauxn_voltages,
			.enable_time	= 200,
			.supply_name    = "vin",
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
	},
	[AB8500_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_voltages),
			.volt_table	= ldo_vaux3_voltages,
			.enable_time	= 450,
			.supply_name    = "vin",
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
	},
	[AB8500_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_linear_range_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= 7,
			.linear_ranges	= ldo_vintcore_ranges,
			.n_linear_ranges = ARRAY_SIZE(ldo_vintcore_ranges),
			.enable_time	= 750,
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.enable_mask		= 0x04,
		.update_val		= 0x44,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB8500_LDO_TVOUT] = {
		.desc = {
			.name		= "LDO-TVOUT",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_TVOUT,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2000000_voltage,
			.enable_time	= 500,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.enable_mask		= 0x02,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB8500_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 140,
			.volt_table	= fixed_2000000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
	},
	[AB8500_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 500,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
	},
	[AB8500_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 500,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
	},
	[AB8500_LDO_DMIC] = {
		.desc = {
			.name		= "LDO-DMIC",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_DMIC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 420,
			.volt_table	= fixed_1800000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},

	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB8500_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 140,
			.volt_table	= fixed_1200000_voltage,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
	},

	/* Buck converters */
	[AB8500_BUCK_SMPS1] = {
		.desc = {
			.name		= "BUCK-SMPS1",
			.ops		= &ab8500_buck_ops,
			.init_cb	= ab8500_buck_init,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_BUCK_SMPS1,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_smps1_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_smps1_voltages),
		},
		.load_lp_uA		= 20000,
		.update_bank		= 0x04,
		.update_reg		= 0x03,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x13,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x03,
		.voltage_ctrl_mask	= 0x0c,
	},
	[AB8500_BUCK_SMPS2] = {
		.desc = {
			.name		= "BUCK-SMPS2",
			.ops		= &ab8500_buck_ops,
			.init_cb	= ab8500_buck_init,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_BUCK_SMPS2,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_smps2_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_smps2_voltages),
		},
		.load_lp_uA		= 20000,
		.update_bank		= 0x04,
		.update_reg		= 0x04,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x17,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x04,
		.voltage_ctrl_mask	= 0x0c,
	},
	[AB8500_BUCK_SMPS3] = {
		.desc = {
			.name		= "BUCK-SMPS3",
			.ops		= &ab8500_buck_ops,
			.init_cb	= ab8500_buck_init,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_BUCK_SMPS3,
			.owner		= THIS_MODULE,
			.n_voltages	= 128,
			.linear_ranges	= buck_low_7bit_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_low_7bit_voltages),
		},
		.load_lp_uA		= 50000,
		.update_bank		= 0x04,
		.update_reg		= 0x05,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1b,
		.voltage_mask		= 0x7f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x05,
		.voltage_ctrl_mask	= 0x0c,
	},
	[AB8500_BUCK_ARM] = {
		.desc = {
			.name		= "BUCK-ARM",
			.ops		= &ab8500_buck_voltage_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_BUCK_ARM,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_low_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_low_voltages),
		},
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x0b,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x00,
		.voltage_ctrl_mask	= 0x0c,
	},
	[AB8500_BUCK_APE] = {
		.desc = {
			.name		= "BUCK-APE",
			.ops		= &ab8500_buck_voltage_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_BUCK_APE,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_low_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_low_voltages),
		},
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x0e,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x02,
		.voltage_ctrl_mask	= 0x24,
	},
	[AB8500_BUCK_MOD] = {
		.desc = {
			.name		= "BUCK-MOD",
			.ops		= &ab8500_buck_voltage_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_BUCK_MOD,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_low_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_low_voltages),
		},
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x41,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x40,
		.voltage_ctrl_mask	= 0x04,
	},
};

/* AB8505 regulator information */
static struct ab8500_regulator_info
		ab8505_regulator_info[AB8505_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask
	 */
	[AB8505_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
			.volt_table	= ldo_vauxn_voltages,
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
	},
	[AB8505_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
			.volt_table	= ldo_vauxn_voltages,
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
	},
	[AB8505_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_expand_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_ab8505_voltages),
			.volt_table	= ldo_vaux3_ab8505_voltages,
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
		.expand_register = {
			.voltage_limit	= 8,
			.voltage_bank	= 0x04,
			.voltage_reg	= 0x01,
			.voltage_mask	= 0x10,
		},
	},
	[AB8505_LDO_AUX4] = {
		.desc = {
			.name		= "LDO-AUX4",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX4,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
			.volt_table	= ldo_vauxn_voltages,
		},
		.load_lp_uA		= 5000,
		/* values for Vaux4Regu register */
		.update_bank		= 0x04,
		.update_reg		= 0x2e,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		/* values for Vaux4SEL register */
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x2f,
		.voltage_mask		= 0x0f,
	},
	[AB8505_LDO_AUX5] = {
		.desc = {
			.name		= "LDO-AUX5",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX5,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux56_voltages),
			.volt_table	= ldo_vaux56_voltages,
		},
		.load_lp_uA		= 2000,
		/* values for CtrlVaux5 register */
		.update_bank		= 0x01,
		.update_reg		= 0x55,
		.update_mask		= 0x18,
		.enable_mask		= 0x10,
		.update_val		= 0x10,
		.update_val_idle	= 0x18,
		.update_val_normal	= 0x10,
		.voltage_bank		= 0x01,
		.voltage_reg		= 0x55,
		.voltage_mask		= 0x07,
	},
	[AB8505_LDO_AUX6] = {
		.desc = {
			.name		= "LDO-AUX6",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX6,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux56_voltages),
			.volt_table	= ldo_vaux56_voltages,
		},
		.load_lp_uA		= 2000,
		/* values for CtrlVaux6 register */
		.update_bank		= 0x01,
		.update_reg		= 0x56,
		.update_mask		= 0x18,
		.enable_mask		= 0x10,
		.update_val		= 0x10,
		.update_val_idle	= 0x18,
		.update_val_normal	= 0x10,
		.voltage_bank		= 0x01,
		.voltage_reg		= 0x56,
		.voltage_mask		= 0x07,
	},
	[AB8505_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_linear_range_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= 8,
			.linear_ranges	= ldo_vintcore_ab8505_ranges,
			.n_linear_ranges = ARRAY_SIZE(ldo_vintcore_ab8505_ranges),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.enable_mask		= 0x04,
		.update_val		= 0x04,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB8505_LDO_ADC] = {
		.desc = {
			.name		= "LDO-ADC",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ADC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2000000_voltage,
			.enable_time	= 10000,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.enable_mask		= 0x02,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB8505_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_linear_range_volt_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= 8,
			.linear_ranges	= ldo_vaudio_ranges,
			.n_linear_ranges = ARRAY_SIZE(ldo_vaudio_ranges),
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
		.voltage_bank		= 0x01,
		.voltage_reg		= 0x57,
		.voltage_mask		= 0x70,
	},
	[AB8505_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_anamic_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2050000_voltage,
		},
		.shared_mode		= &ldo_anamic1_shared,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
		.mode_bank		= 0x01,
		.mode_reg		= 0x54,
		.mode_mask		= 0x04,
		.mode_val_idle		= 0x04,
		.mode_val_normal	= 0x00,
	},
	[AB8505_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_anamic_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2050000_voltage,
		},
		.shared_mode		= &ldo_anamic2_shared,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
		.mode_bank		= 0x01,
		.mode_reg		= 0x54,
		.mode_mask		= 0x04,
		.mode_val_idle		= 0x04,
		.mode_val_normal	= 0x00,
	},
	[AB8505_LDO_AUX8] = {
		.desc = {
			.name		= "LDO-AUX8",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX8,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_1800000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},
	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB8505_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vana_voltages),
			.volt_table	= ldo_vana_voltages,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x29,
		.voltage_mask		= 0x7,
	},

	/* Buck converters */
	[AB8505_BUCK_SMPSA] = {
		.desc = {
			.name		= "BUCK-SMPSA",
			.ops		= &ab8500_buck_ops,
			.init_cb	= ab8500_buck_init,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_BUCK_SMPSA,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_smps1_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_smps1_voltages),
		},
		.load_lp_uA		= 20000,
		.update_bank		= 0x04,
		.update_reg		= 0x03,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x13,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x03,
		.voltage_ctrl_mask	= 0x0c,
	},
	[AB8505_BUCK_SMPSB] = {
		.desc = {
			.name		= "BUCK-SMPSB",
			.ops		= &ab8500_buck_ops,
			.init_cb	= ab8500_buck_init,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_BUCK_SMPSB,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_smps2_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_smps2_voltages),
		},
		.load_lp_uA		= 20000,
		.update_bank		= 0x04,
		.update_reg		= 0x04,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x17,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x04,
		.voltage_ctrl_mask	= 0x0c,
	},
	[AB8505_BUCK_SAFE] = {
		.desc = {
			.name		= "BUCK-SAFE",
			.ops		= &ab8500_buck_ops,
			.init_cb	= ab8500_buck_init,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_BUCK_SAFE,
			.owner		= THIS_MODULE,
			.n_voltages	= 128,
			.linear_ranges	= buck_low_7bit_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_low_7bit_voltages),
		},
		.load_lp_uA		= 50000,
		.update_bank		= 0x04,
		.update_reg		= 0x05,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1b,
		.voltage_mask		= 0x7f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x05,
		.voltage_ctrl_mask	= 0x0c,
	},
	[AB8505_BUCK_ARM] = {
		.desc = {
			.name		= "BUCK-ARM",
			.ops		= &ab8500_buck_voltage_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_BUCK_ARM,
			.owner		= THIS_MODULE,
			.n_voltages	= 128,
			.linear_ranges	= ab8505_buck_arm_voltages,
			.n_linear_ranges = ARRAY_SIZE(ab8505_buck_arm_voltages),
		},
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x0b,
		.voltage_mask		= 0x7f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x00,
		.voltage_ctrl_mask	= 0x0c,
		.voltage_ext_ctrl_bank = 0x04,
		.voltage_ext_ctrl_reg	= 0x28,
		.voltage_ext_ctrl_mask = 0x07,
		.voltage_ext_reg	= 0x24,
		.voltage_ext_regs	= 4,
	},
	[AB8505_BUCK_SMPSC] = {
		.desc = {
			.name		= "BUCK-SMPSC",
			.ops		= &ab8500_buck_voltage_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_BUCK_SMPSC,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_low_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_low_voltages),
		},
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x0e,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x02,
		.voltage_ctrl_mask	= 0x24,
		.voltage_ext_ctrl_bank = 0x04,
		.voltage_ext_ctrl_reg	= 0x2a,
		.voltage_ext_ctrl_mask = 0x03,
		.voltage_ext_reg	= 0x2b,
		.voltage_ext_regs	= 2,
	},
	[AB8505_BUCK_SMPSM] = {
		.desc = {
			.name		= "BUCK-SMPSM",
			.ops		= &ab8500_buck_voltage_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_BUCK_SMPSM,
			.owner		= THIS_MODULE,
			.n_voltages	= 64,
			.linear_ranges	= buck_low_voltages,
			.n_linear_ranges = ARRAY_SIZE(buck_low_voltages),
		},
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x41,
		.voltage_mask		= 0x3f,
		.voltage_ctrl_bank	= 0x04,
		.voltage_ctrl_reg	= 0x40,
		.voltage_ctrl_mask	= 0x04,
		.voltage_ext_ctrl_bank = 0x04,
		.voltage_ext_ctrl_reg	= 0x47,
		.voltage_ext_ctrl_mask = 0x03,
		.voltage_ext_reg	= 0x45,
		.voltage_ext_regs	= 2,
	},
};

static struct ab8500_shared_mode ldo_anamic1_shared = {
	.shared_regulator = &ab8505_regulator_info[AB8505_LDO_ANAMIC2],
};

static struct ab8500_shared_mode ldo_anamic2_shared = {
	.shared_regulator = &ab8505_regulator_info[AB8505_LDO_ANAMIC1],
};

struct ab8500_reg_init {
	u8 bank;
	u8 addr;
	u8 mask;
};

#define REG_INIT(_id, _bank, _addr, _mask)	\
	[_id] = {				\
		.bank = _bank,			\
		.addr = _addr,			\
		.mask = _mask,			\
	}

/* AB8500 register init */
static struct ab8500_reg_init ab8500_reg_init[] = {
	/*
	 * 0x30, VanaRequestCtrl
	 * 0xc0, VextSupply1RequestCtrl
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL2,	0x03, 0x04, 0xf0),
	/*
	 * 0x03, VextSupply2RequestCtrl
	 * 0x0c, VextSupply3RequestCtrl
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL3,	0x03, 0x05, 0xff),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB8500_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xe8),
	/*
	 * 0x10, VextSupply1SysClkReq1HPValid
	 * 0x20, VextSupply2SysClkReq1HPValid
	 * 0x40, VextSupply3SysClkReq1HPValid
	 */
	REG_INIT(AB8500_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x70),
	/*
	 * 0x08, VanaHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xe8),
	/*
	 * 0x01, VextSupply1HwHPReq1Valid
	 * 0x02, VextSupply2HwHPReq1Valid
	 * 0x04, VextSupply3HwHPReq1Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x07),
	/*
	 * 0x08, VanaHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xe8),
	/*
	 * 0x01, VextSupply1HwHPReq2Valid
	 * 0x02, VextSupply2HwHPReq2Valid
	 * 0x04, VextSupply3HwHPReq2Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x07),
	/*
	 * 0x20, VanaSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB8500_REGUSWHPREQVALID1,	0x03, 0x0d, 0xa0),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x04, VextSupply1SwHPReqValid
	 * 0x08, VextSupply2SwHPReqValid
	 * 0x10, VextSupply3SwHPReqValid
	 */
	REG_INIT(AB8500_REGUSWHPREQVALID2,	0x03, 0x0e, 0x1f),
	/*
	 * 0x02, SysClkReq2Valid1
	 * 0x04, SysClkReq3Valid1
	 * 0x08, SysClkReq4Valid1
	 * 0x10, SysClkReq5Valid1
	 * 0x20, SysClkReq6Valid1
	 * 0x40, SysClkReq7Valid1
	 * 0x80, SysClkReq8Valid1
	 */
	REG_INIT(AB8500_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0xfe),
	/*
	 * 0x02, SysClkReq2Valid2
	 * 0x04, SysClkReq3Valid2
	 * 0x08, SysClkReq4Valid2
	 * 0x10, SysClkReq5Valid2
	 * 0x20, SysClkReq6Valid2
	 * 0x40, SysClkReq7Valid2
	 * 0x80, SysClkReq8Valid2
	 */
	REG_INIT(AB8500_REGUSYSCLKREQVALID2,	0x03, 0x10, 0xfe),
	/*
	 * 0x02, VTVoutEna
	 * 0x04, Vintcore12Ena
	 * 0x38, Vintcore12Sel
	 * 0x40, Vintcore12LP
	 * 0x80, VTVoutLP
	 */
	REG_INIT(AB8500_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 */
	REG_INIT(AB8500_VAUDIOSUPPLY,		0x03, 0x83, 0x1e),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB8500_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x03, VpllRegu (NOTE! PRCMU register bits)
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB8500_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x01, VrefDDREna
	 * 0x02, VrefDDRSleepMode
	 */
	REG_INIT(AB8500_VREFDDR,		0x04, 0x07, 0x03),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB8500_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB8500_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x03, Vaux3Regu
	 */
	REG_INIT(AB8500_VRF1VAUX3REGU,		0x04, 0x0a, 0x03),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB8500_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB8500_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 */
	REG_INIT(AB8500_VRF1VAUX3SEL,		0x04, 0x21, 0x07),
	/*
	 * 0x01, VextSupply12LP
	 */
	REG_INIT(AB8500_REGUCTRL2SPARE,		0x04, 0x22, 0x01),
	/*
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB8500_REGUCTRLDISCH,		0x04, 0x43, 0xfc),
	/*
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB8500_REGUCTRLDISCH2,		0x04, 0x44, 0x16),
};

/* AB8505 register init */
static struct ab8500_reg_init ab8505_reg_init[] = {
	/*
	 * 0x03, VarmRequestCtrl
	 * 0x0c, VsmpsCRequestCtrl
	 * 0x30, VsmpsARequestCtrl
	 * 0xc0, VsmpsBRequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL1,	0x03, 0x03, 0xff),
	/*
	 * 0x03, VsafeRequestCtrl
	 * 0x0c, VpllRequestCtrl
	 * 0x30, VanaRequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL2,	0x03, 0x04, 0x3f),
	/*
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL3,	0x03, 0x05, 0xf0),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x01, VsmpsASysClkReq1HPValid
	 * 0x02, VsmpsBSysClkReq1HPValid
	 * 0x04, VsafeSysClkReq1HPValid
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x10, VpllSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xff),
	/*
	 * 0x01, VsmpsCSysClkReq1HPValid
	 * 0x02, VarmSysClkReq1HPValid
	 * 0x04, VbbSysClkReq1HPValid
	 * 0x08, VsmpsMSysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x0f),
	/*
	 * 0x01, VsmpsAHwHPReq1Valid
	 * 0x02, VsmpsBHwHPReq1Valid
	 * 0x04, VsafeHwHPReq1Valid
	 * 0x08, VanaHwHPReq1Valid
	 * 0x10, VpllHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xff),
	/*
	 * 0x08, VsmpsMHwHPReq1Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x08),
	/*
	 * 0x01, VsmpsAHwHPReq2Valid
	 * 0x02, VsmpsBHwHPReq2Valid
	 * 0x04, VsafeHwHPReq2Valid
	 * 0x08, VanaHwHPReq2Valid
	 * 0x10, VpllHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xff),
	/*
	 * 0x08, VsmpsMHwHPReq2Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x08),
	/*
	 * 0x01, VsmpsCSwHPReqValid
	 * 0x02, VarmSwHPReqValid
	 * 0x04, VsmpsASwHPReqValid
	 * 0x08, VsmpsBSwHPReqValid
	 * 0x10, VsafeSwHPReqValid
	 * 0x20, VanaSwHPReqValid
	 * 0x40, VpllSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB8505_REGUSWHPREQVALID1,	0x03, 0x0d, 0xff),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x20, VsmpsMSwHPReqValid
	 */
	REG_INIT(AB8505_REGUSWHPREQVALID2,	0x03, 0x0e, 0x23),
	/*
	 * 0x02, SysClkReq2Valid1
	 * 0x04, SysClkReq3Valid1
	 * 0x08, SysClkReq4Valid1
	 */
	REG_INIT(AB8505_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0x0e),
	/*
	 * 0x02, SysClkReq2Valid2
	 * 0x04, SysClkReq3Valid2
	 * 0x08, SysClkReq4Valid2
	 */
	REG_INIT(AB8505_REGUSYSCLKREQVALID2,	0x03, 0x10, 0x0e),
	/*
	 * 0x01, Vaux4SwHPReqValid
	 * 0x02, Vaux4HwHPReq2Valid
	 * 0x04, Vaux4HwHPReq1Valid
	 * 0x08, Vaux4SysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUVAUX4REQVALID,	0x03, 0x11, 0x0f),
	/*
	 * 0x02, VadcEna
	 * 0x04, VintCore12Ena
	 * 0x38, VintCore12Sel
	 * 0x40, VintCore12LP
	 * 0x80, VadcLP
	 */
	REG_INIT(AB8505_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 */
	REG_INIT(AB8505_VAUDIOSUPPLY,		0x03, 0x83, 0x1e),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB8505_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x03, VsmpsARegu
	 * 0x0c, VsmpsASelCtrl
	 * 0x10, VsmpsAAutoMode
	 * 0x20, VsmpsAPWMMode
	 */
	REG_INIT(AB8505_VSMPSAREGU,		0x04, 0x03, 0x3f),
	/*
	 * 0x03, VsmpsBRegu
	 * 0x0c, VsmpsBSelCtrl
	 * 0x10, VsmpsBAutoMode
	 * 0x20, VsmpsBPWMMode
	 */
	REG_INIT(AB8505_VSMPSBREGU,		0x04, 0x04, 0x3f),
	/*
	 * 0x03, VsafeRegu
	 * 0x0c, VsafeSelCtrl
	 * 0x10, VsafeAutoMode
	 * 0x20, VsafePWMMode
	 */
	REG_INIT(AB8505_VSAFEREGU,		0x04, 0x05, 0x3f),
	/*
	 * 0x03, VpllRegu (NOTE! PRCMU register bits)
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB8505_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB8505_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB8505_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x0f, Vaux3Regu
	 */
	REG_INIT(AB8505_VRF1VAUX3REGU,		0x04, 0x0a, 0x0f),
	/*
	 * 0x3f, VsmpsASel1
	 */
	REG_INIT(AB8505_VSMPSASEL1,		0x04, 0x13, 0x3f),
	/*
	 * 0x3f, VsmpsASel2
	 */
	REG_INIT(AB8505_VSMPSASEL2,		0x04, 0x14, 0x3f),
	/*
	 * 0x3f, VsmpsASel3
	 */
	REG_INIT(AB8505_VSMPSASEL3,		0x04, 0x15, 0x3f),
	/*
	 * 0x3f, VsmpsBSel1
	 */
	REG_INIT(AB8505_VSMPSBSEL1,		0x04, 0x17, 0x3f),
	/*
	 * 0x3f, VsmpsBSel2
	 */
	REG_INIT(AB8505_VSMPSBSEL2,		0x04, 0x18, 0x3f),
	/*
	 * 0x3f, VsmpsBSel3
	 */
	REG_INIT(AB8505_VSMPSBSEL3,		0x04, 0x19, 0x3f),
	/*
	 * 0x7f, VsafeSel1
	 */
	REG_INIT(AB8505_VSAFESEL1,		0x04, 0x1b, 0x7f),
	/*
	 * 0x3f, VsafeSel2
	 */
	REG_INIT(AB8505_VSAFESEL2,		0x04, 0x1c, 0x7f),
	/*
	 * 0x3f, VsafeSel3
	 */
	REG_INIT(AB8505_VSAFESEL3,		0x04, 0x1d, 0x7f),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB8505_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB8505_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 * 0x30, VRF1Sel
	 */
	REG_INIT(AB8505_VRF1VAUX3SEL,		0x04, 0x21, 0x37),
	/*
	 * 0x03, Vaux4RequestCtrl
	 */
	REG_INIT(AB8505_VAUX4REQCTRL,		0x04, 0x2d, 0x03),
	/*
	 * 0x03, Vaux4Regu
	 */
	REG_INIT(AB8505_VAUX4REGU,		0x04, 0x2e, 0x03),
	/*
	 * 0x0f, Vaux4Sel
	 */
	REG_INIT(AB8505_VAUX4SEL,		0x04, 0x2f, 0x0f),
	/*
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH,		0x04, 0x43, 0xfc),
	/*
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH2,		0x04, 0x44, 0x16),
	/*
	 * 0x01, Vaux4Disch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH3,		0x04, 0x48, 0x01),
	/*
	 * 0x07, Vaux5Sel
	 * 0x08, Vaux5LP
	 * 0x10, Vaux5Ena
	 * 0x20, Vaux5Disch
	 * 0x40, Vaux5DisSfst
	 * 0x80, Vaux5DisPulld
	 */
	REG_INIT(AB8505_CTRLVAUX5,		0x01, 0x55, 0xff),
	/*
	 * 0x07, Vaux6Sel
	 * 0x08, Vaux6LP
	 * 0x10, Vaux6Ena
	 * 0x80, Vaux6DisPulld
	 */
	REG_INIT(AB8505_CTRLVAUX6,		0x01, 0x56, 0x9f),
};

static struct of_regulator_match ab8500_regulator_match[] = {
	{ .name	= "ab8500_ldo_aux1",    .driver_data = (void *) AB8500_LDO_AUX1, },
	{ .name	= "ab8500_ldo_aux2",    .driver_data = (void *) AB8500_LDO_AUX2, },
	{ .name	= "ab8500_ldo_aux3",    .driver_data = (void *) AB8500_LDO_AUX3, },
	{ .name	= "ab8500_ldo_intcore", .driver_data = (void *) AB8500_LDO_INTCORE, },
	{ .name	= "ab8500_ldo_tvout",   .driver_data = (void *) AB8500_LDO_TVOUT, },
	{ .name = "ab8500_ldo_audio",   .driver_data = (void *) AB8500_LDO_AUDIO, },
	{ .name	= "ab8500_ldo_anamic1", .driver_data = (void *) AB8500_LDO_ANAMIC1, },
	{ .name	= "ab8500_ldo_anamic2", .driver_data = (void *) AB8500_LDO_ANAMIC2, },
	{ .name	= "ab8500_ldo_dmic",    .driver_data = (void *) AB8500_LDO_DMIC, },
	{ .name	= "ab8500_ldo_ana",     .driver_data = (void *) AB8500_LDO_ANA, },
	{ .name = "ab8500_buck_smps1",  .driver_data = (void *)AB8500_BUCK_SMPS1, },
	{ .name = "ab8500_buck_smps2",  .driver_data = (void *)AB8500_BUCK_SMPS2, },
	{ .name = "ab8500_buck_smps3",  .driver_data = (void *)AB8500_BUCK_SMPS3, },
	{ .name = "ab8500_buck_arm",    .driver_data = (void *)AB8500_BUCK_ARM, },
	{ .name = "ab8500_buck_ape",    .driver_data = (void *)AB8500_BUCK_APE, },
	{ .name = "ab8500_buck_mod",    .driver_data = (void *)AB8500_BUCK_MOD, },
};

static struct of_regulator_match ab8505_regulator_match[] = {
	{ .name	= "ab8500_ldo_aux1",    .driver_data = (void *) AB8505_LDO_AUX1, },
	{ .name	= "ab8500_ldo_aux2",    .driver_data = (void *) AB8505_LDO_AUX2, },
	{ .name	= "ab8500_ldo_aux3",    .driver_data = (void *) AB8505_LDO_AUX3, },
	{ .name	= "ab8500_ldo_aux4",    .driver_data = (void *) AB8505_LDO_AUX4, },
	{ .name	= "ab8500_ldo_aux5",    .driver_data = (void *) AB8505_LDO_AUX5, },
	{ .name	= "ab8500_ldo_aux6",    .driver_data = (void *) AB8505_LDO_AUX6, },
	{ .name	= "ab8500_ldo_intcore", .driver_data = (void *) AB8505_LDO_INTCORE, },
	{ .name	= "ab8500_ldo_adc",	.driver_data = (void *) AB8505_LDO_ADC, },
	{ .name = "ab8500_ldo_audio",   .driver_data = (void *) AB8505_LDO_AUDIO, },
	{ .name	= "ab8500_ldo_anamic1", .driver_data = (void *) AB8505_LDO_ANAMIC1, },
	{ .name	= "ab8500_ldo_anamic2", .driver_data = (void *) AB8505_LDO_ANAMIC2, },
	{ .name	= "ab8500_ldo_aux8",    .driver_data = (void *) AB8505_LDO_AUX8, },
	{ .name	= "ab8500_ldo_ana",     .driver_data = (void *) AB8505_LDO_ANA, },
	{ .name = "ab8505_buck_smpsa",  .driver_data = (void *)AB8505_BUCK_SMPSA, },
	{ .name = "ab8505_buck_smpsb",  .driver_data = (void *)AB8505_BUCK_SMPSB, },
	{ .name = "ab8505_buck_safe",   .driver_data = (void *)AB8505_BUCK_SAFE, },
	{ .name = "ab8505_buck_arm",    .driver_data = (void *)AB8505_BUCK_ARM, },
	{ .name = "ab8505_buck_smpsc",  .driver_data = (void *)AB8505_BUCK_SMPSC, },
	{ .name = "ab8505_buck_smpsm",  .driver_data = (void *)AB8505_BUCK_SMPSM, },
};

static struct {
	struct ab8500_regulator_info *info;
	int info_size;
	struct ab8500_reg_init *init;
	int init_size;
	struct of_regulator_match *match;
	int match_size;
} abx500_regulator;

static void abx500_get_regulator_info(struct ab8500 *ab8500)
{
	if (is_ab8505(ab8500)) {
		abx500_regulator.info = ab8505_regulator_info;
		abx500_regulator.info_size = ARRAY_SIZE(ab8505_regulator_info);
		abx500_regulator.init = ab8505_reg_init;
		abx500_regulator.init_size = AB8505_NUM_REGULATOR_REGISTERS;
		abx500_regulator.match = ab8505_regulator_match;
		abx500_regulator.match_size = ARRAY_SIZE(ab8505_regulator_match);
	} else {
		abx500_regulator.info = ab8500_regulator_info;
		abx500_regulator.info_size = ARRAY_SIZE(ab8500_regulator_info);
		abx500_regulator.init = ab8500_reg_init;
		abx500_regulator.init_size = AB8500_NUM_REGULATOR_REGISTERS;
		abx500_regulator.match = ab8500_regulator_match;
		abx500_regulator.match_size = ARRAY_SIZE(ab8500_regulator_match);
	}
}

static int ab8500_regulator_register(struct platform_device *pdev,
				     struct regulator_init_data *init_data,
				     int id, struct device_node *np)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_regulator_info *info = NULL;
	struct regulator_config config = { };
	struct regulator_dev *rdev;

	/* assign per-regulator data */
	info = &abx500_regulator.info[id];
	info->dev = &pdev->dev;

	config.dev = &pdev->dev;
	config.init_data = init_data;
	config.driver_data = info;
	config.of_node = np;

	/* Handle the different VAUX3 implementations in early AB8500 cuts. */
	if (info->desc.id == AB8500_LDO_AUX3) {
		if (is_ab8500_1p0_or_earlier(ab8500)) {
			info->desc.ops = &ab8500_regulator_mode_ops;
			info->desc.n_voltages = 1;
			info->desc.volt_table = fixed_1200000_voltage;
			info->voltage_mask = 0;
		} else if (is_ab8500_1p1_or_earlier(ab8500)) {
			info->desc.n_voltages =
				ARRAY_SIZE(ldo_vauxn_voltages);
			info->desc.volt_table = ldo_vauxn_voltages;
			info->voltage_mask = 0xf;
		}
	}

	/* register regulator with framework */
	rdev = devm_regulator_register(&pdev->dev, &info->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
			info->desc.name);
		return PTR_ERR(rdev);
	}

	return 0;
}

static int ab8500_regulator_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;
	struct of_regulator_match *match;
	int err, i;

	if (!ab8500) {
		dev_err(&pdev->dev, "null mfd parent\n");
		return -EINVAL;
	}

	abx500_get_regulator_info(ab8500);

	err = of_regulator_match(&pdev->dev, np,
				 abx500_regulator.match,
				 abx500_regulator.match_size);
	if (err < 0) {
		dev_err(&pdev->dev,
			"Error parsing regulator init data: %d\n", err);
		return err;
	}

	match = abx500_regulator.match;
	for (i = 0; i < abx500_regulator.info_size; i++) {
		err = ab8500_regulator_register(pdev, match[i].init_data, i,
						match[i].of_node);
		if (err)
			return err;
	}

	return 0;
}

static struct platform_driver ab8500_regulator_driver = {
	.probe = ab8500_regulator_probe,
	.driver         = {
		.name   = "ab8500-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int __init ab8500_regulator_init(void)
{
	int ret;

	ret = platform_driver_register(&ab8500_regulator_driver);
	if (ret != 0)
		pr_err("Failed to register ab8500 regulator: %d\n", ret);

	return ret;
}
subsys_initcall(ab8500_regulator_init);

static void __exit ab8500_regulator_exit(void)
{
	platform_driver_unregister(&ab8500_regulator_driver);
}
module_exit(ab8500_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sundar Iyer <sundar.iyer@stericsson.com>");
MODULE_AUTHOR("Bengt Jonsson <bengt.g.jonsson@stericsson.com>");
MODULE_AUTHOR("Daniel Willerud <daniel.willerud@stericsson.com>");
MODULE_DESCRIPTION("Regulator Driver for ST-Ericsson AB8500 Mixed-Sig PMIC");
MODULE_ALIAS("platform:ab8500-regulator");
