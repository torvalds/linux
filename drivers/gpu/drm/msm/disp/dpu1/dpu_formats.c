// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <uapi/drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

#include "dpu_kms.h"
#include "dpu_formats.h"

#define DPU_UBWC_PLANE_SIZE_ALIGNMENT	4096

/*
 * struct dpu_media_color_map - maps drm format to media format
 * @format: DRM base pixel format
 * @color: Media API color related to DRM format
 */
struct dpu_media_color_map {
	uint32_t format;
	uint32_t color;
};

/* _dpu_get_v_h_subsample_rate - Get subsample rates for all formats we support
 *   Note: Not using the drm_format_*_subsampling since we have formats
 */
static void _dpu_get_v_h_subsample_rate(
	enum mdp_chroma_samp_type chroma_sample,
	uint32_t *v_sample,
	uint32_t *h_sample)
{
	if (!v_sample || !h_sample)
		return;

	switch (chroma_sample) {
	case CHROMA_H2V1:
		*v_sample = 1;
		*h_sample = 2;
		break;
	case CHROMA_H1V2:
		*v_sample = 2;
		*h_sample = 1;
		break;
	case CHROMA_420:
		*v_sample = 2;
		*h_sample = 2;
		break;
	default:
		*v_sample = 1;
		*h_sample = 1;
		break;
	}
}

static int _dpu_format_populate_plane_sizes_ubwc(
		const struct msm_format *fmt,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	bool meta = MSM_FORMAT_IS_UBWC(fmt);

	if (MSM_FORMAT_IS_YUV(fmt)) {
		unsigned int stride, sclines;
		unsigned int y_tile_width, y_tile_height;
		unsigned int y_meta_stride, y_meta_scanlines;
		unsigned int uv_meta_stride, uv_meta_scanlines;

		if (MSM_FORMAT_IS_DX(fmt)) {
			if (fmt->flags & MSM_FORMAT_FLAG_UNPACK_TIGHT) {
				/* can't use round_up() here because 192 is NPoT */
				stride = roundup(fb->width, 192);
				stride = round_up(stride * 4 / 3, 256);
				y_tile_width = 48;
			} else {
				stride = round_up(fb->width * 2, 256);
				y_tile_width = 32;
			}

			sclines = round_up(fb->height, 16);
			y_tile_height = 4;
		} else {
			stride = round_up(fb->width, 128);
			y_tile_width = 32;

			sclines = round_up(fb->height, 32);
			y_tile_height = 8;
		}

		layout->plane_pitch[0] = stride;
		layout->plane_size[0] = round_up(layout->plane_pitch[0] *
			sclines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		layout->plane_pitch[1] = stride;
		layout->plane_size[1] = round_up(layout->plane_pitch[1] *
			sclines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		if (!meta)
			return 0;

		y_meta_stride = DIV_ROUND_UP(fb->width, y_tile_width);
		layout->plane_pitch[2] = round_up(y_meta_stride, 64);

		y_meta_scanlines = DIV_ROUND_UP(fb->height, y_tile_height);
		y_meta_scanlines = round_up(y_meta_scanlines, 16);
		layout->plane_size[2] = round_up(layout->plane_pitch[2] *
			y_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		uv_meta_stride = DIV_ROUND_UP((fb->width+1)>>1, y_tile_width / 2);
		layout->plane_pitch[3] = round_up(uv_meta_stride, 64);

		uv_meta_scanlines = DIV_ROUND_UP((fb->height+1)>>1, y_tile_height);
		uv_meta_scanlines = round_up(uv_meta_scanlines, 16);
		layout->plane_size[3] = round_up(layout->plane_pitch[3] *
			uv_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);
	} else {
		unsigned int rgb_scanlines, rgb_meta_scanlines, rgb_meta_stride;

		layout->plane_pitch[0] = round_up(fb->width * fmt->bpp, 256);
		rgb_scanlines = round_up(fb->height, 16);
		layout->plane_size[0] = round_up(layout->plane_pitch[0] *
			rgb_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		if (!meta)
			return 0;

		/* uAPI leaves plane[1] empty and plane[2] as meta */
		layout->num_planes += 1;

		rgb_meta_stride = DIV_ROUND_UP(fb->width, 16);
		layout->plane_pitch[2] = round_up(rgb_meta_stride, 64);

		rgb_meta_scanlines = DIV_ROUND_UP(fb->height, 4);
		rgb_meta_scanlines = round_up(rgb_meta_scanlines, 16);

		layout->plane_size[2] = round_up(layout->plane_pitch[2] *
			rgb_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);
	}

	return 0;
}

static int _dpu_format_populate_plane_sizes_linear(
		const struct msm_format *fmt,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	int i;

	/* Due to memset above, only need to set planes of interest */
	if (fmt->fetch_type == MDP_PLANE_INTERLEAVED) {
		layout->plane_size[0] = fb->width * fb->height * fmt->bpp;
		layout->plane_pitch[0] = fb->width * fmt->bpp;
	} else {
		uint32_t v_subsample, h_subsample;
		uint32_t chroma_samp;
		uint32_t bpp = 1;

		chroma_samp = fmt->chroma_sample;
		_dpu_get_v_h_subsample_rate(chroma_samp, &v_subsample,
				&h_subsample);

		if (fb->width % h_subsample || fb->height % v_subsample) {
			DRM_ERROR("mismatch in subsample vs dimensions\n");
			return -EINVAL;
		}

		if ((fmt->pixel_format == DRM_FORMAT_NV12) &&
			(MSM_FORMAT_IS_DX(fmt)))
			bpp = 2;
		layout->plane_pitch[0] = fb->width * bpp;
		layout->plane_pitch[1] = layout->plane_pitch[0] / h_subsample;
		layout->plane_size[0] = layout->plane_pitch[0] * fb->height;
		layout->plane_size[1] = layout->plane_pitch[1] *
				(fb->height / v_subsample);

		if (fmt->fetch_type == MDP_PLANE_PSEUDO_PLANAR) {
			layout->plane_size[1] *= 2;
			layout->plane_pitch[1] *= 2;
		} else {
			/* planar */
			layout->plane_size[2] = layout->plane_size[1];
			layout->plane_pitch[2] = layout->plane_pitch[1];
		}
	}

	/*
	 * linear format: allow user allocated pitches if they are greater than
	 * the requirement.
	 * ubwc format: pitch values are computed uniformly across
	 * all the components based on ubwc specifications.
	 */
	for (i = 0; i < layout->num_planes && i < DPU_MAX_PLANES; ++i) {
		if (layout->plane_pitch[i] <= fb->pitches[i]) {
			layout->plane_pitch[i] = fb->pitches[i];
		} else {
			DRM_DEBUG("plane %u expected pitch %u, fb %u\n",
				  i, layout->plane_pitch[i], fb->pitches[i]);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * dpu_format_populate_plane_sizes - populate non-address part of the layout based on
 *                     fb, and format found in the fb
 * @fb:                framebuffer pointer
 * @layout:              format layout structure to populate
 *
 * Return: error code on failure or 0 if new addresses were populated
 */
int dpu_format_populate_plane_sizes(
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	const struct msm_format *fmt;
	int ret, i;

	if (!layout || !fb) {
		DRM_ERROR("invalid pointer\n");
		return -EINVAL;
	}

	if (fb->width > DPU_MAX_IMG_WIDTH ||
	    fb->height > DPU_MAX_IMG_HEIGHT) {
		DRM_ERROR("image dimensions outside max range\n");
		return -ERANGE;
	}

	fmt = msm_framebuffer_format(fb);

	memset(layout, 0, sizeof(struct dpu_hw_fmt_layout));
	layout->width = fb->width;
	layout->height = fb->height;
	layout->num_planes = fmt->num_planes;

	if (MSM_FORMAT_IS_UBWC(fmt) || MSM_FORMAT_IS_TILE(fmt))
		ret = _dpu_format_populate_plane_sizes_ubwc(fmt, fb, layout);
	else
		ret = _dpu_format_populate_plane_sizes_linear(fmt, fb, layout);

	if (ret)
		return ret;

	for (i = 0; i < DPU_MAX_PLANES; i++)
		layout->total_size += layout->plane_size[i];

	return 0;
}

static void _dpu_format_populate_addrs_ubwc(struct drm_framebuffer *fb,
					    struct dpu_hw_fmt_layout *layout)
{
	const struct msm_format *fmt;
	uint32_t base_addr = 0;
	bool meta;

	base_addr = msm_framebuffer_iova(fb, 0);

	fmt = msm_framebuffer_format(fb);
	meta = MSM_FORMAT_IS_UBWC(fmt);

	/* Per-format logic for verifying active planes */
	if (MSM_FORMAT_IS_YUV(fmt)) {
		/************************************************/
		/*      UBWC            **                      */
		/*      buffer          **      DPU PLANE       */
		/*      format          **                      */
		/************************************************/
		/* -------------------  ** -------------------- */
		/* |      Y meta     |  ** |    Y bitstream   | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/* |    Y bitstream  |  ** |  CbCr bitstream  | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/* |   Cbcr metadata |  ** |       Y meta     | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/* |  CbCr bitstream |  ** |     CbCr meta    | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/************************************************/

		/* configure Y bitstream plane */
		layout->plane_addr[0] = base_addr + layout->plane_size[2];

		/* configure CbCr bitstream plane */
		layout->plane_addr[1] = base_addr + layout->plane_size[0]
			+ layout->plane_size[2] + layout->plane_size[3];

		if (!meta)
			return;

		/* configure Y metadata plane */
		layout->plane_addr[2] = base_addr;

		/* configure CbCr metadata plane */
		layout->plane_addr[3] = base_addr + layout->plane_size[0]
			+ layout->plane_size[2];

	} else {
		/************************************************/
		/*      UBWC            **                      */
		/*      buffer          **      DPU PLANE       */
		/*      format          **                      */
		/************************************************/
		/* -------------------  ** -------------------- */
		/* |      RGB meta   |  ** |   RGB bitstream  | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/* |  RGB bitstream  |  ** |       NONE       | */
		/* |       data      |  ** |                  | */
		/* -------------------  ** -------------------- */
		/*                      ** |     RGB meta     | */
		/*                      ** |       plane      | */
		/*                      ** -------------------- */
		/************************************************/

		layout->plane_addr[0] = base_addr + layout->plane_size[2];
		layout->plane_addr[1] = 0;

		if (!meta)
			return;

		layout->plane_addr[2] = base_addr;
		layout->plane_addr[3] = 0;
	}
}

static void _dpu_format_populate_addrs_linear(struct drm_framebuffer *fb,
					      struct dpu_hw_fmt_layout *layout)
{
	unsigned int i;

	/* Populate addresses for simple formats here */
	for (i = 0; i < layout->num_planes; ++i)
		layout->plane_addr[i] = msm_framebuffer_iova(fb, i);
	}

/**
 * dpu_format_populate_addrs - populate buffer addresses based on
 *                     mmu, fb, and format found in the fb
 * @fb:                framebuffer pointer
 * @layout:            format layout structure to populate
 */
void dpu_format_populate_addrs(struct drm_framebuffer *fb,
			       struct dpu_hw_fmt_layout *layout)
{
	const struct msm_format *fmt;

	fmt = msm_framebuffer_format(fb);

	/* Populate the addresses given the fb */
	if (MSM_FORMAT_IS_UBWC(fmt) ||
			MSM_FORMAT_IS_TILE(fmt))
		_dpu_format_populate_addrs_ubwc(fb, layout);
	else
		_dpu_format_populate_addrs_linear(fb, layout);
}
