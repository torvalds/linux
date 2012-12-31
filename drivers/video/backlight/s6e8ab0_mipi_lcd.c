/* linux/drivers/video/backlight/s6e8ab0_mipi_lcd.c
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
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0, 0);
	mdelay(60);
	/* Exit sleep */
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE,
		0x11, 0);
	mdelay(600);
	s5p_mipi_dsi_wr_data(dsim, MIPI_DSI_TURN_ON_PERIPHERAL,
		0, 0);
}

void s6e8ab0_mipi_lcd_off(struct mipi_dsim_device *dsim)
{
	mdelay(1);
}

static int s6e8ab0_mipi_lcd_suspend(struct mipi_dsim_device *dsim)
{
	s6e8ab0_mipi_lcd_off(dsim);
	return 0;
}

static int s6e8ab0_mipi_lcd_displayon(struct mipi_dsim_device *dsim)
{
	init_lcd(dsim);

	return 0;
}

static int s6e8ab0_mipi_lcd_resume(struct mipi_dsim_device *dsim)
{
	init_lcd(dsim);
	return 0;
}

struct mipi_dsim_lcd_driver s6e8ab0_mipi_lcd_driver = {
	.suspend =  s6e8ab0_mipi_lcd_suspend,
	.displayon = s6e8ab0_mipi_lcd_displayon,
	.resume = s6e8ab0_mipi_lcd_resume,
};
