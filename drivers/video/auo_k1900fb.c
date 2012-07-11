/*
 * auok190xfb.c -- FB driver for AUO-K1900 controllers
 *
 * Copyright (C) 2011, 2012 Heiko Stuebner <heiko@sntech.de>
 *
 * based on broadsheetfb.c
 *
 * Copyright (C) 2008, Jaya Kumar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Layout is based on skeletonfb.c by James Simmons and Geert Uytterhoeven.
 *
 * This driver is written to be used with the AUO-K1900 display controller.
 *
 * It is intended to be architecture independent. A board specific driver
 * must be used to perform all the physical IO interactions.
 *
 * The controller supports different update modes:
 * mode0+1 16 step gray (4bit)
 * mode2 4 step gray (2bit) - FIXME: add strange refresh
 * mode3 2 step gray (1bit) - FIXME: add strange refresh
 * mode4 handwriting mode (strange behaviour)
 * mode5 automatic selection of update mode
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/pm_runtime.h>

#include <video/auo_k190xfb.h>

#include "auo_k190x.h"

/*
 * AUO-K1900 specific commands
 */

#define AUOK1900_CMD_PARTIALDISP	0x1001
#define AUOK1900_CMD_ROTATION		0x1006
#define AUOK1900_CMD_LUT_STOP		0x1009

#define AUOK1900_INIT_TEMP_AVERAGE	(1 << 13)
#define AUOK1900_INIT_ROTATE(_x)	((_x & 0x3) << 10)
#define AUOK1900_INIT_RESOLUTION(_res)	((_res & 0x7) << 2)

static void auok1900_init(struct auok190xfb_par *par)
{
	struct auok190x_board *board = par->board;
	u16 init_param = 0;

	init_param |= AUOK1900_INIT_TEMP_AVERAGE;
	init_param |= AUOK1900_INIT_ROTATE(par->rotation);
	init_param |= AUOK190X_INIT_INVERSE_WHITE;
	init_param |= AUOK190X_INIT_FORMAT0;
	init_param |= AUOK1900_INIT_RESOLUTION(par->resolution);
	init_param |= AUOK190X_INIT_SHIFT_RIGHT;

	auok190x_send_cmdargs(par, AUOK190X_CMD_INIT, 1, &init_param);

	/* let the controller finish */
	board->wait_for_rdy(par);
}

static void auok1900_update_region(struct auok190xfb_par *par, int mode,
						u16 y1, u16 y2)
{
	struct device *dev = par->info->device;
	unsigned char *buf = (unsigned char *)par->info->screen_base;
	int xres = par->info->var.xres;
	u16 args[4];

	pm_runtime_get_sync(dev);

	mutex_lock(&(par->io_lock));

	/* y1 and y2 must be a multiple of 2 so drop the lowest bit */
	y1 &= 0xfffe;
	y2 &= 0xfffe;

	dev_dbg(dev, "update (x,y,w,h,mode)=(%d,%d,%d,%d,%d)\n",
		1, y1+1, xres, y2-y1, mode);

	/* to FIX handle different partial update modes */
	args[0] = mode | 1;
	args[1] = y1 + 1;
	args[2] = xres;
	args[3] = y2 - y1;
	buf += y1 * xres;
	auok190x_send_cmdargs_pixels(par, AUOK1900_CMD_PARTIALDISP, 4, args,
				     ((y2 - y1) * xres)/2, (u16 *) buf);
	auok190x_send_command(par, AUOK190X_CMD_DATA_STOP);

	par->update_cnt++;

	mutex_unlock(&(par->io_lock));

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

static void auok1900fb_dpy_update_pages(struct auok190xfb_par *par,
						u16 y1, u16 y2)
{
	int mode;

	if (par->update_mode < 0) {
		mode = AUOK190X_UPDATE_MODE(1);
		par->last_mode = -1;
	} else {
		mode = AUOK190X_UPDATE_MODE(par->update_mode);
		par->last_mode = par->update_mode;
	}

	if (par->flash)
		mode |= AUOK190X_UPDATE_NONFLASH;

	auok1900_update_region(par, mode, y1, y2);
}

static void auok1900fb_dpy_update(struct auok190xfb_par *par)
{
	int mode;

	if (par->update_mode < 0) {
		mode = AUOK190X_UPDATE_MODE(0);
		par->last_mode = -1;
	} else {
		mode = AUOK190X_UPDATE_MODE(par->update_mode);
		par->last_mode = par->update_mode;
	}

	if (par->flash)
		mode |= AUOK190X_UPDATE_NONFLASH;

	auok1900_update_region(par, mode, 0, par->info->var.yres);
	par->update_cnt = 0;
}

static bool auok1900fb_need_refresh(struct auok190xfb_par *par)
{
	return (par->update_cnt > 10);
}

static int __devinit auok1900fb_probe(struct platform_device *pdev)
{
	struct auok190x_init_data init;
	struct auok190x_board *board;

	/* pick up board specific routines */
	board = pdev->dev.platform_data;
	if (!board)
		return -EINVAL;

	/* fill temporary init struct for common init */
	init.id = "auo_k1900fb";
	init.board = board;
	init.update_partial = auok1900fb_dpy_update_pages;
	init.update_all = auok1900fb_dpy_update;
	init.need_refresh = auok1900fb_need_refresh;
	init.init = auok1900_init;

	return auok190x_common_probe(pdev, &init);
}

static int __devexit auok1900fb_remove(struct platform_device *pdev)
{
	return auok190x_common_remove(pdev);
}

static struct platform_driver auok1900fb_driver = {
	.probe	= auok1900fb_probe,
	.remove = __devexit_p(auok1900fb_remove),
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "auo_k1900fb",
		.pm = &auok190x_pm,
	},
};
module_platform_driver(auok1900fb_driver);

MODULE_DESCRIPTION("framebuffer driver for the AUO-K1900 EPD controller");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_LICENSE("GPL");
