/*
 * Copyright (c) 2014, Sony Mobile Communications AB.
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/qcom_rpm.h>

#include <dt-bindings/mfd/qcom-rpm.h>

#define MAX_REQUEST_LEN 2

struct request_member {
	int		word;
	unsigned int	mask;
	int		shift;
};

struct rpm_reg_parts {
	struct request_member mV;		/* used if voltage is in mV */
	struct request_member uV;		/* used if voltage is in uV */
	struct request_member ip;		/* peak current in mA */
	struct request_member pd;		/* pull down enable */
	struct request_member ia;		/* average current in mA */
	struct request_member fm;		/* force mode */
	struct request_member pm;		/* power mode */
	struct request_member pc;		/* pin control */
	struct request_member pf;		/* pin function */
	struct request_member enable_state;	/* NCP and switch */
	struct request_member comp_mode;	/* NCP */
	struct request_member freq;		/* frequency: NCP and SMPS */
	struct request_member freq_clk_src;	/* clock source: SMPS */
	struct request_member hpm;		/* switch: control OCP and SS */
	int request_len;
};

#define FORCE_MODE_IS_2_BITS(reg) \
	(((reg)->parts->fm.mask >> (reg)->parts->fm.shift) == 3)

struct qcom_rpm_reg {
	struct qcom_rpm *rpm;

	struct mutex lock;
	struct device *dev;
	struct regulator_desc desc;
	const struct rpm_reg_parts *parts;

	int resource;
	u32 val[MAX_REQUEST_LEN];

	int uV;
	int is_enabled;

	bool supports_force_mode_auto;
	bool supports_force_mode_bypass;
};

static const struct rpm_reg_parts rpm8660_ldo_parts = {
	.request_len    = 2,
	.mV             = { 0, 0x00000FFF,  0 },
	.ip             = { 0, 0x00FFF000, 12 },
	.fm             = { 0, 0x03000000, 24 },
	.pc             = { 0, 0x3C000000, 26 },
	.pf             = { 0, 0xC0000000, 30 },
	.pd             = { 1, 0x00000001,  0 },
	.ia             = { 1, 0x00001FFE,  1 },
};

static const struct rpm_reg_parts rpm8660_smps_parts = {
	.request_len    = 2,
	.mV             = { 0, 0x00000FFF,  0 },
	.ip             = { 0, 0x00FFF000, 12 },
	.fm             = { 0, 0x03000000, 24 },
	.pc             = { 0, 0x3C000000, 26 },
	.pf             = { 0, 0xC0000000, 30 },
	.pd             = { 1, 0x00000001,  0 },
	.ia             = { 1, 0x00001FFE,  1 },
	.freq           = { 1, 0x001FE000, 13 },
	.freq_clk_src   = { 1, 0x00600000, 21 },
};

static const struct rpm_reg_parts rpm8660_switch_parts = {
	.request_len    = 1,
	.enable_state   = { 0, 0x00000001,  0 },
	.pd             = { 0, 0x00000002,  1 },
	.pc             = { 0, 0x0000003C,  2 },
	.pf             = { 0, 0x000000C0,  6 },
	.hpm            = { 0, 0x00000300,  8 },
};

static const struct rpm_reg_parts rpm8660_ncp_parts = {
	.request_len    = 1,
	.mV             = { 0, 0x00000FFF,  0 },
	.enable_state   = { 0, 0x00001000, 12 },
	.comp_mode      = { 0, 0x00002000, 13 },
	.freq           = { 0, 0x003FC000, 14 },
};

static const struct rpm_reg_parts rpm8960_ldo_parts = {
	.request_len    = 2,
	.uV             = { 0, 0x007FFFFF,  0 },
	.pd             = { 0, 0x00800000, 23 },
	.pc             = { 0, 0x0F000000, 24 },
	.pf             = { 0, 0xF0000000, 28 },
	.ip             = { 1, 0x000003FF,  0 },
	.ia             = { 1, 0x000FFC00, 10 },
	.fm             = { 1, 0x00700000, 20 },
};

static const struct rpm_reg_parts rpm8960_smps_parts = {
	.request_len    = 2,
	.uV             = { 0, 0x007FFFFF,  0 },
	.pd             = { 0, 0x00800000, 23 },
	.pc             = { 0, 0x0F000000, 24 },
	.pf             = { 0, 0xF0000000, 28 },
	.ip             = { 1, 0x000003FF,  0 },
	.ia             = { 1, 0x000FFC00, 10 },
	.fm             = { 1, 0x00700000, 20 },
	.pm             = { 1, 0x00800000, 23 },
	.freq           = { 1, 0x1F000000, 24 },
	.freq_clk_src   = { 1, 0x60000000, 29 },
};

static const struct rpm_reg_parts rpm8960_switch_parts = {
	.request_len    = 1,
	.enable_state   = { 0, 0x00000001,  0 },
	.pd             = { 0, 0x00000002,  1 },
	.pc             = { 0, 0x0000003C,  2 },
	.pf             = { 0, 0x000003C0,  6 },
	.hpm            = { 0, 0x00000C00, 10 },
};

static const struct rpm_reg_parts rpm8960_ncp_parts = {
	.request_len    = 1,
	.uV             = { 0, 0x007FFFFF,  0 },
	.enable_state   = { 0, 0x00800000, 23 },
	.comp_mode      = { 0, 0x01000000, 24 },
	.freq           = { 0, 0x3E000000, 25 },
};

/*
 * Physically available PMIC regulator voltage ranges
 */
static const struct regulator_linear_range pldo_ranges[] = {
	REGULATOR_LINEAR_RANGE( 750000,   0,  59, 12500),
	REGULATOR_LINEAR_RANGE(1500000,  60, 123, 25000),
	REGULATOR_LINEAR_RANGE(3100000, 124, 160, 50000),
};

static const struct regulator_linear_range nldo_ranges[] = {
	REGULATOR_LINEAR_RANGE( 750000,   0,  63, 12500),
};

static const struct regulator_linear_range nldo1200_ranges[] = {
	REGULATOR_LINEAR_RANGE( 375000,   0,  59,  6250),
	REGULATOR_LINEAR_RANGE( 750000,  60, 123, 12500),
};

static const struct regulator_linear_range smps_ranges[] = {
	REGULATOR_LINEAR_RANGE( 375000,   0,  29, 12500),
	REGULATOR_LINEAR_RANGE( 750000,  30,  89, 12500),
	REGULATOR_LINEAR_RANGE(1500000,  90, 153, 25000),
};

static const struct regulator_linear_range ftsmps_ranges[] = {
	REGULATOR_LINEAR_RANGE( 350000,   0,   6, 50000),
	REGULATOR_LINEAR_RANGE( 700000,   7,  63, 12500),
	REGULATOR_LINEAR_RANGE(1500000,  64, 100, 50000),
};

static const struct regulator_linear_range smb208_ranges[] = {
	REGULATOR_LINEAR_RANGE( 375000,   0,  29, 12500),
	REGULATOR_LINEAR_RANGE( 750000,  30,  89, 12500),
	REGULATOR_LINEAR_RANGE(1500000,  90, 153, 25000),
	REGULATOR_LINEAR_RANGE(3100000, 154, 234, 25000),
};

static const struct regulator_linear_range ncp_ranges[] = {
	REGULATOR_LINEAR_RANGE(1500000,   0,  31, 50000),
};

static int rpm_reg_write(struct qcom_rpm_reg *vreg,
			 const struct request_member *req,
			 const int value)
{
	if (WARN_ON((value << req->shift) & ~req->mask))
		return -EINVAL;

	vreg->val[req->word] &= ~req->mask;
	vreg->val[req->word] |= value << req->shift;

	return qcom_rpm_write(vreg->rpm,
			      QCOM_RPM_ACTIVE_STATE,
			      vreg->resource,
			      vreg->val,
			      vreg->parts->request_len);
}

static int rpm_reg_set_mV_sel(struct regulator_dev *rdev,
			      unsigned selector)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->mV;
	int ret = 0;
	int uV;

	if (req->mask == 0)
		return -EINVAL;

	uV = regulator_list_voltage_linear_range(rdev, selector);
	if (uV < 0)
		return uV;

	mutex_lock(&vreg->lock);
	if (vreg->is_enabled)
		ret = rpm_reg_write(vreg, req, uV / 1000);

	if (!ret)
		vreg->uV = uV;
	mutex_unlock(&vreg->lock);

	return ret;
}

static int rpm_reg_set_uV_sel(struct regulator_dev *rdev,
			      unsigned selector)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->uV;
	int ret = 0;
	int uV;

	if (req->mask == 0)
		return -EINVAL;

	uV = regulator_list_voltage_linear_range(rdev, selector);
	if (uV < 0)
		return uV;

	mutex_lock(&vreg->lock);
	if (vreg->is_enabled)
		ret = rpm_reg_write(vreg, req, uV);

	if (!ret)
		vreg->uV = uV;
	mutex_unlock(&vreg->lock);

	return ret;
}

static int rpm_reg_get_voltage(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);

	return vreg->uV;
}

static int rpm_reg_mV_enable(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->mV;
	int ret;

	if (req->mask == 0)
		return -EINVAL;

	mutex_lock(&vreg->lock);
	ret = rpm_reg_write(vreg, req, vreg->uV / 1000);
	if (!ret)
		vreg->is_enabled = 1;
	mutex_unlock(&vreg->lock);

	return ret;
}

static int rpm_reg_uV_enable(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->uV;
	int ret;

	if (req->mask == 0)
		return -EINVAL;

	mutex_lock(&vreg->lock);
	ret = rpm_reg_write(vreg, req, vreg->uV);
	if (!ret)
		vreg->is_enabled = 1;
	mutex_unlock(&vreg->lock);

	return ret;
}

static int rpm_reg_switch_enable(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->enable_state;
	int ret;

	if (req->mask == 0)
		return -EINVAL;

	mutex_lock(&vreg->lock);
	ret = rpm_reg_write(vreg, req, 1);
	if (!ret)
		vreg->is_enabled = 1;
	mutex_unlock(&vreg->lock);

	return ret;
}

static int rpm_reg_mV_disable(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->mV;
	int ret;

	if (req->mask == 0)
		return -EINVAL;

	mutex_lock(&vreg->lock);
	ret = rpm_reg_write(vreg, req, 0);
	if (!ret)
		vreg->is_enabled = 0;
	mutex_unlock(&vreg->lock);

	return ret;
}

static int rpm_reg_uV_disable(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->uV;
	int ret;

	if (req->mask == 0)
		return -EINVAL;

	mutex_lock(&vreg->lock);
	ret = rpm_reg_write(vreg, req, 0);
	if (!ret)
		vreg->is_enabled = 0;
	mutex_unlock(&vreg->lock);

	return ret;
}

static int rpm_reg_switch_disable(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->enable_state;
	int ret;

	if (req->mask == 0)
		return -EINVAL;

	mutex_lock(&vreg->lock);
	ret = rpm_reg_write(vreg, req, 0);
	if (!ret)
		vreg->is_enabled = 0;
	mutex_unlock(&vreg->lock);

	return ret;
}

static int rpm_reg_is_enabled(struct regulator_dev *rdev)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);

	return vreg->is_enabled;
}

static int rpm_reg_set_load(struct regulator_dev *rdev, int load_uA)
{
	struct qcom_rpm_reg *vreg = rdev_get_drvdata(rdev);
	const struct rpm_reg_parts *parts = vreg->parts;
	const struct request_member *req = &parts->ia;
	int load_mA = load_uA / 1000;
	int max_mA = req->mask >> req->shift;
	int ret;

	if (req->mask == 0)
		return -EINVAL;

	if (load_mA > max_mA)
		load_mA = max_mA;

	mutex_lock(&vreg->lock);
	ret = rpm_reg_write(vreg, req, load_mA);
	mutex_unlock(&vreg->lock);

	return ret;
}

static struct regulator_ops uV_ops = {
	.list_voltage = regulator_list_voltage_linear_range,

	.set_voltage_sel = rpm_reg_set_uV_sel,
	.get_voltage = rpm_reg_get_voltage,

	.enable = rpm_reg_uV_enable,
	.disable = rpm_reg_uV_disable,
	.is_enabled = rpm_reg_is_enabled,

	.set_load = rpm_reg_set_load,
};

static struct regulator_ops mV_ops = {
	.list_voltage = regulator_list_voltage_linear_range,

	.set_voltage_sel = rpm_reg_set_mV_sel,
	.get_voltage = rpm_reg_get_voltage,

	.enable = rpm_reg_mV_enable,
	.disable = rpm_reg_mV_disable,
	.is_enabled = rpm_reg_is_enabled,

	.set_load = rpm_reg_set_load,
};

static struct regulator_ops switch_ops = {
	.enable = rpm_reg_switch_enable,
	.disable = rpm_reg_switch_disable,
	.is_enabled = rpm_reg_is_enabled,
};

/*
 * PM8018 regulators
 */
static const struct qcom_rpm_reg pm8018_pldo = {
	.desc.linear_ranges = pldo_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(pldo_ranges),
	.desc.n_voltages = 161,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8018_nldo = {
	.desc.linear_ranges = nldo_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(nldo_ranges),
	.desc.n_voltages = 64,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8018_smps = {
	.desc.linear_ranges = smps_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(smps_ranges),
	.desc.n_voltages = 154,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_smps_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8018_switch = {
	.desc.ops = &switch_ops,
	.parts = &rpm8960_switch_parts,
};

/*
 * PM8058 regulators
 */
static const struct qcom_rpm_reg pm8058_pldo = {
	.desc.linear_ranges = pldo_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(pldo_ranges),
	.desc.n_voltages = 161,
	.desc.ops = &mV_ops,
	.parts = &rpm8660_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8058_nldo = {
	.desc.linear_ranges = nldo_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(nldo_ranges),
	.desc.n_voltages = 64,
	.desc.ops = &mV_ops,
	.parts = &rpm8660_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8058_smps = {
	.desc.linear_ranges = smps_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(smps_ranges),
	.desc.n_voltages = 154,
	.desc.ops = &mV_ops,
	.parts = &rpm8660_smps_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8058_ncp = {
	.desc.linear_ranges = ncp_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(ncp_ranges),
	.desc.n_voltages = 32,
	.desc.ops = &mV_ops,
	.parts = &rpm8660_ncp_parts,
};

static const struct qcom_rpm_reg pm8058_switch = {
	.desc.ops = &switch_ops,
	.parts = &rpm8660_switch_parts,
};

/*
 * PM8901 regulators
 */
static const struct qcom_rpm_reg pm8901_pldo = {
	.desc.linear_ranges = pldo_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(pldo_ranges),
	.desc.n_voltages = 161,
	.desc.ops = &mV_ops,
	.parts = &rpm8660_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = true,
};

static const struct qcom_rpm_reg pm8901_nldo = {
	.desc.linear_ranges = nldo_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(nldo_ranges),
	.desc.n_voltages = 64,
	.desc.ops = &mV_ops,
	.parts = &rpm8660_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = true,
};

static const struct qcom_rpm_reg pm8901_ftsmps = {
	.desc.linear_ranges = ftsmps_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(ftsmps_ranges),
	.desc.n_voltages = 101,
	.desc.ops = &mV_ops,
	.parts = &rpm8660_smps_parts,
	.supports_force_mode_auto = true,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8901_switch = {
	.desc.ops = &switch_ops,
	.parts = &rpm8660_switch_parts,
};

/*
 * PM8921 regulators
 */
static const struct qcom_rpm_reg pm8921_pldo = {
	.desc.linear_ranges = pldo_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(pldo_ranges),
	.desc.n_voltages = 161,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = true,
};

static const struct qcom_rpm_reg pm8921_nldo = {
	.desc.linear_ranges = nldo_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(nldo_ranges),
	.desc.n_voltages = 64,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = true,
};

static const struct qcom_rpm_reg pm8921_nldo1200 = {
	.desc.linear_ranges = nldo1200_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(nldo1200_ranges),
	.desc.n_voltages = 124,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_ldo_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = true,
};

static const struct qcom_rpm_reg pm8921_smps = {
	.desc.linear_ranges = smps_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(smps_ranges),
	.desc.n_voltages = 154,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_smps_parts,
	.supports_force_mode_auto = true,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8921_ftsmps = {
	.desc.linear_ranges = ftsmps_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(ftsmps_ranges),
	.desc.n_voltages = 101,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_smps_parts,
	.supports_force_mode_auto = true,
	.supports_force_mode_bypass = false,
};

static const struct qcom_rpm_reg pm8921_ncp = {
	.desc.linear_ranges = ncp_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(ncp_ranges),
	.desc.n_voltages = 32,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_ncp_parts,
};

static const struct qcom_rpm_reg pm8921_switch = {
	.desc.ops = &switch_ops,
	.parts = &rpm8960_switch_parts,
};

static const struct qcom_rpm_reg smb208_smps = {
	.desc.linear_ranges = smb208_ranges,
	.desc.n_linear_ranges = ARRAY_SIZE(smb208_ranges),
	.desc.n_voltages = 235,
	.desc.ops = &uV_ops,
	.parts = &rpm8960_smps_parts,
	.supports_force_mode_auto = false,
	.supports_force_mode_bypass = false,
};

static int rpm_reg_set(struct qcom_rpm_reg *vreg,
		       const struct request_member *req,
		       const int value)
{
	if (req->mask == 0 || (value << req->shift) & ~req->mask)
		return -EINVAL;

	vreg->val[req->word] &= ~req->mask;
	vreg->val[req->word] |= value << req->shift;

	return 0;
}

static int rpm_reg_of_parse_freq(struct device *dev,
				 struct device_node *node,
				 struct qcom_rpm_reg *vreg)
{
	static const int freq_table[] = {
		19200000, 9600000, 6400000, 4800000, 3840000, 3200000, 2740000,
		2400000, 2130000, 1920000, 1750000, 1600000, 1480000, 1370000,
		1280000, 1200000,

	};
	const char *key;
	u32 freq;
	int ret;
	int i;

	key = "qcom,switch-mode-frequency";
	ret = of_property_read_u32(node, key, &freq);
	if (ret) {
		dev_err(dev, "regulator requires %s property\n", key);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(freq_table); i++) {
		if (freq == freq_table[i]) {
			rpm_reg_set(vreg, &vreg->parts->freq, i + 1);
			return 0;
		}
	}

	dev_err(dev, "invalid frequency %d\n", freq);
	return -EINVAL;
}

static int rpm_reg_of_parse(struct device_node *node,
			    const struct regulator_desc *desc,
			    struct regulator_config *config)
{
	struct qcom_rpm_reg *vreg = config->driver_data;
	struct device *dev = config->dev;
	const char *key;
	u32 force_mode;
	bool pwm;
	u32 val;
	int ret;

	key = "bias-pull-down";
	if (of_property_read_bool(node, key)) {
		ret = rpm_reg_set(vreg, &vreg->parts->pd, 1);
		if (ret) {
			dev_err(dev, "%s is invalid", key);
			return ret;
		}
	}

	if (vreg->parts->freq.mask) {
		ret = rpm_reg_of_parse_freq(dev, node, vreg);
		if (ret < 0)
			return ret;
	}

	if (vreg->parts->pm.mask) {
		key = "qcom,power-mode-hysteretic";
		pwm = !of_property_read_bool(node, key);

		ret = rpm_reg_set(vreg, &vreg->parts->pm, pwm);
		if (ret) {
			dev_err(dev, "failed to set power mode\n");
			return ret;
		}
	}

	if (vreg->parts->fm.mask) {
		force_mode = -1;

		key = "qcom,force-mode";
		ret = of_property_read_u32(node, key, &val);
		if (ret == -EINVAL) {
			val = QCOM_RPM_FORCE_MODE_NONE;
		} else if (ret < 0) {
			dev_err(dev, "failed to read %s\n", key);
			return ret;
		}

		/*
		 * If force-mode is encoded as 2 bits then the
		 * possible register values are:
		 * NONE, LPM, HPM
		 * otherwise:
		 * NONE, LPM, AUTO, HPM, BYPASS
		 */
		switch (val) {
		case QCOM_RPM_FORCE_MODE_NONE:
			force_mode = 0;
			break;
		case QCOM_RPM_FORCE_MODE_LPM:
			force_mode = 1;
			break;
		case QCOM_RPM_FORCE_MODE_HPM:
			if (FORCE_MODE_IS_2_BITS(vreg))
				force_mode = 2;
			else
				force_mode = 3;
			break;
		case QCOM_RPM_FORCE_MODE_AUTO:
			if (vreg->supports_force_mode_auto)
				force_mode = 2;
			break;
		case QCOM_RPM_FORCE_MODE_BYPASS:
			if (vreg->supports_force_mode_bypass)
				force_mode = 4;
			break;
		}

		if (force_mode == -1) {
			dev_err(dev, "invalid force mode\n");
			return -EINVAL;
		}

		ret = rpm_reg_set(vreg, &vreg->parts->fm, force_mode);
		if (ret) {
			dev_err(dev, "failed to set force mode\n");
			return ret;
		}
	}

	return 0;
}

struct rpm_regulator_data {
	const char *name;
	int resource;
	const struct qcom_rpm_reg *template;
	const char *supply;
};

static const struct rpm_regulator_data rpm_pm8018_regulators[] = {
	{ "s1",  QCOM_RPM_PM8018_SMPS1, &pm8018_smps, "vdd_s1" },
	{ "s2",  QCOM_RPM_PM8018_SMPS2, &pm8018_smps, "vdd_s2" },
	{ "s3",  QCOM_RPM_PM8018_SMPS3, &pm8018_smps, "vdd_s3" },
	{ "s4",  QCOM_RPM_PM8018_SMPS4, &pm8018_smps, "vdd_s4" },
	{ "s5",  QCOM_RPM_PM8018_SMPS5, &pm8018_smps, "vdd_s5" },

	{ "l2",  QCOM_RPM_PM8018_LDO2,  &pm8018_pldo, "vdd_l2" },
	{ "l3",  QCOM_RPM_PM8018_LDO3,  &pm8018_pldo, "vdd_l3" },
	{ "l4",  QCOM_RPM_PM8018_LDO4,  &pm8018_pldo, "vdd_l4" },
	{ "l5",  QCOM_RPM_PM8018_LDO5,  &pm8018_pldo, "vdd_l5" },
	{ "l6",  QCOM_RPM_PM8018_LDO6,  &pm8018_pldo, "vdd_l7" },
	{ "l7",  QCOM_RPM_PM8018_LDO7,  &pm8018_pldo, "vdd_l7" },
	{ "l8",  QCOM_RPM_PM8018_LDO8,  &pm8018_nldo, "vdd_l8" },
	{ "l9",  QCOM_RPM_PM8018_LDO9,  &pm8921_nldo1200,
						      "vdd_l9_l10_l11_l12" },
	{ "l10", QCOM_RPM_PM8018_LDO10, &pm8018_nldo, "vdd_l9_l10_l11_l12" },
	{ "l11", QCOM_RPM_PM8018_LDO11, &pm8018_nldo, "vdd_l9_l10_l11_l12" },
	{ "l12", QCOM_RPM_PM8018_LDO12, &pm8018_nldo, "vdd_l9_l10_l11_l12" },
	{ "l14", QCOM_RPM_PM8018_LDO14, &pm8018_pldo, "vdd_l14" },

	{ "lvs1", QCOM_RPM_PM8018_LVS1, &pm8018_switch, "lvs1_in" },

	{ }
};

static const struct rpm_regulator_data rpm_pm8058_regulators[] = {
	{ "l0",   QCOM_RPM_PM8058_LDO0,   &pm8058_nldo, "vdd_l0_l1_lvs"	},
	{ "l1",   QCOM_RPM_PM8058_LDO1,   &pm8058_nldo, "vdd_l0_l1_lvs" },
	{ "l2",   QCOM_RPM_PM8058_LDO2,   &pm8058_pldo, "vdd_l2_l11_l12" },
	{ "l3",   QCOM_RPM_PM8058_LDO3,   &pm8058_pldo, "vdd_l3_l4_l5" },
	{ "l4",   QCOM_RPM_PM8058_LDO4,   &pm8058_pldo, "vdd_l3_l4_l5" },
	{ "l5",   QCOM_RPM_PM8058_LDO5,   &pm8058_pldo, "vdd_l3_l4_l5" },
	{ "l6",   QCOM_RPM_PM8058_LDO6,   &pm8058_pldo, "vdd_l6_l7" },
	{ "l7",   QCOM_RPM_PM8058_LDO7,   &pm8058_pldo, "vdd_l6_l7" },
	{ "l8",   QCOM_RPM_PM8058_LDO8,   &pm8058_pldo, "vdd_l8" },
	{ "l9",   QCOM_RPM_PM8058_LDO9,   &pm8058_pldo, "vdd_l9" },
	{ "l10",  QCOM_RPM_PM8058_LDO10,  &pm8058_pldo, "vdd_l10" },
	{ "l11",  QCOM_RPM_PM8058_LDO11,  &pm8058_pldo, "vdd_l2_l11_l12" },
	{ "l12",  QCOM_RPM_PM8058_LDO12,  &pm8058_pldo, "vdd_l2_l11_l12" },
	{ "l13",  QCOM_RPM_PM8058_LDO13,  &pm8058_pldo, "vdd_l13_l16" },
	{ "l14",  QCOM_RPM_PM8058_LDO14,  &pm8058_pldo, "vdd_l14_l15" },
	{ "l15",  QCOM_RPM_PM8058_LDO15,  &pm8058_pldo, "vdd_l14_l15" },
	{ "l16",  QCOM_RPM_PM8058_LDO16,  &pm8058_pldo, "vdd_l13_l16" },
	{ "l17",  QCOM_RPM_PM8058_LDO17,  &pm8058_pldo, "vdd_l17_l18" },
	{ "l18",  QCOM_RPM_PM8058_LDO18,  &pm8058_pldo, "vdd_l17_l18" },
	{ "l19",  QCOM_RPM_PM8058_LDO19,  &pm8058_pldo, "vdd_l19_l20" },
	{ "l20",  QCOM_RPM_PM8058_LDO20,  &pm8058_pldo, "vdd_l19_l20" },
	{ "l21",  QCOM_RPM_PM8058_LDO21,  &pm8058_nldo, "vdd_l21" },
	{ "l22",  QCOM_RPM_PM8058_LDO22,  &pm8058_nldo, "vdd_l22" },
	{ "l23",  QCOM_RPM_PM8058_LDO23,  &pm8058_nldo, "vdd_l23_l24_l25" },
	{ "l24",  QCOM_RPM_PM8058_LDO24,  &pm8058_nldo, "vdd_l23_l24_l25" },
	{ "l25",  QCOM_RPM_PM8058_LDO25,  &pm8058_nldo, "vdd_l23_l24_l25" },

	{ "s0",   QCOM_RPM_PM8058_SMPS0,  &pm8058_smps, "vdd_s0" },
	{ "s1",   QCOM_RPM_PM8058_SMPS1,  &pm8058_smps, "vdd_s1" },
	{ "s2",   QCOM_RPM_PM8058_SMPS2,  &pm8058_smps, "vdd_s2" },
	{ "s3",   QCOM_RPM_PM8058_SMPS3,  &pm8058_smps, "vdd_s3" },
	{ "s4",   QCOM_RPM_PM8058_SMPS4,  &pm8058_smps, "vdd_s4" },

	{ "lvs0", QCOM_RPM_PM8058_LVS0, &pm8058_switch, "vdd_l0_l1_lvs" },
	{ "lvs1", QCOM_RPM_PM8058_LVS1, &pm8058_switch, "vdd_l0_l1_lvs" },

	{ "ncp",  QCOM_RPM_PM8058_NCP, &pm8058_ncp, "vdd_ncp" },
	{ }
};

static const struct rpm_regulator_data rpm_pm8901_regulators[] = {
	{ "l0",   QCOM_RPM_PM8901_LDO0, &pm8901_nldo, "vdd_l0" },
	{ "l1",   QCOM_RPM_PM8901_LDO1, &pm8901_pldo, "vdd_l1" },
	{ "l2",   QCOM_RPM_PM8901_LDO2, &pm8901_pldo, "vdd_l2" },
	{ "l3",   QCOM_RPM_PM8901_LDO3, &pm8901_pldo, "vdd_l3" },
	{ "l4",   QCOM_RPM_PM8901_LDO4, &pm8901_pldo, "vdd_l4" },
	{ "l5",   QCOM_RPM_PM8901_LDO5, &pm8901_pldo, "vdd_l5" },
	{ "l6",   QCOM_RPM_PM8901_LDO6, &pm8901_pldo, "vdd_l6" },

	{ "s0",   QCOM_RPM_PM8901_SMPS0, &pm8901_ftsmps, "vdd_s0" },
	{ "s1",   QCOM_RPM_PM8901_SMPS1, &pm8901_ftsmps, "vdd_s1" },
	{ "s2",   QCOM_RPM_PM8901_SMPS2, &pm8901_ftsmps, "vdd_s2" },
	{ "s3",   QCOM_RPM_PM8901_SMPS3, &pm8901_ftsmps, "vdd_s3" },
	{ "s4",   QCOM_RPM_PM8901_SMPS4, &pm8901_ftsmps, "vdd_s4" },

	{ "lvs0", QCOM_RPM_PM8901_LVS0, &pm8901_switch, "lvs0_in" },
	{ "lvs1", QCOM_RPM_PM8901_LVS1, &pm8901_switch, "lvs1_in" },
	{ "lvs2", QCOM_RPM_PM8901_LVS2, &pm8901_switch, "lvs2_in" },
	{ "lvs3", QCOM_RPM_PM8901_LVS3, &pm8901_switch, "lvs3_in" },

	{ "mvs", QCOM_RPM_PM8901_MVS, &pm8901_switch, "mvs_in" },
	{ }
};

static const struct rpm_regulator_data rpm_pm8921_regulators[] = {
	{ "s1",  QCOM_RPM_PM8921_SMPS1, &pm8921_smps, "vdd_s1" },
	{ "s2",  QCOM_RPM_PM8921_SMPS2, &pm8921_smps, "vdd_s2" },
	{ "s3",  QCOM_RPM_PM8921_SMPS3, &pm8921_smps },
	{ "s4",  QCOM_RPM_PM8921_SMPS4, &pm8921_smps, "vdd_s4" },
	{ "s7",  QCOM_RPM_PM8921_SMPS7, &pm8921_smps, "vdd_s7" },
	{ "s8",  QCOM_RPM_PM8921_SMPS8, &pm8921_smps, "vdd_s8"  },

	{ "l1",  QCOM_RPM_PM8921_LDO1, &pm8921_nldo, "vdd_l1_l2_l12_l18" },
	{ "l2",  QCOM_RPM_PM8921_LDO2, &pm8921_nldo, "vdd_l1_l2_l12_l18" },
	{ "l3",  QCOM_RPM_PM8921_LDO3, &pm8921_pldo, "vdd_l3_l15_l17" },
	{ "l4",  QCOM_RPM_PM8921_LDO4, &pm8921_pldo, "vdd_l4_l14" },
	{ "l5",  QCOM_RPM_PM8921_LDO5, &pm8921_pldo, "vdd_l5_l8_l16" },
	{ "l6",  QCOM_RPM_PM8921_LDO6, &pm8921_pldo, "vdd_l6_l7" },
	{ "l7",  QCOM_RPM_PM8921_LDO7, &pm8921_pldo, "vdd_l6_l7" },
	{ "l8",  QCOM_RPM_PM8921_LDO8, &pm8921_pldo, "vdd_l5_l8_l16" },
	{ "l9",  QCOM_RPM_PM8921_LDO9, &pm8921_pldo, "vdd_l9_l11" },
	{ "l10", QCOM_RPM_PM8921_LDO10, &pm8921_pldo, "vdd_l10_l22" },
	{ "l11", QCOM_RPM_PM8921_LDO11, &pm8921_pldo, "vdd_l9_l11" },
	{ "l12", QCOM_RPM_PM8921_LDO12, &pm8921_nldo, "vdd_l1_l2_l12_l18" },
	{ "l14", QCOM_RPM_PM8921_LDO14, &pm8921_pldo, "vdd_l4_l14" },
	{ "l15", QCOM_RPM_PM8921_LDO15, &pm8921_pldo, "vdd_l3_l15_l17" },
	{ "l16", QCOM_RPM_PM8921_LDO16, &pm8921_pldo, "vdd_l5_l8_l16" },
	{ "l17", QCOM_RPM_PM8921_LDO17, &pm8921_pldo, "vdd_l3_l15_l17" },
	{ "l18", QCOM_RPM_PM8921_LDO18, &pm8921_nldo, "vdd_l1_l2_l12_l18" },
	{ "l21", QCOM_RPM_PM8921_LDO21, &pm8921_pldo, "vdd_l21_l23_l29" },
	{ "l22", QCOM_RPM_PM8921_LDO22, &pm8921_pldo, "vdd_l10_l22" },
	{ "l23", QCOM_RPM_PM8921_LDO23, &pm8921_pldo, "vdd_l21_l23_l29" },
	{ "l24", QCOM_RPM_PM8921_LDO24, &pm8921_nldo1200, "vdd_l24" },
	{ "l25", QCOM_RPM_PM8921_LDO25, &pm8921_nldo1200, "vdd_l25" },
	{ "l26", QCOM_RPM_PM8921_LDO26, &pm8921_nldo1200, "vdd_l26" },
	{ "l27", QCOM_RPM_PM8921_LDO27, &pm8921_nldo1200, "vdd_l27" },
	{ "l28", QCOM_RPM_PM8921_LDO28, &pm8921_nldo1200, "vdd_l28" },
	{ "l29", QCOM_RPM_PM8921_LDO29, &pm8921_pldo, "vdd_l21_l23_l29" },

	{ "lvs1", QCOM_RPM_PM8921_LVS1, &pm8921_switch, "vin_lvs1_3_6" },
	{ "lvs2", QCOM_RPM_PM8921_LVS2, &pm8921_switch, "vin_lvs2" },
	{ "lvs3", QCOM_RPM_PM8921_LVS3, &pm8921_switch, "vin_lvs1_3_6" },
	{ "lvs4", QCOM_RPM_PM8921_LVS4, &pm8921_switch, "vin_lvs4_5_7" },
	{ "lvs5", QCOM_RPM_PM8921_LVS5, &pm8921_switch, "vin_lvs4_5_7" },
	{ "lvs6", QCOM_RPM_PM8921_LVS6, &pm8921_switch, "vin_lvs1_3_6" },
	{ "lvs7", QCOM_RPM_PM8921_LVS7, &pm8921_switch, "vin_lvs4_5_7" },

	{ "usb-switch", QCOM_RPM_USB_OTG_SWITCH, &pm8921_switch, "vin_5vs" },
	{ "hdmi-switch", QCOM_RPM_HDMI_SWITCH, &pm8921_switch, "vin_5vs" },
	{ "ncp", QCOM_RPM_PM8921_NCP, &pm8921_ncp, "vdd_ncp" },
	{ }
};

static const struct of_device_id rpm_of_match[] = {
	{ .compatible = "qcom,rpm-pm8018-regulators",
		.data = &rpm_pm8018_regulators },
	{ .compatible = "qcom,rpm-pm8058-regulators", .data = &rpm_pm8058_regulators },
	{ .compatible = "qcom,rpm-pm8901-regulators", .data = &rpm_pm8901_regulators },
	{ .compatible = "qcom,rpm-pm8921-regulators", .data = &rpm_pm8921_regulators },
	{ }
};
MODULE_DEVICE_TABLE(of, rpm_of_match);

static int rpm_reg_probe(struct platform_device *pdev)
{
	const struct rpm_regulator_data *reg;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct qcom_rpm_reg *vreg;
	struct qcom_rpm *rpm;

	rpm = dev_get_drvdata(pdev->dev.parent);
	if (!rpm) {
		dev_err(&pdev->dev, "unable to retrieve handle to rpm\n");
		return -ENODEV;
	}

	match = of_match_device(rpm_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "failed to match device\n");
		return -ENODEV;
	}

	for (reg = match->data; reg->name; reg++) {
		vreg = devm_kmalloc(&pdev->dev, sizeof(*vreg), GFP_KERNEL);
		if (!vreg)
			return -ENOMEM;

		memcpy(vreg, reg->template, sizeof(*vreg));
		mutex_init(&vreg->lock);

		vreg->dev = &pdev->dev;
		vreg->resource = reg->resource;
		vreg->rpm = rpm;

		vreg->desc.id = -1;
		vreg->desc.owner = THIS_MODULE;
		vreg->desc.type = REGULATOR_VOLTAGE;
		vreg->desc.name = reg->name;
		vreg->desc.supply_name = reg->supply;
		vreg->desc.of_match = reg->name;
		vreg->desc.of_parse_cb = rpm_reg_of_parse;

		config.dev = &pdev->dev;
		config.driver_data = vreg;
		rdev = devm_regulator_register(&pdev->dev, &vreg->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n", reg->name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static struct platform_driver rpm_reg_driver = {
	.probe          = rpm_reg_probe,
	.driver  = {
		.name  = "qcom_rpm_reg",
		.of_match_table = of_match_ptr(rpm_of_match),
	},
};

static int __init rpm_reg_init(void)
{
	return platform_driver_register(&rpm_reg_driver);
}
subsys_initcall(rpm_reg_init);

static void __exit rpm_reg_exit(void)
{
	platform_driver_unregister(&rpm_reg_driver);
}
module_exit(rpm_reg_exit)

MODULE_DESCRIPTION("Qualcomm RPM regulator driver");
MODULE_LICENSE("GPL v2");
