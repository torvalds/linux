// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <linux/clk.h>
#include <linux/platform_data/simplefb.h>

#include "arcpgu.h"
#include "arcpgu_regs.h"

#define ENCODE_PGU_XY(x, y)	((((x) - 1) << 16) | ((y) - 1))

static const u32 arc_pgu_supported_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static void arc_pgu_set_pxl_fmt(struct arcpgu_drm_private *arcpgu)
{
	const struct drm_framebuffer *fb = arcpgu->pipe.plane.state->fb;
	uint32_t pixel_format = fb->format->format;
	u32 format = DRM_FORMAT_INVALID;
	int i;
	u32 reg_ctrl;

	for (i = 0; i < ARRAY_SIZE(arc_pgu_supported_formats); i++) {
		if (arc_pgu_supported_formats[i] == pixel_format)
			format = arc_pgu_supported_formats[i];
	}

	if (WARN_ON(format == DRM_FORMAT_INVALID))
		return;

	reg_ctrl = arc_pgu_read(arcpgu, ARCPGU_REG_CTRL);
	if (format == DRM_FORMAT_RGB565)
		reg_ctrl &= ~ARCPGU_MODE_XRGB8888;
	else
		reg_ctrl |= ARCPGU_MODE_XRGB8888;
	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL, reg_ctrl);
}

static enum drm_mode_status arc_pgu_mode_valid(struct drm_simple_display_pipe *pipe,
					       const struct drm_display_mode *mode)
{
	struct arcpgu_drm_private *arcpgu = pipe_to_arcpgu_priv(pipe);
	long rate, clk_rate = mode->clock * 1000;
	long diff = clk_rate / 200; /* +-0.5% allowed by HDMI spec */

	rate = clk_round_rate(arcpgu->clk, clk_rate);
	if ((max(rate, clk_rate) - min(rate, clk_rate) < diff) && (rate > 0))
		return MODE_OK;

	return MODE_NOCLOCK;
}

static void arc_pgu_mode_set(struct arcpgu_drm_private *arcpgu)
{
	struct drm_display_mode *m = &arcpgu->pipe.crtc.state->adjusted_mode;
	u32 val;

	arc_pgu_write(arcpgu, ARCPGU_REG_FMT,
		      ENCODE_PGU_XY(m->crtc_htotal, m->crtc_vtotal));

	arc_pgu_write(arcpgu, ARCPGU_REG_HSYNC,
		      ENCODE_PGU_XY(m->crtc_hsync_start - m->crtc_hdisplay,
				    m->crtc_hsync_end - m->crtc_hdisplay));

	arc_pgu_write(arcpgu, ARCPGU_REG_VSYNC,
		      ENCODE_PGU_XY(m->crtc_vsync_start - m->crtc_vdisplay,
				    m->crtc_vsync_end - m->crtc_vdisplay));

	arc_pgu_write(arcpgu, ARCPGU_REG_ACTIVE,
		      ENCODE_PGU_XY(m->crtc_hblank_end - m->crtc_hblank_start,
				    m->crtc_vblank_end - m->crtc_vblank_start));

	val = arc_pgu_read(arcpgu, ARCPGU_REG_CTRL);

	if (m->flags & DRM_MODE_FLAG_PVSYNC)
		val |= ARCPGU_CTRL_VS_POL_MASK << ARCPGU_CTRL_VS_POL_OFST;
	else
		val &= ~(ARCPGU_CTRL_VS_POL_MASK << ARCPGU_CTRL_VS_POL_OFST);

	if (m->flags & DRM_MODE_FLAG_PHSYNC)
		val |= ARCPGU_CTRL_HS_POL_MASK << ARCPGU_CTRL_HS_POL_OFST;
	else
		val &= ~(ARCPGU_CTRL_HS_POL_MASK << ARCPGU_CTRL_HS_POL_OFST);

	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL, val);
	arc_pgu_write(arcpgu, ARCPGU_REG_STRIDE, 0);
	arc_pgu_write(arcpgu, ARCPGU_REG_START_SET, 1);

	arc_pgu_set_pxl_fmt(arcpgu);

	clk_set_rate(arcpgu->clk, m->crtc_clock * 1000);
}

static void arc_pgu_enable(struct drm_simple_display_pipe *pipe,
			   struct drm_crtc_state *crtc_state,
			   struct drm_plane_state *plane_state)
{
	struct arcpgu_drm_private *arcpgu = pipe_to_arcpgu_priv(pipe);

	arc_pgu_mode_set(arcpgu);

	clk_prepare_enable(arcpgu->clk);
	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL,
		      arc_pgu_read(arcpgu, ARCPGU_REG_CTRL) |
		      ARCPGU_CTRL_ENABLE_MASK);
}

static void arc_pgu_disable(struct drm_simple_display_pipe *pipe)
{
	struct arcpgu_drm_private *arcpgu = pipe_to_arcpgu_priv(pipe);

	clk_disable_unprepare(arcpgu->clk);
	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL,
			      arc_pgu_read(arcpgu, ARCPGU_REG_CTRL) &
			      ~ARCPGU_CTRL_ENABLE_MASK);
}

static void arc_pgu_update(struct drm_simple_display_pipe *pipe,
			   struct drm_plane_state *state)
{
	struct arcpgu_drm_private *arcpgu;
	struct drm_gem_cma_object *gem;

	if (!pipe->plane.state->crtc || !pipe->plane.state->fb)
		return;

	arcpgu = pipe_to_arcpgu_priv(pipe);
	gem = drm_fb_cma_get_gem_obj(pipe->plane.state->fb, 0);
	arc_pgu_write(arcpgu, ARCPGU_REG_BUF0_ADDR, gem->paddr);
}

static const struct drm_simple_display_pipe_funcs arc_pgu_pipe_funcs = {
	.update = arc_pgu_update,
	.mode_valid = arc_pgu_mode_valid,
	.enable	= arc_pgu_enable,
	.disable = arc_pgu_disable,
};

int arc_pgu_setup_pipe(struct drm_device *drm)
{
	struct arcpgu_drm_private *arcpgu = dev_to_arcpgu(drm);

	return drm_simple_display_pipe_init(drm, &arcpgu->pipe, &arc_pgu_pipe_funcs,
					    arc_pgu_supported_formats,
					    ARRAY_SIZE(arc_pgu_supported_formats),
					    NULL, NULL);
}
