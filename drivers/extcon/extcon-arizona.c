/*
 * extcon-arizona.c - Extcon driver Wolfson Arizona devices
 *
 *  Copyright (C) 2012 Wolfson Microelectronics plc
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
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/extcon.h>

#include <sound/soc.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/pdata.h>
#include <linux/mfd/arizona/registers.h>

#define ARIZONA_NUM_BUTTONS 6

#define ARIZONA_ACCDET_MODE_MIC 0
#define ARIZONA_ACCDET_MODE_HPL 1
#define ARIZONA_ACCDET_MODE_HPR 2

struct arizona_extcon_info {
	struct device *dev;
	struct arizona *arizona;
	struct mutex lock;
	struct regulator *micvdd;
	struct input_dev *input;

	int micd_mode;
	const struct arizona_micd_config *micd_modes;
	int micd_num_modes;

	bool micd_reva;
	bool micd_clamp;

	struct delayed_work hpdet_work;

	bool hpdet_active;
	bool hpdet_done;

	int num_hpdet_res;
	unsigned int hpdet_res[3];

	bool mic;
	bool detecting;
	int jack_flips;

	int hpdet_ip;

	struct extcon_dev edev;
};

static const struct arizona_micd_config micd_default_modes[] = {
	{ 0,                  2 << ARIZONA_MICD_BIAS_SRC_SHIFT, 1 },
	{ ARIZONA_ACCDET_SRC, 1 << ARIZONA_MICD_BIAS_SRC_SHIFT, 0 },
};

static struct {
	u16 status;
	int report;
} arizona_lvl_to_key[ARIZONA_NUM_BUTTONS] = {
	{  0x1, BTN_0 },
	{  0x2, BTN_1 },
	{  0x4, BTN_2 },
	{  0x8, BTN_3 },
	{ 0x10, BTN_4 },
	{ 0x20, BTN_5 },
};

#define ARIZONA_CABLE_MECHANICAL 0
#define ARIZONA_CABLE_MICROPHONE 1
#define ARIZONA_CABLE_HEADPHONE  2
#define ARIZONA_CABLE_LINEOUT    3

static const char *arizona_cable[] = {
	"Mechanical",
	"Microphone",
	"Headphone",
	"Line-out",
	NULL,
};

static void arizona_extcon_do_magic(struct arizona_extcon_info *info,
				    unsigned int magic)
{
	struct arizona *arizona = info->arizona;
	int ret;

	mutex_lock(&arizona->dapm->card->dapm_mutex);

	arizona->hpdet_magic = magic;

	/* Keep the HP output stages disabled while doing the magic */
	if (magic) {
		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_OUTPUT_ENABLES_1,
					 ARIZONA_OUT1L_ENA |
					 ARIZONA_OUT1R_ENA, 0);
		if (ret != 0)
			dev_warn(arizona->dev,
				"Failed to disable headphone outputs: %d\n",
				 ret);
	}

	ret = regmap_update_bits(arizona->regmap, 0x225, 0x4000,
				 magic);
	if (ret != 0)
		dev_warn(arizona->dev, "Failed to do magic: %d\n",
				 ret);

	ret = regmap_update_bits(arizona->regmap, 0x226, 0x4000,
				 magic);
	if (ret != 0)
		dev_warn(arizona->dev, "Failed to do magic: %d\n",
			 ret);

	/* Restore the desired state while not doing the magic */
	if (!magic) {
		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_OUTPUT_ENABLES_1,
					 ARIZONA_OUT1L_ENA |
					 ARIZONA_OUT1R_ENA, arizona->hp_ena);
		if (ret != 0)
			dev_warn(arizona->dev,
				 "Failed to restore headphone outputs: %d\n",
				 ret);
	}

	mutex_unlock(&arizona->dapm->card->dapm_mutex);
}

static void arizona_extcon_set_mode(struct arizona_extcon_info *info, int mode)
{
	struct arizona *arizona = info->arizona;

	if (arizona->pdata.micd_pol_gpio > 0)
		gpio_set_value_cansleep(arizona->pdata.micd_pol_gpio,
					info->micd_modes[mode].gpio);
	regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_1,
			   ARIZONA_MICD_BIAS_SRC_MASK,
			   info->micd_modes[mode].bias);
	regmap_update_bits(arizona->regmap, ARIZONA_ACCESSORY_DETECT_MODE_1,
			   ARIZONA_ACCDET_SRC, info->micd_modes[mode].src);

	info->micd_mode = mode;

	dev_dbg(arizona->dev, "Set jack polarity to %d\n", mode);
}

static const char *arizona_extcon_get_micbias(struct arizona_extcon_info *info)
{
	switch (info->micd_modes[0].bias >> ARIZONA_MICD_BIAS_SRC_SHIFT) {
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
	int ret;

	mutex_lock(&dapm->card->dapm_mutex);

	ret = snd_soc_dapm_force_enable_pin(dapm, widget);
	if (ret != 0)
		dev_warn(arizona->dev, "Failed to enable %s: %d\n",
			 widget, ret);

	mutex_unlock(&dapm->card->dapm_mutex);

	snd_soc_dapm_sync(dapm);

	if (!arizona->pdata.micd_force_micbias) {
		mutex_lock(&dapm->card->dapm_mutex);

		ret = snd_soc_dapm_disable_pin(arizona->dapm, widget);
		if (ret != 0)
			dev_warn(arizona->dev, "Failed to disable %s: %d\n",
				 widget, ret);

		mutex_unlock(&dapm->card->dapm_mutex);

		snd_soc_dapm_sync(dapm);
	}
}

static void arizona_start_mic(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	bool change;
	int ret;

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

	regmap_update_bits(arizona->regmap,
			   ARIZONA_ACCESSORY_DETECT_MODE_1,
			   ARIZONA_ACCDET_MODE_MASK, ARIZONA_ACCDET_MODE_MIC);

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
	bool change;
	int ret;

	regmap_update_bits_check(arizona->regmap, ARIZONA_MIC_DETECT_1,
				 ARIZONA_MICD_ENA, 0,
				 &change);

	mutex_lock(&dapm->card->dapm_mutex);

	ret = snd_soc_dapm_disable_pin(dapm, widget);
	if (ret != 0)
		dev_warn(arizona->dev,
			 "Failed to disable %s: %d\n",
			 widget, ret);

	mutex_unlock(&dapm->card->dapm_mutex);

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
	unsigned int factor_a;
	unsigned int factor_b;
} arizona_hpdet_b_ranges[] = {
	{  5528,   362464 },
	{ 11084,  6186851 },
	{ 11065, 65460395 },
};

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

	switch (info->hpdet_ip) {
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
		    (val < 100 || val > 0x3fb)) {
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
		if (val < 100 || val > 0x3fb) {
			dev_dbg(arizona->dev, "Measurement out of range\n");
			return 10000;
		}

		dev_dbg(arizona->dev, "HPDET read %d in range %d\n",
			val, range);

		val = arizona_hpdet_b_ranges[range].factor_b
			/ ((val * 100) -
			   arizona_hpdet_b_ranges[range].factor_a);
		break;

	default:
		dev_warn(arizona->dev, "Unknown HPDET IP revision %d\n",
			 info->hpdet_ip);
	case 2:
		if (!(val & ARIZONA_HP_DONE_B)) {
			dev_err(arizona->dev, "HPDET did not complete: %x\n",
				val);
			return -EAGAIN;
		}

		val &= ARIZONA_HP_LVL_B_MASK;

		regmap_read(arizona->regmap, ARIZONA_HEADPHONE_DETECT_1,
			    &range);
		range = (range & ARIZONA_HP_IMPEDANCE_RANGE_MASK)
			   >> ARIZONA_HP_IMPEDANCE_RANGE_SHIFT;

		/* Skip up or down a range? */
		if (range && (val < arizona_hpdet_c_ranges[range].min)) {
			range--;
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
	}

	dev_dbg(arizona->dev, "HP impedance %d ohms\n", val);
	return val;
}

static int arizona_hpdet_do_id(struct arizona_extcon_info *info, int *reading)
{
	struct arizona *arizona = info->arizona;
	int id_gpio = arizona->pdata.hpdet_id_gpio;

	/*
	 * If we're using HPDET for accessory identification we need
	 * to take multiple measurements, step through them in sequence.
	 */
	if (arizona->pdata.hpdet_acc_id) {
		info->hpdet_res[info->num_hpdet_res++] = *reading;

		/*
		 * If the impedence is too high don't measure the
		 * second ground.
		 */
		if (info->num_hpdet_res == 1 && *reading >= 45) {
			dev_dbg(arizona->dev, "Skipping ground flip\n");
			info->hpdet_res[info->num_hpdet_res++] = *reading;
		}

		if (info->num_hpdet_res == 1) {
			dev_dbg(arizona->dev, "Flipping ground\n");

			regmap_update_bits(arizona->regmap,
					   ARIZONA_ACCESSORY_DETECT_MODE_1,
					   ARIZONA_ACCDET_SRC,
					   ~info->micd_modes[0].src);

			regmap_update_bits(arizona->regmap,
					   ARIZONA_HEADPHONE_DETECT_1,
					   ARIZONA_HP_POLL, ARIZONA_HP_POLL);
			return -EAGAIN;
		}

		/* Only check the mic directly if we didn't already ID it */
		if (id_gpio && info->num_hpdet_res == 2 &&
		    !((info->hpdet_res[0] > info->hpdet_res[1] * 2))) {
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
		dev_dbg(arizona->dev, "HPDET measured %d %d %d\n",
			info->hpdet_res[0], info->hpdet_res[1],
			info->hpdet_res[2]);


		/* Take the headphone impedance for the main report */
		*reading = info->hpdet_res[0];

		/*
		 * Either the two grounds measure differently or we
		 * measure the mic as high impedance.
		 */
		if ((info->hpdet_res[0] > info->hpdet_res[1] * 2) ||
		    (id_gpio && info->hpdet_res[2] > 10)) {
			dev_dbg(arizona->dev, "Detected mic\n");
			info->mic = true;
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
	int report = ARIZONA_CABLE_HEADPHONE;
	int ret, reading;

	mutex_lock(&info->lock);

	/* If we got a spurious IRQ for some reason then ignore it */
	if (!info->hpdet_active) {
		dev_warn(arizona->dev, "Spurious HPDET IRQ\n");
		mutex_unlock(&info->lock);
		return IRQ_NONE;
	}

	/* If the cable was removed while measuring ignore the result */
	ret = extcon_get_cable_state_(&info->edev, ARIZONA_CABLE_MECHANICAL);
	if (ret < 0) {
		dev_err(arizona->dev, "Failed to check cable state: %d\n",
			ret);
		goto out;
	} else if (!ret) {
		dev_dbg(arizona->dev, "Ignoring HPDET for removed cable\n");
		goto done;
	}

	ret = arizona_hpdet_read(info);
	if (ret == -EAGAIN) {
		goto out;
	} else if (ret < 0) {
		goto done;
	}
	reading = ret;

	/* Reset back to starting range */
	regmap_update_bits(arizona->regmap,
			   ARIZONA_HEADPHONE_DETECT_1,
			   ARIZONA_HP_IMPEDANCE_RANGE_MASK | ARIZONA_HP_POLL,
			   0);

	ret = arizona_hpdet_do_id(info, &reading);
	if (ret == -EAGAIN) {
		goto out;
	} else if (ret < 0) {
		goto done;
	}

	/* Report high impedence cables as line outputs */
	if (reading >= 5000)
		report = ARIZONA_CABLE_LINEOUT;
	else
		report = ARIZONA_CABLE_HEADPHONE;

	ret = extcon_set_cable_state_(&info->edev, report, true);
	if (ret != 0)
		dev_err(arizona->dev, "Failed to report HP/line: %d\n",
			ret);

	arizona_extcon_do_magic(info, 0);

done:
	if (id_gpio)
		gpio_set_value_cansleep(id_gpio, 0);

	/* Revert back to MICDET mode */
	regmap_update_bits(arizona->regmap,
			   ARIZONA_ACCESSORY_DETECT_MODE_1,
			   ARIZONA_ACCDET_MODE_MASK, ARIZONA_ACCDET_MODE_MIC);

	/* If we have a mic then reenable MICDET */
	if (info->mic)
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

	arizona_extcon_do_magic(info, 0x4000);

	ret = regmap_update_bits(arizona->regmap,
				 ARIZONA_ACCESSORY_DETECT_MODE_1,
				 ARIZONA_ACCDET_MODE_MASK,
				 ARIZONA_ACCDET_MODE_HPL);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to set HPDETL mode: %d\n", ret);
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
	ret = extcon_update_state(&info->edev,
				  1 << ARIZONA_CABLE_HEADPHONE,
				  1 << ARIZONA_CABLE_HEADPHONE);
	if (ret != 0)
		dev_err(arizona->dev, "Failed to report headphone: %d\n", ret);

	if (info->mic)
		arizona_start_mic(info);

	info->hpdet_active = false;
}

static void arizona_start_hpdet_acc_id(struct arizona_extcon_info *info)
{
	struct arizona *arizona = info->arizona;
	int ret;

	dev_dbg(arizona->dev, "Starting identification via HPDET\n");

	/* Make sure we keep the device enabled during the measurement */
	pm_runtime_get_sync(info->dev);

	info->hpdet_active = true;

	arizona_extcon_pulse_micbias(info);

	arizona_extcon_do_magic(info, 0x4000);

	ret = regmap_update_bits(arizona->regmap,
				 ARIZONA_ACCESSORY_DETECT_MODE_1,
				 ARIZONA_ACCDET_SRC | ARIZONA_ACCDET_MODE_MASK,
				 info->micd_modes[0].src |
				 ARIZONA_ACCDET_MODE_HPL);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to set HPDETL mode: %d\n", ret);
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
	ret = extcon_update_state(&info->edev,
				  1 << ARIZONA_CABLE_HEADPHONE,
				  1 << ARIZONA_CABLE_HEADPHONE);
	if (ret != 0)
		dev_err(arizona->dev, "Failed to report headphone: %d\n", ret);

	info->hpdet_active = false;
}

static irqreturn_t arizona_micdet(int irq, void *data)
{
	struct arizona_extcon_info *info = data;
	struct arizona *arizona = info->arizona;
	unsigned int val, lvl;
	int ret, i;

	mutex_lock(&info->lock);

	ret = regmap_read(arizona->regmap, ARIZONA_MIC_DETECT_3, &val);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to read MICDET: %d\n", ret);
		mutex_unlock(&info->lock);
		return IRQ_NONE;
	}

	dev_dbg(arizona->dev, "MICDET: %x\n", val);

	if (!(val & ARIZONA_MICD_VALID)) {
		dev_warn(arizona->dev, "Microphone detection state invalid\n");
		mutex_unlock(&info->lock);
		return IRQ_NONE;
	}

	/* Due to jack detect this should never happen */
	if (!(val & ARIZONA_MICD_STS)) {
		dev_warn(arizona->dev, "Detected open circuit\n");
		info->detecting = false;
		goto handled;
	}

	/* If we got a high impedence we should have a headset, report it. */
	if (info->detecting && (val & 0x400)) {
		arizona_identify_headphone(info);

		ret = extcon_update_state(&info->edev,
					  1 << ARIZONA_CABLE_MICROPHONE,
					  1 << ARIZONA_CABLE_MICROPHONE);

		if (ret != 0)
			dev_err(arizona->dev, "Headset report failed: %d\n",
				ret);

		/* Don't need to regulate for button detection */
		ret = regulator_allow_bypass(info->micvdd, false);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to bypass MICVDD: %d\n",
				ret);
		}

		info->mic = true;
		info->detecting = false;
		goto handled;
	}

	/* If we detected a lower impedence during initial startup
	 * then we probably have the wrong polarity, flip it.  Don't
	 * do this for the lowest impedences to speed up detection of
	 * plain headphones.  If both polarities report a low
	 * impedence then give up and report headphones.
	 */
	if (info->detecting && (val & 0x3f8)) {
		if (info->jack_flips >= info->micd_num_modes) {
			dev_dbg(arizona->dev, "Detected HP/line\n");
			arizona_identify_headphone(info);

			info->detecting = false;

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
	if (val & 0x3fc) {
		if (info->mic) {
			dev_dbg(arizona->dev, "Mic button detected\n");

			lvl = val & ARIZONA_MICD_LVL_MASK;
			lvl >>= ARIZONA_MICD_LVL_SHIFT;

			for (i = 0; i < ARIZONA_NUM_BUTTONS; i++)
				if (lvl & arizona_lvl_to_key[i].status)
					input_report_key(info->input,
							 arizona_lvl_to_key[i].report,
							 1);
			input_sync(info->input);

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
		for (i = 0; i < ARIZONA_NUM_BUTTONS; i++)
			input_report_key(info->input,
					 arizona_lvl_to_key[i].report, 0);
		input_sync(info->input);
		arizona_extcon_pulse_micbias(info);
	}

handled:
	pm_runtime_mark_last_busy(info->dev);
	mutex_unlock(&info->lock);

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

static irqreturn_t arizona_jackdet(int irq, void *data)
{
	struct arizona_extcon_info *info = data;
	struct arizona *arizona = info->arizona;
	unsigned int val, present, mask;
	int ret, i;

	pm_runtime_get_sync(info->dev);

	cancel_delayed_work_sync(&info->hpdet_work);

	mutex_lock(&info->lock);

	if (arizona->pdata.jd_gpio5) {
		mask = ARIZONA_MICD_CLAMP_STS;
		present = 0;
	} else {
		mask = ARIZONA_JD1_STS;
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

	if ((val & mask) == present) {
		dev_dbg(arizona->dev, "Detected jack\n");
		ret = extcon_set_cable_state_(&info->edev,
					      ARIZONA_CABLE_MECHANICAL, true);

		if (ret != 0)
			dev_err(arizona->dev, "Mechanical report failed: %d\n",
				ret);

		if (!arizona->pdata.hpdet_acc_id) {
			info->detecting = true;
			info->mic = false;
			info->jack_flips = 0;

			arizona_start_mic(info);
		} else {
			schedule_delayed_work(&info->hpdet_work,
					      msecs_to_jiffies(250));
		}

		regmap_update_bits(arizona->regmap,
				   ARIZONA_JACK_DETECT_DEBOUNCE,
				   ARIZONA_MICD_CLAMP_DB | ARIZONA_JD1_DB, 0);
	} else {
		dev_dbg(arizona->dev, "Detected jack removal\n");

		arizona_stop_mic(info);

		info->num_hpdet_res = 0;
		for (i = 0; i < ARRAY_SIZE(info->hpdet_res); i++)
			info->hpdet_res[i] = 0;
		info->mic = false;
		info->hpdet_done = false;

		for (i = 0; i < ARIZONA_NUM_BUTTONS; i++)
			input_report_key(info->input,
					 arizona_lvl_to_key[i].report, 0);
		input_sync(info->input);

		ret = extcon_update_state(&info->edev, 0xffffffff, 0);
		if (ret != 0)
			dev_err(arizona->dev, "Removal report failed: %d\n",
				ret);

		regmap_update_bits(arizona->regmap,
				   ARIZONA_JACK_DETECT_DEBOUNCE,
				   ARIZONA_MICD_CLAMP_DB | ARIZONA_JD1_DB,
				   ARIZONA_MICD_CLAMP_DB | ARIZONA_JD1_DB);
	}

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

static int arizona_extcon_probe(struct platform_device *pdev)
{
	struct arizona *arizona = dev_get_drvdata(pdev->dev.parent);
	struct arizona_pdata *pdata;
	struct arizona_extcon_info *info;
	int jack_irq_fall, jack_irq_rise;
	int ret, mode, i;

	if (!arizona->dapm || !arizona->dapm->card)
		return -EPROBE_DEFER;

	pdata = dev_get_platdata(arizona->dev);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto err;
	}

	info->micvdd = devm_regulator_get(arizona->dev, "MICVDD");
	if (IS_ERR(info->micvdd)) {
		ret = PTR_ERR(info->micvdd);
		dev_err(arizona->dev, "Failed to get MICVDD: %d\n", ret);
		goto err;
	}

	mutex_init(&info->lock);
	info->arizona = arizona;
	info->dev = &pdev->dev;
	INIT_DELAYED_WORK(&info->hpdet_work, arizona_hpdet_work);
	platform_set_drvdata(pdev, info);

	switch (arizona->type) {
	case WM5102:
		switch (arizona->rev) {
		case 0:
			info->micd_reva = true;
			break;
		default:
			info->micd_clamp = true;
			info->hpdet_ip = 1;
			break;
		}
		break;
	default:
		break;
	}

	info->edev.name = "Headset Jack";
	info->edev.supported_cable = arizona_cable;

	ret = extcon_dev_register(&info->edev, arizona->dev);
	if (ret < 0) {
		dev_err(arizona->dev, "extcon_dev_register() failed: %d\n",
			ret);
		goto err;
	}

	if (pdata->num_micd_configs) {
		info->micd_modes = pdata->micd_configs;
		info->micd_num_modes = pdata->num_micd_configs;
	} else {
		info->micd_modes = micd_default_modes;
		info->micd_num_modes = ARRAY_SIZE(micd_default_modes);
	}

	if (arizona->pdata.micd_pol_gpio > 0) {
		if (info->micd_modes[0].gpio)
			mode = GPIOF_OUT_INIT_HIGH;
		else
			mode = GPIOF_OUT_INIT_LOW;

		ret = devm_gpio_request_one(&pdev->dev,
					    arizona->pdata.micd_pol_gpio,
					    mode,
					    "MICD polarity");
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to request GPIO%d: %d\n",
				arizona->pdata.micd_pol_gpio, ret);
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
			goto err_register;
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

	if (arizona->pdata.micd_dbtime)
		regmap_update_bits(arizona->regmap, ARIZONA_MIC_DETECT_1,
				   ARIZONA_MICD_DBTIME_MASK,
				   arizona->pdata.micd_dbtime
				   << ARIZONA_MICD_DBTIME_SHIFT);

	/*
	 * If we have a clamp use it, activating in conjunction with
	 * GPIO5 if that is connected for jack detect operation.
	 */
	if (info->micd_clamp) {
		if (arizona->pdata.jd_gpio5) {
			/* Put the GPIO into input mode */
			regmap_write(arizona->regmap, ARIZONA_GPIO5_CTRL,
				     0xc101);

			regmap_update_bits(arizona->regmap,
					   ARIZONA_MICD_CLAMP_CONTROL,
					   ARIZONA_MICD_CLAMP_MODE_MASK, 0x9);
		} else {
			regmap_update_bits(arizona->regmap,
					   ARIZONA_MICD_CLAMP_CONTROL,
					   ARIZONA_MICD_CLAMP_MODE_MASK, 0x4);
		}

		regmap_update_bits(arizona->regmap,
				   ARIZONA_JACK_DETECT_DEBOUNCE,
				   ARIZONA_MICD_CLAMP_DB,
				   ARIZONA_MICD_CLAMP_DB);
	}

	arizona_extcon_set_mode(info, 0);

	info->input = devm_input_allocate_device(&pdev->dev);
	if (!info->input) {
		dev_err(arizona->dev, "Can't allocate input dev\n");
		ret = -ENOMEM;
		goto err_register;
	}

	for (i = 0; i < ARIZONA_NUM_BUTTONS; i++)
		input_set_capability(info->input, EV_KEY,
				     arizona_lvl_to_key[i].report);
	info->input->name = "Headset";
	info->input->phys = "arizona/extcon";
	info->input->dev.parent = &pdev->dev;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_idle(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	if (arizona->pdata.jd_gpio5) {
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
		goto err_input;
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
err_input:
err_register:
	pm_runtime_disable(&pdev->dev);
	extcon_dev_unregister(&info->edev);
err:
	return ret;
}

static int arizona_extcon_remove(struct platform_device *pdev)
{
	struct arizona_extcon_info *info = platform_get_drvdata(pdev);
	struct arizona *arizona = info->arizona;
	int jack_irq_rise, jack_irq_fall;

	pm_runtime_disable(&pdev->dev);

	regmap_update_bits(arizona->regmap,
			   ARIZONA_MICD_CLAMP_CONTROL,
			   ARIZONA_MICD_CLAMP_MODE_MASK, 0);

	if (arizona->pdata.jd_gpio5) {
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
	extcon_dev_unregister(&info->edev);

	return 0;
}

static struct platform_driver arizona_extcon_driver = {
	.driver		= {
		.name	= "arizona-extcon",
		.owner	= THIS_MODULE,
	},
	.probe		= arizona_extcon_probe,
	.remove		= arizona_extcon_remove,
};

module_platform_driver(arizona_extcon_driver);

MODULE_DESCRIPTION("Arizona Extcon driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:extcon-arizona");
