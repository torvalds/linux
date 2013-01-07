/*
 *  linux/drivers/video/acornfb.c
 *
 *  Copyright (C) 1998-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Frame buffer code for Acorn platforms
 *
 * NOTE: Most of the modes with X!=640 will disappear shortly.
 * NOTE: Startup setting of HS & VS polarity not supported.
 *       (do we need to support it if we're coming up in 640x480?)
 *
 * FIXME: (things broken by the "new improved" FBCON API)
 *  - Blanking 8bpp displays with VIDC
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/gfp.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/pgtable.h>

#include "acornfb.h"

/*
 * VIDC machines can't do 16 or 32BPP modes.
 */
#ifdef HAS_VIDC
#undef FBCON_HAS_CFB16
#undef FBCON_HAS_CFB32
#endif

/*
 * Default resolution.
 * NOTE that it has to be supported in the table towards
 * the end of this file.
 */
#define DEFAULT_XRES	640
#define DEFAULT_YRES	480
#define DEFAULT_BPP	4

/*
 * define this to debug the video mode selection
 */
#undef DEBUG_MODE_SELECTION

/*
 * Translation from RISC OS monitor types to actual
 * HSYNC and VSYNC frequency ranges.  These are
 * probably not right, but they're the best info I
 * have.  Allow 1% either way on the nominal for TVs.
 */
#define NR_MONTYPES	6
static struct fb_monspecs monspecs[NR_MONTYPES] = {
	{	/* TV		*/
		.hfmin	= 15469,
		.hfmax	= 15781,
		.vfmin	= 49,
		.vfmax	= 51,
	}, {	/* Multi Freq	*/
		.hfmin	= 0,
		.hfmax	= 99999,
		.vfmin	= 0,
		.vfmax	= 199,
	}, {	/* Hi-res mono	*/
		.hfmin	= 58608,
		.hfmax	= 58608,
		.vfmin	= 64,
		.vfmax	= 64,
	}, {	/* VGA		*/
		.hfmin	= 30000,
		.hfmax	= 70000,
		.vfmin	= 60,
		.vfmax	= 60,
	}, {	/* SVGA		*/
		.hfmin	= 30000,
		.hfmax	= 70000,
		.vfmin	= 56,
		.vfmax	= 75,
	}, {
		.hfmin	= 30000,
		.hfmax	= 70000,
		.vfmin	= 60,
		.vfmax	= 60,
	}
};

static struct fb_info fb_info;
static struct acornfb_par current_par;
static struct vidc_timing current_vidc;

extern unsigned int vram_size;	/* set by setup.c */

#ifdef HAS_VIDC

#define MAX_SIZE	480*1024

/* CTL     VIDC	Actual
 * 24.000  0	 8.000
 * 25.175  0	 8.392
 * 36.000  0	12.000
 * 24.000  1	12.000
 * 25.175  1	12.588
 * 24.000  2	16.000
 * 25.175  2	16.783
 * 36.000  1	18.000
 * 24.000  3	24.000
 * 36.000  2	24.000
 * 25.175  3	25.175
 * 36.000  3	36.000
 */
struct pixclock {
	u_long	min_clock;
	u_long	max_clock;
	u_int	vidc_ctl;
	u_int	vid_ctl;
};

static struct pixclock arc_clocks[] = {
	/* we allow +/-1% on these */
	{ 123750, 126250, VIDC_CTRL_DIV3,   VID_CTL_24MHz },	/*  8.000MHz */
	{  82500,  84167, VIDC_CTRL_DIV2,   VID_CTL_24MHz },	/* 12.000MHz */
	{  61875,  63125, VIDC_CTRL_DIV1_5, VID_CTL_24MHz },	/* 16.000MHz */
	{  41250,  42083, VIDC_CTRL_DIV1,   VID_CTL_24MHz },	/* 24.000MHz */
};

static struct pixclock *
acornfb_valid_pixrate(struct fb_var_screeninfo *var)
{
	u_long pixclock = var->pixclock;
	u_int i;

	if (!var->pixclock)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(arc_clocks); i++)
		if (pixclock > arc_clocks[i].min_clock &&
		    pixclock < arc_clocks[i].max_clock)
			return arc_clocks + i;

	return NULL;
}

/* VIDC Rules:
 * hcr  : must be even (interlace, hcr/2 must be even)
 * hswr : must be even
 * hdsr : must be odd
 * hder : must be odd
 *
 * vcr  : must be odd
 * vswr : >= 1
 * vdsr : >= 1
 * vder : >= vdsr
 * if interlaced, then hcr/2 must be even
 */
static void
acornfb_set_timing(struct fb_var_screeninfo *var)
{
	struct pixclock *pclk;
	struct vidc_timing vidc;
	u_int horiz_correction;
	u_int sync_len, display_start, display_end, cycle;
	u_int is_interlaced;
	u_int vid_ctl, vidc_ctl;
	u_int bandwidth;

	memset(&vidc, 0, sizeof(vidc));

	pclk = acornfb_valid_pixrate(var);
	vidc_ctl = pclk->vidc_ctl;
	vid_ctl  = pclk->vid_ctl;

	bandwidth = var->pixclock * 8 / var->bits_per_pixel;
	/* 25.175, 4bpp = 79.444ns per byte, 317.776ns per word: fifo = 2,6 */
	if (bandwidth > 143500)
		vidc_ctl |= VIDC_CTRL_FIFO_3_7;
	else if (bandwidth > 71750)
		vidc_ctl |= VIDC_CTRL_FIFO_2_6;
	else if (bandwidth > 35875)
		vidc_ctl |= VIDC_CTRL_FIFO_1_5;
	else
		vidc_ctl |= VIDC_CTRL_FIFO_0_4;

	switch (var->bits_per_pixel) {
	case 1:
		horiz_correction = 19;
		vidc_ctl |= VIDC_CTRL_1BPP;
		break;

	case 2:
		horiz_correction = 11;
		vidc_ctl |= VIDC_CTRL_2BPP;
		break;

	case 4:
		horiz_correction = 7;
		vidc_ctl |= VIDC_CTRL_4BPP;
		break;

	default:
	case 8:
		horiz_correction = 5;
		vidc_ctl |= VIDC_CTRL_8BPP;
		break;
	}

	if (var->sync & FB_SYNC_COMP_HIGH_ACT) /* should be FB_SYNC_COMP */
		vidc_ctl |= VIDC_CTRL_CSYNC;
	else {
		if (!(var->sync & FB_SYNC_HOR_HIGH_ACT))
			vid_ctl |= VID_CTL_HS_NHSYNC;

		if (!(var->sync & FB_SYNC_VERT_HIGH_ACT))
			vid_ctl |= VID_CTL_VS_NVSYNC;
	}

	sync_len	= var->hsync_len;
	display_start	= sync_len + var->left_margin;
	display_end	= display_start + var->xres;
	cycle		= display_end + var->right_margin;

	/* if interlaced, then hcr/2 must be even */
	is_interlaced = (var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED;

	if (is_interlaced) {
		vidc_ctl |= VIDC_CTRL_INTERLACE;
		if (cycle & 2) {
			cycle += 2;
			var->right_margin += 2;
		}
	}

	vidc.h_cycle		= (cycle - 2) / 2;
	vidc.h_sync_width	= (sync_len - 2) / 2;
	vidc.h_border_start	= (display_start - 1) / 2;
	vidc.h_display_start	= (display_start - horiz_correction) / 2;
	vidc.h_display_end	= (display_end - horiz_correction) / 2;
	vidc.h_border_end	= (display_end - 1) / 2;
	vidc.h_interlace	= (vidc.h_cycle + 1) / 2;

	sync_len	= var->vsync_len;
	display_start	= sync_len + var->upper_margin;
	display_end	= display_start + var->yres;
	cycle		= display_end + var->lower_margin;

	if (is_interlaced)
		cycle = (cycle - 3) / 2;
	else
		cycle = cycle - 1;

	vidc.v_cycle		= cycle;
	vidc.v_sync_width	= sync_len - 1;
	vidc.v_border_start	= display_start - 1;
	vidc.v_display_start	= vidc.v_border_start;
	vidc.v_display_end	= display_end - 1;
	vidc.v_border_end	= vidc.v_display_end;

	if (machine_is_a5k())
		__raw_writeb(vid_ctl, IOEB_VID_CTL);

	if (memcmp(&current_vidc, &vidc, sizeof(vidc))) {
		current_vidc = vidc;

		vidc_writel(0xe0000000 | vidc_ctl);
		vidc_writel(0x80000000 | (vidc.h_cycle << 14));
		vidc_writel(0x84000000 | (vidc.h_sync_width << 14));
		vidc_writel(0x88000000 | (vidc.h_border_start << 14));
		vidc_writel(0x8c000000 | (vidc.h_display_start << 14));
		vidc_writel(0x90000000 | (vidc.h_display_end << 14));
		vidc_writel(0x94000000 | (vidc.h_border_end << 14));
		vidc_writel(0x98000000);
		vidc_writel(0x9c000000 | (vidc.h_interlace << 14));
		vidc_writel(0xa0000000 | (vidc.v_cycle << 14));
		vidc_writel(0xa4000000 | (vidc.v_sync_width << 14));
		vidc_writel(0xa8000000 | (vidc.v_border_start << 14));
		vidc_writel(0xac000000 | (vidc.v_display_start << 14));
		vidc_writel(0xb0000000 | (vidc.v_display_end << 14));
		vidc_writel(0xb4000000 | (vidc.v_border_end << 14));
		vidc_writel(0xb8000000);
		vidc_writel(0xbc000000);
	}
#ifdef DEBUG_MODE_SELECTION
	printk(KERN_DEBUG "VIDC registers for %dx%dx%d:\n", var->xres,
	       var->yres, var->bits_per_pixel);
	printk(KERN_DEBUG " H-cycle          : %d\n", vidc.h_cycle);
	printk(KERN_DEBUG " H-sync-width     : %d\n", vidc.h_sync_width);
	printk(KERN_DEBUG " H-border-start   : %d\n", vidc.h_border_start);
	printk(KERN_DEBUG " H-display-start  : %d\n", vidc.h_display_start);
	printk(KERN_DEBUG " H-display-end    : %d\n", vidc.h_display_end);
	printk(KERN_DEBUG " H-border-end     : %d\n", vidc.h_border_end);
	printk(KERN_DEBUG " H-interlace      : %d\n", vidc.h_interlace);
	printk(KERN_DEBUG " V-cycle          : %d\n", vidc.v_cycle);
	printk(KERN_DEBUG " V-sync-width     : %d\n", vidc.v_sync_width);
	printk(KERN_DEBUG " V-border-start   : %d\n", vidc.v_border_start);
	printk(KERN_DEBUG " V-display-start  : %d\n", vidc.v_display_start);
	printk(KERN_DEBUG " V-display-end    : %d\n", vidc.v_display_end);
	printk(KERN_DEBUG " V-border-end     : %d\n", vidc.v_border_end);
	printk(KERN_DEBUG " VIDC Ctrl (E)    : 0x%08X\n", vidc_ctl);
	printk(KERN_DEBUG " IOEB Ctrl        : 0x%08X\n", vid_ctl);
#endif
}

static int
acornfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		  u_int trans, struct fb_info *info)
{
	union palette pal;

	if (regno >= current_par.palette_size)
		return 1;

	pal.p = 0;
	pal.vidc.reg   = regno;
	pal.vidc.red   = red >> 12;
	pal.vidc.green = green >> 12;
	pal.vidc.blue  = blue >> 12;

	current_par.palette[regno] = pal;

	vidc_writel(pal.p);

	return 0;
}
#endif

#ifdef HAS_VIDC20
#include <mach/acornfb.h>

#define MAX_SIZE	2*1024*1024

/* VIDC20 has a different set of rules from the VIDC:
 *  hcr  : must be multiple of 4
 *  hswr : must be even
 *  hdsr : must be even
 *  hder : must be even
 *  vcr  : >= 2, (interlace, must be odd)
 *  vswr : >= 1
 *  vdsr : >= 1
 *  vder : >= vdsr
 */
static void acornfb_set_timing(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	struct vidc_timing vidc;
	u_int vcr, fsize;
	u_int ext_ctl, dat_ctl;
	u_int words_per_line;

	memset(&vidc, 0, sizeof(vidc));

	vidc.h_sync_width	= var->hsync_len - 8;
	vidc.h_border_start	= vidc.h_sync_width + var->left_margin + 8 - 12;
	vidc.h_display_start	= vidc.h_border_start + 12 - 18;
	vidc.h_display_end	= vidc.h_display_start + var->xres;
	vidc.h_border_end	= vidc.h_display_end + 18 - 12;
	vidc.h_cycle		= vidc.h_border_end + var->right_margin + 12 - 8;
	vidc.h_interlace	= vidc.h_cycle / 2;
	vidc.v_sync_width	= var->vsync_len - 1;
	vidc.v_border_start	= vidc.v_sync_width + var->upper_margin;
	vidc.v_display_start	= vidc.v_border_start;
	vidc.v_display_end	= vidc.v_display_start + var->yres;
	vidc.v_border_end	= vidc.v_display_end;
	vidc.control		= acornfb_default_control();

	vcr = var->vsync_len + var->upper_margin + var->yres +
	      var->lower_margin;

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		vidc.v_cycle = (vcr - 3) / 2;
		vidc.control |= VIDC20_CTRL_INT;
	} else
		vidc.v_cycle = vcr - 2;

	switch (var->bits_per_pixel) {
	case  1: vidc.control |= VIDC20_CTRL_1BPP;	break;
	case  2: vidc.control |= VIDC20_CTRL_2BPP;	break;
	case  4: vidc.control |= VIDC20_CTRL_4BPP;	break;
	default:
	case  8: vidc.control |= VIDC20_CTRL_8BPP;	break;
	case 16: vidc.control |= VIDC20_CTRL_16BPP;	break;
	case 32: vidc.control |= VIDC20_CTRL_32BPP;	break;
	}

	acornfb_vidc20_find_rates(&vidc, var);
	fsize = var->vsync_len + var->upper_margin + var->lower_margin - 1;

	if (memcmp(&current_vidc, &vidc, sizeof(vidc))) {
		current_vidc = vidc;

		vidc_writel(VIDC20_CTRL| vidc.control);
		vidc_writel(0xd0000000 | vidc.pll_ctl);
		vidc_writel(0x80000000 | vidc.h_cycle);
		vidc_writel(0x81000000 | vidc.h_sync_width);
		vidc_writel(0x82000000 | vidc.h_border_start);
		vidc_writel(0x83000000 | vidc.h_display_start);
		vidc_writel(0x84000000 | vidc.h_display_end);
		vidc_writel(0x85000000 | vidc.h_border_end);
		vidc_writel(0x86000000);
		vidc_writel(0x87000000 | vidc.h_interlace);
		vidc_writel(0x90000000 | vidc.v_cycle);
		vidc_writel(0x91000000 | vidc.v_sync_width);
		vidc_writel(0x92000000 | vidc.v_border_start);
		vidc_writel(0x93000000 | vidc.v_display_start);
		vidc_writel(0x94000000 | vidc.v_display_end);
		vidc_writel(0x95000000 | vidc.v_border_end);
		vidc_writel(0x96000000);
		vidc_writel(0x97000000);
	}

	iomd_writel(fsize, IOMD_FSIZE);

	ext_ctl = acornfb_default_econtrol();

	if (var->sync & FB_SYNC_COMP_HIGH_ACT) /* should be FB_SYNC_COMP */
		ext_ctl |= VIDC20_ECTL_HS_NCSYNC | VIDC20_ECTL_VS_NCSYNC;
	else {
		if (var->sync & FB_SYNC_HOR_HIGH_ACT)
			ext_ctl |= VIDC20_ECTL_HS_HSYNC;
		else
			ext_ctl |= VIDC20_ECTL_HS_NHSYNC;

		if (var->sync & FB_SYNC_VERT_HIGH_ACT)
			ext_ctl |= VIDC20_ECTL_VS_VSYNC;
		else
			ext_ctl |= VIDC20_ECTL_VS_NVSYNC;
	}

	vidc_writel(VIDC20_ECTL | ext_ctl);

	words_per_line = var->xres * var->bits_per_pixel / 32;

	if (current_par.using_vram && info->fix.smem_len == 2048*1024)
		words_per_line /= 2;

	/* RiscPC doesn't use the VIDC's VRAM control. */
	dat_ctl = VIDC20_DCTL_VRAM_DIS | VIDC20_DCTL_SNA | words_per_line;

	/* The data bus width is dependent on both the type
	 * and amount of video memory.
	 *     DRAM	32bit low
	 * 1MB VRAM	32bit
	 * 2MB VRAM	64bit
	 */
	if (current_par.using_vram && current_par.vram_half_sam == 2048)
		dat_ctl |= VIDC20_DCTL_BUS_D63_0;
	else
		dat_ctl |= VIDC20_DCTL_BUS_D31_0;

	vidc_writel(VIDC20_DCTL | dat_ctl);

#ifdef DEBUG_MODE_SELECTION
	printk(KERN_DEBUG "VIDC registers for %dx%dx%d:\n", var->xres,
	       var->yres, var->bits_per_pixel);
	printk(KERN_DEBUG " H-cycle          : %d\n", vidc.h_cycle);
	printk(KERN_DEBUG " H-sync-width     : %d\n", vidc.h_sync_width);
	printk(KERN_DEBUG " H-border-start   : %d\n", vidc.h_border_start);
	printk(KERN_DEBUG " H-display-start  : %d\n", vidc.h_display_start);
	printk(KERN_DEBUG " H-display-end    : %d\n", vidc.h_display_end);
	printk(KERN_DEBUG " H-border-end     : %d\n", vidc.h_border_end);
	printk(KERN_DEBUG " H-interlace      : %d\n", vidc.h_interlace);
	printk(KERN_DEBUG " V-cycle          : %d\n", vidc.v_cycle);
	printk(KERN_DEBUG " V-sync-width     : %d\n", vidc.v_sync_width);
	printk(KERN_DEBUG " V-border-start   : %d\n", vidc.v_border_start);
	printk(KERN_DEBUG " V-display-start  : %d\n", vidc.v_display_start);
	printk(KERN_DEBUG " V-display-end    : %d\n", vidc.v_display_end);
	printk(KERN_DEBUG " V-border-end     : %d\n", vidc.v_border_end);
	printk(KERN_DEBUG " Ext Ctrl  (C)    : 0x%08X\n", ext_ctl);
	printk(KERN_DEBUG " PLL Ctrl  (D)    : 0x%08X\n", vidc.pll_ctl);
	printk(KERN_DEBUG " Ctrl      (E)    : 0x%08X\n", vidc.control);
	printk(KERN_DEBUG " Data Ctrl (F)    : 0x%08X\n", dat_ctl);
	printk(KERN_DEBUG " Fsize            : 0x%08X\n", fsize);
#endif
}

/*
 * We have to take note of the VIDC20's 16-bit palette here.
 * The VIDC20 looks up a 16 bit pixel as follows:
 *
 *   bits   111111
 *          5432109876543210
 *   red            ++++++++  (8 bits,  7 to 0)
 *  green       ++++++++      (8 bits, 11 to 4)
 *   blue   ++++++++          (8 bits, 15 to 8)
 *
 * We use a pixel which looks like:
 *
 *   bits   111111
 *          5432109876543210
 *   red               +++++  (5 bits,  4 to  0)
 *  green         +++++       (5 bits,  9 to  5)
 *   blue    +++++            (5 bits, 14 to 10)
 */
static int
acornfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		  u_int trans, struct fb_info *info)
{
	union palette pal;

	if (regno >= current_par.palette_size)
		return 1;

	if (regno < 16 && info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		u32 pseudo_val;

		pseudo_val  = regno << info->var.red.offset;
		pseudo_val |= regno << info->var.green.offset;
		pseudo_val |= regno << info->var.blue.offset;

		((u32 *)info->pseudo_palette)[regno] = pseudo_val;
	}

	pal.p = 0;
	pal.vidc20.red   = red >> 8;
	pal.vidc20.green = green >> 8;
	pal.vidc20.blue  = blue >> 8;

	current_par.palette[regno] = pal;

	if (info->var.bits_per_pixel == 16) {
		int i;

		pal.p = 0;
		vidc_writel(0x10000000);
		for (i = 0; i < 256; i += 1) {
			pal.vidc20.red   = current_par.palette[ i       & 31].vidc20.red;
			pal.vidc20.green = current_par.palette[(i >> 1) & 31].vidc20.green;
			pal.vidc20.blue  = current_par.palette[(i >> 2) & 31].vidc20.blue;
			vidc_writel(pal.p);
			/* Palette register pointer auto-increments */
		}
	} else {
		vidc_writel(0x10000000 | regno);
		vidc_writel(pal.p);
	}

	return 0;
}
#endif

/*
 * Before selecting the timing parameters, adjust
 * the resolution to fit the rules.
 */
static int
acornfb_adjust_timing(struct fb_info *info, struct fb_var_screeninfo *var, u_int fontht)
{
	u_int font_line_len, sam_size, min_size, size, nr_y;

	/* xres must be even */
	var->xres = (var->xres + 1) & ~1;

	/*
	 * We don't allow xres_virtual to differ from xres
	 */
	var->xres_virtual = var->xres;
	var->xoffset = 0;

	if (current_par.using_vram)
		sam_size = current_par.vram_half_sam * 2;
	else
		sam_size = 16;

	/*
	 * Now, find a value for yres_virtual which allows
	 * us to do ywrap scrolling.  The value of
	 * yres_virtual must be such that the end of the
	 * displayable frame buffer must be aligned with
	 * the start of a font line.
	 */
	font_line_len = var->xres * var->bits_per_pixel * fontht / 8;
	min_size = var->xres * var->yres * var->bits_per_pixel / 8;

	/*
	 * If minimum screen size is greater than that we have
	 * available, reject it.
	 */
	if (min_size > info->fix.smem_len)
		return -EINVAL;

	/* Find int 'y', such that y * fll == s * sam < maxsize
	 * y = s * sam / fll; s = maxsize / sam
	 */
	for (size = info->fix.smem_len;
	     nr_y = size / font_line_len, min_size <= size;
	     size -= sam_size) {
		if (nr_y * font_line_len == size)
			break;
	}
	nr_y *= fontht;

	if (var->accel_flags & FB_ACCELF_TEXT) {
		if (min_size > size) {
			/*
			 * failed, use ypan
			 */
			size = info->fix.smem_len;
			var->yres_virtual = size / (font_line_len / fontht);
		} else
			var->yres_virtual = nr_y;
	} else if (var->yres_virtual > nr_y)
		var->yres_virtual = nr_y;

	current_par.screen_end = info->fix.smem_start + size;

	/*
	 * Fix yres & yoffset if needed.
	 */
	if (var->yres > var->yres_virtual)
		var->yres = var->yres_virtual;

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset > var->yres_virtual)
			var->yoffset = var->yres_virtual;
	} else {
		if (var->yoffset + var->yres > var->yres_virtual)
			var->yoffset = var->yres_virtual - var->yres;
	}

	/* hsync_len must be even */
	var->hsync_len = (var->hsync_len + 1) & ~1;

#ifdef HAS_VIDC
	/* left_margin must be odd */
	if ((var->left_margin & 1) == 0) {
		var->left_margin -= 1;
		var->right_margin += 1;
	}

	/* right_margin must be odd */
	var->right_margin |= 1;
#elif defined(HAS_VIDC20)
	/* left_margin must be even */
	if (var->left_margin & 1) {
		var->left_margin += 1;
		var->right_margin -= 1;
	}

	/* right_margin must be even */
	if (var->right_margin & 1)
		var->right_margin += 1;
#endif

	if (var->vsync_len < 1)
		var->vsync_len = 1;

	return 0;
}

static int
acornfb_validate_timing(struct fb_var_screeninfo *var,
			struct fb_monspecs *monspecs)
{
	unsigned long hs, vs;

	/*
	 * hs(Hz) = 10^12 / (pixclock * xtotal)
	 * vs(Hz) = hs(Hz) / ytotal
	 *
	 * No need to do long long divisions or anything
	 * like that if you factor it correctly
	 */
	hs = 1953125000 / var->pixclock;
	hs = hs * 512 /
	     (var->xres + var->left_margin + var->right_margin + var->hsync_len);
	vs = hs /
	     (var->yres + var->upper_margin + var->lower_margin + var->vsync_len);

	return (vs >= monspecs->vfmin && vs <= monspecs->vfmax &&
		hs >= monspecs->hfmin && hs <= monspecs->hfmax) ? 0 : -EINVAL;
}

static inline void
acornfb_update_dma(struct fb_info *info, struct fb_var_screeninfo *var)
{
	u_int off = var->yoffset * info->fix.line_length;

#if defined(HAS_MEMC)
	memc_write(VDMA_INIT, off >> 2);
#elif defined(HAS_IOMD)
	iomd_writel(info->fix.smem_start + off, IOMD_VIDINIT);
#endif
}

static int
acornfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u_int fontht;
	int err;

	/*
	 * FIXME: Find the font height
	 */
	fontht = 8;

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	switch (var->bits_per_pixel) {
	case 1:	case 2:	case 4:	case 8:
		var->red.offset    = 0;
		var->red.length    = var->bits_per_pixel;
		var->green         = var->red;
		var->blue          = var->red;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;

#ifdef HAS_VIDC20
	case 16:
		var->red.offset    = 0;
		var->red.length    = 5;
		var->green.offset  = 5;
		var->green.length  = 5;
		var->blue.offset   = 10;
		var->blue.length   = 5;
		var->transp.offset = 15;
		var->transp.length = 1;
		break;

	case 32:
		var->red.offset    = 0;
		var->red.length    = 8;
		var->green.offset  = 8;
		var->green.length  = 8;
		var->blue.offset   = 16;
		var->blue.length   = 8;
		var->transp.offset = 24;
		var->transp.length = 4;
		break;
#endif
	default:
		return -EINVAL;
	}

	/*
	 * Check to see if the pixel rate is valid.
	 */
	if (!acornfb_valid_pixrate(var))
		return -EINVAL;

	/*
	 * Validate and adjust the resolution to
	 * match the video generator hardware.
	 */
	err = acornfb_adjust_timing(info, var, fontht);
	if (err)
		return err;

	/*
	 * Validate the timing against the
	 * monitor hardware.
	 */
	return acornfb_validate_timing(var, &info->monspecs);
}

static int acornfb_set_par(struct fb_info *info)
{
	switch (info->var.bits_per_pixel) {
	case 1:
		current_par.palette_size = 2;
		info->fix.visual = FB_VISUAL_MONO10;
		break;
	case 2:
		current_par.palette_size = 4;
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 4:
		current_par.palette_size = 16;
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 8:
		current_par.palette_size = VIDC_PALETTE_SIZE;
#ifdef HAS_VIDC
		info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
#else
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
#endif
		break;
#ifdef HAS_VIDC20
	case 16:
		current_par.palette_size = 32;
		info->fix.visual = FB_VISUAL_DIRECTCOLOR;
		break;
	case 32:
		current_par.palette_size = VIDC_PALETTE_SIZE;
		info->fix.visual = FB_VISUAL_DIRECTCOLOR;
		break;
#endif
	default:
		BUG();
	}

	info->fix.line_length	= (info->var.xres * info->var.bits_per_pixel) / 8;

#if defined(HAS_MEMC)
	{
		unsigned long size = info->fix.smem_len - VDMA_XFERSIZE;

		memc_write(VDMA_START, 0);
		memc_write(VDMA_END, size >> 2);
	}
#elif defined(HAS_IOMD)
	{
		unsigned long start, size;
		u_int control;

		start = info->fix.smem_start;
		size  = current_par.screen_end;

		if (current_par.using_vram) {
			size -= current_par.vram_half_sam;
			control = DMA_CR_E | (current_par.vram_half_sam / 256);
		} else {
			size -= 16;
			control = DMA_CR_E | DMA_CR_D | 16;
		}

		iomd_writel(start,   IOMD_VIDSTART);
		iomd_writel(size,    IOMD_VIDEND);
		iomd_writel(control, IOMD_VIDCR);
	}
#endif

	acornfb_update_dma(info, &info->var);
	acornfb_set_timing(info);

	return 0;
}

static int
acornfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u_int y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += info->var.yres;

	if (y_bottom > info->var.yres_virtual)
		return -EINVAL;

	acornfb_update_dma(info, var);

	return 0;
}

static struct fb_ops acornfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= acornfb_check_var,
	.fb_set_par	= acornfb_set_par,
	.fb_setcolreg	= acornfb_setcolreg,
	.fb_pan_display	= acornfb_pan_display,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

/*
 * Everything after here is initialisation!!!
 */
static struct fb_videomode modedb[] = {
	{	/* 320x256 @ 50Hz */
		NULL, 50,  320,  256, 125000,  92,  62,  35, 19,  38, 2,
		FB_SYNC_COMP_HIGH_ACT,
		FB_VMODE_NONINTERLACED
	}, {	/* 640x250 @ 50Hz, 15.6 kHz hsync */
		NULL, 50,  640,  250,  62500, 185, 123,  38, 21,  76, 3,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 640x256 @ 50Hz, 15.6 kHz hsync */
		NULL, 50,  640,  256,  62500, 185, 123,  35, 18,  76, 3,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 640x512 @ 50Hz, 26.8 kHz hsync */
		NULL, 50,  640,  512,  41667, 113,  87,  18,  1,  56, 3,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 640x250 @ 70Hz, 31.5 kHz hsync */
		NULL, 70,  640,  250,  39722,  48,  16, 109, 88,  96, 2,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 640x256 @ 70Hz, 31.5 kHz hsync */
		NULL, 70,  640,  256,  39722,  48,  16, 106, 85,  96, 2,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 640x352 @ 70Hz, 31.5 kHz hsync */
		NULL, 70,  640,  352,  39722,  48,  16,  58, 37,  96, 2,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 640x480 @ 60Hz, 31.5 kHz hsync */
		NULL, 60,  640,  480,  39722,  48,  16,  32, 11,  96, 2,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 800x600 @ 56Hz, 35.2 kHz hsync */
		NULL, 56,  800,  600,  27778, 101,  23,  22,  1, 100, 2,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 896x352 @ 60Hz, 21.8 kHz hsync */
		NULL, 60,  896,  352,  41667,  59,  27,   9,  0, 118, 3,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 1024x 768 @ 60Hz, 48.4 kHz hsync */
		NULL, 60, 1024,  768,  15385, 160,  24,  29,  3, 136, 6,
		0,
		FB_VMODE_NONINTERLACED
	}, {	/* 1280x1024 @ 60Hz, 63.8 kHz hsync */
		NULL, 60, 1280, 1024,   9090, 186,  96,  38,  1, 160, 3,
		0,
		FB_VMODE_NONINTERLACED
	}
};

static struct fb_videomode acornfb_default_mode = {
	.name =		NULL,
	.refresh =	60,
	.xres =		640,
	.yres =		480,
	.pixclock =	39722,
	.left_margin =	56,
	.right_margin =	16,
	.upper_margin =	34,
	.lower_margin =	9,
	.hsync_len =	88,
	.vsync_len =	2,
	.sync =		0,
	.vmode =	FB_VMODE_NONINTERLACED
};

static void acornfb_init_fbinfo(void)
{
	static int first = 1;

	if (!first)
		return;
	first = 0;

	fb_info.fbops		= &acornfb_ops;
	fb_info.flags		= FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;
	fb_info.pseudo_palette	= current_par.pseudo_palette;

	strcpy(fb_info.fix.id, "Acorn");
	fb_info.fix.type	= FB_TYPE_PACKED_PIXELS;
	fb_info.fix.type_aux	= 0;
	fb_info.fix.xpanstep	= 0;
	fb_info.fix.ypanstep	= 1;
	fb_info.fix.ywrapstep	= 1;
	fb_info.fix.line_length	= 0;
	fb_info.fix.accel	= FB_ACCEL_NONE;

	/*
	 * setup initial parameters
	 */
	memset(&fb_info.var, 0, sizeof(fb_info.var));

#if defined(HAS_VIDC20)
	fb_info.var.red.length	   = 8;
	fb_info.var.transp.length  = 4;
#elif defined(HAS_VIDC)
	fb_info.var.red.length	   = 4;
	fb_info.var.transp.length  = 1;
#endif
	fb_info.var.green	   = fb_info.var.red;
	fb_info.var.blue	   = fb_info.var.red;
	fb_info.var.nonstd	   = 0;
	fb_info.var.activate	   = FB_ACTIVATE_NOW;
	fb_info.var.height	   = -1;
	fb_info.var.width	   = -1;
	fb_info.var.vmode	   = FB_VMODE_NONINTERLACED;
	fb_info.var.accel_flags	   = FB_ACCELF_TEXT;

	current_par.dram_size	   = 0;
	current_par.montype	   = -1;
	current_par.dpms	   = 0;
}

/*
 * setup acornfb options:
 *
 *  mon:hmin-hmax:vmin-vmax:dpms:width:height
 *	Set monitor parameters:
 *		hmin   = horizontal minimum frequency (Hz)
 *		hmax   = horizontal maximum frequency (Hz)	(optional)
 *		vmin   = vertical minimum frequency (Hz)
 *		vmax   = vertical maximum frequency (Hz)	(optional)
 *		dpms   = DPMS supported?			(optional)
 *		width  = width of picture in mm.		(optional)
 *		height = height of picture in mm.		(optional)
 *
 * montype:type
 *	Set RISC-OS style monitor type:
 *		0 (or tv)	- TV frequency
 *		1 (or multi)	- Multi frequency
 *		2 (or hires)	- Hi-res monochrome
 *		3 (or vga)	- VGA
 *		4 (or svga)	- SVGA
 *		auto, or option missing
 *				- try hardware detect
 *
 * dram:size
 *	Set the amount of DRAM to use for the frame buffer
 *	(even if you have VRAM).
 *	size can optionally be followed by 'M' or 'K' for
 *	MB or KB respectively.
 */
static void acornfb_parse_mon(char *opt)
{
	char *p = opt;

	current_par.montype = -2;

	fb_info.monspecs.hfmin = simple_strtoul(p, &p, 0);
	if (*p == '-')
		fb_info.monspecs.hfmax = simple_strtoul(p + 1, &p, 0);
	else
		fb_info.monspecs.hfmax = fb_info.monspecs.hfmin;

	if (*p != ':')
		goto bad;

	fb_info.monspecs.vfmin = simple_strtoul(p + 1, &p, 0);
	if (*p == '-')
		fb_info.monspecs.vfmax = simple_strtoul(p + 1, &p, 0);
	else
		fb_info.monspecs.vfmax = fb_info.monspecs.vfmin;

	if (*p != ':')
		goto check_values;

	fb_info.monspecs.dpms = simple_strtoul(p + 1, &p, 0);

	if (*p != ':')
		goto check_values;

	fb_info.var.width = simple_strtoul(p + 1, &p, 0);

	if (*p != ':')
		goto check_values;

	fb_info.var.height = simple_strtoul(p + 1, NULL, 0);

check_values:
	if (fb_info.monspecs.hfmax < fb_info.monspecs.hfmin ||
	    fb_info.monspecs.vfmax < fb_info.monspecs.vfmin)
		goto bad;
	return;

bad:
	printk(KERN_ERR "Acornfb: bad monitor settings: %s\n", opt);
	current_par.montype = -1;
}

static void acornfb_parse_montype(char *opt)
{
	current_par.montype = -2;

	if (strncmp(opt, "tv", 2) == 0) {
		opt += 2;
		current_par.montype = 0;
	} else if (strncmp(opt, "multi", 5) == 0) {
		opt += 5;
		current_par.montype = 1;
	} else if (strncmp(opt, "hires", 5) == 0) {
		opt += 5;
		current_par.montype = 2;
	} else if (strncmp(opt, "vga", 3) == 0) {
		opt += 3;
		current_par.montype = 3;
	} else if (strncmp(opt, "svga", 4) == 0) {
		opt += 4;
		current_par.montype = 4;
	} else if (strncmp(opt, "auto", 4) == 0) {
		opt += 4;
		current_par.montype = -1;
	} else if (isdigit(*opt))
		current_par.montype = simple_strtoul(opt, &opt, 0);

	if (current_par.montype == -2 ||
	    current_par.montype > NR_MONTYPES) {
		printk(KERN_ERR "acornfb: unknown monitor type: %s\n",
			opt);
		current_par.montype = -1;
	} else
	if (opt && *opt) {
		if (strcmp(opt, ",dpms") == 0)
			current_par.dpms = 1;
		else
			printk(KERN_ERR
			       "acornfb: unknown monitor option: %s\n",
			       opt);
	}
}

static void acornfb_parse_dram(char *opt)
{
	unsigned int size;

	size = simple_strtoul(opt, &opt, 0);

	if (opt) {
		switch (*opt) {
		case 'M':
		case 'm':
			size *= 1024;
		case 'K':
		case 'k':
			size *= 1024;
		default:
			break;
		}
	}

	current_par.dram_size = size;
}

static struct options {
	char *name;
	void (*parse)(char *opt);
} opt_table[] = {
	{ "mon",     acornfb_parse_mon     },
	{ "montype", acornfb_parse_montype },
	{ "dram",    acornfb_parse_dram    },
	{ NULL, NULL }
};

static int acornfb_setup(char *options)
{
	struct options *optp;
	char *opt;

	if (!options || !*options)
		return 0;

	acornfb_init_fbinfo();

	while ((opt = strsep(&options, ",")) != NULL) {
		if (!*opt)
			continue;

		for (optp = opt_table; optp->name; optp++) {
			int optlen;

			optlen = strlen(optp->name);

			if (strncmp(opt, optp->name, optlen) == 0 &&
			    opt[optlen] == ':') {
				optp->parse(opt + optlen + 1);
				break;
			}
		}

		if (!optp->name)
			printk(KERN_ERR "acornfb: unknown parameter: %s\n",
			       opt);
	}
	return 0;
}

/*
 * Detect type of monitor connected
 *  For now, we just assume SVGA
 */
static int acornfb_detect_monitortype(void)
{
	return 4;
}

/*
 * This enables the unused memory to be freed on older Acorn machines.
 * We are freeing memory on behalf of the architecture initialisation
 * code here.
 */
static inline void
free_unused_pages(unsigned int virtual_start, unsigned int virtual_end)
{
	int mb_freed = 0;

	/*
	 * Align addresses
	 */
	virtual_start = PAGE_ALIGN(virtual_start);
	virtual_end = PAGE_ALIGN(virtual_end);

	while (virtual_start < virtual_end) {
		struct page *page;

		/*
		 * Clear page reserved bit,
		 * set count to 1, and free
		 * the page.
		 */
		page = virt_to_page(virtual_start);
		ClearPageReserved(page);
		init_page_count(page);
		free_page(virtual_start);

		virtual_start += PAGE_SIZE;
		mb_freed += PAGE_SIZE / 1024;
	}

	printk("acornfb: freed %dK memory\n", mb_freed);
}

static int acornfb_probe(struct platform_device *dev)
{
	unsigned long size;
	u_int h_sync, v_sync;
	int rc, i;
	char *option = NULL;

	if (fb_get_options("acornfb", &option))
		return -ENODEV;
	acornfb_setup(option);

	acornfb_init_fbinfo();

	current_par.dev = &dev->dev;

	if (current_par.montype == -1)
		current_par.montype = acornfb_detect_monitortype();

	if (current_par.montype == -1 || current_par.montype > NR_MONTYPES)
		current_par.montype = 4;

	if (current_par.montype >= 0) {
		fb_info.monspecs = monspecs[current_par.montype];
		fb_info.monspecs.dpms = current_par.dpms;
	}

	/*
	 * Try to select a suitable default mode
	 */
	for (i = 0; i < ARRAY_SIZE(modedb); i++) {
		unsigned long hs;

		hs = modedb[i].refresh *
		     (modedb[i].yres + modedb[i].upper_margin +
		      modedb[i].lower_margin + modedb[i].vsync_len);
		if (modedb[i].xres == DEFAULT_XRES &&
		    modedb[i].yres == DEFAULT_YRES &&
		    modedb[i].refresh >= fb_info.monspecs.vfmin &&
		    modedb[i].refresh <= fb_info.monspecs.vfmax &&
		    hs                >= fb_info.monspecs.hfmin &&
		    hs                <= fb_info.monspecs.hfmax) {
			acornfb_default_mode = modedb[i];
			break;
		}
	}

	fb_info.screen_base    = (char *)SCREEN_BASE;
	fb_info.fix.smem_start = SCREEN_START;
	current_par.using_vram = 0;

	/*
	 * If vram_size is set, we are using VRAM in
	 * a Risc PC.  However, if the user has specified
	 * an amount of DRAM then use that instead.
	 */
	if (vram_size && !current_par.dram_size) {
		size = vram_size;
		current_par.vram_half_sam = vram_size / 1024;
		current_par.using_vram = 1;
	} else if (current_par.dram_size)
		size = current_par.dram_size;
	else
		size = MAX_SIZE;

	/*
	 * Limit maximum screen size.
	 */
	if (size > MAX_SIZE)
		size = MAX_SIZE;

	size = PAGE_ALIGN(size);

#if defined(HAS_VIDC20)
	if (!current_par.using_vram) {
		dma_addr_t handle;
		void *base;

		/*
		 * RiscPC needs to allocate the DRAM memory
		 * for the framebuffer if we are not using
		 * VRAM.
		 */
		base = dma_alloc_writecombine(current_par.dev, size, &handle,
					      GFP_KERNEL);
		if (base == NULL) {
			printk(KERN_ERR "acornfb: unable to allocate screen "
			       "memory\n");
			return -ENOMEM;
		}

		fb_info.screen_base = base;
		fb_info.fix.smem_start = handle;
	}
#endif
#if defined(HAS_VIDC)
	/*
	 * Archimedes/A5000 machines use a fixed address for their
	 * framebuffers.  Free unused pages
	 */
	free_unused_pages(PAGE_OFFSET + size, PAGE_OFFSET + MAX_SIZE);
#endif

	fb_info.fix.smem_len = size;
	current_par.palette_size   = VIDC_PALETTE_SIZE;

	/*
	 * Lookup the timing for this resolution.  If we can't
	 * find it, then we can't restore it if we change
	 * the resolution, so we disable this feature.
	 */
	do {
		rc = fb_find_mode(&fb_info.var, &fb_info, NULL, modedb,
				 ARRAY_SIZE(modedb),
				 &acornfb_default_mode, DEFAULT_BPP);
		/*
		 * If we found an exact match, all ok.
		 */
		if (rc == 1)
			break;

		rc = fb_find_mode(&fb_info.var, &fb_info, NULL, NULL, 0,
				  &acornfb_default_mode, DEFAULT_BPP);
		/*
		 * If we found an exact match, all ok.
		 */
		if (rc == 1)
			break;

		rc = fb_find_mode(&fb_info.var, &fb_info, NULL, modedb,
				 ARRAY_SIZE(modedb),
				 &acornfb_default_mode, DEFAULT_BPP);
		if (rc)
			break;

		rc = fb_find_mode(&fb_info.var, &fb_info, NULL, NULL, 0,
				  &acornfb_default_mode, DEFAULT_BPP);
	} while (0);

	/*
	 * If we didn't find an exact match, try the
	 * generic database.
	 */
	if (rc == 0) {
		printk("Acornfb: no valid mode found\n");
		return -EINVAL;
	}

	h_sync = 1953125000 / fb_info.var.pixclock;
	h_sync = h_sync * 512 / (fb_info.var.xres + fb_info.var.left_margin +
		 fb_info.var.right_margin + fb_info.var.hsync_len);
	v_sync = h_sync / (fb_info.var.yres + fb_info.var.upper_margin +
		 fb_info.var.lower_margin + fb_info.var.vsync_len);

	printk(KERN_INFO "Acornfb: %dkB %cRAM, %s, using %dx%d, "
		"%d.%03dkHz, %dHz\n",
		fb_info.fix.smem_len / 1024,
		current_par.using_vram ? 'V' : 'D',
		VIDC_NAME, fb_info.var.xres, fb_info.var.yres,
		h_sync / 1000, h_sync % 1000, v_sync);

	printk(KERN_INFO "Acornfb: Monitor: %d.%03d-%d.%03dkHz, %d-%dHz%s\n",
		fb_info.monspecs.hfmin / 1000, fb_info.monspecs.hfmin % 1000,
		fb_info.monspecs.hfmax / 1000, fb_info.monspecs.hfmax % 1000,
		fb_info.monspecs.vfmin, fb_info.monspecs.vfmax,
		fb_info.monspecs.dpms ? ", DPMS" : "");

	if (fb_set_var(&fb_info, &fb_info.var))
		printk(KERN_ERR "Acornfb: unable to set display parameters\n");

	if (register_framebuffer(&fb_info) < 0)
		return -EINVAL;
	return 0;
}

static struct platform_driver acornfb_driver = {
	.probe	= acornfb_probe,
	.driver	= {
		.name	= "acornfb",
	},
};

static int __init acornfb_init(void)
{
	return platform_driver_register(&acornfb_driver);
}

module_init(acornfb_init);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("VIDC 1/1a/20 framebuffer driver");
MODULE_LICENSE("GPL");
