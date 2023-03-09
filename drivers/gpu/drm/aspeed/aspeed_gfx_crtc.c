// SPDX-License-Identifier: GPL-2.0+
// Copyright 2018 IBM Corporation

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/regmap.h>

#include <drm/drm_device.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "aspeed_gfx.h"

static struct aspeed_gfx *
drm_pipe_to_aspeed_gfx(struct drm_simple_display_pipe *pipe)
{
	return container_of(pipe, struct aspeed_gfx, pipe);
}

static int aspeed_gfx_set_pixel_fmt(struct aspeed_gfx *priv, u32 *bpp)
{
	struct drm_crtc *crtc = &priv->pipe.crtc;
	struct drm_device *drm = crtc->dev;
	const u32 format = crtc->primary->state->fb->format->format;
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

static void aspeed_gfx_crtc_mode_set_nofb(struct aspeed_gfx *priv)
{
	struct drm_display_mode *m = &priv->pipe.crtc.state->adjusted_mode;
	u32 ctrl1, d_offset, t_count, bpp;
	int err;

	err = aspeed_gfx_set_pixel_fmt(priv, &bpp);
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

static void aspeed_gfx_pipe_enable(struct drm_simple_display_pipe *pipe,
			      struct drm_crtc_state *crtc_state,
			      struct drm_plane_state *plane_state)
{
	struct aspeed_gfx *priv = drm_pipe_to_aspeed_gfx(pipe);
	struct drm_crtc *crtc = &pipe->crtc;

	aspeed_gfx_crtc_mode_set_nofb(priv);
	aspeed_gfx_enable_controller(priv);
	drm_crtc_vblank_on(crtc);
}

static void aspeed_gfx_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct aspeed_gfx *priv = drm_pipe_to_aspeed_gfx(pipe);
	struct drm_crtc *crtc = &pipe->crtc;

	drm_crtc_vblank_off(crtc);
	aspeed_gfx_disable_controller(priv);
}

static void aspeed_gfx_pipe_update(struct drm_simple_display_pipe *pipe,
				   struct drm_plane_state *plane_state)
{
	struct aspeed_gfx *priv = drm_pipe_to_aspeed_gfx(pipe);
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_framebuffer *fb = pipe->plane.state->fb;
	struct drm_pending_vblank_event *event;
	struct drm_gem_dma_object *gem;

	spin_lock_irq(&crtc->dev->event_lock);
	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;

		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	if (!fb)
		return;

	gem = drm_fb_dma_get_gem_obj(fb, 0);
	if (!gem)
		return;
	writel(gem->dma_addr, priv->base + CRT_ADDR);
}

static int aspeed_gfx_enable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct aspeed_gfx *priv = drm_pipe_to_aspeed_gfx(pipe);
	u32 reg = readl(priv->base + CRT_CTRL1);

	/* Clear pending VBLANK IRQ */
	writel(reg | CRT_CTRL_VERTICAL_INTR_STS, priv->base + CRT_CTRL1);

	reg |= CRT_CTRL_VERTICAL_INTR_EN;
	writel(reg, priv->base + CRT_CTRL1);

	return 0;
}

static void aspeed_gfx_disable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct aspeed_gfx *priv = drm_pipe_to_aspeed_gfx(pipe);
	u32 reg = readl(priv->base + CRT_CTRL1);

	reg &= ~CRT_CTRL_VERTICAL_INTR_EN;
	writel(reg, priv->base + CRT_CTRL1);

	/* Clear pending VBLANK IRQ */
	writel(reg | CRT_CTRL_VERTICAL_INTR_STS, priv->base + CRT_CTRL1);
}

static const struct drm_simple_display_pipe_funcs aspeed_gfx_funcs = {
	.enable		= aspeed_gfx_pipe_enable,
	.disable	= aspeed_gfx_pipe_disable,
	.update		= aspeed_gfx_pipe_update,
	.enable_vblank	= aspeed_gfx_enable_vblank,
	.disable_vblank	= aspeed_gfx_disable_vblank,
};

static const uint32_t aspeed_gfx_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
};

int aspeed_gfx_create_pipe(struct drm_device *drm)
{
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);

	return drm_simple_display_pipe_init(drm, &priv->pipe, &aspeed_gfx_funcs,
					    aspeed_gfx_formats,
					    ARRAY_SIZE(aspeed_gfx_formats),
					    NULL,
					    &priv->connector);
}
