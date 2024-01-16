/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_MDP3_REGS_H__
#define __MTK_MDP3_REGS_H__

#include <linux/videodev2.h>
#include <media/videobuf2-core.h>
#include "mtk-img-ipi.h"

/*
 * MDP native color code
 * Plane count: 1, 2, 3
 * H-subsample: 0, 1, 2
 * V-subsample: 0, 1
 * Color group: 0-RGB, 1-YUV, 2-raw
 */
#define MDP_COLOR(PACKED, LOOSE, VIDEO, PLANE, HF, VF, BITS, GROUP, SWAP, ID)\
	(((PACKED) << 27) | ((LOOSE) << 26) | ((VIDEO) << 23) |\
	 ((PLANE) << 21) | ((HF) << 19) | ((VF) << 18) | ((BITS) << 8) |\
	 ((GROUP) << 6) | ((SWAP) << 5) | ((ID) << 0))

#define MDP_COLOR_IS_10BIT_PACKED(c)	((0x08000000 & (c)) >> 27)
#define MDP_COLOR_IS_10BIT_LOOSE(c)	(((0x0c000000 & (c)) >> 26) == 1)
#define MDP_COLOR_IS_10BIT_TILE(c)	(((0x0c000000 & (c)) >> 26) == 3)
#define MDP_COLOR_IS_UFP(c)		((0x02000000 & (c)) >> 25)
#define MDP_COLOR_IS_INTERLACED(c)	((0x01000000 & (c)) >> 24)
#define MDP_COLOR_IS_BLOCK_MODE(c)	((0x00800000 & (c)) >> 23)
#define MDP_COLOR_GET_PLANE_COUNT(c)	((0x00600000 & (c)) >> 21)
#define MDP_COLOR_GET_H_SUBSAMPLE(c)	((0x00180000 & (c)) >> 19)
#define MDP_COLOR_GET_V_SUBSAMPLE(c)	((0x00040000 & (c)) >> 18)
#define MDP_COLOR_BITS_PER_PIXEL(c)	((0x0003ff00 & (c)) >>  8)
#define MDP_COLOR_GET_GROUP(c)		((0x000000c0 & (c)) >>  6)
#define MDP_COLOR_IS_SWAPPED(c)		((0x00000020 & (c)) >>  5)
#define MDP_COLOR_GET_UNIQUE_ID(c)	((0x0000001f & (c)) >>  0)
#define MDP_COLOR_GET_HW_FORMAT(c)	((0x0000001f & (c)) >>  0)

#define MDP_COLOR_IS_RGB(c)		(MDP_COLOR_GET_GROUP(c) == 0)
#define MDP_COLOR_IS_YUV(c)		(MDP_COLOR_GET_GROUP(c) == 1)

enum mdp_color {
	MDP_COLOR_UNKNOWN	= 0,

	//MDP_COLOR_FULLG8,
	MDP_COLOR_FULLG8_RGGB	= MDP_COLOR(0, 0, 0, 1, 0, 0,  8, 2,  0, 21),
	MDP_COLOR_FULLG8_GRBG	= MDP_COLOR(0, 0, 0, 1, 0, 1,  8, 2,  0, 21),
	MDP_COLOR_FULLG8_GBRG	= MDP_COLOR(0, 0, 0, 1, 1, 0,  8, 2,  0, 21),
	MDP_COLOR_FULLG8_BGGR	= MDP_COLOR(0, 0, 0, 1, 1, 1,  8, 2,  0, 21),
	MDP_COLOR_FULLG8	= MDP_COLOR_FULLG8_BGGR,

	//MDP_COLOR_FULLG10,
	MDP_COLOR_FULLG10_RGGB	= MDP_COLOR(0, 0, 0, 1, 0, 0, 10, 2,  0, 21),
	MDP_COLOR_FULLG10_GRBG	= MDP_COLOR(0, 0, 0, 1, 0, 1, 10, 2,  0, 21),
	MDP_COLOR_FULLG10_GBRG	= MDP_COLOR(0, 0, 0, 1, 1, 0, 10, 2,  0, 21),
	MDP_COLOR_FULLG10_BGGR	= MDP_COLOR(0, 0, 0, 1, 1, 1, 10, 2,  0, 21),
	MDP_COLOR_FULLG10	= MDP_COLOR_FULLG10_BGGR,

	//MDP_COLOR_FULLG12,
	MDP_COLOR_FULLG12_RGGB	= MDP_COLOR(0, 0, 0, 1, 0, 0, 12, 2,  0, 21),
	MDP_COLOR_FULLG12_GRBG	= MDP_COLOR(0, 0, 0, 1, 0, 1, 12, 2,  0, 21),
	MDP_COLOR_FULLG12_GBRG	= MDP_COLOR(0, 0, 0, 1, 1, 0, 12, 2,  0, 21),
	MDP_COLOR_FULLG12_BGGR	= MDP_COLOR(0, 0, 0, 1, 1, 1, 12, 2,  0, 21),
	MDP_COLOR_FULLG12	= MDP_COLOR_FULLG12_BGGR,

	//MDP_COLOR_FULLG14,
	MDP_COLOR_FULLG14_RGGB	= MDP_COLOR(0, 0, 0, 1, 0, 0, 14, 2,  0, 21),
	MDP_COLOR_FULLG14_GRBG	= MDP_COLOR(0, 0, 0, 1, 0, 1, 14, 2,  0, 21),
	MDP_COLOR_FULLG14_GBRG	= MDP_COLOR(0, 0, 0, 1, 1, 0, 14, 2,  0, 21),
	MDP_COLOR_FULLG14_BGGR	= MDP_COLOR(0, 0, 0, 1, 1, 1, 14, 2,  0, 21),
	MDP_COLOR_FULLG14	= MDP_COLOR_FULLG14_BGGR,

	MDP_COLOR_UFO10		= MDP_COLOR(0, 0, 0, 1, 0, 0, 10, 2,  0, 24),

	//MDP_COLOR_BAYER8,
	MDP_COLOR_BAYER8_RGGB	= MDP_COLOR(0, 0, 0, 1, 0, 0,  8, 2,  0, 20),
	MDP_COLOR_BAYER8_GRBG	= MDP_COLOR(0, 0, 0, 1, 0, 1,  8, 2,  0, 20),
	MDP_COLOR_BAYER8_GBRG	= MDP_COLOR(0, 0, 0, 1, 1, 0,  8, 2,  0, 20),
	MDP_COLOR_BAYER8_BGGR	= MDP_COLOR(0, 0, 0, 1, 1, 1,  8, 2,  0, 20),
	MDP_COLOR_BAYER8	= MDP_COLOR_BAYER8_BGGR,

	//MDP_COLOR_BAYER10,
	MDP_COLOR_BAYER10_RGGB	= MDP_COLOR(0, 0, 0, 1, 0, 0, 10, 2,  0, 20),
	MDP_COLOR_BAYER10_GRBG	= MDP_COLOR(0, 0, 0, 1, 0, 1, 10, 2,  0, 20),
	MDP_COLOR_BAYER10_GBRG	= MDP_COLOR(0, 0, 0, 1, 1, 0, 10, 2,  0, 20),
	MDP_COLOR_BAYER10_BGGR	= MDP_COLOR(0, 0, 0, 1, 1, 1, 10, 2,  0, 20),
	MDP_COLOR_BAYER10	= MDP_COLOR_BAYER10_BGGR,

	//MDP_COLOR_BAYER12,
	MDP_COLOR_BAYER12_RGGB	= MDP_COLOR(0, 0, 0, 1, 0, 0, 12, 2,  0, 20),
	MDP_COLOR_BAYER12_GRBG	= MDP_COLOR(0, 0, 0, 1, 0, 1, 12, 2,  0, 20),
	MDP_COLOR_BAYER12_GBRG	= MDP_COLOR(0, 0, 0, 1, 1, 0, 12, 2,  0, 20),
	MDP_COLOR_BAYER12_BGGR	= MDP_COLOR(0, 0, 0, 1, 1, 1, 12, 2,  0, 20),
	MDP_COLOR_BAYER12	= MDP_COLOR_BAYER12_BGGR,

	//MDP_COLOR_BAYER14,
	MDP_COLOR_BAYER14_RGGB	= MDP_COLOR(0, 0, 0, 1, 0, 0, 14, 2,  0, 20),
	MDP_COLOR_BAYER14_GRBG	= MDP_COLOR(0, 0, 0, 1, 0, 1, 14, 2,  0, 20),
	MDP_COLOR_BAYER14_GBRG	= MDP_COLOR(0, 0, 0, 1, 1, 0, 14, 2,  0, 20),
	MDP_COLOR_BAYER14_BGGR	= MDP_COLOR(0, 0, 0, 1, 1, 1, 14, 2,  0, 20),
	MDP_COLOR_BAYER14	= MDP_COLOR_BAYER14_BGGR,

	MDP_COLOR_RGB48		= MDP_COLOR(0, 0, 0, 1, 0, 0, 48, 0,  0, 23),
	/* For bayer+mono raw-16 */
	MDP_COLOR_RGB565_RAW	= MDP_COLOR(0, 0, 0, 1, 0, 0, 16, 2,  0, 0),

	MDP_COLOR_BAYER8_UNPAK	= MDP_COLOR(0, 0, 0, 1, 0, 0,  8, 2,  0, 22),
	MDP_COLOR_BAYER10_UNPAK	= MDP_COLOR(0, 0, 0, 1, 0, 0, 10, 2,  0, 22),
	MDP_COLOR_BAYER12_UNPAK	= MDP_COLOR(0, 0, 0, 1, 0, 0, 12, 2,  0, 22),
	MDP_COLOR_BAYER14_UNPAK	= MDP_COLOR(0, 0, 0, 1, 0, 0, 14, 2,  0, 22),

	/* Unified formats */
	MDP_COLOR_GREY		= MDP_COLOR(0, 0, 0, 1, 0, 0,  8, 1,  0, 7),

	MDP_COLOR_RGB565	= MDP_COLOR(0, 0, 0, 1, 0, 0, 16, 0,  0, 0),
	MDP_COLOR_BGR565	= MDP_COLOR(0, 0, 0, 1, 0, 0, 16, 0,  1, 0),
	MDP_COLOR_RGB888	= MDP_COLOR(0, 0, 0, 1, 0, 0, 24, 0,  1, 1),
	MDP_COLOR_BGR888	= MDP_COLOR(0, 0, 0, 1, 0, 0, 24, 0,  0, 1),
	MDP_COLOR_RGBA8888	= MDP_COLOR(0, 0, 0, 1, 0, 0, 32, 0,  1, 2),
	MDP_COLOR_BGRA8888	= MDP_COLOR(0, 0, 0, 1, 0, 0, 32, 0,  0, 2),
	MDP_COLOR_ARGB8888	= MDP_COLOR(0, 0, 0, 1, 0, 0, 32, 0,  1, 3),
	MDP_COLOR_ABGR8888	= MDP_COLOR(0, 0, 0, 1, 0, 0, 32, 0,  0, 3),

	MDP_COLOR_UYVY		= MDP_COLOR(0, 0, 0, 1, 1, 0, 16, 1,  0, 4),
	MDP_COLOR_VYUY		= MDP_COLOR(0, 0, 0, 1, 1, 0, 16, 1,  1, 4),
	MDP_COLOR_YUYV		= MDP_COLOR(0, 0, 0, 1, 1, 0, 16, 1,  0, 5),
	MDP_COLOR_YVYU		= MDP_COLOR(0, 0, 0, 1, 1, 0, 16, 1,  1, 5),

	MDP_COLOR_I420		= MDP_COLOR(0, 0, 0, 3, 1, 1,  8, 1,  0, 8),
	MDP_COLOR_YV12		= MDP_COLOR(0, 0, 0, 3, 1, 1,  8, 1,  1, 8),
	MDP_COLOR_I422		= MDP_COLOR(0, 0, 0, 3, 1, 0,  8, 1,  0, 9),
	MDP_COLOR_YV16		= MDP_COLOR(0, 0, 0, 3, 1, 0,  8, 1,  1, 9),
	MDP_COLOR_I444		= MDP_COLOR(0, 0, 0, 3, 0, 0,  8, 1,  0, 10),
	MDP_COLOR_YV24		= MDP_COLOR(0, 0, 0, 3, 0, 0,  8, 1,  1, 10),

	MDP_COLOR_NV12		= MDP_COLOR(0, 0, 0, 2, 1, 1,  8, 1,  0, 12),
	MDP_COLOR_NV21		= MDP_COLOR(0, 0, 0, 2, 1, 1,  8, 1,  1, 12),
	MDP_COLOR_NV16		= MDP_COLOR(0, 0, 0, 2, 1, 0,  8, 1,  0, 13),
	MDP_COLOR_NV61		= MDP_COLOR(0, 0, 0, 2, 1, 0,  8, 1,  1, 13),
	MDP_COLOR_NV24		= MDP_COLOR(0, 0, 0, 2, 0, 0,  8, 1,  0, 14),
	MDP_COLOR_NV42		= MDP_COLOR(0, 0, 0, 2, 0, 0,  8, 1,  1, 14),

	/* MediaTek proprietary formats */
	/* UFO encoded block mode */
	MDP_COLOR_420_BLK_UFO	= MDP_COLOR(0, 0, 5, 2, 1, 1, 256, 1, 0, 12),
	/* Block mode */
	MDP_COLOR_420_BLK	= MDP_COLOR(0, 0, 1, 2, 1, 1, 256, 1, 0, 12),
	/* Block mode + field mode */
	MDP_COLOR_420_BLKI	= MDP_COLOR(0, 0, 3, 2, 1, 1, 256, 1, 0, 12),
	/* Block mode */
	MDP_COLOR_422_BLK	= MDP_COLOR(0, 0, 1, 1, 1, 0, 512, 1, 0, 4),

	MDP_COLOR_IYU2		= MDP_COLOR(0, 0, 0, 1, 0, 0, 24,  1, 0, 25),
	MDP_COLOR_YUV444	= MDP_COLOR(0, 0, 0, 1, 0, 0, 24,  1, 0, 30),

	/* Packed 10-bit formats */
	MDP_COLOR_RGBA1010102	= MDP_COLOR(1, 0, 0, 1, 0, 0, 32,  0, 1, 2),
	MDP_COLOR_BGRA1010102	= MDP_COLOR(1, 0, 0, 1, 0, 0, 32,  0, 0, 2),
	/* Packed 10-bit UYVY */
	MDP_COLOR_UYVY_10P	= MDP_COLOR(1, 0, 0, 1, 1, 0, 20,  1, 0, 4),
	/* Packed 10-bit NV21 */
	MDP_COLOR_NV21_10P	= MDP_COLOR(1, 0, 0, 2, 1, 1, 10,  1, 1, 12),
	/* 10-bit block mode */
	MDP_COLOR_420_BLK_10_H	= MDP_COLOR(1, 0, 1, 2, 1, 1, 320, 1, 0, 12),
	/* 10-bit HEVC tile mode */
	MDP_COLOR_420_BLK_10_V	= MDP_COLOR(1, 1, 1, 2, 1, 1, 320, 1, 0, 12),
	/* UFO encoded 10-bit block mode */
	MDP_COLOR_420_BLK_U10_H	= MDP_COLOR(1, 0, 5, 2, 1, 1, 320, 1, 0, 12),
	/* UFO encoded 10-bit HEVC tile mode */
	MDP_COLOR_420_BLK_U10_V	= MDP_COLOR(1, 1, 5, 2, 1, 1, 320, 1, 0, 12),

	/* Loose 10-bit formats */
	MDP_COLOR_UYVY_10L	= MDP_COLOR(0, 1, 0, 1, 1, 0, 20,  1, 0, 4),
	MDP_COLOR_VYUY_10L	= MDP_COLOR(0, 1, 0, 1, 1, 0, 20,  1, 1, 4),
	MDP_COLOR_YUYV_10L	= MDP_COLOR(0, 1, 0, 1, 1, 0, 20,  1, 0, 5),
	MDP_COLOR_YVYU_10L	= MDP_COLOR(0, 1, 0, 1, 1, 0, 20,  1, 1, 5),
	MDP_COLOR_NV12_10L	= MDP_COLOR(0, 1, 0, 2, 1, 1, 10,  1, 0, 12),
	MDP_COLOR_NV21_10L	= MDP_COLOR(0, 1, 0, 2, 1, 1, 10,  1, 1, 12),
	MDP_COLOR_NV16_10L	= MDP_COLOR(0, 1, 0, 2, 1, 0, 10,  1, 0, 13),
	MDP_COLOR_NV61_10L	= MDP_COLOR(0, 1, 0, 2, 1, 0, 10,  1, 1, 13),
	MDP_COLOR_YV12_10L	= MDP_COLOR(0, 1, 0, 3, 1, 1, 10,  1, 1, 8),
	MDP_COLOR_I420_10L	= MDP_COLOR(0, 1, 0, 3, 1, 1, 10,  1, 0, 8),
};

static inline bool MDP_COLOR_IS_UV_COPLANE(enum mdp_color c)
{
	return (MDP_COLOR_GET_PLANE_COUNT(c) == 2 && MDP_COLOR_IS_YUV(c));
}

/* Minimum Y stride that is accepted by MDP HW */
static inline u32 mdp_color_get_min_y_stride(enum mdp_color c, u32 width)
{
	return ((MDP_COLOR_BITS_PER_PIXEL(c) * width) + 4) >> 3;
}

/* Minimum UV stride that is accepted by MDP HW */
static inline u32 mdp_color_get_min_uv_stride(enum mdp_color c, u32 width)
{
	u32 min_stride;

	if (MDP_COLOR_GET_PLANE_COUNT(c) == 1)
		return 0;
	min_stride = mdp_color_get_min_y_stride(c, width)
		>> MDP_COLOR_GET_H_SUBSAMPLE(c);
	if (MDP_COLOR_IS_UV_COPLANE(c) && !MDP_COLOR_IS_BLOCK_MODE(c))
		min_stride = min_stride * 2;
	return min_stride;
}

/* Minimum Y plane size that is necessary in buffer */
static inline u32 mdp_color_get_min_y_size(enum mdp_color c,
					   u32 width, u32 height)
{
	if (MDP_COLOR_IS_BLOCK_MODE(c))
		return ((MDP_COLOR_BITS_PER_PIXEL(c) * width) >> 8) * height;
	return mdp_color_get_min_y_stride(c, width) * height;
}

/* Minimum UV plane size that is necessary in buffer */
static inline u32 mdp_color_get_min_uv_size(enum mdp_color c,
					    u32 width, u32 height)
{
	height = height >> MDP_COLOR_GET_V_SUBSAMPLE(c);
	if (MDP_COLOR_IS_BLOCK_MODE(c) && (MDP_COLOR_GET_PLANE_COUNT(c) > 1))
		return ((MDP_COLOR_BITS_PER_PIXEL(c) * width) >> 8) * height;
	return mdp_color_get_min_uv_stride(c, width) * height;
}

/* Combine colorspace, xfer_func, ycbcr_encoding, and quantization */
enum mdp_ycbcr_profile {
	/* V4L2_YCBCR_ENC_601 and V4L2_QUANTIZATION_LIM_RANGE */
	MDP_YCBCR_PROFILE_BT601,
	/* V4L2_YCBCR_ENC_709 and V4L2_QUANTIZATION_LIM_RANGE */
	MDP_YCBCR_PROFILE_BT709,
	/* V4L2_YCBCR_ENC_601 and V4L2_QUANTIZATION_FULL_RANGE */
	MDP_YCBCR_PROFILE_JPEG,
	MDP_YCBCR_PROFILE_FULL_BT601 = MDP_YCBCR_PROFILE_JPEG,

	/* Colorspaces not support for capture */
	/* V4L2_YCBCR_ENC_BT2020 and V4L2_QUANTIZATION_LIM_RANGE */
	MDP_YCBCR_PROFILE_BT2020,
	/* V4L2_YCBCR_ENC_709 and V4L2_QUANTIZATION_FULL_RANGE */
	MDP_YCBCR_PROFILE_FULL_BT709,
	/* V4L2_YCBCR_ENC_BT2020 and V4L2_QUANTIZATION_FULL_RANGE */
	MDP_YCBCR_PROFILE_FULL_BT2020,
};

#define MDP_FMT_FLAG_OUTPUT	BIT(0)
#define MDP_FMT_FLAG_CAPTURE	BIT(1)

struct mdp_format {
	u32	pixelformat;
	u32	mdp_color;
	u8	depth[VIDEO_MAX_PLANES];
	u8	row_depth[VIDEO_MAX_PLANES];
	u8	num_planes;
	u8	walign;
	u8	halign;
	u8	salign;
	u32	flags;
};

struct mdp_pix_limit {
	u32	wmin;
	u32	hmin;
	u32	wmax;
	u32	hmax;
};

struct mdp_limit {
	struct mdp_pix_limit	out_limit;
	struct mdp_pix_limit	cap_limit;
	u32			h_scale_up_max;
	u32			v_scale_up_max;
	u32			h_scale_down_max;
	u32			v_scale_down_max;
};

enum mdp_stream_type {
	MDP_STREAM_TYPE_UNKNOWN,
	MDP_STREAM_TYPE_BITBLT,
	MDP_STREAM_TYPE_GPU_BITBLT,
	MDP_STREAM_TYPE_DUAL_BITBLT,
	MDP_STREAM_TYPE_2ND_BITBLT,
	MDP_STREAM_TYPE_ISP_IC,
	MDP_STREAM_TYPE_ISP_VR,
	MDP_STREAM_TYPE_ISP_ZSD,
	MDP_STREAM_TYPE_ISP_IP,
	MDP_STREAM_TYPE_ISP_VSS,
	MDP_STREAM_TYPE_ISP_ZSD_SLOW,
	MDP_STREAM_TYPE_WPE,
	MDP_STREAM_TYPE_WPE2,
};

struct mdp_crop {
	struct v4l2_rect	c;
	struct v4l2_fract	left_subpix;
	struct v4l2_fract	top_subpix;
	struct v4l2_fract	width_subpix;
	struct v4l2_fract	height_subpix;
};

struct mdp_frame {
	struct v4l2_format	format;
	const struct mdp_format	*mdp_fmt;
	u32			ycbcr_prof;	/* enum mdp_ycbcr_profile */
	u32			usage;		/* enum mdp_buffer_usage */
	struct mdp_crop		crop;
	struct v4l2_rect	compose;
	s32			rotation;
	u32			hflip:1;
	u32			vflip:1;
	u32			hdr:1;
	u32			dre:1;
	u32			sharpness:1;
	u32			dither:1;
};

static inline bool mdp_target_is_crop(u32 target)
{
	return (target == V4L2_SEL_TGT_CROP) ||
		(target == V4L2_SEL_TGT_CROP_DEFAULT) ||
		(target == V4L2_SEL_TGT_CROP_BOUNDS);
}

static inline bool mdp_target_is_compose(u32 target)
{
	return (target == V4L2_SEL_TGT_COMPOSE) ||
		(target == V4L2_SEL_TGT_COMPOSE_DEFAULT) ||
		(target == V4L2_SEL_TGT_COMPOSE_BOUNDS);
}

#define MDP_MAX_CAPTURES	IMG_MAX_HW_OUTPUTS

#define MDP_VPU_INIT		BIT(0)
#define MDP_M2M_CTX_ERROR	BIT(1)

struct mdp_frameparam {
	struct list_head	list;
	struct mdp_m2m_ctx	*ctx;
	atomic_t		state;
	const struct mdp_limit	*limit;
	u32			type;	/* enum mdp_stream_type */
	u32			frame_no;
	struct mdp_frame	output;
	struct mdp_frame	captures[MDP_MAX_CAPTURES];
	u32			num_captures;
	enum v4l2_colorspace		colorspace;
	enum v4l2_ycbcr_encoding	ycbcr_enc;
	enum v4l2_xfer_func		xfer_func;
	enum v4l2_quantization		quant;
};

int mdp_enum_fmt_mplane(struct v4l2_fmtdesc *f);
const struct mdp_format *mdp_try_fmt_mplane(struct v4l2_format *f,
					    struct mdp_frameparam *param,
					    u32 ctx_id);
enum mdp_ycbcr_profile mdp_map_ycbcr_prof_mplane(struct v4l2_format *f,
						 u32 mdp_color);
int mdp_try_crop(struct mdp_m2m_ctx *ctx, struct v4l2_rect *r,
		 const struct v4l2_selection *s, struct mdp_frame *frame);
int mdp_check_scaling_ratio(const struct v4l2_rect *crop,
			    const struct v4l2_rect *compose, s32 rotation,
	const struct mdp_limit *limit);
void mdp_set_src_config(struct img_input *in,
			struct mdp_frame *frame, struct vb2_buffer *vb);
void mdp_set_dst_config(struct img_output *out,
			struct mdp_frame *frame, struct vb2_buffer *vb);
int mdp_frameparam_init(struct mdp_frameparam *param);

#endif  /* __MTK_MDP3_REGS_H__ */
