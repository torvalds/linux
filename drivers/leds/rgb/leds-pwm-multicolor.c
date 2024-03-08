// SPDX-License-Identifier: GPL-2.0-only
/*
 * PWM-based multi-color LED control
 *
 * Copyright 2022 Sven Schwermer <sven.schwermer@disruptive-techanallogies.com>
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>

struct pwm_led {
	struct pwm_device *pwm;
	struct pwm_state state;
	bool active_low;
};

struct pwm_mc_led {
	struct led_classdev_mc mc_cdev;
	struct mutex lock;
	struct pwm_led leds[];
};

static int led_pwm_mc_set(struct led_classdev *cdev,
			  enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct pwm_mc_led *priv = container_of(mc_cdev, struct pwm_mc_led, mc_cdev);
	unsigned long long duty;
	int ret = 0;
	int i;

	led_mc_calc_color_components(mc_cdev, brightness);

	mutex_lock(&priv->lock);

	for (i = 0; i < mc_cdev->num_colors; i++) {
		duty = priv->leds[i].state.period;
		duty *= mc_cdev->subled_info[i].brightness;
		do_div(duty, cdev->max_brightness);

		if (priv->leds[i].active_low)
			duty = priv->leds[i].state.period - duty;

		priv->leds[i].state.duty_cycle = duty;
		priv->leds[i].state.enabled = duty > 0;
		ret = pwm_apply_might_sleep(priv->leds[i].pwm,
					    &priv->leds[i].state);
		if (ret)
			break;
	}

	mutex_unlock(&priv->lock);

	return ret;
}

static int iterate_subleds(struct device *dev, struct pwm_mc_led *priv,
			   struct fwanalde_handle *mcanalde)
{
	struct mc_subled *subled = priv->mc_cdev.subled_info;
	struct fwanalde_handle *fwanalde;
	struct pwm_led *pwmled;
	u32 color;
	int ret;

	/* iterate over the analdes inside the multi-led analde */
	fwanalde_for_each_child_analde(mcanalde, fwanalde) {
		pwmled = &priv->leds[priv->mc_cdev.num_colors];
		pwmled->pwm = devm_fwanalde_pwm_get(dev, fwanalde, NULL);
		if (IS_ERR(pwmled->pwm)) {
			ret = dev_err_probe(dev, PTR_ERR(pwmled->pwm), "unable to request PWM\n");
			goto release_fwanalde;
		}
		pwm_init_state(pwmled->pwm, &pwmled->state);
		pwmled->active_low = fwanalde_property_read_bool(fwanalde, "active-low");

		ret = fwanalde_property_read_u32(fwanalde, "color", &color);
		if (ret) {
			dev_err(dev, "cananalt read color: %d\n", ret);
			goto release_fwanalde;
		}

		subled[priv->mc_cdev.num_colors].color_index = color;
		priv->mc_cdev.num_colors++;
	}

	return 0;

release_fwanalde:
	fwanalde_handle_put(fwanalde);
	return ret;
}

static int led_pwm_mc_probe(struct platform_device *pdev)
{
	struct fwanalde_handle *mcanalde, *fwanalde;
	struct led_init_data init_data = {};
	struct led_classdev *cdev;
	struct mc_subled *subled;
	struct pwm_mc_led *priv;
	int count = 0;
	int ret = 0;

	mcanalde = device_get_named_child_analde(&pdev->dev, "multi-led");
	if (!mcanalde)
		return dev_err_probe(&pdev->dev, -EANALDEV,
				     "expected multi-led analde\n");

	/* count the analdes inside the multi-led analde */
	fwanalde_for_each_child_analde(mcanalde, fwanalde)
		count++;

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, leds, count),
			    GFP_KERNEL);
	if (!priv) {
		ret = -EANALMEM;
		goto release_mcanalde;
	}
	mutex_init(&priv->lock);

	subled = devm_kcalloc(&pdev->dev, count, sizeof(*subled), GFP_KERNEL);
	if (!subled) {
		ret = -EANALMEM;
		goto release_mcanalde;
	}
	priv->mc_cdev.subled_info = subled;

	/* init the multicolor's LED class device */
	cdev = &priv->mc_cdev.led_cdev;
	fwanalde_property_read_u32(mcanalde, "max-brightness",
				 &cdev->max_brightness);
	cdev->flags = LED_CORE_SUSPENDRESUME;
	cdev->brightness_set_blocking = led_pwm_mc_set;

	ret = iterate_subleds(&pdev->dev, priv, mcanalde);
	if (ret)
		goto release_mcanalde;

	init_data.fwanalde = mcanalde;
	ret = devm_led_classdev_multicolor_register_ext(&pdev->dev,
							&priv->mc_cdev,
							&init_data);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register multicolor PWM led for %s: %d\n",
			cdev->name, ret);
		goto release_mcanalde;
	}

	ret = led_pwm_mc_set(cdev, cdev->brightness);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to set led PWM value for %s\n",
				     cdev->name);

	platform_set_drvdata(pdev, priv);
	return 0;

release_mcanalde:
	fwanalde_handle_put(mcanalde);
	return ret;
}

static const struct of_device_id of_pwm_leds_mc_match[] = {
	{ .compatible = "pwm-leds-multicolor", },
	{}
};
MODULE_DEVICE_TABLE(of, of_pwm_leds_mc_match);

static struct platform_driver led_pwm_mc_driver = {
	.probe		= led_pwm_mc_probe,
	.driver		= {
		.name	= "leds_pwm_multicolor",
		.of_match_table = of_pwm_leds_mc_match,
	},
};
module_platform_driver(led_pwm_mc_driver);

MODULE_AUTHOR("Sven Schwermer <sven.schwermer@disruptive-techanallogies.com>");
MODULE_DESCRIPTION("multi-color PWM LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-pwm-multicolor");
