// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ChromeOS EC LED Driver
 *
 *  Copyright (C) 2024 Thomas Weißschuh <linux@weissschuh.net>
 */

#include <linux/device.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

static const char * const cros_ec_led_functions[] = {
	[EC_LED_ID_BATTERY_LED]            = LED_FUNCTION_CHARGING,
	[EC_LED_ID_POWER_LED]              = LED_FUNCTION_POWER,
	[EC_LED_ID_ADAPTER_LED]            = "adapter",
	[EC_LED_ID_LEFT_LED]               = "left",
	[EC_LED_ID_RIGHT_LED]              = "right",
	[EC_LED_ID_RECOVERY_HW_REINIT_LED] = "recovery-hw-reinit",
	[EC_LED_ID_SYSRQ_DEBUG_LED]        = "sysrq-debug",
};

static_assert(ARRAY_SIZE(cros_ec_led_functions) == EC_LED_ID_COUNT);

static const int cros_ec_led_to_linux_id[] = {
	[EC_LED_COLOR_RED]    = LED_COLOR_ID_RED,
	[EC_LED_COLOR_GREEN]  = LED_COLOR_ID_GREEN,
	[EC_LED_COLOR_BLUE]   = LED_COLOR_ID_BLUE,
	[EC_LED_COLOR_YELLOW] = LED_COLOR_ID_YELLOW,
	[EC_LED_COLOR_WHITE]  = LED_COLOR_ID_WHITE,
	[EC_LED_COLOR_AMBER]  = LED_COLOR_ID_AMBER,
};

static_assert(ARRAY_SIZE(cros_ec_led_to_linux_id) == EC_LED_COLOR_COUNT);

static const int cros_ec_linux_to_ec_id[] = {
	[LED_COLOR_ID_RED]    = EC_LED_COLOR_RED,
	[LED_COLOR_ID_GREEN]  = EC_LED_COLOR_GREEN,
	[LED_COLOR_ID_BLUE]   = EC_LED_COLOR_BLUE,
	[LED_COLOR_ID_YELLOW] = EC_LED_COLOR_YELLOW,
	[LED_COLOR_ID_WHITE]  = EC_LED_COLOR_WHITE,
	[LED_COLOR_ID_AMBER]  = EC_LED_COLOR_AMBER,
};

struct cros_ec_led_priv {
	struct led_classdev_mc led_mc_cdev;
	struct cros_ec_device *cros_ec;
	enum ec_led_id led_id;
};

static inline struct cros_ec_led_priv *cros_ec_led_cdev_to_priv(struct led_classdev *led_cdev)
{
	return container_of(lcdev_to_mccdev(led_cdev), struct cros_ec_led_priv, led_mc_cdev);
}

union cros_ec_led_cmd_data {
	struct ec_params_led_control req;
	struct ec_response_led_control resp;
} __packed;

static int cros_ec_led_send_cmd(struct cros_ec_device *cros_ec,
				union cros_ec_led_cmd_data *arg)
{
	int ret;
	struct {
		struct cros_ec_command msg;
		union cros_ec_led_cmd_data data;
	} __packed buf = {
		.msg = {
			.version = 1,
			.command = EC_CMD_LED_CONTROL,
			.insize  = sizeof(arg->resp),
			.outsize = sizeof(arg->req),
		},
		.data.req = arg->req
	};

	ret = cros_ec_cmd_xfer_status(cros_ec, &buf.msg);
	if (ret < 0)
		return ret;

	arg->resp = buf.data.resp;

	return 0;
}

static int cros_ec_led_trigger_activate(struct led_classdev *led_cdev)
{
	struct cros_ec_led_priv *priv = cros_ec_led_cdev_to_priv(led_cdev);
	union cros_ec_led_cmd_data arg = {};

	arg.req.led_id = priv->led_id;
	arg.req.flags = EC_LED_FLAGS_AUTO;

	return cros_ec_led_send_cmd(priv->cros_ec, &arg);
}

static struct led_hw_trigger_type cros_ec_led_trigger_type;

static struct led_trigger cros_ec_led_trigger = {
	.name = "chromeos-auto",
	.trigger_type = &cros_ec_led_trigger_type,
	.activate = cros_ec_led_trigger_activate,
};

static int cros_ec_led_brightness_set_blocking(struct led_classdev *led_cdev,
					       enum led_brightness brightness)
{
	struct cros_ec_led_priv *priv = cros_ec_led_cdev_to_priv(led_cdev);
	union cros_ec_led_cmd_data arg = {};
	enum ec_led_colors led_color;
	struct mc_subled *subled;
	size_t i;

	led_mc_calc_color_components(&priv->led_mc_cdev, brightness);

	arg.req.led_id = priv->led_id;

	for (i = 0; i < priv->led_mc_cdev.num_colors; i++) {
		subled = &priv->led_mc_cdev.subled_info[i];
		led_color = cros_ec_linux_to_ec_id[subled->color_index];
		arg.req.brightness[led_color] = subled->brightness;
	}

	return cros_ec_led_send_cmd(priv->cros_ec, &arg);
}

static int cros_ec_led_count_subleds(struct device *dev,
				     struct ec_response_led_control *resp,
				     unsigned int *max_brightness)
{
	unsigned int range, common_range = 0;
	int num_subleds = 0;
	size_t i;

	for (i = 0; i < EC_LED_COLOR_COUNT; i++) {
		range = resp->brightness_range[i];

		if (!range)
			continue;

		num_subleds++;

		if (!common_range)
			common_range = range;

		if (common_range != range) {
			/* The multicolor LED API expects a uniform max_brightness */
			dev_err(dev, "Inconsistent LED brightness values\n");
			return -EINVAL;
		}
	}

	if (!num_subleds)
		return -EINVAL;

	*max_brightness = common_range;
	return num_subleds;
}

static const char *cros_ec_led_get_color_name(struct led_classdev_mc *led_mc_cdev)
{
	int color;

	if (led_mc_cdev->num_colors == 1)
		color = led_mc_cdev->subled_info[0].color_index;
	else
		color = LED_COLOR_ID_MULTI;

	return led_get_color_name(color);
}

static int cros_ec_led_probe_one(struct device *dev, struct cros_ec_device *cros_ec,
				 enum ec_led_id id)
{
	union cros_ec_led_cmd_data arg = {};
	struct cros_ec_led_priv *priv;
	struct led_classdev *led_cdev;
	struct mc_subled *subleds;
	int i, ret, num_subleds;
	size_t subled;

	arg.req.led_id = id;
	arg.req.flags = EC_LED_FLAGS_QUERY;
	ret = cros_ec_led_send_cmd(cros_ec, &arg);
	if (ret == -EINVAL)
		return 0; /* Unknown LED, skip */
	if (ret == -EOPNOTSUPP)
		return -ENODEV;
	if (ret < 0)
		return ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	num_subleds = cros_ec_led_count_subleds(dev, &arg.resp,
						&priv->led_mc_cdev.led_cdev.max_brightness);
	if (num_subleds < 0)
		return num_subleds;

	priv->cros_ec = cros_ec;
	priv->led_id = id;

	subleds = devm_kcalloc(dev, num_subleds, sizeof(*subleds), GFP_KERNEL);
	if (!subleds)
		return -ENOMEM;

	subled = 0;
	for (i = 0; i < EC_LED_COLOR_COUNT; i++) {
		if (!arg.resp.brightness_range[i])
			continue;

		subleds[subled].color_index = cros_ec_led_to_linux_id[i];
		if (subled == 0)
			subleds[subled].intensity = 100;
		subled++;
	}

	priv->led_mc_cdev.subled_info = subleds;
	priv->led_mc_cdev.num_colors = num_subleds;

	led_cdev = &priv->led_mc_cdev.led_cdev;
	led_cdev->brightness_set_blocking = cros_ec_led_brightness_set_blocking;
	led_cdev->trigger_type = &cros_ec_led_trigger_type;
	led_cdev->default_trigger = cros_ec_led_trigger.name;
	led_cdev->hw_control_trigger = cros_ec_led_trigger.name;

	led_cdev->name = devm_kasprintf(dev, GFP_KERNEL, "chromeos:%s:%s",
					cros_ec_led_get_color_name(&priv->led_mc_cdev),
					cros_ec_led_functions[id]);
	if (!led_cdev->name)
		return -ENOMEM;

	return devm_led_classdev_multicolor_register(dev, &priv->led_mc_cdev);
}

static int cros_ec_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct cros_ec_device *cros_ec = ec_dev->ec_dev;
	int i, ret = 0;

	ret = devm_led_trigger_register(dev, &cros_ec_led_trigger);
	if (ret)
		return ret;

	for (i = 0; i < EC_LED_ID_COUNT; i++) {
		ret = cros_ec_led_probe_one(dev, cros_ec, i);
		if (ret)
			break;
	}

	return ret;
}

static const struct platform_device_id cros_ec_led_id[] = {
	{ "cros-ec-led", 0 },
	{}
};

static struct platform_driver cros_ec_led_driver = {
	.driver.name	= "cros-ec-led",
	.probe		= cros_ec_led_probe,
	.id_table	= cros_ec_led_id,
};
module_platform_driver(cros_ec_led_driver);

MODULE_DEVICE_TABLE(platform, cros_ec_led_id);
MODULE_DESCRIPTION("ChromeOS EC LED Driver");
MODULE_AUTHOR("Thomas Weißschuh <linux@weissschuh.net");
MODULE_LICENSE("GPL");
