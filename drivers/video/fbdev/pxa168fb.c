/*
 * linux/drivers/video/pxa168fb.c -- Marvell PXA168 LCD Controller
 *
 *  Copyright (C) 2008 Marvell International Ltd.
 *  All rights reserved.
 *
 *  2009-02-16  adapted from original version for PXA168/910
 *              Jun Nie <njun@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <video/pxa168fb.h>

#include "pxa168fb.h"

#define DEFAULT_REFRESH		60	/* Hz */

static int determine_best_pix_fmt(struct fb_var_screeninfo *var)
{
	/*
	 * Pseudocolor mode?
	 */
	if (var->bits_per_pixel == 8)
		return PIX_FMT_PSEUDOCOLOR;

	/*
	 * Check for 565/1555.
	 */
	if (var->bits_per_pixel == 16 && var->red.length <= 5 &&
	    var->green.length <= 6 && var->blue.length <= 5) {
		if (var->transp.length == 0) {
			if (var->red.offset >= var->blue.offset)
				return PIX_FMT_RGB565;
			else
				return PIX_FMT_BGR565;
		}

		if (var->transp.length == 1 && var->green.length <= 5) {
			if (var->red.offset >= var->blue.offset)
				return PIX_FMT_RGB1555;
			else
				return PIX_FMT_BGR1555;
		}

		/* fall through */
	}

	/*
	 * Check for 888/A888.
	 */
	if (var->bits_per_pixel <= 32 && var->red.length <= 8 &&
	    var->green.length <= 8 && var->blue.length <= 8) {
		if (var->bits_per_pixel == 24 && var->transp.length == 0) {
			if (var->red.offset >= var->blue.offset)
				return PIX_FMT_RGB888PACK;
			else
				return PIX_FMT_BGR888PACK;
		}

		if (var->bits_per_pixel == 32 && var->transp.length == 8) {
			if (var->red.offset >= var->blue.offset)
				return PIX_FMT_RGBA888;
			else
				return PIX_FMT_BGRA888;
		} else {
			if (var->red.offset >= var->blue.offset)
				return PIX_FMT_RGB888UNPACK;
			else
				return PIX_FMT_BGR888UNPACK;
		}

		/* fall through */
	}

	return -EINVAL;
}

static void set_pix_fmt(struct fb_var_screeninfo *var, int pix_fmt)
{
	switch (pix_fmt) {
	case PIX_FMT_RGB565:
		var->bits_per_pixel = 16;
		var->red.offset = 11;    var->red.length = 5;
		var->green.offset = 5;   var->green.length = 6;
		var->blue.offset = 0;    var->blue.length = 5;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIX_FMT_BGR565:
		var->bits_per_pixel = 16;
		var->red.offset = 0;     var->red.length = 5;
		var->green.offset = 5;   var->green.length = 6;
		var->blue.offset = 11;   var->blue.length = 5;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIX_FMT_RGB1555:
		var->bits_per_pixel = 16;
		var->red.offset = 10;    var->red.length = 5;
		var->green.offset = 5;   var->green.length = 5;
		var->blue.offset = 0;    var->blue.length = 5;
		var->transp.offset = 15; var->transp.length = 1;
		break;
	case PIX_FMT_BGR1555:
		var->bits_per_pixel = 16;
		var->red.offset = 0;     var->red.length = 5;
		var->green.offset = 5;   var->green.length = 5;
		var->blue.offset = 10;   var->blue.length = 5;
		var->transp.offset = 15; var->transp.length = 1;
		break;
	case PIX_FMT_RGB888PACK:
		var->bits_per_pixel = 24;
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIX_FMT_BGR888PACK:
		var->bits_per_pixel = 24;
		var->red.offset = 0;     var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 16;   var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIX_FMT_RGBA888:
		var->bits_per_pixel = 32;
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 24; var->transp.length = 8;
		break;
	case PIX_FMT_BGRA888:
		var->bits_per_pixel = 32;
		var->red.offset = 0;     var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 16;   var->blue.length = 8;
		var->transp.offset = 24; var->transp.length = 8;
		break;
	case PIX_FMT_PSEUDOCOLOR:
		var->bits_per_pixel = 8;
		var->red.offset = 0;     var->red.length = 8;
		var->green.offset = 0;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	}
}

static void set_mode(struct pxa168fb_info *fbi, struct fb_var_screeninfo *var,
		     struct fb_videomode *mode, int pix_fmt, int ystretch)
{
	struct fb_info *info = fbi->info;

	set_pix_fmt(var, pix_fmt);

	var->xres = mode->xres;
	var->yres = mode->yres;
	var->xres_virtual = max(var->xres, var->xres_virtual);
	if (ystretch)
		var->yres_virtual = info->fix.smem_len /
			(var->xres_virtual * (var->bits_per_pixel >> 3));
	else
		var->yres_virtual = max(var->yres, var->yres_virtual);
	var->grayscale = 0;
	var->accel_flags = FB_ACCEL_NONE;
	var->pixclock = mode->pixclock;
	var->left_margin = mode->left_margin;
	var->right_margin = mode->right_margin;
	var->upper_margin = mode->upper_margin;
	var->lower_margin = mode->lower_margin;
	var->hsync_len = mode->hsync_len;
	var->vsync_len = mode->vsync_len;
	var->sync = mode->sync;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->rotate = FB_ROTATE_UR;
}

static int pxa168fb_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct pxa168fb_info *fbi = info->par;
	int pix_fmt;

	/*
	 * Determine which pixel format we're going to use.
	 */
	pix_fmt = determine_best_pix_fmt(var);
	if (pix_fmt < 0)
		return pix_fmt;
	set_pix_fmt(var, pix_fmt);
	fbi->pix_fmt = pix_fmt;

	/*
	 * Basic geometry sanity checks.
	 */
	if (var->xoffset + var->xres > var->xres_virtual)
		return -EINVAL;
	if (var->yoffset + var->yres > var->yres_virtual)
		return -EINVAL;
	if (var->xres + var->right_margin +
	    var->hsync_len + var->left_margin > 2048)
		return -EINVAL;
	if (var->yres + var->lower_margin +
	    var->vsync_len + var->upper_margin > 2048)
		return -EINVAL;

	/*
	 * Check size of framebuffer.
	 */
	if (var->xres_virtual * var->yres_virtual *
	    (var->bits_per_pixel >> 3) > info->fix.smem_len)
		return -EINVAL;

	return 0;
}

/*
 * The hardware clock divider has an integer and a fractional
 * stage:
 *
 *	clk2 = clk_in / integer_divider
 *	clk_out = clk2 * (1 - (fractional_divider >> 12))
 *
 * Calculate integer and fractional divider for given clk_in
 * and clk_out.
 */
static void set_clock_divider(struct pxa168fb_info *fbi,
			      const struct fb_videomode *m)
{
	int divider_int;
	int needed_pixclk;
	u64 div_result;
	u32 x = 0;

	/*
	 * Notice: The field pixclock is used by linux fb
	 * is in pixel second. E.g. struct fb_videomode &
	 * struct fb_var_screeninfo
	 */

	/*
	 * Check input values.
	 */
	if (!m || !m->pixclock || !m->refresh) {
		dev_err(fbi->dev, "Input refresh or pixclock is wrong.\n");
		return;
	}

	/*
	 * Using PLL/AXI clock.
	 */
	x = 0x80000000;

	/*
	 * Calc divider according to refresh rate.
	 */
	div_result = 1000000000000ll;
	do_div(div_result, m->pixclock);
	needed_pixclk = (u32)div_result;

	divider_int = clk_get_rate(fbi->clk) / needed_pixclk;

	/* check whether divisor is too small. */
	if (divider_int < 2) {
		dev_warn(fbi->dev, "Warning: clock source is too slow. "
				"Try smaller resolution\n");
		divider_int = 2;
	}

	/*
	 * Set setting to reg.
	 */
	x |= divider_int;
	writel(x, fbi->reg_base + LCD_CFG_SCLK_DIV);
}

static void set_dma_control0(struct pxa168fb_info *fbi)
{
	u32 x;

	/*
	 * Set bit to enable graphics DMA.
	 */
	x = readl(fbi->reg_base + LCD_SPU_DMA_CTRL0);
	x &= ~CFG_GRA_ENA_MASK;
	x |= fbi->active ? CFG_GRA_ENA(1) : CFG_GRA_ENA(0);

	/*
	 * If we are in a pseudo-color mode, we need to enable
	 * palette lookup.
	 */
	if (fbi->pix_fmt == PIX_FMT_PSEUDOCOLOR)
		x |= 0x10000000;

	/*
	 * Configure hardware pixel format.
	 */
	x &= ~(0xF << 16);
	x |= (fbi->pix_fmt >> 1) << 16;

	/*
	 * Check red and blue pixel swap.
	 * 1. source data swap
	 * 2. panel output data swap
	 */
	x &= ~(1 << 12);
	x |= ((fbi->pix_fmt & 1) ^ (fbi->panel_rbswap)) << 12;

	writel(x, fbi->reg_base + LCD_SPU_DMA_CTRL0);
}

static void set_dma_control1(struct pxa168fb_info *fbi, int sync)
{
	u32 x;

	/*
	 * Configure default bits: vsync triggers DMA, gated clock
	 * enable, power save enable, configure alpha registers to
	 * display 100% graphics, and set pixel command.
	 */
	x = readl(fbi->reg_base + LCD_SPU_DMA_CTRL1);
	x |= 0x2032ff81;

	/*
	 * We trigger DMA on the falling edge of vsync if vsync is
	 * active low, or on the rising edge if vsync is active high.
	 */
	if (!(sync & FB_SYNC_VERT_HIGH_ACT))
		x |= 0x08000000;

	writel(x, fbi->reg_base + LCD_SPU_DMA_CTRL1);
}

static void set_graphics_start(struct fb_info *info, int xoffset, int yoffset)
{
	struct pxa168fb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	int pixel_offset;
	unsigned long addr;

	pixel_offset = (yoffset * var->xres_virtual) + xoffset;

	addr = fbi->fb_start_dma + (pixel_offset * (var->bits_per_pixel >> 3));
	writel(addr, fbi->reg_base + LCD_CFG_GRA_START_ADDR0);
}

static void set_dumb_panel_control(struct fb_info *info)
{
	struct pxa168fb_info *fbi = info->par;
	struct pxa168fb_mach_info *mi = dev_get_platdata(fbi->dev);
	u32 x;

	/*
	 * Preserve enable flag.
	 */
	x = readl(fbi->reg_base + LCD_SPU_DUMB_CTRL) & 0x00000001;

	x |= (fbi->is_blanked ? 0x7 : mi->dumb_mode) << 28;
	x |= mi->gpio_output_data << 20;
	x |= mi->gpio_output_mask << 12;
	x |= mi->panel_rgb_reverse_lanes ? 0x00000080 : 0;
	x |= mi->invert_composite_blank ? 0x00000040 : 0;
	x |= (info->var.sync & FB_SYNC_COMP_HIGH_ACT) ? 0x00000020 : 0;
	x |= mi->invert_pix_val_ena ? 0x00000010 : 0;
	x |= (info->var.sync & FB_SYNC_VERT_HIGH_ACT) ? 0 : 0x00000008;
	x |= (info->var.sync & FB_SYNC_HOR_HIGH_ACT) ? 0 : 0x00000004;
	x |= mi->invert_pixclock ? 0x00000002 : 0;

	writel(x, fbi->reg_base + LCD_SPU_DUMB_CTRL);
}

static void set_dumb_screen_dimensions(struct fb_info *info)
{
	struct pxa168fb_info *fbi = info->par;
	struct fb_var_screeninfo *v = &info->var;
	int x;
	int y;

	x = v->xres + v->right_margin + v->hsync_len + v->left_margin;
	y = v->yres + v->lower_margin + v->vsync_len + v->upper_margin;

	writel((y << 16) | x, fbi->reg_base + LCD_SPUT_V_H_TOTAL);
}

static int pxa168fb_set_par(struct fb_info *info)
{
	struct pxa168fb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	struct fb_videomode mode;
	u32 x;

	/*
	 * Set additional mode info.
	 */
	if (fbi->pix_fmt == PIX_FMT_PSEUDOCOLOR)
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else
		info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;
	info->fix.ypanstep = var->yres;

	/*
	 * Disable panel output while we setup the display.
	 */
	x = readl(fbi->reg_base + LCD_SPU_DUMB_CTRL);
	writel(x & ~1, fbi->reg_base + LCD_SPU_DUMB_CTRL);

	/*
	 * Configure global panel parameters.
	 */
	writel((var->yres << 16) | var->xres,
		fbi->reg_base + LCD_SPU_V_H_ACTIVE);

	/*
	 * convet var to video mode
	 */
	fb_var_to_videomode(&mode, &info->var);

	/* Calculate clock divisor. */
	set_clock_divider(fbi, &mode);

	/* Configure dma ctrl regs. */
	set_dma_control0(fbi);
	set_dma_control1(fbi, info->var.sync);

	/*
	 * Configure graphics DMA parameters.
	 */
	x = readl(fbi->reg_base + LCD_CFG_GRA_PITCH);
	x = (x & ~0xFFFF) | ((var->xres_virtual * var->bits_per_pixel) >> 3);
	writel(x, fbi->reg_base + LCD_CFG_GRA_PITCH);
	writel((var->yres << 16) | var->xres,
		fbi->reg_base + LCD_SPU_GRA_HPXL_VLN);
	writel((var->yres << 16) | var->xres,
		fbi->reg_base + LCD_SPU_GZM_HPXL_VLN);

	/*
	 * Configure dumb panel ctrl regs & timings.
	 */
	set_dumb_panel_control(info);
	set_dumb_screen_dimensions(info);

	writel((var->left_margin << 16) | var->right_margin,
			fbi->reg_base + LCD_SPU_H_PORCH);
	writel((var->upper_margin << 16) | var->lower_margin,
			fbi->reg_base + LCD_SPU_V_PORCH);

	/*
	 * Re-enable panel output.
	 */
	x = readl(fbi->reg_base + LCD_SPU_DUMB_CTRL);
	writel(x | 1, fbi->reg_base + LCD_SPU_DUMB_CTRL);

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

static int
pxa168fb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
		 unsigned int blue, unsigned int trans, struct fb_info *info)
{
	struct pxa168fb_info *fbi = info->par;
	u32 val;

	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
					7471 * blue) >> 16;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR && regno < 16) {
		val =  chan_to_field(red,   &info->var.red);
		val |= chan_to_field(green, &info->var.green);
		val |= chan_to_field(blue , &info->var.blue);
		fbi->pseudo_palette[regno] = val;
	}

	if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR && regno < 256) {
		val = to_rgb(red, green, blue);
		writel(val, fbi->reg_base + LCD_SPU_SRAM_WRDAT);
		writel(0x8300 | regno, fbi->reg_base + LCD_SPU_SRAM_CTRL);
	}

	return 0;
}

static int pxa168fb_blank(int blank, struct fb_info *info)
{
	struct pxa168fb_info *fbi = info->par;

	fbi->is_blanked = (blank == FB_BLANK_UNBLANK) ? 0 : 1;
	set_dumb_panel_control(info);

	return 0;
}

static int pxa168fb_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	set_graphics_start(info, var->xoffset, var->yoffset);

	return 0;
}

static irqreturn_t pxa168fb_handle_irq(int irq, void *dev_id)
{
	struct pxa168fb_info *fbi = dev_id;
	u32 isr = readl(fbi->reg_base + SPU_IRQ_ISR);

	if ((isr & GRA_FRAME_IRQ0_ENA_MASK)) {

		writel(isr & (~GRA_FRAME_IRQ0_ENA_MASK),
			fbi->reg_base + SPU_IRQ_ISR);

		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static struct fb_ops pxa168fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= pxa168fb_check_var,
	.fb_set_par	= pxa168fb_set_par,
	.fb_setcolreg	= pxa168fb_setcolreg,
	.fb_blank	= pxa168fb_blank,
	.fb_pan_display	= pxa168fb_pan_display,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int pxa168fb_init_mode(struct fb_info *info,
			      struct pxa168fb_mach_info *mi)
{
	struct pxa168fb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	int ret = 0;
	u32 total_w, total_h, refresh;
	u64 div_result;
	const struct fb_videomode *m;

	/*
	 * Set default value
	 */
	refresh = DEFAULT_REFRESH;

	/* try to find best video mode. */
	m = fb_find_best_mode(&info->var, &info->modelist);
	if (m)
		fb_videomode_to_var(&info->var, m);

	/* Init settings. */
	var->xres_virtual = var->xres;
	var->yres_virtual = info->fix.smem_len /
		(var->xres_virtual * (var->bits_per_pixel >> 3));
	dev_dbg(fbi->dev, "pxa168fb: find best mode: res = %dx%d\n",
				var->xres, var->yres);

	/* correct pixclock. */
	total_w = var->xres + var->left_margin + var->right_margin +
		  var->hsync_len;
	total_h = var->yres + var->upper_margin + var->lower_margin +
		  var->vsync_len;

	div_result = 1000000000000ll;
	do_div(div_result, total_w * total_h * refresh);
	var->pixclock = (u32)div_result;

	return ret;
}

static int pxa168fb_probe(struct platform_device *pdev)
{
	struct pxa168fb_mach_info *mi;
	struct fb_info *info = 0;
	struct pxa168fb_info *fbi = 0;
	struct resource *res;
	struct clk *clk;
	int irq, ret;

	mi = dev_get_platdata(&pdev->dev);
	if (mi == NULL) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	clk = devm_clk_get(&pdev->dev, "LCDCLK");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get LCDCLK");
		return PTR_ERR(clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no IO memory defined\n");
		return -ENOENT;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no IRQ defined\n");
		return -ENOENT;
	}

	info = framebuffer_alloc(sizeof(struct pxa168fb_info), &pdev->dev);
	if (info == NULL) {
		return -ENOMEM;
	}

	/* Initialize private data */
	fbi = info->par;
	fbi->info = info;
	fbi->clk = clk;
	fbi->dev = info->dev = &pdev->dev;
	fbi->panel_rbswap = mi->panel_rbswap;
	fbi->is_blanked = 0;
	fbi->active = mi->active;

	/*
	 * Initialise static fb parameters.
	 */
	info->flags = FBINFO_DEFAULT | FBINFO_PARTIAL_PAN_OK |
		      FBINFO_HWACCEL_XPAN | FBINFO_HWACCEL_YPAN;
	info->node = -1;
	strlcpy(info->fix.id, mi->id, 16);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 0;
	info->fix.ywrapstep = 0;
	info->fix.mmio_start = res->start;
	info->fix.mmio_len = resource_size(res);
	info->fix.accel = FB_ACCEL_NONE;
	info->fbops = &pxa168fb_ops;
	info->pseudo_palette = fbi->pseudo_palette;

	/*
	 * Map LCD controller registers.
	 */
	fbi->reg_base = devm_ioremap_nocache(&pdev->dev, res->start,
					     resource_size(res));
	if (fbi->reg_base == NULL) {
		ret = -ENOMEM;
		goto failed_free_info;
	}

	/*
	 * Allocate framebuffer memory.
	 */
	info->fix.smem_len = PAGE_ALIGN(DEFAULT_FB_SIZE);

	info->screen_base = dma_alloc_wc(fbi->dev, info->fix.smem_len,
					 &fbi->fb_start_dma, GFP_KERNEL);
	if (info->screen_base == NULL) {
		ret = -ENOMEM;
		goto failed_free_info;
	}

	info->fix.smem_start = (unsigned long)fbi->fb_start_dma;
	set_graphics_start(info, 0, 0);

	/*
	 * Set video mode according to platform data.
	 */
	set_mode(fbi, &info->var, mi->modes, mi->pix_fmt, 1);

	fb_videomode_to_modelist(mi->modes, mi->num_modes, &info->modelist);

	/*
	 * init video mode data.
	 */
	pxa168fb_init_mode(info, mi);

	/*
	 * Fill in sane defaults.
	 */
	ret = pxa168fb_check_var(&info->var, info);
	if (ret)
		goto failed_free_fbmem;

	/*
	 * enable controller clock
	 */
	clk_prepare_enable(fbi->clk);

	pxa168fb_set_par(info);

	/*
	 * Configure default register values.
	 */
	writel(0, fbi->reg_base + LCD_SPU_BLANKCOLOR);
	writel(mi->io_pin_allocation_mode, fbi->reg_base + SPU_IOPAD_CONTROL);
	writel(0, fbi->reg_base + LCD_CFG_GRA_START_ADDR1);
	writel(0, fbi->reg_base + LCD_SPU_GRA_OVSA_HPXL_VLN);
	writel(0, fbi->reg_base + LCD_SPU_SRAM_PARA0);
	writel(CFG_CSB_256x32(0x1)|CFG_CSB_256x24(0x1)|CFG_CSB_256x8(0x1),
		fbi->reg_base + LCD_SPU_SRAM_PARA1);

	/*
	 * Allocate color map.
	 */
	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		ret = -ENOMEM;
		goto failed_free_clk;
	}

	/*
	 * Register irq handler.
	 */
	ret = devm_request_irq(&pdev->dev, irq, pxa168fb_handle_irq,
			       IRQF_SHARED, info->fix.id, fbi);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to request IRQ\n");
		ret = -ENXIO;
		goto failed_free_cmap;
	}

	/*
	 * Enable GFX interrupt
	 */
	writel(GRA_FRAME_IRQ0_ENA(0x1), fbi->reg_base + SPU_IRQ_ENA);

	/*
	 * Register framebuffer.
	 */
	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register pxa168-fb: %d\n", ret);
		ret = -ENXIO;
		goto failed_free_cmap;
	}

	platform_set_drvdata(pdev, fbi);
	return 0;

failed_free_cmap:
	fb_dealloc_cmap(&info->cmap);
failed_free_clk:
	clk_disable_unprepare(fbi->clk);
failed_free_fbmem:
	dma_free_coherent(fbi->dev, info->fix.smem_len,
			info->screen_base, fbi->fb_start_dma);
failed_free_info:
	kfree(info);

	dev_err(&pdev->dev, "frame buffer device init failed with %d\n", ret);
	return ret;
}

static int pxa168fb_remove(struct platform_device *pdev)
{
	struct pxa168fb_info *fbi = platform_get_drvdata(pdev);
	struct fb_info *info;
	int irq;
	unsigned int data;

	if (!fbi)
		return 0;

	/* disable DMA transfer */
	data = readl(fbi->reg_base + LCD_SPU_DMA_CTRL0);
	data &= ~CFG_GRA_ENA_MASK;
	writel(data, fbi->reg_base + LCD_SPU_DMA_CTRL0);

	info = fbi->info;

	unregister_framebuffer(info);

	writel(GRA_FRAME_IRQ0_ENA(0x0), fbi->reg_base + SPU_IRQ_ENA);

	if (info->cmap.len)
		fb_dealloc_cmap(&info->cmap);

	irq = platform_get_irq(pdev, 0);

	dma_free_wc(fbi->dev, PAGE_ALIGN(info->fix.smem_len),
		    info->screen_base, info->fix.smem_start);

	clk_disable_unprepare(fbi->clk);

	framebuffer_release(info);

	return 0;
}

static struct platform_driver pxa168fb_driver = {
	.driver		= {
		.name	= "pxa168-fb",
	},
	.probe		= pxa168fb_probe,
	.remove		= pxa168fb_remove,
};

module_platform_driver(pxa168fb_driver);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@marvell.com> "
	      "Green Wan <gwan@marvell.com>");
MODULE_DESCRIPTION("Framebuffer driver for PXA168/910");
MODULE_LICENSE("GPL");
