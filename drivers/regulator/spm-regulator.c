// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2017, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */


#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/debug-regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/spm-regulator.h>
#include <soc/qcom/spm.h>
#include <linux/arm-smccc.h>

#if defined(CONFIG_ARM64) || (defined(CONFIG_ARM) && defined(CONFIG_ARM_PSCI))
#else
	#define __invoke_psci_fn_smc(a, b, c, d) 0
#endif

#define SPM_REGULATOR_DRIVER_NAME "qcom,spm-regulator"

struct voltage_range {
	int min_uV;
	int set_point_min_uV;
	int max_uV;
	int step_uV;
};

enum qpnp_regulator_uniq_type {
	QPNP_TYPE_HF,
	QPNP_TYPE_FTS2,
	QPNP_TYPE_FTS2p5,
	QPNP_TYPE_FTS426,
	QPNP_TYPE_ULT_HF,
	QPNP_TYPE_HFS430,
};

enum qpnp_regulator_type {
	QPNP_HF_TYPE		= 0x03,
	QPNP_FTS2_TYPE		= 0x1C,
	QPNP_FTS2p5_TYPE	= 0x1C,
	QPNP_FTS426_TYPE	= 0x1C,
	QPNP_ULT_HF_TYPE	= 0x22,
};

enum qpnp_regulator_subtype {
	QPNP_FTS2_SUBTYPE	= 0x08,
	QPNP_HF_SUBTYPE		= 0x08,
	QPNP_FTS2p5_SUBTYPE	= 0x09,
	QPNP_FTS426_SUBTYPE	= 0x0A,
	QPNP_ULT_HF_SUBTYPE	= 0x0D,
	QPNP_HFS430_SUBTYPE	= 0x0A,
};

enum qpnp_logical_mode {
	QPNP_LOGICAL_MODE_AUTO,
	QPNP_LOGICAL_MODE_PWM,
};

static const struct voltage_range fts2_range0 = {0, 350000, 1275000,  5000};
static const struct voltage_range fts2_range1 = {0, 700000, 2040000, 10000};
static const struct voltage_range fts2p5_range0
					 = { 80000, 350000, 1355000,  5000};
static const struct voltage_range fts2p5_range1
					 = {160000, 700000, 2200000, 10000};
static const struct voltage_range fts426_range = {0, 320000, 1352000, 4000};
static const struct voltage_range hfs430_range = {0, 320000, 2040000, 8000};
static const struct voltage_range ult_hf_range0 = {375000, 375000, 1562500,
								12500};
static const struct voltage_range ult_hf_range1 = {750000, 750000, 1525000,
								25000};
static const struct voltage_range hf_range0 = {375000, 375000, 1562500, 12500};
static const struct voltage_range hf_range1 = {1550000, 1550000, 3125000,
								25000};

#define QPNP_SMPS_REG_TYPE		0x04
#define QPNP_SMPS_REG_SUBTYPE		0x05
#define QPNP_SMPS_REG_VOLTAGE_RANGE	0x40
#define QPNP_SMPS_REG_VOLTAGE_SETPOINT	0x41
#define QPNP_SMPS_REG_MODE		0x45
#define QPNP_SMPS_REG_STEP_CTRL		0x61
#define QPNP_SMPS_REG_UL_LL_CTRL	0x68

/* FTS426/HFS430 voltage control registers */
#define QPNP_FTS426_HFS430_REG_VOLTAGE_LB	0x40
#define QPNP_FTS426_HFS430_REG_VOLTAGE_UB	0x41
#define QPNP_FTS426_HFS430_REG_VOLTAGE_VALID_LB	0x42
#define QPNP_FTS426_HFS430_REG_VOLTAGE_VALID_UB	0x43

/* HF voltage limit registers */
#define QPNP_HF_REG_VOLTAGE_ULS		0x69
#define QPNP_HF_REG_VOLTAGE_LLS		0x6B

/* FTS voltage limit registers */
#define QPNP_FTS_REG_VOLTAGE_ULS_VALID	0x6A
#define QPNP_FTS_REG_VOLTAGE_LLS_VALID	0x6C

/* FTS426/HFS430 voltage limit registers */
#define QPNP_FTS426_HFS430_REG_VOLTAGE_ULS_LB	0x68
#define QPNP_FTS426_HFS430_REG_VOLTAGE_ULS_UB	0x69

/* Common regulator UL & LL limits control register layout */
#define QPNP_COMMON_UL_EN_MASK		0x80
#define QPNP_COMMON_LL_EN_MASK		0x40

#define QPNP_SMPS_MODE_PWM		0x80
#define QPNP_SMPS_MODE_AUTO		0x40
#define QPNP_FTS426_HFS430_MODE_PWM	0x07
#define QPNP_FTS426_HFS430_MODE_AUTO	0x06

#define QPNP_SMPS_STEP_CTRL_STEP_MASK	0x18
#define QPNP_SMPS_STEP_CTRL_STEP_SHIFT	3
#define QPNP_SMPS_STEP_CTRL_DELAY_MASK	0x07
#define QPNP_SMPS_STEP_CTRL_DELAY_SHIFT	0
#define QPNP_FTS426_HFS430_STEP_CTRL_DELAY_MASK		0x03
#define QPNP_FTS426_HFS430_STEP_CTRL_DELAY_SHIFT	0

/* Clock rate in kHz of the FTS2 regulator reference clock. */
#define QPNP_SMPS_CLOCK_RATE		19200
#define QPNP_FTS426_CLOCK_RATE		4800
#define QPNP_HFS430_CLOCK_RATE		1600

/* Time to delay in us to ensure that a mode change has completed. */
#define QPNP_FTS2_MODE_CHANGE_DELAY	50

/* Minimum time in us that it takes to complete a single SPMI write. */
#define QPNP_SPMI_WRITE_MIN_DELAY	8

/* Minimum voltage stepper delay for each step. */
#define QPNP_FTS2_STEP_DELAY		8
#define QPNP_HF_STEP_DELAY		20
#define QPNP_FTS426_HFS430_STEP_DELAY	2

/* Arbitrarily large max step size used to avoid possible numerical overflow */
#define SPM_REGULATOR_MAX_STEP_UV	10000000

/*
 * The ratio QPNP_FTS2_STEP_MARGIN_NUM/QPNP_FTS2_STEP_MARGIN_DEN is use to
 * adjust the step rate in order to account for oscillator variance.
 */
#define QPNP_FTS2_STEP_MARGIN_NUM	4
#define QPNP_FTS2_STEP_MARGIN_DEN	5
#define QPNP_FTS426_HFS430_STEP_MARGIN_NUM	10
#define QPNP_FTS426_HFS430_STEP_MARGIN_DEN	11

/*
 * Settling delay for FTS2.5
 * Warm-up=20uS, 0-10% & 90-100% non-linear V-ramp delay = 50uS
 */
#define FTS2P5_SETTLING_DELAY_US	70

/* VSET value to decide the range of ULT SMPS */
#define ULT_SMPS_RANGE_SPLIT 0x60

struct spm_vreg {
	struct regulator_desc		rdesc;
	struct regulator_dev		*rdev;
	struct platform_device		*pdev;
	struct regmap			*regmap;
	const struct voltage_range	*range;
	int				uV;
	int				last_set_uV;
	unsigned int			vlevel;
	unsigned int			last_set_vlevel;
	u32				max_step_uV;
	bool				online;
	u16				spmi_base_addr;
	enum qpnp_logical_mode		init_mode;
	enum qpnp_logical_mode		mode;
	int				step_rate;
	enum qpnp_regulator_uniq_type	regulator_type;
	u32				cpu_num;
	bool				bypass_spm;
	struct regulator_desc		avs_rdesc;
	struct regulator_dev		*avs_rdev;
	int				avs_min_uV;
	int				avs_max_uV;
	bool				avs_enabled;
	u32				recal_cluster_mask;
};

static inline bool spm_regulator_using_avs(struct spm_vreg *vreg)
{
	return vreg->avs_rdev && !vreg->bypass_spm;
}

static int spm_regulator_uv_to_vlevel(struct spm_vreg *vreg, int uV)
{
	int vlevel;

	if (vreg->regulator_type == QPNP_TYPE_FTS426
		|| vreg->regulator_type == QPNP_TYPE_HFS430)
		return roundup(uV, vreg->range->step_uV) / 1000;

	vlevel = DIV_ROUND_UP(uV - vreg->range->min_uV, vreg->range->step_uV);

	/* Fix VSET for ULT HF Buck */
	if (vreg->regulator_type == QPNP_TYPE_ULT_HF
	    && vreg->range == &ult_hf_range1) {
		vlevel &= 0x1F;
		vlevel |= ULT_SMPS_RANGE_SPLIT;
	}

	return vlevel;
}

static int spm_regulator_vlevel_to_uv(struct spm_vreg *vreg, int vlevel)
{
	if (vreg->regulator_type == QPNP_TYPE_FTS426
		|| vreg->regulator_type == QPNP_TYPE_HFS430)
		return vlevel * 1000;
	/*
	 * Calculate ULT HF buck VSET based on range:
	 * In case of range 0: VSET is a 7 bit value.
	 * In case of range 1: VSET is a 5 bit value.
	 */
	if (vreg->regulator_type == QPNP_TYPE_ULT_HF
	    && vreg->range == &ult_hf_range1)
		vlevel &= ~ULT_SMPS_RANGE_SPLIT;

	return vlevel * vreg->range->step_uV + vreg->range->min_uV;
}

static unsigned int spm_regulator_vlevel_to_selector(struct spm_vreg *vreg,
						 unsigned int vlevel)
{
	/* Fix VSET for ULT HF Buck */
	if (vreg->regulator_type == QPNP_TYPE_ULT_HF
	    && vreg->range == &ult_hf_range1)
		vlevel &= ~ULT_SMPS_RANGE_SPLIT;

	if (vreg->regulator_type == QPNP_TYPE_HFS430)
		vlevel = spm_regulator_vlevel_to_uv(vreg, vlevel)
				/ vreg->range->step_uV;

	return vlevel - (vreg->range->set_point_min_uV - vreg->range->min_uV)
				/ vreg->range->step_uV;
}

static int qpnp_smps_read_voltage(struct spm_vreg *vreg)
{
	int rc;
	u8 val[2] = {0};

	if (vreg->regulator_type == QPNP_TYPE_FTS426
		|| vreg->regulator_type == QPNP_TYPE_HFS430) {
		rc = regmap_bulk_read(vreg->regmap,
				vreg->spmi_base_addr
				+ QPNP_FTS426_HFS430_REG_VOLTAGE_VALID_LB,
				val, 2);
		if (rc) {
			dev_err(&vreg->pdev->dev, "%s: could not read voltage setpoint registers, rc=%d\n",
				__func__, rc);
			return rc;
		}

		vreg->last_set_vlevel = ((unsigned int)val[1] << 8) | val[0];
	} else {
		rc = regmap_bulk_read(vreg->regmap,
			vreg->spmi_base_addr + QPNP_SMPS_REG_VOLTAGE_SETPOINT,
				val, 1);
		if (rc) {
			dev_err(&vreg->pdev->dev, "%s: could not read voltage setpoint register, rc=%d\n",
				__func__, rc);
			return rc;
		}
		vreg->last_set_vlevel = val[0];
	}

	vreg->last_set_uV = spm_regulator_vlevel_to_uv(vreg,
						vreg->last_set_vlevel);
	return rc;
}

static int qpnp_smps_write_voltage(struct spm_vreg *vreg, unsigned int vlevel)
{
	int rc = 0;
	u8 reg[2];

	/* Set voltage control registers via SPMI. */
	reg[0] = vlevel & 0xFF;
	reg[1] = (vlevel >> 8) & 0xFF;

	if (vreg->regulator_type == QPNP_TYPE_FTS426
		|| vreg->regulator_type == QPNP_TYPE_HFS430) {
		rc = regmap_bulk_write(vreg->regmap,
			  vreg->spmi_base_addr
			  + QPNP_FTS426_HFS430_REG_VOLTAGE_LB,
			  reg, 2);
	} else {
		rc = regmap_write(vreg->regmap,
			  vreg->spmi_base_addr + QPNP_SMPS_REG_VOLTAGE_SETPOINT,
			  reg[0]);
	}

	if (rc)
		pr_err("%s: regmap_write failed, rc=%d\n",
			vreg->rdesc.name, rc);

	return rc;
}

static inline enum qpnp_logical_mode qpnp_regval_to_mode(struct spm_vreg *vreg,
							u8 regval)
{
	if (vreg->regulator_type == QPNP_TYPE_FTS426
		|| vreg->regulator_type == QPNP_TYPE_HFS430)
		return (regval == QPNP_FTS426_HFS430_MODE_PWM)
			? QPNP_LOGICAL_MODE_PWM : QPNP_LOGICAL_MODE_AUTO;
	else
		return (regval & QPNP_SMPS_MODE_PWM)
			? QPNP_LOGICAL_MODE_PWM : QPNP_LOGICAL_MODE_AUTO;
}

static inline u8 qpnp_mode_to_regval(struct spm_vreg *vreg,
					enum qpnp_logical_mode mode)
{
	if (vreg->regulator_type == QPNP_TYPE_FTS426
		|| vreg->regulator_type == QPNP_TYPE_HFS430)
		return (mode == QPNP_LOGICAL_MODE_PWM)
			? QPNP_FTS426_HFS430_MODE_PWM
			: QPNP_FTS426_HFS430_MODE_AUTO;
	else
		return (mode == QPNP_LOGICAL_MODE_PWM)
			? QPNP_SMPS_MODE_PWM : QPNP_SMPS_MODE_AUTO;
}

static int qpnp_smps_set_mode(struct spm_vreg *vreg, u8 mode)
{
	int rc;

	rc = regmap_write(vreg->regmap,
			  vreg->spmi_base_addr + QPNP_SMPS_REG_MODE,
			  qpnp_mode_to_regval(vreg, mode));
	if (rc)
		dev_err(&vreg->pdev->dev,
			"%s: could not write to mode register, rc=%d\n",
			__func__, rc);

	return rc;
}

static int spm_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);
	int vlevel, rc;

	if (spm_regulator_using_avs(vreg)) {
		vlevel = msm_spm_get_vdd(vreg->cpu_num);

		if (vlevel < 0) {
			pr_debug("%s: msm_spm_get_vdd failed, rc=%d; falling back on SPMI read\n",
				vreg->rdesc.name, vlevel);

			rc = qpnp_smps_read_voltage(vreg);
			if (rc) {
				pr_err("%s: voltage read failed, rc=%d\n",
				       vreg->rdesc.name, rc);
				return rc;
			}

			return vreg->last_set_uV;
		}

		vreg->last_set_vlevel = vlevel;
		vreg->last_set_uV = spm_regulator_vlevel_to_uv(vreg, vlevel);

		return vreg->last_set_uV;
	} else {
		return vreg->uV;
	}
};

static int spm_regulator_write_voltage(struct spm_vreg *vreg, int uV)
{
	unsigned int vlevel = spm_regulator_uv_to_vlevel(vreg, uV);
	bool spm_failed = false;
	int rc = 0;
	u32 slew_delay;

	if (likely(!vreg->bypass_spm)) {
		/* Set voltage control register via SPM. */
		rc = msm_spm_set_vdd(vreg->cpu_num, vlevel);
		if (rc) {
			pr_debug("%s: msm_spm_set_vdd failed, rc=%d; falling back on SPMI write\n",
				vreg->rdesc.name, rc);
			spm_failed = true;
		}
	}

	if (unlikely(vreg->bypass_spm || spm_failed)) {
		rc = qpnp_smps_write_voltage(vreg, vlevel);
		if (rc) {
			pr_err("%s: voltage write failed, rc=%d\n",
				vreg->rdesc.name, rc);
			return rc;
		}
	}

	if (uV > vreg->last_set_uV) {
		/* Wait for voltage stepping to complete. */
		slew_delay = DIV_ROUND_UP(uV - vreg->last_set_uV,
					vreg->step_rate);
		if (vreg->regulator_type == QPNP_TYPE_FTS2p5)
			slew_delay += FTS2P5_SETTLING_DELAY_US;
		udelay(slew_delay);
	} else if (vreg->regulator_type == QPNP_TYPE_FTS2p5) {
		/* add the ramp-down delay */
		slew_delay = DIV_ROUND_UP(vreg->last_set_uV - uV,
				vreg->step_rate) + FTS2P5_SETTLING_DELAY_US;
		udelay(slew_delay);
	}

	vreg->last_set_uV = uV;
	vreg->last_set_vlevel = vlevel;

	return rc;
}

static int spm_regulator_recalibrate(struct spm_vreg *vreg)
{
	struct arm_smccc_res res;

	if (!vreg->recal_cluster_mask)
		return 0;

	arm_smccc_smc(0xC4000020, vreg->recal_cluster_mask,
		2, 0, 0, 0, 0, 0, &res);
	if (res.a0)
		pr_err("%s: recalibration failed, rc=%ld\n", vreg->rdesc.name,
			res.a0);

	return res.a0;
}

static int _spm_regulator_set_voltage(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);
	bool pwm_required;
	int rc = 0;
	int uV;

	rc = spm_regulator_get_voltage(rdev);
	if (rc < 0)
		return rc;

	if (vreg->vlevel == vreg->last_set_vlevel)
		return 0;

	pwm_required = (vreg->regulator_type == QPNP_TYPE_FTS2)
			&& (vreg->init_mode != QPNP_LOGICAL_MODE_PWM)
			&& vreg->uV > vreg->last_set_uV;

	if (pwm_required) {
		/* Switch to PWM mode so that voltage ramping is fast. */
		rc = qpnp_smps_set_mode(vreg, QPNP_LOGICAL_MODE_PWM);
		if (rc)
			return rc;
	}

	do {
		uV = vreg->uV > vreg->last_set_uV
		    ? min(vreg->uV, vreg->last_set_uV + (int)vreg->max_step_uV)
		    : max(vreg->uV, vreg->last_set_uV - (int)vreg->max_step_uV);

		rc = spm_regulator_write_voltage(vreg, uV);
		if (rc)
			return rc;
	} while (vreg->last_set_uV != vreg->uV);

	if (pwm_required) {
		/* Wait for mode transition to complete. */
		udelay(QPNP_FTS2_MODE_CHANGE_DELAY - QPNP_SPMI_WRITE_MIN_DELAY);
		/* Switch to AUTO mode so that power consumption is lowered. */
		rc = qpnp_smps_set_mode(vreg, QPNP_LOGICAL_MODE_AUTO);
		if (rc)
			return rc;
	}

	rc = spm_regulator_recalibrate(vreg);

	return rc;
}

static int spm_regulator_set_voltage(struct regulator_dev *rdev, int min_uV,
					int max_uV, unsigned int *selector)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);
	const struct voltage_range *range = vreg->range;
	int uV = min_uV;
	unsigned int vlevel;

	if (uV < range->set_point_min_uV && max_uV >= range->set_point_min_uV)
		uV = range->set_point_min_uV;

	if (uV < range->set_point_min_uV || uV > range->max_uV) {
		pr_err("%s: request v=[%d, %d] is outside possible v=[%d, %d]\n",
			vreg->rdesc.name, min_uV, max_uV,
			range->set_point_min_uV, range->max_uV);
		return -EINVAL;
	}

	vlevel = spm_regulator_uv_to_vlevel(vreg, uV);
	uV = spm_regulator_vlevel_to_uv(vreg, vlevel);

	if (uV > max_uV) {
		pr_err("%s: request v=[%d, %d] cannot be met by any set point\n",
			vreg->rdesc.name, min_uV, max_uV);
		return -EINVAL;
	}

	*selector = spm_regulator_vlevel_to_selector(vreg, vlevel);
	vreg->vlevel = vlevel;
	vreg->uV = uV;

	if (!vreg->online)
		return 0;

	return _spm_regulator_set_voltage(rdev);
}

static int spm_regulator_list_voltage(struct regulator_dev *rdev,
					unsigned int selector)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);

	if (selector >= vreg->rdesc.n_voltages)
		return 0;

	return selector * vreg->range->step_uV + vreg->range->set_point_min_uV;
}

static int spm_regulator_enable(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);
	int rc;

	rc = _spm_regulator_set_voltage(rdev);

	if (!rc)
		vreg->online = true;

	return rc;
}

static int spm_regulator_disable(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);

	vreg->online = false;

	return 0;
}

static int spm_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->online;
}

static unsigned int spm_regulator_get_mode(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->mode == QPNP_LOGICAL_MODE_PWM
			? REGULATOR_MODE_NORMAL : REGULATOR_MODE_IDLE;
}

static int spm_regulator_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);

	/*
	 * Map REGULATOR_MODE_NORMAL to PWM mode and REGULATOR_MODE_IDLE to
	 * init_mode.  This ensures that the regulator always stays in PWM mode
	 * in the case that qcom,mode has been specified as "pwm" in device
	 * tree.
	 */
	vreg->mode = (mode == REGULATOR_MODE_NORMAL) ? QPNP_LOGICAL_MODE_PWM
						     : vreg->init_mode;

	return qpnp_smps_set_mode(vreg, vreg->mode);
}

static const struct regulator_ops spm_regulator_ops = {
	.get_voltage	= spm_regulator_get_voltage,
	.set_voltage	= spm_regulator_set_voltage,
	.list_voltage	= spm_regulator_list_voltage,
	.get_mode	= spm_regulator_get_mode,
	.set_mode	= spm_regulator_set_mode,
	.enable		= spm_regulator_enable,
	.disable	= spm_regulator_disable,
	.is_enabled	= spm_regulator_is_enabled,
};

static int spm_regulator_avs_set_voltage(struct regulator_dev *rdev, int min_uV,
					int max_uV, unsigned int *selector)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);
	const struct voltage_range *range = vreg->range;
	unsigned int vlevel_min, vlevel_max;
	int uV, avs_min_uV, avs_max_uV, rc;

	uV = min_uV;

	if (uV < range->set_point_min_uV && max_uV >= range->set_point_min_uV)
		uV = range->set_point_min_uV;

	if (uV < range->set_point_min_uV || uV > range->max_uV) {
		pr_err("%s: request v=[%d, %d] is outside possible v=[%d, %d]\n",
			vreg->avs_rdesc.name, min_uV, max_uV,
			range->set_point_min_uV, range->max_uV);
		return -EINVAL;
	}

	vlevel_min = spm_regulator_uv_to_vlevel(vreg, uV);
	avs_min_uV = spm_regulator_vlevel_to_uv(vreg, vlevel_min);

	if (avs_min_uV > max_uV) {
		pr_err("%s: request v=[%d, %d] cannot be met by any set point\n",
			vreg->avs_rdesc.name, min_uV, max_uV);
		return -EINVAL;
	}

	uV = max_uV;

	if (uV > range->max_uV && min_uV <= range->max_uV)
		uV = range->max_uV;

	if (uV < range->set_point_min_uV || uV > range->max_uV) {
		pr_err("%s: request v=[%d, %d] is outside possible v=[%d, %d]\n",
			vreg->avs_rdesc.name, min_uV, max_uV,
			range->set_point_min_uV, range->max_uV);
		return -EINVAL;
	}

	vlevel_max = spm_regulator_uv_to_vlevel(vreg, uV);
	avs_max_uV = spm_regulator_vlevel_to_uv(vreg, vlevel_max);

	if (avs_max_uV < min_uV) {
		pr_err("%s: request v=[%d, %d] cannot be met by any set point\n",
			vreg->avs_rdesc.name, min_uV, max_uV);
		return -EINVAL;
	}

	if (likely(!vreg->bypass_spm)) {
		rc = msm_spm_avs_set_limit(vreg->cpu_num, vlevel_min,
						vlevel_max);
		if (rc) {
			pr_err("%s: AVS limit setting failed, rc=%d\n",
				vreg->avs_rdesc.name, rc);
			return rc;
		}
	}

	*selector = spm_regulator_vlevel_to_selector(vreg, vlevel_min);
	vreg->avs_min_uV = avs_min_uV;
	vreg->avs_max_uV = avs_max_uV;

	return 0;
}

static int spm_regulator_avs_get_voltage(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->avs_min_uV;
}

static int spm_regulator_avs_enable(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);
	int rc;

	if (likely(!vreg->bypass_spm)) {
		rc = msm_spm_avs_enable(vreg->cpu_num);
		if (rc) {
			pr_err("%s: AVS enable failed, rc=%d\n",
				vreg->avs_rdesc.name, rc);
			return rc;
		}
	}

	vreg->avs_enabled = true;

	return 0;
}

static int spm_regulator_avs_disable(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);
	int rc;

	if (likely(!vreg->bypass_spm)) {
		rc = msm_spm_avs_disable(vreg->cpu_num);
		if (rc) {
			pr_err("%s: AVS disable failed, rc=%d\n",
				vreg->avs_rdesc.name, rc);
			return rc;
		}
	}

	vreg->avs_enabled = false;

	return 0;
}

static int spm_regulator_avs_is_enabled(struct regulator_dev *rdev)
{
	struct spm_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->avs_enabled;
}

static const struct regulator_ops spm_regulator_avs_ops = {
	.get_voltage	= spm_regulator_avs_get_voltage,
	.set_voltage	= spm_regulator_avs_set_voltage,
	.list_voltage	= spm_regulator_list_voltage,
	.enable		= spm_regulator_avs_enable,
	.disable	= spm_regulator_avs_disable,
	.is_enabled	= spm_regulator_avs_is_enabled,
};

static int qpnp_smps_check_type(struct spm_vreg *vreg)
{
	int rc;
	u8 type[2];

	rc = regmap_bulk_read(vreg->regmap,
			      vreg->spmi_base_addr + QPNP_SMPS_REG_TYPE,
			      type,
			      2);
	if (rc) {
		dev_err(&vreg->pdev->dev,
			"%s: could not read type register, rc=%d\n",
			__func__, rc);
		return rc;
	}

	if (type[0] == QPNP_FTS2_TYPE && type[1] == QPNP_FTS2_SUBTYPE) {
		vreg->regulator_type = QPNP_TYPE_FTS2;
	} else if (type[0] == QPNP_FTS2p5_TYPE
					&& type[1] == QPNP_FTS2p5_SUBTYPE) {
		vreg->regulator_type = QPNP_TYPE_FTS2p5;
	} else if (type[0] == QPNP_FTS426_TYPE
					&& type[1] == QPNP_FTS426_SUBTYPE) {
		vreg->regulator_type = QPNP_TYPE_FTS426;
	} else if (type[0] == QPNP_HF_TYPE
					&& type[1] == QPNP_HFS430_SUBTYPE) {
		vreg->regulator_type = QPNP_TYPE_HFS430;
	} else if (type[0] == QPNP_ULT_HF_TYPE
					&& type[1] == QPNP_ULT_HF_SUBTYPE) {
		vreg->regulator_type = QPNP_TYPE_ULT_HF;
	} else if (type[0] == QPNP_HF_TYPE
					&& type[1] == QPNP_HF_SUBTYPE) {
		vreg->regulator_type = QPNP_TYPE_HF;
	} else {
		dev_err(&vreg->pdev->dev,
			"%s: invalid type=0x%02X, subtype=0x%02X register pair\n",
			 __func__, type[0], type[1]);
		return -ENODEV;
	}

	return rc;
}

static int qpnp_smps_init_range(struct spm_vreg *vreg,
	const struct voltage_range *range0, const struct voltage_range *range1)
{
	int rc;
	u8 reg = 0;
	uint val;

	rc = regmap_read(vreg->regmap,
			 vreg->spmi_base_addr + QPNP_SMPS_REG_VOLTAGE_RANGE,
			 &val);
	if (rc) {
		dev_err(&vreg->pdev->dev,
			"%s: could not read voltage range register, rc=%d\n",
			__func__, rc);
		return rc;
	}
	reg = (u8)val;

	if (reg == 0x00) {
		vreg->range = range0;
	} else if (reg == 0x01) {
		vreg->range = range1;
	} else {
		dev_err(&vreg->pdev->dev, "%s: voltage range=%d is invalid\n",
			__func__, reg);
		rc = -EINVAL;
	}

	return rc;
}

static int qpnp_ult_hf_init_range(struct spm_vreg *vreg)
{
	int rc;
	u8 reg = 0;
	uint val;

	rc = regmap_read(vreg->regmap,
			 vreg->spmi_base_addr + QPNP_SMPS_REG_VOLTAGE_SETPOINT,
			 &val);
	if (rc) {
		dev_err(&vreg->pdev->dev,
			"%s: could not read voltage range register, rc=%d\n",
			__func__, rc);
		return rc;
	}
	reg = (u8)val;

	vreg->range = (reg < ULT_SMPS_RANGE_SPLIT) ? &ult_hf_range0 :
							&ult_hf_range1;
	return rc;
}

static int qpnp_smps_init_voltage(struct spm_vreg *vreg)
{
	int rc;

	rc = qpnp_smps_read_voltage(vreg);
	if (rc) {
		pr_err("%s: voltage read failed, rc=%d\n", vreg->rdesc.name,
			rc);
		return rc;
	}

	vreg->vlevel = vreg->last_set_vlevel;
	vreg->uV = vreg->last_set_uV;

	/* Initialize SAW voltage control register */
	if (!vreg->bypass_spm) {
		rc = msm_spm_set_vdd(vreg->cpu_num, vreg->vlevel);
		if (rc)
			pr_err("%s: msm_spm_set_vdd failed, rc=%d\n",
			       vreg->rdesc.name, rc);
	}

	return 0;
}

static int qpnp_smps_init_mode(struct spm_vreg *vreg)
{
	const char *mode_name;
	int rc;
	uint val;

	rc = of_property_read_string(vreg->pdev->dev.of_node, "qcom,mode",
					&mode_name);
	if (!rc) {
		if (strcmp("pwm", mode_name) == 0) {
			vreg->init_mode = QPNP_LOGICAL_MODE_PWM;
		} else if ((strcmp("auto", mode_name) == 0) &&
				(vreg->regulator_type != QPNP_TYPE_ULT_HF)) {
			vreg->init_mode = QPNP_LOGICAL_MODE_AUTO;
		} else {
			dev_err(&vreg->pdev->dev,
				"%s: unknown regulator mode: %s\n",
				__func__, mode_name);
			return -EINVAL;
		}

		rc = qpnp_smps_set_mode(vreg, vreg->init_mode);
		if (rc)
			return rc;
	} else {
		rc = regmap_read(vreg->regmap,
				 vreg->spmi_base_addr + QPNP_SMPS_REG_MODE,
				 &val);
		if (rc)
			dev_err(&vreg->pdev->dev,
				"%s: could not read mode register, rc=%d\n",
				__func__, rc);
			 vreg->init_mode = qpnp_regval_to_mode(vreg, val);
	}

	vreg->mode = vreg->init_mode;

	return rc;
}

static int qpnp_smps_init_step_rate(struct spm_vreg *vreg)
{
	int rc;
	u8 reg = 0;
	int step = 0, delay;
	uint val;

	rc = regmap_read(vreg->regmap,
			 vreg->spmi_base_addr + QPNP_SMPS_REG_STEP_CTRL, &val);
	if (rc) {
		dev_err(&vreg->pdev->dev,
			"%s: could not read stepping control register, rc=%d\n",
			__func__, rc);
		return rc;
	}
	reg = (u8)val;

	/* ULT and FTS426 bucks do not support steps */
	if (vreg->regulator_type != QPNP_TYPE_ULT_HF && vreg->regulator_type !=
		QPNP_TYPE_FTS426  && vreg->regulator_type != QPNP_TYPE_HFS430)
		step = (reg & QPNP_SMPS_STEP_CTRL_STEP_MASK)
			>> QPNP_SMPS_STEP_CTRL_STEP_SHIFT;

	if (vreg->regulator_type == QPNP_TYPE_FTS426
		|| vreg->regulator_type == QPNP_TYPE_HFS430) {
		delay = (reg & QPNP_FTS426_HFS430_STEP_CTRL_DELAY_MASK)
			>> QPNP_FTS426_HFS430_STEP_CTRL_DELAY_SHIFT;

		/* step_rate has units of uV/us. */
		vreg->step_rate = ((vreg->regulator_type == QPNP_TYPE_FTS426)
					? QPNP_FTS426_CLOCK_RATE
					: QPNP_HFS430_CLOCK_RATE)
					* vreg->range->step_uV;
	} else {
		delay = (reg & QPNP_SMPS_STEP_CTRL_DELAY_MASK)
			>> QPNP_SMPS_STEP_CTRL_DELAY_SHIFT;

		/* step_rate has units of uV/us. */
		vreg->step_rate = QPNP_SMPS_CLOCK_RATE * vreg->range->step_uV
					* (1 << step);
	}

	if ((vreg->regulator_type == QPNP_TYPE_ULT_HF)
			|| (vreg->regulator_type == QPNP_TYPE_HF))
		vreg->step_rate /= 1000 * (QPNP_HF_STEP_DELAY << delay);
	else if (vreg->regulator_type == QPNP_TYPE_FTS426
			|| vreg->regulator_type == QPNP_TYPE_HFS430)
		vreg->step_rate /= 1000 * (QPNP_FTS426_HFS430_STEP_DELAY
						<< delay);
	else
		vreg->step_rate /= 1000 * (QPNP_FTS2_STEP_DELAY << delay);

	if (vreg->regulator_type == QPNP_TYPE_FTS426
			|| vreg->regulator_type == QPNP_TYPE_HFS430)
		vreg->step_rate = vreg->step_rate
					* QPNP_FTS426_HFS430_STEP_MARGIN_NUM
					/ QPNP_FTS426_HFS430_STEP_MARGIN_DEN;
	else
		vreg->step_rate = vreg->step_rate * QPNP_FTS2_STEP_MARGIN_NUM
					/ QPNP_FTS2_STEP_MARGIN_DEN;

	/* Ensure that the stepping rate is greater than 0. */
	vreg->step_rate = max(vreg->step_rate, 1);

	return rc;
}

static int qpnp_smps_check_constraints(struct spm_vreg *vreg,
					struct regulator_init_data *init_data)
{
	int rc = 0, limit_min_uV, limit_max_uV;
	u16 ul_reg, ll_reg;
	u8 reg[2];

	limit_min_uV = 0;
	limit_max_uV = INT_MAX;

	ul_reg = QPNP_FTS_REG_VOLTAGE_ULS_VALID;
	ll_reg = QPNP_FTS_REG_VOLTAGE_LLS_VALID;

	switch (vreg->regulator_type) {
	case QPNP_TYPE_HF:
		ul_reg = QPNP_HF_REG_VOLTAGE_ULS;
		ll_reg = QPNP_HF_REG_VOLTAGE_LLS;
		fallthrough;
	case QPNP_TYPE_FTS2:
	case QPNP_TYPE_FTS2p5:
		rc = regmap_bulk_read(vreg->regmap, vreg->spmi_base_addr
					+ QPNP_SMPS_REG_UL_LL_CTRL, reg, 1);
		if (rc) {
			dev_err(&vreg->pdev->dev, "%s: UL_LL register read failed, rc=%d\n",
				__func__, rc);
			return rc;
		}

		if (reg[0] & QPNP_COMMON_UL_EN_MASK) {
			rc = regmap_bulk_read(vreg->regmap, vreg->spmi_base_addr
						+ ul_reg, &reg[1], 1);
			if (rc) {
				dev_err(&vreg->pdev->dev, "%s: ULS register read failed, rc=%d\n",
					__func__, rc);
				return rc;
			}

			limit_max_uV = spm_regulator_vlevel_to_uv(vreg, reg[1]);
		}

		if (reg[0] & QPNP_COMMON_LL_EN_MASK) {
			rc = regmap_bulk_read(vreg->regmap, vreg->spmi_base_addr
						+ ll_reg, &reg[1], 1);
			if (rc) {
				dev_err(&vreg->pdev->dev, "%s: LLS register read failed, rc=%d\n",
					__func__, rc);
				return rc;
			}

			limit_min_uV = spm_regulator_vlevel_to_uv(vreg, reg[1]);
		}

		break;
	case QPNP_TYPE_FTS426:
	case QPNP_TYPE_HFS430:
		rc = regmap_bulk_read(vreg->regmap, vreg->spmi_base_addr
					+ QPNP_FTS426_HFS430_REG_VOLTAGE_ULS_LB,
					reg, 2);
		if (rc) {
			dev_err(&vreg->pdev->dev, "%s: could not read voltage limit registers, rc=%d\n",
				__func__, rc);
			return rc;
		}

		limit_max_uV = spm_regulator_vlevel_to_uv(vreg,
					((unsigned int)reg[1] << 8) | reg[0]);
		break;
	case QPNP_TYPE_ULT_HF:
		/* no HW voltage limit configuration */
		break;
	}

	if (init_data->constraints.min_uV < limit_min_uV
	    || init_data->constraints.max_uV >  limit_max_uV) {
		dev_err(&vreg->pdev->dev, "regulator min/max(%d/%d) constraints do not fit within HW configured min/max(%d/%d) constraints\n",
			init_data->constraints.min_uV,
			init_data->constraints.max_uV, limit_min_uV,
			limit_max_uV);
		return -EINVAL;
	}

	return rc;
}

static bool spm_regulator_using_range0(struct spm_vreg *vreg)
{
	return vreg->range == &fts2_range0 || vreg->range == &fts2p5_range0
		|| vreg->range == &ult_hf_range0 || vreg->range == &hf_range0
		|| vreg->range == &fts426_range;
}

/* Register a regulator to enable/disable AVS and set AVS min/max limits. */
static int spm_regulator_avs_register(struct spm_vreg *vreg,
				struct device *dev, struct device_node *node)
{
	struct regulator_config reg_config = {};
	struct device_node *avs_node = NULL;
	struct device_node *child_node;
	struct regulator_init_data *init_data;
	int rc;

	/*
	 * Find the first available child node (if any).  It corresponds to an
	 * AVS limits regulator.
	 */
	for_each_available_child_of_node(node, child_node) {
		avs_node = child_node;
		break;
	}

	if (!avs_node)
		return 0;

	init_data = of_get_regulator_init_data(dev, avs_node, &vreg->avs_rdesc);
	if (!init_data) {
		dev_err(dev, "%s: unable to allocate memory\n", __func__);
		return -ENOMEM;
	}
	init_data->constraints.input_uV = init_data->constraints.max_uV;
	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_STATUS
						| REGULATOR_CHANGE_VOLTAGE;

	if (!init_data->constraints.name) {
		dev_err(dev, "%s: AVS node is missing regulator name\n",
			__func__);
		return -EINVAL;
	}

	vreg->avs_rdesc.name	= init_data->constraints.name;
	vreg->avs_rdesc.type	= REGULATOR_VOLTAGE;
	vreg->avs_rdesc.owner	= THIS_MODULE;
	vreg->avs_rdesc.ops	= &spm_regulator_avs_ops;
	vreg->avs_rdesc.n_voltages
		= (vreg->range->max_uV - vreg->range->set_point_min_uV)
			/ vreg->range->step_uV + 1;

	reg_config.dev = dev;
	reg_config.init_data = init_data;
	reg_config.driver_data = vreg;
	reg_config.of_node = avs_node;

	vreg->avs_rdev = regulator_register(dev, &vreg->avs_rdesc, &reg_config);
	if (IS_ERR(vreg->avs_rdev)) {
		rc = PTR_ERR(vreg->avs_rdev);
		dev_err(dev, "%s: AVS regulator_register failed, rc=%d\n",
			__func__, rc);
		return rc;
	}

	if (vreg->bypass_spm)
		pr_debug("%s: SPM bypassed so AVS regulator calls are no-ops\n",
			vreg->avs_rdesc.name);

	return 0;
}

static int spm_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config reg_config = {};
	struct device_node *node = pdev->dev.of_node;
	struct regulator_init_data *init_data;
	struct spm_vreg *vreg;
	unsigned int base;
	bool bypass_spm;
	int rc;

	if (!node) {
		dev_err(&pdev->dev, "%s: device node missing\n", __func__);
		return -ENODEV;
	}

	bypass_spm = of_property_read_bool(node, "qcom,bypass-spm");
	if (!bypass_spm) {
		rc = msm_spm_probe_done();
		if (rc) {
			if (rc != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"%s: spm unavailable, rc=%d\n",
					__func__, rc);
			return rc;
		}
	}

	vreg = devm_kzalloc(&pdev->dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	vreg->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!vreg->regmap) {
		dev_err(&pdev->dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}
	vreg->pdev = pdev;
	vreg->bypass_spm = bypass_spm;

	rc = of_property_read_u32(pdev->dev.of_node, "reg", &base);
	if (rc < 0) {
		dev_err(&pdev->dev,
			"Couldn't find reg in node = %s rc = %d\n",
			pdev->dev.of_node->full_name, rc);
		return rc;
	}
	vreg->spmi_base_addr = base;

	rc = qpnp_smps_check_type(vreg);
	if (rc)
		return rc;

	/* Specify CPU 0 as default in order to handle shared regulator case. */
	vreg->cpu_num = 0;
	of_property_read_u32(vreg->pdev->dev.of_node, "qcom,cpu-num",
						&vreg->cpu_num);

	of_property_read_u32(vreg->pdev->dev.of_node, "qcom,recal-mask",
						&vreg->recal_cluster_mask);

	/*
	 * The regulator must be initialized to range 0 or range 1 during
	 * PMIC power on sequence.  Once it is set, it cannot be changed
	 * dynamically.
	 */
	if (vreg->regulator_type == QPNP_TYPE_FTS2)
		rc = qpnp_smps_init_range(vreg, &fts2_range0, &fts2_range1);
	else if (vreg->regulator_type == QPNP_TYPE_FTS2p5)
		rc = qpnp_smps_init_range(vreg, &fts2p5_range0, &fts2p5_range1);
	else if (vreg->regulator_type == QPNP_TYPE_FTS426)
		vreg->range = &fts426_range;
	else if (vreg->regulator_type == QPNP_TYPE_HFS430)
		vreg->range = &hfs430_range;
	else if (vreg->regulator_type == QPNP_TYPE_HF)
		rc = qpnp_smps_init_range(vreg, &hf_range0, &hf_range1);
	else if (vreg->regulator_type == QPNP_TYPE_ULT_HF)
		rc = qpnp_ult_hf_init_range(vreg);
	if (rc)
		return rc;

	rc = qpnp_smps_init_voltage(vreg);
	if (rc)
		return rc;

	rc = qpnp_smps_init_mode(vreg);
	if (rc)
		return rc;

	rc = qpnp_smps_init_step_rate(vreg);
	if (rc)
		return rc;

	init_data = of_get_regulator_init_data(&pdev->dev, node, &vreg->rdesc);
	if (!init_data) {
		dev_err(&pdev->dev, "%s: unable to allocate memory\n",
				__func__);
		return -ENOMEM;
	}
	init_data->constraints.input_uV = init_data->constraints.max_uV;
	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_STATUS
			| REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE;
	init_data->constraints.valid_modes_mask
				= REGULATOR_MODE_NORMAL | REGULATOR_MODE_IDLE;

	if (!init_data->constraints.name) {
		dev_err(&pdev->dev, "%s: node is missing regulator name\n",
			__func__);
		return -EINVAL;
	}

	rc = qpnp_smps_check_constraints(vreg, init_data);
	if (rc) {
		dev_err(&pdev->dev, "%s: regulator constraints check failed, rc=%d\n",
			__func__, rc);
		return rc;
	}

	vreg->rdesc.name	= init_data->constraints.name;
	vreg->rdesc.type	= REGULATOR_VOLTAGE;
	vreg->rdesc.owner	= THIS_MODULE;
	vreg->rdesc.ops		= &spm_regulator_ops;
	vreg->rdesc.n_voltages
		= (vreg->range->max_uV - vreg->range->set_point_min_uV)
			/ vreg->range->step_uV + 1;

	vreg->max_step_uV = SPM_REGULATOR_MAX_STEP_UV;
	of_property_read_u32(vreg->pdev->dev.of_node,
				"qcom,max-voltage-step", &vreg->max_step_uV);

	if (vreg->max_step_uV > SPM_REGULATOR_MAX_STEP_UV)
		vreg->max_step_uV = SPM_REGULATOR_MAX_STEP_UV;

	vreg->max_step_uV = rounddown(vreg->max_step_uV, vreg->range->step_uV);
	pr_debug("%s: max single voltage step size=%u uV\n",
		vreg->rdesc.name, vreg->max_step_uV);

	reg_config.dev = &pdev->dev;
	reg_config.init_data = init_data;
	reg_config.driver_data = vreg;
	reg_config.of_node = node;
	vreg->rdev = regulator_register(&pdev->dev, &vreg->rdesc, &reg_config);

	if (IS_ERR(vreg->rdev)) {
		rc = PTR_ERR(vreg->rdev);
		dev_err(&pdev->dev, "%s: regulator_register failed, rc=%d\n",
			__func__, rc);
		return rc;
	}

	rc = spm_regulator_avs_register(vreg, &pdev->dev, node);
	if (rc) {
		regulator_unregister(vreg->rdev);
		return rc;
	}

	dev_set_drvdata(&pdev->dev, vreg);

	rc = devm_regulator_debug_register(&pdev->dev, vreg->rdev);
	if (rc)
		dev_err(&pdev->dev, "Error registering SPM debug regulator, rc=%d\n",
						rc);

	pr_info("name=%s, range=%s, voltage=%d uV, mode=%s, step rate=%d uV/us\n",
		vreg->rdesc.name,
		spm_regulator_using_range0(vreg) ? "LV" : "MV",
		vreg->uV,
		vreg->init_mode == QPNP_LOGICAL_MODE_PWM ? "PWM" :
		   (vreg->init_mode == QPNP_LOGICAL_MODE_AUTO ? "AUTO" : "PFM"),
		vreg->step_rate);

	return rc;
}

static int spm_regulator_remove(struct platform_device *pdev)
{
	struct spm_vreg *vreg = dev_get_drvdata(&pdev->dev);

	if (vreg->avs_rdev)
		regulator_unregister(vreg->avs_rdev);
	regulator_unregister(vreg->rdev);

	return 0;
}

static const struct of_device_id spm_regulator_match_table[] = {
	{ .compatible = SPM_REGULATOR_DRIVER_NAME, },
	{}
};

static const struct platform_device_id spm_regulator_id[] = {
	{ SPM_REGULATOR_DRIVER_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(spmi, spm_regulator_id);

static struct platform_driver spm_regulator_driver = {
	.driver = {
		.name		= SPM_REGULATOR_DRIVER_NAME,
		.of_match_table = spm_regulator_match_table,
	},
	.probe		= spm_regulator_probe,
	.remove		= spm_regulator_remove,
	.id_table	= spm_regulator_id,
};

/**
 * spm_regulator_init() - register spmi driver for spm-regulator
 *
 * This initialization function should be called in systems in which driver
 * registration ordering must be controlled precisely.
 *
 * Returns 0 on success or errno on failure.
 */
int __init spm_regulator_init(void)
{
	static bool has_registered;

	if (has_registered)
		return 0;

	has_registered = true;

	return platform_driver_register(&spm_regulator_driver);
}

static void __exit spm_regulator_exit(void)
{
	platform_driver_unregister(&spm_regulator_driver);
}

arch_initcall(spm_regulator_init);
module_exit(spm_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPM regulator driver");
MODULE_ALIAS("platform:spm-regulator");
