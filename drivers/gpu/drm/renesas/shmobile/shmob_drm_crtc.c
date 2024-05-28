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
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include <video/videomode.h>

#include "shmob_drm_crtc.h"
#include "shmob_drm_drv.h"
#include "shmob_drm_kms.h"
#include "shmob_drm_plane.h"
#include "shmob_drm_regs.h"

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

static inline struct shmob_drm_crtc *to_shmob_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct shmob_drm_crtc, base);
}

static void shmob_drm_crtc_atomic_enable(struct drm_crtc *crtc,
					 struct drm_atomic_state *state)
{
	struct shmob_drm_crtc *scrtc = to_shmob_crtc(crtc);
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);
	unsigned int clk_div = sdev->config.clk_div;
	struct device *dev = sdev->dev;
	u32 value;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
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
	if (clk_div) {
		/* FIXME: sh7724 can only use 42, 48, 54 and 60 for the divider
		 * denominator.
		 */
		lcdc_write(sdev, LDDCKPAT1R, 0);
		lcdc_write(sdev, LDDCKPAT2R, (1 << (clk_div / 2)) - 1);

		if (clk_div == 1)
			value |= LDDCKR_MOSEL;
		else
			value |= clk_div;
	}

	lcdc_write(sdev, LDDCKR, value);
	lcdc_write(sdev, LDDCKSTPR, 0);
	lcdc_wait_bit(sdev, LDDCKSTPR, ~0, 0);

	/* Setup geometry, format, frame buffer memory and operation mode. */
	shmob_drm_crtc_setup_geometry(scrtc);

	lcdc_write(sdev, LDSM1R, 0);

	/* Enable the display output. */
	lcdc_write(sdev, LDCNT1R, LDCNT1R_DE);

	shmob_drm_crtc_start_stop(scrtc, true);

	/* Turn vertical blank interrupt reporting back on. */
	drm_crtc_vblank_on(crtc);
}

static void shmob_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					  struct drm_atomic_state *state)
{
	struct shmob_drm_crtc *scrtc = to_shmob_crtc(crtc);
	struct shmob_drm_device *sdev = to_shmob_device(crtc->dev);

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
}

static void shmob_drm_crtc_atomic_flush(struct drm_crtc *crtc,
					struct drm_atomic_state *state)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	if (crtc->state->event) {
		spin_lock_irqsave(&dev->event_lock, flags);
		event = crtc->state->event;
		crtc->state->event = NULL;
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
}

static const struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.atomic_flush = shmob_drm_crtc_atomic_flush,
	.atomic_enable = shmob_drm_crtc_atomic_enable,
	.atomic_disable = shmob_drm_crtc_atomic_disable,
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

	drm_atomic_set_fb_for_plane(crtc->primary->state, fb);

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
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = shmob_drm_crtc_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
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
 * Legacy Encoder
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

/* -----------------------------------------------------------------------------
 * Encoder
 */

int shmob_drm_encoder_create(struct shmob_drm_device *sdev)
{
	struct drm_encoder *encoder = &sdev->encoder;
	struct drm_bridge *bridge;
	int ret;

	encoder->possible_crtcs = 1;

	ret = drm_simple_encoder_init(&sdev->ddev, encoder,
				      DRM_MODE_ENCODER_DPI);
	if (ret < 0)
		return ret;

	if (sdev->pdata) {
		drm_encoder_helper_add(encoder, &encoder_helper_funcs);
		return 0;
	}

	/* Create a panel bridge */
	bridge = devm_drm_of_get_bridge(sdev->dev, sdev->dev->of_node, 0, 0);
	if (IS_ERR(bridge))
		return PTR_ERR(bridge);

	/* Attach the bridge to the encoder */
	ret = drm_bridge_attach(encoder, bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(sdev->dev, "failed to attach bridge: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Legacy Connector
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
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = shmob_drm_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
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

/* -----------------------------------------------------------------------------
 * Connector
 */

int shmob_drm_connector_create(struct shmob_drm_device *sdev,
			       struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	int ret;

	if (sdev->pdata)
		connector = shmob_drm_connector_init(sdev, encoder);
	else
		connector = drm_bridge_connector_init(&sdev->ddev, encoder);
	if (IS_ERR(connector)) {
		dev_err(sdev->dev, "failed to created connector: %pe\n",
			connector);
		return PTR_ERR(connector);
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		goto error;

	connector->dpms = DRM_MODE_DPMS_OFF;

	sdev->connector = connector;

	return 0;

error:
	drm_connector_cleanup(connector);
	return ret;
}
