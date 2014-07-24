/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * iPAQ microcontroller backlight support
 * Author : Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/mfd/ipaq-micro.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static int micro_bl_update_status(struct backlight_device *bd)
{
	struct ipaq_micro *micro = dev_get_drvdata(&bd->dev);
	int intensity = bd->props.brightness;
	struct ipaq_micro_msg msg = {
		.id = MSG_BACKLIGHT,
		.tx_len = 3,
	};

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.state & (BL_CORE_FBBLANK | BL_CORE_SUSPENDED))
		intensity = 0;

	/*
	 * Message format:
	 * Byte 0: backlight instance (usually 1)
	 * Byte 1: on/off
	 * Byte 2: intensity, 0-255
	 */
	msg.tx_data[0] = 0x01;
	msg.tx_data[1] = intensity > 0 ? 1 : 0;
	msg.tx_data[2] = intensity;
	return ipaq_micro_tx_msg_sync(micro, &msg);
}

static const struct backlight_ops micro_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status  = micro_bl_update_status,
};

static struct backlight_properties micro_bl_props = {
	.type = BACKLIGHT_RAW,
	.max_brightness = 255,
	.power = FB_BLANK_UNBLANK,
	.brightness = 64,
};

static int micro_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bd;
	struct ipaq_micro *micro = dev_get_drvdata(pdev->dev.parent);

	bd = devm_backlight_device_register(&pdev->dev, "ipaq-micro-backlight",
					    &pdev->dev, micro, &micro_bl_ops,
					    &micro_bl_props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	platform_set_drvdata(pdev, bd);
	backlight_update_status(bd);

	return 0;
}

struct platform_driver micro_backlight_device_driver = {
	.driver = {
		.name    = "ipaq-micro-backlight",
	},
	.probe   = micro_backlight_probe,
};
module_platform_driver(micro_backlight_device_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("driver for iPAQ Atmel micro backlight");
MODULE_ALIAS("platform:ipaq-micro-backlight");
