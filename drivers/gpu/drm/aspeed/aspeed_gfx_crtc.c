// SPDX-License-Identifier: GPL-2.0+
// Copyright 2018 IBM Corporation

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_vblank.h>

#include "aspeed_gfx.h"

static int aspeed_gfx_set_pixel_fmt(struct aspeed_gfx *priv,
				    struct drm_plane_state *plane_state,
				    u32 *bpp)
{
	struct drm_crtc *crtc = &priv->crtc;
	struct drm_device *drm = crtc->dev;
	const u32 format = plane_state->fb->format->format;
	u32 ctrl1;

	ctrl1 = readl(priv->base + CRT_CTRL1);
	ctrl1 &= ~CRT_CTRL_COLOR_MASK;

	switch (format) {
	case DRM_FORMAT_RGB565:
		dev_dbg(drm->dev, "Setting up RGB565 mode\n");
		ctrl1 |= CRT_CTRL_COLOR_RGB565;
		*bpp = 16;
		break;
	case DRM_FORMAT_XRGB8888:
		dev_dbg(drm->dev, "Setting up XRGB8888 mode\n");
		ctrl1 |= CRT_CTRL_COLOR_XRGB8888;
		*bpp = 32;
		break;
	default:
		dev_err(drm->dev, "Unhandled pixel format %08x\n", format);
		return -EINVAL;
	}

	writel(ctrl1, priv->base + CRT_CTRL1);

	return 0;
}

static void aspeed_gfx_enable_controller(struct aspeed_gfx *priv)
{
	u32 ctrl1 = readl(priv->base + CRT_CTRL1);
	u32 ctrl2 = readl(priv->base + CRT_CTRL2);

	/* Set DAC source for display output to Graphics CRT (GFX) */
	regmap_update_bits(priv->scu, priv->dac_reg, BIT(16), BIT(16));

	writel(ctrl1 | CRT_CTRL_EN, priv->base + CRT_CTRL1);
	writel(ctrl2 | CRT_CTRL_DAC_EN, priv->base + CRT_CTRL2);
}

static void aspeed_gfx_disable_controller(struct aspeed_gfx *priv)
{
	u32 ctrl1 = readl(priv->base + CRT_CTRL1);
	u32 ctrl2 = readl(priv->base + CRT_CTRL2);

	writel(ctrl1 & ~CRT_CTRL_EN, priv->base + CRT_CTRL1);
	writel(ctrl2 & ~CRT_CTRL_DAC_EN, priv->base + CRT_CTRL2);

	regmap_update_bits(priv->scu, priv->dac_reg, BIT(16), 0);
}

static void aspeed_gfx_crtc_mode_set_nofb(struct aspeed_gfx *priv,
					  struct drm_crtc_state *crtc_state,
					  struct drm_plane_state *plane_state)
{
	struct drm_display_mode *m = &crtc_state->adjusted_mode;
	u32 ctrl1, d_offset, t_count, bpp;
	int err;

	err = aspeed_gfx_set_pixel_fmt(priv, plane_state, &bpp);
	if (err)
		return;

#if 0
	/* TODO: we have only been able to test with the 40MHz USB clock. The
	 * clock is fixed, so we cannot adjust it here. */
	clk_set_rate(priv->pixel_clk, m->crtc_clock * 1000);
#endif

	ctrl1 = readl(priv->base + CRT_CTRL1);
	ctrl1 &= ~(CRT_CTRL_INTERLACED |
			CRT_CTRL_HSYNC_NEGATIVE |
			CRT_CTRL_VSYNC_NEGATIVE);

	if (m->flags & DRM_MODE_FLAG_INTERLACE)
		ctrl1 |= CRT_CTRL_INTERLACED;

	if (!(m->flags & DRM_MODE_FLAG_PHSYNC))
		ctrl1 |= CRT_CTRL_HSYNC_NEGATIVE;

	if (!(m->flags & DRM_MODE_FLAG_PVSYNC))
		ctrl1 |= CRT_CTRL_VSYNC_NEGATIVE;

	writel(ctrl1, priv->base + CRT_CTRL1);

	/* Horizontal timing */
	writel(CRT_H_TOTAL(m->htotal - 1) | CRT_H_DE(m->hdisplay - 1),
			priv->base + CRT_HORIZ0);
	writel(CRT_H_RS_START(m->hsync_start - 1) | CRT_H_RS_END(m->hsync_end),
			priv->base + CRT_HORIZ1);


	/* Vertical timing */
	writel(CRT_V_TOTAL(m->vtotal - 1) | CRT_V_DE(m->vdisplay - 1),
			priv->base + CRT_VERT0);
	writel(CRT_V_RS_START(m->vsync_start) | CRT_V_RS_END(m->vsync_end),
			priv->base + CRT_VERT1);

	/*
	 * Display Offset: address difference between consecutive scan lines
	 * Terminal Count: memory size of one scan line
	 */
	d_offset = m->hdisplay * bpp / 8;
	t_count = DIV_ROUND_UP(m->hdisplay * bpp, priv->scan_line_max);

	writel(CRT_DISP_OFFSET(d_offset) | CRT_TERM_COUNT(t_count),
			priv->base + CRT_OFFSET);

	/*
	 * Threshold: FIFO thresholds of refill and stop (16 byte chunks
	 * per line, rounded up)
	 */
	writel(priv->throd_val, priv->base + CRT_THROD);
}

static void aspeed_gfx_crtc_helper_atomic_enable(struct drm_crtc *crtc,
						 struct drm_atomic_commit *commit)
{
	struct aspeed_gfx *priv = to_aspeed_gfx(crtc->dev);
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(commit, crtc);
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(commit, &priv->plane);

	aspeed_gfx_crtc_mode_set_nofb(priv, crtc_state, plane_state);
	aspeed_gfx_enable_controller(priv);
	drm_crtc_vblank_on(crtc);
}

static void aspeed_gfx_crtc_helper_atomic_disable(struct drm_crtc *crtc,
						  struct drm_atomic_commit *commit)
{
	struct aspeed_gfx *priv = to_aspeed_gfx(crtc->dev);

	drm_crtc_vblank_off(crtc);
	aspeed_gfx_disable_controller(priv);
}

static void aspeed_gfx_plane_helper_atomic_update(struct drm_plane *plane,
						  struct drm_atomic_commit *commit)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(commit, plane);
	struct aspeed_gfx *priv = to_aspeed_gfx(plane->dev);
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_gem_dma_object *gem;

	if (!fb)
		return;

	gem = drm_fb_dma_get_gem_obj(fb, 0);
	if (!gem)
		return;
	writel(gem->dma_addr, priv->base + CRT_ADDR);
}

static int aspeed_gfx_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct aspeed_gfx *priv = to_aspeed_gfx(crtc->dev);
	u32 reg = readl(priv->base + CRT_CTRL1);

	/* Clear pending VBLANK IRQ */
	writel(reg | CRT_CTRL_VERTICAL_INTR_STS, priv->base + CRT_CTRL1);

	reg |= CRT_CTRL_VERTICAL_INTR_EN;
	writel(reg, priv->base + CRT_CTRL1);

	return 0;
}

static void aspeed_gfx_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct aspeed_gfx *priv = to_aspeed_gfx(crtc->dev);
	u32 reg = readl(priv->base + CRT_CTRL1);

	reg &= ~CRT_CTRL_VERTICAL_INTR_EN;
	writel(reg, priv->base + CRT_CTRL1);

	/* Clear pending VBLANK IRQ */
	writel(reg | CRT_CTRL_VERTICAL_INTR_STS, priv->base + CRT_CTRL1);
}

static int aspeed_gfx_plane_helper_atomic_check(struct drm_plane *plane,
						struct drm_atomic_commit *commit)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(commit, plane);
	struct drm_crtc_state *crtc_state = NULL;

	if (plane_state->crtc) {
		crtc_state = drm_atomic_get_crtc_state(commit, plane_state->crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static const struct drm_plane_helper_funcs aspeed_gfx_plane_helper_funcs = {
	.prepare_fb	= drm_gem_plane_helper_prepare_fb,
	.atomic_check	= aspeed_gfx_plane_helper_atomic_check,
	.atomic_update	= aspeed_gfx_plane_helper_atomic_update,
};

static const struct drm_plane_funcs aspeed_gfx_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.atomic_create_state	= drm_atomic_helper_plane_create_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int aspeed_gfx_crtc_helper_atomic_check(struct drm_crtc *crtc,
					       struct drm_atomic_commit *commit)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(commit, crtc);
	int ret;

	if (crtc_state->enable) {
		ret = drm_atomic_helper_check_crtc_primary_plane(crtc_state);
		if (ret)
			return ret;
	}

	return drm_atomic_add_affected_planes(commit, crtc);
}

static void aspeed_gfx_crtc_helper_atomic_flush(struct drm_crtc *crtc,
						struct drm_atomic_commit *commit)
{
	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(commit, crtc);
	struct drm_pending_vblank_event *event = new_crtc_state->event;

	if (!event)
		return;

	new_crtc_state->event = NULL;

	spin_lock_irq(&crtc->dev->event_lock);
	if (drm_crtc_vblank_get(crtc) == 0)
		drm_crtc_arm_vblank_event(crtc, event);
	else
		drm_crtc_send_vblank_event(crtc, event);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static const struct drm_crtc_helper_funcs aspeed_gfx_crtc_helper_funcs = {
	.atomic_check	= aspeed_gfx_crtc_helper_atomic_check,
	.atomic_enable	= aspeed_gfx_crtc_helper_atomic_enable,
	.atomic_disable	= aspeed_gfx_crtc_helper_atomic_disable,
	.atomic_flush	= aspeed_gfx_crtc_helper_atomic_flush,
};

static const struct drm_crtc_funcs aspeed_gfx_crtc_funcs = {
	.atomic_create_state	= drm_atomic_helper_crtc_create_state,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= aspeed_gfx_crtc_enable_vblank,
	.disable_vblank		= aspeed_gfx_crtc_disable_vblank,
};

static const struct drm_encoder_funcs aspeed_gfx_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const uint32_t aspeed_gfx_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
};

int aspeed_gfx_create_pipe(struct drm_device *drm)
{
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);
	struct drm_plane *plane = &priv->plane;
	struct drm_crtc *crtc = &priv->crtc;
	struct drm_encoder *encoder = &priv->encoder;
	int ret;

	ret = drm_universal_plane_init(drm, plane, 0,
				       &aspeed_gfx_plane_funcs,
				       aspeed_gfx_formats,
				       ARRAY_SIZE(aspeed_gfx_formats),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(plane, &aspeed_gfx_plane_helper_funcs);

	ret = drm_crtc_init_with_planes(drm, crtc, plane, NULL,
					&aspeed_gfx_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &aspeed_gfx_crtc_helper_funcs);

	ret = drm_encoder_init(drm, encoder, &aspeed_gfx_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = drm_connector_attach_encoder(&priv->connector, encoder);
	if (ret)
		return ret;

	return 0;
}
