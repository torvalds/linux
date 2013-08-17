/* linux/drivers/video/backlight/s6e8aa0_mipi_lcd.c
 *
 * Samsung SoC MIPI LCD driver.
 *
 * Copyright (c) 2012 Samsung Electronics
 *
 * Haowei Li, <haowei.li@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/delay.h>
#include <linux/gpio.h>
#include <video/mipi_display.h>

#include <plat/dsim.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <plat/mipi_dsi.h>

#include "s6e8aa0_gamma.h"

#define GAMMA_PARAM_SIZE 26
#define MAX_BRIGHTNESS 255
#define MIN_BRIGHTNESS 0
#define DEFAULT_BRIGHTNESS 0

static struct mipi_dsim_device *dsim_base;
static struct backlight_device *bd;
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend    s6e8aa0_early_suspend;
#endif

static const unsigned char apply_level_2_key[3] = {
	/* command */
	0xfc,
	/* parameter */
	0x5a, 0x5a
};

static const unsigned char sleep_out[1] = {
	0x11,
};

static const unsigned char panel_condition_set[39] = {
	0xF8,
	0x25, 0x34, 0x00, 0x00, 0x00, 0x95, 0x00, 0x3c, 0x7d, 0x08,
	0x27, 0x00, 0x00, 0x10, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x02, 0x08, 0x08, 0x23, 0x63, 0xc0, 0xc1,
	0x01, 0x81, 0xc1, 0x00, 0xc8, 0xc1, 0xd3, 0x01
};

static const unsigned char display_condition_set[4] = {
	0xF2,
	0x80, 0x03, 0x0D
};

static const unsigned char gamma_update[4] = {
	0xF7,
	0x03, 0x00, 0x00
};

static const unsigned char etc_set_source_ctrl[4] = {
	0xF6,
	0x00, 0x02, 0x00
};

static const unsigned char etc_set_pentile_ctrl[10] = {
	0xB6,
	0x0C, 0x02, 0x03, 0x32, 0xC0, 0x44, 0x44, 0xC0, 0x00
};

static const unsigned char etc_set_power_ctrl[8] = {
	0xF4,
	0xCF, 0x0A, 0x15, 0x10, 0x19, 0x33, 0x02
};

static const unsigned char elvss_NVM_set[15] = {
	0xD9,
	0x14, 0x40, 0x0C, 0xCB, 0xCE, 0x6E, 0xC4, 0x07, 0x40, 0x41,
	0xC1, 0x00, 0x60, 0x19
};

static const unsigned char elvss_ctrl_set[3] = {
	0xB1, 0x04, 0x00
};


static int s6e8aa0_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int get_backlight_level(int brightness)
{
	int backlightlevel;

	switch (brightness) {
	case 0:
		backlightlevel = 0;
		break;
	case 1 ... 29:
		backlightlevel = 0;
		break;
	case 30 ... 34:
		backlightlevel = 1;
		break;
	case 35 ... 39:
		backlightlevel = 2;
		break;
	case 40 ... 44:
		backlightlevel = 3;
		break;
	case 45 ... 49:
		backlightlevel = 4;
		break;
	case 50 ... 54:
		backlightlevel = 5;
		break;
	case 55 ... 64:
		backlightlevel = 6;
		break;
	case 65 ... 74:
		backlightlevel = 7;
		break;
	case 75 ... 83:
		backlightlevel = 8;
		break;
	case 84 ... 93:
		backlightlevel = 9;
		break;
	case 94 ... 103:
		backlightlevel = 10;
		break;
	case 104 ... 113:
		backlightlevel = 11;
		break;
	case 114 ... 122:
		backlightlevel = 12;
		break;
	case 123 ... 132:
		backlightlevel = 13;
		break;
	case 133 ... 142:
		backlightlevel = 14;
		break;
	case 143 ... 152:
		backlightlevel = 15;
		break;
	case 153 ... 162:
		backlightlevel = 16;
		break;
	case 163 ... 171:
		backlightlevel = 17;
		break;
	case 172 ... 181:
		backlightlevel = 18;
		break;
	case 182 ... 191:
		backlightlevel = 19;
		break;
	case 192 ... 201:
		backlightlevel = 20;
		break;
	case 202 ... 210:
		backlightlevel = 21;
		break;
	case 211 ... 220:
		backlightlevel = 22;
		break;
	case 221 ... 230:
		backlightlevel = 23;
		break;
	case 231 ... 240:
		backlightlevel = 24;
		break;
	case 241 ... 250:
		backlightlevel = 25;
		break;
	case 251 ... 255:
		backlightlevel = 26;
		break;
	default:
		backlightlevel = 12;
		break;
	}

	return backlightlevel;
}

static int update_brightness(int brightness)
{
	int backlightlevel;

	backlightlevel = get_backlight_level(brightness);

	while (s5p_mipi_dsi_wr_data(dsim_base, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)gamma22_table[backlightlevel],
				GAMMA_PARAM_SIZE) == -1)
		printk(KERN_ERR "fail to write gamma value.\n");

	while (s5p_mipi_dsi_wr_data(dsim_base, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)gamma_update,
				ARRAY_SIZE(gamma_update)) == -1)
		printk(KERN_ERR "fail to update gamma value.\n");
	return 1;
}

static int s6e8aa0_set_brightness(struct backlight_device *bd)
{
	int brightness = bd->props.brightness;

	if (brightness < MIN_BRIGHTNESS || brightness > MAX_BRIGHTNESS) {
		printk(KERN_ALERT "Brightness should be in the range of 0 ~ 255\n");
		return -EINVAL;
	}

	update_brightness(brightness);

	return 1;
}

static const struct backlight_ops s6e8aa0_backlight_ops = {
	.get_brightness = s6e8aa0_get_brightness,
	.update_status = s6e8aa0_set_brightness,
};

static int s6e8aa0_probe(struct mipi_dsim_device *dsim)
{
	dsim_base = dsim;

	bd = backlight_device_register("pwm-backlight.0", NULL,
		NULL, &s6e8aa0_backlight_ops, NULL);
	if (IS_ERR(bd))
		printk(KERN_ALERT "failed to register backlight device!\n");

	bd->props.max_brightness = MAX_BRIGHTNESS;
	bd->props.brightness = DEFAULT_BRIGHTNESS;

	return 1;
}

static void init_lcd(struct mipi_dsim_device *dsim)
{

	while (s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
		(unsigned int)apply_level_2_key,
			ARRAY_SIZE(apply_level_2_key)) == -1)
		dev_err(dsim->dev, "fail to send apply_level_2_key command.\n");

	usleep_range(16000, 16000);

	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0x11, 0);

	usleep_range(16000, 16000);

	while (s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)panel_condition_set,
				ARRAY_SIZE(panel_condition_set)) == -1)
		dev_err(dsim->dev, "fail to send panel_condition_set command.\n");

	while (s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)display_condition_set,
				ARRAY_SIZE(display_condition_set)) == -1)
		dev_err(dsim->dev, "fail to send display_condition_set command.\n");

	update_brightness(bd->props.brightness);

	while (s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)etc_set_source_ctrl,
				ARRAY_SIZE(etc_set_source_ctrl)) == -1)
		dev_err(dsim->dev, "fail to send set_source_ctrl command.\n");

	while (s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)etc_set_pentile_ctrl,
				ARRAY_SIZE(etc_set_pentile_ctrl)) == -1)
		dev_err(dsim->dev, "fail to send set_pentile_ctrl command.\n");

	while (s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)etc_set_power_ctrl,
				ARRAY_SIZE(etc_set_power_ctrl)) == -1)
		dev_err(dsim->dev, "fail to send set_power_ctrl command.\n");

	while (s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)elvss_NVM_set,
				ARRAY_SIZE(elvss_NVM_set)) == -1)
		dev_err(dsim->dev, "fail to send elvss_NVM_set command.\n");

	while (s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
			(unsigned int)elvss_ctrl_set,
				ARRAY_SIZE(elvss_ctrl_set)) == -1)
		dev_err(dsim->dev, "fail to send elvss_ctrl_set command.\n");

	msleep(120);

	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0x29, 0);
}

static int s6e8aa0_displayon(struct mipi_dsim_device *dsim)
{
	init_lcd(dsim);
	return 1;
}

static int s6e8aa0_suspend(struct mipi_dsim_device *dsim)
{
	return 1;
}

static int s6e8aa0_resume(struct mipi_dsim_device *dsim)
{
	return 1;
}

struct mipi_dsim_lcd_driver s6e8aa0_mipi_lcd_driver = {
	.probe		= s6e8aa0_probe,
	.displayon	= s6e8aa0_displayon,
	.suspend	= s6e8aa0_suspend,
	.resume		= s6e8aa0_resume,
};
