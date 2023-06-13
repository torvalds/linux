// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sanyo LV5207LP LED Driver
 *
 * Copyright (C) 2013 Ideas on board SPRL
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_data/lv5207lp.h>
#include <linux/slab.h>

#define LV5207LP_CTRL1			0x00
#define LV5207LP_CPSW			(1 << 7)
#define LV5207LP_SCTEN			(1 << 6)
#define LV5207LP_C10			(1 << 5)
#define LV5207LP_CKSW			(1 << 4)
#define LV5207LP_RSW			(1 << 3)
#define LV5207LP_GSW			(1 << 2)
#define LV5207LP_BSW			(1 << 1)
#define LV5207LP_CTRL2			0x01
#define LV5207LP_MSW			(1 << 7)
#define LV5207LP_MLED4			(1 << 6)
#define LV5207LP_RED			0x02
#define LV5207LP_GREEN			0x03
#define LV5207LP_BLUE			0x04

#define LV5207LP_MAX_BRIGHTNESS		32

struct lv5207lp {
	struct i2c_client *client;
	struct backlight_device *backlight;
	struct lv5207lp_platform_data *pdata;
};

static int lv5207lp_write(struct lv5207lp *lv, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(lv->client, reg, data);
}

static int lv5207lp_backlight_update_status(struct backlight_device *backlight)
{
	struct lv5207lp *lv = bl_get_data(backlight);
	int brightness = backlight_get_brightness(backlight);

	if (brightness) {
		lv5207lp_write(lv, LV5207LP_CTRL1,
			       LV5207LP_CPSW | LV5207LP_C10 | LV5207LP_CKSW);
		lv5207lp_write(lv, LV5207LP_CTRL2,
			       LV5207LP_MSW | LV5207LP_MLED4 |
			       (brightness - 1));
	} else {
		lv5207lp_write(lv, LV5207LP_CTRL1, 0);
		lv5207lp_write(lv, LV5207LP_CTRL2, 0);
	}

	return 0;
}

static int lv5207lp_backlight_check_fb(struct backlight_device *backlight,
				       struct fb_info *info)
{
	struct lv5207lp *lv = bl_get_data(backlight);

	return lv->pdata->fbdev == NULL || lv->pdata->fbdev == info->device;
}

static const struct backlight_ops lv5207lp_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= lv5207lp_backlight_update_status,
	.check_fb	= lv5207lp_backlight_check_fb,
};

static int lv5207lp_probe(struct i2c_client *client)
{
	struct lv5207lp_platform_data *pdata = dev_get_platdata(&client->dev);
	struct backlight_device *backlight;
	struct backlight_properties props;
	struct lv5207lp *lv;

	if (pdata == NULL) {
		dev_err(&client->dev, "No platform data supplied\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_warn(&client->dev,
			 "I2C adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}

	lv = devm_kzalloc(&client->dev, sizeof(*lv), GFP_KERNEL);
	if (!lv)
		return -ENOMEM;

	lv->client = client;
	lv->pdata = pdata;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = min_t(unsigned int, pdata->max_value,
				     LV5207LP_MAX_BRIGHTNESS);
	props.brightness = clamp_t(unsigned int, pdata->def_value, 0,
				   props.max_brightness);

	backlight = devm_backlight_device_register(&client->dev,
				dev_name(&client->dev), &lv->client->dev,
				lv, &lv5207lp_backlight_ops, &props);
	if (IS_ERR(backlight)) {
		dev_err(&client->dev, "failed to register backlight\n");
		return PTR_ERR(backlight);
	}

	backlight_update_status(backlight);
	i2c_set_clientdata(client, backlight);

	return 0;
}

static void lv5207lp_remove(struct i2c_client *client)
{
	struct backlight_device *backlight = i2c_get_clientdata(client);

	backlight->props.brightness = 0;
	backlight_update_status(backlight);
}

static const struct i2c_device_id lv5207lp_ids[] = {
	{ "lv5207lp", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lv5207lp_ids);

static struct i2c_driver lv5207lp_driver = {
	.driver = {
		.name = "lv5207lp",
	},
	.probe_new = lv5207lp_probe,
	.remove = lv5207lp_remove,
	.id_table = lv5207lp_ids,
};

module_i2c_driver(lv5207lp_driver);

MODULE_DESCRIPTION("Sanyo LV5207LP Backlight Driver");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_LICENSE("GPL");
