/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_plane_helper.h>
#include <linux/workqueue.h>

#include "tilcdc_drv.h"
#include "tilcdc_regs.h"

#define TILCDC_VBLANK_SAFETY_THRESHOLD_US 1000

struct tilcdc_crtc {
	struct drm_crtc base;

	struct drm_plane primary;
	const struct tilcdc_panel_info *info;
	struct drm_pending_vblank_event *event;
	bool enabled;
	wait_queue_head_t frame_done_wq;
	bool frame_done;
	spinlock_t irq_lock;

	unsigned int lcd_fck_rate;

	ktime_t last_vblank;

	struct drm_framebuffer *curr_fb;
	struct drm_framebuffer *next_fb;

	/* for deferred fb unref's: */
	struct drm_flip_work unref_work;

	/* Only set if an external encoder is connected */
	bool simulate_vesa_sync;

	int sync_lost_count;
	bool frame_intact;
};
#define to_tilcdc_crtc(x) container_of(x, struct tilcdc_crtc, base)

static void unref_worker(struct drm_flip_work *work, void *val)
{
	struct tilcdc_crtc *tilcdc_crtc =
		container_of(work, struct tilcdc_crtc, unref_work);
	struct drm_device *dev = tilcdc_crtc->base.dev;

	mutex_lock(&dev->mode_config.mutex);
	drm_framebuffer_unreference(val);
	mutex_unlock(&dev->mode_config.mutex);
}

static void set_scanout(struct drm_crtc *crtc, struct drm_framebuffer *fb)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_gem_cma_object *gem;
	unsigned int depth, bpp;
	dma_addr_t start, end;
	u64 dma_base_and_ceiling;

	drm_fb_get_bpp_depth(fb->pixel_format, &depth, &bpp);
	gem = drm_fb_cma_get_gem_obj(fb, 0);

	start = gem->paddr + fb->offsets[0] +
		crtc->y * fb->pitches[0] +
		crtc->x * bpp / 8;

	end = start + (crtc->mode.vdisplay * fb->pitches[0]);

	/* Write LCDC_DMA_FB_BASE_ADDR_0_REG and LCDC_DMA_FB_CEILING_ADDR_0_REG
	 * with a single insruction, if available. This should make it more
	 * unlikely that LCDC would fetch the DMA addresses in the middle of
	 * an update.
	 */
	dma_base_and_ceiling = (u64)(end - 1) << 32 | start;
	tilcdc_write64(dev, LCDC_DMA_FB_BASE_ADDR_0_REG, dma_base_and_ceiling);

	if (tilcdc_crtc->curr_fb)
		drm_flip_work_queue(&tilcdc_crtc->unref_work,
			tilcdc_crtc->curr_fb);

	tilcdc_crtc->curr_fb = fb;
}

static void tilcdc_crtc_enable_irqs(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = dev->dev_private;

	tilcdc_clear_irqstatus(dev, 0xffffffff);

	if (priv->rev == 1) {
		tilcdc_set(dev, LCDC_RASTER_CTRL_REG,
			LCDC_V1_UNDERFLOW_INT_ENA);
		tilcdc_set(dev, LCDC_DMA_CTRL_REG,
			LCDC_V1_END_OF_FRAME_INT_ENA);
	} else {
		tilcdc_write(dev, LCDC_INT_ENABLE_SET_REG,
			LCDC_V2_UNDERFLOW_INT_ENA |
			LCDC_V2_END_OF_FRAME0_INT_ENA |
			LCDC_FRAME_DONE | LCDC_SYNC_LOST);
	}
}

static void tilcdc_crtc_disable_irqs(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = dev->dev_private;

	/* disable irqs that we might have enabled: */
	if (priv->rev == 1) {
		tilcdc_clear(dev, LCDC_RASTER_CTRL_REG,
			LCDC_V1_UNDERFLOW_INT_ENA | LCDC_V1_PL_INT_ENA);
		tilcdc_clear(dev, LCDC_DMA_CTRL_REG,
			LCDC_V1_END_OF_FRAME_INT_ENA);
	} else {
		tilcdc_write(dev, LCDC_INT_ENABLE_CLR_REG,
			LCDC_V2_UNDERFLOW_INT_ENA | LCDC_V2_PL_INT_ENA |
			LCDC_V2_END_OF_FRAME0_INT_ENA |
			LCDC_FRAME_DONE | LCDC_SYNC_LOST);
	}
}

static void reset(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct tilcdc_drm_private *priv = dev->dev_private;

	if (priv->rev != 2)
		return;

	tilcdc_set(dev, LCDC_CLK_RESET_REG, LCDC_CLK_MAIN_RESET);
	usleep_range(250, 1000);
	tilcdc_clear(dev, LCDC_CLK_RESET_REG, LCDC_CLK_MAIN_RESET);
}

static void tilcdc_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);

	WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

	if (tilcdc_crtc->enabled)
		return;

	pm_runtime_get_sync(dev->dev);

	reset(crtc);

	tilcdc_crtc_enable_irqs(dev);

	tilcdc_clear(dev, LCDC_DMA_CTRL_REG, LCDC_DUAL_FRAME_BUFFER_ENABLE);
	tilcdc_set(dev, LCDC_RASTER_CTRL_REG, LCDC_PALETTE_LOAD_MODE(DATA_ONLY));
	tilcdc_set(dev, LCDC_RASTER_CTRL_REG, LCDC_RASTER_ENABLE);

	drm_crtc_vblank_on(crtc);

	tilcdc_crtc->enabled = true;
}

void tilcdc_crtc_disable(struct drm_crtc *crtc)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct tilcdc_drm_private *priv = dev->dev_private;

	WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

	if (!tilcdc_crtc->enabled)
		return;

	tilcdc_crtc->frame_done = false;
	tilcdc_clear(dev, LCDC_RASTER_CTRL_REG, LCDC_RASTER_ENABLE);

	/*
	 * if necessary wait for framedone irq which will still come
	 * before putting things to sleep..
	 */
	if (priv->rev == 2) {
		int ret = wait_event_timeout(tilcdc_crtc->frame_done_wq,
					     tilcdc_crtc->frame_done,
					     msecs_to_jiffies(500));
		if (ret == 0)
			dev_err(dev->dev, "%s: timeout waiting for framedone\n",
				__func__);
	}

	drm_crtc_vblank_off(crtc);

	tilcdc_crtc_disable_irqs(dev);

	pm_runtime_put_sync(dev->dev);

	if (tilcdc_crtc->next_fb) {
		drm_flip_work_queue(&tilcdc_crtc->unref_work,
				    tilcdc_crtc->next_fb);
		tilcdc_crtc->next_fb = NULL;
	}

	if (tilcdc_crtc->curr_fb) {
		drm_flip_work_queue(&tilcdc_crtc->unref_work,
				    tilcdc_crtc->curr_fb);
		tilcdc_crtc->curr_fb = NULL;
	}

	drm_flip_work_commit(&tilcdc_crtc->unref_work, priv->wq);
	tilcdc_crtc->last_vblank = ktime_set(0, 0);

	tilcdc_crtc->enabled = false;
}

static bool tilcdc_crtc_is_on(struct drm_crtc *crtc)
{
	return crtc->state && crtc->state->enable && crtc->state->active;
}

static void tilcdc_crtc_destroy(struct drm_crtc *crtc)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);
	struct tilcdc_drm_private *priv = crtc->dev->dev_private;

	drm_modeset_lock_crtc(crtc, NULL);
	tilcdc_crtc_disable(crtc);
	drm_modeset_unlock_crtc(crtc);

	flush_workqueue(priv->wq);

	of_node_put(crtc->port);
	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&tilcdc_crtc->unref_work);
}

int tilcdc_crtc_update_fb(struct drm_crtc *crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

	if (tilcdc_crtc->event) {
		dev_err(dev->dev, "already pending page flip!\n");
		return -EBUSY;
	}

	drm_framebuffer_reference(fb);

	crtc->primary->fb = fb;

	spin_lock_irqsave(&tilcdc_crtc->irq_lock, flags);

	if (crtc->hwmode.vrefresh && ktime_to_ns(tilcdc_crtc->last_vblank)) {
		ktime_t next_vblank;
		s64 tdiff;

		next_vblank = ktime_add_us(tilcdc_crtc->last_vblank,
			1000000 / crtc->hwmode.vrefresh);

		tdiff = ktime_to_us(ktime_sub(next_vblank, ktime_get()));

		if (tdiff < TILCDC_VBLANK_SAFETY_THRESHOLD_US)
			tilcdc_crtc->next_fb = fb;
	}

	if (tilcdc_crtc->next_fb != fb)
		set_scanout(crtc, fb);

	tilcdc_crtc->event = event;

	spin_unlock_irqrestore(&tilcdc_crtc->irq_lock, flags);

	return 0;
}

static bool tilcdc_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);

	if (!tilcdc_crtc->simulate_vesa_sync)
		return true;

	/*
	 * tilcdc does not generate VESA-compliant sync but aligns
	 * VS on the second edge of HS instead of first edge.
	 * We use adjusted_mode, to fixup sync by aligning both rising
	 * edges and add HSKEW offset to fix the sync.
	 */
	adjusted_mode->hskew = mode->hsync_end - mode->hsync_start;
	adjusted_mode->flags |= DRM_MODE_FLAG_HSKEW;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC) {
		adjusted_mode->flags |= DRM_MODE_FLAG_PHSYNC;
		adjusted_mode->flags &= ~DRM_MODE_FLAG_NHSYNC;
	} else {
		adjusted_mode->flags |= DRM_MODE_FLAG_NHSYNC;
		adjusted_mode->flags &= ~DRM_MODE_FLAG_PHSYNC;
	}

	return true;
}

static void tilcdc_crtc_set_clk(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct tilcdc_drm_private *priv = dev->dev_private;
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);
	const unsigned clkdiv = 2; /* using a fixed divider of 2 */
	int ret;

	/* mode.clock is in KHz, set_rate wants parameter in Hz */
	ret = clk_set_rate(priv->clk, crtc->mode.clock * 1000 * clkdiv);
	if (ret < 0) {
		dev_err(dev->dev, "failed to set display clock rate to: %d\n",
			crtc->mode.clock);
		return;
	}

	tilcdc_crtc->lcd_fck_rate = clk_get_rate(priv->clk);

	DBG("lcd_clk=%u, mode clock=%d, div=%u",
	    tilcdc_crtc->lcd_fck_rate, crtc->mode.clock, clkdiv);

	/* Configure the LCD clock divisor. */
	tilcdc_write(dev, LCDC_CTRL_REG, LCDC_CLK_DIVISOR(clkdiv) |
		     LCDC_RASTER_MODE);

	if (priv->rev == 2)
		tilcdc_set(dev, LCDC_CLK_ENABLE_REG,
				LCDC_V2_DMA_CLK_EN | LCDC_V2_LIDD_CLK_EN |
				LCDC_V2_CORE_CLK_EN);
}

static void tilcdc_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct tilcdc_drm_private *priv = dev->dev_private;
	const struct tilcdc_panel_info *info = tilcdc_crtc->info;
	uint32_t reg, hbp, hfp, hsw, vbp, vfp, vsw;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct drm_framebuffer *fb = crtc->primary->state->fb;

	WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

	if (WARN_ON(!info))
		return;

	if (WARN_ON(!fb))
		return;

	/* Configure the Burst Size and fifo threshold of DMA: */
	reg = tilcdc_read(dev, LCDC_DMA_CTRL_REG) & ~0x00000770;
	switch (info->dma_burst_sz) {
	case 1:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_1);
		break;
	case 2:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_2);
		break;
	case 4:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_4);
		break;
	case 8:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_8);
		break;
	case 16:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_16);
		break;
	default:
		dev_err(dev->dev, "invalid burst size\n");
		return;
	}
	reg |= (info->fifo_th << 8);
	tilcdc_write(dev, LCDC_DMA_CTRL_REG, reg);

	/* Configure timings: */
	hbp = mode->htotal - mode->hsync_end;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;

	DBG("%dx%d, hbp=%u, hfp=%u, hsw=%u, vbp=%u, vfp=%u, vsw=%u",
	    mode->hdisplay, mode->vdisplay, hbp, hfp, hsw, vbp, vfp, vsw);

	/* Set AC Bias Period and Number of Transitions per Interrupt: */
	reg = tilcdc_read(dev, LCDC_RASTER_TIMING_2_REG) & ~0x000fff00;
	reg |= LCDC_AC_BIAS_FREQUENCY(info->ac_bias) |
		LCDC_AC_BIAS_TRANSITIONS_PER_INT(info->ac_bias_intrpt);

	/*
	 * subtract one from hfp, hbp, hsw because the hardware uses
	 * a value of 0 as 1
	 */
	if (priv->rev == 2) {
		/* clear bits we're going to set */
		reg &= ~0x78000033;
		reg |= ((hfp-1) & 0x300) >> 8;
		reg |= ((hbp-1) & 0x300) >> 4;
		reg |= ((hsw-1) & 0x3c0) << 21;
	}
	tilcdc_write(dev, LCDC_RASTER_TIMING_2_REG, reg);

	reg = (((mode->hdisplay >> 4) - 1) << 4) |
		(((hbp-1) & 0xff) << 24) |
		(((hfp-1) & 0xff) << 16) |
		(((hsw-1) & 0x3f) << 10);
	if (priv->rev == 2)
		reg |= (((mode->hdisplay >> 4) - 1) & 0x40) >> 3;
	tilcdc_write(dev, LCDC_RASTER_TIMING_0_REG, reg);

	reg = ((mode->vdisplay - 1) & 0x3ff) |
		((vbp & 0xff) << 24) |
		((vfp & 0xff) << 16) |
		(((vsw-1) & 0x3f) << 10);
	tilcdc_write(dev, LCDC_RASTER_TIMING_1_REG, reg);

	/*
	 * be sure to set Bit 10 for the V2 LCDC controller,
	 * otherwise limited to 1024 pixels width, stopping
	 * 1920x1080 being supported.
	 */
	if (priv->rev == 2) {
		if ((mode->vdisplay - 1) & 0x400) {
			tilcdc_set(dev, LCDC_RASTER_TIMING_2_REG,
				LCDC_LPP_B10);
		} else {
			tilcdc_clear(dev, LCDC_RASTER_TIMING_2_REG,
				LCDC_LPP_B10);
		}
	}

	/* Configure display type: */
	reg = tilcdc_read(dev, LCDC_RASTER_CTRL_REG) &
		~(LCDC_TFT_MODE | LCDC_MONO_8BIT_MODE | LCDC_MONOCHROME_MODE |
		  LCDC_V2_TFT_24BPP_MODE | LCDC_V2_TFT_24BPP_UNPACK |
		  0x000ff000 /* Palette Loading Delay bits */);
	reg |= LCDC_TFT_MODE; /* no monochrome/passive support */
	if (info->tft_alt_mode)
		reg |= LCDC_TFT_ALT_ENABLE;
	if (priv->rev == 2) {
		unsigned int depth, bpp;

		drm_fb_get_bpp_depth(fb->pixel_format, &depth, &bpp);
		switch (bpp) {
		case 16:
			break;
		case 32:
			reg |= LCDC_V2_TFT_24BPP_UNPACK;
			/* fallthrough */
		case 24:
			reg |= LCDC_V2_TFT_24BPP_MODE;
			break;
		default:
			dev_err(dev->dev, "invalid pixel format\n");
			return;
		}
	}
	reg |= info->fdd < 12;
	tilcdc_write(dev, LCDC_RASTER_CTRL_REG, reg);

	if (info->invert_pxl_clk)
		tilcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_PIXEL_CLOCK);
	else
		tilcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_PIXEL_CLOCK);

	if (info->sync_ctrl)
		tilcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_SYNC_CTRL);
	else
		tilcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_SYNC_CTRL);

	if (info->sync_edge)
		tilcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_SYNC_EDGE);
	else
		tilcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_SYNC_EDGE);

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		tilcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_HSYNC);
	else
		tilcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_HSYNC);

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		tilcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_VSYNC);
	else
		tilcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_VSYNC);

	if (info->raster_order)
		tilcdc_set(dev, LCDC_RASTER_CTRL_REG, LCDC_RASTER_ORDER);
	else
		tilcdc_clear(dev, LCDC_RASTER_CTRL_REG, LCDC_RASTER_ORDER);

	drm_framebuffer_reference(fb);

	set_scanout(crtc, fb);

	tilcdc_crtc_set_clk(crtc);

	crtc->hwmode = crtc->state->adjusted_mode;
}

static int tilcdc_crtc_atomic_check(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct drm_display_mode *mode = &state->mode;
	int ret;

	/* If we are not active we don't care */
	if (!state->active)
		return 0;

	if (state->state->planes[0].ptr != crtc->primary ||
	    state->state->planes[0].state == NULL ||
	    state->state->planes[0].state->crtc != crtc) {
		dev_dbg(crtc->dev->dev, "CRTC primary plane must be present");
		return -EINVAL;
	}

	ret = tilcdc_crtc_mode_valid(crtc, mode);
	if (ret) {
		dev_dbg(crtc->dev->dev, "Mode \"%s\" not valid", mode->name);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_crtc_funcs tilcdc_crtc_funcs = {
	.destroy        = tilcdc_crtc_destroy,
	.set_config     = drm_atomic_helper_set_config,
	.page_flip      = drm_atomic_helper_page_flip,
	.reset		= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_crtc_helper_funcs tilcdc_crtc_helper_funcs = {
		.mode_fixup     = tilcdc_crtc_mode_fixup,
		.enable		= tilcdc_crtc_enable,
		.disable	= tilcdc_crtc_disable,
		.atomic_check	= tilcdc_crtc_atomic_check,
		.mode_set_nofb	= tilcdc_crtc_mode_set_nofb,
};

int tilcdc_crtc_max_width(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct tilcdc_drm_private *priv = dev->dev_private;
	int max_width = 0;

	if (priv->rev == 1)
		max_width = 1024;
	else if (priv->rev == 2)
		max_width = 2048;

	return max_width;
}

int tilcdc_crtc_mode_valid(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct tilcdc_drm_private *priv = crtc->dev->dev_private;
	unsigned int bandwidth;
	uint32_t hbp, hfp, hsw, vbp, vfp, vsw;

	/*
	 * check to see if the width is within the range that
	 * the LCD Controller physically supports
	 */
	if (mode->hdisplay > tilcdc_crtc_max_width(crtc))
		return MODE_VIRTUAL_X;

	/* width must be multiple of 16 */
	if (mode->hdisplay & 0xf)
		return MODE_VIRTUAL_X;

	if (mode->vdisplay > 2048)
		return MODE_VIRTUAL_Y;

	DBG("Processing mode %dx%d@%d with pixel clock %d",
		mode->hdisplay, mode->vdisplay,
		drm_mode_vrefresh(mode), mode->clock);

	hbp = mode->htotal - mode->hsync_end;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;

	if ((hbp-1) & ~0x3ff) {
		DBG("Pruning mode: Horizontal Back Porch out of range");
		return MODE_HBLANK_WIDE;
	}

	if ((hfp-1) & ~0x3ff) {
		DBG("Pruning mode: Horizontal Front Porch out of range");
		return MODE_HBLANK_WIDE;
	}

	if ((hsw-1) & ~0x3ff) {
		DBG("Pruning mode: Horizontal Sync Width out of range");
		return MODE_HSYNC_WIDE;
	}

	if (vbp & ~0xff) {
		DBG("Pruning mode: Vertical Back Porch out of range");
		return MODE_VBLANK_WIDE;
	}

	if (vfp & ~0xff) {
		DBG("Pruning mode: Vertical Front Porch out of range");
		return MODE_VBLANK_WIDE;
	}

	if ((vsw-1) & ~0x3f) {
		DBG("Pruning mode: Vertical Sync Width out of range");
		return MODE_VSYNC_WIDE;
	}

	/*
	 * some devices have a maximum allowed pixel clock
	 * configured from the DT
	 */
	if (mode->clock > priv->max_pixelclock) {
		DBG("Pruning mode: pixel clock too high");
		return MODE_CLOCK_HIGH;
	}

	/*
	 * some devices further limit the max horizontal resolution
	 * configured from the DT
	 */
	if (mode->hdisplay > priv->max_width)
		return MODE_BAD_WIDTH;

	/* filter out modes that would require too much memory bandwidth: */
	bandwidth = mode->hdisplay * mode->vdisplay *
		drm_mode_vrefresh(mode);
	if (bandwidth > priv->max_bandwidth) {
		DBG("Pruning mode: exceeds defined bandwidth limit");
		return MODE_BAD;
	}

	return MODE_OK;
}

void tilcdc_crtc_set_panel_info(struct drm_crtc *crtc,
		const struct tilcdc_panel_info *info)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);
	tilcdc_crtc->info = info;
}

void tilcdc_crtc_set_simulate_vesa_sync(struct drm_crtc *crtc,
					bool simulate_vesa_sync)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);

	tilcdc_crtc->simulate_vesa_sync = simulate_vesa_sync;
}

void tilcdc_crtc_update_clk(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct tilcdc_drm_private *priv = dev->dev_private;
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);

	drm_modeset_lock_crtc(crtc, NULL);
	if (tilcdc_crtc->lcd_fck_rate != clk_get_rate(priv->clk)) {
		if (tilcdc_crtc_is_on(crtc)) {
			pm_runtime_get_sync(dev->dev);
			tilcdc_crtc_disable(crtc);

			tilcdc_crtc_set_clk(crtc);

			tilcdc_crtc_enable(crtc);
			pm_runtime_put_sync(dev->dev);
		}
	}
	drm_modeset_unlock_crtc(crtc);
}

#define SYNC_LOST_COUNT_LIMIT 50

irqreturn_t tilcdc_crtc_irq(struct drm_crtc *crtc)
{
	struct tilcdc_crtc *tilcdc_crtc = to_tilcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct tilcdc_drm_private *priv = dev->dev_private;
	uint32_t stat;

	stat = tilcdc_read_irqstatus(dev);
	tilcdc_clear_irqstatus(dev, stat);

	if (stat & LCDC_END_OF_FRAME0) {
		unsigned long flags;
		bool skip_event = false;
		ktime_t now;

		now = ktime_get();

		drm_flip_work_commit(&tilcdc_crtc->unref_work, priv->wq);

		spin_lock_irqsave(&tilcdc_crtc->irq_lock, flags);

		tilcdc_crtc->last_vblank = now;

		if (tilcdc_crtc->next_fb) {
			set_scanout(crtc, tilcdc_crtc->next_fb);
			tilcdc_crtc->next_fb = NULL;
			skip_event = true;
		}

		spin_unlock_irqrestore(&tilcdc_crtc->irq_lock, flags);

		drm_crtc_handle_vblank(crtc);

		if (!skip_event) {
			struct drm_pending_vblank_event *event;

			spin_lock_irqsave(&dev->event_lock, flags);

			event = tilcdc_crtc->event;
			tilcdc_crtc->event = NULL;
			if (event)
				drm_crtc_send_vblank_event(crtc, event);

			spin_unlock_irqrestore(&dev->event_lock, flags);
		}

		if (tilcdc_crtc->frame_intact)
			tilcdc_crtc->sync_lost_count = 0;
		else
			tilcdc_crtc->frame_intact = true;
	}

	if (stat & LCDC_FIFO_UNDERFLOW)
		dev_err_ratelimited(dev->dev, "%s(0x%08x): FIFO underfow",
				    __func__, stat);

	/* For revision 2 only */
	if (priv->rev == 2) {
		if (stat & LCDC_FRAME_DONE) {
			tilcdc_crtc->frame_done = true;
			wake_up(&tilcdc_crtc->frame_done_wq);
		}

		if (stat & LCDC_SYNC_LOST) {
			dev_err_ratelimited(dev->dev, "%s(0x%08x): Sync lost",
					    __func__, stat);
			tilcdc_crtc->frame_intact = false;
			if (tilcdc_crtc->sync_lost_count++ >
			    SYNC_LOST_COUNT_LIMIT) {
				dev_err(dev->dev, "%s(0x%08x): Sync lost flood detected, disabling the interrupt", __func__, stat);
				tilcdc_write(dev, LCDC_INT_ENABLE_CLR_REG,
					     LCDC_SYNC_LOST);
			}
		}

		/* Indicate to LCDC that the interrupt service routine has
		 * completed, see 13.3.6.1.6 in AM335x TRM.
		 */
		tilcdc_write(dev, LCDC_END_OF_INT_IND_REG, 0);
	}

	return IRQ_HANDLED;
}

struct drm_crtc *tilcdc_crtc_create(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = dev->dev_private;
	struct tilcdc_crtc *tilcdc_crtc;
	struct drm_crtc *crtc;
	int ret;

	tilcdc_crtc = devm_kzalloc(dev->dev, sizeof(*tilcdc_crtc), GFP_KERNEL);
	if (!tilcdc_crtc) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	crtc = &tilcdc_crtc->base;

	ret = tilcdc_plane_init(dev, &tilcdc_crtc->primary);
	if (ret < 0)
		goto fail;

	init_waitqueue_head(&tilcdc_crtc->frame_done_wq);

	drm_flip_work_init(&tilcdc_crtc->unref_work,
			"unref", unref_worker);

	spin_lock_init(&tilcdc_crtc->irq_lock);

	ret = drm_crtc_init_with_planes(dev, crtc,
					&tilcdc_crtc->primary,
					NULL,
					&tilcdc_crtc_funcs,
					"tilcdc crtc");
	if (ret < 0)
		goto fail;

	drm_crtc_helper_add(crtc, &tilcdc_crtc_helper_funcs);

	if (priv->is_componentized) {
		struct device_node *ports =
			of_get_child_by_name(dev->dev->of_node, "ports");

		if (ports) {
			crtc->port = of_get_child_by_name(ports, "port");
			of_node_put(ports);
		} else {
			crtc->port =
				of_get_child_by_name(dev->dev->of_node, "port");
		}
		if (!crtc->port) { /* This should never happen */
			dev_err(dev->dev, "Port node not found in %s\n",
				dev->dev->of_node->full_name);
			goto fail;
		}
	}

	return crtc;

fail:
	tilcdc_crtc_destroy(crtc);
	return NULL;
}
