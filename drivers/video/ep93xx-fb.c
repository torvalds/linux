/*
 * linux/drivers/video/ep93xx-fb.c
 *
 * Framebuffer support for the EP93xx series.
 *
 * Copyright (C) 2007 Bluewater Systems Ltd
 * Author: Ryan Mallon
 *
 * Copyright (c) 2009 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on the Cirrus Logic ep93xxfb driver, and various other ep93xxfb
 * drivers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/fb.h>

#include <mach/fb.h>

/* Vertical Frame Timing Registers */
#define EP93XXFB_VLINES_TOTAL			0x0000	/* SW locked */
#define EP93XXFB_VSYNC				0x0004	/* SW locked */
#define EP93XXFB_VACTIVE			0x0008	/* SW locked */
#define EP93XXFB_VBLANK				0x0228	/* SW locked */
#define EP93XXFB_VCLK				0x000c	/* SW locked */

/* Horizontal Frame Timing Registers */
#define EP93XXFB_HCLKS_TOTAL			0x0010	/* SW locked */
#define EP93XXFB_HSYNC				0x0014	/* SW locked */
#define EP93XXFB_HACTIVE			0x0018	/* SW locked */
#define EP93XXFB_HBLANK				0x022c	/* SW locked */
#define EP93XXFB_HCLK				0x001c	/* SW locked */

/* Frame Buffer Memory Configuration Registers */
#define EP93XXFB_SCREEN_PAGE			0x0028
#define EP93XXFB_SCREEN_HPAGE			0x002c
#define EP93XXFB_SCREEN_LINES			0x0030
#define EP93XXFB_LINE_LENGTH			0x0034
#define EP93XXFB_VLINE_STEP			0x0038
#define EP93XXFB_LINE_CARRY			0x003c	/* SW locked */
#define EP93XXFB_EOL_OFFSET			0x0230

/* Other Video Registers */
#define EP93XXFB_BRIGHTNESS			0x0020
#define EP93XXFB_ATTRIBS			0x0024	/* SW locked */
#define EP93XXFB_SWLOCK				0x007c	/* SW locked */
#define EP93XXFB_AC_RATE			0x0214
#define EP93XXFB_FIFO_LEVEL			0x0234
#define EP93XXFB_PIXELMODE			0x0054
#define EP93XXFB_PIXELMODE_32BPP		(0x7 << 0)
#define EP93XXFB_PIXELMODE_24BPP		(0x6 << 0)
#define EP93XXFB_PIXELMODE_16BPP		(0x4 << 0)
#define EP93XXFB_PIXELMODE_8BPP			(0x2 << 0)
#define EP93XXFB_PIXELMODE_SHIFT_1P_24B		(0x0 << 3)
#define EP93XXFB_PIXELMODE_SHIFT_1P_18B		(0x1 << 3)
#define EP93XXFB_PIXELMODE_COLOR_LUT		(0x0 << 10)
#define EP93XXFB_PIXELMODE_COLOR_888		(0x4 << 10)
#define EP93XXFB_PIXELMODE_COLOR_555		(0x5 << 10)
#define EP93XXFB_PARL_IF_OUT			0x0058
#define EP93XXFB_PARL_IF_IN			0x005c

/* Blink Control Registers */
#define EP93XXFB_BLINK_RATE			0x0040
#define EP93XXFB_BLINK_MASK			0x0044
#define EP93XXFB_BLINK_PATTRN			0x0048
#define EP93XXFB_PATTRN_MASK			0x004c
#define EP93XXFB_BKGRND_OFFSET			0x0050

/* Hardware Cursor Registers */
#define EP93XXFB_CURSOR_ADR_START		0x0060
#define EP93XXFB_CURSOR_ADR_RESET		0x0064
#define EP93XXFB_CURSOR_SIZE			0x0068
#define EP93XXFB_CURSOR_COLOR1			0x006c
#define EP93XXFB_CURSOR_COLOR2			0x0070
#define EP93XXFB_CURSOR_BLINK_COLOR1		0x021c
#define EP93XXFB_CURSOR_BLINK_COLOR2		0x0220
#define EP93XXFB_CURSOR_XY_LOC			0x0074
#define EP93XXFB_CURSOR_DSCAN_HY_LOC		0x0078
#define EP93XXFB_CURSOR_BLINK_RATE_CTRL		0x0224

/* LUT Registers */
#define EP93XXFB_GRY_SCL_LUTR			0x0080
#define EP93XXFB_GRY_SCL_LUTG			0x0280
#define EP93XXFB_GRY_SCL_LUTB			0x0300
#define EP93XXFB_LUT_SW_CONTROL			0x0218
#define EP93XXFB_LUT_SW_CONTROL_SWTCH		(1 << 0)
#define EP93XXFB_LUT_SW_CONTROL_SSTAT		(1 << 1)
#define EP93XXFB_COLOR_LUT			0x0400

/* Video Signature Registers */
#define EP93XXFB_VID_SIG_RSLT_VAL		0x0200
#define EP93XXFB_VID_SIG_CTRL			0x0204
#define EP93XXFB_VSIG				0x0208
#define EP93XXFB_HSIG				0x020c
#define EP93XXFB_SIG_CLR_STR			0x0210

/* Minimum / Maximum resolutions supported */
#define EP93XXFB_MIN_XRES			64
#define EP93XXFB_MIN_YRES			64
#define EP93XXFB_MAX_XRES			1024
#define EP93XXFB_MAX_YRES			768

struct ep93xx_fbi {
	struct ep93xxfb_mach_info	*mach_info;
	struct clk			*clk;
	struct resource			*res;
	void __iomem			*mmio_base;
	unsigned int			pseudo_palette[256];
};

static int check_screenpage_bug = 1;
module_param(check_screenpage_bug, int, 0644);
MODULE_PARM_DESC(check_screenpage_bug,
		 "Check for bit 27 screen page bug. Default = 1");

static inline unsigned int ep93xxfb_readl(struct ep93xx_fbi *fbi,
					  unsigned int off)
{
	return __raw_readl(fbi->mmio_base + off);
}

static inline void ep93xxfb_writel(struct ep93xx_fbi *fbi,
				   unsigned int val, unsigned int off)
{
	__raw_writel(val, fbi->mmio_base + off);
}

/*
 * Write to one of the locked raster registers.
 */
static inline void ep93xxfb_out_locked(struct ep93xx_fbi *fbi,
				       unsigned int val, unsigned int reg)
{
	/*
	 * We don't need a lock or delay here since the raster register
	 * block will remain unlocked until the next access.
	 */
	ep93xxfb_writel(fbi, 0xaa, EP93XXFB_SWLOCK);
	ep93xxfb_writel(fbi, val, reg);
}

static void ep93xxfb_set_video_attribs(struct fb_info *info)
{
	struct ep93xx_fbi *fbi = info->par;
	unsigned int attribs;

	attribs = EP93XXFB_ENABLE;
	attribs |= fbi->mach_info->flags;
	ep93xxfb_out_locked(fbi, attribs, EP93XXFB_ATTRIBS);
}

static int ep93xxfb_set_pixelmode(struct fb_info *info)
{
	struct ep93xx_fbi *fbi = info->par;
	unsigned int val;

	info->var.transp.offset = 0;
	info->var.transp.length = 0;

	switch (info->var.bits_per_pixel) {
	case 8:
		val = EP93XXFB_PIXELMODE_8BPP | EP93XXFB_PIXELMODE_COLOR_LUT |
			EP93XXFB_PIXELMODE_SHIFT_1P_18B;

		info->var.red.offset	= 0;
		info->var.red.length	= 8;
		info->var.green.offset	= 0;
		info->var.green.length	= 8;
		info->var.blue.offset	= 0;
		info->var.blue.length	= 8;
		info->fix.visual 	= FB_VISUAL_PSEUDOCOLOR;
		break;

	case 16:
		val = EP93XXFB_PIXELMODE_16BPP | EP93XXFB_PIXELMODE_COLOR_555 |
			EP93XXFB_PIXELMODE_SHIFT_1P_18B;

		info->var.red.offset	= 11;
		info->var.red.length	= 5;
		info->var.green.offset	= 5;
		info->var.green.length	= 6;
		info->var.blue.offset	= 0;
		info->var.blue.length	= 5;
		info->fix.visual 	= FB_VISUAL_TRUECOLOR;
		break;

	case 24:
		val = EP93XXFB_PIXELMODE_24BPP | EP93XXFB_PIXELMODE_COLOR_888 |
			EP93XXFB_PIXELMODE_SHIFT_1P_24B;

		info->var.red.offset	= 16;
		info->var.red.length	= 8;
		info->var.green.offset	= 8;
		info->var.green.length	= 8;
		info->var.blue.offset	= 0;
		info->var.blue.length	= 8;
		info->fix.visual 	= FB_VISUAL_TRUECOLOR;
		break;

	case 32:
		val = EP93XXFB_PIXELMODE_32BPP | EP93XXFB_PIXELMODE_COLOR_888 |
			EP93XXFB_PIXELMODE_SHIFT_1P_24B;

		info->var.red.offset	= 16;
		info->var.red.length	= 8;
		info->var.green.offset	= 8;
		info->var.green.length	= 8;
		info->var.blue.offset	= 0;
		info->var.blue.length	= 8;
		info->fix.visual 	= FB_VISUAL_TRUECOLOR;
		break;

	default:
		return -EINVAL;
	}

	ep93xxfb_writel(fbi, val, EP93XXFB_PIXELMODE);
	return 0;
}

static void ep93xxfb_set_timing(struct fb_info *info)
{
	struct ep93xx_fbi *fbi = info->par;
	unsigned int vlines_total, hclks_total, start, stop;

	vlines_total = info->var.yres + info->var.upper_margin +
		info->var.lower_margin + info->var.vsync_len - 1;

	hclks_total = info->var.xres + info->var.left_margin +
		info->var.right_margin + info->var.hsync_len - 1;

	ep93xxfb_out_locked(fbi, vlines_total, EP93XXFB_VLINES_TOTAL);
	ep93xxfb_out_locked(fbi, hclks_total, EP93XXFB_HCLKS_TOTAL);

	start = vlines_total;
	stop = vlines_total - info->var.vsync_len;
	ep93xxfb_out_locked(fbi, start | (stop << 16), EP93XXFB_VSYNC);

	start = vlines_total - info->var.vsync_len - info->var.upper_margin;
	stop = info->var.lower_margin - 1;
	ep93xxfb_out_locked(fbi, start | (stop << 16), EP93XXFB_VBLANK);
	ep93xxfb_out_locked(fbi, start | (stop << 16), EP93XXFB_VACTIVE);

	start = vlines_total;
	stop = vlines_total + 1;
	ep93xxfb_out_locked(fbi, start | (stop << 16), EP93XXFB_VCLK);

	start = hclks_total;
	stop = hclks_total - info->var.hsync_len;
	ep93xxfb_out_locked(fbi, start | (stop << 16), EP93XXFB_HSYNC);

	start = hclks_total - info->var.hsync_len - info->var.left_margin;
	stop = info->var.right_margin - 1;
	ep93xxfb_out_locked(fbi, start | (stop << 16), EP93XXFB_HBLANK);
	ep93xxfb_out_locked(fbi, start | (stop << 16), EP93XXFB_HACTIVE);

	start = hclks_total;
	stop = hclks_total;
	ep93xxfb_out_locked(fbi, start | (stop << 16), EP93XXFB_HCLK);

	ep93xxfb_out_locked(fbi, 0x0, EP93XXFB_LINE_CARRY);
}

static int ep93xxfb_set_par(struct fb_info *info)
{
	struct ep93xx_fbi *fbi = info->par;

	clk_set_rate(fbi->clk, 1000 * PICOS2KHZ(info->var.pixclock));

	ep93xxfb_set_timing(info);

	info->fix.line_length = info->var.xres_virtual *
		info->var.bits_per_pixel / 8;

	ep93xxfb_writel(fbi, info->fix.smem_start, EP93XXFB_SCREEN_PAGE);
	ep93xxfb_writel(fbi, info->var.yres - 1, EP93XXFB_SCREEN_LINES);
	ep93xxfb_writel(fbi, ((info->var.xres * info->var.bits_per_pixel)
			      / 32) - 1, EP93XXFB_LINE_LENGTH);
	ep93xxfb_writel(fbi, info->fix.line_length / 4, EP93XXFB_VLINE_STEP);
	ep93xxfb_set_video_attribs(info);
	return 0;
}

static int ep93xxfb_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	int err;

	err = ep93xxfb_set_pixelmode(info);
	if (err)
		return err;

	var->xres = max_t(unsigned int, var->xres, EP93XXFB_MIN_XRES);
	var->xres = min_t(unsigned int, var->xres, EP93XXFB_MAX_XRES);
	var->xres_virtual = max(var->xres_virtual, var->xres);

	var->yres = max_t(unsigned int, var->yres, EP93XXFB_MIN_YRES);
	var->yres = min_t(unsigned int, var->yres, EP93XXFB_MAX_YRES);
	var->yres_virtual = max(var->yres_virtual, var->yres);

	return 0;
}

static int ep93xxfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned int offset = vma->vm_pgoff << PAGE_SHIFT;

	if (offset < info->fix.smem_len) {
		return dma_mmap_writecombine(info->dev, vma, info->screen_base,
					     info->fix.smem_start,
					     info->fix.smem_len);
	}

	return -EINVAL;
}

static int ep93xxfb_blank(int blank_mode, struct fb_info *info)
{
	struct ep93xx_fbi *fbi = info->par;
	unsigned int attribs = ep93xxfb_readl(fbi, EP93XXFB_ATTRIBS);

	if (blank_mode) {
		if (fbi->mach_info->blank)
			fbi->mach_info->blank(blank_mode, info);
		ep93xxfb_out_locked(fbi, attribs & ~EP93XXFB_ENABLE,
				    EP93XXFB_ATTRIBS);
		clk_disable(fbi->clk);
	} else {
		clk_enable(fbi->clk);
		ep93xxfb_out_locked(fbi, attribs | EP93XXFB_ENABLE,
				    EP93XXFB_ATTRIBS);
		if (fbi->mach_info->blank)
			fbi->mach_info->blank(blank_mode, info);
	}

	return 0;
}

static inline int ep93xxfb_convert_color(int val, int width)
{
	return ((val << width) + 0x7fff - val) >> 16;
}

static int ep93xxfb_setcolreg(unsigned int regno, unsigned int red,
			      unsigned int green, unsigned int blue,
			      unsigned int transp, struct fb_info *info)
{
	struct ep93xx_fbi *fbi = info->par;
	unsigned int *pal = info->pseudo_palette;
	unsigned int ctrl, i, rgb, lut_current, lut_stat;

	switch (info->fix.visual) {
	case FB_VISUAL_PSEUDOCOLOR:
		if (regno > 255)
			return 1;
		rgb = ((red & 0xff00) << 8) | (green & 0xff00) |
			((blue & 0xff00) >> 8);

		pal[regno] = rgb;
		ep93xxfb_writel(fbi, rgb, (EP93XXFB_COLOR_LUT + (regno << 2)));
		ctrl = ep93xxfb_readl(fbi, EP93XXFB_LUT_SW_CONTROL);
		lut_stat = !!(ctrl & EP93XXFB_LUT_SW_CONTROL_SSTAT);
		lut_current = !!(ctrl & EP93XXFB_LUT_SW_CONTROL_SWTCH);

		if (lut_stat == lut_current) {
			for (i = 0; i < 256; i++) {
				ep93xxfb_writel(fbi, pal[i],
					EP93XXFB_COLOR_LUT + (i << 2));
			}

			ep93xxfb_writel(fbi,
					ctrl ^ EP93XXFB_LUT_SW_CONTROL_SWTCH,
					EP93XXFB_LUT_SW_CONTROL);
		}
		break;

	case FB_VISUAL_TRUECOLOR:
		if (regno > 16)
			return 1;

		red = ep93xxfb_convert_color(red, info->var.red.length);
		green = ep93xxfb_convert_color(green, info->var.green.length);
		blue = ep93xxfb_convert_color(blue, info->var.blue.length);
		transp = ep93xxfb_convert_color(transp,
						info->var.transp.length);

		pal[regno] = (red << info->var.red.offset) |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset) |
			(transp << info->var.transp.offset);
		break;

	default:
		return 1;
	}

	return 0;
}

static struct fb_ops ep93xxfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= ep93xxfb_check_var,
	.fb_set_par	= ep93xxfb_set_par,
	.fb_blank	= ep93xxfb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_setcolreg	= ep93xxfb_setcolreg,
	.fb_mmap	= ep93xxfb_mmap,
};

static int __init ep93xxfb_calc_fbsize(struct ep93xxfb_mach_info *mach_info)
{
	int i, fb_size = 0;

	if (mach_info->num_modes == EP93XXFB_USE_MODEDB) {
		fb_size = EP93XXFB_MAX_XRES * EP93XXFB_MAX_YRES *
			mach_info->bpp / 8;
	} else {
		for (i = 0; i < mach_info->num_modes; i++) {
			const struct fb_videomode *mode;
			int size;

			mode = &mach_info->modes[i];
			size = mode->xres * mode->yres * mach_info->bpp / 8;
			if (size > fb_size)
				fb_size = size;
		}
	}

	return fb_size;
}

static int __init ep93xxfb_alloc_videomem(struct fb_info *info)
{
	struct ep93xx_fbi *fbi = info->par;
	char __iomem *virt_addr;
	dma_addr_t phys_addr;
	unsigned int fb_size;

	fb_size = ep93xxfb_calc_fbsize(fbi->mach_info);
	virt_addr = dma_alloc_writecombine(info->dev, fb_size,
					   &phys_addr, GFP_KERNEL);
	if (!virt_addr)
		return -ENOMEM;

	/*
	 * There is a bug in the ep93xx framebuffer which causes problems
	 * if bit 27 of the physical address is set.
	 * See: http://marc.info/?l=linux-arm-kernel&m=110061245502000&w=2
	 * There does not seem to be any official errata for this, but I
	 * have confirmed the problem exists on my hardware (ep9315) at
	 * least.
	 */
	if (check_screenpage_bug && phys_addr & (1 << 27)) {
		dev_err(info->dev, "ep93xx framebuffer bug. phys addr (0x%x) "
			"has bit 27 set: cannot init framebuffer\n",
			phys_addr);

		dma_free_coherent(info->dev, fb_size, virt_addr, phys_addr);
		return -ENOMEM;
	}

	info->fix.smem_start = phys_addr;
	info->fix.smem_len = fb_size;
	info->screen_base = virt_addr;

	return 0;
}

static void ep93xxfb_dealloc_videomem(struct fb_info *info)
{
	if (info->screen_base)
		dma_free_coherent(info->dev, info->fix.smem_len,
				  info->screen_base, info->fix.smem_start);
}

static int __devinit ep93xxfb_probe(struct platform_device *pdev)
{
	struct ep93xxfb_mach_info *mach_info = pdev->dev.platform_data;
	struct fb_info *info;
	struct ep93xx_fbi *fbi;
	struct resource *res;
	char *video_mode;
	int err;

	if (!mach_info)
		return -EINVAL;

	info = framebuffer_alloc(sizeof(struct ep93xx_fbi), &pdev->dev);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	fbi = info->par;
	fbi->mach_info = mach_info;

	err = fb_alloc_cmap(&info->cmap, 256, 0);
	if (err)
		goto failed;

	err = ep93xxfb_alloc_videomem(info);
	if (err)
		goto failed;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENXIO;
		goto failed;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		err = -EBUSY;
		goto failed;
	}

	fbi->res = res;
	fbi->mmio_base = ioremap(res->start, resource_size(res));
	if (!fbi->mmio_base) {
		err = -ENXIO;
		goto failed;
	}

	strcpy(info->fix.id, pdev->name);
	info->fbops		= &ep93xxfb_ops;
	info->fix.type		= FB_TYPE_PACKED_PIXELS;
	info->fix.accel		= FB_ACCEL_NONE;
	info->var.activate	= FB_ACTIVATE_NOW;
	info->var.vmode		= FB_VMODE_NONINTERLACED;
	info->flags		= FBINFO_DEFAULT;
	info->node		= -1;
	info->state		= FBINFO_STATE_RUNNING;
	info->pseudo_palette	= &fbi->pseudo_palette;

	fb_get_options("ep93xx-fb", &video_mode);
	err = fb_find_mode(&info->var, info, video_mode,
			   fbi->mach_info->modes, fbi->mach_info->num_modes,
			   fbi->mach_info->default_mode, fbi->mach_info->bpp);
	if (err == 0) {
		dev_err(info->dev, "No suitable video mode found\n");
		err = -EINVAL;
		goto failed;
	}

	if (mach_info->setup) {
		err = mach_info->setup(pdev);
		if (err)
			return err;
	}

	err = ep93xxfb_check_var(&info->var, info);
	if (err)
		goto failed;

	fbi->clk = clk_get(info->dev, NULL);
	if (IS_ERR(fbi->clk)) {
		err = PTR_ERR(fbi->clk);
		fbi->clk = NULL;
		goto failed;
	}

	ep93xxfb_set_par(info);
	clk_enable(fbi->clk);

	err = register_framebuffer(info);
	if (err)
		goto failed;

	dev_info(info->dev, "registered. Mode = %dx%d-%d\n",
		 info->var.xres, info->var.yres, info->var.bits_per_pixel);
	return 0;

failed:
	if (fbi->clk)
		clk_put(fbi->clk);
	if (fbi->mmio_base)
		iounmap(fbi->mmio_base);
	if (fbi->res)
		release_mem_region(fbi->res->start, resource_size(fbi->res));
	ep93xxfb_dealloc_videomem(info);
	if (&info->cmap)
		fb_dealloc_cmap(&info->cmap);
	if (fbi->mach_info->teardown)
		fbi->mach_info->teardown(pdev);
	kfree(info);
	platform_set_drvdata(pdev, NULL);

	return err;
}

static int __devexit ep93xxfb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct ep93xx_fbi *fbi = info->par;

	unregister_framebuffer(info);
	clk_disable(fbi->clk);
	clk_put(fbi->clk);
	iounmap(fbi->mmio_base);
	release_mem_region(fbi->res->start, resource_size(fbi->res));
	ep93xxfb_dealloc_videomem(info);
	fb_dealloc_cmap(&info->cmap);

	if (fbi->mach_info->teardown)
		fbi->mach_info->teardown(pdev);

	kfree(info);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ep93xxfb_driver = {
	.probe		= ep93xxfb_probe,
	.remove		= __devexit_p(ep93xxfb_remove),
	.driver = {
		.name	= "ep93xx-fb",
		.owner	= THIS_MODULE,
	},
};

static int __devinit ep93xxfb_init(void)
{
	return platform_driver_register(&ep93xxfb_driver);
}

static void __exit ep93xxfb_exit(void)
{
	platform_driver_unregister(&ep93xxfb_driver);
}

module_init(ep93xxfb_init);
module_exit(ep93xxfb_exit);

MODULE_DESCRIPTION("EP93XX Framebuffer Driver");
MODULE_ALIAS("platform:ep93xx-fb");
MODULE_AUTHOR("Ryan Mallon, "
	      "H Hartley Sweeten <hsweeten@visionengravers.com");
MODULE_LICENSE("GPL");
