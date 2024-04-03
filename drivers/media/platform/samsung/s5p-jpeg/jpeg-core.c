// SPDX-License-Identifier: GPL-2.0-only
/* linux/drivers/media/platform/samsung/s5p-jpeg/jpeg-core.c
 *
 * Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-rect.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "jpeg-core.h"
#include "jpeg-hw-s5p.h"
#include "jpeg-hw-exynos4.h"
#include "jpeg-hw-exynos3250.h"
#include "jpeg-regs.h"

static struct s5p_jpeg_fmt sjpeg_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.flags		= SJPEG_FMT_FLAG_ENC_CAPTURE |
				  SJPEG_FMT_FLAG_DEC_OUTPUT |
				  SJPEG_FMT_FLAG_S5P |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_FLAG_EXYNOS4,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 4,
		.v_align	= 3,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_S5P |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 1,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 2,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YVYU,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 1,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YVYU,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 2,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 2,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_VYUY,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 2,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 0,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 2,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB565X,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 2,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 0,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_S5P |
				  SJPEG_FMT_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.depth		= 32,
		.colplanes	= 1,
		.h_align	= 0,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.depth		= 32,
		.colplanes	= 1,
		.h_align	= 2,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV24,
		.depth		= 24,
		.colplanes	= 2,
		.h_align	= 0,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV42,
		.depth		= 24,
		.colplanes	= 2,
		.h_align	= 0,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV61,
		.depth		= 16,
		.colplanes	= 2,
		.h_align	= 1,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV16,
		.depth		= 16,
		.colplanes	= 2,
		.h_align	= 1,
		.v_align	= 0,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV12,
		.depth		= 12,
		.colplanes	= 2,
		.h_align	= 1,
		.v_align	= 1,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV12,
		.depth		= 12,
		.colplanes	= 2,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV12,
		.depth		= 12,
		.colplanes	= 2,
		.h_align	= 4,
		.v_align	= 4,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_S5P |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV21,
		.depth		= 12,
		.colplanes	= 2,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV21,
		.depth		= 12,
		.colplanes	= 2,
		.h_align	= 1,
		.v_align	= 1,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV420,
		.depth		= 12,
		.colplanes	= 3,
		.h_align	= 1,
		.v_align	= 1,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV420,
		.depth		= 12,
		.colplanes	= 3,
		.h_align	= 4,
		.v_align	= 4,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS3250 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	},
	{
		.fourcc		= V4L2_PIX_FMT_GREY,
		.depth		= 8,
		.colplanes	= 1,
		.flags		= SJPEG_FMT_FLAG_ENC_OUTPUT |
				  SJPEG_FMT_FLAG_DEC_CAPTURE |
				  SJPEG_FMT_FLAG_EXYNOS4 |
				  SJPEG_FMT_NON_RGB,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY,
	},
};
#define SJPEG_NUM_FORMATS ARRAY_SIZE(sjpeg_formats)

static const unsigned char qtbl_luminance[4][64] = {
	{/*level 0 - high compression quality */
		20, 16, 25, 39, 50, 46, 62, 68,
		16, 18, 23, 38, 38, 53, 65, 68,
		25, 23, 31, 38, 53, 65, 68, 68,
		39, 38, 38, 53, 65, 68, 68, 68,
		50, 38, 53, 65, 68, 68, 68, 68,
		46, 53, 65, 68, 68, 68, 68, 68,
		62, 65, 68, 68, 68, 68, 68, 68,
		68, 68, 68, 68, 68, 68, 68, 68
	},
	{/* level 1 */
		16, 11, 11, 16, 23, 27, 31, 30,
		11, 12, 12, 15, 20, 23, 23, 30,
		11, 12, 13, 16, 23, 26, 35, 47,
		16, 15, 16, 23, 26, 37, 47, 64,
		23, 20, 23, 26, 39, 51, 64, 64,
		27, 23, 26, 37, 51, 64, 64, 64,
		31, 23, 35, 47, 64, 64, 64, 64,
		30, 30, 47, 64, 64, 64, 64, 64
	},
	{/* level 2 */
		12,  8,  8, 12, 17, 21, 24, 23,
		 8,  9,  9, 11, 15, 19, 18, 23,
		 8,  9, 10, 12, 19, 20, 27, 36,
		12, 11, 12, 21, 20, 28, 36, 53,
		17, 15, 19, 20, 30, 39, 51, 59,
		21, 19, 20, 28, 39, 51, 59, 59,
		24, 18, 27, 36, 51, 59, 59, 59,
		23, 23, 36, 53, 59, 59, 59, 59
	},
	{/* level 3 - low compression quality */
		 8,  6,  6,  8, 12, 14, 16, 17,
		 6,  6,  6,  8, 10, 13, 12, 15,
		 6,  6,  7,  8, 13, 14, 18, 24,
		 8,  8,  8, 14, 13, 19, 24, 35,
		12, 10, 13, 13, 20, 26, 34, 39,
		14, 13, 14, 19, 26, 34, 39, 39,
		16, 12, 18, 24, 34, 39, 39, 39,
		17, 15, 24, 35, 39, 39, 39, 39
	}
};

static const unsigned char qtbl_chrominance[4][64] = {
	{/*level 0 - high compression quality */
		21, 25, 32, 38, 54, 68, 68, 68,
		25, 28, 24, 38, 54, 68, 68, 68,
		32, 24, 32, 43, 66, 68, 68, 68,
		38, 38, 43, 53, 68, 68, 68, 68,
		54, 54, 66, 68, 68, 68, 68, 68,
		68, 68, 68, 68, 68, 68, 68, 68,
		68, 68, 68, 68, 68, 68, 68, 68,
		68, 68, 68, 68, 68, 68, 68, 68
	},
	{/* level 1 */
		17, 15, 17, 21, 20, 26, 38, 48,
		15, 19, 18, 17, 20, 26, 35, 43,
		17, 18, 20, 22, 26, 30, 46, 53,
		21, 17, 22, 28, 30, 39, 53, 64,
		20, 20, 26, 30, 39, 48, 64, 64,
		26, 26, 30, 39, 48, 63, 64, 64,
		38, 35, 46, 53, 64, 64, 64, 64,
		48, 43, 53, 64, 64, 64, 64, 64
	},
	{/* level 2 */
		13, 11, 13, 16, 20, 20, 29, 37,
		11, 14, 14, 14, 16, 20, 26, 32,
		13, 14, 15, 17, 20, 23, 35, 40,
		16, 14, 17, 21, 23, 30, 40, 50,
		20, 16, 20, 23, 30, 37, 50, 59,
		20, 20, 23, 30, 37, 48, 59, 59,
		29, 26, 35, 40, 50, 59, 59, 59,
		37, 32, 40, 50, 59, 59, 59, 59
	},
	{/* level 3 - low compression quality */
		 9,  8,  9, 11, 14, 17, 19, 24,
		 8, 10,  9, 11, 14, 13, 17, 22,
		 9,  9, 13, 14, 13, 15, 23, 26,
		11, 11, 14, 14, 15, 20, 26, 33,
		14, 14, 13, 15, 20, 24, 33, 39,
		17, 13, 15, 20, 24, 32, 39, 39,
		19, 17, 23, 26, 33, 39, 39, 39,
		24, 22, 26, 33, 39, 39, 39, 39
	}
};

static const unsigned char hdctbl0[16] = {
	0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char hdctblg0[12] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb
};
static const unsigned char hactbl0[16] = {
	0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d
};
static const unsigned char hactblg0[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
};

/*
 * Fourcc downgrade schema lookup tables for 422 and 420
 * chroma subsampling - fourcc on each position maps on the
 * fourcc from the table fourcc_to_dwngrd_schema_id which allows
 * to get the most suitable fourcc counterpart for the given
 * downgraded subsampling property.
 */
static const u32 subs422_fourcc_dwngrd_schema[] = {
	V4L2_PIX_FMT_NV16,
	V4L2_PIX_FMT_NV61,
};

static const u32 subs420_fourcc_dwngrd_schema[] = {
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_NV21,
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_NV21,
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_NV21,
	V4L2_PIX_FMT_GREY,
	V4L2_PIX_FMT_GREY,
	V4L2_PIX_FMT_GREY,
	V4L2_PIX_FMT_GREY,
};

/*
 * Lookup table for translation of a fourcc to the position
 * of its downgraded counterpart in the *fourcc_dwngrd_schema
 * tables.
 */
static const u32 fourcc_to_dwngrd_schema_id[] = {
	V4L2_PIX_FMT_NV24,
	V4L2_PIX_FMT_NV42,
	V4L2_PIX_FMT_NV16,
	V4L2_PIX_FMT_NV61,
	V4L2_PIX_FMT_YUYV,
	V4L2_PIX_FMT_YVYU,
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_NV21,
	V4L2_PIX_FMT_YUV420,
	V4L2_PIX_FMT_GREY,
};

static int s5p_jpeg_get_dwngrd_sch_id_by_fourcc(u32 fourcc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fourcc_to_dwngrd_schema_id); ++i) {
		if (fourcc_to_dwngrd_schema_id[i] == fourcc)
			return i;
	}

	return -EINVAL;
}

static int s5p_jpeg_adjust_fourcc_to_subsampling(
					enum v4l2_jpeg_chroma_subsampling subs,
					u32 in_fourcc,
					u32 *out_fourcc,
					struct s5p_jpeg_ctx *ctx)
{
	int dwngrd_sch_id;

	if (ctx->subsampling != V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY) {
		dwngrd_sch_id =
			s5p_jpeg_get_dwngrd_sch_id_by_fourcc(in_fourcc);
		if (dwngrd_sch_id < 0)
			return -EINVAL;
	}

	switch (ctx->subsampling) {
	case V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY:
		*out_fourcc = V4L2_PIX_FMT_GREY;
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_420:
		if (dwngrd_sch_id >
				ARRAY_SIZE(subs420_fourcc_dwngrd_schema) - 1)
			return -EINVAL;
		*out_fourcc = subs420_fourcc_dwngrd_schema[dwngrd_sch_id];
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_422:
		if (dwngrd_sch_id >
				ARRAY_SIZE(subs422_fourcc_dwngrd_schema) - 1)
			return -EINVAL;
		*out_fourcc = subs422_fourcc_dwngrd_schema[dwngrd_sch_id];
		break;
	default:
		*out_fourcc = V4L2_PIX_FMT_GREY;
		break;
	}

	return 0;
}

static int exynos4x12_decoded_subsampling[] = {
	V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY,
	V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	V4L2_JPEG_CHROMA_SUBSAMPLING_420,
};

static int exynos3250_decoded_subsampling[] = {
	V4L2_JPEG_CHROMA_SUBSAMPLING_444,
	V4L2_JPEG_CHROMA_SUBSAMPLING_422,
	V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY,
	-1,
	-1,
	V4L2_JPEG_CHROMA_SUBSAMPLING_411,
};

static inline struct s5p_jpeg_ctx *ctrl_to_ctx(struct v4l2_ctrl *c)
{
	return container_of(c->handler, struct s5p_jpeg_ctx, ctrl_handler);
}

static inline struct s5p_jpeg_ctx *fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct s5p_jpeg_ctx, fh);
}

static int s5p_jpeg_to_user_subsampling(struct s5p_jpeg_ctx *ctx)
{
	switch (ctx->jpeg->variant->version) {
	case SJPEG_S5P:
		WARN_ON(ctx->subsampling > 3);
		if (ctx->subsampling > 2)
			return V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY;
		return ctx->subsampling;
	case SJPEG_EXYNOS3250:
	case SJPEG_EXYNOS5420:
		WARN_ON(ctx->subsampling > 6);
		if (ctx->subsampling > 3)
			return V4L2_JPEG_CHROMA_SUBSAMPLING_411;
		return exynos3250_decoded_subsampling[ctx->subsampling];
	case SJPEG_EXYNOS4:
		WARN_ON(ctx->subsampling > 3);
		if (ctx->subsampling > 2)
			return V4L2_JPEG_CHROMA_SUBSAMPLING_420;
		return exynos4x12_decoded_subsampling[ctx->subsampling];
	case SJPEG_EXYNOS5433:
		return ctx->subsampling; /* parsed from header */
	default:
		WARN_ON(ctx->subsampling > 3);
		return V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY;
	}
}

static inline void s5p_jpeg_set_qtbl(void __iomem *regs,
				     const unsigned char *qtbl,
				     unsigned long tab, int len)
{
	int i;

	for (i = 0; i < len; i++)
		writel((unsigned int)qtbl[i], regs + tab + (i * 0x04));
}

static inline void s5p_jpeg_set_qtbl_lum(void __iomem *regs, int quality)
{
	/* this driver fills quantisation table 0 with data for luma */
	s5p_jpeg_set_qtbl(regs, qtbl_luminance[quality],
			  S5P_JPG_QTBL_CONTENT(0),
			  ARRAY_SIZE(qtbl_luminance[quality]));
}

static inline void s5p_jpeg_set_qtbl_chr(void __iomem *regs, int quality)
{
	/* this driver fills quantisation table 1 with data for chroma */
	s5p_jpeg_set_qtbl(regs, qtbl_chrominance[quality],
			  S5P_JPG_QTBL_CONTENT(1),
			  ARRAY_SIZE(qtbl_chrominance[quality]));
}

static inline void s5p_jpeg_set_htbl(void __iomem *regs,
				     const unsigned char *htbl,
				     unsigned long tab, int len)
{
	int i;

	for (i = 0; i < len; i++)
		writel((unsigned int)htbl[i], regs + tab + (i * 0x04));
}

static inline void s5p_jpeg_set_hdctbl(void __iomem *regs)
{
	/* this driver fills table 0 for this component */
	s5p_jpeg_set_htbl(regs, hdctbl0, S5P_JPG_HDCTBL(0),
						ARRAY_SIZE(hdctbl0));
}

static inline void s5p_jpeg_set_hdctblg(void __iomem *regs)
{
	/* this driver fills table 0 for this component */
	s5p_jpeg_set_htbl(regs, hdctblg0, S5P_JPG_HDCTBLG(0),
						ARRAY_SIZE(hdctblg0));
}

static inline void s5p_jpeg_set_hactbl(void __iomem *regs)
{
	/* this driver fills table 0 for this component */
	s5p_jpeg_set_htbl(regs, hactbl0, S5P_JPG_HACTBL(0),
						ARRAY_SIZE(hactbl0));
}

static inline void s5p_jpeg_set_hactblg(void __iomem *regs)
{
	/* this driver fills table 0 for this component */
	s5p_jpeg_set_htbl(regs, hactblg0, S5P_JPG_HACTBLG(0),
						ARRAY_SIZE(hactblg0));
}

static inline void exynos4_jpeg_set_tbl(void __iomem *regs,
					const unsigned char *tbl,
					unsigned long tab, int len)
{
	int i;
	unsigned int dword;

	for (i = 0; i < len; i += 4) {
		dword = tbl[i] |
			(tbl[i + 1] << 8) |
			(tbl[i + 2] << 16) |
			(tbl[i + 3] << 24);
		writel(dword, regs + tab + i);
	}
}

static inline void exynos4_jpeg_set_qtbl_lum(void __iomem *regs, int quality)
{
	/* this driver fills quantisation table 0 with data for luma */
	exynos4_jpeg_set_tbl(regs, qtbl_luminance[quality],
			     EXYNOS4_QTBL_CONTENT(0),
			     ARRAY_SIZE(qtbl_luminance[quality]));
}

static inline void exynos4_jpeg_set_qtbl_chr(void __iomem *regs, int quality)
{
	/* this driver fills quantisation table 1 with data for chroma */
	exynos4_jpeg_set_tbl(regs, qtbl_chrominance[quality],
			     EXYNOS4_QTBL_CONTENT(1),
			     ARRAY_SIZE(qtbl_chrominance[quality]));
}

static void exynos4_jpeg_set_huff_tbl(void __iomem *base)
{
	exynos4_jpeg_set_tbl(base, hdctbl0, EXYNOS4_HUFF_TBL_HDCLL,
							ARRAY_SIZE(hdctbl0));
	exynos4_jpeg_set_tbl(base, hdctbl0, EXYNOS4_HUFF_TBL_HDCCL,
							ARRAY_SIZE(hdctbl0));
	exynos4_jpeg_set_tbl(base, hdctblg0, EXYNOS4_HUFF_TBL_HDCLV,
							ARRAY_SIZE(hdctblg0));
	exynos4_jpeg_set_tbl(base, hdctblg0, EXYNOS4_HUFF_TBL_HDCCV,
							ARRAY_SIZE(hdctblg0));
	exynos4_jpeg_set_tbl(base, hactbl0, EXYNOS4_HUFF_TBL_HACLL,
							ARRAY_SIZE(hactbl0));
	exynos4_jpeg_set_tbl(base, hactbl0, EXYNOS4_HUFF_TBL_HACCL,
							ARRAY_SIZE(hactbl0));
	exynos4_jpeg_set_tbl(base, hactblg0, EXYNOS4_HUFF_TBL_HACLV,
							ARRAY_SIZE(hactblg0));
	exynos4_jpeg_set_tbl(base, hactblg0, EXYNOS4_HUFF_TBL_HACCV,
							ARRAY_SIZE(hactblg0));
}

static inline int __exynos4_huff_tbl(int class, int id, bool lenval)
{
	/*
	 * class: 0 - DC, 1 - AC
	 * id: 0 - Y, 1 - Cb/Cr
	 */
	if (class) {
		if (id)
			return lenval ? EXYNOS4_HUFF_TBL_HACCL :
				EXYNOS4_HUFF_TBL_HACCV;
		return lenval ? EXYNOS4_HUFF_TBL_HACLL : EXYNOS4_HUFF_TBL_HACLV;

	}
	/* class == 0 */
	if (id)
		return lenval ? EXYNOS4_HUFF_TBL_HDCCL : EXYNOS4_HUFF_TBL_HDCCV;

	return lenval ? EXYNOS4_HUFF_TBL_HDCLL : EXYNOS4_HUFF_TBL_HDCLV;
}

static inline int exynos4_huff_tbl_len(int class, int id)
{
	return __exynos4_huff_tbl(class, id, true);
}

static inline int exynos4_huff_tbl_val(int class, int id)
{
	return __exynos4_huff_tbl(class, id, false);
}

static int get_byte(struct s5p_jpeg_buffer *buf);
static int get_word_be(struct s5p_jpeg_buffer *buf, unsigned int *word);
static void skip(struct s5p_jpeg_buffer *buf, long len);

static void exynos4_jpeg_parse_decode_h_tbl(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	struct s5p_jpeg_buffer jpeg_buffer;
	unsigned int word;
	int c, x, components;

	jpeg_buffer.size = 2; /* Ls */
	jpeg_buffer.data =
		(unsigned long)vb2_plane_vaddr(&vb->vb2_buf, 0) + ctx->out_q.sos + 2;
	jpeg_buffer.curr = 0;

	word = 0;

	if (get_word_be(&jpeg_buffer, &word))
		return;
	jpeg_buffer.size = (long)word - 2;
	jpeg_buffer.data += 2;
	jpeg_buffer.curr = 0;

	components = get_byte(&jpeg_buffer);
	if (components == -1)
		return;
	while (components--) {
		c = get_byte(&jpeg_buffer);
		if (c == -1)
			return;
		x = get_byte(&jpeg_buffer);
		if (x == -1)
			return;
		exynos4_jpeg_select_dec_h_tbl(jpeg->regs, c,
					(((x >> 4) & 0x1) << 1) | (x & 0x1));
	}

}

static void exynos4_jpeg_parse_huff_tbl(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	struct s5p_jpeg_buffer jpeg_buffer;
	unsigned int word;
	int c, i, n, j;

	for (j = 0; j < ctx->out_q.dht.n; ++j) {
		jpeg_buffer.size = ctx->out_q.dht.len[j];
		jpeg_buffer.data = (unsigned long)vb2_plane_vaddr(&vb->vb2_buf, 0) +
				   ctx->out_q.dht.marker[j];
		jpeg_buffer.curr = 0;

		word = 0;
		while (jpeg_buffer.curr < jpeg_buffer.size) {
			char id, class;

			c = get_byte(&jpeg_buffer);
			if (c == -1)
				return;
			id = c & 0xf;
			class = (c >> 4) & 0xf;
			n = 0;
			for (i = 0; i < 16; ++i) {
				c = get_byte(&jpeg_buffer);
				if (c == -1)
					return;
				word |= c << ((i % 4) * 8);
				if ((i + 1) % 4 == 0) {
					writel(word, jpeg->regs +
					exynos4_huff_tbl_len(class, id) +
					(i / 4) * 4);
					word = 0;
				}
				n += c;
			}
			word = 0;
			for (i = 0; i < n; ++i) {
				c = get_byte(&jpeg_buffer);
				if (c == -1)
					return;
				word |= c << ((i % 4) * 8);
				if ((i + 1) % 4 == 0) {
					writel(word, jpeg->regs +
					exynos4_huff_tbl_val(class, id) +
					(i / 4) * 4);
					word = 0;
				}
			}
			if (i % 4) {
				writel(word, jpeg->regs +
				exynos4_huff_tbl_val(class, id) + (i / 4) * 4);
			}
			word = 0;
		}
	}
}

static void exynos4_jpeg_parse_decode_q_tbl(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	struct s5p_jpeg_buffer jpeg_buffer;
	int c, x, components;

	jpeg_buffer.size = ctx->out_q.sof_len;
	jpeg_buffer.data =
		(unsigned long)vb2_plane_vaddr(&vb->vb2_buf, 0) + ctx->out_q.sof;
	jpeg_buffer.curr = 0;

	skip(&jpeg_buffer, 5); /* P, Y, X */
	components = get_byte(&jpeg_buffer);
	if (components == -1)
		return;

	exynos4_jpeg_set_dec_components(jpeg->regs, components);

	while (components--) {
		c = get_byte(&jpeg_buffer);
		if (c == -1)
			return;
		skip(&jpeg_buffer, 1);
		x = get_byte(&jpeg_buffer);
		if (x == -1)
			return;
		exynos4_jpeg_select_dec_q_tbl(jpeg->regs, c, x);
	}
}

static void exynos4_jpeg_parse_q_tbl(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	struct s5p_jpeg_buffer jpeg_buffer;
	unsigned int word;
	int c, i, j;

	for (j = 0; j < ctx->out_q.dqt.n; ++j) {
		jpeg_buffer.size = ctx->out_q.dqt.len[j];
		jpeg_buffer.data = (unsigned long)vb2_plane_vaddr(&vb->vb2_buf, 0) +
				   ctx->out_q.dqt.marker[j];
		jpeg_buffer.curr = 0;

		word = 0;
		while (jpeg_buffer.size - jpeg_buffer.curr >= 65) {
			char id;

			c = get_byte(&jpeg_buffer);
			if (c == -1)
				return;
			id = c & 0xf;
			/* nonzero means extended mode - not supported */
			if ((c >> 4) & 0xf)
				return;
			for (i = 0; i < 64; ++i) {
				c = get_byte(&jpeg_buffer);
				if (c == -1)
					return;
				word |= c << ((i % 4) * 8);
				if ((i + 1) % 4 == 0) {
					writel(word, jpeg->regs +
					EXYNOS4_QTBL_CONTENT(id) + (i / 4) * 4);
					word = 0;
				}
			}
			word = 0;
		}
	}
}

/*
 * ============================================================================
 * Device file operations
 * ============================================================================
 */

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq);
static struct s5p_jpeg_fmt *s5p_jpeg_find_format(struct s5p_jpeg_ctx *ctx,
				__u32 pixelformat, unsigned int fmt_type);
static int s5p_jpeg_controls_create(struct s5p_jpeg_ctx *ctx);

static int s5p_jpeg_open(struct file *file)
{
	struct s5p_jpeg *jpeg = video_drvdata(file);
	struct video_device *vfd = video_devdata(file);
	struct s5p_jpeg_ctx *ctx;
	struct s5p_jpeg_fmt *out_fmt, *cap_fmt;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (mutex_lock_interruptible(&jpeg->lock)) {
		ret = -ERESTARTSYS;
		goto free;
	}

	v4l2_fh_init(&ctx->fh, vfd);
	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ctx->jpeg = jpeg;
	if (vfd == jpeg->vfd_encoder) {
		ctx->mode = S5P_JPEG_ENCODE;
		out_fmt = s5p_jpeg_find_format(ctx, V4L2_PIX_FMT_RGB565,
							FMT_TYPE_OUTPUT);
		cap_fmt = s5p_jpeg_find_format(ctx, V4L2_PIX_FMT_JPEG,
							FMT_TYPE_CAPTURE);
	} else {
		ctx->mode = S5P_JPEG_DECODE;
		out_fmt = s5p_jpeg_find_format(ctx, V4L2_PIX_FMT_JPEG,
							FMT_TYPE_OUTPUT);
		cap_fmt = s5p_jpeg_find_format(ctx, V4L2_PIX_FMT_YUYV,
							FMT_TYPE_CAPTURE);
		ctx->scale_factor = EXYNOS3250_DEC_SCALE_FACTOR_8_8;
	}

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(jpeg->m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto error;
	}

	ctx->out_q.fmt = out_fmt;
	ctx->cap_q.fmt = cap_fmt;

	ret = s5p_jpeg_controls_create(ctx);
	if (ret < 0)
		goto error;

	mutex_unlock(&jpeg->lock);
	return 0;

error:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	mutex_unlock(&jpeg->lock);
free:
	kfree(ctx);
	return ret;
}

static int s5p_jpeg_release(struct file *file)
{
	struct s5p_jpeg *jpeg = video_drvdata(file);
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(file->private_data);

	mutex_lock(&jpeg->lock);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mutex_unlock(&jpeg->lock);

	return 0;
}

static const struct v4l2_file_operations s5p_jpeg_fops = {
	.owner		= THIS_MODULE,
	.open		= s5p_jpeg_open,
	.release	= s5p_jpeg_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

/*
 * ============================================================================
 * video ioctl operations
 * ============================================================================
 */

static int get_byte(struct s5p_jpeg_buffer *buf)
{
	if (buf->curr >= buf->size)
		return -1;

	return ((unsigned char *)buf->data)[buf->curr++];
}

static int get_word_be(struct s5p_jpeg_buffer *buf, unsigned int *word)
{
	unsigned int temp;
	int byte;

	byte = get_byte(buf);
	if (byte == -1)
		return -1;
	temp = byte << 8;
	byte = get_byte(buf);
	if (byte == -1)
		return -1;
	*word = (unsigned int)byte | temp;
	return 0;
}

static void skip(struct s5p_jpeg_buffer *buf, long len)
{
	if (len <= 0)
		return;

	while (len--)
		get_byte(buf);
}

static bool s5p_jpeg_subsampling_decode(struct s5p_jpeg_ctx *ctx,
					unsigned int subsampling)
{
	unsigned int version;

	switch (subsampling) {
	case 0x11:
		ctx->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_444;
		break;
	case 0x21:
		ctx->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_422;
		break;
	case 0x22:
		ctx->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_420;
		break;
	case 0x33:
		ctx->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY;
		break;
	case 0x41:
		/*
		 * 4:1:1 subsampling only supported by 3250, 5420, and 5433
		 * variants
		 */
		version = ctx->jpeg->variant->version;
		if (version != SJPEG_EXYNOS3250 &&
		    version != SJPEG_EXYNOS5420 &&
		    version != SJPEG_EXYNOS5433)
			return false;

		ctx->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_411;
		break;
	default:
		return false;
	}

	return true;
}

static bool s5p_jpeg_parse_hdr(struct s5p_jpeg_q_data *result,
			       unsigned long buffer, unsigned long size,
			       struct s5p_jpeg_ctx *ctx)
{
	int c, components = 0, notfound, n_dht = 0, n_dqt = 0;
	unsigned int height = 0, width = 0, word, subsampling = 0;
	unsigned int sos = 0, sof = 0, sof_len = 0;
	unsigned int dht[S5P_JPEG_MAX_MARKER], dht_len[S5P_JPEG_MAX_MARKER];
	unsigned int dqt[S5P_JPEG_MAX_MARKER], dqt_len[S5P_JPEG_MAX_MARKER];
	long length;
	struct s5p_jpeg_buffer jpeg_buffer;

	jpeg_buffer.size = size;
	jpeg_buffer.data = buffer;
	jpeg_buffer.curr = 0;

	notfound = 1;
	while (notfound || !sos) {
		c = get_byte(&jpeg_buffer);
		if (c == -1)
			return false;
		if (c != 0xff)
			continue;
		do
			c = get_byte(&jpeg_buffer);
		while (c == 0xff);
		if (c == -1)
			return false;
		if (c == 0)
			continue;
		length = 0;
		switch (c) {
		/* JPEG_MARKER_SOF0: baseline JPEG */
		case JPEG_MARKER_SOF0:
			if (get_word_be(&jpeg_buffer, &word))
				break;
			length = (long)word - 2;
			if (!length)
				return false;
			sof = jpeg_buffer.curr; /* after 0xffc0 */
			sof_len = length;
			if (get_byte(&jpeg_buffer) == -1)
				break;
			if (get_word_be(&jpeg_buffer, &height))
				break;
			if (get_word_be(&jpeg_buffer, &width))
				break;
			components = get_byte(&jpeg_buffer);
			if (components == -1)
				break;

			if (components == 1) {
				subsampling = 0x33;
			} else {
				skip(&jpeg_buffer, 1);
				subsampling = get_byte(&jpeg_buffer);
				skip(&jpeg_buffer, 1);
			}
			if (components > 3)
				return false;
			skip(&jpeg_buffer, components * 2);
			notfound = 0;
			break;

		case JPEG_MARKER_DQT:
			if (get_word_be(&jpeg_buffer, &word))
				break;
			length = (long)word - 2;
			if (!length)
				return false;
			if (n_dqt >= S5P_JPEG_MAX_MARKER)
				return false;
			dqt[n_dqt] = jpeg_buffer.curr; /* after 0xffdb */
			dqt_len[n_dqt++] = length;
			skip(&jpeg_buffer, length);
			break;

		case JPEG_MARKER_DHT:
			if (get_word_be(&jpeg_buffer, &word))
				break;
			length = (long)word - 2;
			if (!length)
				return false;
			if (n_dht >= S5P_JPEG_MAX_MARKER)
				return false;
			dht[n_dht] = jpeg_buffer.curr; /* after 0xffc4 */
			dht_len[n_dht++] = length;
			skip(&jpeg_buffer, length);
			break;

		case JPEG_MARKER_SOS:
			sos = jpeg_buffer.curr - 2; /* 0xffda */
			break;

		/* skip payload-less markers */
		case JPEG_MARKER_RST ... JPEG_MARKER_RST + 7:
		case JPEG_MARKER_SOI:
		case JPEG_MARKER_EOI:
		case JPEG_MARKER_TEM:
			break;

		/* skip uninteresting payload markers */
		default:
			if (get_word_be(&jpeg_buffer, &word))
				break;
			length = (long)word - 2;
			skip(&jpeg_buffer, length);
			break;
		}
	}

	if (notfound || !sos || !s5p_jpeg_subsampling_decode(ctx, subsampling))
		return false;

	result->w = width;
	result->h = height;
	result->sos = sos;
	result->dht.n = n_dht;
	while (n_dht--) {
		result->dht.marker[n_dht] = dht[n_dht];
		result->dht.len[n_dht] = dht_len[n_dht];
	}
	result->dqt.n = n_dqt;
	while (n_dqt--) {
		result->dqt.marker[n_dqt] = dqt[n_dqt];
		result->dqt.len[n_dqt] = dqt_len[n_dqt];
	}
	result->sof = sof;
	result->sof_len = sof_len;

	return true;
}

static int s5p_jpeg_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == S5P_JPEG_ENCODE) {
		strscpy(cap->driver, S5P_JPEG_M2M_NAME,
			sizeof(cap->driver));
		strscpy(cap->card, S5P_JPEG_M2M_NAME " encoder",
			sizeof(cap->card));
	} else {
		strscpy(cap->driver, S5P_JPEG_M2M_NAME,
			sizeof(cap->driver));
		strscpy(cap->card, S5P_JPEG_M2M_NAME " decoder",
			sizeof(cap->card));
	}
	return 0;
}

static int enum_fmt(struct s5p_jpeg_ctx *ctx,
		    struct s5p_jpeg_fmt *sjpeg_formats, int n,
		    struct v4l2_fmtdesc *f, u32 type)
{
	int i, num = 0;
	unsigned int fmt_ver_flag = ctx->jpeg->variant->fmt_ver_flag;

	for (i = 0; i < n; ++i) {
		if (sjpeg_formats[i].flags & type &&
		    sjpeg_formats[i].flags & fmt_ver_flag) {
			/* index-th format of type type found ? */
			if (num == f->index)
				break;
			/* Correct type but haven't reached our index yet,
			 * just increment per-type index
			 */
			++num;
		}
	}

	/* Format not found */
	if (i >= n)
		return -EINVAL;

	f->pixelformat = sjpeg_formats[i].fourcc;

	return 0;
}

static int s5p_jpeg_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == S5P_JPEG_ENCODE)
		return enum_fmt(ctx, sjpeg_formats, SJPEG_NUM_FORMATS, f,
				SJPEG_FMT_FLAG_ENC_CAPTURE);

	return enum_fmt(ctx, sjpeg_formats, SJPEG_NUM_FORMATS, f,
			SJPEG_FMT_FLAG_DEC_CAPTURE);
}

static int s5p_jpeg_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == S5P_JPEG_ENCODE)
		return enum_fmt(ctx, sjpeg_formats, SJPEG_NUM_FORMATS, f,
				SJPEG_FMT_FLAG_ENC_OUTPUT);

	return enum_fmt(ctx, sjpeg_formats, SJPEG_NUM_FORMATS, f,
			SJPEG_FMT_FLAG_DEC_OUTPUT);
}

static struct s5p_jpeg_q_data *get_q_data(struct s5p_jpeg_ctx *ctx,
					  enum v4l2_buf_type type)
{
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return &ctx->out_q;
	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return &ctx->cap_q;

	return NULL;
}

static int s5p_jpeg_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct s5p_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct s5p_jpeg_ctx *ct = fh_to_ctx(priv);

	vq = v4l2_m2m_get_vq(ct->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    ct->mode == S5P_JPEG_DECODE && !ct->hdr_parsed)
		return -EINVAL;
	q_data = get_q_data(ct, f->type);
	BUG_ON(q_data == NULL);

	pix->width = q_data->w;
	pix->height = q_data->h;
	pix->field = V4L2_FIELD_NONE;
	pix->pixelformat = q_data->fmt->fourcc;
	pix->bytesperline = 0;
	if (q_data->fmt->fourcc != V4L2_PIX_FMT_JPEG) {
		u32 bpl = q_data->w;

		if (q_data->fmt->colplanes == 1)
			bpl = (bpl * q_data->fmt->depth) >> 3;
		pix->bytesperline = bpl;
	}
	pix->sizeimage = q_data->size;

	return 0;
}

static struct s5p_jpeg_fmt *s5p_jpeg_find_format(struct s5p_jpeg_ctx *ctx,
				u32 pixelformat, unsigned int fmt_type)
{
	unsigned int k, fmt_flag;

	if (ctx->mode == S5P_JPEG_ENCODE)
		fmt_flag = (fmt_type == FMT_TYPE_OUTPUT) ?
				SJPEG_FMT_FLAG_ENC_OUTPUT :
				SJPEG_FMT_FLAG_ENC_CAPTURE;
	else
		fmt_flag = (fmt_type == FMT_TYPE_OUTPUT) ?
				SJPEG_FMT_FLAG_DEC_OUTPUT :
				SJPEG_FMT_FLAG_DEC_CAPTURE;

	for (k = 0; k < ARRAY_SIZE(sjpeg_formats); k++) {
		struct s5p_jpeg_fmt *fmt = &sjpeg_formats[k];

		if (fmt->fourcc == pixelformat &&
		    fmt->flags & fmt_flag &&
		    fmt->flags & ctx->jpeg->variant->fmt_ver_flag) {
			return fmt;
		}
	}

	return NULL;
}

static void jpeg_bound_align_image(struct s5p_jpeg_ctx *ctx,
				   u32 *w, unsigned int wmin, unsigned int wmax,
				   unsigned int walign,
				   u32 *h, unsigned int hmin, unsigned int hmax,
				   unsigned int halign)
{
	int width, height, w_step, h_step;

	width = *w;
	height = *h;

	w_step = 1 << walign;
	h_step = 1 << halign;

	if (ctx->jpeg->variant->hw3250_compat) {
		/*
		 * Rightmost and bottommost pixels are cropped by the
		 * Exynos3250/compatible JPEG IP for RGB formats, for the
		 * specific width and height values respectively. This
		 * assignment will result in v4l_bound_align_image returning
		 * dimensions reduced by 1 for the aforementioned cases.
		 */
		if (w_step == 4 && ((width & 3) == 1)) {
			wmax = width;
			hmax = height;
		}
	}

	v4l_bound_align_image(w, wmin, wmax, walign, h, hmin, hmax, halign, 0);

	if (*w < width && (*w + w_step) < wmax)
		*w += w_step;
	if (*h < height && (*h + h_step) < hmax)
		*h += h_step;
}

static int vidioc_try_fmt(struct v4l2_format *f, struct s5p_jpeg_fmt *fmt,
			  struct s5p_jpeg_ctx *ctx, int q_type)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;
	else if (pix->field != V4L2_FIELD_NONE)
		return -EINVAL;

	/* V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported
	 */
	if (q_type == FMT_TYPE_OUTPUT)
		jpeg_bound_align_image(ctx, &pix->width, S5P_JPEG_MIN_WIDTH,
				       S5P_JPEG_MAX_WIDTH, 0,
				       &pix->height, S5P_JPEG_MIN_HEIGHT,
				       S5P_JPEG_MAX_HEIGHT, 0);
	else
		jpeg_bound_align_image(ctx, &pix->width, S5P_JPEG_MIN_WIDTH,
				       S5P_JPEG_MAX_WIDTH, fmt->h_align,
				       &pix->height, S5P_JPEG_MIN_HEIGHT,
				       S5P_JPEG_MAX_HEIGHT, fmt->v_align);

	if (fmt->fourcc == V4L2_PIX_FMT_JPEG) {
		if (pix->sizeimage <= 0)
			pix->sizeimage = PAGE_SIZE;
		pix->bytesperline = 0;
	} else {
		u32 bpl = pix->bytesperline;

		if (fmt->colplanes > 1 && bpl < pix->width)
			bpl = pix->width; /* planar */

		if (fmt->colplanes == 1 && /* packed */
		    (bpl << 3) / fmt->depth < pix->width)
			bpl = (pix->width * fmt->depth) >> 3;

		pix->bytesperline = bpl;
		pix->sizeimage = (pix->width * pix->height * fmt->depth) >> 3;
	}

	return 0;
}

static int s5p_jpeg_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct s5p_jpeg_fmt *fmt;
	int ret;

	fmt = s5p_jpeg_find_format(ctx, f->fmt.pix.pixelformat,
						FMT_TYPE_CAPTURE);
	if (!fmt) {
		v4l2_err(&ctx->jpeg->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	if (!ctx->jpeg->variant->hw_ex4_compat || ctx->mode != S5P_JPEG_DECODE)
		goto exit;

	/*
	 * The exynos4x12 device requires resulting YUV image
	 * subsampling not to be lower than the input jpeg subsampling.
	 * If this requirement is not met then downgrade the requested
	 * capture format to the one with subsampling equal to the input jpeg.
	 */
	if ((fmt->flags & SJPEG_FMT_NON_RGB) &&
	    (fmt->subsampling < ctx->subsampling)) {
		ret = s5p_jpeg_adjust_fourcc_to_subsampling(ctx->subsampling,
							    fmt->fourcc,
							    &pix->pixelformat,
							    ctx);
		if (ret < 0)
			pix->pixelformat = V4L2_PIX_FMT_GREY;

		fmt = s5p_jpeg_find_format(ctx, pix->pixelformat,
							FMT_TYPE_CAPTURE);
	}

	/*
	 * Decompression of a JPEG file with 4:2:0 subsampling and odd
	 * width to the YUV 4:2:0 compliant formats produces a raw image
	 * with broken luma component. Adjust capture format to RGB565
	 * in such a case.
	 */
	if (ctx->subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_420 &&
	    (ctx->out_q.w & 1) &&
	    (pix->pixelformat == V4L2_PIX_FMT_NV12 ||
	     pix->pixelformat == V4L2_PIX_FMT_NV21 ||
	     pix->pixelformat == V4L2_PIX_FMT_YUV420)) {
		pix->pixelformat = V4L2_PIX_FMT_RGB565;
		fmt = s5p_jpeg_find_format(ctx, pix->pixelformat,
							FMT_TYPE_CAPTURE);
	}

exit:
	return vidioc_try_fmt(f, fmt, ctx, FMT_TYPE_CAPTURE);
}

static int s5p_jpeg_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);
	struct s5p_jpeg_fmt *fmt;

	fmt = s5p_jpeg_find_format(ctx, f->fmt.pix.pixelformat,
						FMT_TYPE_OUTPUT);
	if (!fmt) {
		v4l2_err(&ctx->jpeg->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f, fmt, ctx, FMT_TYPE_OUTPUT);
}

static int exynos4_jpeg_get_output_buffer_size(struct s5p_jpeg_ctx *ctx,
						struct v4l2_format *f,
						int fmt_depth)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	u32 pix_fmt = f->fmt.pix.pixelformat;
	int w = pix->width, h = pix->height, wh_align;
	int padding = 0;

	if (pix_fmt == V4L2_PIX_FMT_RGB32 ||
	    pix_fmt == V4L2_PIX_FMT_RGB565 ||
	    pix_fmt == V4L2_PIX_FMT_NV24 ||
	    pix_fmt == V4L2_PIX_FMT_NV42 ||
	    pix_fmt == V4L2_PIX_FMT_NV12 ||
	    pix_fmt == V4L2_PIX_FMT_NV21 ||
	    pix_fmt == V4L2_PIX_FMT_YUV420)
		wh_align = 4;
	else
		wh_align = 1;

	jpeg_bound_align_image(ctx, &w, S5P_JPEG_MIN_WIDTH,
			       S5P_JPEG_MAX_WIDTH, wh_align,
			       &h, S5P_JPEG_MIN_HEIGHT,
			       S5P_JPEG_MAX_HEIGHT, wh_align);

	if (ctx->jpeg->variant->version == SJPEG_EXYNOS4)
		padding = PAGE_SIZE;

	return (w * h * fmt_depth >> 3) + padding;
}

static int exynos3250_jpeg_try_downscale(struct s5p_jpeg_ctx *ctx,
				   struct v4l2_rect *r);

static int s5p_jpeg_s_fmt(struct s5p_jpeg_ctx *ct, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct s5p_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_ctrl *ctrl_subs;
	struct v4l2_rect scale_rect;
	unsigned int f_type;

	vq = v4l2_m2m_get_vq(ct->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ct, f->type);
	BUG_ON(q_data == NULL);

	if (vb2_is_busy(vq)) {
		v4l2_err(&ct->jpeg->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	f_type = V4L2_TYPE_IS_OUTPUT(f->type) ?
			FMT_TYPE_OUTPUT : FMT_TYPE_CAPTURE;

	q_data->fmt = s5p_jpeg_find_format(ct, pix->pixelformat, f_type);
	if (ct->mode == S5P_JPEG_ENCODE ||
		(ct->mode == S5P_JPEG_DECODE &&
		q_data->fmt->fourcc != V4L2_PIX_FMT_JPEG)) {
		q_data->w = pix->width;
		q_data->h = pix->height;
	}
	if (q_data->fmt->fourcc != V4L2_PIX_FMT_JPEG) {
		/*
		 * During encoding Exynos4x12 SoCs access wider memory area
		 * than it results from Image_x and Image_y values written to
		 * the JPEG_IMAGE_SIZE register. In order to avoid sysmmu
		 * page fault calculate proper buffer size in such a case.
		 */
		if (ct->jpeg->variant->hw_ex4_compat &&
		    f_type == FMT_TYPE_OUTPUT && ct->mode == S5P_JPEG_ENCODE)
			q_data->size = exynos4_jpeg_get_output_buffer_size(ct,
							f,
							q_data->fmt->depth);
		else
			q_data->size = q_data->w * q_data->h *
						q_data->fmt->depth >> 3;
	} else {
		q_data->size = pix->sizeimage;
	}

	if (f_type == FMT_TYPE_OUTPUT) {
		ctrl_subs = v4l2_ctrl_find(&ct->ctrl_handler,
					V4L2_CID_JPEG_CHROMA_SUBSAMPLING);
		if (ctrl_subs)
			v4l2_ctrl_s_ctrl(ctrl_subs, q_data->fmt->subsampling);
		ct->crop_altered = false;
	}

	/*
	 * For decoding init crop_rect with capture buffer dimmensions which
	 * contain aligned dimensions of the input JPEG image and do it only
	 * if crop rectangle hasn't been altered by the user space e.g. with
	 * S_SELECTION ioctl. For encoding assign output buffer dimensions.
	 */
	if (!ct->crop_altered &&
	    ((ct->mode == S5P_JPEG_DECODE && f_type == FMT_TYPE_CAPTURE) ||
	     (ct->mode == S5P_JPEG_ENCODE && f_type == FMT_TYPE_OUTPUT))) {
		ct->crop_rect.width = pix->width;
		ct->crop_rect.height = pix->height;
	}

	/*
	 * Prevent downscaling to YUV420 format by more than 2
	 * for Exynos3250/compatible SoC as it produces broken raw image
	 * in such cases.
	 */
	if (ct->mode == S5P_JPEG_DECODE &&
	    f_type == FMT_TYPE_CAPTURE &&
	    ct->jpeg->variant->hw3250_compat &&
	    pix->pixelformat == V4L2_PIX_FMT_YUV420 &&
	    ct->scale_factor > 2) {
		scale_rect.width = ct->out_q.w / 2;
		scale_rect.height = ct->out_q.h / 2;
		exynos3250_jpeg_try_downscale(ct, &scale_rect);
	}

	return 0;
}

static int s5p_jpeg_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = s5p_jpeg_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return s5p_jpeg_s_fmt(fh_to_ctx(priv), f);
}

static int s5p_jpeg_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = s5p_jpeg_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	return s5p_jpeg_s_fmt(fh_to_ctx(priv), f);
}

static int s5p_jpeg_subscribe_event(struct v4l2_fh *fh,
				    const struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_SOURCE_CHANGE)
		return v4l2_src_change_event_subscribe(fh, sub);

	return -EINVAL;
}

static int exynos3250_jpeg_try_downscale(struct s5p_jpeg_ctx *ctx,
				   struct v4l2_rect *r)
{
	int w_ratio, h_ratio, scale_factor, cur_ratio, i;

	w_ratio = ctx->out_q.w / r->width;
	h_ratio = ctx->out_q.h / r->height;

	scale_factor = max(w_ratio, h_ratio);
	scale_factor = clamp_val(scale_factor, 1, 8);

	/* Align scale ratio to the nearest power of 2 */
	for (i = 0; i <= 3; ++i) {
		cur_ratio = 1 << i;
		if (scale_factor <= cur_ratio) {
			ctx->scale_factor = cur_ratio;
			break;
		}
	}

	r->width = round_down(ctx->out_q.w / ctx->scale_factor, 2);
	r->height = round_down(ctx->out_q.h / ctx->scale_factor, 2);

	ctx->crop_rect.width = r->width;
	ctx->crop_rect.height = r->height;
	ctx->crop_rect.left = 0;
	ctx->crop_rect.top = 0;

	ctx->crop_altered = true;

	return 0;
}

static int exynos3250_jpeg_try_crop(struct s5p_jpeg_ctx *ctx,
				   struct v4l2_rect *r)
{
	struct v4l2_rect base_rect;
	int w_step, h_step;

	switch (ctx->cap_q.fmt->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		w_step = 1;
		h_step = 2;
		break;
	case V4L2_PIX_FMT_YUV420:
		w_step = 2;
		h_step = 2;
		break;
	default:
		w_step = 1;
		h_step = 1;
		break;
	}

	base_rect.top = 0;
	base_rect.left = 0;
	base_rect.width = ctx->out_q.w;
	base_rect.height = ctx->out_q.h;

	r->width = round_down(r->width, w_step);
	r->height = round_down(r->height, h_step);
	r->left = round_down(r->left, 2);
	r->top = round_down(r->top, 2);

	if (!v4l2_rect_enclosed(r, &base_rect))
		return -EINVAL;

	ctx->crop_rect.left = r->left;
	ctx->crop_rect.top = r->top;
	ctx->crop_rect.width = r->width;
	ctx->crop_rect.height = r->height;

	ctx->crop_altered = true;

	return 0;
}

/*
 * V4L2 controls
 */

static int s5p_jpeg_g_selection(struct file *file, void *priv,
			 struct v4l2_selection *s)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* For JPEG blob active == default == bounds */
	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r.width = ctx->out_q.w;
		s->r.height = ctx->out_q.h;
		s->r.left = 0;
		s->r.top = 0;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		s->r.width = ctx->crop_rect.width;
		s->r.height =  ctx->crop_rect.height;
		s->r.left = ctx->crop_rect.left;
		s->r.top = ctx->crop_rect.top;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * V4L2 controls
 */
static int s5p_jpeg_s_selection(struct file *file, void *fh,
				  struct v4l2_selection *s)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(file->private_data);
	struct v4l2_rect *rect = &s->r;
	int ret = -EINVAL;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (s->target == V4L2_SEL_TGT_COMPOSE) {
		if (ctx->mode != S5P_JPEG_DECODE)
			return -EINVAL;
		if (ctx->jpeg->variant->hw3250_compat)
			ret = exynos3250_jpeg_try_downscale(ctx, rect);
	} else if (s->target == V4L2_SEL_TGT_CROP) {
		if (ctx->mode != S5P_JPEG_ENCODE)
			return -EINVAL;
		if (ctx->jpeg->variant->hw3250_compat)
			ret = exynos3250_jpeg_try_crop(ctx, rect);
	}

	return ret;
}

static int s5p_jpeg_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5p_jpeg_ctx *ctx = ctrl_to_ctx(ctrl);
	struct s5p_jpeg *jpeg = ctx->jpeg;
	unsigned long flags;

	switch (ctrl->id) {
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
		spin_lock_irqsave(&jpeg->slock, flags);
		ctrl->val = s5p_jpeg_to_user_subsampling(ctx);
		spin_unlock_irqrestore(&jpeg->slock, flags);
		break;
	}

	return 0;
}

static int s5p_jpeg_adjust_subs_ctrl(struct s5p_jpeg_ctx *ctx, int *ctrl_val)
{
	switch (ctx->jpeg->variant->version) {
	case SJPEG_S5P:
		return 0;
	case SJPEG_EXYNOS3250:
	case SJPEG_EXYNOS5420:
		/*
		 * The exynos3250/compatible device can produce JPEG image only
		 * of 4:4:4 subsampling when given RGB32 source image.
		 */
		if (ctx->out_q.fmt->fourcc == V4L2_PIX_FMT_RGB32)
			*ctrl_val = 0;
		break;
	case SJPEG_EXYNOS4:
		/*
		 * The exynos4x12 device requires input raw image fourcc
		 * to be V4L2_PIX_FMT_GREY if gray jpeg format
		 * is to be set.
		 */
		if (ctx->out_q.fmt->fourcc != V4L2_PIX_FMT_GREY &&
		    *ctrl_val == V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY)
			return -EINVAL;
		break;
	}

	/*
	 * The exynos4x12 and exynos3250/compatible devices require resulting
	 * jpeg subsampling not to be lower than the input raw image
	 * subsampling.
	 */
	if (ctx->out_q.fmt->subsampling > *ctrl_val)
		*ctrl_val = ctx->out_q.fmt->subsampling;

	return 0;
}

static int s5p_jpeg_try_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5p_jpeg_ctx *ctx = ctrl_to_ctx(ctrl);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ctx->jpeg->slock, flags);

	if (ctrl->id == V4L2_CID_JPEG_CHROMA_SUBSAMPLING)
		ret = s5p_jpeg_adjust_subs_ctrl(ctx, &ctrl->val);

	spin_unlock_irqrestore(&ctx->jpeg->slock, flags);
	return ret;
}

static int s5p_jpeg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5p_jpeg_ctx *ctx = ctrl_to_ctx(ctrl);
	unsigned long flags;

	spin_lock_irqsave(&ctx->jpeg->slock, flags);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ctx->compr_quality = ctrl->val;
		break;
	case V4L2_CID_JPEG_RESTART_INTERVAL:
		ctx->restart_interval = ctrl->val;
		break;
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
		ctx->subsampling = ctrl->val;
		break;
	}

	spin_unlock_irqrestore(&ctx->jpeg->slock, flags);
	return 0;
}

static const struct v4l2_ctrl_ops s5p_jpeg_ctrl_ops = {
	.g_volatile_ctrl	= s5p_jpeg_g_volatile_ctrl,
	.try_ctrl		= s5p_jpeg_try_ctrl,
	.s_ctrl			= s5p_jpeg_s_ctrl,
};

static int s5p_jpeg_controls_create(struct s5p_jpeg_ctx *ctx)
{
	unsigned int mask = ~0x27; /* 444, 422, 420, GRAY */
	struct v4l2_ctrl *ctrl;
	int ret;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 3);

	if (ctx->mode == S5P_JPEG_ENCODE) {
		v4l2_ctrl_new_std(&ctx->ctrl_handler, &s5p_jpeg_ctrl_ops,
				  V4L2_CID_JPEG_COMPRESSION_QUALITY,
				  0, 3, 1, S5P_JPEG_COMPR_QUAL_WORST);

		v4l2_ctrl_new_std(&ctx->ctrl_handler, &s5p_jpeg_ctrl_ops,
				  V4L2_CID_JPEG_RESTART_INTERVAL,
				  0, 0xffff, 1, 0);
		if (ctx->jpeg->variant->version == SJPEG_S5P)
			mask = ~0x06; /* 422, 420 */
	}

	ctrl = v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &s5p_jpeg_ctrl_ops,
				      V4L2_CID_JPEG_CHROMA_SUBSAMPLING,
				      V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY, mask,
				      V4L2_JPEG_CHROMA_SUBSAMPLING_422);

	if (ctx->ctrl_handler.error) {
		ret = ctx->ctrl_handler.error;
		goto error_free;
	}

	if (ctx->mode == S5P_JPEG_DECODE)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	ret = v4l2_ctrl_handler_setup(&ctx->ctrl_handler);
	if (ret < 0)
		goto error_free;

	return ret;

error_free:
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	return ret;
}

static const struct v4l2_ioctl_ops s5p_jpeg_ioctl_ops = {
	.vidioc_querycap		= s5p_jpeg_querycap,

	.vidioc_enum_fmt_vid_cap	= s5p_jpeg_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= s5p_jpeg_enum_fmt_vid_out,

	.vidioc_g_fmt_vid_cap		= s5p_jpeg_g_fmt,
	.vidioc_g_fmt_vid_out		= s5p_jpeg_g_fmt,

	.vidioc_try_fmt_vid_cap		= s5p_jpeg_try_fmt_vid_cap,
	.vidioc_try_fmt_vid_out		= s5p_jpeg_try_fmt_vid_out,

	.vidioc_s_fmt_vid_cap		= s5p_jpeg_s_fmt_vid_cap,
	.vidioc_s_fmt_vid_out		= s5p_jpeg_s_fmt_vid_out,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_g_selection		= s5p_jpeg_g_selection,
	.vidioc_s_selection		= s5p_jpeg_s_selection,

	.vidioc_subscribe_event		= s5p_jpeg_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/*
 * ============================================================================
 * mem2mem callbacks
 * ============================================================================
 */

static void s5p_jpeg_device_run(void *priv)
{
	struct s5p_jpeg_ctx *ctx = priv;
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	unsigned long src_addr, dst_addr, flags;

	spin_lock_irqsave(&ctx->jpeg->slock, flags);

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	src_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);

	s5p_jpeg_reset(jpeg->regs);
	s5p_jpeg_poweron(jpeg->regs);
	s5p_jpeg_proc_mode(jpeg->regs, ctx->mode);
	if (ctx->mode == S5P_JPEG_ENCODE) {
		if (ctx->out_q.fmt->fourcc == V4L2_PIX_FMT_RGB565)
			s5p_jpeg_input_raw_mode(jpeg->regs,
							S5P_JPEG_RAW_IN_565);
		else
			s5p_jpeg_input_raw_mode(jpeg->regs,
							S5P_JPEG_RAW_IN_422);
		s5p_jpeg_subsampling_mode(jpeg->regs, ctx->subsampling);
		s5p_jpeg_dri(jpeg->regs, ctx->restart_interval);
		s5p_jpeg_x(jpeg->regs, ctx->out_q.w);
		s5p_jpeg_y(jpeg->regs, ctx->out_q.h);
		s5p_jpeg_imgadr(jpeg->regs, src_addr);
		s5p_jpeg_jpgadr(jpeg->regs, dst_addr);

		/* ultimately comes from sizeimage from userspace */
		s5p_jpeg_enc_stream_int(jpeg->regs, ctx->cap_q.size);

		/* JPEG RGB to YCbCr conversion matrix */
		s5p_jpeg_coef(jpeg->regs, 1, 1, S5P_JPEG_COEF11);
		s5p_jpeg_coef(jpeg->regs, 1, 2, S5P_JPEG_COEF12);
		s5p_jpeg_coef(jpeg->regs, 1, 3, S5P_JPEG_COEF13);
		s5p_jpeg_coef(jpeg->regs, 2, 1, S5P_JPEG_COEF21);
		s5p_jpeg_coef(jpeg->regs, 2, 2, S5P_JPEG_COEF22);
		s5p_jpeg_coef(jpeg->regs, 2, 3, S5P_JPEG_COEF23);
		s5p_jpeg_coef(jpeg->regs, 3, 1, S5P_JPEG_COEF31);
		s5p_jpeg_coef(jpeg->regs, 3, 2, S5P_JPEG_COEF32);
		s5p_jpeg_coef(jpeg->regs, 3, 3, S5P_JPEG_COEF33);

		/*
		 * JPEG IP allows storing 4 quantization tables
		 * We fill table 0 for luma and table 1 for chroma
		 */
		s5p_jpeg_set_qtbl_lum(jpeg->regs, ctx->compr_quality);
		s5p_jpeg_set_qtbl_chr(jpeg->regs, ctx->compr_quality);
		/* use table 0 for Y */
		s5p_jpeg_qtbl(jpeg->regs, 1, 0);
		/* use table 1 for Cb and Cr*/
		s5p_jpeg_qtbl(jpeg->regs, 2, 1);
		s5p_jpeg_qtbl(jpeg->regs, 3, 1);

		/* Y, Cb, Cr use Huffman table 0 */
		s5p_jpeg_htbl_ac(jpeg->regs, 1);
		s5p_jpeg_htbl_dc(jpeg->regs, 1);
		s5p_jpeg_htbl_ac(jpeg->regs, 2);
		s5p_jpeg_htbl_dc(jpeg->regs, 2);
		s5p_jpeg_htbl_ac(jpeg->regs, 3);
		s5p_jpeg_htbl_dc(jpeg->regs, 3);
	} else { /* S5P_JPEG_DECODE */
		s5p_jpeg_rst_int_enable(jpeg->regs, true);
		s5p_jpeg_data_num_int_enable(jpeg->regs, true);
		s5p_jpeg_final_mcu_num_int_enable(jpeg->regs, true);
		if (ctx->cap_q.fmt->fourcc == V4L2_PIX_FMT_YUYV)
			s5p_jpeg_outform_raw(jpeg->regs, S5P_JPEG_RAW_OUT_422);
		else
			s5p_jpeg_outform_raw(jpeg->regs, S5P_JPEG_RAW_OUT_420);
		s5p_jpeg_jpgadr(jpeg->regs, src_addr);
		s5p_jpeg_imgadr(jpeg->regs, dst_addr);
	}

	s5p_jpeg_start(jpeg->regs);

	spin_unlock_irqrestore(&ctx->jpeg->slock, flags);
}

static void exynos4_jpeg_set_img_addr(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct s5p_jpeg_fmt *fmt;
	struct vb2_v4l2_buffer *vb;
	struct s5p_jpeg_addr jpeg_addr = {};
	u32 pix_size, padding_bytes = 0;

	jpeg_addr.cb = 0;
	jpeg_addr.cr = 0;

	pix_size = ctx->cap_q.w * ctx->cap_q.h;

	if (ctx->mode == S5P_JPEG_ENCODE) {
		vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
		fmt = ctx->out_q.fmt;
		if (ctx->out_q.w % 2 && fmt->h_align > 0)
			padding_bytes = ctx->out_q.h;
	} else {
		fmt = ctx->cap_q.fmt;
		vb = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	}

	jpeg_addr.y = vb2_dma_contig_plane_dma_addr(&vb->vb2_buf, 0);

	if (fmt->colplanes == 2) {
		jpeg_addr.cb = jpeg_addr.y + pix_size - padding_bytes;
	} else if (fmt->colplanes == 3) {
		jpeg_addr.cb = jpeg_addr.y + pix_size;
		if (fmt->fourcc == V4L2_PIX_FMT_YUV420)
			jpeg_addr.cr = jpeg_addr.cb + pix_size / 4;
		else
			jpeg_addr.cr = jpeg_addr.cb + pix_size / 2;
	}

	exynos4_jpeg_set_frame_buf_address(jpeg->regs, &jpeg_addr);
}

static void exynos4_jpeg_set_jpeg_addr(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *vb;
	unsigned int jpeg_addr = 0;

	if (ctx->mode == S5P_JPEG_ENCODE)
		vb = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	else
		vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);

	jpeg_addr = vb2_dma_contig_plane_dma_addr(&vb->vb2_buf, 0);
	if (jpeg->variant->version == SJPEG_EXYNOS5433 &&
	    ctx->mode == S5P_JPEG_DECODE)
		jpeg_addr += ctx->out_q.sos;
	exynos4_jpeg_set_stream_buf_address(jpeg->regs, jpeg_addr);
}

static inline void exynos4_jpeg_set_img_fmt(void __iomem *base,
					    unsigned int img_fmt)
{
	__exynos4_jpeg_set_img_fmt(base, img_fmt, SJPEG_EXYNOS4);
}

static inline void exynos5433_jpeg_set_img_fmt(void __iomem *base,
					       unsigned int img_fmt)
{
	__exynos4_jpeg_set_img_fmt(base, img_fmt, SJPEG_EXYNOS5433);
}

static inline void exynos4_jpeg_set_enc_out_fmt(void __iomem *base,
						unsigned int out_fmt)
{
	__exynos4_jpeg_set_enc_out_fmt(base, out_fmt, SJPEG_EXYNOS4);
}

static inline void exynos5433_jpeg_set_enc_out_fmt(void __iomem *base,
						   unsigned int out_fmt)
{
	__exynos4_jpeg_set_enc_out_fmt(base, out_fmt, SJPEG_EXYNOS5433);
}

static void exynos4_jpeg_device_run(void *priv)
{
	struct s5p_jpeg_ctx *ctx = priv;
	struct s5p_jpeg *jpeg = ctx->jpeg;
	unsigned int bitstream_size;
	unsigned long flags;

	spin_lock_irqsave(&jpeg->slock, flags);

	if (ctx->mode == S5P_JPEG_ENCODE) {
		exynos4_jpeg_sw_reset(jpeg->regs);
		exynos4_jpeg_set_interrupt(jpeg->regs, jpeg->variant->version);
		exynos4_jpeg_set_huf_table_enable(jpeg->regs, 1);

		exynos4_jpeg_set_huff_tbl(jpeg->regs);

		/*
		 * JPEG IP allows storing 4 quantization tables
		 * We fill table 0 for luma and table 1 for chroma
		 */
		exynos4_jpeg_set_qtbl_lum(jpeg->regs, ctx->compr_quality);
		exynos4_jpeg_set_qtbl_chr(jpeg->regs, ctx->compr_quality);

		exynos4_jpeg_set_encode_tbl_select(jpeg->regs,
							ctx->compr_quality);
		exynos4_jpeg_set_stream_size(jpeg->regs, ctx->cap_q.w,
							ctx->cap_q.h);

		if (ctx->jpeg->variant->version == SJPEG_EXYNOS4) {
			exynos4_jpeg_set_enc_out_fmt(jpeg->regs,
						     ctx->subsampling);
			exynos4_jpeg_set_img_fmt(jpeg->regs,
						 ctx->out_q.fmt->fourcc);
		} else {
			exynos5433_jpeg_set_enc_out_fmt(jpeg->regs,
							ctx->subsampling);
			exynos5433_jpeg_set_img_fmt(jpeg->regs,
						    ctx->out_q.fmt->fourcc);
		}
		exynos4_jpeg_set_img_addr(ctx);
		exynos4_jpeg_set_jpeg_addr(ctx);
		exynos4_jpeg_set_encode_hoff_cnt(jpeg->regs,
							ctx->out_q.fmt->fourcc);
	} else {
		exynos4_jpeg_sw_reset(jpeg->regs);
		exynos4_jpeg_set_interrupt(jpeg->regs,
					   jpeg->variant->version);
		exynos4_jpeg_set_img_addr(ctx);
		exynos4_jpeg_set_jpeg_addr(ctx);

		if (jpeg->variant->version == SJPEG_EXYNOS5433) {
			exynos4_jpeg_parse_huff_tbl(ctx);
			exynos4_jpeg_parse_decode_h_tbl(ctx);

			exynos4_jpeg_parse_q_tbl(ctx);
			exynos4_jpeg_parse_decode_q_tbl(ctx);

			exynos4_jpeg_set_huf_table_enable(jpeg->regs, 1);

			exynos4_jpeg_set_stream_size(jpeg->regs, ctx->cap_q.w,
					ctx->cap_q.h);
			exynos5433_jpeg_set_enc_out_fmt(jpeg->regs,
							ctx->subsampling);
			exynos5433_jpeg_set_img_fmt(jpeg->regs,
						    ctx->cap_q.fmt->fourcc);
			bitstream_size = DIV_ROUND_UP(ctx->out_q.size, 16);
		} else {
			exynos4_jpeg_set_img_fmt(jpeg->regs,
						 ctx->cap_q.fmt->fourcc);
			bitstream_size = DIV_ROUND_UP(ctx->out_q.size, 32);
		}

		exynos4_jpeg_set_dec_bitstream_size(jpeg->regs, bitstream_size);
	}

	exynos4_jpeg_set_sys_int_enable(jpeg->regs, 1);
	exynos4_jpeg_set_enc_dec_mode(jpeg->regs, ctx->mode);

	spin_unlock_irqrestore(&jpeg->slock, flags);
}

static void exynos3250_jpeg_set_img_addr(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct s5p_jpeg_fmt *fmt;
	struct vb2_v4l2_buffer *vb;
	struct s5p_jpeg_addr jpeg_addr = {};
	u32 pix_size;

	pix_size = ctx->cap_q.w * ctx->cap_q.h;

	if (ctx->mode == S5P_JPEG_ENCODE) {
		vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
		fmt = ctx->out_q.fmt;
	} else {
		vb = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
		fmt = ctx->cap_q.fmt;
	}

	jpeg_addr.y = vb2_dma_contig_plane_dma_addr(&vb->vb2_buf, 0);

	if (fmt->colplanes == 2) {
		jpeg_addr.cb = jpeg_addr.y + pix_size;
	} else if (fmt->colplanes == 3) {
		jpeg_addr.cb = jpeg_addr.y + pix_size;
		if (fmt->fourcc == V4L2_PIX_FMT_YUV420)
			jpeg_addr.cr = jpeg_addr.cb + pix_size / 4;
		else
			jpeg_addr.cr = jpeg_addr.cb + pix_size / 2;
	}

	exynos3250_jpeg_imgadr(jpeg->regs, &jpeg_addr);
}

static void exynos3250_jpeg_set_jpeg_addr(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *vb;
	unsigned int jpeg_addr = 0;

	if (ctx->mode == S5P_JPEG_ENCODE)
		vb = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	else
		vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);

	jpeg_addr = vb2_dma_contig_plane_dma_addr(&vb->vb2_buf, 0);
	exynos3250_jpeg_jpgadr(jpeg->regs, jpeg_addr);
}

static void exynos3250_jpeg_device_run(void *priv)
{
	struct s5p_jpeg_ctx *ctx = priv;
	struct s5p_jpeg *jpeg = ctx->jpeg;
	unsigned long flags;

	spin_lock_irqsave(&ctx->jpeg->slock, flags);

	exynos3250_jpeg_reset(jpeg->regs);
	exynos3250_jpeg_set_dma_num(jpeg->regs);
	exynos3250_jpeg_poweron(jpeg->regs);
	exynos3250_jpeg_clk_set(jpeg->regs);
	exynos3250_jpeg_proc_mode(jpeg->regs, ctx->mode);

	if (ctx->mode == S5P_JPEG_ENCODE) {
		exynos3250_jpeg_input_raw_fmt(jpeg->regs,
					      ctx->out_q.fmt->fourcc);
		exynos3250_jpeg_dri(jpeg->regs, ctx->restart_interval);

		/*
		 * JPEG IP allows storing 4 quantization tables
		 * We fill table 0 for luma and table 1 for chroma
		 */
		s5p_jpeg_set_qtbl_lum(jpeg->regs, ctx->compr_quality);
		s5p_jpeg_set_qtbl_chr(jpeg->regs, ctx->compr_quality);
		/* use table 0 for Y */
		exynos3250_jpeg_qtbl(jpeg->regs, 1, 0);
		/* use table 1 for Cb and Cr*/
		exynos3250_jpeg_qtbl(jpeg->regs, 2, 1);
		exynos3250_jpeg_qtbl(jpeg->regs, 3, 1);

		/*
		 * Some SoCs require setting Huffman tables before each run
		 */
		if (jpeg->variant->htbl_reinit) {
			s5p_jpeg_set_hdctbl(jpeg->regs);
			s5p_jpeg_set_hdctblg(jpeg->regs);
			s5p_jpeg_set_hactbl(jpeg->regs);
			s5p_jpeg_set_hactblg(jpeg->regs);
		}

		/* Y, Cb, Cr use Huffman table 0 */
		exynos3250_jpeg_htbl_ac(jpeg->regs, 1);
		exynos3250_jpeg_htbl_dc(jpeg->regs, 1);
		exynos3250_jpeg_htbl_ac(jpeg->regs, 2);
		exynos3250_jpeg_htbl_dc(jpeg->regs, 2);
		exynos3250_jpeg_htbl_ac(jpeg->regs, 3);
		exynos3250_jpeg_htbl_dc(jpeg->regs, 3);

		exynos3250_jpeg_set_x(jpeg->regs, ctx->crop_rect.width);
		exynos3250_jpeg_set_y(jpeg->regs, ctx->crop_rect.height);
		exynos3250_jpeg_stride(jpeg->regs, ctx->out_q.fmt->fourcc,
								ctx->out_q.w);
		exynos3250_jpeg_offset(jpeg->regs, ctx->crop_rect.left,
							ctx->crop_rect.top);
		exynos3250_jpeg_set_img_addr(ctx);
		exynos3250_jpeg_set_jpeg_addr(ctx);
		exynos3250_jpeg_subsampling_mode(jpeg->regs, ctx->subsampling);

		/* ultimately comes from sizeimage from userspace */
		exynos3250_jpeg_enc_stream_bound(jpeg->regs, ctx->cap_q.size);

		if (ctx->out_q.fmt->fourcc == V4L2_PIX_FMT_RGB565 ||
		    ctx->out_q.fmt->fourcc == V4L2_PIX_FMT_RGB565X ||
		    ctx->out_q.fmt->fourcc == V4L2_PIX_FMT_RGB32)
			exynos3250_jpeg_set_y16(jpeg->regs, true);
	} else {
		exynos3250_jpeg_set_img_addr(ctx);
		exynos3250_jpeg_set_jpeg_addr(ctx);
		exynos3250_jpeg_stride(jpeg->regs, ctx->cap_q.fmt->fourcc,
								ctx->cap_q.w);
		exynos3250_jpeg_offset(jpeg->regs, 0, 0);
		exynos3250_jpeg_dec_scaling_ratio(jpeg->regs,
							ctx->scale_factor);
		exynos3250_jpeg_dec_stream_size(jpeg->regs, ctx->out_q.size);
		exynos3250_jpeg_output_raw_fmt(jpeg->regs,
						ctx->cap_q.fmt->fourcc);
	}

	exynos3250_jpeg_interrupts_enable(jpeg->regs);

	/* JPEG RGB to YCbCr conversion matrix */
	exynos3250_jpeg_coef(jpeg->regs, ctx->mode);

	exynos3250_jpeg_set_timer(jpeg->regs, EXYNOS3250_IRQ_TIMEOUT);
	jpeg->irq_status = 0;
	exynos3250_jpeg_start(jpeg->regs);

	spin_unlock_irqrestore(&ctx->jpeg->slock, flags);
}

static int s5p_jpeg_job_ready(void *priv)
{
	struct s5p_jpeg_ctx *ctx = priv;

	if (ctx->mode == S5P_JPEG_DECODE) {
		/*
		 * We have only one input buffer and one output buffer. If there
		 * is a resolution change event, no need to continue decoding.
		 */
		if (ctx->state == JPEGCTX_RESOLUTION_CHANGE)
			return 0;

		return ctx->hdr_parsed;
	}

	return 1;
}

static const struct v4l2_m2m_ops s5p_jpeg_m2m_ops = {
	.device_run	= s5p_jpeg_device_run,
	.job_ready	= s5p_jpeg_job_ready,
};

static const struct v4l2_m2m_ops exynos3250_jpeg_m2m_ops = {
	.device_run	= exynos3250_jpeg_device_run,
	.job_ready	= s5p_jpeg_job_ready,
};

static const struct v4l2_m2m_ops exynos4_jpeg_m2m_ops = {
	.device_run	= exynos4_jpeg_device_run,
	.job_ready	= s5p_jpeg_job_ready,
};

/*
 * ============================================================================
 * Queue operations
 * ============================================================================
 */

static int s5p_jpeg_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	struct s5p_jpeg_q_data *q_data = NULL;
	unsigned int size, count = *nbuffers;

	q_data = get_q_data(ctx, vq->type);
	BUG_ON(q_data == NULL);

	size = q_data->size;

	/*
	 * header is parsed during decoding and parsed information stored
	 * in the context so we do not allow another buffer to overwrite it
	 */
	if (ctx->mode == S5P_JPEG_DECODE)
		count = 1;

	*nbuffers = count;
	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int s5p_jpeg_buf_prepare(struct vb2_buffer *vb)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct s5p_jpeg_q_data *q_data = NULL;

	q_data = get_q_data(ctx, vb->vb2_queue->type);
	BUG_ON(q_data == NULL);

	if (vb2_plane_size(vb, 0) < q_data->size) {
		pr_err("%s data will not fit into plane (%lu < %lu)\n",
				__func__, vb2_plane_size(vb, 0),
				(long)q_data->size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, q_data->size);

	return 0;
}

static void s5p_jpeg_set_capture_queue_data(struct s5p_jpeg_ctx *ctx)
{
	struct s5p_jpeg_q_data *q_data = &ctx->cap_q;

	q_data->w = ctx->out_q.w;
	q_data->h = ctx->out_q.h;

	/*
	 * This call to jpeg_bound_align_image() takes care of width and
	 * height values alignment when user space calls the QBUF of
	 * OUTPUT buffer after the S_FMT of CAPTURE buffer.
	 * Please note that on Exynos4x12 SoCs, resigning from executing
	 * S_FMT on capture buffer for each JPEG image can result in a
	 * hardware hangup if subsampling is lower than the one of input
	 * JPEG.
	 */
	jpeg_bound_align_image(ctx, &q_data->w, S5P_JPEG_MIN_WIDTH,
			       S5P_JPEG_MAX_WIDTH, q_data->fmt->h_align,
			       &q_data->h, S5P_JPEG_MIN_HEIGHT,
			       S5P_JPEG_MAX_HEIGHT, q_data->fmt->v_align);

	q_data->size = q_data->w * q_data->h * q_data->fmt->depth >> 3;
}

static void s5p_jpeg_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (ctx->mode == S5P_JPEG_DECODE &&
	    vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		static const struct v4l2_event ev_src_ch = {
			.type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
		};
		struct vb2_queue *dst_vq;
		u32 ori_w;
		u32 ori_h;

		dst_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
					 V4L2_BUF_TYPE_VIDEO_CAPTURE);
		ori_w = ctx->out_q.w;
		ori_h = ctx->out_q.h;

		ctx->hdr_parsed = s5p_jpeg_parse_hdr(&ctx->out_q,
		     (unsigned long)vb2_plane_vaddr(vb, 0),
		     min((unsigned long)ctx->out_q.size,
			 vb2_get_plane_payload(vb, 0)), ctx);
		if (!ctx->hdr_parsed) {
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
			return;
		}

		/*
		 * If there is a resolution change event, only update capture
		 * queue when it is not streaming. Otherwise, update it in
		 * STREAMOFF. See s5p_jpeg_stop_streaming for detail.
		 */
		if (ctx->out_q.w != ori_w || ctx->out_q.h != ori_h) {
			v4l2_event_queue_fh(&ctx->fh, &ev_src_ch);
			if (vb2_is_streaming(dst_vq))
				ctx->state = JPEGCTX_RESOLUTION_CHANGE;
			else
				s5p_jpeg_set_capture_queue_data(ctx);
		}
	}

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int s5p_jpeg_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(q);

	return pm_runtime_resume_and_get(ctx->jpeg->dev);
}

static void s5p_jpeg_stop_streaming(struct vb2_queue *q)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(q);

	/*
	 * STREAMOFF is an acknowledgment for resolution change event.
	 * Before STREAMOFF, we still have to return the old resolution and
	 * subsampling. Update capture queue when the stream is off.
	 */
	if (ctx->state == JPEGCTX_RESOLUTION_CHANGE &&
	    q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		s5p_jpeg_set_capture_queue_data(ctx);
		ctx->state = JPEGCTX_RUNNING;
	}

	pm_runtime_put(ctx->jpeg->dev);
}

static const struct vb2_ops s5p_jpeg_qops = {
	.queue_setup		= s5p_jpeg_queue_setup,
	.buf_prepare		= s5p_jpeg_buf_prepare,
	.buf_queue		= s5p_jpeg_buf_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= s5p_jpeg_start_streaming,
	.stop_streaming		= s5p_jpeg_stop_streaming,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct s5p_jpeg_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &s5p_jpeg_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->jpeg->lock;
	src_vq->dev = ctx->jpeg->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &s5p_jpeg_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->jpeg->lock;
	dst_vq->dev = ctx->jpeg->dev;

	return vb2_queue_init(dst_vq);
}

/*
 * ============================================================================
 * ISR
 * ============================================================================
 */

static irqreturn_t s5p_jpeg_irq(int irq, void *dev_id)
{
	struct s5p_jpeg *jpeg = dev_id;
	struct s5p_jpeg_ctx *curr_ctx;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	unsigned long payload_size = 0;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	bool enc_jpeg_too_large = false;
	bool timer_elapsed = false;
	bool op_completed = false;

	spin_lock(&jpeg->slock);

	curr_ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);

	src_buf = v4l2_m2m_src_buf_remove(curr_ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(curr_ctx->fh.m2m_ctx);

	if (curr_ctx->mode == S5P_JPEG_ENCODE)
		enc_jpeg_too_large = s5p_jpeg_enc_stream_stat(jpeg->regs);
	timer_elapsed = s5p_jpeg_timer_stat(jpeg->regs);
	op_completed = s5p_jpeg_result_stat_ok(jpeg->regs);
	if (curr_ctx->mode == S5P_JPEG_DECODE)
		op_completed = op_completed &&
					s5p_jpeg_stream_stat_ok(jpeg->regs);

	if (enc_jpeg_too_large) {
		state = VB2_BUF_STATE_ERROR;
		s5p_jpeg_clear_enc_stream_stat(jpeg->regs);
	} else if (timer_elapsed) {
		state = VB2_BUF_STATE_ERROR;
		s5p_jpeg_clear_timer_stat(jpeg->regs);
	} else if (!op_completed) {
		state = VB2_BUF_STATE_ERROR;
	} else {
		payload_size = s5p_jpeg_compressed_size(jpeg->regs);
	}

	dst_buf->timecode = src_buf->timecode;
	dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
	dst_buf->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_buf->flags |=
		src_buf->flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

	v4l2_m2m_buf_done(src_buf, state);
	if (curr_ctx->mode == S5P_JPEG_ENCODE)
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, payload_size);
	v4l2_m2m_buf_done(dst_buf, state);

	curr_ctx->subsampling = s5p_jpeg_get_subsampling_mode(jpeg->regs);
	spin_unlock(&jpeg->slock);

	s5p_jpeg_clear_int(jpeg->regs);

	v4l2_m2m_job_finish(jpeg->m2m_dev, curr_ctx->fh.m2m_ctx);
	return IRQ_HANDLED;
}

static irqreturn_t exynos4_jpeg_irq(int irq, void *priv)
{
	unsigned int int_status;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	struct s5p_jpeg *jpeg = priv;
	struct s5p_jpeg_ctx *curr_ctx;
	unsigned long payload_size = 0;

	spin_lock(&jpeg->slock);

	exynos4_jpeg_set_sys_int_enable(jpeg->regs, 0);

	curr_ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);

	src_vb = v4l2_m2m_src_buf_remove(curr_ctx->fh.m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(curr_ctx->fh.m2m_ctx);

	int_status = exynos4_jpeg_get_int_status(jpeg->regs);

	if (int_status) {
		switch (int_status & 0x1f) {
		case 0x1:
			jpeg->irq_ret = ERR_PROT;
			break;
		case 0x2:
			jpeg->irq_ret = OK_ENC_OR_DEC;
			break;
		case 0x4:
			jpeg->irq_ret = ERR_DEC_INVALID_FORMAT;
			break;
		case 0x8:
			jpeg->irq_ret = ERR_MULTI_SCAN;
			break;
		case 0x10:
			jpeg->irq_ret = ERR_FRAME;
			break;
		default:
			jpeg->irq_ret = ERR_UNKNOWN;
			break;
		}
	} else {
		jpeg->irq_ret = ERR_UNKNOWN;
	}

	if (jpeg->irq_ret == OK_ENC_OR_DEC) {
		if (curr_ctx->mode == S5P_JPEG_ENCODE) {
			payload_size = exynos4_jpeg_get_stream_size(jpeg->regs);
			vb2_set_plane_payload(&dst_vb->vb2_buf,
					0, payload_size);
		}
		v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
	} else {
		v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
		v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);
	}

	if (jpeg->variant->version == SJPEG_EXYNOS4)
		curr_ctx->subsampling = exynos4_jpeg_get_frame_fmt(jpeg->regs);

	exynos4_jpeg_set_enc_dec_mode(jpeg->regs, S5P_JPEG_DISABLE);

	spin_unlock(&jpeg->slock);

	v4l2_m2m_job_finish(jpeg->m2m_dev, curr_ctx->fh.m2m_ctx);
	return IRQ_HANDLED;
}

static irqreturn_t exynos3250_jpeg_irq(int irq, void *dev_id)
{
	struct s5p_jpeg *jpeg = dev_id;
	struct s5p_jpeg_ctx *curr_ctx;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	unsigned long payload_size = 0;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	bool interrupt_timeout = false;
	bool stream_error = false;
	u32 irq_status;

	spin_lock(&jpeg->slock);

	irq_status = exynos3250_jpeg_get_timer_status(jpeg->regs);
	if (irq_status & EXYNOS3250_TIMER_INT_STAT) {
		exynos3250_jpeg_clear_timer_status(jpeg->regs);
		interrupt_timeout = true;
		dev_err(jpeg->dev, "Interrupt timeout occurred.\n");
	}

	irq_status = exynos3250_jpeg_get_int_status(jpeg->regs);
	exynos3250_jpeg_clear_int_status(jpeg->regs, irq_status);

	jpeg->irq_status |= irq_status;

	if (jpeg->variant->version == SJPEG_EXYNOS5420 &&
	    irq_status & EXYNOS3250_STREAM_STAT) {
		stream_error = true;
		dev_err(jpeg->dev, "Syntax error or unrecoverable error occurred.\n");
	}

	curr_ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);

	if (!curr_ctx)
		goto exit_unlock;

	if ((irq_status & EXYNOS3250_HEADER_STAT) &&
	    (curr_ctx->mode == S5P_JPEG_DECODE)) {
		exynos3250_jpeg_rstart(jpeg->regs);
		goto exit_unlock;
	}

	if (jpeg->irq_status & (EXYNOS3250_JPEG_DONE |
				EXYNOS3250_WDMA_DONE |
				EXYNOS3250_RDMA_DONE |
				EXYNOS3250_RESULT_STAT))
		payload_size = exynos3250_jpeg_compressed_size(jpeg->regs);
	else if (interrupt_timeout || stream_error)
		state = VB2_BUF_STATE_ERROR;
	else
		goto exit_unlock;

	src_buf = v4l2_m2m_src_buf_remove(curr_ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(curr_ctx->fh.m2m_ctx);

	dst_buf->timecode = src_buf->timecode;
	dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;

	v4l2_m2m_buf_done(src_buf, state);
	if (curr_ctx->mode == S5P_JPEG_ENCODE)
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, payload_size);
	v4l2_m2m_buf_done(dst_buf, state);

	curr_ctx->subsampling =
			exynos3250_jpeg_get_subsampling_mode(jpeg->regs);

	spin_unlock(&jpeg->slock);

	v4l2_m2m_job_finish(jpeg->m2m_dev, curr_ctx->fh.m2m_ctx);
	return IRQ_HANDLED;

exit_unlock:
	spin_unlock(&jpeg->slock);
	return IRQ_HANDLED;
}

static void *jpeg_get_drv_data(struct device *dev);

/*
 * ============================================================================
 * Driver basic infrastructure
 * ============================================================================
 */

static int s5p_jpeg_probe(struct platform_device *pdev)
{
	struct s5p_jpeg *jpeg;
	int i, ret;

	/* JPEG IP abstraction struct */
	jpeg = devm_kzalloc(&pdev->dev, sizeof(struct s5p_jpeg), GFP_KERNEL);
	if (!jpeg)
		return -ENOMEM;

	jpeg->variant = jpeg_get_drv_data(&pdev->dev);
	if (!jpeg->variant)
		return -ENODEV;

	mutex_init(&jpeg->lock);
	spin_lock_init(&jpeg->slock);
	jpeg->dev = &pdev->dev;

	/* memory-mapped registers */
	jpeg->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(jpeg->regs))
		return PTR_ERR(jpeg->regs);

	/* interrupt service routine registration */
	jpeg->irq = ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	ret = devm_request_irq(&pdev->dev, jpeg->irq, jpeg->variant->jpeg_irq,
				0, dev_name(&pdev->dev), jpeg);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", jpeg->irq);
		return ret;
	}

	/* clocks */
	for (i = 0; i < jpeg->variant->num_clocks; i++) {
		jpeg->clocks[i] = devm_clk_get(&pdev->dev,
					      jpeg->variant->clk_names[i]);
		if (IS_ERR(jpeg->clocks[i])) {
			dev_err(&pdev->dev, "failed to get clock: %s\n",
				jpeg->variant->clk_names[i]);
			return PTR_ERR(jpeg->clocks[i]);
		}
	}

	/* v4l2 device */
	ret = v4l2_device_register(&pdev->dev, &jpeg->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		return ret;
	}

	/* mem2mem device */
	jpeg->m2m_dev = v4l2_m2m_init(jpeg->variant->m2m_ops);
	if (IS_ERR(jpeg->m2m_dev)) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(jpeg->m2m_dev);
		goto device_register_rollback;
	}

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	/* JPEG encoder /dev/videoX node */
	jpeg->vfd_encoder = video_device_alloc();
	if (!jpeg->vfd_encoder) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto m2m_init_rollback;
	}
	snprintf(jpeg->vfd_encoder->name, sizeof(jpeg->vfd_encoder->name),
				"%s-enc", S5P_JPEG_M2M_NAME);
	jpeg->vfd_encoder->fops		= &s5p_jpeg_fops;
	jpeg->vfd_encoder->ioctl_ops	= &s5p_jpeg_ioctl_ops;
	jpeg->vfd_encoder->minor	= -1;
	jpeg->vfd_encoder->release	= video_device_release;
	jpeg->vfd_encoder->lock		= &jpeg->lock;
	jpeg->vfd_encoder->v4l2_dev	= &jpeg->v4l2_dev;
	jpeg->vfd_encoder->vfl_dir	= VFL_DIR_M2M;
	jpeg->vfd_encoder->device_caps	= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M;

	ret = video_register_device(jpeg->vfd_encoder, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register video device\n");
		video_device_release(jpeg->vfd_encoder);
		goto m2m_init_rollback;
	}

	video_set_drvdata(jpeg->vfd_encoder, jpeg);
	v4l2_info(&jpeg->v4l2_dev,
		  "encoder device registered as /dev/video%d\n",
		  jpeg->vfd_encoder->num);

	/* JPEG decoder /dev/videoX node */
	jpeg->vfd_decoder = video_device_alloc();
	if (!jpeg->vfd_decoder) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto enc_vdev_register_rollback;
	}
	snprintf(jpeg->vfd_decoder->name, sizeof(jpeg->vfd_decoder->name),
				"%s-dec", S5P_JPEG_M2M_NAME);
	jpeg->vfd_decoder->fops		= &s5p_jpeg_fops;
	jpeg->vfd_decoder->ioctl_ops	= &s5p_jpeg_ioctl_ops;
	jpeg->vfd_decoder->minor	= -1;
	jpeg->vfd_decoder->release	= video_device_release;
	jpeg->vfd_decoder->lock		= &jpeg->lock;
	jpeg->vfd_decoder->v4l2_dev	= &jpeg->v4l2_dev;
	jpeg->vfd_decoder->vfl_dir	= VFL_DIR_M2M;
	jpeg->vfd_decoder->device_caps	= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M;

	ret = video_register_device(jpeg->vfd_decoder, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register video device\n");
		video_device_release(jpeg->vfd_decoder);
		goto enc_vdev_register_rollback;
	}

	video_set_drvdata(jpeg->vfd_decoder, jpeg);
	v4l2_info(&jpeg->v4l2_dev,
		  "decoder device registered as /dev/video%d\n",
		  jpeg->vfd_decoder->num);

	/* final statements & power management */
	platform_set_drvdata(pdev, jpeg);

	pm_runtime_enable(&pdev->dev);

	v4l2_info(&jpeg->v4l2_dev, "Samsung S5P JPEG codec\n");

	return 0;

enc_vdev_register_rollback:
	video_unregister_device(jpeg->vfd_encoder);

m2m_init_rollback:
	v4l2_m2m_release(jpeg->m2m_dev);

device_register_rollback:
	v4l2_device_unregister(&jpeg->v4l2_dev);

	return ret;
}

static void s5p_jpeg_remove(struct platform_device *pdev)
{
	struct s5p_jpeg *jpeg = platform_get_drvdata(pdev);
	int i;

	pm_runtime_disable(jpeg->dev);

	video_unregister_device(jpeg->vfd_decoder);
	video_unregister_device(jpeg->vfd_encoder);
	vb2_dma_contig_clear_max_seg_size(&pdev->dev);
	v4l2_m2m_release(jpeg->m2m_dev);
	v4l2_device_unregister(&jpeg->v4l2_dev);

	if (!pm_runtime_status_suspended(&pdev->dev)) {
		for (i = jpeg->variant->num_clocks - 1; i >= 0; i--)
			clk_disable_unprepare(jpeg->clocks[i]);
	}
}

#ifdef CONFIG_PM
static int s5p_jpeg_runtime_suspend(struct device *dev)
{
	struct s5p_jpeg *jpeg = dev_get_drvdata(dev);
	int i;

	for (i = jpeg->variant->num_clocks - 1; i >= 0; i--)
		clk_disable_unprepare(jpeg->clocks[i]);

	return 0;
}

static int s5p_jpeg_runtime_resume(struct device *dev)
{
	struct s5p_jpeg *jpeg = dev_get_drvdata(dev);
	unsigned long flags;
	int i, ret;

	for (i = 0; i < jpeg->variant->num_clocks; i++) {
		ret = clk_prepare_enable(jpeg->clocks[i]);
		if (ret) {
			while (--i >= 0)
				clk_disable_unprepare(jpeg->clocks[i]);
			return ret;
		}
	}

	spin_lock_irqsave(&jpeg->slock, flags);

	/*
	 * JPEG IP allows storing two Huffman tables for each component.
	 * We fill table 0 for each component and do this here only
	 * for S5PC210 and Exynos3250 SoCs. Exynos4x12 and Exynos542x SoC
	 * require programming their Huffman tables each time the encoding
	 * process is initialized, and thus it is accomplished in the
	 * device_run callback of m2m_ops.
	 */
	if (!jpeg->variant->htbl_reinit) {
		s5p_jpeg_set_hdctbl(jpeg->regs);
		s5p_jpeg_set_hdctblg(jpeg->regs);
		s5p_jpeg_set_hactbl(jpeg->regs);
		s5p_jpeg_set_hactblg(jpeg->regs);
	}

	spin_unlock_irqrestore(&jpeg->slock, flags);

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops s5p_jpeg_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(s5p_jpeg_runtime_suspend, s5p_jpeg_runtime_resume,
			   NULL)
};

static struct s5p_jpeg_variant s5p_jpeg_drvdata = {
	.version	= SJPEG_S5P,
	.jpeg_irq	= s5p_jpeg_irq,
	.m2m_ops	= &s5p_jpeg_m2m_ops,
	.fmt_ver_flag	= SJPEG_FMT_FLAG_S5P,
	.clk_names	= {"jpeg"},
	.num_clocks	= 1,
};

static struct s5p_jpeg_variant exynos3250_jpeg_drvdata = {
	.version	= SJPEG_EXYNOS3250,
	.jpeg_irq	= exynos3250_jpeg_irq,
	.m2m_ops	= &exynos3250_jpeg_m2m_ops,
	.fmt_ver_flag	= SJPEG_FMT_FLAG_EXYNOS3250,
	.hw3250_compat	= 1,
	.clk_names	= {"jpeg", "sclk"},
	.num_clocks	= 2,
};

static struct s5p_jpeg_variant exynos4_jpeg_drvdata = {
	.version	= SJPEG_EXYNOS4,
	.jpeg_irq	= exynos4_jpeg_irq,
	.m2m_ops	= &exynos4_jpeg_m2m_ops,
	.fmt_ver_flag	= SJPEG_FMT_FLAG_EXYNOS4,
	.htbl_reinit	= 1,
	.clk_names	= {"jpeg"},
	.num_clocks	= 1,
	.hw_ex4_compat	= 1,
};

static struct s5p_jpeg_variant exynos5420_jpeg_drvdata = {
	.version	= SJPEG_EXYNOS5420,
	.jpeg_irq	= exynos3250_jpeg_irq,		/* intentionally 3250 */
	.m2m_ops	= &exynos3250_jpeg_m2m_ops,	/* intentionally 3250 */
	.fmt_ver_flag	= SJPEG_FMT_FLAG_EXYNOS3250,	/* intentionally 3250 */
	.hw3250_compat	= 1,
	.htbl_reinit	= 1,
	.clk_names	= {"jpeg"},
	.num_clocks	= 1,
};

static struct s5p_jpeg_variant exynos5433_jpeg_drvdata = {
	.version	= SJPEG_EXYNOS5433,
	.jpeg_irq	= exynos4_jpeg_irq,
	.m2m_ops	= &exynos4_jpeg_m2m_ops,
	.fmt_ver_flag	= SJPEG_FMT_FLAG_EXYNOS4,
	.htbl_reinit	= 1,
	.clk_names	= {"pclk", "aclk", "aclk_xiu", "sclk"},
	.num_clocks	= 4,
	.hw_ex4_compat	= 1,
};

static const struct of_device_id samsung_jpeg_match[] = {
	{
		.compatible = "samsung,s5pv210-jpeg",
		.data = &s5p_jpeg_drvdata,
	}, {
		.compatible = "samsung,exynos3250-jpeg",
		.data = &exynos3250_jpeg_drvdata,
	}, {
		.compatible = "samsung,exynos4210-jpeg",
		.data = &exynos4_jpeg_drvdata,
	}, {
		.compatible = "samsung,exynos4212-jpeg",
		.data = &exynos4_jpeg_drvdata,
	}, {
		.compatible = "samsung,exynos5420-jpeg",
		.data = &exynos5420_jpeg_drvdata,
	}, {
		.compatible = "samsung,exynos5433-jpeg",
		.data = &exynos5433_jpeg_drvdata,
	},
	{},
};

MODULE_DEVICE_TABLE(of, samsung_jpeg_match);

static void *jpeg_get_drv_data(struct device *dev)
{
	struct s5p_jpeg_variant *driver_data = NULL;
	const struct of_device_id *match;

	if (!IS_ENABLED(CONFIG_OF) || !dev->of_node)
		return &s5p_jpeg_drvdata;

	match = of_match_node(samsung_jpeg_match, dev->of_node);

	if (match)
		driver_data = (struct s5p_jpeg_variant *)match->data;

	return driver_data;
}

static struct platform_driver s5p_jpeg_driver = {
	.probe = s5p_jpeg_probe,
	.remove_new = s5p_jpeg_remove,
	.driver = {
		.of_match_table	= samsung_jpeg_match,
		.name		= S5P_JPEG_M2M_NAME,
		.pm		= &s5p_jpeg_pm_ops,
	},
};

module_platform_driver(s5p_jpeg_driver);

MODULE_AUTHOR("Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>");
MODULE_AUTHOR("Jacek Anaszewski <j.anaszewski@samsung.com>");
MODULE_DESCRIPTION("Samsung JPEG codec driver");
MODULE_LICENSE("GPL");
