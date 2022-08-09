// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRM driver for Solomon SSD130x OLED displays (I2C bus)
 *
 * Copyright 2022 Red Hat Inc.
 * Author: Javier Martinez Canillas <javierm@redhat.com>
 *
 * Based on drivers/video/fbdev/ssd1307fb.c
 * Copyright 2012 Free Electrons
 */
#include <linux/i2c.h>
#include <linux/module.h>

#include "ssd130x.h"

#define DRIVER_NAME	"ssd130x-i2c"
#define DRIVER_DESC	"DRM driver for Solomon SSD130x OLED displays (I2C)"

static const struct regmap_config ssd130x_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int ssd130x_i2c_probe(struct i2c_client *client)
{
	struct ssd130x_device *ssd130x;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &ssd130x_i2c_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ssd130x = ssd130x_probe(&client->dev, regmap);
	if (IS_ERR(ssd130x))
		return PTR_ERR(ssd130x);

	i2c_set_clientdata(client, ssd130x);

	return 0;
}

static int ssd130x_i2c_remove(struct i2c_client *client)
{
	struct ssd130x_device *ssd130x = i2c_get_clientdata(client);

	return ssd130x_remove(ssd130x);
}

static void ssd130x_i2c_shutdown(struct i2c_client *client)
{
	struct ssd130x_device *ssd130x = i2c_get_clientdata(client);

	ssd130x_shutdown(ssd130x);
}

static struct ssd130x_deviceinfo ssd130x_ssd1305_deviceinfo = {
	.default_vcomh = 0x34,
	.default_dclk_div = 1,
	.default_dclk_frq = 7,
};

static struct ssd130x_deviceinfo ssd130x_ssd1306_deviceinfo = {
	.default_vcomh = 0x20,
	.default_dclk_div = 1,
	.default_dclk_frq = 8,
	.need_chargepump = 1,
};

static struct ssd130x_deviceinfo ssd130x_ssd1307_deviceinfo = {
	.default_vcomh = 0x20,
	.default_dclk_div = 2,
	.default_dclk_frq = 12,
	.need_pwm = 1,
};

static struct ssd130x_deviceinfo ssd130x_ssd1309_deviceinfo = {
	.default_vcomh = 0x34,
	.default_dclk_div = 1,
	.default_dclk_frq = 10,
};

static const struct of_device_id ssd130x_of_match[] = {
	{
		.compatible = "solomon,ssd1305fb-i2c",
		.data = &ssd130x_ssd1305_deviceinfo,
	},
	{
		.compatible = "solomon,ssd1306fb-i2c",
		.data = &ssd130x_ssd1306_deviceinfo,
	},
	{
		.compatible = "solomon,ssd1307fb-i2c",
		.data = &ssd130x_ssd1307_deviceinfo,
	},
	{
		.compatible = "solomon,ssd1309fb-i2c",
		.data = &ssd130x_ssd1309_deviceinfo,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ssd130x_of_match);

static struct i2c_driver ssd130x_i2c_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ssd130x_of_match,
	},
	.probe_new = ssd130x_i2c_probe,
	.remove = ssd130x_i2c_remove,
	.shutdown = ssd130x_i2c_shutdown,
};
module_i2c_driver(ssd130x_i2c_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Javier Martinez Canillas <javierm@redhat.com>");
MODULE_LICENSE("GPL v2");
