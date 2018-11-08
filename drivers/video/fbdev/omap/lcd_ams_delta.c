/*
 * Based on drivers/video/omap/lcd_inn1510.c
 *
 * LCD panel support for the Amstrad E3 (Delta) videophone.
 *
 * Copyright (C) 2006 Jonathan McDowell <noodles@earth.li>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/lcd.h>

#include <mach/hardware.h>

#include "omapfb.h"

#define AMS_DELTA_DEFAULT_CONTRAST	112

#define AMS_DELTA_MAX_CONTRAST		0x00FF
#define AMS_DELTA_LCD_POWER		0x0100


/* LCD class device section */

static int ams_delta_lcd;
static struct gpio_desc *gpiod_vblen;
static struct gpio_desc *gpiod_ndisp;

static int ams_delta_lcd_set_power(struct lcd_device *dev, int power)
{
	if (power == FB_BLANK_UNBLANK) {
		if (!(ams_delta_lcd & AMS_DELTA_LCD_POWER)) {
			omap_writeb(ams_delta_lcd & AMS_DELTA_MAX_CONTRAST,
					OMAP_PWL_ENABLE);
			omap_writeb(1, OMAP_PWL_CLK_ENABLE);
			ams_delta_lcd |= AMS_DELTA_LCD_POWER;
		}
	} else {
		if (ams_delta_lcd & AMS_DELTA_LCD_POWER) {
			omap_writeb(0, OMAP_PWL_ENABLE);
			omap_writeb(0, OMAP_PWL_CLK_ENABLE);
			ams_delta_lcd &= ~AMS_DELTA_LCD_POWER;
		}
	}
	return 0;
}

static int ams_delta_lcd_set_contrast(struct lcd_device *dev, int value)
{
	if ((value >= 0) && (value <= AMS_DELTA_MAX_CONTRAST)) {
		omap_writeb(value, OMAP_PWL_ENABLE);
		ams_delta_lcd &= ~AMS_DELTA_MAX_CONTRAST;
		ams_delta_lcd |= value;
	}
	return 0;
}

#ifdef CONFIG_LCD_CLASS_DEVICE
static int ams_delta_lcd_get_power(struct lcd_device *dev)
{
	if (ams_delta_lcd & AMS_DELTA_LCD_POWER)
		return FB_BLANK_UNBLANK;
	else
		return FB_BLANK_POWERDOWN;
}

static int ams_delta_lcd_get_contrast(struct lcd_device *dev)
{
	if (!(ams_delta_lcd & AMS_DELTA_LCD_POWER))
		return 0;

	return ams_delta_lcd & AMS_DELTA_MAX_CONTRAST;
}

static struct lcd_ops ams_delta_lcd_ops = {
	.get_power = ams_delta_lcd_get_power,
	.set_power = ams_delta_lcd_set_power,
	.get_contrast = ams_delta_lcd_get_contrast,
	.set_contrast = ams_delta_lcd_set_contrast,
};
#endif


/* omapfb panel section */

static int ams_delta_panel_enable(struct lcd_panel *panel)
{
	gpiod_set_value(gpiod_ndisp, 1);
	gpiod_set_value(gpiod_vblen, 1);
	return 0;
}

static void ams_delta_panel_disable(struct lcd_panel *panel)
{
	gpiod_set_value(gpiod_vblen, 0);
	gpiod_set_value(gpiod_ndisp, 0);
}

static struct lcd_panel ams_delta_panel = {
	.name		= "ams-delta",
	.config		= 0,

	.bpp		= 12,
	.data_lines	= 16,
	.x_res		= 480,
	.y_res		= 320,
	.pixel_clock	= 4687,
	.hsw		= 3,
	.hfp		= 1,
	.hbp		= 1,
	.vsw		= 1,
	.vfp		= 0,
	.vbp		= 0,
	.pcd		= 0,
	.acb		= 37,

	.enable		= ams_delta_panel_enable,
	.disable	= ams_delta_panel_disable,
};


/* platform driver section */

static int ams_delta_panel_probe(struct platform_device *pdev)
{
	struct lcd_device *lcd_device = NULL;
	int ret;

	gpiod_vblen = devm_gpiod_get(&pdev->dev, "vblen", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_vblen)) {
		ret = PTR_ERR(gpiod_vblen);
		dev_err(&pdev->dev, "VBLEN GPIO request failed (%d)\n", ret);
		return ret;
	}

	gpiod_ndisp = devm_gpiod_get(&pdev->dev, "ndisp", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_ndisp)) {
		ret = PTR_ERR(gpiod_ndisp);
		dev_err(&pdev->dev, "NDISP GPIO request failed (%d)\n", ret);
		return ret;
	}

#ifdef CONFIG_LCD_CLASS_DEVICE
	lcd_device = lcd_device_register("omapfb", &pdev->dev, NULL,
						&ams_delta_lcd_ops);

	if (IS_ERR(lcd_device)) {
		ret = PTR_ERR(lcd_device);
		dev_err(&pdev->dev, "failed to register device\n");
		return ret;
	}

	platform_set_drvdata(pdev, lcd_device);
	lcd_device->props.max_contrast = AMS_DELTA_MAX_CONTRAST;
#endif

	ams_delta_lcd_set_contrast(lcd_device, AMS_DELTA_DEFAULT_CONTRAST);
	ams_delta_lcd_set_power(lcd_device, FB_BLANK_UNBLANK);

	omapfb_register_panel(&ams_delta_panel);
	return 0;
}

static struct platform_driver ams_delta_panel_driver = {
	.probe		= ams_delta_panel_probe,
	.driver		= {
		.name	= "lcd_ams_delta",
	},
};

module_platform_driver(ams_delta_panel_driver);

MODULE_AUTHOR("Jonathan McDowell <noodles@earth.li>");
MODULE_DESCRIPTION("LCD panel support for the Amstrad E3 (Delta) videophone");
MODULE_LICENSE("GPL");
