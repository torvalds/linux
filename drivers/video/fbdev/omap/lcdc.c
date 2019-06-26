// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OMAP1 internal LCD controller
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/clk.h>
#include <linux/gfp.h>

#include <mach/lcdc.h>
#include <linux/omap-dma.h>

#include <asm/mach-types.h>

#include "omapfb.h"

#include "lcdc.h"

#define MODULE_NAME			"lcdc"

#define MAX_PALETTE_SIZE		PAGE_SIZE

enum lcdc_load_mode {
	OMAP_LCDC_LOAD_PALETTE,
	OMAP_LCDC_LOAD_FRAME,
	OMAP_LCDC_LOAD_PALETTE_AND_FRAME
};

static struct omap_lcd_controller {
	enum omapfb_update_mode	update_mode;
	int			ext_mode;

	unsigned long		frame_offset;
	int			screen_width;
	int			xres;
	int			yres;

	enum omapfb_color_format	color_mode;
	int			bpp;
	void			*palette_virt;
	dma_addr_t		palette_phys;
	int			palette_code;
	int			palette_size;

	unsigned int		irq_mask;
	struct completion	last_frame_complete;
	struct completion	palette_load_complete;
	struct clk		*lcd_ck;
	struct omapfb_device	*fbdev;

	void			(*dma_callback)(void *data);
	void			*dma_callback_data;

	dma_addr_t		vram_phys;
	void			*vram_virt;
	unsigned long		vram_size;
} lcdc;

static inline void enable_irqs(int mask)
{
	lcdc.irq_mask |= mask;
}

static inline void disable_irqs(int mask)
{
	lcdc.irq_mask &= ~mask;
}

static void set_load_mode(enum lcdc_load_mode mode)
{
	u32 l;

	l = omap_readl(OMAP_LCDC_CONTROL);
	l &= ~(3 << 20);
	switch (mode) {
	case OMAP_LCDC_LOAD_PALETTE:
		l |= 1 << 20;
		break;
	case OMAP_LCDC_LOAD_FRAME:
		l |= 2 << 20;
		break;
	case OMAP_LCDC_LOAD_PALETTE_AND_FRAME:
		break;
	default:
		BUG();
	}
	omap_writel(l, OMAP_LCDC_CONTROL);
}

static void enable_controller(void)
{
	u32 l;

	l = omap_readl(OMAP_LCDC_CONTROL);
	l |= OMAP_LCDC_CTRL_LCD_EN;
	l &= ~OMAP_LCDC_IRQ_MASK;
	l |= lcdc.irq_mask | OMAP_LCDC_IRQ_DONE;	/* enabled IRQs */
	omap_writel(l, OMAP_LCDC_CONTROL);
}

static void disable_controller_async(void)
{
	u32 l;
	u32 mask;

	l = omap_readl(OMAP_LCDC_CONTROL);
	mask = OMAP_LCDC_CTRL_LCD_EN | OMAP_LCDC_IRQ_MASK;
	/*
	 * Preserve the DONE mask, since we still want to get the
	 * final DONE irq. It will be disabled in the IRQ handler.
	 */
	mask &= ~OMAP_LCDC_IRQ_DONE;
	l &= ~mask;
	omap_writel(l, OMAP_LCDC_CONTROL);
}

static void disable_controller(void)
{
	init_completion(&lcdc.last_frame_complete);
	disable_controller_async();
	if (!wait_for_completion_timeout(&lcdc.last_frame_complete,
				msecs_to_jiffies(500)))
		dev_err(lcdc.fbdev->dev, "timeout waiting for FRAME DONE\n");
}

static void reset_controller(u32 status)
{
	static unsigned long reset_count;
	static unsigned long last_jiffies;

	disable_controller_async();
	reset_count++;
	if (reset_count == 1 || time_after(jiffies, last_jiffies + HZ)) {
		dev_err(lcdc.fbdev->dev,
			  "resetting (status %#010x,reset count %lu)\n",
			  status, reset_count);
		last_jiffies = jiffies;
	}
	if (reset_count < 100) {
		enable_controller();
	} else {
		reset_count = 0;
		dev_err(lcdc.fbdev->dev,
			"too many reset attempts, giving up.\n");
	}
}

/*
 * Configure the LCD DMA according to the current mode specified by parameters
 * in lcdc.fbdev and fbdev->var.
 */
static void setup_lcd_dma(void)
{
	static const int dma_elem_type[] = {
		0,
		OMAP_DMA_DATA_TYPE_S8,
		OMAP_DMA_DATA_TYPE_S16,
		0,
		OMAP_DMA_DATA_TYPE_S32,
	};
	struct omapfb_plane_struct *plane = lcdc.fbdev->fb_info[0]->par;
	struct fb_var_screeninfo *var = &lcdc.fbdev->fb_info[0]->var;
	unsigned long	src;
	int		esize, xelem, yelem;

	src = lcdc.vram_phys + lcdc.frame_offset;

	switch (var->rotate) {
	case 0:
		if (plane->info.mirror || (src & 3) ||
		    lcdc.color_mode == OMAPFB_COLOR_YUV420 ||
		    (lcdc.xres & 1))
			esize = 2;
		else
			esize = 4;
		xelem = lcdc.xres * lcdc.bpp / 8 / esize;
		yelem = lcdc.yres;
		break;
	case 90:
	case 180:
	case 270:
		if (cpu_is_omap15xx()) {
			BUG();
		}
		esize = 2;
		xelem = lcdc.yres * lcdc.bpp / 16;
		yelem = lcdc.xres;
		break;
	default:
		BUG();
		return;
	}
#ifdef VERBOSE
	dev_dbg(lcdc.fbdev->dev,
		 "setup_dma: src %#010lx esize %d xelem %d yelem %d\n",
		 src, esize, xelem, yelem);
#endif
	omap_set_lcd_dma_b1(src, xelem, yelem, dma_elem_type[esize]);
	if (!cpu_is_omap15xx()) {
		int bpp = lcdc.bpp;

		/*
		 * YUV support is only for external mode when we have the
		 * YUV window embedded in a 16bpp frame buffer.
		 */
		if (lcdc.color_mode == OMAPFB_COLOR_YUV420)
			bpp = 16;
		/* Set virtual xres elem size */
		omap_set_lcd_dma_b1_vxres(
			lcdc.screen_width * bpp / 8 / esize);
		/* Setup transformations */
		omap_set_lcd_dma_b1_rotation(var->rotate);
		omap_set_lcd_dma_b1_mirror(plane->info.mirror);
	}
	omap_setup_lcd_dma();
}

static irqreturn_t lcdc_irq_handler(int irq, void *dev_id)
{
	u32 status;

	status = omap_readl(OMAP_LCDC_STATUS);

	if (status & (OMAP_LCDC_STAT_FUF | OMAP_LCDC_STAT_SYNC_LOST))
		reset_controller(status);
	else {
		if (status & OMAP_LCDC_STAT_DONE) {
			u32 l;

			/*
			 * Disable IRQ_DONE. The status bit will be cleared
			 * only when the controller is reenabled and we don't
			 * want to get more interrupts.
			 */
			l = omap_readl(OMAP_LCDC_CONTROL);
			l &= ~OMAP_LCDC_IRQ_DONE;
			omap_writel(l, OMAP_LCDC_CONTROL);
			complete(&lcdc.last_frame_complete);
		}
		if (status & OMAP_LCDC_STAT_LOADED_PALETTE) {
			disable_controller_async();
			complete(&lcdc.palette_load_complete);
		}
	}

	/*
	 * Clear these interrupt status bits.
	 * Sync_lost, FUF bits were cleared by disabling the LCD controller
	 * LOADED_PALETTE can be cleared this way only in palette only
	 * load mode. In other load modes it's cleared by disabling the
	 * controller.
	 */
	status &= ~(OMAP_LCDC_STAT_VSYNC |
		    OMAP_LCDC_STAT_LOADED_PALETTE |
		    OMAP_LCDC_STAT_ABC |
		    OMAP_LCDC_STAT_LINE_INT);
	omap_writel(status, OMAP_LCDC_STATUS);
	return IRQ_HANDLED;
}

/*
 * Change to a new video mode. We defer this to a later time to avoid any
 * flicker and not to mess up the current LCD DMA context. For this we disable
 * the LCD controller, which will generate a DONE irq after the last frame has
 * been transferred. Then it'll be safe to reconfigure both the LCD controller
 * as well as the LCD DMA.
 */
static int omap_lcdc_setup_plane(int plane, int channel_out,
				 unsigned long offset, int screen_width,
				 int pos_x, int pos_y, int width, int height,
				 int color_mode)
{
	struct fb_var_screeninfo *var = &lcdc.fbdev->fb_info[0]->var;
	struct lcd_panel *panel = lcdc.fbdev->panel;
	int rot_x, rot_y;

	if (var->rotate == 0) {
		rot_x = panel->x_res;
		rot_y = panel->y_res;
	} else {
		rot_x = panel->y_res;
		rot_y = panel->x_res;
	}
	if (plane != 0 || channel_out != 0 || pos_x != 0 || pos_y != 0 ||
	    width > rot_x || height > rot_y) {
#ifdef VERBOSE
		dev_dbg(lcdc.fbdev->dev,
			"invalid plane params plane %d pos_x %d pos_y %d "
			"w %d h %d\n", plane, pos_x, pos_y, width, height);
#endif
		return -EINVAL;
	}

	lcdc.frame_offset = offset;
	lcdc.xres = width;
	lcdc.yres = height;
	lcdc.screen_width = screen_width;
	lcdc.color_mode = color_mode;

	switch (color_mode) {
	case OMAPFB_COLOR_CLUT_8BPP:
		lcdc.bpp = 8;
		lcdc.palette_code = 0x3000;
		lcdc.palette_size = 512;
		break;
	case OMAPFB_COLOR_RGB565:
		lcdc.bpp = 16;
		lcdc.palette_code = 0x4000;
		lcdc.palette_size = 32;
		break;
	case OMAPFB_COLOR_RGB444:
		lcdc.bpp = 16;
		lcdc.palette_code = 0x4000;
		lcdc.palette_size = 32;
		break;
	case OMAPFB_COLOR_YUV420:
		if (lcdc.ext_mode) {
			lcdc.bpp = 12;
			break;
		}
		/* fallthrough */
	case OMAPFB_COLOR_YUV422:
		if (lcdc.ext_mode) {
			lcdc.bpp = 16;
			break;
		}
		/* fallthrough */
	default:
		/* FIXME: other BPPs.
		 * bpp1: code  0,     size 256
		 * bpp2: code  0x1000 size 256
		 * bpp4: code  0x2000 size 256
		 * bpp12: code 0x4000 size 32
		 */
		dev_dbg(lcdc.fbdev->dev, "invalid color mode %d\n", color_mode);
		BUG();
		return -1;
	}

	if (lcdc.ext_mode) {
		setup_lcd_dma();
		return 0;
	}

	if (lcdc.update_mode == OMAPFB_AUTO_UPDATE) {
		disable_controller();
		omap_stop_lcd_dma();
		setup_lcd_dma();
		enable_controller();
	}

	return 0;
}

static int omap_lcdc_enable_plane(int plane, int enable)
{
	dev_dbg(lcdc.fbdev->dev,
		"plane %d enable %d update_mode %d ext_mode %d\n",
		plane, enable, lcdc.update_mode, lcdc.ext_mode);
	if (plane != OMAPFB_PLANE_GFX)
		return -EINVAL;

	return 0;
}

/*
 * Configure the LCD DMA for a palette load operation and do the palette
 * downloading synchronously. We don't use the frame+palette load mode of
 * the controller, since the palette can always be downloaded separately.
 */
static void load_palette(void)
{
	u16	*palette;

	palette = (u16 *)lcdc.palette_virt;

	*(u16 *)palette &= 0x0fff;
	*(u16 *)palette |= lcdc.palette_code;

	omap_set_lcd_dma_b1(lcdc.palette_phys,
		lcdc.palette_size / 4 + 1, 1, OMAP_DMA_DATA_TYPE_S32);

	omap_set_lcd_dma_single_transfer(1);
	omap_setup_lcd_dma();

	init_completion(&lcdc.palette_load_complete);
	enable_irqs(OMAP_LCDC_IRQ_LOADED_PALETTE);
	set_load_mode(OMAP_LCDC_LOAD_PALETTE);
	enable_controller();
	if (!wait_for_completion_timeout(&lcdc.palette_load_complete,
				msecs_to_jiffies(500)))
		dev_err(lcdc.fbdev->dev, "timeout waiting for FRAME DONE\n");
	/* The controller gets disabled in the irq handler */
	disable_irqs(OMAP_LCDC_IRQ_LOADED_PALETTE);
	omap_stop_lcd_dma();

	omap_set_lcd_dma_single_transfer(lcdc.ext_mode);
}

/* Used only in internal controller mode */
static int omap_lcdc_setcolreg(u_int regno, u16 red, u16 green, u16 blue,
			       u16 transp, int update_hw_pal)
{
	u16 *palette;

	if (lcdc.color_mode != OMAPFB_COLOR_CLUT_8BPP || regno > 255)
		return -EINVAL;

	palette = (u16 *)lcdc.palette_virt;

	palette[regno] &= ~0x0fff;
	palette[regno] |= ((red >> 12) << 8) | ((green >> 12) << 4 ) |
			   (blue >> 12);

	if (update_hw_pal) {
		disable_controller();
		omap_stop_lcd_dma();
		load_palette();
		setup_lcd_dma();
		set_load_mode(OMAP_LCDC_LOAD_FRAME);
		enable_controller();
	}

	return 0;
}

static void calc_ck_div(int is_tft, int pck, int *pck_div)
{
	unsigned long lck;

	pck = max(1, pck);
	lck = clk_get_rate(lcdc.lcd_ck);
	*pck_div = (lck + pck - 1) / pck;
	if (is_tft)
		*pck_div = max(2, *pck_div);
	else
		*pck_div = max(3, *pck_div);
	if (*pck_div > 255) {
		/* FIXME: try to adjust logic clock divider as well */
		*pck_div = 255;
		dev_warn(lcdc.fbdev->dev, "pixclock %d kHz too low.\n",
			 pck / 1000);
	}
}

static inline void setup_regs(void)
{
	u32 l;
	struct lcd_panel *panel = lcdc.fbdev->panel;
	int is_tft = panel->config & OMAP_LCDC_PANEL_TFT;
	unsigned long lck;
	int pcd;

	l = omap_readl(OMAP_LCDC_CONTROL);
	l &= ~OMAP_LCDC_CTRL_LCD_TFT;
	l |= is_tft ? OMAP_LCDC_CTRL_LCD_TFT : 0;
#ifdef CONFIG_MACH_OMAP_PALMTE
/* FIXME:if (machine_is_omap_palmte()) { */
		/* PalmTE uses alternate TFT setting in 8BPP mode */
		l |= (is_tft && panel->bpp == 8) ? 0x810000 : 0;
/*	} */
#endif
	omap_writel(l, OMAP_LCDC_CONTROL);

	l = omap_readl(OMAP_LCDC_TIMING2);
	l &= ~(((1 << 6) - 1) << 20);
	l |= (panel->config & OMAP_LCDC_SIGNAL_MASK) << 20;
	omap_writel(l, OMAP_LCDC_TIMING2);

	l = panel->x_res - 1;
	l |= (panel->hsw - 1) << 10;
	l |= (panel->hfp - 1) << 16;
	l |= (panel->hbp - 1) << 24;
	omap_writel(l, OMAP_LCDC_TIMING0);

	l = panel->y_res - 1;
	l |= (panel->vsw - 1) << 10;
	l |= panel->vfp << 16;
	l |= panel->vbp << 24;
	omap_writel(l, OMAP_LCDC_TIMING1);

	l = omap_readl(OMAP_LCDC_TIMING2);
	l &= ~0xff;

	lck = clk_get_rate(lcdc.lcd_ck);

	if (!panel->pcd)
		calc_ck_div(is_tft, panel->pixel_clock * 1000, &pcd);
	else {
		dev_warn(lcdc.fbdev->dev,
		    "Pixel clock divider value is obsolete.\n"
		    "Try to set pixel_clock to %lu and pcd to 0 "
		    "in drivers/video/omap/lcd_%s.c and submit a patch.\n",
			lck / panel->pcd / 1000, panel->name);

		pcd = panel->pcd;
	}
	l |= pcd & 0xff;
	l |= panel->acb << 8;
	omap_writel(l, OMAP_LCDC_TIMING2);

	/* update panel info with the exact clock */
	panel->pixel_clock = lck / pcd / 1000;
}

/*
 * Configure the LCD controller, download the color palette and start a looped
 * DMA transfer of the frame image data. Called only in internal
 * controller mode.
 */
static int omap_lcdc_set_update_mode(enum omapfb_update_mode mode)
{
	int r = 0;

	if (mode != lcdc.update_mode) {
		switch (mode) {
		case OMAPFB_AUTO_UPDATE:
			setup_regs();
			load_palette();

			/* Setup and start LCD DMA */
			setup_lcd_dma();

			set_load_mode(OMAP_LCDC_LOAD_FRAME);
			enable_irqs(OMAP_LCDC_IRQ_DONE);
			/* This will start the actual DMA transfer */
			enable_controller();
			lcdc.update_mode = mode;
			break;
		case OMAPFB_UPDATE_DISABLED:
			disable_controller();
			omap_stop_lcd_dma();
			lcdc.update_mode = mode;
			break;
		default:
			r = -EINVAL;
		}
	}

	return r;
}

static enum omapfb_update_mode omap_lcdc_get_update_mode(void)
{
	return lcdc.update_mode;
}

/* PM code called only in internal controller mode */
static void omap_lcdc_suspend(void)
{
	omap_lcdc_set_update_mode(OMAPFB_UPDATE_DISABLED);
}

static void omap_lcdc_resume(void)
{
	omap_lcdc_set_update_mode(OMAPFB_AUTO_UPDATE);
}

static void omap_lcdc_get_caps(int plane, struct omapfb_caps *caps)
{
	return;
}

int omap_lcdc_set_dma_callback(void (*callback)(void *data), void *data)
{
	BUG_ON(callback == NULL);

	if (lcdc.dma_callback)
		return -EBUSY;
	else {
		lcdc.dma_callback = callback;
		lcdc.dma_callback_data = data;
	}
	return 0;
}
EXPORT_SYMBOL(omap_lcdc_set_dma_callback);

void omap_lcdc_free_dma_callback(void)
{
	lcdc.dma_callback = NULL;
}
EXPORT_SYMBOL(omap_lcdc_free_dma_callback);

static void lcdc_dma_handler(u16 status, void *data)
{
	if (lcdc.dma_callback)
		lcdc.dma_callback(lcdc.dma_callback_data);
}

static int alloc_palette_ram(void)
{
	lcdc.palette_virt = dma_alloc_wc(lcdc.fbdev->dev, MAX_PALETTE_SIZE,
					 &lcdc.palette_phys, GFP_KERNEL);
	if (lcdc.palette_virt == NULL) {
		dev_err(lcdc.fbdev->dev, "failed to alloc palette memory\n");
		return -ENOMEM;
	}
	memset(lcdc.palette_virt, 0, MAX_PALETTE_SIZE);

	return 0;
}

static void free_palette_ram(void)
{
	dma_free_wc(lcdc.fbdev->dev, MAX_PALETTE_SIZE, lcdc.palette_virt,
		    lcdc.palette_phys);
}

static int alloc_fbmem(struct omapfb_mem_region *region)
{
	int bpp;
	int frame_size;
	struct lcd_panel *panel = lcdc.fbdev->panel;

	bpp = panel->bpp;
	if (bpp == 12)
		bpp = 16;
	frame_size = PAGE_ALIGN(panel->x_res * bpp / 8 * panel->y_res);
	if (region->size > frame_size)
		frame_size = region->size;
	lcdc.vram_size = frame_size;
	lcdc.vram_virt = dma_alloc_wc(lcdc.fbdev->dev, lcdc.vram_size,
				      &lcdc.vram_phys, GFP_KERNEL);
	if (lcdc.vram_virt == NULL) {
		dev_err(lcdc.fbdev->dev, "unable to allocate FB DMA memory\n");
		return -ENOMEM;
	}
	region->size = frame_size;
	region->paddr = lcdc.vram_phys;
	region->vaddr = lcdc.vram_virt;
	region->alloc = 1;

	memset(lcdc.vram_virt, 0, lcdc.vram_size);

	return 0;
}

static void free_fbmem(void)
{
	dma_free_wc(lcdc.fbdev->dev, lcdc.vram_size, lcdc.vram_virt,
		    lcdc.vram_phys);
}

static int setup_fbmem(struct omapfb_mem_desc *req_md)
{
	if (!req_md->region_cnt) {
		dev_err(lcdc.fbdev->dev, "no memory regions defined\n");
		return -EINVAL;
	}

	if (req_md->region_cnt > 1) {
		dev_err(lcdc.fbdev->dev, "only one plane is supported\n");
		req_md->region_cnt = 1;
	}

	return alloc_fbmem(&req_md->region[0]);
}

static int omap_lcdc_init(struct omapfb_device *fbdev, int ext_mode,
			  struct omapfb_mem_desc *req_vram)
{
	int r;
	u32 l;
	int rate;
	struct clk *tc_ck;

	lcdc.irq_mask = 0;

	lcdc.fbdev = fbdev;
	lcdc.ext_mode = ext_mode;

	l = 0;
	omap_writel(l, OMAP_LCDC_CONTROL);

	/* FIXME:
	 * According to errata some platforms have a clock rate limitiation
	 */
	lcdc.lcd_ck = clk_get(fbdev->dev, "lcd_ck");
	if (IS_ERR(lcdc.lcd_ck)) {
		dev_err(fbdev->dev, "unable to access LCD clock\n");
		r = PTR_ERR(lcdc.lcd_ck);
		goto fail0;
	}

	tc_ck = clk_get(fbdev->dev, "tc_ck");
	if (IS_ERR(tc_ck)) {
		dev_err(fbdev->dev, "unable to access TC clock\n");
		r = PTR_ERR(tc_ck);
		goto fail1;
	}

	rate = clk_get_rate(tc_ck);
	clk_put(tc_ck);

	if (machine_is_ams_delta())
		rate /= 4;
	if (machine_is_omap_h3())
		rate /= 3;
	r = clk_set_rate(lcdc.lcd_ck, rate);
	if (r) {
		dev_err(fbdev->dev, "failed to adjust LCD rate\n");
		goto fail1;
	}
	clk_enable(lcdc.lcd_ck);

	r = request_irq(OMAP_LCDC_IRQ, lcdc_irq_handler, 0, MODULE_NAME, fbdev);
	if (r) {
		dev_err(fbdev->dev, "unable to get IRQ\n");
		goto fail2;
	}

	r = omap_request_lcd_dma(lcdc_dma_handler, NULL);
	if (r) {
		dev_err(fbdev->dev, "unable to get LCD DMA\n");
		goto fail3;
	}

	omap_set_lcd_dma_single_transfer(ext_mode);
	omap_set_lcd_dma_ext_controller(ext_mode);

	if (!ext_mode)
		if ((r = alloc_palette_ram()) < 0)
			goto fail4;

	if ((r = setup_fbmem(req_vram)) < 0)
		goto fail5;

	pr_info("omapfb: LCDC initialized\n");

	return 0;
fail5:
	if (!ext_mode)
		free_palette_ram();
fail4:
	omap_free_lcd_dma();
fail3:
	free_irq(OMAP_LCDC_IRQ, lcdc.fbdev);
fail2:
	clk_disable(lcdc.lcd_ck);
fail1:
	clk_put(lcdc.lcd_ck);
fail0:
	return r;
}

static void omap_lcdc_cleanup(void)
{
	if (!lcdc.ext_mode)
		free_palette_ram();
	free_fbmem();
	omap_free_lcd_dma();
	free_irq(OMAP_LCDC_IRQ, lcdc.fbdev);
	clk_disable(lcdc.lcd_ck);
	clk_put(lcdc.lcd_ck);
}

const struct lcd_ctrl omap1_int_ctrl = {
	.name			= "internal",
	.init			= omap_lcdc_init,
	.cleanup		= omap_lcdc_cleanup,
	.get_caps		= omap_lcdc_get_caps,
	.set_update_mode	= omap_lcdc_set_update_mode,
	.get_update_mode	= omap_lcdc_get_update_mode,
	.update_window		= NULL,
	.suspend		= omap_lcdc_suspend,
	.resume			= omap_lcdc_resume,
	.setup_plane		= omap_lcdc_setup_plane,
	.enable_plane		= omap_lcdc_enable_plane,
	.setcolreg		= omap_lcdc_setcolreg,
};
