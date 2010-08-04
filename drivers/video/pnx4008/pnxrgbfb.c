/*
 * drivers/video/pnx4008/pnxrgbfb.c
 *
 * PNX4008's framebuffer support
 *
 * Author: Grigory Tolstolytkin <gtolstolytkin@ru.mvista.com>
 * Based on Philips Semiconductors's code
 *
 * Copyrght (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2005 Philips Semiconductors
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include "sdum.h"
#include "fbcommon.h"

static u32 colreg[16];

static struct fb_var_screeninfo rgbfb_var __initdata = {
	.xres = LCD_X_RES,
	.yres = LCD_Y_RES,
	.xres_virtual = LCD_X_RES,
	.yres_virtual = LCD_Y_RES,
	.bits_per_pixel = 32,
	.red.offset = 16,
	.red.length = 8,
	.green.offset = 8,
	.green.length = 8,
	.blue.offset = 0,
	.blue.length = 8,
	.left_margin = 0,
	.right_margin = 0,
	.upper_margin = 0,
	.lower_margin = 0,
	.vmode = FB_VMODE_NONINTERLACED,
};
static struct fb_fix_screeninfo rgbfb_fix __initdata = {
	.id = "RGBFB",
	.line_length = LCD_X_RES * LCD_BBP,
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_TRUECOLOR,
	.xpanstep = 0,
	.ypanstep = 0,
	.ywrapstep = 0,
	.accel = FB_ACCEL_NONE,
};

static int channel_owned;

static int no_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	return 0;
}

static int rgbfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	if (regno > 15)
		return 1;

	colreg[regno] = ((red & 0xff00) << 8) | (green & 0xff00) |
	    ((blue & 0xff00) >> 8);
	return 0;
}

static int rgbfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	return pnx4008_sdum_mmap(info, vma, NULL);
}

static struct fb_ops rgbfb_ops = {
	.fb_mmap = rgbfb_mmap,
	.fb_setcolreg = rgbfb_setcolreg,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
};

static int rgbfb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	if (info) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
		platform_set_drvdata(pdev, NULL);
	}

	pnx4008_free_dum_channel(channel_owned, pdev->id);
	pnx4008_set_dum_exit_notification(pdev->id);

	return 0;
}

static int __devinit rgbfb_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	struct dumchannel_uf chan_uf;
	int ret;
	char *option;

	info = framebuffer_alloc(sizeof(u32) * 16, &pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto err;
	}

	pnx4008_get_fb_addresses(FB_TYPE_RGB, (void **)&info->screen_base,
				 (dma_addr_t *) &rgbfb_fix.smem_start,
				 &rgbfb_fix.smem_len);

	if ((ret = pnx4008_alloc_dum_channel(pdev->id)) < 0)
		goto err0;
	else {
		channel_owned = ret;
		chan_uf.channelnr = channel_owned;
		chan_uf.dirty = (u32 *) NULL;
		chan_uf.source = (u32 *) rgbfb_fix.smem_start;
		chan_uf.x_offset = 0;
		chan_uf.y_offset = 0;
		chan_uf.width = LCD_X_RES;
		chan_uf.height = LCD_Y_RES;

		if ((ret = pnx4008_put_dum_channel_uf(chan_uf, pdev->id))< 0)
			goto err1;

		if ((ret =
		     pnx4008_set_dum_channel_sync(channel_owned, CONF_SYNC_ON,
						  pdev->id)) < 0)
			goto err1;

		if ((ret =
		     pnx4008_set_dum_channel_dirty_detect(channel_owned,
							 CONF_DIRTYDETECTION_ON,
							 pdev->id)) < 0)
			goto err1;
	}

	if (!fb_get_options("pnxrgbfb", &option) && option &&
			!strcmp(option, "nocursor"))
		rgbfb_ops.fb_cursor = no_cursor;

	info->node = -1;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->fbops = &rgbfb_ops;
	info->fix = rgbfb_fix;
	info->var = rgbfb_var;
	info->screen_size = rgbfb_fix.smem_len;
	info->pseudo_palette = info->par;
	info->par = NULL;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret < 0)
		goto err1;

	ret = register_framebuffer(info);
	if (ret < 0)
		goto err2;
	platform_set_drvdata(pdev, info);

	return 0;

err2:
	fb_dealloc_cmap(&info->cmap);
err1:
	pnx4008_free_dum_channel(channel_owned, pdev->id);
err0:
	framebuffer_release(info);
err:
	return ret;
}

static struct platform_driver rgbfb_driver = {
	.driver = {
		.name = "pnx4008-rgbfb",
	},
	.probe = rgbfb_probe,
	.remove = rgbfb_remove,
};

static int __init rgbfb_init(void)
{
	return platform_driver_register(&rgbfb_driver);
}

static void __exit rgbfb_exit(void)
{
	platform_driver_unregister(&rgbfb_driver);
}

module_init(rgbfb_init);
module_exit(rgbfb_exit);

MODULE_LICENSE("GPL");
