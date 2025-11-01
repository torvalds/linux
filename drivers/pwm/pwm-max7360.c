// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Bootlin
 *
 * Author: Kamel BOUHARA <kamel.bouhara@bootlin.com>
 * Author: Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>
 *
 * PWM functionality of the MAX7360 multi-function device.
 * https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7360.pdf
 *
 * Limitations:
 * - Only supports normal polarity.
 * - The period is fixed to 2 ms.
 * - Only the duty cycle can be changed, new values are applied at the beginning
 *   of the next cycle.
 * - When disabled, the output is put in Hi-Z immediately.
 */
#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <linux/mfd/max7360.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/types.h>

#define MAX7360_NUM_PWMS			8
#define MAX7360_PWM_MAX				255
#define MAX7360_PWM_STEPS			256
#define MAX7360_PWM_PERIOD_NS			(2 * NSEC_PER_MSEC)

struct max7360_pwm_waveform {
	u8 duty_steps;
	bool enabled;
};

static int max7360_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct regmap *regmap = pwmchip_get_drvdata(chip);

	/*
	 * Make sure we use the individual PWM configuration register and not
	 * the global one.
	 * We never need to use the global one, so there is no need to revert
	 * that in the .free() callback.
	 */
	return regmap_write_bits(regmap, MAX7360_REG_PWMCFG(pwm->hwpwm),
				 MAX7360_PORT_CFG_COMMON_PWM, 0);
}

static int max7360_pwm_round_waveform_tohw(struct pwm_chip *chip,
					   struct pwm_device *pwm,
					   const struct pwm_waveform *wf,
					   void *_wfhw)
{
	struct max7360_pwm_waveform *wfhw = _wfhw;
	u64 duty_steps;

	/*
	 * Ignore user provided values for period_length_ns and duty_offset_ns:
	 * we only support fixed period of MAX7360_PWM_PERIOD_NS and offset of 0.
	 * Values from 0 to 254 as duty_steps will provide duty cycles of 0/256
	 * to 254/256, while value 255 will provide a duty cycle of 100%.
	 */
	if (wf->duty_length_ns >= MAX7360_PWM_PERIOD_NS) {
		duty_steps = MAX7360_PWM_MAX;
	} else {
		duty_steps = (u32)wf->duty_length_ns * MAX7360_PWM_STEPS / MAX7360_PWM_PERIOD_NS;
		if (duty_steps == MAX7360_PWM_MAX)
			duty_steps = MAX7360_PWM_MAX - 1;
	}

	wfhw->duty_steps = min(MAX7360_PWM_MAX, duty_steps);
	wfhw->enabled = !!wf->period_length_ns;

	if (wf->period_length_ns && wf->period_length_ns < MAX7360_PWM_PERIOD_NS)
		return 1;
	else
		return 0;
}

static int max7360_pwm_round_waveform_fromhw(struct pwm_chip *chip, struct pwm_device *pwm,
					     const void *_wfhw, struct pwm_waveform *wf)
{
	const struct max7360_pwm_waveform *wfhw = _wfhw;

	wf->period_length_ns = wfhw->enabled ? MAX7360_PWM_PERIOD_NS : 0;
	wf->duty_offset_ns = 0;

	if (wfhw->enabled) {
		if (wfhw->duty_steps == MAX7360_PWM_MAX)
			wf->duty_length_ns = MAX7360_PWM_PERIOD_NS;
		else
			wf->duty_length_ns = DIV_ROUND_UP(wfhw->duty_steps * MAX7360_PWM_PERIOD_NS,
							  MAX7360_PWM_STEPS);
	} else {
		wf->duty_length_ns = 0;
	}

	return 0;
}

static int max7360_pwm_write_waveform(struct pwm_chip *chip,
				      struct pwm_device *pwm,
				      const void *_wfhw)
{
	struct regmap *regmap = pwmchip_get_drvdata(chip);
	const struct max7360_pwm_waveform *wfhw = _wfhw;
	unsigned int val;
	int ret;

	if (wfhw->enabled) {
		ret = regmap_write(regmap, MAX7360_REG_PWM(pwm->hwpwm), wfhw->duty_steps);
		if (ret)
			return ret;
	}

	val = wfhw->enabled ? BIT(pwm->hwpwm) : 0;
	return regmap_write_bits(regmap, MAX7360_REG_GPIOCTRL, BIT(pwm->hwpwm), val);
}

static int max7360_pwm_read_waveform(struct pwm_chip *chip,
				     struct pwm_device *pwm,
				     void *_wfhw)
{
	struct regmap *regmap = pwmchip_get_drvdata(chip);
	struct max7360_pwm_waveform *wfhw = _wfhw;
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, MAX7360_REG_GPIOCTRL, &val);
	if (ret)
		return ret;

	if (val & BIT(pwm->hwpwm)) {
		wfhw->enabled = true;
		ret = regmap_read(regmap, MAX7360_REG_PWM(pwm->hwpwm), &val);
		if (ret)
			return ret;

		wfhw->duty_steps = val;
	} else {
		wfhw->enabled = false;
		wfhw->duty_steps = 0;
	}

	return 0;
}

static const struct pwm_ops max7360_pwm_ops = {
	.request = max7360_pwm_request,
	.round_waveform_tohw = max7360_pwm_round_waveform_tohw,
	.round_waveform_fromhw = max7360_pwm_round_waveform_fromhw,
	.read_waveform = max7360_pwm_read_waveform,
	.write_waveform = max7360_pwm_write_waveform,
};

static int max7360_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_chip *chip;
	struct regmap *regmap;
	int ret;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return dev_err_probe(dev, -ENODEV, "Could not get parent regmap\n");

	/*
	 * This MFD sub-device does not have any associated device tree node:
	 * properties are stored in the device node of the parent (MFD) device
	 * and this same node is used in phandles of client devices.
	 * Reuse this device tree node here, as otherwise the PWM subsystem
	 * would be confused by this topology.
	 */
	device_set_of_node_from_dev(dev, dev->parent);

	chip = devm_pwmchip_alloc(dev, MAX7360_NUM_PWMS, 0);
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	chip->ops = &max7360_pwm_ops;

	pwmchip_set_drvdata(chip, regmap);

	ret = devm_pwmchip_add(dev, chip);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add PWM chip\n");

	return 0;
}

static struct platform_driver max7360_pwm_driver = {
	.driver = {
		.name = "max7360-pwm",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = max7360_pwm_probe,
};
module_platform_driver(max7360_pwm_driver);

MODULE_DESCRIPTION("MAX7360 PWM driver");
MODULE_AUTHOR("Kamel BOUHARA <kamel.bouhara@bootlin.com>");
MODULE_AUTHOR("Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>");
MODULE_LICENSE("GPL");
