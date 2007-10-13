/*
 * Geode LX framebuffer driver.
 *
 * Copyright (C) 2007 Advanced Micro Devices, Inc.
 * Built from gxfb (which is Copyright (C) 2006 Arcom Control Systems Ltd.)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/console.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/uaccess.h>

#include "lxfb.h"

static char *mode_option;
static int noclear, nopanel, nocrt;
static int fbsize;

/* Most of these modes are sorted in ascending order, but
 * since the first entry in this table is the "default" mode,
 * we try to make it something sane - 640x480-60 is sane
 */

const struct fb_videomode geode_modedb[] __initdata = {
	/* 640x480-60 */
	{ NULL, 60, 640, 480, 39682, 48, 8, 25, 2, 88, 2,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 640x400-70 */
	{ NULL, 70, 640, 400, 39770, 40, 8, 28, 5, 96, 2,
	  FB_SYNC_HOR_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 640x480-70 */
	{ NULL, 70, 640, 480, 35014, 88, 24, 15, 2, 64, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 640x480-72 */
	{ NULL, 72, 640, 480, 32102, 120, 16, 20, 1, 40, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 640x480-75 */
	{ NULL, 75, 640, 480, 31746, 120, 16, 16, 1, 64, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 640x480-85 */
	{ NULL, 85, 640, 480, 27780, 80, 56, 25, 1, 56, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 640x480-90 */
	{ NULL, 90, 640, 480, 26392, 96, 32, 22, 1, 64, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 640x480-100 */
	{ NULL, 100, 640, 480, 23167, 104, 40, 25, 1, 64, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 640x480-60 */
	{ NULL, 60, 640, 480, 39682, 48, 16, 25, 10, 88, 2,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-56 */
	{ NULL, 56, 800, 600, 27901, 128, 24, 22, 1, 72, 2,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-60 */
	{ NULL, 60, 800, 600, 25131, 72, 32, 23, 1, 136, 4,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-70 */
	{ NULL, 70, 800, 600, 21873, 120, 40, 21, 4, 80, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-72 */
	{ NULL, 72, 800, 600, 20052, 64, 56, 23, 37, 120, 6,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-75 */
	{ NULL, 75, 800, 600, 20202, 160, 16, 21, 1, 80, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-85 */
	{ NULL, 85, 800, 600, 17790, 152, 32, 27, 1, 64, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-90 */
	{ NULL, 90, 800, 600, 16648, 128, 40, 28, 1, 88, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-100 */
	{ NULL, 100, 800, 600, 14667, 136, 48, 27, 1, 88, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 800x600-60 */
	{ NULL, 60, 800, 600, 25131, 88, 40, 23, 1, 128, 4,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 1024x768-60 */
	{ NULL, 60, 1024, 768, 15385, 160, 24, 29, 3, 136, 6,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 1024x768-70 */
	{ NULL, 70, 1024, 768, 13346, 144, 24, 29, 3, 136, 6,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 1024x768-72 */
	{ NULL, 72, 1024, 768, 12702, 168, 56, 29, 4, 112, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1024x768-75 */
	{ NULL, 75, 1024, 768, 12703, 176, 16, 28, 1, 96, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1024x768-85 */
	{ NULL, 85, 1024, 768, 10581, 208, 48, 36, 1, 96, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1024x768-90 */
	{ NULL, 90, 1024, 768, 9981, 176, 64, 37, 1, 112, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1024x768-100 */
	{ NULL, 100, 1024, 768, 8825, 184, 72, 42, 1, 112, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1024x768-60 */
	{ NULL, 60, 1024, 768, 15385, 160, 24, 29, 3, 136, 6,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 1152x864-60 */
	{ NULL, 60, 1152, 864, 12251, 184, 64, 27, 1, 120, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1152x864-70 */
	{ NULL, 70, 1152, 864, 10254, 192, 72, 32, 8, 120, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1152x864-72 */
	{ NULL, 72, 1152, 864, 9866, 200, 72, 33, 7, 128, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1152x864-75 */
	{ NULL, 75, 1152, 864, 9259, 256, 64, 32, 1, 128, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1152x864-85 */
	{ NULL, 85, 1152, 864, 8357, 200, 72, 37, 3, 128, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1152x864-90 */
	{ NULL, 90, 1152, 864, 7719, 208, 80, 42, 9, 128, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1152x864-100 */
	{ NULL, 100, 1152, 864, 6947, 208, 80, 48, 3, 128, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1152x864-60 */
	{ NULL, 60, 1152, 864, 12251, 184, 64, 27, 1, 120, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 1280x1024-60 */
	{ NULL, 60, 1280, 1024, 9262, 248, 48, 38, 1, 112, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1280x1024-70 */
	{ NULL, 70, 1280, 1024, 7719, 224, 88, 38, 6, 136, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1280x1024-72 */
	{ NULL, 72, 1280, 1024, 7490, 224, 88, 39, 7, 136, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1280x1024-75 */
	{ NULL, 75, 1280, 1024, 7409, 248, 16, 38, 1, 144, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1280x1024-85 */
	{ NULL, 85, 1280, 1024, 6351, 224, 64, 44, 1, 160, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1280x1024-90 */
	{ NULL, 90, 1280, 1024, 5791, 240, 96, 51, 12, 144, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1280x1024-100 */
	{ NULL, 100, 1280, 1024, 5212, 240, 96, 57, 6, 144, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1280x1024-60 */
	{ NULL, 60, 1280, 1024, 9262, 248, 48, 38, 1, 112, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 1600x1200-60 */
	{ NULL, 60, 1600, 1200, 6172, 304, 64, 46, 1, 192, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1600x1200-70 */
	{ NULL, 70, 1600, 1200, 5291, 304, 64, 46, 1, 192, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1600x1200-72 */
	{ NULL, 72, 1600, 1200, 5053, 288, 112, 47, 13, 176, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1600x1200-75 */
	{ NULL, 75, 1600, 1200, 4938, 304, 64, 46, 1, 192, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1600x1200-85 */
	{ NULL, 85, 1600, 1200, 4357, 304, 64, 46, 1, 192, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1600x1200-90 */
	{ NULL, 90, 1600, 1200, 3981, 304, 128, 60, 1, 176, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1600x1200-100 */
	{ NULL, 100, 1600, 1200, 3563, 304, 128, 67, 1, 176, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1600x1200-60 */
	{ NULL, 60, 1600, 1200, 6172, 304, 64, 46, 1, 192, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
	/* 1920x1440-60 */
	{ NULL, 60, 1920, 1440, 4273, 344, 128, 56, 1, 208, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1920x1440-70 */
	{ NULL, 70, 1920, 1440, 3593, 360, 152, 55, 8, 208, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1920x1440-72 */
	{ NULL, 72, 1920, 1440, 3472, 360, 152, 68, 4, 208, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1920x1440-75 */
	{ NULL, 75, 1920, 1440, 3367, 352, 144, 56, 1, 224, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
	/* 1920x1440-85 */
	{ NULL, 85, 1920, 1440, 2929, 368, 152, 68, 1, 216, 3,
	  0, FB_VMODE_NONINTERLACED, 0 },
};

static int lxfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (var->xres > 1920 || var->yres > 1440)
		return -EINVAL;

	if (var->bits_per_pixel == 32) {
		var->red.offset   = 16; var->red.length   = 8;
		var->green.offset =  8; var->green.length = 8;
		var->blue.offset  =  0; var->blue.length  = 8;
	} else if (var->bits_per_pixel == 16) {
		var->red.offset   = 11; var->red.length   = 5;
		var->green.offset =  5; var->green.length = 6;
		var->blue.offset  =  0; var->blue.length  = 5;
	} else if (var->bits_per_pixel == 8) {
		var->red.offset   = 0; var->red.length   = 8;
		var->green.offset = 0; var->green.length = 8;
		var->blue.offset  = 0; var->blue.length  = 8;
	} else
		return -EINVAL;

	var->transp.offset = 0; var->transp.length = 0;

	/* Enough video memory? */
	if ((lx_get_pitch(var->xres, var->bits_per_pixel) * var->yres)
	    > info->fix.smem_len)
	  return -EINVAL;

	return 0;
}

static int lxfb_set_par(struct fb_info *info)
{
	if (info->var.bits_per_pixel > 8) {
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		fb_dealloc_cmap(&info->cmap);
	} else {
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		fb_alloc_cmap(&info->cmap, 1<<info->var.bits_per_pixel, 0);
	}

	info->fix.line_length = lx_get_pitch(info->var.xres,
		info->var.bits_per_pixel);

	lx_set_mode(info);
	return 0;
}

static inline u_int chan_to_field(u_int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int lxfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
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

		lx_set_palette_reg(info, regno, red, green, blue);
	}

	return 0;
}

static int lxfb_blank(int blank_mode, struct fb_info *info)
{
	return lx_blank_display(info, blank_mode);
}


static int __init lxfb_map_video_memory(struct fb_info *info,
					struct pci_dev *dev)
{
	struct lxfb_par *par = info->par;
	int ret;

	ret = pci_enable_device(dev);

	if (ret)
		return ret;

	ret = pci_request_region(dev, 0, "lxfb-framebuffer");

	if (ret)
		return ret;

	ret = pci_request_region(dev, 1, "lxfb-gp");

	if (ret)
		return ret;

	ret = pci_request_region(dev, 2, "lxfb-vg");

	if (ret)
		return ret;

	ret = pci_request_region(dev, 3, "lxfb-vip");

	if (ret)
		return ret;

	info->fix.smem_start = pci_resource_start(dev, 0);
	info->fix.smem_len = fbsize ? fbsize : lx_framebuffer_size();

	info->screen_base = ioremap(info->fix.smem_start, info->fix.smem_len);

	ret = -ENOMEM;

	if (info->screen_base == NULL)
		return ret;

	par->gp_regs = ioremap(pci_resource_start(dev, 1),
				pci_resource_len(dev, 1));

	if (par->gp_regs == NULL)
		return ret;

	par->dc_regs = ioremap(pci_resource_start(dev, 2),
			       pci_resource_len(dev, 2));

	if (par->dc_regs == NULL)
		return ret;

	par->df_regs = ioremap(pci_resource_start(dev, 3),
			       pci_resource_len(dev, 3));

	if (par->df_regs == NULL)
		return ret;

	writel(DC_UNLOCK_CODE, par->dc_regs + DC_UNLOCK);

	writel(info->fix.smem_start & 0xFF000000,
	       par->dc_regs + DC_PHY_MEM_OFFSET);

	writel(0, par->dc_regs + DC_UNLOCK);

	dev_info(&dev->dev, "%d KB of video memory at 0x%lx\n",
		 info->fix.smem_len / 1024, info->fix.smem_start);

	return 0;
}

static struct fb_ops lxfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= lxfb_check_var,
	.fb_set_par	= lxfb_set_par,
	.fb_setcolreg	= lxfb_setcolreg,
	.fb_blank       = lxfb_blank,
	/* No HW acceleration for now. */
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static struct fb_info * __init lxfb_init_fbinfo(struct device *dev)
{
	struct lxfb_par *par;
	struct fb_info *info;

	/* Alloc enough space for the pseudo palette. */
	info = framebuffer_alloc(sizeof(struct lxfb_par) + sizeof(u32) * 16,
				 dev);
	if (!info)
		return NULL;

	par = info->par;

	strcpy(info->fix.id, "Geode LX");

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

	info->fbops		= &lxfb_ops;
	info->flags		= FBINFO_DEFAULT;
	info->node		= -1;

	info->pseudo_palette	= (void *)par + sizeof(struct lxfb_par);

	info->var.grayscale	= 0;

	return info;
}

static int __init lxfb_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct lxfb_par *par;
	struct fb_info *info;
	int ret;

	struct fb_videomode *modedb_ptr;
	int modedb_size;

	info = lxfb_init_fbinfo(&pdev->dev);

	if (info == NULL)
		return -ENOMEM;

	par = info->par;

	ret = lxfb_map_video_memory(info, pdev);

	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to map frame buffer or controller registers\n");
		goto err;
	}

	/* Set up the desired outputs */

	par->output = 0;
	par->output |= (nopanel) ? 0 : OUTPUT_PANEL;
	par->output |= (nocrt) ? 0 : OUTPUT_CRT;

	/* Set up the mode database */

	modedb_ptr = (struct fb_videomode *) geode_modedb;
	modedb_size = ARRAY_SIZE(geode_modedb);

	ret = fb_find_mode(&info->var, info, mode_option,
			   modedb_ptr, modedb_size, NULL, 16);

	if (ret == 0 || ret == 4) {
		dev_err(&pdev->dev, "could not find valid video mode\n");
		ret = -EINVAL;
		goto err;
	}

	/* Clear the screen of garbage, unless noclear was specified,
	 * in which case we assume the user knows what he is doing */

	if (!noclear)
		memset_io(info->screen_base, 0, info->fix.smem_len);

	/* Set the mode */

	lxfb_check_var(&info->var, info);
	lxfb_set_par(info);

	if (register_framebuffer(info) < 0) {
		ret = -EINVAL;
		goto err;
	}
	pci_set_drvdata(pdev, info);
	printk(KERN_INFO "fb%d: %s frame buffer device\n",
		info->node, info->fix.id);

	return 0;

err:
	if (info->screen_base) {
		iounmap(info->screen_base);
		pci_release_region(pdev, 0);
	}
	if (par->gp_regs) {
		iounmap(par->gp_regs);
		pci_release_region(pdev, 1);
	}
	if (par->dc_regs) {
		iounmap(par->dc_regs);
		pci_release_region(pdev, 2);
	}
	if (par->df_regs) {
		iounmap(par->df_regs);
		pci_release_region(pdev, 3);
	}

	if (info)
		framebuffer_release(info);

	return ret;
}

static void lxfb_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct lxfb_par *par = info->par;

	unregister_framebuffer(info);

	iounmap(info->screen_base);
	pci_release_region(pdev, 0);

	iounmap(par->gp_regs);
	pci_release_region(pdev, 1);

	iounmap(par->dc_regs);
	pci_release_region(pdev, 2);

	iounmap(par->df_regs);
	pci_release_region(pdev, 3);

	pci_set_drvdata(pdev, NULL);
	framebuffer_release(info);
}

static struct pci_device_id lxfb_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LX_VIDEO) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, lxfb_id_table);

static struct pci_driver lxfb_driver = {
	.name		= "lxfb",
	.id_table	= lxfb_id_table,
	.probe		= lxfb_probe,
	.remove		= lxfb_remove,
};

#ifndef MODULE
static int __init lxfb_setup(char *options)
{
	char *opt;

	if (!options || !*options)
		return 0;

	while (1) {
		char *opt = strsep(&options, ",");

		if (opt == NULL)
			break;

		if (!*opt)
			continue;

		if (!strncmp(opt, "fbsize:", 7))
			fbsize = simple_strtoul(opt+7, NULL, 0);
		else if (!strcmp(opt, "noclear"))
			noclear = 1;
		else if (!strcmp(opt, "nopanel"))
			nopanel = 1;
		else if (!strcmp(opt, "nocrt"))
			nocrt = 1;
		else
			mode_option = opt;
	}

	return 0;
}
#endif

static int __init lxfb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("lxfb", &option))
		return -ENODEV;

	lxfb_setup(option);
#endif
	return pci_register_driver(&lxfb_driver);
}
static void __exit lxfb_cleanup(void)
{
	pci_unregister_driver(&lxfb_driver);
}

module_init(lxfb_init);
module_exit(lxfb_cleanup);

module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "video mode (<x>x<y>[-<bpp>][@<refr>])");

module_param(fbsize, int, 0);
MODULE_PARM_DESC(fbsize, "video memory size");

MODULE_DESCRIPTION("Framebuffer driver for the AMD Geode LX");
MODULE_LICENSE("GPL");
