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
#include <drm/drm_blend.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>

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
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XRGB,
	},
	{
		.drm = DRM_FORMAT_BGR565,
		.hvs = HVS_PIXEL_FORMAT_RGB565,
		.pixel_order = HVS_PIXEL_ORDER_XBGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XBGR,
	},
	{
		.drm = DRM_FORMAT_ARGB1555,
		.hvs = HVS_PIXEL_FORMAT_RGBA5551,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
	},
	{
		.drm = DRM_FORMAT_XRGB1555,
		.hvs = HVS_PIXEL_FORMAT_RGBA5551,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
	},
	{
		.drm = DRM_FORMAT_RGB888,
		.hvs = HVS_PIXEL_FORMAT_RGB888,
		.pixel_order = HVS_PIXEL_ORDER_XRGB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XRGB,
	},
	{
		.drm = DRM_FORMAT_BGR888,
		.hvs = HVS_PIXEL_FORMAT_RGB888,
		.pixel_order = HVS_PIXEL_ORDER_XBGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XBGR,
	},
	{
		.drm = DRM_FORMAT_YUV422,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_YVU422,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_YUV444,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_YVU444,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_YUV420,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV420_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_YVU420,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV420_3PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_NV12,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_NV21,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_NV16,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCBCR,
	},
	{
		.drm = DRM_FORMAT_NV61,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCRCB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCRCB,
	},
	{
		.drm = DRM_FORMAT_P030,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_10BIT,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_XYCBCR,
		.hvs5_only = true,
	},
	{
		.drm = DRM_FORMAT_XRGB2101010,
		.hvs = HVS_PIXEL_FORMAT_RGBA1010102,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
		.hvs5_only = true,
	},
	{
		.drm = DRM_FORMAT_ARGB2101010,
		.hvs = HVS_PIXEL_FORMAT_RGBA1010102,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
		.hvs5_only = true,
	},
	{
		.drm = DRM_FORMAT_ABGR2101010,
		.hvs = HVS_PIXEL_FORMAT_RGBA1010102,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ABGR,
		.hvs5_only = true,
	},
	{
		.drm = DRM_FORMAT_XBGR2101010,
		.hvs = HVS_PIXEL_FORMAT_RGBA1010102,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ABGR,
		.hvs5_only = true,
	},
	{
		.drm = DRM_FORMAT_RGB332,
		.hvs = HVS_PIXEL_FORMAT_RGB332,
		.pixel_order = HVS_PIXEL_ORDER_ARGB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
	},
	{
		.drm = DRM_FORMAT_BGR233,
		.hvs = HVS_PIXEL_FORMAT_RGB332,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ABGR,
	},
	{
		.drm = DRM_FORMAT_XRGB4444,
		.hvs = HVS_PIXEL_FORMAT_RGBA4444,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
	},
	{
		.drm = DRM_FORMAT_ARGB4444,
		.hvs = HVS_PIXEL_FORMAT_RGBA4444,
		.pixel_order = HVS_PIXEL_ORDER_ABGR,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ARGB,
	},
	{
		.drm = DRM_FORMAT_XBGR4444,
		.hvs = HVS_PIXEL_FORMAT_RGBA4444,
		.pixel_order = HVS_PIXEL_ORDER_ARGB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ABGR,
	},
	{
		.drm = DRM_FORMAT_ABGR4444,
		.hvs = HVS_PIXEL_FORMAT_RGBA4444,
		.pixel_order = HVS_PIXEL_ORDER_ARGB,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_ABGR,
	},
	{
		.drm = DRM_FORMAT_BGRX4444,
		.hvs = HVS_PIXEL_FORMAT_RGBA4444,
		.pixel_order = HVS_PIXEL_ORDER_RGBA,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_BGRA,
	},
	{
		.drm = DRM_FORMAT_BGRA4444,
		.hvs = HVS_PIXEL_FORMAT_RGBA4444,
		.pixel_order = HVS_PIXEL_ORDER_RGBA,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_BGRA,
	},
	{
		.drm = DRM_FORMAT_RGBX4444,
		.hvs = HVS_PIXEL_FORMAT_RGBA4444,
		.pixel_order = HVS_PIXEL_ORDER_BGRA,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_RGBA,
	},
	{
		.drm = DRM_FORMAT_RGBA4444,
		.hvs = HVS_PIXEL_FORMAT_RGBA4444,
		.pixel_order = HVS_PIXEL_ORDER_BGRA,
		.pixel_order_hvs5 = HVS_PIXEL_ORDER_RGBA,
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
	if (dst == src >> 16)
		return VC4_SCALING_NONE;
	if (3 * dst >= 2 * (src >> 16))
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
	struct vc4_dev *vc4 = to_vc4_dev(plane->dev);
	struct vc4_hvs *hvs = vc4->hvs;
	struct vc4_plane_state *vc4_state;
	unsigned int i;

	if (WARN_ON(!plane->state))
		return NULL;

	vc4_state = kmemdup(plane->state, sizeof(*vc4_state), GFP_KERNEL);
	if (!vc4_state)
		return NULL;

	memset(&vc4_state->lbm, 0, sizeof(vc4_state->lbm));

	for (i = 0; i < DRM_FORMAT_MAX_PLANES; i++) {
		if (vc4_state->upm_handle[i])
			refcount_inc(&hvs->upm_refcounts[vc4_state->upm_handle[i]].refcount);
	}

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

static void vc4_plane_release_upm_ida(struct vc4_hvs *hvs, unsigned int upm_handle)
{
	struct vc4_upm_refcounts *refcount = &hvs->upm_refcounts[upm_handle];
	unsigned long irqflags;

	spin_lock_irqsave(&hvs->mm_lock, irqflags);
	drm_mm_remove_node(&refcount->upm);
	spin_unlock_irqrestore(&hvs->mm_lock, irqflags);
	refcount->upm.start = 0;
	refcount->upm.size = 0;
	refcount->size = 0;

	ida_free(&hvs->upm_handles, upm_handle);
}

static void vc4_plane_destroy_state(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct vc4_dev *vc4 = to_vc4_dev(plane->dev);
	struct vc4_hvs *hvs = vc4->hvs;
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	unsigned int i;

	if (drm_mm_node_allocated(&vc4_state->lbm)) {
		unsigned long irqflags;

		spin_lock_irqsave(&hvs->mm_lock, irqflags);
		drm_mm_remove_node(&vc4_state->lbm);
		spin_unlock_irqrestore(&hvs->mm_lock, irqflags);
	}

	for (i = 0; i < DRM_FORMAT_MAX_PLANES; i++) {
		struct vc4_upm_refcounts *refcount;

		if (!vc4_state->upm_handle[i])
			continue;

		refcount = &hvs->upm_refcounts[vc4_state->upm_handle[i]];

		if (refcount_dec_and_test(&refcount->refcount))
			vc4_plane_release_upm_ida(hvs, vc4_state->upm_handle[i]);
	}

	kfree(vc4_state->dlist);
	__drm_atomic_helper_plane_destroy_state(&vc4_state->base);
	kfree(state);
}

/* Called during init to allocate the plane's atomic state. */
static void vc4_plane_reset(struct drm_plane *plane)
{
	struct vc4_plane_state *vc4_state;

	if (plane->state)
		__drm_atomic_helper_plane_destroy_state(plane->state);

	kfree(plane->state);

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
	if (vc4_pstate->crtc_x > crtc_state->mode.hdisplay - right)
		vc4_pstate->crtc_x = crtc_state->mode.hdisplay - right;

	adjvdisplay = crtc_state->mode.vdisplay - (top + bottom);
	vc4_pstate->crtc_y = DIV_ROUND_CLOSEST(vc4_pstate->crtc_y *
					       adjvdisplay,
					       crtc_state->mode.vdisplay);
	vc4_pstate->crtc_y += top;
	if (vc4_pstate->crtc_y > crtc_state->mode.vdisplay - bottom)
		vc4_pstate->crtc_y = crtc_state->mode.vdisplay - bottom;

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
	int num_planes = fb->format->num_planes;
	struct drm_crtc_state *crtc_state;
	u32 h_subsample = fb->format->hsub;
	u32 v_subsample = fb->format->vsub;
	int ret;

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

	vc4_state->src_x = state->src.x1;
	vc4_state->src_y = state->src.y1;
	vc4_state->src_w[0] = state->src.x2 - vc4_state->src_x;
	vc4_state->src_h[0] = state->src.y2 - vc4_state->src_y;

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

		/* Similarly UV needs vertical scaling to be enabled.
		 * Without this a 1:1 scaled YUV422 plane isn't rendered.
		 */
		if (vc4_state->y_scaling[1] == VC4_SCALING_NONE)
			vc4_state->y_scaling[1] = VC4_SCALING_PPF;
	} else {
		vc4_state->is_yuv = false;
		vc4_state->x_scaling[1] = VC4_SCALING_NONE;
		vc4_state->y_scaling[1] = VC4_SCALING_NONE;
	}

	return 0;
}

static void vc4_write_tpz(struct vc4_plane_state *vc4_state, u32 src, u32 dst)
{
	struct vc4_dev *vc4 = to_vc4_dev(vc4_state->base.plane->dev);
	u32 scale, recip;

	WARN_ON_ONCE(vc4->gen > VC4_GEN_6_D);

	scale = src / dst;

	/* The specs note that while the reciprocal would be defined
	 * as (1<<32)/scale, ~0 is close enough.
	 */
	recip = ~0 / scale;

	vc4_dlist_write(vc4_state,
			/*
			 * The BCM2712 is lacking BIT(31) compared to
			 * the previous generations, but we don't use
			 * it.
			 */
			VC4_SET_FIELD(scale, SCALER_TPZ0_SCALE) |
			VC4_SET_FIELD(0, SCALER_TPZ0_IPHASE));
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(recip, SCALER_TPZ1_RECIP));
}

/* phase magnitude bits */
#define PHASE_BITS 6

static void vc4_write_ppf(struct vc4_plane_state *vc4_state, u32 src, u32 dst,
			  u32 xy, int channel)
{
	struct vc4_dev *vc4 = to_vc4_dev(vc4_state->base.plane->dev);
	u32 scale = src / dst;
	s32 offset, offset2;
	s32 phase;

	WARN_ON_ONCE(vc4->gen > VC4_GEN_6_D);

	/*
	 * Start the phase at 1/2 pixel from the 1st pixel at src_x.
	 * 1/4 pixel for YUV.
	 */
	if (channel) {
		/*
		 * The phase is relative to scale_src->x, so shift it for
		 * display list's x value
		 */
		offset = (xy & 0x1ffff) >> (16 - PHASE_BITS) >> 1;
		offset += -(1 << PHASE_BITS >> 2);
	} else {
		/*
		 * The phase is relative to scale_src->x, so shift it for
		 * display list's x value
		 */
		offset = (xy & 0xffff) >> (16 - PHASE_BITS);
		offset += -(1 << PHASE_BITS >> 1);

		/*
		 * This is a kludge to make sure the scaling factors are
		 * consistent with YUV's luma scaling. We lose 1-bit precision
		 * because of this.
		 */
		scale &= ~1;
	}

	/*
	 * There may be a also small error introduced by precision of scale.
	 * Add half of that as a compromise
	 */
	offset2 = src - dst * scale;
	offset2 >>= 16 - PHASE_BITS;
	phase = offset + (offset2 >> 1);

	/* Ensure +ve values don't touch the sign bit, then truncate negative values */
	if (phase >= 1 << PHASE_BITS)
		phase = (1 << PHASE_BITS) - 1;

	phase &= SCALER_PPF_IPHASE_MASK;

	vc4_dlist_write(vc4_state,
			SCALER_PPF_AGC |
			VC4_SET_FIELD(scale, SCALER_PPF_SCALE) |
			/*
			 * The register layout documentation is slightly
			 * different to setup the phase in the BCM2712,
			 * but they seem equivalent.
			 */
			VC4_SET_FIELD(phase, SCALER_PPF_IPHASE));
}

static u32 __vc4_lbm_size(struct drm_plane_state *state)
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
		pix_per_line = vc4_state->src_w[0] >> 16;

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
	lbm = roundup(lbm, vc4->gen == VC4_GEN_5 ? 128 : 64);

	/* Each "word" of the LBM memory contains 2 or 4 (hvs5) pixels */
	lbm /= vc4->gen == VC4_GEN_5 ? 4 : 2;

	return lbm;
}

static unsigned int vc4_lbm_words_per_component(const struct drm_plane_state *state,
						unsigned int channel)
{
	const struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	switch (vc4_state->y_scaling[channel]) {
	case VC4_SCALING_PPF:
		return 4;

	case VC4_SCALING_TPZ:
		return 2;

	default:
		return 0;
	}
}

static unsigned int vc4_lbm_components(const struct drm_plane_state *state,
				       unsigned int channel)
{
	const struct drm_format_info *info = state->fb->format;
	const struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	if (vc4_state->y_scaling[channel] == VC4_SCALING_NONE)
		return 0;

	if (info->is_yuv)
		return channel ? 2 : 1;

	if (info->has_alpha)
		return 4;

	return 3;
}

static unsigned int vc4_lbm_channel_size(const struct drm_plane_state *state,
					 unsigned int channel)
{
	const struct drm_format_info *info = state->fb->format;
	const struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	unsigned int channels_scaled = 0;
	unsigned int components, words, wpc;
	unsigned int width, lines;
	unsigned int i;

	/* LBM is meant to use the smaller of source or dest width, but there
	 * is a issue with UV scaling that the size required for the second
	 * channel is based on the source width only.
	 */
	if (info->hsub > 1 && channel == 1)
		width = state->src_w >> 16;
	else
		width = min(state->src_w >> 16, state->crtc_w);
	width = round_up(width / info->hsub, 4);

	wpc = vc4_lbm_words_per_component(state, channel);
	if (!wpc)
		return 0;

	components = vc4_lbm_components(state, channel);
	if (!components)
		return 0;

	if (state->alpha != DRM_BLEND_ALPHA_OPAQUE && info->has_alpha)
		components -= 1;

	words = width * wpc * components;

	lines = DIV_ROUND_UP(words, 128 / info->hsub);

	for (i = 0; i < 2; i++)
		if (vc4_state->y_scaling[channel] != VC4_SCALING_NONE)
			channels_scaled++;

	if (channels_scaled == 1)
		lines = lines / 2;

	return lines;
}

static unsigned int __vc6_lbm_size(const struct drm_plane_state *state)
{
	const struct drm_format_info *info = state->fb->format;

	if (info->hsub > 1)
		return max(vc4_lbm_channel_size(state, 0),
			   vc4_lbm_channel_size(state, 1));
	else
		return vc4_lbm_channel_size(state, 0);
}

static u32 vc4_lbm_size(struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct vc4_dev *vc4 = to_vc4_dev(state->plane->dev);

	/* LBM is not needed when there's no vertical scaling. */
	if (vc4_state->y_scaling[0] == VC4_SCALING_NONE &&
	    vc4_state->y_scaling[1] == VC4_SCALING_NONE)
		return 0;

	if (vc4->gen >= VC4_GEN_6_C)
		return __vc6_lbm_size(state);
	else
		return __vc4_lbm_size(state);
}

static size_t vc6_upm_size(const struct drm_plane_state *state,
			   unsigned int plane)
{
	const struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	unsigned int stride = state->fb->pitches[plane];

	/*
	 * TODO: This only works for raster formats, and is sub-optimal
	 * for buffers with a stride aligned on 32 bytes.
	 */
	unsigned int words_per_line = (stride + 62) / 32;
	unsigned int fetch_region_size = words_per_line * 32;
	unsigned int buffer_lines = 2 << vc4_state->upm_buffer_lines;
	unsigned int buffer_size = fetch_region_size * buffer_lines;

	return ALIGN(buffer_size, HVS_UBM_WORD_SIZE);
}

static void vc4_write_scaling_parameters(struct drm_plane_state *state,
					 int channel)
{
	struct vc4_dev *vc4 = to_vc4_dev(state->plane->dev);
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	WARN_ON_ONCE(vc4->gen > VC4_GEN_6_D);

	/* Ch0 H-PPF Word 0: Scaling Parameters */
	if (vc4_state->x_scaling[channel] == VC4_SCALING_PPF) {
		vc4_write_ppf(vc4_state, vc4_state->src_w[channel],
			      vc4_state->crtc_w, vc4_state->src_x, channel);
	}

	/* Ch0 V-PPF Words 0-1: Scaling Parameters, Context */
	if (vc4_state->y_scaling[channel] == VC4_SCALING_PPF) {
		vc4_write_ppf(vc4_state, vc4_state->src_h[channel],
			      vc4_state->crtc_h, vc4_state->src_y, channel);
		vc4_dlist_write(vc4_state, 0xc0c0c0c0);
	}

	/* Ch0 H-TPZ Words 0-1: Scaling Parameters, Recip */
	if (vc4_state->x_scaling[channel] == VC4_SCALING_TPZ) {
		vc4_write_tpz(vc4_state, vc4_state->src_w[channel],
			      vc4_state->crtc_w);
	}

	/* Ch0 V-TPZ Words 0-2: Scaling Parameters, Recip, Context */
	if (vc4_state->y_scaling[channel] == VC4_SCALING_TPZ) {
		vc4_write_tpz(vc4_state, vc4_state->src_h[channel],
			      vc4_state->crtc_h);
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
		 * (vc4_state->src_w[i] >> 16) * (vc4_state->src_h[i] >> 16) *
		 *  cpp * vrefresh
		 *
		 * when downscaling, we have to read more pixels per line in
		 * the time frame reserved for a single line, so the bandwidth
		 * demand can be punctually higher. To account for that, we
		 * calculate the down-scaling factor and multiply the plane
		 * load by this number. We're likely over-estimating the read
		 * demand, but that's better than under-estimating it.
		 */
		vscale_factor = DIV_ROUND_UP(vc4_state->src_h[i] >> 16,
					     vc4_state->crtc_h);
		vc4_state->membus_load += (vc4_state->src_w[i] >> 16) *
					  (vc4_state->src_h[i] >> 16) *
					  vscale_factor * fb->format->cpp[i];
		vc4_state->hvs_load += vc4_state->crtc_h * vc4_state->crtc_w;
	}

	vc4_state->hvs_load *= vrefresh;
	vc4_state->hvs_load >>= hvs_load_shift;
	vc4_state->membus_load *= vrefresh;
}

static int vc4_plane_allocate_lbm(struct drm_plane_state *state)
{
	struct drm_device *drm = state->plane->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct drm_plane *plane = state->plane;
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	unsigned long irqflags;
	u32 lbm_size;

	lbm_size = vc4_lbm_size(state);
	if (!lbm_size)
		return 0;

	/*
	 * NOTE: BCM2712 doesn't need to be aligned, since the size
	 * returned by vc4_lbm_size() is in words already.
	 */
	if (vc4->gen == VC4_GEN_5)
		lbm_size = ALIGN(lbm_size, 64);
	else if (vc4->gen == VC4_GEN_4)
		lbm_size = ALIGN(lbm_size, 32);

	drm_dbg_driver(drm, "[PLANE:%d:%s] LBM Allocation Size: %u\n",
		       plane->base.id, plane->name, lbm_size);

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
						 lbm_size, 1,
						 0, 0);
		spin_unlock_irqrestore(&vc4->hvs->mm_lock, irqflags);

		if (ret) {
			drm_err(drm, "Failed to allocate LBM entry: %d\n", ret);
			return ret;
		}
	} else {
		WARN_ON_ONCE(lbm_size != vc4_state->lbm.size);
	}

	vc4_state->dlist[vc4_state->lbm_offset] = vc4_state->lbm.start;

	return 0;
}

static int vc6_plane_allocate_upm(struct drm_plane_state *state)
{
	const struct drm_format_info *info = state->fb->format;
	struct drm_device *drm = state->plane->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_hvs *hvs = vc4->hvs;
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	unsigned int i;
	int ret;

	WARN_ON_ONCE(vc4->gen < VC4_GEN_6_C);

	vc4_state->upm_buffer_lines = SCALER6_PTR0_UPM_BUFF_SIZE_2_LINES;

	for (i = 0; i < info->num_planes; i++) {
		struct vc4_upm_refcounts *refcount;
		int upm_handle;
		unsigned long irqflags;
		size_t upm_size;

		upm_size = vc6_upm_size(state, i);
		if (!upm_size)
			return -EINVAL;
		upm_handle = vc4_state->upm_handle[i];

		if (upm_handle &&
		    hvs->upm_refcounts[upm_handle].size == upm_size) {
			/* Allocation is the same size as the previous user of
			 * the plane. Keep the allocation.
			 */
			vc4_state->upm_handle[i] = upm_handle;
		} else {
			if (upm_handle &&
			    refcount_dec_and_test(&hvs->upm_refcounts[upm_handle].refcount)) {
				vc4_plane_release_upm_ida(hvs, upm_handle);
				vc4_state->upm_handle[i] = 0;
			}

			upm_handle = ida_alloc_range(&hvs->upm_handles, 1,
						     VC4_NUM_UPM_HANDLES,
						     GFP_KERNEL);
			if (upm_handle < 0) {
				drm_dbg(drm, "Out of upm_handles\n");
				return upm_handle;
			}
			vc4_state->upm_handle[i] = upm_handle;

			refcount = &hvs->upm_refcounts[upm_handle];
			refcount_set(&refcount->refcount, 1);
			refcount->size = upm_size;

			spin_lock_irqsave(&hvs->mm_lock, irqflags);
			ret = drm_mm_insert_node_generic(&hvs->upm_mm,
							 &refcount->upm,
							 upm_size, HVS_UBM_WORD_SIZE,
							 0, 0);
			spin_unlock_irqrestore(&hvs->mm_lock, irqflags);
			if (ret) {
				drm_err(drm, "Failed to allocate UPM entry: %d\n", ret);
				refcount_set(&refcount->refcount, 0);
				ida_free(&hvs->upm_handles, upm_handle);
				vc4_state->upm_handle[i] = 0;
				return ret;
			}
		}

		refcount = &hvs->upm_refcounts[upm_handle];
		vc4_state->dlist[vc4_state->ptr0_offset[i]] |=
			VC4_SET_FIELD(refcount->upm.start / HVS_UBM_WORD_SIZE,
				      SCALER6_PTR0_UPM_BASE) |
			VC4_SET_FIELD(vc4_state->upm_handle[i] - 1,
				      SCALER6_PTR0_UPM_HANDLE) |
			VC4_SET_FIELD(vc4_state->upm_buffer_lines,
				      SCALER6_PTR0_UPM_BUFF_SIZE);
	}

	return 0;
}

static void vc6_plane_free_upm(struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct drm_device *drm = state->plane->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_hvs *hvs = vc4->hvs;
	unsigned int i;

	WARN_ON_ONCE(vc4->gen < VC4_GEN_6_C);

	for (i = 0; i < DRM_FORMAT_MAX_PLANES; i++) {
		unsigned int upm_handle;

		upm_handle = vc4_state->upm_handle[i];
		if (!upm_handle)
			continue;

		if (refcount_dec_and_test(&hvs->upm_refcounts[upm_handle].refcount))
			vc4_plane_release_upm_ida(hvs, upm_handle);
		vc4_state->upm_handle[i] = 0;
	}
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

static u32 vc4_hvs4_get_alpha_blend_mode(struct drm_plane_state *state)
{
	struct drm_device *dev = state->state->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	WARN_ON_ONCE(vc4->gen != VC4_GEN_4);

	if (!state->fb->format->has_alpha)
		return VC4_SET_FIELD(SCALER_POS2_ALPHA_MODE_FIXED,
				     SCALER_POS2_ALPHA_MODE);

	switch (state->pixel_blend_mode) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		return VC4_SET_FIELD(SCALER_POS2_ALPHA_MODE_FIXED,
				     SCALER_POS2_ALPHA_MODE);
	default:
	case DRM_MODE_BLEND_PREMULTI:
		return VC4_SET_FIELD(SCALER_POS2_ALPHA_MODE_PIPELINE,
				     SCALER_POS2_ALPHA_MODE) |
			SCALER_POS2_ALPHA_PREMULT;
	case DRM_MODE_BLEND_COVERAGE:
		return VC4_SET_FIELD(SCALER_POS2_ALPHA_MODE_PIPELINE,
				     SCALER_POS2_ALPHA_MODE);
	}
}

static u32 vc4_hvs5_get_alpha_blend_mode(struct drm_plane_state *state)
{
	struct drm_device *dev = state->state->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	WARN_ON_ONCE(vc4->gen != VC4_GEN_5 && vc4->gen != VC4_GEN_6_C &&
		     vc4->gen != VC4_GEN_6_D);

	switch (vc4->gen) {
	default:
	case VC4_GEN_5:
	case VC4_GEN_6_C:
		if (!state->fb->format->has_alpha)
			return VC4_SET_FIELD(SCALER5_CTL2_ALPHA_MODE_FIXED,
					     SCALER5_CTL2_ALPHA_MODE);

		switch (state->pixel_blend_mode) {
		case DRM_MODE_BLEND_PIXEL_NONE:
			return VC4_SET_FIELD(SCALER5_CTL2_ALPHA_MODE_FIXED,
					     SCALER5_CTL2_ALPHA_MODE);
		default:
		case DRM_MODE_BLEND_PREMULTI:
			return VC4_SET_FIELD(SCALER5_CTL2_ALPHA_MODE_PIPELINE,
					     SCALER5_CTL2_ALPHA_MODE) |
				SCALER5_CTL2_ALPHA_PREMULT;
		case DRM_MODE_BLEND_COVERAGE:
			return VC4_SET_FIELD(SCALER5_CTL2_ALPHA_MODE_PIPELINE,
					     SCALER5_CTL2_ALPHA_MODE);
		}
	case VC4_GEN_6_D:
		/* 2712-D configures fixed alpha mode in CTL0 */
		return state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI ?
			SCALER5_CTL2_ALPHA_PREMULT : 0;
	}
}

static u32 vc4_hvs6_get_alpha_mask_mode(struct drm_plane_state *state)
{
	struct drm_device *dev = state->state->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	WARN_ON_ONCE(vc4->gen != VC4_GEN_6_C && vc4->gen != VC4_GEN_6_D);

	if (vc4->gen == VC4_GEN_6_D &&
	    (!state->fb->format->has_alpha ||
	     state->pixel_blend_mode == DRM_MODE_BLEND_PIXEL_NONE))
		return VC4_SET_FIELD(SCALER6D_CTL0_ALPHA_MASK_FIXED,
				     SCALER6_CTL0_ALPHA_MASK);

	return VC4_SET_FIELD(SCALER6_CTL0_ALPHA_MASK_NONE, SCALER6_CTL0_ALPHA_MASK);
}

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
	u32 tiling, src_x, src_y;
	u32 width, height;
	u32 hvs_format = format->hvs;
	unsigned int rotation;
	u32 offsets[3] = { 0 };
	int ret, i;

	if (vc4_state->dlist_initialized)
		return 0;

	ret = vc4_plane_setup_clipping_and_scaling(state);
	if (ret)
		return ret;

	if (!vc4_state->src_w[0] || !vc4_state->src_h[0] ||
	    !vc4_state->crtc_w || !vc4_state->crtc_h) {
		/* 0 source size probably means the plane is offscreen */
		vc4_state->dlist_initialized = 1;
		return 0;
	}

	width = vc4_state->src_w[0] >> 16;
	height = vc4_state->src_h[0] >> 16;

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
	src_y = vc4_state->src_y >> 16;
	if (rotation & DRM_MODE_REFLECT_Y)
		src_y += height - 1;

	src_x = vc4_state->src_x >> 16;

	switch (base_format_mod) {
	case DRM_FORMAT_MOD_LINEAR:
		tiling = SCALER_CTL0_TILING_LINEAR;
		pitch0 = VC4_SET_FIELD(fb->pitches[0], SCALER_SRC_PITCH);

		/* Adjust the base pointer to the first pixel to be scanned
		 * out.
		 */
		for (i = 0; i < num_planes; i++) {
			offsets[i] += src_y / (i ? v_subsample : 1) * fb->pitches[i];
			offsets[i] += src_x / (i ? h_subsample : 1) * fb->format->cpp[i];
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
		u32 tiles_l = src_x >> tile_w_shift;
		u32 tiles_r = tiles_w - tiles_l;
		u32 tiles_t = src_y >> tile_h_shift;
		/* Intra-tile offsets, which modify the base address (the
		 * SCALER_PITCH0_TILE_Y_OFFSET tells HVS how to walk from that
		 * base address).
		 */
		u32 tile_y = (src_y >> 4) & 1;
		u32 subtile_y = (src_y >> 2) & 3;
		u32 utile_y = src_y & 3;
		u32 x_off = src_x & tile_w_mask;
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
		offsets[0] += tiles_t * (tiles_w << tile_size_shift);
		offsets[0] += subtile_y << 8;
		offsets[0] += utile_y << 4;

		/* Rows of tiles alternate left-to-right and right-to-left. */
		if (tiles_t & 1) {
			pitch0 |= SCALER_PITCH0_TILE_INITIAL_LINE_DIR;
			offsets[0] += (tiles_w - tiles_l) << tile_size_shift;
			offsets[0] -= (1 + !tile_y) << 10;
		} else {
			offsets[0] += tiles_l << tile_size_shift;
			offsets[0] += tile_y << 10;
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
				u32 remaining_pixels = src_x % 96;
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
				x_off = (src_x % pix_per_tile) /
					(i ? h_subsample : 1) *
					fb->format->cpp[i];
			}

			tile = src_x / pix_per_tile;

			offsets[i] += param * tile_w * tile;
			offsets[i] += src_y / (i ? v_subsample : 1) * tile_w;
			offsets[i] += x_off & ~(i ? 1 : 0);
		}

		pitch0 = VC4_SET_FIELD(param, SCALER_TILE_HEIGHT);
		break;
	}

	default:
		DRM_DEBUG_KMS("Unsupported FB tiling flag 0x%16llx",
			      (long long)fb->modifier);
		return -EINVAL;
	}

	/* fetch an extra pixel if we don't actually line up with the left edge. */
	if ((vc4_state->src_x & 0xffff) && vc4_state->src_x < (state->fb->width << 16))
		width++;

	/* same for the right side */
	if (((vc4_state->src_x + vc4_state->src_w[0]) & 0xffff) &&
	    vc4_state->src_x + vc4_state->src_w[0] < (state->fb->width << 16))
		width++;

	/* now for the top */
	if ((vc4_state->src_y & 0xffff) && vc4_state->src_y < (state->fb->height << 16))
		height++;

	/* and the bottom */
	if (((vc4_state->src_y + vc4_state->src_h[0]) & 0xffff) &&
	    vc4_state->src_y + vc4_state->src_h[0] < (state->fb->height << 16))
		height++;

	/* For YUV444 the hardware wants double the width, otherwise it doesn't
	 * fetch full width of chroma
	 */
	if (format->drm == DRM_FORMAT_YUV444 || format->drm == DRM_FORMAT_YVU444)
		width <<= 1;

	/* Don't waste cycles mixing with plane alpha if the set alpha
	 * is opaque or there is no per-pixel alpha information.
	 * In any case we use the alpha property value as the fixed alpha.
	 */
	mix_plane_alpha = state->alpha != DRM_BLEND_ALPHA_OPAQUE &&
			  fb->format->has_alpha;

	if (vc4->gen == VC4_GEN_4) {
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
				(mix_plane_alpha ? SCALER_POS2_ALPHA_MIX : 0) |
				vc4_hvs4_get_alpha_blend_mode(state) |
				VC4_SET_FIELD(width, SCALER_POS2_WIDTH) |
				VC4_SET_FIELD(height, SCALER_POS2_HEIGHT));

		/* Position Word 3: Context.  Written by the HVS. */
		vc4_dlist_write(vc4_state, 0xc0c0c0c0);

	} else {
		/* Control word */
		vc4_dlist_write(vc4_state,
				SCALER_CTL0_VALID |
				(format->pixel_order_hvs5 << SCALER_CTL0_ORDER_SHIFT) |
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
				vc4_hvs5_get_alpha_blend_mode(state) |
				(mix_plane_alpha ?
					SCALER5_CTL2_ALPHA_MIX : 0)
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
				VC4_SET_FIELD(width, SCALER5_POS2_WIDTH) |
				VC4_SET_FIELD(height, SCALER5_POS2_HEIGHT));

		/* Position Word 3: Context.  Written by the HVS. */
		vc4_dlist_write(vc4_state, 0xc0c0c0c0);
	}


	/* Pointer Word 0/1/2: RGB / Y / Cb / Cr Pointers
	 *
	 * The pointers may be any byte address.
	 */
	vc4_state->ptr0_offset[0] = vc4_state->dlist_count;

	for (i = 0; i < num_planes; i++) {
		struct drm_gem_dma_object *bo = drm_fb_dma_get_gem_obj(fb, i);

		vc4_dlist_write(vc4_state, bo->dma_addr + fb->offsets[i] + offsets[i]);
	}

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

static u32 vc6_plane_get_csc_mode(struct vc4_plane_state *vc4_state)
{
	struct drm_plane_state *state = &vc4_state->base;
	struct vc4_dev *vc4 = to_vc4_dev(state->plane->dev);
	u32 ret = 0;

	if (vc4_state->is_yuv) {
		enum drm_color_encoding color_encoding = state->color_encoding;
		enum drm_color_range color_range = state->color_range;

		/* CSC pre-loaded with:
		 * 0 = BT601 limited range
		 * 1 = BT709 limited range
		 * 2 = BT2020 limited range
		 * 3 = BT601 full range
		 * 4 = BT709 full range
		 * 5 = BT2020 full range
		 */
		if (color_encoding > DRM_COLOR_YCBCR_BT2020)
			color_encoding = DRM_COLOR_YCBCR_BT601;
		if (color_range > DRM_COLOR_YCBCR_FULL_RANGE)
			color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;

		if (vc4->gen == VC4_GEN_6_C) {
			ret |= SCALER6C_CTL2_CSC_ENABLE;
			ret |= VC4_SET_FIELD(color_encoding + (color_range * 3),
					     SCALER6C_CTL2_BRCM_CFC_CONTROL);
		} else {
			ret |= SCALER6D_CTL2_CSC_ENABLE;
			ret |= VC4_SET_FIELD(color_encoding + (color_range * 3),
					     SCALER6D_CTL2_BRCM_CFC_CONTROL);
		}
	}

	return ret;
}

static int vc6_plane_mode_set(struct drm_plane *plane,
			      struct drm_plane_state *state)
{
	struct drm_device *drm = plane->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	const struct hvs_format *format = vc4_get_hvs_format(fb->format->format);
	u64 base_format_mod = fourcc_mod_broadcom_mod(fb->modifier);
	int num_planes = fb->format->num_planes;
	u32 h_subsample = fb->format->hsub;
	u32 v_subsample = fb->format->vsub;
	bool mix_plane_alpha;
	bool covers_screen;
	u32 scl0, scl1, pitch0;
	u32 tiling, src_x, src_y;
	u32 width, height;
	u32 hvs_format = format->hvs;
	u32 offsets[3] = { 0 };
	unsigned int rotation;
	int ret, i;

	if (vc4_state->dlist_initialized)
		return 0;

	ret = vc4_plane_setup_clipping_and_scaling(state);
	if (ret)
		return ret;

	if (!vc4_state->src_w[0] || !vc4_state->src_h[0] ||
	    !vc4_state->crtc_w || !vc4_state->crtc_h) {
		/* 0 source size probably means the plane is offscreen.
		 * 0 destination size is a redundant plane.
		 */
		vc4_state->dlist_initialized = 1;
		return 0;
	}

	width = vc4_state->src_w[0] >> 16;
	height = vc4_state->src_h[0] >> 16;

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
	src_y = vc4_state->src_y >> 16;
	if (rotation & DRM_MODE_REFLECT_Y)
		src_y += height - 1;

	src_x = vc4_state->src_x >> 16;

	switch (base_format_mod) {
	case DRM_FORMAT_MOD_LINEAR:
		tiling = SCALER6_CTL0_ADDR_MODE_LINEAR;

		/* Adjust the base pointer to the first pixel to be scanned
		 * out.
		 */
		for (i = 0; i < num_planes; i++) {
			offsets[i] += src_y / (i ? v_subsample : 1) * fb->pitches[i];
			offsets[i] += src_x / (i ? h_subsample : 1) * fb->format->cpp[i];
		}

		break;

	case DRM_FORMAT_MOD_BROADCOM_SAND128:
	case DRM_FORMAT_MOD_BROADCOM_SAND256: {
		uint32_t param = fourcc_mod_broadcom_param(fb->modifier);
		u32 components_per_word;
		u32 starting_offset;
		u32 fetch_count;

		if (param > SCALER_TILE_HEIGHT_MASK) {
			DRM_DEBUG_KMS("SAND height too large (%d)\n",
				      param);
			return -EINVAL;
		}

		if (fb->format->format == DRM_FORMAT_P030) {
			hvs_format = HVS_PIXEL_FORMAT_YCBCR_10BIT;
			tiling = SCALER6_CTL0_ADDR_MODE_128B;
		} else {
			hvs_format = HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE;

			switch (base_format_mod) {
			case DRM_FORMAT_MOD_BROADCOM_SAND128:
				tiling = SCALER6_CTL0_ADDR_MODE_128B;
				break;
			case DRM_FORMAT_MOD_BROADCOM_SAND256:
				tiling = SCALER6_CTL0_ADDR_MODE_256B;
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
				u32 remaining_pixels = src_x % 96;
				u32 aligned = remaining_pixels / 12;
				u32 last_bits = remaining_pixels % 12;

				x_off = aligned * 16 + last_bits;
				tile_w = 128;
				pix_per_tile = 96;
			} else {
				switch (base_format_mod) {
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
				x_off = (src_x % pix_per_tile) /
					(i ? h_subsample : 1) *
					fb->format->cpp[i];
			}

			tile = src_x / pix_per_tile;

			offsets[i] += param * tile_w * tile;
			offsets[i] += src_y / (i ? v_subsample : 1) * tile_w;
			offsets[i] += x_off & ~(i ? 1 : 0);
		}

		components_per_word = fb->format->format == DRM_FORMAT_P030 ? 24 : 32;
		starting_offset = src_x % components_per_word;
		fetch_count = (width + starting_offset + components_per_word - 1) /
			components_per_word;

		pitch0 = VC4_SET_FIELD(param, SCALER6_PTR2_PITCH) |
			 VC4_SET_FIELD(fetch_count - 1, SCALER6_PTR2_FETCH_COUNT);
		break;
	}

	default:
		DRM_DEBUG_KMS("Unsupported FB tiling flag 0x%16llx",
			      (long long)fb->modifier);
		return -EINVAL;
	}

	/* fetch an extra pixel if we don't actually line up with the left edge. */
	if ((vc4_state->src_x & 0xffff) && vc4_state->src_x < (state->fb->width << 16))
		width++;

	/* same for the right side */
	if (((vc4_state->src_x + vc4_state->src_w[0]) & 0xffff) &&
	    vc4_state->src_x + vc4_state->src_w[0] < (state->fb->width << 16))
		width++;

	/* now for the top */
	if ((vc4_state->src_y & 0xffff) && vc4_state->src_y < (state->fb->height << 16))
		height++;

	/* and the bottom */
	if (((vc4_state->src_y + vc4_state->src_h[0]) & 0xffff) &&
	    vc4_state->src_y + vc4_state->src_h[0] < (state->fb->height << 16))
		height++;

	/* for YUV444 hardware wants double the width, otherwise it doesn't
	 * fetch full width of chroma
	 */
	if (format->drm == DRM_FORMAT_YUV444 || format->drm == DRM_FORMAT_YVU444)
		width <<= 1;

	/* Don't waste cycles mixing with plane alpha if the set alpha
	 * is opaque or there is no per-pixel alpha information.
	 * In any case we use the alpha property value as the fixed alpha.
	 */
	mix_plane_alpha = state->alpha != DRM_BLEND_ALPHA_OPAQUE &&
			  fb->format->has_alpha;

	/* Control Word 0: Scaling Configuration & Element Validity*/
	vc4_dlist_write(vc4_state,
			SCALER6_CTL0_VALID |
			VC4_SET_FIELD(tiling, SCALER6_CTL0_ADDR_MODE) |
			vc4_hvs6_get_alpha_mask_mode(state) |
			(vc4_state->is_unity ? SCALER6_CTL0_UNITY : 0) |
			VC4_SET_FIELD(format->pixel_order_hvs5, SCALER6_CTL0_ORDERRGBA) |
			VC4_SET_FIELD(scl1, SCALER6_CTL0_SCL1_MODE) |
			VC4_SET_FIELD(scl0, SCALER6_CTL0_SCL0_MODE) |
			VC4_SET_FIELD(hvs_format, SCALER6_CTL0_PIXEL_FORMAT));

	/* Position Word 0: Image Position */
	vc4_state->pos0_offset = vc4_state->dlist_count;
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(vc4_state->crtc_y, SCALER6_POS0_START_Y) |
			(rotation & DRM_MODE_REFLECT_X ? SCALER6_POS0_HFLIP : 0) |
			VC4_SET_FIELD(vc4_state->crtc_x, SCALER6_POS0_START_X));

	/* Control Word 2: Alpha Value & CSC */
	vc4_dlist_write(vc4_state,
			vc6_plane_get_csc_mode(vc4_state) |
			vc4_hvs5_get_alpha_blend_mode(state) |
			(mix_plane_alpha ? SCALER6_CTL2_ALPHA_MIX : 0) |
			VC4_SET_FIELD(state->alpha >> 4, SCALER5_CTL2_ALPHA));

	/* Position Word 1: Scaled Image Dimensions */
	if (!vc4_state->is_unity)
		vc4_dlist_write(vc4_state,
				VC4_SET_FIELD(vc4_state->crtc_h - 1,
					      SCALER6_POS1_SCL_LINES) |
				VC4_SET_FIELD(vc4_state->crtc_w - 1,
					      SCALER6_POS1_SCL_WIDTH));

	/* Position Word 2: Source Image Size */
	vc4_state->pos2_offset = vc4_state->dlist_count;
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(height - 1,
				      SCALER6_POS2_SRC_LINES) |
			VC4_SET_FIELD(width - 1,
				      SCALER6_POS2_SRC_WIDTH));

	/* Position Word 3: Context */
	vc4_dlist_write(vc4_state, 0xc0c0c0c0);

	/*
	 * TODO: This only covers Raster Scan Order planes
	 */
	for (i = 0; i < num_planes; i++) {
		struct drm_gem_dma_object *bo = drm_fb_dma_get_gem_obj(fb, i);
		dma_addr_t paddr = bo->dma_addr + fb->offsets[i] + offsets[i];

		/* Pointer Word 0 */
		vc4_state->ptr0_offset[i] = vc4_state->dlist_count;
		vc4_dlist_write(vc4_state,
				(rotation & DRM_MODE_REFLECT_Y ? SCALER6_PTR0_VFLIP : 0) |
				/*
				 * The UPM buffer will be allocated in
				 * vc6_plane_allocate_upm().
				 */
				VC4_SET_FIELD(upper_32_bits(paddr) & 0xff,
					      SCALER6_PTR0_UPPER_ADDR));

		/* Pointer Word 1 */
		vc4_dlist_write(vc4_state, lower_32_bits(paddr));

		/* Pointer Word 2 */
		if (base_format_mod != DRM_FORMAT_MOD_BROADCOM_SAND128 &&
		    base_format_mod != DRM_FORMAT_MOD_BROADCOM_SAND256) {
			vc4_dlist_write(vc4_state,
					VC4_SET_FIELD(fb->pitches[i],
						      SCALER6_PTR2_PITCH));
		} else {
			vc4_dlist_write(vc4_state, pitch0);
		}
	}

	/*
	 * Palette Word 0
	 * TODO: We're not using the palette mode
	 */

	/*
	 * Trans Word 0
	 * TODO: It's only relevant if we set the trans_rgb bit in the
	 * control word 0, and we don't at the moment.
	 */

	vc4_state->lbm_offset = 0;

	if (!vc4_state->is_unity || fb->format->is_yuv) {
		/*
		 * Reserve a slot for the LBM Base Address. The real value will
		 * be set when calling vc4_plane_allocate_lbm().
		 */
		if (vc4_state->y_scaling[0] != VC4_SCALING_NONE ||
		    vc4_state->y_scaling[1] != VC4_SCALING_NONE) {
			vc4_state->lbm_offset = vc4_state->dlist_count;
			vc4_dlist_counter_increment(vc4_state);
		}

		if (vc4_state->x_scaling[0] != VC4_SCALING_NONE ||
		    vc4_state->x_scaling[1] != VC4_SCALING_NONE ||
		    vc4_state->y_scaling[0] != VC4_SCALING_NONE ||
		    vc4_state->y_scaling[1] != VC4_SCALING_NONE) {
			if (num_planes > 1)
				/*
				 * Emit Cb/Cr as channel 0 and Y as channel
				 * 1. This matches how we set up scl0/scl1
				 * above.
				 */
				vc4_write_scaling_parameters(state, 1);

			vc4_write_scaling_parameters(state, 0);
		}

		/*
		 * If any PPF setup was done, then all the kernel
		 * pointers get uploaded.
		 */
		if (vc4_state->x_scaling[0] == VC4_SCALING_PPF ||
		    vc4_state->y_scaling[0] == VC4_SCALING_PPF ||
		    vc4_state->x_scaling[1] == VC4_SCALING_PPF ||
		    vc4_state->y_scaling[1] == VC4_SCALING_PPF) {
			u32 kernel =
				VC4_SET_FIELD(vc4->hvs->mitchell_netravali_filter.start,
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

	vc4_dlist_write(vc4_state, SCALER6_CTL0_END);

	vc4_state->dlist[0] |=
		VC4_SET_FIELD(vc4_state->dlist_count, SCALER6_CTL0_NEXT);

	/* crtc_* are already clipped coordinates. */
	covers_screen = vc4_state->crtc_x == 0 && vc4_state->crtc_y == 0 &&
			vc4_state->crtc_w == state->crtc->mode.hdisplay &&
			vc4_state->crtc_h == state->crtc->mode.vdisplay;

	/*
	 * Background fill might be necessary when the plane has per-pixel
	 * alpha content or a non-opaque plane alpha and could blend from the
	 * background or does not cover the entire screen.
	 */
	vc4_state->needs_bg_fill = fb->format->has_alpha || !covers_screen ||
				   state->alpha != DRM_BLEND_ALPHA_OPAQUE;

	/*
	 * Flag the dlist as initialized to avoid checking it twice in case
	 * the async update check already called vc4_plane_mode_set() and
	 * decided to fallback to sync update because async update was not
	 * possible.
	 */
	vc4_state->dlist_initialized = 1;

	vc4_plane_calc_load(state);

	drm_dbg_driver(drm, "[PLANE:%d:%s] Computed DLIST size: %u\n",
		       plane->base.id, plane->name, vc4_state->dlist_count);

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
	struct vc4_dev *vc4 = to_vc4_dev(plane->dev);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(new_plane_state);
	int ret;

	vc4_state->dlist_count = 0;

	if (!plane_enabled(new_plane_state)) {
		struct drm_plane_state *old_plane_state =
				drm_atomic_get_old_plane_state(state, plane);

		if (vc4->gen >= VC4_GEN_6_C && old_plane_state &&
		    plane_enabled(old_plane_state)) {
			vc6_plane_free_upm(new_plane_state);
		}
		return 0;
	}

	if (vc4->gen >= VC4_GEN_6_C)
		ret = vc6_plane_mode_set(plane, new_plane_state);
	else
		ret = vc4_plane_mode_set(plane, new_plane_state);
	if (ret)
		return ret;

	if (!vc4_state->src_w[0] || !vc4_state->src_h[0] ||
	    !vc4_state->crtc_w || !vc4_state->crtc_h)
		return 0;

	ret = vc4_plane_allocate_lbm(new_plane_state);
	if (ret)
		return ret;

	if (vc4->gen >= VC4_GEN_6_C) {
		ret = vc6_plane_allocate_upm(new_plane_state);
		if (ret)
			return ret;
	}

	return 0;
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
	int idx;

	if (!drm_dev_enter(plane->dev, &idx))
		goto out;

	vc4_state->hw_dlist = dlist;

	/* Can't memcpy_toio() because it needs to be 32-bit writes. */
	for (i = 0; i < vc4_state->dlist_count; i++)
		writel(vc4_state->dlist[i], &dlist[i]);

	drm_dev_exit(idx);

out:
	return vc4_state->dlist_count;
}

u32 vc4_plane_dlist_size(const struct drm_plane_state *state)
{
	const struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	return vc4_state->dlist_count;
}

/* Updates the plane to immediately (well, once the FIFO needs
 * refilling) scan out from at a new framebuffer.
 */
void vc4_plane_async_set_fb(struct drm_plane *plane, struct drm_framebuffer *fb)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(plane->state);
	struct drm_gem_dma_object *bo = drm_fb_dma_get_gem_obj(fb, 0);
	struct vc4_dev *vc4 = to_vc4_dev(plane->dev);
	dma_addr_t dma_addr = bo->dma_addr + fb->offsets[0];
	int idx;

	if (!drm_dev_enter(plane->dev, &idx))
		return;

	/* We're skipping the address adjustment for negative origin,
	 * because this is only called on the primary plane.
	 */
	WARN_ON_ONCE(plane->state->crtc_x < 0 || plane->state->crtc_y < 0);

	if (vc4->gen == VC4_GEN_6_C) {
		u32 value;

		value = vc4_state->dlist[vc4_state->ptr0_offset[0]] &
					~SCALER6_PTR0_UPPER_ADDR_MASK;
		value |= VC4_SET_FIELD(upper_32_bits(dma_addr) & 0xff,
				       SCALER6_PTR0_UPPER_ADDR);

		writel(value, &vc4_state->hw_dlist[vc4_state->ptr0_offset[0]]);
		vc4_state->dlist[vc4_state->ptr0_offset[0]] = value;

		value = lower_32_bits(dma_addr);
		writel(value, &vc4_state->hw_dlist[vc4_state->ptr0_offset[0] + 1]);
		vc4_state->dlist[vc4_state->ptr0_offset[0] + 1] = value;
	} else {
		u32 addr;

		addr = (u32)dma_addr;

		/* Write the new address into the hardware immediately.  The
		 * scanout will start from this address as soon as the FIFO
		 * needs to refill with pixels.
		 */
		writel(addr, &vc4_state->hw_dlist[vc4_state->ptr0_offset[0]]);

		/* Also update the CPU-side dlist copy, so that any later
		 * atomic updates that don't do a new modeset on our plane
		 * also use our updated address.
		 */
		vc4_state->dlist[vc4_state->ptr0_offset[0]] = addr;
	}

	drm_dev_exit(idx);
}

static void vc4_plane_atomic_async_update(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct vc4_plane_state *vc4_state, *new_vc4_state;
	int idx;

	if (!drm_dev_enter(plane->dev, &idx))
		return;

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
	vc4_state->needs_bg_fill = new_vc4_state->needs_bg_fill;

	/* Update the current vc4_state pos0, pos2 and ptr0 dlist entries. */
	vc4_state->dlist[vc4_state->pos0_offset] =
		new_vc4_state->dlist[vc4_state->pos0_offset];
	vc4_state->dlist[vc4_state->pos2_offset] =
		new_vc4_state->dlist[vc4_state->pos2_offset];
	vc4_state->dlist[vc4_state->ptr0_offset[0]] =
		new_vc4_state->dlist[vc4_state->ptr0_offset[0]];

	/* Note that we can't just call vc4_plane_write_dlist()
	 * because that would smash the context data that the HVS is
	 * currently using.
	 */
	writel(vc4_state->dlist[vc4_state->pos0_offset],
	       &vc4_state->hw_dlist[vc4_state->pos0_offset]);
	writel(vc4_state->dlist[vc4_state->pos2_offset],
	       &vc4_state->hw_dlist[vc4_state->pos2_offset]);
	writel(vc4_state->dlist[vc4_state->ptr0_offset[0]],
	       &vc4_state->hw_dlist[vc4_state->ptr0_offset[0]]);

	drm_dev_exit(idx);
}

static int vc4_plane_atomic_async_check(struct drm_plane *plane,
					struct drm_atomic_state *state, bool flip)
{
	struct vc4_dev *vc4 = to_vc4_dev(plane->dev);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct vc4_plane_state *old_vc4_state, *new_vc4_state;
	int ret;
	u32 i;

	if (vc4->gen <= VC4_GEN_5)
		ret = vc4_plane_mode_set(plane, new_plane_state);
	else
		ret = vc6_plane_mode_set(plane, new_plane_state);
	if (ret)
		return ret;

	old_vc4_state = to_vc4_plane_state(plane->state);
	new_vc4_state = to_vc4_plane_state(new_plane_state);

	if (!new_vc4_state->hw_dlist)
		return -EINVAL;

	if (old_vc4_state->dlist_count != new_vc4_state->dlist_count ||
	    old_vc4_state->pos0_offset != new_vc4_state->pos0_offset ||
	    old_vc4_state->pos2_offset != new_vc4_state->pos2_offset ||
	    old_vc4_state->ptr0_offset[0] != new_vc4_state->ptr0_offset[0] ||
	    vc4_lbm_size(plane->state) != vc4_lbm_size(new_plane_state))
		return -EINVAL;

	/* Only pos0, pos2 and ptr0 DWORDS can be updated in an async update
	 * if anything else has changed, fallback to a sync update.
	 */
	for (i = 0; i < new_vc4_state->dlist_count; i++) {
		if (i == new_vc4_state->pos0_offset ||
		    i == new_vc4_state->pos2_offset ||
		    i == new_vc4_state->ptr0_offset[0] ||
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
	int ret;

	if (!state->fb)
		return 0;

	bo = to_vc4_bo(&drm_fb_dma_get_gem_obj(state->fb, 0)->base);

	ret = drm_gem_plane_helper_prepare_fb(plane, state);
	if (ret)
		return ret;

	return vc4_bo_inc_usecnt(bo);
}

static void vc4_cleanup_fb(struct drm_plane *plane,
			   struct drm_plane_state *state)
{
	struct vc4_bo *bo;

	if (!state->fb)
		return;

	bo = to_vc4_bo(&drm_fb_dma_get_gem_obj(state->fb, 0)->base);
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
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_BGRX4444:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_RGB332:
	case DRM_FORMAT_BGR233:
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
	.reset = vc4_plane_reset,
	.atomic_duplicate_state = vc4_plane_duplicate_state,
	.atomic_destroy_state = vc4_plane_destroy_state,
	.format_mod_supported = vc4_format_mod_supported,
};

struct drm_plane *vc4_plane_init(struct drm_device *dev,
				 enum drm_plane_type type,
				 uint32_t possible_crtcs)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_plane *plane;
	struct vc4_plane *vc4_plane;
	u32 formats[ARRAY_SIZE(hvs_formats)];
	int num_formats = 0;
	unsigned i;
	static const uint64_t modifiers[] = {
		DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED,
		DRM_FORMAT_MOD_BROADCOM_SAND128,
		DRM_FORMAT_MOD_BROADCOM_SAND64,
		DRM_FORMAT_MOD_BROADCOM_SAND256,
		DRM_FORMAT_MOD_LINEAR,
		DRM_FORMAT_MOD_INVALID
	};

	for (i = 0; i < ARRAY_SIZE(hvs_formats); i++) {
		if (!hvs_formats[i].hvs5_only || vc4->gen >= VC4_GEN_5) {
			formats[num_formats] = hvs_formats[i].drm;
			num_formats++;
		}
	}

	vc4_plane = drmm_universal_plane_alloc(dev, struct vc4_plane, base,
					       possible_crtcs,
					       &vc4_plane_funcs,
					       formats, num_formats,
					       modifiers, type, NULL);
	if (IS_ERR(vc4_plane))
		return ERR_CAST(vc4_plane);
	plane = &vc4_plane->base;

	if (vc4->gen >= VC4_GEN_5)
		drm_plane_helper_add(plane, &vc5_plane_helper_funcs);
	else
		drm_plane_helper_add(plane, &vc4_plane_helper_funcs);

	drm_plane_create_alpha_property(plane);
	drm_plane_create_blend_mode_property(plane,
					     BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					     BIT(DRM_MODE_BLEND_PREMULTI) |
					     BIT(DRM_MODE_BLEND_COVERAGE));
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

	if (type == DRM_PLANE_TYPE_PRIMARY)
		drm_plane_create_zpos_immutable_property(plane, 0);

	return plane;
}

#define VC4_NUM_OVERLAY_PLANES	16

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
	for (i = 0; i < VC4_NUM_OVERLAY_PLANES; i++) {
		struct drm_plane *plane =
			vc4_plane_init(drm, DRM_PLANE_TYPE_OVERLAY,
				       GENMASK(drm->mode_config.num_crtc - 1, 0));

		if (IS_ERR(plane))
			continue;

		/* Create zpos property. Max of all the overlays + 1 primary +
		 * 1 cursor plane on a crtc.
		 */
		drm_plane_create_zpos_property(plane, i + 1, 1,
					       VC4_NUM_OVERLAY_PLANES + 1);
	}

	drm_for_each_crtc(crtc, drm) {
		/* Set up the legacy cursor after overlay initialization,
		 * since the zpos fallback is that planes are rendered by plane
		 * ID order, and that then puts the cursor on top.
		 */
		cursor_plane = vc4_plane_init(drm, DRM_PLANE_TYPE_CURSOR,
					      drm_crtc_mask(crtc));
		if (!IS_ERR(cursor_plane)) {
			crtc->cursor = cursor_plane;

			drm_plane_create_zpos_property(cursor_plane,
						       VC4_NUM_OVERLAY_PLANES + 1,
						       1,
						       VC4_NUM_OVERLAY_PLANES + 1);
		}
	}

	return 0;
}
