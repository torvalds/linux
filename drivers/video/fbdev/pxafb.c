/*
 *  linux/drivers/video/pxafb.c
 *
 *  Copyright (C) 1999 Eric A. Thomas.
 *  Copyright (C) 2004 Jean-Frederic Clere.
 *  Copyright (C) 2004 Ian Campbell.
 *  Copyright (C) 2004 Jeff Lackey.
 *   Based on sa1100fb.c Copyright (C) 1999 Eric A. Thomas
 *  which in turn is
 *   Based on acornfb.c Copyright (C) Russell King.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 *	        Intel PXA250/210 LCD Controller Frame Buffer Driver
 *
 * Please direct your questions and comments on this driver to the following
 * email address:
 *
 *	linux-arm-kernel@lists.arm.linux.org.uk
 *
 * Add support for overlay1 and overlay2 based on pxafb_overlay.c:
 *
 *   Copyright (C) 2004, Intel Corporation
 *
 *     2003/08/27: <yu.tang@intel.com>
 *     2004/03/10: <stanley.cai@intel.com>
 *     2004/10/28: <yan.yin@intel.com>
 *
 *   Copyright (C) 2006-2008 Marvell International Ltd.
 *   All Rights Reserved
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/cpufreq.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/console.h>
#include <linux/of_graph.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/div64.h>
#include <mach/bitfield.h>
#include <linux/platform_data/video-pxafb.h>

/*
 * Complain if VAR is out of range.
 */
#define DEBUG_VAR 1

#include "pxafb.h"

/* Bits which should not be set in machine configuration structures */
#define LCCR0_INVALID_CONFIG_MASK	(LCCR0_OUM | LCCR0_BM | LCCR0_QDM |\
					 LCCR0_DIS | LCCR0_EFM | LCCR0_IUM |\
					 LCCR0_SFM | LCCR0_LDM | LCCR0_ENB)

#define LCCR3_INVALID_CONFIG_MASK	(LCCR3_HSP | LCCR3_VSP |\
					 LCCR3_PCD | LCCR3_BPP(0xf))

static int pxafb_activate_var(struct fb_var_screeninfo *var,
				struct pxafb_info *);
static void set_ctrlr_state(struct pxafb_info *fbi, u_int state);
static void setup_base_frame(struct pxafb_info *fbi,
                             struct fb_var_screeninfo *var, int branch);
static int setup_frame_dma(struct pxafb_info *fbi, int dma, int pal,
			   unsigned long offset, size_t size);

static unsigned long video_mem_size = 0;

static inline unsigned long
lcd_readl(struct pxafb_info *fbi, unsigned int off)
{
	return __raw_readl(fbi->mmio_base + off);
}

static inline void
lcd_writel(struct pxafb_info *fbi, unsigned int off, unsigned long val)
{
	__raw_writel(val, fbi->mmio_base + off);
}

static inline void pxafb_schedule_work(struct pxafb_info *fbi, u_int state)
{
	unsigned long flags;

	local_irq_save(flags);
	/*
	 * We need to handle two requests being made at the same time.
	 * There are two important cases:
	 *  1. When we are changing VT (C_REENABLE) while unblanking
	 *     (C_ENABLE) We must perform the unblanking, which will
	 *     do our REENABLE for us.
	 *  2. When we are blanking, but immediately unblank before
	 *     we have blanked.  We do the "REENABLE" thing here as
	 *     well, just to be sure.
	 */
	if (fbi->task_state == C_ENABLE && state == C_REENABLE)
		state = (u_int) -1;
	if (fbi->task_state == C_DISABLE && state == C_ENABLE)
		state = C_REENABLE;

	if (state != (u_int)-1) {
		fbi->task_state = state;
		schedule_work(&fbi->task);
	}
	local_irq_restore(flags);
}

static inline u_int chan_to_field(u_int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int
pxafb_setpalettereg(u_int regno, u_int red, u_int green, u_int blue,
		       u_int trans, struct fb_info *info)
{
	struct pxafb_info *fbi = container_of(info, struct pxafb_info, fb);
	u_int val;

	if (regno >= fbi->palette_size)
		return 1;

	if (fbi->fb.var.grayscale) {
		fbi->palette_cpu[regno] = ((blue >> 8) & 0x00ff);
		return 0;
	}

	switch (fbi->lccr4 & LCCR4_PAL_FOR_MASK) {
	case LCCR4_PAL_FOR_0:
		val  = ((red   >>  0) & 0xf800);
		val |= ((green >>  5) & 0x07e0);
		val |= ((blue  >> 11) & 0x001f);
		fbi->palette_cpu[regno] = val;
		break;
	case LCCR4_PAL_FOR_1:
		val  = ((red   << 8) & 0x00f80000);
		val |= ((green >> 0) & 0x0000fc00);
		val |= ((blue  >> 8) & 0x000000f8);
		((u32 *)(fbi->palette_cpu))[regno] = val;
		break;
	case LCCR4_PAL_FOR_2:
		val  = ((red   << 8) & 0x00fc0000);
		val |= ((green >> 0) & 0x0000fc00);
		val |= ((blue  >> 8) & 0x000000fc);
		((u32 *)(fbi->palette_cpu))[regno] = val;
		break;
	case LCCR4_PAL_FOR_3:
		val  = ((red   << 8) & 0x00ff0000);
		val |= ((green >> 0) & 0x0000ff00);
		val |= ((blue  >> 8) & 0x000000ff);
		((u32 *)(fbi->palette_cpu))[regno] = val;
		break;
	}

	return 0;
}

static int
pxafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		   u_int trans, struct fb_info *info)
{
	struct pxafb_info *fbi = container_of(info, struct pxafb_info, fb);
	unsigned int val;
	int ret = 1;

	/*
	 * If inverse mode was selected, invert all the colours
	 * rather than the register number.  The register number
	 * is what you poke into the framebuffer to produce the
	 * colour you requested.
	 */
	if (fbi->cmap_inverse) {
		red   = 0xffff - red;
		green = 0xffff - green;
		blue  = 0xffff - blue;
	}

	/*
	 * If greyscale is true, then we convert the RGB value
	 * to greyscale no matter what visual we are using.
	 */
	if (fbi->fb.var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
					7471 * blue) >> 16;

	switch (fbi->fb.fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/*
		 * 16-bit True Colour.  We encode the RGB value
		 * according to the RGB bitfield information.
		 */
		if (regno < 16) {
			u32 *pal = fbi->fb.pseudo_palette;

			val  = chan_to_field(red, &fbi->fb.var.red);
			val |= chan_to_field(green, &fbi->fb.var.green);
			val |= chan_to_field(blue, &fbi->fb.var.blue);

			pal[regno] = val;
			ret = 0;
		}
		break;

	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		ret = pxafb_setpalettereg(regno, red, green, blue, trans, info);
		break;
	}

	return ret;
}

/* calculate pixel depth, transparency bit included, >=16bpp formats _only_ */
static inline int var_to_depth(struct fb_var_screeninfo *var)
{
	return var->red.length + var->green.length +
		var->blue.length + var->transp.length;
}

/* calculate 4-bit BPP value for LCCR3 and OVLxC1 */
static int pxafb_var_to_bpp(struct fb_var_screeninfo *var)
{
	int bpp = -EINVAL;

	switch (var->bits_per_pixel) {
	case 1:  bpp = 0; break;
	case 2:  bpp = 1; break;
	case 4:  bpp = 2; break;
	case 8:  bpp = 3; break;
	case 16: bpp = 4; break;
	case 24:
		switch (var_to_depth(var)) {
		case 18: bpp = 6; break; /* 18-bits/pixel packed */
		case 19: bpp = 8; break; /* 19-bits/pixel packed */
		case 24: bpp = 9; break;
		}
		break;
	case 32:
		switch (var_to_depth(var)) {
		case 18: bpp = 5; break; /* 18-bits/pixel unpacked */
		case 19: bpp = 7; break; /* 19-bits/pixel unpacked */
		case 25: bpp = 10; break;
		}
		break;
	}
	return bpp;
}

/*
 *  pxafb_var_to_lccr3():
 *    Convert a bits per pixel value to the correct bit pattern for LCCR3
 *
 *  NOTE: for PXA27x with overlays support, the LCCR3_PDFOR_x bits have an
 *  implication of the acutal use of transparency bit,  which we handle it
 *  here separatedly. See PXA27x Developer's Manual, Section <<7.4.6 Pixel
 *  Formats>> for the valid combination of PDFOR, PAL_FOR for various BPP.
 *
 *  Transparency for palette pixel formats is not supported at the moment.
 */
static uint32_t pxafb_var_to_lccr3(struct fb_var_screeninfo *var)
{
	int bpp = pxafb_var_to_bpp(var);
	uint32_t lccr3;

	if (bpp < 0)
		return 0;

	lccr3 = LCCR3_BPP(bpp);

	switch (var_to_depth(var)) {
	case 16: lccr3 |= var->transp.length ? LCCR3_PDFOR_3 : 0; break;
	case 18: lccr3 |= LCCR3_PDFOR_3; break;
	case 24: lccr3 |= var->transp.length ? LCCR3_PDFOR_2 : LCCR3_PDFOR_3;
		 break;
	case 19:
	case 25: lccr3 |= LCCR3_PDFOR_0; break;
	}
	return lccr3;
}

#define SET_PIXFMT(v, r, g, b, t)				\
({								\
	(v)->transp.offset = (t) ? (r) + (g) + (b) : 0;		\
	(v)->transp.length = (t) ? (t) : 0;			\
	(v)->blue.length   = (b); (v)->blue.offset = 0;		\
	(v)->green.length  = (g); (v)->green.offset = (b);	\
	(v)->red.length    = (r); (v)->red.offset = (b) + (g);	\
})

/* set the RGBT bitfields of fb_var_screeninf according to
 * var->bits_per_pixel and given depth
 */
static void pxafb_set_pixfmt(struct fb_var_screeninfo *var, int depth)
{
	if (depth == 0)
		depth = var->bits_per_pixel;

	if (var->bits_per_pixel < 16) {
		/* indexed pixel formats */
		var->red.offset    = 0; var->red.length    = 8;
		var->green.offset  = 0; var->green.length  = 8;
		var->blue.offset   = 0; var->blue.length   = 8;
		var->transp.offset = 0; var->transp.length = 8;
	}

	switch (depth) {
	case 16: var->transp.length ?
		 SET_PIXFMT(var, 5, 5, 5, 1) :		/* RGBT555 */
		 SET_PIXFMT(var, 5, 6, 5, 0); break;	/* RGB565 */
	case 18: SET_PIXFMT(var, 6, 6, 6, 0); break;	/* RGB666 */
	case 19: SET_PIXFMT(var, 6, 6, 6, 1); break;	/* RGBT666 */
	case 24: var->transp.length ?
		 SET_PIXFMT(var, 8, 8, 7, 1) :		/* RGBT887 */
		 SET_PIXFMT(var, 8, 8, 8, 0); break;	/* RGB888 */
	case 25: SET_PIXFMT(var, 8, 8, 8, 1); break;	/* RGBT888 */
	}
}

#ifdef CONFIG_CPU_FREQ
/*
 *  pxafb_display_dma_period()
 *    Calculate the minimum period (in picoseconds) between two DMA
 *    requests for the LCD controller.  If we hit this, it means we're
 *    doing nothing but LCD DMA.
 */
static unsigned int pxafb_display_dma_period(struct fb_var_screeninfo *var)
{
	/*
	 * Period = pixclock * bits_per_byte * bytes_per_transfer
	 *              / memory_bits_per_pixel;
	 */
	return var->pixclock * 8 * 16 / var->bits_per_pixel;
}
#endif

/*
 * Select the smallest mode that allows the desired resolution to be
 * displayed. If desired parameters can be rounded up.
 */
static struct pxafb_mode_info *pxafb_getmode(struct pxafb_mach_info *mach,
					     struct fb_var_screeninfo *var)
{
	struct pxafb_mode_info *mode = NULL;
	struct pxafb_mode_info *modelist = mach->modes;
	unsigned int best_x = 0xffffffff, best_y = 0xffffffff;
	unsigned int i;

	for (i = 0; i < mach->num_modes; i++) {
		if (modelist[i].xres >= var->xres &&
		    modelist[i].yres >= var->yres &&
		    modelist[i].xres < best_x &&
		    modelist[i].yres < best_y &&
		    modelist[i].bpp >= var->bits_per_pixel) {
			best_x = modelist[i].xres;
			best_y = modelist[i].yres;
			mode = &modelist[i];
		}
	}

	return mode;
}

static void pxafb_setmode(struct fb_var_screeninfo *var,
			  struct pxafb_mode_info *mode)
{
	var->xres		= mode->xres;
	var->yres		= mode->yres;
	var->bits_per_pixel	= mode->bpp;
	var->pixclock		= mode->pixclock;
	var->hsync_len		= mode->hsync_len;
	var->left_margin	= mode->left_margin;
	var->right_margin	= mode->right_margin;
	var->vsync_len		= mode->vsync_len;
	var->upper_margin	= mode->upper_margin;
	var->lower_margin	= mode->lower_margin;
	var->sync		= mode->sync;
	var->grayscale		= mode->cmap_greyscale;
	var->transp.length	= mode->transparency;

	/* set the initial RGBA bitfields */
	pxafb_set_pixfmt(var, mode->depth);
}

static int pxafb_adjust_timing(struct pxafb_info *fbi,
			       struct fb_var_screeninfo *var)
{
	int line_length;

	var->xres = max_t(int, var->xres, MIN_XRES);
	var->yres = max_t(int, var->yres, MIN_YRES);

	if (!(fbi->lccr0 & LCCR0_LCDT)) {
		clamp_val(var->hsync_len, 1, 64);
		clamp_val(var->vsync_len, 1, 64);
		clamp_val(var->left_margin,  1, 255);
		clamp_val(var->right_margin, 1, 255);
		clamp_val(var->upper_margin, 1, 255);
		clamp_val(var->lower_margin, 1, 255);
	}

	/* make sure each line is aligned on word boundary */
	line_length = var->xres * var->bits_per_pixel / 8;
	line_length = ALIGN(line_length, 4);
	var->xres = line_length * 8 / var->bits_per_pixel;

	/* we don't support xpan, force xres_virtual to be equal to xres */
	var->xres_virtual = var->xres;

	if (var->accel_flags & FB_ACCELF_TEXT)
		var->yres_virtual = fbi->fb.fix.smem_len / line_length;
	else
		var->yres_virtual = max(var->yres_virtual, var->yres);

	/* check for limits */
	if (var->xres > MAX_XRES || var->yres > MAX_YRES)
		return -EINVAL;

	if (var->yres > var->yres_virtual)
		return -EINVAL;

	return 0;
}

/*
 *  pxafb_check_var():
 *    Get the video params out of 'var'. If a value doesn't fit, round it up,
 *    if it's too big, return -EINVAL.
 *
 *    Round up in the following order: bits_per_pixel, xres,
 *    yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
 *    bitfields, horizontal timing, vertical timing.
 */
static int pxafb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct pxafb_info *fbi = container_of(info, struct pxafb_info, fb);
	struct pxafb_mach_info *inf = fbi->inf;
	int err;

	if (inf->fixed_modes) {
		struct pxafb_mode_info *mode;

		mode = pxafb_getmode(inf, var);
		if (!mode)
			return -EINVAL;
		pxafb_setmode(var, mode);
	}

	/* do a test conversion to BPP fields to check the color formats */
	err = pxafb_var_to_bpp(var);
	if (err < 0)
		return err;

	pxafb_set_pixfmt(var, var_to_depth(var));

	err = pxafb_adjust_timing(fbi, var);
	if (err)
		return err;

#ifdef CONFIG_CPU_FREQ
	pr_debug("pxafb: dma period = %d ps\n",
		 pxafb_display_dma_period(var));
#endif

	return 0;
}

/*
 * pxafb_set_par():
 *	Set the user defined part of the display for the specified console
 */
static int pxafb_set_par(struct fb_info *info)
{
	struct pxafb_info *fbi = container_of(info, struct pxafb_info, fb);
	struct fb_var_screeninfo *var = &info->var;

	if (var->bits_per_pixel >= 16)
		fbi->fb.fix.visual = FB_VISUAL_TRUECOLOR;
	else if (!fbi->cmap_static)
		fbi->fb.fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else {
		/*
		 * Some people have weird ideas about wanting static
		 * pseudocolor maps.  I suspect their user space
		 * applications are broken.
		 */
		fbi->fb.fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
	}

	fbi->fb.fix.line_length = var->xres_virtual *
				  var->bits_per_pixel / 8;
	if (var->bits_per_pixel >= 16)
		fbi->palette_size = 0;
	else
		fbi->palette_size = var->bits_per_pixel == 1 ?
					4 : 1 << var->bits_per_pixel;

	fbi->palette_cpu = (u16 *)&fbi->dma_buff->palette[0];

	if (fbi->fb.var.bits_per_pixel >= 16)
		fb_dealloc_cmap(&fbi->fb.cmap);
	else
		fb_alloc_cmap(&fbi->fb.cmap, 1<<fbi->fb.var.bits_per_pixel, 0);

	pxafb_activate_var(var, fbi);

	return 0;
}

static int pxafb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct pxafb_info *fbi = container_of(info, struct pxafb_info, fb);
	struct fb_var_screeninfo newvar;
	int dma = DMA_MAX + DMA_BASE;

	if (fbi->state != C_ENABLE)
		return 0;

	/* Only take .xoffset, .yoffset and .vmode & FB_VMODE_YWRAP from what
	 * was passed in and copy the rest from the old screeninfo.
	 */
	memcpy(&newvar, &fbi->fb.var, sizeof(newvar));
	newvar.xoffset = var->xoffset;
	newvar.yoffset = var->yoffset;
	newvar.vmode &= ~FB_VMODE_YWRAP;
	newvar.vmode |= var->vmode & FB_VMODE_YWRAP;

	setup_base_frame(fbi, &newvar, 1);

	if (fbi->lccr0 & LCCR0_SDS)
		lcd_writel(fbi, FBR1, fbi->fdadr[dma + 1] | 0x1);

	lcd_writel(fbi, FBR0, fbi->fdadr[dma] | 0x1);
	return 0;
}

/*
 * pxafb_blank():
 *	Blank the display by setting all palette values to zero.  Note, the
 * 	16 bpp mode does not really use the palette, so this will not
 *      blank the display in all modes.
 */
static int pxafb_blank(int blank, struct fb_info *info)
{
	struct pxafb_info *fbi = container_of(info, struct pxafb_info, fb);
	int i;

	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		if (fbi->fb.fix.visual == FB_VISUAL_PSEUDOCOLOR ||
		    fbi->fb.fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR)
			for (i = 0; i < fbi->palette_size; i++)
				pxafb_setpalettereg(i, 0, 0, 0, 0, info);

		pxafb_schedule_work(fbi, C_DISABLE);
		/* TODO if (pxafb_blank_helper) pxafb_blank_helper(blank); */
		break;

	case FB_BLANK_UNBLANK:
		/* TODO if (pxafb_blank_helper) pxafb_blank_helper(blank); */
		if (fbi->fb.fix.visual == FB_VISUAL_PSEUDOCOLOR ||
		    fbi->fb.fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR)
			fb_set_cmap(&fbi->fb.cmap, info);
		pxafb_schedule_work(fbi, C_ENABLE);
	}
	return 0;
}

static struct fb_ops pxafb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= pxafb_check_var,
	.fb_set_par	= pxafb_set_par,
	.fb_pan_display	= pxafb_pan_display,
	.fb_setcolreg	= pxafb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_blank	= pxafb_blank,
};

#ifdef CONFIG_FB_PXA_OVERLAY
static void overlay1fb_setup(struct pxafb_layer *ofb)
{
	int size = ofb->fb.fix.line_length * ofb->fb.var.yres_virtual;
	unsigned long start = ofb->video_mem_phys;
	setup_frame_dma(ofb->fbi, DMA_OV1, PAL_NONE, start, size);
}

/* Depending on the enable status of overlay1/2, the DMA should be
 * updated from FDADRx (when disabled) or FBRx (when enabled).
 */
static void overlay1fb_enable(struct pxafb_layer *ofb)
{
	int enabled = lcd_readl(ofb->fbi, OVL1C1) & OVLxC1_OEN;
	uint32_t fdadr1 = ofb->fbi->fdadr[DMA_OV1] | (enabled ? 0x1 : 0);

	lcd_writel(ofb->fbi, enabled ? FBR1 : FDADR1, fdadr1);
	lcd_writel(ofb->fbi, OVL1C2, ofb->control[1]);
	lcd_writel(ofb->fbi, OVL1C1, ofb->control[0] | OVLxC1_OEN);
}

static void overlay1fb_disable(struct pxafb_layer *ofb)
{
	uint32_t lccr5;

	if (!(lcd_readl(ofb->fbi, OVL1C1) & OVLxC1_OEN))
		return;

	lccr5 = lcd_readl(ofb->fbi, LCCR5);

	lcd_writel(ofb->fbi, OVL1C1, ofb->control[0] & ~OVLxC1_OEN);

	lcd_writel(ofb->fbi, LCSR1, LCSR1_BS(1));
	lcd_writel(ofb->fbi, LCCR5, lccr5 & ~LCSR1_BS(1));
	lcd_writel(ofb->fbi, FBR1, ofb->fbi->fdadr[DMA_OV1] | 0x3);

	if (wait_for_completion_timeout(&ofb->branch_done, 1 * HZ) == 0)
		pr_warn("%s: timeout disabling overlay1\n", __func__);

	lcd_writel(ofb->fbi, LCCR5, lccr5);
}

static void overlay2fb_setup(struct pxafb_layer *ofb)
{
	int size, div = 1, pfor = NONSTD_TO_PFOR(ofb->fb.var.nonstd);
	unsigned long start[3] = { ofb->video_mem_phys, 0, 0 };

	if (pfor == OVERLAY_FORMAT_RGB || pfor == OVERLAY_FORMAT_YUV444_PACKED) {
		size = ofb->fb.fix.line_length * ofb->fb.var.yres_virtual;
		setup_frame_dma(ofb->fbi, DMA_OV2_Y, -1, start[0], size);
	} else {
		size = ofb->fb.var.xres_virtual * ofb->fb.var.yres_virtual;
		switch (pfor) {
		case OVERLAY_FORMAT_YUV444_PLANAR: div = 1; break;
		case OVERLAY_FORMAT_YUV422_PLANAR: div = 2; break;
		case OVERLAY_FORMAT_YUV420_PLANAR: div = 4; break;
		}
		start[1] = start[0] + size;
		start[2] = start[1] + size / div;
		setup_frame_dma(ofb->fbi, DMA_OV2_Y,  -1, start[0], size);
		setup_frame_dma(ofb->fbi, DMA_OV2_Cb, -1, start[1], size / div);
		setup_frame_dma(ofb->fbi, DMA_OV2_Cr, -1, start[2], size / div);
	}
}

static void overlay2fb_enable(struct pxafb_layer *ofb)
{
	int pfor = NONSTD_TO_PFOR(ofb->fb.var.nonstd);
	int enabled = lcd_readl(ofb->fbi, OVL2C1) & OVLxC1_OEN;
	uint32_t fdadr2 = ofb->fbi->fdadr[DMA_OV2_Y]  | (enabled ? 0x1 : 0);
	uint32_t fdadr3 = ofb->fbi->fdadr[DMA_OV2_Cb] | (enabled ? 0x1 : 0);
	uint32_t fdadr4 = ofb->fbi->fdadr[DMA_OV2_Cr] | (enabled ? 0x1 : 0);

	if (pfor == OVERLAY_FORMAT_RGB || pfor == OVERLAY_FORMAT_YUV444_PACKED)
		lcd_writel(ofb->fbi, enabled ? FBR2 : FDADR2, fdadr2);
	else {
		lcd_writel(ofb->fbi, enabled ? FBR2 : FDADR2, fdadr2);
		lcd_writel(ofb->fbi, enabled ? FBR3 : FDADR3, fdadr3);
		lcd_writel(ofb->fbi, enabled ? FBR4 : FDADR4, fdadr4);
	}
	lcd_writel(ofb->fbi, OVL2C2, ofb->control[1]);
	lcd_writel(ofb->fbi, OVL2C1, ofb->control[0] | OVLxC1_OEN);
}

static void overlay2fb_disable(struct pxafb_layer *ofb)
{
	uint32_t lccr5;

	if (!(lcd_readl(ofb->fbi, OVL2C1) & OVLxC1_OEN))
		return;

	lccr5 = lcd_readl(ofb->fbi, LCCR5);

	lcd_writel(ofb->fbi, OVL2C1, ofb->control[0] & ~OVLxC1_OEN);

	lcd_writel(ofb->fbi, LCSR1, LCSR1_BS(2));
	lcd_writel(ofb->fbi, LCCR5, lccr5 & ~LCSR1_BS(2));
	lcd_writel(ofb->fbi, FBR2, ofb->fbi->fdadr[DMA_OV2_Y]  | 0x3);
	lcd_writel(ofb->fbi, FBR3, ofb->fbi->fdadr[DMA_OV2_Cb] | 0x3);
	lcd_writel(ofb->fbi, FBR4, ofb->fbi->fdadr[DMA_OV2_Cr] | 0x3);

	if (wait_for_completion_timeout(&ofb->branch_done, 1 * HZ) == 0)
		pr_warn("%s: timeout disabling overlay2\n", __func__);
}

static struct pxafb_layer_ops ofb_ops[] = {
	[0] = {
		.enable		= overlay1fb_enable,
		.disable	= overlay1fb_disable,
		.setup		= overlay1fb_setup,
	},
	[1] = {
		.enable		= overlay2fb_enable,
		.disable	= overlay2fb_disable,
		.setup		= overlay2fb_setup,
	},
};

static int overlayfb_open(struct fb_info *info, int user)
{
	struct pxafb_layer *ofb = container_of(info, struct pxafb_layer, fb);

	/* no support for framebuffer console on overlay */
	if (user == 0)
		return -ENODEV;

	if (ofb->usage++ == 0) {
		/* unblank the base framebuffer */
		console_lock();
		fb_blank(&ofb->fbi->fb, FB_BLANK_UNBLANK);
		console_unlock();
	}

	return 0;
}

static int overlayfb_release(struct fb_info *info, int user)
{
	struct pxafb_layer *ofb = container_of(info, struct pxafb_layer, fb);

	if (ofb->usage == 1) {
		ofb->ops->disable(ofb);
		ofb->fb.var.height	= -1;
		ofb->fb.var.width	= -1;
		ofb->fb.var.xres = ofb->fb.var.xres_virtual = 0;
		ofb->fb.var.yres = ofb->fb.var.yres_virtual = 0;

		ofb->usage--;
	}
	return 0;
}

static int overlayfb_check_var(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	struct pxafb_layer *ofb = container_of(info, struct pxafb_layer, fb);
	struct fb_var_screeninfo *base_var = &ofb->fbi->fb.var;
	int xpos, ypos, pfor, bpp;

	xpos = NONSTD_TO_XPOS(var->nonstd);
	ypos = NONSTD_TO_YPOS(var->nonstd);
	pfor = NONSTD_TO_PFOR(var->nonstd);

	bpp = pxafb_var_to_bpp(var);
	if (bpp < 0)
		return -EINVAL;

	/* no support for YUV format on overlay1 */
	if (ofb->id == OVERLAY1 && pfor != 0)
		return -EINVAL;

	/* for YUV packed formats, bpp = 'minimum bpp of YUV components' */
	switch (pfor) {
	case OVERLAY_FORMAT_RGB:
		bpp = pxafb_var_to_bpp(var);
		if (bpp < 0)
			return -EINVAL;

		pxafb_set_pixfmt(var, var_to_depth(var));
		break;
	case OVERLAY_FORMAT_YUV444_PACKED: bpp = 24; break;
	case OVERLAY_FORMAT_YUV444_PLANAR: bpp = 8; break;
	case OVERLAY_FORMAT_YUV422_PLANAR: bpp = 4; break;
	case OVERLAY_FORMAT_YUV420_PLANAR: bpp = 2; break;
	default:
		return -EINVAL;
	}

	/* each line must start at a 32-bit word boundary */
	if ((xpos * bpp) % 32)
		return -EINVAL;

	/* xres must align on 32-bit word boundary */
	var->xres = roundup(var->xres * bpp, 32) / bpp;

	if ((xpos + var->xres > base_var->xres) ||
	    (ypos + var->yres > base_var->yres))
		return -EINVAL;

	var->xres_virtual = var->xres;
	var->yres_virtual = max(var->yres, var->yres_virtual);
	return 0;
}

static int overlayfb_check_video_memory(struct pxafb_layer *ofb)
{
	struct fb_var_screeninfo *var = &ofb->fb.var;
	int pfor = NONSTD_TO_PFOR(var->nonstd);
	int size, bpp = 0;

	switch (pfor) {
	case OVERLAY_FORMAT_RGB: bpp = var->bits_per_pixel; break;
	case OVERLAY_FORMAT_YUV444_PACKED: bpp = 24; break;
	case OVERLAY_FORMAT_YUV444_PLANAR: bpp = 24; break;
	case OVERLAY_FORMAT_YUV422_PLANAR: bpp = 16; break;
	case OVERLAY_FORMAT_YUV420_PLANAR: bpp = 12; break;
	}

	ofb->fb.fix.line_length = var->xres_virtual * bpp / 8;

	size = PAGE_ALIGN(ofb->fb.fix.line_length * var->yres_virtual);

	if (ofb->video_mem) {
		if (ofb->video_mem_size >= size)
			return 0;
	}
	return -EINVAL;
}

static int overlayfb_set_par(struct fb_info *info)
{
	struct pxafb_layer *ofb = container_of(info, struct pxafb_layer, fb);
	struct fb_var_screeninfo *var = &info->var;
	int xpos, ypos, pfor, bpp, ret;

	ret = overlayfb_check_video_memory(ofb);
	if (ret)
		return ret;

	bpp  = pxafb_var_to_bpp(var);
	xpos = NONSTD_TO_XPOS(var->nonstd);
	ypos = NONSTD_TO_YPOS(var->nonstd);
	pfor = NONSTD_TO_PFOR(var->nonstd);

	ofb->control[0] = OVLxC1_PPL(var->xres) | OVLxC1_LPO(var->yres) |
			  OVLxC1_BPP(bpp);
	ofb->control[1] = OVLxC2_XPOS(xpos) | OVLxC2_YPOS(ypos);

	if (ofb->id == OVERLAY2)
		ofb->control[1] |= OVL2C2_PFOR(pfor);

	ofb->ops->setup(ofb);
	ofb->ops->enable(ofb);
	return 0;
}

static struct fb_ops overlay_fb_ops = {
	.owner			= THIS_MODULE,
	.fb_open		= overlayfb_open,
	.fb_release		= overlayfb_release,
	.fb_check_var 		= overlayfb_check_var,
	.fb_set_par		= overlayfb_set_par,
};

static void init_pxafb_overlay(struct pxafb_info *fbi, struct pxafb_layer *ofb,
			       int id)
{
	sprintf(ofb->fb.fix.id, "overlay%d", id + 1);

	ofb->fb.fix.type		= FB_TYPE_PACKED_PIXELS;
	ofb->fb.fix.xpanstep		= 0;
	ofb->fb.fix.ypanstep		= 1;

	ofb->fb.var.activate		= FB_ACTIVATE_NOW;
	ofb->fb.var.height		= -1;
	ofb->fb.var.width		= -1;
	ofb->fb.var.vmode		= FB_VMODE_NONINTERLACED;

	ofb->fb.fbops			= &overlay_fb_ops;
	ofb->fb.flags			= FBINFO_FLAG_DEFAULT;
	ofb->fb.node			= -1;
	ofb->fb.pseudo_palette		= NULL;

	ofb->id = id;
	ofb->ops = &ofb_ops[id];
	ofb->usage = 0;
	ofb->fbi = fbi;
	init_completion(&ofb->branch_done);
}

static inline int pxafb_overlay_supported(void)
{
	if (cpu_is_pxa27x() || cpu_is_pxa3xx())
		return 1;

	return 0;
}

static int pxafb_overlay_map_video_memory(struct pxafb_info *pxafb,
					  struct pxafb_layer *ofb)
{
	/* We assume that user will use at most video_mem_size for overlay fb,
	 * anyway, it's useless to use 16bpp main plane and 24bpp overlay
	 */
	ofb->video_mem = alloc_pages_exact(PAGE_ALIGN(pxafb->video_mem_size),
		GFP_KERNEL | __GFP_ZERO);
	if (ofb->video_mem == NULL)
		return -ENOMEM;

	ofb->video_mem_phys = virt_to_phys(ofb->video_mem);
	ofb->video_mem_size = PAGE_ALIGN(pxafb->video_mem_size);

	mutex_lock(&ofb->fb.mm_lock);
	ofb->fb.fix.smem_start	= ofb->video_mem_phys;
	ofb->fb.fix.smem_len	= pxafb->video_mem_size;
	mutex_unlock(&ofb->fb.mm_lock);

	ofb->fb.screen_base	= ofb->video_mem;

	return 0;
}

static void pxafb_overlay_init(struct pxafb_info *fbi)
{
	int i, ret;

	if (!pxafb_overlay_supported())
		return;

	for (i = 0; i < 2; i++) {
		struct pxafb_layer *ofb = &fbi->overlay[i];
		init_pxafb_overlay(fbi, ofb, i);
		ret = register_framebuffer(&ofb->fb);
		if (ret) {
			dev_err(fbi->dev, "failed to register overlay %d\n", i);
			continue;
		}
		ret = pxafb_overlay_map_video_memory(fbi, ofb);
		if (ret) {
			dev_err(fbi->dev,
				"failed to map video memory for overlay %d\n",
				i);
			unregister_framebuffer(&ofb->fb);
			continue;
		}
		ofb->registered = 1;
	}

	/* mask all IU/BS/EOF/SOF interrupts */
	lcd_writel(fbi, LCCR5, ~0);

	pr_info("PXA Overlay driver loaded successfully!\n");
}

static void pxafb_overlay_exit(struct pxafb_info *fbi)
{
	int i;

	if (!pxafb_overlay_supported())
		return;

	for (i = 0; i < 2; i++) {
		struct pxafb_layer *ofb = &fbi->overlay[i];
		if (ofb->registered) {
			if (ofb->video_mem)
				free_pages_exact(ofb->video_mem,
					ofb->video_mem_size);
			unregister_framebuffer(&ofb->fb);
		}
	}
}
#else
static inline void pxafb_overlay_init(struct pxafb_info *fbi) {}
static inline void pxafb_overlay_exit(struct pxafb_info *fbi) {}
#endif /* CONFIG_FB_PXA_OVERLAY */

/*
 * Calculate the PCD value from the clock rate (in picoseconds).
 * We take account of the PPCR clock setting.
 * From PXA Developer's Manual:
 *
 *   PixelClock =      LCLK
 *                -------------
 *                2 ( PCD + 1 )
 *
 *   PCD =      LCLK
 *         ------------- - 1
 *         2(PixelClock)
 *
 * Where:
 *   LCLK = LCD/Memory Clock
 *   PCD = LCCR3[7:0]
 *
 * PixelClock here is in Hz while the pixclock argument given is the
 * period in picoseconds. Hence PixelClock = 1 / ( pixclock * 10^-12 )
 *
 * The function get_lclk_frequency_10khz returns LCLK in units of
 * 10khz. Calling the result of this function lclk gives us the
 * following
 *
 *    PCD = (lclk * 10^4 ) * ( pixclock * 10^-12 )
 *          -------------------------------------- - 1
 *                          2
 *
 * Factoring the 10^4 and 10^-12 out gives 10^-8 == 1 / 100000000 as used below.
 */
static inline unsigned int get_pcd(struct pxafb_info *fbi,
				   unsigned int pixclock)
{
	unsigned long long pcd;

	/* FIXME: Need to take into account Double Pixel Clock mode
	 * (DPC) bit? or perhaps set it based on the various clock
	 * speeds */
	pcd = (unsigned long long)(clk_get_rate(fbi->clk) / 10000);
	pcd *= pixclock;
	do_div(pcd, 100000000 * 2);
	/* no need for this, since we should subtract 1 anyway. they cancel */
	/* pcd += 1; */ /* make up for integer math truncations */
	return (unsigned int)pcd;
}

/*
 * Some touchscreens need hsync information from the video driver to
 * function correctly. We export it here.  Note that 'hsync_time' and
 * the value returned from pxafb_get_hsync_time() is the *reciprocal*
 * of the hsync period in seconds.
 */
static inline void set_hsync_time(struct pxafb_info *fbi, unsigned int pcd)
{
	unsigned long htime;

	if ((pcd == 0) || (fbi->fb.var.hsync_len == 0)) {
		fbi->hsync_time = 0;
		return;
	}

	htime = clk_get_rate(fbi->clk) / (pcd * fbi->fb.var.hsync_len);

	fbi->hsync_time = htime;
}

unsigned long pxafb_get_hsync_time(struct device *dev)
{
	struct pxafb_info *fbi = dev_get_drvdata(dev);

	/* If display is blanked/suspended, hsync isn't active */
	if (!fbi || (fbi->state != C_ENABLE))
		return 0;

	return fbi->hsync_time;
}
EXPORT_SYMBOL(pxafb_get_hsync_time);

static int setup_frame_dma(struct pxafb_info *fbi, int dma, int pal,
			   unsigned long start, size_t size)
{
	struct pxafb_dma_descriptor *dma_desc, *pal_desc;
	unsigned int dma_desc_off, pal_desc_off;

	if (dma < 0 || dma >= DMA_MAX * 2)
		return -EINVAL;

	dma_desc = &fbi->dma_buff->dma_desc[dma];
	dma_desc_off = offsetof(struct pxafb_dma_buff, dma_desc[dma]);

	dma_desc->fsadr = start;
	dma_desc->fidr  = 0;
	dma_desc->ldcmd = size;

	if (pal < 0 || pal >= PAL_MAX * 2) {
		dma_desc->fdadr = fbi->dma_buff_phys + dma_desc_off;
		fbi->fdadr[dma] = fbi->dma_buff_phys + dma_desc_off;
	} else {
		pal_desc = &fbi->dma_buff->pal_desc[pal];
		pal_desc_off = offsetof(struct pxafb_dma_buff, pal_desc[pal]);

		pal_desc->fsadr = fbi->dma_buff_phys + pal * PALETTE_SIZE;
		pal_desc->fidr  = 0;

		if ((fbi->lccr4 & LCCR4_PAL_FOR_MASK) == LCCR4_PAL_FOR_0)
			pal_desc->ldcmd = fbi->palette_size * sizeof(u16);
		else
			pal_desc->ldcmd = fbi->palette_size * sizeof(u32);

		pal_desc->ldcmd |= LDCMD_PAL;

		/* flip back and forth between palette and frame buffer */
		pal_desc->fdadr = fbi->dma_buff_phys + dma_desc_off;
		dma_desc->fdadr = fbi->dma_buff_phys + pal_desc_off;
		fbi->fdadr[dma] = fbi->dma_buff_phys + dma_desc_off;
	}

	return 0;
}

static void setup_base_frame(struct pxafb_info *fbi,
                             struct fb_var_screeninfo *var,
                             int branch)
{
	struct fb_fix_screeninfo *fix = &fbi->fb.fix;
	int nbytes, dma, pal, bpp = var->bits_per_pixel;
	unsigned long offset;

	dma = DMA_BASE + (branch ? DMA_MAX : 0);
	pal = (bpp >= 16) ? PAL_NONE : PAL_BASE + (branch ? PAL_MAX : 0);

	nbytes = fix->line_length * var->yres;
	offset = fix->line_length * var->yoffset + fbi->video_mem_phys;

	if (fbi->lccr0 & LCCR0_SDS) {
		nbytes = nbytes / 2;
		setup_frame_dma(fbi, dma + 1, PAL_NONE, offset + nbytes, nbytes);
	}

	setup_frame_dma(fbi, dma, pal, offset, nbytes);
}

#ifdef CONFIG_FB_PXA_SMARTPANEL
static int setup_smart_dma(struct pxafb_info *fbi)
{
	struct pxafb_dma_descriptor *dma_desc;
	unsigned long dma_desc_off, cmd_buff_off;

	dma_desc = &fbi->dma_buff->dma_desc[DMA_CMD];
	dma_desc_off = offsetof(struct pxafb_dma_buff, dma_desc[DMA_CMD]);
	cmd_buff_off = offsetof(struct pxafb_dma_buff, cmd_buff);

	dma_desc->fdadr = fbi->dma_buff_phys + dma_desc_off;
	dma_desc->fsadr = fbi->dma_buff_phys + cmd_buff_off;
	dma_desc->fidr  = 0;
	dma_desc->ldcmd = fbi->n_smart_cmds * sizeof(uint16_t);

	fbi->fdadr[DMA_CMD] = dma_desc->fdadr;
	return 0;
}

int pxafb_smart_flush(struct fb_info *info)
{
	struct pxafb_info *fbi = container_of(info, struct pxafb_info, fb);
	uint32_t prsr;
	int ret = 0;

	/* disable controller until all registers are set up */
	lcd_writel(fbi, LCCR0, fbi->reg_lccr0 & ~LCCR0_ENB);

	/* 1. make it an even number of commands to align on 32-bit boundary
	 * 2. add the interrupt command to the end of the chain so we can
	 *    keep track of the end of the transfer
	 */

	while (fbi->n_smart_cmds & 1)
		fbi->smart_cmds[fbi->n_smart_cmds++] = SMART_CMD_NOOP;

	fbi->smart_cmds[fbi->n_smart_cmds++] = SMART_CMD_INTERRUPT;
	fbi->smart_cmds[fbi->n_smart_cmds++] = SMART_CMD_WAIT_FOR_VSYNC;
	setup_smart_dma(fbi);

	/* continue to execute next command */
	prsr = lcd_readl(fbi, PRSR) | PRSR_ST_OK | PRSR_CON_NT;
	lcd_writel(fbi, PRSR, prsr);

	/* stop the processor in case it executed "wait for sync" cmd */
	lcd_writel(fbi, CMDCR, 0x0001);

	/* don't send interrupts for fifo underruns on channel 6 */
	lcd_writel(fbi, LCCR5, LCCR5_IUM(6));

	lcd_writel(fbi, LCCR1, fbi->reg_lccr1);
	lcd_writel(fbi, LCCR2, fbi->reg_lccr2);
	lcd_writel(fbi, LCCR3, fbi->reg_lccr3);
	lcd_writel(fbi, LCCR4, fbi->reg_lccr4);
	lcd_writel(fbi, FDADR0, fbi->fdadr[0]);
	lcd_writel(fbi, FDADR6, fbi->fdadr[6]);

	/* begin sending */
	lcd_writel(fbi, LCCR0, fbi->reg_lccr0 | LCCR0_ENB);

	if (wait_for_completion_timeout(&fbi->command_done, HZ/2) == 0) {
		pr_warn("%s: timeout waiting for command done\n", __func__);
		ret = -ETIMEDOUT;
	}

	/* quick disable */
	prsr = lcd_readl(fbi, PRSR) & ~(PRSR_ST_OK | PRSR_CON_NT);
	lcd_writel(fbi, PRSR, prsr);
	lcd_writel(fbi, LCCR0, fbi->reg_lccr0 & ~LCCR0_ENB);
	lcd_writel(fbi, FDADR6, 0);
	fbi->n_smart_cmds = 0;
	return ret;
}

int pxafb_smart_queue(struct fb_info *info, uint16_t *cmds, int n_cmds)
{
	int i;
	struct pxafb_info *fbi = container_of(info, struct pxafb_info, fb);

	for (i = 0; i < n_cmds; i++, cmds++) {
		/* if it is a software delay, flush and delay */
		if ((*cmds & 0xff00) == SMART_CMD_DELAY) {
			pxafb_smart_flush(info);
			mdelay(*cmds & 0xff);
			continue;
		}

		/* leave 2 commands for INTERRUPT and WAIT_FOR_SYNC */
		if (fbi->n_smart_cmds == CMD_BUFF_SIZE - 8)
			pxafb_smart_flush(info);

		fbi->smart_cmds[fbi->n_smart_cmds++] = *cmds;
	}

	return 0;
}

static unsigned int __smart_timing(unsigned time_ns, unsigned long lcd_clk)
{
	unsigned int t = (time_ns * (lcd_clk / 1000000) / 1000);
	return (t == 0) ? 1 : t;
}

static void setup_smart_timing(struct pxafb_info *fbi,
				struct fb_var_screeninfo *var)
{
	struct pxafb_mach_info *inf = fbi->inf;
	struct pxafb_mode_info *mode = &inf->modes[0];
	unsigned long lclk = clk_get_rate(fbi->clk);
	unsigned t1, t2, t3, t4;

	t1 = max(mode->a0csrd_set_hld, mode->a0cswr_set_hld);
	t2 = max(mode->rd_pulse_width, mode->wr_pulse_width);
	t3 = mode->op_hold_time;
	t4 = mode->cmd_inh_time;

	fbi->reg_lccr1 =
		LCCR1_DisWdth(var->xres) |
		LCCR1_BegLnDel(__smart_timing(t1, lclk)) |
		LCCR1_EndLnDel(__smart_timing(t2, lclk)) |
		LCCR1_HorSnchWdth(__smart_timing(t3, lclk));

	fbi->reg_lccr2 = LCCR2_DisHght(var->yres);
	fbi->reg_lccr3 = fbi->lccr3 | LCCR3_PixClkDiv(__smart_timing(t4, lclk));
	fbi->reg_lccr3 |= (var->sync & FB_SYNC_HOR_HIGH_ACT) ? LCCR3_HSP : 0;
	fbi->reg_lccr3 |= (var->sync & FB_SYNC_VERT_HIGH_ACT) ? LCCR3_VSP : 0;

	/* FIXME: make this configurable */
	fbi->reg_cmdcr = 1;
}

static int pxafb_smart_thread(void *arg)
{
	struct pxafb_info *fbi = arg;
	struct pxafb_mach_info *inf = fbi->inf;

	if (!inf->smart_update) {
		pr_err("%s: not properly initialized, thread terminated\n",
				__func__);
		return -EINVAL;
	}

	pr_debug("%s(): task starting\n", __func__);

	set_freezable();
	while (!kthread_should_stop()) {

		if (try_to_freeze())
			continue;

		mutex_lock(&fbi->ctrlr_lock);

		if (fbi->state == C_ENABLE) {
			inf->smart_update(&fbi->fb);
			complete(&fbi->refresh_done);
		}

		mutex_unlock(&fbi->ctrlr_lock);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(30));
	}

	pr_debug("%s(): task ending\n", __func__);
	return 0;
}

static int pxafb_smart_init(struct pxafb_info *fbi)
{
	if (!(fbi->lccr0 & LCCR0_LCDT))
		return 0;

	fbi->smart_cmds = (uint16_t *) fbi->dma_buff->cmd_buff;
	fbi->n_smart_cmds = 0;

	init_completion(&fbi->command_done);
	init_completion(&fbi->refresh_done);

	fbi->smart_thread = kthread_run(pxafb_smart_thread, fbi,
					"lcd_refresh");
	if (IS_ERR(fbi->smart_thread)) {
		pr_err("%s: unable to create kernel thread\n", __func__);
		return PTR_ERR(fbi->smart_thread);
	}

	return 0;
}
#else
static inline int pxafb_smart_init(struct pxafb_info *fbi) { return 0; }
#endif /* CONFIG_FB_PXA_SMARTPANEL */

static void setup_parallel_timing(struct pxafb_info *fbi,
				  struct fb_var_screeninfo *var)
{
	unsigned int lines_per_panel, pcd = get_pcd(fbi, var->pixclock);

	fbi->reg_lccr1 =
		LCCR1_DisWdth(var->xres) +
		LCCR1_HorSnchWdth(var->hsync_len) +
		LCCR1_BegLnDel(var->left_margin) +
		LCCR1_EndLnDel(var->right_margin);

	/*
	 * If we have a dual scan LCD, we need to halve
	 * the YRES parameter.
	 */
	lines_per_panel = var->yres;
	if ((fbi->lccr0 & LCCR0_SDS) == LCCR0_Dual)
		lines_per_panel /= 2;

	fbi->reg_lccr2 =
		LCCR2_DisHght(lines_per_panel) +
		LCCR2_VrtSnchWdth(var->vsync_len) +
		LCCR2_BegFrmDel(var->upper_margin) +
		LCCR2_EndFrmDel(var->lower_margin);

	fbi->reg_lccr3 = fbi->lccr3 |
		(var->sync & FB_SYNC_HOR_HIGH_ACT ?
		 LCCR3_HorSnchH : LCCR3_HorSnchL) |
		(var->sync & FB_SYNC_VERT_HIGH_ACT ?
		 LCCR3_VrtSnchH : LCCR3_VrtSnchL);

	if (pcd) {
		fbi->reg_lccr3 |= LCCR3_PixClkDiv(pcd);
		set_hsync_time(fbi, pcd);
	}
}

/*
 * pxafb_activate_var():
 *	Configures LCD Controller based on entries in var parameter.
 *	Settings are only written to the controller if changes were made.
 */
static int pxafb_activate_var(struct fb_var_screeninfo *var,
			      struct pxafb_info *fbi)
{
	u_long flags;

	/* Update shadow copy atomically */
	local_irq_save(flags);

#ifdef CONFIG_FB_PXA_SMARTPANEL
	if (fbi->lccr0 & LCCR0_LCDT)
		setup_smart_timing(fbi, var);
	else
#endif
		setup_parallel_timing(fbi, var);

	setup_base_frame(fbi, var, 0);

	fbi->reg_lccr0 = fbi->lccr0 |
		(LCCR0_LDM | LCCR0_SFM | LCCR0_IUM | LCCR0_EFM |
		 LCCR0_QDM | LCCR0_BM  | LCCR0_OUM);

	fbi->reg_lccr3 |= pxafb_var_to_lccr3(var);

	fbi->reg_lccr4 = lcd_readl(fbi, LCCR4) & ~LCCR4_PAL_FOR_MASK;
	fbi->reg_lccr4 |= (fbi->lccr4 & LCCR4_PAL_FOR_MASK);
	local_irq_restore(flags);

	/*
	 * Only update the registers if the controller is enabled
	 * and something has changed.
	 */
	if ((lcd_readl(fbi, LCCR0) != fbi->reg_lccr0) ||
	    (lcd_readl(fbi, LCCR1) != fbi->reg_lccr1) ||
	    (lcd_readl(fbi, LCCR2) != fbi->reg_lccr2) ||
	    (lcd_readl(fbi, LCCR3) != fbi->reg_lccr3) ||
	    (lcd_readl(fbi, LCCR4) != fbi->reg_lccr4) ||
	    (lcd_readl(fbi, FDADR0) != fbi->fdadr[0]) ||
	    ((fbi->lccr0 & LCCR0_SDS) &&
	    (lcd_readl(fbi, FDADR1) != fbi->fdadr[1])))
		pxafb_schedule_work(fbi, C_REENABLE);

	return 0;
}

/*
 * NOTE!  The following functions are purely helpers for set_ctrlr_state.
 * Do not call them directly; set_ctrlr_state does the correct serialisation
 * to ensure that things happen in the right way 100% of time time.
 *	-- rmk
 */
static inline void __pxafb_backlight_power(struct pxafb_info *fbi, int on)
{
	pr_debug("pxafb: backlight o%s\n", on ? "n" : "ff");

	if (fbi->backlight_power)
		fbi->backlight_power(on);
}

static inline void __pxafb_lcd_power(struct pxafb_info *fbi, int on)
{
	pr_debug("pxafb: LCD power o%s\n", on ? "n" : "ff");

	if (fbi->lcd_power)
		fbi->lcd_power(on, &fbi->fb.var);
}

static void pxafb_enable_controller(struct pxafb_info *fbi)
{
	pr_debug("pxafb: Enabling LCD controller\n");
	pr_debug("fdadr0 0x%08x\n", (unsigned int) fbi->fdadr[0]);
	pr_debug("fdadr1 0x%08x\n", (unsigned int) fbi->fdadr[1]);
	pr_debug("reg_lccr0 0x%08x\n", (unsigned int) fbi->reg_lccr0);
	pr_debug("reg_lccr1 0x%08x\n", (unsigned int) fbi->reg_lccr1);
	pr_debug("reg_lccr2 0x%08x\n", (unsigned int) fbi->reg_lccr2);
	pr_debug("reg_lccr3 0x%08x\n", (unsigned int) fbi->reg_lccr3);

	/* enable LCD controller clock */
	if (clk_prepare_enable(fbi->clk)) {
		pr_err("%s: Failed to prepare clock\n", __func__);
		return;
	}

	if (fbi->lccr0 & LCCR0_LCDT)
		return;

	/* Sequence from 11.7.10 */
	lcd_writel(fbi, LCCR4, fbi->reg_lccr4);
	lcd_writel(fbi, LCCR3, fbi->reg_lccr3);
	lcd_writel(fbi, LCCR2, fbi->reg_lccr2);
	lcd_writel(fbi, LCCR1, fbi->reg_lccr1);
	lcd_writel(fbi, LCCR0, fbi->reg_lccr0 & ~LCCR0_ENB);

	lcd_writel(fbi, FDADR0, fbi->fdadr[0]);
	if (fbi->lccr0 & LCCR0_SDS)
		lcd_writel(fbi, FDADR1, fbi->fdadr[1]);
	lcd_writel(fbi, LCCR0, fbi->reg_lccr0 | LCCR0_ENB);
}

static void pxafb_disable_controller(struct pxafb_info *fbi)
{
	uint32_t lccr0;

#ifdef CONFIG_FB_PXA_SMARTPANEL
	if (fbi->lccr0 & LCCR0_LCDT) {
		wait_for_completion_timeout(&fbi->refresh_done,
				msecs_to_jiffies(200));
		return;
	}
#endif

	/* Clear LCD Status Register */
	lcd_writel(fbi, LCSR, 0xffffffff);

	lccr0 = lcd_readl(fbi, LCCR0) & ~LCCR0_LDM;
	lcd_writel(fbi, LCCR0, lccr0);
	lcd_writel(fbi, LCCR0, lccr0 | LCCR0_DIS);

	wait_for_completion_timeout(&fbi->disable_done, msecs_to_jiffies(200));

	/* disable LCD controller clock */
	clk_disable_unprepare(fbi->clk);
}

/*
 *  pxafb_handle_irq: Handle 'LCD DONE' interrupts.
 */
static irqreturn_t pxafb_handle_irq(int irq, void *dev_id)
{
	struct pxafb_info *fbi = dev_id;
	unsigned int lccr0, lcsr;

	lcsr = lcd_readl(fbi, LCSR);
	if (lcsr & LCSR_LDD) {
		lccr0 = lcd_readl(fbi, LCCR0);
		lcd_writel(fbi, LCCR0, lccr0 | LCCR0_LDM);
		complete(&fbi->disable_done);
	}

#ifdef CONFIG_FB_PXA_SMARTPANEL
	if (lcsr & LCSR_CMD_INT)
		complete(&fbi->command_done);
#endif
	lcd_writel(fbi, LCSR, lcsr);

#ifdef CONFIG_FB_PXA_OVERLAY
	{
		unsigned int lcsr1 = lcd_readl(fbi, LCSR1);
		if (lcsr1 & LCSR1_BS(1))
			complete(&fbi->overlay[0].branch_done);

		if (lcsr1 & LCSR1_BS(2))
			complete(&fbi->overlay[1].branch_done);

		lcd_writel(fbi, LCSR1, lcsr1);
	}
#endif
	return IRQ_HANDLED;
}

/*
 * This function must be called from task context only, since it will
 * sleep when disabling the LCD controller, or if we get two contending
 * processes trying to alter state.
 */
static void set_ctrlr_state(struct pxafb_info *fbi, u_int state)
{
	u_int old_state;

	mutex_lock(&fbi->ctrlr_lock);

	old_state = fbi->state;

	/*
	 * Hack around fbcon initialisation.
	 */
	if (old_state == C_STARTUP && state == C_REENABLE)
		state = C_ENABLE;

	switch (state) {
	case C_DISABLE_CLKCHANGE:
		/*
		 * Disable controller for clock change.  If the
		 * controller is already disabled, then do nothing.
		 */
		if (old_state != C_DISABLE && old_state != C_DISABLE_PM) {
			fbi->state = state;
			/* TODO __pxafb_lcd_power(fbi, 0); */
			pxafb_disable_controller(fbi);
		}
		break;

	case C_DISABLE_PM:
	case C_DISABLE:
		/*
		 * Disable controller
		 */
		if (old_state != C_DISABLE) {
			fbi->state = state;
			__pxafb_backlight_power(fbi, 0);
			__pxafb_lcd_power(fbi, 0);
			if (old_state != C_DISABLE_CLKCHANGE)
				pxafb_disable_controller(fbi);
		}
		break;

	case C_ENABLE_CLKCHANGE:
		/*
		 * Enable the controller after clock change.  Only
		 * do this if we were disabled for the clock change.
		 */
		if (old_state == C_DISABLE_CLKCHANGE) {
			fbi->state = C_ENABLE;
			pxafb_enable_controller(fbi);
			/* TODO __pxafb_lcd_power(fbi, 1); */
		}
		break;

	case C_REENABLE:
		/*
		 * Re-enable the controller only if it was already
		 * enabled.  This is so we reprogram the control
		 * registers.
		 */
		if (old_state == C_ENABLE) {
			__pxafb_lcd_power(fbi, 0);
			pxafb_disable_controller(fbi);
			pxafb_enable_controller(fbi);
			__pxafb_lcd_power(fbi, 1);
		}
		break;

	case C_ENABLE_PM:
		/*
		 * Re-enable the controller after PM.  This is not
		 * perfect - think about the case where we were doing
		 * a clock change, and we suspended half-way through.
		 */
		if (old_state != C_DISABLE_PM)
			break;
		/* fall through */

	case C_ENABLE:
		/*
		 * Power up the LCD screen, enable controller, and
		 * turn on the backlight.
		 */
		if (old_state != C_ENABLE) {
			fbi->state = C_ENABLE;
			pxafb_enable_controller(fbi);
			__pxafb_lcd_power(fbi, 1);
			__pxafb_backlight_power(fbi, 1);
		}
		break;
	}
	mutex_unlock(&fbi->ctrlr_lock);
}

/*
 * Our LCD controller task (which is called when we blank or unblank)
 * via keventd.
 */
static void pxafb_task(struct work_struct *work)
{
	struct pxafb_info *fbi =
		container_of(work, struct pxafb_info, task);
	u_int state = xchg(&fbi->task_state, -1);

	set_ctrlr_state(fbi, state);
}

#ifdef CONFIG_CPU_FREQ
/*
 * CPU clock speed change handler.  We need to adjust the LCD timing
 * parameters when the CPU clock is adjusted by the power management
 * subsystem.
 *
 * TODO: Determine why f->new != 10*get_lclk_frequency_10khz()
 */
static int
pxafb_freq_transition(struct notifier_block *nb, unsigned long val, void *data)
{
	struct pxafb_info *fbi = TO_INF(nb, freq_transition);
	/* TODO struct cpufreq_freqs *f = data; */
	u_int pcd;

	switch (val) {
	case CPUFREQ_PRECHANGE:
#ifdef CONFIG_FB_PXA_OVERLAY
		if (!(fbi->overlay[0].usage || fbi->overlay[1].usage))
#endif
			set_ctrlr_state(fbi, C_DISABLE_CLKCHANGE);
		break;

	case CPUFREQ_POSTCHANGE:
		pcd = get_pcd(fbi, fbi->fb.var.pixclock);
		set_hsync_time(fbi, pcd);
		fbi->reg_lccr3 = (fbi->reg_lccr3 & ~0xff) |
				  LCCR3_PixClkDiv(pcd);
		set_ctrlr_state(fbi, C_ENABLE_CLKCHANGE);
		break;
	}
	return 0;
}

static int
pxafb_freq_policy(struct notifier_block *nb, unsigned long val, void *data)
{
	struct pxafb_info *fbi = TO_INF(nb, freq_policy);
	struct fb_var_screeninfo *var = &fbi->fb.var;
	struct cpufreq_policy *policy = data;

	switch (val) {
	case CPUFREQ_ADJUST:
		pr_debug("min dma period: %d ps, "
			"new clock %d kHz\n", pxafb_display_dma_period(var),
			policy->max);
		/* TODO: fill in min/max values */
		break;
	}
	return 0;
}
#endif

#ifdef CONFIG_PM
/*
 * Power management hooks.  Note that we won't be called from IRQ context,
 * unlike the blank functions above, so we may sleep.
 */
static int pxafb_suspend(struct device *dev)
{
	struct pxafb_info *fbi = dev_get_drvdata(dev);

	set_ctrlr_state(fbi, C_DISABLE_PM);
	return 0;
}

static int pxafb_resume(struct device *dev)
{
	struct pxafb_info *fbi = dev_get_drvdata(dev);

	set_ctrlr_state(fbi, C_ENABLE_PM);
	return 0;
}

static const struct dev_pm_ops pxafb_pm_ops = {
	.suspend	= pxafb_suspend,
	.resume		= pxafb_resume,
};
#endif

static int pxafb_init_video_memory(struct pxafb_info *fbi)
{
	int size = PAGE_ALIGN(fbi->video_mem_size);

	fbi->video_mem = alloc_pages_exact(size, GFP_KERNEL | __GFP_ZERO);
	if (fbi->video_mem == NULL)
		return -ENOMEM;

	fbi->video_mem_phys = virt_to_phys(fbi->video_mem);
	fbi->video_mem_size = size;

	fbi->fb.fix.smem_start	= fbi->video_mem_phys;
	fbi->fb.fix.smem_len	= fbi->video_mem_size;
	fbi->fb.screen_base	= fbi->video_mem;

	return fbi->video_mem ? 0 : -ENOMEM;
}

static void pxafb_decode_mach_info(struct pxafb_info *fbi,
				   struct pxafb_mach_info *inf)
{
	unsigned int lcd_conn = inf->lcd_conn;
	struct pxafb_mode_info *m;
	int i;

	fbi->cmap_inverse	= inf->cmap_inverse;
	fbi->cmap_static	= inf->cmap_static;
	fbi->lccr4 		= inf->lccr4;

	switch (lcd_conn & LCD_TYPE_MASK) {
	case LCD_TYPE_MONO_STN:
		fbi->lccr0 = LCCR0_CMS;
		break;
	case LCD_TYPE_MONO_DSTN:
		fbi->lccr0 = LCCR0_CMS | LCCR0_SDS;
		break;
	case LCD_TYPE_COLOR_STN:
		fbi->lccr0 = 0;
		break;
	case LCD_TYPE_COLOR_DSTN:
		fbi->lccr0 = LCCR0_SDS;
		break;
	case LCD_TYPE_COLOR_TFT:
		fbi->lccr0 = LCCR0_PAS;
		break;
	case LCD_TYPE_SMART_PANEL:
		fbi->lccr0 = LCCR0_LCDT | LCCR0_PAS;
		break;
	default:
		/* fall back to backward compatibility way */
		fbi->lccr0 = inf->lccr0;
		fbi->lccr3 = inf->lccr3;
		goto decode_mode;
	}

	if (lcd_conn == LCD_MONO_STN_8BPP)
		fbi->lccr0 |= LCCR0_DPD;

	fbi->lccr0 |= (lcd_conn & LCD_ALTERNATE_MAPPING) ? LCCR0_LDDALT : 0;

	fbi->lccr3 = LCCR3_Acb((inf->lcd_conn >> 10) & 0xff);
	fbi->lccr3 |= (lcd_conn & LCD_BIAS_ACTIVE_LOW) ? LCCR3_OEP : 0;
	fbi->lccr3 |= (lcd_conn & LCD_PCLK_EDGE_FALL)  ? LCCR3_PCP : 0;

decode_mode:
	pxafb_setmode(&fbi->fb.var, &inf->modes[0]);

	/* decide video memory size as follows:
	 * 1. default to mode of maximum resolution
	 * 2. allow platform to override
	 * 3. allow module parameter to override
	 */
	for (i = 0, m = &inf->modes[0]; i < inf->num_modes; i++, m++)
		fbi->video_mem_size = max_t(size_t, fbi->video_mem_size,
				m->xres * m->yres * m->bpp / 8);

	if (inf->video_mem_size > fbi->video_mem_size)
		fbi->video_mem_size = inf->video_mem_size;

	if (video_mem_size > fbi->video_mem_size)
		fbi->video_mem_size = video_mem_size;
}

static struct pxafb_info *pxafb_init_fbinfo(struct device *dev,
					    struct pxafb_mach_info *inf)
{
	struct pxafb_info *fbi;
	void *addr;

	/* Alloc the pxafb_info and pseudo_palette in one step */
	fbi = devm_kzalloc(dev, sizeof(struct pxafb_info) + sizeof(u32) * 16,
			   GFP_KERNEL);
	if (!fbi)
		return NULL;

	fbi->dev = dev;
	fbi->inf = inf;

	fbi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(fbi->clk))
		return NULL;

	strcpy(fbi->fb.fix.id, PXA_NAME);

	fbi->fb.fix.type	= FB_TYPE_PACKED_PIXELS;
	fbi->fb.fix.type_aux	= 0;
	fbi->fb.fix.xpanstep	= 0;
	fbi->fb.fix.ypanstep	= 1;
	fbi->fb.fix.ywrapstep	= 0;
	fbi->fb.fix.accel	= FB_ACCEL_NONE;

	fbi->fb.var.nonstd	= 0;
	fbi->fb.var.activate	= FB_ACTIVATE_NOW;
	fbi->fb.var.height	= -1;
	fbi->fb.var.width	= -1;
	fbi->fb.var.accel_flags	= FB_ACCELF_TEXT;
	fbi->fb.var.vmode	= FB_VMODE_NONINTERLACED;

	fbi->fb.fbops		= &pxafb_ops;
	fbi->fb.flags		= FBINFO_DEFAULT;
	fbi->fb.node		= -1;

	addr = fbi;
	addr = addr + sizeof(struct pxafb_info);
	fbi->fb.pseudo_palette	= addr;

	fbi->state		= C_STARTUP;
	fbi->task_state		= (u_char)-1;

	pxafb_decode_mach_info(fbi, inf);

#ifdef CONFIG_FB_PXA_OVERLAY
	/* place overlay(s) on top of base */
	if (pxafb_overlay_supported())
		fbi->lccr0 |= LCCR0_OUC;
#endif

	init_waitqueue_head(&fbi->ctrlr_wait);
	INIT_WORK(&fbi->task, pxafb_task);
	mutex_init(&fbi->ctrlr_lock);
	init_completion(&fbi->disable_done);

	return fbi;
}

#ifdef CONFIG_FB_PXA_PARAMETERS
static int parse_opt_mode(struct device *dev, const char *this_opt,
			  struct pxafb_mach_info *inf)
{
	const char *name = this_opt+5;
	unsigned int namelen = strlen(name);
	int res_specified = 0, bpp_specified = 0;
	unsigned int xres = 0, yres = 0, bpp = 0;
	int yres_specified = 0;
	int i;
	for (i = namelen-1; i >= 0; i--) {
		switch (name[i]) {
		case '-':
			namelen = i;
			if (!bpp_specified && !yres_specified) {
				bpp = simple_strtoul(&name[i+1], NULL, 0);
				bpp_specified = 1;
			} else
				goto done;
			break;
		case 'x':
			if (!yres_specified) {
				yres = simple_strtoul(&name[i+1], NULL, 0);
				yres_specified = 1;
			} else
				goto done;
			break;
		case '0' ... '9':
			break;
		default:
			goto done;
		}
	}
	if (i < 0 && yres_specified) {
		xres = simple_strtoul(name, NULL, 0);
		res_specified = 1;
	}
done:
	if (res_specified) {
		dev_info(dev, "overriding resolution: %dx%d\n", xres, yres);
		inf->modes[0].xres = xres; inf->modes[0].yres = yres;
	}
	if (bpp_specified)
		switch (bpp) {
		case 1:
		case 2:
		case 4:
		case 8:
		case 16:
			inf->modes[0].bpp = bpp;
			dev_info(dev, "overriding bit depth: %d\n", bpp);
			break;
		default:
			dev_err(dev, "Depth %d is not valid\n", bpp);
			return -EINVAL;
		}
	return 0;
}

static int parse_opt(struct device *dev, char *this_opt,
		     struct pxafb_mach_info *inf)
{
	struct pxafb_mode_info *mode = &inf->modes[0];
	char s[64];

	s[0] = '\0';

	if (!strncmp(this_opt, "vmem:", 5)) {
		video_mem_size = memparse(this_opt + 5, NULL);
	} else if (!strncmp(this_opt, "mode:", 5)) {
		return parse_opt_mode(dev, this_opt, inf);
	} else if (!strncmp(this_opt, "pixclock:", 9)) {
		mode->pixclock = simple_strtoul(this_opt+9, NULL, 0);
		sprintf(s, "pixclock: %ld\n", mode->pixclock);
	} else if (!strncmp(this_opt, "left:", 5)) {
		mode->left_margin = simple_strtoul(this_opt+5, NULL, 0);
		sprintf(s, "left: %u\n", mode->left_margin);
	} else if (!strncmp(this_opt, "right:", 6)) {
		mode->right_margin = simple_strtoul(this_opt+6, NULL, 0);
		sprintf(s, "right: %u\n", mode->right_margin);
	} else if (!strncmp(this_opt, "upper:", 6)) {
		mode->upper_margin = simple_strtoul(this_opt+6, NULL, 0);
		sprintf(s, "upper: %u\n", mode->upper_margin);
	} else if (!strncmp(this_opt, "lower:", 6)) {
		mode->lower_margin = simple_strtoul(this_opt+6, NULL, 0);
		sprintf(s, "lower: %u\n", mode->lower_margin);
	} else if (!strncmp(this_opt, "hsynclen:", 9)) {
		mode->hsync_len = simple_strtoul(this_opt+9, NULL, 0);
		sprintf(s, "hsynclen: %u\n", mode->hsync_len);
	} else if (!strncmp(this_opt, "vsynclen:", 9)) {
		mode->vsync_len = simple_strtoul(this_opt+9, NULL, 0);
		sprintf(s, "vsynclen: %u\n", mode->vsync_len);
	} else if (!strncmp(this_opt, "hsync:", 6)) {
		if (simple_strtoul(this_opt+6, NULL, 0) == 0) {
			sprintf(s, "hsync: Active Low\n");
			mode->sync &= ~FB_SYNC_HOR_HIGH_ACT;
		} else {
			sprintf(s, "hsync: Active High\n");
			mode->sync |= FB_SYNC_HOR_HIGH_ACT;
		}
	} else if (!strncmp(this_opt, "vsync:", 6)) {
		if (simple_strtoul(this_opt+6, NULL, 0) == 0) {
			sprintf(s, "vsync: Active Low\n");
			mode->sync &= ~FB_SYNC_VERT_HIGH_ACT;
		} else {
			sprintf(s, "vsync: Active High\n");
			mode->sync |= FB_SYNC_VERT_HIGH_ACT;
		}
	} else if (!strncmp(this_opt, "dpc:", 4)) {
		if (simple_strtoul(this_opt+4, NULL, 0) == 0) {
			sprintf(s, "double pixel clock: false\n");
			inf->lccr3 &= ~LCCR3_DPC;
		} else {
			sprintf(s, "double pixel clock: true\n");
			inf->lccr3 |= LCCR3_DPC;
		}
	} else if (!strncmp(this_opt, "outputen:", 9)) {
		if (simple_strtoul(this_opt+9, NULL, 0) == 0) {
			sprintf(s, "output enable: active low\n");
			inf->lccr3 = (inf->lccr3 & ~LCCR3_OEP) | LCCR3_OutEnL;
		} else {
			sprintf(s, "output enable: active high\n");
			inf->lccr3 = (inf->lccr3 & ~LCCR3_OEP) | LCCR3_OutEnH;
		}
	} else if (!strncmp(this_opt, "pixclockpol:", 12)) {
		if (simple_strtoul(this_opt+12, NULL, 0) == 0) {
			sprintf(s, "pixel clock polarity: falling edge\n");
			inf->lccr3 = (inf->lccr3 & ~LCCR3_PCP) | LCCR3_PixFlEdg;
		} else {
			sprintf(s, "pixel clock polarity: rising edge\n");
			inf->lccr3 = (inf->lccr3 & ~LCCR3_PCP) | LCCR3_PixRsEdg;
		}
	} else if (!strncmp(this_opt, "color", 5)) {
		inf->lccr0 = (inf->lccr0 & ~LCCR0_CMS) | LCCR0_Color;
	} else if (!strncmp(this_opt, "mono", 4)) {
		inf->lccr0 = (inf->lccr0 & ~LCCR0_CMS) | LCCR0_Mono;
	} else if (!strncmp(this_opt, "active", 6)) {
		inf->lccr0 = (inf->lccr0 & ~LCCR0_PAS) | LCCR0_Act;
	} else if (!strncmp(this_opt, "passive", 7)) {
		inf->lccr0 = (inf->lccr0 & ~LCCR0_PAS) | LCCR0_Pas;
	} else if (!strncmp(this_opt, "single", 6)) {
		inf->lccr0 = (inf->lccr0 & ~LCCR0_SDS) | LCCR0_Sngl;
	} else if (!strncmp(this_opt, "dual", 4)) {
		inf->lccr0 = (inf->lccr0 & ~LCCR0_SDS) | LCCR0_Dual;
	} else if (!strncmp(this_opt, "4pix", 4)) {
		inf->lccr0 = (inf->lccr0 & ~LCCR0_DPD) | LCCR0_4PixMono;
	} else if (!strncmp(this_opt, "8pix", 4)) {
		inf->lccr0 = (inf->lccr0 & ~LCCR0_DPD) | LCCR0_8PixMono;
	} else {
		dev_err(dev, "unknown option: %s\n", this_opt);
		return -EINVAL;
	}

	if (s[0] != '\0')
		dev_info(dev, "override %s", s);

	return 0;
}

static int pxafb_parse_options(struct device *dev, char *options,
			       struct pxafb_mach_info *inf)
{
	char *this_opt;
	int ret;

	if (!options || !*options)
		return 0;

	dev_dbg(dev, "options are \"%s\"\n", options ? options : "null");

	/* could be made table driven or similar?... */
	while ((this_opt = strsep(&options, ",")) != NULL) {
		ret = parse_opt(dev, this_opt, inf);
		if (ret)
			return ret;
	}
	return 0;
}

static char g_options[256] = "";

#ifndef MODULE
static int __init pxafb_setup_options(void)
{
	char *options = NULL;

	if (fb_get_options("pxafb", &options))
		return -ENODEV;

	if (options)
		strlcpy(g_options, options, sizeof(g_options));

	return 0;
}
#else
#define pxafb_setup_options()		(0)

module_param_string(options, g_options, sizeof(g_options), 0);
MODULE_PARM_DESC(options, "LCD parameters (see Documentation/fb/pxafb.txt)");
#endif

#else
#define pxafb_parse_options(...)	(0)
#define pxafb_setup_options()		(0)
#endif

#ifdef DEBUG_VAR
/* Check for various illegal bit-combinations. Currently only
 * a warning is given. */
static void pxafb_check_options(struct device *dev, struct pxafb_mach_info *inf)
{
	if (inf->lcd_conn)
		return;

	if (inf->lccr0 & LCCR0_INVALID_CONFIG_MASK)
		dev_warn(dev, "machine LCCR0 setting contains "
				"illegal bits: %08x\n",
			inf->lccr0 & LCCR0_INVALID_CONFIG_MASK);
	if (inf->lccr3 & LCCR3_INVALID_CONFIG_MASK)
		dev_warn(dev, "machine LCCR3 setting contains "
				"illegal bits: %08x\n",
			inf->lccr3 & LCCR3_INVALID_CONFIG_MASK);
	if (inf->lccr0 & LCCR0_DPD &&
	    ((inf->lccr0 & LCCR0_PAS) != LCCR0_Pas ||
	     (inf->lccr0 & LCCR0_SDS) != LCCR0_Sngl ||
	     (inf->lccr0 & LCCR0_CMS) != LCCR0_Mono))
		dev_warn(dev, "Double Pixel Data (DPD) mode is "
				"only valid in passive mono"
				" single panel mode\n");
	if ((inf->lccr0 & LCCR0_PAS) == LCCR0_Act &&
	    (inf->lccr0 & LCCR0_SDS) == LCCR0_Dual)
		dev_warn(dev, "Dual panel only valid in passive mode\n");
	if ((inf->lccr0 & LCCR0_PAS) == LCCR0_Pas &&
	     (inf->modes->upper_margin || inf->modes->lower_margin))
		dev_warn(dev, "Upper and lower margins must be 0 in "
				"passive mode\n");
}
#else
#define pxafb_check_options(...)	do {} while (0)
#endif

#if defined(CONFIG_OF)
static const char * const lcd_types[] = {
	"unknown", "mono-stn", "mono-dstn", "color-stn", "color-dstn",
	"color-tft", "smart-panel", NULL
};

static int of_get_pxafb_display(struct device *dev, struct device_node *disp,
				struct pxafb_mach_info *info, u32 bus_width)
{
	struct display_timings *timings;
	struct videomode vm;
	int i, ret = -EINVAL;
	const char *s;

	ret = of_property_read_string(disp, "lcd-type", &s);
	if (ret)
		s = "color-tft";

	i = match_string(lcd_types, -1, s);
	if (i < 0) {
		dev_err(dev, "lcd-type %s is unknown\n", s);
		return i;
	}
	info->lcd_conn |= LCD_CONN_TYPE(i);
	info->lcd_conn |= LCD_CONN_WIDTH(bus_width);

	timings = of_get_display_timings(disp);
	if (!timings)
		return -EINVAL;

	ret = -ENOMEM;
	info->modes = devm_kcalloc(dev, timings->num_timings,
				   sizeof(info->modes[0]),
				   GFP_KERNEL);
	if (!info->modes)
		goto out;
	info->num_modes = timings->num_timings;

	for (i = 0; i < timings->num_timings; i++) {
		ret = videomode_from_timings(timings, &vm, i);
		if (ret) {
			dev_err(dev, "videomode_from_timings %d failed: %d\n",
				i, ret);
			goto out;
		}
		if (vm.flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
			info->lcd_conn |= LCD_PCLK_EDGE_RISE;
		if (vm.flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
			info->lcd_conn |= LCD_PCLK_EDGE_FALL;
		if (vm.flags & DISPLAY_FLAGS_DE_HIGH)
			info->lcd_conn |= LCD_BIAS_ACTIVE_HIGH;
		if (vm.flags & DISPLAY_FLAGS_DE_LOW)
			info->lcd_conn |= LCD_BIAS_ACTIVE_LOW;
		if (vm.flags & DISPLAY_FLAGS_HSYNC_HIGH)
			info->modes[i].sync |= FB_SYNC_HOR_HIGH_ACT;
		if (vm.flags & DISPLAY_FLAGS_VSYNC_HIGH)
			info->modes[i].sync |= FB_SYNC_VERT_HIGH_ACT;

		info->modes[i].pixclock = 1000000000UL / (vm.pixelclock / 1000);
		info->modes[i].xres = vm.hactive;
		info->modes[i].yres = vm.vactive;
		info->modes[i].hsync_len = vm.hsync_len;
		info->modes[i].left_margin = vm.hback_porch;
		info->modes[i].right_margin = vm.hfront_porch;
		info->modes[i].vsync_len = vm.vsync_len;
		info->modes[i].upper_margin = vm.vback_porch;
		info->modes[i].lower_margin = vm.vfront_porch;
	}
	ret = 0;

out:
	display_timings_release(timings);
	return ret;
}

static int of_get_pxafb_mode_info(struct device *dev,
				  struct pxafb_mach_info *info)
{
	struct device_node *display, *np;
	u32 bus_width;
	int ret, i;

	np = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!np) {
		dev_err(dev, "could not find endpoint\n");
		return -EINVAL;
	}
	ret = of_property_read_u32(np, "bus-width", &bus_width);
	if (ret) {
		dev_err(dev, "no bus-width specified: %d\n", ret);
		of_node_put(np);
		return ret;
	}

	display = of_graph_get_remote_port_parent(np);
	of_node_put(np);
	if (!display) {
		dev_err(dev, "no display defined\n");
		return -EINVAL;
	}

	ret = of_get_pxafb_display(dev, display, info, bus_width);
	of_node_put(display);
	if (ret)
		return ret;

	for (i = 0; i < info->num_modes; i++)
		info->modes[i].bpp = bus_width;

	return 0;
}

static struct pxafb_mach_info *of_pxafb_of_mach_info(struct device *dev)
{
	int ret;
	struct pxafb_mach_info *info;

	if (!dev->of_node)
		return NULL;
	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);
	ret = of_get_pxafb_mode_info(dev, info);
	if (ret) {
		kfree(info->modes);
		return ERR_PTR(ret);
	}

	/*
	 * On purpose, neither lccrX registers nor video memory size can be
	 * specified through device-tree, they are considered more a debug hack
	 * available through command line.
	 */
	return info;
}
#else
static struct pxafb_mach_info *of_pxafb_of_mach_info(struct device *dev)
{
	return NULL;
}
#endif

static int pxafb_probe(struct platform_device *dev)
{
	struct pxafb_info *fbi;
	struct pxafb_mach_info *inf, *pdata;
	struct resource *r;
	int i, irq, ret;

	dev_dbg(&dev->dev, "pxafb_probe\n");

	ret = -ENOMEM;
	pdata = dev_get_platdata(&dev->dev);
	inf = devm_kmalloc(&dev->dev, sizeof(*inf), GFP_KERNEL);
	if (!inf)
		goto failed;

	if (pdata) {
		*inf = *pdata;
		inf->modes =
			devm_kmalloc_array(&dev->dev, pdata->num_modes,
					   sizeof(inf->modes[0]), GFP_KERNEL);
		if (!inf->modes)
			goto failed;
		for (i = 0; i < inf->num_modes; i++)
			inf->modes[i] = pdata->modes[i];
	}

	if (!pdata)
		inf = of_pxafb_of_mach_info(&dev->dev);
	if (IS_ERR_OR_NULL(inf))
		goto failed;

	ret = pxafb_parse_options(&dev->dev, g_options, inf);
	if (ret < 0)
		goto failed;

	pxafb_check_options(&dev->dev, inf);

	dev_dbg(&dev->dev, "got a %dx%dx%d LCD\n",
			inf->modes->xres,
			inf->modes->yres,
			inf->modes->bpp);
	if (inf->modes->xres == 0 ||
	    inf->modes->yres == 0 ||
	    inf->modes->bpp == 0) {
		dev_err(&dev->dev, "Invalid resolution or bit depth\n");
		ret = -EINVAL;
		goto failed;
	}

	fbi = pxafb_init_fbinfo(&dev->dev, inf);
	if (!fbi) {
		/* only reason for pxafb_init_fbinfo to fail is kmalloc */
		dev_err(&dev->dev, "Failed to initialize framebuffer device\n");
		ret = -ENOMEM;
		goto failed;
	}

	if (cpu_is_pxa3xx() && inf->acceleration_enabled)
		fbi->fb.fix.accel = FB_ACCEL_PXA3XX;

	fbi->backlight_power = inf->pxafb_backlight_power;
	fbi->lcd_power = inf->pxafb_lcd_power;

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&dev->dev, "no I/O memory resource defined\n");
		ret = -ENODEV;
		goto failed;
	}

	fbi->mmio_base = devm_ioremap_resource(&dev->dev, r);
	if (IS_ERR(fbi->mmio_base)) {
		dev_err(&dev->dev, "failed to get I/O memory\n");
		ret = -EBUSY;
		goto failed;
	}

	fbi->dma_buff_size = PAGE_ALIGN(sizeof(struct pxafb_dma_buff));
	fbi->dma_buff = dma_alloc_coherent(fbi->dev, fbi->dma_buff_size,
				&fbi->dma_buff_phys, GFP_KERNEL);
	if (fbi->dma_buff == NULL) {
		dev_err(&dev->dev, "failed to allocate memory for DMA\n");
		ret = -ENOMEM;
		goto failed;
	}

	ret = pxafb_init_video_memory(fbi);
	if (ret) {
		dev_err(&dev->dev, "Failed to allocate video RAM: %d\n", ret);
		ret = -ENOMEM;
		goto failed_free_dma;
	}

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		dev_err(&dev->dev, "no IRQ defined\n");
		ret = -ENODEV;
		goto failed_free_mem;
	}

	ret = devm_request_irq(&dev->dev, irq, pxafb_handle_irq, 0, "LCD", fbi);
	if (ret) {
		dev_err(&dev->dev, "request_irq failed: %d\n", ret);
		ret = -EBUSY;
		goto failed_free_mem;
	}

	ret = pxafb_smart_init(fbi);
	if (ret) {
		dev_err(&dev->dev, "failed to initialize smartpanel\n");
		goto failed_free_mem;
	}

	/*
	 * This makes sure that our colour bitfield
	 * descriptors are correctly initialised.
	 */
	ret = pxafb_check_var(&fbi->fb.var, &fbi->fb);
	if (ret) {
		dev_err(&dev->dev, "failed to get suitable mode\n");
		goto failed_free_mem;
	}

	ret = pxafb_set_par(&fbi->fb);
	if (ret) {
		dev_err(&dev->dev, "Failed to set parameters\n");
		goto failed_free_mem;
	}

	platform_set_drvdata(dev, fbi);

	ret = register_framebuffer(&fbi->fb);
	if (ret < 0) {
		dev_err(&dev->dev,
			"Failed to register framebuffer device: %d\n", ret);
		goto failed_free_cmap;
	}

	pxafb_overlay_init(fbi);

#ifdef CONFIG_CPU_FREQ
	fbi->freq_transition.notifier_call = pxafb_freq_transition;
	fbi->freq_policy.notifier_call = pxafb_freq_policy;
	cpufreq_register_notifier(&fbi->freq_transition,
				CPUFREQ_TRANSITION_NOTIFIER);
	cpufreq_register_notifier(&fbi->freq_policy,
				CPUFREQ_POLICY_NOTIFIER);
#endif

	/*
	 * Ok, now enable the LCD controller
	 */
	set_ctrlr_state(fbi, C_ENABLE);

	return 0;

failed_free_cmap:
	if (fbi->fb.cmap.len)
		fb_dealloc_cmap(&fbi->fb.cmap);
failed_free_mem:
	free_pages_exact(fbi->video_mem, fbi->video_mem_size);
failed_free_dma:
	dma_free_coherent(&dev->dev, fbi->dma_buff_size,
			fbi->dma_buff, fbi->dma_buff_phys);
failed:
	return ret;
}

static int pxafb_remove(struct platform_device *dev)
{
	struct pxafb_info *fbi = platform_get_drvdata(dev);
	struct fb_info *info;

	if (!fbi)
		return 0;

	info = &fbi->fb;

	pxafb_overlay_exit(fbi);
	unregister_framebuffer(info);

	pxafb_disable_controller(fbi);

	if (fbi->fb.cmap.len)
		fb_dealloc_cmap(&fbi->fb.cmap);

	free_pages_exact(fbi->video_mem, fbi->video_mem_size);

	dma_free_wc(&dev->dev, fbi->dma_buff_size, fbi->dma_buff,
		    fbi->dma_buff_phys);

	return 0;
}

static const struct of_device_id pxafb_of_dev_id[] = {
	{ .compatible = "marvell,pxa270-lcdc", },
	{ .compatible = "marvell,pxa300-lcdc", },
	{ .compatible = "marvell,pxa2xx-lcdc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pxafb_of_dev_id);

static struct platform_driver pxafb_driver = {
	.probe		= pxafb_probe,
	.remove 	= pxafb_remove,
	.driver		= {
		.name	= "pxa2xx-fb",
		.of_match_table = pxafb_of_dev_id,
#ifdef CONFIG_PM
		.pm	= &pxafb_pm_ops,
#endif
	},
};

static int __init pxafb_init(void)
{
	if (pxafb_setup_options())
		return -EINVAL;

	return platform_driver_register(&pxafb_driver);
}

static void __exit pxafb_exit(void)
{
	platform_driver_unregister(&pxafb_driver);
}

module_init(pxafb_init);
module_exit(pxafb_exit);

MODULE_DESCRIPTION("loadable framebuffer driver for PXA");
MODULE_LICENSE("GPL");
