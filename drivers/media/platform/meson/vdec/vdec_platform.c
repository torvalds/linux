// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#include "vdec_platform.h"
#include "vdec.h"

#include "vdec_1.h"
#include "vdec_hevc.h"
#include "codec_mpeg12.h"
#include "codec_h264.h"
#include "codec_mpeg4.h"
#include "codec_mjpeg.h"
#include "codec_hevc.h"

static const struct amvdec_format vdec_formats_gxbb[] = {
	{
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.min_buffers = 16,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_hevc_ops,
		.codec_ops = &codec_hevc_ops,
		.firmware_path = "meson/gx/vh265_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_AM21C, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MJPEG,
		.min_buffers = 4,
		.max_buffers = 4,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mjpeg_ops,
		.firmware_path = "meson/gx/vmjpeg_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG4,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/vmpeg4_mc_5",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_H263,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/h263_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_XVID,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/vmpeg4_mc_5",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_H264,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_h264_ops,
		.firmware_path = "meson/gxbb/vh264_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG1,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.flags = V4L2_FMT_FLAG_FIXED_RESOLUTION,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg12_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG2,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.flags = V4L2_FMT_FLAG_FIXED_RESOLUTION,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg12_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	},
};

static const struct amvdec_format vdec_formats_gxl[] = {
	{
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.min_buffers = 16,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_hevc_ops,
		.codec_ops = &codec_hevc_ops,
		.firmware_path = "meson/gx/vh265_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_AM21C, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MJPEG,
		.min_buffers = 4,
		.max_buffers = 4,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mjpeg_ops,
		.firmware_path = "meson/gx/vmjpeg_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG4,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/vmpeg4_mc_5",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_H263,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/h263_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_XVID,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/vmpeg4_mc_5",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_H264,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_h264_ops,
		.firmware_path = "meson/gxl/vh264_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG1,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.flags = V4L2_FMT_FLAG_FIXED_RESOLUTION,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg12_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG2,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.flags = V4L2_FMT_FLAG_FIXED_RESOLUTION,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg12_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	},
};

static const struct amvdec_format vdec_formats_gxm[] = {
	{
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.min_buffers = 16,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_hevc_ops,
		.codec_ops = &codec_hevc_ops,
		.firmware_path = "meson/gx/vh265_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_AM21C, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MJPEG,
		.min_buffers = 4,
		.max_buffers = 4,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mjpeg_ops,
		.firmware_path = "meson/gx/vmjpeg_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG4,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/vmpeg4_mc_5",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_H263,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/h263_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_XVID,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg4_ops,
		.firmware_path = "meson/gx/vmpeg4_mc_5",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_H264,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_h264_ops,
		.firmware_path = "meson/gxm/vh264_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG1,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.flags = V4L2_FMT_FLAG_FIXED_RESOLUTION,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg12_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG2,
		.min_buffers = 8,
		.max_buffers = 8,
		.max_width = 1920,
		.max_height = 1080,
		.flags = V4L2_FMT_FLAG_FIXED_RESOLUTION,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_mpeg12_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUV420M, 0 },
	},
};

const struct vdec_platform vdec_platform_gxbb = {
	.formats = vdec_formats_gxbb,
	.num_formats = ARRAY_SIZE(vdec_formats_gxbb),
	.revision = VDEC_REVISION_GXBB,
};

const struct vdec_platform vdec_platform_gxl = {
	.formats = vdec_formats_gxl,
	.num_formats = ARRAY_SIZE(vdec_formats_gxl),
	.revision = VDEC_REVISION_GXL,
};

const struct vdec_platform vdec_platform_gxm = {
	.formats = vdec_formats_gxm,
	.num_formats = ARRAY_SIZE(vdec_formats_gxm),
	.revision = VDEC_REVISION_GXM,
};
