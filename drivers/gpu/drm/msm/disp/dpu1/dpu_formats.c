// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <uapi/drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

#include "msm_media_info.h"
#include "dpu_kms.h"
#include "dpu_formats.h"

#define DPU_UBWC_PLANE_SIZE_ALIGNMENT	4096

#define DPU_MAX_IMG_WIDTH		0x3FFF
#define DPU_MAX_IMG_HEIGHT		0x3FFF

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

static int _dpu_format_get_media_color_ubwc(const struct msm_format *fmt)
{
	static const struct dpu_media_color_map dpu_media_ubwc_map[] = {
		{DRM_FORMAT_ABGR8888, COLOR_FMT_RGBA8888_UBWC},
		{DRM_FORMAT_ARGB8888, COLOR_FMT_RGBA8888_UBWC},
		{DRM_FORMAT_XBGR8888, COLOR_FMT_RGBA8888_UBWC},
		{DRM_FORMAT_XRGB8888, COLOR_FMT_RGBA8888_UBWC},
		{DRM_FORMAT_ABGR2101010, COLOR_FMT_RGBA1010102_UBWC},
		{DRM_FORMAT_ARGB2101010, COLOR_FMT_RGBA1010102_UBWC},
		{DRM_FORMAT_XRGB2101010, COLOR_FMT_RGBA1010102_UBWC},
		{DRM_FORMAT_XBGR2101010, COLOR_FMT_RGBA1010102_UBWC},
		{DRM_FORMAT_BGR565, COLOR_FMT_RGB565_UBWC},
	};
	int color_fmt = -1;
	int i;

	if (fmt->pixel_format == DRM_FORMAT_NV12 ||
	    fmt->pixel_format == DRM_FORMAT_P010) {
		if (MSM_FORMAT_IS_DX(fmt)) {
			if (fmt->flags & MSM_FORMAT_FLAG_UNPACK_TIGHT)
				color_fmt = COLOR_FMT_NV12_BPP10_UBWC;
			else
				color_fmt = COLOR_FMT_P010_UBWC;
		} else
			color_fmt = COLOR_FMT_NV12_UBWC;
		return color_fmt;
	}

	for (i = 0; i < ARRAY_SIZE(dpu_media_ubwc_map); ++i)
		if (fmt->pixel_format == dpu_media_ubwc_map[i].format) {
			color_fmt = dpu_media_ubwc_map[i].color;
			break;
		}
	return color_fmt;
}

static int _dpu_format_get_plane_sizes_ubwc(
		const struct msm_format *fmt,
		const uint32_t width,
		const uint32_t height,
		struct dpu_hw_fmt_layout *layout)
{
	int i;
	int color;
	bool meta = MSM_FORMAT_IS_UBWC(fmt);

	memset(layout, 0, sizeof(struct dpu_hw_fmt_layout));
	layout->format = fmt;
	layout->width = width;
	layout->height = height;
	layout->num_planes = fmt->num_planes;

	color = _dpu_format_get_media_color_ubwc(fmt);
	if (color < 0) {
		DRM_ERROR("UBWC format not supported for fmt: %p4cc\n",
			  &fmt->pixel_format);
		return -EINVAL;
	}

	if (MSM_FORMAT_IS_YUV(layout->format)) {
		uint32_t y_sclines, uv_sclines;
		uint32_t y_meta_scanlines = 0;
		uint32_t uv_meta_scanlines = 0;

		layout->num_planes = 2;
		layout->plane_pitch[0] = VENUS_Y_STRIDE(color, width);
		y_sclines = VENUS_Y_SCANLINES(color, height);
		layout->plane_size[0] = MSM_MEDIA_ALIGN(layout->plane_pitch[0] *
			y_sclines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		layout->plane_pitch[1] = VENUS_UV_STRIDE(color, width);
		uv_sclines = VENUS_UV_SCANLINES(color, height);
		layout->plane_size[1] = MSM_MEDIA_ALIGN(layout->plane_pitch[1] *
			uv_sclines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		if (!meta)
			goto done;

		layout->num_planes += 2;
		layout->plane_pitch[2] = VENUS_Y_META_STRIDE(color, width);
		y_meta_scanlines = VENUS_Y_META_SCANLINES(color, height);
		layout->plane_size[2] = MSM_MEDIA_ALIGN(layout->plane_pitch[2] *
			y_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		layout->plane_pitch[3] = VENUS_UV_META_STRIDE(color, width);
		uv_meta_scanlines = VENUS_UV_META_SCANLINES(color, height);
		layout->plane_size[3] = MSM_MEDIA_ALIGN(layout->plane_pitch[3] *
			uv_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

	} else {
		uint32_t rgb_scanlines, rgb_meta_scanlines;

		layout->num_planes = 1;

		layout->plane_pitch[0] = VENUS_RGB_STRIDE(color, width);
		rgb_scanlines = VENUS_RGB_SCANLINES(color, height);
		layout->plane_size[0] = MSM_MEDIA_ALIGN(layout->plane_pitch[0] *
			rgb_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		if (!meta)
			goto done;
		layout->num_planes += 2;
		layout->plane_pitch[2] = VENUS_RGB_META_STRIDE(color, width);
		rgb_meta_scanlines = VENUS_RGB_META_SCANLINES(color, height);
		layout->plane_size[2] = MSM_MEDIA_ALIGN(layout->plane_pitch[2] *
			rgb_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);
	}

done:
	for (i = 0; i < DPU_MAX_PLANES; i++)
		layout->total_size += layout->plane_size[i];

	return 0;
}

static int _dpu_format_get_plane_sizes_linear(
		const struct msm_format *fmt,
		const uint32_t width,
		const uint32_t height,
		struct dpu_hw_fmt_layout *layout,
		const uint32_t *pitches)
{
	int i;

	memset(layout, 0, sizeof(struct dpu_hw_fmt_layout));
	layout->format = fmt;
	layout->width = width;
	layout->height = height;
	layout->num_planes = fmt->num_planes;

	/* Due to memset above, only need to set planes of interest */
	if (fmt->fetch_type == MDP_PLANE_INTERLEAVED) {
		layout->num_planes = 1;
		layout->plane_size[0] = width * height * layout->format->bpp;
		layout->plane_pitch[0] = width * layout->format->bpp;
	} else {
		uint32_t v_subsample, h_subsample;
		uint32_t chroma_samp;
		uint32_t bpp = 1;

		chroma_samp = fmt->chroma_sample;
		_dpu_get_v_h_subsample_rate(chroma_samp, &v_subsample,
				&h_subsample);

		if (width % h_subsample || height % v_subsample) {
			DRM_ERROR("mismatch in subsample vs dimensions\n");
			return -EINVAL;
		}

		if ((fmt->pixel_format == DRM_FORMAT_NV12) &&
			(MSM_FORMAT_IS_DX(fmt)))
			bpp = 2;
		layout->plane_pitch[0] = width * bpp;
		layout->plane_pitch[1] = layout->plane_pitch[0] / h_subsample;
		layout->plane_size[0] = layout->plane_pitch[0] * height;
		layout->plane_size[1] = layout->plane_pitch[1] *
				(height / v_subsample);

		if (fmt->fetch_type == MDP_PLANE_PSEUDO_PLANAR) {
			layout->num_planes = 2;
			layout->plane_size[1] *= 2;
			layout->plane_pitch[1] *= 2;
		} else {
			/* planar */
			layout->num_planes = 3;
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
		if (pitches && layout->plane_pitch[i] < pitches[i])
			layout->plane_pitch[i] = pitches[i];
	}

	for (i = 0; i < DPU_MAX_PLANES; i++)
		layout->total_size += layout->plane_size[i];

	return 0;
}

static int dpu_format_get_plane_sizes(
		const struct msm_format *fmt,
		const uint32_t w,
		const uint32_t h,
		struct dpu_hw_fmt_layout *layout,
		const uint32_t *pitches)
{
	if (!layout || !fmt) {
		DRM_ERROR("invalid pointer\n");
		return -EINVAL;
	}

	if ((w > DPU_MAX_IMG_WIDTH) || (h > DPU_MAX_IMG_HEIGHT)) {
		DRM_ERROR("image dimensions outside max range\n");
		return -ERANGE;
	}

	if (MSM_FORMAT_IS_UBWC(fmt) || MSM_FORMAT_IS_TILE(fmt))
		return _dpu_format_get_plane_sizes_ubwc(fmt, w, h, layout);

	return _dpu_format_get_plane_sizes_linear(fmt, w, h, layout, pitches);
}

static int _dpu_format_populate_addrs_ubwc(
		struct msm_gem_address_space *aspace,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	uint32_t base_addr = 0;
	bool meta;

	if (!fb || !layout) {
		DRM_ERROR("invalid pointers\n");
		return -EINVAL;
	}

	if (aspace)
		base_addr = msm_framebuffer_iova(fb, aspace, 0);
	if (!base_addr) {
		DRM_ERROR("failed to retrieve base addr\n");
		return -EFAULT;
	}

	meta = MSM_FORMAT_IS_UBWC(layout->format);

	/* Per-format logic for verifying active planes */
	if (MSM_FORMAT_IS_YUV(layout->format)) {
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
			return 0;

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
			return 0;

		layout->plane_addr[2] = base_addr;
		layout->plane_addr[3] = 0;
	}
	return 0;
}

static int _dpu_format_populate_addrs_linear(
		struct msm_gem_address_space *aspace,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	unsigned int i;

	/* Can now check the pitches given vs pitches expected */
	for (i = 0; i < layout->num_planes; ++i) {
		if (layout->plane_pitch[i] > fb->pitches[i]) {
			DRM_ERROR("plane %u expected pitch %u, fb %u\n",
				i, layout->plane_pitch[i], fb->pitches[i]);
			return -EINVAL;
		}
	}

	/* Populate addresses for simple formats here */
	for (i = 0; i < layout->num_planes; ++i) {
		if (aspace)
			layout->plane_addr[i] =
				msm_framebuffer_iova(fb, aspace, i);
		if (!layout->plane_addr[i]) {
			DRM_ERROR("failed to retrieve base addr\n");
			return -EFAULT;
		}
	}

	return 0;
}

int dpu_format_populate_layout(
		struct msm_gem_address_space *aspace,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	int ret;

	if (!fb || !layout) {
		DRM_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	if ((fb->width > DPU_MAX_IMG_WIDTH) ||
			(fb->height > DPU_MAX_IMG_HEIGHT)) {
		DRM_ERROR("image dimensions outside max range\n");
		return -ERANGE;
	}

	layout->format = msm_framebuffer_format(fb);

	/* Populate the plane sizes etc via get_format */
	ret = dpu_format_get_plane_sizes(layout->format, fb->width, fb->height,
			layout, fb->pitches);
	if (ret)
		return ret;

	/* Populate the addresses given the fb */
	if (MSM_FORMAT_IS_UBWC(layout->format) ||
			MSM_FORMAT_IS_TILE(layout->format))
		ret = _dpu_format_populate_addrs_ubwc(aspace, fb, layout);
	else
		ret = _dpu_format_populate_addrs_linear(aspace, fb, layout);

	return ret;
}

int dpu_format_check_modified_format(
		const struct msm_kms *kms,
		const struct msm_format *fmt,
		const struct drm_mode_fb_cmd2 *cmd,
		struct drm_gem_object **bos)
{
	const struct drm_format_info *info;
	struct dpu_hw_fmt_layout layout;
	uint32_t bos_total_size = 0;
	int ret, i;

	if (!fmt || !cmd || !bos) {
		DRM_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	info = drm_format_info(fmt->pixel_format);
	if (!info)
		return -EINVAL;

	ret = dpu_format_get_plane_sizes(fmt, cmd->width, cmd->height,
			&layout, cmd->pitches);
	if (ret)
		return ret;

	for (i = 0; i < info->num_planes; i++) {
		if (!bos[i]) {
			DRM_ERROR("invalid handle for plane %d\n", i);
			return -EINVAL;
		}
		if ((i == 0) || (bos[i] != bos[0]))
			bos_total_size += bos[i]->size;
	}

	if (bos_total_size < layout.total_size) {
		DRM_ERROR("buffers total size too small %u expected %u\n",
				bos_total_size, layout.total_size);
		return -EINVAL;
	}

	return 0;
}
