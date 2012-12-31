/* linux/drivers/video/samsung/s3cfb_lms501kf03.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
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

#include <plat/gpio-cfg.h>

#include "s3cfb.h"

#define ENDDEF			0xFF00
#define COMMAND_ONLY	0x00
#define DATA_ONLY		0x01

#ifdef CONFIG_BACKLIGHT_LMS501KF03_TFT

#define dbg(fmt...)

static int locked;
struct s5p_lcd {
	struct spi_device *g_spi;
	struct lcd_device *lcd_dev;
	struct backlight_device *bl_dev;
};
static struct s5p_lcd lcd;
#else
static struct spi_device *g_spi;
#endif

const unsigned short SEQ_SET_PASSWORD[] = {
	0xb9, 0xff, 0x83, 0x69,
	ENDDEF
};

const unsigned short SEQ_SET_POWER[] = {
	0xb1, 0x01, 0x00, 0x34, 0x06, 0x00, 0x14, 0x14, 0x20, 0x28,
	0x12, 0x12, 0x17, 0x0a, 0x01, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
	ENDDEF
};

const unsigned short SEQ_SET_DISPLAY[] = {
	0xb2, 0x00, 0x2b, 0x03, 0x03, 0x70, 0x00, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x03, 0x03, 0x00, 0x01,
	ENDDEF
};

const unsigned short SEQ_SET_RGB_IF[] = {
	0xb3, 0x09,
	ENDDEF
};

const unsigned short SEQ_SET_DISPLAY_INV[] = {
	0xb4, 0x01, 0x08, 0x77, 0x0e, 0x06,
	ENDDEF
};

const unsigned short SEQ_SET_VCOM[] = {
	0xb6, 0x4c, 0x2e,
	ENDDEF
};

const unsigned short SEQ_SET_GATE[] = {
	0xd5, 0x00, 0x05, 0x03, 0x29, 0x01, 0x07, 0x17, 0x68, 0x13,
	0x37, 0x20, 0x31, 0x8a, 0x46, 0x9b, 0x57, 0x13, 0x02, 0x75,
	0xb9, 0x64, 0xa8, 0x07, 0x0f, 0x04, 0x07,
	ENDDEF
};

const unsigned short SEQ_SET_PANEL[] = {
	0xcc, 0x02,
	ENDDEF
};

const unsigned short SEQ_SET_COL_MOD[] = {
	0x3a, 0x77,
	ENDDEF
};

const unsigned short SEQ_SET_W_GAMMA[] = {
	0xe0, 0x00, 0x04, 0x09, 0x0f, 0x1f, 0x3f, 0x1f, 0x2f, 0x0a,
	0x0f, 0x10, 0x16, 0x18, 0x16, 0x17, 0x0d, 0x15, 0x00, 0x04,
	0x09, 0x0f, 0x38, 0x3f, 0x20, 0x39, 0x0a, 0x0f, 0x10, 0x16,
	0x18, 0x16, 0x17, 0x0d, 0x15,
	ENDDEF
};

const unsigned short SEQ_SET_RGB_GAMMA[] = {
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

const unsigned short SEQ_SET_UP_DN[] = {
	0x36, 0x10,
	ENDDEF
};

const unsigned short SEQ_SET_SLEEP_IN[] = {
	0x10,
	ENDDEF
};

const unsigned short SEQ_SET_SLEEP_OUT[] = {
	0x11,
	ENDDEF
};

const unsigned short SEQ_SET_DISPLAY_ON[] = {
	0x29,
	ENDDEF
};

const unsigned short SEQ_SET_DISPLAY_OFF[] = {
	0x10,
	ENDDEF
};

static struct s3cfb_lcd lms501kf03 = {
	.width = 480,
	.height = 800,
	.bpp = 24,

	.freq = 60,
	.timing = {
		.h_fp = 8,
		.h_bp = 8,
		.h_sw = 6,
		.v_fp = 6,
		.v_fpe = 1,
		.v_bp = 6,
		.v_bpe = 1,
		.v_sw = 4,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static int lms501kf03_spi_write_driver(int addr, int data)
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

#ifdef CONFIG_BACKLIGHT_LMS501KF03_TFT
	int ret;
	locked  = 1;
	ret = spi_sync(lcd.g_spi, &msg);
	locked = 0;
	return ret ;
#else
	return spi_sync(g_spi, &msg);
#endif
}

static void lms501kf03_spi_write(unsigned char address, unsigned char command)
{
	lms501kf03_spi_write_driver(address, command);
}

static void lms501kf03_panel_send_sequence(const unsigned short *wbuf)
{
	int i = 0;

	while (wbuf[i] != ENDDEF) {
		if (i == 0)
			lms501kf03_spi_write(COMMAND_ONLY, wbuf[i]);
		else
			lms501kf03_spi_write(DATA_ONLY, wbuf[i]);

		udelay(100);
		i += 1;
	}
}

void lms501kf03_ldi_init(void)
{
	lms501kf03_panel_send_sequence(SEQ_SET_PASSWORD);
	lms501kf03_panel_send_sequence(SEQ_SET_POWER);
	lms501kf03_panel_send_sequence(SEQ_SET_DISPLAY);
	lms501kf03_panel_send_sequence(SEQ_SET_RGB_IF);
	lms501kf03_panel_send_sequence(SEQ_SET_DISPLAY_INV);
	lms501kf03_panel_send_sequence(SEQ_SET_VCOM);
	lms501kf03_panel_send_sequence(SEQ_SET_GATE);
	lms501kf03_panel_send_sequence(SEQ_SET_PANEL);
	lms501kf03_panel_send_sequence(SEQ_SET_COL_MOD);
	lms501kf03_panel_send_sequence(SEQ_SET_W_GAMMA);
	lms501kf03_panel_send_sequence(SEQ_SET_RGB_GAMMA);
	lms501kf03_panel_send_sequence(SEQ_SET_SLEEP_OUT);
	mdelay(120);
	lms501kf03_panel_send_sequence(SEQ_SET_DISPLAY_ON);
}

void lms501kf03_ldi_enable(void)
{
	lms501kf03_panel_send_sequence(SEQ_SET_DISPLAY_ON);
}

void lms501kf03_ldi_disable(void)
{
	lms501kf03_panel_send_sequence(SEQ_SET_DISPLAY_OFF);
	mdelay(120);
}

void lms501kf03_init_ldi(void)
{
	lms501kf03_ldi_init();
}

void s3cfb_set_lcd_info(struct s3cfb_global *ctrl)
{
	lms501kf03.init_ldi = lms501kf03_ldi_init;
	ctrl->lcd = &lms501kf03;
}

void lms501kf03_gpio_cfg(void)
{
	/* LCD _SPI CS */
	s3c_gpio_cfgpin(EXYNOS4_GPB(5), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPB(5), S3C_GPIO_PULL_NONE);

	/* LCD_SPI SCLK */
	s3c_gpio_cfgpin(EXYNOS4_GPB(4), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPB(4), S3C_GPIO_PULL_NONE);

	/* LCD_SPI MOSI */
	s3c_gpio_cfgpin(EXYNOS4_GPB(7), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPB(7), S3C_GPIO_PULL_NONE);
}

/* backlight operations and functions */
#ifdef CONFIG_BACKLIGHT_LMS501KF03_TFT
static int s5p_bl_update_status(struct backlight_device *bd)
{
	int bl = bd->props.brightness;
	dbg("\nUpdate brightness=%d\n", bd->props.brightness);
	int level = 0;

	if (!locked) {
		if ((bl >= 0) && (bl <= 50))
			level = 1;
		else if ((bl > 50) && (bl <= 100))
			level = 2;
		else if ((bl > 100) && (bl <= 150))
			level = 3;
		else if ((bl > 150) && (bl <= 200))
			level = 4;
		else if ((bl > 200) && (bl <= 255))
			level = 5;

		if (level) {

			switch (level) {
			/* If bl is not halved, variation in brightness is
			* observed as a curve with the middle region being
			* brightest and the sides being darker. It is
			* required that brightness increases gradually
			* from left to right.*/
			case 1:
				lms501kf03_spi_write(0x46, 0x2F);
				lms501kf03_spi_write(0x56, 0x2E);
				lms501kf03_spi_write(0x66, 0x3F);

				break;
			case 2:
				lms501kf03_spi_write(0x46, 0x37);
				lms501kf03_spi_write(0x56, 0x36);
				lms501kf03_spi_write(0x66, 0x4A);

				break;
			case 3:
				lms501kf03_spi_write(0x46, 0x3E);
				lms501kf03_spi_write(0x56, 0x3D);
				lms501kf03_spi_write(0x66, 0x53);

				break;
			case 4:
				lms501kf03_spi_write(0x46, 0x44);
				lms501kf03_spi_write(0x56, 0x43);
				lms501kf03_spi_write(0x66, 0x5C);

				break;
			case 5:
				lms501kf03_spi_write(0x46, 0x47);
				lms501kf03_spi_write(0x56, 0x45);
				lms501kf03_spi_write(0x66, 0x5F);
				break;
			default:
				break;
			}
		} /* level check over */
	} else {
		dbg("\nLOCKED!!!Brightness cannot be changed now!locked=%d",
			locked);
	}
	return 0;
}

static int s5p_bl_get_brightness(struct backlilght_device *bd)
{
	dbg("\n reading brightness\n");
	return 0;
}

static const struct backlight_ops s5p_bl_ops = {
	.update_status = s5p_bl_update_status,
	.get_brightness = s5p_bl_get_brightness,
};
#endif

static int __devinit lms501kf03_probe(struct spi_device *spi)
{
	int ret;
#ifdef CONFIG_BACKLIGHT_LMS501KF03_TFT
	struct backlight_properties props;
#endif
	spi->bits_per_word = 9;
	ret = spi_setup(spi);

#ifdef CONFIG_BACKLIGHT_LMS501KF03_TFT
	lcd.g_spi = spi;

	/* The node is named as pwm-backlight even though PWM
	 * control is not being done since Eclair interface is
	 * looking for "pwm-backlight" for backlight brightness
	 * control*/
	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 255;
	lcd.bl_dev = backlight_device_register("pwm-backlight",
					&spi->dev, &lcd, &s5p_bl_ops, &props);

	dev_set_drvdata(&spi->dev, &lcd);
#else
	g_spi = spi;
#endif

	lms501kf03_gpio_cfg();
	lms501kf03_ldi_init();

	if (ret < 0)
		return 0;

	return ret;
}
#ifdef CONFIG_PM
int lms501kf03_suspend(struct platform_device *pdev, pm_message_t state)
{
	lms501kf03_ldi_disable();
	return 0;
}

int lms501kf03_resume(struct platform_device *pdev, pm_message_t state)
{
	lms501kf03_ldi_init();
	return 0;
}
#endif

static struct spi_driver lms501kf03_driver = {
	.driver = {
		.name	= "lms501kf03",
		.owner	= THIS_MODULE,
	},
	.probe		= lms501kf03_probe,
	.remove		= __exit_p(lms501kf03_remove),
	.suspend	= NULL,
	.resume		= NULL,
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
