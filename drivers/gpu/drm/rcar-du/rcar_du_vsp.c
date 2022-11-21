// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_du_vsp.h  --  R-Car Display Unit VSP-Based Compositor
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/vsp1.h>

#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_vsp.h"
#include "rcar_du_writeback.h"

static void rcar_du_vsp_complete(void *private, unsigned int status, u32 crc)
{
	struct rcar_du_crtc *crtc = private;

	if (crtc->vblank_enable)
		drm_crtc_handle_vblank(&crtc->crtc);

	if (status & VSP1_DU_STATUS_COMPLETE)
		rcar_du_crtc_finish_page_flip(crtc);
	if (status & VSP1_DU_STATUS_WRITEBACK)
		rcar_du_writeback_complete(crtc);

	drm_crtc_add_crc_entry(&crtc->crtc, false, 0, &crc);
}

void rcar_du_vsp_enable(struct rcar_du_crtc *crtc)
{
	const struct drm_display_mode *mode = &crtc->crtc.state->adjusted_mode;
	struct rcar_du_device *rcdu = crtc->dev;
	struct vsp1_du_lif_config cfg = {
		.width = mode->hdisplay,
		.height = mode->vdisplay,
		.interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE,
		.callback = rcar_du_vsp_complete,
		.callback_data = crtc,
	};
	struct rcar_du_plane_state state = {
		.state = {
			.alpha = DRM_BLEND_ALPHA_OPAQUE,
			.crtc = &crtc->crtc,
			.dst.x1 = 0,
			.dst.y1 = 0,
			.dst.x2 = mode->hdisplay,
			.dst.y2 = mode->vdisplay,
			.src.x1 = 0,
			.src.y1 = 0,
			.src.x2 = mode->hdisplay << 16,
			.src.y2 = mode->vdisplay << 16,
			.zpos = 0,
		},
		.format = rcar_du_format_info(DRM_FORMAT_ARGB8888),
		.source = RCAR_DU_PLANE_VSPD1,
		.colorkey = 0,
	};

	if (rcdu->info->gen >= 3)
		state.hwindex = (crtc->index % 2) ? 2 : 0;
	else
		state.hwindex = crtc->index % 2;

	__rcar_du_plane_setup(crtc->group, &state);

	vsp1_du_setup_lif(crtc->vsp->vsp, crtc->vsp_pipe, &cfg);
}

void rcar_du_vsp_disable(struct rcar_du_crtc *crtc)
{
	vsp1_du_setup_lif(crtc->vsp->vsp, crtc->vsp_pipe, NULL);
}

void rcar_du_vsp_atomic_begin(struct rcar_du_crtc *crtc)
{
	vsp1_du_atomic_begin(crtc->vsp->vsp, crtc->vsp_pipe);
}

void rcar_du_vsp_atomic_flush(struct rcar_du_crtc *crtc)
{
	struct vsp1_du_atomic_pipe_config cfg = { { 0, } };
	struct rcar_du_crtc_state *state;

	state = to_rcar_crtc_state(crtc->crtc.state);
	cfg.crc = state->crc;

	rcar_du_writeback_setup(crtc, &cfg.writeback);

	vsp1_du_atomic_flush(crtc->vsp->vsp, crtc->vsp_pipe, &cfg);
}

static const u32 rcar_du_vsp_formats[] = {
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

static void rcar_du_vsp_plane_setup(struct rcar_du_vsp_plane *plane)
{
	struct rcar_du_vsp_plane_state *state =
		to_rcar_vsp_plane_state(plane->plane.state);
	struct rcar_du_crtc *crtc = to_rcar_crtc(state->state.crtc);
	struct drm_framebuffer *fb = plane->plane.state->fb;
	const struct rcar_du_format_info *format;
	struct vsp1_du_atomic_config cfg = {
		.pixelformat = 0,
		.pitch = fb->pitches[0],
		.alpha = state->state.alpha >> 8,
		.zpos = state->state.zpos,
	};
	unsigned int i;

	cfg.src.left = state->state.src.x1 >> 16;
	cfg.src.top = state->state.src.y1 >> 16;
	cfg.src.width = drm_rect_width(&state->state.src) >> 16;
	cfg.src.height = drm_rect_height(&state->state.src) >> 16;

	cfg.dst.left = state->state.dst.x1;
	cfg.dst.top = state->state.dst.y1;
	cfg.dst.width = drm_rect_width(&state->state.dst);
	cfg.dst.height = drm_rect_height(&state->state.dst);

	for (i = 0; i < state->format->planes; ++i)
		cfg.mem[i] = sg_dma_address(state->sg_tables[i].sgl)
			   + fb->offsets[i];

	format = rcar_du_format_info(state->format->fourcc);
	cfg.pixelformat = format->v4l2;

	vsp1_du_atomic_update(plane->vsp->vsp, crtc->vsp_pipe,
			      plane->index, &cfg);
}

int rcar_du_vsp_map_fb(struct rcar_du_vsp *vsp, struct drm_framebuffer *fb,
		       struct sg_table sg_tables[3])
{
	struct rcar_du_device *rcdu = vsp->dev;
	unsigned int i, j;
	int ret;

	for (i = 0; i < fb->format->num_planes; ++i) {
		struct drm_gem_cma_object *gem = drm_fb_cma_get_gem_obj(fb, i);
		struct sg_table *sgt = &sg_tables[i];

		if (gem->sgt) {
			struct scatterlist *src;
			struct scatterlist *dst;

			/*
			 * If the GEM buffer has a scatter gather table, it has
			 * been imported from a dma-buf and has no physical
			 * address as it might not be physically contiguous.
			 * Copy the original scatter gather table to map it to
			 * the VSP.
			 */
			ret = sg_alloc_table(sgt, gem->sgt->orig_nents,
					     GFP_KERNEL);
			if (ret)
				goto fail;

			src = gem->sgt->sgl;
			dst = sgt->sgl;
			for (j = 0; j < gem->sgt->orig_nents; ++j) {
				sg_set_page(dst, sg_page(src), src->length,
					    src->offset);
				src = sg_next(src);
				dst = sg_next(dst);
			}
		} else {
			ret = dma_get_sgtable(rcdu->dev, sgt, gem->vaddr,
					      gem->paddr, gem->base.size);
			if (ret)
				goto fail;
		}

		ret = vsp1_du_map_sg(vsp->vsp, sgt);
		if (ret) {
			sg_free_table(sgt);
			goto fail;
		}
	}

	return 0;

fail:
	while (i--) {
		struct sg_table *sgt = &sg_tables[i];

		vsp1_du_unmap_sg(vsp->vsp, sgt);
		sg_free_table(sgt);
	}

	return ret;
}

static int rcar_du_vsp_plane_prepare_fb(struct drm_plane *plane,
					struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_vsp *vsp = to_rcar_vsp_plane(plane)->vsp;
	int ret;

	/*
	 * There's no need to prepare (and unprepare) the framebuffer when the
	 * plane is not visible, as it will not be displayed.
	 */
	if (!state->visible)
		return 0;

	ret = rcar_du_vsp_map_fb(vsp, state->fb, rstate->sg_tables);
	if (ret < 0)
		return ret;

	return drm_gem_plane_helper_prepare_fb(plane, state);
}

void rcar_du_vsp_unmap_fb(struct rcar_du_vsp *vsp, struct drm_framebuffer *fb,
			  struct sg_table sg_tables[3])
{
	unsigned int i;

	for (i = 0; i < fb->format->num_planes; ++i) {
		struct sg_table *sgt = &sg_tables[i];

		vsp1_du_unmap_sg(vsp->vsp, sgt);
		sg_free_table(sgt);
	}
}

static void rcar_du_vsp_plane_cleanup_fb(struct drm_plane *plane,
					 struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_vsp *vsp = to_rcar_vsp_plane(plane)->vsp;

	if (!state->visible)
		return;

	rcar_du_vsp_unmap_fb(vsp, state->fb, rstate->sg_tables);
}

static int rcar_du_vsp_plane_atomic_check(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(new_plane_state);

	return __rcar_du_plane_atomic_check(plane, new_plane_state,
					    &rstate->format);
}

static void rcar_du_vsp_plane_atomic_update(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state, plane);
	struct rcar_du_vsp_plane *rplane = to_rcar_vsp_plane(plane);
	struct rcar_du_crtc *crtc = to_rcar_crtc(old_state->crtc);

	if (new_state->visible)
		rcar_du_vsp_plane_setup(rplane);
	else if (old_state->crtc)
		vsp1_du_atomic_update(rplane->vsp->vsp, crtc->vsp_pipe,
				      rplane->index, NULL);
}

static const struct drm_plane_helper_funcs rcar_du_vsp_plane_helper_funcs = {
	.prepare_fb = rcar_du_vsp_plane_prepare_fb,
	.cleanup_fb = rcar_du_vsp_plane_cleanup_fb,
	.atomic_check = rcar_du_vsp_plane_atomic_check,
	.atomic_update = rcar_du_vsp_plane_atomic_update,
};

static struct drm_plane_state *
rcar_du_vsp_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct rcar_du_vsp_plane_state *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	copy = kzalloc(sizeof(*copy), GFP_KERNEL);
	if (copy == NULL)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);

	return &copy->state;
}

static void rcar_du_vsp_plane_atomic_destroy_state(struct drm_plane *plane,
						   struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_rcar_vsp_plane_state(state));
}

static void rcar_du_vsp_plane_reset(struct drm_plane *plane)
{
	struct rcar_du_vsp_plane_state *state;

	if (plane->state) {
		rcar_du_vsp_plane_atomic_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	__drm_atomic_helper_plane_reset(plane, &state->state);
}

static const struct drm_plane_funcs rcar_du_vsp_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = rcar_du_vsp_plane_reset,
	.destroy = drm_plane_cleanup,
	.atomic_duplicate_state = rcar_du_vsp_plane_atomic_duplicate_state,
	.atomic_destroy_state = rcar_du_vsp_plane_atomic_destroy_state,
};

static void rcar_du_vsp_cleanup(struct drm_device *dev, void *res)
{
	struct rcar_du_vsp *vsp = res;
	unsigned int i;

	for (i = 0; i < vsp->num_planes; ++i) {
		struct rcar_du_vsp_plane *plane = &vsp->planes[i];

		drm_plane_cleanup(&plane->plane);
	}

	kfree(vsp->planes);

	put_device(vsp->vsp);
}

int rcar_du_vsp_init(struct rcar_du_vsp *vsp, struct device_node *np,
		     unsigned int crtcs)
{
	struct rcar_du_device *rcdu = vsp->dev;
	struct platform_device *pdev;
	unsigned int num_crtcs = hweight32(crtcs);
	unsigned int num_planes;
	unsigned int i;
	int ret;

	/* Find the VSP device and initialize it. */
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return -ENXIO;

	vsp->vsp = &pdev->dev;

	ret = drmm_add_action_or_reset(&rcdu->ddev, rcar_du_vsp_cleanup, vsp);
	if (ret < 0)
		return ret;

	ret = vsp1_du_init(vsp->vsp);
	if (ret < 0)
		return ret;

	 /*
	  * The VSP2D (Gen3) has 5 RPFs, but the VSP1D (Gen2) is limited to
	  * 4 RPFs.
	  */
	num_planes = rcdu->info->gen >= 3 ? 5 : 4;

	vsp->planes = kcalloc(num_planes, sizeof(*vsp->planes), GFP_KERNEL);
	if (!vsp->planes)
		return -ENOMEM;

	for (i = 0; i < num_planes; ++i) {
		enum drm_plane_type type = i < num_crtcs
					 ? DRM_PLANE_TYPE_PRIMARY
					 : DRM_PLANE_TYPE_OVERLAY;
		struct rcar_du_vsp_plane *plane = &vsp->planes[i];

		plane->vsp = vsp;
		plane->index = i;

		ret = drm_universal_plane_init(&rcdu->ddev, &plane->plane,
					       crtcs, &rcar_du_vsp_plane_funcs,
					       rcar_du_vsp_formats,
					       ARRAY_SIZE(rcar_du_vsp_formats),
					       NULL, type, NULL);
		if (ret < 0)
			return ret;

		drm_plane_helper_add(&plane->plane,
				     &rcar_du_vsp_plane_helper_funcs);

		if (type == DRM_PLANE_TYPE_PRIMARY) {
			drm_plane_create_zpos_immutable_property(&plane->plane,
								 0);
		} else {
			drm_plane_create_alpha_property(&plane->plane);
			drm_plane_create_zpos_property(&plane->plane, 1, 1,
						       num_planes - 1);
		}

		vsp->num_planes++;
	}

	return 0;
}
