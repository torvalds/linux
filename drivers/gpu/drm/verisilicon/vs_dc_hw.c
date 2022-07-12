// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/io.h>
#include <linux/bits.h>
#include <linux/media-bus-format.h>

#include <drm/vs_drm.h>

#include "vs_type.h"
#include "vs_dc_hw.h"
#include "vs_dc_dec.h"

static const u32 horKernel[] = {
	0x00000000, 0x20000000, 0x00002000, 0x00000000,
	0x00000000, 0x00000000, 0x23fd1c03, 0x00000000,
	0x00000000, 0x00000000, 0x181f0000, 0x000027e1,
	0x00000000, 0x00000000, 0x00000000, 0x2b981468,
	0x00000000, 0x00000000, 0x00000000, 0x10f00000,
	0x00002f10, 0x00000000, 0x00000000, 0x00000000,
	0x32390dc7, 0x00000000, 0x00000000, 0x00000000,
	0x0af50000, 0x0000350b, 0x00000000, 0x00000000,
	0x00000000, 0x3781087f, 0x00000000, 0x00000000,
	0x00000000, 0x06660000, 0x0000399a, 0x00000000,
	0x00000000, 0x00000000, 0x3b5904a7, 0x00000000,
	0x00000000, 0x00000000, 0x033c0000, 0x00003cc4,
	0x00000000, 0x00000000, 0x00000000, 0x3de1021f,
	0x00000000, 0x00000000, 0x00000000, 0x01470000,
	0x00003eb9, 0x00000000, 0x00000000, 0x00000000,
	0x3f5300ad, 0x00000000, 0x00000000, 0x00000000,
	0x00480000, 0x00003fb8, 0x00000000, 0x00000000,
	0x00000000, 0x3fef0011, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00004000, 0x00000000,
	0x00000000, 0x00000000, 0x20002000, 0x00000000,
	0x00000000, 0x00000000, 0x1c030000, 0x000023fd,
	0x00000000, 0x00000000, 0x00000000, 0x27e1181f,
	0x00000000, 0x00000000, 0x00000000, 0x14680000,
	0x00002b98, 0x00000000, 0x00000000, 0x00000000,
	0x2f1010f0, 0x00000000, 0x00000000, 0x00000000,
	0x0dc70000, 0x00003239, 0x00000000, 0x00000000,
	0x00000000, 0x350b0af5, 0x00000000, 0x00000000,
	0x00000000, 0x087f0000, 0x00003781, 0x00000000,
	0x00000000, 0x00000000, 0x399a0666, 0x00000000,
	0x00000000, 0x00000000, 0x04a70000, 0x00003b59,
	0x00000000, 0x00000000, 0x00000000, 0x3cc4033c,
	0x00000000, 0x00000000, 0x00000000, 0x021f0000,
};
#define H_COEF_SIZE (sizeof(horKernel) / sizeof(u32))

static const u32 verKernel[] = {
	0x00000000, 0x20000000, 0x00002000, 0x00000000,
	0x00000000, 0x00000000, 0x23fd1c03, 0x00000000,
	0x00000000, 0x00000000, 0x181f0000, 0x000027e1,
	0x00000000, 0x00000000, 0x00000000, 0x2b981468,
	0x00000000, 0x00000000, 0x00000000, 0x10f00000,
	0x00002f10, 0x00000000, 0x00000000, 0x00000000,
	0x32390dc7, 0x00000000, 0x00000000, 0x00000000,
	0x0af50000, 0x0000350b, 0x00000000, 0x00000000,
	0x00000000, 0x3781087f, 0x00000000, 0x00000000,
	0x00000000, 0x06660000, 0x0000399a, 0x00000000,
	0x00000000, 0x00000000, 0x3b5904a7, 0x00000000,
	0x00000000, 0x00000000, 0x033c0000, 0x00003cc4,
	0x00000000, 0x00000000, 0x00000000, 0x3de1021f,
	0x00000000, 0x00000000, 0x00000000, 0x01470000,
	0x00003eb9, 0x00000000, 0x00000000, 0x00000000,
	0x3f5300ad, 0x00000000, 0x00000000, 0x00000000,
	0x00480000, 0x00003fb8, 0x00000000, 0x00000000,
	0x00000000, 0x3fef0011, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00004000, 0x00000000,
	0xcdcd0000, 0xfdfdfdfd, 0xabababab, 0xabababab,
	0x00000000, 0x00000000, 0x5ff5f456, 0x000f5f58,
	0x02cc6c78, 0x02cc0c28, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
	0xfeeefeee, 0xfeeefeee, 0xfeeefeee, 0xfeeefeee,
};
#define V_COEF_SIZE (sizeof(verKernel) / sizeof(u32))

/*
 * RGB 709->2020 conversion parameters
 */
static u16 RGB2RGB[RGB_TO_RGB_TABLE_SIZE] = {
	10279,	5395,	709,
	1132,	15065,	187,
	269,	1442,	14674
};

/*
 * YUV601 to RGB conversion parameters
 * YUV2RGB[0]  - [8] : C0 - C8;
 * YUV2RGB[9]  - [11]: D0 - D2;
 * YUV2RGB[12] - [13]: Y clamp min & max calue;
 * YUV2RGB[14] - [15]: UV clamp min & max calue;
 */
static s32 YUV601_2RGB[YUV_TO_RGB_TABLE_SIZE] = {
	1196,	0,			1640,	1196,
	-404,	-836,		1196,	2076,
	0,		-916224,	558336,	-1202944,
	64,		 940,		64,		960
};

/*
 * YUV709 to RGB conversion parameters
 * YUV2RGB[0]  - [8] : C0 - C8;
 * YUV2RGB[9]  - [11]: D0 - D2;
 * YUV2RGB[12] - [13]: Y clamp min & max calue;
 * YUV2RGB[14] - [15]: UV clamp min & max calue;
 */
static s32 YUV709_2RGB[YUV_TO_RGB_TABLE_SIZE] = {
	1196,		0,		1844,	1196,
	-220,		-548,	1196,	2172,
	0,			-1020672, 316672,  -1188608,
	64,			940,		64,		960
};

/*
 * YUV2020 to RGB conversion parameters
 * YUV2RGB[0]  - [8] : C0 - C8;
 * YUV2RGB[9]  - [11]: D0 - D2;
 * YUV2RGB[12] - [13]: Y clamp min & max calue;
 * YUV2RGB[14] - [15]: UV clamp min & max calue;
 */
static s32 YUV2020_2RGB[YUV_TO_RGB_TABLE_SIZE] = {
	1196, 0, 1724, 1196,
	-192, -668, 1196, 2200,
	0, -959232, 363776, -1202944,
	64, 940, 64, 960
};

/*
 * RGB to YUV2020 conversion parameters
 * RGB2YUV[0] - [8] : C0 - C8;
 * RGB2YUV[9] - [11]: D0 - D2;
 */
static s16 RGB2YUV[RGB_TO_YUV_TABLE_SIZE] = {
	230,	594,	52,
	-125,  -323,	448,
	448,   -412,   -36,
	64,		512,	512
};

/*
 * Degamma table for 709 color space data.
 */
static u16 DEGAMMA_709[DEGAMMA_SIZE] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0004, 0x0005,
	0x0007, 0x000a, 0x000d, 0x0011, 0x0015, 0x0019, 0x001e, 0x0024,
	0x002a, 0x0030, 0x0038, 0x003f, 0x0048, 0x0051, 0x005a, 0x0064,
	0x006f, 0x007b, 0x0087, 0x0094, 0x00a1, 0x00af, 0x00be, 0x00ce,
	0x00de, 0x00ef, 0x0101, 0x0114, 0x0127, 0x013b, 0x0150, 0x0166,
	0x017c, 0x0193, 0x01ac, 0x01c4, 0x01de, 0x01f9, 0x0214, 0x0230,
	0x024d, 0x026b, 0x028a, 0x02aa, 0x02ca, 0x02ec, 0x030e, 0x0331,
	0x0355, 0x037a, 0x03a0, 0x03c7, 0x03ef, 0x0418, 0x0441, 0x046c,
	0x0498, 0x04c4, 0x04f2, 0x0520, 0x0550, 0x0581, 0x05b2, 0x05e5,
	0x0618, 0x064d, 0x0682, 0x06b9, 0x06f0, 0x0729, 0x0763, 0x079d,
	0x07d9, 0x0816, 0x0854, 0x0893, 0x08d3, 0x0914, 0x0956, 0x0999,
	0x09dd, 0x0a23, 0x0a69, 0x0ab1, 0x0afa, 0x0b44, 0x0b8f, 0x0bdb,
	0x0c28, 0x0c76, 0x0cc6, 0x0d17, 0x0d69, 0x0dbb, 0x0e10, 0x0e65,
	0x0ebb, 0x0f13, 0x0f6c, 0x0fc6, 0x1021, 0x107d, 0x10db, 0x113a,
	0x119a, 0x11fb, 0x125d, 0x12c1, 0x1325, 0x138c, 0x13f3, 0x145b,
	0x14c5, 0x1530, 0x159c, 0x160a, 0x1678, 0x16e8, 0x175a, 0x17cc,
	0x1840, 0x18b5, 0x192b, 0x19a3, 0x1a1c, 0x1a96, 0x1b11, 0x1b8e,
	0x1c0c, 0x1c8c, 0x1d0c, 0x1d8e, 0x1e12, 0x1e96, 0x1f1c, 0x1fa3,
	0x202c, 0x20b6, 0x2141, 0x21ce, 0x225c, 0x22eb, 0x237c, 0x240e,
	0x24a1, 0x2536, 0x25cc, 0x2664, 0x26fc, 0x2797, 0x2832, 0x28cf,
	0x296e, 0x2a0e, 0x2aaf, 0x2b51, 0x2bf5, 0x2c9b, 0x2d41, 0x2dea,
	0x2e93, 0x2f3e, 0x2feb, 0x3099, 0x3148, 0x31f9, 0x32ab, 0x335f,
	0x3414, 0x34ca, 0x3582, 0x363c, 0x36f7, 0x37b3, 0x3871, 0x3930,
	0x39f1, 0x3ab3, 0x3b77, 0x3c3c, 0x3d02, 0x3dcb, 0x3e94, 0x3f5f,
	0x402c, 0x40fa, 0x41ca, 0x429b, 0x436d, 0x4442, 0x4517, 0x45ee,
	0x46c7, 0x47a1, 0x487d, 0x495a, 0x4a39, 0x4b19, 0x4bfb, 0x4cde,
	0x4dc3, 0x4eaa, 0x4f92, 0x507c, 0x5167, 0x5253, 0x5342, 0x5431,
	0x5523, 0x5616, 0x570a, 0x5800, 0x58f8, 0x59f1, 0x5aec, 0x5be9,
	0x5ce7, 0x5de6, 0x5ee7, 0x5fea, 0x60ef, 0x61f5, 0x62fc, 0x6406,
	0x6510, 0x661d, 0x672b, 0x683b, 0x694c, 0x6a5f, 0x6b73, 0x6c8a,
	0x6da2, 0x6ebb, 0x6fd6, 0x70f3, 0x7211, 0x7331, 0x7453, 0x7576,
	0x769b, 0x77c2, 0x78ea, 0x7a14, 0x7b40, 0x7c6d, 0x7d9c, 0x7ecd,
	0x3f65, 0x3f8c, 0x3fb2, 0x3fd8
};

/*
 * Degamma table for 2020 color space data.
 */
static u16 DEGAMMA_2020[DEGAMMA_SIZE] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
	0x0001, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0003, 0x0003,
	0x0003, 0x0003, 0x0004, 0x0004, 0x0004, 0x0005, 0x0005, 0x0006,
	0x0006, 0x0006, 0x0007, 0x0007, 0x0008, 0x0008, 0x0009, 0x000a,
	0x000a, 0x000b, 0x000c, 0x000c, 0x000d, 0x000e, 0x000f, 0x000f,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0016, 0x0017, 0x0018,
	0x0019, 0x001b, 0x001c, 0x001e, 0x001f, 0x0021, 0x0022, 0x0024,
	0x0026, 0x0028, 0x002a, 0x002c, 0x002e, 0x0030, 0x0033, 0x0035,
	0x0038, 0x003a, 0x003d, 0x0040, 0x0043, 0x0046, 0x0049, 0x004d,
	0x0050, 0x0054, 0x0057, 0x005b, 0x005f, 0x0064, 0x0068, 0x006d,
	0x0071, 0x0076, 0x007c, 0x0081, 0x0086, 0x008c, 0x0092, 0x0098,
	0x009f, 0x00a5, 0x00ac, 0x00b4, 0x00bb, 0x00c3, 0x00cb, 0x00d3,
	0x00dc, 0x00e5, 0x00ee, 0x00f8, 0x0102, 0x010c, 0x0117, 0x0123,
	0x012e, 0x013a, 0x0147, 0x0154, 0x0161, 0x016f, 0x017e, 0x018d,
	0x019c, 0x01ac, 0x01bd, 0x01ce, 0x01e0, 0x01f3, 0x0206, 0x021a,
	0x022f, 0x0244, 0x025a, 0x0272, 0x0289, 0x02a2, 0x02bc, 0x02d6,
	0x02f2, 0x030f, 0x032c, 0x034b, 0x036b, 0x038b, 0x03ae, 0x03d1,
	0x03f5, 0x041b, 0x0443, 0x046b, 0x0495, 0x04c1, 0x04ee, 0x051d,
	0x054e, 0x0580, 0x05b4, 0x05ea, 0x0622, 0x065c, 0x0698, 0x06d6,
	0x0717, 0x075a, 0x079f, 0x07e7, 0x0831, 0x087e, 0x08cd, 0x0920,
	0x0976, 0x09ce, 0x0a2a, 0x0a89, 0x0aec, 0x0b52, 0x0bbc, 0x0c2a,
	0x0c9b, 0x0d11, 0x0d8b, 0x0e0a, 0x0e8d, 0x0f15, 0x0fa1, 0x1033,
	0x10ca, 0x1167, 0x120a, 0x12b2, 0x1360, 0x1415, 0x14d1, 0x1593,
	0x165d, 0x172e, 0x1806, 0x18e7, 0x19d0, 0x1ac1, 0x1bbb, 0x1cbf,
	0x1dcc, 0x1ee3, 0x2005, 0x2131, 0x2268, 0x23ab, 0x24fa, 0x2656,
	0x27be, 0x2934, 0x2ab8, 0x2c4a, 0x2dec, 0x2f9d, 0x315f, 0x3332,
	0x3516, 0x370d, 0x3916, 0x3b34, 0x3d66, 0x3fad, 0x420b, 0x4480,
	0x470d, 0x49b3, 0x4c73, 0x4f4e, 0x5246, 0x555a, 0x588e, 0x5be1,
	0x5f55, 0x62eb, 0x66a6, 0x6a86, 0x6e8c, 0x72bb, 0x7714, 0x7b99,
	0x3dcb, 0x3e60, 0x3ef5, 0x3f8c
};

/* one is for primary plane and the other is for all overlay planes */
static const struct dc_hw_plane_reg dc_plane_reg[] = {
	{
	.y_address		= DC_FRAMEBUFFER_ADDRESS,
	.u_address		= DC_FRAMEBUFFER_U_ADDRESS,
	.v_address		= DC_FRAMEBUFFER_V_ADDRESS,
	.y_stride		= DC_FRAMEBUFFER_STRIDE,
	.u_stride		= DC_FRAMEBUFFER_U_STRIDE,
	.v_stride		= DC_FRAMEBUFFER_V_STRIDE,
	.size			= DC_FRAMEBUFFER_SIZE,
	.top_left		= DC_FRAMEBUFFER_TOP_LEFT,
	.bottom_right	= DC_FRAMEBUFFER_BOTTOM_RIGHT,
	.scale_factor_x			= DC_FRAMEBUFFER_SCALE_FACTOR_X,
	.scale_factor_y			= DC_FRAMEBUFFER_SCALE_FACTOR_Y,
	.h_filter_coef_index	= DC_FRAMEBUFFER_H_FILTER_COEF_INDEX,
	.h_filter_coef_data		= DC_FRAMEBUFFER_H_FILTER_COEF_DATA,
	.v_filter_coef_index	= DC_FRAMEBUFFER_V_FILTER_COEF_INDEX,
	.v_filter_coef_data		= DC_FRAMEBUFFER_V_FILTER_COEF_DATA,
	.init_offset			= DC_FRAMEBUFFER_INIT_OFFSET,
	.color_key				= DC_FRAMEBUFFER_COLOR_KEY,
	.color_key_high			= DC_FRAMEBUFFER_COLOR_KEY_HIGH,
	.clear_value			= DC_FRAMEBUFFER_CLEAR_VALUE,
	.color_table_index		= DC_FRAMEBUFFER_COLOR_TABLE_INDEX,
	.color_table_data		= DC_FRAMEBUFFER_COLOR_TABLE_DATA,
	.scale_config			= DC_FRAMEBUFFER_SCALE_CONFIG,
	.water_mark				= DC_FRAMEBUFFER_WATER_MARK,
	.degamma_index			= DC_FRAMEBUFFER_DEGAMMA_INDEX,
	.degamma_data			= DC_FRAMEBUFFER_DEGAMMA_DATA,
	.degamma_ex_data		= DC_FRAMEBUFFER_DEGAMMA_EX_DATA,
	.src_global_color		= DC_FRAMEBUFFER_SRC_GLOBAL_COLOR,
	.dst_global_color		= DC_FRAMEBUFFER_DST_GLOBAL_COLOR,
	.blend_config			= DC_FRAMEBUFFER_BLEND_CONFIG,
	.roi_origin				= DC_FRAMEBUFFER_ROI_ORIGIN,
	.roi_size				= DC_FRAMEBUFFER_ROI_SIZE,
	.YUVToRGBCoef0			= DC_FRAMEBUFFER_YUVTORGB_COEF0,
	.YUVToRGBCoef1			= DC_FRAMEBUFFER_YUVTORGB_COEF1,
	.YUVToRGBCoef2			= DC_FRAMEBUFFER_YUVTORGB_COEF2,
	.YUVToRGBCoef3			= DC_FRAMEBUFFER_YUVTORGB_COEF3,
	.YUVToRGBCoef4			= DC_FRAMEBUFFER_YUVTORGB_COEF4,
	.YUVToRGBCoefD0			= DC_FRAMEBUFFER_YUVTORGB_COEFD0,
	.YUVToRGBCoefD1			= DC_FRAMEBUFFER_YUVTORGB_COEFD1,
	.YUVToRGBCoefD2			= DC_FRAMEBUFFER_YUVTORGB_COEFD2,
	.YClampBound			= DC_FRAMEBUFFER_Y_CLAMP_BOUND,
	.UVClampBound			= DC_FRAMEBUFFER_UV_CLAMP_BOUND,
	.RGBToRGBCoef0			= DC_FRAMEBUFFER_RGBTORGB_COEF0,
	.RGBToRGBCoef1			= DC_FRAMEBUFFER_RGBTORGB_COEF1,
	.RGBToRGBCoef2			= DC_FRAMEBUFFER_RGBTORGB_COEF2,
	.RGBToRGBCoef3			= DC_FRAMEBUFFER_RGBTORGB_COEF3,
	.RGBToRGBCoef4			= DC_FRAMEBUFFER_RGBTORGB_COEF4,
	},
	{
	.y_address		= DC_OVERLAY_ADDRESS,
	.u_address		= DC_OVERLAY_U_ADDRESS,
	.v_address		= DC_OVERLAY_V_ADDRESS,
	.y_stride		= DC_OVERLAY_STRIDE,
	.u_stride		= DC_OVERLAY_U_STRIDE,
	.v_stride		= DC_OVERLAY_V_STRIDE,
	.size			= DC_OVERLAY_SIZE,
	.top_left		= DC_OVERLAY_TOP_LEFT,
	.bottom_right	= DC_OVERLAY_BOTTOM_RIGHT,
	.scale_factor_x	= DC_OVERLAY_SCALE_FACTOR_X,
	.scale_factor_y	= DC_OVERLAY_SCALE_FACTOR_Y,
	.h_filter_coef_index = DC_OVERLAY_H_FILTER_COEF_INDEX,
	.h_filter_coef_data  = DC_OVERLAY_H_FILTER_COEF_DATA,
	.v_filter_coef_index = DC_OVERLAY_V_FILTER_COEF_INDEX,
	.v_filter_coef_data  = DC_OVERLAY_V_FILTER_COEF_DATA,
	.init_offset		 = DC_OVERLAY_INIT_OFFSET,
	.color_key			 = DC_OVERLAY_COLOR_KEY,
	.color_key_high			= DC_OVERLAY_COLOR_KEY_HIGH,
	.clear_value		 = DC_OVERLAY_CLEAR_VALUE,
	.color_table_index	 = DC_OVERLAY_COLOR_TABLE_INDEX,
	.color_table_data	 = DC_OVERLAY_COLOR_TABLE_DATA,
	.scale_config		 = DC_OVERLAY_SCALE_CONFIG,
	.water_mark				= DC_OVERLAY_WATER_MARK,
	.degamma_index		 = DC_OVERLAY_DEGAMMA_INDEX,
	.degamma_data		 = DC_OVERLAY_DEGAMMA_DATA,
	.degamma_ex_data	 = DC_OVERLAY_DEGAMMA_EX_DATA,
	.src_global_color	 = DC_OVERLAY_SRC_GLOBAL_COLOR,
	.dst_global_color	 = DC_OVERLAY_DST_GLOBAL_COLOR,
	.blend_config		 = DC_OVERLAY_BLEND_CONFIG,
	.roi_origin				= DC_OVERLAY_ROI_ORIGIN,
	.roi_size				= DC_OVERLAY_ROI_SIZE,
	.YUVToRGBCoef0		 = DC_OVERLAY_YUVTORGB_COEF0,
	.YUVToRGBCoef1		 = DC_OVERLAY_YUVTORGB_COEF1,
	.YUVToRGBCoef2		 = DC_OVERLAY_YUVTORGB_COEF2,
	.YUVToRGBCoef3		 = DC_OVERLAY_YUVTORGB_COEF3,
	.YUVToRGBCoef4			= DC_OVERLAY_YUVTORGB_COEF4,
	.YUVToRGBCoefD0			= DC_OVERLAY_YUVTORGB_COEFD0,
	.YUVToRGBCoefD1			= DC_OVERLAY_YUVTORGB_COEFD1,
	.YUVToRGBCoefD2			= DC_OVERLAY_YUVTORGB_COEFD2,
	.YClampBound		 = DC_OVERLAY_Y_CLAMP_BOUND,
	.UVClampBound		 = DC_OVERLAY_UV_CLAMP_BOUND,
	.RGBToRGBCoef0		 = DC_OVERLAY_RGBTORGB_COEF0,
	.RGBToRGBCoef1		 = DC_OVERLAY_RGBTORGB_COEF1,
	.RGBToRGBCoef2		 = DC_OVERLAY_RGBTORGB_COEF2,
	.RGBToRGBCoef3		 = DC_OVERLAY_RGBTORGB_COEF3,
	.RGBToRGBCoef4		 = DC_OVERLAY_RGBTORGB_COEF4,
	},
};

#ifdef CONFIG_VERISILICON_MMU
static const u32 mmu_reg_base = SE_MMU_REG_BASE;

static const struct dc_hw_mmu_reg dc_mmu_reg = {
	.mmu_config = SE_MMU_REG_CONFIG,
	.mmu_control = SE_MMU_REG_CONTROL,
	.table_array_size = SE_MMU_REG_TABLE_ARRAY_SIZE,
	.safe_non_secure = SE_MMU_REG_SAFE_NON_SECUR,
	.safe_secure = SE_MMU_REG_SAFE_SECURE,
	.safe_ex = SE_MMU_REG_SAFE_EXT_ADDRESS,
	.context_pd_entry = SE_MMU_REG_CONTEXT_PD,
};
#endif

static const u32 primary_overlay_format0[] = {
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_RGBA1010102,
	DRM_FORMAT_BGRA1010102,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
	DRM_FORMAT_P010,
};

static const u32 primary_overlay_format1[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_RGBA1010102,
	DRM_FORMAT_BGRA1010102,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_YUV444,
};

static const u32 cursor_formats[] = {
	DRM_FORMAT_ARGB8888
};

static const u64 format_modifier0[] = {
	DRM_FORMAT_MOD_LINEAR,
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_LINEAR),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_SUPER_TILED_XMAJOR),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_SUPER_TILED_YMAJOR),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_TILE_8X8),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_TILE_8X4),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_SUPER_TILED_XMAJOR_8X4),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_SUPER_TILED_YMAJOR_4X8),
#ifdef CONFIG_VERISILICON_DEC
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_TILE_8X8_XMAJOR,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_TILE_8X4,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_TILE_4X8,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_RASTER_256X1,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_RASTER_128X1,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_RASTER_64X1,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_TILE_16X8,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_RASTER_32X1,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_TILE_32X8,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
#endif
	DRM_FORMAT_MOD_INVALID
};

static const u64 format_modifier1[] = {
	DRM_FORMAT_MOD_LINEAR,
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_LINEAR),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_SUPER_TILED_XMAJOR),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_SUPER_TILED_YMAJOR),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_TILE_8X8),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_TILE_8X4),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_SUPER_TILED_XMAJOR_8X4),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_SUPER_TILED_YMAJOR_4X8),
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_TILE_MODE4X4),
	fourcc_mod_vs_custom_code(DRM_FORMAT_MOD_VS_TILE_MODE4X4),
	DRM_FORMAT_MOD_INVALID
};

static const u64 secondary_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
#ifdef CONFIG_VERISILICON_DEC
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_RASTER_256X1,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_RASTER_128X1,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_RASTER_64X1,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
	fourcc_mod_vs_dec_code(DRM_FORMAT_MOD_VS_DEC_RASTER_32X1,
					DRM_FORMAT_MOD_VS_DEC_ALIGN_32),
#endif
	DRM_FORMAT_MOD_INVALID
};

#define FRAC_16_16(mult, div)	 (((mult) << 16) / (div))

static const struct vs_plane_info dc_hw_planes[][PLANE_NUM] = {
	{
		/* DC_REV_0 */
		{
		.name			= "Primary",
		.id				= PRIMARY_PLANE_0,
		.type			= DRM_PLANE_TYPE_PRIMARY,
		.num_formats	= ARRAY_SIZE(primary_overlay_format0),
		.formats		= primary_overlay_format0,
		.num_modifiers	= ARRAY_SIZE(format_modifier0),
		.modifiers		= format_modifier0,
		.min_width		= 0,
		.min_height		= 0,
		.max_width		= 4096,
		.max_height		= 4096,
		.rotation		= DRM_MODE_ROTATE_0 |
							DRM_MODE_ROTATE_90 |
							DRM_MODE_ROTATE_180 |
							DRM_MODE_ROTATE_270 |
							DRM_MODE_REFLECT_X |
							DRM_MODE_REFLECT_Y,
		.blend_mode		= BIT(DRM_MODE_BLEND_PIXEL_NONE) |
							BIT(DRM_MODE_BLEND_PREMULTI) |
							BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 0,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name			= "Overlay",
		.id				= OVERLAY_PLANE_0,
		.type			= DRM_PLANE_TYPE_OVERLAY,
		.num_formats	= ARRAY_SIZE(primary_overlay_format0),
		.formats		= primary_overlay_format0,
		.num_modifiers	= ARRAY_SIZE(format_modifier0),
		.modifiers		= format_modifier0,
		.min_width		= 0,
		.min_height		= 0,
		.max_width		= 4096,
		.max_height		= 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 1,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name			= "Overlay_1",
		.id				= OVERLAY_PLANE_1,
		.type			= DRM_PLANE_TYPE_OVERLAY,
		.num_formats	= ARRAY_SIZE(primary_overlay_format0),
		.formats		= primary_overlay_format0,
		.num_modifiers	= ARRAY_SIZE(secondary_format_modifiers),
		.modifiers		= secondary_format_modifiers,
		.min_width		= 0,
		.min_height		= 0,
		.max_width		= 4096,
		.max_height		= 4096,
		.rotation		= 0,
		.blend_mode		= BIT(DRM_MODE_BLEND_PIXEL_NONE) |
							BIT(DRM_MODE_BLEND_PREMULTI) |
							BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 2,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Primary_1",
		.id				= PRIMARY_PLANE_1,
		.type		 = DRM_PLANE_TYPE_PRIMARY,
		.num_formats = ARRAY_SIZE(primary_overlay_format0),
		.formats	 = primary_overlay_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier0),
		.modifiers	 = format_modifier0,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 3,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Overlay_2",
		.id				= OVERLAY_PLANE_2,
		.type		 = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(primary_overlay_format0),
		.formats	 = primary_overlay_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier0),
		.modifiers	 = format_modifier0,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 4,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Overlay_3",
		.id			= OVERLAY_PLANE_3,
		.type		 = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(primary_overlay_format0),
		.formats	 = primary_overlay_format0,
		.num_modifiers = ARRAY_SIZE(secondary_format_modifiers),
		.modifiers	 = secondary_format_modifiers,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = 0,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 5,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Cursor",
		.id				= CURSOR_PLANE_0,
		.type		 = DRM_PLANE_TYPE_CURSOR,
		.num_formats = ARRAY_SIZE(cursor_formats),
		.formats	 = cursor_formats,
		.num_modifiers = 0,
		.modifiers	 = NULL,
		.min_width	 = 32,
		.min_height  = 32,
		.max_width	 = 64,
		.max_height  = 64,
		.rotation	 = 0,
		.degamma_size = 0,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 255,
		.watermark	 = false,
		.color_mgmt  = false,
		.roi		 = false,
		},
		{
		.name		 = "Cursor_1",
		.id				= CURSOR_PLANE_1,
		.type		 = DRM_PLANE_TYPE_CURSOR,
		.num_formats = ARRAY_SIZE(cursor_formats),
		.formats	 = cursor_formats,
		.num_modifiers = 0,
		.modifiers	 = NULL,
		.min_width	 = 32,
		.min_height  = 32,
		.max_width	 = 64,
		.max_height  = 64,
		.rotation	 = 0,
		.degamma_size = 0,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 255,
		.watermark	 = false,
		.color_mgmt  = false,
		.roi		 = false,
		},
	},
	{
		/* DC_REV_1 */
		{
		.name		 = "Primary",
		.id				= PRIMARY_PLANE_0,
		.type		 = DRM_PLANE_TYPE_PRIMARY,
		.num_formats = ARRAY_SIZE(primary_overlay_format0),
		.formats	 = primary_overlay_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier0),
		.modifiers	 = format_modifier0,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 0,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Overlay",
		.id				= OVERLAY_PLANE_0,
		.type		 = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(primary_overlay_format0),
		.formats	 = primary_overlay_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier0),
		.modifiers	 = format_modifier0,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 1,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Primary_1",
		.id				= PRIMARY_PLANE_1,
		.type		 = DRM_PLANE_TYPE_PRIMARY,
		.num_formats = ARRAY_SIZE(primary_overlay_format0),
		.formats	 = primary_overlay_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier0),
		.modifiers	 = format_modifier0,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 2,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Overlay_2",
		.id				= OVERLAY_PLANE_2,
		.type		 = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(primary_overlay_format0),
		.formats	 = primary_overlay_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier0),
		.modifiers	 = format_modifier0,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 3,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Cursor",
		.id				= CURSOR_PLANE_0,
		.type		 = DRM_PLANE_TYPE_CURSOR,
		.num_formats = ARRAY_SIZE(cursor_formats),
		.formats	 = cursor_formats,
		.num_modifiers = 0,
		.modifiers	 = NULL,
		.min_width	 = 32,
		.min_height  = 32,
		.max_width	 = 64,
		.max_height  = 64,
		.rotation	 = 0,
		.degamma_size = 0,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 255,
		.watermark	 = false,
		.color_mgmt  = false,
		.roi		 = false,
		},
		{
		.name		 = "Cursor_1",
		.id				= CURSOR_PLANE_1,
		.type		 = DRM_PLANE_TYPE_CURSOR,
		.num_formats = ARRAY_SIZE(cursor_formats),
		.formats	 = cursor_formats,
		.num_modifiers = 0,
		.modifiers	 = NULL,
		.min_width	 = 32,
		.min_height  = 32,
		.max_width	 = 64,
		.max_height  = 64,
		.rotation	 = 0,
		.degamma_size = 0,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 255,
		.watermark	 = false,
		.color_mgmt  = false,
		.roi		 = false,
		},
	},
	{
		/* DC_REV_2 */
		{
		.name		 = "Primary",
		.id				= PRIMARY_PLANE_0,
		.type		 = DRM_PLANE_TYPE_PRIMARY,
		.num_formats = ARRAY_SIZE(primary_overlay_format1),
		.formats	 = primary_overlay_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier1),
		.modifiers	 = format_modifier1,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 0,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Overlay",
		.id				= OVERLAY_PLANE_0,
		.type		 = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(primary_overlay_format1),
		.formats	 = primary_overlay_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier1),
		.modifiers	 = format_modifier1,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 1,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Overlay_1",
		.id				= OVERLAY_PLANE_1,
		.type		 = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(primary_overlay_format1),
		.formats	 = primary_overlay_format1,
		.num_modifiers = ARRAY_SIZE(secondary_format_modifiers),
		.modifiers	 = secondary_format_modifiers,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = 0,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 2,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Primary_1",
		.id				= PRIMARY_PLANE_1,
		.type		 = DRM_PLANE_TYPE_PRIMARY,
		.num_formats = ARRAY_SIZE(primary_overlay_format1),
		.formats	 = primary_overlay_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier1),
		.modifiers	 = format_modifier1,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 3,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Overlay_2",
		.id				= OVERLAY_PLANE_2,
		.type		 = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(primary_overlay_format1),
		.formats	 = primary_overlay_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier1),
		.modifiers	 = format_modifier1,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = DRM_MODE_ROTATE_0 |
					   DRM_MODE_ROTATE_90 |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X |
					   DRM_MODE_REFLECT_Y,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = FRAC_16_16(1, 3),
		.max_scale	 = FRAC_16_16(10, 1),
		.zpos		 = 4,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Overlay_3",
		.id				= OVERLAY_PLANE_3,
		.type		 = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(primary_overlay_format1),
		.formats	 = primary_overlay_format1,
		.num_modifiers = ARRAY_SIZE(secondary_format_modifiers),
		.modifiers	 = secondary_format_modifiers,
		.min_width	 = 0,
		.min_height  = 0,
		.max_width	 = 4096,
		.max_height  = 4096,
		.rotation	 = 0,
		.blend_mode  = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					   BIT(DRM_MODE_BLEND_PREMULTI) |
					   BIT(DRM_MODE_BLEND_COVERAGE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT709) |
						  BIT(DRM_COLOR_YCBCR_BT2020),
		.degamma_size	= DEGAMMA_SIZE,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 5,
		.watermark	 = true,
		.color_mgmt  = true,
		.roi		 = true,
		},
		{
		.name		 = "Cursor",
		.id				= CURSOR_PLANE_0,
		.type		 = DRM_PLANE_TYPE_CURSOR,
		.num_formats = ARRAY_SIZE(cursor_formats),
		.formats	 = cursor_formats,
		.num_modifiers = 0,
		.modifiers	 = NULL,
		.min_width	 = 32,
		.min_height  = 32,
		.max_width	 = 64,
		.max_height  = 64,
		.rotation	 = 0,
		.degamma_size = 0,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 255,
		.watermark	 = false,
		.color_mgmt  = false,
		.roi		 = false,
		},
		{
		.name		 = "Cursor_1",
		.id				= CURSOR_PLANE_1,
		.type		 = DRM_PLANE_TYPE_CURSOR,
		.num_formats = ARRAY_SIZE(cursor_formats),
		.formats	 = cursor_formats,
		.num_modifiers = 0,
		.modifiers	 = NULL,
		.min_width	 = 32,
		.min_height  = 32,
		.max_width	 = 64,
		.max_height  = 64,
		.rotation	 = 0,
		.degamma_size = 0,
		.min_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.max_scale	 = DRM_PLANE_HELPER_NO_SCALING,
		.zpos		 = 255,
		.watermark	 = false,
		.color_mgmt  = false,
		.roi		 = false,
		},
	},
};

static const struct vs_dc_info dc_info[] = {
	{
		/* DC_REV_0 */
		.name			= "DC8200",
		.panel_num		= 2,
		.plane_num		= 8,
		.planes			= dc_hw_planes[DC_REV_0],
		.layer_num		= 6,
		.max_bpc		= 10,
		.color_formats	= DRM_COLOR_FORMAT_RGB444 |
						  DRM_COLOR_FORMAT_YCRCB444 |
						  DRM_COLOR_FORMAT_YCRCB422 |
						  DRM_COLOR_FORMAT_YCRCB420,
		.gamma_size		= GAMMA_EX_SIZE,
		.gamma_bits		= 12,
		.pitch_alignment	= 128,
		.pipe_sync		= false,
		.mmu_prefetch	= false,
		.background		= true,
		.panel_sync		= true,
		.cap_dec		= true,
	},
	{
		/* DC_REV_1 */
		.name			= "DC8200",
		.panel_num		= 2,
		.plane_num		= 6,
		.planes			= dc_hw_planes[DC_REV_1],
		.layer_num		= 4,
		.max_bpc		= 10,
		.color_formats	= DRM_COLOR_FORMAT_RGB444 |
						  DRM_COLOR_FORMAT_YCRCB444 |
						  DRM_COLOR_FORMAT_YCRCB422 |
						  DRM_COLOR_FORMAT_YCRCB420,
		.gamma_size		= GAMMA_EX_SIZE,
		.gamma_bits		= 12,
		.pitch_alignment	= 128,
		.pipe_sync		= false,
		.mmu_prefetch	= false,
		.background		= true,
		.panel_sync		= true,
		.cap_dec		= true,
	},
	{
		/* DC_REV_2 */
		.name			= "DC8200",
		.panel_num		= 2,
		.plane_num		= 8,
		.planes			= dc_hw_planes[DC_REV_2],
		.layer_num		= 6,
		.max_bpc		= 10,
		.color_formats	= DRM_COLOR_FORMAT_RGB444 |
						  DRM_COLOR_FORMAT_YCRCB444 |
						  DRM_COLOR_FORMAT_YCRCB422 |
						  DRM_COLOR_FORMAT_YCRCB420,
		.gamma_size		= GAMMA_EX_SIZE,
		.gamma_bits		= 12,
		.pitch_alignment	= 128,
		.pipe_sync		= false,
		.mmu_prefetch	= false,
		.background		= true,
		.panel_sync		= true,
		.cap_dec		= false,
	},
};

static const struct dc_hw_funcs hw_func;

static inline u32 hi_read(struct dc_hw *hw, u32 reg)
{
	return readl(hw->hi_base + reg);
}

static inline void hi_write(struct dc_hw *hw, u32 reg, u32 value)
{
	writel(value, hw->hi_base + reg);
}

static inline void dc_write(struct dc_hw *hw, u32 reg, u32 value)
{
	writel(value, hw->reg_base + reg - DC_REG_BASE);
}

static inline u32 dc_read(struct dc_hw *hw, u32 reg)
{
	u32 value = readl(hw->reg_base + reg - DC_REG_BASE);

	return value;
}

static inline void dc_set_clear(struct dc_hw *hw, u32 reg, u32 set, u32 clear)
{
	u32 value = dc_read(hw, reg);

	value &= ~clear;
	value |= set;
	dc_write(hw, reg, value);
}

static void load_default_filter(struct dc_hw *hw,
				const struct dc_hw_plane_reg *reg, u32 offset)
{
	u8 i;

	dc_write(hw, reg->scale_config + offset, 0x33);
	dc_write(hw, reg->init_offset + offset, 0x80008000);
	dc_write(hw, reg->h_filter_coef_index + offset, 0x00);
	for (i = 0; i < H_COEF_SIZE; i++)
		dc_write(hw, reg->h_filter_coef_data + offset, horKernel[i]);

	dc_write(hw, reg->v_filter_coef_index + offset, 0x00);
	for (i = 0; i < V_COEF_SIZE; i++)
		dc_write(hw, reg->v_filter_coef_data + offset, verKernel[i]);
}

static void load_rgb_to_rgb(struct dc_hw *hw, const struct dc_hw_plane_reg *reg,
				u32 offset, u16 *table)
{
	dc_write(hw, reg->RGBToRGBCoef0 + offset, table[0] | (table[1] << 16));
	dc_write(hw, reg->RGBToRGBCoef1 + offset, table[2] | (table[3] << 16));
	dc_write(hw, reg->RGBToRGBCoef2 + offset, table[4] | (table[5] << 16));
	dc_write(hw, reg->RGBToRGBCoef3 + offset, table[6] | (table[7] << 16));
	dc_write(hw, reg->RGBToRGBCoef4 + offset, table[8]);
}

static void load_yuv_to_rgb(struct dc_hw *hw, const struct dc_hw_plane_reg *reg,
							u32 offset, s32 *table)
{
	dc_write(hw, reg->YUVToRGBCoef0 + offset,
			 (0xFFFF & table[0]) | (table[1] << 16));
	dc_write(hw, reg->YUVToRGBCoef1 + offset,
			 (0xFFFF & table[2]) | (table[3] << 16));
	dc_write(hw, reg->YUVToRGBCoef2 + offset,
			 (0xFFFF & table[4]) | (table[5] << 16));
	dc_write(hw, reg->YUVToRGBCoef3 + offset,
			 (0xFFFF & table[6]) | (table[7] << 16));
	dc_write(hw, reg->YUVToRGBCoef4 + offset, table[8]);
	dc_write(hw, reg->YUVToRGBCoefD0 + offset, table[9]);
	dc_write(hw, reg->YUVToRGBCoefD1 + offset, table[10]);
	dc_write(hw, reg->YUVToRGBCoefD2 + offset, table[11]);
	dc_write(hw, reg->YClampBound + offset, table[12] | (table[13] << 16));
	dc_write(hw, reg->UVClampBound + offset, table[14] | (table[15] << 16));
}

static void load_rgb_to_yuv(struct dc_hw *hw, u32 offset, s16 *table)
{
	dc_write(hw, DC_DISPLAY_RGBTOYUV_COEF0 + offset,
			 table[0] | (table[1] << 16));
	dc_write(hw, DC_DISPLAY_RGBTOYUV_COEF1 + offset,
			 table[2] | (table[3] << 16));
	dc_write(hw, DC_DISPLAY_RGBTOYUV_COEF2 + offset,
			 table[4] | (table[5] << 16));
	dc_write(hw, DC_DISPLAY_RGBTOYUV_COEF3 + offset,
			 table[6] | (table[7] << 16));
	dc_write(hw, DC_DISPLAY_RGBTOYUV_COEF4 + offset, table[8]);
	dc_write(hw, DC_DISPLAY_RGBTOYUV_COEFD0 + offset, table[9]);
	dc_write(hw, DC_DISPLAY_RGBTOYUV_COEFD1 + offset, table[10]);
	dc_write(hw, DC_DISPLAY_RGBTOYUV_COEFD2 + offset, table[11]);
}

static bool is_rgb(enum dc_hw_color_format format)
{
	switch (format) {
	case FORMAT_X4R4G4B4:
	case FORMAT_A4R4G4B4:
	case FORMAT_X1R5G5B5:
	case FORMAT_A1R5G5B5:
	case FORMAT_R5G6B5:
	case FORMAT_X8R8G8B8:
	case FORMAT_A8R8G8B8:
	case FORMAT_A2R10G10B10:
		return true;
	default:
		return false;
	}
}

static void load_degamma_table(struct dc_hw *hw,
				   const struct dc_hw_plane_reg *reg,
				   u32 offset, u16 *table)
{
	u16 i;
	u32 value;

	dc_write(hw, reg->degamma_index + offset, 0);

	for (i = 0; i < DEGAMMA_SIZE; i++) {
		value = table[i] | (table[i] << 16);
		dc_write(hw, reg->degamma_data + offset, value);
		dc_write(hw, reg->degamma_ex_data + offset, table[i]);
	}
}

static u32 get_addr_offset(u32 id)
{
	u32 offset = 0;

	switch (id) {
	case PRIMARY_PLANE_1:
	case OVERLAY_PLANE_1:
		offset = 0x04;
		break;
	case OVERLAY_PLANE_2:
		offset = 0x08;
		break;
	case OVERLAY_PLANE_3:
		offset = 0x0C;
		break;
	default:
		break;
	}

	return offset;
}

int dc_hw_init(struct dc_hw *hw)
{
	u8 i, id, panel_num, layer_num;
	u32 offset;
	u32 revision = hi_read(hw, DC_HW_REVISION);
	u32 cid = hi_read(hw, DC_HW_CHIP_CID);
	const struct dc_hw_plane_reg *reg;

	switch (revision) {
	case 0x5720:
		hw->rev = DC_REV_0;
		break;
	case 0x5721:
		switch (cid) {
		case 0x30B:
			hw->rev = DC_REV_1;
			break;
		case 0x310:
			hw->rev = DC_REV_2;
			break;
		default:
			hw->rev = DC_REV_0;
			break;
		}
		break;
	default:
		return -ENXIO;
	}

	hw->info = (struct vs_dc_info *)&dc_info[hw->rev];
	hw->func = (struct dc_hw_funcs *)&hw_func;

	layer_num = hw->info->layer_num;
	for (i = 0; i < layer_num; i++) {
		id = hw->info->planes[i].id;
		offset = get_addr_offset(id);
		if (id == PRIMARY_PLANE_0 || id == PRIMARY_PLANE_1)
			reg = &dc_plane_reg[0];
		else
			reg = &dc_plane_reg[1];

		load_default_filter(hw, reg, offset);
		load_rgb_to_rgb(hw, reg, offset, RGB2RGB);

	}

	panel_num = hw->info->panel_num;
	for (i = 0; i < panel_num; i++) {
		offset = i << 2;

		load_rgb_to_yuv(hw, offset, RGB2YUV);
		dc_write(hw, DC_DISPLAY_PANEL_CONFIG + offset, 0x111);

		offset = i ? DC_CURSOR_OFFSET : 0;
		dc_write(hw, DC_CURSOR_BACKGROUND + offset, 0x00FFFFFF);
		dc_write(hw, DC_CURSOR_FOREGROUND + offset, 0x00AAAAAA);

	}

	return 0;
}

void dc_hw_deinit(struct dc_hw *hw)
{
	/* Nothing to do */
}

void dc_hw_update_plane(struct dc_hw *hw, u8 id,
			struct dc_hw_fb *fb, struct dc_hw_scale *scale,
			struct dc_hw_position *pos, struct dc_hw_blend *blend)
{
	struct dc_hw_plane *plane = &hw->plane[id];

	if (plane) {
		if (fb) {
			if (fb->enable == false)
				plane->fb.enable = false;
			else
				memcpy(&plane->fb, fb,
					   sizeof(*fb) - sizeof(fb->dirty));
			plane->fb.dirty = true;
		}
		if (scale) {
			memcpy(&plane->scale, scale,
				   sizeof(*scale) - sizeof(scale->dirty));
			plane->scale.dirty = true;
		}
		if (pos) {
			memcpy(&plane->pos, pos,
				   sizeof(*pos) - sizeof(pos->dirty));
			plane->pos.dirty = true;
		}
		if (blend) {
			memcpy(&plane->blend, blend,
				   sizeof(*blend) - sizeof(blend->dirty));
			plane->blend.dirty = true;
		}
	}
}

void dc_hw_update_degamma(struct dc_hw *hw, u8 id, u32 mode)
{
	struct dc_hw_plane *plane = &hw->plane[id];

	if (plane) {
		if (hw->info->planes[id].degamma_size) {
			plane->degamma.mode = mode;
			plane->degamma.dirty = true;
		} else {
			plane->degamma.dirty = false;
		}
	}
}

void dc_hw_update_roi(struct dc_hw *hw, u8 id, struct dc_hw_roi *roi)
{
	struct dc_hw_plane *plane = &hw->plane[id];

	if (plane) {
		memcpy(&plane->roi, roi, sizeof(*roi) - sizeof(roi->dirty));
		plane->roi.dirty = true;
	}
}

void dc_hw_update_colorkey(struct dc_hw *hw, u8 id,
						   struct dc_hw_colorkey *colorkey)
{
	struct dc_hw_plane *plane = &hw->plane[id];

	if (plane) {
		memcpy(&plane->colorkey, colorkey,
			   sizeof(*colorkey) - sizeof(colorkey->dirty));
		plane->colorkey.dirty = true;
	}
}

void dc_hw_update_qos(struct dc_hw *hw, struct dc_hw_qos *qos)
{
	memcpy(&hw->qos, qos, sizeof(*qos) - sizeof(qos->dirty));
	hw->qos.dirty = true;
}

void dc_hw_update_cursor(struct dc_hw *hw, u8 id, struct dc_hw_cursor *cursor)
{
	memcpy(&hw->cursor[id], cursor, sizeof(*cursor) - sizeof(cursor->dirty));
	hw->cursor[id].dirty = true;
}

void dc_hw_update_gamma(struct dc_hw *hw, u8 id, u16 index,
						u16 r, u16 g, u16 b)
{
	if (index >= hw->info->gamma_size)
		return;

	hw->gamma[id].gamma[index][0] = r;
	hw->gamma[id].gamma[index][1] = g;
	hw->gamma[id].gamma[index][2] = b;
	hw->gamma[id].dirty = true;
}

void dc_hw_enable_gamma(struct dc_hw *hw, u8 id, bool enable)
{
	hw->gamma[id].enable = enable;
	hw->gamma[id].dirty = true;
}

void dc_hw_enable_dump(struct dc_hw *hw, u32 addr, u32 pitch)
{
	dc_write(hw, 0x14F0, addr);
	dc_write(hw, 0x14E8, addr);
	dc_write(hw, 0x1500, pitch);
	dc_write(hw, 0x14F8, 0x30000);
}

void dc_hw_disable_dump(struct dc_hw *hw)
{
	dc_write(hw, 0x14F8, 0x00);
}

void dc_hw_setup_display(struct dc_hw *hw, struct dc_hw_display *display)
{
	u8 id = display->id;

	memcpy(&hw->display[id], display, sizeof(*display));

	hw->func->display(hw, display);
}

void dc_hw_enable_interrupt(struct dc_hw *hw, bool enable)
{
	if (enable)
		hi_write(hw, AQ_INTR_ENBL, 0xFFFFFFFF);
	else
		hi_write(hw, AQ_INTR_ENBL, 0);
}

u32 dc_hw_get_interrupt(struct dc_hw *hw)
{
	return hi_read(hw, AQ_INTR_ACKNOWLEDGE);
}

bool dc_hw_check_underflow(struct dc_hw *hw)
{
	return dc_read(hw, DC_FRAMEBUFFER_CONFIG) & BIT(5);
}

void dc_hw_enable_shadow_register(struct dc_hw *hw, bool enable)
{
	u32 i, offset;
	u8 id, layer_num = hw->info->layer_num;
	u8 panel_num = hw->info->panel_num;

	for (i = 0; i < layer_num; i++) {
		id = hw->info->planes[i].id;
		offset = get_addr_offset(id);
		if (enable) {
			if (id == PRIMARY_PLANE_0 || id == PRIMARY_PLANE_1)
				dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG_EX + offset, BIT(12), 0);
			else
				dc_set_clear(hw, DC_OVERLAY_CONFIG + offset, BIT(31), 0);
		} else {
			if (id == PRIMARY_PLANE_0 || id == PRIMARY_PLANE_1)
				dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG_EX + offset, 0, BIT(12));
			else
				dc_set_clear(hw, DC_OVERLAY_CONFIG + offset, 0, BIT(31));
		}
	}

	for (i = 0; i < panel_num; i++) {
		offset = i << 2;
		if (enable)
			dc_set_clear(hw, DC_DISPLAY_PANEL_CONFIG_EX + offset, 0, BIT(0));
		else
			dc_set_clear(hw, DC_DISPLAY_PANEL_CONFIG_EX + offset, BIT(0), 0);
	}
}

void dc_hw_set_out(struct dc_hw *hw, enum dc_hw_out out, u8 id)
{
	if (out <= OUT_DP)
		hw->out[id] = out;
}

static void gamma_ex_commit(struct dc_hw *hw)
{
	u8 panel_num = hw->info->panel_num;
	u16 i, j;
	u32 value;

	for (j = 0; j < panel_num; j++) {
		if (hw->gamma[j].dirty) {
			if (hw->gamma[j].enable) {
				dc_write(hw, DC_DISPLAY_GAMMA_EX_INDEX + (j << 2), 0x00);
				for (i = 0; i < GAMMA_EX_SIZE; i++) {
					value = hw->gamma[j].gamma[i][2] |
						(hw->gamma[j].gamma[i][1] << 12);
					dc_write(hw, DC_DISPLAY_GAMMA_EX_DATA + (j << 2), value);
					dc_write(hw, DC_DISPLAY_GAMMA_EX_ONE_DATA + (j << 2),
						 hw->gamma[j].gamma[i][0]);
				}
				dc_set_clear(hw, DC_DISPLAY_PANEL_CONFIG + (j << 2),
							 BIT(13), 0);
			} else {
				dc_set_clear(hw, DC_DISPLAY_PANEL_CONFIG + (j << 2),
							 0, BIT(13));
			}
			hw->gamma[j].dirty = false;
		}
	}
}

static void plane_commit(struct dc_hw *hw)
{
	struct dc_hw_plane *plane;
	const struct dc_hw_plane_reg *reg;
	bool primary = false;
	u8 id, layer_num = hw->info->layer_num;
	u32 i, offset;

	for (i = 0; i < layer_num; i++) {
		plane = &hw->plane[i];
		id = hw->info->planes[i].id;
		offset = get_addr_offset(id);
		if (id == PRIMARY_PLANE_0 || id == PRIMARY_PLANE_1) {
			reg = &dc_plane_reg[0];
			primary = true;
		} else {
			reg = &dc_plane_reg[1];
			primary = false;
		}

		if (plane->fb.dirty) {
			if (plane->fb.enable) {
				dc_write(hw, reg->y_address + offset,
					 plane->fb.y_address);
				dc_write(hw, reg->u_address + offset,
					 plane->fb.u_address);
				dc_write(hw, reg->v_address + offset,
					 plane->fb.v_address);
				dc_write(hw, reg->y_stride + offset,
					 plane->fb.y_stride);
				dc_write(hw, reg->u_stride + offset,
					 plane->fb.u_stride);
				dc_write(hw, reg->v_stride + offset,
					 plane->fb.v_stride);
				dc_write(hw, reg->size + offset,
					 plane->fb.width |
					 (plane->fb.height << 15));
				dc_write(hw, reg->water_mark + offset,
					 plane->fb.water_mark);

				if (plane->fb.clear_enable)
					dc_write(hw, reg->clear_value + offset,
						plane->fb.clear_value);
			}

			if (primary) {
				dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG + offset,
						 (plane->fb.format << 26) |
						 (plane->fb.uv_swizzle << 25) |
						 (plane->fb.swizzle << 23) |
						 (plane->fb.tile_mode << 17) |
						 (plane->fb.yuv_color_space << 14) |
						 (plane->fb.rotation << 11) |
						 (plane->fb.clear_enable << 8),
						 (0x1F << 26) |
						 BIT(25) |
						 (0x03 << 23) |
						 (0x1F << 17) |
						 (0x07 << 14) |
						 (0x07 << 11) |
						 BIT(8));
				dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG_EX + offset,
					(plane->fb.dec_enable << 1) |
					(plane->fb.enable << 13) |
					(plane->fb.zpos << 16) |
					(plane->fb.display_id << 19),
					BIT(1) | BIT(13) | (0x07 << 16) | BIT(19));
			} else {
				dc_set_clear(hw, DC_OVERLAY_CONFIG + offset,
						 (plane->fb.dec_enable << 27) |
						 (plane->fb.clear_enable << 25) |
						 (plane->fb.enable << 24) |
						 (plane->fb.format << 16) |
						 (plane->fb.uv_swizzle << 15) |
						 (plane->fb.swizzle << 13) |
						 (plane->fb.tile_mode << 8) |
						 (plane->fb.yuv_color_space << 5) |
						 (plane->fb.rotation << 2),
						 BIT(27) |
						 BIT(25) |
						 BIT(24) |
						 (0x1F << 16) |
						 BIT(15) |
						 (0x03 << 13) |
						 (0x1F << 8) |
						 (0x07 << 5) |
						 (0x07 << 2));
				dc_set_clear(hw, DC_OVERLAY_CONFIG_EX + offset,
						 plane->fb.zpos | (plane->fb.display_id << 3),
						 0x07 | BIT(3));
			}
			plane->fb.dirty = false;
		}

		if (plane->scale.dirty) {
			if (plane->scale.enable) {
				dc_write(hw, reg->scale_factor_x + offset,
					plane->scale.scale_factor_x);
				dc_write(hw, reg->scale_factor_y + offset,
					plane->scale.scale_factor_y);
				if (primary)
					dc_set_clear(hw,
							 DC_FRAMEBUFFER_CONFIG + offset,
							 BIT(22), 0);
				else
					dc_set_clear(hw,
							 DC_OVERLAY_SCALE_CONFIG + offset,
							 BIT(8), 0);
			} else {
				if (primary)
					dc_set_clear(hw,
							 DC_FRAMEBUFFER_CONFIG + offset,
							 0, BIT(22));
				else
					dc_set_clear(hw,
							 DC_OVERLAY_SCALE_CONFIG + offset,
							 0, BIT(8));
			}
			plane->scale.dirty = false;
		}

		if (plane->pos.dirty) {
			dc_write(hw, reg->top_left + offset,
				plane->pos.start_x |
				(plane->pos.start_y << 15));
			dc_write(hw, reg->bottom_right + offset,
				plane->pos.end_x |
				(plane->pos.end_y << 15));
			plane->pos.dirty = false;
		}

		if (plane->blend.dirty) {
			dc_write(hw, reg->src_global_color + offset,
					 plane->blend.alpha << 24);
			dc_write(hw, reg->dst_global_color + offset,
					 plane->blend.alpha << 24);
			switch (plane->blend.blend_mode) {
			case BLEND_PREMULTI:
				dc_write(hw, reg->blend_config + offset, 0x3450);
				break;
			case BLEND_COVERAGE:
				dc_write(hw, reg->blend_config + offset, 0x3950);
				break;
			case BLEND_PIXEL_NONE:
				dc_write(hw, reg->blend_config + offset, 0x3548);
				break;
			default:
				break;
			}
			plane->blend.dirty = false;
		}

		if (plane->colorkey.dirty) {
			dc_write(hw, reg->color_key + offset, plane->colorkey.colorkey);
			dc_write(hw, reg->color_key_high + offset,
					 plane->colorkey.colorkey_high);

			if (primary)
				dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG + offset,
							 plane->colorkey.transparency << 9, 0x03 << 9);
			else
				dc_set_clear(hw, DC_OVERLAY_CONFIG + offset,
							 plane->colorkey.transparency, 0x03);

			plane->colorkey.dirty = false;
		}

		if (plane->roi.dirty) {
			if (plane->roi.enable) {
				dc_write(hw, reg->roi_origin + offset,
						 plane->roi.x | (plane->roi.y << 16));
				dc_write(hw, reg->roi_size + offset,
						 plane->roi.width | (plane->roi.height << 16));
				if (primary)
					dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG_EX + offset,
								 BIT(0), 0);
				else
					dc_set_clear(hw, DC_OVERLAY_CONFIG + offset,
								 BIT(22), 0);
			} else {
				if (primary)
					dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG_EX + offset,
								 0, BIT(0));
				else
					dc_set_clear(hw, DC_OVERLAY_CONFIG + offset,
								 0, BIT(22));
			}
			plane->roi.dirty = false;
		}
	}
}

static void plane_ex_commit(struct dc_hw *hw)
{
	struct dc_hw_plane *plane;
	const struct dc_hw_plane_reg *reg;
	bool primary = false;
	u8 id, layer_num = hw->info->layer_num;
	u32 i, offset;

	for (i = 0; i < layer_num; i++) {
		plane = &hw->plane[i];
		id = hw->info->planes[i].id;
		offset = get_addr_offset(id);
		if (id == PRIMARY_PLANE_0 || id == PRIMARY_PLANE_1) {
			reg = &dc_plane_reg[0];
			primary = true;
		} else {
			reg = &dc_plane_reg[1];
			primary = false;
		}

		if (plane->fb.dirty) {
			if (is_rgb(plane->fb.format)) {
				if (primary)
					dc_set_clear(hw,
							 DC_FRAMEBUFFER_CONFIG_EX + offset,
							 BIT(6), BIT(8));
				else
					dc_set_clear(hw,
							 DC_OVERLAY_CONFIG + offset,
							 BIT(29), BIT(30));
			} else {
				if (primary)
					dc_set_clear(hw,
							 DC_FRAMEBUFFER_CONFIG_EX + offset,
							 BIT(8), BIT(6));
				else
					dc_set_clear(hw,
							 DC_OVERLAY_CONFIG + offset,
							 BIT(30), BIT(29));
				switch (plane->fb.yuv_color_space) {
				case COLOR_SPACE_601:
					load_yuv_to_rgb(hw, reg, offset, YUV601_2RGB);
					break;
				case COLOR_SPACE_709:
					load_yuv_to_rgb(hw, reg, offset, YUV709_2RGB);
					break;
				case COLOR_SPACE_2020:
					load_yuv_to_rgb(hw, reg, offset, YUV2020_2RGB);
					break;
				default:
					break;
				}
			}
		}
		if (plane->degamma.dirty) {
			switch (plane->degamma.mode) {
			case VS_DEGAMMA_DISABLE:
				if (primary)
					dc_set_clear(hw,
						 DC_FRAMEBUFFER_CONFIG_EX + offset,
						 0, BIT(5));
				else
					dc_set_clear(hw,
						 DC_OVERLAY_CONFIG + offset,
						 0, BIT(28));
				break;
			case VS_DEGAMMA_BT709:
				load_degamma_table(hw, reg, offset, DEGAMMA_709);
				if (primary)
					dc_set_clear(hw,
						 DC_FRAMEBUFFER_CONFIG_EX + offset,
						 BIT(5), 0);
				else
					dc_set_clear(hw,
						 DC_OVERLAY_CONFIG + offset,
						 BIT(28), 0);
				break;
			case VS_DEGAMMA_BT2020:
				load_degamma_table(hw, reg, offset, DEGAMMA_2020);
				if (primary)
					dc_set_clear(hw,
						 DC_FRAMEBUFFER_CONFIG_EX + offset,
						 BIT(5), 0);
				else
					dc_set_clear(hw,
						 DC_OVERLAY_CONFIG + offset,
						 BIT(28), 0);
				break;
			default:
				break;
			}
			plane->degamma.dirty = false;
		}
	}
	plane_commit(hw);
}

static void setup_display(struct dc_hw *hw, struct dc_hw_display *display)
{
	u8 id = display->id;
	u32 dpi_cfg, offset = id << 2;

	if (hw->display[id].enable) {
		switch (display->bus_format) {
		case MEDIA_BUS_FMT_RGB565_1X16:
			dpi_cfg = 0;
			break;
		case MEDIA_BUS_FMT_RGB666_1X18:
			dpi_cfg = 3;
			break;
		case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
			dpi_cfg = 4;
			break;
		case MEDIA_BUS_FMT_RGB888_1X24:
			dpi_cfg = 5;
			break;
		case MEDIA_BUS_FMT_RGB101010_1X30:
			dpi_cfg = 6;
			break;
		default:
			dpi_cfg = 5;
			break;
		}
		dc_write(hw, DC_DISPLAY_DPI_CONFIG + offset, dpi_cfg);

		if (id == 0)
			dc_set_clear(hw, DC_DISPLAY_PANEL_START, 0, BIT(0) | BIT(2));
		else
			dc_set_clear(hw, DC_DISPLAY_PANEL_START, 0, BIT(1) | BIT(2));

#ifdef CONFIG_STARFIVE_DSI
		dc_write(hw, DC_DISPLAY_H + offset, hw->display[id].h_active |
				(hw->display[id].h_total << 16));

		dc_write(hw, DC_DISPLAY_H_SYNC + offset,
				hw->display[id].h_sync_start |
				(hw->display[id].h_sync_end << 15) |
				BIT(31) |
				BIT(30));

		dc_write(hw, DC_DISPLAY_V + offset, hw->display[id].v_active |
				(hw->display[id].v_total << 16));

		dc_write(hw, DC_DISPLAY_V_SYNC + offset,
				hw->display[id].v_sync_start |
				(hw->display[id].v_sync_end << 15) |
				(hw->display[id].v_sync_polarity ? 0 : BIT(31)) |
				BIT(30));

#else
		dc_write(hw, DC_DISPLAY_H + offset, hw->display[id].h_active |
				(hw->display[id].h_total << 16));
		dc_write(hw, DC_DISPLAY_H_SYNC + offset,
				hw->display[id].h_sync_start |
				(hw->display[id].h_sync_end << 15) |
				(hw->display[id].h_sync_polarity ? 0 : BIT(31)) |
				BIT(30));
		dc_write(hw, DC_DISPLAY_V + offset, hw->display[id].v_active |
				(hw->display[id].v_total << 16));
		dc_write(hw, DC_DISPLAY_V_SYNC + offset,
				hw->display[id].v_sync_start |
				(hw->display[id].v_sync_end << 15) |
				(hw->display[id].v_sync_polarity ? 0 : BIT(31)) |
				BIT(30));
#endif

		if (hw->info->pipe_sync) {
			switch (display->sync_mode) {
			case VS_SINGLE_DC:
				dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG_EX,
						 0, BIT(3) | BIT(4));
				break;
			case VS_MULTI_DC_PRIMARY:
				dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG_EX,
						 BIT(3) | BIT(4), 0);
				break;
			case VS_MULTI_DC_SECONDARY:
				dc_set_clear(hw, DC_FRAMEBUFFER_CONFIG_EX,
						 BIT(3), BIT(4));
				break;
			default:
				break;
			}
		}

		if (hw->info->background)
			dc_write(hw, DC_FRAMEBUFFER_BG_COLOR + offset,
					 hw->display[id].bg_color);

		if (hw->display[id].dither_enable) {
			dc_write(hw, DC_DISPLAY_DITHER_TABLE_LOW + offset,
					 DC_DISPLAY_DITHERTABLE_LOW);
			dc_write(hw, DC_DISPLAY_DITHER_TABLE_HIGH + offset,
					 DC_DISPLAY_DITHERTABLE_HIGH);
			dc_write(hw, DC_DISPLAY_DITHER_CONFIG + offset, BIT(31));
		} else {
			dc_write(hw, DC_DISPLAY_DITHER_CONFIG + offset, 0);
		}

		dc_set_clear(hw, DC_DISPLAY_PANEL_CONFIG + offset, BIT(12), 0);
		if (hw->display[id].sync_enable)
			dc_set_clear(hw, DC_DISPLAY_PANEL_START, BIT(2) | BIT(3), 0);
		else if (id == 0)
			dc_set_clear(hw, DC_DISPLAY_PANEL_START, BIT(0), BIT(3));
		else
			dc_set_clear(hw, DC_DISPLAY_PANEL_START, BIT(1), BIT(3));
	} else {
		dc_set_clear(hw, DC_DISPLAY_PANEL_CONFIG + offset, 0, BIT(12));
		if (id == 0)
			dc_set_clear(hw, DC_DISPLAY_PANEL_START, 0, BIT(0) | BIT(2));
		else
			dc_set_clear(hw, DC_DISPLAY_PANEL_START, 0, BIT(1) | BIT(2));

		dc_set_clear(hw, DC_OVERLAY_CONFIG + 0x0, 0x0, BIT(24));
		dc_set_clear(hw, DC_OVERLAY_CONFIG + 0x4, 0x0, BIT(24));
		dc_set_clear(hw, DC_OVERLAY_CONFIG + 0x8, 0x0, BIT(24));
		dc_set_clear(hw, DC_OVERLAY_CONFIG + 0xc, 0x0, BIT(24));

		dc_set_clear(hw, DC_CURSOR_CONFIG + 0x0, BIT(3), 0x03);
		dc_set_clear(hw, DC_CURSOR_CONFIG + DC_CURSOR_OFFSET, BIT(3), 0x03);
	}
}

static void setup_display_ex(struct dc_hw *hw, struct dc_hw_display *display)
{
	u8 id = display->id;
	u32 dp_cfg, offset = id << 2;
	bool is_yuv = false;

	if (hw->display[id].enable && hw->out[id] == OUT_DP) {
		switch (display->bus_format) {
		case MEDIA_BUS_FMT_RGB565_1X16:
			dp_cfg = 0;
			break;
		case MEDIA_BUS_FMT_RGB666_1X18:
			dp_cfg = 1;
			break;
		case MEDIA_BUS_FMT_RGB888_1X24:
			dp_cfg = 2;
			break;
		case MEDIA_BUS_FMT_RGB101010_1X30:
			dp_cfg = 3;
			break;
		case MEDIA_BUS_FMT_UYVY8_1X16:
			dp_cfg = 2 << 4;
			is_yuv = true;
			break;
		case MEDIA_BUS_FMT_YUV8_1X24:
			dp_cfg = 4 << 4;
			is_yuv = true;
			break;
		case MEDIA_BUS_FMT_UYVY10_1X20:
			dp_cfg = 8 << 4;
			is_yuv = true;
			break;
		case MEDIA_BUS_FMT_YUV10_1X30:
			dp_cfg = 10 << 4;
			is_yuv = true;
			break;
		case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
			dp_cfg = 12 << 4;
			is_yuv = true;
			break;
		case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
			dp_cfg = 13 << 4;
			is_yuv = true;
			break;
		default:
			dp_cfg = 2;
			break;
		}
		if (is_yuv)
			dc_set_clear(hw, DC_DISPLAY_PANEL_CONFIG + offset, BIT(16), 0);
		else
			dc_set_clear(hw, DC_DISPLAY_PANEL_CONFIG + offset, 0, BIT(16));
		dc_write(hw, DC_DISPLAY_DP_CONFIG + offset, dp_cfg | BIT(3));
	}

	if (hw->out[id] == OUT_DPI)
		dc_set_clear(hw, DC_DISPLAY_DP_CONFIG + offset, 0, BIT(3));

	setup_display(hw, display);
}

static const struct dc_hw_funcs hw_func = {
	.gamma = &gamma_ex_commit,
	.plane = &plane_ex_commit,
	.display = setup_display_ex,
};

void dc_hw_commit(struct dc_hw *hw)
{
	u32 i, offset = 0;
	u8 plane_num = hw->info->plane_num;
	u8 layer_num = hw->info->layer_num;
	u8 cursor_num = plane_num - layer_num;

	hw->func->gamma(hw);
	hw->func->plane(hw);

	for (i = 0; i < cursor_num; i++) {
		if (hw->cursor[i].dirty) {
			offset = hw->cursor[i].display_id ? DC_CURSOR_OFFSET : 0;
			if (hw->cursor[i].enable) {
				dc_write(hw, DC_CURSOR_ADDRESS + offset,
						 hw->cursor[i].address);
				dc_write(hw, DC_CURSOR_LOCATION + offset, hw->cursor[i].x |
					 (hw->cursor[i].y << 16));
				dc_set_clear(hw, DC_CURSOR_CONFIG + offset,
						 (hw->cursor[i].hot_x << 16) |
						 (hw->cursor[i].hot_y << 8) |
						 (hw->cursor[i].size << 5) |
						 BIT(3) | BIT(2) | 0x02,
						 (0xFF << 16) |
						 (0xFF << 8) |
						 (0x07 << 5) | 0x1F);
			} else {
				dc_set_clear(hw, DC_CURSOR_CONFIG + offset, BIT(3), 0x03);
			}
			hw->cursor[i].dirty = false;
		}
	}

	if (hw->qos.dirty) {
		dc_set_clear(hw, DC_QOS_CONFIG, (hw->qos.high_value << 4) |
					 hw->qos.low_value, 0xFF);
		hw->qos.dirty = false;
	}
}

#ifdef CONFIG_VERISILICON_DEC
void dc_hw_dec_init(struct dc_hw *hw)
{
	u32 config = 0;

	config = DEC_CONTROL_RESET & (~COMPRESSION_DISABLE);
	dc_write(hw, DEC_CONTROL, config | FLUSH_ENABLE);

	config = DEC_CONTROL_EX2_RESET &
			 (~TILE_STATUS_READ_ID_MASK) &
			 (~TILE_STATUS_READ_ID_H_MASK) &
			 (~DISABLE_HW_DEC_FLUSH);
	dc_write(hw, DEC_CONTROL_EX2,
			 config | (TILE_STATUS_READ_ID_H << 22) |
			 TILE_STATUS_READ_ID);

	config = DEC_CONTROL_EX_RESET &
			 (~WRITE_MISS_POLICY_MASK) &
			 (~READ_MISS_POLICY_MASK);
	dc_write(hw, DEC_CONTROL_EX, config | (WRITE_MISS_POLICY1 << 19));
}

void dc_hw_dec_stream_set(struct dc_hw *hw, u32	main_base_addr,
						  u32 ts_base_addr, u8 tile_mode, u8 align_mode,
						  u8 format, u8 depth, u8 stream_id)
{
	u32 offset = stream_id << 2;

	dc_set_clear(hw, DEC_READ_CONFIG + offset,
				 (tile_mode << 25) |
				 (align_mode << 16) |
				 (format << 3) |
				 COMPRESSION_EN,
				 TILE_MODE_MASK |
				 COMPRESSION_ALIGN_MODE_MASK |
				 COMPRESSION_FORMAT_MASK);

	dc_set_clear(hw, DEC_READ_EX_CONFIG + offset,
				 (depth << 16), BIT_DEPTH_MASK);

	dc_write(hw, DEC_READ_BUFFER_BASE + offset, main_base_addr);
	dc_write(hw, DEC_READ_BUFFER_END + offset, ts_base_addr - 128);
	dc_write(hw, DEC_READ_CACHE_BASE + offset, ts_base_addr);
}

void dc_hw_dec_stream_disable(struct dc_hw *hw, u8 stream_id)
{
	u32 offset = stream_id << 2;

	dc_write(hw, DEC_READ_CONFIG + offset, DEC_READ_CONFIG_RESET);
	dc_write(hw, DEC_READ_BUFFER_BASE + offset, 0xFFFFFFFF);
	dc_write(hw, DEC_READ_BUFFER_END + offset, 0xFFFFFFFF);
}
#endif

#ifdef CONFIG_VERISILICON_MMU
static u32 mmu_read(struct dc_hw *hw, u32 reg)
{
	return readl(hw->mmu_base + reg - mmu_reg_base);
}

static void mmu_write(struct dc_hw *hw, u32 reg, u32 value)
{
	writel(value, hw->mmu_base + reg - mmu_reg_base);
}

static void mmu_set_clear(struct dc_hw *hw, u32 reg, u32 set, u32 clear)
{
	u32 value = mmu_read(hw, reg);

	value &= ~clear;
	value |= set;
	mmu_write(hw, reg, value);
}

int dc_hw_mmu_init(struct dc_hw *hw, dc_mmu_pt mmu)
{
	const struct dc_hw_mmu_reg *reg;
	u32 mtlb = 0, ext_mtlb = 0;
	u32 safe_addr = 0, ext_safe_addr = 0;
	u32 config = 0;

	reg = &dc_mmu_reg;

	mtlb = (u32)(mmu->mtlb_physical & 0xFFFFFFFF);
	ext_mtlb = (u32)(mmu->mtlb_physical >> 32);

	/* more than 40bit physical address */
	if (ext_mtlb & 0xFFFFFF00) {
		pr_err("Mtlb address out of range.\n");
		return -EFAULT;
	}

	config = (ext_mtlb << 20) | (mtlb >> 12);
	if (mmu->mode == MMU_MODE_1K)
		mmu_set_clear(hw, reg->context_pd_entry,
				  (config << 4) | BIT(0),
				  (0xFFFFFFF << 4) | (0x07));
	else
		mmu_set_clear(hw, reg->context_pd_entry,
				  (config << 4),
				  (0xFFFFFFF << 4) | (0x07));

	safe_addr = (u32)(mmu->safe_page_physical & 0xFFFFFFFF);
	ext_safe_addr = (u32)(mmu->safe_page_physical >> 32);

	if ((safe_addr & 0x3F) || (ext_safe_addr & 0xFFFFFF00)) {
		pr_err("Invalid safe_address.\n");
		return -EFAULT;
	}

	mmu_write(hw, reg->table_array_size, 1);
	mmu_write(hw, reg->safe_secure, safe_addr);
	mmu_write(hw, reg->safe_non_secure, safe_addr);

	mmu_set_clear(hw, reg->safe_ex,
			  (ext_safe_addr << 16) | ext_safe_addr,
			  BIT(31) | (0xFF << 16) | BIT(15) | 0xFF);

	/* mmu configuration for ree driver */
	mmu_write(hw, reg->mmu_control, BIT(5) | BIT(0));

	mmu_write(hw, SE_MMU_REG_INTR_ENBL, 0xFFFFFFFF);

	return 0;
}

void dc_hw_enable_mmu_prefetch(struct dc_hw *hw, bool enable)
{
	if (!hw->info->mmu_prefetch)
		return;

	if (enable)
		dc_write(hw, DC_MMU_PREFETCH, BIT(0));
	else
		dc_write(hw, DC_MMU_PREFETCH, 0);
}

void dc_hw_mmu_flush(struct dc_hw *hw)
{
	const struct dc_hw_mmu_reg *reg = &dc_mmu_reg;
	u32 value = mmu_read(hw, reg->mmu_config);

	mmu_write(hw, reg->mmu_config, value | BIT(4));
	mmu_write(hw, reg->mmu_config, value);
}
#endif
