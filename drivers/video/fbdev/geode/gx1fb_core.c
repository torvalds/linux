// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/video/geode/gx1fb_core.c
 *   -- Geode GX1 framebuffer driver
 *
 * Copyright (C) 2005 Arcom Control Systems Ltd.
 */

#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
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

/* Modes relevant to the GX1 (taken from modedb.c) */
static const struct fb_videomode gx1_modedb[] = {
	/* 640x480-60 VESA */
	{ NULL, 60, 640, 480, 39682,  48, 16, 33, 10, 96, 2,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 640x480-75 VESA */
	{ NULL, 75, 640, 480, 31746, 120, 16, 16, 01, 64, 3,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 640x480-85 VESA */
	{ NULL, 85, 640, 480, 27777, 80, 56, 25, 01, 56, 3,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 800x600-60 VESA */
	{ NULL, 60, 800, 600, 25000, 88, 40, 23, 01, 128, 4,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 800x600-75 VESA */
	{ NULL, 75, 800, 600, 20202, 160, 16, 21, 01, 80, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 800x600-85 VESA */
	{ NULL, 85, 800, 600, 17761, 152, 32, 27, 01, 64, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1024x768-60 VESA */
	{ NULL, 60, 1024, 768, 15384, 160, 24, 29, 3, 136, 6,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1024x768-75 VESA */
	{ NULL, 75, 1024, 768, 12690, 176, 16, 28, 1, 96, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1024x768-85 VESA */
	{ NULL, 85, 1024, 768, 10582, 208, 48, 36, 1, 96, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1280x960-60 VESA */
	{ NULL, 60, 1280, 960, 9259, 312, 96, 36, 1, 112, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1280x960-85 VESA */
	{ NULL, 85, 1280, 960, 6734, 224, 64, 47, 1, 160, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1280x1024-60 VESA */
	{ NULL, 60, 1280, 1024, 9259, 248, 48, 38, 1, 112, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1280x1024-75 VESA */
	{ NULL, 75, 1280, 1024, 7407, 248, 16, 38, 1, 144, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1280x1024-85 VESA */
	{ NULL, 85, 1280, 1024, 6349, 224, 64, 44, 1, 160, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
};

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

	if (info->var.bits_per_pixel == 16)
		info->fix.visual = FB_VISUAL_TRUECOLOR;
	else
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

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

static int gx1fb_map_video_memory(struct fb_info *info, struct pci_dev *dev)
{
	struct geodefb_par *par = info->par;
	unsigned gx_base;
	int fb_len;
	int ret;

	gx_base = gx1_gx_base();
	if (!gx_base)
		return -ENODEV;

	ret = pci_enable_device(dev);
	if (ret < 0)
		return ret;

	ret = pci_request_region(dev, 0, "gx1fb (video)");
	if (ret < 0)
		return ret;
	par->vid_regs = pci_ioremap_bar(dev, 0);
	if (!par->vid_regs)
		return -ENOMEM;

	if (!request_mem_region(gx_base + 0x8300, 0x100, "gx1fb (display controller)"))
		return -EBUSY;
	par->dc_regs = ioremap(gx_base + 0x8300, 0x100);
	if (!par->dc_regs)
		return -ENOMEM;

	if ((fb_len = gx1_frame_buffer_size()) < 0)
		return -ENOMEM;
	info->fix.smem_start = gx_base + 0x800000;
	info->fix.smem_len = fb_len;
	info->screen_base = ioremap(info->fix.smem_start, info->fix.smem_len);
	if (!info->screen_base)
		return -ENOMEM;

	dev_info(&dev->dev, "%d Kibyte of video memory at 0x%lx\n",
		 info->fix.smem_len / 1024, info->fix.smem_start);

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

static const struct fb_ops gx1fb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_IOMEM_OPS,
	.fb_check_var	= gx1fb_check_var,
	.fb_set_par	= gx1fb_set_par,
	.fb_setcolreg	= gx1fb_setcolreg,
	.fb_blank       = gx1fb_blank,
};

static struct fb_info *gx1fb_init_fbinfo(struct device *dev)
{
	struct geodefb_par *par;
	struct fb_info *info;

	/* Alloc enough space for the pseudo palette. */
	info = framebuffer_alloc(sizeof(struct geodefb_par) + sizeof(u32) * 16, dev);
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
	info->node		= -1;

	info->pseudo_palette	= (void *)par + sizeof(struct geodefb_par);

	info->var.grayscale	= 0;

	/* CRT and panel options */
	par->enable_crt = crt_option;
	if (parse_panel_option(info) < 0)
		printk(KERN_WARNING "gx1fb: invalid 'panel' option -- disabling flat panel\n");
	if (!par->panel_x)
		par->enable_crt = 1; /* fall back to CRT if no panel is specified */

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		framebuffer_release(info);
		return NULL;
	}
	return info;
}

static int gx1fb_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct geodefb_par *par;
	struct fb_info *info;
	int ret;

	ret = aperture_remove_conflicting_pci_devices(pdev, "gx1fb");
	if (ret)
		return ret;

	info = gx1fb_init_fbinfo(&pdev->dev);
	if (!info)
		return -ENOMEM;
	par = info->par;

	/* GX1 display controller and CS5530 video device */
	par->dc_ops  = &gx1_dc_ops;
	par->vid_ops = &cs5530_vid_ops;

	if ((ret = gx1fb_map_video_memory(info, pdev)) < 0) {
		dev_err(&pdev->dev, "failed to map frame buffer or controller registers\n");
		goto err;
	}

	ret = fb_find_mode(&info->var, info, mode_option,
			   gx1_modedb, ARRAY_SIZE(gx1_modedb), NULL, 16);
	if (ret == 0 || ret == 4) {
		dev_err(&pdev->dev, "could not find valid video mode\n");
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
	pci_set_drvdata(pdev, info);
	fb_info(info, "%s frame buffer device\n", info->fix.id);
	return 0;

  err:
	if (info->screen_base) {
		iounmap(info->screen_base);
		pci_release_region(pdev, 0);
	}
	if (par->vid_regs) {
		iounmap(par->vid_regs);
		pci_release_region(pdev, 1);
	}
	if (par->dc_regs) {
		iounmap(par->dc_regs);
		release_mem_region(gx1_gx_base() + 0x8300, 0x100);
	}

	fb_dealloc_cmap(&info->cmap);
	framebuffer_release(info);

	return ret;
}

static void gx1fb_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct geodefb_par *par = info->par;

	unregister_framebuffer(info);

	iounmap((void __iomem *)info->screen_base);
	pci_release_region(pdev, 0);

	iounmap(par->vid_regs);
	pci_release_region(pdev, 1);

	iounmap(par->dc_regs);
	release_mem_region(gx1_gx_base() + 0x8300, 0x100);

	fb_dealloc_cmap(&info->cmap);

	framebuffer_release(info);
}

#ifndef MODULE
static void __init gx1fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return;

	while ((this_opt = strsep(&options, ","))) {
		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "mode:", 5))
			strscpy(mode_option, this_opt + 5, sizeof(mode_option));
		else if (!strncmp(this_opt, "crt:", 4))
			crt_option = !!simple_strtoul(this_opt + 4, NULL, 0);
		else if (!strncmp(this_opt, "panel:", 6))
			strscpy(panel_option, this_opt + 6, sizeof(panel_option));
		else
			strscpy(mode_option, this_opt, sizeof(mode_option));
	}
}
#endif

static struct pci_device_id gx1fb_id_table[] = {
	{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5530_VIDEO,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY << 16,
	  0xff0000, 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, gx1fb_id_table);

static struct pci_driver gx1fb_driver = {
	.name		= "gx1fb",
	.id_table	= gx1fb_id_table,
	.probe		= gx1fb_probe,
	.remove		= gx1fb_remove,
};

static int __init gx1fb_init(void)
{
#ifndef MODULE
	char *option = NULL;
#endif

	if (fb_modesetting_disabled("gx1fb"))
		return -ENODEV;

#ifndef MODULE
	if (fb_get_options("gx1fb", &option))
		return -ENODEV;
	gx1fb_setup(option);
#endif
	return pci_register_driver(&gx1fb_driver);
}

static void gx1fb_cleanup(void)
{
	pci_unregister_driver(&gx1fb_driver);
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
