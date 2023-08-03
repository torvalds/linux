// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/drivers/video/mmp/fb/mmpfb.c
 * Framebuffer driver for Marvell Display controller.
 *
 * Copyright (C) 2012 Marvell Technology Group Ltd.
 * Authors: Zhou Zhu <zzhu3@marvell.com>
 */
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include "mmpfb.h"

static int var_to_pixfmt(struct fb_var_screeninfo *var)
{
	/*
	 * Pseudocolor mode?
	 */
	if (var->bits_per_pixel == 8)
		return PIXFMT_PSEUDOCOLOR;

	/*
	 * Check for YUV422PLANAR.
	 */
	if (var->bits_per_pixel == 16 && var->red.length == 8 &&
			var->green.length == 4 && var->blue.length == 4) {
		if (var->green.offset >= var->blue.offset)
			return PIXFMT_YUV422P;
		else
			return PIXFMT_YVU422P;
	}

	/*
	 * Check for YUV420PLANAR.
	 */
	if (var->bits_per_pixel == 12 && var->red.length == 8 &&
			var->green.length == 2 && var->blue.length == 2) {
		if (var->green.offset >= var->blue.offset)
			return PIXFMT_YUV420P;
		else
			return PIXFMT_YVU420P;
	}

	/*
	 * Check for YUV422PACK.
	 */
	if (var->bits_per_pixel == 16 && var->red.length == 16 &&
			var->green.length == 16 && var->blue.length == 16) {
		if (var->red.offset == 0)
			return PIXFMT_YUYV;
		else if (var->green.offset >= var->blue.offset)
			return PIXFMT_UYVY;
		else
			return PIXFMT_VYUY;
	}

	/*
	 * Check for 565/1555.
	 */
	if (var->bits_per_pixel == 16 && var->red.length <= 5 &&
			var->green.length <= 6 && var->blue.length <= 5) {
		if (var->transp.length == 0) {
			if (var->red.offset >= var->blue.offset)
				return PIXFMT_RGB565;
			else
				return PIXFMT_BGR565;
		}
	}

	/*
	 * Check for 888/A888.
	 */
	if (var->bits_per_pixel <= 32 && var->red.length <= 8 &&
			var->green.length <= 8 && var->blue.length <= 8) {
		if (var->bits_per_pixel == 24 && var->transp.length == 0) {
			if (var->red.offset >= var->blue.offset)
				return PIXFMT_RGB888PACK;
			else
				return PIXFMT_BGR888PACK;
		}

		if (var->bits_per_pixel == 32 && var->transp.offset == 24) {
			if (var->red.offset >= var->blue.offset)
				return PIXFMT_RGBA888;
			else
				return PIXFMT_BGRA888;
		} else {
			if (var->red.offset >= var->blue.offset)
				return PIXFMT_RGB888UNPACK;
			else
				return PIXFMT_BGR888UNPACK;
		}
	}

	return -EINVAL;
}

static void pixfmt_to_var(struct fb_var_screeninfo *var, int pix_fmt)
{
	switch (pix_fmt) {
	case PIXFMT_RGB565:
		var->bits_per_pixel = 16;
		var->red.offset = 11;	var->red.length = 5;
		var->green.offset = 5;   var->green.length = 6;
		var->blue.offset = 0;	var->blue.length = 5;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_BGR565:
		var->bits_per_pixel = 16;
		var->red.offset = 0;	var->red.length = 5;
		var->green.offset = 5;	 var->green.length = 6;
		var->blue.offset = 11;	var->blue.length = 5;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_RGB888UNPACK:
		var->bits_per_pixel = 32;
		var->red.offset = 16;	var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;	var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_BGR888UNPACK:
		var->bits_per_pixel = 32;
		var->red.offset = 0;	var->red.length = 8;
		var->green.offset = 8;	 var->green.length = 8;
		var->blue.offset = 16;	var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_RGBA888:
		var->bits_per_pixel = 32;
		var->red.offset = 16;	var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;	var->blue.length = 8;
		var->transp.offset = 24; var->transp.length = 8;
		break;
	case PIXFMT_BGRA888:
		var->bits_per_pixel = 32;
		var->red.offset = 0;	var->red.length = 8;
		var->green.offset = 8;	 var->green.length = 8;
		var->blue.offset = 16;	var->blue.length = 8;
		var->transp.offset = 24; var->transp.length = 8;
		break;
	case PIXFMT_RGB888PACK:
		var->bits_per_pixel = 24;
		var->red.offset = 16;	var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;	var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_BGR888PACK:
		var->bits_per_pixel = 24;
		var->red.offset = 0;	var->red.length = 8;
		var->green.offset = 8;	 var->green.length = 8;
		var->blue.offset = 16;	var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_YUV420P:
		var->bits_per_pixel = 12;
		var->red.offset = 4;	 var->red.length = 8;
		var->green.offset = 2;   var->green.length = 2;
		var->blue.offset = 0;   var->blue.length = 2;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_YVU420P:
		var->bits_per_pixel = 12;
		var->red.offset = 4;	 var->red.length = 8;
		var->green.offset = 0;	 var->green.length = 2;
		var->blue.offset = 2;	var->blue.length = 2;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_YUV422P:
		var->bits_per_pixel = 16;
		var->red.offset = 8;	 var->red.length = 8;
		var->green.offset = 4;   var->green.length = 4;
		var->blue.offset = 0;   var->blue.length = 4;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_YVU422P:
		var->bits_per_pixel = 16;
		var->red.offset = 8;	 var->red.length = 8;
		var->green.offset = 0;	 var->green.length = 4;
		var->blue.offset = 4;	var->blue.length = 4;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_UYVY:
		var->bits_per_pixel = 16;
		var->red.offset = 8;	 var->red.length = 16;
		var->green.offset = 4;   var->green.length = 16;
		var->blue.offset = 0;   var->blue.length = 16;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_VYUY:
		var->bits_per_pixel = 16;
		var->red.offset = 8;	 var->red.length = 16;
		var->green.offset = 0;	 var->green.length = 16;
		var->blue.offset = 4;	var->blue.length = 16;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_YUYV:
		var->bits_per_pixel = 16;
		var->red.offset = 0;	 var->red.length = 16;
		var->green.offset = 4;	 var->green.length = 16;
		var->blue.offset = 8;	var->blue.length = 16;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIXFMT_PSEUDOCOLOR:
		var->bits_per_pixel = 8;
		var->red.offset = 0;	 var->red.length = 8;
		var->green.offset = 0;   var->green.length = 8;
		var->blue.offset = 0;	var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	}
}

/*
 * fb framework has its limitation:
 * 1. input color/output color is not seprated
 * 2. fb_videomode not include output color
 * so for fb usage, we keep a output format which is not changed
 *  then it's added for mmpmode
 */
static void fbmode_to_mmpmode(struct mmp_mode *mode,
		struct fb_videomode *videomode, int output_fmt)
{
	u64 div_result = 1000000000000ll;
	mode->name = videomode->name;
	mode->refresh = videomode->refresh;
	mode->xres = videomode->xres;
	mode->yres = videomode->yres;

	do_div(div_result, videomode->pixclock);
	mode->pixclock_freq = (u32)div_result;

	mode->left_margin = videomode->left_margin;
	mode->right_margin = videomode->right_margin;
	mode->upper_margin = videomode->upper_margin;
	mode->lower_margin = videomode->lower_margin;
	mode->hsync_len = videomode->hsync_len;
	mode->vsync_len = videomode->vsync_len;
	mode->hsync_invert = !!(videomode->sync & FB_SYNC_HOR_HIGH_ACT);
	mode->vsync_invert = !!(videomode->sync & FB_SYNC_VERT_HIGH_ACT);
	/* no defined flag in fb, use vmode>>3*/
	mode->invert_pixclock = !!(videomode->vmode & 8);
	mode->pix_fmt_out = output_fmt;
}

static void mmpmode_to_fbmode(struct fb_videomode *videomode,
		struct mmp_mode *mode)
{
	u64 div_result = 1000000000000ll;

	videomode->name = mode->name;
	videomode->refresh = mode->refresh;
	videomode->xres = mode->xres;
	videomode->yres = mode->yres;

	do_div(div_result, mode->pixclock_freq);
	videomode->pixclock = (u32)div_result;

	videomode->left_margin = mode->left_margin;
	videomode->right_margin = mode->right_margin;
	videomode->upper_margin = mode->upper_margin;
	videomode->lower_margin = mode->lower_margin;
	videomode->hsync_len = mode->hsync_len;
	videomode->vsync_len = mode->vsync_len;
	videomode->sync = (mode->hsync_invert ? FB_SYNC_HOR_HIGH_ACT : 0)
		| (mode->vsync_invert ? FB_SYNC_VERT_HIGH_ACT : 0);
	videomode->vmode = mode->invert_pixclock ? 8 : 0;
}

static int mmpfb_check_var(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	struct mmpfb_info *fbi = info->par;

	if (var->bits_per_pixel == 8)
		return -EINVAL;
	/*
	 * Basic geometry sanity checks.
	 */
	if (var->xoffset + var->xres > var->xres_virtual)
		return -EINVAL;
	if (var->yoffset + var->yres > var->yres_virtual)
		return -EINVAL;

	/*
	 * Check size of framebuffer.
	 */
	if (var->xres_virtual * var->yres_virtual *
			(var->bits_per_pixel >> 3) > fbi->fb_size)
		return -EINVAL;

	return 0;
}

static unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
	return ((chan & 0xffff) >> (16 - bf->length)) << bf->offset;
}

static u32 to_rgb(u16 red, u16 green, u16 blue)
{
	red >>= 8;
	green >>= 8;
	blue >>= 8;

	return (red << 16) | (green << 8) | blue;
}

static int mmpfb_setcolreg(unsigned int regno, unsigned int red,
		unsigned int green, unsigned int blue,
		unsigned int trans, struct fb_info *info)
{
	struct mmpfb_info *fbi = info->par;
	u32 val;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR && regno < 16) {
		val =  chan_to_field(red,   &info->var.red);
		val |= chan_to_field(green, &info->var.green);
		val |= chan_to_field(blue , &info->var.blue);
		fbi->pseudo_palette[regno] = val;
	}

	if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR && regno < 256) {
		val = to_rgb(red, green, blue);
		/* TODO */
	}

	return 0;
}

static int mmpfb_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	struct mmpfb_info *fbi = info->par;
	struct mmp_addr addr;

	memset(&addr, 0, sizeof(addr));
	addr.phys[0] = (var->yoffset * var->xres_virtual + var->xoffset)
		* var->bits_per_pixel / 8 + fbi->fb_start_dma;
	mmp_overlay_set_addr(fbi->overlay, &addr);

	return 0;
}

static int var_update(struct fb_info *info)
{
	struct mmpfb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	struct fb_videomode *m;
	int pix_fmt;

	/* set pix_fmt */
	pix_fmt = var_to_pixfmt(var);
	if (pix_fmt < 0)
		return -EINVAL;
	pixfmt_to_var(var, pix_fmt);
	fbi->pix_fmt = pix_fmt;

	/* set var according to best video mode*/
	m = (struct fb_videomode *)fb_match_mode(var, &info->modelist);
	if (!m) {
		dev_err(fbi->dev, "set par: no match mode, use best mode\n");
		m = (struct fb_videomode *)fb_find_best_mode(var,
				&info->modelist);
		fb_videomode_to_var(var, m);
	}
	memcpy(&fbi->mode, m, sizeof(struct fb_videomode));

	/* fix to 2* yres */
	var->yres_virtual = var->yres * 2;
	info->fix.visual = (pix_fmt == PIXFMT_PSEUDOCOLOR) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;
	info->fix.ypanstep = var->yres;
	return 0;
}

static void mmpfb_set_win(struct fb_info *info)
{
	struct mmpfb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	struct mmp_win win;
	u32 stride;

	memset(&win, 0, sizeof(win));
	win.xsrc = win.xdst = fbi->mode.xres;
	win.ysrc = win.ydst = fbi->mode.yres;
	win.pix_fmt = fbi->pix_fmt;
	stride = pixfmt_to_stride(win.pix_fmt);
	win.pitch[0] = var->xres_virtual * stride;
	win.pitch[1] = win.pitch[2] =
		(stride == 1) ? (var->xres_virtual >> 1) : 0;
	mmp_overlay_set_win(fbi->overlay, &win);
}

static int mmpfb_set_par(struct fb_info *info)
{
	struct mmpfb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	struct mmp_addr addr;
	struct mmp_mode mode;
	int ret;

	ret = var_update(info);
	if (ret != 0)
		return ret;

	/* set window/path according to new videomode */
	fbmode_to_mmpmode(&mode, &fbi->mode, fbi->output_fmt);
	mmp_path_set_mode(fbi->path, &mode);

	/* set window related info */
	mmpfb_set_win(info);

	/* set address always */
	memset(&addr, 0, sizeof(addr));
	addr.phys[0] = (var->yoffset * var->xres_virtual + var->xoffset)
		* var->bits_per_pixel / 8 + fbi->fb_start_dma;
	mmp_overlay_set_addr(fbi->overlay, &addr);

	return 0;
}

static void mmpfb_power(struct mmpfb_info *fbi, int power)
{
	struct mmp_addr addr;
	struct fb_var_screeninfo *var = &fbi->fb_info->var;

	/* for power on, always set address/window again */
	if (power) {
		/* set window related info */
		mmpfb_set_win(fbi->fb_info);

		/* set address always */
		memset(&addr, 0, sizeof(addr));
		addr.phys[0] = fbi->fb_start_dma +
			(var->yoffset * var->xres_virtual + var->xoffset)
			* var->bits_per_pixel / 8;
		mmp_overlay_set_addr(fbi->overlay, &addr);
	}
	mmp_overlay_set_onoff(fbi->overlay, power);
}

static int mmpfb_blank(int blank, struct fb_info *info)
{
	struct mmpfb_info *fbi = info->par;

	mmpfb_power(fbi, (blank == FB_BLANK_UNBLANK));

	return 0;
}

static const struct fb_ops mmpfb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_IOMEM_OPS,
	.fb_blank	= mmpfb_blank,
	.fb_check_var	= mmpfb_check_var,
	.fb_set_par	= mmpfb_set_par,
	.fb_setcolreg	= mmpfb_setcolreg,
	.fb_pan_display	= mmpfb_pan_display,
};

static int modes_setup(struct mmpfb_info *fbi)
{
	struct fb_videomode *videomodes;
	struct mmp_mode *mmp_modes;
	struct fb_info *info = fbi->fb_info;
	int videomode_num, i;

	/* get videomodes from path */
	videomode_num = mmp_path_get_modelist(fbi->path, &mmp_modes);
	if (!videomode_num) {
		dev_warn(fbi->dev, "can't get videomode num\n");
		return 0;
	}
	/* put videomode list to info structure */
	videomodes = kcalloc(videomode_num, sizeof(struct fb_videomode),
			     GFP_KERNEL);
	if (!videomodes)
		return -ENOMEM;

	for (i = 0; i < videomode_num; i++)
		mmpmode_to_fbmode(&videomodes[i], &mmp_modes[i]);
	fb_videomode_to_modelist(videomodes, videomode_num, &info->modelist);

	/* set videomode[0] as default mode */
	memcpy(&fbi->mode, &videomodes[0], sizeof(struct fb_videomode));
	fbi->output_fmt = mmp_modes[0].pix_fmt_out;
	fb_videomode_to_var(&info->var, &fbi->mode);
	mmp_path_set_mode(fbi->path, &mmp_modes[0]);

	kfree(videomodes);
	return videomode_num;
}

static int fb_info_setup(struct fb_info *info,
			struct mmpfb_info *fbi)
{
	int ret = 0;
	/* Initialise static fb parameters.*/
	info->flags = FBINFO_PARTIAL_PAN_OK |
		FBINFO_HWACCEL_XPAN | FBINFO_HWACCEL_YPAN;
	info->node = -1;
	strcpy(info->fix.id, fbi->name);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = info->var.yres;
	info->fix.ywrapstep = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->fix.smem_start = fbi->fb_start_dma;
	info->fix.smem_len = fbi->fb_size;
	info->fix.visual = (fbi->pix_fmt == PIXFMT_PSEUDOCOLOR) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	info->fix.line_length = info->var.xres_virtual *
		info->var.bits_per_pixel / 8;
	info->fbops = &mmpfb_ops;
	info->pseudo_palette = fbi->pseudo_palette;
	info->screen_buffer = fbi->fb_start;
	info->screen_size = fbi->fb_size;

	/* For FB framework: Allocate color map and Register framebuffer*/
	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0)
		ret = -ENOMEM;

	return ret;
}

static void fb_info_clear(struct fb_info *info)
{
	fb_dealloc_cmap(&info->cmap);
}

static int mmpfb_probe(struct platform_device *pdev)
{
	struct mmp_buffer_driver_mach_info *mi;
	struct fb_info *info;
	struct mmpfb_info *fbi;
	int ret, modes_num;

	mi = pdev->dev.platform_data;
	if (mi == NULL) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	/* initialize fb */
	info = framebuffer_alloc(sizeof(struct mmpfb_info), &pdev->dev);
	if (info == NULL)
		return -ENOMEM;
	fbi = info->par;

	/* init fb */
	fbi->fb_info = info;
	platform_set_drvdata(pdev, fbi);
	fbi->dev = &pdev->dev;
	fbi->name = mi->name;
	fbi->pix_fmt = mi->default_pixfmt;
	pixfmt_to_var(&info->var, fbi->pix_fmt);
	mutex_init(&fbi->access_ok);

	/* get display path by name */
	fbi->path = mmp_get_path(mi->path_name);
	if (!fbi->path) {
		dev_err(&pdev->dev, "can't get the path %s\n", mi->path_name);
		ret = -EINVAL;
		goto failed_destroy_mutex;
	}

	dev_info(fbi->dev, "path %s get\n", fbi->path->name);

	/* get overlay */
	fbi->overlay = mmp_path_get_overlay(fbi->path, mi->overlay_id);
	if (!fbi->overlay) {
		ret = -EINVAL;
		goto failed_destroy_mutex;
	}
	/* set fetch used */
	mmp_overlay_set_fetch(fbi->overlay, mi->dmafetch_id);

	modes_num = modes_setup(fbi);
	if (modes_num < 0) {
		ret = modes_num;
		goto failed_destroy_mutex;
	}

	/*
	 * if get modes success, means not hotplug panels, use caculated buffer
	 * or use default size
	 */
	if (modes_num > 0) {
		/* fix to 2* yres */
		info->var.yres_virtual = info->var.yres * 2;

		/* Allocate framebuffer memory: size = modes xy *4 */
		fbi->fb_size = info->var.xres_virtual * info->var.yres_virtual
				* info->var.bits_per_pixel / 8;
	} else {
		fbi->fb_size = MMPFB_DEFAULT_SIZE;
	}

	fbi->fb_start = dma_alloc_coherent(&pdev->dev, PAGE_ALIGN(fbi->fb_size),
				&fbi->fb_start_dma, GFP_KERNEL);
	if (fbi->fb_start == NULL) {
		dev_err(&pdev->dev, "can't alloc framebuffer\n");
		ret = -ENOMEM;
		goto failed_destroy_mutex;
	}
	dev_info(fbi->dev, "fb %dk allocated\n", fbi->fb_size/1024);

	/* fb power on */
	if (modes_num > 0)
		mmpfb_power(fbi, 1);

	ret = fb_info_setup(info, fbi);
	if (ret < 0)
		goto failed_free_buff;

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register fb: %d\n", ret);
		ret = -ENXIO;
		goto failed_clear_info;
	}

	dev_info(fbi->dev, "loaded to /dev/fb%d <%s>.\n",
		info->node, info->fix.id);

#ifdef CONFIG_LOGO
	if (fbi->fb_start) {
		fb_prepare_logo(info, 0);
		fb_show_logo(info, 0);
	}
#endif

	return 0;

failed_clear_info:
	fb_info_clear(info);
failed_free_buff:
	dma_free_coherent(&pdev->dev, PAGE_ALIGN(fbi->fb_size), fbi->fb_start,
		fbi->fb_start_dma);
failed_destroy_mutex:
	mutex_destroy(&fbi->access_ok);
	dev_err(fbi->dev, "mmp-fb: frame buffer device init failed\n");

	framebuffer_release(info);

	return ret;
}

static struct platform_driver mmpfb_driver = {
	.driver		= {
		.name	= "mmp-fb",
	},
	.probe		= mmpfb_probe,
};

static int mmpfb_init(void)
{
	return platform_driver_register(&mmpfb_driver);
}
module_init(mmpfb_init);

MODULE_AUTHOR("Zhou Zhu <zhou.zhu@marvell.com>");
MODULE_DESCRIPTION("Framebuffer driver for Marvell displays");
MODULE_LICENSE("GPL");
