/*
 *  linux/drivers/video/amba-clcd.c
 *
 * Copyright (C) 2001 ARM Limited, by David A Rusling
 * Updated to 2.5, Deep Blue Solutions Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 *  ARM PrimeCell PL110 Color LCD Controller
 */
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/backlight.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include "amba-clcd-nomadik.h"
#include "amba-clcd-versatile.h"

#define to_clcd(info)	container_of(info, struct clcd_fb, fb)

/* This is limited to 16 characters when displayed by X startup */
static const char *clcd_name = "CLCD FB";

/*
 * Unfortunately, the enable/disable functions may be called either from
 * process or IRQ context, and we _need_ to delay.  This is _not_ good.
 */
static inline void clcdfb_sleep(unsigned int ms)
{
	if (in_atomic()) {
		mdelay(ms);
	} else {
		msleep(ms);
	}
}

static inline void clcdfb_set_start(struct clcd_fb *fb)
{
	unsigned long ustart = fb->fb.fix.smem_start;
	unsigned long lstart;

	ustart += fb->fb.var.yoffset * fb->fb.fix.line_length;
	lstart = ustart + fb->fb.var.yres * fb->fb.fix.line_length / 2;

	writel(ustart, fb->regs + CLCD_UBAS);
	writel(lstart, fb->regs + CLCD_LBAS);
}

static void clcdfb_disable(struct clcd_fb *fb)
{
	u32 val;

	if (fb->board->disable)
		fb->board->disable(fb);

	if (fb->panel->backlight) {
		fb->panel->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(fb->panel->backlight);
	}

	val = readl(fb->regs + fb->off_cntl);
	if (val & CNTL_LCDPWR) {
		val &= ~CNTL_LCDPWR;
		writel(val, fb->regs + fb->off_cntl);

		clcdfb_sleep(20);
	}
	if (val & CNTL_LCDEN) {
		val &= ~CNTL_LCDEN;
		writel(val, fb->regs + fb->off_cntl);
	}

	/*
	 * Disable CLCD clock source.
	 */
	if (fb->clk_enabled) {
		fb->clk_enabled = false;
		clk_disable(fb->clk);
	}
}

static void clcdfb_enable(struct clcd_fb *fb, u32 cntl)
{
	/*
	 * Enable the CLCD clock source.
	 */
	if (!fb->clk_enabled) {
		fb->clk_enabled = true;
		clk_enable(fb->clk);
	}

	/*
	 * Bring up by first enabling..
	 */
	cntl |= CNTL_LCDEN;
	writel(cntl, fb->regs + fb->off_cntl);

	clcdfb_sleep(20);

	/*
	 * and now apply power.
	 */
	cntl |= CNTL_LCDPWR;
	writel(cntl, fb->regs + fb->off_cntl);

	/*
	 * Turn on backlight
	 */
	if (fb->panel->backlight) {
		fb->panel->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(fb->panel->backlight);
	}

	/*
	 * finally, enable the interface.
	 */
	if (fb->board->enable)
		fb->board->enable(fb);
}

static int
clcdfb_set_bitfields(struct clcd_fb *fb, struct fb_var_screeninfo *var)
{
	u32 caps;
	int ret = 0;

	if (fb->panel->caps && fb->board->caps)
		caps = fb->panel->caps & fb->board->caps;
	else {
		/* Old way of specifying what can be used */
		caps = fb->panel->cntl & CNTL_BGR ?
			CLCD_CAP_BGR : CLCD_CAP_RGB;
		/* But mask out 444 modes as they weren't supported */
		caps &= ~CLCD_CAP_444;
	}

	/* Only TFT panels can do RGB888/BGR888 */
	if (!(fb->panel->cntl & CNTL_LCDTFT))
		caps &= ~CLCD_CAP_888;

	memset(&var->transp, 0, sizeof(var->transp));

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;

	switch (var->bits_per_pixel) {
	case 1:
	case 2:
	case 4:
	case 8:
		/* If we can't do 5551, reject */
		caps &= CLCD_CAP_5551;
		if (!caps) {
			ret = -EINVAL;
			break;
		}

		var->red.length		= var->bits_per_pixel;
		var->red.offset		= 0;
		var->green.length	= var->bits_per_pixel;
		var->green.offset	= 0;
		var->blue.length	= var->bits_per_pixel;
		var->blue.offset	= 0;
		break;

	case 16:
		/* If we can't do 444, 5551 or 565, reject */
		if (!(caps & (CLCD_CAP_444 | CLCD_CAP_5551 | CLCD_CAP_565))) {
			ret = -EINVAL;
			break;
		}

		/*
		 * Green length can be 4, 5 or 6 depending whether
		 * we're operating in 444, 5551 or 565 mode.
		 */
		if (var->green.length == 4 && caps & CLCD_CAP_444)
			caps &= CLCD_CAP_444;
		if (var->green.length == 5 && caps & CLCD_CAP_5551)
			caps &= CLCD_CAP_5551;
		else if (var->green.length == 6 && caps & CLCD_CAP_565)
			caps &= CLCD_CAP_565;
		else {
			/*
			 * PL110 officially only supports RGB555,
			 * but may be wired up to allow RGB565.
			 */
			if (caps & CLCD_CAP_565) {
				var->green.length = 6;
				caps &= CLCD_CAP_565;
			} else if (caps & CLCD_CAP_5551) {
				var->green.length = 5;
				caps &= CLCD_CAP_5551;
			} else {
				var->green.length = 4;
				caps &= CLCD_CAP_444;
			}
		}

		if (var->green.length >= 5) {
			var->red.length = 5;
			var->blue.length = 5;
		} else {
			var->red.length = 4;
			var->blue.length = 4;
		}
		break;
	case 24:
		if (fb->vendor->packed_24_bit_pixels) {
			var->red.length = 8;
			var->green.length = 8;
			var->blue.length = 8;
		} else {
			ret = -EINVAL;
		}
		break;
	case 32:
		/* If we can't do 888, reject */
		caps &= CLCD_CAP_888;
		if (!caps) {
			ret = -EINVAL;
			break;
		}

		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	/*
	 * >= 16bpp displays have separate colour component bitfields
	 * encoded in the pixel data.  Calculate their position from
	 * the bitfield length defined above.
	 */
	if (ret == 0 && var->bits_per_pixel >= 16) {
		bool bgr, rgb;

		bgr = caps & CLCD_CAP_BGR && var->blue.offset == 0;
		rgb = caps & CLCD_CAP_RGB && var->red.offset == 0;

		if (!bgr && !rgb)
			/*
			 * The requested format was not possible, try just
			 * our capabilities.  One of BGR or RGB must be
			 * supported.
			 */
			bgr = caps & CLCD_CAP_BGR;

		if (bgr) {
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		} else {
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset = var->green.offset + var->green.length;
		}
	}

	return ret;
}

static int clcdfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct clcd_fb *fb = to_clcd(info);
	int ret = -EINVAL;

	if (fb->board->check)
		ret = fb->board->check(fb, var);

	if (ret == 0 &&
	    var->xres_virtual * var->bits_per_pixel / 8 *
	    var->yres_virtual > fb->fb.fix.smem_len)
		ret = -EINVAL;

	if (ret == 0)
		ret = clcdfb_set_bitfields(fb, var);

	return ret;
}

static int clcdfb_set_par(struct fb_info *info)
{
	struct clcd_fb *fb = to_clcd(info);
	struct clcd_regs regs;

	fb->fb.fix.line_length = fb->fb.var.xres_virtual *
				 fb->fb.var.bits_per_pixel / 8;

	if (fb->fb.var.bits_per_pixel <= 8)
		fb->fb.fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fb->fb.fix.visual = FB_VISUAL_TRUECOLOR;

	fb->board->decode(fb, &regs);

	clcdfb_disable(fb);

	/* Some variants must be clocked here */
	if (fb->vendor->clock_timregs && !fb->clk_enabled) {
		fb->clk_enabled = true;
		clk_enable(fb->clk);
	}

	writel(regs.tim0, fb->regs + CLCD_TIM0);
	writel(regs.tim1, fb->regs + CLCD_TIM1);
	writel(regs.tim2, fb->regs + CLCD_TIM2);
	writel(regs.tim3, fb->regs + CLCD_TIM3);

	clcdfb_set_start(fb);

	clk_set_rate(fb->clk, (1000000000 / regs.pixclock) * 1000);

	fb->clcd_cntl = regs.cntl;

	clcdfb_enable(fb, regs.cntl);

#ifdef DEBUG
	printk(KERN_INFO
	       "CLCD: Registers set to\n"
	       "  %08x %08x %08x %08x\n"
	       "  %08x %08x %08x %08x\n",
		readl(fb->regs + CLCD_TIM0), readl(fb->regs + CLCD_TIM1),
		readl(fb->regs + CLCD_TIM2), readl(fb->regs + CLCD_TIM3),
		readl(fb->regs + CLCD_UBAS), readl(fb->regs + CLCD_LBAS),
		readl(fb->regs + fb->off_ienb), readl(fb->regs + fb->off_cntl));
#endif

	return 0;
}

static inline u32 convert_bitfield(int val, struct fb_bitfield *bf)
{
	unsigned int mask = (1 << bf->length) - 1;

	return (val >> (16 - bf->length) & mask) << bf->offset;
}

/*
 *  Set a single color register. The values supplied have a 16 bit
 *  magnitude.  Return != 0 for invalid regno.
 */
static int
clcdfb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
		 unsigned int blue, unsigned int transp, struct fb_info *info)
{
	struct clcd_fb *fb = to_clcd(info);

	if (regno < 16)
		fb->cmap[regno] = convert_bitfield(transp, &fb->fb.var.transp) |
				  convert_bitfield(blue, &fb->fb.var.blue) |
				  convert_bitfield(green, &fb->fb.var.green) |
				  convert_bitfield(red, &fb->fb.var.red);

	if (fb->fb.fix.visual == FB_VISUAL_PSEUDOCOLOR && regno < 256) {
		int hw_reg = CLCD_PALETTE + ((regno * 2) & ~3);
		u32 val, mask, newval;

		newval  = (red >> 11)  & 0x001f;
		newval |= (green >> 6) & 0x03e0;
		newval |= (blue >> 1)  & 0x7c00;

		/*
		 * 3.2.11: if we're configured for big endian
		 * byte order, the palette entries are swapped.
		 */
		if (fb->clcd_cntl & CNTL_BEBO)
			regno ^= 1;

		if (regno & 1) {
			newval <<= 16;
			mask = 0x0000ffff;
		} else {
			mask = 0xffff0000;
		}

		val = readl(fb->regs + hw_reg) & mask;
		writel(val | newval, fb->regs + hw_reg);
	}

	return regno > 255;
}

/*
 *  Blank the screen if blank_mode != 0, else unblank. If blank == NULL
 *  then the caller blanks by setting the CLUT (Color Look Up Table) to all
 *  black. Return 0 if blanking succeeded, != 0 if un-/blanking failed due
 *  to e.g. a video mode which doesn't support it. Implements VESA suspend
 *  and powerdown modes on hardware that supports disabling hsync/vsync:
 *    blank_mode == 2: suspend vsync
 *    blank_mode == 3: suspend hsync
 *    blank_mode == 4: powerdown
 */
static int clcdfb_blank(int blank_mode, struct fb_info *info)
{
	struct clcd_fb *fb = to_clcd(info);

	if (blank_mode != 0) {
		clcdfb_disable(fb);
	} else {
		clcdfb_enable(fb, fb->clcd_cntl);
	}
	return 0;
}

static int clcdfb_mmap(struct fb_info *info,
		       struct vm_area_struct *vma)
{
	struct clcd_fb *fb = to_clcd(info);
	unsigned long len, off = vma->vm_pgoff << PAGE_SHIFT;
	int ret = -EINVAL;

	len = info->fix.smem_len;

	if (off <= len && vma->vm_end - vma->vm_start <= len - off &&
	    fb->board->mmap)
		ret = fb->board->mmap(fb, vma);

	return ret;
}

static struct fb_ops clcdfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= clcdfb_check_var,
	.fb_set_par	= clcdfb_set_par,
	.fb_setcolreg	= clcdfb_setcolreg,
	.fb_blank	= clcdfb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_mmap	= clcdfb_mmap,
};

static int clcdfb_register(struct clcd_fb *fb)
{
	int ret;

	/*
	 * ARM PL111 always has IENB at 0x1c; it's only PL110
	 * which is reversed on some platforms.
	 */
	if (amba_manf(fb->dev) == 0x41 && amba_part(fb->dev) == 0x111) {
		fb->off_ienb = CLCD_PL111_IENB;
		fb->off_cntl = CLCD_PL111_CNTL;
	} else {
		if (of_machine_is_compatible("arm,versatile-ab") ||
		    of_machine_is_compatible("arm,versatile-pb")) {
			fb->off_ienb = CLCD_PL111_IENB;
			fb->off_cntl = CLCD_PL111_CNTL;
		} else {
			fb->off_ienb = CLCD_PL110_IENB;
			fb->off_cntl = CLCD_PL110_CNTL;
		}
	}

	fb->clk = clk_get(&fb->dev->dev, NULL);
	if (IS_ERR(fb->clk)) {
		ret = PTR_ERR(fb->clk);
		goto out;
	}

	ret = clk_prepare(fb->clk);
	if (ret)
		goto free_clk;

	fb->fb.device		= &fb->dev->dev;

	fb->fb.fix.mmio_start	= fb->dev->res.start;
	fb->fb.fix.mmio_len	= resource_size(&fb->dev->res);

	fb->regs = ioremap(fb->fb.fix.mmio_start, fb->fb.fix.mmio_len);
	if (!fb->regs) {
		printk(KERN_ERR "CLCD: unable to remap registers\n");
		ret = -ENOMEM;
		goto clk_unprep;
	}

	fb->fb.fbops		= &clcdfb_ops;
	fb->fb.flags		= FBINFO_FLAG_DEFAULT;
	fb->fb.pseudo_palette	= fb->cmap;

	strncpy(fb->fb.fix.id, clcd_name, sizeof(fb->fb.fix.id));
	fb->fb.fix.type		= FB_TYPE_PACKED_PIXELS;
	fb->fb.fix.type_aux	= 0;
	fb->fb.fix.xpanstep	= 0;
	fb->fb.fix.ypanstep	= 0;
	fb->fb.fix.ywrapstep	= 0;
	fb->fb.fix.accel	= FB_ACCEL_NONE;

	fb->fb.var.xres		= fb->panel->mode.xres;
	fb->fb.var.yres		= fb->panel->mode.yres;
	fb->fb.var.xres_virtual	= fb->panel->mode.xres;
	fb->fb.var.yres_virtual	= fb->panel->mode.yres;
	fb->fb.var.bits_per_pixel = fb->panel->bpp;
	fb->fb.var.grayscale	= fb->panel->grayscale;
	fb->fb.var.pixclock	= fb->panel->mode.pixclock;
	fb->fb.var.left_margin	= fb->panel->mode.left_margin;
	fb->fb.var.right_margin	= fb->panel->mode.right_margin;
	fb->fb.var.upper_margin	= fb->panel->mode.upper_margin;
	fb->fb.var.lower_margin	= fb->panel->mode.lower_margin;
	fb->fb.var.hsync_len	= fb->panel->mode.hsync_len;
	fb->fb.var.vsync_len	= fb->panel->mode.vsync_len;
	fb->fb.var.sync		= fb->panel->mode.sync;
	fb->fb.var.vmode	= fb->panel->mode.vmode;
	fb->fb.var.activate	= FB_ACTIVATE_NOW;
	fb->fb.var.nonstd	= 0;
	fb->fb.var.height	= fb->panel->height;
	fb->fb.var.width	= fb->panel->width;
	fb->fb.var.accel_flags	= 0;

	fb->fb.monspecs.hfmin	= 0;
	fb->fb.monspecs.hfmax   = 100000;
	fb->fb.monspecs.vfmin	= 0;
	fb->fb.monspecs.vfmax	= 400;
	fb->fb.monspecs.dclkmin = 1000000;
	fb->fb.monspecs.dclkmax	= 100000000;

	/*
	 * Make sure that the bitfields are set appropriately.
	 */
	clcdfb_set_bitfields(fb, &fb->fb.var);

	/*
	 * Allocate colourmap.
	 */
	ret = fb_alloc_cmap(&fb->fb.cmap, 256, 0);
	if (ret)
		goto unmap;

	/*
	 * Ensure interrupts are disabled.
	 */
	writel(0, fb->regs + fb->off_ienb);

	fb_set_var(&fb->fb, &fb->fb.var);

	dev_info(&fb->dev->dev, "%s hardware, %s display\n",
	         fb->board->name, fb->panel->mode.name);

	ret = register_framebuffer(&fb->fb);
	if (ret == 0)
		goto out;

	printk(KERN_ERR "CLCD: cannot register framebuffer (%d)\n", ret);

	fb_dealloc_cmap(&fb->fb.cmap);
 unmap:
	iounmap(fb->regs);
 clk_unprep:
	clk_unprepare(fb->clk);
 free_clk:
	clk_put(fb->clk);
 out:
	return ret;
}

#ifdef CONFIG_OF
static int clcdfb_of_get_dpi_panel_mode(struct device_node *node,
		struct clcd_panel *clcd_panel)
{
	int err;
	struct display_timing timing;
	struct videomode video;

	err = of_get_display_timing(node, "panel-timing", &timing);
	if (err)
		return err;

	videomode_from_timing(&timing, &video);

	err = fb_videomode_from_videomode(&video, &clcd_panel->mode);
	if (err)
		return err;

	/* Set up some inversion flags */
	if (timing.flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		clcd_panel->tim2 |= TIM2_IPC;
	else if (!(timing.flags & DISPLAY_FLAGS_PIXDATA_POSEDGE))
		/*
		 * To preserve backwards compatibility, the IPC (inverted
		 * pixel clock) flag needs to be set on any display that
		 * doesn't explicitly specify that the pixel clock is
		 * active on the negative or positive edge.
		 */
		clcd_panel->tim2 |= TIM2_IPC;

	if (timing.flags & DISPLAY_FLAGS_HSYNC_LOW)
		clcd_panel->tim2 |= TIM2_IHS;

	if (timing.flags & DISPLAY_FLAGS_VSYNC_LOW)
		clcd_panel->tim2 |= TIM2_IVS;

	if (timing.flags & DISPLAY_FLAGS_DE_LOW)
		clcd_panel->tim2 |= TIM2_IOE;

	return 0;
}

static int clcdfb_snprintf_mode(char *buf, int size, struct fb_videomode *mode)
{
	return snprintf(buf, size, "%ux%u@%u", mode->xres, mode->yres,
			mode->refresh);
}

static int clcdfb_of_get_backlight(struct device_node *panel,
				   struct clcd_panel *clcd_panel)
{
	struct device_node *backlight;

	/* Look up the optional backlight phandle */
	backlight = of_parse_phandle(panel, "backlight", 0);
	if (backlight) {
		clcd_panel->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!clcd_panel->backlight)
			return -EPROBE_DEFER;
	}
	return 0;
}

static int clcdfb_of_get_mode(struct device *dev, struct device_node *panel,
			      struct clcd_panel *clcd_panel)
{
	int err;
	struct fb_videomode *mode;
	char *name;
	int len;

	/* Only directly connected DPI panels supported for now */
	if (of_device_is_compatible(panel, "panel-dpi"))
		err = clcdfb_of_get_dpi_panel_mode(panel, clcd_panel);
	else
		err = -ENOENT;
	if (err)
		return err;
	mode = &clcd_panel->mode;

	len = clcdfb_snprintf_mode(NULL, 0, mode);
	name = devm_kzalloc(dev, len + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	clcdfb_snprintf_mode(name, len + 1, mode);
	mode->name = name;

	return 0;
}

static int clcdfb_of_init_tft_panel(struct clcd_fb *fb, u32 r0, u32 g0, u32 b0)
{
	static struct {
		unsigned int part;
		u32 r0, g0, b0;
		u32 caps;
	} panels[] = {
		{ 0x110, 1,  7, 13, CLCD_CAP_5551 },
		{ 0x110, 0,  8, 16, CLCD_CAP_888 },
		{ 0x110, 16, 8, 0,  CLCD_CAP_888 },
		{ 0x111, 4, 14, 20, CLCD_CAP_444 },
		{ 0x111, 3, 11, 19, CLCD_CAP_444 | CLCD_CAP_5551 },
		{ 0x111, 3, 10, 19, CLCD_CAP_444 | CLCD_CAP_5551 |
				    CLCD_CAP_565 },
		{ 0x111, 0,  8, 16, CLCD_CAP_444 | CLCD_CAP_5551 |
				    CLCD_CAP_565 | CLCD_CAP_888 },
	};
	int i;

	/* Bypass pixel clock divider */
	fb->panel->tim2 |= TIM2_BCD;

	/* TFT display, vert. comp. interrupt at the start of the back porch */
	fb->panel->cntl |= CNTL_LCDTFT | CNTL_LCDVCOMP(1);

	fb->panel->caps = 0;

	/* Match the setup with known variants */
	for (i = 0; i < ARRAY_SIZE(panels) && !fb->panel->caps; i++) {
		if (amba_part(fb->dev) != panels[i].part)
			continue;
		if (g0 != panels[i].g0)
			continue;
		if (r0 == panels[i].r0 && b0 == panels[i].b0)
			fb->panel->caps = panels[i].caps;
	}

	/*
	 * If we actually physically connected the R lines to B and
	 * vice versa
	 */
	if (r0 != 0 && b0 == 0)
		fb->panel->bgr_connection = true;

	if (fb->panel->caps && fb->vendor->st_bitmux_control) {
		/*
		 * Set up the special bits for the Nomadik control register
		 * (other platforms tend to do this through an external
		 * register).
		 */

		/* Offset of the highest used color */
		int maxoff = max3(r0, g0, b0);
		/* Most significant bit out, highest used bit */
		int msb = 0;

		if (fb->panel->caps & CLCD_CAP_888) {
			msb = maxoff + 8 - 1;
		} else if (fb->panel->caps & CLCD_CAP_565) {
			msb = maxoff + 5 - 1;
			fb->panel->cntl |= CNTL_ST_1XBPP_565;
		} else if (fb->panel->caps & CLCD_CAP_5551) {
			msb = maxoff + 5 - 1;
			fb->panel->cntl |= CNTL_ST_1XBPP_5551;
		} else if (fb->panel->caps & CLCD_CAP_444) {
			msb = maxoff + 4 - 1;
			fb->panel->cntl |= CNTL_ST_1XBPP_444;
		}

		/* Send out as many bits as we need */
		if (msb > 17)
			fb->panel->cntl |= CNTL_ST_CDWID_24;
		else if (msb > 15)
			fb->panel->cntl |= CNTL_ST_CDWID_18;
		else if (msb > 11)
			fb->panel->cntl |= CNTL_ST_CDWID_16;
		else
			fb->panel->cntl |= CNTL_ST_CDWID_12;
	}

	return fb->panel->caps ? 0 : -EINVAL;
}

static int clcdfb_of_init_display(struct clcd_fb *fb)
{
	struct device_node *endpoint, *panel;
	int err;
	unsigned int bpp;
	u32 max_bandwidth;
	u32 tft_r0b0g0[3];

	fb->panel = devm_kzalloc(&fb->dev->dev, sizeof(*fb->panel), GFP_KERNEL);
	if (!fb->panel)
		return -ENOMEM;

	/*
	 * Fetch the panel endpoint.
	 */
	endpoint = of_graph_get_next_endpoint(fb->dev->dev.of_node, NULL);
	if (!endpoint)
		return -ENODEV;

	panel = of_graph_get_remote_port_parent(endpoint);
	if (!panel)
		return -ENODEV;

	if (fb->vendor->init_panel) {
		err = fb->vendor->init_panel(fb, panel);
		if (err)
			return err;
	}

	err = clcdfb_of_get_backlight(panel, fb->panel);
	if (err)
		return err;

	err = clcdfb_of_get_mode(&fb->dev->dev, panel, fb->panel);
	if (err)
		return err;

	err = of_property_read_u32(fb->dev->dev.of_node, "max-memory-bandwidth",
			&max_bandwidth);
	if (!err) {
		/*
		 * max_bandwidth is in bytes per second and pixclock in
		 * pico-seconds, so the maximum allowed bits per pixel is
		 *   8 * max_bandwidth / (PICOS2KHZ(pixclock) * 1000)
		 * Rearrange this calculation to avoid overflow and then ensure
		 * result is a valid format.
		 */
		bpp = max_bandwidth / (1000 / 8)
			/ PICOS2KHZ(fb->panel->mode.pixclock);
		bpp = rounddown_pow_of_two(bpp);
		if (bpp > 32)
			bpp = 32;
	} else
		bpp = 32;
	fb->panel->bpp = bpp;

#ifdef CONFIG_CPU_BIG_ENDIAN
	fb->panel->cntl |= CNTL_BEBO;
#endif
	fb->panel->width = -1;
	fb->panel->height = -1;

	if (of_property_read_u32_array(endpoint,
			"arm,pl11x,tft-r0g0b0-pads",
			tft_r0b0g0, ARRAY_SIZE(tft_r0b0g0)) != 0)
		return -ENOENT;

	return clcdfb_of_init_tft_panel(fb, tft_r0b0g0[0],
					tft_r0b0g0[1],  tft_r0b0g0[2]);
}

static int clcdfb_of_vram_setup(struct clcd_fb *fb)
{
	int err;
	struct device_node *memory;
	u64 size;

	err = clcdfb_of_init_display(fb);
	if (err)
		return err;

	memory = of_parse_phandle(fb->dev->dev.of_node, "memory-region", 0);
	if (!memory)
		return -ENODEV;

	fb->fb.screen_base = of_iomap(memory, 0);
	if (!fb->fb.screen_base)
		return -ENOMEM;

	fb->fb.fix.smem_start = of_translate_address(memory,
			of_get_address(memory, 0, &size, NULL));
	fb->fb.fix.smem_len = size;

	return 0;
}

static int clcdfb_of_vram_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	unsigned long off, user_size, kernel_size;


	off = vma->vm_pgoff << PAGE_SHIFT;
	user_size = vma->vm_end - vma->vm_start;
	kernel_size = fb->fb.fix.smem_len;

	if (off >= kernel_size || user_size > (kernel_size - off))
		return -ENXIO;

	return remap_pfn_range(vma, vma->vm_start,
			__phys_to_pfn(fb->fb.fix.smem_start) + vma->vm_pgoff,
			user_size,
			pgprot_writecombine(vma->vm_page_prot));
}

static void clcdfb_of_vram_remove(struct clcd_fb *fb)
{
	iounmap(fb->fb.screen_base);
}

static int clcdfb_of_dma_setup(struct clcd_fb *fb)
{
	unsigned long framesize;
	dma_addr_t dma;
	int err;

	err = clcdfb_of_init_display(fb);
	if (err)
		return err;

	framesize = PAGE_ALIGN(fb->panel->mode.xres * fb->panel->mode.yres *
			fb->panel->bpp / 8);
	fb->fb.screen_base = dma_alloc_coherent(&fb->dev->dev, framesize,
			&dma, GFP_KERNEL);
	if (!fb->fb.screen_base)
		return -ENOMEM;

	fb->fb.fix.smem_start = dma;
	fb->fb.fix.smem_len = framesize;

	return 0;
}

static int clcdfb_of_dma_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_wc(&fb->dev->dev, vma, fb->fb.screen_base,
			   fb->fb.fix.smem_start, fb->fb.fix.smem_len);
}

static void clcdfb_of_dma_remove(struct clcd_fb *fb)
{
	dma_free_coherent(&fb->dev->dev, fb->fb.fix.smem_len,
			fb->fb.screen_base, fb->fb.fix.smem_start);
}

static struct clcd_board *clcdfb_of_get_board(struct amba_device *dev)
{
	struct clcd_board *board = devm_kzalloc(&dev->dev, sizeof(*board),
			GFP_KERNEL);
	struct device_node *node = dev->dev.of_node;

	if (!board)
		return NULL;

	board->name = of_node_full_name(node);
	board->caps = CLCD_CAP_ALL;
	board->check = clcdfb_check;
	board->decode = clcdfb_decode;
	if (of_find_property(node, "memory-region", NULL)) {
		board->setup = clcdfb_of_vram_setup;
		board->mmap = clcdfb_of_vram_mmap;
		board->remove = clcdfb_of_vram_remove;
	} else {
		board->setup = clcdfb_of_dma_setup;
		board->mmap = clcdfb_of_dma_mmap;
		board->remove = clcdfb_of_dma_remove;
	}

	return board;
}
#else
static struct clcd_board *clcdfb_of_get_board(struct amba_device *dev)
{
	return NULL;
}
#endif

static int clcdfb_probe(struct amba_device *dev, const struct amba_id *id)
{
	struct clcd_board *board = dev_get_platdata(&dev->dev);
	struct clcd_vendor_data *vendor = id->data;
	struct clcd_fb *fb;
	int ret;

	if (!board)
		board = clcdfb_of_get_board(dev);

	if (!board)
		return -EINVAL;

	if (vendor->init_board) {
		ret = vendor->init_board(dev, board);
		if (ret)
			return ret;
	}

	ret = dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto out;

	ret = amba_request_regions(dev, NULL);
	if (ret) {
		printk(KERN_ERR "CLCD: unable to reserve regs region\n");
		goto out;
	}

	fb = kzalloc(sizeof(struct clcd_fb), GFP_KERNEL);
	if (!fb) {
		ret = -ENOMEM;
		goto free_region;
	}

	fb->dev = dev;
	fb->vendor = vendor;
	fb->board = board;

	dev_info(&fb->dev->dev, "PL%03x designer %02x rev%u at 0x%08llx\n",
		amba_part(dev), amba_manf(dev), amba_rev(dev),
		(unsigned long long)dev->res.start);

	ret = fb->board->setup(fb);
	if (ret)
		goto free_fb;

	ret = clcdfb_register(fb);
	if (ret == 0) {
		amba_set_drvdata(dev, fb);
		goto out;
	}

	fb->board->remove(fb);
 free_fb:
	kfree(fb);
 free_region:
	amba_release_regions(dev);
 out:
	return ret;
}

static int clcdfb_remove(struct amba_device *dev)
{
	struct clcd_fb *fb = amba_get_drvdata(dev);

	clcdfb_disable(fb);
	unregister_framebuffer(&fb->fb);
	if (fb->fb.cmap.len)
		fb_dealloc_cmap(&fb->fb.cmap);
	iounmap(fb->regs);
	clk_unprepare(fb->clk);
	clk_put(fb->clk);

	fb->board->remove(fb);

	kfree(fb);

	amba_release_regions(dev);

	return 0;
}

static struct clcd_vendor_data vendor_arm = {
	/* Sets up the versatile board displays */
	.init_panel = versatile_clcd_init_panel,
};

static struct clcd_vendor_data vendor_nomadik = {
	.clock_timregs = true,
	.packed_24_bit_pixels = true,
	.st_bitmux_control = true,
	.init_board = nomadik_clcd_init_board,
	.init_panel = nomadik_clcd_init_panel,
};

static const struct amba_id clcdfb_id_table[] = {
	{
		.id	= 0x00041110,
		.mask	= 0x000ffffe,
		.data	= &vendor_arm,
	},
	/* ST Electronics Nomadik variant */
	{
		.id	= 0x00180110,
		.mask	= 0x00fffffe,
		.data	= &vendor_nomadik,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, clcdfb_id_table);

static struct amba_driver clcd_driver = {
	.drv 		= {
		.name	= "clcd-pl11x",
	},
	.probe		= clcdfb_probe,
	.remove		= clcdfb_remove,
	.id_table	= clcdfb_id_table,
};

static int __init amba_clcdfb_init(void)
{
	if (fb_get_options("ambafb", NULL))
		return -ENODEV;

	return amba_driver_register(&clcd_driver);
}

module_init(amba_clcdfb_init);

static void __exit amba_clcdfb_exit(void)
{
	amba_driver_unregister(&clcd_driver);
}

module_exit(amba_clcdfb_exit);

MODULE_DESCRIPTION("ARM PrimeCell PL110 CLCD core driver");
MODULE_LICENSE("GPL");
