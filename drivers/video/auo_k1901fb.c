/*
 * auok190xfb.c -- FB driver for AUO-K1901 controllers
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
 * This driver is written to be used with the AUO-K1901 display controller.
 *
 * It is intended to be architecture independent. A board specific driver
 * must be used to perform all the physical IO interactions.
 *
 * The controller supports different update modes:
 * mode0+1 16 step gray (4bit)
 * mode2+3 4 step gray (2bit)
 * mode4+5 2 step gray (1bit)
 * - mode4 is described as "without LUT"
 * mode7 automatic selection of update mode
 *
 * The most interesting difference to the K1900 is the ability to do screen
 * updates in an asynchronous fashion. Where the K1900 needs to wait for the
 * current update to complete, the K1901 can process later updates already.
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
 * AUO-K1901 specific commands
 */

#define AUOK1901_CMD_LUT_INTERFACE	0x0005
#define AUOK1901_CMD_DMA_START		0x1001
#define AUOK1901_CMD_CURSOR_START	0x1007
#define AUOK1901_CMD_CURSOR_STOP	AUOK190X_CMD_DATA_STOP
#define AUOK1901_CMD_DDMA_START		0x1009

#define AUOK1901_INIT_GATE_PULSE_LOW	(0 << 14)
#define AUOK1901_INIT_GATE_PULSE_HIGH	(1 << 14)
#define AUOK1901_INIT_SINGLE_GATE	(0 << 13)
#define AUOK1901_INIT_DOUBLE_GATE	(1 << 13)

/* Bits to pixels
 *   Mode	15-12	11-8	7-4	3-0
 *   format2	2	T	1	T
 *   format3	1	T	2	T
 *   format4	T	2	T	1
 *   format5	T	1	T	2
 *
 *   halftone modes:
 *   format6	2	2	1	1
 *   format7	1	1	2	2
 */
#define AUOK1901_INIT_FORMAT2		(1 << 7)
#define AUOK1901_INIT_FORMAT3		((1 << 7) | (1 << 6))
#define AUOK1901_INIT_FORMAT4		(1 << 8)
#define AUOK1901_INIT_FORMAT5		((1 << 8) | (1 << 6))
#define AUOK1901_INIT_FORMAT6		((1 << 8) | (1 << 7))
#define AUOK1901_INIT_FORMAT7		((1 << 8) | (1 << 7) | (1 << 6))

/* res[4] to bit 10
 * res[3-0] to bits 5-2
 */
#define AUOK1901_INIT_RESOLUTION(_res)	(((_res & (1 << 4)) << 6) \
					 | ((_res & 0xf) << 2))

/*
 * portrait / landscape orientation in AUOK1901_CMD_DMA_START
 */
#define AUOK1901_DMA_ROTATE90(_rot)		((_rot & 1) << 13)

/*
 * equivalent to 1 << 11, needs the ~ to have same rotation like K1900
 */
#define AUOK1901_DDMA_ROTATE180(_rot)		((~_rot & 2) << 10)

static void auok1901_init(struct auok190xfb_par *par)
{
	struct auok190x_board *board = par->board;
	u16 init_param = 0;

	init_param |= AUOK190X_INIT_INVERSE_WHITE;
	init_param |= AUOK190X_INIT_FORMAT0;
	init_param |= AUOK1901_INIT_RESOLUTION(par->resolution);
	init_param |= AUOK190X_INIT_SHIFT_LEFT;

	auok190x_send_cmdargs(par, AUOK190X_CMD_INIT, 1, &init_param);

	/* let the controller finish */
	board->wait_for_rdy(par);
}

static void auok1901_update_region(struct auok190xfb_par *par, int mode,
						u16 y1, u16 y2)
{
	struct device *dev = par->info->device;
	unsigned char *buf = (unsigned char *)par->info->screen_base;
	int xres = par->info->var.xres;
	u16 args[5];

	pm_runtime_get_sync(dev);

	mutex_lock(&(par->io_lock));

	/* y1 and y2 must be a multiple of 2 so drop the lowest bit */
	y1 &= 0xfffe;
	y2 &= 0xfffe;

	dev_dbg(dev, "update (x,y,w,h,mode)=(%d,%d,%d,%d,%d)\n",
		1, y1+1, xres, y2-y1, mode);

	/* K1901: first transfer the region data */
	args[0] = AUOK1901_DMA_ROTATE90(par->rotation) | 1;
	args[1] = y1 + 1;
	args[2] = xres;
	args[3] = y2 - y1;
	buf += y1 * xres;
	auok190x_send_cmdargs_pixels_nowait(par, AUOK1901_CMD_DMA_START, 4,
					    args, ((y2 - y1) * xres)/2,
					    (u16 *) buf);
	auok190x_send_command_nowait(par, AUOK190X_CMD_DATA_STOP);

	/* K1901: second tell the controller to update the region with mode */
	args[0] = mode | AUOK1901_DDMA_ROTATE180(par->rotation);
	args[1] = 1;
	args[2] = y1 + 1;
	args[3] = xres;
	args[4] = y2 - y1;
	auok190x_send_cmdargs_nowait(par, AUOK1901_CMD_DDMA_START, 5, args);

	par->update_cnt++;

	mutex_unlock(&(par->io_lock));

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

static void auok1901fb_dpy_update_pages(struct auok190xfb_par *par,
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

	auok1901_update_region(par, mode, y1, y2);
}

static void auok1901fb_dpy_update(struct auok190xfb_par *par)
{
	int mode;

	/* When doing full updates, wait for the controller to be ready
	 * This will hopefully catch some hangs of the K1901
	 */
	par->board->wait_for_rdy(par);

	if (par->update_mode < 0) {
		mode = AUOK190X_UPDATE_MODE(0);
		par->last_mode = -1;
	} else {
		mode = AUOK190X_UPDATE_MODE(par->update_mode);
		par->last_mode = par->update_mode;
	}

	if (par->flash)
		mode |= AUOK190X_UPDATE_NONFLASH;

	auok1901_update_region(par, mode, 0, par->info->var.yres);
	par->update_cnt = 0;
}

static bool auok1901fb_need_refresh(struct auok190xfb_par *par)
{
	return (par->update_cnt > 10);
}

static int auok1901fb_probe(struct platform_device *pdev)
{
	struct auok190x_init_data init;
	struct auok190x_board *board;

	/* pick up board specific routines */
	board = pdev->dev.platform_data;
	if (!board)
		return -EINVAL;

	/* fill temporary init struct for common init */
	init.id = "auo_k1901fb";
	init.board = board;
	init.update_partial = auok1901fb_dpy_update_pages;
	init.update_all = auok1901fb_dpy_update;
	init.need_refresh = auok1901fb_need_refresh;
	init.init = auok1901_init;

	return auok190x_common_probe(pdev, &init);
}

static int auok1901fb_remove(struct platform_device *pdev)
{
	return auok190x_common_remove(pdev);
}

static struct platform_driver auok1901fb_driver = {
	.probe	= auok1901fb_probe,
	.remove = auok1901fb_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "auo_k1901fb",
		.pm = &auok190x_pm,
	},
};
module_platform_driver(auok1901fb_driver);

MODULE_DESCRIPTION("framebuffer driver for the AUO-K1901 EPD controller");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_LICENSE("GPL");
