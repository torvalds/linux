// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Texas Instruments Incorporated -  http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * Based on pwm_bl.c
 */

#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct led_bl_data {
	struct device		*dev;
	struct backlight_device	*bl_dev;
	struct led_classdev	**leds;
	bool			enabled;
	int			nb_leds;
	unsigned int		*levels;
	unsigned int		default_brightness;
	unsigned int		max_brightness;
};

static void led_bl_set_brightness(struct led_bl_data *priv, int level)
{
	int i;
	int bkl_brightness;

	if (priv->levels)
		bkl_brightness = priv->levels[level];
	else
		bkl_brightness = level;

	for (i = 0; i < priv->nb_leds; i++)
		led_set_brightness(priv->leds[i], bkl_brightness);

	priv->enabled = true;
}

static void led_bl_power_off(struct led_bl_data *priv)
{
	int i;

	if (!priv->enabled)
		return;

	for (i = 0; i < priv->nb_leds; i++)
		led_set_brightness(priv->leds[i], LED_OFF);

	priv->enabled = false;
}

static int led_bl_update_status(struct backlight_device *bl)
{
	struct led_bl_data *priv = bl_get_data(bl);
	int brightness = bl->props.brightness;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;

	if (brightness > 0)
		led_bl_set_brightness(priv, brightness);
	else
		led_bl_power_off(priv);

	return 0;
}

static const struct backlight_ops led_bl_ops = {
	.update_status	= led_bl_update_status,
};

static int led_bl_get_leds(struct device *dev,
			   struct led_bl_data *priv)
{
	int i, nb_leds, ret;
	struct device_node *node = dev->of_node;
	struct led_classdev **leds;
	unsigned int max_brightness;
	unsigned int default_brightness;

	ret = of_count_phandle_with_args(node, "leds", NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to get led count\n");
		return -EINVAL;
	}

	nb_leds = ret;
	if (nb_leds < 1) {
		dev_err(dev, "At least one LED must be specified!\n");
		return -EINVAL;
	}

	leds = devm_kzalloc(dev, sizeof(struct led_classdev *) * nb_leds,
			    GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	for (i = 0; i < nb_leds; i++) {
		leds[i] = devm_of_led_get(dev, i);
		if (IS_ERR(leds[i]))
			return PTR_ERR(leds[i]);
	}

	/* check that the LEDs all have the same brightness range */
	max_brightness = leds[0]->max_brightness;
	for (i = 1; i < nb_leds; i++) {
		if (max_brightness != leds[i]->max_brightness) {
			dev_err(dev, "LEDs must have identical ranges\n");
			return -EINVAL;
		}
	}

	/* get the default brightness from the first LED from the list */
	default_brightness = leds[0]->brightness;

	priv->nb_leds = nb_leds;
	priv->leds = leds;
	priv->max_brightness = max_brightness;
	priv->default_brightness = default_brightness;

	return 0;
}

static int led_bl_parse_levels(struct device *dev,
			   struct led_bl_data *priv)
{
	struct device_node *node = dev->of_node;
	int num_levels;
	u32 value;
	int ret;

	if (!node)
		return -ENODEV;

	num_levels = of_property_count_u32_elems(node, "brightness-levels");
	if (num_levels > 1) {
		int i;
		unsigned int db;
		u32 *levels = NULL;

		levels = devm_kzalloc(dev, sizeof(u32) * num_levels,
				      GFP_KERNEL);
		if (!levels)
			return -ENOMEM;

		ret = of_property_read_u32_array(node, "brightness-levels",
						levels,
						num_levels);
		if (ret < 0)
			return ret;

		/*
		 * Try to map actual LED brightness to backlight brightness
		 * level
		 */
		db = priv->default_brightness;
		for (i = 0 ; i < num_levels; i++) {
			if ((i && db > levels[i-1]) && db <= levels[i])
				break;
		}
		priv->default_brightness = i;
		priv->max_brightness = num_levels - 1;
		priv->levels = levels;
	} else if (num_levels >= 0)
		dev_warn(dev, "Not enough levels defined\n");

	ret = of_property_read_u32(node, "default-brightness-level", &value);
	if (!ret && value <= priv->max_brightness)
		priv->default_brightness = value;
	else if (!ret  && value > priv->max_brightness)
		dev_warn(dev, "Invalid default brightness. Ignoring it\n");

	return 0;
}

static int led_bl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct led_bl_data *priv;
	int ret, i;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->dev = &pdev->dev;

	ret = led_bl_get_leds(&pdev->dev, priv);
	if (ret)
		return ret;

	ret = led_bl_parse_levels(&pdev->dev, priv);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to parse DT data\n");
		return ret;
	}

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = priv->max_brightness;
	props.brightness = priv->default_brightness;
	props.power = (priv->default_brightness > 0) ? FB_BLANK_POWERDOWN :
		      FB_BLANK_UNBLANK;
	priv->bl_dev = backlight_device_register(dev_name(&pdev->dev),
			&pdev->dev, priv, &led_bl_ops, &props);
	if (IS_ERR(priv->bl_dev)) {
		dev_err(&pdev->dev, "Failed to register backlight\n");
		return PTR_ERR(priv->bl_dev);
	}

	for (i = 0; i < priv->nb_leds; i++)
		led_sysfs_disable(priv->leds[i]);

	backlight_update_status(priv->bl_dev);

	return 0;
}

static int led_bl_remove(struct platform_device *pdev)
{
	struct led_bl_data *priv = platform_get_drvdata(pdev);
	struct backlight_device *bl = priv->bl_dev;
	int i;

	backlight_device_unregister(bl);

	led_bl_power_off(priv);
	for (i = 0; i < priv->nb_leds; i++)
		led_sysfs_enable(priv->leds[i]);

	return 0;
}

static const struct of_device_id led_bl_of_match[] = {
	{ .compatible = "led-backlight" },
	{ }
};

MODULE_DEVICE_TABLE(of, led_bl_of_match);

static struct platform_driver led_bl_driver = {
	.driver		= {
		.name		= "led-backlight",
		.of_match_table	= of_match_ptr(led_bl_of_match),
	},
	.probe		= led_bl_probe,
	.remove		= led_bl_remove,
};

module_platform_driver(led_bl_driver);

MODULE_DESCRIPTION("LED based Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:led-backlight");
