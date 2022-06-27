// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Broadcom
 */

/**
 * DOC: VC4 plane module
 *
 * Each DRM plane is a layer of pixels being scanned out by the HVS.
 *
 * At atomic modeset check time, we compute the HVS display element
 * state that would be necessary for displaying the plane (giving us a
 * chance to figure out if a plane configuration is invalid), then at
 * atomic flush time the CRTC will ask us to write our element state
 * into the region of the HVS that it has allocated for us.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include "uapi/drm/vc4_drm.h"

#include "vc4_drv.h"
#include "vc4_regs.h"

static const struct hvs_format {
	u32 drm; /* DRM_FORMAT_* */
	u32 hvs; /* HVS_FORMAT_* */
	u32 pixel_order;
	u32 pixel_order_hvs5;
	bool hvs5_only;
} hvs_formats[] = {
	{
		.drm = DRM_FORMAT_XRGB8888,
		.hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
	},
	{
		.drm = DRM_FORMAT_ARGB8888,
		.hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
	},
	{
		.drm = DRM_FORMAT_ABGR8888,
		.hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ARGB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ABGR,
	},
	{
		.drm = DRM_FORMAT_XBGR8888,
		.hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ARGB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ABGR,
	},
	{
		.drm = DRM_FORMAT_RGB565,
		.hvs = HVS_PIXEL_FORMAT_RGB565,
		.pixel_order = HVS_PIXEL_ORDER_XRGB,
	},
	{
		.drm = DRM_FORMAT_BGR565,
		.hvs = HVS_PIXEL_FORMAT_RGB565,
		.pixel_order = HVS_PIXEL_ORDER_XBGR,
	},
	{
		.drm = DRM_FORMAT_ARGB1555,
		.hvs = HVS_PIXEL_FORMAT_RGBA5551,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
	},
	{
		.drm = DRM_FORMAT_XRGB1555,
		.hvs = HVS_PIXEL_FORMAT_RGBA5551,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
	},
	{
		.drm = DRM_FORMAT_RGB888,
		.hvs = HVS_PIXEL_FORMAT_RGB888,
		.pixel_order = HVS_PIXEL_ORDER_XRGB,
	},
	{
		.drm = DRM_FORMAT_BGR888,
		.hvs = HVS_PIXEL_FORMAT_RGB888,
		.pixel_order = HVS_PIXEL_ORDER_XBGR,
	},
	{
		.drm = DRM_FORMAT_YUV422,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_YVU422,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_YUV420,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV420_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_YVU420,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV420_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_NV12,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_NV21,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_NV16,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_NV61,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_P030,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_10BIT,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
		.hvs5_only = true,
	},
};

static const struct hvs_format *vc4_get_hvs_format(u32 drm_format)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(hvs_formats); i++) {
		if (hvs_formats[i].drm == drm_format)
			return &hvs_formats[i];
	}

	return NULL;
}

static enum vc4_scaling_mode vc4_get_scaling_mode(u32 src, u32 dst)
{
	if (dst == src)
		return VC4_SCALING_NONE;
	if (3 * dst >= 2 * src)
		return VC4_SCALING_PPF;
	else
		return VC4_SCALING_TPZ;
}

static bool plane_enabled(struct drm_plane_state *state)
{
	return state->fb && !WARN_ON(!state->crtc);
}

static struct drm_plane_state *vc4_plane_duplicate_state(struct drm_plane *plane)
{
	struct vc4_plane_state *vc4_state;

	if (WARN_ON(!plane->state))
		return NULL;

	vc4_state = kmemdup(plane->state, sizeof(*vc4_state), GFP_KERNEL);
	if (!vc4_state)
		return NULL;

	memset(&vc4_state->lbm, 0, sizeof(vc4_state->lbm));
	vc4_state->dlist_initialized = 0;

	__drm_atomic_helper_plane_duplicate_state(plane, &vc4_state->base);

	if (vc4_state->dlist) {
		vc4_state->dlist = kmemdup(vc4_state->dlist,
					   vc4_state->dlist_count * 4,
					   GFP_KERNEL);
		if (!vc4_state->dlist) {
			kfree(vc4_state);
			return NULL;
		}
		vc4_state->dlist_size = vc4_state->dlist_count;
	}

	return &vc4_state->base;
}

static void vc4_plane_destroy_state(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct vc4_dev *vc4 = to_vc4_dev(plane->dev);
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	if (drm_mm_node_allocated(&vc4_state->lbm)) {
		unsigned long irqflags;

		spin_lock_irqsave(&vc4->hvs->mm_lock, irqflags);
		drm_mm_remove_node(&vc4_state->lbm);
		spin_unlock_irqrestore(&vc4->hvs->mm_lock, irqflags);
	}

	kfree(vc4_state->dlist);
	__drm_atomic_helper_plane_destroy_state(&vc4_state->base);
	kfree(state);
}

/* Called during init to allocate the plane's atomic state. */
static void vc4_plane_reset(struct drm_plane *plane)
{
	struct vc4_plane_state *vc4_state;

	WARN_ON(plane->state);

	vc4_state = kzalloc(sizeof(*vc4_state), GFP_KERNEL);
	if (!vc4_state)
		return;

	__drm_atomic_helper_plane_reset(plane, &vc4_state->base);
}

static void vc4_dlist_counter_increment(struct vc4_plane_state *vc4_state)
{
	if (vc4_state->dlist_count == vc4_state->dlist_size) {
		u32 new_size = max(4u, vc4_state->dlist_count * 2);
		u32 *new_dlist = kmalloc_array(new_size, 4, GFP_KERNEL);

		if (!new_dlist)
			return;
		memcpy(new_dlist, vc4_state->dlist, vc4_state->dlist_count * 4);

		kfree(vc4_state->dlist);
		vc4_state->dlist = new_dlist;
		vc4_state->dlist_size = new_size;
	}

	vc4_state->dlist_count++;
}

static void vc4_dlist_write(struct vc4_plane_state *vc4_state, u32 val)
{
	unsigned int idx = vc4_state->dlist_count;

	vc4_dlist_counter_increment(vc4_state);
	vc4_state->dlist[idx] = val;
}

/* Returns the scl0/scl1 field based on whether the dimensions need to
 * be up/down/non-scaled.
 *
 * This is a replication of a table from the spec.
 */
static u32 vc4_get_scl_field(struct drm_plane_state *state, int plane)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	switch (vc4_state->x_scaling[plane] << 2 | vc4_state->y_scaling[plane]) {
	case VC4_SCALING_PPF << 2 | VC4_SCALING_PPF:
		return SCALER_CTL0_SCL_H_PPF_V_PPF;
	case VC4_SCALING_TPZ << 2 | VC4_SCALING_PPF:
		return SCALER_CTL0_SCL_H_TPZ_V_PPF;
	case VC4_SCALING_PPF << 2 | VC4_SCALING_TPZ:
		return SCALER_CTL0_SCL_H_PPF_V_TPZ;
	case VC4_SCALING_TPZ << 2 | VC4_SCALING_TPZ:
		return SCALER_CTL0_SCL_H_TPZ_V_TPZ;
	case VC4_SCALING_PPF << 2 | VC4_SCALING_NONE:
		return SCALER_CTL0_SCL_H_PPF_V_NONE;
	case VC4_SCALING_NONE << 2 | VC4_SCALING_PPF:
		return SCALER_CTL0_SCL_H_NONE_V_PPF;
	case VC4_SCALING_NONE << 2 | VC4_SCALING_TPZ:
		return SCALER_CTL0_SCL_H_NONE_V_TPZ;
	case VC4_SCALING_TPZ << 2 | VC4_SCALING_NONE:
		return SCALER_CTL0_SCL_H_TPZ_V_NONE;
	default:
	case VC4_SCALING_NONE << 2 | VC4_SCALING_NONE:
		/* The unity case is independently handled by
		 * SCALER_CTL0_UNITY.
		 */
		return 0;
	}
}

static int vc4_plane_margins_adj(struct drm_plane_state *pstate)
{
	struct vc4_plane_state *vc4_pstate = to_vc4_plane_state(pstate);
	unsigned int left, right, top, bottom, adjhdisplay, adjvdisplay;
	struct drm_crtc_state *crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(pstate->state,
						   pstate->crtc);

	vc4_crtc_get_margins(crtc_state, &left, &right, &top, &bottom);
	if (!left && !right && !top && !bottom)
		return 0;

	if (left + right >= crtc_state->mode.hdisplay ||
	    top + bottom >= crtc_state->mode.vdisplay)
		return -EINVAL;

	adjhdisplay = crtc_state->mode.hdisplay - (left + right);
	vc4_pstate->crtc_x = DIV_ROUND_CLOSEST(vc4_pstate->crtc_x *
					       adjhdisplay,
					       crtc_state->mode.hdisplay);
	vc4_pstate->crtc_x += left;
	if (vc4_pstate->crtc_x > crtc_state->mode.hdisplay - left)
		vc4_pstate->crtc_x = crtc_state->mode.hdisplay - left;

	adjvdisplay = crtc_state->mode.vdisplay - (top + bottom);
	vc4_pstate->crtc_y = DIV_ROUND_CLOSEST(vc4_pstate->crtc_y *
					       adjvdisplay,
					       crtc_state->mode.vdisplay);
	vc4_pstate->crtc_y += top;
	if (vc4_pstate->crtc_y > crtc_state->mode.vdisplay - top)
		vc4_pstate->crtc_y = crtc_state->mode.vdisplay - top;

	vc4_pstate->crtc_w = DIV_ROUND_CLOSEST(vc4_pstate->crtc_w *
					       adjhdisplay,
					       crtc_state->mode.hdisplay);
	vc4_pstate->crtc_h = DIV_ROUND_CLOSEST(vc4_pstate->crtc_h *
					       adjvdisplay,
					       crtc_state->mode.vdisplay);

	if (!vc4_pstate->crtc_w || !vc4_pstate->crtc_h)
		return -EINVAL;

	return 0;
}

static int vc4_plane_setup_clipping_and_scaling(struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *bo = drm_fb_cma_get_gem_obj(fb, 0);
	u32 subpixel_src_mask = (1 << 16) - 1;
	int num_planes = fb->format->num_planes;
	struct drm_crtc_state *crtc_state;
	u32 h_subsample = fb->format->hsub;
	u32 v_subsample = fb->format->vsub;
	int i, ret;

	crtc_state = drm_atomic_get_existing_crtc_state(state->state,
							state->crtc);
	if (!crtc_state) {
		DRM_DEBUG_KMS("Invalid crtc state\n");
		return -EINVAL;
	}

	ret = drm_atomic_helper_check_plane_state(state, crtc_state, 1,
						  INT_MAX, true, true);
	if (ret)
		return ret;

	for (i = 0; i < num_planes; i++)
		vc4_state->offsets[i] = bo->paddr + fb->offsets[i];

	/* We don't support subpixel source positioning for scaling. */
	if ((state->src.x1 & subpixel_src_mask) ||
	    (state->src.x2 & subpixel_src_mask) ||
	    (state->src.y1 & subpixel_src_mask) ||
	    (state->src.y2 & subpixel_src_mask)) {
		return -EINVAL;
	}

	vc4_state->src_x = state->src.x1 >> 16;
	vc4_state->src_y = state->src.y1 >> 16;
	vc4_state->src_w[0] = (state->src.x2 - state->src.x1) >> 16;
	vc4_state->src_h[0] = (state->src.y2 - state->src.y1) >> 16;

	vc4_state->crtc_x = state->dst.x1;
	vc4_state->crtc_y = state->dst.y1;
	vc4_state->crtc_w = state->dst.x2 - state->dst.x1;
	vc4_state->crtc_h = state->dst.y2 - state->dst.y1;

	ret = vc4_plane_margins_adj(state);
	if (ret)
		return ret;

	vc4_state->x_scaling[0] = vc4_get_scaling_mode(vc4_state->src_w[0],
						       vc4_state->crtc_w);
	vc4_state->y_scaling[0] = vc4_get_scaling_mode(vc4_state->src_h[0],
						       vc4_state->crtc_h);

	vc4_state->is_unity = (vc4_state->x_scaling[0] == VC4_SCALING_NONE &&
			       vc4_state->y_scaling[0] == VC4_SCALING_NONE);

	if (num_planes > 1) {
		vc4_state->is_yuv = true;

		vc4_state->src_w[1] = vc4_state->src_w[0] / h_subsample;
		vc4_state->src_h[1] = vc4_state->src_h[0] / v_subsample;

		vc4_state->x_scaling[1] =
			vc4_get_scaling_mode(vc4_state->src_w[1],
					     vc4_state->crtc_w);
		vc4_state->y_scaling[1] =
			vc4_get_scaling_mode(vc4_state->src_h[1],
					     vc4_state->crtc_h);

		/* YUV conversion requires that horizontal scaling be enabled
		 * on the UV plane even if vc4_get_scaling_mode() returned
		 * VC4_SCALING_NONE (which can happen when the down-scaling
		 * ratio is 0.5). Let's force it to VC4_SCALING_PPF in this
		 * case.
		 */
		if (vc4_state->x_scaling[1] == VC4_SCALING_NONE)
			vc4_state->x_scaling[1] = VC4_SCALING_PPF;
	} else {
		vc4_state->is_yuv = false;
		vc4_state->x_scaling[1] = VC4_SCALING_NONE;
		vc4_state->y_scaling[1] = VC4_SCALING_NONE;
	}

	return 0;
}

static void vc4_write_tpz(struct vc4_plane_state *vc4_state, u32 src, u32 dst)
{
	u32 scale, recip;

	scale = (1 << 16) * src / dst;

	/* The specs note that while the reciprocal would be defined
	 * as (1<<32)/scale, ~0 is close enough.
	 */
	recip = ~0 / scale;

	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(scale, SCALER_TPZ0_SCALE) |
			VC4_SET_FIELD(0, SCALER_TPZ0_IPHASE));
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(recip, SCALER_TPZ1_RECIP));
}

static void vc4_write_ppf(struct vc4_plane_state *vc4_state, u32 src, u32 dst)
{
	u32 scale = (1 << 16) * src / dst;

	vc4_dlist_write(vc4_state,
			SCALER_PPF_AGC |
			VC4_SET_FIELD(scale, SCALER_PPF_SCALE) |
			VC4_SET_FIELD(0, SCALER_PPF_IPHASE));
}

static u32 vc4_lbm_size(struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct vc4_dev *vc4 = to_vc4_dev(state->plane->dev);
	u32 pix_per_line;
	u32 lbm;

	/* LBM is not needed when there's no vertical scaling. */
	if (vc4_state->y_scaling[0] == VC4_SCALING_NONE &&
	    vc4_state->y_scaling[1] == VC4_SCALING_NONE)
		return 0;

	/*
	 * This can be further optimized in the RGB/YUV444 case if the PPF
	 * decimation factor is between 0.5 and 1.0 by using crtc_w.
	 *
	 * It's not an issue though, since in that case since src_w[0] is going
	 * to be greater than or equal to crtc_w.
	 */
	if (vc4_state->x_scaling[0] == VC4_SCALING_TPZ)
		pix_per_line = vc4_state->crtc_w;
	else
		pix_per_line = vc4_state->src_w[0];

	if (!vc4_state->is_yuv) {
		if (vc4_state->y_scaling[0] == VC4_SCALING_TPZ)
			lbm = pix_per_line * 8;
		else {
			/* In special cases, this multiplier might be 12. */
			lbm = pix_per_line * 16;
		}
	} else {
		/* There are cases for this going down to a multiplier
		 * of 2, but according to the firmware source, the
		 * table in the docs is somewhat wrong.
		 */
		lbm = pix_per_line * 16;
	}

	/* Align it to 64 or 128 (hvs5) bytes */
	lbm = roundup(lbm, vc4->is_vc5 ? 128 : 64);

	/* Each "word" of the LBM memory contains 2 or 4 (hvs5) pixels */
	lbm /= vc4->is_vc5 ? 4 : 2;

	return lbm;
}

static void vc4_write_scaling_parameters(struct drm_plane_state *state,
					 int channel)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	/* Ch0 H-PPF Word 0: Scaling Parameters */
	if (vc4_state->x_scaling[channel] == VC4_SCALING_PPF) {
		vc4_write_ppf(vc4_state,
			      vc4_state->src_w[channel], vc4_state->crtc_w);
	}

	/* Ch0 V-PPF Words 0-1: Scaling Parameters, Context */
	if (vc4_state->y_scaling[channel] == VC4_SCALING_PPF) {
		vc4_write_ppf(vc4_state,
			      vc4_state->src_h[channel], vc4_state->crtc_h);
		vc4_dlist_write(vc4_state, 0xc0c0c0c0);
	}

	/* Ch0 H-TPZ Words 0-1: Scaling Parameters, Recip */
	if (vc4_state->x_scaling[channel] == VC4_SCALING_TPZ) {
		vc4_write_tpz(vc4_state,
			      vc4_state->src_w[channel], vc4_state->crtc_w);
	}

	/* Ch0 V-TPZ Words 0-2: Scaling Parameters, Recip, Context */
	if (vc4_state->y_scaling[channel] == VC4_SCALING_TPZ) {
		vc4_write_tpz(vc4_state,
			      vc4_state->src_h[channel], vc4_state->crtc_h);
		vc4_dlist_write(vc4_state, 0xc0c0c0c0);
	}
}

static void vc4_plane_calc_load(struct drm_plane_state *state)
{
	unsigned int hvs_load_shift, vrefresh, i;
	struct drm_framebuffer *fb = state->fb;
	struct vc4_plane_state *vc4_state;
	struct drm_crtc_state *crtc_state;
	unsigned int vscale_factor;

	vc4_state = to_vc4_plane_state(state);
	crtc_state = drm_atomic_get_existing_crtc_state(state->state,
							state->crtc);
	vrefresh = drm_mode_vrefresh(&crtc_state->adjusted_mode);

	/* The HVS is able to process 2 pixels/cycle when scaling the source,
	 * 4 pixels/cycle otherwise.
	 * Alpha blending step seems to be pipelined and it's always operating
	 * at 4 pixels/cycle, so the limiting aspect here seems to be the
	 * scaler block.
	 * HVS load is expressed in clk-cycles/sec (AKA Hz).
	 */
	if (vc4_state->x_scaling[0] != VC4_SCALING_NONE ||
	    vc4_state->x_scaling[1] != VC4_SCALING_NONE ||
	    vc4_state->y_scaling[0] != VC4_SCALING_NONE ||
	    vc4_state->y_scaling[1] != VC4_SCALING_NONE)
		hvs_load_shift = 1;
	else
		hvs_load_shift = 2;

	vc4_state->membus_load = 0;
	vc4_state->hvs_load = 0;
	for (i = 0; i < fb->format->num_planes; i++) {
		/* Even if the bandwidth/plane required for a single frame is
		 *
		 * vc4_state->src_w[i] * vc4_state->src_h[i] * cpp * vrefresh
		 *
		 * when downscaling, we have to read more pixels per line in
		 * the time frame reserved for a single line, so the bandwidth
		 * demand can be punctually higher. To account for that, we
		 * calculate the down-scaling factor and multiply the plane
		 * load by this number. We're likely over-estimating the read
		 * demand, but that's better than under-estimating it.
		 */
		vscale_factor = DIV_ROUND_UP(vc4_state->src_h[i],
					     vc4_state->crtc_h);
		vc4_state->membus_load += vc4_state->src_w[i] *
					  vc4_state->src_h[i] * vscale_factor *
					  fb->format->cpp[i];
		vc4_state->hvs_load += vc4_state->crtc_h * vc4_state->crtc_w;
	}

	vc4_state->hvs_load *= vrefresh;
	vc4_state->hvs_load >>= hvs_load_shift;
	vc4_state->membus_load *= vrefresh;
}

static int vc4_plane_allocate_lbm(struct drm_plane_state *state)
{
	struct vc4_dev *vc4 = to_vc4_dev(state->plane->dev);
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	unsigned long irqflags;
	u32 lbm_size;

	lbm_size = vc4_lbm_size(state);
	if (!lbm_size)
		return 0;

	if (WARN_ON(!vc4_state->lbm_offset))
		return -EINVAL;

	/* Allocate the LBM memory that the HVS will use for temporary
	 * storage due to our scaling/format conversion.
	 */
	if (!drm_mm_node_allocated(&vc4_state->lbm)) {
		int ret;

		spin_lock_irqsave(&vc4->hvs->mm_lock, irqflags);
		ret = drm_mm_insert_node_generic(&vc4->hvs->lbm_mm,
						 &vc4_state->lbm,
						 lbm_size,
						 vc4->is_vc5 ? 64 : 32,
						 0, 0);
		spin_unlock_irqrestore(&vc4->hvs->mm_lock, irqflags);

		if (ret)
			return ret;
	} else {
		WARN_ON_ONCE(lbm_size != vc4_state->lbm.size);
	}

	vc4_state->dlist[vc4_state->lbm_offset] = vc4_state->lbm.start;

	return 0;
}

/*
 * The colorspace conversion matrices are held in 3 entries in the dlist.
 * Create an array of them, with entries for each full and limited mode, and
 * each supported colorspace.
 */
static const u32 colorspace_coeffs[2][DRM_COLOR_ENCODING_MAX][3] = {
	{
		/* Limited range */
		{
			/* BT601 */
			SCALER_CSC0_ITR_R_601_5,
			SCALER_CSC1_ITR_R_601_5,
			SCALER_CSC2_ITR_R_601_5,
		}, {
			/* BT709 */
			SCALER_CSC0_ITR_R_709_3,
			SCALER_CSC1_ITR_R_709_3,
			SCALER_CSC2_ITR_R_709_3,
		}, {
			/* BT2020 */
			SCALER_CSC0_ITR_R_2020,
			SCALER_CSC1_ITR_R_2020,
			SCALER_CSC2_ITR_R_2020,
		}
	}, {
		/* Full range */
		{
			/* JFIF */
			SCALER_CSC0_JPEG_JFIF,
			SCALER_CSC1_JPEG_JFIF,
			SCALER_CSC2_JPEG_JFIF,
		}, {
			/* BT709 */
			SCALER_CSC0_ITR_R_709_3_FR,
			SCALER_CSC1_ITR_R_709_3_FR,
			SCALER_CSC2_ITR_R_709_3_FR,
		}, {
			/* BT2020 */
			SCALER_CSC0_ITR_R_2020_FR,
			SCALER_CSC1_ITR_R_2020_FR,
			SCALER_CSC2_ITR_R_2020_FR,
		}
	}
};

/* Writes out a full display list for an active plane to the plane's
 * private dlist state.
 */
static int vc4_plane_mode_set(struct drm_plane *plane,
			      struct drm_plane_state *state)
{
	struct vc4_dev *vc4 = to_vc4_dev(plane->dev);
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	u32 ctl0_offset = vc4_state->dlist_count;
	const struct hvs_format *format = vc4_get_hvs_format(fb->format->format);
	u64 base_format_mod = fourcc_mod_broadcom_mod(fb->modifier);
	int num_planes = fb->format->num_planes;
	u32 h_subsample = fb->format->hsub;
	u32 v_subsample = fb->format->vsub;
	bool mix_plane_alpha;
	bool covers_screen;
	u32 scl0, scl1, pitch0;
	u32 tiling, src_y;
	u32 hvs_format = format->hvs;
	unsigned int rotation;
	int ret, i;

	if (vc4_state->dlist_initialized)
		return 0;

	ret = vc4_plane_setup_clipping_and_scaling(state);
	if (ret)
		return ret;

	/* SCL1 is used for Cb/Cr scaling of planar formats.  For RGB
	 * and 4:4:4, scl1 should be set to scl0 so both channels of
	 * the scaler do the same thing.  For YUV, the Y plane needs
	 * to be put in channel 1 and Cb/Cr in channel 0, so we swap
	 * the scl fields here.
	 */
	if (num_planes == 1) {
		scl0 = vc4_get_scl_field(state, 0);
		scl1 = scl0;
	} else {
		scl0 = vc4_get_scl_field(state, 1);
		scl1 = vc4_get_scl_field(state, 0);
	}

	rotation = drm_rotation_simplify(state->rotation,
					 DRM_MODE_ROTATE_0 |
					 DRM_MODE_REFLECT_X |
					 DRM_MODE_REFLECT_Y);

	/* We must point to the last line when Y reflection is enabled. */
	src_y = vc4_state->src_y;
	if (rotation & DRM_MODE_REFLECT_Y)
		src_y += vc4_state->src_h[0] - 1;

	switch (base_format_mod) {
	case DRM_FORMAT_MOD_LINEAR:
		tiling = SCALER_CTL0_TILING_LINEAR;
		pitch0 = VC4_SET_FIELD(fb->pitches[0], SCALER_SRC_PITCH);

		/* Adjust the base pointer to the first pixel to be scanned
		 * out.
		 */
		for (i = 0; i < num_planes; i++) {
			vc4_state->offsets[i] += src_y /
						 (i ? v_subsample : 1) *
						 fb->pitches[i];

			vc4_state->offsets[i] += vc4_state->src_x /
						 (i ? h_subsample : 1) *
						 fb->format->cpp[i];
		}

		break;

	case DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED: {
		u32 tile_size_shift = 12; /* T tiles are 4kb */
		/* Whole-tile offsets, mostly for setting the pitch. */
		u32 tile_w_shift = fb->format->cpp[0] == 2 ? 6 : 5;
		u32 tile_h_shift = 5; /* 16 and 32bpp are 32 pixels high */
		u32 tile_w_mask = (1 << tile_w_shift) - 1;
		/* The height mask on 32-bit-per-pixel tiles is 63, i.e. twice
		 * the height (in pixels) of a 4k tile.
		 */
		u32 tile_h_mask = (2 << tile_h_shift) - 1;
		/* For T-tiled, the FB pitch is "how many bytes from one row to
		 * the next, such that
		 *
		 *	pitch * tile_h == tile_size * tiles_per_row
		 */
		u32 tiles_w = fb->pitches[0] >> (tile_size_shift - tile_h_shift);
		u32 tiles_l = vc4_state->src_x >> tile_w_shift;
		u32 tiles_r = tiles_w - tiles_l;
		u32 tiles_t = src_y >> tile_h_shift;
		/* Intra-tile offsets, which modify the base address (the
		 * SCALER_PITCH0_TILE_Y_OFFSET tells HVS how to walk from that
		 * base address).
		 */
		u32 tile_y = (src_y >> 4) & 1;
		u32 subtile_y = (src_y >> 2) & 3;
		u32 utile_y = src_y & 3;
		u32 x_off = vc4_state->src_x & tile_w_mask;
		u32 y_off = src_y & tile_h_mask;

		/* When Y reflection is requested we must set the
		 * SCALER_PITCH0_TILE_LINE_DIR flag to tell HVS that all lines
		 * after the initial one should be fetched in descending order,
		 * which makes sense since we start from the last line and go
		 * backward.
		 * Don't know why we need y_off = max_y_off - y_off, but it's
		 * definitely required (I guess it's also related to the "going
		 * backward" situation).
		 */
		if (rotation & DRM_MODE_REFLECT_Y) {
			y_off = tile_h_mask - y_off;
			pitch0 = SCALER_PITCH0_TILE_LINE_DIR;
		} else {
			pitch0 = 0;
		}

		tiling = SCALER_CTL0_TILING_256B_OR_T;
		pitch0 |= (VC4_SET_FIELD(x_off, SCALER_PITCH0_SINK_PIX) |
			   VC4_SET_FIELD(y_off, SCALER_PITCH0_TILE_Y_OFFSET) |
			   VC4_SET_FIELD(tiles_l, SCALER_PITCH0_TILE_WIDTH_L) |
			   VC4_SET_FIELD(tiles_r, SCALER_PITCH0_TILE_WIDTH_R));
		vc4_state->offsets[0] += tiles_t * (tiles_w << tile_size_shift);
		vc4_state->offsets[0] += subtile_y << 8;
		vc4_state->offsets[0] += utile_y << 4;

		/* Rows of tiles alternate left-to-right and right-to-left. */
		if (tiles_t & 1) {
			pitch0 |= SCALER_PITCH0_TILE_INITIAL_LINE_DIR;
			vc4_state->offsets[0] += (tiles_w - tiles_l) <<
						 tile_size_shift;
			vc4_state->offsets[0] -= (1 + !tile_y) << 10;
		} else {
			vc4_state->offsets[0] += tiles_l << tile_size_shift;
			vc4_state->offsets[0] += tile_y << 10;
		}

		break;
	}

	case DRM_FORMAT_MOD_BROADCOM_SAND64:
	case DRM_FORMAT_MOD_BROADCOM_SAND128:
	case DRM_FORMAT_MOD_BROADCOM_SAND256: {
		uint32_t param = fourcc_mod_broadcom_param(fb->modifier);

		if (param > SCALER_TILE_HEIGHT_MASK) {
			DRM_DEBUG_KMS("SAND height too large (%d)\n",
				      param);
			return -EINVAL;
		}

		if (fb->format->format == DRM_FORMAT_P030) {
			hvs_format = HVS_PIXEL_FORMAT_YCBCR_10BIT;
			tiling = SCALER_CTL0_TILING_128B;
		} else {
			hvs_format = HVS_PIXEL_FORMAT_H264;

			switch (base_format_mod) {
			case DRM_FORMAT_MOD_BROADCOM_SAND64:
				tiling = SCALER_CTL0_TILING_64B;
				break;
			case DRM_FORMAT_MOD_BROADCOM_SAND128:
				tiling = SCALER_CTL0_TILING_128B;
				break;
			case DRM_FORMAT_MOD_BROADCOM_SAND256:
				tiling = SCALER_CTL0_TILING_256B_OR_T;
				break;
			default:
				return -EINVAL;
			}
		}

		/* Adjust the base pointer to the first pixel to be scanned
		 * out.
		 *
		 * For P030, y_ptr [31:4] is the 128bit word for the start pixel
		 * y_ptr [3:0] is the pixel (0-11) contained within that 128bit
		 * word that should be taken as the first pixel.
		 * Ditto uv_ptr [31:4] vs [3:0], however [3:0] contains the
		 * element within the 128bit word, eg for pixel 3 the value
		 * should be 6.
		 */
		for (i = 0; i < num_planes; i++) {
			u32 tile_w, tile, x_off, pix_per_tile;

			if (fb->format->format == DRM_FORMAT_P030) {
				/*
				 * Spec says: bits [31:4] of the given address
				 * should point to the 128-bit word containing
				 * the desired starting pixel, and bits[3:0]
				 * should be between 0 and 11, indicating which
				 * of the 12-pixels in that 128-bit word is the
				 * first pixel to be used
				 */
				u32 remaining_pixels = vc4_state->src_x % 96;
				u32 aligned = remaining_pixels / 12;
				u32 last_bits = remaining_pixels % 12;

				x_off = aligned * 16 + last_bits;
				tile_w = 128;
				pix_per_tile = 96;
			} else {
				switch (base_format_mod) {
				case DRM_FORMAT_MOD_BROADCOM_SAND64:
					tile_w = 64;
					break;
				case DRM_FORMAT_MOD_BROADCOM_SAND128:
					tile_w = 128;
					break;
				case DRM_FORMAT_MOD_BROADCOM_SAND256:
					tile_w = 256;
					break;
				default:
					return -EINVAL;
				}
				pix_per_tile = tile_w / fb->format->cpp[0];
				x_off = (vc4_state->src_x % pix_per_tile) /
					(i ? h_subsample : 1) *
					fb->format->cpp[i];
			}

			tile = vc4_state->src_x / pix_per_tile;

			vc4_state->offsets[i] += param * tile_w * tile;
			vc4_state->offsets[i] += src_y /
						 (i ? v_subsample : 1) *
						 tile_w;
			vc4_state->offsets[i] += x_off & ~(i ? 1 : 0);
		}

		pitch0 = VC4_SET_FIELD(param, SCALER_TILE_HEIGHT);
		break;
	}

	default:
		DRM_DEBUG_KMS("Unsupported FB tiling flag 0x%16llx",
			      (long long)fb->modifier);
		return -EINVAL;
	}

	/* Don't waste cycles mixing with plane alpha if the set alpha
	 * is opaque or there is no per-pixel alpha information.
	 * In any case we use the alpha property value as the fixed alpha.
	 */
	mix_plane_alpha = state->alpha != DRM_BLEND_ALPHA_OPAQUE &&
			  fb->format->has_alpha;

	if (!vc4->is_vc5) {
	/* Control word */
		vc4_dlist_write(vc4_state,
				SCALER_CTL0_VALID |
				(rotation & DRM_MODE_REFLECT_X ? SCALER_CTL0_HFLIP : 0) |
				(rotation & DRM_MODE_REFLECT_Y ? SCALER_CTL0_VFLIP : 0) |
				VC4_SET_FIELD(SCALER_CTL0_RGBA_EXPAND_ROUND, SCALER_CTL0_RGBA_EXPAND) |
				(format->pixel_order << SCALER_CTL0_ORDER_SHIFT) |
				(hvs_format << SCALER_CTL0_PIXEL_FORMAT_SHIFT) |
				VC4_SET_FIELD(tiling, SCALER_CTL0_TILING) |
				(vc4_state->is_unity ? SCALER_CTL0_UNITY : 0) |
				VC4_SET_FIELD(scl0, SCALER_CTL0_SCL0) |
				VC4_SET_FIELD(scl1, SCALER_CTL0_SCL1));

		/* Position Word 0: Image Positions and Alpha Value */
		vc4_state->pos0_offset = vc4_state->dlist_count;
		vc4_dlist_write(vc4_state,
				VC4_SET_FIELD(state->alpha >> 8, SCALER_POS0_FIXED_ALPHA) |
				VC4_SET_FIELD(vc4_state->crtc_x, SCALER_POS0_START_X) |
				VC4_SET_FIELD(vc4_state->crtc_y, SCALER_POS0_START_Y));

		/* Position Word 1: Scaled Image Dimensions. */
		if (!vc4_state->is_unity) {
			vc4_dlist_write(vc4_state,
					VC4_SET_FIELD(vc4_state->crtc_w,
						      SCALER_POS1_SCL_WIDTH) |
					VC4_SET_FIELD(vc4_state->crtc_h,
						      SCALER_POS1_SCL_HEIGHT));
		}

		/* Position Word 2: Source Image Size, Alpha */
		vc4_state->pos2_offset = vc4_state->dlist_count;
		vc4_dlist_write(vc4_state,
				VC4_SET_FIELD(fb->format->has_alpha ?
					      SCALER_POS2_ALPHA_MODE_PIPELINE :
					      SCALER_POS2_ALPHA_MODE_FIXED,
					      SCALER_POS2_ALPHA_MODE) |
				(mix_plane_alpha ? SCALER_POS2_ALPHA_MIX : 0) |
				(fb->format->has_alpha ?
						SCALER_POS2_ALPHA_PREMULT : 0) |
				VC4_SET_FIELD(vc4_state->src_w[0],
					      SCALER_POS2_WIDTH) |
				VC4_SET_FIELD(vc4_state->src_h[0],
					      SCALER_POS2_HEIGHT));

		/* Position Word 3: Context.  Written by the HVS. */
		vc4_dlist_write(vc4_state, 0xc0c0c0c0);

	} else {
		u32 hvs_pixel_order = format->pixel_order;

		if (format->pixel_order_hvs5)
			hvs_pixel_order = format->pixel_order_hvs5;

		/* Control word */
		vc4_dlist_write(vc4_state,
				SCALER_CTL0_VALID |
				(hvs_pixel_order << SCALER_CTL0_ORDER_SHIFT) |
				(hvs_format << SCALER_CTL0_PIXEL_FORMAT_SHIFT) |
				VC4_SET_FIELD(tiling, SCALER_CTL0_TILING) |
				(vc4_state->is_unity ?
						SCALER5_CTL0_UNITY : 0) |
				VC4_SET_FIELD(scl0, SCALER_CTL0_SCL0) |
				VC4_SET_FIELD(scl1, SCALER_CTL0_SCL1) |
				SCALER5_CTL0_ALPHA_EXPAND |
				SCALER5_CTL0_RGB_EXPAND);

		/* Position Word 0: Image Positions and Alpha Value */
		vc4_state->pos0_offset = vc4_state->dlist_count;
		vc4_dlist_write(vc4_state,
				(rotation & DRM_MODE_REFLECT_Y ?
						SCALER5_POS0_VFLIP : 0) |
				VC4_SET_FIELD(vc4_state->crtc_x,
					      SCALER_POS0_START_X) |
				(rotation & DRM_MODE_REFLECT_X ?
					      SCALER5_POS0_HFLIP : 0) |
				VC4_SET_FIELD(vc4_state->crtc_y,
					      SCALER5_POS0_START_Y)
			       );

		/* Control Word 2 */
		vc4_dlist_write(vc4_state,
				VC4_SET_FIELD(state->alpha >> 4,
					      SCALER5_CTL2_ALPHA) |
				(fb->format->has_alpha ?
					SCALER5_CTL2_ALPHA_PREMULT : 0) |
				(mix_plane_alpha ?
					SCALER5_CTL2_ALPHA_MIX : 0) |
				VC4_SET_FIELD(fb->format->has_alpha ?
				      SCALER5_CTL2_ALPHA_MODE_PIPELINE :
				      SCALER5_CTL2_ALPHA_MODE_FIXED,
				      SCALER5_CTL2_ALPHA_MODE)
			       );

		/* Position Word 1: Scaled Image Dimensions. */
		if (!vc4_state->is_unity) {
			vc4_dlist_write(vc4_state,
					VC4_SET_FIELD(vc4_state->crtc_w,
						      SCALER5_POS1_SCL_WIDTH) |
					VC4_SET_FIELD(vc4_state->crtc_h,
						      SCALER5_POS1_SCL_HEIGHT));
		}

		/* Position Word 2: Source Image Size */
		vc4_state->pos2_offset = vc4_state->dlist_count;
		vc4_dlist_write(vc4_state,
				VC4_SET_FIELD(vc4_state->src_w[0],
					      SCALER5_POS2_WIDTH) |
				VC4_SET_FIELD(vc4_state->src_h[0],
					      SCALER5_POS2_HEIGHT));

		/* Position Word 3: Context.  Written by the HVS. */
		vc4_dlist_write(vc4_state, 0xc0c0c0c0);
	}


	/* Pointer Word 0/1/2: RGB / Y / Cb / Cr Pointers
	 *
	 * The pointers may be any byte address.
	 */
	vc4_state->ptr0_offset = vc4_state->dlist_count;
	for (i = 0; i < num_planes; i++)
		vc4_dlist_write(vc4_state, vc4_state->offsets[i]);

	/* Pointer Context Word 0/1/2: Written by the HVS */
	for (i = 0; i < num_planes; i++)
		vc4_dlist_write(vc4_state, 0xc0c0c0c0);

	/* Pitch word 0 */
	vc4_dlist_write(vc4_state, pitch0);

	/* Pitch word 1/2 */
	for (i = 1; i < num_planes; i++) {
		if (hvs_format != HVS_PIXEL_FORMAT_H264 &&
		    hvs_format != HVS_PIXEL_FORMAT_YCBCR_10BIT) {
			vc4_dlist_write(vc4_state,
					VC4_SET_FIELD(fb->pitches[i],
						      SCALER_SRC_PITCH));
		} else {
			vc4_dlist_write(vc4_state, pitch0);
		}
	}

	/* Colorspace conversion words */
	if (vc4_state->is_yuv) {
		enum drm_color_encoding color_encoding = state->color_encoding;
		enum drm_color_range color_range = state->color_range;
		const u32 *ccm;

		if (color_encoding >= DRM_COLOR_ENCODING_MAX)
			color_encoding = DRM_COLOR_YCBCR_BT601;
		if (color_range >= DRM_COLOR_RANGE_MAX)
			color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;

		ccm = colorspace_coeffs[color_range][color_encoding];

		vc4_dlist_write(vc4_state, ccm[0]);
		vc4_dlist_write(vc4_state, ccm[1]);
		vc4_dlist_write(vc4_state, ccm[2]);
	}

	vc4_state->lbm_offset = 0;

	if (vc4_state->x_scaling[0] != VC4_SCALING_NONE ||
	    vc4_state->x_scaling[1] != VC4_SCALING_NONE ||
	    vc4_state->y_scaling[0] != VC4_SCALING_NONE ||
	    vc4_state->y_scaling[1] != VC4_SCALING_NONE) {
		/* Reserve a slot for the LBM Base Address. The real value will
		 * be set when calling vc4_plane_allocate_lbm().
		 */
		if (vc4_state->y_scaling[0] != VC4_SCALING_NONE ||
		    vc4_state->y_scaling[1] != VC4_SCALING_NONE) {
			vc4_state->lbm_offset = vc4_state->dlist_count;
			vc4_dlist_counter_increment(vc4_state);
		}

		if (num_planes > 1) {
			/* Emit Cb/Cr as channel 0 and Y as channel
			 * 1. This matches how we set up scl0/scl1
			 * above.
			 */
			vc4_write_scaling_parameters(state, 1);
		}
		vc4_write_scaling_parameters(state, 0);

		/* If any PPF setup was done, then all the kernel
		 * pointers get uploaded.
		 */
		if (vc4_state->x_scaling[0] == VC4_SCALING_PPF ||
		    vc4_state->y_scaling[0] == VC4_SCALING_PPF ||
		    vc4_state->x_scaling[1] == VC4_SCALING_PPF ||
		    vc4_state->y_scaling[1] == VC4_SCALING_PPF) {
			u32 kernel = VC4_SET_FIELD(vc4->hvs->mitchell_netravali_filter.start,
						   SCALER_PPF_KERNEL_OFFSET);

			/* HPPF plane 0 */
			vc4_dlist_write(vc4_state, kernel);
			/* VPPF plane 0 */
			vc4_dlist_write(vc4_state, kernel);
			/* HPPF plane 1 */
			vc4_dlist_write(vc4_state, kernel);
			/* VPPF plane 1 */
			vc4_dlist_write(vc4_state, kernel);
		}
	}

	vc4_state->dlist[ctl0_offset] |=
		VC4_SET_FIELD(vc4_state->dlist_count, SCALER_CTL0_SIZE);

	/* crtc_* are already clipped coordinates. */
	covers_screen = vc4_state->crtc_x == 0 && vc4_state->crtc_y == 0 &&
			vc4_state->crtc_w == state->crtc->mode.hdisplay &&
			vc4_state->crtc_h == state->crtc->mode.vdisplay;
	/* Background fill might be necessary when the plane has per-pixel
	 * alpha content or a non-opaque plane alpha and could blend from the
	 * background or does not cover the entire screen.
	 */
	vc4_state->needs_bg_fill = fb->format->has_alpha || !covers_screen ||
				   state->alpha != DRM_BLEND_ALPHA_OPAQUE;

	/* Flag the dlist as initialized to avoid checking it twice in case
	 * the async update check already called vc4_plane_mode_set() and
	 * decided to fallback to sync update because async update was not
	 * possible.
	 */
	vc4_state->dlist_initialized = 1;

	vc4_plane_calc_load(state);

	return 0;
}

/* If a modeset involves changing the setup of a plane, the atomic
 * infrastructure will call this to validate a proposed plane setup.
 * However, if a plane isn't getting updated, this (and the
 * corresponding vc4_plane_atomic_update) won't get called.  Thus, we
 * compute the dlist here and have all active plane dlists get updated
 * in the CRTC's flush.
 */
static int vc4_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(new_plane_state);
	int ret;

	vc4_state->dlist_count = 0;

	if (!plane_enabled(new_plane_state))
		return 0;

	ret = vc4_plane_mode_set(plane, new_plane_state);
	if (ret)
		return ret;

	return vc4_plane_allocate_lbm(new_plane_state);
}

static void vc4_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	/* No contents here.  Since we don't know where in the CRTC's
	 * dlist we should be stored, our dlist is uploaded to the
	 * hardware with vc4_plane_write_dlist() at CRTC atomic_flush
	 * time.
	 */
}

u32 vc4_plane_write_dlist(struct drm_plane *plane, u32 __iomem *dlist)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(plane->state);
	int i;

	vc4_state->hw_dlist = dlist;

	/* Can't memcpy_toio() because it needs to be 32-bit writes. */
	for (i = 0; i < vc4_state->dlist_count; i++)
		writel(vc4_state->dlist[i], &dlist[i]);

	return vc4_state->dlist_count;
}

u32 vc4_plane_dlist_size(const struct drm_plane_state *state)
{
	const struct vc4_plane_state *vc4_state =
		container_of(state, typeof(*vc4_state), base);

	return vc4_state->dlist_count;
}

/* Updates the plane to immediately (well, once the FIFO needs
 * refilling) scan out from at a new framebuffer.
 */
void vc4_plane_async_set_fb(struct drm_plane *plane, struct drm_framebuffer *fb)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(plane->state);
	struct drm_gem_cma_object *bo = drm_fb_cma_get_gem_obj(fb, 0);
	uint32_t addr;

	/* We're skipping the address adjustment for negative origin,
	 * because this is only called on the primary plane.
	 */
	WARN_ON_ONCE(plane->state->crtc_x < 0 || plane->state->crtc_y < 0);
	addr = bo->paddr + fb->offsets[0];

	/* Write the new address into the hardware immediately.  The
	 * scanout will start from this address as soon as the FIFO
	 * needs to refill with pixels.
	 */
	writel(addr, &vc4_state->hw_dlist[vc4_state->ptr0_offset]);

	/* Also update the CPU-side dlist copy, so that any later
	 * atomic updates that don't do a new modeset on our plane
	 * also use our updated address.
	 */
	vc4_state->dlist[vc4_state->ptr0_offset] = addr;
}

static void vc4_plane_atomic_async_update(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct vc4_plane_state *vc4_state, *new_vc4_state;

	swap(plane->state->fb, new_plane_state->fb);
	plane->state->crtc_x = new_plane_state->crtc_x;
	plane->state->crtc_y = new_plane_state->crtc_y;
	plane->state->crtc_w = new_plane_state->crtc_w;
	plane->state->crtc_h = new_plane_state->crtc_h;
	plane->state->src_x = new_plane_state->src_x;
	plane->state->src_y = new_plane_state->src_y;
	plane->state->src_w = new_plane_state->src_w;
	plane->state->src_h = new_plane_state->src_h;
	plane->state->alpha = new_plane_state->alpha;
	plane->state->pixel_blend_mode = new_plane_state->pixel_blend_mode;
	plane->state->rotation = new_plane_state->rotation;
	plane->state->zpos = new_plane_state->zpos;
	plane->state->normalized_zpos = new_plane_state->normalized_zpos;
	plane->state->color_encoding = new_plane_state->color_encoding;
	plane->state->color_range = new_plane_state->color_range;
	plane->state->src = new_plane_state->src;
	plane->state->dst = new_plane_state->dst;
	plane->state->visible = new_plane_state->visible;

	new_vc4_state = to_vc4_plane_state(new_plane_state);
	vc4_state = to_vc4_plane_state(plane->state);

	vc4_state->crtc_x = new_vc4_state->crtc_x;
	vc4_state->crtc_y = new_vc4_state->crtc_y;
	vc4_state->crtc_h = new_vc4_state->crtc_h;
	vc4_state->crtc_w = new_vc4_state->crtc_w;
	vc4_state->src_x = new_vc4_state->src_x;
	vc4_state->src_y = new_vc4_state->src_y;
	memcpy(vc4_state->src_w, new_vc4_state->src_w,
	       sizeof(vc4_state->src_w));
	memcpy(vc4_state->src_h, new_vc4_state->src_h,
	       sizeof(vc4_state->src_h));
	memcpy(vc4_state->x_scaling, new_vc4_state->x_scaling,
	       sizeof(vc4_state->x_scaling));
	memcpy(vc4_state->y_scaling, new_vc4_state->y_scaling,
	       sizeof(vc4_state->y_scaling));
	vc4_state->is_unity = new_vc4_state->is_unity;
	vc4_state->is_yuv = new_vc4_state->is_yuv;
	memcpy(vc4_state->offsets, new_vc4_state->offsets,
	       sizeof(vc4_state->offsets));
	vc4_state->needs_bg_fill = new_vc4_state->needs_bg_fill;

	/* Update the current vc4_state pos0, pos2 and ptr0 dlist entries. */
	vc4_state->dlist[vc4_state->pos0_offset] =
		new_vc4_state->dlist[vc4_state->pos0_offset];
	vc4_state->dlist[vc4_state->pos2_offset] =
		new_vc4_state->dlist[vc4_state->pos2_offset];
	vc4_state->dlist[vc4_state->ptr0_offset] =
		new_vc4_state->dlist[vc4_state->ptr0_offset];

	/* Note that we can't just call vc4_plane_write_dlist()
	 * because that would smash the context data that the HVS is
	 * currently using.
	 */
	writel(vc4_state->dlist[vc4_state->pos0_offset],
	       &vc4_state->hw_dlist[vc4_state->pos0_offset]);
	writel(vc4_state->dlist[vc4_state->pos2_offset],
	       &vc4_state->hw_dlist[vc4_state->pos2_offset]);
	writel(vc4_state->dlist[vc4_state->ptr0_offset],
	       &vc4_state->hw_dlist[vc4_state->ptr0_offset]);
}

static int vc4_plane_atomic_async_check(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct vc4_plane_state *old_vc4_state, *new_vc4_state;
	int ret;
	u32 i;

	ret = vc4_plane_mode_set(plane, new_plane_state);
	if (ret)
		return ret;

	old_vc4_state = to_vc4_plane_state(plane->state);
	new_vc4_state = to_vc4_plane_state(new_plane_state);

	if (!new_vc4_state->hw_dlist)
		return -EINVAL;

	if (old_vc4_state->dlist_count != new_vc4_state->dlist_count ||
	    old_vc4_state->pos0_offset != new_vc4_state->pos0_offset ||
	    old_vc4_state->pos2_offset != new_vc4_state->pos2_offset ||
	    old_vc4_state->ptr0_offset != new_vc4_state->ptr0_offset ||
	    vc4_lbm_size(plane->state) != vc4_lbm_size(new_plane_state))
		return -EINVAL;

	/* Only pos0, pos2 and ptr0 DWORDS can be updated in an async update
	 * if anything else has changed, fallback to a sync update.
	 */
	for (i = 0; i < new_vc4_state->dlist_count; i++) {
		if (i == new_vc4_state->pos0_offset ||
		    i == new_vc4_state->pos2_offset ||
		    i == new_vc4_state->ptr0_offset ||
		    (new_vc4_state->lbm_offset &&
		     i == new_vc4_state->lbm_offset))
			continue;

		if (new_vc4_state->dlist[i] != old_vc4_state->dlist[i])
			return -EINVAL;
	}

	return 0;
}

static int vc4_prepare_fb(struct drm_plane *plane,
			  struct drm_plane_state *state)
{
	struct vc4_bo *bo;

	if (!state->fb)
		return 0;

	bo = to_vc4_bo(&drm_fb_cma_get_gem_obj(state->fb, 0)->base);

	drm_gem_plane_helper_prepare_fb(plane, state);

	if (plane->state->fb == state->fb)
		return 0;

	return vc4_bo_inc_usecnt(bo);
}

static void vc4_cleanup_fb(struct drm_plane *plane,
			   struct drm_plane_state *state)
{
	struct vc4_bo *bo;

	if (plane->state->fb == state->fb || !state->fb)
		return;

	bo = to_vc4_bo(&drm_fb_cma_get_gem_obj(state->fb, 0)->base);
	vc4_bo_dec_usecnt(bo);
}

static const struct drm_plane_helper_funcs vc4_plane_helper_funcs = {
	.atomic_check = vc4_plane_atomic_check,
	.atomic_update = vc4_plane_atomic_update,
	.prepare_fb = vc4_prepare_fb,
	.cleanup_fb = vc4_cleanup_fb,
	.atomic_async_check = vc4_plane_atomic_async_check,
	.atomic_async_update = vc4_plane_atomic_async_update,
};

static const struct drm_plane_helper_funcs vc5_plane_helper_funcs = {
	.atomic_check = vc4_plane_atomic_check,
	.atomic_update = vc4_plane_atomic_update,
	.atomic_async_check = vc4_plane_atomic_async_check,
	.atomic_async_update = vc4_plane_atomic_async_update,
};

static bool vc4_format_mod_supported(struct drm_plane *plane,
				     uint32_t format,
				     uint64_t modifier)
{
	/* Support T_TILING for RGB formats only. */
	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
		switch (fourcc_mod_broadcom_mod(modifier)) {
		case DRM_FORMAT_MOD_LINEAR:
		case DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED:
			return true;
		default:
			return false;
		}
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		switch (fourcc_mod_broadcom_mod(modifier)) {
		case DRM_FORMAT_MOD_LINEAR:
		case DRM_FORMAT_MOD_BROADCOM_SAND64:
		case DRM_FORMAT_MOD_BROADCOM_SAND128:
		case DRM_FORMAT_MOD_BROADCOM_SAND256:
			return true;
		default:
			return false;
		}
	case DRM_FORMAT_P030:
		switch (fourcc_mod_broadcom_mod(modifier)) {
		case DRM_FORMAT_MOD_BROADCOM_SAND128:
			return true;
		default:
			return false;
		}
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	default:
		return (modifier == DRM_FORMAT_MOD_LINEAR);
	}
}

static const struct drm_plane_funcs vc4_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.set_property = NULL,
	.reset = vc4_plane_reset,
	.atomic_duplicate_state = vc4_plane_duplicate_state,
	.atomic_destroy_state = vc4_plane_destroy_state,
	.format_mod_supported = vc4_format_mod_supported,
};

struct drm_plane *vc4_plane_init(struct drm_device *dev,
				 enum drm_plane_type type)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_plane *plane = NULL;
	struct vc4_plane *vc4_plane;
	u32 formats[ARRAY_SIZE(hvs_formats)];
	int num_formats = 0;
	int ret = 0;
	unsigned i;
	static const uint64_t modifiers[] = {
		DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED,
		DRM_FORMAT_MOD_BROADCOM_SAND128,
		DRM_FORMAT_MOD_BROADCOM_SAND64,
		DRM_FORMAT_MOD_BROADCOM_SAND256,
		DRM_FORMAT_MOD_LINEAR,
		DRM_FORMAT_MOD_INVALID
	};

	vc4_plane = devm_kzalloc(dev->dev, sizeof(*vc4_plane),
				 GFP_KERNEL);
	if (!vc4_plane)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ARRAY_SIZE(hvs_formats); i++) {
		if (!hvs_formats[i].hvs5_only || vc4->is_vc5) {
			formats[num_formats] = hvs_formats[i].drm;
			num_formats++;
		}
	}

	plane = &vc4_plane->base;
	ret = drm_universal_plane_init(dev, plane, 0,
				       &vc4_plane_funcs,
				       formats, num_formats,
				       modifiers, type, NULL);
	if (ret)
		return ERR_PTR(ret);

	if (vc4->is_vc5)
		drm_plane_helper_add(plane, &vc5_plane_helper_funcs);
	else
		drm_plane_helper_add(plane, &vc4_plane_helper_funcs);

	drm_plane_create_alpha_property(plane);
	drm_plane_create_rotation_property(plane, DRM_MODE_ROTATE_0,
					   DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y);

	drm_plane_create_color_properties(plane,
					  BIT(DRM_COLOR_YCBCR_BT601) |
					  BIT(DRM_COLOR_YCBCR_BT709) |
					  BIT(DRM_COLOR_YCBCR_BT2020),
					  BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
					  BIT(DRM_COLOR_YCBCR_FULL_RANGE),
					  DRM_COLOR_YCBCR_BT709,
					  DRM_COLOR_YCBCR_LIMITED_RANGE);

	return plane;
}

int vc4_plane_create_additional_planes(struct drm_device *drm)
{
	struct drm_plane *cursor_plane;
	struct drm_crtc *crtc;
	unsigned int i;

	/* Set up some arbitrary number of planes.  We're not limited
	 * by a set number of physical registers, just the space in
	 * the HVS (16k) and how small an plane can be (28 bytes).
	 * However, each plane we set up takes up some memory, and
	 * increases the cost of looping over planes, which atomic
	 * modesetting does quite a bit.  As a result, we pick a
	 * modest number of planes to expose, that should hopefully
	 * still cover any sane usecase.
	 */
	for (i = 0; i < 16; i++) {
		struct drm_plane *plane =
			vc4_plane_init(drm, DRM_PLANE_TYPE_OVERLAY);

		if (IS_ERR(plane))
			continue;

		plane->possible_crtcs =
			GENMASK(drm->mode_config.num_crtc - 1, 0);
	}

	drm_for_each_crtc(crtc, drm) {
		/* Set up the legacy cursor after overlay initialization,
		 * since we overlay planes on the CRTC in the order they were
		 * initialized.
		 */
		cursor_plane = vc4_plane_init(drm, DRM_PLANE_TYPE_CURSOR);
		if (!IS_ERR(cursor_plane)) {
			cursor_plane->possible_crtcs = drm_crtc_mask(crtc);
			crtc->cursor = cursor_plane;
		}
	}

	return 0;
}
