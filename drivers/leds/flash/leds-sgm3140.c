// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Luca Weiss <luca@z3ntu.xyz>

#include <linux/gpio/consumer.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>

#include <media/v4l2-flash-led-class.h>

#define FLASH_TIMEOUT_DEFAULT		250000U /* 250ms */
#define FLASH_MAX_TIMEOUT_DEFAULT	300000U /* 300ms */

struct sgm3140 {
	struct led_classdev_flash fled_cdev;
	struct v4l2_flash *v4l2_flash;

	struct timer_list powerdown_timer;

	struct gpio_desc *flash_gpio;
	struct gpio_desc *enable_gpio;
	struct regulator *vin_regulator;

	bool enabled;

	/* current timeout in us */
	u32 timeout;
	/* maximum timeout in us */
	u32 max_timeout;
};

static struct sgm3140 *flcdev_to_sgm3140(struct led_classdev_flash *flcdev)
{
	return container_of(flcdev, struct sgm3140, fled_cdev);
}

static int sgm3140_strobe_set(struct led_classdev_flash *fled_cdev, bool state)
{
	struct sgm3140 *priv = flcdev_to_sgm3140(fled_cdev);
	int ret;

	if (priv->enabled == state)
		return 0;

	if (state) {
		ret = regulator_enable(priv->vin_regulator);
		if (ret) {
			dev_err(fled_cdev->led_cdev.dev,
				"failed to enable regulator: %d\n", ret);
			return ret;
		}
		gpiod_set_value_cansleep(priv->flash_gpio, 1);
		gpiod_set_value_cansleep(priv->enable_gpio, 1);
		mod_timer(&priv->powerdown_timer,
			  jiffies + usecs_to_jiffies(priv->timeout));
	} else {
		del_timer_sync(&priv->powerdown_timer);
		gpiod_set_value_cansleep(priv->enable_gpio, 0);
		gpiod_set_value_cansleep(priv->flash_gpio, 0);
		ret = regulator_disable(priv->vin_regulator);
		if (ret) {
			dev_err(fled_cdev->led_cdev.dev,
				"failed to disable regulator: %d\n", ret);
			return ret;
		}
	}

	priv->enabled = state;

	return 0;
}

static int sgm3140_strobe_get(struct led_classdev_flash *fled_cdev, bool *state)
{
	struct sgm3140 *priv = flcdev_to_sgm3140(fled_cdev);

	*state = timer_pending(&priv->powerdown_timer);

	return 0;
}

static int sgm3140_timeout_set(struct led_classdev_flash *fled_cdev,
			       u32 timeout)
{
	struct sgm3140 *priv = flcdev_to_sgm3140(fled_cdev);

	priv->timeout = timeout;

	return 0;
}

static const struct led_flash_ops sgm3140_flash_ops = {
	.strobe_set = sgm3140_strobe_set,
	.strobe_get = sgm3140_strobe_get,
	.timeout_set = sgm3140_timeout_set,
};

static int sgm3140_brightness_set(struct led_classdev *led_cdev,
				  enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(led_cdev);
	struct sgm3140 *priv = flcdev_to_sgm3140(fled_cdev);
	bool enable = brightness == LED_ON;
	int ret;

	if (priv->enabled == enable)
		return 0;

	if (enable) {
		ret = regulator_enable(priv->vin_regulator);
		if (ret) {
			dev_err(led_cdev->dev,
				"failed to enable regulator: %d\n", ret);
			return ret;
		}
		gpiod_set_value_cansleep(priv->enable_gpio, 1);
	} else {
		gpiod_set_value_cansleep(priv->enable_gpio, 0);
		ret = regulator_disable(priv->vin_regulator);
		if (ret) {
			dev_err(led_cdev->dev,
				"failed to disable regulator: %d\n", ret);
			return ret;
		}
	}

	priv->enabled = enable;

	return 0;
}

static void sgm3140_powerdown_timer(struct timer_list *t)
{
	struct sgm3140 *priv = from_timer(priv, t, powerdown_timer);

	gpiod_set_value(priv->enable_gpio, 0);
	gpiod_set_value(priv->flash_gpio, 0);
	regulator_disable(priv->vin_regulator);

	priv->enabled = false;
}

static void sgm3140_init_flash_timeout(struct sgm3140 *priv)
{
	struct led_classdev_flash *fled_cdev = &priv->fled_cdev;
	struct led_flash_setting *s;

	/* Init flash timeout setting */
	s = &fled_cdev->timeout;
	s->min = 1;
	s->max = priv->max_timeout;
	s->step = 1;
	s->val = FLASH_TIMEOUT_DEFAULT;
}

#if IS_ENABLED(CONFIG_V4L2_FLASH_LED_CLASS)
static void sgm3140_init_v4l2_flash_config(struct sgm3140 *priv,
					struct v4l2_flash_config *v4l2_sd_cfg)
{
	struct led_classdev *led_cdev = &priv->fled_cdev.led_cdev;
	struct led_flash_setting *s;

	strscpy(v4l2_sd_cfg->dev_name, led_cdev->dev->kobj.name,
		sizeof(v4l2_sd_cfg->dev_name));

	/* Init flash intensity setting */
	s = &v4l2_sd_cfg->intensity;
	s->min = 0;
	s->max = 1;
	s->step = 1;
	s->val = 1;
}

#else
static void sgm3140_init_v4l2_flash_config(struct sgm3140 *priv,
					struct v4l2_flash_config *v4l2_sd_cfg)
{
}
#endif

static int sgm3140_probe(struct platform_device *pdev)
{
	struct sgm3140 *priv;
	struct led_classdev *led_cdev;
	struct led_classdev_flash *fled_cdev;
	struct led_init_data init_data = {};
	struct fwnode_handle *child_node;
	struct v4l2_flash_config v4l2_sd_cfg = {};
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->flash_gpio = devm_gpiod_get(&pdev->dev, "flash", GPIOD_OUT_LOW);
	ret = PTR_ERR_OR_ZERO(priv->flash_gpio);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request flash gpio\n");

	priv->enable_gpio = devm_gpiod_get(&pdev->dev, "enable", GPIOD_OUT_LOW);
	ret = PTR_ERR_OR_ZERO(priv->enable_gpio);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request enable gpio\n");

	priv->vin_regulator = devm_regulator_get(&pdev->dev, "vin");
	ret = PTR_ERR_OR_ZERO(priv->vin_regulator);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request regulator\n");

	child_node = fwnode_get_next_available_child_node(pdev->dev.fwnode,
							  NULL);
	if (!child_node) {
		dev_err(&pdev->dev,
			"No fwnode child node found for connected LED.\n");
		return -EINVAL;
	}

	ret = fwnode_property_read_u32(child_node, "flash-max-timeout-us",
				       &priv->max_timeout);
	if (ret) {
		priv->max_timeout = FLASH_MAX_TIMEOUT_DEFAULT;
		dev_warn(&pdev->dev,
			 "flash-max-timeout-us property missing\n");
	}

	/*
	 * Set default timeout to FLASH_DEFAULT_TIMEOUT except if max_timeout
	 * from DT is lower.
	 */
	priv->timeout = min(priv->max_timeout, FLASH_TIMEOUT_DEFAULT);

	timer_setup(&priv->powerdown_timer, sgm3140_powerdown_timer, 0);

	fled_cdev = &priv->fled_cdev;
	led_cdev = &fled_cdev->led_cdev;

	fled_cdev->ops = &sgm3140_flash_ops;

	led_cdev->brightness_set_blocking = sgm3140_brightness_set;
	led_cdev->max_brightness = LED_ON;
	led_cdev->flags |= LED_DEV_CAP_FLASH;

	sgm3140_init_flash_timeout(priv);

	init_data.fwnode = child_node;

	platform_set_drvdata(pdev, priv);

	/* Register in the LED subsystem */
	ret = devm_led_classdev_flash_register_ext(&pdev->dev,
						   fled_cdev, &init_data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register flash device: %d\n",
			ret);
		goto err;
	}

	sgm3140_init_v4l2_flash_config(priv, &v4l2_sd_cfg);

	/* Create V4L2 Flash subdev */
	priv->v4l2_flash = v4l2_flash_init(&pdev->dev,
					   child_node,
					   fled_cdev, NULL,
					   &v4l2_sd_cfg);
	if (IS_ERR(priv->v4l2_flash)) {
		ret = PTR_ERR(priv->v4l2_flash);
		goto err;
	}

	return ret;

err:
	fwnode_handle_put(child_node);
	return ret;
}

static int sgm3140_remove(struct platform_device *pdev)
{
	struct sgm3140 *priv = platform_get_drvdata(pdev);

	del_timer_sync(&priv->powerdown_timer);

	v4l2_flash_release(priv->v4l2_flash);

	return 0;
}

static const struct of_device_id sgm3140_dt_match[] = {
	{ .compatible = "sgmicro,sgm3140" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sgm3140_dt_match);

static struct platform_driver sgm3140_driver = {
	.probe	= sgm3140_probe,
	.remove	= sgm3140_remove,
	.driver	= {
		.name	= "sgm3140",
		.of_match_table = sgm3140_dt_match,
	},
};

module_platform_driver(sgm3140_driver);

MODULE_AUTHOR("Luca Weiss <luca@z3ntu.xyz>");
MODULE_DESCRIPTION("SG Micro SGM3140 charge pump LED driver");
MODULE_LICENSE("GPL v2");
