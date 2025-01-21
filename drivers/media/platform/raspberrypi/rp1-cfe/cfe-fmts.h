/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RP1 Camera Front End formats definition
 *
 * Copyright (C) 2021-2024 - Raspberry Pi Ltd.
 */
#ifndef _CFE_FMTS_H_
#define _CFE_FMTS_H_

#include "cfe.h"
#include <media/mipi-csi2.h>

static const struct cfe_fmt formats[] = {
	/* YUV Formats */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_YUV422_8B,
	},
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_YUV422_8B,
	},
	{
		.fourcc = V4L2_PIX_FMT_YVYU,
		.code = MEDIA_BUS_FMT_YVYU8_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_YUV422_8B,
	},
	{
		.fourcc = V4L2_PIX_FMT_VYUY,
		.code = MEDIA_BUS_FMT_VYUY8_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_YUV422_8B,
	},
	{
		/* RGB Formats */
		.fourcc = V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RGB565,
	},
	{	.fourcc = V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.code = MEDIA_BUS_FMT_RGB565_2X8_BE,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RGB565,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB555, /* gggbbbbb arrrrrgg */
		.code = MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RGB555,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB555X, /* arrrrrgg gggbbbbb */
		.code = MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RGB555,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB24, /* rgb */
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.depth = 24,
		.csi_dt = MIPI_CSI2_DT_RGB888,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24, /* bgr */
		.code = MEDIA_BUS_FMT_BGR888_1X24,
		.depth = 24,
		.csi_dt = MIPI_CSI2_DT_RGB888,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB32, /* argb */
		.code = MEDIA_BUS_FMT_ARGB8888_1X32,
		.depth = 32,
		.csi_dt = 0x0,
	},

	/* Bayer Formats */
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.depth = 8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
		.remap = { V4L2_PIX_FMT_SBGGR16, V4L2_PIX_FMT_PISP_COMP1_BGGR },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.depth = 8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
		.remap = { V4L2_PIX_FMT_SGBRG16, V4L2_PIX_FMT_PISP_COMP1_GBRG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.depth = 8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
		.remap = { V4L2_PIX_FMT_SGRBG16, V4L2_PIX_FMT_PISP_COMP1_GRBG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.depth = 8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
		.remap = { V4L2_PIX_FMT_SRGGB16, V4L2_PIX_FMT_PISP_COMP1_RGGB },
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR10P,
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.depth = 10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
		.remap = { V4L2_PIX_FMT_SBGGR16, V4L2_PIX_FMT_PISP_COMP1_BGGR },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG10P,
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.depth = 10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
		.remap = { V4L2_PIX_FMT_SGBRG16, V4L2_PIX_FMT_PISP_COMP1_GBRG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG10P,
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.depth = 10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
		.remap = { V4L2_PIX_FMT_SGRBG16, V4L2_PIX_FMT_PISP_COMP1_GRBG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB10P,
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.depth = 10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
		.remap = { V4L2_PIX_FMT_SRGGB16, V4L2_PIX_FMT_PISP_COMP1_RGGB },
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR12P,
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.depth = 12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
		.remap = { V4L2_PIX_FMT_SBGGR16, V4L2_PIX_FMT_PISP_COMP1_BGGR },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG12P,
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.depth = 12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
		.remap = { V4L2_PIX_FMT_SGBRG16, V4L2_PIX_FMT_PISP_COMP1_GBRG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG12P,
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.depth = 12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
		.remap = { V4L2_PIX_FMT_SGRBG16, V4L2_PIX_FMT_PISP_COMP1_GRBG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB12P,
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.depth = 12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
		.remap = { V4L2_PIX_FMT_SRGGB16, V4L2_PIX_FMT_PISP_COMP1_RGGB },
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR14P,
		.code = MEDIA_BUS_FMT_SBGGR14_1X14,
		.depth = 14,
		.csi_dt = MIPI_CSI2_DT_RAW14,
		.remap = { V4L2_PIX_FMT_SBGGR16, V4L2_PIX_FMT_PISP_COMP1_BGGR },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG14P,
		.code = MEDIA_BUS_FMT_SGBRG14_1X14,
		.depth = 14,
		.csi_dt = MIPI_CSI2_DT_RAW14,
		.remap = { V4L2_PIX_FMT_SGBRG16, V4L2_PIX_FMT_PISP_COMP1_GBRG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG14P,
		.code = MEDIA_BUS_FMT_SGRBG14_1X14,
		.depth = 14,
		.csi_dt = MIPI_CSI2_DT_RAW14,
		.remap = { V4L2_PIX_FMT_SGRBG16, V4L2_PIX_FMT_PISP_COMP1_GRBG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB14P,
		.code = MEDIA_BUS_FMT_SRGGB14_1X14,
		.depth = 14,
		.csi_dt = MIPI_CSI2_DT_RAW14,
		.remap = { V4L2_PIX_FMT_SRGGB16, V4L2_PIX_FMT_PISP_COMP1_RGGB },
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.code = MEDIA_BUS_FMT_SBGGR16_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RAW16,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
		.remap = { V4L2_PIX_FMT_SBGGR16, V4L2_PIX_FMT_PISP_COMP1_BGGR },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG16,
		.code = MEDIA_BUS_FMT_SGBRG16_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RAW16,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
		.remap = { V4L2_PIX_FMT_SGBRG16, V4L2_PIX_FMT_PISP_COMP1_GBRG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG16,
		.code = MEDIA_BUS_FMT_SGRBG16_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RAW16,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
		.remap = { V4L2_PIX_FMT_SGRBG16, V4L2_PIX_FMT_PISP_COMP1_GRBG },
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB16,
		.code = MEDIA_BUS_FMT_SRGGB16_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RAW16,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
		.remap = { V4L2_PIX_FMT_SRGGB16, V4L2_PIX_FMT_PISP_COMP1_RGGB },
	},
	/* PiSP Compressed Mode 1 */
	{
		.fourcc = V4L2_PIX_FMT_PISP_COMP1_RGGB,
		.code = MEDIA_BUS_FMT_SRGGB16_1X16,
		.depth = 8,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
	},
	{
		.fourcc = V4L2_PIX_FMT_PISP_COMP1_BGGR,
		.code = MEDIA_BUS_FMT_SBGGR16_1X16,
		.depth = 8,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
	},
	{
		.fourcc = V4L2_PIX_FMT_PISP_COMP1_GBRG,
		.code = MEDIA_BUS_FMT_SGBRG16_1X16,
		.depth = 8,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
	},
	{
		.fourcc = V4L2_PIX_FMT_PISP_COMP1_GRBG,
		.code = MEDIA_BUS_FMT_SGRBG16_1X16,
		.depth = 8,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
	},
	/* Greyscale format */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.depth = 8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
		.remap = { V4L2_PIX_FMT_Y16, V4L2_PIX_FMT_PISP_COMP1_MONO },
	},
	{
		.fourcc = V4L2_PIX_FMT_Y10P,
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.depth = 10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
		.remap = { V4L2_PIX_FMT_Y16, V4L2_PIX_FMT_PISP_COMP1_MONO },
	},
	{
		.fourcc = V4L2_PIX_FMT_Y12P,
		.code = MEDIA_BUS_FMT_Y12_1X12,
		.depth = 12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
		.remap = { V4L2_PIX_FMT_Y16, V4L2_PIX_FMT_PISP_COMP1_MONO },
	},
	{
		.fourcc = V4L2_PIX_FMT_Y14P,
		.code = MEDIA_BUS_FMT_Y14_1X14,
		.depth = 14,
		.csi_dt = MIPI_CSI2_DT_RAW14,
		.remap = { V4L2_PIX_FMT_Y16, V4L2_PIX_FMT_PISP_COMP1_MONO },
	},
	{
		.fourcc = V4L2_PIX_FMT_Y16,
		.code = MEDIA_BUS_FMT_Y16_1X16,
		.depth = 16,
		.csi_dt = MIPI_CSI2_DT_RAW16,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
		.remap = { V4L2_PIX_FMT_Y16, V4L2_PIX_FMT_PISP_COMP1_MONO },
	},
	{
		.fourcc = V4L2_PIX_FMT_PISP_COMP1_MONO,
		.code = MEDIA_BUS_FMT_Y16_1X16,
		.depth = 8,
		.flags = CFE_FORMAT_FLAG_FE_OUT,
	},

	/* Embedded data formats */
	{
		.fourcc = V4L2_META_FMT_GENERIC_8,
		.code = MEDIA_BUS_FMT_META_8,
		.depth = 8,
		.csi_dt = MIPI_CSI2_DT_EMBEDDED_8B,
		.flags = CFE_FORMAT_FLAG_META_CAP,
	},
	{
		.fourcc = V4L2_META_FMT_GENERIC_CSI2_10,
		.code = MEDIA_BUS_FMT_META_10,
		.depth = 10,
		.csi_dt = MIPI_CSI2_DT_EMBEDDED_8B,
		.flags = CFE_FORMAT_FLAG_META_CAP,
	},
	{
		.fourcc = V4L2_META_FMT_GENERIC_CSI2_12,
		.code = MEDIA_BUS_FMT_META_12,
		.depth = 12,
		.csi_dt = MIPI_CSI2_DT_EMBEDDED_8B,
		.flags = CFE_FORMAT_FLAG_META_CAP,
	},

	/* Frontend formats */
	{
		.fourcc = V4L2_META_FMT_RPI_FE_CFG,
		.code = MEDIA_BUS_FMT_FIXED,
		.flags = CFE_FORMAT_FLAG_META_OUT,
	},
	{
		.fourcc = V4L2_META_FMT_RPI_FE_STATS,
		.code = MEDIA_BUS_FMT_FIXED,
		.flags = CFE_FORMAT_FLAG_META_CAP,
	},
};

#endif /* _CFE_FMTS_H_ */
