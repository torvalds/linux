/* linux/drivers/video/backlight/lms501kf03.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * LMS501KF03 5.0" WVGA Landscape LCD module driver for the SMDK
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/wait.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/lcd.h>
#include <linux/backlight.h>

#define ENDDEF			0xFF00
#define COMMAND_ONLY	0x00
#define DATA_ONLY		0x01

#define MIN_BRIGHTNESS		0
#define MAX_BRIGHTNESS		255
#define DEFAULT_BRIGHTNESS	150

#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

struct lms501kf03 {
	struct device			*dev;
	struct spi_device		*spi;
	unsigned int			power;
	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct lcd_platform_data	*lcd_pd;
};

const unsigned short SEQ_PASSWORD[] = {
	0xb9, 0xff, 0x83, 0x69,
	ENDDEF
};

const unsigned short SEQ_POWER[] = {
	0xb1, 0x01, 0x00, 0x34, 0x06, 0x00, 0x14, 0x14, 0x20, 0x28,
	0x12, 0x12, 0x17, 0x0a, 0x01, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
	ENDDEF
};

const unsigned short SEQ_DISPLAY[] = {
	0xb2, 0x00, 0x2b, 0x03, 0x03, 0x70, 0x00, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x03, 0x03, 0x00, 0x01,
	ENDDEF
};

const unsigned short SEQ_RGB_IF[] = {
	0xb3, 0x09,
	ENDDEF
};

const unsigned short SEQ_DISPLAY_INV[] = {
	0xb4, 0x01, 0x08, 0x77, 0x0e, 0x06,
	ENDDEF
};

const unsigned short SEQ_VCOM[] = {
	0xb6, 0x4c, 0x2e,
	ENDDEF
};

const unsigned short SEQ_GATE[] = {
	0xd5, 0x00, 0x05, 0x03, 0x29, 0x01, 0x07, 0x17, 0x68, 0x13,
	0x37, 0x20, 0x31, 0x8a, 0x46, 0x9b, 0x57, 0x13, 0x02, 0x75,
	0xb9, 0x64, 0xa8, 0x07, 0x0f, 0x04, 0x07,
	ENDDEF
};

const unsigned short SEQ_PANEL[] = {
	0xcc, 0x02,
	ENDDEF
};

const unsigned short SEQ_COL_MOD[] = {
	0x3a, 0x77,
	ENDDEF
};

const unsigned short SEQ_W_GAMMA[] = {
	0xe0, 0x00, 0x04, 0x09, 0x0f, 0x1f, 0x3f, 0x1f, 0x2f, 0x0a,
	0x0f, 0x10, 0x16, 0x18, 0x16, 0x17, 0x0d, 0x15, 0x00, 0x04,
	0x09, 0x0f, 0x38, 0x3f, 0x20, 0x39, 0x0a, 0x0f, 0x10, 0x16,
	0x18, 0x16, 0x17, 0x0d, 0x15,
	ENDDEF
};

const unsigned short SEQ_RGB_GAMMA[] = {
	0xc1, 0x01, 0x03, 0x07, 0x0f, 0x1a, 0x22, 0x2c, 0x33, 0x3c,
	0x46, 0x4f, 0x58, 0x60, 0x69, 0x71, 0x79, 0x82, 0x89, 0x92,
	0x9a, 0xa1, 0xa9, 0xb1, 0xb9, 0xc1, 0xc9, 0xcf, 0xd6, 0xde,
	0xe5, 0xec, 0xf3, 0xf9, 0xff, 0xdd, 0x39, 0x07, 0x1c, 0xcb,
	0xab, 0x5f, 0x49, 0x80, 0x03, 0x07, 0x0f, 0x19, 0x20, 0x2a,
	0x31, 0x39, 0x42, 0x4b, 0x53, 0x5b, 0x63, 0x6b, 0x73, 0x7b,
	0x83, 0x8a, 0x92, 0x9b, 0xa2, 0xaa, 0xb2, 0xba, 0xc2, 0xca,
	0xd0, 0xd8, 0xe1, 0xe8, 0xf0, 0xf8, 0xff, 0xf7, 0xd8, 0xbe,
	0xa7, 0x39, 0x40, 0x85, 0x8c, 0xc0, 0x04, 0x07, 0x0c, 0x17,
	0x1c, 0x23, 0x2b, 0x34, 0x3b, 0x43, 0x4c, 0x54, 0x5b, 0x63,
	0x6a, 0x73, 0x7a, 0x82, 0x8a, 0x91, 0x98, 0xa1, 0xa8, 0xb0,
	0xb7, 0xc1, 0xc9, 0xcf, 0xd9, 0xe3, 0xea, 0xf4, 0xff, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	ENDDEF
};

const unsigned short SEQ_UP_DN[] = {
	0x36, 0x10,
	ENDDEF
};

const unsigned short SEQ_SLEEP_IN[] = {
	0x10,
	ENDDEF
};

const unsigned short SEQ_SLEEP_OUT[] = {
	0x11,
	ENDDEF
};

const unsigned short SEQ_DISPLAY_ON[] = {
	0x29,
	ENDDEF
};

const unsigned short SEQ_DISPLAY_OFF[] = {
	0x10,
	ENDDEF
};

static int lms501kf03_spi_write_byte(struct lms501kf03 *lcd, int addr, int data)
{
	u16 buf[1];
	struct spi_message msg;

	struct spi_transfer xfer = {
		.len	= 2,
		.tx_buf	= buf,
	};

	buf[0] = (addr << 8) | data;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(lcd->spi, &msg);
}

static int lms501kf03_spi_write(struct lms501kf03 *lcd, unsigned char address,
	unsigned char command)
{
	int ret = 0;

	ret = lms501kf03_spi_write_byte(lcd, address, command);

	return ret;
}

static int lms501kf03_panel_send_sequence(struct lms501kf03 *lcd,
	const unsigned short *wbuf)
{
	int ret = 0, i = 0;

	while (wbuf[i] != ENDDEF) {
		if (i == 0)
			ret = lms501kf03_spi_write(lcd, COMMAND_ONLY, wbuf[i]);
		else
			ret = lms501kf03_spi_write(lcd, DATA_ONLY, wbuf[i]);
		if (ret)
			break;

		udelay(100);
		i += 1;
	}
	return ret;
}

static int lms501kf03_ldi_init(struct lms501kf03 *lcd)
{
	int ret, i;
	const unsigned short *init_seq[] = {
		SEQ_PASSWORD,
		SEQ_POWER,
		SEQ_DISPLAY,
		SEQ_RGB_IF,
		SEQ_DISPLAY_INV,
		SEQ_VCOM,
		SEQ_GATE,
		SEQ_PANEL,
		SEQ_COL_MOD,
		SEQ_W_GAMMA,
		SEQ_RGB_GAMMA,
		SEQ_SLEEP_OUT,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = lms501kf03_panel_send_sequence(lcd, init_seq[i]);
		if (ret)
			break;
	}
	msleep(120);

	return ret;
}

static int lms501kf03_ldi_enable(struct lms501kf03 *lcd)
{
	int ret, i;
	const unsigned short *init_seq[] = {
		SEQ_DISPLAY_ON,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = lms501kf03_panel_send_sequence(lcd, init_seq[i]);
		if (ret)
			break;
	}

	return ret;
}

static int lms501kf03_ldi_disable(struct lms501kf03 *lcd)
{
	int ret, i;

	const unsigned short *init_seq[] = {
		SEQ_DISPLAY_OFF,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = lms501kf03_panel_send_sequence(lcd, init_seq[i]);
		if (ret)
			break;
	}

	return ret;
}

static int lms501kf03_power_on(struct lms501kf03 *lcd)
{
	int ret = 0;
	struct lcd_platform_data *pd = NULL;
	struct backlight_device *bd = NULL;

	pd = lcd->lcd_pd;
	if (!pd) {
		dev_err(lcd->dev, "platform data is NULL.\n");
		return -EFAULT;
	}

	bd = lcd->bd;
	if (!bd) {
		dev_err(lcd->dev, "backlight device is NULL.\n");
		return -EFAULT;
	}

	if (!pd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EFAULT;
	} else {
		pd->power_on(lcd->ld, 1);
		mdelay(pd->power_on_delay);
	}

	if (!pd->reset) {
		dev_err(lcd->dev, "reset is NULL.\n");
		return -EFAULT;
	} else {
		pd->reset(lcd->ld);
		mdelay(pd->reset_delay);
	}

	ret = lms501kf03_ldi_init(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return ret;
	}

	ret = lms501kf03_ldi_enable(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to enable ldi.\n");
		return ret;
	}

	return 0;
}

static int lms501kf03_power_off(struct lms501kf03 *lcd)
{
	int ret = 0;
	struct lcd_platform_data *pd = NULL;

	pd = lcd->lcd_pd;
	if (!pd) {
		dev_err(lcd->dev, "platform data is NULL\n");
		return -EFAULT;
	}

	ret = lms501kf03_ldi_disable(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd setting failed.\n");
		return -EIO;
	}

	mdelay(pd->power_off_delay);

	if (!pd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EFAULT;
	} else {
		pd->power_on(lcd->ld, 0);
	}
	return 0;
}

static int lms501kf03_power(struct lms501kf03 *lcd, int power)
{
	int ret = 0;

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = lms501kf03_power_on(lcd);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = lms501kf03_power_off(lcd);

	if (!ret)
		lcd->power = power;

	return ret;
}

static int lms501kf03_get_power(struct lcd_device *ld)
{
	struct lms501kf03 *lcd = lcd_get_data(ld);

	return lcd->power;
}

static int lms501kf03_set_power(struct lcd_device *ld, int power)
{
	struct lms501kf03 *lcd = lcd_get_data(ld);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return lms501kf03_power(lcd, power);
}

static int lms501kf03_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int lms501kf03_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	int brightness = bd->props.brightness;

	if (brightness < MIN_BRIGHTNESS ||
		brightness > bd->props.max_brightness) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d.\n",
			MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		return -EINVAL;
	}

	return ret;
}

static struct lcd_ops lms501kf03_lcd_ops = {
	.get_power = lms501kf03_get_power,
	.set_power = lms501kf03_set_power,
};

static const struct backlight_ops lms501kf03_backlight_ops = {
	.get_brightness = lms501kf03_get_brightness,
	.update_status = lms501kf03_set_brightness,
};

static int __devinit lms501kf03_probe(struct spi_device *spi)
{
	int ret = 0;
	struct lms501kf03 *lcd = NULL;
	struct lcd_device *ld = NULL;
	struct backlight_device *bd = NULL;

	lcd = kzalloc(sizeof(struct lms501kf03), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	/* lms501kf03 lcd panel uses 3-wire 9-bit SPI Mode. */
	spi->bits_per_word = 9;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi setup failed.\n");
		goto out_free_lcd;
	}

	lcd->spi = spi;
	lcd->dev = &spi->dev;

	lcd->lcd_pd = (struct lcd_platform_data *)spi->dev.platform_data;
	if (!lcd->lcd_pd) {
		dev_err(&spi->dev, "platform data is NULL\n");
		goto out_free_lcd;
	}

	ld = lcd_device_register("lms501kf03", &spi->dev, lcd,
		&lms501kf03_lcd_ops);
	if (IS_ERR(ld)) {
		ret = PTR_ERR(ld);
		goto out_free_lcd;
	}

	lcd->ld = ld;

	bd = backlight_device_register("lms501kf03-bl", &spi->dev, lcd,
		&lms501kf03_backlight_ops, NULL);
	if (IS_ERR(bd)) {
		ret =  PTR_ERR(bd);
		goto out_lcd_unregister;
	}

	bd->props.max_brightness = MAX_BRIGHTNESS;
	bd->props.brightness = DEFAULT_BRIGHTNESS;
	bd->props.type = BACKLIGHT_RAW;
	lcd->bd = bd;

	if (!lcd->lcd_pd->lcd_enabled) {
		/*
		 * if lcd panel was off from bootloader then
		 * current lcd status is powerdown and then
		 * it enables lcd panel.
		 */
		lcd->power = FB_BLANK_POWERDOWN;

		lms501kf03_power(lcd, FB_BLANK_UNBLANK);
	} else
		lcd->power = FB_BLANK_UNBLANK;

	dev_set_drvdata(&spi->dev, lcd);

	dev_info(&spi->dev, "lms501kf03 panel driver has been probed.\n");

	return 0;

out_lcd_unregister:
	lcd_device_unregister(ld);
out_free_lcd:
	kfree(lcd);
	return ret;
}

static int __devexit lms501kf03_remove(struct spi_device *spi)
{
	struct lms501kf03 *lcd = dev_get_drvdata(&spi->dev);

	lms501kf03_power(lcd, FB_BLANK_POWERDOWN);
	lcd_device_unregister(lcd->ld);
	kfree(lcd);

	return 0;
}

#if defined(CONFIG_PM)
unsigned int before_power;

static int lms501kf03_suspend(struct spi_device *spi, pm_message_t mesg)
{
	int ret = 0;
	struct lms501kf03 *lcd = dev_get_drvdata(&spi->dev);

	dev_dbg(&spi->dev, "lcd->power = %d\n", lcd->power);

	before_power = lcd->power;

	/*
	 * when lcd panel is suspend, lcd panel becomes off
	 * regardless of status.
	 */
	ret = lms501kf03_power(lcd, FB_BLANK_POWERDOWN);

	return ret;
}

static int lms501kf03_resume(struct spi_device *spi)
{
	int ret = 0;
	struct lms501kf03 *lcd = dev_get_drvdata(&spi->dev);

	/*
	 * after suspended, if lcd panel status is FB_BLANK_UNBLANK
	 * (at that time, before_power is FB_BLANK_UNBLANK) then
	 * it changes that status to FB_BLANK_POWERDOWN to get lcd on.
	 */
	if (before_power == FB_BLANK_UNBLANK)
		lcd->power = FB_BLANK_POWERDOWN;

	dev_dbg(&spi->dev, "before_power = %d\n", before_power);

	ret = lms501kf03_power(lcd, before_power);

	return ret;
}
#endif

void lms501kf03_shutdown(struct spi_device *spi)
{
	struct lms501kf03 *lcd = dev_get_drvdata(&spi->dev);

	lms501kf03_power(lcd, FB_BLANK_POWERDOWN);
}

static struct spi_driver lms501kf03_driver = {
	.driver = {
		.name	= "lms501kf03",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= lms501kf03_probe,
	.remove		= __devexit_p(lms501kf03_remove),
	.shutdown	= lms501kf03_shutdown,
	.suspend	= lms501kf03_suspend,
	.resume		= lms501kf03_resume,
};

static int __init lms501kf03_init(void)
{
	return spi_register_driver(&lms501kf03_driver);
}

static void __exit lms501kf03_exit(void)
{
	spi_unregister_driver(&lms501kf03_driver);
}

module_init(lms501kf03_init);
module_exit(lms501kf03_exit);
