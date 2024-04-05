// SPDX-License-Identifier: GPL-2.0+
/*
 * RZ/G2L Display Unit VSP-Based Compositor
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_vsp.c
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_vblank.h>

#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>

#include <media/vsp1.h>

#include "rzg2l_du_drv.h"
#include "rzg2l_du_kms.h"
#include "rzg2l_du_vsp.h"

static void rzg2l_du_vsp_complete(void *private, unsigned int status, u32 crc)
{
	struct rzg2l_du_crtc *crtc = private;

	if (crtc->vblank_enable)
		drm_crtc_handle_vblank(&crtc->crtc);

	if (status & VSP1_DU_STATUS_COMPLETE)
		rzg2l_du_crtc_finish_page_flip(crtc);

	drm_crtc_add_crc_entry(&crtc->crtc, false, 0, &crc);
}

void rzg2l_du_vsp_enable(struct rzg2l_du_crtc *crtc)
{
	const struct drm_display_mode *mode = &crtc->crtc.state->adjusted_mode;
	struct vsp1_du_lif_config cfg = {
		.width = mode->hdisplay,
		.height = mode->vdisplay,
		.interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE,
		.callback = rzg2l_du_vsp_complete,
		.callback_data = crtc,
	};

	vsp1_du_setup_lif(crtc->vsp->vsp, crtc->vsp_pipe, &cfg);
}

void rzg2l_du_vsp_disable(struct rzg2l_du_crtc *crtc)
{
	vsp1_du_setup_lif(crtc->vsp->vsp, crtc->vsp_pipe, NULL);
}

void rzg2l_du_vsp_atomic_flush(struct rzg2l_du_crtc *crtc)
{
	struct vsp1_du_atomic_pipe_config cfg = { { 0, } };

	vsp1_du_atomic_flush(crtc->vsp->vsp, crtc->vsp_pipe, &cfg);
}

struct drm_plane *rzg2l_du_vsp_get_drm_plane(struct rzg2l_du_crtc *crtc,
					     unsigned int pipe_index)
{
	struct rzg2l_du_device *rcdu = crtc->vsp->dev;
	struct drm_plane *plane = NULL;

	drm_for_each_plane(plane, &rcdu->ddev) {
		struct rzg2l_du_vsp_plane *vsp_plane = to_rzg2l_vsp_plane(plane);

		if (vsp_plane->index == pipe_index)
			break;
	}

	return plane ? plane : ERR_PTR(-EINVAL);
}

static const u32 rzg2l_du_vsp_formats[] = {
	DRM_FORMAT_RGB332,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YVU444,
};

static void rzg2l_du_vsp_plane_setup(struct rzg2l_du_vsp_plane *plane)
{
	struct rzg2l_du_vsp_plane_state *state =
		to_rzg2l_vsp_plane_state(plane->plane.state);
	struct rzg2l_du_crtc *crtc = to_rzg2l_crtc(state->state.crtc);
	struct drm_framebuffer *fb = plane->plane.state->fb;
	const struct rzg2l_du_format_info *format;
	struct vsp1_du_atomic_config cfg = {
		.pixelformat = 0,
		.pitch = fb->pitches[0],
		.alpha = state->state.alpha >> 8,
		.zpos = state->state.zpos,
	};
	u32 fourcc = state->format->fourcc;
	unsigned int i;

	cfg.src.left = state->state.src.x1 >> 16;
	cfg.src.top = state->state.src.y1 >> 16;
	cfg.src.width = drm_rect_width(&state->state.src) >> 16;
	cfg.src.height = drm_rect_height(&state->state.src) >> 16;

	cfg.dst.left = state->state.dst.x1;
	cfg.dst.top = state->state.dst.y1;
	cfg.dst.width = drm_rect_width(&state->state.dst);
	cfg.dst.height = drm_rect_height(&state->state.dst);

	for (i = 0; i < state->format->planes; ++i) {
		struct drm_gem_dma_object *gem;

		gem = drm_fb_dma_get_gem_obj(fb, i);
		cfg.mem[i] = gem->dma_addr + fb->offsets[i];
	}

	if (state->state.pixel_blend_mode == DRM_MODE_BLEND_PIXEL_NONE) {
		switch (fourcc) {
		case DRM_FORMAT_ARGB1555:
			fourcc = DRM_FORMAT_XRGB1555;
			break;

		case DRM_FORMAT_ARGB4444:
			fourcc = DRM_FORMAT_XRGB4444;
			break;

		case DRM_FORMAT_ARGB8888:
			fourcc = DRM_FORMAT_XRGB8888;
			break;
		}
	}

	format = rzg2l_du_format_info(fourcc);
	cfg.pixelformat = format->v4l2;

	cfg.premult = state->state.pixel_blend_mode == DRM_MODE_BLEND_PREMULTI;

	vsp1_du_atomic_update(plane->vsp->vsp, crtc->vsp_pipe,
			      plane->index, &cfg);
}

static int __rzg2l_du_vsp_plane_atomic_check(struct drm_plane *plane,
					     struct drm_plane_state *state,
					     const struct rzg2l_du_format_info **format)
{
	struct drm_crtc_state *crtc_state;
	int ret;

	if (!state->crtc) {
		/*
		 * The visible field is not reset by the DRM core but only
		 * updated by drm_atomic_helper_check_plane_state, set it
		 * manually.
		 */
		state->visible = false;
		*format = NULL;
		return 0;
	}

	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  true, true);
	if (ret < 0)
		return ret;

	if (!state->visible) {
		*format = NULL;
		return 0;
	}

	*format = rzg2l_du_format_info(state->fb->format->format);

	return 0;
}

static int rzg2l_du_vsp_plane_atomic_check(struct drm_plane *plane,
					   struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct rzg2l_du_vsp_plane_state *rstate = to_rzg2l_vsp_plane_state(new_plane_state);

	return __rzg2l_du_vsp_plane_atomic_check(plane, new_plane_state, &rstate->format);
}

static void rzg2l_du_vsp_plane_atomic_update(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state, plane);
	struct rzg2l_du_vsp_plane *rplane = to_rzg2l_vsp_plane(plane);
	struct rzg2l_du_crtc *crtc = to_rzg2l_crtc(old_state->crtc);

	if (new_state->visible)
		rzg2l_du_vsp_plane_setup(rplane);
	else if (old_state->crtc)
		vsp1_du_atomic_update(rplane->vsp->vsp, crtc->vsp_pipe,
				      rplane->index, NULL);
}

static const struct drm_plane_helper_funcs rzg2l_du_vsp_plane_helper_funcs = {
	.atomic_check = rzg2l_du_vsp_plane_atomic_check,
	.atomic_update = rzg2l_du_vsp_plane_atomic_update,
};

static struct drm_plane_state *
rzg2l_du_vsp_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct rzg2l_du_vsp_plane_state *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	copy = kzalloc(sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);

	return &copy->state;
}

static void rzg2l_du_vsp_plane_atomic_destroy_state(struct drm_plane *plane,
						    struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_rzg2l_vsp_plane_state(state));
}

static void rzg2l_du_vsp_plane_reset(struct drm_plane *plane)
{
	struct rzg2l_du_vsp_plane_state *state;

	if (plane->state) {
		rzg2l_du_vsp_plane_atomic_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return;

	__drm_atomic_helper_plane_reset(plane, &state->state);
}

static const struct drm_plane_funcs rzg2l_du_vsp_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = rzg2l_du_vsp_plane_reset,
	.atomic_duplicate_state = rzg2l_du_vsp_plane_atomic_duplicate_state,
	.atomic_destroy_state = rzg2l_du_vsp_plane_atomic_destroy_state,
};

static void rzg2l_du_vsp_cleanup(struct drm_device *dev, void *res)
{
	struct rzg2l_du_vsp *vsp = res;

	put_device(vsp->vsp);
}

int rzg2l_du_vsp_init(struct rzg2l_du_vsp *vsp, struct device_node *np,
		      unsigned int crtcs)
{
	struct rzg2l_du_device *rcdu = vsp->dev;
	struct platform_device *pdev;
	unsigned int num_crtcs = hweight32(crtcs);
	unsigned int num_planes = 2;
	unsigned int i;
	int ret;

	/* Find the VSP device and initialize it. */
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return -ENXIO;

	vsp->vsp = &pdev->dev;

	ret = drmm_add_action_or_reset(&rcdu->ddev, rzg2l_du_vsp_cleanup, vsp);
	if (ret < 0)
		return ret;

	ret = vsp1_du_init(vsp->vsp);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_planes; ++i) {
		enum drm_plane_type type = i < num_crtcs
					 ? DRM_PLANE_TYPE_PRIMARY
					 : DRM_PLANE_TYPE_OVERLAY;
		struct rzg2l_du_vsp_plane *plane;

		plane = drmm_universal_plane_alloc(&rcdu->ddev, struct rzg2l_du_vsp_plane,
						   plane, crtcs, &rzg2l_du_vsp_plane_funcs,
						   rzg2l_du_vsp_formats,
						   ARRAY_SIZE(rzg2l_du_vsp_formats),
						   NULL, type, NULL);
		if (IS_ERR(plane))
			return PTR_ERR(plane);

		plane->vsp = vsp;
		plane->index = i;

		drm_plane_helper_add(&plane->plane,
				     &rzg2l_du_vsp_plane_helper_funcs);
	}

	return 0;
}
