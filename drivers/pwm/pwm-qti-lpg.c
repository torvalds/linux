// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/qpnp/qpnp-pbs.h>
#include <linux/qpnp/qti-pwm.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>

#define REG_SIZE_PER_LPG	0x100
#define LPG_BASE		"lpg-base"
#define LUT_BASE		"lut-base"

/* LPG module registers */
#define REG_LPG_PERPH_SUBTYPE		0x05
#define REG_LPG_PATTERN_CONFIG		0x40
#define REG_LPG_PWM_SIZE_CLK		0x41
#define REG_LPG_PWM_FREQ_PREDIV_CLK	0x42
#define REG_LPG_PWM_TYPE_CONFIG		0x43
#define REG_LPG_PWM_VALUE_LSB		0x44
#define REG_LPG_PWM_VALUE_MSB		0x45
#define REG_LPG_ENABLE_CONTROL		0x46
#define REG_LPG_PWM_SYNC		0x47
#define REG_LPG_RAMP_STEP_DURATION_LSB	0x50
#define REG_LPG_RAMP_STEP_DURATION_MSB	0x51
#define REG_LPG_PAUSE_HI_MULTIPLIER	0x52
#define REG_LPG_PAUSE_LO_MULTIPLIER	0x54
#define REG_LPG_HI_INDEX		0x56
#define REG_LPG_LO_INDEX		0x57

/* PWM module registers */
#define REG_PWM_STATUS1			0x08
#define FM_MODE_PRESENT			BIT(0)

#define REG_PWM_FM_MODE			0x50
#define FM_MODE_ENABLE			BIT(7)

/* REG_LPG_PATTERN_CONFIG */
#define LPG_PATTERN_EN_PAUSE_LO		BIT(0)
#define LPG_PATTERN_EN_PAUSE_HI		BIT(1)
#define LPG_PATTERN_RAMP_TOGGLE		BIT(2)
#define LPG_PATTERN_REPEAT		BIT(3)
#define LPG_PATTERN_RAMP_LO_TO_HI	BIT(4)

/* REG_LPG_PERPH_SUBTYPE */
#define SUBTYPE_PWM			0x0b
#define SUBTYPE_HI_RES_PWM		0x0c
#define SUBTYPE_LPG_LITE		0x11

/* REG_LPG_PWM_SIZE_CLK */
#define LPG_PWM_SIZE_LPG_MASK		BIT(4)
#define LPG_PWM_SIZE_PWM_MASK		BIT(2)
#define LPG_PWM_SIZE_PWM_HI_RES_MASK	GENMASK(6, 4)
#define LPG_PWM_SIZE_LPG_SHIFT		4
#define LPG_PWM_SIZE_PWM_SHIFT		2
#define LPG_PWM_SIZE_PWM_HI_RES_SHIFT	4
#define LPG_PWM_CLK_FREQ_SEL_MASK	GENMASK(1, 0)
#define LPG_PWM_HI_RES_CLK_FREQ_SEL_MASK	GENMASK(2, 0)

/* REG_LPG_PWM_FREQ_PREDIV_CLK */
#define LPG_PWM_FREQ_PREDIV_MASK	GENMASK(6, 5)
#define LPG_PWM_FREQ_PREDIV_SHIFT	5
#define LPG_PWM_FREQ_EXPONENT_MASK	GENMASK(2, 0)

/* REG_LPG_PWM_TYPE_CONFIG */
#define LPG_PWM_EN_GLITCH_REMOVAL_MASK	BIT(5)

/* REG_LPG_PWM_VALUE_LSB */
#define LPG_PWM_VALUE_LSB_MASK		GENMASK(7, 0)

/* REG_LPG_PWM_VALUE_MSB */
#define LPG_PWM_VALUE_MSB_MASK		BIT(0)
#define LPG_PWM_HI_RES_VALUE_MSB_MASK	GENMASK(7, 0)

/* REG_LPG_ENABLE_CONTROL */
#define LPG_EN_LPG_OUT_BIT		BIT(7)
#define LPG_EN_LPG_OUT_SHIFT		7
#define LPG_PWM_SRC_SELECT_MASK		BIT(2)
#define LPG_PWM_SRC_SELECT_SHIFT	2
#define LPG_EN_RAMP_GEN_MASK		BIT(1)
#define LPG_EN_RAMP_GEN_SHIFT		1

/* REG_LPG_PWM_SYNC */
#define LPG_PWM_VALUE_SYNC		BIT(0)

#define NUM_PWM_SIZE			2
#define NUM_PWM_HI_RES_SIZE		8
#define NUM_PWM_CLK			3
#define NUM_PWM_HI_RES_CLK		4
#define NUM_CLK_PREDIV			4
#define NUM_PWM_EXP			8

#define LPG_HI_LO_IDX_MASK		GENMASK(5, 0)

/* LUT module registers */
#define REG_LPG_LUT_1_LSB		0x42
#define REG_LPG_LUT_RAMP_CONTROL	0xc8

#define LPG_LUT_VALUE_MSB_MASK		BIT(0)
#define LPG_LUT_COUNT_MAX		47

/* LPG config settings in SDAM */
#define SDAM_REG_PBS_SEQ_EN			0x42
#define PBS_SW_TRG_BIT				BIT(0)

#define SDAM_REG_RAMP_STEP_DURATION		0x47

#define SDAM_LUT_EN_OFFSET			0x0
#define SDAM_PATTERN_CONFIG_OFFSET		0x1
#define SDAM_END_INDEX_OFFSET			0x3
#define SDAM_START_INDEX_OFFSET			0x4
#define SDAM_PBS_SCRATCH_LUT_COUNTER_OFFSET	0x6
#define SDAM_PAUSE_START_MULTIPLIER_OFFSET		0x8
#define SDAM_PAUSE_END_MULTIPLIER_OFFSET		0x9

/* SDAM_REG_LUT_EN */
#define SDAM_LUT_EN_BIT				BIT(0)

/* SDAM_REG_PATTERN_CONFIG */
#define SDAM_PATTERN_LOOP_ENABLE		BIT(3)
#define SDAM_PATTERN_EN_PAUSE_START		BIT(1)
#define SDAM_PATTERN_EN_PAUSE_END		BIT(0)
#define SDAM_PAUSE_COUNT_MAX			(U8_MAX - 1)

#define SDAM_LUT_COUNT_MAX			64

enum lpg_src {
	LUT_PATTERN = 0,
	PWM_VALUE,
};

enum ppg_num_nvmems {
	PPG_NO_NVMEMS,
	PPG_NVMEMS_1, /* A single nvmem for both LUT and LPG channel config */
	PPG_NVMEMS_2, /* Two separate nvmems for LUT and LPG channel config */
};

static const int pwm_size[NUM_PWM_SIZE] = {6, 9};
static const int pwm_hi_res_size[NUM_PWM_HI_RES_SIZE] = {8, 9, 10, 11, 12, 13, 14, 15};
static const int clk_freq_hz[NUM_PWM_CLK] = {1024, 32768, 19200000};
static const int clk_freq_hz_hi_res[NUM_PWM_HI_RES_CLK] = {1024, 32768, 19200000, 76800000};
/* clk_period_ns = NSEC_PER_SEC / clk_freq_hz */
static const int clk_period_ns[NUM_PWM_CLK] = {976562, 30517, 52};
static const int clk_period_ns_hi_res[NUM_PWM_HI_RES_CLK] = {976562, 30517, 52, 13};
static const int clk_prediv[NUM_CLK_PREDIV] = {1, 3, 5, 6};
static const int pwm_exponent[NUM_PWM_EXP] = {0, 1, 2, 3, 4, 5, 6, 7};

struct lpg_ramp_config {
	u16			step_ms;
	u8			pause_hi_count;
	u8			pause_lo_count;
	u8			hi_idx;
	u8			lo_idx;
	bool			ramp_dir_low_to_hi;
	bool			pattern_repeat;
	bool			toggle;
	u32			*pattern;
	u32			pattern_length;
};

struct lpg_pwm_config {
	u32	pwm_size;
	u32	pwm_clk;
	u32	prediv;
	u32	clk_exp;
	u16	pwm_value;
	u64	best_period_ns;
};

struct qpnp_lpg_lut {
	struct qpnp_lpg_chip	*chip;
	struct mutex		lock;
	enum ppg_num_nvmems	nvmem_count;
	u32			reg_base;
	u32			*pattern; /* patterns in percentage */
	u32			ramp_step_tick_us;
};

struct qpnp_lpg_channel {
	struct qpnp_lpg_chip		*chip;
	struct lpg_pwm_config		pwm_config;
	struct lpg_ramp_config		ramp_config;
	enum pwm_output_type		output_type;
	u32				lpg_idx;
	u32				reg_base;
	u32				max_pattern_length;
	u32				lpg_sdam_base;
	u8				src_sel;
	u8				subtype;
	bool				lut_written;
	bool				enable_pfm;
	u64				current_period_ns;
	u64				current_duty_ns;
};

struct qpnp_lpg_chip {
	struct pwm_chip		pwm_chip;
	struct regmap		*regmap;
	struct device		*dev;
	struct qpnp_lpg_channel	*lpgs;
	struct qpnp_lpg_lut	*lut;
	struct mutex		bus_lock;
	u32			*lpg_group;
	struct nvmem_device	*lpg_chan_nvmem;
	struct nvmem_device	*lut_nvmem;
	struct device_node	*pbs_dev_node;
	u32			num_lpgs;
	unsigned long		pbs_en_bitmap;
	bool			use_sdam;
};

static int qpnp_lpg_read(struct qpnp_lpg_channel *lpg, u16 addr, u8 *val)
{
	int rc;
	unsigned int tmp;

	mutex_lock(&lpg->chip->bus_lock);
	rc = regmap_read(lpg->chip->regmap, lpg->reg_base + addr, &tmp);
	if (rc < 0)
		dev_err(lpg->chip->dev, "Read addr 0x%x failed, rc=%d\n",
				lpg->reg_base + addr, rc);
	else
		*val = (u8)tmp;
	mutex_unlock(&lpg->chip->bus_lock);

	return rc;
}

static int qpnp_lpg_write(struct qpnp_lpg_channel *lpg, u16 addr, u8 val)
{
	int rc;

	mutex_lock(&lpg->chip->bus_lock);
	rc = regmap_write(lpg->chip->regmap, lpg->reg_base + addr, val);
	if (rc < 0)
		dev_err(lpg->chip->dev, "Write addr 0x%x with value 0x%x failed, rc=%d\n",
				lpg->reg_base + addr, val, rc);
	mutex_unlock(&lpg->chip->bus_lock);

	return rc;
}

static int qpnp_lpg_masked_write(struct qpnp_lpg_channel *lpg,
				u16 addr, u8 mask, u8 val)
{
	int rc;

	mutex_lock(&lpg->chip->bus_lock);
	rc = regmap_update_bits(lpg->chip->regmap, lpg->reg_base + addr,
							mask, val);
	if (rc < 0)
		dev_err(lpg->chip->dev, "Update addr 0x%x to val 0x%x with mask 0x%x failed, rc=%d\n",
				lpg->reg_base + addr, val, mask, rc);
	mutex_unlock(&lpg->chip->bus_lock);

	return rc;
}

static int qpnp_lut_write(struct qpnp_lpg_lut *lut, u16 addr, u8 val)
{
	int rc;

	mutex_lock(&lut->chip->bus_lock);
	rc = regmap_write(lut->chip->regmap, lut->reg_base + addr, val);
	if (rc < 0)
		dev_err(lut->chip->dev, "Write addr 0x%x with value %d failed, rc=%d\n",
				lut->reg_base + addr, val, rc);
	mutex_unlock(&lut->chip->bus_lock);

	return rc;
}

static int qpnp_lut_masked_write(struct qpnp_lpg_lut *lut,
				u16 addr, u8 mask, u8 val)
{
	int rc;

	mutex_lock(&lut->chip->bus_lock);
	rc = regmap_update_bits(lut->chip->regmap, lut->reg_base + addr,
							mask, val);
	if (rc < 0)
		dev_err(lut->chip->dev, "Update addr 0x%x to val 0x%x with mask 0x%x failed, rc=%d\n",
				lut->reg_base + addr, val, mask, rc);
	mutex_unlock(&lut->chip->bus_lock);

	return rc;
}

static int qpnp_lpg_chan_nvmem_write(struct qpnp_lpg_chip *chip, u16 addr,
				    u8 val)
{
	int rc;

	mutex_lock(&chip->bus_lock);
	rc = nvmem_device_write(chip->lpg_chan_nvmem, addr, 1, &val);
	if (rc < 0)
		dev_err(chip->dev, "write SDAM add 0x%x failed, rc=%d\n",
				addr, rc);

	mutex_unlock(&chip->bus_lock);

	return rc > 0 ? 0 : rc;
}

static int qpnp_lpg_sdam_write(struct qpnp_lpg_channel *lpg, u16 addr, u8 val)
{
	struct qpnp_lpg_chip *chip = lpg->chip;
	int rc;

	mutex_lock(&chip->bus_lock);
	rc = nvmem_device_write(chip->lpg_chan_nvmem,
			lpg->lpg_sdam_base + addr, 1, &val);
	if (rc < 0)
		dev_err(chip->dev, "write SDAM add 0x%x failed, rc=%d\n",
				lpg->lpg_sdam_base + addr, rc);

	mutex_unlock(&chip->bus_lock);

	return rc > 0 ? 0 : rc;
}

static int qpnp_lpg_sdam_masked_write(struct qpnp_lpg_channel *lpg,
					u16 addr, u8 mask, u8 val)
{
	int rc;
	u8 tmp;
	struct qpnp_lpg_chip *chip = lpg->chip;

	mutex_lock(&chip->bus_lock);

	rc = nvmem_device_read(chip->lpg_chan_nvmem,
			lpg->lpg_sdam_base + addr, 1, &tmp);
	if (rc < 0) {
		dev_err(chip->dev, "Read SDAM addr %d failed, rc=%d\n",
				lpg->lpg_sdam_base + addr, rc);
		goto unlock;
	}

	tmp = tmp & ~mask;
	tmp |= val & mask;
	rc = nvmem_device_write(chip->lpg_chan_nvmem,
			lpg->lpg_sdam_base + addr, 1, &tmp);
	if (rc < 0)
		dev_err(chip->dev, "write SDAM addr %d failed, rc=%d\n",
				lpg->lpg_sdam_base + addr, rc);

unlock:
	mutex_unlock(&chip->bus_lock);

	return rc > 0 ? 0 : rc;
}

static int qpnp_lut_sdam_write(struct qpnp_lpg_lut *lut,
		u16 addr, u8 *val, size_t length)
{
	struct qpnp_lpg_chip *chip = lut->chip;
	int rc;

	if (addr >= SDAM_LUT_COUNT_MAX)
		return -EINVAL;

	mutex_lock(&chip->bus_lock);
	rc = nvmem_device_write(chip->lut_nvmem,
			lut->reg_base + addr, length, val);
	if (rc < 0)
		dev_err(chip->dev, "write SDAM addr %d failed, rc=%d\n",
				lut->reg_base + addr, rc);

	mutex_unlock(&chip->bus_lock);

	return rc > 0 ? 0 : rc;
}

static struct qpnp_lpg_channel *pwm_dev_to_qpnp_lpg(struct pwm_chip *pwm_chip,
				struct pwm_device *pwm)
{

	struct qpnp_lpg_chip *chip = container_of(pwm_chip,
			struct qpnp_lpg_chip, pwm_chip);
	u32 hw_idx = pwm->hwpwm;

	if (hw_idx >= chip->num_lpgs) {
		dev_err(chip->dev, "hw index %d out of range [0-%d]\n",
				hw_idx, chip->num_lpgs - 1);
		return NULL;
	}

	return &chip->lpgs[hw_idx];
}

static int __find_index_in_array(int member, const int array[], int length)
{
	int i;

	for (i = 0; i < length; i++) {
		if (member == array[i])
			return i;
	}

	return -EINVAL;
}

static int qpnp_lpg_set_glitch_removal(struct qpnp_lpg_channel *lpg, bool en)
{
	int rc;
	u8 mask, val;

	val = en ? LPG_PWM_EN_GLITCH_REMOVAL_MASK : 0;
	mask = LPG_PWM_EN_GLITCH_REMOVAL_MASK;
	rc = qpnp_lpg_masked_write(lpg, REG_LPG_PWM_TYPE_CONFIG, mask, val);
	if (rc < 0)
		dev_err(lpg->chip->dev, "Write LPG_PWM_TYPE_CONFIG failed, rc=%d\n",
							rc);
	return rc;
}

static int qpnp_lpg_set_pwm_config(struct qpnp_lpg_channel *lpg)
{
	int rc;
	u8 val, mask, shift;
	int pwm_size_idx, pwm_clk_idx, prediv_idx, clk_exp_idx;

	if (lpg->subtype == SUBTYPE_HI_RES_PWM) {
		pwm_size_idx = __find_index_in_array(lpg->pwm_config.pwm_size,
				pwm_hi_res_size, ARRAY_SIZE(pwm_hi_res_size));
		pwm_clk_idx = __find_index_in_array(lpg->pwm_config.pwm_clk,
				clk_freq_hz_hi_res, ARRAY_SIZE(clk_freq_hz_hi_res));
	} else {
		pwm_size_idx = __find_index_in_array(lpg->pwm_config.pwm_size,
				pwm_size, ARRAY_SIZE(pwm_size));
		pwm_clk_idx = __find_index_in_array(lpg->pwm_config.pwm_clk,
				clk_freq_hz, ARRAY_SIZE(clk_freq_hz));
	}

	prediv_idx = __find_index_in_array(lpg->pwm_config.prediv,
			clk_prediv, ARRAY_SIZE(clk_prediv));
	clk_exp_idx = __find_index_in_array(lpg->pwm_config.clk_exp,
			pwm_exponent, ARRAY_SIZE(pwm_exponent));

	if (pwm_size_idx < 0 || pwm_clk_idx < 0
			|| prediv_idx < 0 || clk_exp_idx < 0)
		return -EINVAL;

	/* pwm_clk_idx is 1 bit lower than the register value */
	pwm_clk_idx += 1;
	if (lpg->subtype == SUBTYPE_PWM) {
		shift = LPG_PWM_SIZE_PWM_SHIFT;
		mask = LPG_PWM_SIZE_PWM_MASK | LPG_PWM_CLK_FREQ_SEL_MASK;
	} else if (lpg->subtype == SUBTYPE_HI_RES_PWM) {
		shift = LPG_PWM_SIZE_PWM_HI_RES_SHIFT;
		mask = LPG_PWM_SIZE_PWM_HI_RES_MASK | LPG_PWM_HI_RES_CLK_FREQ_SEL_MASK;
	} else {
		shift = LPG_PWM_SIZE_LPG_SHIFT;
		mask = LPG_PWM_SIZE_LPG_MASK | LPG_PWM_CLK_FREQ_SEL_MASK;
	}

	val = pwm_size_idx << shift | pwm_clk_idx;
	rc = qpnp_lpg_masked_write(lpg, REG_LPG_PWM_SIZE_CLK, mask, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_SIZE_CLK failed, rc=%d\n",
							rc);
		return rc;
	}

	val = prediv_idx << LPG_PWM_FREQ_PREDIV_SHIFT | clk_exp_idx;
	mask = LPG_PWM_FREQ_PREDIV_MASK | LPG_PWM_FREQ_EXPONENT_MASK;
	rc = qpnp_lpg_masked_write(lpg, REG_LPG_PWM_FREQ_PREDIV_CLK, mask, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_FREQ_PREDIV_CLK failed, rc=%d\n",
							rc);
		return rc;
	}

	if (lpg->src_sel == LUT_PATTERN)
		return 0;

	val = lpg->pwm_config.pwm_value >> 8;
	if (lpg->subtype == SUBTYPE_HI_RES_PWM)
		mask = LPG_PWM_HI_RES_VALUE_MSB_MASK;
	else
		mask = LPG_PWM_VALUE_MSB_MASK;
	rc = qpnp_lpg_masked_write(lpg, REG_LPG_PWM_VALUE_MSB, mask, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_VALUE_MSB failed, rc=%d\n",
							rc);
		return rc;
	}

	val = lpg->pwm_config.pwm_value & LPG_PWM_VALUE_LSB_MASK;
	rc = qpnp_lpg_write(lpg, REG_LPG_PWM_VALUE_LSB, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_VALUE_LSB failed, rc=%d\n",
							rc);
		return rc;
	}

	val = LPG_PWM_VALUE_SYNC;
	rc = qpnp_lpg_write(lpg, REG_LPG_PWM_SYNC, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_SYNC failed, rc=%d\n",
							rc);
		return rc;
	}

	return rc;
}

static int qpnp_lpg_set_pfm_config(struct qpnp_lpg_channel *lpg)
{
	int rc;
	u8 val, mask;
	int pwm_clk_idx, clk_exp_idx;

	if (lpg->subtype == SUBTYPE_HI_RES_PWM)
		pwm_clk_idx = __find_index_in_array(lpg->pwm_config.pwm_clk,
				clk_freq_hz_hi_res, ARRAY_SIZE(clk_freq_hz_hi_res));
	else
		pwm_clk_idx = __find_index_in_array(lpg->pwm_config.pwm_clk,
				clk_freq_hz, ARRAY_SIZE(clk_freq_hz));

	clk_exp_idx = __find_index_in_array(lpg->pwm_config.clk_exp,
			pwm_exponent, ARRAY_SIZE(pwm_exponent));

	if (pwm_clk_idx < 0 || clk_exp_idx < 0)
		return -EINVAL;

	/* pwm_clk_idx is 1 bit lower than the register value */
	pwm_clk_idx += 1;

	val = pwm_clk_idx;
	if (lpg->subtype == SUBTYPE_HI_RES_PWM)
		mask = LPG_PWM_HI_RES_CLK_FREQ_SEL_MASK;
	else
		mask = LPG_PWM_CLK_FREQ_SEL_MASK;

	rc = qpnp_lpg_masked_write(lpg, REG_LPG_PWM_SIZE_CLK, mask, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_SIZE_CLK failed, rc=%d\n",
			rc);
		return rc;
	}

	val = clk_exp_idx;
	mask = LPG_PWM_FREQ_EXPONENT_MASK;
	rc = qpnp_lpg_masked_write(lpg, REG_LPG_PWM_FREQ_PREDIV_CLK, mask, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_FREQ_PREDIV_CLK failed, rc=%d\n",
			rc);
		return rc;
	}

	val = lpg->pwm_config.pwm_value;
	rc = qpnp_lpg_masked_write(lpg, REG_LPG_PWM_VALUE_LSB,
				   LPG_PWM_VALUE_LSB_MASK, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_VALUE_LSB failed, rc=%d\n",
			rc);
		return rc;
	}

	val = LPG_PWM_VALUE_SYNC;
	rc = qpnp_lpg_write(lpg, REG_LPG_PWM_SYNC, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PWM_SYNC failed, rc=%d\n",
							rc);
		return rc;
	}

	return rc;
}

static int qpnp_lpg_set_sdam_lut_pattern(struct qpnp_lpg_channel *lpg,
		unsigned int *pattern, unsigned int length)
{
	struct qpnp_lpg_lut *lut = lpg->chip->lut;
	int i, rc = 0;
	u8 val[SDAM_LUT_COUNT_MAX + 1], addr;

	if (length > lpg->max_pattern_length) {
		dev_err(lpg->chip->dev, "new pattern length (%d) larger than predefined (%d)\n",
				length, lpg->max_pattern_length);
		return -EINVAL;
	}

	/* Program LUT pattern */
	mutex_lock(&lut->lock);
	addr = lpg->ramp_config.lo_idx;
	for (i = 0; i < length; i++)
		val[i] = pattern[i] * 255 / 100;

	rc = qpnp_lut_sdam_write(lut, addr, val, length);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write pattern in SDAM failed, rc=%d\n",
				rc);
		goto unlock;
	}

	lpg->ramp_config.pattern_length = length;
unlock:
	mutex_unlock(&lut->lock);

	return rc;
}

#define SDAM_START_BASE			0x40
static u8 qpnp_lpg_get_sdam_lut_idx(struct qpnp_lpg_channel *lpg, u8 idx)
{
	struct qpnp_lpg_chip *chip = lpg->chip;
	u8 val = idx;

	if (chip->lut->nvmem_count == PPG_NVMEMS_2)
		val += (chip->lut->reg_base - SDAM_START_BASE);

	return val;
}

static int qpnp_lpg_set_sdam_ramp_config(struct qpnp_lpg_channel *lpg)
{
	struct lpg_ramp_config *ramp = &lpg->ramp_config;
	u8 addr, mask, val;
	int rc = 0;

	/* clear PBS scatchpad register */
	val = 0;
	rc = qpnp_lpg_sdam_write(lpg,
			SDAM_PBS_SCRATCH_LUT_COUNTER_OFFSET, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write SDAM_PBS_SCRATCH_LUT_COUNTER_OFFSET failed, rc=%d\n",
				rc);
		return rc;
	}

	/* Set ramp step duration, in ticks */
	val = (ramp->step_ms * 1000 / lpg->chip->lut->ramp_step_tick_us) & 0xff;
	if (val > 0)
		val--;
	addr = SDAM_REG_RAMP_STEP_DURATION;
	rc = qpnp_lpg_chan_nvmem_write(lpg->chip, addr, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write SDAM_REG_RAMP_STEP_DURATION failed, rc=%d\n",
				rc);
		return rc;
	}

	/* Set hi_idx and lo_idx */
	val = qpnp_lpg_get_sdam_lut_idx(lpg, ramp->hi_idx);
	rc = qpnp_lpg_sdam_write(lpg, SDAM_END_INDEX_OFFSET, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write SDAM_REG_END_INDEX failed, rc=%d\n",
					rc);
		return rc;
	}

	val = qpnp_lpg_get_sdam_lut_idx(lpg, ramp->lo_idx);
	rc = qpnp_lpg_sdam_write(lpg, SDAM_START_INDEX_OFFSET, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write SDAM_REG_START_INDEX failed, rc=%d\n",
					rc);
		return rc;
	}

	/* Set LPG_PATTERN_CONFIG */
	addr = SDAM_PATTERN_CONFIG_OFFSET;
	mask = SDAM_PATTERN_LOOP_ENABLE;
	val = 0;
	if (ramp->pattern_repeat)
		val |= SDAM_PATTERN_LOOP_ENABLE;
	if (ramp->pause_hi_count) {
		val |= SDAM_PATTERN_EN_PAUSE_START;
		mask |= SDAM_PATTERN_EN_PAUSE_START;
	}
	if (ramp->pause_lo_count) {
		val |= SDAM_PATTERN_EN_PAUSE_END;
		mask |= SDAM_PATTERN_EN_PAUSE_END;
	}

	rc = qpnp_lpg_sdam_masked_write(lpg, addr, mask, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write SDAM_REG_PATTERN_CONFIG failed, rc=%d\n",
					rc);
		return rc;
	}

	/* Set PAUSE HI and LO */
	rc = qpnp_lpg_sdam_write(lpg, SDAM_PAUSE_START_MULTIPLIER_OFFSET,
				 ramp->pause_hi_count);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write SDAM_REG_PAUSE_START_MULTIPLIER failed, rc=%d\n",
			rc);
		return rc;
	}

	rc = qpnp_lpg_sdam_write(lpg, SDAM_PAUSE_END_MULTIPLIER_OFFSET,
				 ramp->pause_lo_count);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write SDAM_REG_PAUSE_END_MULTIPLIER failed, rc=%d\n",
			rc);
		return rc;
	}

	return rc;
}

static int qpnp_lpg_set_lut_pattern(struct qpnp_lpg_channel *lpg,
		unsigned int *pattern, unsigned int length)
{
	struct qpnp_lpg_lut *lut = lpg->chip->lut;
	u16 full_duty_value, pwm_values[SDAM_LUT_COUNT_MAX + 1] = {0};
	int i, rc = 0;
	u8 lsb, msb, addr;

	if (lpg->chip->use_sdam)
		return qpnp_lpg_set_sdam_lut_pattern(lpg, pattern, length);

	if (length > lpg->max_pattern_length) {
		dev_err(lpg->chip->dev, "new pattern length (%d) larger than predefined (%d)\n",
				length, lpg->max_pattern_length);
		return -EINVAL;
	}

	/* Program LUT pattern */
	mutex_lock(&lut->lock);
	addr = REG_LPG_LUT_1_LSB + lpg->ramp_config.lo_idx * 2;
	for (i = 0; i < length; i++) {
		full_duty_value = 1 << lpg->pwm_config.pwm_size;
		pwm_values[i] = pattern[i] * full_duty_value / 100;

		if (unlikely(pwm_values[i] > full_duty_value)) {
			dev_err(lpg->chip->dev, "PWM value %d exceed the max %d\n",
					pwm_values[i], full_duty_value);
			rc = -EINVAL;
			goto unlock;
		}

		if (pwm_values[i] == full_duty_value)
			pwm_values[i] = full_duty_value - 1;

		lsb = pwm_values[i] & 0xff;
		msb = pwm_values[i] >> 8;
		rc = qpnp_lut_write(lut, addr++, lsb);
		if (rc < 0) {
			dev_err(lpg->chip->dev, "Write NO.%d LUT pattern LSB (%d) failed, rc=%d\n",
					i, lsb, rc);
			goto unlock;
		}

		rc = qpnp_lut_masked_write(lut, addr++,
				LPG_LUT_VALUE_MSB_MASK, msb);
		if (rc < 0) {
			dev_err(lpg->chip->dev, "Write NO.%d LUT pattern MSB (%d) failed, rc=%d\n",
					i, msb, rc);
			goto unlock;
		}
	}
	lpg->ramp_config.pattern_length = length;
unlock:
	mutex_unlock(&lut->lock);

	return rc;
}

static int qpnp_lpg_set_ramp_config(struct qpnp_lpg_channel *lpg)
{
	struct lpg_ramp_config *ramp = &lpg->ramp_config;
	u8 lsb, msb, addr, mask, val;
	int rc = 0;

	if (lpg->chip->use_sdam)
		return qpnp_lpg_set_sdam_ramp_config(lpg);

	/* Set ramp step duration */
	lsb = ramp->step_ms & 0xff;
	msb = ramp->step_ms >> 8;
	addr = REG_LPG_RAMP_STEP_DURATION_LSB;
	rc = qpnp_lpg_write(lpg, addr, lsb);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write RAMP_STEP_DURATION_LSB failed, rc=%d\n",
					rc);
		return rc;
	}
	rc = qpnp_lpg_write(lpg, addr + 1, msb);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write RAMP_STEP_DURATION_MSB failed, rc=%d\n",
					rc);
		return rc;
	}

	/* Set hi_idx and lo_idx */
	rc = qpnp_lpg_masked_write(lpg, REG_LPG_HI_INDEX,
			LPG_HI_LO_IDX_MASK, ramp->hi_idx);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_HI_IDX failed, rc=%d\n",
					rc);
		return rc;
	}

	rc = qpnp_lpg_masked_write(lpg, REG_LPG_LO_INDEX,
			LPG_HI_LO_IDX_MASK, ramp->lo_idx);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_LO_IDX failed, rc=%d\n",
					rc);
		return rc;
	}

	/* Set pause_hi/lo_count */
	rc = qpnp_lpg_write(lpg, REG_LPG_PAUSE_HI_MULTIPLIER,
					ramp->pause_hi_count);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PAUSE_HI_MULTIPLIER failed, rc=%d\n",
					rc);
		return rc;
	}

	rc = qpnp_lpg_write(lpg, REG_LPG_PAUSE_LO_MULTIPLIER,
					ramp->pause_lo_count);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PAUSE_LO_MULTIPLIER failed, rc=%d\n",
					rc);
		return rc;
	}

	/* Set LPG_PATTERN_CONFIG */
	addr = REG_LPG_PATTERN_CONFIG;
	mask = LPG_PATTERN_EN_PAUSE_LO | LPG_PATTERN_EN_PAUSE_HI
		| LPG_PATTERN_RAMP_TOGGLE | LPG_PATTERN_REPEAT
		| LPG_PATTERN_RAMP_LO_TO_HI;
	val = 0;
	if (ramp->pause_lo_count != 0)
		val |= LPG_PATTERN_EN_PAUSE_LO;
	if (ramp->pause_hi_count != 0)
		val |= LPG_PATTERN_EN_PAUSE_HI;
	if (ramp->ramp_dir_low_to_hi)
		val |= LPG_PATTERN_RAMP_LO_TO_HI;
	if (ramp->pattern_repeat)
		val |= LPG_PATTERN_REPEAT;
	if (ramp->toggle)
		val |= LPG_PATTERN_RAMP_TOGGLE;

	rc = qpnp_lpg_masked_write(lpg, addr, mask, val);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Write LPG_PATTERN_CONFIG failed, rc=%d\n",
					rc);
		return rc;
	}

	return rc;
}

static int __qpnp_lpg_calc_pwm_period(u64 period_ns,
			struct lpg_pwm_config *pwm_config)
{
	struct qpnp_lpg_channel *lpg = container_of(pwm_config,
			struct qpnp_lpg_channel, pwm_config);
	struct lpg_pwm_config configs[NUM_PWM_HI_RES_SIZE];
	int i, j, m, n;
	u64 tmp1, tmp2;
	u64 clk_period_ns = 0, pwm_clk_period_ns;
	u64 clk_delta_ns = U64_MAX, min_clk_delta_ns = U64_MAX;
	u64 pwm_period_delta = U64_MAX, min_pwm_period_delta = U64_MAX;
	int pwm_size_step, clk_len, pwm_size_len;
	const int *pwm_size_arr, *clk_freq_hz_arr;

	/*
	 *              (2^pwm_size) * (2^pwm_exp) * prediv * NSEC_PER_SEC
	 * pwm_period = ---------------------------------------------------
	 *                               clk_freq_hz
	 *
	 * Searching the closest settings for the requested PWM period.
	 */
	if (lpg->subtype == SUBTYPE_HI_RES_PWM) {
		pwm_size_arr = pwm_hi_res_size;
		pwm_size_len = NUM_PWM_HI_RES_SIZE;
		clk_freq_hz_arr = clk_freq_hz_hi_res;
		clk_len = NUM_PWM_HI_RES_CLK;
	} else {
		pwm_size_arr = pwm_size;
		pwm_size_len = NUM_PWM_SIZE;
		clk_freq_hz_arr = clk_freq_hz;
		clk_len = NUM_PWM_CLK;
	}

	if (lpg->chip->use_sdam) {
		/* SDAM pattern control can only use 9 bit resolution */
		n = 1;
		pwm_size_len = 2;
	}
	else
		n = 0;
	for (; n < pwm_size_len; n++) {
		pwm_clk_period_ns = period_ns >> pwm_size_arr[n];
		for (i = clk_len - 1; i >= 0; i--) {
			for (j = 0; j < clk_len; j++) {
				for (m = 0; m < ARRAY_SIZE(pwm_exponent); m++) {
					tmp1 = 1 << pwm_exponent[m];
					tmp1 *= clk_prediv[j];
					tmp2 = NSEC_PER_SEC;
					do_div(tmp2, clk_freq_hz_arr[i]);

					clk_period_ns = tmp1 * tmp2;

					clk_delta_ns = abs(pwm_clk_period_ns
						- clk_period_ns);
					/*
					 * Find the closest setting for
					 * PWM frequency predivide value
					 */
					if (clk_delta_ns < min_clk_delta_ns) {
						min_clk_delta_ns
							= clk_delta_ns;
						configs[n].pwm_clk
							= clk_freq_hz_arr[i];
						configs[n].prediv
							= clk_prediv[j];
						configs[n].clk_exp
							= pwm_exponent[m];
						configs[n].pwm_size
							= pwm_size_arr[n];
						configs[n].best_period_ns
							= clk_period_ns;
					}
				}
			}
		}

		configs[n].best_period_ns *= 1 << pwm_size_arr[n];
		/* Find the closest setting for PWM period */
		pwm_period_delta = min_clk_delta_ns << pwm_size_arr[n];
		if (pwm_period_delta < min_pwm_period_delta) {
			min_pwm_period_delta = pwm_period_delta;
			memcpy(pwm_config, &configs[n],
					sizeof(struct lpg_pwm_config));
		}
	}

	/* Larger PWM size can achieve better resolution for PWM duty */
	for (n = pwm_size_len - 1; n > 0; n--) {
		if (pwm_config->pwm_size >= pwm_size_arr[n])
			break;
		pwm_size_step = pwm_size_arr[n] - pwm_config->pwm_size;
		if (pwm_config->clk_exp >= pwm_size_step) {
			pwm_config->pwm_size = pwm_size_arr[n];
			pwm_config->clk_exp -= pwm_size_step;
		}
	}
	pr_debug("PWM setting for period_ns %llu: pwm_clk = %dHZ, prediv = %d, exponent = %d, pwm_size = %d\n",
			period_ns, pwm_config->pwm_clk, pwm_config->prediv,
			pwm_config->clk_exp, pwm_config->pwm_size);
	pr_debug("Actual period: %lluns\n", pwm_config->best_period_ns);

	return 0;
}

static void __qpnp_lpg_calc_pwm_duty(u64 period_ns, u64 duty_ns,
			struct lpg_pwm_config *pwm_config)
{
	u16 pwm_value, max_pwm_value;
	u64 tmp;

	tmp = (u64)duty_ns << pwm_config->pwm_size;
	pwm_value = (u16)div64_u64(tmp, period_ns);

	max_pwm_value = (1 << pwm_config->pwm_size) - 1;
	if (pwm_value > max_pwm_value)
		pwm_value = max_pwm_value;
	pwm_config->pwm_value = pwm_value;
}

static int __qpnp_lpg_calc_pfm_period(u64 period_ns,
				       struct lpg_pwm_config *pwm_config)
{
	struct qpnp_lpg_channel *lpg = container_of(pwm_config,
			struct qpnp_lpg_channel, pwm_config);
	int clk, exp, lsb, clk_len;
	int best_clk = 0, best_exp = 0, best_lsb = -EINVAL;
	u64 lsb_tmp, period_tmp, curr_p_err, last_p_err = 0;
	u64 min_p_err = U64_MAX;
	const int *clk_freq_hz_arr, *clk_period_ns_arr;

	/*
	 *               2 * (pwm_value_lsb + 1) * (2^pwm_exp) * NSEC_PER_SEC
	 * pwm_period = ---------------------------------------------------
	 *                               clk_freq_hz
	 *
	 * For each (clk, exp) solve above equation for pwm_value_lsb, and then
	 * use this pwm_lsb to calculate pwm_period and compare with desired
	 * period. Store the triplet values that yield the closest value to
	 * desired period.
	 */

	if (lpg->subtype == SUBTYPE_HI_RES_PWM) {
		clk_freq_hz_arr = clk_freq_hz_hi_res;
		clk_period_ns_arr = clk_period_ns_hi_res;
		clk_len = NUM_PWM_HI_RES_CLK;
	} else {
		clk_freq_hz_arr = clk_freq_hz;
		clk_period_ns_arr = clk_period_ns;
		clk_len = NUM_PWM_CLK;
	}

	for (clk = 0; clk < clk_len; clk++) {
		last_p_err = U64_MAX;
		for (exp = 0; exp < NUM_PWM_EXP; exp++) {
			lsb_tmp = div_u64(period_ns, clk_period_ns_arr[clk]);
			lsb_tmp >>= (exp + 1);
			lsb = lsb_tmp - 1;
			if (lsb >= 0 && lsb <= U8_MAX) {
				period_tmp = (lsb + 1);
				period_tmp <<= (exp + 1);
				period_tmp *= clk_period_ns_arr[clk];
				curr_p_err = abs(period_ns - period_tmp);

				if (curr_p_err < min_p_err) {
					min_p_err = curr_p_err;

					/* Closest settings found! Save them. */
					best_clk = clk;
					best_exp = exp;
					best_lsb = lsb;
					/*
					 * No need to set pwm_size or prediv in
					 * `struct pwm_config` as they are
					 * no-ops in PFM mode.
					 */
					pwm_config->best_period_ns = period_tmp;
				}

				if (curr_p_err > last_p_err)
					/* No need to iterate further */
					break;
				last_p_err = curr_p_err;
			}
		}
	}

	if (best_lsb < 0) {
		pr_err("Cannot generate %llu ns\n", period_ns);
		return -EINVAL;
	}

	pwm_config->pwm_clk = clk_freq_hz_arr[best_clk];
	pwm_config->clk_exp = pwm_exponent[best_exp];
	pwm_config->pwm_value = best_lsb;

	pr_debug("PFM setting for period_ns %llu: pwm_clk = %d Hz, exponent = %d, pwm_val_lsb = %d\n",
			period_ns, pwm_config->pwm_clk,
			pwm_config->clk_exp, pwm_config->pwm_value);
	pr_debug("Actual PFM period: %llu ns\n", pwm_config->best_period_ns);

	return 0;
}

static int qpnp_lpg_config(struct qpnp_lpg_channel *lpg,
		u64 duty_ns, u64 period_ns)
{
	int rc;

	if (duty_ns > period_ns) {
		dev_err(lpg->chip->dev, "Duty %lluns is larger than period %lluns\n",
						duty_ns, period_ns);
		return -EINVAL;
	}

	if (period_ns != lpg->current_period_ns) {
		if (lpg->enable_pfm) {
			rc = __qpnp_lpg_calc_pfm_period(period_ns,
							&lpg->pwm_config);
			if (rc)
				return rc;
		} else {
			rc = __qpnp_lpg_calc_pwm_period(period_ns,
							&lpg->pwm_config);
			if (rc)
				return rc;

			/* program LUT if PWM period is changed */
			if (lpg->src_sel == LUT_PATTERN) {
				rc = qpnp_lpg_set_lut_pattern(lpg,
					lpg->ramp_config.pattern,
					lpg->ramp_config.pattern_length);
				if (rc < 0) {
					dev_err(lpg->chip->dev, "set LUT pattern failed for LPG%d, rc=%d\n",
							lpg->lpg_idx, rc);
					return rc;
				}
				lpg->lut_written = true;
			}
		}
	}

	/* Don't calculate duty cycle for PFM as it is fixed at 50% */
	if ((period_ns != lpg->current_period_ns ||
		duty_ns != lpg->current_duty_ns) && !lpg->enable_pfm)
		__qpnp_lpg_calc_pwm_duty(period_ns, duty_ns, &lpg->pwm_config);

	if (lpg->enable_pfm)
		rc = qpnp_lpg_set_pfm_config(lpg);
	else
		rc = qpnp_lpg_set_pwm_config(lpg);

	if (rc < 0) {
		dev_err(lpg->chip->dev, "Config %s failed for channel %d, rc=%d\n",
			lpg->enable_pfm ? "PFM" : "PWM", lpg->lpg_idx, rc);
		return rc;
	}

	lpg->current_period_ns = period_ns;
	lpg->current_duty_ns = duty_ns;

	return rc;
}

static int qpnp_lpg_pwm_config(struct pwm_chip *pwm_chip,
		struct pwm_device *pwm, u64 duty_ns, u64 period_ns)
{
	struct qpnp_lpg_channel *lpg;

	lpg = pwm_dev_to_qpnp_lpg(pwm_chip, pwm);
	if (lpg == NULL) {
		dev_err(pwm_chip->dev, "lpg not found\n");
		return -ENODEV;
	}

	return qpnp_lpg_config(lpg, duty_ns, period_ns);
}

#define SDAM_PBS_TRIG_SET			0xe5
#define SDAM_PBS_TRIG_CLR			0xe6
static int qpnp_lpg_clear_pbs_trigger(struct qpnp_lpg_chip *chip)
{
	int rc;

	rc = qpnp_lpg_chan_nvmem_write(chip,
			SDAM_REG_PBS_SEQ_EN, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Write SDAM_REG_PBS_SEQ_EN failed, rc=%d\n",
				rc);
		return rc;
	}

	if (chip->lut->nvmem_count == PPG_NVMEMS_2) {
		rc = qpnp_lpg_chan_nvmem_write(chip, SDAM_PBS_TRIG_CLR,
				PBS_SW_TRG_BIT);
		if (rc < 0) {
			dev_err(chip->dev, "Failed to fire PBS seq, rc=%d\n",
					rc);
			return rc;
		}
	}

	return 0;
}

static int qpnp_lpg_set_pbs_trigger(struct qpnp_lpg_chip *chip)
{
	int rc;

	rc = qpnp_lpg_chan_nvmem_write(chip,
			SDAM_REG_PBS_SEQ_EN, PBS_SW_TRG_BIT);
	if (rc < 0) {
		dev_err(chip->dev, "Write SDAM_REG_PBS_SEQ_EN failed, rc=%d\n",
				rc);
		return rc;
	}

	if (chip->lut->nvmem_count == PPG_NVMEMS_1) {
		if (!chip->pbs_dev_node) {
			dev_err(chip->dev, "PBS device unavailable\n");
			return -ENODEV;
		}
		rc = qpnp_pbs_trigger_event(chip->pbs_dev_node,
				PBS_SW_TRG_BIT);
	} else {
		rc = qpnp_lpg_chan_nvmem_write(chip, SDAM_PBS_TRIG_SET,
					      PBS_SW_TRG_BIT);
	}

	if (rc < 0)
		dev_err(chip->dev, "Failed to trigger PBS, rc=%d\n", rc);

	return rc;
}

static int qpnp_lpg_pbs_trigger_enable(struct qpnp_lpg_channel *lpg, bool en)
{
	struct qpnp_lpg_chip *chip = lpg->chip;
	int rc = 0;

	if (en) {
		if (chip->pbs_en_bitmap == 0) {
			rc = qpnp_lpg_set_pbs_trigger(chip);
			if (rc < 0)
				return rc;
		}
		set_bit(lpg->lpg_idx, &chip->pbs_en_bitmap);
	} else {
		clear_bit(lpg->lpg_idx, &chip->pbs_en_bitmap);
		if (chip->pbs_en_bitmap == 0) {
			rc = qpnp_lpg_clear_pbs_trigger(chip);
			if (rc < 0)
				return rc;
		}
	}

	return rc;
}

static int qpnp_lpg_pwm_src_enable(struct qpnp_lpg_channel *lpg, bool en)
{
	struct qpnp_lpg_chip *chip = lpg->chip;
	struct qpnp_lpg_lut *lut = chip->lut;
	struct pwm_device *pwm;
	u8 mask, val;
	int i, lpg_idx, rc;

	mask = LPG_PWM_SRC_SELECT_MASK | LPG_EN_LPG_OUT_BIT |
					LPG_EN_RAMP_GEN_MASK;
	val = lpg->src_sel << LPG_PWM_SRC_SELECT_SHIFT;

	if (lpg->src_sel == LUT_PATTERN && !chip->use_sdam)
		val |= 1 << LPG_EN_RAMP_GEN_SHIFT;

	if (en)
		val |= 1 << LPG_EN_LPG_OUT_SHIFT;

	rc = qpnp_lpg_masked_write(lpg, REG_LPG_ENABLE_CONTROL, mask, val);
	if (rc < 0) {
		dev_err(chip->dev, "Write LPG_ENABLE_CONTROL failed, rc=%d\n",
				rc);
		return rc;
	}

	if (chip->use_sdam) {
		if (lpg->src_sel == LUT_PATTERN && en) {
			val = SDAM_LUT_EN_BIT;
			en = true;
		} else {
			val = 0;
			en = false;
		}

		rc = qpnp_lpg_sdam_write(lpg, SDAM_LUT_EN_OFFSET, val);
		if (rc < 0) {
			dev_err(chip->dev, "Write SDAM_REG_LUT_EN failed, rc=%d\n",
					rc);
			return rc;
		}

		qpnp_lpg_pbs_trigger_enable(lpg, en);

		return rc;
	}

	if (lpg->src_sel == LUT_PATTERN && en) {
		val = 1 << lpg->lpg_idx;
		for (i = 0; i < chip->num_lpgs; i++) {
			if (chip->lpg_group == NULL)
				break;
			if (chip->lpg_group[i] == 0)
				break;
			lpg_idx = chip->lpg_group[i] - 1;
			pwm = &chip->pwm_chip.pwms[lpg_idx];
			if ((lpg->output_type == PWM_OUTPUT_MODULATED)
						&& pwm_is_enabled(pwm)) {
				rc = qpnp_lpg_masked_write(&chip->lpgs[lpg_idx],
						REG_LPG_ENABLE_CONTROL,
						LPG_EN_LPG_OUT_BIT, 0);
				if (rc < 0)
					break;
				rc = qpnp_lpg_masked_write(&chip->lpgs[lpg_idx],
						REG_LPG_ENABLE_CONTROL,
						LPG_EN_LPG_OUT_BIT,
						LPG_EN_LPG_OUT_BIT);
				if (rc < 0)
					break;
				val |= 1 << lpg_idx;
			}
		}
		mutex_lock(&lut->lock);
		rc = qpnp_lut_write(lut, REG_LPG_LUT_RAMP_CONTROL, val);
		if (rc < 0)
			dev_err(chip->dev, "Write LPG_LUT_RAMP_CONTROL failed, rc=%d\n",
					rc);
		mutex_unlock(&lut->lock);
	}

	return rc;
}

int qpnp_lpg_pwm_set_output_type(struct pwm_device *pwm,
				enum pwm_output_type output_type)
{
	struct qpnp_lpg_channel *lpg;
	enum lpg_src src_sel;
	int rc;
	bool is_enabled;

	if (!pwm) {
		pr_err("pwm cannot be NULL\n");
		return -ENODEV;
	}

	lpg = pwm_dev_to_qpnp_lpg(pwm->chip, pwm);
	if (lpg == NULL) {
		dev_err(pwm->chip->dev, "lpg not found\n");
		return -ENODEV;
	}

	if (lpg->chip->lut == NULL) {
		pr_debug("lpg%d only support PWM mode\n", lpg->lpg_idx);
		return 0;
	}

	if (output_type == lpg->output_type)
		return 0;

	src_sel = (output_type == PWM_OUTPUT_MODULATED) ?
				LUT_PATTERN : PWM_VALUE;
	if (src_sel == lpg->src_sel)
		return 0;

	is_enabled = pwm_is_enabled(pwm);
	if (is_enabled) {
		/*
		 * Disable the channel first then enable it later to make
		 * sure the output type is changed successfully. This is
		 * especially useful in SDAM use case to stop the PBS
		 * sequence when changing the PWM output type from
		 * MODULATED to FIXED.
		 */
		rc = qpnp_lpg_pwm_src_enable(lpg, false);
		if (rc < 0) {
			dev_err(pwm->chip->dev, "Enable PWM output failed for channel %d, rc=%d\n",
					lpg->lpg_idx, rc);
			return rc;
		}
	}

	if (src_sel == LUT_PATTERN) {
		/* program LUT if it's never been programmed */
		if (!lpg->lut_written) {
			rc = qpnp_lpg_set_lut_pattern(lpg,
					lpg->ramp_config.pattern,
					lpg->ramp_config.pattern_length);
			if (rc < 0) {
				dev_err(lpg->chip->dev, "set LUT pattern failed for LPG%d, rc=%d\n",
						lpg->lpg_idx, rc);
				return rc;
			}
			lpg->lut_written = true;
		}

		rc = qpnp_lpg_set_ramp_config(lpg);
		if (rc < 0) {
			dev_err(pwm->chip->dev, "Config LPG%d ramping failed, rc=%d\n",
					lpg->lpg_idx, rc);
			return rc;
		}
	}

	lpg->src_sel = src_sel;

	if (is_enabled) {
		rc = qpnp_lpg_set_pwm_config(lpg);
		if (rc < 0) {
			dev_err(pwm->chip->dev, "Config PWM failed for channel %d, rc=%d\n",
							lpg->lpg_idx, rc);
			return rc;
		}

		rc = qpnp_lpg_pwm_src_enable(lpg, true);
		if (rc < 0) {
			dev_err(pwm->chip->dev, "Enable PWM output failed for channel %d, rc=%d\n",
					lpg->lpg_idx, rc);
			return rc;
		}
	}

	lpg->output_type = output_type;

	return 0;
}
EXPORT_SYMBOL(qpnp_lpg_pwm_set_output_type);

int qpnp_lpg_pwm_get_output_types_supported(struct pwm_device *pwm)
{
	enum pwm_output_type type = PWM_OUTPUT_FIXED;
	struct qpnp_lpg_channel *lpg;

	if (!pwm) {
		pr_err("pwm cannot be NULL\n");
		return -ENODEV;
	}

	lpg = pwm_dev_to_qpnp_lpg(pwm->chip, pwm);
	if (lpg == NULL) {
		dev_err(pwm->chip->dev, "lpg not found\n");
		return -ENODEV;
	}

	if (lpg->chip->lut != NULL)
		type |= PWM_OUTPUT_MODULATED;

	return type;
}
EXPORT_SYMBOL(qpnp_lpg_pwm_get_output_types_supported);

static int qpnp_lpg_pwm_enable(struct pwm_chip *pwm_chip,
				struct pwm_device *pwm)
{
	struct qpnp_lpg_channel *lpg;
	int rc = 0;

	lpg = pwm_dev_to_qpnp_lpg(pwm_chip, pwm);
	if (lpg == NULL) {
		dev_err(pwm_chip->dev, "lpg not found\n");
		return -ENODEV;
	}

	/*
	 * Update PWM_VALUE_SYNC to make sure PWM_VALUE
	 * will be updated everytime before enabling.
	 */
	if (lpg->src_sel == PWM_VALUE) {
		rc = qpnp_lpg_write(lpg, REG_LPG_PWM_SYNC, LPG_PWM_VALUE_SYNC);
		if (rc < 0) {
			dev_err(lpg->chip->dev, "Write LPG_PWM_SYNC failed, rc=%d\n",
					rc);
			return rc;
		}
	}

	rc = qpnp_lpg_set_glitch_removal(lpg, true);
	if (rc < 0) {
		dev_err(lpg->chip->dev, "Enable glitch-removal failed, rc=%d\n",
							rc);
		return rc;
	}

	rc = qpnp_lpg_pwm_src_enable(lpg, true);
	if (rc < 0)
		dev_err(pwm_chip->dev, "Enable PWM output failed for channel %d, rc=%d\n",
						lpg->lpg_idx, rc);

	return rc;
}

static void qpnp_lpg_pwm_disable(struct pwm_chip *pwm_chip,
				struct pwm_device *pwm)
{
	struct qpnp_lpg_channel *lpg;
	int rc;

	lpg = pwm_dev_to_qpnp_lpg(pwm_chip, pwm);
	if (lpg == NULL) {
		dev_err(pwm_chip->dev, "lpg not found\n");
		return;
	}

	rc = qpnp_lpg_pwm_src_enable(lpg, false);
	if (rc < 0) {
		dev_err(pwm_chip->dev, "Disable PWM output failed for channel %d, rc=%d\n",
						lpg->lpg_idx, rc);
		return;
	}

	rc = qpnp_lpg_set_glitch_removal(lpg, false);
	if (rc < 0)
		dev_err(lpg->chip->dev, "Disable glitch-removal failed, rc=%d\n",
							rc);
}

static int qpnp_lpg_pwm_apply(struct pwm_chip *pwm_chip, struct pwm_device *pwm,
		     const struct pwm_state *state)
{
	int rc;

	if (state->period != pwm->state.period ||
			state->duty_cycle != pwm->state.duty_cycle) {
		rc = qpnp_lpg_pwm_config(pwm->chip, pwm,
				state->duty_cycle, state->period);
		if (rc < 0)
			return rc;

		pwm->state.duty_cycle = state->duty_cycle;
		pwm->state.period = state->period;
	}

	if (state->enabled != pwm->state.enabled) {
		if (state->enabled) {
			rc = qpnp_lpg_pwm_enable(pwm->chip, pwm);
			if (rc < 0)
				return rc;
		} else {
			qpnp_lpg_pwm_disable(pwm->chip, pwm);
		}

		pwm->state.enabled = state->enabled;
	}

	return 0;
}

static const struct pwm_ops qpnp_lpg_pwm_ops = {
	.apply = qpnp_lpg_pwm_apply,
	.owner = THIS_MODULE,
};

static int qpnp_get_lpg_channels(struct qpnp_lpg_chip *chip, u32 *base)
{
	int rc;
	const __be32 *addr;

	addr = of_get_address(chip->dev->of_node, 0, NULL, NULL);
	if (!addr) {
		dev_err(chip->dev, "Get %s address failed\n", LPG_BASE);
		return -EINVAL;
	}

	*base = be32_to_cpu(addr[0]);
	rc = of_property_read_u32(chip->dev->of_node, "qcom,num-lpg-channels",
						&chip->num_lpgs);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to get qcom,num-lpg-channels, rc=%d\n",
				rc);
		return rc;
	}

	if (chip->num_lpgs == 0) {
		dev_err(chip->dev, "No LPG channels specified\n");
		return -EINVAL;
	}

	return 0;
}

static int qpnp_lpg_parse_ramp_props_dt(struct device_node *node,
					struct qpnp_lpg_chip *chip,
					u32 lpg_chan_id, u32 max_count)
{
	int rc;
	u32 tmp;
	struct qpnp_lpg_channel *lpg;
	struct lpg_ramp_config *ramp;

	/* lpg channel id is indexed from 1 in hardware */
	lpg = &chip->lpgs[lpg_chan_id - 1];
	ramp = &lpg->ramp_config;

	rc = of_property_read_u32(node, "qcom,ramp-step-ms", &tmp);
	if (rc < 0) {
		dev_err(chip->dev, "get qcom,ramp-step-ms failed for lpg%d, rc=%d\n",
				lpg_chan_id, rc);
		return rc;
	}
	ramp->step_ms = (u16)tmp;

	rc = of_property_read_u32(node, "qcom,ramp-low-index", &tmp);
	if (rc < 0) {
		dev_err(chip->dev, "get qcom,ramp-low-index failed for lpg%d, rc=%d\n",
				lpg_chan_id, rc);
		return rc;
	}
	ramp->lo_idx = (u8)tmp;
	if (ramp->lo_idx >= max_count) {
		dev_err(chip->dev, "qcom,ramp-low-index should less than max %d\n",
				max_count);
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,ramp-high-index", &tmp);
	if (rc < 0) {
		dev_err(chip->dev, "get qcom,ramp-high-index failed for lpg%d, rc=%d\n",
				lpg_chan_id, rc);
		return rc;
	}
	ramp->hi_idx = (u8)tmp;

	if (ramp->hi_idx > max_count) {
		dev_err(chip->dev, "qcom,ramp-high-index shouldn't exceed max %d\n",
				max_count);
		return -EINVAL;
	}

	if (chip->use_sdam && ramp->hi_idx <= ramp->lo_idx) {
		dev_err(chip->dev, "high-index(%d) should be larger than low-index(%d) when SDAM used\n",
				ramp->hi_idx, ramp->lo_idx);
		return -EINVAL;
	}

	ramp->pattern_length = ramp->hi_idx - ramp->lo_idx + 1;
	ramp->pattern = &chip->lut->pattern[ramp->lo_idx];
	lpg->max_pattern_length = ramp->pattern_length;

	ramp->pattern_repeat = of_property_read_bool(node,
			"qcom,ramp-pattern-repeat");

	ramp->pause_hi_count = 0;
	rc = of_property_read_u32(node, "qcom,ramp-pause-hi-count", &tmp);
	if (!rc) {
		if (chip->use_sdam && tmp > SDAM_PAUSE_COUNT_MAX)
			return -EINVAL;
		ramp->pause_hi_count = (u8)tmp;
	}

	ramp->pause_lo_count = 0;
	rc = of_property_read_u32(node, "qcom,ramp-pause-lo-count", &tmp);
	if (!rc) {
		if (chip->use_sdam && tmp > SDAM_PAUSE_COUNT_MAX)
			return -EINVAL;
		ramp->pause_lo_count = (u8)tmp;
	}

	if (chip->use_sdam)
		return 0;

	ramp->ramp_dir_low_to_hi = of_property_read_bool(node,
			"qcom,ramp-from-low-to-high");

	ramp->toggle =  of_property_read_bool(node, "qcom,ramp-toggle");

	return rc;
}

static int qpnp_lpg_parse_pattern_dt(struct qpnp_lpg_chip *chip,
					u32 max_count)
{
	struct device_node *child;
	struct qpnp_lpg_channel *lpg;
	struct lpg_ramp_config *ramp;
	int rc = 0, i;
	u32 length, lpg_chan_id, tmp;

	chip->lut->chip = chip;
	mutex_init(&chip->lut->lock);

	rc = of_property_count_elems_of_size(chip->dev->of_node,
			"qcom,lut-patterns", sizeof(u32));
	if (rc < 0) {
		dev_err(chip->dev, "Read qcom,lut-patterns failed, rc=%d\n",
							rc);
		return rc;
	}

	length = rc;
	if (length > max_count) {
		dev_err(chip->dev, "qcom,lut-patterns length %d exceed max %d\n",
				length, max_count);
		return -EINVAL;
	}

	chip->lut->pattern = devm_kcalloc(chip->dev, max_count,
			sizeof(*chip->lut->pattern), GFP_KERNEL);
	if (!chip->lut->pattern)
		return -ENOMEM;

	rc = of_property_read_u32_array(chip->dev->of_node, "qcom,lut-patterns",
					chip->lut->pattern, length);
	if (rc < 0) {
		dev_err(chip->dev, "Get qcom,lut-patterns failed, rc=%d\n",
				rc);
		return rc;
	}

	if (of_get_available_child_count(chip->dev->of_node) == 0) {
		dev_err(chip->dev, "No ramp configuration for any LPG\n");
		return -EINVAL;
	}

	for_each_available_child_of_node(chip->dev->of_node, child) {
		rc = of_property_read_u32(child, "qcom,lpg-chan-id",
						&lpg_chan_id);
		if (rc < 0) {
			dev_err(chip->dev, "Get qcom,lpg-chan-id failed for node %s, rc=%d\n",
					child->name, rc);
			return rc;
		}

		if (lpg_chan_id < 1 || lpg_chan_id > chip->num_lpgs) {
			dev_err(chip->dev, "lpg-chann-id %d is out of range 1~%d\n",
					lpg_chan_id, chip->num_lpgs);
			return -EINVAL;
		}

		if (chip->lpgs[lpg_chan_id - 1].enable_pfm) {
			dev_err(chip->dev, "Cannot configure ramp for PFM-enabled channel %d\n",
					lpg_chan_id);
			return -EINVAL;
		}

		if (chip->use_sdam) {
			rc = of_property_read_u32(child,
					"qcom,lpg-sdam-base",
					&tmp);
			if (rc < 0) {
				dev_err(chip->dev, "get qcom,lpg-sdam-base failed for lpg%d, rc=%d\n",
						lpg_chan_id, rc);
				return rc;
			}
			chip->lpgs[lpg_chan_id - 1].lpg_sdam_base = tmp;
		}

		rc = qpnp_lpg_parse_ramp_props_dt(child, chip, lpg_chan_id,
						  max_count);
		if (rc) {
			dev_err(chip->dev, "Parsing ramp props failed for lpg%d, rc=%d\n",
				lpg_chan_id, rc);
			return rc;
		}
	}

	rc = of_property_count_elems_of_size(chip->dev->of_node,
			"qcom,sync-channel-ids", sizeof(u32));
	if (rc < 0)
		return 0;

	length = rc;
	if (length > chip->num_lpgs) {
		dev_err(chip->dev, "qcom,sync-channel-ids has too many channels: %d\n",
				length);
		return -EINVAL;
	}

	chip->lpg_group = devm_kcalloc(chip->dev, chip->num_lpgs,
			sizeof(u32), GFP_KERNEL);
	if (!chip->lpg_group)
		return -ENOMEM;

	rc = of_property_read_u32_array(chip->dev->of_node,
			"qcom,sync-channel-ids", chip->lpg_group, length);
	if (rc < 0) {
		dev_err(chip->dev, "Get qcom,sync-channel-ids failed, rc=%d\n",
				rc);
		return rc;
	}

	for (i = 0; i < length; i++) {
		if (chip->lpg_group[i] <= 0 ||
				chip->lpg_group[i] > chip->num_lpgs) {
			dev_err(chip->dev, "lpg_group[%d]: %d is not a valid channel\n",
					i, chip->lpg_group[i]);
			return -EINVAL;
		}
	}

	/*
	 * The LPG channel in the same group should have the same ramping
	 * configuration, so force to use the ramping configuration of the
	 * 1st LPG channel in the group for synchronization.
	 */
	lpg = &chip->lpgs[chip->lpg_group[0] - 1];
	ramp = &lpg->ramp_config;

	for (i = 1; i < length; i++) {
		lpg = &chip->lpgs[chip->lpg_group[i] - 1];
		memcpy(&lpg->ramp_config, ramp, sizeof(struct lpg_ramp_config));
	}

	return 0;
}

static bool lut_is_defined(struct qpnp_lpg_chip *chip, const __be32 **addr)
{
	*addr = of_get_address(chip->dev->of_node, 1, NULL, NULL);
	if (*addr == NULL)
		return false;

	return true;
}

static int qpnp_lpg_get_nvmem_dt(struct qpnp_lpg_chip *chip)
{
	int rc = 0;
	struct nvmem_device *ppg_nv, *lut_nv, *lpg_nv;

	/* Ensure backward compatibility */
	ppg_nv = devm_nvmem_device_get(chip->dev, "ppg_sdam");
	if (IS_ERR(ppg_nv)) {
		lut_nv = devm_nvmem_device_get(chip->dev, "lut_sdam");
		if (IS_ERR(lut_nv)) {
			rc = PTR_ERR(lut_nv);
			goto err;
		}

		lpg_nv = devm_nvmem_device_get(chip->dev, "lpg_chan_sdam");
		if (IS_ERR(lpg_nv)) {
			rc = PTR_ERR(lpg_nv);
			goto err;
		}

		chip->lut_nvmem = lut_nv;
		chip->lpg_chan_nvmem = lpg_nv;
		chip->lut->nvmem_count = PPG_NVMEMS_2;
	} else {
		chip->lut_nvmem = chip->lpg_chan_nvmem = ppg_nv;
		chip->lut->nvmem_count = PPG_NVMEMS_1;
	}

	return 0;
err:
	if (rc != -EPROBE_DEFER)
		dev_err(chip->dev, "Failed to get nvmem device, rc=%d\n",
				rc);
	return rc;
}

static int qpnp_lpg_parse_pfm_support(struct qpnp_lpg_chip *chip)
{
	u32 chan_idx;
	u32 num_pfm_channels;
	u8 val;
	int i, rc;

	rc = of_property_count_elems_of_size(chip->dev->of_node,
					     "qcom,pfm-chan-ids",
					     sizeof(u32));
	if (rc < 0)
		return rc;

	if (rc == 0 || rc > chip->num_lpgs)
		return -EINVAL;

	num_pfm_channels = rc;

	for (i = 0; i < num_pfm_channels; i++) {
		rc = of_property_read_u32_index(chip->dev->of_node,
				"qcom,pfm-chan-ids", i, &chan_idx);
		if (rc) {
			dev_err(chip->dev, "Read pfm channel %d failed, rc=%d\n",
					i, rc);
			return -EINVAL;
		}

		if (chan_idx < 1 || chan_idx > chip->num_lpgs) {
			dev_err(chip->dev, "pfm chan id %u is out of range 1~%d\n",
					chan_idx, chip->num_lpgs);
			return -EINVAL;
		}

		chan_idx--; /* zero-based indexing */

		if (chip->lpgs[chan_idx].subtype == SUBTYPE_PWM ||
			chip->lpgs[chan_idx].subtype == SUBTYPE_HI_RES_PWM) {
			rc = qpnp_lpg_read(&chip->lpgs[chan_idx],
					   REG_PWM_STATUS1, &val);
			if (rc < 0) {
				dev_err(chip->dev, "Read status1 failed, rc=%d\n",
					rc);
				return rc;
			}

			if (val & FM_MODE_PRESENT)
				chip->lpgs[chan_idx].enable_pfm = true;
			else
				return -EOPNOTSUPP;
		}
	}

	return rc;
}

#define DEFAULT_TICK_DURATION_US	7800
static int qpnp_lpg_parse_dt(struct qpnp_lpg_chip *chip)
{
	int rc = 0, i;
	u32 base;
	const __be32 *addr;

	rc = qpnp_get_lpg_channels(chip, &base);
	if (rc < 0)
		return rc;

	chip->lpgs = devm_kcalloc(chip->dev, chip->num_lpgs,
			sizeof(*chip->lpgs), GFP_KERNEL);
	if (!chip->lpgs)
		return -ENOMEM;

	for (i = 0; i < chip->num_lpgs; i++) {
		chip->lpgs[i].chip = chip;
		chip->lpgs[i].lpg_idx = i;
		chip->lpgs[i].reg_base = base + i * REG_SIZE_PER_LPG;
		chip->lpgs[i].src_sel = PWM_VALUE;
		rc = qpnp_lpg_read(&chip->lpgs[i], REG_LPG_PERPH_SUBTYPE,
				&chip->lpgs[i].subtype);
		if (rc < 0) {
			dev_err(chip->dev, "Read subtype failed, rc=%d\n", rc);
			return rc;
		}
	}

	if (of_find_property(chip->dev->of_node,
				"qcom,pfm-chan-ids", NULL)) {
		rc = qpnp_lpg_parse_pfm_support(chip);
		if (rc < 0) {
			dev_err(chip->dev, "PFM channels specified incorrectly\n");
			return rc;
		}
	}

	if (of_find_property(chip->dev->of_node, "nvmem", NULL)) {
		chip->lut = devm_kmalloc(chip->dev, sizeof(*chip->lut),
				GFP_KERNEL);
		if (!chip->lut)
			return -ENOMEM;

		rc = qpnp_lpg_get_nvmem_dt(chip);
		if (rc < 0)
			return rc;

		chip->use_sdam = true;
		if (of_find_property(chip->dev->of_node, "qcom,pbs-client",
					NULL)) {
			chip->pbs_dev_node = of_parse_phandle(
					chip->dev->of_node,
					"qcom,pbs-client", 0);
			if (!chip->pbs_dev_node) {
				dev_err(chip->dev, "Missing qcom,pbs-client property\n");
				return -ENODEV;
			}
		}

		rc = of_property_read_u32(chip->dev->of_node,
				"qcom,lut-sdam-base", &chip->lut->reg_base);
		if (rc < 0) {
			dev_err(chip->dev, "Read qcom,lut-sdam-base failed, rc=%d\n",
					rc);
			of_node_put(chip->pbs_dev_node);
			return rc;
		}

		chip->lut->ramp_step_tick_us = DEFAULT_TICK_DURATION_US;
		of_property_read_u32(chip->dev->of_node, "qcom,tick-duration-us",
				&chip->lut->ramp_step_tick_us);

		rc = qpnp_lpg_parse_pattern_dt(chip, SDAM_LUT_COUNT_MAX);
		if (rc < 0) {
			of_node_put(chip->pbs_dev_node);
			return rc;
		}
	} else if (lut_is_defined(chip, &addr)) {
		chip->lut = devm_kmalloc(chip->dev, sizeof(*chip->lut),
				GFP_KERNEL);
		if (!chip->lut)
			return -ENOMEM;

		chip->lut->reg_base = be32_to_cpu(*addr);

		rc = qpnp_lpg_parse_pattern_dt(chip, LPG_LUT_COUNT_MAX);
		if (rc < 0)
			return rc;
	} else {
		pr_debug("Neither SDAM nor LUT specified\n");
	}

	return 0;
}

static int qpnp_lpg_sdam_hw_init(struct qpnp_lpg_chip *chip)
{
	struct qpnp_lpg_channel *lpg;
	int i, rc = 0;

	if (!chip->use_sdam)
		return 0;

	for (i = 0; i < chip->num_lpgs; i++) {
		lpg = &chip->lpgs[i];
		if (lpg->lpg_sdam_base != 0) {
			rc = qpnp_lpg_sdam_write(lpg, SDAM_LUT_EN_OFFSET, 0);
			if (rc < 0) {
				dev_err(chip->dev, "Write SDAM_REG_LUT_EN failed, rc=%d\n",
						rc);
				return rc;
			}
			rc = qpnp_lpg_sdam_write(lpg,
					SDAM_PBS_SCRATCH_LUT_COUNTER_OFFSET, 0);
			if (rc < 0) {
				dev_err(lpg->chip->dev, "Write SDAM_REG_PBS_SCRATCH_LUT_COUNTER failed, rc=%d\n",
						rc);
				return rc;
			}
		}
	}

	return rc;
}

static int qpnp_lpg_probe(struct platform_device *pdev)
{
	int rc, i;
	struct qpnp_lpg_chip *chip;
	struct qpnp_lpg_channel *lpg;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->regmap = dev_get_regmap(chip->dev->parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Getting regmap failed\n");
		return -EINVAL;
	}

	mutex_init(&chip->bus_lock);
	rc = qpnp_lpg_parse_dt(chip);
	if (rc < 0) {
		if (rc != -EPROBE_DEFER)
			dev_err(chip->dev, "Devicetree properties parsing failed, rc=%d\n",
				rc);
		goto err_out;
	}

	rc = qpnp_lpg_sdam_hw_init(chip);
	if (rc < 0) {
		dev_err(chip->dev, "SDAM HW init failed, rc=%d\n",
				rc);
		goto err_out;
	}

	for (i = 0; i < chip->num_lpgs; i++) {
		lpg = &chip->lpgs[i];
		lpg->output_type = PWM_OUTPUT_FIXED;

		if (chip->lut != NULL) {
			rc = qpnp_lpg_pwm_src_enable(lpg, false);
			if (rc < 0) {
				dev_err(chip->dev, "Disable PWM output failed for channel %d, rc=%d\n",
					lpg->lpg_idx, rc);
				return rc;
			}
		}

		if (lpg->enable_pfm) {
			rc = qpnp_lpg_write(lpg, REG_PWM_FM_MODE,
					FM_MODE_ENABLE);
			if (rc < 0) {
				dev_err(chip->dev, "Write fm_mode_enable failed, rc=%d\n",
					rc);
				return rc;
			}
		}
	}

	dev_set_drvdata(chip->dev, chip);
	chip->pwm_chip.dev = chip->dev;
	chip->pwm_chip.base = -1;
	chip->pwm_chip.npwm = chip->num_lpgs;
	chip->pwm_chip.ops = &qpnp_lpg_pwm_ops;

	rc = pwmchip_add(&chip->pwm_chip);
	if (rc < 0) {
		dev_err(chip->dev, "Add pwmchip failed, rc=%d\n", rc);
		goto err_out;
	}

	return 0;
err_out:
	mutex_destroy(&chip->bus_lock);
	return rc;
}

static int qpnp_lpg_remove(struct platform_device *pdev)
{
	struct qpnp_lpg_chip *chip = dev_get_drvdata(&pdev->dev);

	of_node_put(chip->pbs_dev_node);
	pwmchip_remove(&chip->pwm_chip);
	mutex_destroy(&chip->bus_lock);
	dev_set_drvdata(chip->dev, NULL);

	return 0;
}

static const struct of_device_id qpnp_lpg_of_match[] = {
	{ .compatible = "qcom,pwm-lpg",},
	{ },
};

static struct platform_driver qpnp_lpg_driver = {
	.driver		= {
		.name		= "qcom,pwm-lpg",
		.of_match_table	= qpnp_lpg_of_match,
	},
	.probe		= qpnp_lpg_probe,
	.remove		= qpnp_lpg_remove,
};
module_platform_driver(qpnp_lpg_driver);

MODULE_DESCRIPTION("QTI LPG driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("pwm:pwm-lpg");
