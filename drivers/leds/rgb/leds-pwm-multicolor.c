// SPDX-License-Identifier: GPL-2.0-only
/*
 * PWM-based multi-color LED control
 *
 * Copyright 2022 Sven Schwermer <sven.schwermer@disruptive-technologies.com>
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

		priv->leds[i].state.duty_cycle = duty;
		priv->leds[i].state.enabled = duty > 0;
		ret = pwm_apply_state(priv->leds[i].pwm,
				      &priv->leds[i].state);
		if (ret)
			break;
	}

	mutex_unlock(&priv->lock);

	return ret;
}

static int iterate_subleds(struct device *dev, struct pwm_mc_led *priv,
			   struct fwnode_handle *mcnode)
{
	struct mc_subled *subled = priv->mc_cdev.subled_info;
	struct fwnode_handle *fwnode;
	struct pwm_led *pwmled;
	u32 color;
	int ret;

	/* iterate over the nodes inside the multi-led node */
	fwnode_for_each_child_node(mcnode, fwnode) {
		pwmled = &priv->leds[priv->mc_cdev.num_colors];
		pwmled->pwm = devm_fwnode_pwm_get(dev, fwnode, NULL);
		if (IS_ERR(pwmled->pwm)) {
			ret = PTR_ERR(pwmled->pwm);
			dev_err(dev, "unable to request PWM: %d\n", ret);
			goto release_fwnode;
		}
		pwm_init_state(pwmled->pwm, &pwmled->state);

		ret = fwnode_property_read_u32(fwnode, "color", &color);
		if (ret) {
			dev_err(dev, "cannot read color: %d\n", ret);
			goto release_fwnode;
		}

		subled[priv->mc_cdev.num_colors].color_index = color;
		priv->mc_cdev.num_colors++;
	}

	return 0;

release_fwnode:
	fwnode_handle_put(fwnode);
	return ret;
}

static int led_pwm_mc_probe(struct platform_device *pdev)
{
	struct fwnode_handle *mcnode, *fwnode;
	struct led_init_data init_data = {};
	struct led_classdev *cdev;
	struct mc_subled *subled;
	struct pwm_mc_led *priv;
	int count = 0;
	int ret = 0;

	mcnode = device_get_named_child_node(&pdev->dev, "multi-led");
	if (!mcnode)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "expected multi-led node\n");

	/* count the nodes inside the multi-led node */
	fwnode_for_each_child_node(mcnode, fwnode)
		count++;

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, leds, count),
			    GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto release_mcnode;
	}
	mutex_init(&priv->lock);

	subled = devm_kcalloc(&pdev->dev, count, sizeof(*subled), GFP_KERNEL);
	if (!subled) {
		ret = -ENOMEM;
		goto release_mcnode;
	}
	priv->mc_cdev.subled_info = subled;

	/* init the multicolor's LED class device */
	cdev = &priv->mc_cdev.led_cdev;
	fwnode_property_read_u32(mcnode, "max-brightness",
				 &cdev->max_brightness);
	cdev->flags = LED_CORE_SUSPENDRESUME;
	cdev->brightness_set_blocking = led_pwm_mc_set;

	ret = iterate_subleds(&pdev->dev, priv, mcnode);
	if (ret)
		goto release_mcnode;

	init_data.fwnode = mcnode;
	ret = devm_led_classdev_multicolor_register_ext(&pdev->dev,
							&priv->mc_cdev,
							&init_data);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register multicolor PWM led for %s: %d\n",
			cdev->name, ret);
		goto release_mcnode;
	}

	ret = led_pwm_mc_set(cdev, cdev->brightness);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to set led PWM value for %s: %d",
				     cdev->name, ret);

	platform_set_drvdata(pdev, priv);
	return 0;

release_mcnode:
	fwnode_handle_put(mcnode);
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

MODULE_AUTHOR("Sven Schwermer <sven.schwermer@disruptive-technologies.com>");
MODULE_DESCRIPTION("multi-color PWM LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-pwm-multicolor");
