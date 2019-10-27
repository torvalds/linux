// SPDX-License-Identifier: GPL-2.0+

/*
 * LCD Backlight driver for RAVE SP
 *
 * Copyright (C) 2018 Zodiac Inflight Innovations
 *
 */

#include <linux/backlight.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/rave-sp.h>
#include <linux/platform_device.h>

#define	RAVE_SP_BACKLIGHT_LCD_EN	BIT(7)

static int rave_sp_backlight_update_status(struct backlight_device *bd)
{
	const struct backlight_properties *p = &bd->props;
	const u8 intensity =
		(p->power == FB_BLANK_UNBLANK) ? p->brightness : 0;
	struct rave_sp *sp = dev_get_drvdata(&bd->dev);
	u8 cmd[] = {
		[0] = RAVE_SP_CMD_SET_BACKLIGHT,
		[1] = 0,
		[2] = intensity ? RAVE_SP_BACKLIGHT_LCD_EN | intensity : 0,
		[3] = 0,
		[4] = 0,
	};

	return rave_sp_exec(sp, cmd, sizeof(cmd), NULL, 0);
}

static const struct backlight_ops rave_sp_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= rave_sp_backlight_update_status,
};

static struct backlight_properties rave_sp_backlight_props = {
	.type		= BACKLIGHT_PLATFORM,
	.max_brightness = 100,
	.brightness	= 50,
};

static int rave_sp_backlight_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct backlight_device *bd;

	bd = devm_backlight_device_register(dev, pdev->name, dev,
					    dev_get_drvdata(dev->parent),
					    &rave_sp_backlight_ops,
					    &rave_sp_backlight_props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	/*
	 * If there is a phandle pointing to the device node we can
	 * assume that another device will manage the status changes.
	 * If not we make sure the backlight is in a consistent state.
	 */
	if (!dev->of_node->phandle)
		backlight_update_status(bd);

	return 0;
}

static const struct of_device_id rave_sp_backlight_of_match[] = {
	{ .compatible = "zii,rave-sp-backlight" },
	{}
};

static struct platform_driver rave_sp_backlight_driver = {
	.probe = rave_sp_backlight_probe,
	.driver	= {
		.name = KBUILD_MODNAME,
		.of_match_table = rave_sp_backlight_of_match,
	},
};
module_platform_driver(rave_sp_backlight_driver);

MODULE_DEVICE_TABLE(of, rave_sp_backlight_of_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrey Vostrikov <andrey.vostrikov@cogentembedded.com>");
MODULE_AUTHOR("Nikita Yushchenko <nikita.yoush@cogentembedded.com>");
MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("RAVE SP Backlight driver");
