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
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
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
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/div64.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-gpio.h>
#include <asm/arch/bitfield.h>
#include <asm/arch/pxafb.h>

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
					 LCCR3_PCD | LCCR3_BPP)

static void (*pxafb_backlight_power)(int);
static void (*pxafb_lcd_power)(int, struct fb_var_screeninfo *);

static int pxafb_activate_var(struct fb_var_screeninfo *var,
				struct pxafb_info *);
static void set_ctrlr_state(struct pxafb_info *fbi, u_int state);

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
	struct pxafb_info *fbi = (struct pxafb_info *)info;
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
	}

	return 0;
}

static int
pxafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		   u_int trans, struct fb_info *info)
{
	struct pxafb_info *fbi = (struct pxafb_info *)info;
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

/*
 *  pxafb_bpp_to_lccr3():
 *    Convert a bits per pixel value to the correct bit pattern for LCCR3
 */
static int pxafb_bpp_to_lccr3(struct fb_var_screeninfo *var)
{
	int ret = 0;
	switch (var->bits_per_pixel) {
	case 1:  ret = LCCR3_1BPP; break;
	case 2:  ret = LCCR3_2BPP; break;
	case 4:  ret = LCCR3_4BPP; break;
	case 8:  ret = LCCR3_8BPP; break;
	case 16: ret = LCCR3_16BPP; break;
	}
	return ret;
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
	var->xres_virtual 	= var->xres;
	var->yres_virtual	= var->yres;
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
	struct pxafb_info *fbi = (struct pxafb_info *)info;
	struct pxafb_mach_info *inf = fbi->dev->platform_data;

	if (var->xres < MIN_XRES)
		var->xres = MIN_XRES;
	if (var->yres < MIN_YRES)
		var->yres = MIN_YRES;

	if (inf->fixed_modes) {
		struct pxafb_mode_info *mode;

		mode = pxafb_getmode(inf, var);
		if (!mode)
			return -EINVAL;
		pxafb_setmode(var, mode);
	} else {
		if (var->xres > inf->modes->xres)
			return -EINVAL;
		if (var->yres > inf->modes->yres)
			return -EINVAL;
		if (var->bits_per_pixel > inf->modes->bpp)
			return -EINVAL;
	}

	var->xres_virtual =
		max(var->xres_virtual, var->xres);
	var->yres_virtual =
		max(var->yres_virtual, var->yres);

	/*
	 * Setup the RGB parameters for this display.
	 *
	 * The pixel packing format is described on page 7-11 of the
	 * PXA2XX Developer's Manual.
	 */
	if (var->bits_per_pixel == 16) {
		var->red.offset   = 11; var->red.length   = 5;
		var->green.offset = 5;  var->green.length = 6;
		var->blue.offset  = 0;  var->blue.length  = 5;
		var->transp.offset = var->transp.length = 0;
	} else {
		var->red.offset = var->green.offset = 0;
		var->blue.offset = var->transp.offset = 0;
		var->red.length   = 8;
		var->green.length = 8;
		var->blue.length  = 8;
		var->transp.length = 0;
	}

#ifdef CONFIG_CPU_FREQ
	pr_debug("pxafb: dma period = %d ps, clock = %d kHz\n",
		 pxafb_display_dma_period(var),
		 get_clk_frequency_khz(0));
#endif

	return 0;
}

static inline void pxafb_set_truecolor(u_int is_true_color)
{
	/* do your machine-specific setup if needed */
}

/*
 * pxafb_set_par():
 *	Set the user defined part of the display for the specified console
 */
static int pxafb_set_par(struct fb_info *info)
{
	struct pxafb_info *fbi = (struct pxafb_info *)info;
	struct fb_var_screeninfo *var = &info->var;

	if (var->bits_per_pixel == 16)
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
	if (var->bits_per_pixel == 16)
		fbi->palette_size = 0;
	else
		fbi->palette_size = var->bits_per_pixel == 1 ?
					4 : 1 << var->bits_per_pixel;

	fbi->palette_cpu = (u16 *)&fbi->dma_buff->palette[0];

	/*
	 * Set (any) board control register to handle new color depth
	 */
	pxafb_set_truecolor(fbi->fb.fix.visual == FB_VISUAL_TRUECOLOR);

	if (fbi->fb.var.bits_per_pixel == 16)
		fb_dealloc_cmap(&fbi->fb.cmap);
	else
		fb_alloc_cmap(&fbi->fb.cmap, 1<<fbi->fb.var.bits_per_pixel, 0);

	pxafb_activate_var(var, fbi);

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
	struct pxafb_info *fbi = (struct pxafb_info *)info;
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

static int pxafb_mmap(struct fb_info *info,
		      struct vm_area_struct *vma)
{
	struct pxafb_info *fbi = (struct pxafb_info *)info;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;

	if (off < info->fix.smem_len) {
		vma->vm_pgoff += fbi->video_offset / PAGE_SIZE;
		return dma_mmap_writecombine(fbi->dev, vma, fbi->map_cpu,
					     fbi->map_dma, fbi->map_size);
	}
	return -EINVAL;
}

static struct fb_ops pxafb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= pxafb_check_var,
	.fb_set_par	= pxafb_set_par,
	.fb_setcolreg	= pxafb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_blank	= pxafb_blank,
	.fb_mmap	= pxafb_mmap,
};

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
		unsigned int offset, size_t size)
{
	struct pxafb_dma_descriptor *dma_desc, *pal_desc;
	unsigned int dma_desc_off, pal_desc_off;

	if (dma < 0 || dma >= DMA_MAX)
		return -EINVAL;

	dma_desc = &fbi->dma_buff->dma_desc[dma];
	dma_desc_off = offsetof(struct pxafb_dma_buff, dma_desc[dma]);

	dma_desc->fsadr = fbi->screen_dma + offset;
	dma_desc->fidr  = 0;
	dma_desc->ldcmd = size;

	if (pal < 0 || pal >= PAL_MAX) {
		dma_desc->fdadr = fbi->dma_buff_phys + dma_desc_off;
		fbi->fdadr[dma] = fbi->dma_buff_phys + dma_desc_off;
	} else {
		pal_desc = &fbi->dma_buff->pal_desc[dma];
		pal_desc_off = offsetof(struct pxafb_dma_buff, dma_desc[pal]);

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
	lcd_writel(fbi, FDADR0, fbi->fdadr[0]);
	lcd_writel(fbi, FDADR6, fbi->fdadr[6]);

	/* begin sending */
	lcd_writel(fbi, LCCR0, fbi->reg_lccr0 | LCCR0_ENB);

	if (wait_for_completion_timeout(&fbi->command_done, HZ/2) == 0) {
		pr_warning("%s: timeout waiting for command done\n",
				__func__);
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

	/* leave 2 commands for INTERRUPT and WAIT_FOR_SYNC */
	for (i = 0; i < n_cmds; i++) {
		if (fbi->n_smart_cmds == CMD_BUFF_SIZE - 8)
			pxafb_smart_flush(info);

		fbi->smart_cmds[fbi->n_smart_cmds++] = *cmds++;
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
	struct pxafb_mach_info *inf = fbi->dev->platform_data;
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
	fbi->reg_lccr3 = LCCR3_PixClkDiv(__smart_timing(t4, lclk));

	/* FIXME: make this configurable */
	fbi->reg_cmdcr = 1;
}

static int pxafb_smart_thread(void *arg)
{
	struct pxafb_info *fbi = arg;
	struct pxafb_mach_info *inf = fbi->dev->platform_data;

	if (!fbi || !inf->smart_update) {
		pr_err("%s: not properly initialized, thread terminated\n",
				__func__);
		return -EINVAL;
	}

	pr_debug("%s(): task starting\n", __func__);

	set_freezable();
	while (!kthread_should_stop()) {

		if (try_to_freeze())
			continue;

		if (fbi->state == C_ENABLE) {
			inf->smart_update(&fbi->fb);
			complete(&fbi->refresh_done);
		}

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(30 * HZ / 1000);
	}

	pr_debug("%s(): task ending\n", __func__);
	return 0;
}

static int pxafb_smart_init(struct pxafb_info *fbi)
{
	fbi->smart_thread = kthread_run(pxafb_smart_thread, fbi,
					"lcd_refresh");
	if (IS_ERR(fbi->smart_thread)) {
		printk(KERN_ERR "%s: unable to create kernel thread\n",
				__func__);
		return PTR_ERR(fbi->smart_thread);
	}
	return 0;
}
#else
int pxafb_smart_queue(struct fb_info *info, uint16_t *cmds, int n_cmds)
{
	return 0;
}

int pxafb_smart_flush(struct fb_info *info)
{
	return 0;
}
#endif /* CONFIG_FB_SMART_PANEL */

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
	size_t nbytes;

#if DEBUG_VAR
	if (!(fbi->lccr0 & LCCR0_LCDT)) {
		if (var->xres < 16 || var->xres > 1024)
			printk(KERN_ERR "%s: invalid xres %d\n",
				fbi->fb.fix.id, var->xres);
		switch (var->bits_per_pixel) {
		case 1:
		case 2:
		case 4:
		case 8:
		case 16:
			break;
		default:
			printk(KERN_ERR "%s: invalid bit depth %d\n",
			       fbi->fb.fix.id, var->bits_per_pixel);
			break;
		}

		if (var->hsync_len < 1 || var->hsync_len > 64)
			printk(KERN_ERR "%s: invalid hsync_len %d\n",
				fbi->fb.fix.id, var->hsync_len);
		if (var->left_margin < 1 || var->left_margin > 255)
			printk(KERN_ERR "%s: invalid left_margin %d\n",
				fbi->fb.fix.id, var->left_margin);
		if (var->right_margin < 1 || var->right_margin > 255)
			printk(KERN_ERR "%s: invalid right_margin %d\n",
				fbi->fb.fix.id, var->right_margin);
		if (var->yres < 1 || var->yres > 1024)
			printk(KERN_ERR "%s: invalid yres %d\n",
				fbi->fb.fix.id, var->yres);
		if (var->vsync_len < 1 || var->vsync_len > 64)
			printk(KERN_ERR "%s: invalid vsync_len %d\n",
				fbi->fb.fix.id, var->vsync_len);
		if (var->upper_margin < 0 || var->upper_margin > 255)
			printk(KERN_ERR "%s: invalid upper_margin %d\n",
				fbi->fb.fix.id, var->upper_margin);
		if (var->lower_margin < 0 || var->lower_margin > 255)
			printk(KERN_ERR "%s: invalid lower_margin %d\n",
				fbi->fb.fix.id, var->lower_margin);
	}
#endif
	/* Update shadow copy atomically */
	local_irq_save(flags);

#ifdef CONFIG_FB_PXA_SMARTPANEL
	if (fbi->lccr0 & LCCR0_LCDT)
		setup_smart_timing(fbi, var);
	else
#endif
		setup_parallel_timing(fbi, var);

	fbi->reg_lccr0 = fbi->lccr0 |
		(LCCR0_LDM | LCCR0_SFM | LCCR0_IUM | LCCR0_EFM |
		 LCCR0_QDM | LCCR0_BM  | LCCR0_OUM);

	fbi->reg_lccr3 |= pxafb_bpp_to_lccr3(var);

	nbytes = var->yres * fbi->fb.fix.line_length;

	if ((fbi->lccr0 & LCCR0_SDS) == LCCR0_Dual) {
		nbytes = nbytes / 2;
		setup_frame_dma(fbi, DMA_LOWER, PAL_NONE, nbytes, nbytes);
	}

	if ((var->bits_per_pixel >= 16) || (fbi->lccr0 & LCCR0_LCDT))
		setup_frame_dma(fbi, DMA_BASE, PAL_NONE, 0, nbytes);
	else
		setup_frame_dma(fbi, DMA_BASE, PAL_BASE, 0, nbytes);

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
	    (lcd_readl(fbi, FDADR0) != fbi->fdadr[0]) ||
	    (lcd_readl(fbi, FDADR1) != fbi->fdadr[1]))
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

	if (pxafb_backlight_power)
		pxafb_backlight_power(on);
}

static inline void __pxafb_lcd_power(struct pxafb_info *fbi, int on)
{
	pr_debug("pxafb: LCD power o%s\n", on ? "n" : "ff");

	if (pxafb_lcd_power)
		pxafb_lcd_power(on, &fbi->fb.var);
}

static void pxafb_setup_gpio(struct pxafb_info *fbi)
{
	int gpio, ldd_bits;
	unsigned int lccr0 = fbi->lccr0;

	/*
	 * setup is based on type of panel supported
	 */

	/* 4 bit interface */
	if ((lccr0 & LCCR0_CMS) == LCCR0_Mono &&
	    (lccr0 & LCCR0_SDS) == LCCR0_Sngl &&
	    (lccr0 & LCCR0_DPD) == LCCR0_4PixMono)
		ldd_bits = 4;

	/* 8 bit interface */
	else if (((lccr0 & LCCR0_CMS) == LCCR0_Mono &&
		  ((lccr0 & LCCR0_SDS) == LCCR0_Dual ||
		   (lccr0 & LCCR0_DPD) == LCCR0_8PixMono)) ||
		 ((lccr0 & LCCR0_CMS) == LCCR0_Color &&
		  (lccr0 & LCCR0_PAS) == LCCR0_Pas &&
		  (lccr0 & LCCR0_SDS) == LCCR0_Sngl))
		ldd_bits = 8;

	/* 16 bit interface */
	else if ((lccr0 & LCCR0_CMS) == LCCR0_Color &&
		 ((lccr0 & LCCR0_SDS) == LCCR0_Dual ||
		  (lccr0 & LCCR0_PAS) == LCCR0_Act))
		ldd_bits = 16;

	else {
		printk(KERN_ERR "pxafb_setup_gpio: unable to determine "
			       "bits per pixel\n");
		return;
	}

	for (gpio = 58; ldd_bits; gpio++, ldd_bits--)
		pxa_gpio_mode(gpio | GPIO_ALT_FN_2_OUT);
	pxa_gpio_mode(GPIO74_LCD_FCLK_MD);
	pxa_gpio_mode(GPIO75_LCD_LCLK_MD);
	pxa_gpio_mode(GPIO76_LCD_PCLK_MD);
	pxa_gpio_mode(GPIO77_LCD_ACBIAS_MD);
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
	clk_enable(fbi->clk);

	if (fbi->lccr0 & LCCR0_LCDT)
		return;

	/* Sequence from 11.7.10 */
	lcd_writel(fbi, LCCR3, fbi->reg_lccr3);
	lcd_writel(fbi, LCCR2, fbi->reg_lccr2);
	lcd_writel(fbi, LCCR1, fbi->reg_lccr1);
	lcd_writel(fbi, LCCR0, fbi->reg_lccr0 & ~LCCR0_ENB);

	lcd_writel(fbi, FDADR0, fbi->fdadr[0]);
	lcd_writel(fbi, FDADR1, fbi->fdadr[1]);
	lcd_writel(fbi, LCCR0, fbi->reg_lccr0 | LCCR0_ENB);
}

static void pxafb_disable_controller(struct pxafb_info *fbi)
{
	uint32_t lccr0;

#ifdef CONFIG_FB_PXA_SMARTPANEL
	if (fbi->lccr0 & LCCR0_LCDT) {
		wait_for_completion_timeout(&fbi->refresh_done,
				200 * HZ / 1000);
		return;
	}
#endif

	/* Clear LCD Status Register */
	lcd_writel(fbi, LCSR, 0xffffffff);

	lccr0 = lcd_readl(fbi, LCCR0) & ~LCCR0_LDM;
	lcd_writel(fbi, LCCR0, lccr0);
	lcd_writel(fbi, LCCR0, lccr0 | LCCR0_DIS);

	wait_for_completion_timeout(&fbi->disable_done, 200 * HZ / 1000);

	/* disable LCD controller clock */
	clk_disable(fbi->clk);
}

/*
 *  pxafb_handle_irq: Handle 'LCD DONE' interrupts.
 */
static irqreturn_t pxafb_handle_irq(int irq, void *dev_id)
{
	struct pxafb_info *fbi = dev_id;
	unsigned int lccr0, lcsr = lcd_readl(fbi, LCSR);

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

	down(&fbi->ctrlr_sem);

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
			pxafb_setup_gpio(fbi);
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
			pxafb_setup_gpio(fbi);
			pxafb_enable_controller(fbi);
			__pxafb_lcd_power(fbi, 1);
			__pxafb_backlight_power(fbi, 1);
		}
		break;
	}
	up(&fbi->ctrlr_sem);
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
	case CPUFREQ_INCOMPATIBLE:
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
static int pxafb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct pxafb_info *fbi = platform_get_drvdata(dev);

	set_ctrlr_state(fbi, C_DISABLE_PM);
	return 0;
}

static int pxafb_resume(struct platform_device *dev)
{
	struct pxafb_info *fbi = platform_get_drvdata(dev);

	set_ctrlr_state(fbi, C_ENABLE_PM);
	return 0;
}
#else
#define pxafb_suspend	NULL
#define pxafb_resume	NULL
#endif

/*
 * pxafb_map_video_memory():
 *      Allocates the DRAM memory for the frame buffer.  This buffer is
 *	remapped into a non-cached, non-buffered, memory region to
 *      allow palette and pixel writes to occur without flushing the
 *      cache.  Once this area is remapped, all virtual memory
 *      access to the video memory should occur at the new region.
 */
static int __init pxafb_map_video_memory(struct pxafb_info *fbi)
{
	/*
	 * We reserve one page for the palette, plus the size
	 * of the framebuffer.
	 */
	fbi->video_offset = PAGE_ALIGN(sizeof(struct pxafb_dma_buff));
	fbi->map_size = PAGE_ALIGN(fbi->fb.fix.smem_len + fbi->video_offset);
	fbi->map_cpu = dma_alloc_writecombine(fbi->dev, fbi->map_size,
					      &fbi->map_dma, GFP_KERNEL);

	if (fbi->map_cpu) {
		/* prevent initial garbage on screen */
		memset(fbi->map_cpu, 0, fbi->map_size);
		fbi->fb.screen_base = fbi->map_cpu + fbi->video_offset;
		fbi->screen_dma = fbi->map_dma + fbi->video_offset;

		/*
		 * FIXME: this is actually the wrong thing to place in
		 * smem_start.  But fbdev suffers from the problem that
		 * it needs an API which doesn't exist (in this case,
		 * dma_writecombine_mmap)
		 */
		fbi->fb.fix.smem_start = fbi->screen_dma;
		fbi->palette_size = fbi->fb.var.bits_per_pixel == 8 ? 256 : 16;

		fbi->dma_buff = (void *) fbi->map_cpu;
		fbi->dma_buff_phys = fbi->map_dma;
		fbi->palette_cpu = (u16 *) fbi->dma_buff->palette;

#ifdef CONFIG_FB_PXA_SMARTPANEL
		fbi->smart_cmds = (uint16_t *) fbi->dma_buff->cmd_buff;
		fbi->n_smart_cmds = 0;
#endif
	}

	return fbi->map_cpu ? 0 : -ENOMEM;
}

static void pxafb_decode_mode_info(struct pxafb_info *fbi,
				   struct pxafb_mode_info *modes,
				   unsigned int num_modes)
{
	unsigned int i, smemlen;

	pxafb_setmode(&fbi->fb.var, &modes[0]);

	for (i = 0; i < num_modes; i++) {
		smemlen = modes[i].xres * modes[i].yres * modes[i].bpp / 8;
		if (smemlen > fbi->fb.fix.smem_len)
			fbi->fb.fix.smem_len = smemlen;
	}
}

static void pxafb_decode_mach_info(struct pxafb_info *fbi,
				   struct pxafb_mach_info *inf)
{
	unsigned int lcd_conn = inf->lcd_conn;

	fbi->cmap_inverse	= inf->cmap_inverse;
	fbi->cmap_static	= inf->cmap_static;

	switch (lcd_conn & 0xf) {
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
		fbi->lccr4 = inf->lccr4;
		goto decode_mode;
	}

	if (lcd_conn == LCD_MONO_STN_8BPP)
		fbi->lccr0 |= LCCR0_DPD;

	fbi->lccr3 = LCCR3_Acb((inf->lcd_conn >> 10) & 0xff);
	fbi->lccr3 |= (lcd_conn & LCD_BIAS_ACTIVE_LOW) ? LCCR3_OEP : 0;
	fbi->lccr3 |= (lcd_conn & LCD_PCLK_EDGE_FALL)  ? LCCR3_PCP : 0;

decode_mode:
	pxafb_decode_mode_info(fbi, inf->modes, inf->num_modes);
}

static struct pxafb_info * __init pxafb_init_fbinfo(struct device *dev)
{
	struct pxafb_info *fbi;
	void *addr;
	struct pxafb_mach_info *inf = dev->platform_data;
	struct pxafb_mode_info *mode = inf->modes;

	/* Alloc the pxafb_info and pseudo_palette in one step */
	fbi = kmalloc(sizeof(struct pxafb_info) + sizeof(u32) * 16, GFP_KERNEL);
	if (!fbi)
		return NULL;

	memset(fbi, 0, sizeof(struct pxafb_info));
	fbi->dev = dev;

	fbi->clk = clk_get(dev, "LCDCLK");
	if (IS_ERR(fbi->clk)) {
		kfree(fbi);
		return NULL;
	}

	strcpy(fbi->fb.fix.id, PXA_NAME);

	fbi->fb.fix.type	= FB_TYPE_PACKED_PIXELS;
	fbi->fb.fix.type_aux	= 0;
	fbi->fb.fix.xpanstep	= 0;
	fbi->fb.fix.ypanstep	= 0;
	fbi->fb.fix.ywrapstep	= 0;
	fbi->fb.fix.accel	= FB_ACCEL_NONE;

	fbi->fb.var.nonstd	= 0;
	fbi->fb.var.activate	= FB_ACTIVATE_NOW;
	fbi->fb.var.height	= -1;
	fbi->fb.var.width	= -1;
	fbi->fb.var.accel_flags	= 0;
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

	init_waitqueue_head(&fbi->ctrlr_wait);
	INIT_WORK(&fbi->task, pxafb_task);
	init_MUTEX(&fbi->ctrlr_sem);
	init_completion(&fbi->disable_done);
#ifdef CONFIG_FB_PXA_SMARTPANEL
	init_completion(&fbi->command_done);
	init_completion(&fbi->refresh_done);
#endif

	return fbi;
}

#ifdef CONFIG_FB_PXA_PARAMETERS
static int __init parse_opt_mode(struct device *dev, const char *this_opt)
{
	struct pxafb_mach_info *inf = dev->platform_data;

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

static int __init parse_opt(struct device *dev, char *this_opt)
{
	struct pxafb_mach_info *inf = dev->platform_data;
	struct pxafb_mode_info *mode = &inf->modes[0];
	char s[64];

	s[0] = '\0';

	if (!strncmp(this_opt, "mode:", 5)) {
		return parse_opt_mode(dev, this_opt);
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

static int __init pxafb_parse_options(struct device *dev, char *options)
{
	char *this_opt;
	int ret;

	if (!options || !*options)
		return 0;

	dev_dbg(dev, "options are \"%s\"\n", options ? options : "null");

	/* could be made table driven or similar?... */
	while ((this_opt = strsep(&options, ",")) != NULL) {
		ret = parse_opt(dev, this_opt);
		if (ret)
			return ret;
	}
	return 0;
}

static char g_options[256] __devinitdata = "";

#ifndef CONFIG_MODULES
static int __devinit pxafb_setup_options(void)
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

static int __init pxafb_probe(struct platform_device *dev)
{
	struct pxafb_info *fbi;
	struct pxafb_mach_info *inf;
	struct resource *r;
	int irq, ret;

	dev_dbg(&dev->dev, "pxafb_probe\n");

	inf = dev->dev.platform_data;
	ret = -ENOMEM;
	fbi = NULL;
	if (!inf)
		goto failed;

	ret = pxafb_parse_options(&dev->dev, g_options);
	if (ret < 0)
		goto failed;

#ifdef DEBUG_VAR
	/* Check for various illegal bit-combinations. Currently only
	 * a warning is given. */

	if (inf->lccr0 & LCCR0_INVALID_CONFIG_MASK)
		dev_warn(&dev->dev, "machine LCCR0 setting contains "
				"illegal bits: %08x\n",
			inf->lccr0 & LCCR0_INVALID_CONFIG_MASK);
	if (inf->lccr3 & LCCR3_INVALID_CONFIG_MASK)
		dev_warn(&dev->dev, "machine LCCR3 setting contains "
				"illegal bits: %08x\n",
			inf->lccr3 & LCCR3_INVALID_CONFIG_MASK);
	if (inf->lccr0 & LCCR0_DPD &&
	    ((inf->lccr0 & LCCR0_PAS) != LCCR0_Pas ||
	     (inf->lccr0 & LCCR0_SDS) != LCCR0_Sngl ||
	     (inf->lccr0 & LCCR0_CMS) != LCCR0_Mono))
		dev_warn(&dev->dev, "Double Pixel Data (DPD) mode is "
				"only valid in passive mono"
				" single panel mode\n");
	if ((inf->lccr0 & LCCR0_PAS) == LCCR0_Act &&
	    (inf->lccr0 & LCCR0_SDS) == LCCR0_Dual)
		dev_warn(&dev->dev, "Dual panel only valid in passive mode\n");
	if ((inf->lccr0 & LCCR0_PAS) == LCCR0_Pas &&
	     (inf->modes->upper_margin || inf->modes->lower_margin))
		dev_warn(&dev->dev, "Upper and lower margins must be 0 in "
				"passive mode\n");
#endif

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
	pxafb_backlight_power = inf->pxafb_backlight_power;
	pxafb_lcd_power = inf->pxafb_lcd_power;
	fbi = pxafb_init_fbinfo(&dev->dev);
	if (!fbi) {
		/* only reason for pxafb_init_fbinfo to fail is kmalloc */
		dev_err(&dev->dev, "Failed to initialize framebuffer device\n");
		ret = -ENOMEM;
		goto failed;
	}

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&dev->dev, "no I/O memory resource defined\n");
		ret = -ENODEV;
		goto failed;
	}

	r = request_mem_region(r->start, r->end - r->start + 1, dev->name);
	if (r == NULL) {
		dev_err(&dev->dev, "failed to request I/O memory\n");
		ret = -EBUSY;
		goto failed;
	}

	fbi->mmio_base = ioremap(r->start, r->end - r->start + 1);
	if (fbi->mmio_base == NULL) {
		dev_err(&dev->dev, "failed to map I/O memory\n");
		ret = -EBUSY;
		goto failed_free_res;
	}

	/* Initialize video memory */
	ret = pxafb_map_video_memory(fbi);
	if (ret) {
		dev_err(&dev->dev, "Failed to allocate video RAM: %d\n", ret);
		ret = -ENOMEM;
		goto failed_free_io;
	}

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		dev_err(&dev->dev, "no IRQ defined\n");
		ret = -ENODEV;
		goto failed_free_mem;
	}

	ret = request_irq(irq, pxafb_handle_irq, IRQF_DISABLED, "LCD", fbi);
	if (ret) {
		dev_err(&dev->dev, "request_irq failed: %d\n", ret);
		ret = -EBUSY;
		goto failed_free_mem;
	}

#ifdef CONFIG_FB_PXA_SMARTPANEL
	ret = pxafb_smart_init(fbi);
	if (ret) {
		dev_err(&dev->dev, "failed to initialize smartpanel\n");
		goto failed_free_irq;
	}
#endif
	/*
	 * This makes sure that our colour bitfield
	 * descriptors are correctly initialised.
	 */
	pxafb_check_var(&fbi->fb.var, &fbi->fb);
	pxafb_set_par(&fbi->fb);

	platform_set_drvdata(dev, fbi);

	ret = register_framebuffer(&fbi->fb);
	if (ret < 0) {
		dev_err(&dev->dev,
			"Failed to register framebuffer device: %d\n", ret);
		goto failed_free_irq;
	}

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

failed_free_irq:
	free_irq(irq, fbi);
failed_free_res:
	release_mem_region(r->start, r->end - r->start + 1);
failed_free_io:
	iounmap(fbi->mmio_base);
failed_free_mem:
	dma_free_writecombine(&dev->dev, fbi->map_size,
			fbi->map_cpu, fbi->map_dma);
failed:
	platform_set_drvdata(dev, NULL);
	kfree(fbi);
	return ret;
}

static struct platform_driver pxafb_driver = {
	.probe		= pxafb_probe,
	.suspend	= pxafb_suspend,
	.resume		= pxafb_resume,
	.driver		= {
		.name	= "pxa2xx-fb",
	},
};

static int __devinit pxafb_init(void)
{
	if (pxafb_setup_options())
		return -EINVAL;

	return platform_driver_register(&pxafb_driver);
}

module_init(pxafb_init);

MODULE_DESCRIPTION("loadable framebuffer driver for PXA");
MODULE_LICENSE("GPL");
