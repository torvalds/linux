/*
 * drivers/video/geode/gx1fb_core.c
 *   -- Geode GX1 framebuffer driver
 *
 * Copyright (C) 2005 Arcom Control Systems Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>

#include "geodefb.h"
#include "display_gx1.h"
#include "video_cs5530.h"

static char mode_option[32] = "640x480-16@60";
static int  crt_option = 1;
static char panel_option[32] = "";

static int gx1_line_delta(int xres, int bpp)
{
	int line_delta = xres * (bpp >> 3);

	if (line_delta > 2048)
		line_delta = 4096;
	else if (line_delta > 1024)
		line_delta = 2048;
	else
		line_delta = 1024;
	return line_delta;
}

static int gx1fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct geodefb_par *par = info->par;

	printk(KERN_DEBUG "%s()\n", __FUNCTION__);

	/* Maximum resolution is 1280x1024. */
	if (var->xres > 1280 || var->yres > 1024)
		return -EINVAL;

	if (par->panel_x && (var->xres > par->panel_x || var->yres > par->panel_y))
		return -EINVAL;

	/* Only 16 bpp and 8 bpp is supported by the hardware. */
	if (var->bits_per_pixel == 16) {
		var->red.offset   = 11; var->red.length   = 5;
		var->green.offset =  5; var->green.length = 6;
		var->blue.offset  =  0; var->blue.length  = 5;
		var->transp.offset = 0; var->transp.length = 0;
	} else if (var->bits_per_pixel == 8) {
		var->red.offset   = 0; var->red.length   = 8;
		var->green.offset = 0; var->green.length = 8;
		var->blue.offset  = 0; var->blue.length  = 8;
		var->transp.offset = 0; var->transp.length = 0;
	} else
		return -EINVAL;

	/* Enough video memory? */
	if (gx1_line_delta(var->xres, var->bits_per_pixel) * var->yres > info->fix.smem_len)
		return -EINVAL;

	/* FIXME: Check timing parameters here? */

	return 0;
}

static int gx1fb_set_par(struct fb_info *info)
{
	struct geodefb_par *par = info->par;

	if (info->var.bits_per_pixel == 16) {
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		fb_dealloc_cmap(&info->cmap);
	} else {
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		fb_alloc_cmap(&info->cmap, 1<<info->var.bits_per_pixel, 0);
	}

	info->fix.line_length = gx1_line_delta(info->var.xres, info->var.bits_per_pixel);

	par->dc_ops->set_mode(info);

	return 0;
}

static inline u_int chan_to_field(u_int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int gx1fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct geodefb_par *par = info->par;

	if (info->var.grayscale) {
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

	/* Truecolor has hardware independent palette */
	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 *pal = info->pseudo_palette;
		u32 v;

		if (regno >= 16)
			return -EINVAL;

		v  = chan_to_field(red, &info->var.red);
		v |= chan_to_field(green, &info->var.green);
		v |= chan_to_field(blue, &info->var.blue);

		pal[regno] = v;
	} else {
		if (regno >= 256)
			return -EINVAL;

		par->dc_ops->set_palette_reg(info, regno, red, green, blue);
	}

	return 0;
}

static int gx1fb_blank(int blank_mode, struct fb_info *info)
{
	struct geodefb_par *par = info->par;

	return par->vid_ops->blank_display(info, blank_mode);
}

static int __init gx1fb_map_video_memory(struct fb_info *info)
{
	struct geodefb_par *par = info->par;
	unsigned gx_base;
	int fb_len;

	gx_base = gx1_gx_base();
	if (!gx_base)
		return -ENODEV;

	par->vid_dev = pci_get_device(PCI_VENDOR_ID_CYRIX,
				      PCI_DEVICE_ID_CYRIX_5530_VIDEO, NULL);
	if (!par->vid_dev)
		return -ENODEV;

	par->vid_regs = ioremap(pci_resource_start(par->vid_dev, 1),
				pci_resource_len(par->vid_dev, 1));
	if (!par->vid_regs)
		return -ENOMEM;

	par->dc_regs = ioremap(gx_base + 0x8300, 0x100);
	if (!par->dc_regs)
		return -ENOMEM;

	info->fix.smem_start = gx_base + 0x800000;
	if ((fb_len = gx1_frame_buffer_size()) < 0)
		return -ENOMEM;
	info->fix.smem_len = fb_len;
	info->screen_base = ioremap(info->fix.smem_start, info->fix.smem_len);
	if (!info->screen_base)
		return -ENOMEM;

	printk(KERN_INFO "%s: %d Kibyte of video memory at 0x%lx\n",
	       info->fix.id, info->fix.smem_len / 1024, info->fix.smem_start);

	return 0;
}

static int parse_panel_option(struct fb_info *info)
{
	struct geodefb_par *par = info->par;

	if (strcmp(panel_option, "") != 0) {
		int x, y;
		char *s;
		x = simple_strtol(panel_option, &s, 10);
		if (!x)
			return -EINVAL;
		y = simple_strtol(s + 1, NULL, 10);
		if (!y)
			return -EINVAL;
		par->panel_x = x;
		par->panel_y = y;
	}
	return 0;
}

static struct fb_ops gx1fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= gx1fb_check_var,
	.fb_set_par	= gx1fb_set_par,
	.fb_setcolreg	= gx1fb_setcolreg,
	.fb_blank       = gx1fb_blank,
	/* No HW acceleration for now. */
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
};

static struct fb_info * __init gx1fb_init_fbinfo(void)
{
	struct fb_info *info;
	struct geodefb_par *par;

	/* Alloc enough space for the pseudo palette. */
	info = framebuffer_alloc(sizeof(struct geodefb_par) + sizeof(u32) * 16, NULL);
	if (!info)
		return NULL;

	par = info->par;

	strcpy(info->fix.id, "GX1");

	info->fix.type		= FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux	= 0;
	info->fix.xpanstep	= 0;
	info->fix.ypanstep	= 0;
	info->fix.ywrapstep	= 0;
	info->fix.accel		= FB_ACCEL_NONE;

	info->var.nonstd	= 0;
	info->var.activate	= FB_ACTIVATE_NOW;
	info->var.height	= -1;
	info->var.width	= -1;
	info->var.accel_flags = 0;
	info->var.vmode	= FB_VMODE_NONINTERLACED;

	info->fbops		= &gx1fb_ops;
	info->flags		= FBINFO_DEFAULT;
	info->node		= -1;

	info->pseudo_palette	= (void *)par + sizeof(struct geodefb_par);

	info->var.grayscale	= 0;

	/* CRT and panel options */
	par->enable_crt = crt_option;
	if (parse_panel_option(info) < 0)
		printk(KERN_WARNING "%s: invalid 'panel' option -- disabling flat panel\n",
		       info->fix.id);
	if (!par->panel_x)
		par->enable_crt = 1; /* fall back to CRT if no panel is specified */

	return info;
}


static struct fb_info *gx1fb_info;

static int __init gx1fb_init(void)
{
	struct fb_info *info;
        struct geodefb_par *par;
	int ret;

#ifndef MODULE
	if (fb_get_options("gx1fb", NULL))
		return -ENODEV;
#endif

	info = gx1fb_init_fbinfo();
	if (!info)
		return -ENOMEM;
	gx1fb_info = info;

	par = info->par;

	/* GX1 display controller and CS5530 video device */
	par->dc_ops  = &gx1_dc_ops;
	par->vid_ops = &cs5530_vid_ops;

	if ((ret = gx1fb_map_video_memory(info)) < 0) {
		printk(KERN_ERR "%s: gx1fb_map_video_memory() failed\n", info->fix.id);
		goto err;
	}

	ret = fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 16);
	if (ret == 0 || ret == 4) {
		printk(KERN_ERR "%s: could not find valid video mode\n", info->fix.id);
		ret = -EINVAL;
		goto err;
	}

        /* Clear the frame buffer of garbage. */
        memset_io(info->screen_base, 0, info->fix.smem_len);

	gx1fb_check_var(&info->var, info);
	gx1fb_set_par(info);

	if (register_framebuffer(info) < 0) {
		ret = -EINVAL;
		goto err;
	}
	printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node, info->fix.id);
	return 0;

  err:
	if (info->screen_base)
		iounmap(info->screen_base);
	if (par->vid_regs)
		iounmap(par->vid_regs);
	if (par->dc_regs)
		iounmap(par->dc_regs);
	if (par->vid_dev)
		pci_dev_put(par->vid_dev);
	if (info)
		framebuffer_release(info);
	return ret;
}

static void __exit gx1fb_cleanup(void)
{
	struct fb_info *info = gx1fb_info;
	struct geodefb_par *par = gx1fb_info->par;

	unregister_framebuffer(info);

	iounmap((void __iomem *)info->screen_base);
	iounmap(par->vid_regs);
	iounmap(par->dc_regs);

	pci_dev_put(par->vid_dev);

	framebuffer_release(info);
}

module_init(gx1fb_init);
module_exit(gx1fb_cleanup);

module_param_string(mode, mode_option, sizeof(mode_option), 0444);
MODULE_PARM_DESC(mode, "video mode (<x>x<y>[-<bpp>][@<refr>])");

module_param_named(crt, crt_option, int, 0444);
MODULE_PARM_DESC(crt, "enable CRT output. 0 = off, 1 = on (default)");

module_param_string(panel, panel_option, sizeof(panel_option), 0444);
MODULE_PARM_DESC(panel, "size of attached flat panel (<x>x<y>)");

MODULE_DESCRIPTION("framebuffer driver for the AMD Geode GX1");
MODULE_LICENSE("GPL");
