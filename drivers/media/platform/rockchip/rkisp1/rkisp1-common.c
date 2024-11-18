// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Common definitions
 *
 * Copyright (C) 2019 Collabora, Ltd.
 */

#include <media/mipi-csi2.h>
#include <media/v4l2-rect.h>

#include "rkisp1-common.h"

static const struct rkisp1_mbus_info rkisp1_formats[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.direction	= RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_RGGB,
		.bus_width	= 10,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 10,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 10,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 10,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_RGGB,
		.bus_width	= 12,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 12,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 12,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 12,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_RGGB,
		.bus_width	= 8,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 8,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 8,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= MIPI_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 8,
		.direction	= RKISP1_ISP_SD_SINK | RKISP1_ISP_SD_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_1X16,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.mipi_dt	= MIPI_CSI2_DT_YUV422_8B,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 16,
		.direction	= RKISP1_ISP_SD_SINK,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_1X16,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.mipi_dt	= MIPI_CSI2_DT_YUV422_8B,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 16,
		.direction	= RKISP1_ISP_SD_SINK,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_1X16,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.mipi_dt	= MIPI_CSI2_DT_YUV422_8B,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 16,
		.direction	= RKISP1_ISP_SD_SINK,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_1X16,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.mipi_dt	= MIPI_CSI2_DT_YUV422_8B,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 16,
		.direction	= RKISP1_ISP_SD_SINK,
	},
};

const struct rkisp1_mbus_info *rkisp1_mbus_info_get_by_index(unsigned int index)
{
	if (index >= ARRAY_SIZE(rkisp1_formats))
		return NULL;

	return &rkisp1_formats[index];
}

const struct rkisp1_mbus_info *rkisp1_mbus_info_get_by_code(u32 mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rkisp1_formats); i++) {
		const struct rkisp1_mbus_info *fmt = &rkisp1_formats[i];

		if (fmt->mbus_code == mbus_code)
			return fmt;
	}

	return NULL;
}

static const struct v4l2_rect rkisp1_sd_min_crop = {
	.width = RKISP1_ISP_MIN_WIDTH,
	.height = RKISP1_ISP_MIN_HEIGHT,
	.top = 0,
	.left = 0,
};

void rkisp1_sd_adjust_crop_rect(struct v4l2_rect *crop,
				const struct v4l2_rect *bounds)
{
	v4l2_rect_set_min_size(crop, &rkisp1_sd_min_crop);
	v4l2_rect_map_inside(crop, bounds);
}

void rkisp1_sd_adjust_crop(struct v4l2_rect *crop,
			   const struct v4l2_mbus_framefmt *bounds)
{
	struct v4l2_rect crop_bounds = {
		.left = 0,
		.top = 0,
		.width = bounds->width,
		.height = bounds->height,
	};

	rkisp1_sd_adjust_crop_rect(crop, &crop_bounds);
}

void rkisp1_bls_swap_regs(enum rkisp1_fmt_raw_pat_type pattern,
			  const u32 input[4], u32 output[4])
{
	static const unsigned int swap[4][4] = {
		[RKISP1_RAW_RGGB] = { 0, 1, 2, 3 },
		[RKISP1_RAW_GRBG] = { 1, 0, 3, 2 },
		[RKISP1_RAW_GBRG] = { 2, 3, 0, 1 },
		[RKISP1_RAW_BGGR] = { 3, 2, 1, 0 },
	};

	for (unsigned int i = 0; i < 4; ++i)
		output[i] = input[swap[pattern][i]];
}
