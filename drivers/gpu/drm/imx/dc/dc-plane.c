// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/container_of.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>

#include "dc-drv.h"
#include "dc-fu.h"
#include "dc-kms.h"

#define DC_PLANE_MAX_PITCH	0x10000
#define DC_PLANE_MAX_PIX_CNT	8192

#define dc_plane_dbg(plane, fmt, ...)					\
do {									\
	struct drm_plane *_plane = (plane);				\
	drm_dbg_kms(_plane->dev, "[PLANE:%d:%s] " fmt,			\
		    _plane->base.id, _plane->name, ##__VA_ARGS__);	\
} while (0)

static const uint32_t dc_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_plane_funcs dc_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static inline struct dc_plane *to_dc_plane(struct drm_plane *plane)
{
	return container_of(plane, struct dc_plane, base);
}

static int dc_plane_check_max_source_resolution(struct drm_plane_state *state)
{
	int src_h = drm_rect_height(&state->src) >> 16;
	int src_w = drm_rect_width(&state->src) >> 16;

	if (src_w > DC_PLANE_MAX_PIX_CNT || src_h > DC_PLANE_MAX_PIX_CNT) {
		dc_plane_dbg(state->plane, "invalid source resolution\n");
		return -EINVAL;
	}

	return 0;
}

static int dc_plane_check_fb(struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	dma_addr_t baseaddr = drm_fb_dma_get_gem_addr(fb, state, 0);

	/* base address alignment */
	if (baseaddr & 0x3) {
		dc_plane_dbg(state->plane, "fb bad baddr alignment\n");
		return -EINVAL;
	}

	/* pitches[0] range */
	if (fb->pitches[0] > DC_PLANE_MAX_PITCH) {
		dc_plane_dbg(state->plane, "fb pitches[0] is out of range\n");
		return -EINVAL;
	}

	/* pitches[0] alignment */
	if (fb->pitches[0] & 0x3) {
		dc_plane_dbg(state->plane, "fb bad pitches[0] alignment\n");
		return -EINVAL;
	}

	return 0;
}

static int
dc_plane_atomic_check(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state =
				drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc_state;
	int ret;

	/* ok to disable */
	if (!plane_state->fb)
		return 0;

	if (!plane_state->crtc) {
		dc_plane_dbg(plane, "no CRTC in plane state\n");
		return -EINVAL;
	}

	crtc_state =
		drm_atomic_get_existing_crtc_state(state, plane_state->crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	ret = drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  true, false);
	if (ret) {
		dc_plane_dbg(plane, "failed to check plane state: %d\n", ret);
		return ret;
	}

	ret = dc_plane_check_max_source_resolution(plane_state);
	if (ret)
		return ret;

	return dc_plane_check_fb(plane_state);
}

static void
dc_plane_atomic_update(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state =
				drm_atomic_get_new_plane_state(state, plane);
	struct dc_plane *dplane = to_dc_plane(plane);
	struct drm_framebuffer *fb = new_state->fb;
	const struct dc_fu_ops *fu_ops;
	struct dc_lb *lb = dplane->lb;
	struct dc_fu *fu = dplane->fu;
	dma_addr_t baseaddr;
	int src_w, src_h;
	int idx;

	if (!drm_dev_enter(plane->dev, &idx))
		return;

	src_w = drm_rect_width(&new_state->src) >> 16;
	src_h = drm_rect_height(&new_state->src) >> 16;

	baseaddr = drm_fb_dma_get_gem_addr(fb, new_state, 0);

	fu_ops = dc_fu_get_ops(dplane->fu);

	fu_ops->set_layerblend(fu, lb);
	fu_ops->set_burstlength(fu, baseaddr);
	fu_ops->set_src_stride(fu, DC_FETCHUNIT_FRAC0, fb->pitches[0]);
	fu_ops->set_src_buf_dimensions(fu, DC_FETCHUNIT_FRAC0, src_w, src_h);
	fu_ops->set_fmt(fu, DC_FETCHUNIT_FRAC0, fb->format);
	fu_ops->set_framedimensions(fu, src_w, src_h);
	fu_ops->set_baseaddress(fu, DC_FETCHUNIT_FRAC0, baseaddr);
	fu_ops->enable_src_buf(fu, DC_FETCHUNIT_FRAC0);

	dc_plane_dbg(plane, "uses %s\n", fu_ops->get_name(fu));

	dc_lb_pec_dynamic_prim_sel(lb, dc_cf_get_link_id(dplane->cf));
	dc_lb_pec_dynamic_sec_sel(lb, fu_ops->get_link_id(fu));
	dc_lb_mode(lb, LB_BLEND);
	dc_lb_position(lb, new_state->dst.x1, new_state->dst.y1);
	dc_lb_pec_clken(lb, CLKEN_AUTOMATIC);

	dc_plane_dbg(plane, "uses LayerBlend%d\n", dc_lb_get_id(lb));

	/* set ExtDst's source to LayerBlend */
	dc_ed_pec_src_sel(dplane->ed, dc_lb_get_link_id(lb));

	drm_dev_exit(idx);
}

static void dc_plane_atomic_disable(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct dc_plane *dplane = to_dc_plane(plane);
	const struct dc_fu_ops *fu_ops;
	int idx;

	if (!drm_dev_enter(plane->dev, &idx))
		return;

	/* disable fetchunit in shadow */
	fu_ops = dc_fu_get_ops(dplane->fu);
	fu_ops->disable_src_buf(dplane->fu, DC_FETCHUNIT_FRAC0);

	/* set ExtDst's source to ConstFrame */
	dc_ed_pec_src_sel(dplane->ed, dc_cf_get_link_id(dplane->cf));

	drm_dev_exit(idx);
}

static const struct drm_plane_helper_funcs dc_plane_helper_funcs = {
	.atomic_check = dc_plane_atomic_check,
	.atomic_update = dc_plane_atomic_update,
	.atomic_disable = dc_plane_atomic_disable,
};

int dc_plane_init(struct dc_drm_device *dc_drm, struct dc_plane *dc_plane)
{
	struct drm_plane *plane = &dc_plane->base;
	int ret;

	ret = drm_universal_plane_init(&dc_drm->base, plane, 0, &dc_plane_funcs,
				       dc_plane_formats,
				       ARRAY_SIZE(dc_plane_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_helper_add(plane, &dc_plane_helper_funcs);

	dc_plane->fu = dc_drm->pe->fu_disp[plane->index];
	dc_plane->cf = dc_drm->pe->cf_cont[plane->index];
	dc_plane->lb = dc_drm->pe->lb[plane->index];
	dc_plane->ed = dc_drm->pe->ed_cont[plane->index];

	return 0;
}
