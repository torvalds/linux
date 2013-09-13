/*
 * ld9040 AMOLED LCD panel driver.
 *
 * Copyright (c) 2011 Samsung Electronics
 * Author: Donghwa Lee  <dh09.lee@samsung.com>
 * Derived from drivers/video/backlight/s6e63m0.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>

#include "ld9040_gamma.h"

#define SLEEPMSEC		0x1000
#define ENDDEF			0x2000
#define	DEFMASK			0xFF00
#define COMMAND_ONLY		0xFE
#define DATA_ONLY		0xFF

#define MIN_BRIGHTNESS		0
#define MAX_BRIGHTNESS		24

struct ld9040 {
	struct device			*dev;
	struct spi_device		*spi;
	unsigned int			power;
	unsigned int			current_brightness;

	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct lcd_platform_data	*lcd_pd;

	struct mutex			lock;
	bool  enabled;
};

static struct regulator_bulk_data supplies[] = {
	{ .supply = "vdd3", },
	{ .supply = "vci", },
};

static void ld9040_regulator_enable(struct ld9040 *lcd)
{
	int ret = 0;
	struct lcd_platform_data *pd = NULL;

	pd = lcd->lcd_pd;
	mutex_lock(&lcd->lock);
	if (!lcd->enabled) {
		ret = regulator_bulk_enable(ARRAY_SIZE(supplies), supplies);
		if (ret)
			goto out;

		lcd->enabled = true;
	}
	msleep(pd->power_on_delay);
out:
	mutex_unlock(&lcd->lock);
}

static void ld9040_regulator_disable(struct ld9040 *lcd)
{
	int ret = 0;

	mutex_lock(&lcd->lock);
	if (lcd->enabled) {
		ret = regulator_bulk_disable(ARRAY_SIZE(supplies), supplies);
		if (ret)
			goto out;

		lcd->enabled = false;
	}
out:
	mutex_unlock(&lcd->lock);
}

static const unsigned short seq_swreset[] = {
	0x01, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short seq_user_setting[] = {
	0xF0, 0x5A,

	DATA_ONLY, 0x5A,
	ENDDEF, 0x00
};

static const unsigned short seq_elvss_on[] = {
	0xB1, 0x0D,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x16,
	ENDDEF, 0x00
};

static const unsigned short seq_gtcon[] = {
	0xF7, 0x09,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	ENDDEF, 0x00
};

static const unsigned short seq_panel_condition[] = {
	0xF8, 0x05,

	DATA_ONLY, 0x65,
	DATA_ONLY, 0x96,
	DATA_ONLY, 0x71,
	DATA_ONLY, 0x7D,
	DATA_ONLY, 0x19,
	DATA_ONLY, 0x3B,
	DATA_ONLY, 0x0D,
	DATA_ONLY, 0x19,
	DATA_ONLY, 0x7E,
	DATA_ONLY, 0x0D,
	DATA_ONLY, 0xE2,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x7E,
	DATA_ONLY, 0x7D,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x02,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_gamma_set1[] = {
	0xF9, 0x00,

	DATA_ONLY, 0xA7,
	DATA_ONLY, 0xB4,
	DATA_ONLY, 0xAE,
	DATA_ONLY, 0xBF,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x91,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xB2,
	DATA_ONLY, 0xB4,
	DATA_ONLY, 0xAA,
	DATA_ONLY, 0xBB,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xAC,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xB3,
	DATA_ONLY, 0xB1,
	DATA_ONLY, 0xAA,
	DATA_ONLY, 0xBC,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xB3,
	ENDDEF, 0x00
};

static const unsigned short seq_gamma_ctrl[] = {
	0xFB, 0x02,

	DATA_ONLY, 0x5A,
	ENDDEF, 0x00
};

static const unsigned short seq_gamma_start[] = {
	0xF9, COMMAND_ONLY,

	ENDDEF, 0x00
};

static const unsigned short seq_apon[] = {
	0xF3, 0x00,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x0A,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_display_ctrl[] = {
	0xF2, 0x02,

	DATA_ONLY, 0x08,
	DATA_ONLY, 0x08,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x10,
	ENDDEF, 0x00
};

static const unsigned short seq_manual_pwr[] = {
	0xB0, 0x04,
	ENDDEF, 0x00
};

static const unsigned short seq_pwr_ctrl[] = {
	0xF4, 0x0A,

	DATA_ONLY, 0x87,
	DATA_ONLY, 0x25,
	DATA_ONLY, 0x6A,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x02,
	DATA_ONLY, 0x88,
	ENDDEF, 0x00
};

static const unsigned short seq_sleep_out[] = {
	0x11, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short seq_sleep_in[] = {
	0x10, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short seq_display_on[] = {
	0x29, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short seq_display_off[] = {
	0x28, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short seq_vci1_1st_en[] = {
	0xF3, 0x10,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_vl1_en[] = {
	0xF3, 0x11,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_vl2_en[] = {
	0xF3, 0x13,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_vci1_2nd_en[] = {
	0xF3, 0x33,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_vl3_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_vreg1_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0x01,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_vgh_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0x11,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_vgl_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0x31,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short seq_vmos_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xB1,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	ENDDEF, 0x00
};

static const unsigned short seq_vint_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xF1,
	/* DATA_ONLY, 0x71,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short seq_vbh_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xF9,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	ENDDEF, 0x00
};

static const unsigned short seq_vbl_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFD,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	ENDDEF, 0x00
};

static const unsigned short seq_gam_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short seq_sd_amp_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x80,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short seq_gls_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x81,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short seq_els_en[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x83,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short seq_el_on[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x87,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static int ld9040_spi_write_byte(struct ld9040 *lcd, int addr, int data)
{
	u16 buf[1];
	struct spi_message msg;

	struct spi_transfer xfer = {
		.len		= 2,
		.tx_buf		= buf,
	};

	buf[0] = (addr << 8) | data;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(lcd->spi, &msg);
}

static int ld9040_spi_write(struct ld9040 *lcd, unsigned char address,
	unsigned char command)
{
	int ret = 0;

	if (address != DATA_ONLY)
		ret = ld9040_spi_write_byte(lcd, 0x0, address);
	if (command != COMMAND_ONLY)
		ret = ld9040_spi_write_byte(lcd, 0x1, command);

	return ret;
}

static int ld9040_panel_send_sequence(struct ld9040 *lcd,
	const unsigned short *wbuf)
{
	int ret = 0, i = 0;

	while ((wbuf[i] & DEFMASK) != ENDDEF) {
		if ((wbuf[i] & DEFMASK) != SLEEPMSEC) {
			ret = ld9040_spi_write(lcd, wbuf[i], wbuf[i+1]);
			if (ret)
				break;
		} else {
			msleep(wbuf[i+1]);
		}
		i += 2;
	}

	return ret;
}

static int _ld9040_gamma_ctl(struct ld9040 *lcd, const unsigned int *gamma)
{
	unsigned int i = 0;
	int ret = 0;

	/* start gamma table updating. */
	ret = ld9040_panel_send_sequence(lcd, seq_gamma_start);
	if (ret) {
		dev_err(lcd->dev, "failed to disable gamma table updating.\n");
		goto gamma_err;
	}

	for (i = 0 ; i < GAMMA_TABLE_COUNT; i++) {
		ret = ld9040_spi_write(lcd, DATA_ONLY, gamma[i]);
		if (ret) {
			dev_err(lcd->dev, "failed to set gamma table.\n");
			goto gamma_err;
		}
	}

	/* update gamma table. */
	ret = ld9040_panel_send_sequence(lcd, seq_gamma_ctrl);
	if (ret)
		dev_err(lcd->dev, "failed to update gamma table.\n");

gamma_err:
	return ret;
}

static int ld9040_gamma_ctl(struct ld9040 *lcd, int gamma)
{
	return _ld9040_gamma_ctl(lcd, gamma_table.gamma_22_table[gamma]);
}

static int ld9040_ldi_init(struct ld9040 *lcd)
{
	int ret, i;
	static const unsigned short *init_seq[] = {
		seq_user_setting,
		seq_panel_condition,
		seq_display_ctrl,
		seq_manual_pwr,
		seq_elvss_on,
		seq_gtcon,
		seq_gamma_set1,
		seq_gamma_ctrl,
		seq_sleep_out,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = ld9040_panel_send_sequence(lcd, init_seq[i]);
		/* workaround: minimum delay time for transferring CMD */
		usleep_range(300, 310);
		if (ret)
			break;
	}

	return ret;
}

static int ld9040_ldi_enable(struct ld9040 *lcd)
{
	return ld9040_panel_send_sequence(lcd, seq_display_on);
}

static int ld9040_ldi_disable(struct ld9040 *lcd)
{
	int ret;

	ret = ld9040_panel_send_sequence(lcd, seq_display_off);
	ret = ld9040_panel_send_sequence(lcd, seq_sleep_in);

	return ret;
}

static int ld9040_power_is_on(int power)
{
	return power <= FB_BLANK_NORMAL;
}

static int ld9040_power_on(struct ld9040 *lcd)
{
	int ret = 0;
	struct lcd_platform_data *pd;

	pd = lcd->lcd_pd;

	/* lcd power on */
	ld9040_regulator_enable(lcd);

	if (!pd->reset) {
		dev_err(lcd->dev, "reset is NULL.\n");
		return -EINVAL;
	} else {
		pd->reset(lcd->ld);
		msleep(pd->reset_delay);
	}

	ret = ld9040_ldi_init(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return ret;
	}

	ret = ld9040_ldi_enable(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to enable ldi.\n");
		return ret;
	}

	return 0;
}

static int ld9040_power_off(struct ld9040 *lcd)
{
	int ret;
	struct lcd_platform_data *pd;

	pd = lcd->lcd_pd;

	ret = ld9040_ldi_disable(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd setting failed.\n");
		return -EIO;
	}

	msleep(pd->power_off_delay);

	/* lcd power off */
	ld9040_regulator_disable(lcd);

	return 0;
}

static int ld9040_power(struct ld9040 *lcd, int power)
{
	int ret = 0;

	if (ld9040_power_is_on(power) && !ld9040_power_is_on(lcd->power))
		ret = ld9040_power_on(lcd);
	else if (!ld9040_power_is_on(power) && ld9040_power_is_on(lcd->power))
		ret = ld9040_power_off(lcd);

	if (!ret)
		lcd->power = power;

	return ret;
}

static int ld9040_set_power(struct lcd_device *ld, int power)
{
	struct ld9040 *lcd = lcd_get_data(ld);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return ld9040_power(lcd, power);
}

static int ld9040_get_power(struct lcd_device *ld)
{
	struct ld9040 *lcd = lcd_get_data(ld);

	return lcd->power;
}

static int ld9040_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int ld9040_set_brightness(struct backlight_device *bd)
{
	int ret = 0, brightness = bd->props.brightness;
	struct ld9040 *lcd = bl_get_data(bd);

	if (brightness < MIN_BRIGHTNESS ||
		brightness > bd->props.max_brightness) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d.\n",
			MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		return -EINVAL;
	}

	ret = ld9040_gamma_ctl(lcd, bd->props.brightness);
	if (ret) {
		dev_err(&bd->dev, "lcd brightness setting failed.\n");
		return -EIO;
	}

	return ret;
}

static struct lcd_ops ld9040_lcd_ops = {
	.set_power = ld9040_set_power,
	.get_power = ld9040_get_power,
};

static const struct backlight_ops ld9040_backlight_ops  = {
	.get_brightness = ld9040_get_brightness,
	.update_status = ld9040_set_brightness,
};

static int ld9040_probe(struct spi_device *spi)
{
	int ret = 0;
	struct ld9040 *lcd = NULL;
	struct lcd_device *ld = NULL;
	struct backlight_device *bd = NULL;
	struct backlight_properties props;

	lcd = devm_kzalloc(&spi->dev, sizeof(struct ld9040), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	/* ld9040 lcd panel uses 3-wire 9bits SPI Mode. */
	spi->bits_per_word = 9;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi setup failed.\n");
		return ret;
	}

	lcd->spi = spi;
	lcd->dev = &spi->dev;

	lcd->lcd_pd = spi->dev.platform_data;
	if (!lcd->lcd_pd) {
		dev_err(&spi->dev, "platform data is NULL.\n");
		return -EINVAL;
	}

	mutex_init(&lcd->lock);

	ret = devm_regulator_bulk_get(lcd->dev, ARRAY_SIZE(supplies), supplies);
	if (ret) {
		dev_err(lcd->dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	ld = lcd_device_register("ld9040", &spi->dev, lcd, &ld9040_lcd_ops);
	if (IS_ERR(ld))
		return PTR_ERR(ld);

	lcd->ld = ld;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = MAX_BRIGHTNESS;

	bd = backlight_device_register("ld9040-bl", &spi->dev,
		lcd, &ld9040_backlight_ops, &props);
	if (IS_ERR(bd)) {
		ret = PTR_ERR(bd);
		goto out_unregister_lcd;
	}

	bd->props.brightness = MAX_BRIGHTNESS;
	lcd->bd = bd;

	/*
	 * if lcd panel was on from bootloader like u-boot then
	 * do not lcd on.
	 */
	if (!lcd->lcd_pd->lcd_enabled) {
		/*
		 * if lcd panel was off from bootloader then
		 * current lcd status is powerdown and then
		 * it enables lcd panel.
		 */
		lcd->power = FB_BLANK_POWERDOWN;

		ld9040_power(lcd, FB_BLANK_UNBLANK);
	} else {
		lcd->power = FB_BLANK_UNBLANK;
	}

	spi_set_drvdata(spi, lcd);

	dev_info(&spi->dev, "ld9040 panel driver has been probed.\n");
	return 0;

out_unregister_lcd:
	lcd_device_unregister(lcd->ld);

	return ret;
}

static int ld9040_remove(struct spi_device *spi)
{
	struct ld9040 *lcd = spi_get_drvdata(spi);

	ld9040_power(lcd, FB_BLANK_POWERDOWN);
	backlight_device_unregister(lcd->bd);
	lcd_device_unregister(lcd->ld);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ld9040_suspend(struct device *dev)
{
	struct ld9040 *lcd = dev_get_drvdata(dev);

	dev_dbg(dev, "lcd->power = %d\n", lcd->power);

	/*
	 * when lcd panel is suspend, lcd panel becomes off
	 * regardless of status.
	 */
	return ld9040_power(lcd, FB_BLANK_POWERDOWN);
}

static int ld9040_resume(struct device *dev)
{
	struct ld9040 *lcd = dev_get_drvdata(dev);

	lcd->power = FB_BLANK_POWERDOWN;

	return ld9040_power(lcd, FB_BLANK_UNBLANK);
}
#endif

static SIMPLE_DEV_PM_OPS(ld9040_pm_ops, ld9040_suspend, ld9040_resume);

/* Power down all displays on reboot, poweroff or halt. */
static void ld9040_shutdown(struct spi_device *spi)
{
	struct ld9040 *lcd = spi_get_drvdata(spi);

	ld9040_power(lcd, FB_BLANK_POWERDOWN);
}

static struct spi_driver ld9040_driver = {
	.driver = {
		.name	= "ld9040",
		.owner	= THIS_MODULE,
		.pm	= &ld9040_pm_ops,
	},
	.probe		= ld9040_probe,
	.remove		= ld9040_remove,
	.shutdown	= ld9040_shutdown,
};

module_spi_driver(ld9040_driver);

MODULE_AUTHOR("Donghwa Lee <dh09.lee@samsung.com>");
MODULE_DESCRIPTION("ld9040 LCD Driver");
MODULE_LICENSE("GPL");
