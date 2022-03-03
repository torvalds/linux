// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *	Huang Lee <Putin.li@rock-chips.com>
 */

#include "rga_hw_config.h"

const uint32_t rga3_input_raster_format[] = {
	RGA_FORMAT_RGBA_8888,
	RGA_FORMAT_BGRA_8888,
	RGA_FORMAT_RGB_888,
	RGA_FORMAT_BGR_888,
	RGA_FORMAT_RGB_565,
	RGA_FORMAT_BGR_565,
	RGA_FORMAT_YCbCr_422_SP,
	RGA_FORMAT_YCbCr_420_SP,
	RGA_FORMAT_YCrCb_422_SP,
	RGA_FORMAT_YCrCb_420_SP,
	RGA_FORMAT_YVYU_422,
	RGA_FORMAT_VYUY_422,
	RGA_FORMAT_YUYV_422,
	RGA_FORMAT_UYVY_422,
	RGA_FORMAT_YCbCr_420_SP_10B,
	RGA_FORMAT_YCrCb_420_SP_10B,
	RGA_FORMAT_YCbCr_422_SP_10B,
	RGA_FORMAT_YCrCb_422_SP_10B,
	RGA_FORMAT_ARGB_8888,
	RGA_FORMAT_ABGR_8888,
};

const uint32_t rga3_output_raster_format[] = {
	RGA_FORMAT_RGBA_8888,
	RGA_FORMAT_BGRA_8888,
	RGA_FORMAT_RGB_888,
	RGA_FORMAT_BGR_888,
	RGA_FORMAT_RGB_565,
	RGA_FORMAT_BGR_565,
	RGA_FORMAT_YCbCr_422_SP,
	RGA_FORMAT_YCbCr_420_SP,
	RGA_FORMAT_YCrCb_422_SP,
	RGA_FORMAT_YCrCb_420_SP,
	RGA_FORMAT_YVYU_422,
	RGA_FORMAT_VYUY_422,
	RGA_FORMAT_YUYV_422,
	RGA_FORMAT_UYVY_422,
	RGA_FORMAT_YCbCr_420_SP_10B,
	RGA_FORMAT_YCrCb_420_SP_10B,
	RGA_FORMAT_YCbCr_422_SP_10B,
	RGA_FORMAT_YCrCb_422_SP_10B,
};

const uint32_t rga3_fbcd_format[] = {
	RGA_FORMAT_RGBA_8888,
	RGA_FORMAT_BGRA_8888,
	RGA_FORMAT_RGB_888,
	RGA_FORMAT_BGR_888,
	RGA_FORMAT_RGB_565,
	RGA_FORMAT_BGR_565,
	RGA_FORMAT_YCbCr_422_SP,
	RGA_FORMAT_YCbCr_420_SP,
	RGA_FORMAT_YCrCb_422_SP,
	RGA_FORMAT_YCrCb_420_SP,
	RGA_FORMAT_YCbCr_420_SP_10B,
	RGA_FORMAT_YCrCb_420_SP_10B,
	RGA_FORMAT_YCbCr_422_SP_10B,
	RGA_FORMAT_YCrCb_422_SP_10B,
};

const uint32_t rga3_tile_format[] = {
	RGA_FORMAT_YCbCr_422_SP,
	RGA_FORMAT_YCbCr_420_SP,
	RGA_FORMAT_YCrCb_422_SP,
	RGA_FORMAT_YCrCb_420_SP,
	RGA_FORMAT_YCbCr_420_SP_10B,
	RGA_FORMAT_YCrCb_420_SP_10B,
	RGA_FORMAT_YCbCr_422_SP_10B,
	RGA_FORMAT_YCrCb_422_SP_10B,
};

const uint32_t rga2e_input_raster_format[] = {
	RGA_FORMAT_RGBA_8888,
	RGA_FORMAT_RGBX_8888,
	RGA_FORMAT_BGRA_8888,
	RGA_FORMAT_BGRX_8888,
	RGA_FORMAT_RGB_888,
	RGA_FORMAT_BGR_888,
	RGA_FORMAT_RGB_565,
	RGA_FORMAT_BGR_565,
	RGA_FORMAT_YCbCr_422_P,
	RGA_FORMAT_YCbCr_420_P,
	RGA_FORMAT_YCrCb_422_P,
	RGA_FORMAT_YCrCb_420_P,
	RGA_FORMAT_YCbCr_422_SP,
	RGA_FORMAT_YCbCr_420_SP,
	RGA_FORMAT_YCrCb_422_SP,
	RGA_FORMAT_YCrCb_420_SP,
	RGA_FORMAT_YVYU_422,
	RGA_FORMAT_VYUY_422,
	RGA_FORMAT_YUYV_422,
	RGA_FORMAT_UYVY_422,
	RGA_FORMAT_YCbCr_420_SP_10B,
	RGA_FORMAT_YCrCb_420_SP_10B,
	RGA_FORMAT_YCbCr_422_SP_10B,
	RGA_FORMAT_YCrCb_422_SP_10B,
	RGA_FORMAT_YCbCr_400,
	RGA_FORMAT_RGBA_5551,
	RGA_FORMAT_BGRA_5551,
	RGA_FORMAT_RGBA_4444,
	RGA_FORMAT_BGRA_4444,
	RGA_FORMAT_XRGB_8888,
	RGA_FORMAT_XBGR_8888,
	RGA_FORMAT_BPP1,
	RGA_FORMAT_BPP2,
	RGA_FORMAT_BPP4,
	RGA_FORMAT_BPP8,
	RGA_FORMAT_ARGB_8888,
	RGA_FORMAT_ARGB_5551,
	RGA_FORMAT_ARGB_4444,
	RGA_FORMAT_ABGR_8888,
	RGA_FORMAT_ABGR_5551,
	RGA_FORMAT_ABGR_4444,
};

const uint32_t rga2e_output_raster_format[] = {
	RGA_FORMAT_RGBA_8888,
	RGA_FORMAT_RGBX_8888,
	RGA_FORMAT_BGRA_8888,
	RGA_FORMAT_BGRX_8888,
	RGA_FORMAT_RGB_888,
	RGA_FORMAT_BGR_888,
	RGA_FORMAT_RGB_565,
	RGA_FORMAT_BGR_565,
	RGA_FORMAT_YCbCr_422_P,
	RGA_FORMAT_YCbCr_420_P,
	RGA_FORMAT_YCrCb_422_P,
	RGA_FORMAT_YCrCb_420_P,
	RGA_FORMAT_YCbCr_422_SP,
	RGA_FORMAT_YCbCr_420_SP,
	RGA_FORMAT_YCrCb_422_SP,
	RGA_FORMAT_YCrCb_420_SP,
	RGA_FORMAT_YVYU_420,
	RGA_FORMAT_VYUY_420,
	RGA_FORMAT_YUYV_420,
	RGA_FORMAT_UYVY_420,
	RGA_FORMAT_YVYU_422,
	RGA_FORMAT_VYUY_422,
	RGA_FORMAT_YUYV_422,
	RGA_FORMAT_UYVY_422,
	RGA_FORMAT_YCbCr_420_SP_10B,
	RGA_FORMAT_YCrCb_420_SP_10B,
	RGA_FORMAT_YCbCr_422_SP_10B,
	RGA_FORMAT_YCrCb_422_SP_10B,
	RGA_FORMAT_Y4,
	RGA_FORMAT_YCbCr_400,
	RGA_FORMAT_RGBA_5551,
	RGA_FORMAT_BGRA_5551,
	RGA_FORMAT_RGBA_4444,
	RGA_FORMAT_BGRA_4444,
	RGA_FORMAT_XRGB_8888,
	RGA_FORMAT_XBGR_8888,
	RGA_FORMAT_ARGB_8888,
	RGA_FORMAT_ARGB_5551,
	RGA_FORMAT_ARGB_4444,
	RGA_FORMAT_ABGR_8888,
	RGA_FORMAT_ABGR_5551,
	RGA_FORMAT_ABGR_4444,
};

const struct rga_win_data rga3_win_data[] = {
	{
		.name = "rga3-win0",
		.raster_formats = rga3_input_raster_format,
		.num_of_raster_formats = ARRAY_SIZE(rga3_input_raster_format),
		.fbc_formats = rga3_fbcd_format,
		.num_of_fbc_formats = ARRAY_SIZE(rga3_fbcd_format),
		.tile_formats = rga3_tile_format,
		.num_of_tile_formats = ARRAY_SIZE(rga3_tile_format),
		.supported_rotations = RGA_MODE_ROTATE_MASK,
		.scale_up_mode = RGA_SCALE_UP_BIC,
		.scale_down_mode = RGA_SCALE_DOWN_AVG,
		.rd_mode = RGA_RASTER_MODE | RGA_FBC_MODE | RGA_TILE_MODE,

	},

	{
		.name = "rga3-win1",
		.raster_formats = rga3_input_raster_format,
		.num_of_raster_formats = ARRAY_SIZE(rga3_input_raster_format),
		.fbc_formats = rga3_fbcd_format,
		.num_of_fbc_formats = ARRAY_SIZE(rga3_fbcd_format),
		.tile_formats = rga3_tile_format,
		.num_of_tile_formats = ARRAY_SIZE(rga3_tile_format),
		.supported_rotations = RGA_MODE_ROTATE_MASK,
		.scale_up_mode = RGA_SCALE_UP_BIC,
		.scale_down_mode = RGA_SCALE_DOWN_AVG,
		.rd_mode = RGA_RASTER_MODE | RGA_FBC_MODE | RGA_TILE_MODE,

	},

	{
		.name = "rga3-wr",
		.raster_formats = rga3_output_raster_format,
		.num_of_raster_formats = ARRAY_SIZE(rga3_output_raster_format),
		.fbc_formats = rga3_fbcd_format,
		.num_of_fbc_formats = ARRAY_SIZE(rga3_fbcd_format),
		.tile_formats = rga3_tile_format,
		.num_of_tile_formats = ARRAY_SIZE(rga3_tile_format),
		.supported_rotations = 0,
		.scale_up_mode = RGA_SCALE_UP_NONE,
		.scale_down_mode = RGA_SCALE_DOWN_NONE,
		.rd_mode = RGA_RASTER_MODE | RGA_FBC_MODE | RGA_TILE_MODE,

	},
};

const struct rga_win_data rga2e_win_data[] = {
	{
		.name = "rga2e-src0",
		.raster_formats = rga2e_input_raster_format,
		.num_of_raster_formats = ARRAY_SIZE(rga2e_input_raster_format),
		.supported_rotations = RGA_MODE_ROTATE_MASK,
		.scale_up_mode = RGA_SCALE_UP_BIC,
		.scale_down_mode = RGA_SCALE_DOWN_AVG,
		.rd_mode = RGA_RASTER_MODE,

	},

	{
		.name = "rga2e-src1",
		.raster_formats = rga2e_input_raster_format,
		.num_of_raster_formats = ARRAY_SIZE(rga2e_input_raster_format),
		.supported_rotations = RGA_MODE_ROTATE_MASK,
		.scale_up_mode = RGA_SCALE_UP_BIC,
		.scale_down_mode = RGA_SCALE_DOWN_AVG,
		.rd_mode = RGA_RASTER_MODE,

	},

	{
		.name = "rga2-dst",
		.raster_formats = rga2e_output_raster_format,
		.num_of_raster_formats = ARRAY_SIZE(rga2e_output_raster_format),
		.supported_rotations = 0,
		.scale_up_mode = RGA_SCALE_UP_NONE,
		.scale_down_mode = RGA_SCALE_DOWN_NONE,
		.rd_mode = RGA_RASTER_MODE,

	},
};

const struct rga_hw_data rga3_data = {
	.version = 0,
	.min_input = { 128, 128 },
	.min_output = { 128, 128 },
	.max_input = { 8176, 8176 },
	.max_output = { 8128, 8128 },

	.win = rga3_win_data,
	.win_size = ARRAY_SIZE(rga3_win_data),
	/* 1 << factor mean real factor */
	.max_upscale_factor = 3,
	.max_downscale_factor = 3,

	.feature = RGA_COLOR_KEY,
	.csc_r2y_mode = RGA_MODE_CSC_BT601L |
		RGA_MODE_CSC_BT601F | RGA_MODE_CSC_BT709 |
		RGA_MODE_CSC_BT2020,
	.csc_y2r_mode = RGA_MODE_CSC_BT601L |
		RGA_MODE_CSC_BT601F | RGA_MODE_CSC_BT709 |
		RGA_MODE_CSC_BT2020,
};

const struct rga_hw_data rga2e_data = {
	.version = 0,
	.min_input = { 0, 0 },
	.min_output = { 0, 0 },
	.max_input = { 8192, 8192 },
	.max_output = { 4096, 4096 },

	.win = rga2e_win_data,
	.win_size = ARRAY_SIZE(rga2e_win_data),
	/* 1 << factor mean real factor */
	.max_upscale_factor = 4,
	.max_downscale_factor = 4,

	.feature = RGA_COLOR_FILL | RGA_COLOR_PALETTE |
			RGA_COLOR_KEY | RGA_ROP_CALCULATE |
			RGA_NN_QUANTIZE | RGA_DITHER,
	.csc_r2y_mode = RGA_MODE_CSC_BT601L | RGA_MODE_CSC_BT601F |
					RGA_MODE_CSC_BT709,
	.csc_y2r_mode = RGA_MODE_CSC_BT601L | RGA_MODE_CSC_BT601F |
					RGA_MODE_CSC_BT709,
};
