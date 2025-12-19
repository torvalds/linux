// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 * Copyright (C) 2020 Maxime Chevallier <maxime.chevallier@bootlin.com>
 * Copyright (C) 2023 Mehdi Djait <mehdi.djait@bootlin.com>
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#include <media/v4l2-common.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

#include "rkcif-capture-dvp.h"
#include "rkcif-common.h"
#include "rkcif-interface.h"
#include "rkcif-regs.h"
#include "rkcif-stream.h"

static const struct rkcif_output_fmt dvp_out_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_OUTPUT_422 |
			       RKCIF_FORMAT_UV_STORAGE_ORDER_UVUV,
		.cplanes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16M,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_OUTPUT_422 |
			       RKCIF_FORMAT_UV_STORAGE_ORDER_UVUV,
		.cplanes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_OUTPUT_422 |
			       RKCIF_FORMAT_UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61M,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_OUTPUT_422 |
			       RKCIF_FORMAT_UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_OUTPUT_420 |
			       RKCIF_FORMAT_UV_STORAGE_ORDER_UVUV,
		.cplanes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_OUTPUT_420 |
			       RKCIF_FORMAT_UV_STORAGE_ORDER_UVUV,
		.cplanes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_OUTPUT_420 |
			       RKCIF_FORMAT_UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21M,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_OUTPUT_420 |
			       RKCIF_FORMAT_UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR666,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.cplanes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_Y16,
		.cplanes = 1,
	},
};

static const struct rkcif_input_fmt px30_dvp_in_fmts[] = {
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YUYV,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YUYV,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YVYU,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YVYU,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_UYVY,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_UYVY,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_VYUY,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_VYUY,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	}
};

const struct rkcif_dvp_match_data rkcif_px30_vip_dvp_match_data = {
	.in_fmts = px30_dvp_in_fmts,
	.in_fmts_num = ARRAY_SIZE(px30_dvp_in_fmts),
	.out_fmts = dvp_out_fmts,
	.out_fmts_num = ARRAY_SIZE(dvp_out_fmts),
	.has_scaler = true,
	.regs = {
		[RKCIF_DVP_CTRL] = 0x00,
		[RKCIF_DVP_INTEN] = 0x04,
		[RKCIF_DVP_INTSTAT] = 0x08,
		[RKCIF_DVP_FOR] = 0x0c,
		[RKCIF_DVP_LINE_NUM_ADDR] = 0x10,
		[RKCIF_DVP_FRM0_ADDR_Y] = 0x14,
		[RKCIF_DVP_FRM0_ADDR_UV] = 0x18,
		[RKCIF_DVP_FRM1_ADDR_Y] = 0x1c,
		[RKCIF_DVP_FRM1_ADDR_UV] = 0x20,
		[RKCIF_DVP_VIR_LINE_WIDTH] = 0x24,
		[RKCIF_DVP_SET_SIZE] = 0x28,
		[RKCIF_DVP_SCL_CTRL] = 0x48,
		[RKCIF_DVP_FRAME_STATUS] = 0x60,
		[RKCIF_DVP_LAST_LINE] = 0x68,
		[RKCIF_DVP_LAST_PIX] = 0x6c,
	},
};

static const struct rkcif_input_fmt rk3568_dvp_in_fmts[] = {
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YUYV,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YUYV,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YVYU,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YVYU,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_UYVY,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_UYVY,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_VYUY,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_VYUY,
		.fmt_type = RKCIF_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_1X16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YUYV |
			       RKCIF_FORMAT_INPUT_MODE_BT1120 |
			       RKCIF_FORMAT_BT1120_TRANSMIT_PROGRESS,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_1X16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YUYV |
			       RKCIF_FORMAT_INPUT_MODE_BT1120,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_1X16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YVYU |
			       RKCIF_FORMAT_INPUT_MODE_BT1120 |
			       RKCIF_FORMAT_BT1120_TRANSMIT_PROGRESS,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_1X16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YVYU |
			       RKCIF_FORMAT_INPUT_MODE_BT1120,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YUYV |
			       RKCIF_FORMAT_INPUT_MODE_BT1120 |
			       RKCIF_FORMAT_BT1120_YC_SWAP |
			       RKCIF_FORMAT_BT1120_TRANSMIT_PROGRESS,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YUYV |
			       RKCIF_FORMAT_BT1120_YC_SWAP |
			       RKCIF_FORMAT_INPUT_MODE_BT1120,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_1X16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YVYU |
			       RKCIF_FORMAT_INPUT_MODE_BT1120 |
			       RKCIF_FORMAT_BT1120_YC_SWAP |
			       RKCIF_FORMAT_BT1120_TRANSMIT_PROGRESS,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_1X16,
		.dvp_fmt_val = RKCIF_FORMAT_YUV_INPUT_422 |
			       RKCIF_FORMAT_YUV_INPUT_ORDER_YVYU |
			       RKCIF_FORMAT_BT1120_YC_SWAP |
			       RKCIF_FORMAT_INPUT_MODE_BT1120,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_8,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y10_1X10,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_10,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y12_1X12,
		.dvp_fmt_val = RKCIF_FORMAT_INPUT_MODE_RAW |
			       RKCIF_FORMAT_RAW_DATA_WIDTH_12,
		.fmt_type = RKCIF_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
};

static void rk3568_dvp_grf_setup(struct rkcif_device *rkcif)
{
	u32 con1 = RK3568_GRF_WRITE_ENABLE(RK3568_GRF_VI_CON1_CIF_DATAPATH |
					   RK3568_GRF_VI_CON1_CIF_CLK_DELAYNUM);

	if (!rkcif->grf)
		return;

	con1 |= rkcif->interfaces[RKCIF_DVP].dvp.dvp_clk_delay &
		RK3568_GRF_VI_CON1_CIF_CLK_DELAYNUM;

	if (rkcif->interfaces[RKCIF_DVP].vep.bus.parallel.flags &
	    V4L2_MBUS_PCLK_SAMPLE_DUALEDGE)
		con1 |= RK3568_GRF_VI_CON1_CIF_DATAPATH;

	regmap_write(rkcif->grf, RK3568_GRF_VI_CON1, con1);
}

const struct rkcif_dvp_match_data rkcif_rk3568_vicap_dvp_match_data = {
	.in_fmts = rk3568_dvp_in_fmts,
	.in_fmts_num = ARRAY_SIZE(rk3568_dvp_in_fmts),
	.out_fmts = dvp_out_fmts,
	.out_fmts_num = ARRAY_SIZE(dvp_out_fmts),
	.setup = rk3568_dvp_grf_setup,
	.has_scaler = false,
	.regs = {
		[RKCIF_DVP_CTRL] = 0x00,
		[RKCIF_DVP_INTEN] = 0x04,
		[RKCIF_DVP_INTSTAT] = 0x08,
		[RKCIF_DVP_FOR] = 0x0c,
		[RKCIF_DVP_LINE_NUM_ADDR] = 0x2c,
		[RKCIF_DVP_FRM0_ADDR_Y] = 0x14,
		[RKCIF_DVP_FRM0_ADDR_UV] = 0x18,
		[RKCIF_DVP_FRM1_ADDR_Y] = 0x1c,
		[RKCIF_DVP_FRM1_ADDR_UV] = 0x20,
		[RKCIF_DVP_VIR_LINE_WIDTH] = 0x24,
		[RKCIF_DVP_SET_SIZE] = 0x28,
		[RKCIF_DVP_CROP] = 0x34,
		[RKCIF_DVP_FRAME_STATUS] = 0x3c,
		[RKCIF_DVP_LAST_LINE] = 0x44,
		[RKCIF_DVP_LAST_PIX] = 0x48,
	},
};

static inline unsigned int rkcif_dvp_get_addr(struct rkcif_device *rkcif,
					      unsigned int index)
{
	if (WARN_ON_ONCE(index >= RKCIF_DVP_REGISTER_MAX))
		return RKCIF_REGISTER_NOTSUPPORTED;

	return rkcif->match_data->dvp->regs[index];
}

static inline __maybe_unused void rkcif_dvp_write(struct rkcif_device *rkcif,
						  unsigned int index, u32 val)
{
	unsigned int addr = rkcif_dvp_get_addr(rkcif, index);

	if (addr == RKCIF_REGISTER_NOTSUPPORTED)
		return;

	writel(val, rkcif->base_addr + addr);
}

static inline __maybe_unused u32 rkcif_dvp_read(struct rkcif_device *rkcif,
						unsigned int index)
{
	unsigned int addr = rkcif_dvp_get_addr(rkcif, index);

	if (addr == RKCIF_REGISTER_NOTSUPPORTED)
		return 0;

	return readl(rkcif->base_addr + addr);
}

static void rkcif_dvp_queue_buffer(struct rkcif_stream *stream,
				   unsigned int index)
{
	struct rkcif_device *rkcif = stream->rkcif;
	struct rkcif_buffer *buffer = stream->buffers[index];
	u32 frm_addr_y, frm_addr_uv;

	frm_addr_y = index ? RKCIF_DVP_FRM1_ADDR_Y : RKCIF_DVP_FRM0_ADDR_Y;
	frm_addr_uv = index ? RKCIF_DVP_FRM1_ADDR_UV : RKCIF_DVP_FRM0_ADDR_UV;

	rkcif_dvp_write(rkcif, frm_addr_y, buffer->buff_addr[RKCIF_PLANE_Y]);
	rkcif_dvp_write(rkcif, frm_addr_uv, buffer->buff_addr[RKCIF_PLANE_UV]);
}

static int rkcif_dvp_start_streaming(struct rkcif_stream *stream)
{
	struct rkcif_device *rkcif = stream->rkcif;
	struct rkcif_interface *interface = stream->interface;
	struct v4l2_mbus_config_parallel *parallel;
	struct v4l2_mbus_framefmt *source_fmt;
	struct v4l2_subdev_state *state;
	const struct rkcif_input_fmt *active_in_fmt;
	const struct rkcif_output_fmt *active_out_fmt;
	u32 val = 0;
	int ret = -EINVAL;

	state = v4l2_subdev_lock_and_get_active_state(&interface->sd);
	source_fmt = v4l2_subdev_state_get_format(state, RKCIF_IF_PAD_SRC,
						  stream->id);
	if (!source_fmt)
		goto out;

	active_in_fmt = rkcif_interface_find_input_fmt(interface, false,
						       source_fmt->code);
	active_out_fmt = rkcif_stream_find_output_fmt(stream, false,
						      stream->pix.pixelformat);
	if (!active_in_fmt || !active_out_fmt)
		goto out;

	parallel = &interface->vep.bus.parallel;
	if (parallel->bus_width == 16 &&
	    (parallel->flags & V4L2_MBUS_PCLK_SAMPLE_DUALEDGE))
		val |= RKCIF_FORMAT_BT1120_CLOCK_DOUBLE_EDGES;
	val |= active_in_fmt->dvp_fmt_val;
	val |= active_out_fmt->dvp_fmt_val;
	rkcif_dvp_write(rkcif, RKCIF_DVP_FOR, val);

	val = stream->pix.width;
	if (active_in_fmt->fmt_type == RKCIF_FMT_TYPE_RAW)
		val = stream->pix.width * 2;
	rkcif_dvp_write(rkcif, RKCIF_DVP_VIR_LINE_WIDTH, val);

	val = RKCIF_XY_COORD(stream->pix.width, stream->pix.height);
	rkcif_dvp_write(rkcif, RKCIF_DVP_SET_SIZE, val);

	rkcif_dvp_write(rkcif, RKCIF_DVP_FRAME_STATUS, RKCIF_FRAME_STAT_CLS);
	rkcif_dvp_write(rkcif, RKCIF_DVP_INTSTAT, RKCIF_INTSTAT_CLS);
	if (rkcif->match_data->dvp->has_scaler) {
		val = active_in_fmt->fmt_type == RKCIF_FMT_TYPE_YUV ?
			      RKCIF_SCL_CTRL_ENABLE_YUV_16BIT_BYPASS :
			      RKCIF_SCL_CTRL_ENABLE_RAW_16BIT_BYPASS;
		rkcif_dvp_write(rkcif, RKCIF_DVP_SCL_CTRL, val);
	}

	rkcif_dvp_write(rkcif, RKCIF_DVP_INTEN,
			RKCIF_INTEN_FRAME_END_EN |
			RKCIF_INTEN_PST_INF_FRAME_END_EN);

	rkcif_dvp_write(rkcif, RKCIF_DVP_CTRL,
			RKCIF_CTRL_AXI_BURST_16 | RKCIF_CTRL_MODE_PINGPONG |
			RKCIF_CTRL_ENABLE_CAPTURE);

	ret = 0;

out:
	v4l2_subdev_unlock_state(state);
	return ret;
}

static void rkcif_dvp_stop_streaming(struct rkcif_stream *stream)
{
	struct rkcif_device *rkcif = stream->rkcif;
	u32 val;

	val = rkcif_dvp_read(rkcif, RKCIF_DVP_CTRL);
	rkcif_dvp_write(rkcif, RKCIF_DVP_CTRL,
			val & (~RKCIF_CTRL_ENABLE_CAPTURE));
	rkcif_dvp_write(rkcif, RKCIF_DVP_INTEN, 0x0);
	rkcif_dvp_write(rkcif, RKCIF_DVP_INTSTAT, 0x3ff);
	rkcif_dvp_write(rkcif, RKCIF_DVP_FRAME_STATUS, 0x0);

	stream->stopping = false;
}

static void rkcif_dvp_reset_stream(struct rkcif_device *rkcif)
{
	u32 ctl = rkcif_dvp_read(rkcif, RKCIF_DVP_CTRL);

	rkcif_dvp_write(rkcif, RKCIF_DVP_CTRL,
			ctl & (~RKCIF_CTRL_ENABLE_CAPTURE));
	rkcif_dvp_write(rkcif, RKCIF_DVP_CTRL, ctl | RKCIF_CTRL_ENABLE_CAPTURE);
}

static void rkcif_dvp_set_crop(struct rkcif_stream *stream, u16 left, u16 top)
{
	struct rkcif_device *rkcif = stream->rkcif;
	u32 val;

	val = RKCIF_XY_COORD(left, top);
	rkcif_dvp_write(rkcif, RKCIF_DVP_CROP, val);
}

irqreturn_t rkcif_dvp_isr(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkcif_device *rkcif = dev_get_drvdata(dev);
	struct rkcif_stream *stream;
	u32 intstat, lastline, lastpix, cif_frmst;
	irqreturn_t ret = IRQ_NONE;

	if (!rkcif->match_data->dvp)
		return ret;

	intstat = rkcif_dvp_read(rkcif, RKCIF_DVP_INTSTAT);
	cif_frmst = rkcif_dvp_read(rkcif, RKCIF_DVP_FRAME_STATUS);
	lastline = RKCIF_FETCH_Y(rkcif_dvp_read(rkcif, RKCIF_DVP_LAST_LINE));
	lastpix = RKCIF_FETCH_Y(rkcif_dvp_read(rkcif, RKCIF_DVP_LAST_PIX));

	if (intstat & RKCIF_INTSTAT_FRAME_END) {
		rkcif_dvp_write(rkcif, RKCIF_DVP_INTSTAT,
				RKCIF_INTSTAT_FRAME_END_CLR |
				RKCIF_INTSTAT_LINE_END_CLR);

		stream = &rkcif->interfaces[RKCIF_DVP].streams[RKCIF_ID0];

		if (stream->stopping) {
			rkcif_dvp_stop_streaming(stream);
			wake_up(&stream->wq_stopped);
			ret = IRQ_HANDLED;
			goto out;
		}

		if (lastline != stream->pix.height) {
			v4l2_err(&rkcif->v4l2_dev,
				 "bad frame, irq:%#x frmst:%#x size:%dx%d\n",
				 intstat, cif_frmst, lastpix, lastline);

			rkcif_dvp_reset_stream(rkcif);
		}

		rkcif_stream_pingpong(stream);

		ret = IRQ_HANDLED;
	}
out:
	return ret;
}

int rkcif_dvp_register(struct rkcif_device *rkcif)
{
	struct rkcif_interface *interface;
	unsigned int streams_num;
	int ret;

	if (!rkcif->match_data->dvp)
		return 0;

	interface = &rkcif->interfaces[RKCIF_DVP];
	interface->index = RKCIF_DVP;
	interface->type = RKCIF_IF_DVP;
	interface->in_fmts = rkcif->match_data->dvp->in_fmts;
	interface->in_fmts_num = rkcif->match_data->dvp->in_fmts_num;
	interface->set_crop = rkcif_dvp_set_crop;
	ret = rkcif_interface_register(rkcif, interface);
	if (ret)
		return ret;

	if (rkcif->match_data->dvp->setup)
		rkcif->match_data->dvp->setup(rkcif);

	streams_num = rkcif->match_data->dvp->has_ids ? 4 : 1;
	for (unsigned int i = 0; i < streams_num; i++) {
		struct rkcif_stream *stream = &interface->streams[i];

		stream->id = i;
		stream->interface = interface;
		stream->out_fmts = rkcif->match_data->dvp->out_fmts;
		stream->out_fmts_num = rkcif->match_data->dvp->out_fmts_num;
		stream->queue_buffer = rkcif_dvp_queue_buffer;
		stream->start_streaming = rkcif_dvp_start_streaming;
		stream->stop_streaming = rkcif_dvp_stop_streaming;

		ret = rkcif_stream_register(rkcif, stream);
		if (ret)
			goto err_streams_unregister;

		interface->streams_num++;
	}

	return 0;

err_streams_unregister:
	for (unsigned int i = 0; i < interface->streams_num; i++)
		rkcif_stream_unregister(&interface->streams[i]);

	rkcif_interface_unregister(interface);

	return ret;
}

void rkcif_dvp_unregister(struct rkcif_device *rkcif)
{
	struct rkcif_interface *interface;

	if (!rkcif->match_data->dvp)
		return;

	interface = &rkcif->interfaces[RKCIF_DVP];

	for (unsigned int i = 0; i < interface->streams_num; i++)
		rkcif_stream_unregister(&interface->streams[i]);

	rkcif_interface_unregister(interface);
}
