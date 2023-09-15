// SPDX-License-Identifier: GPL-2.0+
/*
 * shmob_drm_crtc.c  --  SH Mobile DRM CRTCs
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/clk.h>
#include <linux/media-bus-format.h>
#include <linux/pm_runtime.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include <video/videomode.h>

#include "shmob_drm_crtc.h"
#include "shmob_drm_drv.h"
#include "shmob_drm_kms.h"
#include "shmob_drm_plane.h"
#include "shmob_drm_regs.h"

/*
 * TODO: panel support
 */

/* -----------------------------------------------------------------------------
 * Page Flip
 */

void shmob_drm_crtc_finish_page_flip(struct shmob_drm_crtc *scrtc)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = scrtc->base.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = scrtc->event;
	scrtc->event = NULL;
	if (event) {
		drm_crtc_send_vblank_event(&scrtc->base, event);
		wake_up(&scrtc->flip_wait);
		drm_crtc_vblank_put(&scrtc->base);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static bool shmob_drm_crtc_page_flip_pending(struct shmob_drm_crtc *scrtc)
{
	struct drm_device *dev = scrtc->base.dev;
	unsigned long flags;
	bool pending;

	spin_lock_irqsave(&dev->event_lock, flags);
	pending = scrtc->event != NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return pending;
}

static void shmob_drm_crtc_wait_page_flip(struct shmob_drm_crtc *scrtc)
{
	struct drm_crtc *crtc = &scrtc->base;
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);

	if (wait_event_timeout(scrtc->flip_wait,
			       !shmob_drm_crtc_page_flip_pending(scrtc),
			       msecs_to_jiffies(50)))
		return;

	dev_warn(sdev->dev, "page flip timeout\n");

	shmob_drm_crtc_finish_page_flip(scrtc);
}

/* -----------------------------------------------------------------------------
 * CRTC
 */

static const struct {
	u32 fmt;
	u32 ldmt1r;
} shmob_drm_bus_fmts[] = {
	{ MEDIA_BUS_FMT_RGB888_3X8,	 LDMT1R_MIFTYP_RGB8 },
	{ MEDIA_BUS_FMT_RGB666_2X9_BE,	 LDMT1R_MIFTYP_RGB9 },
	{ MEDIA_BUS_FMT_RGB888_2X12_BE,	 LDMT1R_MIFTYP_RGB12A },
	{ MEDIA_BUS_FMT_RGB444_1X12,	 LDMT1R_MIFTYP_RGB12B },
	{ MEDIA_BUS_FMT_RGB565_1X16,	 LDMT1R_MIFTYP_RGB16 },
	{ MEDIA_BUS_FMT_RGB666_1X18,	 LDMT1R_MIFTYP_RGB18 },
	{ MEDIA_BUS_FMT_RGB888_1X24,	 LDMT1R_MIFTYP_RGB24 },
	{ MEDIA_BUS_FMT_UYVY8_1X16,	 LDMT1R_MIFTYP_YCBCR },
};

static void shmob_drm_crtc_setup_geometry(struct shmob_drm_crtc *scrtc)
{
	struct drm_crtc *crtc = &scrtc->base;
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);
	const struct drm_display_info *info = &sdev->connector->display_info;
	const struct drm_display_mode *mode = &crtc->mode;
	unsigned int i;
	u32 value;

	if (!info->num_bus_formats || !info->bus_formats) {
		dev_warn(sdev->dev, "No bus format reported, using RGB888\n");
		value = LDMT1R_MIFTYP_RGB24;
	} else {
		for (i = 0; i < ARRAY_SIZE(shmob_drm_bus_fmts); i++) {
			if (shmob_drm_bus_fmts[i].fmt == info->bus_formats[0])
				break;
		}
		if (i < ARRAY_SIZE(shmob_drm_bus_fmts)) {
			value = shmob_drm_bus_fmts[i].ldmt1r;
		} else {
			dev_warn(sdev->dev,
				 "unsupported bus format 0x%x, using RGB888\n",
				 info->bus_formats[0]);
			value = LDMT1R_MIFTYP_RGB24;
		}
	}

	if (info->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE)
		value |= LDMT1R_DWPOL;
	if (info->bus_flags & DRM_BUS_FLAG_DE_LOW)
		value |= LDMT1R_DIPOL;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		value |= LDMT1R_VPOL;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		value |= LDMT1R_HPOL;
	lcdc_write(sdev, LDMT1R, value);

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
	struct shmob_drm_device *sdev = to_shmob_device(scrtc->base.dev);
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
	struct drm_crtc *crtc = &scrtc->base;
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);
	const struct shmob_drm_interface_data *idata = &sdev->pdata->iface;
	const struct shmob_drm_format_info *format;
	struct drm_device *dev = &sdev->ddev;
	struct drm_plane *plane;
	u32 value;
	int ret;

	if (scrtc->started)
		return;

	format = shmob_drm_format_info(crtc->primary->fb->format->format);
	if (WARN_ON(format == NULL))
		return;

	ret = pm_runtime_resume_and_get(sdev->dev);
	if (ret)
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

	/* Setup geometry, format, frame buffer memory and operation mode. */
	shmob_drm_crtc_setup_geometry(scrtc);

	/* TODO: Handle YUV colorspaces. Hardcode REC709 for now. */
	lcdc_write(sdev, LDDFR, format->lddfr | LDDFR_CF1);
	lcdc_write(sdev, LDMLSR, scrtc->line_size);
	lcdc_write(sdev, LDSA1R, scrtc->dma[0]);
	if (shmob_drm_format_is_yuv(format))
		lcdc_write(sdev, LDSA2R, scrtc->dma[1]);
	lcdc_write(sdev, LDSM1R, 0);

	/* Word and long word swap. */
	lcdc_write(sdev, LDDDSR, format->ldddsr);

	/* Setup planes. */
	drm_for_each_legacy_plane(plane, dev) {
		if (plane->crtc == crtc)
			shmob_drm_plane_setup(plane);
	}

	/* Enable the display output. */
	lcdc_write(sdev, LDCNT1R, LDCNT1R_DE);

	shmob_drm_crtc_start_stop(scrtc, true);

	/* Turn vertical blank interrupt reporting back on. */
	drm_crtc_vblank_on(crtc);

	scrtc->started = true;
}

static void shmob_drm_crtc_stop(struct shmob_drm_crtc *scrtc)
{
	struct drm_crtc *crtc = &scrtc->base;
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);

	if (!scrtc->started)
		return;

	/*
	 * Disable vertical blank interrupt reporting.  We first need to wait
	 * for page flip completion before stopping the CRTC as userspace
	 * expects page flips to eventually complete.
	 */
	shmob_drm_crtc_wait_page_flip(scrtc);
	drm_crtc_vblank_off(crtc);

	/* Stop the LCDC. */
	shmob_drm_crtc_start_stop(scrtc, false);

	/* Disable the display output. */
	lcdc_write(sdev, LDCNT1R, 0);

	pm_runtime_put(sdev->dev);

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
	struct drm_crtc *crtc = &scrtc->base;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct drm_gem_dma_object *gem;
	unsigned int bpp;

	bpp = shmob_drm_format_is_yuv(scrtc->format) ? 8 : scrtc->format->bpp;
	gem = drm_fb_dma_get_gem_obj(fb, 0);
	scrtc->dma[0] = gem->dma_addr + fb->offsets[0]
		      + y * fb->pitches[0] + x * bpp / 8;

	if (shmob_drm_format_is_yuv(scrtc->format)) {
		bpp = scrtc->format->bpp - 8;
		gem = drm_fb_dma_get_gem_obj(fb, 1);
		scrtc->dma[1] = gem->dma_addr + fb->offsets[1]
			      + y / (bpp == 4 ? 2 : 1) * fb->pitches[1]
			      + x * (bpp == 16 ? 2 : 1);
	}
}

static void shmob_drm_crtc_update_base(struct shmob_drm_crtc *scrtc)
{
	struct drm_crtc *crtc = &scrtc->base;
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);

	shmob_drm_crtc_compute_base(scrtc, crtc->x, crtc->y);

	lcdc_write_mirror(sdev, LDSA1R, scrtc->dma[0]);
	if (shmob_drm_format_is_yuv(scrtc->format))
		lcdc_write_mirror(sdev, LDSA2R, scrtc->dma[1]);

	lcdc_write(sdev, LDRCNTR, lcdc_read(sdev, LDRCNTR) ^ LDRCNTR_MRS);
}

static inline struct shmob_drm_crtc *to_shmob_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct shmob_drm_crtc, base);
}

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
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);
	struct shmob_drm_crtc *scrtc = to_shmob_crtc(crtc);
	const struct shmob_drm_format_info *format;

	format = shmob_drm_format_info(crtc->primary->fb->format->format);
	if (format == NULL) {
		dev_dbg(sdev->dev, "mode_set: unsupported format %p4cc\n",
			&crtc->primary->fb->format->format);
		return -EINVAL;
	}

	scrtc->format = format;
	scrtc->line_size = crtc->primary->fb->pitches[0];

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

static int shmob_drm_crtc_page_flip(struct drm_crtc *crtc,
				    struct drm_framebuffer *fb,
				    struct drm_pending_vblank_event *event,
				    uint32_t page_flip_flags,
				    struct drm_modeset_acquire_ctx *ctx)
{
	struct shmob_drm_crtc *scrtc = to_shmob_crtc(crtc);
	struct drm_device *dev = scrtc->base.dev;
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
		drm_crtc_vblank_get(&scrtc->base);
		spin_lock_irqsave(&dev->event_lock, flags);
		scrtc->event = event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	return 0;
}

static void shmob_drm_crtc_enable_vblank(struct shmob_drm_device *sdev,
					 bool enable)
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

static int shmob_drm_enable_vblank(struct drm_crtc *crtc)
{
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);

	shmob_drm_crtc_enable_vblank(sdev, true);

	return 0;
}

static void shmob_drm_disable_vblank(struct drm_crtc *crtc)
{
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);

	shmob_drm_crtc_enable_vblank(sdev, false);
}

static const struct drm_crtc_funcs crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_crtc_helper_set_config,
	.page_flip = shmob_drm_crtc_page_flip,
	.enable_vblank = shmob_drm_enable_vblank,
	.disable_vblank = shmob_drm_disable_vblank,
};

int shmob_drm_crtc_create(struct shmob_drm_device *sdev)
{
	struct drm_crtc *crtc = &sdev->crtc.base;
	struct drm_plane *primary, *plane;
	unsigned int i;
	int ret;

	init_waitqueue_head(&sdev->crtc.flip_wait);

	sdev->crtc.dpms = DRM_MODE_DPMS_OFF;

	primary = shmob_drm_plane_create(sdev, DRM_PLANE_TYPE_PRIMARY, 0);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	for (i = 1; i < 5; ++i) {
		plane = shmob_drm_plane_create(sdev, DRM_PLANE_TYPE_OVERLAY, i);
		if (IS_ERR(plane))
			return PTR_ERR(plane);
	}

	ret = drm_crtc_init_with_planes(&sdev->ddev, crtc, primary, NULL,
					&crtc_funcs, NULL);
	if (ret < 0)
		return ret;

	drm_crtc_helper_add(crtc, &crtc_helper_funcs);

	/* Start with vertical blank interrupt reporting disabled. */
	drm_crtc_vblank_off(crtc);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Encoder
 */

static bool shmob_drm_encoder_mode_fixup(struct drm_encoder *encoder,
					 const struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct shmob_drm_device *sdev = to_shmob_device(dev);
	struct drm_connector *connector = sdev->connector;
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

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.mode_fixup = shmob_drm_encoder_mode_fixup,
};

int shmob_drm_encoder_create(struct shmob_drm_device *sdev)
{
	struct drm_encoder *encoder = &sdev->encoder;
	int ret;

	encoder->possible_crtcs = 1;

	ret = drm_simple_encoder_init(&sdev->ddev, encoder,
				      DRM_MODE_ENCODER_DPI);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Connector
 */

static inline struct shmob_drm_connector *to_shmob_connector(struct drm_connector *connector)
{
	return container_of(connector, struct shmob_drm_connector, base);
}

static int shmob_drm_connector_get_modes(struct drm_connector *connector)
{
	struct shmob_drm_connector *scon = to_shmob_connector(connector);
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (mode == NULL)
		return 0;

	mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;

	drm_display_mode_from_videomode(scon->mode, mode);

	drm_mode_probed_add(connector, mode);

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
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);

	kfree(connector);
}

static const struct drm_connector_funcs connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = shmob_drm_connector_destroy,
};

static struct drm_connector *
shmob_drm_connector_init(struct shmob_drm_device *sdev,
			 struct drm_encoder *encoder)
{
	u32 bus_fmt = sdev->pdata->iface.bus_fmt;
	struct shmob_drm_connector *scon;
	struct drm_connector *connector;
	struct drm_display_info *info;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(shmob_drm_bus_fmts); i++) {
		if (shmob_drm_bus_fmts[i].fmt == bus_fmt)
			break;
	}
	if (i == ARRAY_SIZE(shmob_drm_bus_fmts)) {
		dev_err(sdev->dev, "unsupported bus format 0x%x\n", bus_fmt);
		return ERR_PTR(-EINVAL);
	}

	scon = kzalloc(sizeof(*scon), GFP_KERNEL);
	if (!scon)
		return ERR_PTR(-ENOMEM);

	connector = &scon->base;
	scon->encoder = encoder;
	scon->mode = &sdev->pdata->panel.mode;

	info = &connector->display_info;
	info->width_mm = sdev->pdata->panel.width_mm;
	info->height_mm = sdev->pdata->panel.height_mm;

	if (scon->mode->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		info->bus_flags |= DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;
	if (scon->mode->flags & DISPLAY_FLAGS_DE_LOW)
		info->bus_flags |= DRM_BUS_FLAG_DE_LOW;

	ret = drm_display_info_set_bus_formats(info, &bus_fmt, 1);
	if (ret < 0) {
		kfree(scon);
		return ERR_PTR(ret);
	}

	ret = drm_connector_init(&sdev->ddev, connector, &connector_funcs,
				 DRM_MODE_CONNECTOR_DPI);
	if (ret < 0) {
		kfree(scon);
		return ERR_PTR(ret);
	}

	drm_connector_helper_add(connector, &connector_helper_funcs);

	return connector;
}

int shmob_drm_connector_create(struct shmob_drm_device *sdev,
			       struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	int ret;

	connector = shmob_drm_connector_init(sdev, encoder);
	if (IS_ERR(connector)) {
		dev_err(sdev->dev, "failed to created connector: %pe\n",
			connector);
		return PTR_ERR(connector);
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		goto error;

	drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
	drm_object_property_set_value(&connector->base,
		sdev->ddev.mode_config.dpms_property, DRM_MODE_DPMS_OFF);

	sdev->connector = connector;

	return 0;

error:
	drm_connector_cleanup(connector);
	return ret;
}
