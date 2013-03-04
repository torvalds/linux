/*
 * S6E63M0 AMOLED LCD panel driver.
 *
 * Author: InKi Dae  <inki.dae@samsung.com>
 *
 * Derived from drivers/video/omap/lcd-apollon.c
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
#include <linux/spi/spi.h>
#include <linux/wait.h>

#include "s6e63m0_gamma.h"

#define SLEEPMSEC		0x1000
#define ENDDEF			0x2000
#define	DEFMASK			0xFF00
#define COMMAND_ONLY		0xFE
#define DATA_ONLY		0xFF

#define MIN_BRIGHTNESS		0
#define MAX_BRIGHTNESS		10

struct s6e63m0 {
	struct device			*dev;
	struct spi_device		*spi;
	unsigned int			power;
	unsigned int			current_brightness;
	unsigned int			gamma_mode;
	unsigned int			gamma_table_count;
	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct lcd_platform_data	*lcd_pd;
};

static const unsigned short seq_panel_condition_set[] = {
	0xF8, 0x01,
	DATA_ONLY, 0x27,
	DATA_ONLY, 0x27,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x54,
	DATA_ONLY, 0x9f,
	DATA_ONLY, 0x63,
	DATA_ONLY, 0x86,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0x0d,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,

	ENDDEF, 0x0000
};

static const unsigned short seq_display_condition_set[] = {
	0xf2, 0x02,
	DATA_ONLY, 0x03,
	DATA_ONLY, 0x1c,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x10,

	0xf7, 0x03,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,

	ENDDEF, 0x0000
};

static const unsigned short seq_gamma_setting[] = {
	0xfa, 0x00,
	DATA_ONLY, 0x18,
	DATA_ONLY, 0x08,
	DATA_ONLY, 0x24,
	DATA_ONLY, 0x64,
	DATA_ONLY, 0x56,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0xb6,
	DATA_ONLY, 0xba,
	DATA_ONLY, 0xa8,
	DATA_ONLY, 0xac,
	DATA_ONLY, 0xb1,
	DATA_ONLY, 0x9d,
	DATA_ONLY, 0xc1,
	DATA_ONLY, 0xc1,
	DATA_ONLY, 0xb7,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x9c,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x9f,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xd6,

	0xfa, 0x01,

	ENDDEF, 0x0000
};

static const unsigned short seq_etc_condition_set[] = {
	0xf6, 0x00,
	DATA_ONLY, 0x8c,
	DATA_ONLY, 0x07,

	0xb3, 0xc,

	0xb5, 0x2c,
	DATA_ONLY, 0x12,
	DATA_ONLY, 0x0c,
	DATA_ONLY, 0x0a,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x0e,
	DATA_ONLY, 0x17,
	DATA_ONLY, 0x13,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x2a,
	DATA_ONLY, 0x24,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1b,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x17,

	DATA_ONLY, 0x2b,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x3a,
	DATA_ONLY, 0x34,
	DATA_ONLY, 0x30,
	DATA_ONLY, 0x2c,
	DATA_ONLY, 0x29,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x25,
	DATA_ONLY, 0x23,
	DATA_ONLY, 0x21,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x1e,
	DATA_ONLY, 0x1e,

	0xb6, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x11,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,

	DATA_ONLY, 0x55,
	DATA_ONLY, 0x55,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,

	0xb7, 0x2c,
	DATA_ONLY, 0x12,
	DATA_ONLY, 0x0c,
	DATA_ONLY, 0x0a,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x0e,
	DATA_ONLY, 0x17,
	DATA_ONLY, 0x13,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x2a,
	DATA_ONLY, 0x24,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1b,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x17,

	DATA_ONLY, 0x2b,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x3a,
	DATA_ONLY, 0x34,
	DATA_ONLY, 0x30,
	DATA_ONLY, 0x2c,
	DATA_ONLY, 0x29,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x25,
	DATA_ONLY, 0x23,
	DATA_ONLY, 0x21,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x1e,
	DATA_ONLY, 0x1e,

	0xb8, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x11,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,

	DATA_ONLY, 0x55,
	DATA_ONLY, 0x55,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,

	0xb9, 0x2c,
	DATA_ONLY, 0x12,
	DATA_ONLY, 0x0c,
	DATA_ONLY, 0x0a,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x0e,
	DATA_ONLY, 0x17,
	DATA_ONLY, 0x13,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x2a,
	DATA_ONLY, 0x24,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1b,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x17,

	DATA_ONLY, 0x2b,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x3a,
	DATA_ONLY, 0x34,
	DATA_ONLY, 0x30,
	DATA_ONLY, 0x2c,
	DATA_ONLY, 0x29,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x25,
	DATA_ONLY, 0x23,
	DATA_ONLY, 0x21,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x1e,
	DATA_ONLY, 0x1e,

	0xba, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x11,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,

	DATA_ONLY, 0x55,
	DATA_ONLY, 0x55,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,

	0xc1, 0x4d,
	DATA_ONLY, 0x96,
	DATA_ONLY, 0x1d,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x01,
	DATA_ONLY, 0xdf,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	DATA_ONLY, 0x06,
	DATA_ONLY, 0x09,
	DATA_ONLY, 0x0d,
	DATA_ONLY, 0x0f,
	DATA_ONLY, 0x12,
	DATA_ONLY, 0x15,
	DATA_ONLY, 0x18,

	0xb2, 0x10,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x0b,
	DATA_ONLY, 0x05,

	ENDDEF, 0x0000
};

static const unsigned short seq_acl_on[] = {
	/* ACL on */
	0xc0, 0x01,

	ENDDEF, 0x0000
};

static const unsigned short seq_acl_off[] = {
	/* ACL off */
	0xc0, 0x00,

	ENDDEF, 0x0000
};

static const unsigned short seq_elvss_on[] = {
	/* ELVSS on */
	0xb1, 0x0b,

	ENDDEF, 0x0000
};

static const unsigned short seq_elvss_off[] = {
	/* ELVSS off */
	0xb1, 0x0a,

	ENDDEF, 0x0000
};

static const unsigned short seq_stand_by_off[] = {
	0x11, COMMAND_ONLY,

	ENDDEF, 0x0000
};

static const unsigned short seq_stand_by_on[] = {
	0x10, COMMAND_ONLY,

	ENDDEF, 0x0000
};

static const unsigned short seq_display_on[] = {
	0x29, COMMAND_ONLY,

	ENDDEF, 0x0000
};


static int s6e63m0_spi_write_byte(struct s6e63m0 *lcd, int addr, int data)
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

static int s6e63m0_spi_write(struct s6e63m0 *lcd, unsigned char address,
	unsigned char command)
{
	int ret = 0;

	if (address != DATA_ONLY)
		ret = s6e63m0_spi_write_byte(lcd, 0x0, address);
	if (command != COMMAND_ONLY)
		ret = s6e63m0_spi_write_byte(lcd, 0x1, command);

	return ret;
}

static int s6e63m0_panel_send_sequence(struct s6e63m0 *lcd,
	const unsigned short *wbuf)
{
	int ret = 0, i = 0;

	while ((wbuf[i] & DEFMASK) != ENDDEF) {
		if ((wbuf[i] & DEFMASK) != SLEEPMSEC) {
			ret = s6e63m0_spi_write(lcd, wbuf[i], wbuf[i+1]);
			if (ret)
				break;
		} else {
			msleep(wbuf[i+1]);
		}
		i += 2;
	}

	return ret;
}

static int _s6e63m0_gamma_ctl(struct s6e63m0 *lcd, const unsigned int *gamma)
{
	unsigned int i = 0;
	int ret = 0;

	/* disable gamma table updating. */
	ret = s6e63m0_spi_write(lcd, 0xfa, 0x00);
	if (ret) {
		dev_err(lcd->dev, "failed to disable gamma table updating.\n");
		goto gamma_err;
	}

	for (i = 0 ; i < GAMMA_TABLE_COUNT; i++) {
		ret = s6e63m0_spi_write(lcd, DATA_ONLY, gamma[i]);
		if (ret) {
			dev_err(lcd->dev, "failed to set gamma table.\n");
			goto gamma_err;
		}
	}

	/* update gamma table. */
	ret = s6e63m0_spi_write(lcd, 0xfa, 0x01);
	if (ret)
		dev_err(lcd->dev, "failed to update gamma table.\n");

gamma_err:
	return ret;
}

static int s6e63m0_gamma_ctl(struct s6e63m0 *lcd, int gamma)
{
	int ret = 0;

	ret = _s6e63m0_gamma_ctl(lcd, gamma_table.gamma_22_table[gamma]);

	return ret;
}


static int s6e63m0_ldi_init(struct s6e63m0 *lcd)
{
	int ret, i;
	const unsigned short *init_seq[] = {
		seq_panel_condition_set,
		seq_display_condition_set,
		seq_gamma_setting,
		seq_etc_condition_set,
		seq_acl_on,
		seq_elvss_on,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = s6e63m0_panel_send_sequence(lcd, init_seq[i]);
		if (ret)
			break;
	}

	return ret;
}

static int s6e63m0_ldi_enable(struct s6e63m0 *lcd)
{
	int ret = 0, i;
	const unsigned short *enable_seq[] = {
		seq_stand_by_off,
		seq_display_on,
	};

	for (i = 0; i < ARRAY_SIZE(enable_seq); i++) {
		ret = s6e63m0_panel_send_sequence(lcd, enable_seq[i]);
		if (ret)
			break;
	}

	return ret;
}

static int s6e63m0_ldi_disable(struct s6e63m0 *lcd)
{
	int ret;

	ret = s6e63m0_panel_send_sequence(lcd, seq_stand_by_on);

	return ret;
}

static int s6e63m0_power_is_on(int power)
{
	return power <= FB_BLANK_NORMAL;
}

static int s6e63m0_power_on(struct s6e63m0 *lcd)
{
	int ret = 0;
	struct lcd_platform_data *pd;
	struct backlight_device *bd;

	pd = lcd->lcd_pd;
	bd = lcd->bd;

	if (!pd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EINVAL;
	} else {
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

	ret = s6e63m0_ldi_init(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return ret;
	}

	ret = s6e63m0_ldi_enable(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to enable ldi.\n");
		return ret;
	}

	/* set brightness to current value after power on or resume. */
	ret = s6e63m0_gamma_ctl(lcd, bd->props.brightness);
	if (ret) {
		dev_err(lcd->dev, "lcd gamma setting failed.\n");
		return ret;
	}

	return 0;
}

static int s6e63m0_power_off(struct s6e63m0 *lcd)
{
	int ret;
	struct lcd_platform_data *pd;

	pd = lcd->lcd_pd;

	ret = s6e63m0_ldi_disable(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd setting failed.\n");
		return -EIO;
	}

	msleep(pd->power_off_delay);

	pd->power_on(lcd->ld, 0);

	return 0;
}

static int s6e63m0_power(struct s6e63m0 *lcd, int power)
{
	int ret = 0;

	if (s6e63m0_power_is_on(power) && !s6e63m0_power_is_on(lcd->power))
		ret = s6e63m0_power_on(lcd);
	else if (!s6e63m0_power_is_on(power) && s6e63m0_power_is_on(lcd->power))
		ret = s6e63m0_power_off(lcd);

	if (!ret)
		lcd->power = power;

	return ret;
}

static int s6e63m0_set_power(struct lcd_device *ld, int power)
{
	struct s6e63m0 *lcd = lcd_get_data(ld);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return s6e63m0_power(lcd, power);
}

static int s6e63m0_get_power(struct lcd_device *ld)
{
	struct s6e63m0 *lcd = lcd_get_data(ld);

	return lcd->power;
}

static int s6e63m0_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int s6e63m0_set_brightness(struct backlight_device *bd)
{
	int ret = 0, brightness = bd->props.brightness;
	struct s6e63m0 *lcd = bl_get_data(bd);

	if (brightness < MIN_BRIGHTNESS ||
		brightness > bd->props.max_brightness) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d.\n",
			MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		return -EINVAL;
	}

	ret = s6e63m0_gamma_ctl(lcd, bd->props.brightness);
	if (ret) {
		dev_err(&bd->dev, "lcd brightness setting failed.\n");
		return -EIO;
	}

	return ret;
}

static struct lcd_ops s6e63m0_lcd_ops = {
	.set_power = s6e63m0_set_power,
	.get_power = s6e63m0_get_power,
};

static const struct backlight_ops s6e63m0_backlight_ops  = {
	.get_brightness = s6e63m0_get_brightness,
	.update_status = s6e63m0_set_brightness,
};

static ssize_t s6e63m0_sysfs_show_gamma_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	char temp[10];

	switch (lcd->gamma_mode) {
	case 0:
		sprintf(temp, "2.2 mode\n");
		strcat(buf, temp);
		break;
	case 1:
		sprintf(temp, "1.9 mode\n");
		strcat(buf, temp);
		break;
	case 2:
		sprintf(temp, "1.7 mode\n");
		strcat(buf, temp);
		break;
	default:
		dev_info(dev, "gamma mode could be 0:2.2, 1:1.9 or 2:1.7)n");
		break;
	}

	return strlen(buf);
}

static ssize_t s6e63m0_sysfs_store_gamma_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	struct backlight_device *bd = NULL;
	int brightness, rc;

	rc = kstrtouint(buf, 0, &lcd->gamma_mode);
	if (rc < 0)
		return rc;

	bd = lcd->bd;

	brightness = bd->props.brightness;

	switch (lcd->gamma_mode) {
	case 0:
		_s6e63m0_gamma_ctl(lcd, gamma_table.gamma_22_table[brightness]);
		break;
	case 1:
		_s6e63m0_gamma_ctl(lcd, gamma_table.gamma_19_table[brightness]);
		break;
	case 2:
		_s6e63m0_gamma_ctl(lcd, gamma_table.gamma_17_table[brightness]);
		break;
	default:
		dev_info(dev, "gamma mode could be 0:2.2, 1:1.9 or 2:1.7\n");
		_s6e63m0_gamma_ctl(lcd, gamma_table.gamma_22_table[brightness]);
		break;
	}
	return len;
}

static DEVICE_ATTR(gamma_mode, 0644,
		s6e63m0_sysfs_show_gamma_mode, s6e63m0_sysfs_store_gamma_mode);

static ssize_t s6e63m0_sysfs_show_gamma_table(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", lcd->gamma_table_count);
	strcpy(buf, temp);

	return strlen(buf);
}
static DEVICE_ATTR(gamma_table, 0444,
		s6e63m0_sysfs_show_gamma_table, NULL);

static int s6e63m0_probe(struct spi_device *spi)
{
	int ret = 0;
	struct s6e63m0 *lcd = NULL;
	struct lcd_device *ld = NULL;
	struct backlight_device *bd = NULL;
	struct backlight_properties props;

	lcd = devm_kzalloc(&spi->dev, sizeof(struct s6e63m0), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	/* s6e63m0 lcd panel uses 3-wire 9bits SPI Mode. */
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

	ld = lcd_device_register("s6e63m0", &spi->dev, lcd, &s6e63m0_lcd_ops);
	if (IS_ERR(ld))
		return PTR_ERR(ld);

	lcd->ld = ld;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = MAX_BRIGHTNESS;

	bd = backlight_device_register("s6e63m0bl-bl", &spi->dev, lcd,
		&s6e63m0_backlight_ops, &props);
	if (IS_ERR(bd)) {
		ret =  PTR_ERR(bd);
		goto out_lcd_unregister;
	}

	bd->props.brightness = MAX_BRIGHTNESS;
	lcd->bd = bd;

	/*
	 * it gets gamma table count available so it gets user
	 * know that.
	 */
	lcd->gamma_table_count =
	    sizeof(gamma_table) / (MAX_GAMMA_LEVEL * sizeof(int *));

	ret = device_create_file(&(spi->dev), &dev_attr_gamma_mode);
	if (ret < 0)
		dev_err(&(spi->dev), "failed to add sysfs entries\n");

	ret = device_create_file(&(spi->dev), &dev_attr_gamma_table);
	if (ret < 0)
		dev_err(&(spi->dev), "failed to add sysfs entries\n");

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

		s6e63m0_power(lcd, FB_BLANK_UNBLANK);
	} else {
		lcd->power = FB_BLANK_UNBLANK;
	}

	spi_set_drvdata(spi, lcd);

	dev_info(&spi->dev, "s6e63m0 panel driver has been probed.\n");

	return 0;

out_lcd_unregister:
	lcd_device_unregister(ld);
	return ret;
}

static int s6e63m0_remove(struct spi_device *spi)
{
	struct s6e63m0 *lcd = spi_get_drvdata(spi);

	s6e63m0_power(lcd, FB_BLANK_POWERDOWN);
	device_remove_file(&spi->dev, &dev_attr_gamma_table);
	device_remove_file(&spi->dev, &dev_attr_gamma_mode);
	backlight_device_unregister(lcd->bd);
	lcd_device_unregister(lcd->ld);

	return 0;
}

#if defined(CONFIG_PM)
static int s6e63m0_suspend(struct spi_device *spi, pm_message_t mesg)
{
	struct s6e63m0 *lcd = spi_get_drvdata(spi);

	dev_dbg(&spi->dev, "lcd->power = %d\n", lcd->power);

	/*
	 * when lcd panel is suspend, lcd panel becomes off
	 * regardless of status.
	 */
	return s6e63m0_power(lcd, FB_BLANK_POWERDOWN);
}

static int s6e63m0_resume(struct spi_device *spi)
{
	struct s6e63m0 *lcd = spi_get_drvdata(spi);

	lcd->power = FB_BLANK_POWERDOWN;

	return s6e63m0_power(lcd, FB_BLANK_UNBLANK);
}
#else
#define s6e63m0_suspend		NULL
#define s6e63m0_resume		NULL
#endif

/* Power down all displays on reboot, poweroff or halt. */
static void s6e63m0_shutdown(struct spi_device *spi)
{
	struct s6e63m0 *lcd = spi_get_drvdata(spi);

	s6e63m0_power(lcd, FB_BLANK_POWERDOWN);
}

static struct spi_driver s6e63m0_driver = {
	.driver = {
		.name	= "s6e63m0",
		.owner	= THIS_MODULE,
	},
	.probe		= s6e63m0_probe,
	.remove		= s6e63m0_remove,
	.shutdown	= s6e63m0_shutdown,
	.suspend	= s6e63m0_suspend,
	.resume		= s6e63m0_resume,
};

module_spi_driver(s6e63m0_driver);

MODULE_AUTHOR("InKi Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("S6E63M0 LCD Driver");
MODULE_LICENSE("GPL");

