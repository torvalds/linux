/*
 * Copyright (c) 2017 Linaro Ltd
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
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
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define LPG_PATTERN_CONFIG_REG	0x40
#define LPG_SIZE_CLK_REG	0x41
#define LPG_PREDIV_CLK_REG	0x42
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

#define TRI_LED_SRC_SEL		0x45
#define TRI_LED_EN_CTL		0x46
#define TRI_LED_ATC_CTL		0x47

#define LPG_LUT_REG(x)		(0x40 + (x) * 2)
#define RAMP_CONTROL_REG	0xc8

struct lpg_channel;
struct lpg_data;

/**
 * struct lpg - LPG device context
 * @dev:	struct device for LPG device
 * @map:	regmap for register access
 * @pwm:	PWM-chip object, if operating in PWM mode
 * @pwm_9bit_mask: bitmask for enabling 9bit pwm
 * @lut_base:	base address of the LUT block (optional)
 * @lut_size:	number of entries in the LUT block
 * @lut_bitmap:	allocation bitmap for LUT entries
 * @triled_base: base address of the TRILED block (optional)
 * @triled_src:	power-source for the TRILED
 * @channels:	list of PWM channels
 * @num_channels: number of @channels
 */
struct lpg {
	struct device *dev;
	struct regmap *map;

	struct pwm_chip pwm;

	const struct lpg_data *data;

	u32 lut_base;
	u32 lut_size;
	unsigned long *lut_bitmap;

	u32 triled_base;
	u32 triled_src;

	struct lpg_channel *channels;
	unsigned int num_channels;
};

/**
 * struct lpg_channel - per channel data
 * @lpg:	reference to parent lpg
 * @base:	base address of the PWM channel
 * @triled_mask: mask in TRILED to enable this channel
 * @lut_mask:	mask in LUT to start pattern generator for this channel
 * @in_use:	channel is exposed to LED framework
 * @dtest_line:	DTEST line for output, or 0 if disabled
 * @dtest_value: DTEST line configuration
 * @pwm_value:	duty (in microseconds) of the generated pulses, overriden by LUT
 * @enabled:	output enabled?
 * @period_us:	period (in microseconds) of the generated pulses
 * @pwm_size:	resolution of the @pwm_value, 6 or 9 bits
 * @clk:	base frequency of the clock generator
 * @pre_div:	divider of @clk
 * @pre_div_exp: exponential divider of @clk
 * @ramp_enabled: duty cycle is driven by iterating over lookup table
 * @ramp_ping_pong: reverse through pattern, rather than wrapping to start
 * @ramp_oneshot: perform only a single pass over the pattern
 * @ramp_reverse: iterate over pattern backwards
 * @ramp_duration_ms: length (in milliseconds) of one pattern run
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

	bool in_use;

	u32 dtest_line;
	u32 dtest_value;

	u16 pwm_value;
	bool enabled;

	unsigned int period_us;
	unsigned int pwm_size;
	unsigned int clk;
	unsigned int pre_div;
	unsigned int pre_div_exp;

	bool ramp_enabled;
	bool ramp_ping_pong;
	bool ramp_oneshot;
	bool ramp_reverse;
	unsigned long ramp_duration_ms;
	unsigned long ramp_lo_pause_ms;
	unsigned long ramp_hi_pause_ms;

	unsigned int pattern_lo_idx;
	unsigned int pattern_hi_idx;
};

/**
 * struct lpg_led - logical LED object
 * @lpg:		lpg context reference
 * @cdev:		LED class device
 * @num_channels:	number of @channels
 * @channels:		list of channels associated with the LED
 */
struct lpg_led {
	struct lpg *lpg;

	struct led_classdev cdev;

	unsigned int num_channels;
	struct lpg_channel *channels[];
};

/**
 * struct lpg_channel_data - per channel initialization data
 * @base:		base address for PWM channel registers
 * @triled_mask:	bitmask for controlling this channel in TRILED
 */
struct lpg_channel_data {
	unsigned int base;
	u8 triled_mask;
};

/**
 * struct lpg_data - initialization data
 * @lut_base:		base address of LUT block
 * @lut_size:		number of entries in LUT
 * @triled_base:	base address of TRILED
 * @pwm_9bit_mask:	bitmask for switching from 6bit to 9bit pwm
 * @num_channels:	number of channels in LPG
 * @channels:		list of channel initialization data
 */
struct lpg_data {
	unsigned int lut_base;
	unsigned int lut_size;
	unsigned int triled_base;
	unsigned int pwm_9bit_mask;
	int num_channels;
	struct lpg_channel_data *channels;
};

static int triled_set(struct lpg *lpg, unsigned int mask, bool enable)
{
	/* Skip if we don't have a triled block */
	if (!lpg->triled_base)
		return 0;

	return regmap_update_bits(lpg->map, lpg->triled_base + TRI_LED_EN_CTL,
				  mask, enable ? mask : 0);
}

static int lpg_lut_store(struct lpg *lpg, const u16 *values, size_t len,
			 unsigned int *lo_idx, unsigned int *hi_idx)
{
	unsigned int idx;
	u8 val[2];
	int i;

	/* Hardware does not behave when LO_IDX == HI_IDX */
	if (len == 1)
		return -EINVAL;

	idx = bitmap_find_next_zero_area(lpg->lut_bitmap, lpg->lut_size,
					 0, len, 0);
	if (idx >= lpg->lut_size)
		return -ENOMEM;

	for (i = 0; i < len; i++) {
		val[0] = values[i] & 0xff;
		val[1] = values[i] >> 8;

		regmap_bulk_write(lpg->map,
				  lpg->lut_base + LPG_LUT_REG(idx + i), val, 2);
	}

	bitmap_set(lpg->lut_bitmap, idx, len);

	*lo_idx = idx;
	*hi_idx = idx + len - 1;

	return 0;
}

static u16 *lpg_lut_read(struct lpg *lpg, unsigned int lo_idx,
			 unsigned int hi_idx, size_t *len)
{
	u16 *values;
	u8 val[2];
	int ret;
	int i;

	*len = hi_idx - lo_idx + 1;

	values = kcalloc(*len, sizeof(u16), GFP_KERNEL);
	if (!values)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < *len; i++) {
		ret = regmap_bulk_read(lpg->map,
				       lpg->lut_base + LPG_LUT_REG(lo_idx + i),
				       &val, 2);
		if (ret < 0) {
			kfree(values);
			return ERR_PTR(ret);
		}

		values[i] = val[0] | val[1] << 8;
	}

	return values;
}

static void lpg_lut_free(struct lpg *lpg, unsigned int lo_idx, unsigned int hi_idx)
{
	int len;

	if (lo_idx == hi_idx)
		return;

	len = hi_idx - lo_idx + 1;
	bitmap_clear(lpg->lut_bitmap, lo_idx, len);
}

static int lpg_lut_sync(struct lpg *lpg, unsigned int mask)
{
	return regmap_update_bits(lpg->map, lpg->lut_base + RAMP_CONTROL_REG,
				  mask, 0xff);
}

#define NUM_PWM_PREDIV	4
#define NUM_PWM_CLK	3
#define NUM_EXP		7

static const unsigned int lpg_clk_table[NUM_PWM_PREDIV][NUM_PWM_CLK] = {
	{
		1 * (NSEC_PER_SEC / 1024),
		1 * (NSEC_PER_SEC / 32768),
		1 * (NSEC_PER_SEC / 19200000),
	},
	{
		3 * (NSEC_PER_SEC / 1024),
		3 * (NSEC_PER_SEC / 32768),
		3 * (NSEC_PER_SEC / 19200000),
	},
	{
		5 * (NSEC_PER_SEC / 1024),
		5 * (NSEC_PER_SEC / 32768),
		5 * (NSEC_PER_SEC / 19200000),
	},
	{
		6 * (NSEC_PER_SEC / 1024),
		6 * (NSEC_PER_SEC / 32768),
		6 * (NSEC_PER_SEC / 19200000),
	},
};

/*
 * PWM Frequency = Clock Frequency / (N * T)
 *      or
 * PWM Period = Clock Period * (N * T)
 *      where
 * N = 2^9 or 2^6 for 9-bit or 6-bit PWM size
 * T = Pre-divide * 2^m, where m = 0..7 (exponent)
 *
 * This is the formula to figure out m for the best pre-divide and clock:
 * (PWM Period / N) = (Pre-divide * Clock Period) * 2^m
 */
static void lpg_calc_freq(struct lpg_channel *chan, unsigned int period_us)
{
	int             n, m, clk, div;
	int             best_m, best_div, best_clk;
	unsigned int    last_err, cur_err, min_err;
	unsigned int    tmp_p, period_n;

	if (period_us == chan->period_us)
		return;

	/* PWM Period / N */
	if (period_us < ((unsigned int)(-1) / NSEC_PER_USEC)) {
		period_n = (period_us * NSEC_PER_USEC) >> 6;
		n = 6;
	} else {
		period_n = (period_us >> 9) * NSEC_PER_USEC;
		n = 9;
	}

	min_err = last_err = (unsigned int)(-1);
	best_m = 0;
	best_clk = 0;
	best_div = 0;
	for (clk = 0; clk < NUM_PWM_CLK; clk++) {
		for (div = 0; div < NUM_PWM_PREDIV; div++) {
			/* period_n = (PWM Period / N) */
			/* tmp_p = (Pre-divide * Clock Period) * 2^m */
			tmp_p = lpg_clk_table[div][clk];
			for (m = 0; m <= NUM_EXP; m++) {
				if (period_n > tmp_p)
					cur_err = period_n - tmp_p;
				else
					cur_err = tmp_p - period_n;

				if (cur_err < min_err) {
					min_err = cur_err;
					best_m = m;
					best_clk = clk;
					best_div = div;
				}

				if (m && cur_err > last_err)
					/* Break for bigger cur_err */
					break;

				last_err = cur_err;
				tmp_p <<= 1;
			}
		}
	}

	/* Use higher resolution */
	if (best_m >= 3 && n == 6) {
		n += 3;
		best_m -= 3;
	}

	chan->clk = best_clk;
	chan->pre_div = best_div;
	chan->pre_div_exp = best_m;
	chan->pwm_size = n;

	chan->period_us = period_us;
}

static void lpg_calc_duty(struct lpg_channel *chan, unsigned int duty_us)
{
	unsigned long max = (1 << chan->pwm_size) - 1;
	unsigned long val;

	/* Figure out pwm_value with overflow handling */
	if (duty_us < 1 << (sizeof(val) * 8 - chan->pwm_size))
		val = (duty_us << chan->pwm_size) / chan->period_us;
	else
		val = duty_us / (chan->period_us >> chan->pwm_size);

	if (val > max)
		val = max;

	chan->pwm_value = val;
}

static void lpg_apply_freq(struct lpg_channel *chan)
{
	unsigned long val;
	struct lpg *lpg = chan->lpg;

	if (!chan->enabled)
		return;

	/* Clock register values are off-by-one from lpg_clk_table */
	val = chan->clk + 1;

	if (chan->pwm_size == 9)
		val |= lpg->data->pwm_9bit_mask;

	regmap_write(lpg->map, chan->base + LPG_SIZE_CLK_REG, val);

	val = chan->pre_div << 5 | chan->pre_div_exp;
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
	u8 val[] = { chan->pwm_value & 0xff, chan->pwm_value >> 8 };
	struct lpg *lpg = chan->lpg;

	if (!chan->enabled)
		return;

	regmap_bulk_write(lpg->map, chan->base + PWM_VALUE_REG, val, 2);
}

#define LPG_PATTERN_CONFIG_LO_TO_HI	BIT(4)
#define LPG_PATTERN_CONFIG_REPEAT	BIT(3)
#define LPG_PATTERN_CONFIG_TOGGLE	BIT(2)
#define LPG_PATTERN_CONFIG_PAUSE_HI	BIT(1)
#define LPG_PATTERN_CONFIG_PAUSE_LO	BIT(0)

static void lpg_apply_lut_control(struct lpg_channel *chan)
{
	struct lpg *lpg = chan->lpg;
	unsigned int hi_pause;
	unsigned int lo_pause;
	unsigned int step;
	unsigned int conf = 0;
	unsigned int lo_idx = chan->pattern_lo_idx;
	unsigned int hi_idx = chan->pattern_hi_idx;
	int pattern_len;

	if (!chan->ramp_enabled || chan->pattern_lo_idx == chan->pattern_hi_idx)
		return;

	pattern_len = hi_idx - lo_idx + 1;

	step = DIV_ROUND_UP(chan->ramp_duration_ms, pattern_len);
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

	regmap_write(lpg->map, chan->base + LPG_RAMP_DURATION_REG, step);
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
	lpg_apply_lut_control(chan);
	lpg_enable_glitch(chan);
}

static void lpg_brightness_set(struct led_classdev *cdev,
			      enum led_brightness value)
{
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);
	struct lpg_channel *chan;
	struct lpg *lpg = led->lpg;
	unsigned int duty_us;
	unsigned int triled_mask = 0;
	unsigned int lut_mask = 0;
	int i;

	for (i = 0; i < led->num_channels; i++) {
		chan = led->channels[i];

		if (value == LED_OFF) {
			chan->enabled = false;
			chan->ramp_enabled = false;
		} else if (chan->pattern_lo_idx != chan->pattern_hi_idx) {
			lpg_calc_freq(chan, NSEC_PER_USEC);

			chan->enabled = true;
			chan->ramp_enabled = true;

			lut_mask |= chan->lut_mask;
			triled_mask |= chan->triled_mask;
		} else {
			lpg_calc_freq(chan, NSEC_PER_USEC);

			duty_us = value * chan->period_us / cdev->max_brightness;
			lpg_calc_duty(chan, duty_us);
			chan->enabled = true;
			chan->ramp_enabled = false;

			triled_mask |= chan->triled_mask;
		}

		lpg_apply(chan);
	}

	/* Toggle triled lines */
	if (triled_mask)
		triled_set(lpg, triled_mask, chan->enabled);

	/* Trigger start of ramp generator(s) */
	if (lut_mask)
		lpg_lut_sync(lpg, lut_mask);
}

static enum led_brightness lpg_brightness_get(struct led_classdev *cdev)
{
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);
	struct lpg_channel *chan = led->channels[0];
	unsigned long max = (1 << chan->pwm_size) - 1;

	if (!chan->enabled)
		return LED_OFF;
	else if (chan->pattern_lo_idx != chan->pattern_hi_idx)
		return LED_FULL;
	else
		return chan->pwm_value * cdev->max_brightness / max;
}

static int lpg_blink_set(struct led_classdev *cdev,
			 unsigned long *delay_on, unsigned long *delay_off)
{
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);
	struct lpg_channel *chan = led->channels[0];
	unsigned int period_us;
	unsigned int duty_us;

	if (!*delay_on && !*delay_off) {
		*delay_on = 500;
		*delay_off = 500;
	}

	duty_us = *delay_on * USEC_PER_MSEC;
	period_us = (*delay_on + *delay_off) * USEC_PER_MSEC;

	lpg_calc_freq(chan, period_us);
	lpg_calc_duty(chan, duty_us);

	chan->enabled = true;
	chan->ramp_enabled = false;

	lpg_apply(chan);

	return 0;
}

#define interpolate(x1, y1, x2, y2, x) \
	((y1) + ((y2) - (y1)) * ((x) - (x1)) / ((x2) - (x1)))

static int lpg_pattern_set(struct led_classdev *led_cdev,
			   struct led_pattern *led_pattern, int len,
			   bool repeat)
{
	struct lpg_led *led = container_of(led_cdev, struct lpg_led, cdev);
	struct lpg_channel *chan = led->channels[0];
	struct lpg *lpg = led->lpg;
	unsigned int duration = 0;
	unsigned int min_delta = (unsigned int)-1;
	unsigned int hi_pause;
	unsigned int lo_pause = 0;
	unsigned int lo_idx;
	unsigned int hi_idx;
	unsigned int max = (1 << chan->pwm_size) - 1;
	bool ping_pong = true;
	int brightness_a;
	int brightness_b;
	u16 *pattern;
	int src_idx;
	int dst_idx;
	int step_t;
	int time_a;
	int time_b;
	int value;
	int steps;
	int ret = 0;
	int i;

	/*
	 * The led_pattern specifies brightness values, potentially distributed
	 * unevenly over the duration of the pattern. The LPG only support
	 * evenly distributed values, so we interpolate new values from the
	 * led_pattern.
	 */

	/* Sum the duration over the inner delta_ts and the tail is hi_pause */
	for (src_idx = 0; src_idx < len - 1; src_idx++)
		duration += led_pattern[src_idx].delta_t;
	hi_pause = led_pattern[src_idx].delta_t;

	for (src_idx = 0; src_idx < len; src_idx++) {
		min_delta = min_t(unsigned int, min_delta,
				  led_pattern[src_idx].delta_t);
	}

	steps = duration / min_delta + 1;
	pattern = kcalloc(steps, sizeof(*pattern), GFP_KERNEL);
	if (!pattern)
		return -ENOMEM;

	time_a = 0;
	for (src_idx = 0, dst_idx = 0; dst_idx < steps; dst_idx++) {
		/* The timestamp of this evenly distributed data point */
		step_t = dst_idx * min_delta;

		/*
		 * Find time_a - time_b interval from source pattern that spans
		 * step_t
		 */
		while (time_a + led_pattern[src_idx].delta_t < step_t) {
			if (src_idx >= len - 1)
				break;
			time_a += led_pattern[++src_idx].delta_t;
		}

		if (src_idx < len - 1) {
			time_b = time_a + led_pattern[src_idx].delta_t;

			brightness_a = led_pattern[src_idx].brightness;
			brightness_b = led_pattern[src_idx + 1].brightness;

			/* Interpolate over the source pattern segment */
			value = interpolate(time_a, brightness_a, time_b,
					    brightness_b, step_t);
		} else {
			value = led_pattern[src_idx].brightness;
		}

		/* Scale calculated value to the hardware brightness value */
		pattern[dst_idx] = value * max / led_cdev->max_brightness;
	}

	/* Detect palindromes and use "ping pong" to reduce LUT usage */
	for (dst_idx = 0; dst_idx < steps / 2; dst_idx++) {
		if (pattern[dst_idx] != pattern[len - dst_idx - 1]) {
			ping_pong = false;
			break;
		}
	}
	if (ping_pong) {
		steps = (steps + 1) / 2;

		/*
		 * When ping_pong is set the hi_pause will happen in the middle
		 * of the pattern, so we need to use lo_pause to delay between
		 * the loops.
		 */
		if (repeat)
			lo_pause = hi_pause;

		hi_pause = 0;
	}

	ret = lpg_lut_store(lpg, pattern, steps, &lo_idx, &hi_idx);
	if (ret < 0)
		goto out;

	chan = led->channels[0];

	lpg_lut_free(lpg, chan->pattern_lo_idx, chan->pattern_hi_idx);

	/* Update settings on each associated channel */
	for (i = 0; i < led->num_channels; i++) {
		chan = led->channels[i];

		chan->ramp_duration_ms = duration;
		chan->ramp_ping_pong = ping_pong;
		chan->ramp_oneshot = !repeat;

		chan->pattern_lo_idx = lo_idx;
		chan->pattern_hi_idx = hi_idx;
	}

out:
	kfree(pattern);

	return ret;
}

static int lpg_pattern_clear(struct led_classdev *cdev)
{
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);
	struct lpg_channel *chan;
	struct lpg *lpg = led->lpg;
	int i;

	chan = led->channels[0];

	lpg_lut_free(lpg, chan->pattern_lo_idx, chan->pattern_hi_idx);

	for (i = 0; i < led->num_channels; i++) {
		chan = led->channels[i];
		chan->pattern_lo_idx = 0;
		chan->pattern_hi_idx = 0;
	}

	return 0;
}

static struct led_pattern *lpg_pattern_get(struct led_classdev *cdev,
					   size_t *len, bool *repeat)
{
	struct led_pattern *led_pattern;
	struct lpg_led *led = container_of(cdev, struct lpg_led, cdev);
	struct lpg *lpg = led->lpg;
	struct lpg_channel *chan = led->channels[0];
	unsigned int delta_t;
	unsigned int max = (1 << chan->pwm_size) - 1;
	size_t all_steps;
	size_t steps;
	u16 *pattern;
	size_t i;
	u16 val;

	pattern = lpg_lut_read(lpg, chan->pattern_lo_idx, chan->pattern_hi_idx,
			       &steps);
	if (IS_ERR_OR_NULL(pattern))
		return ERR_CAST(pattern);

	all_steps = chan->ramp_ping_pong ? steps * 2 - 1 : steps;

	delta_t = (chan->ramp_duration_ms + chan->ramp_hi_pause_ms) / all_steps;

	led_pattern = kcalloc(all_steps, sizeof(*pattern), GFP_KERNEL);
	if (!led_pattern) {
		led_pattern = ERR_PTR(-ENOMEM);
		goto out;
	}

	for (i = 0; i < all_steps; i++) {
		if (i < steps)
			val = pattern[i];
		else
			val = pattern[steps - i];

		led_pattern[i].delta_t = delta_t;
		led_pattern[i].brightness = val * cdev->max_brightness / max;
	}

	*len = all_steps;
	*repeat = !chan->ramp_oneshot;

out:
	kfree(pattern);
	return led_pattern;
}

static int lpg_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct lpg *lpg = container_of(chip, struct lpg, pwm);
	struct lpg_channel *chan = &lpg->channels[pwm->hwpwm];

	return chan->in_use ? -EBUSY : 0;
}

static int lpg_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 struct pwm_state *state)
{
	struct lpg *lpg = container_of(chip, struct lpg, pwm);
	struct lpg_channel *chan = &lpg->channels[pwm->hwpwm];

	lpg_calc_freq(chan, state->period / NSEC_PER_USEC);
	lpg_calc_duty(chan, state->duty_cycle / NSEC_PER_USEC);
	chan->enabled = state->enabled;

	lpg_apply(chan);

	triled_set(lpg, chan->triled_mask, chan->enabled);

	state->polarity = PWM_POLARITY_NORMAL;
	state->period = chan->period_us * NSEC_PER_USEC;

	return 0;
}

static const struct pwm_ops lpg_pwm_ops = {
	.request = lpg_pwm_request,
	.apply = lpg_pwm_apply,
	.owner = THIS_MODULE,
};

static int lpg_add_pwm(struct lpg *lpg)
{
	int ret;

	lpg->pwm.base = -1;
	lpg->pwm.dev = lpg->dev;
	lpg->pwm.npwm = lpg->num_channels;
	lpg->pwm.ops = &lpg_pwm_ops;

	ret = pwmchip_add(&lpg->pwm);
	if (ret)
		dev_err(lpg->dev, "failed to add PWM chip: ret %d\n", ret);

	return ret;
}

static int lpg_add_led(struct lpg *lpg, struct device_node *np)
{
	struct lpg_led *led;
	const char *state;
	int sources;
	int size;
	u32 chan;
	int ret;
	int i;

	sources = of_property_count_u32_elems(np, "led-sources");
	if (sources <= 0) {
		dev_err(lpg->dev, "invalid led-sources of %s\n",
			np->name);
		return -EINVAL;
	}

	size = sizeof(*led) + sources * sizeof(struct lpg_channel*);
	led = devm_kzalloc(lpg->dev, size, GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->lpg = lpg;
	led->num_channels = sources;

	for (i = 0; i < sources; i++) {
		ret = of_property_read_u32_index(np, "led-sources",
						 i, &chan);
		if (ret || !chan || chan > lpg->num_channels) {
			dev_err(lpg->dev,
				"invalid led-sources of %s\n",
				np->name);
			return -EINVAL;
		}

		led->channels[i] = &lpg->channels[chan - 1];

		led->channels[i]->in_use = true;
	}

	/* Use label else node name */
	led->cdev.name = of_get_property(np, "label", NULL) ? : np->name;
	led->cdev.default_trigger = of_get_property(np, "linux,default-trigger", NULL);
	led->cdev.brightness_set = lpg_brightness_set;
	led->cdev.brightness_get = lpg_brightness_get;
	led->cdev.blink_set = lpg_blink_set;
	led->cdev.max_brightness = 255;

	/* Register pattern accessors only if we have a LUT block */
	if (lpg->lut_base) {
		led->cdev.pattern_set = lpg_pattern_set;
		led->cdev.pattern_clear = lpg_pattern_clear;
		led->cdev.pattern_get = lpg_pattern_get;
	}

	if (!of_property_read_string(np, "default-state", &state) &&
	    !strcmp(state, "on"))
		led->cdev.brightness = LED_FULL;
	else
		led->cdev.brightness = LED_OFF;

	lpg_brightness_set(&led->cdev, led->cdev.brightness);

	ret = devm_led_classdev_register(lpg->dev, &led->cdev);
	if (ret)
		dev_err(lpg->dev, "unable to register %s\n", led->cdev.name);

	return ret;
}

static int lpg_init_channels(struct lpg *lpg)
{
	const struct lpg_data *data = lpg->data;
	int i;

	lpg->num_channels = data->num_channels;
	lpg->channels = devm_kcalloc(lpg->dev, data->num_channels,
				     sizeof(struct lpg_channel), GFP_KERNEL);
	if (!lpg->channels)
		return -ENOMEM;

	for (i = 0; i < data->num_channels; i++) {
		lpg->channels[i].lpg = lpg;
		lpg->channels[i].base = data->channels[i].base;
		lpg->channels[i].triled_mask = data->channels[i].triled_mask;
		lpg->channels[i].lut_mask = BIT(i);
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

	ret = of_property_read_u32(np, "qcom,power-source", &lpg->triled_src);
	if (ret || lpg->triled_src == 2 || lpg->triled_src > 3) {
		dev_err(lpg->dev, "invalid power source\n");
		return -EINVAL;
	}

	/* Disable automatic trickle charge LED */
	regmap_write(lpg->map, lpg->triled_base + TRI_LED_ATC_CTL, 0);

	/* Configure power source */
	regmap_write(lpg->map, lpg->triled_base + TRI_LED_SRC_SEL,
		     lpg->triled_src);

	/* Default all outputs to off */
	regmap_write(lpg->map, lpg->triled_base + TRI_LED_EN_CTL, 0);

	return 0;
}

static int lpg_init_lut(struct lpg *lpg)
{
	const struct lpg_data *data = lpg->data;
	size_t bitmap_size;

	if (!data->lut_base)
		return 0;

	lpg->lut_base = data->lut_base;
	lpg->lut_size = data->lut_size;

	bitmap_size = BITS_TO_LONGS(lpg->lut_size) / sizeof(unsigned long);
	lpg->lut_bitmap = devm_kzalloc(lpg->dev, bitmap_size, GFP_KERNEL);

	return lpg->lut_bitmap ? 0 : -ENOMEM;
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
	} else if (count < 0 || count != lpg->data->num_channels * 2) {
		ret = count;
		goto err_malformed;
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
	dev_err(lpg->dev, "malformed qcom,dtest\n");
	return ret;
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

	lpg->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!lpg->map) {
		dev_err(&pdev->dev, "parent regmap unavailable\n");
		return -ENXIO;
	}

	ret = lpg_init_channels(lpg);
	if (ret < 0)
		return ret;

	ret = lpg_init_triled(lpg);
	if (ret < 0)
		return ret;

	ret = lpg_init_lut(lpg);
	if (ret < 0)
		return ret;

	ret = lpg_parse_dtest(lpg);
	if (ret < 0)
		return ret;

	for_each_available_child_of_node(pdev->dev.of_node, np) {
		ret = lpg_add_led(lpg, np);
		if (ret)
			return ret;
	}

	for (i = 0; i < lpg->num_channels; i++)
		lpg_apply_dtest(&lpg->channels[i]);

	ret = lpg_add_pwm(lpg);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, lpg);

	return 0;
}

static int lpg_remove(struct platform_device *pdev)
{
	struct lpg *lpg = platform_get_drvdata(pdev);

	pwmchip_remove(&lpg->pwm);

	return 0;
}

static const struct lpg_data pm8916_pwm_data = {
	.pwm_9bit_mask = BIT(2),

	.num_channels = 1,
	.channels = (struct lpg_channel_data[]) {
		{ .base = 0xbc00 },
	},
};

static const struct lpg_data pm8941_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 64,

	.triled_base = 0xd000,

	.pwm_9bit_mask = 3 << 4,

	.num_channels = 8,
	.channels = (struct lpg_channel_data[]) {
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

	.pwm_9bit_mask = 3 << 4,

	.num_channels = 6,
	.channels = (struct lpg_channel_data[]) {
		{ .base = 0xb100 },
		{ .base = 0xb200 },
		{ .base = 0xb300 },
		{ .base = 0xb400 },
		{ .base = 0xb500 },
		{ .base = 0xb600 },
	},
};

static const struct lpg_data pmi8994_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 24,

	.triled_base = 0xd000,

	.pwm_9bit_mask = BIT(4),

	.num_channels = 4,
	.channels = (struct lpg_channel_data[]) {
		{ .base = 0xb100, .triled_mask = BIT(5) },
		{ .base = 0xb200, .triled_mask = BIT(6) },
		{ .base = 0xb300, .triled_mask = BIT(7) },
		{ .base = 0xb400 },
	},
};

static const struct lpg_data pmi8998_lpg_data = {
	.lut_base = 0xb000,
	.lut_size = 49,

	.pwm_9bit_mask = BIT(4),

	.num_channels = 6,
	.channels = (struct lpg_channel_data[]) {
		{ .base = 0xb100 },
		{ .base = 0xb200 },
		{ .base = 0xb300, .triled_mask = BIT(5) },
		{ .base = 0xb400, .triled_mask = BIT(6) },
		{ .base = 0xb500, .triled_mask = BIT(7) },
		{ .base = 0xb600 },
	},
};

static const struct of_device_id lpg_of_table[] = {
	{ .compatible = "qcom,pm8916-pwm", .data = &pm8916_pwm_data },
	{ .compatible = "qcom,pm8941-lpg", .data = &pm8941_lpg_data },
	{ .compatible = "qcom,pm8994-lpg", .data = &pm8994_lpg_data },
	{ .compatible = "qcom,pmi8994-lpg", .data = &pmi8994_lpg_data },
	{ .compatible = "qcom,pmi8998-lpg", .data = &pmi8998_lpg_data },
	{},
};
MODULE_DEVICE_TABLE(of, lpg_of_table);

static struct platform_driver lpg_driver = {
	.probe = lpg_probe,
	.remove = lpg_remove,
	.driver = {
		.name = "qcom-spmi-lpg",
		.of_match_table = lpg_of_table,
	},
};
module_platform_driver(lpg_driver);

MODULE_DESCRIPTION("Qualcomm TRI LED driver");
MODULE_LICENSE("GPL v2");
