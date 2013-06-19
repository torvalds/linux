/*
 * rcar_du_crtc.c  --  R-Car Display Unit CRTCs
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/mutex.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "rcar_du_crtc.h"
#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_lvds.h"
#include "rcar_du_plane.h"
#include "rcar_du_regs.h"
#include "rcar_du_vga.h"

#define to_rcar_crtc(c)	container_of(c, struct rcar_du_crtc, crtc)

static u32 rcar_du_crtc_read(struct rcar_du_crtc *rcrtc, u32 reg)
{
	struct rcar_du_device *rcdu = rcrtc->crtc.dev->dev_private;

	return rcar_du_read(rcdu, rcrtc->mmio_offset + reg);
}

static void rcar_du_crtc_write(struct rcar_du_crtc *rcrtc, u32 reg, u32 data)
{
	struct rcar_du_device *rcdu = rcrtc->crtc.dev->dev_private;

	rcar_du_write(rcdu, rcrtc->mmio_offset + reg, data);
}

static void rcar_du_crtc_clr(struct rcar_du_crtc *rcrtc, u32 reg, u32 clr)
{
	struct rcar_du_device *rcdu = rcrtc->crtc.dev->dev_private;

	rcar_du_write(rcdu, rcrtc->mmio_offset + reg,
		      rcar_du_read(rcdu, rcrtc->mmio_offset + reg) & ~clr);
}

static void rcar_du_crtc_set(struct rcar_du_crtc *rcrtc, u32 reg, u32 set)
{
	struct rcar_du_device *rcdu = rcrtc->crtc.dev->dev_private;

	rcar_du_write(rcdu, rcrtc->mmio_offset + reg,
		      rcar_du_read(rcdu, rcrtc->mmio_offset + reg) | set);
}

static void rcar_du_crtc_clr_set(struct rcar_du_crtc *rcrtc, u32 reg,
				 u32 clr, u32 set)
{
	struct rcar_du_device *rcdu = rcrtc->crtc.dev->dev_private;
	u32 value = rcar_du_read(rcdu, rcrtc->mmio_offset + reg);

	rcar_du_write(rcdu, rcrtc->mmio_offset + reg, (value & ~clr) | set);
}

static void rcar_du_crtc_set_display_timing(struct rcar_du_crtc *rcrtc)
{
	struct drm_crtc *crtc = &rcrtc->crtc;
	struct rcar_du_device *rcdu = crtc->dev->dev_private;
	const struct drm_display_mode *mode = &crtc->mode;
	unsigned long clk;
	u32 value;
	u32 div;

	/* Dot clock */
	clk = clk_get_rate(rcdu->clock);
	div = DIV_ROUND_CLOSEST(clk, mode->clock * 1000);
	div = clamp(div, 1U, 64U) - 1;

	rcar_du_write(rcdu, rcrtc->index ? ESCR2 : ESCR,
		      ESCR_DCLKSEL_CLKS | div);
	rcar_du_write(rcdu, rcrtc->index ? OTAR2 : OTAR, 0);

	/* Signal polarities */
	value = ((mode->flags & DRM_MODE_FLAG_PVSYNC) ? 0 : DSMR_VSL)
	      | ((mode->flags & DRM_MODE_FLAG_PHSYNC) ? 0 : DSMR_HSL)
	      | DSMR_DIPM_DE;
	rcar_du_crtc_write(rcrtc, DSMR, value);

	/* Display timings */
	rcar_du_crtc_write(rcrtc, HDSR, mode->htotal - mode->hsync_start - 19);
	rcar_du_crtc_write(rcrtc, HDER, mode->htotal - mode->hsync_start +
					mode->hdisplay - 19);
	rcar_du_crtc_write(rcrtc, HSWR, mode->hsync_end -
					mode->hsync_start - 1);
	rcar_du_crtc_write(rcrtc, HCR,  mode->htotal - 1);

	rcar_du_crtc_write(rcrtc, VDSR, mode->vtotal - mode->vsync_end - 2);
	rcar_du_crtc_write(rcrtc, VDER, mode->vtotal - mode->vsync_end +
					mode->vdisplay - 2);
	rcar_du_crtc_write(rcrtc, VSPR, mode->vtotal - mode->vsync_end +
					mode->vsync_start - 1);
	rcar_du_crtc_write(rcrtc, VCR,  mode->vtotal - 1);

	rcar_du_crtc_write(rcrtc, DESR,  mode->htotal - mode->hsync_start);
	rcar_du_crtc_write(rcrtc, DEWR,  mode->hdisplay);
}

static void rcar_du_crtc_set_routing(struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = rcrtc->crtc.dev->dev_private;
	u32 dorcr = rcar_du_read(rcdu, DORCR);

	dorcr &= ~(DORCR_PG2T | DORCR_DK2S | DORCR_PG2D_MASK);

	/* Set the DU1 pins sources. Select CRTC 0 if explicitly requested and
	 * CRTC 1 in all other cases to avoid cloning CRTC 0 to DU0 and DU1 by
	 * default.
	 */
	if (rcrtc->outputs & (1 << 1) && rcrtc->index == 0)
		dorcr |= DORCR_PG2D_DS1;
	else
		dorcr |= DORCR_PG2T | DORCR_DK2S | DORCR_PG2D_DS2;

	rcar_du_write(rcdu, DORCR, dorcr);
}

static void __rcar_du_start_stop(struct rcar_du_device *rcdu, bool start)
{
	rcar_du_write(rcdu, DSYSR,
		      (rcar_du_read(rcdu, DSYSR) & ~(DSYSR_DRES | DSYSR_DEN)) |
		      (start ? DSYSR_DEN : DSYSR_DRES));
}

static void rcar_du_start_stop(struct rcar_du_device *rcdu, bool start)
{
	/* Many of the configuration bits are only updated when the display
	 * reset (DRES) bit in DSYSR is set to 1, disabling *both* CRTCs. Some
	 * of those bits could be pre-configured, but others (especially the
	 * bits related to plane assignment to display timing controllers) need
	 * to be modified at runtime.
	 *
	 * Restart the display controller if a start is requested. Sorry for the
	 * flicker. It should be possible to move most of the "DRES-update" bits
	 * setup to driver initialization time and minimize the number of cases
	 * when the display controller will have to be restarted.
	 */
	if (start) {
		if (rcdu->used_crtcs++ != 0)
			__rcar_du_start_stop(rcdu, false);
		__rcar_du_start_stop(rcdu, true);
	} else {
		if (--rcdu->used_crtcs == 0)
			__rcar_du_start_stop(rcdu, false);
	}
}

void rcar_du_crtc_route_output(struct drm_crtc *crtc, unsigned int output)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	/* Store the route from the CRTC output to the DU output. The DU will be
	 * configured when starting the CRTC.
	 */
	rcrtc->outputs |= 1 << output;
}

void rcar_du_crtc_update_planes(struct drm_crtc *crtc)
{
	struct rcar_du_device *rcdu = crtc->dev->dev_private;
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);
	struct rcar_du_plane *planes[RCAR_DU_NUM_HW_PLANES];
	unsigned int num_planes = 0;
	unsigned int prio = 0;
	unsigned int i;
	u32 dptsr = 0;
	u32 dspr = 0;

	for (i = 0; i < ARRAY_SIZE(rcdu->planes.planes); ++i) {
		struct rcar_du_plane *plane = &rcdu->planes.planes[i];
		unsigned int j;

		if (plane->crtc != &rcrtc->crtc || !plane->enabled)
			continue;

		/* Insert the plane in the sorted planes array. */
		for (j = num_planes++; j > 0; --j) {
			if (planes[j-1]->zpos <= plane->zpos)
				break;
			planes[j] = planes[j-1];
		}

		planes[j] = plane;
		prio += plane->format->planes * 4;
	}

	for (i = 0; i < num_planes; ++i) {
		struct rcar_du_plane *plane = planes[i];
		unsigned int index = plane->hwindex;

		prio -= 4;
		dspr |= (index + 1) << prio;
		dptsr |= DPTSR_PnDK(index) |  DPTSR_PnTS(index);

		if (plane->format->planes == 2) {
			index = (index + 1) % 8;

			prio -= 4;
			dspr |= (index + 1) << prio;
			dptsr |= DPTSR_PnDK(index) |  DPTSR_PnTS(index);
		}
	}

	/* Select display timing and dot clock generator 2 for planes associated
	 * with superposition controller 2.
	 */
	if (rcrtc->index) {
		u32 value = rcar_du_read(rcdu, DPTSR);

		/* The DPTSR register is updated when the display controller is
		 * stopped. We thus need to restart the DU. Once again, sorry
		 * for the flicker. One way to mitigate the issue would be to
		 * pre-associate planes with CRTCs (either with a fixed 4/4
		 * split, or through a module parameter). Flicker would then
		 * occur only if we need to break the pre-association.
		 */
		if (value != dptsr) {
			rcar_du_write(rcdu, DPTSR, dptsr);
			if (rcdu->used_crtcs) {
				__rcar_du_start_stop(rcdu, false);
				__rcar_du_start_stop(rcdu, true);
			}
		}
	}

	rcar_du_write(rcdu, rcrtc->index ? DS2PR : DS1PR, dspr);
}

static void rcar_du_crtc_start(struct rcar_du_crtc *rcrtc)
{
	struct drm_crtc *crtc = &rcrtc->crtc;
	struct rcar_du_device *rcdu = crtc->dev->dev_private;
	unsigned int i;

	if (rcrtc->started)
		return;

	if (WARN_ON(rcrtc->plane->format == NULL))
		return;

	/* Set display off and background to black */
	rcar_du_crtc_write(rcrtc, DOOR, DOOR_RGB(0, 0, 0));
	rcar_du_crtc_write(rcrtc, BPOR, BPOR_RGB(0, 0, 0));

	/* Configure display timings and output routing */
	rcar_du_crtc_set_display_timing(rcrtc);
	rcar_du_crtc_set_routing(rcrtc);

	mutex_lock(&rcdu->planes.lock);
	rcrtc->plane->enabled = true;
	rcar_du_crtc_update_planes(crtc);
	mutex_unlock(&rcdu->planes.lock);

	/* Setup planes. */
	for (i = 0; i < ARRAY_SIZE(rcdu->planes.planes); ++i) {
		struct rcar_du_plane *plane = &rcdu->planes.planes[i];

		if (plane->crtc != crtc || !plane->enabled)
			continue;

		rcar_du_plane_setup(plane);
	}

	/* Select master sync mode. This enables display operation in master
	 * sync mode (with the HSYNC and VSYNC signals configured as outputs and
	 * actively driven).
	 */
	rcar_du_crtc_clr_set(rcrtc, DSYSR, DSYSR_TVM_MASK, DSYSR_TVM_MASTER);

	rcar_du_start_stop(rcdu, true);

	rcrtc->started = true;
}

static void rcar_du_crtc_stop(struct rcar_du_crtc *rcrtc)
{
	struct drm_crtc *crtc = &rcrtc->crtc;
	struct rcar_du_device *rcdu = crtc->dev->dev_private;

	if (!rcrtc->started)
		return;

	mutex_lock(&rcdu->planes.lock);
	rcrtc->plane->enabled = false;
	rcar_du_crtc_update_planes(crtc);
	mutex_unlock(&rcdu->planes.lock);

	/* Select switch sync mode. This stops display operation and configures
	 * the HSYNC and VSYNC signals as inputs.
	 */
	rcar_du_crtc_clr_set(rcrtc, DSYSR, DSYSR_TVM_MASK, DSYSR_TVM_SWITCH);

	rcar_du_start_stop(rcdu, false);

	rcrtc->started = false;
}

void rcar_du_crtc_suspend(struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = rcrtc->crtc.dev->dev_private;

	rcar_du_crtc_stop(rcrtc);
	rcar_du_put(rcdu);
}

void rcar_du_crtc_resume(struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = rcrtc->crtc.dev->dev_private;

	if (rcrtc->dpms != DRM_MODE_DPMS_ON)
		return;

	rcar_du_get(rcdu);
	rcar_du_crtc_start(rcrtc);
}

static void rcar_du_crtc_update_base(struct rcar_du_crtc *rcrtc)
{
	struct drm_crtc *crtc = &rcrtc->crtc;

	rcar_du_plane_compute_base(rcrtc->plane, crtc->fb);
	rcar_du_plane_update_base(rcrtc->plane);
}

static void rcar_du_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct rcar_du_device *rcdu = crtc->dev->dev_private;
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	if (rcrtc->dpms == mode)
		return;

	if (mode == DRM_MODE_DPMS_ON) {
		rcar_du_get(rcdu);
		rcar_du_crtc_start(rcrtc);
	} else {
		rcar_du_crtc_stop(rcrtc);
		rcar_du_put(rcdu);
	}

	rcrtc->dpms = mode;
}

static bool rcar_du_crtc_mode_fixup(struct drm_crtc *crtc,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{
	/* TODO Fixup modes */
	return true;
}

static void rcar_du_crtc_mode_prepare(struct drm_crtc *crtc)
{
	struct rcar_du_device *rcdu = crtc->dev->dev_private;
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	/* We need to access the hardware during mode set, acquire a reference
	 * to the DU.
	 */
	rcar_du_get(rcdu);

	/* Stop the CRTC and release the plane. Force the DPMS mode to off as a
	 * result.
	 */
	rcar_du_crtc_stop(rcrtc);
	rcar_du_plane_release(rcrtc->plane);

	rcrtc->dpms = DRM_MODE_DPMS_OFF;
}

static int rcar_du_crtc_mode_set(struct drm_crtc *crtc,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode,
				 int x, int y,
				 struct drm_framebuffer *old_fb)
{
	struct rcar_du_device *rcdu = crtc->dev->dev_private;
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);
	const struct rcar_du_format_info *format;
	int ret;

	format = rcar_du_format_info(crtc->fb->pixel_format);
	if (format == NULL) {
		dev_dbg(rcdu->dev, "mode_set: unsupported format %08x\n",
			crtc->fb->pixel_format);
		ret = -EINVAL;
		goto error;
	}

	ret = rcar_du_plane_reserve(rcrtc->plane, format);
	if (ret < 0)
		goto error;

	rcrtc->plane->format = format;
	rcrtc->plane->pitch = crtc->fb->pitches[0];

	rcrtc->plane->src_x = x;
	rcrtc->plane->src_y = y;
	rcrtc->plane->width = mode->hdisplay;
	rcrtc->plane->height = mode->vdisplay;

	rcar_du_plane_compute_base(rcrtc->plane, crtc->fb);

	rcrtc->outputs = 0;

	return 0;

error:
	/* There's no rollback/abort operation to clean up in case of error. We
	 * thus need to release the reference to the DU acquired in prepare()
	 * here.
	 */
	rcar_du_put(rcdu);
	return ret;
}

static void rcar_du_crtc_mode_commit(struct drm_crtc *crtc)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	/* We're done, restart the CRTC and set the DPMS mode to on. The
	 * reference to the DU acquired at prepare() time will thus be released
	 * by the DPMS handler (possibly called by the disable() handler).
	 */
	rcar_du_crtc_start(rcrtc);
	rcrtc->dpms = DRM_MODE_DPMS_ON;
}

static int rcar_du_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
				      struct drm_framebuffer *old_fb)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	rcrtc->plane->src_x = x;
	rcrtc->plane->src_y = y;

	rcar_du_crtc_update_base(to_rcar_crtc(crtc));

	return 0;
}

static void rcar_du_crtc_disable(struct drm_crtc *crtc)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	rcar_du_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
	rcar_du_plane_release(rcrtc->plane);
}

static const struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.dpms = rcar_du_crtc_dpms,
	.mode_fixup = rcar_du_crtc_mode_fixup,
	.prepare = rcar_du_crtc_mode_prepare,
	.commit = rcar_du_crtc_mode_commit,
	.mode_set = rcar_du_crtc_mode_set,
	.mode_set_base = rcar_du_crtc_mode_set_base,
	.disable = rcar_du_crtc_disable,
};

void rcar_du_crtc_cancel_page_flip(struct rcar_du_crtc *rcrtc,
				   struct drm_file *file)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	/* Destroy the pending vertical blanking event associated with the
	 * pending page flip, if any, and disable vertical blanking interrupts.
	 */
	spin_lock_irqsave(&dev->event_lock, flags);
	event = rcrtc->event;
	if (event && event->base.file_priv == file) {
		rcrtc->event = NULL;
		event->base.destroy(&event->base);
		drm_vblank_put(dev, rcrtc->index);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void rcar_du_crtc_finish_page_flip(struct rcar_du_crtc *rcrtc)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = rcrtc->event;
	rcrtc->event = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (event == NULL)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	drm_send_vblank_event(dev, rcrtc->index, event);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	drm_vblank_put(dev, rcrtc->index);
}

static int rcar_du_crtc_page_flip(struct drm_crtc *crtc,
				  struct drm_framebuffer *fb,
				  struct drm_pending_vblank_event *event)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (rcrtc->event != NULL) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	crtc->fb = fb;
	rcar_du_crtc_update_base(rcrtc);

	if (event) {
		event->pipe = rcrtc->index;
		drm_vblank_get(dev, rcrtc->index);
		spin_lock_irqsave(&dev->event_lock, flags);
		rcrtc->event = event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	return 0;
}

static const struct drm_crtc_funcs crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_crtc_helper_set_config,
	.page_flip = rcar_du_crtc_page_flip,
};

int rcar_du_crtc_create(struct rcar_du_device *rcdu, unsigned int index)
{
	struct rcar_du_crtc *rcrtc = &rcdu->crtcs[index];
	struct drm_crtc *crtc = &rcrtc->crtc;
	int ret;

	rcrtc->mmio_offset = index ? DISP2_REG_OFFSET : 0;
	rcrtc->index = index;
	rcrtc->dpms = DRM_MODE_DPMS_OFF;
	rcrtc->plane = &rcdu->planes.planes[index];

	rcrtc->plane->crtc = crtc;

	ret = drm_crtc_init(rcdu->ddev, crtc, &crtc_funcs);
	if (ret < 0)
		return ret;

	drm_crtc_helper_add(crtc, &crtc_helper_funcs);

	return 0;
}

void rcar_du_crtc_enable_vblank(struct rcar_du_crtc *rcrtc, bool enable)
{
	if (enable) {
		rcar_du_crtc_write(rcrtc, DSRCR, DSRCR_VBCL);
		rcar_du_crtc_set(rcrtc, DIER, DIER_VBE);
	} else {
		rcar_du_crtc_clr(rcrtc, DIER, DIER_VBE);
	}
}

void rcar_du_crtc_irq(struct rcar_du_crtc *rcrtc)
{
	u32 status;

	status = rcar_du_crtc_read(rcrtc, DSSR);
	rcar_du_crtc_write(rcrtc, DSRCR, status & DSRCR_MASK);

	if (status & DSSR_VBK) {
		drm_handle_vblank(rcrtc->crtc.dev, rcrtc->index);
		rcar_du_crtc_finish_page_flip(rcrtc);
	}
}
