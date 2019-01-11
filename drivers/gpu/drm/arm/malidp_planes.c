/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * ARM Mali DP plane manipulation routines.
 */

#include <linux/iommu.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>

#include "malidp_hw.h"
#include "malidp_drv.h"

/* Layer specific register offsets */
#define MALIDP_LAYER_FORMAT		0x000
#define   LAYER_FORMAT_MASK		0x3f
#define MALIDP_LAYER_CONTROL		0x004
#define   LAYER_ENABLE			(1 << 0)
#define   LAYER_FLOWCFG_MASK		7
#define   LAYER_FLOWCFG(x)		(((x) & LAYER_FLOWCFG_MASK) << 1)
#define     LAYER_FLOWCFG_SCALE_SE	3
#define   LAYER_ROT_OFFSET		8
#define   LAYER_H_FLIP			(1 << 10)
#define   LAYER_V_FLIP			(1 << 11)
#define   LAYER_ROT_MASK		(0xf << 8)
#define   LAYER_COMP_MASK		(0x3 << 12)
#define   LAYER_COMP_PIXEL		(0x3 << 12)
#define   LAYER_COMP_PLANE		(0x2 << 12)
#define   LAYER_PMUL_ENABLE		(0x1 << 14)
#define   LAYER_ALPHA_OFFSET		(16)
#define   LAYER_ALPHA_MASK		(0xff)
#define   LAYER_ALPHA(x)		(((x) & LAYER_ALPHA_MASK) << LAYER_ALPHA_OFFSET)
#define MALIDP_LAYER_COMPOSE		0x008
#define MALIDP_LAYER_SIZE		0x00c
#define   LAYER_H_VAL(x)		(((x) & 0x1fff) << 0)
#define   LAYER_V_VAL(x)		(((x) & 0x1fff) << 16)
#define MALIDP_LAYER_COMP_SIZE		0x010
#define MALIDP_LAYER_OFFSET		0x014
#define MALIDP550_LS_ENABLE		0x01c
#define MALIDP550_LS_R1_IN_SIZE		0x020

/*
 * This 4-entry look-up-table is used to determine the full 8-bit alpha value
 * for formats with 1- or 2-bit alpha channels.
 * We set it to give 100%/0% opacity for 1-bit formats and 100%/66%/33%/0%
 * opacity for 2-bit formats.
 */
#define MALIDP_ALPHA_LUT 0xffaa5500

/* page sizes the MMU prefetcher can support */
#define MALIDP_MMU_PREFETCH_PARTIAL_PGSIZES	(SZ_4K | SZ_64K)
#define MALIDP_MMU_PREFETCH_FULL_PGSIZES	(SZ_1M | SZ_2M)

/* readahead for partial-frame prefetch */
#define MALIDP_MMU_PREFETCH_READAHEAD		8

static void malidp_de_plane_destroy(struct drm_plane *plane)
{
	struct malidp_plane *mp = to_malidp_plane(plane);

	drm_plane_cleanup(plane);
	kfree(mp);
}

/*
 * Replicate what the default ->reset hook does: free the state pointer and
 * allocate a new empty object. We just need enough space to store
 * a malidp_plane_state instead of a drm_plane_state.
 */
static void malidp_plane_reset(struct drm_plane *plane)
{
	struct malidp_plane_state *state = to_malidp_plane_state(plane->state);

	if (state)
		__drm_atomic_helper_plane_destroy_state(&state->base);
	kfree(state);
	plane->state = NULL;
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_plane_reset(plane, &state->base);
}

static struct
drm_plane_state *malidp_duplicate_plane_state(struct drm_plane *plane)
{
	struct malidp_plane_state *state, *m_state;

	if (!plane->state)
		return NULL;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	m_state = to_malidp_plane_state(plane->state);
	__drm_atomic_helper_plane_duplicate_state(plane, &state->base);
	state->rotmem_size = m_state->rotmem_size;
	state->format = m_state->format;
	state->n_planes = m_state->n_planes;

	state->mmu_prefetch_mode = m_state->mmu_prefetch_mode;
	state->mmu_prefetch_pgsize = m_state->mmu_prefetch_pgsize;

	return &state->base;
}

static void malidp_destroy_plane_state(struct drm_plane *plane,
				       struct drm_plane_state *state)
{
	struct malidp_plane_state *m_state = to_malidp_plane_state(state);

	__drm_atomic_helper_plane_destroy_state(state);
	kfree(m_state);
}

static const char * const prefetch_mode_names[] = {
	[MALIDP_PREFETCH_MODE_NONE] = "MMU_PREFETCH_NONE",
	[MALIDP_PREFETCH_MODE_PARTIAL] = "MMU_PREFETCH_PARTIAL",
	[MALIDP_PREFETCH_MODE_FULL] = "MMU_PREFETCH_FULL",
};

static void malidp_plane_atomic_print_state(struct drm_printer *p,
					    const struct drm_plane_state *state)
{
	struct malidp_plane_state *ms = to_malidp_plane_state(state);

	drm_printf(p, "\trotmem_size=%u\n", ms->rotmem_size);
	drm_printf(p, "\tformat_id=%u\n", ms->format);
	drm_printf(p, "\tn_planes=%u\n", ms->n_planes);
	drm_printf(p, "\tmmu_prefetch_mode=%s\n",
		   prefetch_mode_names[ms->mmu_prefetch_mode]);
	drm_printf(p, "\tmmu_prefetch_pgsize=%d\n", ms->mmu_prefetch_pgsize);
}

static const struct drm_plane_funcs malidp_de_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = malidp_de_plane_destroy,
	.reset = malidp_plane_reset,
	.atomic_duplicate_state = malidp_duplicate_plane_state,
	.atomic_destroy_state = malidp_destroy_plane_state,
	.atomic_print_state = malidp_plane_atomic_print_state,
};

static int malidp_se_check_scaling(struct malidp_plane *mp,
				   struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_existing_crtc_state(state->state, state->crtc);
	struct malidp_crtc_state *mc;
	u32 src_w, src_h;
	int ret;

	if (!crtc_state)
		return -EINVAL;

	mc = to_malidp_crtc_state(crtc_state);

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  0, INT_MAX, true, true);
	if (ret)
		return ret;

	if (state->rotation & MALIDP_ROTATED_MASK) {
		src_w = state->src_h >> 16;
		src_h = state->src_w >> 16;
	} else {
		src_w = state->src_w >> 16;
		src_h = state->src_h >> 16;
	}

	if ((state->crtc_w == src_w) && (state->crtc_h == src_h)) {
		/* Scaling not necessary for this plane. */
		mc->scaled_planes_mask &= ~(mp->layer->id);
		return 0;
	}

	if (mp->layer->id & (DE_SMART | DE_GRAPHICS2))
		return -EINVAL;

	mc->scaled_planes_mask |= mp->layer->id;
	/* Defer scaling requirements calculation to the crtc check. */
	return 0;
}

static u32 malidp_get_pgsize_bitmap(struct malidp_plane *mp)
{
	u32 pgsize_bitmap = 0;

	if (iommu_present(&platform_bus_type)) {
		struct iommu_domain *mmu_dom =
			iommu_get_domain_for_dev(mp->base.dev->dev);

		if (mmu_dom)
			pgsize_bitmap = mmu_dom->pgsize_bitmap;
	}

	return pgsize_bitmap;
}

/*
 * Check if the framebuffer is entirely made up of pages at least pgsize in
 * size. Only a heuristic: assumes that each scatterlist entry has been aligned
 * to the largest page size smaller than its length and that the MMU maps to
 * the largest page size possible.
 */
static bool malidp_check_pages_threshold(struct malidp_plane_state *ms,
					 u32 pgsize)
{
	int i;

	for (i = 0; i < ms->n_planes; i++) {
		struct drm_gem_object *obj;
		struct drm_gem_cma_object *cma_obj;
		struct sg_table *sgt;
		struct scatterlist *sgl;

		obj = drm_gem_fb_get_obj(ms->base.fb, i);
		cma_obj = to_drm_gem_cma_obj(obj);

		if (cma_obj->sgt)
			sgt = cma_obj->sgt;
		else
			sgt = obj->dev->driver->gem_prime_get_sg_table(obj);

		if (!sgt)
			return false;

		sgl = sgt->sgl;

		while (sgl) {
			if (sgl->length < pgsize) {
				if (!cma_obj->sgt)
					kfree(sgt);
				return false;
			}

			sgl = sg_next(sgl);
		}
		if (!cma_obj->sgt)
			kfree(sgt);
	}

	return true;
}

/*
 * Check if it is possible to enable partial-frame MMU prefetch given the
 * current format, AFBC state and rotation.
 */
static bool malidp_partial_prefetch_supported(u32 format, u64 modifier,
					      unsigned int rotation)
{
	bool afbc, sparse;

	/* rotation and horizontal flip not supported for partial prefetch */
	if (rotation & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_180 |
			DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_X))
		return false;

	afbc = modifier & DRM_FORMAT_MOD_ARM_AFBC(0);
	sparse = modifier & AFBC_FORMAT_MOD_SPARSE;

	switch (format) {
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_RGB565:
		/* always supported */
		return true;

	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_BGR565:
		/* supported, but if AFBC then must be sparse mode */
		return (!afbc) || (afbc && sparse);

	case DRM_FORMAT_BGR888:
		/* supported, but not for AFBC */
		return !afbc;

	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_YUV420:
		/* not supported */
		return false;

	default:
		return false;
	}
}

/*
 * Select the preferred MMU prefetch mode. Full-frame prefetch is preferred as
 * long as the framebuffer is all large pages. Otherwise partial-frame prefetch
 * is selected as long as it is supported for the current format. The selected
 * page size for prefetch is returned in pgsize_bitmap.
 */
static enum mmu_prefetch_mode malidp_mmu_prefetch_select_mode
		(struct malidp_plane_state *ms,	u32 *pgsize_bitmap)
{
	u32 pgsizes;

	/* get the full-frame prefetch page size(s) supported by the MMU */
	pgsizes = *pgsize_bitmap & MALIDP_MMU_PREFETCH_FULL_PGSIZES;

	while (pgsizes) {
		u32 largest_pgsize = 1 << __fls(pgsizes);

		if (malidp_check_pages_threshold(ms, largest_pgsize)) {
			*pgsize_bitmap = largest_pgsize;
			return MALIDP_PREFETCH_MODE_FULL;
		}

		pgsizes -= largest_pgsize;
	}

	/* get the partial-frame prefetch page size(s) supported by the MMU */
	pgsizes = *pgsize_bitmap & MALIDP_MMU_PREFETCH_PARTIAL_PGSIZES;

	if (malidp_partial_prefetch_supported(ms->base.fb->format->format,
					      ms->base.fb->modifier,
					      ms->base.rotation)) {
		/* partial prefetch using the smallest page size */
		*pgsize_bitmap = 1 << __ffs(pgsizes);
		return MALIDP_PREFETCH_MODE_PARTIAL;
	}
	*pgsize_bitmap = 0;
	return MALIDP_PREFETCH_MODE_NONE;
}

static u32 malidp_calc_mmu_control_value(enum mmu_prefetch_mode mode,
					 u8 readahead, u8 n_planes, u32 pgsize)
{
	u32 mmu_ctrl = 0;

	if (mode != MALIDP_PREFETCH_MODE_NONE) {
		mmu_ctrl |= MALIDP_MMU_CTRL_EN;

		if (mode == MALIDP_PREFETCH_MODE_PARTIAL) {
			mmu_ctrl |= MALIDP_MMU_CTRL_MODE;
			mmu_ctrl |= MALIDP_MMU_CTRL_PP_NUM_REQ(readahead);
		}

		if (pgsize == SZ_64K || pgsize == SZ_2M) {
			int i;

			for (i = 0; i < n_planes; i++)
				mmu_ctrl |= MALIDP_MMU_CTRL_PX_PS(i);
		}
	}

	return mmu_ctrl;
}

static void malidp_de_prefetch_settings(struct malidp_plane *mp,
					struct malidp_plane_state *ms)
{
	if (!mp->layer->mmu_ctrl_offset)
		return;

	/* get the page sizes supported by the MMU */
	ms->mmu_prefetch_pgsize = malidp_get_pgsize_bitmap(mp);
	ms->mmu_prefetch_mode  =
		malidp_mmu_prefetch_select_mode(ms, &ms->mmu_prefetch_pgsize);
}

static int malidp_de_plane_check(struct drm_plane *plane,
				 struct drm_plane_state *state)
{
	struct malidp_plane *mp = to_malidp_plane(plane);
	struct malidp_plane_state *ms = to_malidp_plane_state(state);
	bool rotated = state->rotation & MALIDP_ROTATED_MASK;
	struct drm_framebuffer *fb;
	u16 pixel_alpha = state->pixel_blend_mode;
	int i, ret;
	unsigned int block_w, block_h;

	if (!state->crtc || !state->fb)
		return 0;

	fb = state->fb;

	ms->format = malidp_hw_get_format_id(&mp->hwdev->hw->map,
					     mp->layer->id,
					     fb->format->format);
	if (ms->format == MALIDP_INVALID_FORMAT_ID)
		return -EINVAL;

	ms->n_planes = fb->format->num_planes;
	for (i = 0; i < ms->n_planes; i++) {
		u8 alignment = malidp_hw_get_pitch_align(mp->hwdev, rotated);

		if ((fb->pitches[i] * drm_format_info_block_height(fb->format, i))
				& (alignment - 1)) {
			DRM_DEBUG_KMS("Invalid pitch %u for plane %d\n",
				      fb->pitches[i], i);
			return -EINVAL;
		}
	}

	block_w = drm_format_info_block_width(fb->format, 0);
	block_h = drm_format_info_block_height(fb->format, 0);
	if (fb->width % block_w || fb->height % block_h) {
		DRM_DEBUG_KMS("Buffer width/height needs to be a multiple of tile sizes");
		return -EINVAL;
	}
	if ((state->src_x >> 16) % block_w || (state->src_y >> 16) % block_h) {
		DRM_DEBUG_KMS("Plane src_x/src_y needs to be a multiple of tile sizes");
		return -EINVAL;
	}

	if ((state->crtc_w > mp->hwdev->max_line_size) ||
	    (state->crtc_h > mp->hwdev->max_line_size) ||
	    (state->crtc_w < mp->hwdev->min_line_size) ||
	    (state->crtc_h < mp->hwdev->min_line_size))
		return -EINVAL;

	/*
	 * DP550/650 video layers can accept 3 plane formats only if
	 * fb->pitches[1] == fb->pitches[2] since they don't have a
	 * third plane stride register.
	 */
	if (ms->n_planes == 3 &&
	    !(mp->hwdev->hw->features & MALIDP_DEVICE_LV_HAS_3_STRIDES) &&
	    (state->fb->pitches[1] != state->fb->pitches[2]))
		return -EINVAL;

	ret = malidp_se_check_scaling(mp, state);
	if (ret)
		return ret;

	/* validate the rotation constraints for each layer */
	if (state->rotation != DRM_MODE_ROTATE_0) {
		if (mp->layer->rot == ROTATE_NONE)
			return -EINVAL;
		if ((mp->layer->rot == ROTATE_COMPRESSED) && !(fb->modifier))
			return -EINVAL;
		/*
		 * packed RGB888 / BGR888 can't be rotated or flipped
		 * unless they are stored in a compressed way
		 */
		if ((fb->format->format == DRM_FORMAT_RGB888 ||
		     fb->format->format == DRM_FORMAT_BGR888) && !(fb->modifier))
			return -EINVAL;
	}

	ms->rotmem_size = 0;
	if (state->rotation & MALIDP_ROTATED_MASK) {
		int val;

		val = mp->hwdev->hw->rotmem_required(mp->hwdev, state->crtc_w,
						     state->crtc_h,
						     fb->format->format);
		if (val < 0)
			return val;

		ms->rotmem_size = val;
	}

	/* HW can't support plane + pixel blending */
	if ((state->alpha != DRM_BLEND_ALPHA_OPAQUE) &&
	    (pixel_alpha != DRM_MODE_BLEND_PIXEL_NONE) &&
	    fb->format->has_alpha)
		return -EINVAL;

	malidp_de_prefetch_settings(mp, ms);

	return 0;
}

static void malidp_de_set_plane_pitches(struct malidp_plane *mp,
					int num_planes, unsigned int pitches[3])
{
	int i;
	int num_strides = num_planes;

	if (!mp->layer->stride_offset)
		return;

	if (num_planes == 3)
		num_strides = (mp->hwdev->hw->features &
			       MALIDP_DEVICE_LV_HAS_3_STRIDES) ? 3 : 2;

	/*
	 * The drm convention for pitch is that it needs to cover width * cpp,
	 * but our hardware wants the pitch/stride to cover all rows included
	 * in a tile.
	 */
	for (i = 0; i < num_strides; ++i) {
		unsigned int block_h = drm_format_info_block_height(mp->base.state->fb->format, i);

		malidp_hw_write(mp->hwdev, pitches[i] * block_h,
				mp->layer->base +
				mp->layer->stride_offset + i * 4);
	}
}

static const s16
malidp_yuv2rgb_coeffs[][DRM_COLOR_RANGE_MAX][MALIDP_COLORADJ_NUM_COEFFS] = {
	[DRM_COLOR_YCBCR_BT601][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		1192,    0, 1634,
		1192, -401, -832,
		1192, 2066,    0,
		  64,  512,  512
	},
	[DRM_COLOR_YCBCR_BT601][DRM_COLOR_YCBCR_FULL_RANGE] = {
		1024,    0, 1436,
		1024, -352, -731,
		1024, 1815,    0,
		   0,  512,  512
	},
	[DRM_COLOR_YCBCR_BT709][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		1192,    0, 1836,
		1192, -218, -546,
		1192, 2163,    0,
		  64,  512,  512
	},
	[DRM_COLOR_YCBCR_BT709][DRM_COLOR_YCBCR_FULL_RANGE] = {
		1024,    0, 1613,
		1024, -192, -479,
		1024, 1900,    0,
		   0,  512,  512
	},
	[DRM_COLOR_YCBCR_BT2020][DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		1024,    0, 1476,
		1024, -165, -572,
		1024, 1884,    0,
		   0,  512,  512
	},
	[DRM_COLOR_YCBCR_BT2020][DRM_COLOR_YCBCR_FULL_RANGE] = {
		1024,    0, 1510,
		1024, -168, -585,
		1024, 1927,    0,
		   0,  512,  512
	}
};

static void malidp_de_set_color_encoding(struct malidp_plane *plane,
					 enum drm_color_encoding enc,
					 enum drm_color_range range)
{
	unsigned int i;

	for (i = 0; i < MALIDP_COLORADJ_NUM_COEFFS; i++) {
		/* coefficients are signed, two's complement values */
		malidp_hw_write(plane->hwdev, malidp_yuv2rgb_coeffs[enc][range][i],
				plane->layer->base + plane->layer->yuv2rgb_offset +
				i * 4);
	}
}

static void malidp_de_set_mmu_control(struct malidp_plane *mp,
				      struct malidp_plane_state *ms)
{
	u32 mmu_ctrl;

	/* check hardware supports MMU prefetch */
	if (!mp->layer->mmu_ctrl_offset)
		return;

	mmu_ctrl = malidp_calc_mmu_control_value(ms->mmu_prefetch_mode,
						 MALIDP_MMU_PREFETCH_READAHEAD,
						 ms->n_planes,
						 ms->mmu_prefetch_pgsize);

	malidp_hw_write(mp->hwdev, mmu_ctrl,
			mp->layer->base + mp->layer->mmu_ctrl_offset);
}

static void malidp_de_plane_update(struct drm_plane *plane,
				   struct drm_plane_state *old_state)
{
	struct malidp_plane *mp;
	struct malidp_plane_state *ms = to_malidp_plane_state(plane->state);
	struct drm_plane_state *state = plane->state;
	u16 pixel_alpha = state->pixel_blend_mode;
	u8 plane_alpha = state->alpha >> 8;
	u32 src_w, src_h, dest_w, dest_h, val;
	int i;

	mp = to_malidp_plane(plane);

	/* convert src values from Q16 fixed point to integer */
	src_w = state->src_w >> 16;
	src_h = state->src_h >> 16;
	dest_w = state->crtc_w;
	dest_h = state->crtc_h;

	val = malidp_hw_read(mp->hwdev, mp->layer->base);
	val = (val & ~LAYER_FORMAT_MASK) | ms->format;
	malidp_hw_write(mp->hwdev, val, mp->layer->base);

	for (i = 0; i < ms->n_planes; i++) {
		/* calculate the offset for the layer's plane registers */
		u16 ptr = mp->layer->ptr + (i << 4);
		dma_addr_t fb_addr = drm_fb_cma_get_gem_addr(state->fb,
							     state, i);

		malidp_hw_write(mp->hwdev, lower_32_bits(fb_addr), ptr);
		malidp_hw_write(mp->hwdev, upper_32_bits(fb_addr), ptr + 4);
	}

	malidp_de_set_mmu_control(mp, ms);

	malidp_de_set_plane_pitches(mp, ms->n_planes,
				    state->fb->pitches);

	if ((plane->state->color_encoding != old_state->color_encoding) ||
	    (plane->state->color_range != old_state->color_range))
		malidp_de_set_color_encoding(mp, plane->state->color_encoding,
					     plane->state->color_range);

	malidp_hw_write(mp->hwdev, LAYER_H_VAL(src_w) | LAYER_V_VAL(src_h),
			mp->layer->base + MALIDP_LAYER_SIZE);

	malidp_hw_write(mp->hwdev, LAYER_H_VAL(dest_w) | LAYER_V_VAL(dest_h),
			mp->layer->base + MALIDP_LAYER_COMP_SIZE);

	malidp_hw_write(mp->hwdev, LAYER_H_VAL(state->crtc_x) |
			LAYER_V_VAL(state->crtc_y),
			mp->layer->base + MALIDP_LAYER_OFFSET);

	if (mp->layer->id == DE_SMART) {
		/*
		 * Enable the first rectangle in the SMART layer to be
		 * able to use it as a drm plane.
		 */
		malidp_hw_write(mp->hwdev, 1,
				mp->layer->base + MALIDP550_LS_ENABLE);
		malidp_hw_write(mp->hwdev,
				LAYER_H_VAL(src_w) | LAYER_V_VAL(src_h),
				mp->layer->base + MALIDP550_LS_R1_IN_SIZE);
	}

	/* first clear the rotation bits */
	val = malidp_hw_read(mp->hwdev, mp->layer->base + MALIDP_LAYER_CONTROL);
	val &= ~LAYER_ROT_MASK;

	/* setup the rotation and axis flip bits */
	if (state->rotation & DRM_MODE_ROTATE_MASK)
		val |= ilog2(plane->state->rotation & DRM_MODE_ROTATE_MASK) <<
		       LAYER_ROT_OFFSET;
	if (state->rotation & DRM_MODE_REFLECT_X)
		val |= LAYER_H_FLIP;
	if (state->rotation & DRM_MODE_REFLECT_Y)
		val |= LAYER_V_FLIP;

	val &= ~(LAYER_COMP_MASK | LAYER_PMUL_ENABLE | LAYER_ALPHA(0xff));

	if (state->alpha != DRM_BLEND_ALPHA_OPAQUE) {
		val |= LAYER_COMP_PLANE;
	} else if (state->fb->format->has_alpha) {
		/* We only care about blend mode if the format has alpha */
		switch (pixel_alpha) {
		case DRM_MODE_BLEND_PREMULTI:
			val |= LAYER_COMP_PIXEL | LAYER_PMUL_ENABLE;
			break;
		case DRM_MODE_BLEND_COVERAGE:
			val |= LAYER_COMP_PIXEL;
			break;
		}
	}
	val |= LAYER_ALPHA(plane_alpha);

	val &= ~LAYER_FLOWCFG(LAYER_FLOWCFG_MASK);
	if (state->crtc) {
		struct malidp_crtc_state *m =
			to_malidp_crtc_state(state->crtc->state);

		if (m->scaler_config.scale_enable &&
		    m->scaler_config.plane_src_id == mp->layer->id)
			val |= LAYER_FLOWCFG(LAYER_FLOWCFG_SCALE_SE);
	}

	/* set the 'enable layer' bit */
	val |= LAYER_ENABLE;

	malidp_hw_write(mp->hwdev, val,
			mp->layer->base + MALIDP_LAYER_CONTROL);
}

static void malidp_de_plane_disable(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct malidp_plane *mp = to_malidp_plane(plane);

	malidp_hw_clearbits(mp->hwdev,
			    LAYER_ENABLE | LAYER_FLOWCFG(LAYER_FLOWCFG_MASK),
			    mp->layer->base + MALIDP_LAYER_CONTROL);
}

static const struct drm_plane_helper_funcs malidp_de_plane_helper_funcs = {
	.atomic_check = malidp_de_plane_check,
	.atomic_update = malidp_de_plane_update,
	.atomic_disable = malidp_de_plane_disable,
};

int malidp_de_planes_init(struct drm_device *drm)
{
	struct malidp_drm *malidp = drm->dev_private;
	const struct malidp_hw_regmap *map = &malidp->dev->hw->map;
	struct malidp_plane *plane = NULL;
	enum drm_plane_type plane_type;
	unsigned long crtcs = 1 << drm->mode_config.num_crtc;
	unsigned long flags = DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_180 |
			      DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y;
	unsigned int blend_caps = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
				  BIT(DRM_MODE_BLEND_PREMULTI)   |
				  BIT(DRM_MODE_BLEND_COVERAGE);
	u32 *formats;
	int ret, i, j, n;

	formats = kcalloc(map->n_pixel_formats, sizeof(*formats), GFP_KERNEL);
	if (!formats) {
		ret = -ENOMEM;
		goto cleanup;
	}

	for (i = 0; i < map->n_layers; i++) {
		u8 id = map->layers[i].id;

		plane = kzalloc(sizeof(*plane), GFP_KERNEL);
		if (!plane) {
			ret = -ENOMEM;
			goto cleanup;
		}

		/* build the list of DRM supported formats based on the map */
		for (n = 0, j = 0;  j < map->n_pixel_formats; j++) {
			if ((map->pixel_formats[j].layer & id) == id)
				formats[n++] = map->pixel_formats[j].format;
		}

		plane_type = (i == 0) ? DRM_PLANE_TYPE_PRIMARY :
					DRM_PLANE_TYPE_OVERLAY;
		ret = drm_universal_plane_init(drm, &plane->base, crtcs,
					       &malidp_de_plane_funcs, formats,
					       n, NULL, plane_type, NULL);
		if (ret < 0)
			goto cleanup;

		drm_plane_helper_add(&plane->base,
				     &malidp_de_plane_helper_funcs);
		plane->hwdev = malidp->dev;
		plane->layer = &map->layers[i];

		drm_plane_create_alpha_property(&plane->base);
		drm_plane_create_blend_mode_property(&plane->base, blend_caps);

		if (id == DE_SMART) {
			/* Skip the features which the SMART layer doesn't have. */
			continue;
		}

		drm_plane_create_rotation_property(&plane->base, DRM_MODE_ROTATE_0, flags);
		malidp_hw_write(malidp->dev, MALIDP_ALPHA_LUT,
				plane->layer->base + MALIDP_LAYER_COMPOSE);

		/* Attach the YUV->RGB property only to video layers */
		if (id & (DE_VIDEO1 | DE_VIDEO2)) {
			/* default encoding for YUV->RGB is BT601 NARROW */
			enum drm_color_encoding enc = DRM_COLOR_YCBCR_BT601;
			enum drm_color_range range = DRM_COLOR_YCBCR_LIMITED_RANGE;

			ret = drm_plane_create_color_properties(&plane->base,
					BIT(DRM_COLOR_YCBCR_BT601) | \
					BIT(DRM_COLOR_YCBCR_BT709) | \
					BIT(DRM_COLOR_YCBCR_BT2020),
					BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) | \
					BIT(DRM_COLOR_YCBCR_FULL_RANGE),
					enc, range);
			if (!ret)
				/* program the HW registers */
				malidp_de_set_color_encoding(plane, enc, range);
			else
				DRM_WARN("Failed to create video layer %d color properties\n", id);
		}
	}

	kfree(formats);

	return 0;

cleanup:
	kfree(formats);

	return ret;
}
