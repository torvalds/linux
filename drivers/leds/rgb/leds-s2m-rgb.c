// SPDX-License-Identifier: GPL-2.0
/*
 * RGB LED Driver for Samsung S2M series PMICs.
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd
 * Copyright (c) 2026 Kaustabh Chakraborty <kauschluss@disroot.org>
 */

#include <linux/container_of.h>
#include <linux/led-class-multicolor.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mu005.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct s2m_rgb {
	struct device *dev;
	struct regmap *regmap;
	struct led_classdev_mc mc;
	/*
	 * The mutex object prevents race conditions when evaluation and
	 * application of LED pattern state.
	 */
	struct mutex lock;
	/*
	 * State variables representing the current LED pattern, these only to
	 * be accessed when lock is held.
	 */
	u8 ramp_up;
	u8 ramp_dn;
	u8 stay_hi;
	u8 stay_lo;
};

static struct led_classdev_mc *to_s2m_mc(struct led_classdev *cdev)
{
	return container_of(cdev, struct led_classdev_mc, led_cdev);
}

static struct s2m_rgb *to_s2m_rgb(struct led_classdev_mc *mc)
{
	return container_of(mc, struct s2m_rgb, mc);
}

static const u32 s2mu005_rgb_lut_ramp[] = {
	0,	100,	200,	300,	400,	500,	600,	700,
	800,	1000,	1200,	1400,	1600,	1800,	2000,	2200,
};

static const u32 s2mu005_rgb_lut_stay_hi[] = {
	100,	200,	300,	400,	500,	750,	1000,	1250,
	1500,	1750,	2000,	2250,	2500,	2750,	3000,	3250,
};

static const u32 s2mu005_rgb_lut_stay_lo[] = {
	0,	500,	1000,	1500,	2000,	2500,	3000,	3500,
	4000,	4500,	5000,	6000,	7000,	8000,	10000,	12000,
};

static int s2mu005_rgb_apply_params(struct s2m_rgb *rgb)
{
	struct regmap *regmap = rgb->regmap;
	unsigned int ramp_val = 0;
	unsigned int stay_val = 0;
	int ret;

	ramp_val |= FIELD_PREP(S2MU005_RGB_CH_RAMP_UP, rgb->ramp_up);
	ramp_val |= FIELD_PREP(S2MU005_RGB_CH_RAMP_DN, rgb->ramp_dn);

	stay_val |= FIELD_PREP(S2MU005_RGB_CH_STAY_HI, rgb->stay_hi);
	stay_val |= FIELD_PREP(S2MU005_RGB_CH_STAY_LO, rgb->stay_lo);

	ret = regmap_write(regmap, S2MU005_REG_RGB_EN, S2MU005_RGB_RESET);
	if (ret) {
		dev_err(rgb->dev, "failed to reset RGB LEDs\n");
		return ret;
	}

	for (int i = 0; i < rgb->mc.num_colors; i++) {
		ret = regmap_write(regmap, S2MU005_REG_RGB_CH_CTRL(i),
				   rgb->mc.subled_info[i].brightness);
		if (ret) {
			dev_err(rgb->dev, "failed to set LED brightness\n");
			return ret;
		}

		ret = regmap_write(regmap, S2MU005_REG_RGB_CH_RAMP(i), ramp_val);
		if (ret) {
			dev_err(rgb->dev, "failed to set ramp timings\n");
			return ret;
		}

		ret = regmap_write(regmap, S2MU005_REG_RGB_CH_STAY(i), stay_val);
		if (ret) {
			dev_err(rgb->dev, "failed to set stay timings\n");
			return ret;
		}
	}

	ret = regmap_write(regmap, S2MU005_REG_RGB_EN, S2MU005_RGB_SLOPE_SMOOTH);
	if (ret) {
		dev_err(rgb->dev, "failed to set ramp slope\n");
		return ret;
	}

	return 0;
}

static int s2mu005_rgb_reset_params(struct s2m_rgb *rgb)
{
	struct regmap *regmap = rgb->regmap;
	int ret;

	ret = regmap_write(regmap, S2MU005_REG_RGB_EN, S2MU005_RGB_RESET);
	if (ret) {
		dev_err(rgb->dev, "failed to reset RGB LEDs\n");
		return ret;
	}

	rgb->ramp_up = 0;
	rgb->ramp_dn = 0;
	rgb->stay_hi = 0;
	rgb->stay_lo = 0;

	return 0;
}

/*
 * s2m_rgb_lut_get_closest_duration - find closest duration in look-up table
 * @lut: the look-up table to search for the closest timing
 * @len: number of elements in the look-up table array
 * @duration: the timing duration requested
 *
 * This function does a binary search on the given array, and finds the closest
 * value to the requested timing. It is expected that the look-up table to be
 * provided, is already sorted.
 *
 * This function returns a negative error code, or a non-negative index of the
 * value in the look-up table closest to the one requested.
 */
static int s2m_rgb_lut_get_closest_duration(const u32 *lut, const size_t len, const u32 duration)
{
	u32 closest_distance = abs(duration - lut[0]);
	int closest_index = 0;
	int lo = 0;
	int hi = len - 1;

	/*
	 * Allow a small amount of extrapolation beyond the highest timing value.
	 *
	 * Consider x and y to be the two last values in the table, and x < y.
	 * Since (y - x) / 2 integers, in the range [x + (y - x) / 2, y)
	 * returns y as the closest, allow extrapolation for the succeeding
	 * (y - x) / 2 integers as well, viz, up to (y, y + (y - x) / 2].
	 * Anything beyond that is invalid.
	 */
	if (len >= 2 && duration > lut[len - 1] + (lut[len - 1] - lut[len - 2]) / 2)
		return -EINVAL;

	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;

		/* Narrow down search window as per binary-search algorithm. */
		if (duration < lut[mid])
			hi = mid - 1;
		else
			lo = mid + 1;

		if (abs(duration - lut[mid]) < closest_distance) {
			closest_distance = abs(duration - lut[mid]);
			closest_index = mid;
		}
	}

	return closest_index;
}

static int s2m_rgb_pattern_set(struct led_classdev *cdev, struct led_pattern *pattern, u32 len,
			       int repeat)
{
	struct s2m_rgb *rgb = to_s2m_rgb(to_s2m_mc(cdev));
	const u32 *lut_ramp_up, *lut_ramp_dn, *lut_stay_hi, *lut_stay_lo;
	size_t lut_ramp_up_len, lut_ramp_dn_len, lut_stay_hi_len, lut_stay_lo_len;
	int brightness_peak = 0;
	u32 time_hi = 0, time_lo = 0;
	bool ramp_up_en = false, ramp_dn_en = false;
	int ret;

	/*
	 * The typical pattern supported by this device can be represented with
	 * the following graph:
	 *
	 *  255 T ''''''-.                         .-'''''''-.
	 *      |         '.                     .'           '.
	 *      |           \                   /               \
	 *      |            '.               .'                 '.
	 *      |              '-...........-'                     '-
	 *    0 +----------------------------------------------------> time (s)
	 *
	 *       <---- HIGH ----><-- LOW --><-------- HIGH --------->
	 *       <-----><-------><---------><-------><-----><------->
	 *       stay_hi ramp_dn   stay_lo   ramp_up stay_hi ramp_dn
	 *
	 * There are two states, named HIGH and LOW. HIGH has a non-zero
	 * brightness level, while LOW is of zero brightness. The pattern
	 * provided should mention only one zero and non-zero brightness level.
	 * The hardware always starts the pattern from the HIGH state, as shown
	 * in the graph.
	 *
	 * The HIGH state can be divided in three somewhat equal timings:
	 * ramp_up, stay_hi, and ramp_dn. The LOW state has only one timing:
	 * stay_lo.
	 */

	/* Only indefinitely looping patterns are supported. */
	if (repeat != -1)
		return -EINVAL;

	/* Pattern should consist of at least two tuples. */
	if (len < 2)
		return -EINVAL;

	for (int i = 0; i < len; i++) {
		int brightness = pattern[i].brightness;
		u32 delta_t = pattern[i].delta_t;

		if (brightness) {
			/*
			 * The pattern should define only one non-zero
			 * brightness in the HIGH state. The device doesn't
			 * have any provisions to handle multiple peak
			 * brightness levels.
			 */
			if (brightness_peak && brightness_peak != brightness)
				return -EINVAL;

			brightness_peak = brightness;
			time_hi += delta_t;
			ramp_dn_en = !!delta_t;
		} else {
			time_lo += delta_t;
			ramp_up_en = !!delta_t;
		}
	}

	/* LUTs are specific to device variant. */
	lut_ramp_up = s2mu005_rgb_lut_ramp;
	lut_ramp_up_len = ARRAY_SIZE(s2mu005_rgb_lut_ramp);
	lut_ramp_dn = s2mu005_rgb_lut_ramp;
	lut_ramp_dn_len = ARRAY_SIZE(s2mu005_rgb_lut_ramp);
	lut_stay_hi = s2mu005_rgb_lut_stay_hi;
	lut_stay_hi_len = ARRAY_SIZE(s2mu005_rgb_lut_stay_hi);
	lut_stay_lo = s2mu005_rgb_lut_stay_lo;
	lut_stay_lo_len = ARRAY_SIZE(s2mu005_rgb_lut_stay_lo);

	mutex_lock(&rgb->lock);

	/*
	 * The timings ramp_up, stay_hi, and ramp_dn of the HIGH state are
	 * roughly equal. Firstly, calculate and set timings for ramp_up and
	 * ramp_dn (making sure they're exactly equal).
	 */
	rgb->ramp_up = 0;
	rgb->ramp_dn = 0;

	if (ramp_up_en) {
		ret = s2m_rgb_lut_get_closest_duration(lut_ramp_up, lut_ramp_up_len, time_hi / 3);
		if (ret < 0)
			goto param_fail;
		rgb->ramp_up = (u8)ret;
	}

	if (ramp_dn_en) {
		ret = s2m_rgb_lut_get_closest_duration(lut_ramp_dn, lut_ramp_dn_len, time_hi / 3);
		if (ret < 0)
			goto param_fail;
		rgb->ramp_dn = (u8)ret;
	}

	/*
	 * Subtract the allocated ramp timings from time_hi (and also making
	 * sure it doesn't underflow!). The remaining time is allocated to
	 * stay_hi.
	 */
	time_hi -= min(time_hi, lut_ramp_up[rgb->ramp_up]);
	time_hi -= min(time_hi, lut_ramp_dn[rgb->ramp_dn]);

	ret = s2m_rgb_lut_get_closest_duration(lut_stay_hi, lut_stay_hi_len, time_hi);
	if (ret < 0)
		goto param_fail;
	rgb->stay_hi = (u8)ret;

	ret = s2m_rgb_lut_get_closest_duration(lut_stay_lo, lut_stay_lo_len, time_lo);
	if (ret < 0)
		goto param_fail;
	rgb->stay_lo = (u8)ret;

	led_mc_calc_color_components(&rgb->mc, brightness_peak);
	/* Apply params with variant-specific implementation. */
	ret = s2mu005_rgb_apply_params(rgb);
	if (ret)
		goto param_fail;

	mutex_unlock(&rgb->lock);

	return 0;

param_fail:
	rgb->ramp_up = 0;
	rgb->ramp_dn = 0;
	rgb->stay_hi = 0;
	rgb->stay_lo = 0;

	mutex_unlock(&rgb->lock);

	return ret;
}

static int s2m_rgb_pattern_clear(struct led_classdev *cdev)
{
	struct s2m_rgb *rgb = to_s2m_rgb(to_s2m_mc(cdev));
	int ret = 0;

	mutex_lock(&rgb->lock);

	/* Reset params with variant-specific implementation. */
	ret = s2mu005_rgb_reset_params(rgb);

	mutex_unlock(&rgb->lock);

	return ret;
}

static int s2m_rgb_brightness_set(struct led_classdev *cdev, enum led_brightness value)
{
	struct s2m_rgb *rgb = to_s2m_rgb(to_s2m_mc(cdev));
	int ret = 0;

	if (!value)
		return s2m_rgb_pattern_clear(cdev);

	mutex_lock(&rgb->lock);

	led_mc_calc_color_components(&rgb->mc, value);
	/* Apply params with variant-specific implementation. */
	ret = s2mu005_rgb_apply_params(rgb);

	mutex_unlock(&rgb->lock);

	return ret;
}

static const struct mc_subled s2mu005_rgb_subled_info[] = {
	{ .channel = 0, .color_index = LED_COLOR_ID_BLUE },
	{ .channel = 1, .color_index = LED_COLOR_ID_GREEN },
	{ .channel = 2, .color_index = LED_COLOR_ID_RED },
};

static int s2m_rgb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sec_pmic_dev *pmic_drvdata = dev_get_drvdata(dev->parent);
	struct s2m_rgb *rgb;
	struct led_init_data init_data = {};
	int ret;

	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	platform_set_drvdata(pdev, rgb);
	rgb->dev = dev;
	rgb->regmap = pmic_drvdata->regmap_pmic;

	/* Configure variant-specific details. */
	rgb->mc.num_colors = ARRAY_SIZE(s2mu005_rgb_subled_info);
	rgb->mc.subled_info = devm_kmemdup(dev, s2mu005_rgb_subled_info,
					   sizeof(s2mu005_rgb_subled_info), GFP_KERNEL);
	if (!rgb->mc.subled_info)
		return -ENOMEM;

	rgb->mc.led_cdev.max_brightness = 255;
	rgb->mc.led_cdev.brightness_set_blocking = s2m_rgb_brightness_set;
	rgb->mc.led_cdev.pattern_set = s2m_rgb_pattern_set;
	rgb->mc.led_cdev.pattern_clear = s2m_rgb_pattern_clear;

	ret = devm_mutex_init(dev, &rgb->lock);
	if (ret)
		return dev_err_probe(dev, ret, "failed to create mutex lock\n");

	init_data.fwnode = of_fwnode_handle(dev->of_node);
	ret = devm_led_classdev_multicolor_register_ext(dev, &rgb->mc, &init_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to create LED device\n");

	return 0;
}

static const struct platform_device_id s2m_rgb_id_table[] = {
	{ "s2mu005-rgb", S2MU005 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, s2m_rgb_id_table);

static const struct of_device_id s2m_rgb_of_match_table[] = {
	{ .compatible = "samsung,s2mu005-rgb", .data = (void *)S2MU005 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, s2m_rgb_of_match_table);

static struct platform_driver s2m_rgb_driver = {
	.driver = {
		.name = "s2m-rgb",
	},
	.probe = s2m_rgb_probe,
	.id_table = s2m_rgb_id_table,
};
module_platform_driver(s2m_rgb_driver);

MODULE_DESCRIPTION("RGB LED Driver for Samsung S2M Series PMICs");
MODULE_AUTHOR("Kaustabh Chakraborty <kauschluss@disroot.org>");
MODULE_LICENSE("GPL");
