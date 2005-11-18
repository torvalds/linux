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

#include <linux/config.h>
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

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/div64.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/bitfield.h>
#include <asm/arch/pxafb.h>

/*
 * Complain if VAR is out of range.
 */
#define DEBUG_VAR 1

#include "pxafb.h"

/* Bits which should not be set in machine configuration structures */
#define LCCR0_INVALID_CONFIG_MASK (LCCR0_OUM|LCCR0_BM|LCCR0_QDM|LCCR0_DIS|LCCR0_EFM|LCCR0_IUM|LCCR0_SFM|LCCR0_LDM|LCCR0_ENB)
#define LCCR3_INVALID_CONFIG_MASK (LCCR3_HSP|LCCR3_VSP|LCCR3_PCD|LCCR3_BPP)

static void (*pxafb_backlight_power)(int);
static void (*pxafb_lcd_power)(int);

static int pxafb_activate_var(struct fb_var_screeninfo *var, struct pxafb_info *);
static void set_ctrlr_state(struct pxafb_info *fbi, u_int state);

#ifdef CONFIG_FB_PXA_PARAMETERS
#define PXAFB_OPTIONS_SIZE 256
static char g_options[PXAFB_OPTIONS_SIZE] __initdata = "";
#endif

static inline void pxafb_schedule_work(struct pxafb_info *fbi, u_int state)
{
	unsigned long flags;

	local_irq_save(flags);
	/*
	 * We need to handle two requests being made at the same time.
	 * There are two important cases:
	 *  1. When we are changing VT (C_REENABLE) while unblanking (C_ENABLE)
	 *     We must perform the unblanking, which will do our REENABLE for us.
	 *  2. When we are blanking, but immediately unblank before we have
	 *     blanked.  We do the "REENABLE" thing here as well, just to be sure.
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
	u_int val, ret = 1;

	if (regno < fbi->palette_size) {
		if (fbi->fb.var.grayscale) {
			val = ((blue >> 8) & 0x00ff);
		} else {
			val  = ((red   >>  0) & 0xf800);
			val |= ((green >>  5) & 0x07e0);
			val |= ((blue  >> 11) & 0x001f);
		}
		fbi->palette_cpu[regno] = val;
		ret = 0;
	}
	return ret;
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

extern unsigned int get_clk_frequency_khz(int info);
#endif

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

	if (var->xres < MIN_XRES)
		var->xres = MIN_XRES;
	if (var->yres < MIN_YRES)
		var->yres = MIN_YRES;
	if (var->xres > fbi->max_xres)
		var->xres = fbi->max_xres;
	if (var->yres > fbi->max_yres)
		var->yres = fbi->max_yres;
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
		var->red.offset = var->green.offset = var->blue.offset = var->transp.offset = 0;
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
	pr_debug("pxafb: true_color = %d\n", is_true_color);
	// do your machine-specific setup if needed
}

/*
 * pxafb_set_par():
 *	Set the user defined part of the display for the specified console
 */
static int pxafb_set_par(struct fb_info *info)
{
	struct pxafb_info *fbi = (struct pxafb_info *)info;
	struct fb_var_screeninfo *var = &info->var;
	unsigned long palette_mem_size;

	pr_debug("pxafb: set_par\n");

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
		fbi->palette_size = var->bits_per_pixel == 1 ? 4 : 1 << var->bits_per_pixel;

	palette_mem_size = fbi->palette_size * sizeof(u16);

	pr_debug("pxafb: palette_mem_size = 0x%08lx\n", palette_mem_size);

	fbi->palette_cpu = (u16 *)(fbi->map_cpu + PAGE_SIZE - palette_mem_size);
	fbi->palette_dma = fbi->map_dma + PAGE_SIZE - palette_mem_size;

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
 * Formal definition of the VESA spec:
 *  On
 *  	This refers to the state of the display when it is in full operation
 *  Stand-By
 *  	This defines an optional operating state of minimal power reduction with
 *  	the shortest recovery time
 *  Suspend
 *  	This refers to a level of power management in which substantial power
 *  	reduction is achieved by the display.  The display can have a longer
 *  	recovery time from this state than from the Stand-by state
 *  Off
 *  	This indicates that the display is consuming the lowest level of power
 *  	and is non-operational. Recovery from this state may optionally require
 *  	the user to manually power on the monitor
 *
 *  Now, the fbdev driver adds an additional state, (blank), where they
 *  turn off the video (maybe by colormap tricks), but don't mess with the
 *  video itself: think of it semantically between on and Stand-By.
 *
 *  So here's what we should do in our fbdev blank routine:
 *
 *  	VESA_NO_BLANKING (mode 0)	Video on,  front/back light on
 *  	VESA_VSYNC_SUSPEND (mode 1)  	Video on,  front/back light off
 *  	VESA_HSYNC_SUSPEND (mode 2)  	Video on,  front/back light off
 *  	VESA_POWERDOWN (mode 3)		Video off, front/back light off
 *
 *  This will match the matrox implementation.
 */

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

	pr_debug("pxafb: blank=%d\n", blank);

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
		//TODO if (pxafb_blank_helper) pxafb_blank_helper(blank);
		break;

	case FB_BLANK_UNBLANK:
		//TODO if (pxafb_blank_helper) pxafb_blank_helper(blank);
		if (fbi->fb.fix.visual == FB_VISUAL_PSEUDOCOLOR ||
		    fbi->fb.fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR)
			fb_set_cmap(&fbi->fb.cmap, info);
		pxafb_schedule_work(fbi, C_ENABLE);
	}
	return 0;
}

static int pxafb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma)
{
	struct pxafb_info *fbi = (struct pxafb_info *)info;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;

	if (off < info->fix.smem_len) {
		vma->vm_pgoff += 1;
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
static inline unsigned int get_pcd(unsigned int pixclock)
{
	unsigned long long pcd;

	/* FIXME: Need to take into account Double Pixel Clock mode
         * (DPC) bit? or perhaps set it based on the various clock
         * speeds */

	pcd = (unsigned long long)get_lcdclk_frequency_10khz() * pixclock;
	do_div(pcd, 100000000 * 2);
	/* no need for this, since we should subtract 1 anyway. they cancel */
	/* pcd += 1; */ /* make up for integer math truncations */
	return (unsigned int)pcd;
}

/*
 * Some touchscreens need hsync information from the video driver to
 * function correctly. We export it here.
 */
static inline void set_hsync_time(struct pxafb_info *fbi, unsigned int pcd)
{
	unsigned long long htime;

	if ((pcd == 0) || (fbi->fb.var.hsync_len == 0)) {
		fbi->hsync_time=0;
		return;
	}

	htime = (unsigned long long)get_lcdclk_frequency_10khz() * 10000;
	do_div(htime, pcd * fbi->fb.var.hsync_len);
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

/*
 * pxafb_activate_var():
 *	Configures LCD Controller based on entries in var parameter.  Settings are
 *	only written to the controller if changes were made.
 */
static int pxafb_activate_var(struct fb_var_screeninfo *var, struct pxafb_info *fbi)
{
	struct pxafb_lcd_reg new_regs;
	u_long flags;
	u_int lines_per_panel, pcd = get_pcd(var->pixclock);

	pr_debug("pxafb: Configuring PXA LCD\n");

	pr_debug("var: xres=%d hslen=%d lm=%d rm=%d\n",
		 var->xres, var->hsync_len,
		 var->left_margin, var->right_margin);
	pr_debug("var: yres=%d vslen=%d um=%d bm=%d\n",
		 var->yres, var->vsync_len,
		 var->upper_margin, var->lower_margin);
	pr_debug("var: pixclock=%d pcd=%d\n", var->pixclock, pcd);

#if DEBUG_VAR
	if (var->xres < 16        || var->xres > 1024)
		printk(KERN_ERR "%s: invalid xres %d\n",
			fbi->fb.fix.id, var->xres);
	switch(var->bits_per_pixel) {
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
	if (var->hsync_len < 1    || var->hsync_len > 64)
		printk(KERN_ERR "%s: invalid hsync_len %d\n",
			fbi->fb.fix.id, var->hsync_len);
	if (var->left_margin < 1  || var->left_margin > 255)
		printk(KERN_ERR "%s: invalid left_margin %d\n",
			fbi->fb.fix.id, var->left_margin);
	if (var->right_margin < 1 || var->right_margin > 255)
		printk(KERN_ERR "%s: invalid right_margin %d\n",
			fbi->fb.fix.id, var->right_margin);
	if (var->yres < 1         || var->yres > 1024)
		printk(KERN_ERR "%s: invalid yres %d\n",
			fbi->fb.fix.id, var->yres);
	if (var->vsync_len < 1    || var->vsync_len > 64)
		printk(KERN_ERR "%s: invalid vsync_len %d\n",
			fbi->fb.fix.id, var->vsync_len);
	if (var->upper_margin < 0 || var->upper_margin > 255)
		printk(KERN_ERR "%s: invalid upper_margin %d\n",
			fbi->fb.fix.id, var->upper_margin);
	if (var->lower_margin < 0 || var->lower_margin > 255)
		printk(KERN_ERR "%s: invalid lower_margin %d\n",
			fbi->fb.fix.id, var->lower_margin);
#endif

	new_regs.lccr0 = fbi->lccr0 |
		(LCCR0_LDM | LCCR0_SFM | LCCR0_IUM | LCCR0_EFM |
                 LCCR0_QDM | LCCR0_BM  | LCCR0_OUM);

	new_regs.lccr1 =
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

	new_regs.lccr2 =
		LCCR2_DisHght(lines_per_panel) +
		LCCR2_VrtSnchWdth(var->vsync_len) +
		LCCR2_BegFrmDel(var->upper_margin) +
		LCCR2_EndFrmDel(var->lower_margin);

	new_regs.lccr3 = fbi->lccr3 |
		pxafb_bpp_to_lccr3(var) |
		(var->sync & FB_SYNC_HOR_HIGH_ACT ? LCCR3_HorSnchH : LCCR3_HorSnchL) |
		(var->sync & FB_SYNC_VERT_HIGH_ACT ? LCCR3_VrtSnchH : LCCR3_VrtSnchL);

	if (pcd)
		new_regs.lccr3 |= LCCR3_PixClkDiv(pcd);

	pr_debug("nlccr0 = 0x%08x\n", new_regs.lccr0);
	pr_debug("nlccr1 = 0x%08x\n", new_regs.lccr1);
	pr_debug("nlccr2 = 0x%08x\n", new_regs.lccr2);
	pr_debug("nlccr3 = 0x%08x\n", new_regs.lccr3);

	/* Update shadow copy atomically */
	local_irq_save(flags);

	/* setup dma descriptors */
	fbi->dmadesc_fblow_cpu = (struct pxafb_dma_descriptor *)((unsigned int)fbi->palette_cpu - 3*16);
	fbi->dmadesc_fbhigh_cpu = (struct pxafb_dma_descriptor *)((unsigned int)fbi->palette_cpu - 2*16);
	fbi->dmadesc_palette_cpu = (struct pxafb_dma_descriptor *)((unsigned int)fbi->palette_cpu - 1*16);

	fbi->dmadesc_fblow_dma = fbi->palette_dma - 3*16;
	fbi->dmadesc_fbhigh_dma = fbi->palette_dma - 2*16;
	fbi->dmadesc_palette_dma = fbi->palette_dma - 1*16;

#define BYTES_PER_PANEL (lines_per_panel * fbi->fb.fix.line_length)

	/* populate descriptors */
	fbi->dmadesc_fblow_cpu->fdadr = fbi->dmadesc_fblow_dma;
	fbi->dmadesc_fblow_cpu->fsadr = fbi->screen_dma + BYTES_PER_PANEL;
	fbi->dmadesc_fblow_cpu->fidr  = 0;
	fbi->dmadesc_fblow_cpu->ldcmd = BYTES_PER_PANEL;

	fbi->fdadr1 = fbi->dmadesc_fblow_dma; /* only used in dual-panel mode */

	fbi->dmadesc_fbhigh_cpu->fsadr = fbi->screen_dma;
	fbi->dmadesc_fbhigh_cpu->fidr = 0;
	fbi->dmadesc_fbhigh_cpu->ldcmd = BYTES_PER_PANEL;

	fbi->dmadesc_palette_cpu->fsadr = fbi->palette_dma;
	fbi->dmadesc_palette_cpu->fidr  = 0;
	fbi->dmadesc_palette_cpu->ldcmd = (fbi->palette_size * 2) | LDCMD_PAL;

	if (var->bits_per_pixel == 16) {
		/* palette shouldn't be loaded in true-color mode */
		fbi->dmadesc_fbhigh_cpu->fdadr = fbi->dmadesc_fbhigh_dma;
		fbi->fdadr0 = fbi->dmadesc_fbhigh_dma; /* no pal just fbhigh */
		/* init it to something, even though we won't be using it */
		fbi->dmadesc_palette_cpu->fdadr = fbi->dmadesc_palette_dma;
	} else {
		fbi->dmadesc_palette_cpu->fdadr = fbi->dmadesc_fbhigh_dma;
		fbi->dmadesc_fbhigh_cpu->fdadr = fbi->dmadesc_palette_dma;
		fbi->fdadr0 = fbi->dmadesc_palette_dma; /* flips back and forth between pal and fbhigh */
	}

#if 0
	pr_debug("fbi->dmadesc_fblow_cpu = 0x%p\n", fbi->dmadesc_fblow_cpu);
	pr_debug("fbi->dmadesc_fbhigh_cpu = 0x%p\n", fbi->dmadesc_fbhigh_cpu);
	pr_debug("fbi->dmadesc_palette_cpu = 0x%p\n", fbi->dmadesc_palette_cpu);
	pr_debug("fbi->dmadesc_fblow_dma = 0x%x\n", fbi->dmadesc_fblow_dma);
	pr_debug("fbi->dmadesc_fbhigh_dma = 0x%x\n", fbi->dmadesc_fbhigh_dma);
	pr_debug("fbi->dmadesc_palette_dma = 0x%x\n", fbi->dmadesc_palette_dma);

	pr_debug("fbi->dmadesc_fblow_cpu->fdadr = 0x%x\n", fbi->dmadesc_fblow_cpu->fdadr);
	pr_debug("fbi->dmadesc_fbhigh_cpu->fdadr = 0x%x\n", fbi->dmadesc_fbhigh_cpu->fdadr);
	pr_debug("fbi->dmadesc_palette_cpu->fdadr = 0x%x\n", fbi->dmadesc_palette_cpu->fdadr);

	pr_debug("fbi->dmadesc_fblow_cpu->fsadr = 0x%x\n", fbi->dmadesc_fblow_cpu->fsadr);
	pr_debug("fbi->dmadesc_fbhigh_cpu->fsadr = 0x%x\n", fbi->dmadesc_fbhigh_cpu->fsadr);
	pr_debug("fbi->dmadesc_palette_cpu->fsadr = 0x%x\n", fbi->dmadesc_palette_cpu->fsadr);

	pr_debug("fbi->dmadesc_fblow_cpu->ldcmd = 0x%x\n", fbi->dmadesc_fblow_cpu->ldcmd);
	pr_debug("fbi->dmadesc_fbhigh_cpu->ldcmd = 0x%x\n", fbi->dmadesc_fbhigh_cpu->ldcmd);
	pr_debug("fbi->dmadesc_palette_cpu->ldcmd = 0x%x\n", fbi->dmadesc_palette_cpu->ldcmd);
#endif

	fbi->reg_lccr0 = new_regs.lccr0;
	fbi->reg_lccr1 = new_regs.lccr1;
	fbi->reg_lccr2 = new_regs.lccr2;
	fbi->reg_lccr3 = new_regs.lccr3;
	set_hsync_time(fbi, pcd);
	local_irq_restore(flags);

	/*
	 * Only update the registers if the controller is enabled
	 * and something has changed.
	 */
	if ((LCCR0  != fbi->reg_lccr0) || (LCCR1  != fbi->reg_lccr1) ||
	    (LCCR2  != fbi->reg_lccr2) || (LCCR3  != fbi->reg_lccr3) ||
	    (FDADR0 != fbi->fdadr0)    || (FDADR1 != fbi->fdadr1))
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
		pxafb_lcd_power(on);
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
		  ((lccr0 & LCCR0_SDS) == LCCR0_Dual || (lccr0 & LCCR0_DPD) == LCCR0_8PixMono)) ||
                 ((lccr0 & LCCR0_CMS) == LCCR0_Color &&
		  (lccr0 & LCCR0_PAS) == LCCR0_Pas && (lccr0 & LCCR0_SDS) == LCCR0_Sngl))
		ldd_bits = 8;

	/* 16 bit interface */
	else if ((lccr0 & LCCR0_CMS) == LCCR0_Color &&
		 ((lccr0 & LCCR0_SDS) == LCCR0_Dual || (lccr0 & LCCR0_PAS) == LCCR0_Act))
		ldd_bits = 16;

	else {
	        printk(KERN_ERR "pxafb_setup_gpio: unable to determine bits per pixel\n");
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
	pr_debug("fdadr0 0x%08x\n", (unsigned int) fbi->fdadr0);
	pr_debug("fdadr1 0x%08x\n", (unsigned int) fbi->fdadr1);
	pr_debug("reg_lccr0 0x%08x\n", (unsigned int) fbi->reg_lccr0);
	pr_debug("reg_lccr1 0x%08x\n", (unsigned int) fbi->reg_lccr1);
	pr_debug("reg_lccr2 0x%08x\n", (unsigned int) fbi->reg_lccr2);
	pr_debug("reg_lccr3 0x%08x\n", (unsigned int) fbi->reg_lccr3);

	/* enable LCD controller clock */
	pxa_set_cken(CKEN16_LCD, 1);

	/* Sequence from 11.7.10 */
	LCCR3 = fbi->reg_lccr3;
	LCCR2 = fbi->reg_lccr2;
	LCCR1 = fbi->reg_lccr1;
	LCCR0 = fbi->reg_lccr0 & ~LCCR0_ENB;

	FDADR0 = fbi->fdadr0;
	FDADR1 = fbi->fdadr1;
	LCCR0 |= LCCR0_ENB;

	pr_debug("FDADR0 0x%08x\n", (unsigned int) FDADR0);
	pr_debug("FDADR1 0x%08x\n", (unsigned int) FDADR1);
	pr_debug("LCCR0 0x%08x\n", (unsigned int) LCCR0);
	pr_debug("LCCR1 0x%08x\n", (unsigned int) LCCR1);
	pr_debug("LCCR2 0x%08x\n", (unsigned int) LCCR2);
	pr_debug("LCCR3 0x%08x\n", (unsigned int) LCCR3);
}

static void pxafb_disable_controller(struct pxafb_info *fbi)
{
	DECLARE_WAITQUEUE(wait, current);

	pr_debug("pxafb: disabling LCD controller\n");

	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&fbi->ctrlr_wait, &wait);

	LCSR = 0xffffffff;	/* Clear LCD Status Register */
	LCCR0 &= ~LCCR0_LDM;	/* Enable LCD Disable Done Interrupt */
	LCCR0 |= LCCR0_DIS;	/* Disable LCD Controller */

	schedule_timeout(20 * HZ / 1000);
	remove_wait_queue(&fbi->ctrlr_wait, &wait);

	/* disable LCD controller clock */
	pxa_set_cken(CKEN16_LCD, 0);
}

/*
 *  pxafb_handle_irq: Handle 'LCD DONE' interrupts.
 */
static irqreturn_t pxafb_handle_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pxafb_info *fbi = dev_id;
	unsigned int lcsr = LCSR;

	if (lcsr & LCSR_LDD) {
		LCCR0 |= LCCR0_LDM;
		wake_up(&fbi->ctrlr_wait);
	}

	LCSR = lcsr;
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
			//TODO __pxafb_lcd_power(fbi, 0);
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
			//TODO __pxafb_lcd_power(fbi, 1);
		}
		break;

	case C_REENABLE:
		/*
		 * Re-enable the controller only if it was already
		 * enabled.  This is so we reprogram the control
		 * registers.
		 */
		if (old_state == C_ENABLE) {
			pxafb_disable_controller(fbi);
			pxafb_setup_gpio(fbi);
			pxafb_enable_controller(fbi);
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
static void pxafb_task(void *dummy)
{
	struct pxafb_info *fbi = dummy;
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
	//TODO struct cpufreq_freqs *f = data;
	u_int pcd;

	switch (val) {
	case CPUFREQ_PRECHANGE:
		set_ctrlr_state(fbi, C_DISABLE_CLKCHANGE);
		break;

	case CPUFREQ_POSTCHANGE:
		pcd = get_pcd(fbi->fb.var.pixclock);
		set_hsync_time(fbi, pcd);
		fbi->reg_lccr3 = (fbi->reg_lccr3 & ~0xff) | LCCR3_PixClkDiv(pcd);
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
		printk(KERN_DEBUG "min dma period: %d ps, "
			"new clock %d kHz\n", pxafb_display_dma_period(var),
			policy->max);
		// TODO: fill in min/max values
		break;
#if 0
	case CPUFREQ_NOTIFY:
		printk(KERN_ERR "%s: got CPUFREQ_NOTIFY\n", __FUNCTION__);
		do {} while(0);
		/* todo: panic if min/max values aren't fulfilled
		 * [can't really happen unless there's a bug in the
		 * CPU policy verification process *
		 */
		break;
#endif
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
	u_long palette_mem_size;

	/*
	 * We reserve one page for the palette, plus the size
	 * of the framebuffer.
	 */
	fbi->map_size = PAGE_ALIGN(fbi->fb.fix.smem_len + PAGE_SIZE);
	fbi->map_cpu = dma_alloc_writecombine(fbi->dev, fbi->map_size,
					      &fbi->map_dma, GFP_KERNEL);

	if (fbi->map_cpu) {
		/* prevent initial garbage on screen */
		memset(fbi->map_cpu, 0, fbi->map_size);
		fbi->fb.screen_base = fbi->map_cpu + PAGE_SIZE;
		fbi->screen_dma = fbi->map_dma + PAGE_SIZE;
		/*
		 * FIXME: this is actually the wrong thing to place in
		 * smem_start.  But fbdev suffers from the problem that
		 * it needs an API which doesn't exist (in this case,
		 * dma_writecombine_mmap)
		 */
		fbi->fb.fix.smem_start = fbi->screen_dma;

		fbi->palette_size = fbi->fb.var.bits_per_pixel == 8 ? 256 : 16;

		palette_mem_size = fbi->palette_size * sizeof(u16);
		pr_debug("pxafb: palette_mem_size = 0x%08lx\n", palette_mem_size);

		fbi->palette_cpu = (u16 *)(fbi->map_cpu + PAGE_SIZE - palette_mem_size);
		fbi->palette_dma = fbi->map_dma + PAGE_SIZE - palette_mem_size;
	}

	return fbi->map_cpu ? 0 : -ENOMEM;
}

static struct pxafb_info * __init pxafb_init_fbinfo(struct device *dev)
{
	struct pxafb_info *fbi;
	void *addr;
	struct pxafb_mach_info *inf = dev->platform_data;

	/* Alloc the pxafb_info and pseudo_palette in one step */
	fbi = kmalloc(sizeof(struct pxafb_info) + sizeof(u32) * 16, GFP_KERNEL);
	if (!fbi)
		return NULL;

	memset(fbi, 0, sizeof(struct pxafb_info));
	fbi->dev = dev;

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

	fbi->max_xres			= inf->xres;
	fbi->fb.var.xres		= inf->xres;
	fbi->fb.var.xres_virtual	= inf->xres;
	fbi->max_yres			= inf->yres;
	fbi->fb.var.yres		= inf->yres;
	fbi->fb.var.yres_virtual	= inf->yres;
	fbi->max_bpp			= inf->bpp;
	fbi->fb.var.bits_per_pixel	= inf->bpp;
	fbi->fb.var.pixclock		= inf->pixclock;
	fbi->fb.var.hsync_len		= inf->hsync_len;
	fbi->fb.var.left_margin		= inf->left_margin;
	fbi->fb.var.right_margin	= inf->right_margin;
	fbi->fb.var.vsync_len		= inf->vsync_len;
	fbi->fb.var.upper_margin	= inf->upper_margin;
	fbi->fb.var.lower_margin	= inf->lower_margin;
	fbi->fb.var.sync		= inf->sync;
	fbi->fb.var.grayscale		= inf->cmap_greyscale;
	fbi->cmap_inverse		= inf->cmap_inverse;
	fbi->cmap_static		= inf->cmap_static;
	fbi->lccr0			= inf->lccr0;
	fbi->lccr3			= inf->lccr3;
	fbi->state			= C_STARTUP;
	fbi->task_state			= (u_char)-1;
	fbi->fb.fix.smem_len		= fbi->max_xres * fbi->max_yres *
					  fbi->max_bpp / 8;

	init_waitqueue_head(&fbi->ctrlr_wait);
	INIT_WORK(&fbi->task, pxafb_task, fbi);
	init_MUTEX(&fbi->ctrlr_sem);

	return fbi;
}

#ifdef CONFIG_FB_PXA_PARAMETERS
static int __init pxafb_parse_options(struct device *dev, char *options)
{
	struct pxafb_mach_info *inf = dev->platform_data;
	char *this_opt;

        if (!options || !*options)
                return 0;

	dev_dbg(dev, "options are \"%s\"\n", options ? options : "null");

	/* could be made table driven or similar?... */
        while ((this_opt = strsep(&options, ",")) != NULL) {
                if (!strncmp(this_opt, "mode:", 5)) {
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
				case '0'...'9':
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
				inf->xres = xres; inf->yres = yres;
			}
			if (bpp_specified)
				switch (bpp) {
				case 1:
				case 2:
				case 4:
				case 8:
				case 16:
					inf->bpp = bpp;
					dev_info(dev, "overriding bit depth: %d\n", bpp);
					break;
				default:
					dev_err(dev, "Depth %d is not valid\n", bpp);
				}
                } else if (!strncmp(this_opt, "pixclock:", 9)) {
                        inf->pixclock = simple_strtoul(this_opt+9, NULL, 0);
			dev_info(dev, "override pixclock: %ld\n", inf->pixclock);
                } else if (!strncmp(this_opt, "left:", 5)) {
                        inf->left_margin = simple_strtoul(this_opt+5, NULL, 0);
			dev_info(dev, "override left: %u\n", inf->left_margin);
                } else if (!strncmp(this_opt, "right:", 6)) {
                        inf->right_margin = simple_strtoul(this_opt+6, NULL, 0);
			dev_info(dev, "override right: %u\n", inf->right_margin);
                } else if (!strncmp(this_opt, "upper:", 6)) {
                        inf->upper_margin = simple_strtoul(this_opt+6, NULL, 0);
			dev_info(dev, "override upper: %u\n", inf->upper_margin);
                } else if (!strncmp(this_opt, "lower:", 6)) {
                        inf->lower_margin = simple_strtoul(this_opt+6, NULL, 0);
			dev_info(dev, "override lower: %u\n", inf->lower_margin);
                } else if (!strncmp(this_opt, "hsynclen:", 9)) {
                        inf->hsync_len = simple_strtoul(this_opt+9, NULL, 0);
			dev_info(dev, "override hsynclen: %u\n", inf->hsync_len);
                } else if (!strncmp(this_opt, "vsynclen:", 9)) {
                        inf->vsync_len = simple_strtoul(this_opt+9, NULL, 0);
			dev_info(dev, "override vsynclen: %u\n", inf->vsync_len);
                } else if (!strncmp(this_opt, "hsync:", 6)) {
                        if (simple_strtoul(this_opt+6, NULL, 0) == 0) {
				dev_info(dev, "override hsync: Active Low\n");
				inf->sync &= ~FB_SYNC_HOR_HIGH_ACT;
			} else {
				dev_info(dev, "override hsync: Active High\n");
				inf->sync |= FB_SYNC_HOR_HIGH_ACT;
			}
                } else if (!strncmp(this_opt, "vsync:", 6)) {
                        if (simple_strtoul(this_opt+6, NULL, 0) == 0) {
				dev_info(dev, "override vsync: Active Low\n");
				inf->sync &= ~FB_SYNC_VERT_HIGH_ACT;
			} else {
				dev_info(dev, "override vsync: Active High\n");
				inf->sync |= FB_SYNC_VERT_HIGH_ACT;
			}
                } else if (!strncmp(this_opt, "dpc:", 4)) {
                        if (simple_strtoul(this_opt+4, NULL, 0) == 0) {
				dev_info(dev, "override double pixel clock: false\n");
				inf->lccr3 &= ~LCCR3_DPC;
			} else {
				dev_info(dev, "override double pixel clock: true\n");
				inf->lccr3 |= LCCR3_DPC;
			}
                } else if (!strncmp(this_opt, "outputen:", 9)) {
                        if (simple_strtoul(this_opt+9, NULL, 0) == 0) {
				dev_info(dev, "override output enable: active low\n");
				inf->lccr3 = (inf->lccr3 & ~LCCR3_OEP) | LCCR3_OutEnL;
			} else {
				dev_info(dev, "override output enable: active high\n");
				inf->lccr3 = (inf->lccr3 & ~LCCR3_OEP) | LCCR3_OutEnH;
			}
                } else if (!strncmp(this_opt, "pixclockpol:", 12)) {
                        if (simple_strtoul(this_opt+12, NULL, 0) == 0) {
				dev_info(dev, "override pixel clock polarity: falling edge\n");
				inf->lccr3 = (inf->lccr3 & ~LCCR3_PCP) | LCCR3_PixFlEdg;
			} else {
				dev_info(dev, "override pixel clock polarity: rising edge\n");
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
        }
        return 0;

}
#endif

int __init pxafb_probe(struct platform_device *dev)
{
	struct pxafb_info *fbi;
	struct pxafb_mach_info *inf;
	int ret;

	dev_dbg(dev, "pxafb_probe\n");

	inf = dev->dev.platform_data;
	ret = -ENOMEM;
	fbi = NULL;
	if (!inf)
		goto failed;

#ifdef CONFIG_FB_PXA_PARAMETERS
	ret = pxafb_parse_options(&dev->dev, g_options);
	if (ret < 0)
		goto failed;
#endif

#ifdef DEBUG_VAR
        /* Check for various illegal bit-combinations. Currently only
	 * a warning is given. */

        if (inf->lccr0 & LCCR0_INVALID_CONFIG_MASK)
                dev_warn(&dev->dev, "machine LCCR0 setting contains illegal bits: %08x\n",
                        inf->lccr0 & LCCR0_INVALID_CONFIG_MASK);
        if (inf->lccr3 & LCCR3_INVALID_CONFIG_MASK)
                dev_warn(&dev->dev, "machine LCCR3 setting contains illegal bits: %08x\n",
                        inf->lccr3 & LCCR3_INVALID_CONFIG_MASK);
        if (inf->lccr0 & LCCR0_DPD &&
	    ((inf->lccr0 & LCCR0_PAS) != LCCR0_Pas ||
	     (inf->lccr0 & LCCR0_SDS) != LCCR0_Sngl ||
	     (inf->lccr0 & LCCR0_CMS) != LCCR0_Mono))
                dev_warn(&dev->dev, "Double Pixel Data (DPD) mode is only valid in passive mono"
			 " single panel mode\n");
        if ((inf->lccr0 & LCCR0_PAS) == LCCR0_Act &&
	    (inf->lccr0 & LCCR0_SDS) == LCCR0_Dual)
                dev_warn(&dev->dev, "Dual panel only valid in passive mode\n");
        if ((inf->lccr0 & LCCR0_PAS) == LCCR0_Pas &&
             (inf->upper_margin || inf->lower_margin))
                dev_warn(&dev->dev, "Upper and lower margins must be 0 in passive mode\n");
#endif

	dev_dbg(&dev->dev, "got a %dx%dx%d LCD\n",inf->xres, inf->yres, inf->bpp);
	if (inf->xres == 0 || inf->yres == 0 || inf->bpp == 0) {
		dev_err(&dev->dev, "Invalid resolution or bit depth\n");
		ret = -EINVAL;
		goto failed;
	}
	pxafb_backlight_power = inf->pxafb_backlight_power;
	pxafb_lcd_power = inf->pxafb_lcd_power;
	fbi = pxafb_init_fbinfo(&dev->dev);
	if (!fbi) {
		dev_err(&dev->dev, "Failed to initialize framebuffer device\n");
		ret = -ENOMEM; // only reason for pxafb_init_fbinfo to fail is kmalloc
		goto failed;
	}

	/* Initialize video memory */
	ret = pxafb_map_video_memory(fbi);
	if (ret) {
		dev_err(&dev->dev, "Failed to allocate video RAM: %d\n", ret);
		ret = -ENOMEM;
		goto failed;
	}

	ret = request_irq(IRQ_LCD, pxafb_handle_irq, SA_INTERRUPT, "LCD", fbi);
	if (ret) {
		dev_err(&dev->dev, "request_irq failed: %d\n", ret);
		ret = -EBUSY;
		goto failed;
	}

	/*
	 * This makes sure that our colour bitfield
	 * descriptors are correctly initialised.
	 */
	pxafb_check_var(&fbi->fb.var, &fbi->fb);
	pxafb_set_par(&fbi->fb);

	platform_set_drvdata(dev, fbi);

	ret = register_framebuffer(&fbi->fb);
	if (ret < 0) {
		dev_err(&dev->dev, "Failed to register framebuffer device: %d\n", ret);
		goto failed;
	}

#ifdef CONFIG_PM
	// TODO
#endif

#ifdef CONFIG_CPU_FREQ
	fbi->freq_transition.notifier_call = pxafb_freq_transition;
	fbi->freq_policy.notifier_call = pxafb_freq_policy;
	cpufreq_register_notifier(&fbi->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
	cpufreq_register_notifier(&fbi->freq_policy, CPUFREQ_POLICY_NOTIFIER);
#endif

	/*
	 * Ok, now enable the LCD controller
	 */
	set_ctrlr_state(fbi, C_ENABLE);

	return 0;

failed:
	platform_set_drvdata(dev, NULL);
	kfree(fbi);
	return ret;
}

static struct platform_driver pxafb_driver = {
	.probe		= pxafb_probe,
#ifdef CONFIG_PM
	.suspend	= pxafb_suspend,
	.resume		= pxafb_resume,
#endif
	.driver		= {
		.name	= "pxa2xx-fb",
	},
};

#ifndef MODULE
int __devinit pxafb_setup(char *options)
{
# ifdef CONFIG_FB_PXA_PARAMETERS
	strlcpy(g_options, options, sizeof(g_options));
# endif
	return 0;
}
#else
# ifdef CONFIG_FB_PXA_PARAMETERS
module_param_string(options, g_options, sizeof(g_options), 0);
MODULE_PARM_DESC(options, "LCD parameters (see Documentation/fb/pxafb.txt)");
# endif
#endif

int __devinit pxafb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("pxafb", &option))
		return -ENODEV;
	pxafb_setup(option);
#endif
	return platform_driver_register(&pxafb_driver);
}

module_init(pxafb_init);

MODULE_DESCRIPTION("loadable framebuffer driver for PXA");
MODULE_LICENSE("GPL");
