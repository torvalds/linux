/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "uapi/drm/vc4_drm.h"
#include "vc4_drv.h"
#include "vc4_regs.h"

enum vc4_scaling_mode {
	VC4_SCALING_NONE,
	VC4_SCALING_TPZ,
	VC4_SCALING_PPF,
};

struct vc4_plane_state {
	struct drm_plane_state base;
	/* System memory copy of the display list for this element, computed
	 * at atomic_check time.
	 */
	u32 *dlist;
	u32 dlist_size; /* Number of dwords allocated for the display list */
	u32 dlist_count; /* Number of used dwords in the display list. */

	/* Offset in the dlist to various words, for pageflip or
	 * cursor updates.
	 */
	u32 pos0_offset;
	u32 pos2_offset;
	u32 ptr0_offset;

	/* Offset where the plane's dlist was last stored in the
	 * hardware at vc4_crtc_atomic_flush() time.
	 */
	u32 __iomem *hw_dlist;

	/* Clipped coordinates of the plane on the display. */
	int crtc_x, crtc_y, crtc_w, crtc_h;
	/* Clipped area being scanned from in the FB. */
	u32 src_x, src_y;

	u32 src_w[2], src_h[2];

	/* Scaling selection for the RGB/Y plane and the Cb/Cr planes. */
	enum vc4_scaling_mode x_scaling[2], y_scaling[2];
	bool is_unity;
	bool is_yuv;

	/* Offset to start scanning out from the start of the plane's
	 * BO.
	 */
	u32 offsets[3];

	/* Our allocation in LBM for temporary storage during scaling. */
	struct drm_mm_node lbm;
};

static inline struct vc4_plane_state *
to_vc4_plane_state(struct drm_plane_state *state)
{
	return (struct vc4_plane_state *)state;
}

static const struct hvs_format {
	u32 drm; /* DRM_FORMAT_* */
	u32 hvs; /* HVS_FORMAT_* */
	u32 pixel_order;
	bool has_alpha;
} hvs_formats[] = {
	{
		.drm = DRM_FORMAT_XRGB8888, .hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ABGR, .has_alpha = false,
	},
	{
		.drm = DRM_FORMAT_ARGB8888, .hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ABGR, .has_alpha = true,
	},
	{
		.drm = DRM_FORMAT_ABGR8888, .hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ARGB, .has_alpha = true,
	},
	{
		.drm = DRM_FORMAT_XBGR8888, .hvs = HVS_PIXEL_FORMAT_RGBA8888,
		.pixel_order = HVS_PIXEL_ORDER_ARGB, .has_alpha = false,
	},
	{
		.drm = DRM_FORMAT_RGB565, .hvs = HVS_PIXEL_FORMAT_RGB565,
		.pixel_order = HVS_PIXEL_ORDER_XRGB, .has_alpha = false,
	},
	{
		.drm = DRM_FORMAT_BGR565, .hvs = HVS_PIXEL_FORMAT_RGB565,
		.pixel_order = HVS_PIXEL_ORDER_XBGR, .has_alpha = false,
	},
	{
		.drm = DRM_FORMAT_ARGB1555, .hvs = HVS_PIXEL_FORMAT_RGBA5551,
		.pixel_order = HVS_PIXEL_ORDER_ABGR, .has_alpha = true,
	},
	{
		.drm = DRM_FORMAT_XRGB1555, .hvs = HVS_PIXEL_FORMAT_RGBA5551,
		.pixel_order = HVS_PIXEL_ORDER_ABGR, .has_alpha = false,
	},
	{
		.drm = DRM_FORMAT_RGB888, .hvs = HVS_PIXEL_FORMAT_RGB888,
		.pixel_order = HVS_PIXEL_ORDER_XRGB, .has_alpha = false,
	},
	{
		.drm = DRM_FORMAT_BGR888, .hvs = HVS_PIXEL_FORMAT_RGB888,
		.pixel_order = HVS_PIXEL_ORDER_XBGR, .has_alpha = false,
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
		.drm = DRM_FORMAT_NV16,
		.hvs = HVS_PIXEL_FORMAT_YCBCR_YUV422_2PLANE,
		.pixel_order = HVS_PIXEL_ORDER_XYCBCR,
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
	if (dst > src)
		return VC4_SCALING_PPF;
	else if (dst < src)
		return VC4_SCALING_TPZ;
	else
		return VC4_SCALING_NONE;
}

static bool plane_enabled(struct drm_plane_state *state)
{
	return state->fb && state->crtc;
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

	if (vc4_state->lbm.allocated) {
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

	plane->state = &vc4_state->base;
	vc4_state->base.plane = plane;
}

static void vc4_dlist_write(struct vc4_plane_state *vc4_state, u32 val)
{
	if (vc4_state->dlist_count == vc4_state->dlist_size) {
		u32 new_size = max(4u, vc4_state->dlist_count * 2);
		u32 *new_dlist = kmalloc(new_size * 4, GFP_KERNEL);

		if (!new_dlist)
			return;
		memcpy(new_dlist, vc4_state->dlist, vc4_state->dlist_count * 4);

		kfree(vc4_state->dlist);
		vc4_state->dlist = new_dlist;
		vc4_state->dlist_size = new_size;
	}

	vc4_state->dlist[vc4_state->dlist_count++] = val;
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

static int vc4_plane_setup_clipping_and_scaling(struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *bo = drm_fb_cma_get_gem_obj(fb, 0);
	u32 subpixel_src_mask = (1 << 16) - 1;
	u32 format = fb->format->format;
	int num_planes = fb->format->num_planes;
	u32 h_subsample = 1;
	u32 v_subsample = 1;
	int i;

	for (i = 0; i < num_planes; i++)
		vc4_state->offsets[i] = bo->paddr + fb->offsets[i];

	/* We don't support subpixel source positioning for scaling. */
	if ((state->src_x & subpixel_src_mask) ||
	    (state->src_y & subpixel_src_mask) ||
	    (state->src_w & subpixel_src_mask) ||
	    (state->src_h & subpixel_src_mask)) {
		return -EINVAL;
	}

	vc4_state->src_x = state->src_x >> 16;
	vc4_state->src_y = state->src_y >> 16;
	vc4_state->src_w[0] = state->src_w >> 16;
	vc4_state->src_h[0] = state->src_h >> 16;

	vc4_state->crtc_x = state->crtc_x;
	vc4_state->crtc_y = state->crtc_y;
	vc4_state->crtc_w = state->crtc_w;
	vc4_state->crtc_h = state->crtc_h;

	vc4_state->x_scaling[0] = vc4_get_scaling_mode(vc4_state->src_w[0],
						       vc4_state->crtc_w);
	vc4_state->y_scaling[0] = vc4_get_scaling_mode(vc4_state->src_h[0],
						       vc4_state->crtc_h);

	if (num_planes > 1) {
		vc4_state->is_yuv = true;

		h_subsample = drm_format_horz_chroma_subsampling(format);
		v_subsample = drm_format_vert_chroma_subsampling(format);
		vc4_state->src_w[1] = vc4_state->src_w[0] / h_subsample;
		vc4_state->src_h[1] = vc4_state->src_h[0] / v_subsample;

		vc4_state->x_scaling[1] =
			vc4_get_scaling_mode(vc4_state->src_w[1],
					     vc4_state->crtc_w);
		vc4_state->y_scaling[1] =
			vc4_get_scaling_mode(vc4_state->src_h[1],
					     vc4_state->crtc_h);

		/* YUV conversion requires that scaling be enabled,
		 * even on a plane that's otherwise 1:1.  Choose TPZ
		 * for simplicity.
		 */
		if (vc4_state->x_scaling[0] == VC4_SCALING_NONE)
			vc4_state->x_scaling[0] = VC4_SCALING_TPZ;
		if (vc4_state->y_scaling[0] == VC4_SCALING_NONE)
			vc4_state->y_scaling[0] = VC4_SCALING_TPZ;
	}

	vc4_state->is_unity = (vc4_state->x_scaling[0] == VC4_SCALING_NONE &&
			       vc4_state->y_scaling[0] == VC4_SCALING_NONE &&
			       vc4_state->x_scaling[1] == VC4_SCALING_NONE &&
			       vc4_state->y_scaling[1] == VC4_SCALING_NONE);

	/* No configuring scaling on the cursor plane, since it gets
	   non-vblank-synced updates, and scaling requires requires
	   LBM changes which have to be vblank-synced.
	 */
	if (plane->type == DRM_PLANE_TYPE_CURSOR && !vc4_state->is_unity)
		return -EINVAL;

	/* Clamp the on-screen start x/y to 0.  The hardware doesn't
	 * support negative y, and negative x wastes bandwidth.
	 */
	if (vc4_state->crtc_x < 0) {
		for (i = 0; i < num_planes; i++) {
			u32 cpp = fb->format->cpp[i];
			u32 subs = ((i == 0) ? 1 : h_subsample);

			vc4_state->offsets[i] += (cpp *
						  (-vc4_state->crtc_x) / subs);
		}
		vc4_state->src_w[0] += vc4_state->crtc_x;
		vc4_state->src_w[1] += vc4_state->crtc_x / h_subsample;
		vc4_state->crtc_x = 0;
	}

	if (vc4_state->crtc_y < 0) {
		for (i = 0; i < num_planes; i++) {
			u32 subs = ((i == 0) ? 1 : v_subsample);

			vc4_state->offsets[i] += (fb->pitches[i] *
						  (-vc4_state->crtc_y) / subs);
		}
		vc4_state->src_h[0] += vc4_state->crtc_y;
		vc4_state->src_h[1] += vc4_state->crtc_y / v_subsample;
		vc4_state->crtc_y = 0;
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
	/* This is the worst case number.  One of the two sizes will
	 * be used depending on the scaling configuration.
	 */
	u32 pix_per_line = max(vc4_state->src_w[0], (u32)vc4_state->crtc_w);
	u32 lbm;

	if (!vc4_state->is_yuv) {
		if (vc4_state->is_unity)
			return 0;
		else if (vc4_state->y_scaling[0] == VC4_SCALING_TPZ)
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

	lbm = roundup(lbm, 32);

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
	int num_planes = drm_format_num_planes(format->drm);
	u32 scl0, scl1, pitch0;
	u32 lbm_size, tiling;
	unsigned long irqflags;
	int ret, i;

	ret = vc4_plane_setup_clipping_and_scaling(state);
	if (ret)
		return ret;

	/* Allocate the LBM memory that the HVS will use for temporary
	 * storage due to our scaling/format conversion.
	 */
	lbm_size = vc4_lbm_size(state);
	if (lbm_size) {
		if (!vc4_state->lbm.allocated) {
			spin_lock_irqsave(&vc4->hvs->mm_lock, irqflags);
			ret = drm_mm_insert_node_generic(&vc4->hvs->lbm_mm,
							 &vc4_state->lbm,
							 lbm_size, 32, 0, 0);
			spin_unlock_irqrestore(&vc4->hvs->mm_lock, irqflags);
		} else {
			WARN_ON_ONCE(lbm_size != vc4_state->lbm.size);
		}
	}

	if (ret)
		return ret;

	/* SCL1 is used for Cb/Cr scaling of planar formats.  For RGB
	 * and 4:4:4, scl1 should be set to scl0 so both channels of
	 * the scaler do the same thing.  For YUV, the Y plane needs
	 * to be put in channel 1 and Cb/Cr in channel 0, so we swap
	 * the scl fields here.
	 */
	if (num_planes == 1) {
		scl0 = vc4_get_scl_field(state, 1);
		scl1 = scl0;
	} else {
		scl0 = vc4_get_scl_field(state, 1);
		scl1 = vc4_get_scl_field(state, 0);
	}

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		tiling = SCALER_CTL0_TILING_LINEAR;
		pitch0 = VC4_SET_FIELD(fb->pitches[0], SCALER_SRC_PITCH);
		break;

	case DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED: {
		/* For T-tiled, the FB pitch is "how many bytes from
		 * one row to the next, such that pitch * tile_h ==
		 * tile_size * tiles_per_row."
		 */
		u32 tile_size_shift = 12; /* T tiles are 4kb */
		u32 tile_h_shift = 5; /* 16 and 32bpp are 32 pixels high */
		u32 tiles_w = fb->pitches[0] >> (tile_size_shift - tile_h_shift);

		tiling = SCALER_CTL0_TILING_256B_OR_T;

		pitch0 = (VC4_SET_FIELD(0, SCALER_PITCH0_TILE_Y_OFFSET) |
			  VC4_SET_FIELD(0, SCALER_PITCH0_TILE_WIDTH_L) |
			  VC4_SET_FIELD(tiles_w, SCALER_PITCH0_TILE_WIDTH_R));
		break;
	}

	default:
		DRM_DEBUG_KMS("Unsupported FB tiling flag 0x%16llx",
			      (long long)fb->modifier);
		return -EINVAL;
	}

	/* Control word */
	vc4_dlist_write(vc4_state,
			SCALER_CTL0_VALID |
			(format->pixel_order << SCALER_CTL0_ORDER_SHIFT) |
			(format->hvs << SCALER_CTL0_PIXEL_FORMAT_SHIFT) |
			VC4_SET_FIELD(tiling, SCALER_CTL0_TILING) |
			(vc4_state->is_unity ? SCALER_CTL0_UNITY : 0) |
			VC4_SET_FIELD(scl0, SCALER_CTL0_SCL0) |
			VC4_SET_FIELD(scl1, SCALER_CTL0_SCL1));

	/* Position Word 0: Image Positions and Alpha Value */
	vc4_state->pos0_offset = vc4_state->dlist_count;
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(0xff, SCALER_POS0_FIXED_ALPHA) |
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

	/* Position Word 2: Source Image Size, Alpha Mode */
	vc4_state->pos2_offset = vc4_state->dlist_count;
	vc4_dlist_write(vc4_state,
			VC4_SET_FIELD(format->has_alpha ?
				      SCALER_POS2_ALPHA_MODE_PIPELINE :
				      SCALER_POS2_ALPHA_MODE_FIXED,
				      SCALER_POS2_ALPHA_MODE) |
			VC4_SET_FIELD(vc4_state->src_w[0], SCALER_POS2_WIDTH) |
			VC4_SET_FIELD(vc4_state->src_h[0], SCALER_POS2_HEIGHT));

	/* Position Word 3: Context.  Written by the HVS. */
	vc4_dlist_write(vc4_state, 0xc0c0c0c0);


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
		vc4_dlist_write(vc4_state,
				VC4_SET_FIELD(fb->pitches[i], SCALER_SRC_PITCH));
	}

	/* Colorspace conversion words */
	if (vc4_state->is_yuv) {
		vc4_dlist_write(vc4_state, SCALER_CSC0_ITR_R_601_5);
		vc4_dlist_write(vc4_state, SCALER_CSC1_ITR_R_601_5);
		vc4_dlist_write(vc4_state, SCALER_CSC2_ITR_R_601_5);
	}

	if (!vc4_state->is_unity) {
		/* LBM Base Address. */
		if (vc4_state->y_scaling[0] != VC4_SCALING_NONE ||
		    vc4_state->y_scaling[1] != VC4_SCALING_NONE) {
			vc4_dlist_write(vc4_state, vc4_state->lbm.start);
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
				  struct drm_plane_state *state)
{
	struct vc4_plane_state *vc4_state = to_vc4_plane_state(state);

	vc4_state->dlist_count = 0;

	if (plane_enabled(state))
		return vc4_plane_mode_set(plane, state);
	else
		return 0;
}

static void vc4_plane_atomic_update(struct drm_plane *plane,
				    struct drm_plane_state *old_state)
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

static int vc4_prepare_fb(struct drm_plane *plane,
			  struct drm_plane_state *state)
{
	struct vc4_bo *bo;
	struct dma_fence *fence;
	int ret;

	if ((plane->state->fb == state->fb) || !state->fb)
		return 0;

	bo = to_vc4_bo(&drm_fb_cma_get_gem_obj(state->fb, 0)->base);

	ret = vc4_bo_inc_usecnt(bo);
	if (ret)
		return ret;

	fence = reservation_object_get_excl_rcu(bo->resv);
	drm_atomic_set_fence_for_plane(state, fence);

	return 0;
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
};

static void vc4_plane_destroy(struct drm_plane *plane)
{
	drm_plane_helper_disable(plane);
	drm_plane_cleanup(plane);
}

/* Implements immediate (non-vblank-synced) updates of the cursor
 * position, or falls back to the atomic helper otherwise.
 */
static int
vc4_update_plane(struct drm_plane *plane,
		 struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t src_x, uint32_t src_y,
		 uint32_t src_w, uint32_t src_h,
		 struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_plane_state *plane_state;
	struct vc4_plane_state *vc4_state;

	if (plane != crtc->cursor)
		goto out;

	plane_state = plane->state;
	vc4_state = to_vc4_plane_state(plane_state);

	if (!plane_state)
		goto out;

	/* No configuring new scaling in the fast path. */
	if (crtc_w != plane_state->crtc_w ||
	    crtc_h != plane_state->crtc_h ||
	    src_w != plane_state->src_w ||
	    src_h != plane_state->src_h) {
		goto out;
	}

	if (fb != plane_state->fb) {
		drm_atomic_set_fb_for_plane(plane->state, fb);
		vc4_plane_async_set_fb(plane, fb);
	}

	/* Set the cursor's position on the screen.  This is the
	 * expected change from the drm_mode_cursor_universal()
	 * helper.
	 */
	plane_state->crtc_x = crtc_x;
	plane_state->crtc_y = crtc_y;

	/* Allow changing the start position within the cursor BO, if
	 * that matters.
	 */
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;

	/* Update the display list based on the new crtc_x/y. */
	vc4_plane_atomic_check(plane, plane_state);

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

	return 0;

out:
	return drm_atomic_helper_update_plane(plane, crtc, fb,
					      crtc_x, crtc_y,
					      crtc_w, crtc_h,
					      src_x, src_y,
					      src_w, src_h,
					      ctx);
}

static const struct drm_plane_funcs vc4_plane_funcs = {
	.update_plane = vc4_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = vc4_plane_destroy,
	.set_property = NULL,
	.reset = vc4_plane_reset,
	.atomic_duplicate_state = vc4_plane_duplicate_state,
	.atomic_destroy_state = vc4_plane_destroy_state,
};

struct drm_plane *vc4_plane_init(struct drm_device *dev,
				 enum drm_plane_type type)
{
	struct drm_plane *plane = NULL;
	struct vc4_plane *vc4_plane;
	u32 formats[ARRAY_SIZE(hvs_formats)];
	u32 num_formats = 0;
	int ret = 0;
	unsigned i;

	vc4_plane = devm_kzalloc(dev->dev, sizeof(*vc4_plane),
				 GFP_KERNEL);
	if (!vc4_plane)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ARRAY_SIZE(hvs_formats); i++) {
		/* Don't allow YUV in cursor planes, since that means
		 * tuning on the scaler, which we don't allow for the
		 * cursor.
		 */
		if (type != DRM_PLANE_TYPE_CURSOR ||
		    hvs_formats[i].hvs < HVS_PIXEL_FORMAT_YCBCR_YUV420_3PLANE) {
			formats[num_formats++] = hvs_formats[i].drm;
		}
	}
	plane = &vc4_plane->base;
	ret = drm_universal_plane_init(dev, plane, 0,
				       &vc4_plane_funcs,
				       formats, num_formats,
				       NULL, type, NULL);

	drm_plane_helper_add(plane, &vc4_plane_helper_funcs);

	return plane;
}
