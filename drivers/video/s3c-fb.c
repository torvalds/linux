/* linux/drivers/video/s3c-fb.c
 *
 * Copyright 2008 Openmoko Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * Samsung SoC Framebuffer driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/clk.h>
#include <linux/fb.h>
#include <linux/io.h>

#include <mach/map.h>
#include <mach/regs-fb.h>
#include <plat/fb.h>

/* This driver will export a number of framebuffer interfaces depending
 * on the configuration passed in via the platform data. Each fb instance
 * maps to a hardware window. Currently there is no support for runtime
 * setting of the alpha-blending functions that each window has, so only
 * window 0 is actually useful.
 *
 * Window 0 is treated specially, it is used for the basis of the LCD
 * output timings and as the control for the output power-down state.
*/

/* note, some of the functions that get called are derived from including
 * <mach/regs-fb.h> as they are specific to the architecture that the code
 * is being built for.
*/

#ifdef CONFIG_FB_S3C_DEBUG_REGWRITE
#undef writel
#define writel(v, r) do { \
	printk(KERN_DEBUG "%s: %08x => %p\n", __func__, (unsigned int)v, r); \
	__raw_writel(v, r); } while(0)
#endif /* FB_S3C_DEBUG_REGWRITE */

struct s3c_fb;

/**
 * struct s3c_fb_win - per window private data for each framebuffer.
 * @windata: The platform data supplied for the window configuration.
 * @parent: The hardware that this window is part of.
 * @fbinfo: Pointer pack to the framebuffer info for this window.
 * @palette_buffer: Buffer/cache to hold palette entries.
 * @pseudo_palette: For use in TRUECOLOUR modes for entries 0..15/
 * @index: The window number of this window.
 * @palette: The bitfields for changing r/g/b into a hardware palette entry.
 */
struct s3c_fb_win {
	struct s3c_fb_pd_win	*windata;
	struct s3c_fb		*parent;
	struct fb_info		*fbinfo;
	struct s3c_fb_palette	 palette;

	u32			*palette_buffer;
	u32			 pseudo_palette[16];
	unsigned int		 index;
};

/**
 * struct s3c_fb - overall hardware state of the hardware
 * @dev: The device that we bound to, for printing, etc.
 * @regs_res: The resource we claimed for the IO registers.
 * @bus_clk: The clk (hclk) feeding our interface and possibly pixclk.
 * @regs: The mapped hardware registers.
 * @enabled: A bitmask of enabled hardware windows.
 * @pdata: The platform configuration data passed with the device.
 * @windows: The hardware windows that have been claimed.
 */
struct s3c_fb {
	struct device		*dev;
	struct resource		*regs_res;
	struct clk		*bus_clk;
	void __iomem		*regs;

	unsigned char		 enabled;

	struct s3c_fb_platdata	*pdata;
	struct s3c_fb_win	*windows[S3C_FB_MAX_WIN];
};

/**
 * s3c_fb_win_has_palette() - determine if a mode has a palette
 * @win: The window number being queried.
 * @bpp: The number of bits per pixel to test.
 *
 * Work out if the given window supports palletised data at the specified bpp.
 */
static int s3c_fb_win_has_palette(unsigned int win, unsigned int bpp)
{
	return s3c_fb_win_pal_size(win) <= (1 << bpp);
}

/**
 * s3c_fb_check_var() - framebuffer layer request to verify a given mode.
 * @var: The screen information to verify.
 * @info: The framebuffer device.
 *
 * Framebuffer layer call to verify the given information and allow us to
 * update various information depending on the hardware capabilities.
 */
static int s3c_fb_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb_pd_win *windata = win->windata;
	struct s3c_fb *sfb = win->parent;

	dev_dbg(sfb->dev, "checking parameters\n");

	var->xres_virtual = max((unsigned int)windata->virtual_x, var->xres);
	var->yres_virtual = max((unsigned int)windata->virtual_y, var->yres);

	if (!s3c_fb_validate_win_bpp(win->index, var->bits_per_pixel)) {
		dev_dbg(sfb->dev, "win %d: unsupported bpp %d\n",
			win->index, var->bits_per_pixel);
		return -EINVAL;
	}

	/* always ensure these are zero, for drop through cases below */
	var->transp.offset = 0;
	var->transp.length = 0;

	switch (var->bits_per_pixel) {
	case 1:
	case 2:
	case 4:
	case 8:
		if (!s3c_fb_win_has_palette(win->index, var->bits_per_pixel)) {
			/* non palletised, A:1,R:2,G:3,B:2 mode */
			var->red.offset		= 4;
			var->green.offset	= 2;
			var->blue.offset	= 0;
			var->red.length		= 5;
			var->green.length	= 3;
			var->blue.length	= 2;
			var->transp.offset	= 7;
			var->transp.length	= 1;
		} else {
			var->red.offset	= 0;
			var->red.length	= var->bits_per_pixel;
			var->green	= var->red;
			var->blue	= var->red;
		}
		break;

	case 19:
		/* 666 with one bit alpha/transparency */
		var->transp.offset	= 18;
		var->transp.length	= 1;
	case 18:
		var->bits_per_pixel	= 32;

		/* 666 format */
		var->red.offset		= 12;
		var->green.offset	= 6;
		var->blue.offset	= 0;
		var->red.length		= 6;
		var->green.length	= 6;
		var->blue.length	= 6;
		break;

	case 16:
		/* 16 bpp, 565 format */
		var->red.offset		= 11;
		var->green.offset	= 5;
		var->blue.offset	= 0;
		var->red.length		= 5;
		var->green.length	= 6;
		var->blue.length	= 5;
		break;

	case 28:
	case 25:
		var->transp.length	= var->bits_per_pixel - 24;
		var->transp.offset	= 24;
		/* drop through */
	case 24:
		/* our 24bpp is unpacked, so 32bpp */
		var->bits_per_pixel	= 32;
	case 32:
		var->red.offset		= 16;
		var->red.length		= 8;
		var->green.offset	= 8;
		var->green.length	= 8;
		var->blue.offset	= 0;
		var->blue.length	= 8;
		break;

	default:
		dev_err(sfb->dev, "invalid bpp\n");
	}

	dev_dbg(sfb->dev, "%s: verified parameters\n", __func__);
	return 0;
}

/**
 * s3c_fb_calc_pixclk() - calculate the divider to create the pixel clock.
 * @sfb: The hardware state.
 * @pixclock: The pixel clock wanted, in picoseconds.
 *
 * Given the specified pixel clock, work out the necessary divider to get
 * close to the output frequency.
 */
static int s3c_fb_calc_pixclk(struct s3c_fb *sfb, unsigned int pixclk)
{
	unsigned long clk = clk_get_rate(sfb->bus_clk);
	unsigned long long tmp;
	unsigned int result;

	tmp = (unsigned long long)clk;
	tmp *= pixclk;

	do_div(tmp, 1000000000UL);
	result = (unsigned int)tmp / 1000;

	dev_dbg(sfb->dev, "pixclk=%u, clk=%lu, div=%d (%lu)\n",
		pixclk, clk, result, clk / result);

	return result;
}

/**
 * s3c_fb_align_word() - align pixel count to word boundary
 * @bpp: The number of bits per pixel
 * @pix: The value to be aligned.
 *
 * Align the given pixel count so that it will start on an 32bit word
 * boundary.
 */
static int s3c_fb_align_word(unsigned int bpp, unsigned int pix)
{
	int pix_per_word;

	if (bpp > 16)
		return pix;

	pix_per_word = (8 * 32) / bpp;
	return ALIGN(pix, pix_per_word);
}

/**
 * s3c_fb_set_par() - framebuffer request to set new framebuffer state.
 * @info: The framebuffer to change.
 *
 * Framebuffer layer request to set a new mode for the specified framebuffer
 */
static int s3c_fb_set_par(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	void __iomem *regs = sfb->regs;
	int win_no = win->index;
	u32 data;
	u32 pagewidth;
	int clkdiv;

	dev_dbg(sfb->dev, "setting framebuffer parameters\n");

	switch (var->bits_per_pixel) {
	case 32:
	case 24:
	case 16:
	case 12:
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 8:
		if (s3c_fb_win_has_palette(win_no, 8))
			info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		else
			info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 1:
		info->fix.visual = FB_VISUAL_MONO01;
		break;
	default:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	}

	info->fix.line_length = (var->xres_virtual * var->bits_per_pixel) / 8;

	/* disable the window whilst we update it */
	writel(0, regs + WINCON(win_no));

	/* use window 0 as the basis for the lcd output timings */

	if (win_no == 0) {
		clkdiv = s3c_fb_calc_pixclk(sfb, var->pixclock);

		data = sfb->pdata->vidcon0;
		data &= ~(VIDCON0_CLKVAL_F_MASK | VIDCON0_CLKDIR);

		if (clkdiv > 1)
			data |= VIDCON0_CLKVAL_F(clkdiv-1) | VIDCON0_CLKDIR;
		else
			data &= ~VIDCON0_CLKDIR;	/* 1:1 clock */

		/* write the timing data to the panel */

		data |= VIDCON0_ENVID | VIDCON0_ENVID_F;
		writel(data, regs + VIDCON0);

		data = VIDTCON0_VBPD(var->upper_margin - 1) |
		       VIDTCON0_VFPD(var->lower_margin - 1) |
		       VIDTCON0_VSPW(var->vsync_len - 1);

		writel(data, regs + VIDTCON0);

		data = VIDTCON1_HBPD(var->left_margin - 1) |
		       VIDTCON1_HFPD(var->right_margin - 1) |
		       VIDTCON1_HSPW(var->hsync_len - 1);

		writel(data, regs + VIDTCON1);

		data = VIDTCON2_LINEVAL(var->yres - 1) |
		       VIDTCON2_HOZVAL(var->xres - 1);
		writel(data, regs + VIDTCON2);
	}

	/* write the buffer address */

	writel(info->fix.smem_start, regs + VIDW_BUF_START(win_no));

	data = info->fix.smem_start + info->fix.line_length * var->yres;
	writel(data, regs + VIDW_BUF_END(win_no));

	pagewidth = (var->xres * var->bits_per_pixel) >> 3;
	data = VIDW_BUF_SIZE_OFFSET(info->fix.line_length - pagewidth) |
	       VIDW_BUF_SIZE_PAGEWIDTH(pagewidth);
	writel(data, regs + VIDW_BUF_SIZE(win_no));

	/* write 'OSD' registers to control position of framebuffer */

	data = VIDOSDxA_TOPLEFT_X(0) | VIDOSDxA_TOPLEFT_Y(0);
	writel(data, regs + VIDOSD_A(win_no));

	data = VIDOSDxB_BOTRIGHT_X(s3c_fb_align_word(var->bits_per_pixel,
						     var->xres - 1)) |
	       VIDOSDxB_BOTRIGHT_Y(var->yres - 1);

	writel(data, regs + VIDOSD_B(win_no));

	data = var->xres * var->yres;
	if (s3c_fb_has_osd_d(win_no)) {
		writel(data, regs + VIDOSD_D(win_no));
		writel(0, regs + VIDOSD_C(win_no));
	} else
		writel(data, regs + VIDOSD_C(win_no));

	data = WINCONx_ENWIN;

	/* note, since we have to round up the bits-per-pixel, we end up
	 * relying on the bitfield information for r/g/b/a to work out
	 * exactly which mode of operation is intended. */

	switch (var->bits_per_pixel) {
	case 1:
		data |= WINCON0_BPPMODE_1BPP;
		data |= WINCONx_BITSWP;
		data |= WINCONx_BURSTLEN_4WORD;
		break;
	case 2:
		data |= WINCON0_BPPMODE_2BPP;
		data |= WINCONx_BITSWP;
		data |= WINCONx_BURSTLEN_8WORD;
		break;
	case 4:
		data |= WINCON0_BPPMODE_4BPP;
		data |= WINCONx_BITSWP;
		data |= WINCONx_BURSTLEN_8WORD;
		break;
	case 8:
		if (var->transp.length != 0)
			data |= WINCON1_BPPMODE_8BPP_1232;
		else
			data |= WINCON0_BPPMODE_8BPP_PALETTE;
		data |= WINCONx_BURSTLEN_8WORD;
		data |= WINCONx_BYTSWP;
		break;
	case 16:
		if (var->transp.length != 0)
			data |= WINCON1_BPPMODE_16BPP_A1555;
		else
			data |= WINCON0_BPPMODE_16BPP_565;
		data |= WINCONx_HAWSWP;
		data |= WINCONx_BURSTLEN_16WORD;
		break;
	case 24:
	case 32:
		if (var->red.length == 6) {
			if (var->transp.length != 0)
				data |= WINCON1_BPPMODE_19BPP_A1666;
			else
				data |= WINCON1_BPPMODE_18BPP_666;
		} else if (var->transp.length != 0)
			data |= WINCON1_BPPMODE_25BPP_A1888;
		else
			data |= WINCON0_BPPMODE_24BPP_888;

		data |= WINCONx_BURSTLEN_16WORD;
		break;
	}

	writel(data, regs + WINCON(win_no));
	writel(0x0, regs + WINxMAP(win_no));

	return 0;
}

/**
 * s3c_fb_update_palette() - set or schedule a palette update.
 * @sfb: The hardware information.
 * @win: The window being updated.
 * @reg: The palette index being changed.
 * @value: The computed palette value.
 *
 * Change the value of a palette register, either by directly writing to
 * the palette (this requires the palette RAM to be disconnected from the
 * hardware whilst this is in progress) or schedule the update for later.
 *
 * At the moment, since we have no VSYNC interrupt support, we simply set
 * the palette entry directly.
 */
static void s3c_fb_update_palette(struct s3c_fb *sfb,
				  struct s3c_fb_win *win,
				  unsigned int reg,
				  u32 value)
{
	void __iomem *palreg;
	u32 palcon;

	palreg = sfb->regs + s3c_fb_pal_reg(win->index, reg);

	dev_dbg(sfb->dev, "%s: win %d, reg %d (%p): %08x\n",
		__func__, win->index, reg, palreg, value);

	win->palette_buffer[reg] = value;

	palcon = readl(sfb->regs + WPALCON);
	writel(palcon | WPALCON_PAL_UPDATE, sfb->regs + WPALCON);

	if (s3c_fb_pal_is16(win->index))
		writew(value, palreg);
	else
		writel(value, palreg);

	writel(palcon, sfb->regs + WPALCON);
}

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

/**
 * s3c_fb_setcolreg() - framebuffer layer request to change palette.
 * @regno: The palette index to change.
 * @red: The red field for the palette data.
 * @green: The green field for the palette data.
 * @blue: The blue field for the palette data.
 * @trans: The transparency (alpha) field for the palette data.
 * @info: The framebuffer being changed.
 */
static int s3c_fb_setcolreg(unsigned regno,
			    unsigned red, unsigned green, unsigned blue,
			    unsigned transp, struct fb_info *info)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	unsigned int val;

	dev_dbg(sfb->dev, "%s: win %d: %d => rgb=%d/%d/%d\n",
		__func__, win->index, regno, red, green, blue);

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */

		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);

			pal[regno] = val;
		}
		break;

	case FB_VISUAL_PSEUDOCOLOR:
		if (regno < s3c_fb_win_pal_size(win->index)) {
			val  = chan_to_field(red, &win->palette.r);
			val |= chan_to_field(green, &win->palette.g);
			val |= chan_to_field(blue, &win->palette.b);

			s3c_fb_update_palette(sfb, win, regno, val);
		}

		break;

	default:
		return 1;	/* unknown type */
	}

	return 0;
}

/**
 * s3c_fb_enable() - Set the state of the main LCD output
 * @sfb: The main framebuffer state.
 * @enable: The state to set.
 */
static void s3c_fb_enable(struct s3c_fb *sfb, int enable)
{
	u32 vidcon0 = readl(sfb->regs + VIDCON0);

	if (enable)
		vidcon0 |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	else {
		/* see the note in the framebuffer datasheet about
		 * why you cannot take both of these bits down at the
		 * same time. */

		if (!(vidcon0 & VIDCON0_ENVID))
			return;

		vidcon0 |= VIDCON0_ENVID;
		vidcon0 &= ~VIDCON0_ENVID_F;
	}

	writel(vidcon0, sfb->regs + VIDCON0);
}

/**
 * s3c_fb_blank() - blank or unblank the given window
 * @blank_mode: The blank state from FB_BLANK_*
 * @info: The framebuffer to blank.
 *
 * Framebuffer layer request to change the power state.
 */
static int s3c_fb_blank(int blank_mode, struct fb_info *info)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	unsigned int index = win->index;
	u32 wincon;

	dev_dbg(sfb->dev, "blank mode %d\n", blank_mode);

	wincon = readl(sfb->regs + WINCON(index));

	switch (blank_mode) {
	case FB_BLANK_POWERDOWN:
		wincon &= ~WINCONx_ENWIN;
		sfb->enabled &= ~(1 << index);
		/* fall through to FB_BLANK_NORMAL */

	case FB_BLANK_NORMAL:
		/* disable the DMA and display 0x0 (black) */
		writel(WINxMAP_MAP | WINxMAP_MAP_COLOUR(0x0),
		       sfb->regs + WINxMAP(index));
		break;

	case FB_BLANK_UNBLANK:
		writel(0x0, sfb->regs + WINxMAP(index));
		wincon |= WINCONx_ENWIN;
		sfb->enabled |= (1 << index);
		break;

	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	default:
		return 1;
	}

	writel(wincon, sfb->regs + WINCON(index));

	/* Check the enabled state to see if we need to be running the
	 * main LCD interface, as if there are no active windows then
	 * it is highly likely that we also do not need to output
	 * anything.
	 */

	/* We could do something like the following code, but the current
	 * system of using framebuffer events means that we cannot make
	 * the distinction between just window 0 being inactive and all
	 * the windows being down.
	 *
	 * s3c_fb_enable(sfb, sfb->enabled ? 1 : 0);
	*/

	/* we're stuck with this until we can do something about overriding
	 * the power control using the blanking event for a single fb.
	 */
	if (index == 0)
		s3c_fb_enable(sfb, blank_mode != FB_BLANK_POWERDOWN ? 1 : 0);

	return 0;
}

static struct fb_ops s3c_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= s3c_fb_check_var,
	.fb_set_par	= s3c_fb_set_par,
	.fb_blank	= s3c_fb_blank,
	.fb_setcolreg	= s3c_fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

/**
 * s3c_fb_alloc_memory() - allocate display memory for framebuffer window
 * @sfb: The base resources for the hardware.
 * @win: The window to initialise memory for.
 *
 * Allocate memory for the given framebuffer.
 */
static int __devinit s3c_fb_alloc_memory(struct s3c_fb *sfb,
					 struct s3c_fb_win *win)
{
	struct s3c_fb_pd_win *windata = win->windata;
	unsigned int real_size, virt_size, size;
	struct fb_info *fbi = win->fbinfo;
	dma_addr_t map_dma;

	dev_dbg(sfb->dev, "allocating memory for display\n");

	real_size = windata->win_mode.xres * windata->win_mode.yres;
	virt_size = windata->virtual_x * windata->virtual_y;

	dev_dbg(sfb->dev, "real_size=%u (%u.%u), virt_size=%u (%u.%u)\n",
		real_size, windata->win_mode.xres, windata->win_mode.yres,
		virt_size, windata->virtual_x, windata->virtual_y);

	size = (real_size > virt_size) ? real_size : virt_size;
	size *= (windata->max_bpp > 16) ? 32 : windata->max_bpp;
	size /= 8;

	fbi->fix.smem_len = size;
	size = PAGE_ALIGN(size);

	dev_dbg(sfb->dev, "want %u bytes for window\n", size);

	fbi->screen_base = dma_alloc_writecombine(sfb->dev, size,
						  &map_dma, GFP_KERNEL);
	if (!fbi->screen_base)
		return -ENOMEM;

	dev_dbg(sfb->dev, "mapped %x to %p\n",
		(unsigned int)map_dma, fbi->screen_base);

	memset(fbi->screen_base, 0x0, size);
	fbi->fix.smem_start = map_dma;

	return 0;
}

/**
 * s3c_fb_free_memory() - free the display memory for the given window
 * @sfb: The base resources for the hardware.
 * @win: The window to free the display memory for.
 *
 * Free the display memory allocated by s3c_fb_alloc_memory().
 */
static void s3c_fb_free_memory(struct s3c_fb *sfb, struct s3c_fb_win *win)
{
	struct fb_info *fbi = win->fbinfo;

	dma_free_writecombine(sfb->dev, PAGE_ALIGN(fbi->fix.smem_len),
			      fbi->screen_base, fbi->fix.smem_start);
}

/**
 * s3c_fb_release_win() - release resources for a framebuffer window.
 * @win: The window to cleanup the resources for.
 *
 * Release the resources that where claimed for the hardware window,
 * such as the framebuffer instance and any memory claimed for it.
 */
static void s3c_fb_release_win(struct s3c_fb *sfb, struct s3c_fb_win *win)
{
	fb_dealloc_cmap(&win->fbinfo->cmap);
	unregister_framebuffer(win->fbinfo);
	s3c_fb_free_memory(sfb, win);
}

/**
 * s3c_fb_probe_win() - register an hardware window
 * @sfb: The base resources for the hardware
 * @res: Pointer to where to place the resultant window.
 *
 * Allocate and do the basic initialisation for one of the hardware's graphics
 * windows.
 */
static int __devinit s3c_fb_probe_win(struct s3c_fb *sfb, unsigned int win_no,
				      struct s3c_fb_win **res)
{
	struct fb_var_screeninfo *var;
	struct fb_videomode *initmode;
	struct s3c_fb_pd_win *windata;
	struct s3c_fb_win *win;
	struct fb_info *fbinfo;
	int palette_size;
	int ret;

	dev_dbg(sfb->dev, "probing window %d\n", win_no);

	palette_size = s3c_fb_win_pal_size(win_no);

	fbinfo = framebuffer_alloc(sizeof(struct s3c_fb_win) +
				   palette_size * sizeof(u32), sfb->dev);
	if (!fbinfo) {
		dev_err(sfb->dev, "failed to allocate framebuffer\n");
		return -ENOENT;
	}

	windata = sfb->pdata->win[win_no];
	initmode = &windata->win_mode;

	WARN_ON(windata->max_bpp == 0);
	WARN_ON(windata->win_mode.xres == 0);
	WARN_ON(windata->win_mode.yres == 0);

	win = fbinfo->par;
	var = &fbinfo->var;
	win->fbinfo = fbinfo;
	win->parent = sfb;
	win->windata = windata;
	win->index = win_no;
	win->palette_buffer = (u32 *)(win + 1);

	ret = s3c_fb_alloc_memory(sfb, win);
	if (ret) {
		dev_err(sfb->dev, "failed to allocate display memory\n");
		goto err_framebuffer;
	}

	/* setup the r/b/g positions for the window's palette */
	s3c_fb_init_palette(win_no, &win->palette);

	/* setup the initial video mode from the window */
	fb_videomode_to_var(&fbinfo->var, initmode);

	fbinfo->fix.type	= FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.accel	= FB_ACCEL_NONE;
	fbinfo->var.activate	= FB_ACTIVATE_NOW;
	fbinfo->var.vmode	= FB_VMODE_NONINTERLACED;
	fbinfo->var.bits_per_pixel = windata->default_bpp;
	fbinfo->fbops		= &s3c_fb_ops;
	fbinfo->flags		= FBINFO_FLAG_DEFAULT;
	fbinfo->pseudo_palette  = &win->pseudo_palette;

	/* prepare to actually start the framebuffer */

	ret = s3c_fb_check_var(&fbinfo->var, fbinfo);
	if (ret < 0) {
		dev_err(sfb->dev, "check_var failed on initial video params\n");
		goto err_alloc_mem;
	}

	/* create initial colour map */

	ret = fb_alloc_cmap(&fbinfo->cmap, s3c_fb_win_pal_size(win_no), 1);
	if (ret == 0)
		fb_set_cmap(&fbinfo->cmap, fbinfo);
	else
		dev_err(sfb->dev, "failed to allocate fb cmap\n");

	s3c_fb_set_par(fbinfo);

	dev_dbg(sfb->dev, "about to register framebuffer\n");

	/* run the check_var and set_par on our configuration. */

	ret = register_framebuffer(fbinfo);
	if (ret < 0) {
		dev_err(sfb->dev, "failed to register framebuffer\n");
		goto err_alloc_mem;
	}

	*res = win;
	dev_info(sfb->dev, "window %d: fb %s\n", win_no, fbinfo->fix.id);

	return 0;

err_alloc_mem:
	s3c_fb_free_memory(sfb, win);

err_framebuffer:
	unregister_framebuffer(fbinfo);
	return ret;
}

/**
 * s3c_fb_clear_win() - clear hardware window registers.
 * @sfb: The base resources for the hardware.
 * @win: The window to process.
 *
 * Reset the specific window registers to a known state.
 */
static void s3c_fb_clear_win(struct s3c_fb *sfb, int win)
{
	void __iomem *regs = sfb->regs;

	writel(0, regs + WINCON(win));
	writel(0xffffff, regs + WxKEYCONy(win, 0));
	writel(0xffffff, regs + WxKEYCONy(win, 1));

	writel(0, regs + VIDOSD_A(win));
	writel(0, regs + VIDOSD_B(win));
	writel(0, regs + VIDOSD_C(win));
}

static int __devinit s3c_fb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct s3c_fb_platdata *pd;
	struct s3c_fb *sfb;
	struct resource *res;
	int win;
	int ret = 0;

	pd = pdev->dev.platform_data;
	if (!pd) {
		dev_err(dev, "no platform data specified\n");
		return -EINVAL;
	}

	sfb = kzalloc(sizeof(struct s3c_fb), GFP_KERNEL);
	if (!sfb) {
		dev_err(dev, "no memory for framebuffers\n");
		return -ENOMEM;
	}

	sfb->dev = dev;
	sfb->pdata = pd;

	sfb->bus_clk = clk_get(dev, "lcd");
	if (IS_ERR(sfb->bus_clk)) {
		dev_err(dev, "failed to get bus clock\n");
		goto err_sfb;
	}

	clk_enable(sfb->bus_clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to find registers\n");
		ret = -ENOENT;
		goto err_clk;
	}

	sfb->regs_res = request_mem_region(res->start, resource_size(res),
					   dev_name(dev));
	if (!sfb->regs_res) {
		dev_err(dev, "failed to claim register region\n");
		ret = -ENOENT;
		goto err_clk;
	}

	sfb->regs = ioremap(res->start, resource_size(res));
	if (!sfb->regs) {
		dev_err(dev, "failed to map registers\n");
		ret = -ENXIO;
		goto err_req_region;
	}

	dev_dbg(dev, "got resources (regs %p), probing windows\n", sfb->regs);

	/* setup gpio and output polarity controls */

	pd->setup_gpio();

	writel(pd->vidcon1, sfb->regs + VIDCON1);

	/* zero all windows before we do anything */

	for (win = 0; win < S3C_FB_MAX_WIN; win++)
		s3c_fb_clear_win(sfb, win);

	/* we have the register setup, start allocating framebuffers */

	for (win = 0; win < S3C_FB_MAX_WIN; win++) {
		if (!pd->win[win])
			continue;

		ret = s3c_fb_probe_win(sfb, win, &sfb->windows[win]);
		if (ret < 0) {
			dev_err(dev, "failed to create window %d\n", win);
			for (; win >= 0; win--)
				s3c_fb_release_win(sfb, sfb->windows[win]);
			goto err_ioremap;
		}
	}

	platform_set_drvdata(pdev, sfb);

	return 0;

err_ioremap:
	iounmap(sfb->regs);

err_req_region:
	release_resource(sfb->regs_res);
	kfree(sfb->regs_res);

err_clk:
	clk_disable(sfb->bus_clk);
	clk_put(sfb->bus_clk);

err_sfb:
	kfree(sfb);
	return ret;
}

/**
 * s3c_fb_remove() - Cleanup on module finalisation
 * @pdev: The platform device we are bound to.
 *
 * Shutdown and then release all the resources that the driver allocated
 * on initialisation.
 */
static int __devexit s3c_fb_remove(struct platform_device *pdev)
{
	struct s3c_fb *sfb = platform_get_drvdata(pdev);
	int win;

	for (win = 0; win <= S3C_FB_MAX_WIN; win++)
		s3c_fb_release_win(sfb, sfb->windows[win]);

	iounmap(sfb->regs);

	clk_disable(sfb->bus_clk);
	clk_put(sfb->bus_clk);

	release_resource(sfb->regs_res);
	kfree(sfb->regs_res);

	kfree(sfb);

	return 0;
}

#ifdef CONFIG_PM
static int s3c_fb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct s3c_fb *sfb = platform_get_drvdata(pdev);
	struct s3c_fb_win *win;
	int win_no;

	for (win_no = S3C_FB_MAX_WIN; win_no >= 0; win_no--) {
		win = sfb->windows[win_no];
		if (!win)
			continue;

		/* use the blank function to push into power-down */
		s3c_fb_blank(FB_BLANK_POWERDOWN, win->fbinfo);
	}

	clk_disable(sfb->bus_clk);
	return 0;
}

static int s3c_fb_resume(struct platform_device *pdev)
{
	struct s3c_fb *sfb = platform_get_drvdata(pdev);
	struct s3c_fb_win *win;
	int win_no;

	clk_enable(sfb->bus_clk);

	for (win_no = 0; win_no < S3C_FB_MAX_WIN; win_no++) {
		win = sfb->windows[win_no];
		if (!win)
			continue;

		dev_dbg(&pdev->dev, "resuming window %d\n", win_no);
		s3c_fb_set_par(win->fbinfo);
	}

	return 0;
}
#else
#define s3c_fb_suspend NULL
#define s3c_fb_resume  NULL
#endif

static struct platform_driver s3c_fb_driver = {
	.probe		= s3c_fb_probe,
	.remove		= s3c_fb_remove,
	.suspend	= s3c_fb_suspend,
	.resume		= s3c_fb_resume,
	.driver		= {
		.name	= "s3c-fb",
		.owner	= THIS_MODULE,
	},
};

static int __init s3c_fb_init(void)
{
	return platform_driver_register(&s3c_fb_driver);
}

static void __exit s3c_fb_cleanup(void)
{
	platform_driver_unregister(&s3c_fb_driver);
}

module_init(s3c_fb_init);
module_exit(s3c_fb_cleanup);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("Samsung S3C SoC Framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c-fb");
