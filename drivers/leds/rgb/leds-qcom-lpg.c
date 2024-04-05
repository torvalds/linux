// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2022 Linaro Ltd
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/led-class-multicolor.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qcom-pbs.h>

#define LPG_SUBTYPE_REG		0x05
#define  LPG_SUBTYPE_LPG	0x2
#define  LPG_SUBTYPE_PWM	0xb
#define  LPG_SUBTYPE_HI_RES_PWM	0xc
#define  LPG_SUBTYPE_LPG_LITE	0x11
#define LPG_PATTERN_CONFIG_REG	0x40
#define LPG_SIZE_CLK_REG	0x41
#define  PWM_CLK_SELECT_MASK	GENMASK(1, 0)
#define  PWM_CLK_SELECT_HI_RES_MASK	GENMASK(2, 0)
#define  PWM_SIZE_HI_RES_MASK	GENMASK(6, 4)
#define LPG_PREDIV_CLK_REG	0x42
#define  PWM_FREQ_PRE_DIV_MASK	GENMASK(6, 5)
#define  PWM_FREQ_EXP_MASK	GENMASK(2, 0)
#define PWM_TYPE_CONFIG_REG	0x43
#define PWM_VALUE_REG		0x44
#define PWM_ENABLE_CONTROL_REG	0x46
#define PWM_SYNC_REG		0x47
#define LPG_RAMP_DURATION_REG	0x50
#define LPG_HI_PAUSE_REG	0x52
#define LPG_LO_PAUSE_REG	0x54
#define LPG_HI_IDX_REG		0x56
#define LPG_LO_IDX_REG		0x57
#define PWM_SEC_ACCESS_REG	0xd0
#define PWM_DTEST_REG(x)	(0xe2 + (x) - 1)

#define SDAM_REG_PBS_SEQ_EN		0x42
#define SDAM_PBS_TRIG_SET		0xe5
#define SDAM_PBS_TRIG_CLR		0xe6

#define TRI_LED_SRC_SEL		0x45
#define TRI_LED_EN_CTL		0x46
#define TRI_LED_ATC_CTL		0x47

#define LPG_LUT_REG(x)		(0x40 + (x) * 2)
#define RAMP_CONTROL_REG	0xc8

#define LPG_RESOLUTION_9BIT	BIT(9)
#define LPG_RESOLUTION_15BIT	BIT(15)
#define PPG_MAX_LED_BRIGHTNESS	255

#define LPG_MAX_M		7
#define LPG_MAX_PREDIV		6

#define DEFAULT_TICK_DURATION_US	7800
#define RAMP_STEP_DURATION(x)		(((x) * 1000 / DEFAULT_TICK_DURATION_US) & 0xff)

#define SDAM_MAX_DEVICES	2
/* LPG common config settings for PPG */
#define SDAM_START_BASE			0x40
#define SDAM_REG_RAMP_STEP_DURATION		0x47

#define SDAM_LUT_SDAM_LUT_PATTERN_OFFSET	0x45
#define SDAM_LPG_SDAM_LUT_PATTERN_OFFSET	0x80

/* LPG per channel config settings for PPG */
#define SDAM_LUT_EN_OFFSET			0x0
#define SDAM_PATTERN_CONFIG_OFFSET		0x1
#define SDAM_END_INDEX_OFFSET			0x3
#define SDAM_START_INDEX_OFFSET		0x4
#define SDAM_PBS_SCRATCH_LUT_COUNTER_OFFSET	0x6
#define SDAM_PAUSE_HI_MULTIPLIER_OFFSET	0x8
#define SDAM_PAUSE_LO_MULTIPLIER_OFFSET	0x9

struct lpg_channel;
struct lpg_data;

/**
 * struct lpg - LPG device context
 * @dev:	pointer to LPG device
 * @map:	regmap for register access
 * @lock:	used to synchronize LED and pwm callback requests
 * @pwm:	PWM-chip object, if operating in PWM mode
 * @data:	reference to version specific data
 * @lut_base:	base address of the LUT block (optional)
 * @lut_size:	number of entries in the LUT block
 * @lut_bitmap:	allocation bitmap for LUT entries
 * @pbs_dev:	PBS device
 * @lpg_chan_sdam:	LPG SDAM peripheral device
 * @lut_sdam:	LUT SDAM peripheral device
 * @pbs_en_bitmap:	bitmap for tracking PBS triggers
 * @triled_base: base address of the TRILED block (optional)
 * @triled_src:	power-source for the TRILED
 * @triled_has_atc_ctl:	true if there is TRI_LED_ATC_CTL register
 * @triled_has_src_sel:	true if there is TRI_LED_SRC_SEL register
 * @channels:	list of PWM channels
 * @num_channels: number of @channels
 */
struct lpg {
	struct device *dev;
	struct regmap *map;

	struct mutex lock;

	struct pwm_chip *pwm;

	const struct lpg_data *data;

	u32 lut_base;
	u32 lut_size;
	unsigned long *lut_bitmap;

	struct pbs_dev *pbs_dev;
	struct nvmem_device *lpg_chan_sdam;
	struct nvmem_device *lut_sdam;
	unsigned long pbs_en_bitmap;

	u32 triled_base;
	u32 triled_src;
	bool triled_has_atc_ctl;
	bool triled_has_src_sel;

	struct lpg_channel *channels;
	unsigned int num_channels;
};

/**
 * struct lpg_channel - per channel data
 * @lpg:	reference to parent lpg
 * @base:	base address of the PWM channel
 * @triled_mask: mask in TRILED to enable this channel
 * @lut_mask:	mask in LUT to start pattern generator for this channel
 * @subtype:	PMIC hardware block subtype
 * @sdam_offset:	channel offset in LPG SDAM
 * @in_use:	channel is exposed to LED framework
 * @color:	color of the LED attached to this channel
 * @dtest_line:	DTEST line for output, or 0 if disabled
 * @dtest_value: DTEST line configuration
 * @pwm_value:	duty (in microseconds) of the generated pulses, overridden by LUT
 * @enabled:	output enabled?
 * @period:	period (in nanoseconds) of the generated pulses
 * @clk_sel:	reference clock frequency selector
 * @pre_div_sel: divider selector of the reference clock
 * @pre_div_exp: exponential divider of the reference clock
 * @pwm_resolution_sel:	pwm resolution selector
 * @ramp_enabled: duty cycle is driven by iterating over lookup table
 * @ramp_ping_pong: reverse through pattern, rather than wrapping to start
 * @ramp_oneshot: perform only a single pass over the pattern
 * @ramp_reverse: iterate over pattern backwards
 * @ramp_tick_ms: length (in milliseconds) of one step in the pattern
 * @ramp_lo_pause_ms: pause (in milliseconds) before iterating over pattern
 * @ramp_hi_pause_ms: pause (in milliseconds) after iterating over pattern
 * @pattern_lo_idx: start index of associated pattern
 * @pattern_hi_idx: last index of associated pattern
 */
struct lpg_channel {
	struct lpg *lpg;

	u32 base;
	unsigned int triled_mask;
	unsigned int lut_mask;
	unsigned int subtype;
	u32 sdam_offset;

	bool in_use;

	int color;

	u32 dtest_line;
	u32 dtest_value;

	u16 pwm_value;
	bool enabled;

	u64 period;
	unsigned int clk_sel;
	unsigned int pre_div_sel;
	unsigned int pre_div_exp;
	unsigned int pwm_resolution_sel;

	bool ramp_enabled;
	bool ramp_ping_pong;
	bool ramp_oneshot;
	bool ramp_reverse;
	unsigned short ramp_tick_ms;
	unsigned long ramp_lo_pause_ms;
	unsigned long ramp_hi_pause_ms;

	unsigned int pattern_lo_idx;
	unsigned int pattern_hi_idx;
};

/**
 * struct lpg_led - logical LED object
 * @lpg:		lpg context reference
 * @cdev:		LED class device
 * @mcdev:		Multicolor LED class device
 * @num_channels:	number of @channels
 * @channels:		list of channels associated with the LED
 */
struct lpg_led {
	struct lpg *lpg;

	struct led_classdev cdev;
	struct led_classdev_mc mcdev;

	unsigned int num_channels;
	struct lpg_channel *channels[] __counted_by(num_channels);
};

/**
 * struct lpg_channel_data - per channel initialization data
 * @sdam_offset:	Channel offset in LPG SDAM
 * @base:		base address for PWM channel registers
 * @triled_mask:	bitmask for controlling this channel in TRILED
 */
struct lpg_channel_data {
	unsigned int sdam_offset;
	unsigned int base;
	u8 triled_mask;
};

/**
 * struct lpg_data - initialization data
 * @lut_base:		base address of LUT block
 * @lut_size:		number of entries in LUT
 * @triled_base:	base address of TRILED
 * @triled_has_atc_ctl:	true if there is TRI_LED_ATC_CTL register
 * @triled_has_src_sel:	true if there is TRI_LED_SRC_SEL register
 * @num_channels:	number of channels in LPG
 * @channels:		list of channel initialization data
 */
struct lpg_data {
	unsigned int lut_base;
	unsigned int lut_size;
	unsigned int triled_base;
	bool triled_has_atc_ctl;
	bool triled_has_src_sel;
	int num_channels;
	const struct lpg_channel_data *channels;
};

#define PBS_SW_TRIG_BIT		BIT(0)

static int lpg_clear_pbs_trigger(struct lpg *lpg, unsigned int lut_mask)
{
	u8 val = 0;
	int rc;

	lpg->pbs_en_bitmap &= (~lut_mask);
	if (!lpg->pbs_en_bitmap) {
		rc = nvmem_device_write(lpg->lpg_chan_sdam, SDAM_REG_PBS_SEQ_EN, 1, &val);
		if (rc < 0)
			return rc;

		if (lpg->lut_sdam) {
			val = PBS_SW_TRIG_BIT;
			rc = nvmem_device_write(lpg->lpg_chan_sdam, SDAM_PBS_TRIG_CLR, 1, &val);
			if (rc < 0)
				return rc;
		}
	}

	return 0;
}

static int lpg_set_pbs_trigger(struct lpg *lpg, unsigned int lut_mask)
{
	u8 val = PBS_SW_TRIG_BIT;
	int rc;

	if (!lpg->pbs_en_bitmap) {
		rc = nvmem_device_write(lpg->lpg_chan_sdam, SDAM_REG_PBS_SEQ_EN, 1, &val);
		if (rc < 0)
			return rc;

		if (lpg->lut_sdam) {
			rc = nvmem_device_write(lpg->lpg_chan_sdam, SDAM_PBS_TRIG_SET, 1, &val);
			if (rc < 0)
				return rc;
		} else {
			rc = qcom_pbs_trigger_event(lpg->pbs_dev, val);
			if (rc < 0)
				return rc;
		}
	}
	lpg->pbs_en_bitmap |= lut_mask;

	return 0;
}

static int lpg_sdam_configure_triggers(struct lpg_channel *chan, u8 set_trig)
{
	u32 addr = SDAM_LUT_EN_OFFSET + chan->sdam_offset;

	if (!chan->lpg->lpg_chan_sdam)
		return 0;

	return nvmem_device_write(chan->lpg->lpg_chan_sdam, addr, 1, &set_trig);
}

static int triled_set(struct lpg *lpg, unsigned int mask, unsigned int enable)
{
	/* Skip if we don't have a triled block */
	if (!lpg->triled_base)
		return 0;

	return regmap_update_bits(lpg->map, lpg->triled_base + TRI_LED_EN_CTL,
				  mask, enable);
}

static int lpg_lut_store_sdam(struct lpg *lpg, struct led_pattern *pattern,
			 size_t len, unsigned int *lo_idx, unsigned int *hi_idx)
{
	unsigned int idx;
	u8 brightness;
	int i, rc;
	u16 addr;

	if (len > lpg->lut_size) {
		dev_err(lpg->dev, "Pattern length (%zu) exceeds maximum pattern length (%d)\n",
			len, lpg->lut_size);
		return -EINVAL;
	}

	idx = bitmap_find_next_zero_area(lpg->lut_bitmap, lpg->lut_size, 0, len, 0);
	if (idx >= lpg->lut_size)
		return -ENOSPC;

	for (i = 0; i < len; i++) {
		brightness = pattern[i].brightness;

		if (lpg->lut_sdam) {
			addr = SDAM_LUT_SDAM_LUT_PATTERN_OFFSET + i + idx;
			rc = nvmem_device_write(lpg->lut_sdam, addr, 1, &brightness);
		} else {
			addr = SDAM_LPG_SDAM_LUT_PATTERN_OFFSET + i + idx;
			rc = nvmem_device_write(lpg->lpg_chan_sdam, addr, 1, &brightness);
		}

		if (rc < 0)
			return rc;
	}

	bitmap_set(lpg->lut_bitmap, idx, len);

	*lo_idx = idx;
	*hi_idx = idx + len - 1;

	return 0;
}

static int lpg_lut_store(struct lpg *lpg, struct led_pattern *pattern,
			 size_t len, unsigned int *lo_idx, unsigned int *hi_idx)
{
	unsigned int idx;
	u16 val;
	int i;

	idx = bitmap_find_next_zero_area(lpg->lut_bitmap, lpg->lut_size,
					 0, len, 0);
	if (idx >= lpg->lut_size)
		return -ENOMEM;

	for (i = 0; i < len; i++) {
		val = pattern[i].brightness;

		regmap_bulk_write(lpg->map, lpg->lut_base + LPG_LUT_REG(idx + i),
				  &val, sizeof(val));
	}

	bitmap_set(lpg->lut_bitmap, idx, len);

	*lo_idx = idx;
	*hi_idx = idx + len - 1;

	return 0;
}

static void lpg_lut_free(struct lpg *lpg, unsigned int lo_idx, unsigned int hi_idx)
{
	int len;

	len = hi_idx - lo_idx + 1;
	if (len == 1)
		return;

	bitmap_clear(lpg->lut_bitmap, lo_idx, len);
}

static int lpg_lut_sync(struct lpg *lpg, unsigned int mask)
{
	if (!lpg->lut_base)
		return 0;

	return regmap_write(lpg->map, lpg->lut_base + RAMP_CONTROL_REG, mask);
}

static const unsigned int lpg_clk_rates[] = {0, 1024, 32768, 19200000};
static const unsigned int lpg_clk_rates_hi_res[] = {0, 1024, 32768, 19200000, 76800000};
static const unsigned int lpg_pre_divs[] = {1, 3, 5, 6};
static const unsigned int lpg_pwm_resolution[] =  {9};
static const unsigned int lpg_pwm_resolution_hi_res[] =  {8, 9, 10, 11, 12, 13, 14, 15};

static int lpg_calc_freq(struct lpg_channel *chan, uint64_t period)
{
	unsigned int i, pwm_resolution_count, best_pwm_resolution_sel = 0;
	const unsigned int *clk_rate_arr, *pwm_resolution_arr;
	unsigned int clk_sel, clk_len, best_clk = 0;
	unsigned int div, best_div = 0;
	unsigned int m, best_m = 0;
	unsigned int resolution;
	unsigned int error;
	unsigned int best_err = UINT_MAX;
	u64 max_period, min_period;
	u64 best_period = 0;
	u64 max_res;

	/*
	 * The PWM period is determined by:
	 *
	 *          resolution * pre_div * 2^M
	 * period = --------------------------
	 *                   refclk
	 *
	 * Resolution = 2^9 bits for PWM or
	 *              2^{8, 9, 10, 11, 12, 13, 14, 15} bits for high resolution PWM
	 * pre_div = {1, 3, 5, 6} and
	 * M = [0..7].
	 *
	 * This allows for periods between 27uS and 384s for PWM channels and periods between
	 * 3uS and 24576s for high resolution PWMs.
	 * The PWM framework wants a period of equal or lower length than requested,
	 * reject anything below minimum period.
	 */

	if (chan->subtype == LPG_SUBTYPE_HI_RES_PWM) {
		clk_rate_arr = lpg_clk_rates_hi_res;
		clk_len = ARRAY_SIZE(lpg_clk_rates_hi_res);
		pwm_resolution_arr = lpg_pwm_resolution_hi_res;
		pwm_resolution_count = ARRAY_SIZE(lpg_pwm_resolution_hi_res);
		max_res = LPG_RESOLUTION_15BIT;
	} else {
		clk_rate_arr = lpg_clk_rates;
		clk_len = ARRAY_SIZE(lpg_clk_rates);
		pwm_resolution_arr = lpg_pwm_resolution;
		pwm_resolution_count = ARRAY_SIZE(lpg_pwm_resolution);
		max_res = LPG_RESOLUTION_9BIT;
	}

	min_period = div64_u64((u64)NSEC_PER_SEC * (1 << pwm_resolution_arr[0]),
			       clk_rate_arr[clk_len - 1]);
	if (period <= min_period)
		return -EINVAL;

	/* Limit period to largest possible value, to avoid overflows */
	max_period = div64_u64((u64)NSEC_PER_SEC * max_res * LPG_MAX_PREDIV * (1 << LPG_MAX_M),
			       1024);
	if (period > max_period)
		period = max_period;

	/*
	 * Search for the pre_div, refclk, resolution and M by solving the rewritten formula
	 * for each refclk, resolution and pre_div value:
	 *
	 *                     period * refclk
	 * M = log2 -------------------------------------
	 *           NSEC_PER_SEC * pre_div * resolution
	 */

	for (i = 0; i < pwm_resolution_count; i++) {
		resolution = 1 << pwm_resolution_arr[i];
		for (clk_sel = 1; clk_sel < clk_len; clk_sel++) {
			u64 numerator = period * clk_rate_arr[clk_sel];

			for (div = 0; div < ARRAY_SIZE(lpg_pre_divs); div++) {
				u64 denominator = (u64)NSEC_PER_SEC * lpg_pre_divs[div] *
						  resolution;
				u64 actual;
				u64 ratio;

				if (numerator < denominator)
					continue;

				ratio = div64_u64(numerator, denominator);
				m = ilog2(ratio);
				if (m > LPG_MAX_M)
					m = LPG_MAX_M;

				actual = DIV_ROUND_UP_ULL(denominator * (1 << m),
							  clk_rate_arr[clk_sel]);
				error = period - actual;
				if (error < best_err) {
					best_err = error;
					best_div = div;
					best_m = m;
					best_clk = clk_sel;
					best_period = actual;
					best_pwm_resolution_sel = i;
				}
			}
		}
	}
	chan->clk_sel = best_clk;
	chan->pre_div_sel = best_div;
	chan->pre_div_exp = best_m;
	chan->period = best_period;
	chan->pwm_resolution_sel = best_pwm_resolution_sel;
	return 0;
}

static void lpg_calc_duty(struct lpg_channel *chan, uint64_t duty)
{
	unsigned int max;
	unsigned int val;
	unsigned int clk_rate;

	if (chan->subtype == LPG_SUBTYPE_HI_RES_PWM) {
		max = LPG_RESOLUTION_15BIT - 1;
		clk_rate = lpg_clk_rates_hi_res[chan->clk_sel];
	} else {
		max = LPG_RESOLUTION_9BIT - 1;
		clk_rate = lpg_clk_rates[chan->clk_sel];
	}

	val = div64_u64(duty * clk_rate,
			(u64)NSEC_PER_SEC * lpg_pre_divs[chan->pre_div_sel] * (1 << chan->pre_div_exp));

	chan->pwm_value = min(val, max);
}

static void lpg_apply_freq(struct lpg_channel *chan)
{
	unsigned long val;
	struct lpg *lpg = chan->lpg;

	if (!chan->enabled)
		return;

	val = chan->clk_sel;

	/* Specify resolution, based on the subtype of the channel */
	switch (chan->subtype) {
	case LPG_SUBTYPE_LPG:
		val |= GENMASK(5, 4);
		break;
	case LPG_SUBTYPE_PWM:
		val |= BIT(2);
		break;
	case LPG_SUBTYPE_HI_RES_PWM:
		val |= FIELD_PREP(PWM_SIZE_HI_RES_MASK, chan->pwm_resolution_sel);
		break;
	case LPG_SUBTYPE_LPG_LITE:
	default:
		val |= BIT(4);
		break;
	}

	regmap_write(lpg->map, chan->base + LPG_SIZE_CLK_REG, val);

	val = FIELD_PREP(PWM_FREQ_PRE_DIV_MASK, chan->pre_div_sel) |
	      FIELD_PREP(PWM_FREQ_EXP_MASK, chan->pre_div_exp);
	regmap_write(lpg->map, chan->base + LPG_PREDIV_CLK_REG, val);
}

#define LPG_ENABLE_GLITCH_REMOVAL	BIT(5)

static void lpg_enable_glitch(struct lpg_channel *chan)
{
	struct lpg *lpg = chan->lpg;

	regmap_update_bits(lpg->map, chan->base + PWM_TYPE_CONFIG_REG,
			   LPG_ENABLE_GLITCH_REMOVAL, 0);
}

static void lpg_disable_glitch(struct lpg_channel *chan)
{
	struct lpg *lpg = chan->lpg;

	regmap_update_bits(lpg->map, chan->base + PWM_TYPE_CONFIG_REG,
			   LPG_ENABLE_GLITCH_REMOVAL,
			   LPG_ENABLE_GLITCH_REMOVAL);
}

static void lpg_apply_pwm_value(struct lpg_channel *chan)
{
	struct lpg *lpg = chan->lpg;
	u16 val = chan->pwm_value;

	if (!chan->enabled)
		return;

	regmap_bulk_write(lpg->map, chan->base + PWM_VALUE_REG, &val, sizeof(val));
}

#define LPG_PATTERN_CONFIG_LO_TO_HI	BIT(4)
#define LPG_PATTERN_CONFIG_REPEAT	BIT(3)
#define LPG_PATTERN_CONFIG_TOGGLE	BIT(2)
#define LPG_PATTERN_CONFIG_PAUSE_HI	BIT(1)
#define LPG_PATTERN_CONFIG_PAUSE_LO	BIT(0)

static void lpg_sdam_apply_lut_control(struct lpg_channel *chan)
{
	struct nvmem_device *lpg_chan_sdam = chan->lpg->lpg_chan_sdam;
	unsigned int lo_idx = chan->pattern_lo_idx;
	unsigned int hi_idx = chan->pattern_hi_idx;
	u8 val = 0, conf = 0, lut_offset = 0;
	unsigned int hi_pause, lo_pause;
	struct lpg *lpg = chan->lpg;

	if (!chan->ramp_enabled || chan->pattern_lo_idx == chan->pattern_hi_idx)
		return;

	hi_pause = DIV_ROUND_UP(chan->ramp_hi_pause_ms, chan->ramp_tick_ms);
	lo_pause = DIV_ROUND_UP(chan->ramp_lo_pause_ms, chan->ramp_tick_ms);

	if (!chan->ramp_oneshot)
		conf |= LPG_PATTERN_CONFIG_REPEAT;
	if (chan->ramp_hi_pause_ms && lpg->lut_sdam)
		conf |= LPG_PATTERN_CONFIG_PAUSE_HI;
	if (chan->ramp_lo_pause_ms && lpg->lut_sdam)
		conf |= LPG_PATTERN_CONFIG_PAUSE_LO;

	if (lpg->lut_sdam) {
		lut_offset = SDAM_LUT_SDAM_LUT_PATTERN_OFFSET - SDAM_START_BASE;
		hi_idx += lut_offset;
		lo_idx += lut_offset;
	}

	nvmem_device_write(lpg_chan_sdam, SDAM_PBS_SCRATCH_LUT_COUNTER_OFFSET + chan->sdam_offset, 1, &val);
	nvmem_device_write(lpg_chan_sdam, SDAM_PATTERN_CONFIG_OFFSET + chan->sdam_offset, 1, &conf);
	nvmem_device_write(lpg_chan_sdam, SDAM_END_INDEX_OFFSET + chan->sdam_offset, 1, &hi_idx);
	nvmem_device_write(lpg_chan_sdam, SDAM_START_INDEX_OFFSET + chan->sdam_offset, 1, &lo_idx);

	val = RAMP_STEP_DURATION(chan->ramp_tick_ms);
	nvmem_device_write(lpg_chan_sdam, SDAM_REG_RAMP_STEP_DURATION, 1, &val);

	if (lpg->lut_sdam) {
		nvmem_device_write(lpg_chan_sdam, SDAM_PAUSE_HI_MULTIPLIER_OFFSET + chan->sdam_offset, 1, &hi_pause);
		nvmem_device_write(lpg_chan_sdam, SDAM_PAUSE_LO_MULTIPLIER_OFFSET + chan->sdam_offset, 1, &lo_pause);
	}

}

static void lpg_apply_lut_control(struct lpg_channel *chan)
{
	struct lpg *lpg = chan->lpg;
	unsigned int hi_pause;
	unsigned int lo_pause;
	unsigned int conf = 0;
	unsigned int lo_idx = chan->pattern_lo_idx;
	unsigned int hi_idx = chan->pattern_hi_idx;
	u16 step = chan->ramp_tick_ms;

	if (!chan->ramp_enabled || chan->pattern_lo_idx == chan->pattern_hi_idx)
		return;

	hi_pause = DIV_ROUND_UP(chan->ramp_hi_pause_ms, step);
	lo_pause = DIV_ROUND_UP(chan->ramp_lo_pause_ms, step);

	if (!chan->ramp_reverse)
		conf |= LPG_PATTERN_CONFIG_LO_TO_HI;
	if (!chan->ramp_oneshot)
		conf |= LPG_PATTERN_CONFIG_REPEAT;
	if (chan->ramp_ping_pong)
		conf |= LPG_PATTERN_CONFIG_TOGGLE;
	if (chan->ramp_hi_pause_ms)
		conf |= LPG_PATTERN_CONFIG_PAUSE_HI;
	if (chan->ramp_lo_pause_ms)
		conf |= LPG_PATTERN_CONFIG_PAUSE_LO;

	regmap_write(lpg->map, chan->base + LPG_PATTERN_CONFIG_REG, conf);
	regmap_write(lpg->map, chan->base + LPG_HI_IDX_REG, hi_idx);
	regmap_write(lpg->map, chan->base + LPG_LO_IDX_REG, lo_idx);

	regmap_bulk_write(lpg->map, chan->base + LPG_RAMP_DURATION_REG, &step, sizeof(step));
	regmap_write(lpg->map, chan->base + LPG_HI_PAUSE_REG, hi_pause);
	regmap_write(lpg->map, chan->base + LPG_LO_PAUSE_REG, lo_pause);
}

#define LPG_ENABLE_CONTROL_OUTPUT		BIT(7)
#define LPG_ENABLE_CONTROL_BUFFER_TRISTATE	BIT(5)
#define LPG_ENABLE_CONTROL_SRC_PWM		BIT(2)
#define LPG_ENABLE_CONTROL_RAMP_GEN		BIT(1)

static void lpg_apply_control(struct lpg_channel *chan)
{
	unsigned int ctrl;
	struct lpg *lpg = chan->lpg;

	ctrl = LPG_ENABLE_CONTROL_BUFFER_TRISTATE;

	if (chan->enabled)
		ctrl |= LPG_ENABLE_CONTROL_OUTPUT;

	if (chan->pattern_lo_idx != chan->pattern_hi_idx)
		ctrl |= LPG_ENABLE_CONTROL_RAMP_GEN;
	else
		ctrl |= LPG_ENABLE_CONTROL_SRC_PWM;

	regmap_write(lpg->map, chan->base + PWM_ENABLE_CONTROL_REG, ctrl);

	/*
	 * Due to LPG hardware bug, in the PWM mode, having enabled PWM,
	 * We have to write PWM values one more time.
	 */
	if (chan->enabled)
		lpg_apply_pwm_value(chan);
}

#define LPG_SYNC_PWM	BIT(0)

static void lpg_apply_sync(struct lpg_channel *chan)
{
	struct lpg *lpg = chan->lpg;

	regmap_write(lpg->map, chan->base + PWM_SYNC_REG, LPG_SYNC_PWM);
}

static int lpg_parse_dtest(struct lpg *lpg)
{
	struct lpg_channel *chan;
	struct device_node *np = lpg->dev->of_node;
	int count;
	int ret;
	int i;

	count = of_property_count_u32_elems(np, "qcom,dtest");
	if (count == -EINVAL) {
		return 0;
	} else if (count < 0) {
		ret = count;
		goto err_malformed;
	} else if (count != lpg->data->num_channels * 2) {
		return dev_err_probe(lpg->dev, -EINVAL,
				     "qcom,dtest needs to be %d items\n",
				     lpg->data->num_channels * 2);
	}

	for (i = 0; i < lpg->data->num_channels; i++) {
		chan = &lpg->channels[i];

		ret = of_property_read_u32_index(np, "qcom,dtest", i * 2,
						 &chan->dtest_line);
		if (ret)
			goto err_malformed;

		ret = of_property_read_u32_index(np, "qcom,dtest", i * 2 + 1,
						 &chan->dtest_value);
		if (ret)
			goto err_malformed;
	}

	return 0;

err_malformed:
	return dev_err_probe(lpg->dev, ret, "malformed qcom,dtest\n");
}

static void lpg_apply_dtest(struct lpg_channel *chan)
{
	struct lpg *lpg = chan->lpg;

	if (!chan->dtest_line)
		return;

	regmap_write(lpg->map, chan->base + PWM_SEC_ACCESS_REG, 0xa5);
	regmap_write(lpg->map, chan->base + PWM_DTEST_REG(chan->dtest_line),
		     chan->dtest_value);
}

static void lpg_apply(struct lpg_channel *chan)
{
	lpg_disable_glitch(chan);
	lpg_apply_freq(chan);
	lpg_apply_pwm_value(chan);
	lpg_apply_control(chan);
	lpg_apply_sync(chan);
	if (chan->lpg->lpg_chan_sdam)
		lpg_sdam_apply_lut_control(chan);
	else
		lpg_apply_lut_control(chan);
	lpg_enable_glitch(chan);
}

static void lpg_brightness_set(struct lpg_led *led, struct led_classdev *cdev,
			       struct mc_subled *subleds)
{
	enum led_brightness brightness;
	struct lpg_channel *chan;
	unsigned int triled_enabled = 0;
	unsigned int triled_mask = 0;
	unsigned int lut_mask = 0;
	unsigned int duty;
	struct lpg *lpg = led->lpg;
	int i;

	for (i = 0; i < led->num_channels; i++) {
		chan = led->channels[i];
		brightness = subleds[i].brightness;

		if (brightness == LED_OFF) {
			chan->enabled = false;
			chan->ramp_enabled = false;
		} else if (chan->pattern_lo_idx != chan->pattern_hi_idx) {
			lpg_calc_freq(chan, NSEC_PER_MSEC);
			lpg_sdam_configure_triggers(chan, 1);

			chan->enabled = true;
			chan->ramp_enabled = true;

			lut_mask |= chan->lut_mask;
			triled_enabled |= chan->triled_mask;
		} else {
			lpg_calc_freq(chan, NSEC_PER_MSEC);

			duty = div_u64(brightness * chan->period, cdev->max_brightness);
			lpg_calc_duty(chan, duty);
			chan->enabled = true;
			chan->ramp_enabled = false;

			triled_enabled |= chan->triled_mask;
		}

		triled_mask |= chan->triled_mask;

		lpg_apply(chan);
	}

	/* Toggle triled lines */
	if (triled_mask)
		triled_set(lpg, triled_mask, triled_enabled);

	/* Trigger start of ramp generator(s) */
	if (lut_mask) {
		lpg_lut_sync(lpg, lut_mask);
		lpg_set_pbs_trigger(lpg, lut_mask);
	}
}

static int lpg_brightness_single_set(struct led_classdev *cdev,
				     enum led_brightness value)
{
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);
	struct mc_subled info;

	mutex_lock(&led->lpg->lock);

	info.brightness = value;
	lpg_brightness_set(led, cdev, &info);

	mutex_unlock(&led->lpg->lock);

	return 0;
}

static int lpg_brightness_mc_set(struct led_classdev *cdev,
				 enum led_brightness value)
{
	struct led_classdev_mc *mc = lcdev_to_mccdev(cdev);
	struct lpg_led *led = container_of(mc, struct lpg_led, mcdev);

	mutex_lock(&led->lpg->lock);

	led_mc_calc_color_components(mc, value);
	lpg_brightness_set(led, cdev, mc->subled_info);

	mutex_unlock(&led->lpg->lock);

	return 0;
}

static int lpg_blink_set(struct lpg_led *led,
			 unsigned long *delay_on, unsigned long *delay_off)
{
	struct lpg_channel *chan;
	unsigned int period;
	unsigned int triled_mask = 0;
	struct lpg *lpg = led->lpg;
	u64 duty;
	int i;

	if (!*delay_on && !*delay_off) {
		*delay_on = 500;
		*delay_off = 500;
	}

	duty = *delay_on * NSEC_PER_MSEC;
	period = (*delay_on + *delay_off) * NSEC_PER_MSEC;

	for (i = 0; i < led->num_channels; i++) {
		chan = led->channels[i];

		lpg_calc_freq(chan, period);
		lpg_calc_duty(chan, duty);

		chan->enabled = true;
		chan->ramp_enabled = false;

		triled_mask |= chan->triled_mask;

		lpg_apply(chan);
	}

	/* Enable triled lines */
	triled_set(lpg, triled_mask, triled_mask);

	chan = led->channels[0];
	duty = div_u64(chan->pwm_value * chan->period, LPG_RESOLUTION_9BIT);
	*delay_on = div_u64(duty, NSEC_PER_MSEC);
	*delay_off = div_u64(chan->period - duty, NSEC_PER_MSEC);

	return 0;
}

static int lpg_blink_single_set(struct led_classdev *cdev,
				unsigned long *delay_on, unsigned long *delay_off)
{
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);
	int ret;

	mutex_lock(&led->lpg->lock);

	ret = lpg_blink_set(led, delay_on, delay_off);

	mutex_unlock(&led->lpg->lock);

	return ret;
}

static int lpg_blink_mc_set(struct led_classdev *cdev,
			    unsigned long *delay_on, unsigned long *delay_off)
{
	struct led_classdev_mc *mc = lcdev_to_mccdev(cdev);
	struct lpg_led *led = container_of(mc, struct lpg_led, mcdev);
	int ret;

	mutex_lock(&led->lpg->lock);

	ret = lpg_blink_set(led, delay_on, delay_off);

	mutex_unlock(&led->lpg->lock);

	return ret;
}

static int lpg_pattern_set(struct lpg_led *led, struct led_pattern *led_pattern,
			   u32 len, int repeat)
{
	struct lpg_channel *chan;
	struct lpg *lpg = led->lpg;
	struct led_pattern *pattern;
	unsigned int brightness_a;
	unsigned int brightness_b;
	unsigned int hi_pause = 0;
	unsigned int lo_pause = 0;
	unsigned int actual_len;
	unsigned int delta_t;
	unsigned int lo_idx;
	unsigned int hi_idx;
	unsigned int i;
	bool ping_pong = true;
	int ret = -EINVAL;

	/* Hardware only support oneshot or indefinite loops */
	if (repeat != -1 && repeat != 1)
		return -EINVAL;

	/*
	 * The standardized leds-trigger-pattern format defines that the
	 * brightness of the LED follows a linear transition from one entry
	 * in the pattern to the next, over the given delta_t time. It
	 * describes that the way to perform instant transitions a zero-length
	 * entry should be added following a pattern entry.
	 *
	 * The LPG hardware is only able to perform the latter (no linear
	 * transitions), so require each entry in the pattern to be followed by
	 * a zero-length transition.
	 */
	if (len % 2)
		return -EINVAL;

	pattern = kcalloc(len / 2, sizeof(*pattern), GFP_KERNEL);
	if (!pattern)
		return -ENOMEM;

	for (i = 0; i < len; i += 2) {
		if (led_pattern[i].brightness != led_pattern[i + 1].brightness)
			goto out_free_pattern;
		if (led_pattern[i + 1].delta_t != 0)
			goto out_free_pattern;

		pattern[i / 2].brightness = led_pattern[i].brightness;
		pattern[i / 2].delta_t = led_pattern[i].delta_t;
	}

	len /= 2;

	/*
	 * Specifying a pattern of length 1 causes the hardware to iterate
	 * through the entire LUT, so prohibit this.
	 */
	if (len < 2)
		goto out_free_pattern;

	/*
	 * The LPG plays patterns with at a fixed pace, a "low pause" can be
	 * used to stretch the first delay of the pattern and a "high pause"
	 * the last one.
	 *
	 * In order to save space the pattern can be played in "ping pong"
	 * mode, in which the pattern is first played forward, then "high
	 * pause" is applied, then the pattern is played backwards and finally
	 * the "low pause" is applied.
	 *
	 * The middle elements of the pattern are used to determine delta_t and
	 * the "low pause" and "high pause" multipliers are derrived from this.
	 *
	 * The first element in the pattern is used to determine "low pause".
	 *
	 * If the specified pattern is a palindrome the ping pong mode is
	 * enabled. In this scenario the delta_t of the middle entry (i.e. the
	 * last in the programmed pattern) determines the "high pause".
	 *
	 * SDAM-based devices do not support "ping pong", and only supports
	 * "low pause" and "high pause" with a dedicated SDAM LUT.
	 */

	/* Detect palindromes and use "ping pong" to reduce LUT usage */
	if (lpg->lut_base) {
		for (i = 0; i < len / 2; i++) {
			brightness_a = pattern[i].brightness;
			brightness_b = pattern[len - i - 1].brightness;

			if (brightness_a != brightness_b) {
				ping_pong = false;
				break;
			}
		}
	} else
		ping_pong = false;

	/* The pattern length to be written to the LUT */
	if (ping_pong)
		actual_len = (len + 1) / 2;
	else
		actual_len = len;

	/*
	 * Validate that all delta_t in the pattern are the same, with the
	 * exception of the middle element in case of ping_pong.
	 */
	delta_t = pattern[1].delta_t;
	for (i = 2; i < len; i++) {
		if (pattern[i].delta_t != delta_t) {
			/*
			 * Allow last entry in the full or shortened pattern to
			 * specify hi pause. Reject other variations.
			 */
			if (i != actual_len - 1)
				goto out_free_pattern;
		}
	}

	/* LPG_RAMP_DURATION_REG is a 9bit */
	if (delta_t >= BIT(9))
		goto out_free_pattern;

	/*
	 * Find "low pause" and "high pause" in the pattern in the LUT case.
	 * SDAM-based devices without dedicated LUT SDAM require equal
	 * duration of all steps.
	 */
	if (lpg->lut_base || lpg->lut_sdam) {
		lo_pause = pattern[0].delta_t;
		hi_pause = pattern[actual_len - 1].delta_t;
	} else {
		if (delta_t != pattern[0].delta_t || delta_t != pattern[actual_len - 1].delta_t)
			goto out_free_pattern;
	}


	mutex_lock(&lpg->lock);

	if (lpg->lut_base)
		ret = lpg_lut_store(lpg, pattern, actual_len, &lo_idx, &hi_idx);
	else
		ret = lpg_lut_store_sdam(lpg, pattern, actual_len, &lo_idx, &hi_idx);

	if (ret < 0)
		goto out_unlock;

	for (i = 0; i < led->num_channels; i++) {
		chan = led->channels[i];

		chan->ramp_tick_ms = delta_t;
		chan->ramp_ping_pong = ping_pong;
		chan->ramp_oneshot = repeat != -1;

		chan->ramp_lo_pause_ms = lo_pause;
		chan->ramp_hi_pause_ms = hi_pause;

		chan->pattern_lo_idx = lo_idx;
		chan->pattern_hi_idx = hi_idx;
	}

out_unlock:
	mutex_unlock(&lpg->lock);
out_free_pattern:
	kfree(pattern);

	return ret;
}

static int lpg_pattern_single_set(struct led_classdev *cdev,
				  struct led_pattern *pattern, u32 len,
				  int repeat)
{
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);
	int ret;

	ret = lpg_pattern_set(led, pattern, len, repeat);
	if (ret < 0)
		return ret;

	lpg_brightness_single_set(cdev, LED_FULL);

	return 0;
}

static int lpg_pattern_mc_set(struct led_classdev *cdev,
			      struct led_pattern *pattern, u32 len,
			      int repeat)
{
	struct led_classdev_mc *mc = lcdev_to_mccdev(cdev);
	struct lpg_led *led = container_of(mc, struct lpg_led, mcdev);
	unsigned int triled_mask = 0;
	int ret, i;

	for (i = 0; i < led->num_channels; i++)
		triled_mask |= led->channels[i]->triled_mask;
	triled_set(led->lpg, triled_mask, 0);

	ret = lpg_pattern_set(led, pattern, len, repeat);
	if (ret < 0)
		return ret;

	led_mc_calc_color_components(mc, LED_FULL);
	lpg_brightness_set(led, cdev, mc->subled_info);

	return 0;
}

static int lpg_pattern_clear(struct lpg_led *led)
{
	struct lpg_channel *chan;
	struct lpg *lpg = led->lpg;
	int i;

	mutex_lock(&lpg->lock);

	chan = led->channels[0];
	lpg_lut_free(lpg, chan->pattern_lo_idx, chan->pattern_hi_idx);

	for (i = 0; i < led->num_channels; i++) {
		chan = led->channels[i];
		lpg_sdam_configure_triggers(chan, 0);
		lpg_clear_pbs_trigger(chan->lpg, chan->lut_mask);
		chan->pattern_lo_idx = 0;
		chan->pattern_hi_idx = 0;
	}

	mutex_unlock(&lpg->lock);

	return 0;
}

static int lpg_pattern_single_clear(struct led_classdev *cdev)
{
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);

	return lpg_pattern_clear(led);
}

static int lpg_pattern_mc_clear(struct led_classdev *cdev)
{
	struct led_classdev_mc *mc = lcdev_to_mccdev(cdev);
	struct lpg_led *led = container_of(mc, struct lpg_led, mcdev);

	return lpg_pattern_clear(led);
}

static inline struct lpg *lpg_pwm_from_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int lpg_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct lpg *lpg = lpg_pwm_from_chip(chip);
	struct lpg_channel *chan = &lpg->channels[pwm->hwpwm];

	return chan->in_use ? -EBUSY : 0;
}

/*
 * Limitations:
 * - Updating both duty and period is not done atomically, so the output signal
 *   will momentarily be a mix of the settings.
 * - Changed parameters takes effect immediately.
 * - A disabled channel outputs a logical 0.
 */
static int lpg_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct lpg *lpg = lpg_pwm_from_chip(chip);
	struct lpg_channel *chan = &lpg->channels[pwm->hwpwm];
	int ret = 0;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	mutex_lock(&lpg->lock);

	if (state->enabled) {
		ret = lpg_calc_freq(chan, state->period);
		if (ret < 0)
			goto out_unlock;

		lpg_calc_duty(chan, state->duty_cycle);
	}
	chan->enabled = state->enabled;

	lpg_apply(chan);

	triled_set(lpg, chan->triled_mask, chan->enabled ? chan->triled_mask : 0);

out_unlock:
	mutex_unlock(&lpg->lock);

	return ret;
}

static int lpg_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct lpg *lpg = lpg_pwm_from_chip(chip);
	struct lpg_channel *chan = &lpg->channels[pwm->hwpwm];
	unsigned int resolution;
	unsigned int pre_div;
	unsigned int refclk;
	unsigned int val;
	unsigned int m;
	u16 pwm_value;
	int ret;

	ret = regmap_read(lpg->map, chan->base + LPG_SIZE_CLK_REG, &val);
	if (ret)
		return ret;

	if (chan->subtype == LPG_SUBTYPE_HI_RES_PWM) {
		refclk = lpg_clk_rates_hi_res[FIELD_GET(PWM_CLK_SELECT_HI_RES_MASK, val)];
		resolution = lpg_pwm_resolution_hi_res[FIELD_GET(PWM_SIZE_HI_RES_MASK, val)];
	} else {
		refclk = lpg_clk_rates[FIELD_GET(PWM_CLK_SELECT_MASK, val)];
		resolution = 9;
	}

	if (refclk) {
		ret = regmap_read(lpg->map, chan->base + LPG_PREDIV_CLK_REG, &val);
		if (ret)
			return ret;

		pre_div = lpg_pre_divs[FIELD_GET(PWM_FREQ_PRE_DIV_MASK, val)];
		m = FIELD_GET(PWM_FREQ_EXP_MASK, val);

		ret = regmap_bulk_read(lpg->map, chan->base + PWM_VALUE_REG, &pwm_value, sizeof(pwm_value));
		if (ret)
			return ret;

		state->period = DIV_ROUND_UP_ULL((u64)NSEC_PER_SEC * (1 << resolution) *
						 pre_div * (1 << m), refclk);
		state->duty_cycle = DIV_ROUND_UP_ULL((u64)NSEC_PER_SEC * pwm_value * pre_div * (1 << m), refclk);
	} else {
		state->period = 0;
		state->duty_cycle = 0;
	}

	ret = regmap_read(lpg->map, chan->base + PWM_ENABLE_CONTROL_REG, &val);
	if (ret)
		return ret;

	state->enabled = FIELD_GET(LPG_ENABLE_CONTROL_OUTPUT, val);
	state->polarity = PWM_POLARITY_NORMAL;

	if (state->duty_cycle > state->period)
		state->duty_cycle = state->period;

	return 0;
}

static const struct pwm_ops lpg_pwm_ops = {
	.request = lpg_pwm_request,
	.apply = lpg_pwm_apply,
	.get_state = lpg_pwm_get_state,
};

static int lpg_add_pwm(struct lpg *lpg)
{
	struct pwm_chip *chip;
	int ret;

	lpg->pwm = chip = devm_pwmchip_alloc(lpg->dev, lpg->num_channels, 0);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	chip->ops = &lpg_pwm_ops;
	pwmchip_set_drvdata(chip, lpg);

	ret = devm_pwmchip_add(lpg->dev, chip);
	if (ret)
		dev_err_probe(lpg->dev, ret, "failed to add PWM chip\n");

	return ret;
}

static int lpg_parse_channel(struct lpg *lpg, struct device_node *np,
			     struct lpg_channel **channel)
{
	struct lpg_channel *chan;
	u32 color = LED_COLOR_ID_GREEN;
	u32 reg;
	int ret;

	ret = of_property_read_u32(np, "reg", &reg);
	if (ret || !reg || reg > lpg->num_channels)
		return dev_err_probe(lpg->dev, -EINVAL, "invalid \"reg\" of %pOFn\n", np);

	chan = &lpg->channels[reg - 1];
	chan->in_use = true;

	ret = of_property_read_u32(np, "color", &color);
	if (ret < 0 && ret != -EINVAL)
		return dev_err_probe(lpg->dev, ret,
				     "failed to parse \"color\" of %pOF\n", np);

	chan->color = color;

	*channel = chan;

	return 0;
}

static int lpg_add_led(struct lpg *lpg, struct device_node *np)
{
	struct led_init_data init_data = {};
	struct led_classdev *cdev;
	struct device_node *child;
	struct mc_subled *info;
	struct lpg_led *led;
	const char *state;
	int num_channels;
	u32 color = 0;
	int ret;
	int i;

	ret = of_property_read_u32(np, "color", &color);
	if (ret < 0 && ret != -EINVAL)
		return dev_err_probe(lpg->dev, ret,
			      "failed to parse \"color\" of %pOF\n", np);

	if (color == LED_COLOR_ID_RGB)
		num_channels = of_get_available_child_count(np);
	else
		num_channels = 1;

	led = devm_kzalloc(lpg->dev, struct_size(led, channels, num_channels), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->lpg = lpg;
	led->num_channels = num_channels;

	if (color == LED_COLOR_ID_RGB) {
		info = devm_kcalloc(lpg->dev, num_channels, sizeof(*info), GFP_KERNEL);
		if (!info)
			return -ENOMEM;
		i = 0;
		for_each_available_child_of_node(np, child) {
			ret = lpg_parse_channel(lpg, child, &led->channels[i]);
			if (ret < 0) {
				of_node_put(child);
				return ret;
			}

			info[i].color_index = led->channels[i]->color;
			info[i].intensity = 0;
			i++;
		}

		led->mcdev.subled_info = info;
		led->mcdev.num_colors = num_channels;

		cdev = &led->mcdev.led_cdev;
		cdev->brightness_set_blocking = lpg_brightness_mc_set;
		cdev->blink_set = lpg_blink_mc_set;

		/* Register pattern accessors if we have a LUT block or when using PPG */
		if (lpg->lut_base || lpg->lpg_chan_sdam) {
			cdev->pattern_set = lpg_pattern_mc_set;
			cdev->pattern_clear = lpg_pattern_mc_clear;
		}
	} else {
		ret = lpg_parse_channel(lpg, np, &led->channels[0]);
		if (ret < 0)
			return ret;

		cdev = &led->cdev;
		cdev->brightness_set_blocking = lpg_brightness_single_set;
		cdev->blink_set = lpg_blink_single_set;

		/* Register pattern accessors if we have a LUT block or when using PPG */
		if (lpg->lut_base || lpg->lpg_chan_sdam) {
			cdev->pattern_set = lpg_pattern_single_set;
			cdev->pattern_clear = lpg_pattern_single_clear;
		}
	}

	cdev->default_trigger = of_get_property(np, "linux,default-trigger", NULL);

	if (lpg->lpg_chan_sdam)
		cdev->max_brightness = PPG_MAX_LED_BRIGHTNESS;
	else
		cdev->max_brightness = LPG_RESOLUTION_9BIT - 1;

	if (!of_property_read_string(np, "default-state", &state) &&
	    !strcmp(state, "on"))
		cdev->brightness = cdev->max_brightness;
	else
		cdev->brightness = LED_OFF;

	cdev->brightness_set_blocking(cdev, cdev->brightness);

	init_data.fwnode = of_fwnode_handle(np);

	if (color == LED_COLOR_ID_RGB)
		ret = devm_led_classdev_multicolor_register_ext(lpg->dev, &led->mcdev, &init_data);
	else
		ret = devm_led_classdev_register_ext(lpg->dev, &led->cdev, &init_data);
	if (ret)
		dev_err_probe(lpg->dev, ret, "unable to register %s\n", cdev->name);

	return ret;
}

static int lpg_init_channels(struct lpg *lpg)
{
	const struct lpg_data *data = lpg->data;
	struct lpg_channel *chan;
	int i;

	lpg->num_channels = data->num_channels;
	lpg->channels = devm_kcalloc(lpg->dev, data->num_channels,
				     sizeof(struct lpg_channel), GFP_KERNEL);
	if (!lpg->channels)
		return -ENOMEM;

	for (i = 0; i < data->num_channels; i++) {
		chan = &lpg->channels[i];

		chan->lpg = lpg;
		chan->base = data->channels[i].base;
		chan->triled_mask = data->channels[i].triled_mask;
		chan->lut_mask = BIT(i);
		chan->sdam_offset = data->channels[i].sdam_offset;

		regmap_read(lpg->map, chan->base + LPG_SUBTYPE_REG, &chan->subtype);
	}

	return 0;
}

static int lpg_init_triled(struct lpg *lpg)
{
	struct device_node *np = lpg->dev->of_node;
	int ret;

	/* Skip initialization if we don't have a triled block */
	if (!lpg->data->triled_base)
		return 0;

	lpg->triled_base = lpg->data->triled_base;
	lpg->triled_has_atc_ctl = lpg->data->triled_has_atc_ctl;
	lpg->triled_has_src_sel = lpg->data->triled_has_src_sel;

	if (lpg->triled_has_src_sel) {
		ret = of_property_read_u32(np, "qcom,power-source", &lpg->triled_src);
		if (ret || lpg->triled_src == 2 || lpg->triled_src > 3)
			return dev_err_probe(lpg->dev, -EINVAL,
					     "invalid power source\n");
	}

	/* Disable automatic trickle charge LED */
	if (lpg->triled_has_atc_ctl)
		regmap_write(lpg->map, lpg->triled_base + TRI_LED_ATC_CTL, 0);

	/* Configure power source */
	if (lpg->triled_has_src_sel)
		regmap_write(lpg->map, lpg->triled_base + TRI_LED_SRC_SEL, lpg->triled_src);

	/* Default all outputs to off */
	regmap_write(lpg->map, lpg->triled_base + TRI_LED_EN_CTL, 0);

	return 0;
}

static int lpg_init_lut(struct lpg *lpg)
{
	const struct lpg_data *data = lpg->data;

	if (!data->lut_size)
		return 0;

	lpg->lut_size = data->lut_size;
	if (data->lut_base)
		lpg->lut_base = data->lut_base;

	lpg->lut_bitmap = devm_bitmap_zalloc(lpg->dev, lpg->lut_size, GFP_KERNEL);
	if (!lpg->lut_bitmap)
		return -ENOMEM;

	return 0;
}

static int lpg_init_sdam(struct lpg *lpg)
{
	int i, sdam_count, rc;
	u8 val = 0;

	sdam_count = of_property_count_strings(lpg->dev->of_node, "nvmem-names");
	if (sdam_count <= 0)
		return 0;
	if (sdam_count > SDAM_MAX_DEVICES)
		return -EINVAL;

	/* Get the 1st SDAM device for LPG/LUT config */
	lpg->lpg_chan_sdam = devm_nvmem_device_get(lpg->dev, "lpg_chan_sdam");
	if (IS_ERR(lpg->lpg_chan_sdam))
		return dev_err_probe(lpg->dev, PTR_ERR(lpg->lpg_chan_sdam),
				"Failed to get LPG chan SDAM device\n");

	if (sdam_count == 1) {
		/* Get PBS device node if single SDAM device */
		lpg->pbs_dev = get_pbs_client_device(lpg->dev);
		if (IS_ERR(lpg->pbs_dev))
			return dev_err_probe(lpg->dev, PTR_ERR(lpg->pbs_dev),
					"Failed to get PBS client device\n");
	} else if (sdam_count == 2) {
		/* Get the 2nd SDAM device for LUT pattern */
		lpg->lut_sdam = devm_nvmem_device_get(lpg->dev, "lut_sdam");
		if (IS_ERR(lpg->lut_sdam))
			return dev_err_probe(lpg->dev, PTR_ERR(lpg->lut_sdam),
					"Failed to get LPG LUT SDAM device\n");
	}

	for (i = 0; i < lpg->num_channels; i++) {
		struct lpg_channel *chan = &lpg->channels[i];

		if (chan->sdam_offset) {
			rc = nvmem_device_write(lpg->lpg_chan_sdam,
				SDAM_PBS_SCRATCH_LUT_COUNTER_OFFSET + chan->sdam_offset, 1, &val);
			if (rc < 0)
				return rc;

			rc = lpg_sdam_configure_triggers(chan, 0);
			if (rc < 0)
				return rc;

			rc = lpg_clear_pbs_trigger(chan->lpg, chan->lut_mask);
			if (rc < 0)
				return rc;
		}
	}

	return 0;
}

static int lpg_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct lpg *lpg;
	int ret;
	int i;

	lpg = devm_kzalloc(&pdev->dev, sizeof(*lpg), GFP_KERNEL);
	if (!lpg)
		return -ENOMEM;

	lpg->data = of_device_get_match_data(&pdev->dev);
	if (!lpg->data)
		return -EINVAL;

	lpg->dev = &pdev->dev;
	mutex_init(&lpg->lock);

	lpg->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!lpg->map)
		return dev_err_probe(&pdev->dev, -ENXIO, "parent regmap unavailable\n");

	ret = lpg_init_channels(lpg);
	if (ret < 0)
		return ret;

	ret = lpg_parse_dtest(lpg);
	if (ret < 0)
		return ret;

	ret = lpg_init_triled(lpg);
	if (ret < 0)
		return ret;

	ret = lpg_init_sdam(lpg);
	if (ret < 0)
		return ret;

	ret = lpg_init_lut(lpg);
	if (ret < 0)
		return ret;

	for_each_available_child_of_node(pdev->dev.of_node, np) {
		ret = lpg_add_led(lpg, np);
		if (ret) {
			of_node_put(np);
			return ret;
		}
	}

	for (i = 0; i < lpg->num_channels; i++)
		lpg_apply_dtest(&lpg->channels[i]);

	return lpg_add_pwm(lpg);
}

static const struct lpg_data pm660l_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 49,

	.triled_base = 0xd000,
	.triled_has_atc_ctl = true,
	.triled_has_src_sel = true,

	.num_channels = 4,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xb100, .triled_mask = BIT(5) },
		{ .base = 0xb200, .triled_mask = BIT(6) },
		{ .base = 0xb300, .triled_mask = BIT(7) },
		{ .base = 0xb400 },
	},
};

static const struct lpg_data pm8916_pwm_data = {
	.num_channels = 1,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xbc00 },
	},
};

static const struct lpg_data pm8941_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 64,

	.triled_base = 0xd000,
	.triled_has_atc_ctl = true,
	.triled_has_src_sel = true,

	.num_channels = 8,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xb100 },
		{ .base = 0xb200 },
		{ .base = 0xb300 },
		{ .base = 0xb400 },
		{ .base = 0xb500, .triled_mask = BIT(5) },
		{ .base = 0xb600, .triled_mask = BIT(6) },
		{ .base = 0xb700, .triled_mask = BIT(7) },
		{ .base = 0xb800 },
	},
};

static const struct lpg_data pm8994_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 64,

	.num_channels = 6,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xb100 },
		{ .base = 0xb200 },
		{ .base = 0xb300 },
		{ .base = 0xb400 },
		{ .base = 0xb500 },
		{ .base = 0xb600 },
	},
};

/* PMI632 uses SDAM instead of LUT for pattern */
static const struct lpg_data pmi632_lpg_data = {
	.triled_base = 0xd000,

	.lut_size = 64,

	.num_channels = 5,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xb300, .triled_mask = BIT(7), .sdam_offset = 0x48 },
		{ .base = 0xb400, .triled_mask = BIT(6), .sdam_offset = 0x56 },
		{ .base = 0xb500, .triled_mask = BIT(5), .sdam_offset = 0x64 },
		{ .base = 0xb600 },
		{ .base = 0xb700 },
	},
};

static const struct lpg_data pmi8994_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 24,

	.triled_base = 0xd000,
	.triled_has_atc_ctl = true,
	.triled_has_src_sel = true,

	.num_channels = 4,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xb100, .triled_mask = BIT(5) },
		{ .base = 0xb200, .triled_mask = BIT(6) },
		{ .base = 0xb300, .triled_mask = BIT(7) },
		{ .base = 0xb400 },
	},
};

static const struct lpg_data pmi8998_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 49,

	.triled_base = 0xd000,

	.num_channels = 6,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xb100 },
		{ .base = 0xb200 },
		{ .base = 0xb300, .triled_mask = BIT(5) },
		{ .base = 0xb400, .triled_mask = BIT(6) },
		{ .base = 0xb500, .triled_mask = BIT(7) },
		{ .base = 0xb600 },
	},
};

static const struct lpg_data pm8150b_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 24,

	.triled_base = 0xd000,

	.num_channels = 2,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xb100, .triled_mask = BIT(7) },
		{ .base = 0xb200, .triled_mask = BIT(6) },
	},
};

static const struct lpg_data pm8150l_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 48,

	.triled_base = 0xd000,

	.num_channels = 5,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xb100, .triled_mask = BIT(7) },
		{ .base = 0xb200, .triled_mask = BIT(6) },
		{ .base = 0xb300, .triled_mask = BIT(5) },
		{ .base = 0xbc00 },
		{ .base = 0xbd00 },

	},
};

static const struct lpg_data pm8350c_pwm_data = {
	.triled_base = 0xef00,

	.lut_size = 122,

	.num_channels = 4,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xe800, .triled_mask = BIT(7), .sdam_offset = 0x48 },
		{ .base = 0xe900, .triled_mask = BIT(6), .sdam_offset = 0x56 },
		{ .base = 0xea00, .triled_mask = BIT(5), .sdam_offset = 0x64 },
		{ .base = 0xeb00 },
	},
};

static const struct lpg_data pmk8550_pwm_data = {
	.num_channels = 2,
	.channels = (const struct lpg_channel_data[]) {
		{ .base = 0xe800 },
		{ .base = 0xe900 },
	},
};

static const struct of_device_id lpg_of_table[] = {
	{ .compatible = "qcom,pm660l-lpg", .data = &pm660l_lpg_data },
	{ .compatible = "qcom,pm8150b-lpg", .data = &pm8150b_lpg_data },
	{ .compatible = "qcom,pm8150l-lpg", .data = &pm8150l_lpg_data },
	{ .compatible = "qcom,pm8350c-pwm", .data = &pm8350c_pwm_data },
	{ .compatible = "qcom,pm8916-pwm", .data = &pm8916_pwm_data },
	{ .compatible = "qcom,pm8941-lpg", .data = &pm8941_lpg_data },
	{ .compatible = "qcom,pm8994-lpg", .data = &pm8994_lpg_data },
	{ .compatible = "qcom,pmi632-lpg", .data = &pmi632_lpg_data },
	{ .compatible = "qcom,pmi8994-lpg", .data = &pmi8994_lpg_data },
	{ .compatible = "qcom,pmi8998-lpg", .data = &pmi8998_lpg_data },
	{ .compatible = "qcom,pmc8180c-lpg", .data = &pm8150l_lpg_data },
	{ .compatible = "qcom,pmk8550-pwm", .data = &pmk8550_pwm_data },
	{}
};
MODULE_DEVICE_TABLE(of, lpg_of_table);

static struct platform_driver lpg_driver = {
	.probe = lpg_probe,
	.driver = {
		.name = "qcom-spmi-lpg",
		.of_match_table = lpg_of_table,
	},
};
module_platform_driver(lpg_driver);

MODULE_DESCRIPTION("Qualcomm LPG LED driver");
MODULE_LICENSE("GPL v2");
