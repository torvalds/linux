/*
 *  linux/drivers/video/68328fb.c -- Low level implementation of the
 *                                   mc68x328 LCD frame buffer device
 *
 *	Copyright (C) 2003 Georges Menie
 *
 *  This driver assumes an already configured controller (e.g. from config.c)
 *  Keep the code clean of board specific initialization.
 *
 *  This code has not been tested with colors, colormap management functions
 *  are minimal (no colormap data written to the 68328 registers...)
 *
 *  initial version of this driver:
 *    Copyright (C) 1998,1999 Kenneth Albanowski <kjahds@kjahds.com>,
 *                            The Silver Hammer Group, Ltd.
 *
 *  this version is based on :
 *
 *  linux/drivers/video/vfb.c -- Virtual frame buffer device
 *
 *      Copyright (C) 2002 James Simmons
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>

#if defined(CONFIG_M68VZ328)
#include <asm/MC68VZ328.h>
#elif defined(CONFIG_M68EZ328)
#include <asm/MC68EZ328.h>
#elif defined(CONFIG_M68328)
#include <asm/MC68328.h>
#else
#error wrong architecture for the MC68x328 frame buffer device
#endif

#if defined(CONFIG_FB_68328_INVERT)
#define MC68X328FB_MONO_VISUAL FB_VISUAL_MONO01
#else
#define MC68X328FB_MONO_VISUAL FB_VISUAL_MONO10
#endif

static u_long videomemory;
static u_long videomemorysize;

static struct fb_info fb_info;
static u32 mc68x328fb_pseudo_palette[17];

static struct fb_var_screeninfo mc68x328fb_default __initdata = {
	.red =		{ 0, 8, 0 },
      	.green =	{ 0, 8, 0 },
      	.blue =		{ 0, 8, 0 },
      	.activate =	FB_ACTIVATE_TEST,
      	.height =	-1,
      	.width =	-1,
      	.pixclock =	20000,
      	.left_margin =	64,
      	.right_margin =	64,
      	.upper_margin =	32,
      	.lower_margin =	32,
      	.hsync_len =	64,
      	.vsync_len =	2,
      	.vmode =	FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo mc68x328fb_fix __initdata = {
	.id =		"68328fb",
	.type =		FB_TYPE_PACKED_PIXELS,
	.xpanstep =	1,
	.ypanstep =	1,
	.ywrapstep =	1,
	.accel =	FB_ACCEL_NONE,
};

    /*
     *  Interface used by the world
     */
int mc68x328fb_init(void);
int mc68x328fb_setup(char *);

static int mc68x328fb_check_var(struct fb_var_screeninfo *var,
			 struct fb_info *info);
static int mc68x328fb_set_par(struct fb_info *info);
static int mc68x328fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info);
static int mc68x328fb_pan_display(struct fb_var_screeninfo *var,
			   struct fb_info *info);
static int mc68x328fb_mmap(struct fb_info *info, struct file *file,
		    struct vm_area_struct *vma);

static struct fb_ops mc68x328fb_ops = {
	.fb_check_var	= mc68x328fb_check_var,
	.fb_set_par	= mc68x328fb_set_par,
	.fb_setcolreg	= mc68x328fb_setcolreg,
	.fb_pan_display	= mc68x328fb_pan_display,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_mmap	= mc68x328fb_mmap,
};

    /*
     *  Internal routines
     */

static u_long get_line_length(int xres_virtual, int bpp)
{
	u_long length;

	length = xres_virtual * bpp;
	length = (length + 31) & ~31;
	length >>= 3;
	return (length);
}

    /*
     *  Setting the video mode has been split into two parts.
     *  First part, xxxfb_check_var, must not write anything
     *  to hardware, it should only verify and adjust var.
     *  This means it doesn't alter par but it does use hardware
     *  data from it to check this var. 
     */

static int mc68x328fb_check_var(struct fb_var_screeninfo *var,
			 struct fb_info *info)
{
	u_long line_length;

	/*
	 *  FB_VMODE_CONUPDATE and FB_VMODE_SMOOTH_XPAN are equal!
	 *  as FB_VMODE_SMOOTH_XPAN is only used internally
	 */

	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->vmode |= FB_VMODE_YWRAP;
		var->xoffset = info->var.xoffset;
		var->yoffset = info->var.yoffset;
	}

	/*
	 *  Some very basic checks
	 */
	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;
	if (var->bits_per_pixel <= 1)
		var->bits_per_pixel = 1;
	else if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 24)
		var->bits_per_pixel = 24;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;
	else
		return -EINVAL;

	if (var->xres_virtual < var->xoffset + var->xres)
		var->xres_virtual = var->xoffset + var->xres;
	if (var->yres_virtual < var->yoffset + var->yres)
		var->yres_virtual = var->yoffset + var->yres;

	/*
	 *  Memory limit
	 */
	line_length =
	    get_line_length(var->xres_virtual, var->bits_per_pixel);
	if (line_length * var->yres_virtual > videomemorysize)
		return -ENOMEM;

	/*
	 * Now that we checked it we alter var. The reason being is that the video
	 * mode passed in might not work but slight changes to it might make it 
	 * work. This way we let the user know what is acceptable.
	 */
	switch (var->bits_per_pixel) {
	case 1:
		var->red.offset = 0;
		var->red.length = 1;
		var->green.offset = 0;
		var->green.length = 1;
		var->blue.offset = 0;
		var->blue.length = 1;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 16:		/* RGBA 5551 */
		if (var->transp.length) {
			var->red.offset = 0;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 5;
			var->blue.offset = 10;
			var->blue.length = 5;
			var->transp.offset = 15;
			var->transp.length = 1;
		} else {	/* RGB 565 */
			var->red.offset = 0;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset = 11;
			var->blue.length = 5;
			var->transp.offset = 0;
			var->transp.length = 0;
		}
		break;
	case 24:		/* RGB 888 */
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32:		/* RGBA 8888 */
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	return 0;
}

/* This routine actually sets the video mode. It's in here where we
 * the hardware state info->par and fix which can be affected by the 
 * change in par. For this driver it doesn't do much. 
 */
static int mc68x328fb_set_par(struct fb_info *info)
{
	info->fix.line_length = get_line_length(info->var.xres_virtual,
						info->var.bits_per_pixel);
	return 0;
}

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int mc68x328fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
	if (regno >= 256)	/* no. of hw registers */
		return 1;
	/*
	 * Program hardware... do anything you want with transp
	 */

	/* grayscale works only partially under directcolor */
	if (info->var.grayscale) {
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue =
		    (red * 77 + green * 151 + blue * 28) >> 8;
	}

	/* Directcolor:
	 *   var->{color}.offset contains start of bitfield
	 *   var->{color}.length contains length of bitfield
	 *   {hardwarespecific} contains width of RAMDAC
	 *   cmap[X] is programmed to (X << red.offset) | (X << green.offset) | (X << blue.offset)
	 *   RAMDAC[X] is programmed to (red, green, blue)
	 * 
	 * Pseudocolor:
	 *    uses offset = 0 && length = RAMDAC register width.
	 *    var->{color}.offset is 0
	 *    var->{color}.length contains widht of DAC
	 *    cmap is not used
	 *    RAMDAC[X] is programmed to (red, green, blue)
	 * Truecolor:
	 *    does not use DAC. Usually 3 are present.
	 *    var->{color}.offset contains start of bitfield
	 *    var->{color}.length contains length of bitfield
	 *    cmap is programmed to (red << red.offset) | (green << green.offset) |
	 *                      (blue << blue.offset) | (transp << transp.offset)
	 *    RAMDAC does not exist
	 */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		red = CNVT_TOHW(red, info->var.red.length);
		green = CNVT_TOHW(green, info->var.green.length);
		blue = CNVT_TOHW(blue, info->var.blue.length);
		transp = CNVT_TOHW(transp, info->var.transp.length);
		break;
	case FB_VISUAL_DIRECTCOLOR:
		red = CNVT_TOHW(red, 8);	/* expect 8 bit DAC */
		green = CNVT_TOHW(green, 8);
		blue = CNVT_TOHW(blue, 8);
		/* hey, there is bug in transp handling... */
		transp = CNVT_TOHW(transp, 8);
		break;
	}
#undef CNVT_TOHW
	/* Truecolor has hardware independent palette */
	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 v;

		if (regno >= 16)
			return 1;

		v = (red << info->var.red.offset) |
		    (green << info->var.green.offset) |
		    (blue << info->var.blue.offset) |
		    (transp << info->var.transp.offset);
		switch (info->var.bits_per_pixel) {
		case 8:
			break;
		case 16:
			((u32 *) (info->pseudo_palette))[regno] = v;
			break;
		case 24:
		case 32:
			((u32 *) (info->pseudo_palette))[regno] = v;
			break;
		}
		return 0;
	}
	return 0;
}

    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int mc68x328fb_pan_display(struct fb_var_screeninfo *var,
			   struct fb_info *info)
{
	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0
		    || var->yoffset >= info->var.yres_virtual
		    || var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset + var->xres > info->var.xres_virtual ||
		    var->yoffset + var->yres > info->var.yres_virtual)
			return -EINVAL;
	}
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

    /*
     *  Most drivers don't need their own mmap function 
     */

static int mc68x328fb_mmap(struct fb_info *info, struct file *file,
		    struct vm_area_struct *vma)
{
#ifndef MMU
	/* this is uClinux (no MMU) specific code */

	vma->vm_flags |= VM_RESERVED;
	vma->vm_start = videomemory;

	return 0;
#else
	return -EINVAL;
#endif
}

int __init mc68x328fb_setup(char *options)
{
#if 0
	char *this_opt;
#endif

	if (!options || !*options)
		return 1;
#if 0
	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strncmp(this_opt, "disable", 7))
			mc68x328fb_enable = 0;
	}
#endif
	return 1;
}

    /*
     *  Initialisation
     */

int __init mc68x328fb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("68328fb", &option))
		return -ENODEV;
	mc68x328fb_setup(option);
#endif
	/*
	 *  initialize the default mode from the LCD controller registers
	 */
	mc68x328fb_default.xres = LXMAX;
	mc68x328fb_default.yres = LYMAX+1;
	mc68x328fb_default.xres_virtual = mc68x328fb_default.xres;
	mc68x328fb_default.yres_virtual = mc68x328fb_default.yres;
	mc68x328fb_default.bits_per_pixel = 1 + (LPICF & 0x01);
	videomemory = LSSA;
	videomemorysize = (mc68x328fb_default.xres_virtual+7) / 8 *
		mc68x328fb_default.yres_virtual * mc68x328fb_default.bits_per_pixel;

	fb_info.screen_base = (void *)videomemory;
	fb_info.fbops = &mc68x328fb_ops;
	fb_info.var = mc68x328fb_default;
	fb_info.fix = mc68x328fb_fix;
	fb_info.fix.smem_start = videomemory;
	fb_info.fix.smem_len = videomemorysize;
	fb_info.fix.line_length =
		get_line_length(mc68x328fb_default.xres_virtual, mc68x328fb_default.bits_per_pixel);
	fb_info.fix.visual = (mc68x328fb_default.bits_per_pixel) == 1 ?
		MC68X328FB_MONO_VISUAL : FB_VISUAL_PSEUDOCOLOR;
	if (fb_info.var.bits_per_pixel == 1) {
		fb_info.var.red.length = fb_info.var.green.length = fb_info.var.blue.length = 1;
		fb_info.var.red.offset = fb_info.var.green.offset = fb_info.var.blue.offset = 0;
	}
	fb_info.pseudo_palette = &mc68x328fb_pseudo_palette;
	fb_info.flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;

	fb_alloc_cmap(&fb_info.cmap, 256, 0);

	if (register_framebuffer(&fb_info) < 0) {
		return -EINVAL;
	}

	printk(KERN_INFO
		"fb%d: %s frame buffer device\n", fb_info.node,	fb_info.fix.id);
	printk(KERN_INFO
		"fb%d: %dx%dx%d at 0x%08lx\n", fb_info.node,
		mc68x328fb_default.xres_virtual, mc68x328fb_default.yres_virtual,
		1 << mc68x328fb_default.bits_per_pixel, videomemory);

	return 0;
}

module_init(mc68x328fb_init);

#ifdef MODULE

static void __exit mc68x328fb_cleanup(void)
{
	unregister_framebuffer(&fb_info);
}

module_exit(mc68x328fb_cleanup);

MODULE_LICENSE("GPL");
#endif				/* MODULE */
