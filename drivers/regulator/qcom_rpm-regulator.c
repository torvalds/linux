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
	vreg->uV = uV;
	if (vreg->is_enabled)
		ret = rpm_reg_write(vreg, req, vreg->uV / 1000);
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
	vreg->uV = uV;
	if (vreg->is_enabled)
		ret = rpm_reg_write(vreg, req, vreg->uV);
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

static struct regulator_ops uV_ops = {
	.list_voltage = regulator_list_voltage_linear_range,

	.set_voltage_sel = rpm_reg_set_uV_sel,
	.get_voltage = rpm_reg_get_voltage,

	.enable = rpm_reg_uV_enable,
	.disable = rpm_reg_uV_disable,
	.is_enabled = rpm_reg_is_enabled,
};

static struct regulator_ops mV_ops = {
	.list_voltage = regulator_list_voltage_linear_range,

	.set_voltage_sel = rpm_reg_set_mV_sel,
	.get_voltage = rpm_reg_get_voltage,

	.enable = rpm_reg_mV_enable,
	.disable = rpm_reg_mV_disable,
	.is_enabled = rpm_reg_is_enabled,
};

static struct regulator_ops switch_ops = {
	.enable = rpm_reg_switch_enable,
	.disable = rpm_reg_switch_disable,
	.is_enabled = rpm_reg_is_enabled,
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

static const struct of_device_id rpm_of_match[] = {
	{ .compatible = "qcom,rpm-pm8058-pldo",     .data = &pm8058_pldo },
	{ .compatible = "qcom,rpm-pm8058-nldo",     .data = &pm8058_nldo },
	{ .compatible = "qcom,rpm-pm8058-smps",     .data = &pm8058_smps },
	{ .compatible = "qcom,rpm-pm8058-ncp",      .data = &pm8058_ncp },
	{ .compatible = "qcom,rpm-pm8058-switch",   .data = &pm8058_switch },

	{ .compatible = "qcom,rpm-pm8901-pldo",     .data = &pm8901_pldo },
	{ .compatible = "qcom,rpm-pm8901-nldo",     .data = &pm8901_nldo },
	{ .compatible = "qcom,rpm-pm8901-ftsmps",   .data = &pm8901_ftsmps },
	{ .compatible = "qcom,rpm-pm8901-switch",   .data = &pm8901_switch },

	{ .compatible = "qcom,rpm-pm8921-pldo",     .data = &pm8921_pldo },
	{ .compatible = "qcom,rpm-pm8921-nldo",     .data = &pm8921_nldo },
	{ .compatible = "qcom,rpm-pm8921-nldo1200", .data = &pm8921_nldo1200 },
	{ .compatible = "qcom,rpm-pm8921-smps",     .data = &pm8921_smps },
	{ .compatible = "qcom,rpm-pm8921-ftsmps",   .data = &pm8921_ftsmps },
	{ .compatible = "qcom,rpm-pm8921-ncp",      .data = &pm8921_ncp },
	{ .compatible = "qcom,rpm-pm8921-switch",   .data = &pm8921_switch },
	{ }
};
MODULE_DEVICE_TABLE(of, rpm_of_match);

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

static int rpm_reg_of_parse_freq(struct device *dev, struct qcom_rpm_reg *vreg)
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
	ret = of_property_read_u32(dev->of_node, key, &freq);
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

static int rpm_reg_probe(struct platform_device *pdev)
{
	struct regulator_init_data *initdata;
	const struct qcom_rpm_reg *template;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct qcom_rpm_reg *vreg;
	const char *key;
	u32 force_mode;
	bool pwm;
	u32 val;
	int ret;

	match = of_match_device(rpm_of_match, &pdev->dev);
	template = match->data;

	vreg = devm_kmalloc(&pdev->dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg) {
		dev_err(&pdev->dev, "failed to allocate vreg\n");
		return -ENOMEM;
	}
	memcpy(vreg, template, sizeof(*vreg));
	mutex_init(&vreg->lock);
	vreg->dev = &pdev->dev;
	vreg->desc.id = -1;
	vreg->desc.owner = THIS_MODULE;
	vreg->desc.type = REGULATOR_VOLTAGE;
	vreg->desc.name = pdev->dev.of_node->name;

	vreg->rpm = dev_get_drvdata(pdev->dev.parent);
	if (!vreg->rpm) {
		dev_err(&pdev->dev, "unable to retrieve handle to rpm\n");
		return -ENODEV;
	}

	initdata = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node,
					      &vreg->desc);
	if (!initdata)
		return -EINVAL;

	key = "reg";
	ret = of_property_read_u32(pdev->dev.of_node, key, &val);
	if (ret) {
		dev_err(&pdev->dev, "failed to read %s\n", key);
		return ret;
	}
	vreg->resource = val;

	if ((vreg->parts->uV.mask || vreg->parts->mV.mask) &&
	    (!initdata->constraints.min_uV || !initdata->constraints.max_uV)) {
		dev_err(&pdev->dev, "no voltage specified for regulator\n");
		return -EINVAL;
	}

	key = "bias-pull-down";
	if (of_property_read_bool(pdev->dev.of_node, key)) {
		ret = rpm_reg_set(vreg, &vreg->parts->pd, 1);
		if (ret) {
			dev_err(&pdev->dev, "%s is invalid", key);
			return ret;
		}
	}

	if (vreg->parts->freq.mask) {
		ret = rpm_reg_of_parse_freq(&pdev->dev, vreg);
		if (ret < 0)
			return ret;
	}

	if (vreg->parts->pm.mask) {
		key = "qcom,power-mode-hysteretic";
		pwm = !of_property_read_bool(pdev->dev.of_node, key);

		ret = rpm_reg_set(vreg, &vreg->parts->pm, pwm);
		if (ret) {
			dev_err(&pdev->dev, "failed to set power mode\n");
			return ret;
		}
	}

	if (vreg->parts->fm.mask) {
		force_mode = -1;

		key = "qcom,force-mode";
		ret = of_property_read_u32(pdev->dev.of_node, key, &val);
		if (ret == -EINVAL) {
			val = QCOM_RPM_FORCE_MODE_NONE;
		} else if (ret < 0) {
			dev_err(&pdev->dev, "failed to read %s\n", key);
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

		if (force_mode < 0) {
			dev_err(&pdev->dev, "invalid force mode\n");
			return -EINVAL;
		}

		ret = rpm_reg_set(vreg, &vreg->parts->fm, force_mode);
		if (ret) {
			dev_err(&pdev->dev, "failed to set force mode\n");
			return ret;
		}
	}

	config.dev = &pdev->dev;
	config.init_data = initdata;
	config.driver_data = vreg;
	config.of_node = pdev->dev.of_node;
	rdev = devm_regulator_register(&pdev->dev, &vreg->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "can't register regulator\n");
		return PTR_ERR(rdev);
	}

	return 0;
}

static struct platform_driver rpm_reg_driver = {
	.probe          = rpm_reg_probe,
	.driver  = {
		.name  = "qcom_rpm_reg",
		.owner = THIS_MODULE,
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
