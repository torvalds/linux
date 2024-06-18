// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * LCD driver for HP Jornada 700 series (710/720/728)
 * Copyright (C) 2006-2009 Kristoffer Ericson <kristoffer.ericson@gmail.com>
 */

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <mach/jornada720.h>
#include <mach/hardware.h>

#include <video/s1d13xxxfb.h>

#define LCD_MAX_CONTRAST	0xff
#define LCD_DEF_CONTRAST	0x80

static int jornada_lcd_get_power(struct lcd_device *ld)
{
	return PPSR & PPC_LDD2 ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
}

static int jornada_lcd_get_contrast(struct lcd_device *ld)
{
	int ret;

	if (jornada_lcd_get_power(ld) != FB_BLANK_UNBLANK)
		return 0;

	jornada_ssp_start();

	if (jornada_ssp_byte(GETCONTRAST) == TXDUMMY) {
		ret = jornada_ssp_byte(TXDUMMY);
		goto success;
	}

	dev_err(&ld->dev, "failed to set contrast\n");
	ret = -ETIMEDOUT;

success:
	jornada_ssp_end();
	return ret;
}

static int jornada_lcd_set_contrast(struct lcd_device *ld, int value)
{
	int ret = 0;

	jornada_ssp_start();

	/* start by sending our set contrast cmd to mcu */
	if (jornada_ssp_byte(SETCONTRAST) == TXDUMMY) {
		/* if successful push the new value */
		if (jornada_ssp_byte(value) == TXDUMMY)
			goto success;
	}

	dev_err(&ld->dev, "failed to set contrast\n");
	ret = -ETIMEDOUT;

success:
	jornada_ssp_end();
	return ret;
}

static int jornada_lcd_set_power(struct lcd_device *ld, int power)
{
	if (power != FB_BLANK_UNBLANK) {
		PPSR &= ~PPC_LDD2;
		PPDR |= PPC_LDD2;
	} else {
		PPSR |= PPC_LDD2;
	}

	return 0;
}

static const struct lcd_ops jornada_lcd_props = {
	.get_contrast = jornada_lcd_get_contrast,
	.set_contrast = jornada_lcd_set_contrast,
	.get_power = jornada_lcd_get_power,
	.set_power = jornada_lcd_set_power,
};

static int jornada_lcd_probe(struct platform_device *pdev)
{
	struct lcd_device *lcd_device;
	int ret;

	lcd_device = devm_lcd_device_register(&pdev->dev, S1D_DEVICENAME,
					&pdev->dev, NULL, &jornada_lcd_props);

	if (IS_ERR(lcd_device)) {
		ret = PTR_ERR(lcd_device);
		dev_err(&pdev->dev, "failed to register device\n");
		return ret;
	}

	platform_set_drvdata(pdev, lcd_device);

	/* lets set our default values */
	jornada_lcd_set_contrast(lcd_device, LCD_DEF_CONTRAST);
	jornada_lcd_set_power(lcd_device, FB_BLANK_UNBLANK);
	/* give it some time to startup */
	msleep(100);

	return 0;
}

static struct platform_driver jornada_lcd_driver = {
	.probe	= jornada_lcd_probe,
	.driver	= {
		.name	= "jornada_lcd",
	},
};

module_platform_driver(jornada_lcd_driver);

MODULE_AUTHOR("Kristoffer Ericson <kristoffer.ericson@gmail.com>");
MODULE_DESCRIPTION("HP Jornada 710/720/728 LCD driver");
MODULE_LICENSE("GPL");
