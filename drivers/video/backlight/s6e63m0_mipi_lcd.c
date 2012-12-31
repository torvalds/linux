/* linux/drivers/video/backlight/s6e63m0_mipi_lcd.c
 *
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/backlight.h>
#include <linux/lcd.h>

#include <video/mipi_display.h>

#include <plat/gpio-cfg.h>
#include <plat/regs-dsim.h>

#include <plat/dsim.h>
#include <plat/mipi_dsi.h>

void init_lcd(struct mipi_dsim_device *dsim)
{
	unsigned char buf1[4] = {0xf2, 0x02, 0x03, 0x1b};
	unsigned char buf2[15] = {0xf8, 0x01, 0x27, 0x27, 0x07, 0x07, 0x54,
							0x9f, 0x63, 0x86, 0x1a,
							0x33, 0x0d, 0x00, 0x00};
	unsigned char buf3[4] = {0xf6, 0x00, 0x8c, 0x07};

	/* password */
	unsigned char rf[3] = {0x00, 0x5a, 0x5a};

	rf[0] = 0xf0;
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
		(unsigned int) rf, 3);

	rf[0] = 0xf1;
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
		(unsigned int) rf, 3);

	rf[0] = 0xfc;
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
		(unsigned int) rf, 3);

	/* HZ (0x16 = 60Hz) */
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM,
		0xfd, 0x14);

	s5p_mipi_dsi_wr_data(dsim,
		MIPI_DSI_DCS_SHORT_WRITE_PARAM, 0xb0, 0x09);
	s5p_mipi_dsi_wr_data(dsim,
		MIPI_DSI_DCS_SHORT_WRITE_PARAM, 0xd5, 0x64);
	s5p_mipi_dsi_wr_data(dsim,
		MIPI_DSI_DCS_SHORT_WRITE_PARAM, 0xb0, 0x09);

	/* nonBurstSyncPulse */
	if (dsim->pd->dsim_config->e_burst_mode == DSIM_NON_BURST_SYNC_PULSE)
		s5p_mipi_dsi_wr_data(dsim,
			MIPI_DSI_DCS_SHORT_WRITE_PARAM, 0xd5, 0x84);
	else
		s5p_mipi_dsi_wr_data(dsim,
			MIPI_DSI_DCS_SHORT_WRITE_PARAM, 0xd5, 0xc4);

	s5p_mipi_dsi_wr_data(dsim,
		MIPI_DSI_DCS_LONG_WRITE, (unsigned int) buf1, sizeof(buf1));
	s5p_mipi_dsi_wr_data(dsim,
		MIPI_DSI_DCS_LONG_WRITE, (unsigned int) buf2, sizeof(buf2));
	s5p_mipi_dsi_wr_data(dsim,
		MIPI_DSI_DCS_LONG_WRITE, (unsigned int) buf3, sizeof(buf3));

	s5p_mipi_dsi_wr_data(dsim,
		MIPI_DSI_DCS_SHORT_WRITE_PARAM, 0xfa, 0x01);
	/* Exit sleep */
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0x11, 0);
	mdelay(600);
	/* Set Display ON */
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0x29, 0);
}

void s6e63m0_mipi_lcd_off(struct mipi_dsim_device *dsim)
{
	/* softreset */
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0x1, 0);
	/* Enter sleep */
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0x10, 0);
	mdelay(60);

	/* Set Display off */
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0x28, 0);
}

static int s6e63m0_mipi_lcd_bl_update_status(struct backlight_device *bd)
{
	return 0;
}

static const struct backlight_ops s6e63m0_mipi_lcd_bl_ops = {
	.update_status = s6e63m0_mipi_lcd_bl_update_status,
};

static int s6e63m0_mipi_lcd_probe(struct mipi_dsim_device *dsim)
{
	struct mipi_dsim_device *dsim_drv;
	struct backlight_device *bd = NULL;
	struct backlight_properties props;

	dsim_drv = kzalloc(sizeof(struct mipi_dsim_device), GFP_KERNEL);
	if (!dsim_drv)
		return -ENOMEM;

	dsim_drv = (struct mipi_dsim_device *) dsim;

	props.max_brightness = 1;
	props.type = BACKLIGHT_PLATFORM;

	bd = backlight_device_register("pwm-backlight",
		dsim_drv->dev, dsim_drv, &s6e63m0_mipi_lcd_bl_ops, &props);

	return 0;
}

static int s6e63m0_mipi_lcd_suspend(struct mipi_dsim_device *dsim)
{
	s6e63m0_mipi_lcd_off(dsim);
	return 0;
}

static int s6e63m0_mipi_lcd_displayon(struct mipi_dsim_device *dsim)
{
	init_lcd(dsim);

	return 0;
}

static int s6e63m0_mipi_lcd_resume(struct mipi_dsim_device *dsim)
{
	init_lcd(dsim);
	return 0;
}

struct mipi_dsim_lcd_driver s6e63m0_mipi_lcd_driver = {
	.probe = s6e63m0_mipi_lcd_probe,
	.suspend =  s6e63m0_mipi_lcd_suspend,
	.displayon = s6e63m0_mipi_lcd_displayon,
	.resume = s6e63m0_mipi_lcd_resume,
};

