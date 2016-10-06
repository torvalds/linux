/*
 * shmob_drm_crtc.c  --  SH Mobile DRM CRTCs
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/backlight.h>
#include <linux/clk.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include <video/sh_mobile_meram.h>

#include "shmob_drm_backlight.h"
#include "shmob_drm_crtc.h"
#include "shmob_drm_drv.h"
#include "shmob_drm_kms.h"
#include "shmob_drm_plane.h"
#include "shmob_drm_regs.h"

/*
 * TODO: panel support
 */

/* -----------------------------------------------------------------------------
 * Clock management
 */

static int shmob_drm_clk_on(struct shmob_drm_device *sdev)
{
	int ret;

	if (sdev->clock) {
		ret = clk_prepare_enable(sdev->clock);
		if (ret < 0)
			return ret;
	}
#if 0
	if (sdev->meram_dev && sdev->meram_dev->pdev)
		pm_runtime_get_sync(&sdev->meram_dev->pdev->dev);
#endif

	return 0;
}

static void shmob_drm_clk_off(struct shmob_drm_device *sdev)
{
#if 0
	if (sdev->meram_dev && sdev->meram_dev->pdev)
		pm_runtime_put_sync(&sdev->meram_dev->pdev->dev);
#endif
	if (sdev->clock)
		clk_disable_unprepare(sdev->clock);
}

/* -----------------------------------------------------------------------------
 * CRTC
 */

static void shmob_drm_crtc_setup_geometry(struct shmob_drm_crtc *scrtc)
{
	struct drm_crtc *crtc = &scrtc->crtc;
	struct shmob_drm_device *sdev = crtc->dev->dev_private;
	const struct shmob_drm_interface_data *idata = &sdev->pdata->iface;
	const struct drm_display_mode *mode = &crtc->mode;
	u32 value;

	value = sdev->ldmt1r
	      | ((mode->flags & DRM_MODE_FLAG_PVSYNC) ? 0 : LDMT1R_VPOL)
	      | ((mode->flags & DRM_MODE_FLAG_PHSYNC) ? 0 : LDMT1R_HPOL)
	      | ((idata->flags & SHMOB_DRM_IFACE_FL_DWPOL) ? LDMT1R_DWPOL : 0)
	      | ((idata->flags & SHMOB_DRM_IFACE_FL_DIPOL) ? LDMT1R_DIPOL : 0)
	      | ((idata->flags & SHMOB_DRM_IFACE_FL_DAPOL) ? LDMT1R_DAPOL : 0)
	      | ((idata->flags & SHMOB_DRM_IFACE_FL_HSCNT) ? LDMT1R_HSCNT : 0)
	      | ((idata->flags & SHMOB_DRM_IFACE_FL_DWCNT) ? LDMT1R_DWCNT : 0);
	lcdc_write(sdev, LDMT1R, value);

	if (idata->interface >= SHMOB_DRM_IFACE_SYS8A &&
	    idata->interface <= SHMOB_DRM_IFACE_SYS24) {
		/* Setup SYS bus. */
		value = (idata->sys.cs_setup << LDMT2R_CSUP_SHIFT)
		      | (idata->sys.vsync_active_high ? LDMT2R_RSV : 0)
		      | (idata->sys.vsync_dir_input ? LDMT2R_VSEL : 0)
		      | (idata->sys.write_setup << LDMT2R_WCSC_SHIFT)
		      | (idata->sys.write_cycle << LDMT2R_WCEC_SHIFT)
		      | (idata->sys.write_strobe << LDMT2R_WCLW_SHIFT);
		lcdc_write(sdev, LDMT2R, value);

		value = (idata->sys.read_latch << LDMT3R_RDLC_SHIFT)
		      | (idata->sys.read_setup << LDMT3R_RCSC_SHIFT)
		      | (idata->sys.read_cycle << LDMT3R_RCEC_SHIFT)
		      | (idata->sys.read_strobe << LDMT3R_RCLW_SHIFT);
		lcdc_write(sdev, LDMT3R, value);
	}

	value = ((mode->hdisplay / 8) << 16)			/* HDCN */
	      | (mode->htotal / 8);				/* HTCN */
	lcdc_write(sdev, LDHCNR, value);

	value = (((mode->hsync_end - mode->hsync_start) / 8) << 16) /* HSYNW */
	      | (mode->hsync_start / 8);			/* HSYNP */
	lcdc_write(sdev, LDHSYNR, value);

	value = ((mode->hdisplay & 7) << 24) | ((mode->htotal & 7) << 16)
	      | (((mode->hsync_end - mode->hsync_start) & 7) << 8)
	      | (mode->hsync_start & 7);
	lcdc_write(sdev, LDHAJR, value);

	value = ((mode->vdisplay) << 16)			/* VDLN */
	      | mode->vtotal;					/* VTLN */
	lcdc_write(sdev, LDVLNR, value);

	value = ((mode->vsync_end - mode->vsync_start) << 16)	/* VSYNW */
	      | mode->vsync_start;				/* VSYNP */
	lcdc_write(sdev, LDVSYNR, value);
}

static void shmob_drm_crtc_start_stop(struct shmob_drm_crtc *scrtc, bool start)
{
	struct shmob_drm_device *sdev = scrtc->crtc.dev->dev_private;
	u32 value;

	value = lcdc_read(sdev, LDCNT2R);
	if (start)
		lcdc_write(sdev, LDCNT2R, value | LDCNT2R_DO);
	else
		lcdc_write(sdev, LDCNT2R, value & ~LDCNT2R_DO);

	/* Wait until power is applied/stopped. */
	while (1) {
		value = lcdc_read(sdev, LDPMR) & LDPMR_LPS;
		if ((start && value) || (!start && !value))
			break;

		cpu_relax();
	}

	if (!start) {
		/* Stop the dot clock. */
		lcdc_write(sdev, LDDCKSTPR, LDDCKSTPR_DCKSTP);
	}
}

/*
 * shmob_drm_crtc_start - Configure and start the LCDC
 * @scrtc: the SH Mobile CRTC
 *
 * Configure and start the LCDC device. External devices (clocks, MERAM, panels,
 * ...) are not touched by this function.
 */
static void shmob_drm_crtc_start(struct shmob_drm_crtc *scrtc)
{
	struct drm_crtc *crtc = &scrtc->crtc;
	struct shmob_drm_device *sdev = crtc->dev->dev_private;
	const struct shmob_drm_interface_data *idata = &sdev->pdata->iface;
	const struct shmob_drm_format_info *format;
	struct drm_device *dev = sdev->ddev;
	struct drm_plane *plane;
	u32 value;
	int ret;

	if (scrtc->started)
		return;

	format = shmob_drm_format_info(crtc->primary->fb->pixel_format);
	if (WARN_ON(format == NULL))
		return;

	/* Enable clocks before accessing the hardware. */
	ret = shmob_drm_clk_on(sdev);
	if (ret < 0)
		return;

	/* Reset and enable the LCDC. */
	lcdc_write(sdev, LDCNT2R, lcdc_read(sdev, LDCNT2R) | LDCNT2R_BR);
	lcdc_wait_bit(sdev, LDCNT2R, LDCNT2R_BR, 0);
	lcdc_write(sdev, LDCNT2R, LDCNT2R_ME);

	/* Stop the LCDC first and disable all interrupts. */
	shmob_drm_crtc_start_stop(scrtc, false);
	lcdc_write(sdev, LDINTR, 0);

	/* Configure power supply, dot clocks and start them. */
	lcdc_write(sdev, LDPMR, 0);

	value = sdev->lddckr;
	if (idata->clk_div) {
		/* FIXME: sh7724 can only use 42, 48, 54 and 60 for the divider
		 * denominator.
		 */
		lcdc_write(sdev, LDDCKPAT1R, 0);
		lcdc_write(sdev, LDDCKPAT2R, (1 << (idata->clk_div / 2)) - 1);

		if (idata->clk_div == 1)
			value |= LDDCKR_MOSEL;
		else
			value |= idata->clk_div;
	}

	lcdc_write(sdev, LDDCKR, value);
	lcdc_write(sdev, LDDCKSTPR, 0);
	lcdc_wait_bit(sdev, LDDCKSTPR, ~0, 0);

	/* TODO: Setup SYS panel */

	/* Setup geometry, format, frame buffer memory and operation mode. */
	shmob_drm_crtc_setup_geometry(scrtc);

	/* TODO: Handle YUV colorspaces. Hardcode REC709 for now. */
	lcdc_write(sdev, LDDFR, format->lddfr | LDDFR_CF1);
	lcdc_write(sdev, LDMLSR, scrtc->line_size);
	lcdc_write(sdev, LDSA1R, scrtc->dma[0]);
	if (format->yuv)
		lcdc_write(sdev, LDSA2R, scrtc->dma[1]);
	lcdc_write(sdev, LDSM1R, 0);

	/* Word and long word swap. */
	switch (format->fourcc) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV42:
		value = LDDDSR_LS | LDDDSR_WS;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV24:
		value = LDDDSR_LS | LDDDSR_WS | LDDDSR_BS;
		break;
	case DRM_FORMAT_ARGB8888:
	default:
		value = LDDDSR_LS;
		break;
	}
	lcdc_write(sdev, LDDDSR, value);

	/* Setup planes. */
	drm_for_each_legacy_plane(plane, dev) {
		if (plane->crtc == crtc)
			shmob_drm_plane_setup(plane);
	}

	/* Enable the display output. */
	lcdc_write(sdev, LDCNT1R, LDCNT1R_DE);

	shmob_drm_crtc_start_stop(scrtc, true);

	scrtc->started = true;
}

static void shmob_drm_crtc_stop(struct shmob_drm_crtc *scrtc)
{
	struct drm_crtc *crtc = &scrtc->crtc;
	struct shmob_drm_device *sdev = crtc->dev->dev_private;

	if (!scrtc->started)
		return;

	/* Disable the MERAM cache. */
	if (scrtc->cache) {
		sh_mobile_meram_cache_free(sdev->meram, scrtc->cache);
		scrtc->cache = NULL;
	}

	/* Stop the LCDC. */
	shmob_drm_crtc_start_stop(scrtc, false);

	/* Disable the display output. */
	lcdc_write(sdev, LDCNT1R, 0);

	/* Stop clocks. */
	shmob_drm_clk_off(sdev);

	scrtc->started = false;
}

void shmob_drm_crtc_suspend(struct shmob_drm_crtc *scrtc)
{
	shmob_drm_crtc_stop(scrtc);
}

void shmob_drm_crtc_resume(struct shmob_drm_crtc *scrtc)
{
	if (scrtc->dpms != DRM_MODE_DPMS_ON)
		return;

	shmob_drm_crtc_start(scrtc);
}

static void shmob_drm_crtc_compute_base(struct shmob_drm_crtc *scrtc,
					int x, int y)
{
	struct drm_crtc *crtc = &scrtc->crtc;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct shmob_drm_device *sdev = crtc->dev->dev_private;
	struct drm_gem_cma_object *gem;
	unsigned int bpp;

	bpp = scrtc->format->yuv ? 8 : scrtc->format->bpp;
	gem = drm_fb_cma_get_gem_obj(fb, 0);
	scrtc->dma[0] = gem->paddr + fb->offsets[0]
		      + y * fb->pitches[0] + x * bpp / 8;

	if (scrtc->format->yuv) {
		bpp = scrtc->format->bpp - 8;
		gem = drm_fb_cma_get_gem_obj(fb, 1);
		scrtc->dma[1] = gem->paddr + fb->offsets[1]
			      + y / (bpp == 4 ? 2 : 1) * fb->pitches[1]
			      + x * (bpp == 16 ? 2 : 1);
	}

	if (scrtc->cache)
		sh_mobile_meram_cache_update(sdev->meram, scrtc->cache,
					     scrtc->dma[0], scrtc->dma[1],
					     &scrtc->dma[0], &scrtc->dma[1]);
}

static void shmob_drm_crtc_update_base(struct shmob_drm_crtc *scrtc)
{
	struct drm_crtc *crtc = &scrtc->crtc;
	struct shmob_drm_device *sdev = crtc->dev->dev_private;

	shmob_drm_crtc_compute_base(scrtc, crtc->x, crtc->y);

	lcdc_write_mirror(sdev, LDSA1R, scrtc->dma[0]);
	if (scrtc->format->yuv)
		lcdc_write_mirror(sdev, LDSA2R, scrtc->dma[1]);

	lcdc_write(sdev, LDRCNTR, lcdc_read(sdev, LDRCNTR) ^ LDRCNTR_MRS);
}

#define to_shmob_crtc(c)	container_of(c, struct shmob_drm_crtc, crtc)

static void shmob_drm_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct shmob_drm_crtc *scrtc = to_shmob_crtc(crtc);

	if (scrtc->dpms == mode)
		return;

	if (mode == DRM_MODE_DPMS_ON)
		shmob_drm_crtc_start(scrtc);
	else
		shmob_drm_crtc_stop(scrtc);

	scrtc->dpms = mode;
}

static void shmob_drm_crtc_mode_prepare(struct drm_crtc *crtc)
{
	shmob_drm_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static int shmob_drm_crtc_mode_set(struct drm_crtc *crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode,
				   int x, int y,
				   struct drm_framebuffer *old_fb)
{
	struct shmob_drm_crtc *scrtc = to_shmob_crtc(crtc);
	struct shmob_drm_device *sdev = crtc->dev->dev_private;
	const struct sh_mobile_meram_cfg *mdata = sdev->pdata->meram;
	const struct shmob_drm_format_info *format;
	void *cache;

	format = shmob_drm_format_info(crtc->primary->fb->pixel_format);
	if (format == NULL) {
		dev_dbg(sdev->dev, "mode_set: unsupported format %08x\n",
			crtc->primary->fb->pixel_format);
		return -EINVAL;
	}

	scrtc->format = format;
	scrtc->line_size = crtc->primary->fb->pitches[0];

	if (sdev->meram) {
		/* Enable MERAM cache if configured. We need to de-init
		 * configured ICBs before we can re-initialize them.
		 */
		if (scrtc->cache) {
			sh_mobile_meram_cache_free(sdev->meram, scrtc->cache);
			scrtc->cache = NULL;
		}

		cache = sh_mobile_meram_cache_alloc(sdev->meram, mdata,
						    crtc->primary->fb->pitches[0],
						    adjusted_mode->vdisplay,
						    format->meram,
						    &scrtc->line_size);
		if (!IS_ERR(cache))
			scrtc->cache = cache;
	}

	shmob_drm_crtc_compute_base(scrtc, x, y);

	return 0;
}

static void shmob_drm_crtc_mode_commit(struct drm_crtc *crtc)
{
	shmob_drm_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static int shmob_drm_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
					struct drm_framebuffer *old_fb)
{
	shmob_drm_crtc_update_base(to_shmob_crtc(crtc));

	return 0;
}

static const struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.dpms = shmob_drm_crtc_dpms,
	.prepare = shmob_drm_crtc_mode_prepare,
	.commit = shmob_drm_crtc_mode_commit,
	.mode_set = shmob_drm_crtc_mode_set,
	.mode_set_base = shmob_drm_crtc_mode_set_base,
};

void shmob_drm_crtc_finish_page_flip(struct shmob_drm_crtc *scrtc)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = scrtc->crtc.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = scrtc->event;
	scrtc->event = NULL;
	if (event) {
		drm_crtc_send_vblank_event(&scrtc->crtc, event);
		drm_crtc_vblank_put(&scrtc->crtc);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static int shmob_drm_crtc_page_flip(struct drm_crtc *crtc,
				    struct drm_framebuffer *fb,
				    struct drm_pending_vblank_event *event,
				    uint32_t page_flip_flags)
{
	struct shmob_drm_crtc *scrtc = to_shmob_crtc(crtc);
	struct drm_device *dev = scrtc->crtc.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (scrtc->event != NULL) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	crtc->primary->fb = fb;
	shmob_drm_crtc_update_base(scrtc);

	if (event) {
		event->pipe = 0;
		drm_crtc_vblank_get(&scrtc->crtc);
		spin_lock_irqsave(&dev->event_lock, flags);
		scrtc->event = event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	return 0;
}

static const struct drm_crtc_funcs crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_crtc_helper_set_config,
	.page_flip = shmob_drm_crtc_page_flip,
};

int shmob_drm_crtc_create(struct shmob_drm_device *sdev)
{
	struct drm_crtc *crtc = &sdev->crtc.crtc;
	int ret;

	sdev->crtc.dpms = DRM_MODE_DPMS_OFF;

	ret = drm_crtc_init(sdev->ddev, crtc, &crtc_funcs);
	if (ret < 0)
		return ret;

	drm_crtc_helper_add(crtc, &crtc_helper_funcs);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Encoder
 */

#define to_shmob_encoder(e) \
	container_of(e, struct shmob_drm_encoder, encoder)

static void shmob_drm_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct shmob_drm_encoder *senc = to_shmob_encoder(encoder);
	struct shmob_drm_device *sdev = encoder->dev->dev_private;
	struct shmob_drm_connector *scon = &sdev->connector;

	if (senc->dpms == mode)
		return;

	shmob_drm_backlight_dpms(scon, mode);

	senc->dpms = mode;
}

static bool shmob_drm_encoder_mode_fixup(struct drm_encoder *encoder,
					 const struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct shmob_drm_device *sdev = dev->dev_private;
	struct drm_connector *connector = &sdev->connector.connector;
	const struct drm_display_mode *panel_mode;

	if (list_empty(&connector->modes)) {
		dev_dbg(dev->dev, "mode_fixup: empty modes list\n");
		return false;
	}

	/* The flat panel mode is fixed, just copy it to the adjusted mode. */
	panel_mode = list_first_entry(&connector->modes,
				      struct drm_display_mode, head);
	drm_mode_copy(adjusted_mode, panel_mode);

	return true;
}

static void shmob_drm_encoder_mode_prepare(struct drm_encoder *encoder)
{
	/* No-op, everything is handled in the CRTC code. */
}

static void shmob_drm_encoder_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	/* No-op, everything is handled in the CRTC code. */
}

static void shmob_drm_encoder_mode_commit(struct drm_encoder *encoder)
{
	/* No-op, everything is handled in the CRTC code. */
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.dpms = shmob_drm_encoder_dpms,
	.mode_fixup = shmob_drm_encoder_mode_fixup,
	.prepare = shmob_drm_encoder_mode_prepare,
	.commit = shmob_drm_encoder_mode_commit,
	.mode_set = shmob_drm_encoder_mode_set,
};

static void shmob_drm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = shmob_drm_encoder_destroy,
};

int shmob_drm_encoder_create(struct shmob_drm_device *sdev)
{
	struct drm_encoder *encoder = &sdev->encoder.encoder;
	int ret;

	sdev->encoder.dpms = DRM_MODE_DPMS_OFF;

	encoder->possible_crtcs = 1;

	ret = drm_encoder_init(sdev->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_LVDS, NULL);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	return 0;
}

void shmob_drm_crtc_enable_vblank(struct shmob_drm_device *sdev, bool enable)
{
	unsigned long flags;
	u32 ldintr;

	/* Be careful not to acknowledge any pending interrupt. */
	spin_lock_irqsave(&sdev->irq_lock, flags);
	ldintr = lcdc_read(sdev, LDINTR) | LDINTR_STATUS_MASK;
	if (enable)
		ldintr |= LDINTR_VEE;
	else
		ldintr &= ~LDINTR_VEE;
	lcdc_write(sdev, LDINTR, ldintr);
	spin_unlock_irqrestore(&sdev->irq_lock, flags);
}

/* -----------------------------------------------------------------------------
 * Connector
 */

#define to_shmob_connector(c) \
	container_of(c, struct shmob_drm_connector, connector)

static int shmob_drm_connector_get_modes(struct drm_connector *connector)
{
	struct shmob_drm_device *sdev = connector->dev->dev_private;
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (mode == NULL)
		return 0;

	mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;
	mode->clock = sdev->pdata->panel.mode.clock;
	mode->hdisplay = sdev->pdata->panel.mode.hdisplay;
	mode->hsync_start = sdev->pdata->panel.mode.hsync_start;
	mode->hsync_end = sdev->pdata->panel.mode.hsync_end;
	mode->htotal = sdev->pdata->panel.mode.htotal;
	mode->vdisplay = sdev->pdata->panel.mode.vdisplay;
	mode->vsync_start = sdev->pdata->panel.mode.vsync_start;
	mode->vsync_end = sdev->pdata->panel.mode.vsync_end;
	mode->vtotal = sdev->pdata->panel.mode.vtotal;
	mode->flags = sdev->pdata->panel.mode.flags;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = sdev->pdata->panel.width_mm;
	connector->display_info.height_mm = sdev->pdata->panel.height_mm;

	return 1;
}

static struct drm_encoder *
shmob_drm_connector_best_encoder(struct drm_connector *connector)
{
	struct shmob_drm_connector *scon = to_shmob_connector(connector);

	return scon->encoder;
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = shmob_drm_connector_get_modes,
	.best_encoder = shmob_drm_connector_best_encoder,
};

static void shmob_drm_connector_destroy(struct drm_connector *connector)
{
	struct shmob_drm_connector *scon = to_shmob_connector(connector);

	shmob_drm_backlight_exit(scon);
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
shmob_drm_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = shmob_drm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = shmob_drm_connector_destroy,
};

int shmob_drm_connector_create(struct shmob_drm_device *sdev,
			       struct drm_encoder *encoder)
{
	struct drm_connector *connector = &sdev->connector.connector;
	int ret;

	sdev->connector.encoder = encoder;

	connector->display_info.width_mm = sdev->pdata->panel.width_mm;
	connector->display_info.height_mm = sdev->pdata->panel.height_mm;

	ret = drm_connector_init(sdev->ddev, connector, &connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &connector_helper_funcs);
	ret = drm_connector_register(connector);
	if (ret < 0)
		goto err_cleanup;

	ret = shmob_drm_backlight_init(&sdev->connector);
	if (ret < 0)
		goto err_sysfs;

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		goto err_backlight;

	drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
	drm_object_property_set_value(&connector->base,
		sdev->ddev->mode_config.dpms_property, DRM_MODE_DPMS_OFF);

	return 0;

err_backlight:
	shmob_drm_backlight_exit(&sdev->connector);
err_sysfs:
	drm_connector_unregister(connector);
err_cleanup:
	drm_connector_cleanup(connector);
	return ret;
}
