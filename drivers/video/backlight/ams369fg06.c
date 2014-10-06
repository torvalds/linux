/*
 * ams369fg06 AMOLED LCD panel driver.
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han  <jg1.han@samsung.com>
 *
 * Derived from drivers/video/s6e63m0.c
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
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>

#define SLEEPMSEC		0x1000
#define ENDDEF			0x2000
#define	DEFMASK			0xFF00
#define COMMAND_ONLY		0xFE
#define DATA_ONLY		0xFF

#define MAX_GAMMA_LEVEL		5
#define GAMMA_TABLE_COUNT	21

#define MIN_BRIGHTNESS		0
#define MAX_BRIGHTNESS		255
#define DEFAULT_BRIGHTNESS	150

struct ams369fg06 {
	struct device			*dev;
	struct spi_device		*spi;
	unsigned int			power;
	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct lcd_platform_data	*lcd_pd;
};

static const unsigned short seq_display_on[] = {
	0x14, 0x03,
	ENDDEF, 0x0000
};

static const unsigned short seq_display_off[] = {
	0x14, 0x00,
	ENDDEF, 0x0000
};

static const unsigned short seq_stand_by_on[] = {
	0x1D, 0xA1,
	SLEEPMSEC, 200,
	ENDDEF, 0x0000
};

static const unsigned short seq_stand_by_off[] = {
	0x1D, 0xA0,
	SLEEPMSEC, 250,
	ENDDEF, 0x0000
};

static const unsigned short seq_setting[] = {
	0x31, 0x08,
	0x32, 0x14,
	0x30, 0x02,
	0x27, 0x01,
	0x12, 0x08,
	0x13, 0x08,
	0x15, 0x00,
	0x16, 0x00,

	0xef, 0xd0,
	DATA_ONLY, 0xe8,

	0x39, 0x44,
	0x40, 0x00,
	0x41, 0x3f,
	0x42, 0x2a,
	0x43, 0x27,
	0x44, 0x27,
	0x45, 0x1f,
	0x46, 0x44,
	0x50, 0x00,
	0x51, 0x00,
	0x52, 0x17,
	0x53, 0x24,
	0x54, 0x26,
	0x55, 0x1f,
	0x56, 0x43,
	0x60, 0x00,
	0x61, 0x3f,
	0x62, 0x2a,
	0x63, 0x25,
	0x64, 0x24,
	0x65, 0x1b,
	0x66, 0x5c,

	0x17, 0x22,
	0x18, 0x33,
	0x19, 0x03,
	0x1a, 0x01,
	0x22, 0xa4,
	0x23, 0x00,
	0x26, 0xa0,

	0x1d, 0xa0,
	SLEEPMSEC, 300,

	0x14, 0x03,

	ENDDEF, 0x0000
};

/* gamma value: 2.2 */
static const unsigned int ams369fg06_22_250[] = {
	0x00, 0x3f, 0x2a, 0x27, 0x27, 0x1f, 0x44,
	0x00, 0x00, 0x17, 0x24, 0x26, 0x1f, 0x43,
	0x00, 0x3f, 0x2a, 0x25, 0x24, 0x1b, 0x5c,
};

static const unsigned int ams369fg06_22_200[] = {
	0x00, 0x3f, 0x28, 0x29, 0x27, 0x21, 0x3e,
	0x00, 0x00, 0x10, 0x25, 0x27, 0x20, 0x3d,
	0x00, 0x3f, 0x28, 0x27, 0x25, 0x1d, 0x53,
};

static const unsigned int ams369fg06_22_150[] = {
	0x00, 0x3f, 0x2d, 0x29, 0x28, 0x23, 0x37,
	0x00, 0x00, 0x0b, 0x25, 0x28, 0x22, 0x36,
	0x00, 0x3f, 0x2b, 0x28, 0x26, 0x1f, 0x4a,
};

static const unsigned int ams369fg06_22_100[] = {
	0x00, 0x3f, 0x30, 0x2a, 0x2b, 0x24, 0x2f,
	0x00, 0x00, 0x00, 0x25, 0x29, 0x24, 0x2e,
	0x00, 0x3f, 0x2f, 0x29, 0x29, 0x21, 0x3f,
};

static const unsigned int ams369fg06_22_50[] = {
	0x00, 0x3f, 0x3c, 0x2c, 0x2d, 0x27, 0x24,
	0x00, 0x00, 0x00, 0x22, 0x2a, 0x27, 0x23,
	0x00, 0x3f, 0x3b, 0x2c, 0x2b, 0x24, 0x31,
};

struct ams369fg06_gamma {
	unsigned int *gamma_22_table[MAX_GAMMA_LEVEL];
};

static struct ams369fg06_gamma gamma_table = {
	.gamma_22_table[0] = (unsigned int *)&ams369fg06_22_50,
	.gamma_22_table[1] = (unsigned int *)&ams369fg06_22_100,
	.gamma_22_table[2] = (unsigned int *)&ams369fg06_22_150,
	.gamma_22_table[3] = (unsigned int *)&ams369fg06_22_200,
	.gamma_22_table[4] = (unsigned int *)&ams369fg06_22_250,
};

static int ams369fg06_spi_write_byte(struct ams369fg06 *lcd, int addr, int data)
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

static int ams369fg06_spi_write(struct ams369fg06 *lcd, unsigned char address,
	unsigned char command)
{
	int ret = 0;

	if (address != DATA_ONLY)
		ret = ams369fg06_spi_write_byte(lcd, 0x70, address);
	if (command != COMMAND_ONLY)
		ret = ams369fg06_spi_write_byte(lcd, 0x72, command);

	return ret;
}

static int ams369fg06_panel_send_sequence(struct ams369fg06 *lcd,
	const unsigned short *wbuf)
{
	int ret = 0, i = 0;

	while ((wbuf[i] & DEFMASK) != ENDDEF) {
		if ((wbuf[i] & DEFMASK) != SLEEPMSEC) {
			ret = ams369fg06_spi_write(lcd, wbuf[i], wbuf[i+1]);
			if (ret)
				break;
		} else {
			msleep(wbuf[i+1]);
		}
		i += 2;
	}

	return ret;
}

static int _ams369fg06_gamma_ctl(struct ams369fg06 *lcd,
	const unsigned int *gamma)
{
	unsigned int i = 0;
	int ret = 0;

	for (i = 0 ; i < GAMMA_TABLE_COUNT / 3; i++) {
		ret = ams369fg06_spi_write(lcd, 0x40 + i, gamma[i]);
		ret = ams369fg06_spi_write(lcd, 0x50 + i, gamma[i+7*1]);
		ret = ams369fg06_spi_write(lcd, 0x60 + i, gamma[i+7*2]);
		if (ret) {
			dev_err(lcd->dev, "failed to set gamma table.\n");
			goto gamma_err;
		}
	}

gamma_err:
	return ret;
}

static int ams369fg06_gamma_ctl(struct ams369fg06 *lcd, int brightness)
{
	int ret = 0;
	int gamma = 0;

	if ((brightness >= 0) && (brightness <= 50))
		gamma = 0;
	else if ((brightness > 50) && (brightness <= 100))
		gamma = 1;
	else if ((brightness > 100) && (brightness <= 150))
		gamma = 2;
	else if ((brightness > 150) && (brightness <= 200))
		gamma = 3;
	else if ((brightness > 200) && (brightness <= 255))
		gamma = 4;

	ret = _ams369fg06_gamma_ctl(lcd, gamma_table.gamma_22_table[gamma]);

	return ret;
}

static int ams369fg06_ldi_init(struct ams369fg06 *lcd)
{
	int ret, i;
	static const unsigned short *init_seq[] = {
		seq_setting,
		seq_stand_by_off,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = ams369fg06_panel_send_sequence(lcd, init_seq[i]);
		if (ret)
			break;
	}

	return ret;
}

static int ams369fg06_ldi_enable(struct ams369fg06 *lcd)
{
	int ret, i;
	static const unsigned short *init_seq[] = {
		seq_stand_by_off,
		seq_display_on,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = ams369fg06_panel_send_sequence(lcd, init_seq[i]);
		if (ret)
			break;
	}

	return ret;
}

static int ams369fg06_ldi_disable(struct ams369fg06 *lcd)
{
	int ret, i;

	static const unsigned short *init_seq[] = {
		seq_display_off,
		seq_stand_by_on,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = ams369fg06_panel_send_sequence(lcd, init_seq[i]);
		if (ret)
			break;
	}

	return ret;
}

static int ams369fg06_power_is_on(int power)
{
	return power <= FB_BLANK_NORMAL;
}

static int ams369fg06_power_on(struct ams369fg06 *lcd)
{
	int ret = 0;
	struct lcd_platform_data *pd;
	struct backlight_device *bd;

	pd = lcd->lcd_pd;
	bd = lcd->bd;

	if (pd->power_on) {
		pd->power_on(lcd->ld, 1);
		msleep(pd->power_on_delay);
	}

	if (!pd->reset) {
		dev_err(lcd->dev, "reset is NULL.\n");
		return -EINVAL;
	} else {
		pd->reset(lcd->ld);
		msleep(pd->reset_delay);
	}

	ret = ams369fg06_ldi_init(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return ret;
	}

	ret = ams369fg06_ldi_enable(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to enable ldi.\n");
		return ret;
	}

	/* set brightness to current value after power on or resume. */
	ret = ams369fg06_gamma_ctl(lcd, bd->props.brightness);
	if (ret) {
		dev_err(lcd->dev, "lcd gamma setting failed.\n");
		return ret;
	}

	return 0;
}

static int ams369fg06_power_off(struct ams369fg06 *lcd)
{
	int ret;
	struct lcd_platform_data *pd;

	pd = lcd->lcd_pd;

	ret = ams369fg06_ldi_disable(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd setting failed.\n");
		return -EIO;
	}

	msleep(pd->power_off_delay);

	if (pd->power_on)
		pd->power_on(lcd->ld, 0);

	return 0;
}

static int ams369fg06_power(struct ams369fg06 *lcd, int power)
{
	int ret = 0;

	if (ams369fg06_power_is_on(power) &&
		!ams369fg06_power_is_on(lcd->power))
		ret = ams369fg06_power_on(lcd);
	else if (!ams369fg06_power_is_on(power) &&
		ams369fg06_power_is_on(lcd->power))
		ret = ams369fg06_power_off(lcd);

	if (!ret)
		lcd->power = power;

	return ret;
}

static int ams369fg06_get_power(struct lcd_device *ld)
{
	struct ams369fg06 *lcd = lcd_get_data(ld);

	return lcd->power;
}

static int ams369fg06_set_power(struct lcd_device *ld, int power)
{
	struct ams369fg06 *lcd = lcd_get_data(ld);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return ams369fg06_power(lcd, power);
}

static int ams369fg06_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	int brightness = bd->props.brightness;
	struct ams369fg06 *lcd = bl_get_data(bd);

	if (brightness < MIN_BRIGHTNESS ||
		brightness > bd->props.max_brightness) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d.\n",
			MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		return -EINVAL;
	}

	ret = ams369fg06_gamma_ctl(lcd, bd->props.brightness);
	if (ret) {
		dev_err(&bd->dev, "lcd brightness setting failed.\n");
		return -EIO;
	}

	return ret;
}

static struct lcd_ops ams369fg06_lcd_ops = {
	.get_power = ams369fg06_get_power,
	.set_power = ams369fg06_set_power,
};

static const struct backlight_ops ams369fg06_backlight_ops = {
	.update_status = ams369fg06_set_brightness,
};

static int ams369fg06_probe(struct spi_device *spi)
{
	int ret = 0;
	struct ams369fg06 *lcd = NULL;
	struct lcd_device *ld = NULL;
	struct backlight_device *bd = NULL;
	struct backlight_properties props;

	lcd = devm_kzalloc(&spi->dev, sizeof(struct ams369fg06), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	/* ams369fg06 lcd panel uses 3-wire 16bits SPI Mode. */
	spi->bits_per_word = 16;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi setup failed.\n");
		return ret;
	}

	lcd->spi = spi;
	lcd->dev = &spi->dev;

	lcd->lcd_pd = dev_get_platdata(&spi->dev);
	if (!lcd->lcd_pd) {
		dev_err(&spi->dev, "platform data is NULL\n");
		return -EINVAL;
	}

	ld = devm_lcd_device_register(&spi->dev, "ams369fg06", &spi->dev, lcd,
					&ams369fg06_lcd_ops);
	if (IS_ERR(ld))
		return PTR_ERR(ld);

	lcd->ld = ld;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = MAX_BRIGHTNESS;

	bd = devm_backlight_device_register(&spi->dev, "ams369fg06-bl",
					&spi->dev, lcd,
					&ams369fg06_backlight_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	bd->props.brightness = DEFAULT_BRIGHTNESS;
	lcd->bd = bd;

	if (!lcd->lcd_pd->lcd_enabled) {
		/*
		 * if lcd panel was off from bootloader then
		 * current lcd status is powerdown and then
		 * it enables lcd panel.
		 */
		lcd->power = FB_BLANK_POWERDOWN;

		ams369fg06_power(lcd, FB_BLANK_UNBLANK);
	} else {
		lcd->power = FB_BLANK_UNBLANK;
	}

	spi_set_drvdata(spi, lcd);

	dev_info(&spi->dev, "ams369fg06 panel driver has been probed.\n");

	return 0;
}

static int ams369fg06_remove(struct spi_device *spi)
{
	struct ams369fg06 *lcd = spi_get_drvdata(spi);

	ams369fg06_power(lcd, FB_BLANK_POWERDOWN);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ams369fg06_suspend(struct device *dev)
{
	struct ams369fg06 *lcd = dev_get_drvdata(dev);

	dev_dbg(dev, "lcd->power = %d\n", lcd->power);

	/*
	 * when lcd panel is suspend, lcd panel becomes off
	 * regardless of status.
	 */
	return ams369fg06_power(lcd, FB_BLANK_POWERDOWN);
}

static int ams369fg06_resume(struct device *dev)
{
	struct ams369fg06 *lcd = dev_get_drvdata(dev);

	lcd->power = FB_BLANK_POWERDOWN;

	return ams369fg06_power(lcd, FB_BLANK_UNBLANK);
}
#endif

static SIMPLE_DEV_PM_OPS(ams369fg06_pm_ops, ams369fg06_suspend,
			ams369fg06_resume);

static void ams369fg06_shutdown(struct spi_device *spi)
{
	struct ams369fg06 *lcd = spi_get_drvdata(spi);

	ams369fg06_power(lcd, FB_BLANK_POWERDOWN);
}

static struct spi_driver ams369fg06_driver = {
	.driver = {
		.name	= "ams369fg06",
		.owner	= THIS_MODULE,
		.pm	= &ams369fg06_pm_ops,
	},
	.probe		= ams369fg06_probe,
	.remove		= ams369fg06_remove,
	.shutdown	= ams369fg06_shutdown,
};

module_spi_driver(ams369fg06_driver);

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("ams369fg06 LCD Driver");
MODULE_LICENSE("GPL");
