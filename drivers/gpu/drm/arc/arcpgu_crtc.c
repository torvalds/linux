/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <linux/clk.h>
#include <linux/platform_data/simplefb.h>

#include "arcpgu.h"
#include "arcpgu_regs.h"

#define ENCODE_PGU_XY(x, y)	((((x) - 1) << 16) | ((y) - 1))

static struct simplefb_format supported_formats[] = {
	{ "r5g6b5", 16, {11, 5}, {5, 6}, {0, 5}, {0, 0}, DRM_FORMAT_RGB565 },
	{ "r8g8b8", 24, {16, 8}, {8, 8}, {0, 8}, {0, 0}, DRM_FORMAT_RGB888 },
};

static void arc_pgu_set_pxl_fmt(struct drm_crtc *crtc)
{
	struct arcpgu_drm_private *arcpgu = crtc_to_arcpgu_priv(crtc);
	uint32_t pixel_format = crtc->primary->state->fb->pixel_format;
	struct simplefb_format *format = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_formats); i++) {
		if (supported_formats[i].fourcc == pixel_format)
			format = &supported_formats[i];
	}

	if (WARN_ON(!format))
		return;

	if (format->fourcc == DRM_FORMAT_RGB888)
		arc_pgu_write(arcpgu, ARCPGU_REG_CTRL,
			      arc_pgu_read(arcpgu, ARCPGU_REG_CTRL) |
					   ARCPGU_MODE_RGB888_MASK);

}

static const struct drm_crtc_funcs arc_pgu_crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static void arc_pgu_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct arcpgu_drm_private *arcpgu = crtc_to_arcpgu_priv(crtc);
	struct drm_display_mode *m = &crtc->state->adjusted_mode;
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

	arc_pgu_set_pxl_fmt(crtc);

	clk_set_rate(arcpgu->clk, m->crtc_clock * 1000);
}

static void arc_pgu_crtc_enable(struct drm_crtc *crtc)
{
	struct arcpgu_drm_private *arcpgu = crtc_to_arcpgu_priv(crtc);

	clk_prepare_enable(arcpgu->clk);
	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL,
		      arc_pgu_read(arcpgu, ARCPGU_REG_CTRL) |
		      ARCPGU_CTRL_ENABLE_MASK);
}

static void arc_pgu_crtc_disable(struct drm_crtc *crtc)
{
	struct arcpgu_drm_private *arcpgu = crtc_to_arcpgu_priv(crtc);

	if (!crtc->primary->fb)
		return;

	clk_disable_unprepare(arcpgu->clk);
	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL,
			      arc_pgu_read(arcpgu, ARCPGU_REG_CTRL) &
			      ~ARCPGU_CTRL_ENABLE_MASK);
}

static int arc_pgu_crtc_atomic_check(struct drm_crtc *crtc,
				     struct drm_crtc_state *state)
{
	struct arcpgu_drm_private *arcpgu = crtc_to_arcpgu_priv(crtc);
	struct drm_display_mode *mode = &state->adjusted_mode;
	long rate, clk_rate = mode->clock * 1000;

	rate = clk_round_rate(arcpgu->clk, clk_rate);
	if (rate != clk_rate)
		return -EINVAL;

	return 0;
}

static void arc_pgu_crtc_atomic_begin(struct drm_crtc *crtc,
				      struct drm_crtc_state *state)
{
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const struct drm_crtc_helper_funcs arc_pgu_crtc_helper_funcs = {
	.mode_set	= drm_helper_crtc_mode_set,
	.mode_set_base	= drm_helper_crtc_mode_set_base,
	.mode_set_nofb	= arc_pgu_crtc_mode_set_nofb,
	.enable		= arc_pgu_crtc_enable,
	.disable	= arc_pgu_crtc_disable,
	.prepare	= arc_pgu_crtc_disable,
	.commit		= arc_pgu_crtc_enable,
	.atomic_check	= arc_pgu_crtc_atomic_check,
	.atomic_begin	= arc_pgu_crtc_atomic_begin,
};

static void arc_pgu_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *state)
{
	struct arcpgu_drm_private *arcpgu;
	struct drm_gem_cma_object *gem;

	if (!plane->state->crtc || !plane->state->fb)
		return;

	arcpgu = crtc_to_arcpgu_priv(plane->state->crtc);
	gem = drm_fb_cma_get_gem_obj(plane->state->fb, 0);
	arc_pgu_write(arcpgu, ARCPGU_REG_BUF0_ADDR, gem->paddr);
}

static const struct drm_plane_helper_funcs arc_pgu_plane_helper_funcs = {
	.atomic_update = arc_pgu_plane_atomic_update,
};

static void arc_pgu_plane_destroy(struct drm_plane *plane)
{
	drm_plane_helper_disable(plane);
	drm_plane_cleanup(plane);
}

static const struct drm_plane_funcs arc_pgu_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= arc_pgu_plane_destroy,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static struct drm_plane *arc_pgu_plane_init(struct drm_device *drm)
{
	struct arcpgu_drm_private *arcpgu = drm->dev_private;
	struct drm_plane *plane = NULL;
	u32 formats[ARRAY_SIZE(supported_formats)], i;
	int ret;

	plane = devm_kzalloc(drm->dev, sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ARRAY_SIZE(supported_formats); i++)
		formats[i] = supported_formats[i].fourcc;

	ret = drm_universal_plane_init(drm, plane, 0xff, &arc_pgu_plane_funcs,
				       formats, ARRAY_SIZE(formats),
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ERR_PTR(ret);

	drm_plane_helper_add(plane, &arc_pgu_plane_helper_funcs);
	arcpgu->plane = plane;

	return plane;
}

int arc_pgu_setup_crtc(struct drm_device *drm)
{
	struct arcpgu_drm_private *arcpgu = drm->dev_private;
	struct drm_plane *primary;
	int ret;

	primary = arc_pgu_plane_init(drm);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	ret = drm_crtc_init_with_planes(drm, &arcpgu->crtc, primary, NULL,
					&arc_pgu_crtc_funcs, NULL);
	if (ret) {
		arc_pgu_plane_destroy(primary);
		return ret;
	}

	drm_crtc_helper_add(&arcpgu->crtc, &arc_pgu_crtc_helper_funcs);
	return 0;
}
