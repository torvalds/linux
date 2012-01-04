/*
 * Driver for Aeroflex Gaisler SVGACTRL framebuffer device.
 *
 * 2011 (c) Aeroflex Gaisler AB
 *
 * Full documentation of the core can be found here:
 * http://www.gaisler.com/products/grlib/grip.pdf
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * Contributors: Kristoffer Glembo <kristoffer@gaisler.com>
 *
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/io.h>

struct grvga_regs {
	u32 status; 		/* 0x00 */
	u32 video_length; 	/* 0x04 */
	u32 front_porch;	/* 0x08 */
	u32 sync_length;	/* 0x0C */
	u32 line_length;	/* 0x10 */
	u32 fb_pos;		/* 0x14 */
	u32 clk_vector[4];	/* 0x18 */
	u32 clut;	        /* 0x20 */
};

struct grvga_par {
	struct grvga_regs *regs;
	u32 color_palette[16];  /* 16 entry pseudo palette used by fbcon in true color mode */
	int clk_sel;
	int fb_alloced;         /* = 1 if framebuffer is allocated in main memory */
};


static const struct fb_videomode grvga_modedb[] = {
    {
	/* 640x480 @ 60 Hz */
	NULL, 60, 640, 480, 40000, 48, 16, 39, 11, 96, 2,
	0, FB_VMODE_NONINTERLACED
    }, {
	/* 800x600 @ 60 Hz */
	NULL, 60, 800, 600, 25000, 88, 40, 23, 1, 128, 4,
	0, FB_VMODE_NONINTERLACED
    }, {
	/* 800x600 @ 72 Hz */
	NULL, 72, 800, 600, 20000, 64, 56, 23, 37, 120, 6,
	0, FB_VMODE_NONINTERLACED
    }, {
	/* 1024x768 @ 60 Hz */
	NULL, 60, 1024, 768, 15385, 160, 24, 29, 3, 136, 6,
	0, FB_VMODE_NONINTERLACED
    }
 };

static struct fb_fix_screeninfo grvga_fix __initdata = {
	.id =		"AG SVGACTRL",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =       FB_VISUAL_PSEUDOCOLOR,
	.xpanstep =	0,
	.ypanstep =	1,
	.ywrapstep =	0,
	.accel =	FB_ACCEL_NONE,
};

static int grvga_check_var(struct fb_var_screeninfo *var,
			   struct fb_info *info)
{
	struct grvga_par *par = info->par;
	int i;

	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;
	if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 24)
		var->bits_per_pixel = 24;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;
	else
		return -EINVAL;

	var->xres_virtual = var->xres;
	var->yres_virtual = 2*var->yres;

	if (info->fix.smem_len) {
		if ((var->yres_virtual*var->xres_virtual*var->bits_per_pixel/8) > info->fix.smem_len)
			return -ENOMEM;
	}

	/* Which clocks that are available can be read out in these registers */
	for (i = 0; i <= 3 ; i++) {
		if (var->pixclock == par->regs->clk_vector[i])
			break;
	}
	if (i <= 3)
		par->clk_sel = i;
	else
		return -EINVAL;

	switch (info->var.bits_per_pixel) {
	case 8:
		var->red   = (struct fb_bitfield) {0, 8, 0};      /* offset, length, msb-right */
		var->green = (struct fb_bitfield) {0, 8, 0};
		var->blue  = (struct fb_bitfield) {0, 8, 0};
		var->transp = (struct fb_bitfield) {0, 0, 0};
		break;
	case 16:
		var->red   = (struct fb_bitfield) {11, 5, 0};
		var->green = (struct fb_bitfield) {5, 6, 0};
		var->blue  = (struct fb_bitfield) {0, 5, 0};
		var->transp = (struct fb_bitfield) {0, 0, 0};
		break;
	case 24:
	case 32:
		var->red   = (struct fb_bitfield) {16, 8, 0};
		var->green = (struct fb_bitfield) {8, 8, 0};
		var->blue  = (struct fb_bitfield) {0, 8, 0};
		var->transp = (struct fb_bitfield) {24, 8, 0};
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int grvga_set_par(struct fb_info *info)
{

	u32 func = 0;
	struct grvga_par *par = info->par;

	__raw_writel(((info->var.yres - 1) << 16) | (info->var.xres - 1),
		     &par->regs->video_length);

	__raw_writel((info->var.lower_margin << 16) | (info->var.right_margin),
		     &par->regs->front_porch);

	__raw_writel((info->var.vsync_len << 16) | (info->var.hsync_len),
		     &par->regs->sync_length);

	__raw_writel(((info->var.yres + info->var.lower_margin + info->var.upper_margin + info->var.vsync_len - 1) << 16) |
		     (info->var.xres + info->var.right_margin + info->var.left_margin + info->var.hsync_len - 1),
		     &par->regs->line_length);

	switch (info->var.bits_per_pixel) {
	case 8:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		func = 1;
		break;
	case 16:
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		func = 2;
		break;
	case 24:
	case 32:
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		func = 3;
		break;
	default:
		return -EINVAL;
	}

	__raw_writel((par->clk_sel << 6) | (func << 4) | 1,
		     &par->regs->status);

	info->fix.line_length = (info->var.xres_virtual*info->var.bits_per_pixel)/8;
	return 0;
}

static int grvga_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue, unsigned transp, struct fb_info *info)
{
	struct grvga_par *par;
	par = info->par;

	if (regno >= 256)	/* Size of CLUT */
		return -EINVAL;

	if (info->var.grayscale) {
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}



#define CNVT_TOHW(val, width) ((((val)<<(width))+0x7FFF-(val))>>16)

	red    = CNVT_TOHW(red,   info->var.red.length);
	green  = CNVT_TOHW(green, info->var.green.length);
	blue   = CNVT_TOHW(blue,  info->var.blue.length);
	transp = CNVT_TOHW(transp, info->var.transp.length);

#undef CNVT_TOHW

	/* In PSEUDOCOLOR we use the hardware CLUT */
	if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR)
		__raw_writel((regno << 24) | (red << 16) | (green << 8) | blue,
			     &par->regs->clut);

	/* Truecolor uses the pseudo palette */
	else if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 v;
		if (regno >= 16)
			return -EINVAL;


		v =     (red    << info->var.red.offset)   |
			(green  << info->var.green.offset) |
			(blue   << info->var.blue.offset)  |
			(transp << info->var.transp.offset);

		((u32 *) (info->pseudo_palette))[regno] = v;
	}
	return 0;
}

static int grvga_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct grvga_par *par = info->par;
	struct fb_fix_screeninfo *fix = &info->fix;
	u32 base_addr;

	if (var->xoffset != 0)
		return -EINVAL;

	base_addr = fix->smem_start + (var->yoffset * fix->line_length);
	base_addr &= ~3UL;

	/* Set framebuffer base address  */
	__raw_writel(base_addr,
		     &par->regs->fb_pos);

	return 0;
}

static struct fb_ops grvga_ops = {
	.owner          = THIS_MODULE,
	.fb_check_var   = grvga_check_var,
	.fb_set_par	= grvga_set_par,
	.fb_setcolreg   = grvga_setcolreg,
	.fb_pan_display = grvga_pan_display,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit
};

static int __init grvga_parse_custom(char *options,
				     struct fb_var_screeninfo *screendata)
{
	char *this_opt;
	int count = 0;
	if (!options || !*options)
		return -1;

	while ((this_opt = strsep(&options, " ")) != NULL) {
		if (!*this_opt)
			continue;

		switch (count) {
		case 0:
			screendata->pixclock = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 1:
			screendata->xres = screendata->xres_virtual = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 2:
			screendata->right_margin = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 3:
			screendata->hsync_len = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 4:
			screendata->left_margin = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 5:
			screendata->yres = screendata->yres_virtual = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 6:
			screendata->lower_margin = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 7:
			screendata->vsync_len = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 8:
			screendata->upper_margin = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		case 9:
			screendata->bits_per_pixel = simple_strtoul(this_opt, NULL, 0);
			count++;
			break;
		default:
			return -1;
		}
	}
	screendata->activate  = FB_ACTIVATE_NOW;
	screendata->vmode     = FB_VMODE_NONINTERLACED;
	return 0;
}

static int __devinit grvga_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int retval = -ENOMEM;
	unsigned long virtual_start;
	unsigned long grvga_fix_addr = 0;
	unsigned long physical_start = 0;
	unsigned long grvga_mem_size = 0;
	struct grvga_par *par = NULL;
	char *options = NULL, *mode_opt = NULL;

	info = framebuffer_alloc(sizeof(struct grvga_par), &dev->dev);
	if (!info) {
		dev_err(&dev->dev, "framebuffer_alloc failed\n");
		return -ENOMEM;
	}

	/* Expecting: "grvga: modestring, [addr:<framebuffer physical address>], [size:<framebuffer size>]
	 *
	 * If modestring is custom:<custom mode string> we parse the string which then contains all videoparameters
	 * If address is left out, we allocate memory,
	 * if size is left out we only allocate enough to support the given mode.
	 */
	if (fb_get_options("grvga", &options)) {
		retval = -ENODEV;
		goto err;
	}

	if (!options || !*options)
		options =  "640x480-8@60";

	while (1) {
		char *this_opt = strsep(&options, ",");

		if (!this_opt)
			break;

		if (!strncmp(this_opt, "custom", 6)) {
			if (grvga_parse_custom(this_opt, &info->var) < 0) {
				dev_err(&dev->dev, "Failed to parse custom mode (%s).\n", this_opt);
				retval = -EINVAL;
				goto err1;
			}
		} else if (!strncmp(this_opt, "addr", 4))
			grvga_fix_addr = simple_strtoul(this_opt + 5, NULL, 16);
		else if (!strncmp(this_opt, "size", 4))
			grvga_mem_size = simple_strtoul(this_opt + 5, NULL, 0);
		else
			mode_opt = this_opt;
	}

	par = info->par;
	info->fbops = &grvga_ops;
	info->fix = grvga_fix;
	info->pseudo_palette = par->color_palette;
	info->flags = FBINFO_DEFAULT | FBINFO_PARTIAL_PAN_OK | FBINFO_HWACCEL_YPAN;
	info->fix.smem_len = grvga_mem_size;

	if (!request_mem_region(dev->resource[0].start, resource_size(&dev->resource[0]), "grlib-svgactrl regs")) {
		dev_err(&dev->dev, "registers already mapped\n");
		retval = -EBUSY;
		goto err;
	}

	par->regs = of_ioremap(&dev->resource[0], 0,
			       resource_size(&dev->resource[0]),
			       "grlib-svgactrl regs");

	if (!par->regs) {
		dev_err(&dev->dev, "failed to map registers\n");
		retval = -ENOMEM;
		goto err1;
	}

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0) {
		dev_err(&dev->dev, "failed to allocate mem with fb_alloc_cmap\n");
		retval = -ENOMEM;
		goto err2;
	}

	if (mode_opt) {
		retval = fb_find_mode(&info->var, info, mode_opt,
				      grvga_modedb, sizeof(grvga_modedb), &grvga_modedb[0], 8);
		if (!retval || retval == 4) {
			retval = -EINVAL;
			goto err3;
		}
	}

	if (!grvga_mem_size)
		grvga_mem_size = info->var.xres_virtual * info->var.yres_virtual * info->var.bits_per_pixel/8;

	if (grvga_fix_addr) {
		/* Got framebuffer base address from argument list */

		physical_start = grvga_fix_addr;

		if (!request_mem_region(physical_start, grvga_mem_size, dev->name)) {
			dev_err(&dev->dev, "failed to request memory region\n");
			retval = -ENOMEM;
			goto err3;
		}

		virtual_start = (unsigned long) ioremap(physical_start, grvga_mem_size);

		if (!virtual_start) {
			dev_err(&dev->dev, "error mapping framebuffer memory\n");
			retval = -ENOMEM;
			goto err4;
		}
	} else {	/* Allocate frambuffer memory */

		unsigned long page;

		virtual_start = (unsigned long) __get_free_pages(GFP_DMA,
								 get_order(grvga_mem_size));
		if (!virtual_start) {
			dev_err(&dev->dev,
				"unable to allocate framebuffer memory (%lu bytes)\n",
				grvga_mem_size);
			retval = -ENOMEM;
			goto err3;
		}

		physical_start = dma_map_single(&dev->dev, (void *)virtual_start, grvga_mem_size, DMA_TO_DEVICE);

		/* Set page reserved so that mmap will work. This is necessary
		 * since we'll be remapping normal memory.
		 */
		for (page = virtual_start;
		     page < PAGE_ALIGN(virtual_start + grvga_mem_size);
		     page += PAGE_SIZE) {
			SetPageReserved(virt_to_page(page));
		}

		par->fb_alloced = 1;
	}

	memset((unsigned long *) virtual_start, 0, grvga_mem_size);

	info->screen_base = (char __iomem *) virtual_start;
	info->fix.smem_start = physical_start;
	info->fix.smem_len   = grvga_mem_size;

	dev_set_drvdata(&dev->dev, info);

	dev_info(&dev->dev,
		 "Aeroflex Gaisler framebuffer device (fb%d), %dx%d-%d, using %luK of video memory @ %p\n",
		 info->node, info->var.xres, info->var.yres, info->var.bits_per_pixel,
		 grvga_mem_size >> 10, info->screen_base);

	retval = register_framebuffer(info);
	if (retval < 0) {
		dev_err(&dev->dev, "failed to register framebuffer\n");
		goto err4;
	}

	__raw_writel(physical_start, &par->regs->fb_pos);
	__raw_writel(__raw_readl(&par->regs->status) | 1,  /* Enable framebuffer */
		     &par->regs->status);

	return 0;

err4:
	dev_set_drvdata(&dev->dev, NULL);
	if (grvga_fix_addr) {
		release_mem_region(physical_start, grvga_mem_size);
		iounmap((void *)virtual_start);
	} else
		kfree((void *)virtual_start);
err3:
	fb_dealloc_cmap(&info->cmap);
err2:
	of_iounmap(&dev->resource[0], par->regs,
		   resource_size(&dev->resource[0]));
err1:
	release_mem_region(dev->resource[0].start, resource_size(&dev->resource[0]));
err:
	framebuffer_release(info);

	return retval;
}

static int __devexit grvga_remove(struct platform_device *device)
{
	struct fb_info *info = dev_get_drvdata(&device->dev);
	struct grvga_par *par = info->par;

	if (info) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);

		of_iounmap(&device->resource[0], par->regs,
			   resource_size(&device->resource[0]));
		release_mem_region(device->resource[0].start, resource_size(&device->resource[0]));

		if (!par->fb_alloced) {
			release_mem_region(info->fix.smem_start, info->fix.smem_len);
			iounmap(info->screen_base);
		} else
			kfree((void *)info->screen_base);

		framebuffer_release(info);
		dev_set_drvdata(&device->dev, NULL);
	}

	return 0;
}

static struct of_device_id svgactrl_of_match[] = {
	{
		.name = "GAISLER_SVGACTRL",
	},
	{
		.name = "01_063",
	},
	{},
};
MODULE_DEVICE_TABLE(of, svgactrl_of_match);

static struct platform_driver grvga_driver = {
	.driver = {
		.name = "grlib-svgactrl",
		.owner = THIS_MODULE,
		.of_match_table = svgactrl_of_match,
	},
	.probe		= grvga_probe,
	.remove		= __devexit_p(grvga_remove),
};


static int __init grvga_init(void)
{
	return platform_driver_register(&grvga_driver);
}

static void __exit grvga_exit(void)
{
	platform_driver_unregister(&grvga_driver);
}

module_init(grvga_init);
module_exit(grvga_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aeroflex Gaisler");
MODULE_DESCRIPTION("Aeroflex Gaisler framebuffer device driver");
