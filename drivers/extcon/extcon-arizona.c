/*
 * extcon-arizona.c - Extcon driver Wolfson Arizona devices
 *
 *  Copyright (C) 2012-2014 Wolfson Microelectronics plc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/extcon.h>

#include <sound/soc.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/pdata.h>
#include <linux/mfd/arizona/registers.h>
#include <dt-bindings/mfd/arizona.h>

#define ARIZONA_MAX_MICD_RANGE 8

#define ARIZONA_MICD_CLAMP_MODE_JDL      0x4
#define ARIZONA_MICD_CLAMP_MODE_JDH      0x5
#define ARIZONA_MICD_CLAMP_MODE_JDL_GP5H 0x9
#define ARIZONA_MICD_CLAMP_MODE_JDH_GP5H 0xb

#define ARIZONA_TST_CAP_DEFAULT 0x3
#define ARIZONA_TST_CAP_CLAMP   0x1

#define ARIZONA_HPDET_MAX 10000

#define HPDET_DEBOUNCE 500
#define DEFAULT_MICD_TIMEOUT 2000

#define ARIZONA_HPDET_WAIT_COUNT 15
#define ARIZONA_HPDET_WAIT_DELAY_MS 20

#define QUICK_HEADPHONE_MAX_OHM 3
#define MICROPHONE_MIN_OHM      1257
#define MICROPHONE_MAX_OHM      30000

#define MICD_DBTIME_TWO_READINGS 2
#define MICD_DBTIME_FOUR_READINGS 4

#define MICD_LVL_1_TO_7 (ARIZONA_MICD_LVL_1 | ARIZONA_MICD_LVL_2 | \
			 ARIZONA_MICD_LVL_3 | ARIZONA_MICD_LVL_4 | \
			 ARIZONA_MICD_LVL_5 | ARIZONA_MICD_LVL_6 | \
			 ARIZONA_MICD_LVL_7)

#define MICD_LVL_0_TO_7 (ARIZONA_MICD_LVL_0 | MICD_LVL_1_TO_7)

#define MICD_LVL_0_TO_8 (MICD_LVL_0_TO_7 | ARIZONA_MICD_LVL_8)

struct arizona_extcon_info {
	struct device *dev;
	struct arizona *arizona;
	struct mutex lock;
	struct regulator *micvdd;
	struct input_dev *input;

	u16 last_jackdet;

	int micd_mode;
	const struct arizona_micd_config *micd_modes;
	int micd_num_modes;

	const struct arizona_micd_range *micd_ranges;
	int num_micd_ranges;

	int micd_timeout;

	bool micd_reva;
	bool micd_clamp;

	struct delayed_work hpdet_work;
	struct delayed_work micd_detect_work;
	struct delayed_work micd_timeout_work;

	bool hpdet_active;
	bool hpdet_done;
	bool hpdet_retried;

	int num_hpdet_res;
	unsigned int hpdet_res[3];

	bool mic;
	bool detecting;
	int jack_flips;

	int hpdet_ip_version;

	struct extcon_dev *edev;

	struct gpio_desc *micd_pol_gpio;
};

static const struct arizona_micd_config micd_default_modes[] = {
	{ ARIZONA_ACCDET_SRC, 1, 0 },
	{ 0,                  2, 1 },
};

static const struct arizona_micd_range micd_default_ranges[] = {
	{ .max =  11, .key = BTN_0 },
	{ .max =  28, .key = BTN_1 },
	{ .max =  54, .key = BTN_2 },
	{ .max = 100, .key = BTN_3 },
	{ .max = 186, .key = BTN_4 },
	{ .max = 430, .key = BTN_5 },
};

/* The number of levels in arizona_micd_levels valid for button thresholds */
#define ARIZONA_NUM_MICD_BUTTON_LEVELS 64

static const int arizona_micd_levels[] = {
	3, 6, 8, 11, 13, 16, 18, 21, 23, 26, 28, 31, 34, 36, 39, 41, 44, 46,
	49, 52, 54, 57, 60, 62, 65, 67, 70, 73, 75, 78, 81, 83, 89, 94, 100,
	105, 111, 116, 122, 127, 139, 150, 161, 173, 186, 196, 209, 220, 245,
	270, 295, 321, 348, 375, 402, 430, 489, 550, 614, 681, 752, 903, 1071,
	1257, 30000,
};

static const unsigned int arizona_cable[] = {
	EXTCON_MECHANICAL,
	EXTCON_JACK_MICROPHONE,
	EXTCON_JACK_HEADPHONE,
	EXTCON_JACK_LINE_OUT,
	EXTCON_NONE,
};

static void arizona_start_hpdet_acc_id(struct arizona_extcon_info *info);

static void arizona_extcon_hp_clamp(struct arizona_extcon_info *info,
				    bool clamp)
{
	struct arizona *arizona = info->arizona;
	unsigned int mask = 0, val = 0;
	unsigned int cap_sel = 0;
	int ret;

	switch (arizona->type) {
	case WM8998:
	case WM1814:
		mask = 0;
		break;
	case WM5110:
	case WM8280:
		mask = ARIZONA_HP1L_SHRTO | ARIZONA_HP1L_FLWR |
		       ARIZONA_HP1L_SHRTI;
		if (clamp) {
			val = ARIZONA_HP1L_SHRTO;
			cap_sel = ARIZONA_TST_CAP_CLAMP;
		} else {
			val = ARIZONA_HP1L_FLWR | ARIZONA_HP1L_SHRTI;
			cap_sel = ARIZONA_TST_CAP_DEFAULT;
		}

		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_HP_TEST_CTRL_1,
					 ARIZONA_HP1_TST_CAP_SEL_MASK,
					 cap_sel);
		if (ret != 0)
			dev_warn(arizona->dev,
				 "Failed to set TST_CAP_SEL: %d\n", ret);
		break;
	default:
		mask = ARIZONA_RMV_SHRT_HP1L;
		if (clamp)
			val = ARIZONA_RMV_SHRT_HP1L;
		break;
	}

	snd_soc_dapm_mutex_lock(arizona->dapm);

	arizona->hpdet_clamp = clamp;

	/* Keep the HP output stages disabled while doing the clamp */
	if (clamp) {
		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_OUTPUT_ENABLES_1,
					 ARIZONA_OUT1L_ENA |
					 ARIZONA_OUT1R_ENA, 0);
		if (ret != 0)
			dev_warn(arizona->dev,
				"Failed to disable headphone outputs: %d\n",
				 ret);
	}

	if (mask) {
		ret = regmap_update_bits(arizona->regmap, ARIZONA_HP_CTRL_1L,
					 mask, val);
		if (ret != 0)
			dev_warn(arizona->dev, "Failed to do clamp: %d\n",
				 ret);

		ret = regmap_update_bits(arizona->regmap, ARIZONA_HP_CTRL_1R,
					 mask, val);
		if (ret != 0)
			dev_warn(arizona->dev, "Failed to do clamp: %d\n",
				 ret);
	}

	/* Restore the desired state while not doing the clamp */
	if (!clamp) {
		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_OUTPUT_ENABLES_1,
					 ARIZONA_OUT1L_ENA |
					 ARIZONA_OUT1R_ENA, arizona->hp_ena);
		if (ret != 0)
			dev_warn(arizona->dev,
				 "Failed to restore headphone outputs: %d\n",
				 ret);
	}

	snd_soc_dapm_mutex_unlock(arizona->dapm);
}

static void arizona_extcon_set_mode(struct arizona_extcon_info *info, int mode)
{
	struct arizona *arizona = info->arizona;

	mode %= info->micd_num_modes;

	gpiod_set_value_cansleep(info->micd_pol_gpio,
				 info->micd_modes[mode].gpio);

	regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_1,
			   ARIZONA_MICD_BIAS_SRC_MASK,
			   info->micd_modes[mode].bias <<
			   ARIZONA_MICD_BIAS_SRC_SHIFT);
	regmap_update_bits(arizona->regmap, ARIZONA_ACCESSORY_DETECT_MODE_1,
			   ARIZONA_ACCDET_SRC, info->micd_modes[mode].src);

	info->micd_mode = mode;

	dev_dbg(arizona->dev, "Set jack polarity to %d\n", mode);
}

static const char *arizona_extcon_get_micbias(struct arizona_extcon_info *info)
{
	switch (info->micd_modes[0].bias) {
	case 1:
		return "MICBIAS1";
	case 2:
		return "MICBIAS2";
	case 3:
		return "MICBIAS3";
	default:
		return "MICVDD";
	}
}

static void arizona_extcon_pulse_micbias(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	const char *widget = arizona_extcon_get_micbias(info);
	struct snd_soc_dapm_context *dapm = arizona->dapm;
	struct snd_soc_component *component = snd_soc_dapm_to_component(dapm);
	int ret;

	ret = snd_soc_component_force_enable_pin(component, widget);
	if (ret != 0)
		dev_warn(arizona->dev, "Failed to enable %s: %d\n",
			 widget, ret);

	snd_soc_dapm_sync(dapm);

	if (!arizona->pdata.micd_force_micbias) {
		ret = snd_soc_component_disable_pin(component, widget);
		if (ret != 0)
			dev_warn(arizona->dev, "Failed to disable %s: %d\n",
				 widget, ret);

		snd_soc_dapm_sync(dapm);
	}
}

static void arizona_start_mic(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	bool change;
	int ret;
	unsigned int mode;

	/* Microphone detection can't use idle mode */
	pm_runtime_get(info->dev);

	if (info->detecting) {
		ret = regulator_allow_bypass(info->micvdd, false);
		if (ret != 0) {
			dev_err(arizona->dev,
				"Failed to regulate MICVDD: %d\n",
				ret);
		}
	}

	ret = regulator_enable(info->micvdd);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to enable MICVDD: %d\n",
			ret);
	}

	if (info->micd_reva) {
		regmap_write(arizona->regmap, 0x80, 0x3);
		regmap_write(arizona->regmap, 0x294, 0);
		regmap_write(arizona->regmap, 0x80, 0x0);
	}

	if (info->detecting && arizona->pdata.micd_software_compare)
		mode = ARIZONA_ACCDET_MODE_ADC;
	else
		mode = ARIZONA_ACCDET_MODE_MIC;

	regmap_update_bits(arizona->regmap,
			   ARIZONA_ACCESSORY_DETECT_MODE_1,
			   ARIZONA_ACCDET_MODE_MASK, mode);

	arizona_extcon_pulse_micbias(info);

	regmap_update_bits_check(arizona->regmap, ARIZONA_MIC_DETECT_1,
				 ARIZONA_MICD_ENA, ARIZONA_MICD_ENA,
				 &change);
	if (!change) {
		regulator_disable(info->micvdd);
		pm_runtime_put_autosuspend(info->dev);
	}
}

static void arizona_stop_mic(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	const char *widget = arizona_extcon_get_micbias(info);
	struct snd_soc_dapm_context *dapm = arizona->dapm;
	struct snd_soc_component *component = snd_soc_dapm_to_component(dapm);
	bool change;
	int ret;

	regmap_update_bits_check(arizona->regmap, ARIZONA_MIC_DETECT_1,
				 ARIZONA_MICD_ENA, 0,
				 &change);

	ret = snd_soc_component_disable_pin(component, widget);
	if (ret != 0)
		dev_warn(arizona->dev,
			 "Failed to disable %s: %d\n",
			 widget, ret);

	snd_soc_dapm_sync(dapm);

	if (info->micd_reva) {
		regmap_write(arizona->regmap, 0x80, 0x3);
		regmap_write(arizona->regmap, 0x294, 2);
		regmap_write(arizona->regmap, 0x80, 0x0);
	}

	ret = regulator_allow_bypass(info->micvdd, true);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to bypass MICVDD: %d\n",
			ret);
	}

	if (change) {
		regulator_disable(info->micvdd);
		pm_runtime_mark_last_busy(info->dev);
		pm_runtime_put_autosuspend(info->dev);
	}
}

static struct {
	unsigned int threshold;
	unsigned int factor_a;
	unsigned int factor_b;
} arizona_hpdet_b_ranges[] = {
	{ 100,  5528,   362464 },
	{ 169, 11084,  6186851 },
	{ 169, 11065, 65460395 },
};

#define ARIZONA_HPDET_B_RANGE_MAX 0x3fb

static struct {
	int min;
	int max;
} arizona_hpdet_c_ranges[] = {
	{ 0,       30 },
	{ 8,      100 },
	{ 100,   1000 },
	{ 1000, 10000 },
};

static int arizona_hpdet_read(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	unsigned int val, range;
	int ret;

	ret = regmap_read(arizona->regmap, ARIZONA_HEADPHONE_DETECT_2, &val);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to read HPDET status: %d\n",
			ret);
		return ret;
	}

	switch (info->hpdet_ip_version) {
	case 0:
		if (!(val & ARIZONA_HP_DONE)) {
			dev_err(arizona->dev, "HPDET did not complete: %x\n",
				val);
			return -EAGAIN;
		}

		val &= ARIZONA_HP_LVL_MASK;
		break;

	case 1:
		if (!(val & ARIZONA_HP_DONE_B)) {
			dev_err(arizona->dev, "HPDET did not complete: %x\n",
				val);
			return -EAGAIN;
		}

		ret = regmap_read(arizona->regmap, ARIZONA_HP_DACVAL, &val);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to read HP value: %d\n",
				ret);
			return -EAGAIN;
		}

		regmap_read(arizona->regmap, ARIZONA_HEADPHONE_DETECT_1,
			    &range);
		range = (range & ARIZONA_HP_IMPEDANCE_RANGE_MASK)
			   >> ARIZONA_HP_IMPEDANCE_RANGE_SHIFT;

		if (range < ARRAY_SIZE(arizona_hpdet_b_ranges) - 1 &&
		    (val < arizona_hpdet_b_ranges[range].threshold ||
		     val >= ARIZONA_HPDET_B_RANGE_MAX)) {
			range++;
			dev_dbg(arizona->dev, "Moving to HPDET range %d\n",
				range);
			regmap_update_bits(arizona->regmap,
					   ARIZONA_HEADPHONE_DETECT_1,
					   ARIZONA_HP_IMPEDANCE_RANGE_MASK,
					   range <<
					   ARIZONA_HP_IMPEDANCE_RANGE_SHIFT);
			return -EAGAIN;
		}

		/* If we go out of range report top of range */
		if (val < arizona_hpdet_b_ranges[range].threshold ||
		    val >= ARIZONA_HPDET_B_RANGE_MAX) {
			dev_dbg(arizona->dev, "Measurement out of range\n");
			return ARIZONA_HPDET_MAX;
		}

		dev_dbg(arizona->dev, "HPDET read %d in range %d\n",
			val, range);

		val = arizona_hpdet_b_ranges[range].factor_b
			/ ((val * 100) -
			   arizona_hpdet_b_ranges[range].factor_a);
		break;

	case 2:
		if (!(val & ARIZONA_HP_DONE_B)) {
			dev_err(arizona->dev, "HPDET did not complete: %x\n",
				val);
			return -EAGAIN;
		}

		val &= ARIZONA_HP_LVL_B_MASK;
		/* Convert to ohms, the value is in 0.5 ohm increments */
		val /= 2;

		regmap_read(arizona->regmap, ARIZONA_HEADPHONE_DETECT_1,
			    &range);
		range = (range & ARIZONA_HP_IMPEDANCE_RANGE_MASK)
			   >> ARIZONA_HP_IMPEDANCE_RANGE_SHIFT;

		/* Skip up a range, or report? */
		if (range < ARRAY_SIZE(arizona_hpdet_c_ranges) - 1 &&
		    (val >= arizona_hpdet_c_ranges[range].max)) {
			range++;
			dev_dbg(arizona->dev, "Moving to HPDET range %d-%d\n",
				arizona_hpdet_c_ranges[range].min,
				arizona_hpdet_c_ranges[range].max);
			regmap_update_bits(arizona->regmap,
					   ARIZONA_HEADPHONE_DETECT_1,
					   ARIZONA_HP_IMPEDANCE_RANGE_MASK,
					   range <<
					   ARIZONA_HP_IMPEDANCE_RANGE_SHIFT);
			return -EAGAIN;
		}

		if (range && (val < arizona_hpdet_c_ranges[range].min)) {
			dev_dbg(arizona->dev, "Reporting range boundary %d\n",
				arizona_hpdet_c_ranges[range].min);
			val = arizona_hpdet_c_ranges[range].min;
		}
		break;

	default:
		dev_warn(arizona->dev, "Unknown HPDET IP revision %d\n",
			 info->hpdet_ip_version);
		return -EINVAL;
	}

	dev_dbg(arizona->dev, "HP impedance %d ohms\n", val);
	return val;
}

static int arizona_hpdet_do_id(struct arizona_extcon_info *info, int *reading,
			       bool *mic)
{
	struct arizona *arizona = info->arizona;
	int id_gpio = arizona->pdata.hpdet_id_gpio;

	/*
	 * If we're using HPDET for accessory identification we need
	 * to take multiple measurements, step through them in sequence.
	 */
	if (arizona->pdata.hpdet_acc_id) {
		info->hpdet_res[info->num_hpdet_res++] = *reading;

		/* Only check the mic directly if we didn't already ID it */
		if (id_gpio && info->num_hpdet_res == 1) {
			dev_dbg(arizona->dev, "Measuring mic\n");

			regmap_update_bits(arizona->regmap,
					   ARIZONA_ACCESSORY_DETECT_MODE_1,
					   ARIZONA_ACCDET_MODE_MASK |
					   ARIZONA_ACCDET_SRC,
					   ARIZONA_ACCDET_MODE_HPR |
					   info->micd_modes[0].src);

			gpio_set_value_cansleep(id_gpio, 1);

			regmap_update_bits(arizona->regmap,
					   ARIZONA_HEADPHONE_DETECT_1,
					   ARIZONA_HP_POLL, ARIZONA_HP_POLL);
			return -EAGAIN;
		}

		/* OK, got both.  Now, compare... */
		dev_dbg(arizona->dev, "HPDET measured %d %d\n",
			info->hpdet_res[0], info->hpdet_res[1]);

		/* Take the headphone impedance for the main report */
		*reading = info->hpdet_res[0];

		/* Sometimes we get false readings due to slow insert */
		if (*reading >= ARIZONA_HPDET_MAX && !info->hpdet_retried) {
			dev_dbg(arizona->dev, "Retrying high impedance\n");
			info->num_hpdet_res = 0;
			info->hpdet_retried = true;
			arizona_start_hpdet_acc_id(info);
			pm_runtime_put(info->dev);
			return -EAGAIN;
		}

		/*
		 * If we measure the mic as high impedance
		 */
		if (!id_gpio || info->hpdet_res[1] > 50) {
			dev_dbg(arizona->dev, "Detected mic\n");
			*mic = true;
			info->detecting = true;
		} else {
			dev_dbg(arizona->dev, "Detected headphone\n");
		}

		/* Make sure everything is reset back to the real polarity */
		regmap_update_bits(arizona->regmap,
				   ARIZONA_ACCESSORY_DETECT_MODE_1,
				   ARIZONA_ACCDET_SRC,
				   info->micd_modes[0].src);
	}

	return 0;
}

static irqreturn_t arizona_hpdet_irq(int irq, void *data)
{
	struct arizona_extcon_info *info = data;
	struct arizona *arizona = info->arizona;
	int id_gpio = arizona->pdata.hpdet_id_gpio;
	unsigned int report = EXTCON_JACK_HEADPHONE;
	int ret, reading;
	bool mic = false;

	mutex_lock(&info->lock);

	/* If we got a spurious IRQ for some reason then ignore it */
	if (!info->hpdet_active) {
		dev_warn(arizona->dev, "Spurious HPDET IRQ\n");
		mutex_unlock(&info->lock);
		return IRQ_NONE;
	}

	/* If the cable was removed while measuring ignore the result */
	ret = extcon_get_state(info->edev, EXTCON_MECHANICAL);
	if (ret < 0) {
		dev_err(arizona->dev, "Failed to check cable state: %d\n",
			ret);
		goto out;
	} else if (!ret) {
		dev_dbg(arizona->dev, "Ignoring HPDET for removed cable\n");
		goto done;
	}

	ret = arizona_hpdet_read(info);
	if (ret == -EAGAIN)
		goto out;
	else if (ret < 0)
		goto done;
	reading = ret;

	/* Reset back to starting range */
	regmap_update_bits(arizona->regmap,
			   ARIZONA_HEADPHONE_DETECT_1,
			   ARIZONA_HP_IMPEDANCE_RANGE_MASK | ARIZONA_HP_POLL,
			   0);

	ret = arizona_hpdet_do_id(info, &reading, &mic);
	if (ret == -EAGAIN)
		goto out;
	else if (ret < 0)
		goto done;

	/* Report high impedence cables as line outputs */
	if (reading >= 5000)
		report = EXTCON_JACK_LINE_OUT;
	else
		report = EXTCON_JACK_HEADPHONE;

	ret = extcon_set_state_sync(info->edev, report, true);
	if (ret != 0)
		dev_err(arizona->dev, "Failed to report HP/line: %d\n",
			ret);

done:
	/* Reset back to starting range */
	regmap_update_bits(arizona->regmap,
			   ARIZONA_HEADPHONE_DETECT_1,
			   ARIZONA_HP_IMPEDANCE_RANGE_MASK | ARIZONA_HP_POLL,
			   0);

	arizona_extcon_hp_clamp(info, false);

	if (id_gpio)
		gpio_set_value_cansleep(id_gpio, 0);

	/* Revert back to MICDET mode */
	regmap_update_bits(arizona->regmap,
			   ARIZONA_ACCESSORY_DETECT_MODE_1,
			   ARIZONA_ACCDET_MODE_MASK, ARIZONA_ACCDET_MODE_MIC);

	/* If we have a mic then reenable MICDET */
	if (mic || info->mic)
		arizona_start_mic(info);

	if (info->hpdet_active) {
		pm_runtime_put_autosuspend(info->dev);
		info->hpdet_active = false;
	}

	info->hpdet_done = true;

out:
	mutex_unlock(&info->lock);

	return IRQ_HANDLED;
}

static void arizona_identify_headphone(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	int ret;

	if (info->hpdet_done)
		return;

	dev_dbg(arizona->dev, "Starting HPDET\n");

	/* Make sure we keep the device enabled during the measurement */
	pm_runtime_get(info->dev);

	info->hpdet_active = true;

	if (info->mic)
		arizona_stop_mic(info);

	arizona_extcon_hp_clamp(info, true);

	ret = regmap_update_bits(arizona->regmap,
				 ARIZONA_ACCESSORY_DETECT_MODE_1,
				 ARIZONA_ACCDET_MODE_MASK,
				 arizona->pdata.hpdet_channel);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to set HPDET mode: %d\n", ret);
		goto err;
	}

	ret = regmap_update_bits(arizona->regmap, ARIZONA_HEADPHONE_DETECT_1,
				 ARIZONA_HP_POLL, ARIZONA_HP_POLL);
	if (ret != 0) {
		dev_err(arizona->dev, "Can't start HPDETL measurement: %d\n",
			ret);
		goto err;
	}

	return;

err:
	regmap_update_bits(arizona->regmap, ARIZONA_ACCESSORY_DETECT_MODE_1,
			   ARIZONA_ACCDET_MODE_MASK, ARIZONA_ACCDET_MODE_MIC);

	/* Just report headphone */
	ret = extcon_set_state_sync(info->edev, EXTCON_JACK_HEADPHONE, true);
	if (ret != 0)
		dev_err(arizona->dev, "Failed to report headphone: %d\n", ret);

	if (info->mic)
		arizona_start_mic(info);

	info->hpdet_active = false;
}

static void arizona_start_hpdet_acc_id(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	int hp_reading = 32;
	bool mic;
	int ret;

	dev_dbg(arizona->dev, "Starting identification via HPDET\n");

	/* Make sure we keep the device enabled during the measurement */
	pm_runtime_get_sync(info->dev);

	info->hpdet_active = true;

	arizona_extcon_hp_clamp(info, true);

	ret = regmap_update_bits(arizona->regmap,
				 ARIZONA_ACCESSORY_DETECT_MODE_1,
				 ARIZONA_ACCDET_SRC | ARIZONA_ACCDET_MODE_MASK,
				 info->micd_modes[0].src |
				 arizona->pdata.hpdet_channel);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to set HPDET mode: %d\n", ret);
		goto err;
	}

	if (arizona->pdata.hpdet_acc_id_line) {
		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_HEADPHONE_DETECT_1,
					 ARIZONA_HP_POLL, ARIZONA_HP_POLL);
		if (ret != 0) {
			dev_err(arizona->dev,
				"Can't start HPDETL measurement: %d\n",
				ret);
			goto err;
		}
	} else {
		arizona_hpdet_do_id(info, &hp_reading, &mic);
	}

	return;

err:
	regmap_update_bits(arizona->regmap, ARIZONA_ACCESSORY_DETECT_MODE_1,
			   ARIZONA_ACCDET_MODE_MASK, ARIZONA_ACCDET_MODE_MIC);

	/* Just report headphone */
	ret = extcon_set_state_sync(info->edev, EXTCON_JACK_HEADPHONE, true);
	if (ret != 0)
		dev_err(arizona->dev, "Failed to report headphone: %d\n", ret);

	info->hpdet_active = false;
}

static void arizona_micd_timeout_work(struct work_struct *work)
{
	struct arizona_extcon_info *info = container_of(work,
						struct arizona_extcon_info,
						micd_timeout_work.work);

	mutex_lock(&info->lock);

	dev_dbg(info->arizona->dev, "MICD timed out, reporting HP\n");

	info->detecting = false;

	arizona_identify_headphone(info);

	arizona_stop_mic(info);

	mutex_unlock(&info->lock);
}

static void arizona_micd_detect(struct work_struct *work)
{
	struct arizona_extcon_info *info = container_of(work,
						struct arizona_extcon_info,
						micd_detect_work.work);
	struct arizona *arizona = info->arizona;
	unsigned int val = 0, lvl;
	int ret, i, key;

	cancel_delayed_work_sync(&info->micd_timeout_work);

	mutex_lock(&info->lock);

	/* If the cable was removed while measuring ignore the result */
	ret = extcon_get_state(info->edev, EXTCON_MECHANICAL);
	if (ret < 0) {
		dev_err(arizona->dev, "Failed to check cable state: %d\n",
				ret);
		mutex_unlock(&info->lock);
		return;
	} else if (!ret) {
		dev_dbg(arizona->dev, "Ignoring MICDET for removed cable\n");
		mutex_unlock(&info->lock);
		return;
	}

	if (info->detecting && arizona->pdata.micd_software_compare) {
		/* Must disable MICD before we read the ADCVAL */
		regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_1,
				   ARIZONA_MICD_ENA, 0);
		ret = regmap_read(arizona->regmap, ARIZONA_MIC_DETECT_4, &val);
		if (ret != 0) {
			dev_err(arizona->dev,
				"Failed to read MICDET_ADCVAL: %d\n",
				ret);
			mutex_unlock(&info->lock);
			return;
		}

		dev_dbg(arizona->dev, "MICDET_ADCVAL: %x\n", val);

		val &= ARIZONA_MICDET_ADCVAL_MASK;
		if (val < ARRAY_SIZE(arizona_micd_levels))
			val = arizona_micd_levels[val];
		else
			val = INT_MAX;

		if (val <= QUICK_HEADPHONE_MAX_OHM)
			val = ARIZONA_MICD_STS | ARIZONA_MICD_LVL_0;
		else if (val <= MICROPHONE_MIN_OHM)
			val = ARIZONA_MICD_STS | ARIZONA_MICD_LVL_1;
		else if (val <= MICROPHONE_MAX_OHM)
			val = ARIZONA_MICD_STS | ARIZONA_MICD_LVL_8;
		else
			val = ARIZONA_MICD_LVL_8;
	}

	for (i = 0; i < 10 && !(val & MICD_LVL_0_TO_8); i++) {
		ret = regmap_read(arizona->regmap, ARIZONA_MIC_DETECT_3, &val);
		if (ret != 0) {
			dev_err(arizona->dev,
				"Failed to read MICDET: %d\n", ret);
			mutex_unlock(&info->lock);
			return;
		}

		dev_dbg(arizona->dev, "MICDET: %x\n", val);

		if (!(val & ARIZONA_MICD_VALID)) {
			dev_warn(arizona->dev,
				 "Microphone detection state invalid\n");
			mutex_unlock(&info->lock);
			return;
		}
	}

	if (i == 10 && !(val & MICD_LVL_0_TO_8)) {
		dev_err(arizona->dev, "Failed to get valid MICDET value\n");
		mutex_unlock(&info->lock);
		return;
	}

	/* Due to jack detect this should never happen */
	if (!(val & ARIZONA_MICD_STS)) {
		dev_warn(arizona->dev, "Detected open circuit\n");
		info->mic = false;
		arizona_stop_mic(info);
		info->detecting = false;
		arizona_identify_headphone(info);
		goto handled;
	}

	/* If we got a high impedence we should have a headset, report it. */
	if (info->detecting && (val & ARIZONA_MICD_LVL_8)) {
		info->mic = true;
		info->detecting = false;

		arizona_identify_headphone(info);

		ret = extcon_set_state_sync(info->edev,
					      EXTCON_JACK_MICROPHONE, true);
		if (ret != 0)
			dev_err(arizona->dev, "Headset report failed: %d\n",
				ret);

		/* Don't need to regulate for button detection */
		ret = regulator_allow_bypass(info->micvdd, true);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to bypass MICVDD: %d\n",
				ret);
		}

		goto handled;
	}

	/* If we detected a lower impedence during initial startup
	 * then we probably have the wrong polarity, flip it.  Don't
	 * do this for the lowest impedences to speed up detection of
	 * plain headphones.  If both polarities report a low
	 * impedence then give up and report headphones.
	 */
	if (info->detecting && (val & MICD_LVL_1_TO_7)) {
		if (info->jack_flips >= info->micd_num_modes * 10) {
			dev_dbg(arizona->dev, "Detected HP/line\n");

			info->detecting = false;

			arizona_identify_headphone(info);

			arizona_stop_mic(info);
		} else {
			info->micd_mode++;
			if (info->micd_mode == info->micd_num_modes)
				info->micd_mode = 0;
			arizona_extcon_set_mode(info, info->micd_mode);

			info->jack_flips++;
		}

		goto handled;
	}

	/*
	 * If we're still detecting and we detect a short then we've
	 * got a headphone.  Otherwise it's a button press.
	 */
	if (val & MICD_LVL_0_TO_7) {
		if (info->mic) {
			dev_dbg(arizona->dev, "Mic button detected\n");

			lvl = val & ARIZONA_MICD_LVL_MASK;
			lvl >>= ARIZONA_MICD_LVL_SHIFT;

			for (i = 0; i < info->num_micd_ranges; i++)
				input_report_key(info->input,
						 info->micd_ranges[i].key, 0);

			WARN_ON(!lvl);
			WARN_ON(ffs(lvl) - 1 >= info->num_micd_ranges);
			if (lvl && ffs(lvl) - 1 < info->num_micd_ranges) {
				key = info->micd_ranges[ffs(lvl) - 1].key;
				input_report_key(info->input, key, 1);
				input_sync(info->input);
			}

		} else if (info->detecting) {
			dev_dbg(arizona->dev, "Headphone detected\n");
			info->detecting = false;
			arizona_stop_mic(info);

			arizona_identify_headphone(info);
		} else {
			dev_warn(arizona->dev, "Button with no mic: %x\n",
				 val);
		}
	} else {
		dev_dbg(arizona->dev, "Mic button released\n");
		for (i = 0; i < info->num_micd_ranges; i++)
			input_report_key(info->input,
					 info->micd_ranges[i].key, 0);
		input_sync(info->input);
		arizona_extcon_pulse_micbias(info);
	}

handled:
	if (info->detecting) {
		if (arizona->pdata.micd_software_compare)
			regmap_update_bits(arizona->regmap,
					   ARIZONA_MIC_DETECT_1,
					   ARIZONA_MICD_ENA,
					   ARIZONA_MICD_ENA);

		queue_delayed_work(system_power_efficient_wq,
				   &info->micd_timeout_work,
				   msecs_to_jiffies(info->micd_timeout));
	}

	pm_runtime_mark_last_busy(info->dev);
	mutex_unlock(&info->lock);
}

static irqreturn_t arizona_micdet(int irq, void *data)
{
	struct arizona_extcon_info *info = data;
	struct arizona *arizona = info->arizona;
	int debounce = arizona->pdata.micd_detect_debounce;

	cancel_delayed_work_sync(&info->micd_detect_work);
	cancel_delayed_work_sync(&info->micd_timeout_work);

	mutex_lock(&info->lock);
	if (!info->detecting)
		debounce = 0;
	mutex_unlock(&info->lock);

	if (debounce)
		queue_delayed_work(system_power_efficient_wq,
				   &info->micd_detect_work,
				   msecs_to_jiffies(debounce));
	else
		arizona_micd_detect(&info->micd_detect_work.work);

	return IRQ_HANDLED;
}

static void arizona_hpdet_work(struct work_struct *work)
{
	struct arizona_extcon_info *info = container_of(work,
						struct arizona_extcon_info,
						hpdet_work.work);

	mutex_lock(&info->lock);
	arizona_start_hpdet_acc_id(info);
	mutex_unlock(&info->lock);
}

static int arizona_hpdet_wait(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	unsigned int val;
	int i, ret;

	for (i = 0; i < ARIZONA_HPDET_WAIT_COUNT; i++) {
		ret = regmap_read(arizona->regmap, ARIZONA_HEADPHONE_DETECT_2,
				&val);
		if (ret) {
			dev_err(arizona->dev,
				"Failed to read HPDET state: %d\n", ret);
			return ret;
		}

		switch (info->hpdet_ip_version) {
		case 0:
			if (val & ARIZONA_HP_DONE)
				return 0;
			break;
		default:
			if (val & ARIZONA_HP_DONE_B)
				return 0;
			break;
		}

		msleep(ARIZONA_HPDET_WAIT_DELAY_MS);
	}

	dev_warn(arizona->dev, "HPDET did not appear to complete\n");

	return -ETIMEDOUT;
}

static irqreturn_t arizona_jackdet(int irq, void *data)
{
	struct arizona_extcon_info *info = data;
	struct arizona *arizona = info->arizona;
	unsigned int val, present, mask;
	bool cancelled_hp, cancelled_mic;
	int ret, i;

	cancelled_hp = cancel_delayed_work_sync(&info->hpdet_work);
	cancelled_mic = cancel_delayed_work_sync(&info->micd_timeout_work);

	pm_runtime_get_sync(info->dev);

	mutex_lock(&info->lock);

	if (info->micd_clamp) {
		mask = ARIZONA_MICD_CLAMP_STS;
		present = 0;
	} else {
		mask = ARIZONA_JD1_STS;
		if (arizona->pdata.jd_invert)
			present = 0;
		else
			present = ARIZONA_JD1_STS;
	}

	ret = regmap_read(arizona->regmap, ARIZONA_AOD_IRQ_RAW_STATUS, &val);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to read jackdet status: %d\n",
			ret);
		mutex_unlock(&info->lock);
		pm_runtime_put_autosuspend(info->dev);
		return IRQ_NONE;
	}

	val &= mask;
	if (val == info->last_jackdet) {
		dev_dbg(arizona->dev, "Suppressing duplicate JACKDET\n");
		if (cancelled_hp)
			queue_delayed_work(system_power_efficient_wq,
					   &info->hpdet_work,
					   msecs_to_jiffies(HPDET_DEBOUNCE));

		if (cancelled_mic) {
			int micd_timeout = info->micd_timeout;

			queue_delayed_work(system_power_efficient_wq,
					   &info->micd_timeout_work,
					   msecs_to_jiffies(micd_timeout));
		}

		goto out;
	}
	info->last_jackdet = val;

	if (info->last_jackdet == present) {
		dev_dbg(arizona->dev, "Detected jack\n");
		ret = extcon_set_state_sync(info->edev,
					      EXTCON_MECHANICAL, true);

		if (ret != 0)
			dev_err(arizona->dev, "Mechanical report failed: %d\n",
				ret);

		if (!arizona->pdata.hpdet_acc_id) {
			info->detecting = true;
			info->mic = false;
			info->jack_flips = 0;

			arizona_start_mic(info);
		} else {
			queue_delayed_work(system_power_efficient_wq,
					   &info->hpdet_work,
					   msecs_to_jiffies(HPDET_DEBOUNCE));
		}

		if (info->micd_clamp || !arizona->pdata.jd_invert)
			regmap_update_bits(arizona->regmap,
					   ARIZONA_JACK_DETECT_DEBOUNCE,
					   ARIZONA_MICD_CLAMP_DB |
					   ARIZONA_JD1_DB, 0);
	} else {
		dev_dbg(arizona->dev, "Detected jack removal\n");

		arizona_stop_mic(info);

		info->num_hpdet_res = 0;
		for (i = 0; i < ARRAY_SIZE(info->hpdet_res); i++)
			info->hpdet_res[i] = 0;
		info->mic = false;
		info->hpdet_done = false;
		info->hpdet_retried = false;

		for (i = 0; i < info->num_micd_ranges; i++)
			input_report_key(info->input,
					 info->micd_ranges[i].key, 0);
		input_sync(info->input);

		for (i = 0; i < ARRAY_SIZE(arizona_cable) - 1; i++) {
			ret = extcon_set_state_sync(info->edev,
					arizona_cable[i], false);
			if (ret != 0)
				dev_err(arizona->dev,
					"Removal report failed: %d\n", ret);
		}

		/*
		 * If the jack was removed during a headphone detection we
		 * need to wait for the headphone detection to finish, as
		 * it can not be aborted. We don't want to be able to start
		 * a new headphone detection from a fresh insert until this
		 * one is finished.
		 */
		arizona_hpdet_wait(info);

		regmap_update_bits(arizona->regmap,
				   ARIZONA_JACK_DETECT_DEBOUNCE,
				   ARIZONA_MICD_CLAMP_DB | ARIZONA_JD1_DB,
				   ARIZONA_MICD_CLAMP_DB | ARIZONA_JD1_DB);
	}

	if (arizona->pdata.micd_timeout)
		info->micd_timeout = arizona->pdata.micd_timeout;
	else
		info->micd_timeout = DEFAULT_MICD_TIMEOUT;

out:
	/* Clear trig_sts to make sure DCVDD is not forced up */
	regmap_write(arizona->regmap, ARIZONA_AOD_WKUP_AND_TRIG,
		     ARIZONA_MICD_CLAMP_FALL_TRIG_STS |
		     ARIZONA_MICD_CLAMP_RISE_TRIG_STS |
		     ARIZONA_JD1_FALL_TRIG_STS |
		     ARIZONA_JD1_RISE_TRIG_STS);

	mutex_unlock(&info->lock);

	pm_runtime_mark_last_busy(info->dev);
	pm_runtime_put_autosuspend(info->dev);

	return IRQ_HANDLED;
}

/* Map a level onto a slot in the register bank */
static void arizona_micd_set_level(struct arizona *arizona, int index,
				   unsigned int level)
{
	int reg;
	unsigned int mask;

	reg = ARIZONA_MIC_DETECT_LEVEL_4 - (index / 2);

	if (!(index % 2)) {
		mask = 0x3f00;
		level <<= 8;
	} else {
		mask = 0x3f;
	}

	/* Program the level itself */
	regmap_update_bits(arizona->regmap, reg, mask, level);
}

static int arizona_extcon_get_micd_configs(struct device *dev,
					   struct arizona *arizona)
{
	const char * const prop = "wlf,micd-configs";
	const int entries_per_config = 3;
	struct arizona_micd_config *micd_configs;
	int nconfs, ret;
	int i, j;
	u32 *vals;

	nconfs = device_property_read_u32_array(arizona->dev, prop, NULL, 0);
	if (nconfs <= 0)
		return 0;

	vals = kcalloc(nconfs, sizeof(u32), GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	ret = device_property_read_u32_array(arizona->dev, prop, vals, nconfs);
	if (ret < 0)
		goto out;

	nconfs /= entries_per_config;
	micd_configs = devm_kcalloc(dev, nconfs, sizeof(*micd_configs),
				    GFP_KERNEL);
	if (!micd_configs) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0, j = 0; i < nconfs; ++i) {
		micd_configs[i].src = vals[j++] ? ARIZONA_ACCDET_SRC : 0;
		micd_configs[i].bias = vals[j++];
		micd_configs[i].gpio = vals[j++];
	}

	arizona->pdata.micd_configs = micd_configs;
	arizona->pdata.num_micd_configs = nconfs;

out:
	kfree(vals);
	return ret;
}

static int arizona_extcon_device_get_pdata(struct device *dev,
					   struct arizona *arizona)
{
	struct arizona_pdata *pdata = &arizona->pdata;
	unsigned int val = ARIZONA_ACCDET_MODE_HPL;
	int ret;

	device_property_read_u32(arizona->dev, "wlf,hpdet-channel", &val);
	switch (val) {
	case ARIZONA_ACCDET_MODE_HPL:
	case ARIZONA_ACCDET_MODE_HPR:
		pdata->hpdet_channel = val;
		break;
	default:
		dev_err(arizona->dev,
			"Wrong wlf,hpdet-channel DT value %d\n", val);
		pdata->hpdet_channel = ARIZONA_ACCDET_MODE_HPL;
	}

	device_property_read_u32(arizona->dev, "wlf,micd-detect-debounce",
				 &pdata->micd_detect_debounce);

	device_property_read_u32(arizona->dev, "wlf,micd-bias-start-time",
				 &pdata->micd_bias_start_time);

	device_property_read_u32(arizona->dev, "wlf,micd-rate",
				 &pdata->micd_rate);

	device_property_read_u32(arizona->dev, "wlf,micd-dbtime",
				 &pdata->micd_dbtime);

	device_property_read_u32(arizona->dev, "wlf,micd-timeout-ms",
				 &pdata->micd_timeout);

	pdata->micd_force_micbias = device_property_read_bool(arizona->dev,
						"wlf,micd-force-micbias");

	pdata->micd_software_compare = device_property_read_bool(arizona->dev,
						"wlf,micd-software-compare");

	pdata->jd_invert = device_property_read_bool(arizona->dev,
						     "wlf,jd-invert");

	device_property_read_u32(arizona->dev, "wlf,gpsw", &pdata->gpsw);

	pdata->jd_gpio5 = device_property_read_bool(arizona->dev,
						    "wlf,use-jd2");
	pdata->jd_gpio5_nopull = device_property_read_bool(arizona->dev,
						"wlf,use-jd2-nopull");

	ret = arizona_extcon_get_micd_configs(dev, arizona);
	if (ret < 0)
		dev_err(arizona->dev, "Failed to read micd configs: %d\n", ret);

	return 0;
}

static int arizona_extcon_probe(struct platform_device *pdev)
{
	struct arizona *arizona = dev_get_drvdata(pdev->dev.parent);
	struct arizona_pdata *pdata = &arizona->pdata;
	struct arizona_extcon_info *info;
	unsigned int val;
	unsigned int clamp_mode;
	int jack_irq_fall, jack_irq_rise;
	int ret, mode, i, j;

	if (!arizona->dapm || !arizona->dapm->card)
		return -EPROBE_DEFER;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!dev_get_platdata(arizona->dev))
		arizona_extcon_device_get_pdata(&pdev->dev, arizona);

	info->micvdd = devm_regulator_get(&pdev->dev, "MICVDD");
	if (IS_ERR(info->micvdd)) {
		ret = PTR_ERR(info->micvdd);
		dev_err(arizona->dev, "Failed to get MICVDD: %d\n", ret);
		return ret;
	}

	mutex_init(&info->lock);
	info->arizona = arizona;
	info->dev = &pdev->dev;
	info->last_jackdet = ~(ARIZONA_MICD_CLAMP_STS | ARIZONA_JD1_STS);
	INIT_DELAYED_WORK(&info->hpdet_work, arizona_hpdet_work);
	INIT_DELAYED_WORK(&info->micd_detect_work, arizona_micd_detect);
	INIT_DELAYED_WORK(&info->micd_timeout_work, arizona_micd_timeout_work);
	platform_set_drvdata(pdev, info);

	switch (arizona->type) {
	case WM5102:
		switch (arizona->rev) {
		case 0:
			info->micd_reva = true;
			break;
		default:
			info->micd_clamp = true;
			info->hpdet_ip_version = 1;
			break;
		}
		break;
	case WM5110:
	case WM8280:
		switch (arizona->rev) {
		case 0 ... 2:
			break;
		default:
			info->micd_clamp = true;
			info->hpdet_ip_version = 2;
			break;
		}
		break;
	case WM8998:
	case WM1814:
		info->micd_clamp = true;
		info->hpdet_ip_version = 2;
		break;
	default:
		break;
	}

	info->edev = devm_extcon_dev_allocate(&pdev->dev, arizona_cable);
	if (IS_ERR(info->edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(&pdev->dev, info->edev);
	if (ret < 0) {
		dev_err(arizona->dev, "extcon_dev_register() failed: %d\n",
			ret);
		return ret;
	}

	info->input = devm_input_allocate_device(&pdev->dev);
	if (!info->input) {
		dev_err(arizona->dev, "Can't allocate input dev\n");
		ret = -ENOMEM;
		goto err_register;
	}

	info->input->name = "Headset";
	info->input->phys = "arizona/extcon";

	if (pdata->num_micd_configs) {
		info->micd_modes = pdata->micd_configs;
		info->micd_num_modes = pdata->num_micd_configs;
	} else {
		info->micd_modes = micd_default_modes;
		info->micd_num_modes = ARRAY_SIZE(micd_default_modes);
	}

	if (arizona->pdata.gpsw > 0)
		regmap_update_bits(arizona->regmap, ARIZONA_GP_SWITCH_1,
				ARIZONA_SW1_MODE_MASK, arizona->pdata.gpsw);

	if (pdata->micd_pol_gpio > 0) {
		if (info->micd_modes[0].gpio)
			mode = GPIOF_OUT_INIT_HIGH;
		else
			mode = GPIOF_OUT_INIT_LOW;

		ret = devm_gpio_request_one(&pdev->dev, pdata->micd_pol_gpio,
					    mode, "MICD polarity");
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to request GPIO%d: %d\n",
				pdata->micd_pol_gpio, ret);
			goto err_register;
		}

		info->micd_pol_gpio = gpio_to_desc(pdata->micd_pol_gpio);
	} else {
		if (info->micd_modes[0].gpio)
			mode = GPIOD_OUT_HIGH;
		else
			mode = GPIOD_OUT_LOW;

		/* We can't use devm here because we need to do the get
		 * against the MFD device, as that is where the of_node
		 * will reside, but if we devm against that the GPIO
		 * will not be freed if the extcon driver is unloaded.
		 */
		info->micd_pol_gpio = gpiod_get_optional(arizona->dev,
							 "wlf,micd-pol",
							 GPIOD_OUT_LOW);
		if (IS_ERR(info->micd_pol_gpio)) {
			ret = PTR_ERR(info->micd_pol_gpio);
			dev_err(arizona->dev,
				"Failed to get microphone polarity GPIO: %d\n",
				ret);
			goto err_register;
		}
	}

	if (arizona->pdata.hpdet_id_gpio > 0) {
		ret = devm_gpio_request_one(&pdev->dev,
					    arizona->pdata.hpdet_id_gpio,
					    GPIOF_OUT_INIT_LOW,
					    "HPDET");
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to request GPIO%d: %d\n",
				arizona->pdata.hpdet_id_gpio, ret);
			goto err_gpio;
		}
	}

	if (arizona->pdata.micd_bias_start_time)
		regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_1,
				   ARIZONA_MICD_BIAS_STARTTIME_MASK,
				   arizona->pdata.micd_bias_start_time
				   << ARIZONA_MICD_BIAS_STARTTIME_SHIFT);

	if (arizona->pdata.micd_rate)
		regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_1,
				   ARIZONA_MICD_RATE_MASK,
				   arizona->pdata.micd_rate
				   << ARIZONA_MICD_RATE_SHIFT);

	switch (arizona->pdata.micd_dbtime) {
	case MICD_DBTIME_FOUR_READINGS:
		regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_1,
				   ARIZONA_MICD_DBTIME_MASK,
				   ARIZONA_MICD_DBTIME);
		break;
	case MICD_DBTIME_TWO_READINGS:
		regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_1,
				   ARIZONA_MICD_DBTIME_MASK, 0);
		break;
	default:
		break;
	}

	BUILD_BUG_ON(ARRAY_SIZE(arizona_micd_levels) <
		     ARIZONA_NUM_MICD_BUTTON_LEVELS);

	if (arizona->pdata.num_micd_ranges) {
		info->micd_ranges = pdata->micd_ranges;
		info->num_micd_ranges = pdata->num_micd_ranges;
	} else {
		info->micd_ranges = micd_default_ranges;
		info->num_micd_ranges = ARRAY_SIZE(micd_default_ranges);
	}

	if (arizona->pdata.num_micd_ranges > ARIZONA_MAX_MICD_RANGE) {
		dev_err(arizona->dev, "Too many MICD ranges: %d\n",
			arizona->pdata.num_micd_ranges);
	}

	if (info->num_micd_ranges > 1) {
		for (i = 1; i < info->num_micd_ranges; i++) {
			if (info->micd_ranges[i - 1].max >
			    info->micd_ranges[i].max) {
				dev_err(arizona->dev,
					"MICD ranges must be sorted\n");
				ret = -EINVAL;
				goto err_gpio;
			}
		}
	}

	/* Disable all buttons by default */
	regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_2,
			   ARIZONA_MICD_LVL_SEL_MASK, 0x81);

	/* Set up all the buttons the user specified */
	for (i = 0; i < info->num_micd_ranges; i++) {
		for (j = 0; j < ARIZONA_NUM_MICD_BUTTON_LEVELS; j++)
			if (arizona_micd_levels[j] >= info->micd_ranges[i].max)
				break;

		if (j == ARIZONA_NUM_MICD_BUTTON_LEVELS) {
			dev_err(arizona->dev, "Unsupported MICD level %d\n",
				info->micd_ranges[i].max);
			ret = -EINVAL;
			goto err_gpio;
		}

		dev_dbg(arizona->dev, "%d ohms for MICD threshold %d\n",
			arizona_micd_levels[j], i);

		arizona_micd_set_level(arizona, i, j);
		input_set_capability(info->input, EV_KEY,
				     info->micd_ranges[i].key);

		/* Enable reporting of that range */
		regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_2,
				   1 << i, 1 << i);
	}

	/* Set all the remaining keys to a maximum */
	for (; i < ARIZONA_MAX_MICD_RANGE; i++)
		arizona_micd_set_level(arizona, i, 0x3f);

	/*
	 * If we have a clamp use it, activating in conjunction with
	 * GPIO5 if that is connected for jack detect operation.
	 */
	if (info->micd_clamp) {
		if (arizona->pdata.jd_gpio5) {
			/* Put the GPIO into input mode with optional pull */
			val = 0xc101;
			if (arizona->pdata.jd_gpio5_nopull)
				val &= ~ARIZONA_GPN_PU;

			regmap_write(arizona->regmap, ARIZONA_GPIO5_CTRL,
				     val);

			if (arizona->pdata.jd_invert)
				clamp_mode = ARIZONA_MICD_CLAMP_MODE_JDH_GP5H;
			else
				clamp_mode = ARIZONA_MICD_CLAMP_MODE_JDL_GP5H;
		} else {
			if (arizona->pdata.jd_invert)
				clamp_mode = ARIZONA_MICD_CLAMP_MODE_JDH;
			else
				clamp_mode = ARIZONA_MICD_CLAMP_MODE_JDL;
		}

		regmap_update_bits(arizona->regmap,
				   ARIZONA_MICD_CLAMP_CONTROL,
				   ARIZONA_MICD_CLAMP_MODE_MASK, clamp_mode);

		regmap_update_bits(arizona->regmap,
				   ARIZONA_JACK_DETECT_DEBOUNCE,
				   ARIZONA_MICD_CLAMP_DB,
				   ARIZONA_MICD_CLAMP_DB);
	}

	arizona_extcon_set_mode(info, 0);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_idle(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	if (info->micd_clamp) {
		jack_irq_rise = ARIZONA_IRQ_MICD_CLAMP_RISE;
		jack_irq_fall = ARIZONA_IRQ_MICD_CLAMP_FALL;
	} else {
		jack_irq_rise = ARIZONA_IRQ_JD_RISE;
		jack_irq_fall = ARIZONA_IRQ_JD_FALL;
	}

	ret = arizona_request_irq(arizona, jack_irq_rise,
				  "JACKDET rise", arizona_jackdet, info);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to get JACKDET rise IRQ: %d\n",
			ret);
		goto err_gpio;
	}

	ret = arizona_set_irq_wake(arizona, jack_irq_rise, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to set JD rise IRQ wake: %d\n",
			ret);
		goto err_rise;
	}

	ret = arizona_request_irq(arizona, jack_irq_fall,
				  "JACKDET fall", arizona_jackdet, info);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to get JD fall IRQ: %d\n", ret);
		goto err_rise_wake;
	}

	ret = arizona_set_irq_wake(arizona, jack_irq_fall, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to set JD fall IRQ wake: %d\n",
			ret);
		goto err_fall;
	}

	ret = arizona_request_irq(arizona, ARIZONA_IRQ_MICDET,
				  "MICDET", arizona_micdet, info);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to get MICDET IRQ: %d\n", ret);
		goto err_fall_wake;
	}

	ret = arizona_request_irq(arizona, ARIZONA_IRQ_HPDET,
				  "HPDET", arizona_hpdet_irq, info);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to get HPDET IRQ: %d\n", ret);
		goto err_micdet;
	}

	arizona_clk32k_enable(arizona);
	regmap_update_bits(arizona->regmap, ARIZONA_JACK_DETECT_DEBOUNCE,
			   ARIZONA_JD1_DB, ARIZONA_JD1_DB);
	regmap_update_bits(arizona->regmap, ARIZONA_JACK_DETECT_ANALOGUE,
			   ARIZONA_JD1_ENA, ARIZONA_JD1_ENA);

	ret = regulator_allow_bypass(info->micvdd, true);
	if (ret != 0)
		dev_warn(arizona->dev, "Failed to set MICVDD to bypass: %d\n",
			 ret);

	pm_runtime_put(&pdev->dev);

	ret = input_register_device(info->input);
	if (ret) {
		dev_err(&pdev->dev, "Can't register input device: %d\n", ret);
		goto err_hpdet;
	}

	return 0;

err_hpdet:
	arizona_free_irq(arizona, ARIZONA_IRQ_HPDET, info);
err_micdet:
	arizona_free_irq(arizona, ARIZONA_IRQ_MICDET, info);
err_fall_wake:
	arizona_set_irq_wake(arizona, jack_irq_fall, 0);
err_fall:
	arizona_free_irq(arizona, jack_irq_fall, info);
err_rise_wake:
	arizona_set_irq_wake(arizona, jack_irq_rise, 0);
err_rise:
	arizona_free_irq(arizona, jack_irq_rise, info);
err_gpio:
	gpiod_put(info->micd_pol_gpio);
err_register:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int arizona_extcon_remove(struct platform_device *pdev)
{
	struct arizona_extcon_info *info = platform_get_drvdata(pdev);
	struct arizona *arizona = info->arizona;
	int jack_irq_rise, jack_irq_fall;

	gpiod_put(info->micd_pol_gpio);

	pm_runtime_disable(&pdev->dev);

	regmap_update_bits(arizona->regmap,
			   ARIZONA_MICD_CLAMP_CONTROL,
			   ARIZONA_MICD_CLAMP_MODE_MASK, 0);

	if (info->micd_clamp) {
		jack_irq_rise = ARIZONA_IRQ_MICD_CLAMP_RISE;
		jack_irq_fall = ARIZONA_IRQ_MICD_CLAMP_FALL;
	} else {
		jack_irq_rise = ARIZONA_IRQ_JD_RISE;
		jack_irq_fall = ARIZONA_IRQ_JD_FALL;
	}

	arizona_set_irq_wake(arizona, jack_irq_rise, 0);
	arizona_set_irq_wake(arizona, jack_irq_fall, 0);
	arizona_free_irq(arizona, ARIZONA_IRQ_HPDET, info);
	arizona_free_irq(arizona, ARIZONA_IRQ_MICDET, info);
	arizona_free_irq(arizona, jack_irq_rise, info);
	arizona_free_irq(arizona, jack_irq_fall, info);
	cancel_delayed_work_sync(&info->hpdet_work);
	regmap_update_bits(arizona->regmap, ARIZONA_JACK_DETECT_ANALOGUE,
			   ARIZONA_JD1_ENA, 0);
	arizona_clk32k_disable(arizona);

	return 0;
}

static struct platform_driver arizona_extcon_driver = {
	.driver		= {
		.name	= "arizona-extcon",
	},
	.probe		= arizona_extcon_probe,
	.remove		= arizona_extcon_remove,
};

module_platform_driver(arizona_extcon_driver);

MODULE_DESCRIPTION("Arizona Extcon driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:extcon-arizona");
