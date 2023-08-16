// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/component.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <dt-bindings/display/rockchip_vop.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include "rockchip_drm_vop.h"
#include "rockchip_vop_reg.h"
#include "rockchip_drm_drv.h"

#define _VOP_REG(off, _mask, _shift, _write_mask) \
		{ \
		 .offset = off, \
		 .mask = _mask, \
		 .shift = _shift, \
		 .write_mask = _write_mask, \
		}

#define VOP_REG(off, _mask, _shift) \
		_VOP_REG(off, _mask, _shift, false)

#define VOP_REG_MASK(off, _mask, s) \
		_VOP_REG(off, _mask, s, true)

static const uint32_t formats_for_cluster[] = {
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_YUV420_8BIT, /* yuv420_8bit non-Linear mode only */
	DRM_FORMAT_YUV420_10BIT, /* yuv420_10bit non-Linear mode only */
	DRM_FORMAT_YUYV, /* yuv422_8bit non-Linear mode only*/
	DRM_FORMAT_Y210, /* yuv422_10bit non-Linear mode only */
};

static const uint32_t formats_for_vop3_cluster[] = {
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12, /* yuv420_8bit linear mode, 2 plane */
	DRM_FORMAT_NV21, /* yvu420_8bit linear mode, 2 plane */
	DRM_FORMAT_NV16, /* yuv422_8bit linear mode, 2 plane */
	DRM_FORMAT_NV61, /* yvu422_8bit linear mode, 2 plane */
	DRM_FORMAT_NV24, /* yuv444_8bit linear mode, 2 plane */
	DRM_FORMAT_NV42, /* yvu444_8bit linear mode, 2 plane */
	DRM_FORMAT_NV15, /* yuv420_10bit linear mode, 2 plane, no padding */
#ifdef CONFIG_NO_GKI
	DRM_FORMAT_NV20, /* yuv422_10bit linear mode, 2 plane, no padding */
	DRM_FORMAT_NV30, /* yuv444_10bit linear mode, 2 plane, no padding */
#endif
	DRM_FORMAT_YUV420_8BIT, /* yuv420_8bit non-Linear mode only */
	DRM_FORMAT_YUV420_10BIT, /* yuv420_10bit non-Linear mode only */
	DRM_FORMAT_YUYV, /* yuv422_8bit non-Linear mode only*/
	DRM_FORMAT_Y210, /* yuv422_10bit non-Linear mode only */
};

static const uint32_t formats_for_esmart[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12, /* yuv420_8bit linear mode, 2 plane */
	DRM_FORMAT_NV21, /* yvu420_8bit linear mode, 2 plane */
	DRM_FORMAT_NV16, /* yuv422_8bit linear mode, 2 plane */
	DRM_FORMAT_NV61, /* yvu422_8bit linear mode, 2 plane */
	DRM_FORMAT_NV24, /* yuv444_8bit linear mode, 2 plane */
	DRM_FORMAT_NV42, /* yvu444_8bit linear mode, 2 plane */
	DRM_FORMAT_NV15, /* yuv420_10bit linear mode, 2 plane, no padding */
#ifdef CONFIG_NO_GKI
	DRM_FORMAT_NV20, /* yuv422_10bit linear mode, 2 plane, no padding */
	DRM_FORMAT_NV30, /* yuv444_10bit linear mode, 2 plane, no padding */
#endif
	DRM_FORMAT_YVYU, /* yuv422_8bit[YVYU] linear mode */
	DRM_FORMAT_VYUY, /* yuv422_8bit[VYUY] linear mode */
	DRM_FORMAT_YUYV, /* yuv422_8bit[YUYV] linear mode */
	DRM_FORMAT_UYVY, /* yuv422_8bit[UYVY] linear mode */
};

/* RK356x can't support uv swap for YUYV and UYVY */
static const uint32_t formats_for_rk356x_esmart[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12, /* yuv420_8bit linear mode, 2 plane */
	DRM_FORMAT_NV16, /* yuv422_8bit linear mode, 2 plane */
	DRM_FORMAT_NV24, /* yuv444_8bit linear mode, 2 plane */
	DRM_FORMAT_NV15, /* yuv420_10bit linear mode, 2 plane, no padding */
#ifdef CONFIG_NO_GKI
	DRM_FORMAT_NV20, /* yuv422_10bit linear mode, 2 plane, no padding */
	DRM_FORMAT_NV30, /* yuv444_10bit linear mode, 2 plane, no padding */
#endif
	DRM_FORMAT_YUYV, /* yuv422_8bit[YUYV] linear mode */
	DRM_FORMAT_UYVY, /* yuv422_8bit[UYVY] linear mode */
};

static const uint32_t formats_for_smart[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

static const u32 formats_wb[] = {
	DRM_FORMAT_BGR888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_NV12,
};

static const uint64_t format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_afbc[] = {
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	/* SPLIT mandates SPARSE, RGB modes mandates YTR */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),

	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_afbc_no_linear_mode[] = {
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	/* SPLIT mandates SPARSE, RGB modes mandates YTR */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_afbc_tiled[] = {
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	/* SPLIT mandates SPARSE, RGB modes mandates YTR */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),

	DRM_FORMAT_MOD_ROCKCHIP_TILED(ROCKCHIP_TILED_BLOCK_SIZE_8x8),
	DRM_FORMAT_MOD_ROCKCHIP_TILED(ROCKCHIP_TILED_BLOCK_SIZE_4x4_MODE0),
	DRM_FORMAT_MOD_ROCKCHIP_TILED(ROCKCHIP_TILED_BLOCK_SIZE_4x4_MODE1),

	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const u32 sdr2hdr_bt1886eotf_yn_for_hlg_hdr[65] = {
	0,
	1,	7,	17,	35,
	60,	92,	134,	184,
	244,	315,	396,	487,
	591,	706,	833,	915,
	1129,	1392,	1717,	2118,
	2352,	2612,	2900,	3221,
	3577,	3972,	4411,	4899,
	5441,	6042,	6710,	7452,
	7853,	8276,	8721,	9191,
	9685,	10207,	10756,	11335,
	11945,	12588,	13266,	13980,
	14732,	15525,	16361,	17241,
	17699,	18169,	18652,	19147,
	19656,	20178,	20714,	21264,
	21829,	22408,	23004,	23615,
	24242,	24886,	25547,	26214,
};

static const u32 sdr2hdr_bt1886eotf_yn_for_bt2020[65] = {
	0,
	1820,   3640,   5498,   7674,
	10256,  13253,  16678,  20539,
	24847,  29609,  34833,  40527,
	46699,  53354,  60499,  68141,
	76285,  84937,  94103,  103787,
	108825, 113995, 119296, 124731,
	130299, 136001, 141837, 147808,
	153915, 160158, 166538, 173055,
	176365, 179709, 183089, 186502,
	189951, 193434, 196952, 200505,
	204093, 207715, 211373, 215066,
	218795, 222558, 226357, 230191,
	232121, 234060, 236008, 237965,
	239931, 241906, 243889, 245882,
	247883, 249894, 251913, 253941,
	255978, 258024, 260079, 262143,
};

static u32 sdr2hdr_bt1886eotf_yn_for_hdr[65] = {
	/* dst_range 425int */
	0,
	5,     21,    49,     91,
	150,   225,   320,   434,
	569,   726,   905,   1108,
	1336,  1588,  1866,  2171,
	2502,  2862,  3250,  3667,
	3887,  4114,  4349,  4591,
	4841,  5099,  5364,  5638,
	5920,  6209,  6507,  6812,
	6968,  7126,  7287,  7449,
	7613,  7779,  7948,  8118,
	8291,  8466,  8643,  8822,
	9003,  9187,  9372,  9560,
	9655,  9750,  9846,  9942,
	10039, 10136, 10234, 10333,
	10432, 10531, 10631, 10732,
	10833, 10935, 11038, 11141,
};

static const u32 sdr2hdr_st2084oetf_yn_for_hlg_hdr[65] = {
	0,
	668,	910,	1217,	1600,
	2068,	2384,	2627,	3282,
	3710,	4033,	4879,	5416,
	5815,	6135,	6401,	6631,
	6833,	7176,	7462,	7707,
	7921,	8113,	8285,	8442,
	8586,	8843,	9068,	9268,
	9447,	9760,	10027,	10259,
	10465,	10650,	10817,	10971,
	11243,	11480,	11689,	11877,
	12047,	12202,	12345,	12477,
	12601,	12716,	12926,	13115,
	13285,	13441,	13583,	13716,
	13839,	13953,	14163,	14350,
	14519,	14673,	14945,	15180,
	15570,	15887,	16153,	16383,
};

static const u32 sdr2hdr_st2084oetf_yn_for_bt2020[65] = {
	0,
	0,     0,     1,     2,
	4,     6,     9,     18,
	27,    36,    72,    108,
	144,   180,   216,   252,
	288,   360,   432,   504,
	576,   648,   720,   792,
	864,   1008,  1152,  1296,
	1444,  1706,  1945,  2166,
	2372,  2566,  2750,  2924,
	3251,  3553,  3834,  4099,
	4350,  4588,  4816,  5035,
	5245,  5447,  5832,  6194,
	6536,  6862,  7173,  7471,
	7758,  8035,  8560,  9055,
	9523,  9968,  10800, 11569,
	12963, 14210, 15347, 16383,
};

static u32 sdr2hdr_st2084oetf_yn_for_hdr[65] = {
	0,
	281,   418,   610,   871,
	1217,  1464,  1662,  2218,
	2599,  2896,  3699,  4228,
	4628,  4953,  5227,  5466,
	5676,  6038,  6341,  6602,
	6833,  7039,  7226,  7396,
	7554,  7835,  8082,  8302,
	8501,  8848,  9145,  9405,
	9635,  9842,  10031, 10204,
	10512, 10779, 11017, 11230,
	11423, 11599, 11762, 11913,
	12054, 12185, 12426, 12641,
	12835, 13013, 13177, 13328,
	13469, 13600, 13840, 14055,
	14248, 14425, 14737, 15006,
	15453, 15816, 16121, 16383,
};

static const u32 sdr2hdr_st2084oetf_dxn_pow2[64] = {
	0,  0,  1,  2,
	3,  3,  3,  5,
	5,  5,  7,  7,
	7,  7,  7,  7,
	7,  8,  8,  8,
	8,  8,  8,  8,
	8,  9,  9,  9,
	9,  10, 10, 10,
	10, 10, 10, 10,
	11, 11, 11, 11,
	11, 11, 11, 11,
	11, 11, 12, 12,
	12, 12, 12, 12,
	12, 12, 13, 13,
	13, 13, 14, 14,
	15, 15, 15, 15,
};

static const u32 sdr2hdr_st2084oetf_dxn[64] = {
	1,     1,     2,     4,
	8,     8,     8,     32,
	32,    32,    128,   128,
	128,   128,   128,   128,
	128,   256,   256,   256,
	256,   256,   256,   256,
	256,   512,   512,   512,
	512,   1024,  1024,  1024,
	1024,  1024,  1024,  1024,
	2048,  2048,  2048,  2048,
	2048,  2048,  2048,  2048,
	2048,  2048,  4096,  4096,
	4096,  4096,  4096,  4096,
	4096,  4096,  8192,  8192,
	8192,  8192,  16384, 16384,
	32768, 32768, 32768, 32768,
};

static const u32 sdr2hdr_st2084oetf_xn[63] = {
	1,      2,      4,      8,
	16,     24,     32,     64,
	96,     128,    256,    384,
	512,    640,    768,    896,
	1024,   1280,   1536,   1792,
	2048,   2304,   2560,   2816,
	3072,   3584,   4096,   4608,
	5120,   6144,   7168,   8192,
	9216,   10240,  11264,  12288,
	14336,  16384,  18432,  20480,
	22528,  24576,  26624,  28672,
	30720,  32768,  36864,  40960,
	45056,  49152,  53248,  57344,
	61440,  65536,  73728,  81920,
	90112,  98304,  114688, 131072,
	163840, 196608, 229376,
};

static u32 hdr2sdr_eetf_yn[33] = {
	1716,
	1880,	2067,	2277,	2508,
	2758,	3026,	3310,	3609,
	3921,	4246,	4581,	4925,
	5279,	5640,	6007,	6380,
	6758,	7140,	7526,	7914,
	8304,	8694,	9074,	9438,
	9779,	10093,	10373,	10615,
	10812,	10960,	11053,	11084,
};

static u32 hdr2sdr_bt1886oetf_yn[33] = {
	0,
	0,	0,	0,	0,
	0,	0,	0,	314,
	746,	1323,	2093,	2657,
	3120,	3519,	3874,	4196,
	4492,	5024,	5498,	5928,
	6323,	7034,	7666,	8239,
	8766,	9716,	10560,	11325,
	12029,	13296,	14422,	16383,
};

static const u32 hdr2sdr_sat_yn[9] = {
	0,
	1792, 3584, 3472, 2778,
	2083, 1389, 694,  0,
};

static const struct vop_hdr_table rk3568_vop_hdr_table = {
	.hdr2sdr_eetf_yn = hdr2sdr_eetf_yn,
	.hdr2sdr_bt1886oetf_yn = hdr2sdr_bt1886oetf_yn,
	.hdr2sdr_sat_yn = hdr2sdr_sat_yn,

	.hdr2sdr_src_range_min = 494,
	.hdr2sdr_src_range_max = 12642,
	.hdr2sdr_normfaceetf = 1327,
	.hdr2sdr_dst_range_min = 4,
	.hdr2sdr_dst_range_max = 3276,
	.hdr2sdr_normfacgamma = 5120,

	.sdr2hdr_bt1886eotf_yn_for_hlg_hdr = sdr2hdr_bt1886eotf_yn_for_hlg_hdr,
	.sdr2hdr_bt1886eotf_yn_for_bt2020 = sdr2hdr_bt1886eotf_yn_for_bt2020,
	.sdr2hdr_bt1886eotf_yn_for_hdr = sdr2hdr_bt1886eotf_yn_for_hdr,
	.sdr2hdr_st2084oetf_yn_for_hlg_hdr = sdr2hdr_st2084oetf_yn_for_hlg_hdr,
	.sdr2hdr_st2084oetf_yn_for_bt2020 = sdr2hdr_st2084oetf_yn_for_bt2020,
	.sdr2hdr_st2084oetf_yn_for_hdr = sdr2hdr_st2084oetf_yn_for_hdr,
	.sdr2hdr_st2084oetf_dxn_pow2 = sdr2hdr_st2084oetf_dxn_pow2,
	.sdr2hdr_st2084oetf_dxn = sdr2hdr_st2084oetf_dxn,
	.sdr2hdr_st2084oetf_xn = sdr2hdr_st2084oetf_xn,
};

static const int rk3568_vop_axi_intrs[] = {
	0,
	BUS_ERROR_INTR,
	0,
	WB_UV_FIFO_FULL_INTR,
	WB_YRGB_FIFO_FULL_INTR,
	WB_COMPLETE_INTR,

};

static const struct vop_intr rk3528_vop_axi_intr[] = {
	{
	  .intrs = rk3568_vop_axi_intrs,
	  .nintrs = ARRAY_SIZE(rk3568_vop_axi_intrs),
	  .status = VOP_REG(RK3568_SYS0_INT_STATUS, 0xfe, 0),
	  .enable = VOP_REG_MASK(RK3568_SYS0_INT_EN, 0xfe, 0),
	  .clear = VOP_REG_MASK(RK3568_SYS0_INT_CLR, 0xfe, 0),
	},
};

static const struct vop_intr rk3568_vop_axi_intr[] = {
	{
	  .intrs = rk3568_vop_axi_intrs,
	  .nintrs = ARRAY_SIZE(rk3568_vop_axi_intrs),
	  .status = VOP_REG(RK3568_SYS0_INT_STATUS, 0xfe, 0),
	  .enable = VOP_REG_MASK(RK3568_SYS0_INT_EN, 0xfe, 0),
	  .clear = VOP_REG_MASK(RK3568_SYS0_INT_CLR, 0xfe, 0),
	},

	{
	  .intrs = rk3568_vop_axi_intrs,
	  .nintrs = ARRAY_SIZE(rk3568_vop_axi_intrs),
	  .status = VOP_REG(RK3568_SYS1_INT_STATUS, 0xfe, 0),
	  .enable = VOP_REG_MASK(RK3568_SYS1_INT_EN, 0xfe, 0),
	  .clear = VOP_REG_MASK(RK3568_SYS1_INT_CLR, 0xfe, 0),
	},
};

static const int rk3568_vop_intrs[] = {
	FS_INTR,
	FS_NEW_INTR,
	LINE_FLAG_INTR,
	LINE_FLAG1_INTR,
	POST_BUF_EMPTY_INTR,
	FS_FIELD_INTR,
	DSP_HOLD_VALID_INTR,
};

static const struct vop_intr rk3568_vp0_intr = {
	.intrs = rk3568_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3568_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3568_VP0_LINE_FLAG, 0x1fff, 0),
	.line_flag_num[1] = VOP_REG(RK3568_VP0_LINE_FLAG, 0x1fff, 16),
	.status = VOP_REG(RK3568_VP0_INT_STATUS, 0xffff, 0),
	.enable = VOP_REG_MASK(RK3568_VP0_INT_EN, 0xffff, 0),
	.clear = VOP_REG_MASK(RK3568_VP0_INT_CLR, 0xffff, 0),
};

static const struct vop_intr rk3568_vp1_intr = {
	.intrs = rk3568_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3568_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3568_VP1_LINE_FLAG, 0x1fff, 0),
	.line_flag_num[1] = VOP_REG(RK3568_VP1_LINE_FLAG, 0x1fff, 16),
	.status = VOP_REG(RK3568_VP1_INT_STATUS, 0xffff, 0),
	.enable = VOP_REG_MASK(RK3568_VP1_INT_EN, 0xffff, 0),
	.clear = VOP_REG_MASK(RK3568_VP1_INT_CLR, 0xffff, 0),
};

static const struct vop_intr rk3568_vp2_intr = {
	.intrs = rk3568_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3568_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3568_VP2_LINE_FLAG, 0x1fff, 0),
	.line_flag_num[1] = VOP_REG(RK3568_VP2_LINE_FLAG, 0x1fff, 16),
	.status = VOP_REG(RK3568_VP2_INT_STATUS, 0xffff, 0),
	.enable = VOP_REG_MASK(RK3568_VP2_INT_EN, 0xffff, 0),
	.clear = VOP_REG_MASK(RK3568_VP2_INT_CLR, 0xffff, 0),
};

static const struct vop_intr rk3588_vp3_intr = {
	.intrs = rk3568_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3568_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3588_VP3_LINE_FLAG, 0x1fff, 0),
	.line_flag_num[1] = VOP_REG(RK3588_VP3_LINE_FLAG, 0x1fff, 16),
	.status = VOP_REG(RK3588_VP3_INT_STATUS, 0xffff, 0),
	.enable = VOP_REG_MASK(RK3588_VP3_INT_EN, 0xffff, 0),
	.clear = VOP_REG_MASK(RK3588_VP3_INT_CLR, 0xffff, 0),
};

static const struct vop2_dsc_regs rk3588_vop_dsc_8k_regs = {
	/* DSC SYS CTRL */
	.dsc_port_sel = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x3, 0),
	.dsc_man_mode = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x1, 2),
	.dsc_interface_mode = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x3, 4),
	.dsc_pixel_num = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x3, 6),
	.dsc_pxl_clk_div = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x1, 8),
	.dsc_cds_clk_div = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x3, 12),
	.dsc_txp_clk_div = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x3, 14),
	.dsc_init_dly_mode = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x1, 16),
	.dsc_scan_en = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x1, 17),
	.dsc_halt_en = VOP_REG(RK3588_DSC_8K_SYS_CTRL, 0x1, 18),
	.rst_deassert = VOP_REG(RK3588_DSC_8K_RST, 0x1, 0),
	.dsc_flush = VOP_REG(RK3588_DSC_8K_RST, 0x1, 16),
	.dsc_cfg_done = VOP_REG(RK3588_DSC_8K_CFG_DONE, 0x1, 0),
	.dsc_init_dly_num = VOP_REG(RK3588_DSC_8K_INIT_DLY, 0xffff, 0),
	.scan_timing_para_imd_en = VOP_REG(RK3588_DSC_8K_INIT_DLY, 0x1, 16),
	.dsc_htotal_pw = VOP_REG(RK3588_DSC_8K_HTOTAL_HS_END, 0xffffffff, 0),
	.dsc_hact_st_end = VOP_REG(RK3588_DSC_8K_HACT_ST_END, 0xffffffff, 0),
	.dsc_vtotal = VOP_REG(RK3588_DSC_8K_VTOTAL_VS_END, 0xffff, 16),
	.dsc_vs_end = VOP_REG(RK3588_DSC_8K_VTOTAL_VS_END, 0xffff, 0),
	.dsc_vact_st_end = VOP_REG(RK3588_DSC_8K_VACT_ST_END, 0xffffffff, 0),
	.dsc_error_status = VOP_REG(RK3588_DSC_8K_STATUS, 0x1, 0),

	/* DSC encoder */
	.dsc_pps0_3 = VOP_REG(RK3588_DSC_8K_PPS0_3, 0xffffffff, 0),
	.dsc_en = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 0),
	.dsc_rbit = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 2),
	.dsc_rbyt = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 3),
	.dsc_flal = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 4),
	.dsc_mer = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 5),
	.dsc_epb = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 6),
	.dsc_epl = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 7),
	.dsc_nslc = VOP_REG(RK3588_DSC_8K_CTRL0, 0x7, 16),
	.dsc_sbo = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 28),
	.dsc_ifep = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 29),
	.dsc_pps_upd = VOP_REG(RK3588_DSC_8K_CTRL0, 0x1, 31),
	.dsc_status = VOP_REG(RK3588_DSC_8K_STS0, 0xffffffff, 0),
	.dsc_ecw = VOP_REG(RK3588_DSC_8K_ERS, 0xffffffff, 0),
};

static const struct vop2_dsc_regs rk3588_vop_dsc_4k_regs = {
	/* DSC SYS CTRL */
	.dsc_port_sel = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x3, 0),
	.dsc_man_mode = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x1, 2),
	.dsc_interface_mode = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x3, 4),
	.dsc_pixel_num = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x3, 6),
	.dsc_pxl_clk_div = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x1, 8),
	.dsc_cds_clk_div = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x3, 12),
	.dsc_txp_clk_div = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x3, 14),
	.dsc_init_dly_mode = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x1, 16),
	.dsc_scan_en = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x1, 17),
	.dsc_halt_en = VOP_REG(RK3588_DSC_4K_SYS_CTRL, 0x1, 18),
	.rst_deassert = VOP_REG(RK3588_DSC_4K_RST, 0x1, 0),
	.dsc_flush = VOP_REG(RK3588_DSC_4K_RST, 0x1, 16),
	.dsc_cfg_done = VOP_REG(RK3588_DSC_4K_CFG_DONE, 0x1, 0),
	.dsc_init_dly_num = VOP_REG(RK3588_DSC_4K_INIT_DLY, 0xffff, 0),
	.scan_timing_para_imd_en = VOP_REG(RK3588_DSC_4K_INIT_DLY, 0x1, 16),
	.dsc_htotal_pw = VOP_REG(RK3588_DSC_4K_HTOTAL_HS_END, 0xffffffff, 0),
	.dsc_hact_st_end = VOP_REG(RK3588_DSC_4K_HACT_ST_END, 0xffffffff, 0),
	.dsc_vtotal = VOP_REG(RK3588_DSC_4K_VTOTAL_VS_END, 0xffff, 16),
	.dsc_vs_end = VOP_REG(RK3588_DSC_4K_VTOTAL_VS_END, 0xffff, 0),
	.dsc_vact_st_end = VOP_REG(RK3588_DSC_4K_VACT_ST_END, 0xffffffff, 0),
	.dsc_error_status = VOP_REG(RK3588_DSC_4K_STATUS, 0x1, 0),

	/* DSC encoder */
	.dsc_pps0_3 = VOP_REG(RK3588_DSC_4K_PPS0_3, 0xffffffff, 0),
	.dsc_en = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 0),
	.dsc_rbit = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 2),
	.dsc_rbyt = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 3),
	.dsc_flal = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 4),
	.dsc_mer = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 5),
	.dsc_epb = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 6),
	.dsc_epl = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 7),
	.dsc_nslc = VOP_REG(RK3588_DSC_4K_CTRL0, 0x7, 16),
	.dsc_sbo = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 28),
	.dsc_ifep = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 29),
	.dsc_pps_upd = VOP_REG(RK3588_DSC_4K_CTRL0, 0x1, 31),
	.dsc_status = VOP_REG(RK3588_DSC_4K_STS0, 0xffffffff, 0),
	.dsc_ecw = VOP_REG(RK3588_DSC_4K_ERS, 0xffffffff, 0),
};

static const struct dsc_error_info dsc_ecw[] = {
	{0x00000000, "no error detected by DSC encoder"},
	{0x0030ffff, "bits per component error"},
	{0x0040ffff, "multiple mode error"},
	{0x0050ffff, "line buffer depth error"},
	{0x0060ffff, "minor version error"},
	{0x0070ffff, "picture height error"},
	{0x0080ffff, "picture width error"},
	{0x0090ffff, "number of slices error"},
	{0x00c0ffff, "slice height Error "},
	{0x00d0ffff, "slice width error"},
	{0x00e0ffff, "second line BPG offset error"},
	{0x00f0ffff, "non second line BPG offset error"},
	{0x0100ffff, "PPS ID error"},
	{0x0110ffff, "bits per pixel (BPP) Error"},
	{0x0120ffff, "buffer flow error"},  /* dsc_buffer_flow */

	{0x01510001, "slice 0 RC buffer model overflow error"},
	{0x01510002, "slice 1 RC buffer model overflow error"},
	{0x01510004, "slice 2 RC buffer model overflow error"},
	{0x01510008, "slice 3 RC buffer model overflow error"},
	{0x01510010, "slice 4 RC buffer model overflow error"},
	{0x01510020, "slice 5 RC buffer model overflow error"},
	{0x01510040, "slice 6 RC buffer model overflow error"},
	{0x01510080, "slice 7 RC buffer model overflow error"},

	{0x01610001, "slice 0 RC buffer model underflow error"},
	{0x01610002, "slice 1 RC buffer model underflow error"},
	{0x01610004, "slice 2 RC buffer model underflow error"},
	{0x01610008, "slice 3 RC buffer model underflow error"},
	{0x01610010, "slice 4 RC buffer model underflow error"},
	{0x01610020, "slice 5 RC buffer model underflow error"},
	{0x01610040, "slice 6 RC buffer model underflow error"},
	{0x01610080, "slice 7 RC buffer model underflow error"},

	{0xffffffff, "unsuccessful RESET cycle status"},
	{0x00a0ffff, "ICH full error precision settings error"},
	{0x0020ffff, "native mode"},
};

static const struct dsc_error_info dsc_buffer_flow[] = {
	{0x00000000, "rate buffer status"},
	{0x00000001, "line buffer status"},
	{0x00000002, "decoder model status"},
	{0x00000003, "pixel buffer status"},
	{0x00000004, "balance fifo buffer status"},
	{0x00000005, "syntax element fifo status"},
};

static const struct vop2_dsc_data rk3588_vop_dsc_data[] = {
	{
	 .id = ROCKCHIP_VOP2_DSC_8K,
	 .pd_id = VOP2_PD_DSC_8K,
	 .max_slice_num = 8,
	 .max_linebuf_depth = 11,
	 .min_bits_per_pixel = 8,
	 .dsc_txp_clk_src_name = "dsc_8k_txp_clk_src",
	 .dsc_txp_clk_name = "dsc_8k_txp_clk",
	 .dsc_pxl_clk_name = "dsc_8k_pxl_clk",
	 .dsc_cds_clk_name = "dsc_8k_cds_clk",
	 .regs = &rk3588_vop_dsc_8k_regs,
	},

	{
	 .id = ROCKCHIP_VOP2_DSC_4K,
	 .pd_id = VOP2_PD_DSC_4K,
	 .max_slice_num = 2,
	 .max_linebuf_depth = 11,
	 .min_bits_per_pixel = 8,
	 .dsc_txp_clk_src_name = "dsc_4k_txp_clk_src",
	 .dsc_txp_clk_name = "dsc_4k_txp_clk",
	 .dsc_pxl_clk_name = "dsc_4k_pxl_clk",
	 .dsc_cds_clk_name = "dsc_4k_cds_clk",
	 .regs = &rk3588_vop_dsc_4k_regs,
	},
};

static const struct vop2_wb_regs rk3568_vop_wb_regs = {
	.enable = VOP_REG(RK3568_WB_CTRL, 0x1, 0),
	.format = VOP_REG(RK3568_WB_CTRL, 0x7, 1),
	.dither_en = VOP_REG(RK3568_WB_CTRL, 0x1, 4),
	.r2y_en = VOP_REG(RK3568_WB_CTRL, 0x1, 5),
	.scale_x_en = VOP_REG(RK3568_WB_CTRL, 0x1, 7),
	.scale_y_en = VOP_REG(RK3568_WB_CTRL, 0x1, 8),
	.axi_yrgb_id = VOP_REG(RK3568_WB_CTRL, 0xff, 19),
	.axi_uv_id = VOP_REG(RK3568_WB_CTRL, 0x1f, 27),
	.scale_x_factor = VOP_REG(RK3568_WB_XSCAL_FACTOR, 0x3fff, 16),
	.yrgb_mst = VOP_REG(RK3568_WB_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_WB_CBR_MST, 0xffffffff, 0),
	.vp_id = VOP_REG(RK3568_LUT_PORT_SEL, 0x3, 8),
	.fifo_throd = VOP_REG(RK3568_WB_XSCAL_FACTOR, 0x3ff, 0),
};

static const struct vop2_wb_data rk3568_vop_wb_data = {
	.formats = formats_wb,
	.nformats = ARRAY_SIZE(formats_wb),
	.max_output = { 1920, 1080 },
	.fifo_depth =  1920 * 4 / 16,
	.regs = &rk3568_vop_wb_regs,
};

static const struct vop2_video_port_regs rk3528_vop_vp0_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 0),
	.overlay_mode = VOP_REG(RK3528_OVL_PORT0_CTRL, 0x1, 0),
	.dsp_background = VOP_REG(RK3568_VP0_DSP_BG, 0xffffffff, 0),
	.out_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0xf, 0),
	.core_dclk_div = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 4),
	.dclk_div2 = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 4),
	.dclk_div2_phase_lock = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 5),
	.p2i_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 15),
	.pre_dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP0_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 20),
	.gamma_update_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 22),
	.dsp_lut_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 28),
	.standby = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31),
	.bg_mix_ctrl = VOP_REG(RK3528_OVL_PORT0_BG_MIX_CTRL, 0xffff, 0),
	.bg_dly = VOP_REG(RK3528_OVL_PORT0_BG_MIX_CTRL, 0xff, 24),
	.pre_scan_htiming = VOP_REG(RK3568_VP0_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.hpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP0_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP0_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3568_VP0_DSP_HTOTAL_HS_END, 0xffffffff, 0),
	.hact_st_end = VOP_REG(RK3568_VP0_DSP_HACT_ST_END, 0xffffffff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.sw_dsp_vtotal_imd = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1, 15),
	.dsp_vs_end = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP0_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.lut_dma_rid = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0xf, 4),
	.layer_sel = VOP_REG(RK3528_OVL_PORT0_LAYER_SEL, 0xffff, 0),
	.hdr_src_color_ctrl = VOP_REG(RK3528_HDR_SRC_COLOR_CTRL, 0xffffffff, 0),
	.hdr_dst_color_ctrl = VOP_REG(RK3528_HDR_DST_COLOR_CTRL, 0xffffffff, 0),
	.hdr_src_alpha_ctrl = VOP_REG(RK3528_HDR_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.hdr_dst_alpha_ctrl = VOP_REG(RK3528_HDR_DST_ALPHA_CTRL, 0xffffffff, 0),
	.hdr_lut_update_en = VOP_REG(RK3568_HDR_LUT_CTRL, 0x1, 0),
	.hdr_lut_mode = VOP_REG(RK3568_HDR_LUT_CTRL, 0x1, 1),
	.hdr_lut_mst = VOP_REG(RK3568_HDR_LUT_MST, 0xffffffff, 0),
	.hdr_lut_fetch_done = VOP_REG(RK3528_HDR_LUT_STATUS, 0x1, 0),
	.hdr10_en = VOP_REG(RK3568_OVL_CTRL, 0x1, 4),
	.sdr2hdr_path_en = VOP_REG(RK3568_OVL_CTRL, 0x1, 5),
	.sdr2hdr_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 0),
	.sdr2hdr_auto_gating_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 1),
	.sdr2hdr_bypass_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 2),
	.sdr2hdr_dstmode = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 3),
	.hdr_vivid_en = VOP_REG(RK3528_HDRVIVID_CTRL, 0x1, 0),
	.hdr_vivid_bypass_en = VOP_REG(RK3528_HDRVIVID_CTRL, 0x1, 2),
	.hdr_vivid_path_mode = VOP_REG(RK3528_HDRVIVID_CTRL, 0x7, 3),
	.hdr_vivid_dstgamut = VOP_REG(RK3528_HDRVIVID_CTRL, 0x1, 6),
	.acm_bypass_en = VOP_REG(RK3528_VP0_ACM_CTRL, 0x1, 0),
	.csc_en = VOP_REG(RK3528_VP0_ACM_CTRL, 0x1, 1),
	.acm_r2y_en = VOP_REG(RK3528_VP0_ACM_CTRL, 0x1, 2),
	.csc_mode = VOP_REG(RK3528_VP0_ACM_CTRL, 0x7, 3),
	.acm_r2y_mode = VOP_REG(RK3528_VP0_ACM_CTRL, 0x7, 8),
	.csc_coe00 = VOP_REG(RK3528_VP0_ACM_CTRL, 0xffff, 16),
	.csc_coe01 = VOP_REG(RK3528_VP0_CSC_COE01_02, 0xffff, 0),
	.csc_coe02 = VOP_REG(RK3528_VP0_CSC_COE01_02, 0xffff, 16),
	.csc_coe10 = VOP_REG(RK3528_VP0_CSC_COE10_11, 0xffff, 0),
	.csc_coe11 = VOP_REG(RK3528_VP0_CSC_COE10_11, 0xffff, 16),
	.csc_coe12 = VOP_REG(RK3528_VP0_CSC_COE12_20, 0xffff, 0),
	.csc_coe20 = VOP_REG(RK3528_VP0_CSC_COE12_20, 0xffff, 16),
	.csc_coe21 = VOP_REG(RK3528_VP0_CSC_COE21_22, 0xffff, 0),
	.csc_coe22 = VOP_REG(RK3528_VP0_CSC_COE21_22, 0xffff, 16),
	.csc_offset0 = VOP_REG(RK3528_VP0_CSC_OFFSET0, 0xffffffff, 0),
	.csc_offset1 = VOP_REG(RK3528_VP0_CSC_OFFSET1, 0xffffffff, 0),
	.csc_offset2 = VOP_REG(RK3528_VP0_CSC_OFFSET2, 0xffffffff, 0),
	.color_bar_mode = VOP_REG(RK3568_VP0_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP0_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop2_video_port_regs rk3528_vop_vp1_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 1),
	.overlay_mode = VOP_REG(RK3528_OVL_PORT1_CTRL, 0x1, 0),
	.dsp_background = VOP_REG(RK3568_VP1_DSP_BG, 0xffffffff, 0),
	.out_mode = VOP_REG(RK3568_VP1_DSP_CTRL, 0xf, 0),
	.core_dclk_div = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 4),
	.p2i_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 15),
	.pre_dither_down_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP1_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 20),
	.gamma_update_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 22),
	.dsp_lut_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 28),
	.standby = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31),
	.bg_mix_ctrl = VOP_REG(RK3528_OVL_PORT1_BG_MIX_CTRL, 0xffff, 0),
	.bg_dly = VOP_REG(RK3528_OVL_PORT1_BG_MIX_CTRL, 0xff, 24),
	.pre_scan_htiming = VOP_REG(RK3568_VP1_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.hpost_st_end = VOP_REG(RK3568_VP1_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP1_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP1_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP1_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3568_VP1_DSP_HTOTAL_HS_END, 0xffffffff, 0),
	.hact_st_end = VOP_REG(RK3568_VP1_DSP_HACT_ST_END, 0xffffffff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.sw_dsp_vtotal_imd = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1, 15),
	.dsp_vs_end = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP1_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP1_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP1_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP1_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.bcsh_brightness = VOP_REG(RK3568_VP1_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3568_VP1_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3568_VP1_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3568_VP1_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3568_VP1_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3568_VP1_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3568_VP1_BCSH_COLOR_BAR, 0x1, 31),
	.lut_dma_rid = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0xf, 4),
	.layer_sel = VOP_REG(RK3528_OVL_PORT1_LAYER_SEL, 0xffff, 0),
	.color_bar_mode = VOP_REG(RK3568_VP1_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP1_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop3_ovl_mix_regs rk3528_vop_hdr_mix_regs = {
	.src_color_ctrl = VOP_REG(RK3528_HDR_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3528_HDR_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3528_HDR_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3528_HDR_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop3_ovl_mix_regs rk3528_vop_vp0_layer_mix_regs = {
	.src_color_ctrl = VOP_REG(RK3528_OVL_PORT0_MIX0_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3528_OVL_PORT0_MIX0_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3528_OVL_PORT0_MIX0_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3528_OVL_PORT0_MIX0_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop3_ovl_mix_regs rk3528_vop_vp1_layer_mix_regs = {
	.src_color_ctrl = VOP_REG(RK3528_OVL_PORT1_MIX0_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3528_OVL_PORT1_MIX0_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3528_OVL_PORT1_MIX0_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3528_OVL_PORT1_MIX0_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop3_ovl_regs rk3528_vop_vp0_ovl_regs = {
	.layer_mix_regs = &rk3528_vop_vp0_layer_mix_regs,
	.hdr_mix_regs = &rk3528_vop_hdr_mix_regs,
};

static const struct vop3_ovl_regs rk3528_vop_vp1_ovl_regs = {
	.layer_mix_regs = &rk3528_vop_vp1_layer_mix_regs,
};

static const struct vop2_video_port_data rk3528_vop_video_ports[] = {
	{
	 .id = 0,
	 .soc_id = { 0x3528, 0x3528 },
	 .lut_dma_rid = 14,
	 .feature = VOP_FEATURE_ALPHA_SCALE | VOP_FEATURE_OVERSCAN | VOP_FEATURE_VIVID_HDR |
		    VOP_FEATURE_POST_ACM | VOP_FEATURE_POST_CSC | VOP_FEATURE_OUTPUT_10BIT,
	 .gamma_lut_len = 1024,
	 .max_output = { 4096, 4096 },
	 .hdrvivid_dly = {17, 29, 32, 44, 15, 38, 1, 29, 0, 0},
	 .sdr2hdr_dly = 21,
	 .layer_mix_dly = 6,
	 .hdr_mix_dly = 2,
	 .win_dly = 8,
	 .intr = &rk3568_vp0_intr,
	 .regs = &rk3528_vop_vp0_regs,
	 .ovl_regs = &rk3528_vop_vp0_ovl_regs,
	},
	{
	 .id = 1,
	 .soc_id = { 0x3528, 0x3528 },
	 .feature = VOP_FEATURE_ALPHA_SCALE | VOP_FEATURE_OVERSCAN,
	 .max_output = { 720, 576 },
	 .hdrvivid_dly = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	 .sdr2hdr_dly = 0,
	 .layer_mix_dly = 2,
	 .hdr_mix_dly = 0,
	 .win_dly = 8,
	 .intr = &rk3568_vp1_intr,
	 .regs = &rk3528_vop_vp1_regs,
	 .ovl_regs = &rk3528_vop_vp1_ovl_regs,
	},
};

static const struct vop2_video_port_regs rk3562_vop_vp0_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 0),
	.overlay_mode = VOP_REG(RK3528_OVL_PORT0_CTRL, 0x1, 0),
	.dsp_background = VOP_REG(RK3568_VP0_DSP_BG, 0xffffffff, 0),
	.out_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0xf, 0),
	.core_dclk_div = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 4),
	.p2i_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 15),
	.pre_dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP0_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 20),
	.gamma_update_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 22),
	.dsp_lut_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 28),
	.standby = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31),
	.bg_mix_ctrl = VOP_REG(RK3528_OVL_PORT0_BG_MIX_CTRL, 0xffff, 0),
	.bg_dly = VOP_REG(RK3528_OVL_PORT0_BG_MIX_CTRL, 0xff, 24),
	.pre_scan_htiming = VOP_REG(RK3568_VP0_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.hpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP0_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP0_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3568_VP0_DSP_HTOTAL_HS_END, 0xffffffff, 0),
	.hact_st_end = VOP_REG(RK3568_VP0_DSP_HACT_ST_END, 0xffffffff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.sw_dsp_vtotal_imd = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1, 15),
	.dsp_vs_end = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP0_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.bcsh_brightness = VOP_REG(RK3568_VP0_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3568_VP0_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3568_VP0_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3568_VP0_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3568_VP0_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3568_VP0_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3568_VP0_BCSH_COLOR_BAR, 0x1, 31),
	.edpi_te_en = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 28),
	.edpi_wms_hold_en = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 30),
	.edpi_wms_fs = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 31),
	.lut_dma_rid = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0xf, 4),
	.cubic_lut_en = VOP_REG(RK3568_VP0_3D_LUT_CTRL, 0x1, 0),
	.cubic_lut_update_en = VOP_REG(RK3568_VP0_3D_LUT_CTRL, 0x1, 2),
	.cubic_lut_mst = VOP_REG(RK3568_VP0_3D_LUT_MST, 0xffffffff, 0),

	.mcu_pix_total = VOP_REG(RK3562_VP0_MCU_CTRL, 0x3f, 0),
	.mcu_cs_pst = VOP_REG(RK3562_VP0_MCU_CTRL, 0xf, 6),
	.mcu_cs_pend = VOP_REG(RK3562_VP0_MCU_CTRL, 0x3f, 10),
	.mcu_rw_pst = VOP_REG(RK3562_VP0_MCU_CTRL, 0xf, 16),
	.mcu_rw_pend = VOP_REG(RK3562_VP0_MCU_CTRL, 0x3f, 20),
	.mcu_hold_mode = VOP_REG(RK3562_VP0_MCU_CTRL, 0x1, 27),
	.mcu_frame_st = VOP_REG(RK3562_VP0_MCU_CTRL, 0x1, 28),
	.mcu_rs = VOP_REG(RK3562_VP0_MCU_CTRL, 0x1, 29),
	.mcu_bypass = VOP_REG(RK3562_VP0_MCU_CTRL, 0x1, 30),
	.mcu_type = VOP_REG(RK3562_VP0_MCU_CTRL, 0x1, 31),
	.mcu_rw_bypass_port = VOP_REG(RK3562_VP0_MCU_RW_BYPASS_PORT, 0xffffffff, 0),
	.layer_sel = VOP_REG(RK3528_OVL_PORT0_LAYER_SEL, 0xffff, 0),

	.color_bar_mode = VOP_REG(RK3568_VP0_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP0_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop2_video_port_data rk3562_vop_video_ports[] = {
	{
	 .id = 0,
	 .soc_id = { 0x3562, 0x3562 },
	 .lut_dma_rid = 14,
	 .feature = VOP_FEATURE_ALPHA_SCALE | VOP_FEATURE_OVERSCAN,
	 .gamma_lut_len = 1024,
	 .cubic_lut_len = 729, /* 9x9x9 */
	 .max_output = { 2048, 4096 },
	 .win_dly = 8,
	 .layer_mix_dly = 8,
	 .intr = &rk3568_vp0_intr,
	 .regs = &rk3562_vop_vp0_regs,
	 .ovl_regs = &rk3528_vop_vp0_ovl_regs,
	},
};

static const struct vop2_video_port_regs rk3568_vop_vp0_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 0),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 0),
	.dsp_background = VOP_REG(RK3568_VP0_DSP_BG, 0x3fffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 0),
	.out_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0xf, 0),
	.standby = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31),
	.core_dclk_div = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 4),
	.dclk_div2 = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 4),
	.dclk_div2_phase_lock = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 5),
	.p2i_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 15),
	.pre_scan_htiming = VOP_REG(RK3568_VP0_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP0_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.htotal_pw = VOP_REG(RK3568_VP0_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP0_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP0_POST_SCL_CTRL, 0x3, 0),
	.hact_st_end = VOP_REG(RK3568_VP0_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.dsp_vs_end = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP0_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.pre_dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP0_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 20),
	.dual_channel_en = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 20),
	.dual_channel_swap = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 21),
	.dsp_lut_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 28),
	.hdr10_en = VOP_REG(RK3568_OVL_CTRL, 0x1, 4),
	.hdr_lut_update_en = VOP_REG(RK3568_HDR_LUT_CTRL, 0x1, 0),
	.hdr_lut_mode = VOP_REG(RK3568_HDR_LUT_CTRL, 0x1, 1),
	.hdr_lut_mst = VOP_REG(RK3568_HDR_LUT_MST, 0xffffffff, 0),
	.sdr2hdr_eotf_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 0),
	.sdr2hdr_r2r_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 1),
	.sdr2hdr_r2r_mode = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 2),
	.sdr2hdr_oetf_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 3),
	.sdr2hdr_bypass_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 8),
	.sdr2hdr_auto_gating_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 9),
	.sdr2hdr_path_en = VOP_REG(RK3568_OVL_CTRL, 0x1, 5),
	.hdr2sdr_en = VOP_REG(RK3568_HDR2SDR_CTRL, 0x1, 0),
	.hdr2sdr_bypass_en = VOP_REG(RK3568_HDR2SDR_CTRL, 0x1, 8),
	.hdr2sdr_auto_gating_en = VOP_REG(RK3568_HDR2SDR_CTRL, 0x1, 9),
	.hdr2sdr_src_min = VOP_REG(RK3568_HDR2SDR_SRC_RANGE, 0x3fff, 0),
	.hdr2sdr_src_max = VOP_REG(RK3568_HDR2SDR_SRC_RANGE, 0x3fff, 16),
	.hdr2sdr_normfaceetf = VOP_REG(RK3568_HDR2SDR_NORMFACEETF, 0x7ff, 0),
	.hdr2sdr_dst_min = VOP_REG(RK3568_HDR2SDR_DST_RANGE, 0xffff, 0),
	.hdr2sdr_dst_max = VOP_REG(RK3568_HDR2SDR_DST_RANGE, 0xffff, 16),
	.hdr2sdr_normfacgamma = VOP_REG(RK3568_HDR2SDR_NORMFACCGAMMA, 0xffff, 0),
	.hdr2sdr_eetf_oetf_y0_offset = RK3568_HDR_EETF_OETF_Y0,
	.hdr2sdr_sat_y0_offset = RK3568_HDR_SAT_Y0,
	.sdr2hdr_eotf_oetf_y0_offset = RK3568_HDR_EOTF_OETF_Y0,
	.sdr2hdr_oetf_dx_pow1_offset = RK3568_HDR_OETF_DX_POW1,
	.sdr2hdr_oetf_xn1_offset = RK3568_HDR_OETF_XN1,
	.hdr_src_color_ctrl = VOP_REG(RK3568_HDR0_SRC_COLOR_CTRL, 0xffffffff, 0),
	.hdr_dst_color_ctrl = VOP_REG(RK3568_HDR0_DST_COLOR_CTRL, 0xffffffff, 0),
	.hdr_src_alpha_ctrl = VOP_REG(RK3568_HDR0_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.hdr_dst_alpha_ctrl = VOP_REG(RK3568_HDR0_DST_ALPHA_CTRL, 0xffffffff, 0),

	.bcsh_brightness = VOP_REG(RK3568_VP0_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3568_VP0_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3568_VP0_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3568_VP0_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3568_VP0_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3568_VP0_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3568_VP0_BCSH_COLOR_BAR, 0x1, 31),

	.cubic_lut_en = VOP_REG(RK3568_VP0_3D_LUT_CTRL, 0x1, 0),
	.cubic_lut_update_en = VOP_REG(RK3568_VP0_3D_LUT_CTRL, 0x1, 2),
	.cubic_lut_mst = VOP_REG(RK3568_VP0_3D_LUT_MST, 0xffffffff, 0),

	.color_bar_mode = VOP_REG(RK3568_VP0_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP0_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop2_video_port_regs rk3568_vop_vp1_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 1),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 1),
	.dsp_background = VOP_REG(RK3568_VP1_DSP_BG, 0x3fffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 4),
	.out_mode = VOP_REG(RK3568_VP1_DSP_CTRL, 0xf, 0),
	.standby = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31),
	.core_dclk_div = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 4),
	.dclk_div2 = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 4),
	.dclk_div2_phase_lock = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 5),
	.p2i_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 15),
	.pre_scan_htiming = VOP_REG(RK3568_VP1_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP1_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP1_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP1_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.htotal_pw = VOP_REG(RK3568_VP1_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP1_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP1_POST_SCL_CTRL, 0x3, 0),
	.hact_st_end = VOP_REG(RK3568_VP1_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.dsp_vs_end = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP1_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP1_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP1_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP1_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.pre_dither_down_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP1_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 20),
	.dual_channel_en = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 20),
	.dual_channel_swap = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 21),

	.bcsh_brightness = VOP_REG(RK3568_VP1_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3568_VP1_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3568_VP1_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3568_VP1_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3568_VP1_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3568_VP1_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3568_VP1_BCSH_COLOR_BAR, 0x1, 31),
	.dsp_lut_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 28),

	.color_bar_mode = VOP_REG(RK3568_VP1_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP1_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop2_video_port_regs rk3568_vop_vp2_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 2),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 2),
	.dsp_background = VOP_REG(RK3568_VP2_DSP_BG, 0x3fffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 8),
	.out_mode = VOP_REG(RK3568_VP2_DSP_CTRL, 0xf, 0),
	.standby = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 31),
	.core_dclk_div = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 4),
	.dclk_div2 = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 4),
	.dclk_div2_phase_lock = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 5),
	.p2i_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 15),
	.pre_scan_htiming = VOP_REG(RK3568_VP2_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP2_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP2_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP2_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP2_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP2_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3568_VP2_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3568_VP2_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP2_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.dsp_vs_end = VOP_REG(RK3568_VP2_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP2_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP2_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP2_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP2_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.pre_dither_down_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP2_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 20),
	.dual_channel_en = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 20),
	.dual_channel_swap = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 21),

	.bcsh_brightness = VOP_REG(RK3568_VP2_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3568_VP2_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3568_VP2_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3568_VP2_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3568_VP2_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3568_VP2_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3568_VP2_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3568_VP2_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3568_VP2_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3568_VP2_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3568_VP2_BCSH_COLOR_BAR, 0x1, 31),
	.dsp_lut_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 28),

	.color_bar_mode = VOP_REG(RK3568_VP2_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP2_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop2_video_port_data rk3568_vop_video_ports[] = {
	{
	 .id = 0,
	 .soc_id = { 0x3568, 0x3566 },
	 .feature = VOP_FEATURE_OUTPUT_10BIT | VOP_FEATURE_ALPHA_SCALE |
			VOP_FEATURE_HDR10 | VOP_FEATURE_OVERSCAN,
	 .gamma_lut_len = 1024,
	 .cubic_lut_len = 729, /* 9x9x9 */
	 .max_output = { 4096, 4096 },
	 .pre_scan_max_dly = { 69, 53, 53, 42 },
	 .intr = &rk3568_vp0_intr,
	 .hdr_table = &rk3568_vop_hdr_table,
	 .regs = &rk3568_vop_vp0_regs,
	},
	{
	 .id = 1,
	 .soc_id = { 0x3568, 0x3566 },
	 .feature = VOP_FEATURE_ALPHA_SCALE | VOP_FEATURE_OVERSCAN,
	 .gamma_lut_len = 1024,
	 .max_output = { 2048, 2048 },
	 .pre_scan_max_dly = { 40, 40, 40, 40 },
	 .intr = &rk3568_vp1_intr,
	 .regs = &rk3568_vop_vp1_regs,
	},
	{
	 .id = 2,
	 .feature = VOP_FEATURE_ALPHA_SCALE | VOP_FEATURE_OVERSCAN,
	 .soc_id = { 0x3568, 0x3566 },
	 .gamma_lut_len = 1024,
	 .max_output = { 1920, 1920 },
	 .pre_scan_max_dly = { 40, 40, 40, 40 },
	 .intr = &rk3568_vp2_intr,
	 .regs = &rk3568_vop_vp2_regs,
	},
};

static const struct vop2_video_port_regs rk3588_vop_vp0_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 0),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 0),
	.dsp_background = VOP_REG(RK3568_VP0_DSP_BG, 0xffffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 0),
	.out_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0xf, 0),
	.p2i_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 15),
	.pre_dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP0_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 20),
	.gamma_update_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 22),
	.dsp_lut_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 28),
	.standby = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31),
	.dclk_src_sel = VOP_REG(RK3568_LUT_PORT_SEL, 0x1, 30),
	.splice_en = VOP_REG(RK3568_LUT_PORT_SEL, 0x1, 16),
	.dclk_core_div = VOP_REG(RK3568_VP0_CLK_CTRL, 0x3, 0),
	.dclk_out_div = VOP_REG(RK3568_VP0_CLK_CTRL, 0x3, 2),
	.pre_scan_htiming = VOP_REG(RK3568_VP0_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP0_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP0_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP0_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3568_VP0_DSP_HTOTAL_HS_END, 0xffffffff, 0),
	.hact_st_end = VOP_REG(RK3568_VP0_DSP_HACT_ST_END, 0xffffffff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.sw_dsp_vtotal_imd = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1, 15),
	.dsp_vs_end = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP0_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.dual_channel_en = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 20),
	.dual_channel_swap = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 21),
	.hdr10_en = VOP_REG(RK3568_OVL_CTRL, 0x1, 4),
	.hdr_lut_update_en = VOP_REG(RK3568_HDR_LUT_CTRL, 0x1, 0),
	.hdr_lut_mode = VOP_REG(RK3568_HDR_LUT_CTRL, 0x1, 1),
	.hdr_lut_mst = VOP_REG(RK3568_HDR_LUT_MST, 0xffffffff, 0),
	.sdr2hdr_eotf_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 0),
	.sdr2hdr_r2r_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 1),
	.sdr2hdr_r2r_mode = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 2),
	.sdr2hdr_oetf_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 3),
	.sdr2hdr_bypass_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 8),
	.sdr2hdr_auto_gating_en = VOP_REG(RK3568_SDR2HDR_CTRL, 0x1, 9),
	.sdr2hdr_path_en = VOP_REG(RK3568_OVL_CTRL, 0x1, 5),
	.hdr2sdr_en = VOP_REG(RK3568_HDR2SDR_CTRL, 0x1, 0),
	.hdr2sdr_bypass_en = VOP_REG(RK3568_HDR2SDR_CTRL, 0x1, 8),
	.hdr2sdr_auto_gating_en = VOP_REG(RK3568_HDR2SDR_CTRL, 0x1, 9),
	.hdr2sdr_src_min = VOP_REG(RK3568_HDR2SDR_SRC_RANGE, 0x3fff, 0),
	.hdr2sdr_src_max = VOP_REG(RK3568_HDR2SDR_SRC_RANGE, 0x3fff, 16),
	.hdr2sdr_normfaceetf = VOP_REG(RK3568_HDR2SDR_NORMFACEETF, 0x7ff, 0),
	.hdr2sdr_dst_min = VOP_REG(RK3568_HDR2SDR_DST_RANGE, 0xffff, 0),
	.hdr2sdr_dst_max = VOP_REG(RK3568_HDR2SDR_DST_RANGE, 0xffff, 16),
	.hdr2sdr_normfacgamma = VOP_REG(RK3568_HDR2SDR_NORMFACCGAMMA, 0xffff, 0),
	.hdr2sdr_eetf_oetf_y0_offset = RK3568_HDR_EETF_OETF_Y0,
	.hdr2sdr_sat_y0_offset = RK3568_HDR_SAT_Y0,
	.sdr2hdr_eotf_oetf_y0_offset = RK3568_HDR_EOTF_OETF_Y0,
	.sdr2hdr_oetf_dx_pow1_offset = RK3568_HDR_OETF_DX_POW1,
	.sdr2hdr_oetf_xn1_offset = RK3568_HDR_OETF_XN1,
	.hdr_src_color_ctrl = VOP_REG(RK3568_HDR0_SRC_COLOR_CTRL, 0xffffffff, 0),
	.hdr_dst_color_ctrl = VOP_REG(RK3568_HDR0_DST_COLOR_CTRL, 0xffffffff, 0),
	.hdr_src_alpha_ctrl = VOP_REG(RK3568_HDR0_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.hdr_dst_alpha_ctrl = VOP_REG(RK3568_HDR0_DST_ALPHA_CTRL, 0xffffffff, 0),

	.bcsh_brightness = VOP_REG(RK3568_VP0_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3568_VP0_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3568_VP0_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3568_VP0_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3568_VP0_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3568_VP0_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3568_VP0_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3568_VP0_BCSH_COLOR_BAR, 0x1, 31),
	.edpi_te_en = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 28),
	.edpi_wms_hold_en = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 30),
	.edpi_wms_fs = VOP_REG(RK3568_VP0_DUAL_CHANNEL_CTRL, 0x1, 31),

	.lut_dma_rid = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0xf, 4),
	.cubic_lut_en = VOP_REG(RK3568_VP0_3D_LUT_CTRL, 0x1, 0),
	.cubic_lut_update_en = VOP_REG(RK3568_VP0_3D_LUT_CTRL, 0x1, 2),
	.cubic_lut_mst = VOP_REG(RK3568_VP0_3D_LUT_MST, 0xffffffff, 0),

	.line_flag_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 20),
	.dsp_hold_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 24),
	.almost_full_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 28),

	.color_bar_mode = VOP_REG(RK3568_VP0_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP0_COLOR_BAR_CTRL, 0x1, 0),
};

/*
 * VP1 can splice with VP0 to output hdisplay > 4096,
 * VP1 has a another HDR10 controller, but share the
 * same eotf curve with VP1.
 */
static const struct vop2_video_port_regs rk3588_vop_vp1_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 1),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 1),
	.dsp_background = VOP_REG(RK3568_VP1_DSP_BG, 0xffffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 4),
	.out_mode = VOP_REG(RK3568_VP1_DSP_CTRL, 0xf, 0),
	.p2i_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 15),
	.pre_dither_down_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP1_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 20),
	.gamma_update_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 22),
	.dsp_lut_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 28),
	.standby = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31),
	.dclk_core_div = VOP_REG(RK3568_VP1_CLK_CTRL, 0x3, 0),
	.dclk_out_div = VOP_REG(RK3568_VP1_CLK_CTRL, 0x3, 2),
	.pre_scan_htiming = VOP_REG(RK3568_VP1_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP1_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP1_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP1_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP1_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP1_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3568_VP1_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3568_VP1_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.sw_dsp_vtotal_imd = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1, 15),
	.dsp_vs_end = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP1_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP1_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP1_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP1_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.dual_channel_en = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 20),
	.dual_channel_swap = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 21),
	.hdr10_en = VOP_REG(RK3568_OVL_CTRL, 0x1, 24),
	.hdr_lut_update_en = VOP_REG(RK3568_HDR_LUT_CTRL, 0x1, 0),
	.hdr_lut_mode = VOP_REG(RK3568_HDR_LUT_CTRL, 0x1, 1),
	.hdr_lut_mst = VOP_REG(RK3568_HDR_LUT_MST, 0xffffffff, 0),
	.sdr2hdr_eotf_en = VOP_REG(RK3568_SDR2HDR_CTRL1, 0x1, 0),
	.sdr2hdr_r2r_en = VOP_REG(RK3568_SDR2HDR_CTRL1, 0x1, 1),
	.sdr2hdr_r2r_mode = VOP_REG(RK3568_SDR2HDR_CTRL1, 0x1, 2),
	.sdr2hdr_oetf_en = VOP_REG(RK3568_SDR2HDR_CTRL1, 0x1, 3),
	.sdr2hdr_bypass_en = VOP_REG(RK3568_SDR2HDR_CTRL1, 0x1, 8),
	.sdr2hdr_auto_gating_en = VOP_REG(RK3568_SDR2HDR_CTRL1, 0x1, 9),
	.sdr2hdr_path_en = VOP_REG(RK3568_OVL_CTRL, 0x1, 25),
	.hdr2sdr_en = VOP_REG(RK3568_HDR2SDR_CTRL1, 0x1, 0),
	.hdr2sdr_bypass_en = VOP_REG(RK3568_HDR2SDR_CTRL1, 0x1, 8),
	.hdr2sdr_auto_gating_en = VOP_REG(RK3568_HDR2SDR_CTRL1, 0x1, 9),
	.hdr2sdr_src_min = VOP_REG(RK3568_HDR2SDR_SRC_RANGE, 0x3fff, 0),
	.hdr2sdr_src_max = VOP_REG(RK3568_HDR2SDR_SRC_RANGE, 0x3fff, 16),
	.hdr2sdr_normfaceetf = VOP_REG(RK3568_HDR2SDR_NORMFACEETF, 0x7ff, 0),
	.hdr2sdr_dst_min = VOP_REG(RK3568_HDR2SDR_DST_RANGE, 0xffff, 0),
	.hdr2sdr_dst_max = VOP_REG(RK3568_HDR2SDR_DST_RANGE, 0xffff, 16),
	.hdr2sdr_normfacgamma = VOP_REG(RK3568_HDR2SDR_NORMFACCGAMMA, 0xffff, 0),
	.hdr2sdr_eetf_oetf_y0_offset = RK3568_HDR_EETF_OETF_Y0,
	.hdr2sdr_sat_y0_offset = RK3568_HDR_SAT_Y0,
	.sdr2hdr_eotf_oetf_y0_offset = RK3568_HDR_EOTF_OETF_Y0,
	.sdr2hdr_oetf_dx_pow1_offset = RK3568_HDR_OETF_DX_POW1,
	.sdr2hdr_oetf_xn1_offset = RK3568_HDR_OETF_XN1,
	.hdr_src_color_ctrl = VOP_REG(RK3568_HDR1_SRC_COLOR_CTRL, 0xffffffff, 0),
	.hdr_dst_color_ctrl = VOP_REG(RK3568_HDR1_DST_COLOR_CTRL, 0xffffffff, 0),
	.hdr_src_alpha_ctrl = VOP_REG(RK3568_HDR1_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.hdr_dst_alpha_ctrl = VOP_REG(RK3568_HDR1_DST_ALPHA_CTRL, 0xffffffff, 0),

	.bcsh_brightness = VOP_REG(RK3568_VP1_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3568_VP1_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3568_VP1_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3568_VP1_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3568_VP1_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3568_VP1_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3568_VP1_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3568_VP1_BCSH_COLOR_BAR, 0x1, 31),
	.edpi_te_en = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 28),
	.edpi_wms_hold_en = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 30),
	.edpi_wms_fs = VOP_REG(RK3568_VP1_DUAL_CHANNEL_CTRL, 0x1, 31),

	.lut_dma_rid = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0xf, 4),
	.cubic_lut_en = VOP_REG(RK3588_VP1_3D_LUT_CTRL, 0x1, 0),
	.cubic_lut_update_en = VOP_REG(RK3588_VP1_3D_LUT_CTRL, 0x1, 2),
	.cubic_lut_mst = VOP_REG(RK3588_VP1_3D_LUT_MST, 0xffffffff, 0),

	.line_flag_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 21),
	.dsp_hold_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 25),
	.almost_full_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 29),

	.color_bar_mode = VOP_REG(RK3568_VP1_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP1_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop2_video_port_regs rk3588_vop_vp2_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 2),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 2),
	.dsp_background = VOP_REG(RK3568_VP2_DSP_BG, 0xffffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 8),
	.out_mode = VOP_REG(RK3568_VP2_DSP_CTRL, 0xf, 0),
	.p2i_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 15),
	.pre_dither_down_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP2_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 20),
	.gamma_update_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 22),
	.dsp_lut_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 28),
	.standby = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 31),
	.dclk_src_sel = VOP_REG(RK3568_LUT_PORT_SEL, 0x1, 31),
	.dclk_core_div = VOP_REG(RK3568_VP2_CLK_CTRL, 0x3, 0),
	.dclk_out_div = VOP_REG(RK3568_VP2_CLK_CTRL, 0x3, 2),
	.pre_scan_htiming = VOP_REG(RK3568_VP2_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP2_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP2_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP2_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP2_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP2_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3568_VP2_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3568_VP2_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.dsp_vtotal = VOP_REG(RK3568_VP2_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.sw_dsp_vtotal_imd = VOP_REG(RK3568_VP2_DSP_VTOTAL_VS_END, 0x1, 15),
	.dsp_vs_end = VOP_REG(RK3568_VP2_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP2_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP2_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP2_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP2_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.dual_channel_en = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 20),
	.dual_channel_swap = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 21),
	.bcsh_brightness = VOP_REG(RK3568_VP2_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3568_VP2_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3568_VP2_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3568_VP2_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3568_VP2_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3568_VP2_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3568_VP2_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3568_VP2_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3568_VP2_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3568_VP2_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3568_VP2_BCSH_COLOR_BAR, 0x1, 31),
	.edpi_te_en = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 28),
	.edpi_wms_hold_en = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 30),
	.edpi_wms_fs = VOP_REG(RK3568_VP2_DUAL_CHANNEL_CTRL, 0x1, 31),

	.lut_dma_rid = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0xf, 4),
	.cubic_lut_en = VOP_REG(RK3588_VP2_3D_LUT_CTRL, 0x1, 0),
	.cubic_lut_update_en = VOP_REG(RK3588_VP2_3D_LUT_CTRL, 0x1, 2),
	.cubic_lut_mst = VOP_REG(RK3588_VP2_3D_LUT_MST, 0xffffffff, 0),

	.line_flag_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 22),
	.dsp_hold_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 26),
	.almost_full_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 30),

	.color_bar_mode = VOP_REG(RK3568_VP2_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3568_VP2_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop2_video_port_regs rk3588_vop_vp3_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 3),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 3),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 12),
	.dsp_background = VOP_REG(RK3588_VP3_DSP_BG, 0xffffffff, 0),
	.out_mode = VOP_REG(RK3588_VP3_DSP_CTRL, 0xf, 0),
	.p2i_en = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1f, 8),
	.dsp_x_mir_en = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 13),
	.post_dsp_out_r2y = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 15),
	.pre_dither_down_en = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3588_VP3_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 20),
	.gamma_update_en = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 22),
	.dsp_lut_en = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 28),
	.standby = VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 31),
	.dclk_src_sel = VOP_REG(RK3568_LUT_PORT_SEL, 0x1, 30),
	.dclk_core_div = VOP_REG(RK3568_VP3_CLK_CTRL, 0x3, 0),
	.dclk_out_div = VOP_REG(RK3568_VP3_CLK_CTRL, 0x3, 2),
	.pre_scan_htiming = VOP_REG(RK3588_VP3_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3588_VP3_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3588_VP3_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3588_VP3_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3588_VP3_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3588_VP3_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3588_VP3_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3588_VP3_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.dsp_vtotal = VOP_REG(RK3588_VP3_DSP_VTOTAL_VS_END, 0x1fff, 16),
	.sw_dsp_vtotal_imd = VOP_REG(RK3588_VP3_DSP_VTOTAL_VS_END, 0x1, 15),
	.dsp_vs_end = VOP_REG(RK3588_VP3_DSP_VTOTAL_VS_END, 0x1fff, 0),
	.vact_st_end = VOP_REG(RK3588_VP3_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3588_VP3_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3588_VP3_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3588_VP3_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.dual_channel_en = VOP_REG(RK3588_VP3_DUAL_CHANNEL_CTRL, 0x1, 20),
	.dual_channel_swap = VOP_REG(RK3588_VP3_DUAL_CHANNEL_CTRL, 0x1, 21),
	.bcsh_brightness = VOP_REG(RK3588_VP3_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3588_VP3_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3588_VP3_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3588_VP3_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3588_VP3_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3588_VP3_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3588_VP3_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3588_VP3_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3588_VP3_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3588_VP3_BCSH_CTRL, 0x1, 0),
	.bcsh_en = VOP_REG(RK3588_VP3_BCSH_COLOR_BAR, 0x1, 31),
	.edpi_te_en = VOP_REG(RK3588_VP3_DUAL_CHANNEL_CTRL, 0x1, 28),
	.edpi_wms_hold_en = VOP_REG(RK3588_VP3_DUAL_CHANNEL_CTRL, 0x1, 30),
	.edpi_wms_fs = VOP_REG(RK3588_VP3_DUAL_CHANNEL_CTRL, 0x1, 31),

	.line_flag_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 23),
	.dsp_hold_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 27),
	.almost_full_or_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 31),

	.color_bar_mode = VOP_REG(RK3588_VP3_COLOR_BAR_CTRL, 0x1, 1),
	.color_bar_en = VOP_REG(RK3588_VP3_COLOR_BAR_CTRL, 0x1, 0),
};

static const struct vop2_video_port_data rk3588_vop_video_ports[] = {
	{
	 .id = 0,
	 .splice_vp_id = 1,
	 .lut_dma_rid = 0xd,
	 .soc_id = { 0x3588, 0x3588 },
	 .feature = VOP_FEATURE_OUTPUT_10BIT | VOP_FEATURE_ALPHA_SCALE |
			VOP_FEATURE_HDR10 | VOP_FEATURE_NEXT_HDR,
	 .gamma_lut_len = 1024,
	 .cubic_lut_len = 729, /* 9x9x9 */
	 .dclk_max = 600000000,
	 .max_output = { 7680, 4320 },
	 /* hdr2sdr sdr2hdr hdr2hdr sdr2sdr */
	 .pre_scan_max_dly = { 76, 65, 65, 54 },
	 .intr = &rk3568_vp0_intr,
	 .hdr_table = &rk3568_vop_hdr_table,
	 .regs = &rk3588_vop_vp0_regs,
	},
	{
	 .id = 1,
	 .lut_dma_rid = 0xe,
	 .soc_id = { 0x3588, 0x3588 },
	 .feature = VOP_FEATURE_OUTPUT_10BIT | VOP_FEATURE_ALPHA_SCALE,
	 .gamma_lut_len = 1024,
	 .cubic_lut_len = 729, /* 9x9x9 */
	 .dclk_max = 600000000,
	 .max_output = { 4096, 2304 },
	 .pre_scan_max_dly = { 76, 65, 65, 54 },
	 .intr = &rk3568_vp1_intr,
	 /* vp1 share the same hdr curve with vp0 */
	 .hdr_table = &rk3568_vop_hdr_table,
	 .regs = &rk3588_vop_vp1_regs,
	},
	{
	 .id = 2,
	 .lut_dma_rid = 0xe,
	 .soc_id = { 0x3588, 0x3588 },
	 .feature = VOP_FEATURE_OUTPUT_10BIT | VOP_FEATURE_ALPHA_SCALE,
	 .gamma_lut_len = 1024,
	 .cubic_lut_len = 4913, /* 17x17x17 */
	 .dclk_max = 600000000,
	 .max_output = { 4096, 2304 },
	 .pre_scan_max_dly = { 52, 52, 52, 52 },
	 .intr = &rk3568_vp2_intr,
	 .regs = &rk3588_vop_vp2_regs,
	},
	{
	 .id = 3,
	 .soc_id = { 0x3588, 0x3588 },
	 .feature = VOP_FEATURE_ALPHA_SCALE,
	 .gamma_lut_len = 1024,
	 .dclk_max = 200000000,
	 .max_output = { 2048, 1536 },
	 .pre_scan_max_dly = { 52, 52, 52, 52 },
	 .intr = &rk3588_vp3_intr,
	 .regs = &rk3588_vop_vp3_regs,
	},
};

/*
 * HDMI/eDP infterface pixclk and dclk are independent of each other.
 * MIPI and DP interface pixclk and dclk are the same in itself.
 */
static const struct vop2_connector_if_data rk3588_conn_if_data[] = {
	{
	 .id = VOP_OUTPUT_IF_HDMI0,
	 .clk_src_name = "hdmi_edp0_clk_src",
	 .clk_parent_name = "dclk",
	 .pixclk_name = "hdmi_edp0_pixclk",
	 .dclk_name = "hdmi_edp0_dclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 4,
	 .if_div_yuv420_shift = 1,
	 .bus_div_shift = 2,
	 .pixel_clk_div_shift = 2,
	},

	{
	 .id = VOP_OUTPUT_IF_HDMI1,
	 .clk_src_name = "hdmi_edp1_clk_src",
	 .clk_parent_name = "dclk",
	 .pixclk_name = "hdmi_edp1_pixclk",
	 .dclk_name = "hdmi_edp1_dclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 4,
	 .if_div_yuv420_shift = 1,
	 .bus_div_shift = 2,
	 .pixel_clk_div_shift = 2,
	},

	{
	 .id = VOP_OUTPUT_IF_eDP0,
	 .clk_src_name = "hdmi_edp0_clk_src",
	 .clk_parent_name = "dclk",
	 .pixclk_name = "hdmi_edp0_pixclk",
	 .dclk_name = "hdmi_edp0_dclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 4,
	 .if_div_yuv420_shift = 1,
	 .bus_div_shift = 1,
	 .pixel_clk_div_shift = 1,
	},

	{
	 .id = VOP_OUTPUT_IF_eDP1,
	 .clk_src_name = "hdmi_edp1_clk_src",
	 .clk_parent_name = "dclk",
	 .pixclk_name = "hdmi_edp1_pixclk",
	 .dclk_name = "hdmi_edp1_dclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 4,
	 .if_div_yuv420_shift = 1,
	 .bus_div_shift = 1,
	 .pixel_clk_div_shift = 1,
	},

	{
	 .id = VOP_OUTPUT_IF_DP0,
	 .clk_src_name = "dp0_pixclk",
	 .clk_parent_name = "dclk_out",
	 .pixclk_name = "dp0_pixclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 1,
	 .if_div_yuv420_shift = 2,
	 .bus_div_shift = 1,
	 .pixel_clk_div_shift = 1,

	},

	{
	 .id = VOP_OUTPUT_IF_DP1,
	 .clk_src_name = "dp1_pixclk",
	 .clk_parent_name = "dclk_out",
	 .pixclk_name = "dp1_pixclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 1,
	 .if_div_yuv420_shift = 2,
	 .bus_div_shift = 1,
	 .pixel_clk_div_shift = 1,

	},

	{
	 .id = VOP_OUTPUT_IF_MIPI0,
	 .clk_src_name = "mipi0_clk_src",
	 .clk_parent_name = "dclk_out",
	 .pixclk_name = "mipi0_pixclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 1,
	 .if_div_yuv420_shift = 1,
	 .bus_div_shift = 1,
	 .pixel_clk_div_shift = 1,
	},

	{
	 .id = VOP_OUTPUT_IF_MIPI1,
	 .clk_src_name = "mipi1_clk_src",
	 .clk_parent_name = "dclk_out",
	 .pixclk_name = "mipi1_pixclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 1,
	 .if_div_yuv420_shift = 1,
	 .bus_div_shift = 1,
	 .pixel_clk_div_shift = 1,
	},

	{
	 .id = VOP_OUTPUT_IF_RGB,
	 .clk_src_name = "port3_dclk_src",
	 .clk_parent_name = "dclk",
	 .pixclk_name = "rgb_pixclk",
	 .post_proc_div_shift = 2,
	 .if_div_shift = 0,
	 .if_div_yuv420_shift = 0,
	 .bus_div_shift = 0,
	 .pixel_clk_div_shift = 0,
	},
};


const struct vop2_layer_regs rk3568_vop_layer0_regs = {
	.layer_sel = VOP_REG(RK3568_OVL_LAYER_SEL, 0x7, 0)
};

const struct vop2_layer_regs rk3568_vop_layer1_regs = {
	.layer_sel = VOP_REG(RK3568_OVL_LAYER_SEL, 0x7, 4)
};

const struct vop2_layer_regs rk3568_vop_layer2_regs = {
	.layer_sel = VOP_REG(RK3568_OVL_LAYER_SEL, 0x7, 8)
};

const struct vop2_layer_regs rk3568_vop_layer3_regs = {
	.layer_sel = VOP_REG(RK3568_OVL_LAYER_SEL, 0x7, 12)
};

const struct vop2_layer_regs rk3568_vop_layer4_regs = {
	.layer_sel = VOP_REG(RK3568_OVL_LAYER_SEL, 0x7, 16)
};

const struct vop2_layer_regs rk3568_vop_layer5_regs = {
	.layer_sel = VOP_REG(RK3568_OVL_LAYER_SEL, 0x7, 20)
};

const struct vop2_layer_regs rk3568_vop_layer6_regs = {
	.layer_sel = VOP_REG(RK3568_OVL_LAYER_SEL, 0x7, 24)
};

const struct vop2_layer_regs rk3568_vop_layer7_regs = {
	.layer_sel = VOP_REG(RK3568_OVL_LAYER_SEL, 0x7, 28)
};

static const struct vop2_layer_data rk3568_vop_layers[] = {
	{
	 .id = 0,
	 .regs = &rk3568_vop_layer0_regs,
	},

	{
	 .id = 1,
	 .regs = &rk3568_vop_layer1_regs,
	},

	{
	 .id = 2,
	 .regs = &rk3568_vop_layer2_regs,
	},

	{
	 .id = 3,
	 .regs = &rk3568_vop_layer3_regs,
	},

	{
	 .id = 4,
	 .regs = &rk3568_vop_layer4_regs,
	},

	{
	 .id = 5,
	 .regs = &rk3568_vop_layer5_regs,
	},

	{
	 .id = 6,
	 .regs = &rk3568_vop_layer6_regs,
	},

	{
	 .id = 7,
	 .regs = &rk3568_vop_layer7_regs,
	},

};

static const struct vop2_cluster_regs rk3528_vop_cluster0 = {
	.afbc_enable = VOP_REG(RK3568_CLUSTER0_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3568_CLUSTER0_CTRL, 1, 0),
	.lb_mode = VOP_REG(RK3568_CLUSTER0_CTRL, 0xf, 4),
	.scl_lb_mode = VOP_REG(RK3568_CLUSTER0_CTRL, 0x3, 9),
	.frm_reset_en = VOP_REG(RK3568_CLUSTER0_CTRL, 1, 31),
	.src_color_ctrl = VOP_REG(RK3528_CLUSTER0_MIX_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3528_CLUSTER0_MIX_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3528_CLUSTER0_MIX_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3528_CLUSTER0_MIX_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop2_cluster_regs rk3568_vop_cluster0 = {
	.afbc_enable = VOP_REG(RK3568_CLUSTER0_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3568_CLUSTER0_CTRL, 1, 0),
	.lb_mode = VOP_REG(RK3568_CLUSTER0_CTRL, 0xf, 4),
	.frm_reset_en = VOP_REG(RK3568_CLUSTER0_CTRL, 1, 31),
	.src_color_ctrl = VOP_REG(RK3568_CLUSTER0_MIX_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3568_CLUSTER0_MIX_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3568_CLUSTER0_MIX_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3568_CLUSTER0_MIX_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop2_cluster_regs rk3568_vop_cluster1 = {
	.afbc_enable = VOP_REG(RK3568_CLUSTER1_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3568_CLUSTER1_CTRL, 1, 0),
	.lb_mode = VOP_REG(RK3568_CLUSTER1_CTRL, 0xf, 4),
	.frm_reset_en = VOP_REG(RK3568_CLUSTER1_CTRL, 1, 31),
	.src_color_ctrl = VOP_REG(RK3568_CLUSTER1_MIX_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3568_CLUSTER1_MIX_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3568_CLUSTER1_MIX_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3568_CLUSTER1_MIX_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop2_cluster_regs rk3588_vop_cluster2 = {
	.afbc_enable = VOP_REG(RK3588_CLUSTER2_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3588_CLUSTER2_CTRL, 1, 0),
	.lb_mode = VOP_REG(RK3588_CLUSTER2_CTRL, 0xf, 4),
	.frm_reset_en = VOP_REG(RK3588_CLUSTER2_CTRL, 1, 31),
	.src_color_ctrl = VOP_REG(RK3588_CLUSTER2_MIX_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3588_CLUSTER2_MIX_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3588_CLUSTER2_MIX_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3588_CLUSTER2_MIX_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop2_cluster_regs rk3588_vop_cluster3 =  {
	.afbc_enable = VOP_REG(RK3588_CLUSTER3_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3588_CLUSTER3_CTRL, 1, 0),
	.lb_mode = VOP_REG(RK3588_CLUSTER3_CTRL, 0xf, 4),
	.frm_reset_en = VOP_REG(RK3588_CLUSTER3_CTRL, 1, 31),
	.src_color_ctrl = VOP_REG(RK3588_CLUSTER3_MIX_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3588_CLUSTER3_MIX_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3588_CLUSTER3_MIX_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3588_CLUSTER3_MIX_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop_afbc rk3568_cluster0_afbc = {
	.format = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_CTRL, 0x1f, 2),
	.rb_swap = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_CTRL, 0x1, 9),
	.uv_swap = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_CTRL, 0x1, 10),
	.auto_gating_en = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_OUTPUT_CTRL, 0x1, 4),
	.half_block_en = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_CTRL, 0x1, 7),
	.block_split_en = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_CTRL, 0x1, 8),
	.hdr_ptr = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_HDR_PTR, 0xffffffff, 0),
	.pic_size = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_PIC_SIZE, 0xffffffff, 0),
	.pic_vir_width = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_VIR_WIDTH, 0xffff, 0),
	.tile_num = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_VIR_WIDTH, 0xffff, 16),
	.pic_offset = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_PIC_OFFSET, 0xffffffff, 0),
	.dsp_offset = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_DSP_OFFSET, 0xffffffff, 0),
	.transform_offset = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_TRANSFORM_OFFSET, 0xffffffff, 0),
	.rotate_90 = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_ROTATE_MODE, 0x1, 0),
	.rotate_270 = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_ROTATE_MODE, 0x1, 1),
	.xmirror = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_ROTATE_MODE, 0x1, 2),
	.ymirror = VOP_REG(RK3568_CLUSTER0_WIN0_AFBCD_ROTATE_MODE, 0x1, 3),
};

static const struct vop2_scl_regs rk3528_cluster0_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_CLUSTER0_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_CLUSTER0_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.yrgb_ver_scl_mode = VOP_REG(RK3528_CLUSTER0_WIN0_CTRL1, 0x3, 14),
	.yrgb_hor_scl_mode = VOP_REG(RK3528_CLUSTER0_WIN0_CTRL1, 0x3, 22),

	.yrgb_vscl_filter_mode = VOP_REG(RK3528_CLUSTER0_WIN0_CTRL1, 0x3, 12),/* supported from vop3 */
	.yrgb_hscl_filter_mode = VOP_REG(RK3528_CLUSTER0_WIN0_CTRL1, 0x3, 20),/* supported from vop3 */

	.vsd_yrgb_gt2 = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 28),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 29),
	.vsd_cbcr_gt2 = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 30),
	.vsd_cbcr_gt4 = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 31),

	.vsd_avg2 = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 18),/* supported from vop3 */
	.vsd_avg4 = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 19),
	.xavg_en = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 27),
	.xgt_en = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 24),
	.xgt_mode = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x3, 25),
};

static const struct vop2_scl_regs rk3568_cluster0_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_CLUSTER0_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_CLUSTER0_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.yrgb_ver_scl_mode = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x3, 14),
	.yrgb_hor_scl_mode = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x3, 12),
	.bic_coe_sel = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x3, 2),
	.vsd_yrgb_gt2 = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 28),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL1, 0x1, 29),
};

static const struct vop_afbc rk3568_cluster1_afbc = {
	.format = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_CTRL, 0x1f, 2),
	.rb_swap = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_CTRL, 0x1, 9),
	.uv_swap = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_CTRL, 0x1, 10),
	.auto_gating_en = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_OUTPUT_CTRL, 0x1, 4),
	.half_block_en = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_CTRL, 0x1, 7),
	.block_split_en = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_CTRL, 0x1, 8),
	.hdr_ptr = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_HDR_PTR, 0xffffffff, 0),
	.pic_size = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_PIC_SIZE, 0xffffffff, 0),
	.pic_vir_width = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_VIR_WIDTH, 0xffff, 0),
	.tile_num = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_VIR_WIDTH, 0xffff, 16),
	.pic_offset = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_PIC_OFFSET, 0xffffffff, 0),
	.dsp_offset = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_DSP_OFFSET, 0xffffffff, 0),
	.transform_offset = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_TRANSFORM_OFFSET, 0xffffffff, 0),
	.rotate_90 = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_ROTATE_MODE, 0x1, 0),
	.rotate_270 = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_ROTATE_MODE, 0x1, 1),
	.xmirror = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_ROTATE_MODE, 0x1, 2),
	.ymirror = VOP_REG(RK3568_CLUSTER1_WIN0_AFBCD_ROTATE_MODE, 0x1, 3),
};

static const struct vop2_scl_regs rk3568_cluster1_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_CLUSTER1_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_CLUSTER1_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.yrgb_ver_scl_mode = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL1, 0x3, 14),
	.yrgb_hor_scl_mode = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL1, 0x3, 12),
	.bic_coe_sel = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL1, 0x3, 2),
	.vsd_yrgb_gt2 = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL1, 0x1, 28),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL1, 0x1, 29),
};

static const struct vop_afbc rk3588_cluster2_afbc = {
	.format = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_CTRL, 0x1f, 2),
	.rb_swap = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_CTRL, 0x1, 9),
	.uv_swap = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_CTRL, 0x1, 10),
	.auto_gating_en = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_OUTPUT_CTRL, 0x1, 4),
	.half_block_en = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_CTRL, 0x1, 7),
	.block_split_en = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_CTRL, 0x1, 8),
	.hdr_ptr = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_HDR_PTR, 0xffffffff, 0),
	.pic_size = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_PIC_SIZE, 0xffffffff, 0),
	.pic_vir_width = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_VIR_WIDTH, 0xffff, 0),
	.tile_num = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_VIR_WIDTH, 0xffff, 16),
	.pic_offset = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_PIC_OFFSET, 0xffffffff, 0),
	.dsp_offset = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_DSP_OFFSET, 0xffffffff, 0),
	.transform_offset = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_TRANSFORM_OFFSET, 0xffffffff, 0),
	.rotate_90 = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_ROTATE_MODE, 0x1, 0),
	.rotate_270 = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_ROTATE_MODE, 0x1, 1),
	.xmirror = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_ROTATE_MODE, 0x1, 2),
	.ymirror = VOP_REG(RK3588_CLUSTER2_WIN0_AFBCD_ROTATE_MODE, 0x1, 3),
};

static const struct vop2_scl_regs rk3588_cluster2_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3588_CLUSTER2_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3588_CLUSTER2_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.yrgb_ver_scl_mode = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL1, 0x3, 14),
	.yrgb_hor_scl_mode = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL1, 0x3, 12),
	.bic_coe_sel = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL1, 0x3, 2),
	.vsd_yrgb_gt2 = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL1, 0x1, 28),
	.vsd_yrgb_gt4 = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL1, 0x1, 29),
};

static const struct vop_afbc rk3588_cluster3_afbc = {
	.format = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_CTRL, 0x1f, 2),
	.rb_swap = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_CTRL, 0x1, 9),
	.uv_swap = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_CTRL, 0x1, 10),
	.auto_gating_en = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_OUTPUT_CTRL, 0x1, 4),
	.half_block_en = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_CTRL, 0x1, 7),
	.block_split_en = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_CTRL, 0x1, 8),
	.hdr_ptr = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_HDR_PTR, 0xffffffff, 0),
	.pic_size = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_PIC_SIZE, 0xffffffff, 0),
	.pic_vir_width = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_VIR_WIDTH, 0xffff, 0),
	.tile_num = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_VIR_WIDTH, 0xffff, 16),
	.pic_offset = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_PIC_OFFSET, 0xffffffff, 0),
	.dsp_offset = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_DSP_OFFSET, 0xffffffff, 0),
	.transform_offset = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_TRANSFORM_OFFSET, 0xffffffff, 0),
	.rotate_90 = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_ROTATE_MODE, 0x1, 0),
	.rotate_270 = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_ROTATE_MODE, 0x1, 1),
	.xmirror = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_ROTATE_MODE, 0x1, 2),
	.ymirror = VOP_REG(RK3588_CLUSTER3_WIN0_AFBCD_ROTATE_MODE, 0x1, 3),
};

static const struct vop2_scl_regs rk3588_cluster3_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3588_CLUSTER3_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3588_CLUSTER3_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.yrgb_ver_scl_mode = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL1, 0x3, 14),
	.yrgb_hor_scl_mode = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL1, 0x3, 12),
	.bic_coe_sel = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL1, 0x3, 2),
	.vsd_yrgb_gt2 = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL1, 0x1, 28),
	.vsd_yrgb_gt4 = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL1, 0x1, 29),
};

static const struct vop2_scl_regs rk3568_esmart_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_ESMART0_REGION0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_ESMART0_REGION0_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3568_ESMART0_REGION0_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3568_ESMART0_REGION0_SCL_FACTOR_CBR, 0xffff, 16),
	.yrgb_hor_scl_mode = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 0),
	.yrgb_hscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 2),
	.yrgb_ver_scl_mode = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 4),
	.yrgb_vscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 6),
	.cbcr_hor_scl_mode = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 8),
	.cbcr_hscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 10),
	.cbcr_ver_scl_mode = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 12),
	.cbcr_vscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 14),
	.bic_coe_sel = VOP_REG(RK3568_ESMART0_REGION0_SCL_CTRL, 0x3, 16),
	.vsd_yrgb_gt2 = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 8),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 9),
	.vsd_cbcr_gt2 = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 10),
	.vsd_cbcr_gt4 = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 11),
	.xavg_en = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 20),/* supported from vop3 */
	.xgt_en = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 21),
	.xgt_mode = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x3, 22),
};

static const struct vop2_scl_regs rk3568_area1_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_ESMART0_REGION1_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_ESMART0_REGION1_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3568_ESMART0_REGION1_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3568_ESMART0_REGION1_SCL_FACTOR_CBR, 0xffff, 16),
	.yrgb_hor_scl_mode = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 0),
	.yrgb_hscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 2),
	.yrgb_ver_scl_mode = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 4),
	.yrgb_vscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 6),
	.cbcr_hor_scl_mode = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 8),
	.cbcr_hscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 10),
	.cbcr_ver_scl_mode = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 12),
	.cbcr_vscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 14),
	.bic_coe_sel = VOP_REG(RK3568_ESMART0_REGION1_SCL_CTRL, 0x3, 16),
	.vsd_yrgb_gt2 = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 8),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 9),
	.vsd_cbcr_gt2 = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 10),
	.vsd_cbcr_gt4 = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 11),
	.xavg_en = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 20),/* supported from vop3 */
	.xgt_en = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 21),
	.xgt_mode = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x3, 22),
};

static const struct vop2_scl_regs rk3568_area2_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_ESMART0_REGION2_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_ESMART0_REGION2_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3568_ESMART0_REGION2_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3568_ESMART0_REGION2_SCL_FACTOR_CBR, 0xffff, 16),
	.yrgb_hor_scl_mode = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 0),
	.yrgb_hscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 2),
	.yrgb_ver_scl_mode = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 4),
	.yrgb_vscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 6),
	.cbcr_hor_scl_mode = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 8),
	.cbcr_hscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 10),
	.cbcr_ver_scl_mode = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 12),
	.cbcr_vscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 14),
	.bic_coe_sel = VOP_REG(RK3568_ESMART0_REGION2_SCL_CTRL, 0x3, 16),
	.vsd_yrgb_gt2 = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 8),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 9),
	.vsd_cbcr_gt2 = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 10),
	.vsd_cbcr_gt4 = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 11),
	.xavg_en = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 20),/* supported from vop3 */
	.xgt_en = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 21),
	.xgt_mode = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x3, 22),
};

static const struct vop2_scl_regs rk3568_area3_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_ESMART0_REGION3_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_ESMART0_REGION3_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3568_ESMART0_REGION3_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3568_ESMART0_REGION3_SCL_FACTOR_CBR, 0xffff, 16),
	.yrgb_hor_scl_mode = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 0),
	.yrgb_hscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 2),
	.yrgb_ver_scl_mode = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 4),
	.yrgb_vscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 6),
	.cbcr_hor_scl_mode = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 8),
	.cbcr_hscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 10),
	.cbcr_ver_scl_mode = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 12),
	.cbcr_vscl_filter_mode = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 14),
	.bic_coe_sel = VOP_REG(RK3568_ESMART0_REGION3_SCL_CTRL, 0x3, 16),
	.vsd_yrgb_gt2 = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 8),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 9),
	.vsd_cbcr_gt2 = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 10),
	.vsd_cbcr_gt4 = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 11),
	.xavg_en = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 20),/* supported from vop3 */
	.xgt_en = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 21),
	.xgt_mode = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x3, 22),
};

static const struct vop2_win_regs rk3568_area1_data = {
	.scl = &rk3568_area1_scl,
	.enable = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 0),
	.format = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1f, 1),
	.rb_swap = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 14),
	.uv_swap = VOP_REG(RK3568_ESMART0_REGION1_CTRL, 0x1, 16),
	.act_info = VOP_REG(RK3568_ESMART0_REGION1_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_ESMART0_REGION1_DSP_INFO, 0x1fff1fff, 0),
	.dsp_st = VOP_REG(RK3568_ESMART0_REGION1_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_ESMART0_REGION1_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_ESMART0_REGION1_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_ESMART0_REGION1_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_ESMART0_REGION1_VIR, 0xffff, 16),
};

static const struct vop2_win_regs rk3568_area2_data = {
	.scl = &rk3568_area2_scl,
	.enable = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 0),
	.format = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1f, 1),
	.rb_swap = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 14),
	.uv_swap = VOP_REG(RK3568_ESMART0_REGION2_CTRL, 0x1, 16),
	.act_info = VOP_REG(RK3568_ESMART0_REGION2_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_ESMART0_REGION2_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3568_ESMART0_REGION2_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_ESMART0_REGION2_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_ESMART0_REGION2_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_ESMART0_REGION2_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_ESMART0_REGION2_VIR, 0xffff, 16),
};

static const struct vop2_win_regs rk3568_area3_data = {
	.scl = &rk3568_area3_scl,
	.enable = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 0),
	.format = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1f, 1),
	.rb_swap = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 14),
	.uv_swap = VOP_REG(RK3568_ESMART0_REGION3_CTRL, 0x1, 16),
	.act_info = VOP_REG(RK3568_ESMART0_REGION3_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_ESMART0_REGION3_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3568_ESMART0_REGION3_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_ESMART0_REGION3_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_ESMART0_REGION3_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_ESMART0_REGION3_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_ESMART0_REGION3_VIR, 0xffff, 16),
};

static const struct vop2_win_regs *rk3568_area_data[] = {
	&rk3568_area1_data,
	&rk3568_area2_data,
	&rk3568_area3_data
};

static const struct vop2_win_regs rk3528_cluster0_win_data = {
	.scl = &rk3528_cluster0_win_scl,
	.afbc = &rk3568_cluster0_afbc,
	.cluster = &rk3528_vop_cluster0,
	.enable = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x3f, 1),
	.tile_mode = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 7),
	.rb_swap = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 14),
	.uv_swap = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 17),
	.dither_up = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 18),
	.act_info = VOP_REG(RK3568_CLUSTER0_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_CLUSTER0_WIN0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3568_CLUSTER0_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_CLUSTER0_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_CLUSTER0_WIN0_CBR_MST, 0xffffffff, 0),
	.yuv_clip = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 19),
	.yrgb_vir = VOP_REG(RK3568_CLUSTER0_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_CLUSTER0_WIN0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 8),
	.r2y_en = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 9),
	.csc_mode = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x7, 10),
	.axi_yrgb_id = VOP_REG(RK3528_CLUSTER0_WIN0_CTRL2, 0x1f, 0),
	.axi_uv_id = VOP_REG(RK3528_CLUSTER0_WIN0_CTRL2, 0x1f, 5),
};

static const struct vop2_win_regs rk3568_cluster0_win_data = {
	.scl = &rk3568_cluster0_win_scl,
	.afbc = &rk3568_cluster0_afbc,
	.cluster = &rk3568_vop_cluster0,
	.enable = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1f, 1),
	.rb_swap = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 14),
	.dither_up = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 18),
	.act_info = VOP_REG(RK3568_CLUSTER0_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_CLUSTER0_WIN0_DSP_INFO, 0x1fff1fff, 0),
	.dsp_st = VOP_REG(RK3568_CLUSTER0_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_CLUSTER0_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_CLUSTER0_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_CLUSTER0_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_CLUSTER0_WIN0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 8),
	.r2y_en = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 9),
	.csc_mode = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x3, 10),
	.axi_yrgb_id = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL2, 0x1f, 0),
	.axi_uv_id = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL2, 0x1f, 5),
	.axi_id = VOP_REG(RK3568_CLUSTER0_CTRL, 0x1, 13),
};

static const struct vop2_win_regs rk3568_cluster1_win_data = {
	.scl = &rk3568_cluster1_win_scl,
	.afbc = &rk3568_cluster1_afbc,
	.cluster = &rk3568_vop_cluster1,
	.enable = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1f, 1),
	.rb_swap = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 14),
	.dither_up = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 18),
	.act_info = VOP_REG(RK3568_CLUSTER1_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_CLUSTER1_WIN0_DSP_INFO, 0x1fff1fff, 0),
	.dsp_st = VOP_REG(RK3568_CLUSTER1_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_CLUSTER1_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_CLUSTER1_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_CLUSTER1_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_CLUSTER1_WIN0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 8),
	.r2y_en = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 9),
	.csc_mode = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x3, 10),
	.axi_yrgb_id = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL2, 0x1f, 0),
	.axi_uv_id = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL2, 0x1f, 5),
	.axi_id = VOP_REG(RK3568_CLUSTER1_CTRL, 0x1, 13),
};

static const struct vop2_win_regs rk3588_cluster2_win_data = {
	.scl = &rk3588_cluster2_win_scl,
	.afbc = &rk3588_cluster2_afbc,
	.cluster = &rk3588_vop_cluster2,
	.enable = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL0, 0x1f, 1),
	.rb_swap = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL0, 0x1, 14),
	.act_info = VOP_REG(RK3588_CLUSTER2_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3588_CLUSTER2_WIN0_DSP_INFO, 0x1fff1fff, 0),
	.dsp_st = VOP_REG(RK3588_CLUSTER2_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3588_CLUSTER2_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3588_CLUSTER2_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3588_CLUSTER2_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3588_CLUSTER2_WIN0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL0, 0x1, 8),
	.r2y_en = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL0, 0x1, 9),
	.csc_mode = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL0, 0x3, 10),
	.axi_yrgb_id = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL2, 0x1f, 0),
	.axi_uv_id = VOP_REG(RK3588_CLUSTER2_WIN0_CTRL2, 0x1f, 5),
	.axi_id = VOP_REG(RK3588_CLUSTER2_CTRL, 0x1, 13),
};

static const struct vop2_win_regs rk3588_cluster3_win_data = {
	.scl = &rk3588_cluster3_win_scl,
	.afbc = &rk3588_cluster3_afbc,
	.cluster = &rk3588_vop_cluster3,
	.enable = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL0, 0x1f, 1),
	.rb_swap = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL0, 0x1, 14),
	.act_info = VOP_REG(RK3588_CLUSTER3_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3588_CLUSTER3_WIN0_DSP_INFO, 0x1fff1fff, 0),
	.dsp_st = VOP_REG(RK3588_CLUSTER3_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3588_CLUSTER3_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3588_CLUSTER3_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3588_CLUSTER3_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3588_CLUSTER3_WIN0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL0, 0x1, 8),
	.r2y_en = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL0, 0x1, 9),
	.csc_mode = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL0, 0x3, 10),
	.axi_yrgb_id = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL2, 0x1f, 0),
	.axi_uv_id = VOP_REG(RK3588_CLUSTER3_WIN0_CTRL2, 0x1f, 5),
	.axi_id = VOP_REG(RK3588_CLUSTER3_CTRL, 0x1, 13),
};

static const struct vop2_win_regs rk3568_esmart_win_data = {
	.scl = &rk3568_esmart_win_scl,
	.axi_yrgb_id = VOP_REG(RK3568_ESMART0_CTRL1, 0x1f, 4),
	.axi_uv_id = VOP_REG(RK3568_ESMART0_CTRL1, 0x1f, 12),
	.axi_id = VOP_REG(RK3568_ESMART0_AXI_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 0),
	.format = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1f, 1),
	.dither_up = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 12),
	.rb_swap = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 14),
	.uv_swap = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 16),
	.act_info = VOP_REG(RK3568_ESMART0_REGION0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_ESMART0_REGION0_DSP_INFO, 0x1fff1fff, 0),
	.dsp_st = VOP_REG(RK3568_ESMART0_REGION0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_ESMART0_REGION0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_ESMART0_REGION0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_ESMART0_REGION0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_ESMART0_REGION0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_ESMART0_CTRL0, 0x1, 0),
	.r2y_en = VOP_REG(RK3568_ESMART0_CTRL0, 0x1, 1),
	.csc_mode = VOP_REG(RK3568_ESMART0_CTRL0, 0x3, 2),
	.csc_13bit_en = VOP_REG(RK3568_ESMART0_CTRL0, 0x1, 16),
	.ymirror = VOP_REG(RK3568_ESMART0_CTRL1, 0x1, 31),
	.color_key = VOP_REG(RK3568_ESMART0_COLOR_KEY_CTRL, 0x3fffffff, 0),
	.color_key_en = VOP_REG(RK3568_ESMART0_COLOR_KEY_CTRL, 0x1, 31),
	.scale_engine_num = VOP_REG(RK3568_ESMART0_CTRL0, 0x3, 12),/* supported from vop3 */
};

/*
 * RK3528 VOP with 1 Cluster win and 4 Esmart win.
 * Every Esmart win support 4 multi-region.
 * VP0 can use Cluster win and Esmart0/1/2
 * VP1 can use Esmart 2/3
 *
 * Scale filter mode:
 *
 * * Cluster:
 * * Support prescale down:
 * * H/V: gt2/avg2 or gt4/avg4
 * * After prescale down:
 *    * nearest-neighbor/bilinear/bicubic for scale up
 *    * nearest-neighbor/bilinear for scale down
 *
 * * Esmart:
 * * Support prescale down:
 * * H: gt2/avg2 or gt4/avg4
 * * V: gt2 or gt4
 * * After prescale down:
 *    * nearest-neighbor/bilinear/bicubic for scale up
 *    * nearest-neighbor/bilinear/average for scale down
 */
static const struct vop2_win_data rk3528_vop_win_data[] = {
	{
	  .name = "Esmart0-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART0,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x0,
	  .layer_sel_id = { 1, 0xff, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .hsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_AVG,/* gt or avg */
	  .vsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_GT,/* gt only */
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x06,
	  .axi_uv_id = 0x07,
	  .possible_crtcs = 0x1,/* vp0 only */
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA | WIN_FEATURE_Y2R_13BIT_DEPTH,
	},

	{
	  .name = "Esmart1-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART1,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x200,
	  .layer_sel_id = { 2, 0xff, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .hsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_AVG,/* gt or avg */
	  .vsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_GT,/* gt only */
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x08,
	  .axi_uv_id = 0x09,
	  .possible_crtcs = 0x1,/* vp0 only */
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart2-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART2,
	  .base = 0x400,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .layer_sel_id = { 3, 0, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .hsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_AVG,/* gt or avg */
	  .vsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_GT,/* gt only */
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_CURSOR,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x0a,
	  .axi_uv_id = 0x0b,
	  .possible_crtcs = 0x3,/* vp0 or vp1 */
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart3-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART3,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x600,
	  .layer_sel_id = { 0xff, 1, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .hsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_AVG,/* gt or avg */
	  .vsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_GT,/* gt only */
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x0c,
	  .axi_uv_id = 0x0d,
	  .possible_crtcs = 0x2,/* vp1 only */
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Cluster0-win0",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER0,
	  .base = 0x00,
	  .formats = formats_for_vop3_cluster,
	  .nformats = ARRAY_SIZE(formats_for_vop3_cluster),
	  .format_modifiers = format_modifiers_afbc_tiled,
	  .layer_sel_id = { 0, 0xff, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
			   DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .hsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_AVG,/* gt or avg */
	  .vsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_AVG,/* gt or avg */
	  .regs = &rk3528_cluster0_win_data,
	  .axi_yrgb_id = 0x02,
	  .axi_uv_id = 0x03,
	  .possible_crtcs = 0x1,/* vp0 only */
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 27, 21 },
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_MAIN | WIN_FEATURE_Y2R_13BIT_DEPTH,
	},

	{
	  .name = "Cluster0-win1",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER0,
	  .base = 0x80,
	  .layer_sel_id = { 0, 0xff, 0xff, 0xff },
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc_tiled,
	  .supported_rotations = DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .hsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_AVG,/* gt or avg */
	  .vsd_pre_filter_mode = VOP3_PRE_SCALE_DOWN_AVG,/* gt or avg */
	  .regs = &rk3528_cluster0_win_data,
	  .axi_yrgb_id = 0x04,
	  .axi_uv_id = 0x05,
	  .possible_crtcs = 0x1,/* vp0 only */
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_SUB,
	},
};

/*
 * RK3562 VOP with 4 Esmart win.
 * Every Esmart win support 4 multi-region and each Esmart win can by used by VP0 or VP1
 *
 * Scale filter mode:
 *
 * * Esmart:
 * * Support prescale down:
 * * H: gt2/avg2 or gt4/avg4
 * * V: gt2 or gt4
 * * After prescale down:
 *	* nearest-neighbor/bilinear/bicubic for scale up
 *	* nearest-neighbor/bilinear/average for scale down
 */
static const struct vop2_win_data rk3562_vop_win_data[] = {
	{
	  .name = "Esmart0-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART0,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x0,
	  .layer_sel_id = { 0, 0, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x02,
	  .axi_uv_id = 0x03,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart1-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART1,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x200,
	  .layer_sel_id = { 1, 1, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x04,
	  .axi_uv_id = 0x05,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart2-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART2,
	  .base = 0x400,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .layer_sel_id = { 2, 2, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x06,
	  .axi_uv_id = 0x07,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart3-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART3,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x600,
	  .layer_sel_id = { 3, 3, 0xff, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x08,
	  .axi_uv_id = 0x0d,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 27, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},
};

/*
 * rk3568 vop with 2 cluster, 2 esmart win, 2 smart win.
 * Every cluster can work as 4K win or split into two win.
 * All win in cluster support AFBCD.
 *
 * Every esmart win and smart win support 4 Multi-region.
 *
 * Scale filter mode:
 *
 * * Cluster:  bicubic for horizontal scale up, others use bilinear
 * * ESmart:
 *    * nearest-neighbor/bilinear/bicubic for scale up
 *    * nearest-neighbor/bilinear/average for scale down
 *
 *
 * @TODO describe the wind like cpu-map dt nodes;
 */
static const struct vop2_win_data rk3568_vop_win_data[] = {
	{
	  .name = "Smart0-win0",
	  .phys_id = ROCKCHIP_VOP2_SMART0,
	  .base = 0x400,
	  .formats = formats_for_smart,
	  .nformats = ARRAY_SIZE(formats_for_smart),
	  .format_modifiers = format_modifiers,
	  .layer_sel_id = { 3, 3, 3, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 20, 47, 41 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Smart1-win0",
	  .phys_id = ROCKCHIP_VOP2_SMART1,
	  .formats = formats_for_smart,
	  .nformats = ARRAY_SIZE(formats_for_smart),
	  .format_modifiers = format_modifiers,
	  .base = 0x600,
	  .layer_sel_id = { 7, 7, 7, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 20, 47, 41 },
	  .feature = WIN_FEATURE_MIRROR | WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart1-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART1,
	  .formats = formats_for_rk356x_esmart,
	  .nformats = ARRAY_SIZE(formats_for_rk356x_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x200,
	  .layer_sel_id = { 6, 6, 6, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 20, 47, 41 },
	  .feature = WIN_FEATURE_MIRROR | WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart0-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART0,
	  .formats = formats_for_rk356x_esmart,
	  .nformats = ARRAY_SIZE(formats_for_rk356x_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x0,
	  .layer_sel_id = { 2, 2, 2, 0xff },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 20, 47, 41 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Cluster0-win0",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER0,
	  .base = 0x00,
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc_no_linear_mode,
	  .layer_sel_id = { 0, 0, 0, 0xff },
	  .supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
				 DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_cluster0_win_data,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .dly = { 0, 27, 21 },
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_MAIN,
	},

	{
	  .name = "Cluster0-win1",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER0,
	  .base = 0x80,
	  .layer_sel_id = { 0xff, 0xff, 0xff, 0xff },
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc_no_linear_mode,
	  .supported_rotations = DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_cluster0_win_data,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_SUB,
	},

	{
	  .name = "Cluster1-win0",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER1,
	  .base = 0x00,
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc_no_linear_mode,
	  .layer_sel_id = { 1, 1, 1, 0xff },
	  .supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
				 DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_cluster1_win_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .dly = { 0, 27, 21 },
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_MAIN | WIN_FEATURE_MIRROR,
	},

	{
	  .name = "Cluster1-win1",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER1,
	  .layer_sel_id = { 0xff, 0xff, 0xff, 0xff },
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc_no_linear_mode,
	  .base = 0x80,
	  .supported_rotations = DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_cluster1_win_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_SUB | WIN_FEATURE_MIRROR,
	},
};

const struct vop2_power_domain_regs rk3588_cluster0_pd_regs = {
	.pd = VOP_REG(RK3568_SYS_PD_CTRL, 0x1, 0),
	.status = VOP_REG(RK3568_SYS_STATUS0, 0x1, 8),
	.pmu_status = VOP_REG(RK3588_PMU_BISR_STATUS5, 0x1, 9),
	.bisr_en_status = VOP_REG(RK3588_PMU_BISR_CON3, 0x1, 9),
};

const struct vop2_power_domain_regs rk3588_cluster1_pd_regs = {
	.pd = VOP_REG(RK3568_SYS_PD_CTRL, 0x1, 1),
	.status = VOP_REG(RK3568_SYS_STATUS0, 0x1, 9),
	.pmu_status = VOP_REG(RK3588_PMU_BISR_STATUS5, 0x1, 10),
	.bisr_en_status = VOP_REG(RK3588_PMU_BISR_CON3, 0x1, 10),
};

const struct vop2_power_domain_regs rk3588_cluster2_pd_regs = {
	.pd = VOP_REG(RK3568_SYS_PD_CTRL, 0x1, 2),
	.status = VOP_REG(RK3568_SYS_STATUS0, 0x1, 10),
	.pmu_status = VOP_REG(RK3588_PMU_BISR_STATUS5, 0x1, 11),
	.bisr_en_status = VOP_REG(RK3588_PMU_BISR_CON3, 0x1, 11),
};

const struct vop2_power_domain_regs rk3588_cluster3_pd_regs = {
	.pd = VOP_REG(RK3568_SYS_PD_CTRL, 0x1, 3),
	.status = VOP_REG(RK3568_SYS_STATUS0, 0x1, 11),
	.pmu_status = VOP_REG(RK3588_PMU_BISR_STATUS5, 0x1, 12),
	.bisr_en_status = VOP_REG(RK3588_PMU_BISR_CON3, 0x1, 12),
};

const struct vop2_power_domain_regs rk3588_esmart_pd_regs = {
	.pd = VOP_REG(RK3568_SYS_PD_CTRL, 0x1, 7),
	.status = VOP_REG(RK3568_SYS_STATUS0, 0x1, 15),
	.pmu_status = VOP_REG(RK3588_PMU_BISR_STATUS5, 0x1, 15),
	.bisr_en_status = VOP_REG(RK3588_PMU_BISR_CON3, 0x1, 15),
};

const struct vop2_power_domain_regs rk3588_dsc_8k_pd_regs = {
	.pd = VOP_REG(RK3568_SYS_PD_CTRL, 0x1, 5),
	.status = VOP_REG(RK3568_SYS_STATUS0, 0x1, 13),
	.pmu_status = VOP_REG(RK3588_PMU_BISR_STATUS5, 0x1, 13),
	.bisr_en_status = VOP_REG(RK3588_PMU_BISR_CON3, 0x1, 13),
};

const struct vop2_power_domain_regs rk3588_dsc_4k_pd_regs = {
	.pd = VOP_REG(RK3568_SYS_PD_CTRL, 0x1, 6),
	.status = VOP_REG(RK3568_SYS_STATUS0, 0x1, 14),
	.pmu_status = VOP_REG(RK3588_PMU_BISR_STATUS5, 0x1, 14),
	.bisr_en_status = VOP_REG(RK3588_PMU_BISR_CON3, 0x1, 14),
};

/*
 * There are 7 internal power domains on rk3588 vop,
 * Cluster0/1/2/3 each have on pd, and PD_CLUSTER0 as parent,
 * that means PD_CLUSTER0 should turn on first before
 * PD_CLUSTER1/2/3 turn on.
 *
 * Esmart1/2/3 share one pd PD_ESMART, and Esmart0 has no PD
 * DSC_8K/DSC_4K each have on pd.
 */
static const struct vop2_power_domain_data rk3588_vop_pd_data[] = {
	{
	  .id = VOP2_PD_CLUSTER0,
	  .module_id_mask = BIT(ROCKCHIP_VOP2_CLUSTER0),
	  .regs = &rk3588_cluster0_pd_regs,
	},

	{
	  .id = VOP2_PD_CLUSTER1,
	  .module_id_mask = BIT(ROCKCHIP_VOP2_CLUSTER1),
	  .parent_id = VOP2_PD_CLUSTER0,
	  .regs = &rk3588_cluster1_pd_regs,
	},

	{
	  .id = VOP2_PD_CLUSTER2,
	  .module_id_mask = BIT(ROCKCHIP_VOP2_CLUSTER2),
	  .parent_id = VOP2_PD_CLUSTER0,
	  .regs = &rk3588_cluster2_pd_regs,
	},

	{
	  .id = VOP2_PD_CLUSTER3,
	  .module_id_mask = BIT(ROCKCHIP_VOP2_CLUSTER3),
	  .parent_id = VOP2_PD_CLUSTER0,
	  .regs = &rk3588_cluster3_pd_regs,
	},

	{
	  .id = VOP2_PD_ESMART,
	  .module_id_mask = BIT(ROCKCHIP_VOP2_ESMART1) |
			    BIT(ROCKCHIP_VOP2_ESMART2) |
			    BIT(ROCKCHIP_VOP2_ESMART3),
	  .regs = &rk3588_esmart_pd_regs,
	},

	{
	  .id = VOP2_PD_DSC_8K,
	  .module_id_mask = BIT(ROCKCHIP_VOP2_DSC_8K),
	  .regs = &rk3588_dsc_8k_pd_regs,
	},

	{
	  .id = VOP2_PD_DSC_4K,
	  .module_id_mask = BIT(ROCKCHIP_VOP2_DSC_4K),
	  .regs = &rk3588_dsc_4k_pd_regs,
	},
};

const struct vop2_power_domain_regs rk3588_mem_pg_vp0_regs = {
	.pd = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_CON1, 0x1, 15),
	.status = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_STATUS, 0x1, 19),
};

const struct vop2_power_domain_regs rk3588_mem_pg_vp1_regs = {
	.pd = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_CON2, 0x1, 0),
	.status = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_STATUS, 0x1, 20),
};

const struct vop2_power_domain_regs rk3588_mem_pg_vp2_regs = {
	.pd = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_CON2, 0x1, 1),
	.status = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_STATUS, 0x1, 21),
};

const struct vop2_power_domain_regs rk3588_mem_pg_vp3_regs = {
	.pd = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_CON2, 0x1, 2),
	.status = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_STATUS, 0x1, 22),
};

const struct vop2_power_domain_regs rk3588_mem_pg_db0_regs = {
	.pd = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_CON2, 0x1, 3),
	.status = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_STATUS, 0x1, 23),
};

const struct vop2_power_domain_regs rk3588_mem_pg_db1_regs = {
	.pd = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_CON2, 0x1, 4),
	.status = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_STATUS, 0x1, 24),
};

const struct vop2_power_domain_regs rk3588_mem_pg_db2_regs = {
	.pd = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_CON2, 0x1, 5),
	.status = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_STATUS, 0x1, 25),
};

const struct vop2_power_domain_regs rk3588_mem_pg_wb_regs = {
	.pd = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_CON2, 0x1, 6),
	.status = VOP_REG(RK3588_PMU_SUBMEM_PWR_GATE_STATUS, 0x1, 26),
};

/*
 * All power gates will power on when PD_VOP is turn on.
 * Corresponding mem_pwr_ack_bypass bit should be enabled
 * if power gate powe down before PD_VOP.
 * power gates take effect immediately, this means there
 * is no synchronization between vop frame scanout, so
 * we can only enable a power gate before we enable
 * a module, and turn off power gate after the module
 * is actually disabled.
 */
static const struct vop2_power_domain_data rk3588_vop_mem_pg_data[] = {
	{
	  .id = VOP2_MEM_PG_VP0,
	  .regs = &rk3588_mem_pg_vp0_regs,
	},

	{
	  .id = VOP2_MEM_PG_VP1,
	  .regs = &rk3588_mem_pg_vp1_regs,
	},

	{
	  .id = VOP2_MEM_PG_VP2,
	  .regs = &rk3588_mem_pg_vp2_regs,
	},

	{
	  .id = VOP2_MEM_PG_VP3,
	  .regs = &rk3588_mem_pg_vp3_regs,
	},

	{
	  .id = VOP2_MEM_PG_DB0,
	  .regs = &rk3588_mem_pg_db0_regs,
	},

	{
	  .id = VOP2_MEM_PG_DB1,
	  .regs = &rk3588_mem_pg_db1_regs,
	},

	{
	  .id = VOP2_MEM_PG_DB2,
	  .regs = &rk3588_mem_pg_db2_regs,
	},

	{
	  .id = VOP2_MEM_PG_WB,
	  .regs = &rk3588_mem_pg_wb_regs,
	},
};

/*
 * rk3588 vop with 4 cluster, 4 esmart win.
 * Every cluster can work as 4K win or split into two win.
 * All win in cluster support AFBCD.
 *
 * Every esmart win and smart win support 4 Multi-region.
 *
 * Scale filter mode:
 *
 * * Cluster:  bicubic for horizontal scale up, others use bilinear
 * * ESmart:
 *    * nearest-neighbor/bilinear/bicubic for scale up
 *    * nearest-neighbor/bilinear/average for scale down
 *
 * AXI Read ID assignment:
 * Two AXI bus:
 * AXI0 is a read/write bus with a higher performance.
 * AXI1 is a read only bus.
 *
 * Every window on a AXI bus must assigned two unique
 * read id(yrgb_id/uv_id, valid id are 0x1~0xe).
 *
 * AXI0:
 * Cluster0/1, Esmart0/1, WriteBack
 *
 * AXI 1:
 * Cluster2/3, Esmart2/3
 *
 * @TODO describe the wind like cpu-map dt nodes;
 */
static const struct vop2_win_data rk3588_vop_win_data[] = {
	{
	  .name = "Cluster0-win0",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER0,
	  .splice_win_id = ROCKCHIP_VOP2_CLUSTER1,
	  .base = 0x00,
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc,
	  .layer_sel_id = { 0, 0, 0, 0 },
	  .supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
				 DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_cluster0_win_data,
	  .pd_id = VOP2_PD_CLUSTER0,
	  .axi_id = 0,
	  .axi_yrgb_id = 2,
	  .axi_uv_id = 3,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .dly = { 4, 26, 29 },
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_MAIN | WIN_FEATURE_SPLICE_LEFT,
	},

	{
	  .name = "Cluster0-win1",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER0,
	  .base = 0x80,
	  .layer_sel_id = { 0xff, 0xff, 0xff, 0xff },
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc,
	  .supported_rotations = DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_cluster0_win_data,
	  .axi_id = 0,
	  .axi_yrgb_id = 4,
	  .axi_uv_id = 5,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_SUB,
	},

	{
	  .name = "Cluster1-win0",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER1,
	  .base = 0x00,
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc,
	  .layer_sel_id = { 1, 1, 1, 1 },
	  .supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
				 DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_cluster1_win_data,
	  .pd_id = VOP2_PD_CLUSTER1,
	  .axi_id = 0,
	  .axi_yrgb_id = 6,
	  .axi_uv_id = 7,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .dly = { 4, 26, 29 },
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_MAIN,
	},

	{
	  .name = "Cluster1-win1",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER1,
	  .layer_sel_id = { 0xff, 0xff, 0xff, 0xff },
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc,
	  .base = 0x80,
	  .supported_rotations = DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_cluster1_win_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .axi_id = 0,
	  .axi_yrgb_id = 8,
	  .axi_uv_id = 9,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_SUB,
	},

	{
	  .name = "Cluster2-win0",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER2,
	  .pd_id = VOP2_PD_CLUSTER2,
	  .splice_win_id = ROCKCHIP_VOP2_CLUSTER3,
	  .base = 0x00,
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc,
	  .layer_sel_id = { 4, 4, 4, 4 },
	  .supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
				 DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3588_cluster2_win_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .axi_id = 1,
	  .axi_yrgb_id = 2,
	  .axi_uv_id = 3,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .dly = { 4, 26, 29 },
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_MAIN | WIN_FEATURE_SPLICE_LEFT,
	},

	{
	  .name = "Cluster2-win1",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER2,
	  .layer_sel_id = { 0xff, 0xff, 0xff, 0xff },
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc,
	  .base = 0x80,
	  .supported_rotations = DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3588_cluster2_win_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .axi_id = 1,
	  .axi_yrgb_id = 4,
	  .axi_uv_id = 5,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_SUB,
	},

	{
	  .name = "Cluster3-win0",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER3,
	  .pd_id = VOP2_PD_CLUSTER3,
	  .base = 0x00,
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc,
	  .layer_sel_id = { 5, 5, 5, 5 },
	  .supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
				 DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3588_cluster3_win_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .axi_id = 1,
	  .axi_yrgb_id = 6,
	  .axi_uv_id = 7,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .dly = { 4, 26, 29 },
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_MAIN,
	},

	{
	  .name = "Cluster3-win1",
	  .phys_id = ROCKCHIP_VOP2_CLUSTER3,
	  .layer_sel_id = { 0xff, 0xff, 0xff, 0xff },
	  .formats = formats_for_cluster,
	  .nformats = ARRAY_SIZE(formats_for_cluster),
	  .format_modifiers = format_modifiers_afbc,
	  .base = 0x80,
	  .supported_rotations = DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3588_cluster3_win_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .axi_id = 1,
	  .axi_yrgb_id = 8,
	  .axi_uv_id = 9,
	  .max_upscale_factor = 4,
	  .max_downscale_factor = 4,
	  .feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER_SUB,
	},

	{
	  .name = "Esmart0-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART0,
	  .splice_win_id = ROCKCHIP_VOP2_ESMART1,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x0,
	  .layer_sel_id = { 2, 2, 2, 2 },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x0a,
	  .axi_uv_id = 0x0b,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 23, 45, 48 },
	  .feature = WIN_FEATURE_SPLICE_LEFT | WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart2-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART2,
	  .pd_id = VOP2_PD_ESMART,
	  .splice_win_id = ROCKCHIP_VOP2_ESMART3,
	  .base = 0x400,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .layer_sel_id = { 6, 6, 6, 6 },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .axi_id = 1,
	  .axi_yrgb_id = 0x0a,
	  .axi_uv_id = 0x0b,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 23, 45, 48 },
	  .feature = WIN_FEATURE_SPLICE_LEFT | WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart1-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART1,
	  .pd_id = VOP2_PD_ESMART,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x200,
	  .layer_sel_id = { 3, 3, 3, 3 },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .axi_id = 0,
	  .axi_yrgb_id = 0x0c,
	  .axi_uv_id = 0x01,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 23, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},

	{
	  .name = "Esmart3-win0",
	  .phys_id = ROCKCHIP_VOP2_ESMART3,
	  .pd_id = VOP2_PD_ESMART,
	  .formats = formats_for_esmart,
	  .nformats = ARRAY_SIZE(formats_for_esmart),
	  .format_modifiers = format_modifiers,
	  .base = 0x600,
	  .layer_sel_id = { 7, 7, 7, 7 },
	  .supported_rotations = DRM_MODE_REFLECT_Y,
	  .hsu_filter_mode = VOP2_SCALE_UP_BIC,
	  .hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .vsu_filter_mode = VOP2_SCALE_UP_BIL,
	  .vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
	  .regs = &rk3568_esmart_win_data,
	  .area = rk3568_area_data,
	  .area_size = ARRAY_SIZE(rk3568_area_data),
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .axi_id = 1,
	  .axi_yrgb_id = 0x0c,
	  .axi_uv_id = 0x0d,
	  .max_upscale_factor = 8,
	  .max_downscale_factor = 8,
	  .dly = { 23, 45, 48 },
	  .feature = WIN_FEATURE_MULTI_AREA,
	},
};

static const struct vop2_ctrl rk3528_vop_ctrl = {
	.cfg_done_en = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 15),
	.wb_cfg_done = VOP_REG_MASK(RK3568_REG_CFG_DONE, 0x1, 14),
	.auto_gating_en = VOP_REG(RK3568_SYS_AUTO_GATING_CTRL, 0x1, 31),
	.aclk_pre_auto_gating_en = VOP_REG(RK3568_SYS_AUTO_GATING_CTRL, 0x1, 7),
	.if_ctrl_cfg_done_imd = VOP_REG(RK3568_DSP_IF_POL, 0x1, 28),
	.version = VOP_REG(RK3568_VERSION_INFO, 0xffff, 16),
	.lut_dma_en = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0x1, 0),
	.dsp_vs_t_sel = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0x1, 16),
	.rgb_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 0),
	.hdmi0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 1),
	.bt656_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 7),
	.rgb_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 8),
	.hdmi0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 10),
	.bt656_yc_swap = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 5),
	.bt656_dclk_pol = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 6),
	.hdmi_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 4),
	.hdmi_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 7),
	.esmart_lb_mode = VOP_REG(RK3568_LUT_PORT_SEL, 0x3, 26),
	.win_vp_id[ROCKCHIP_VOP2_CLUSTER0] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 0),
	.win_vp_id[ROCKCHIP_VOP2_ESMART0] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 16),
	.win_vp_id[ROCKCHIP_VOP2_ESMART1] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 20),
	.win_vp_id[ROCKCHIP_VOP2_ESMART2] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 24),
	.win_vp_id[ROCKCHIP_VOP2_ESMART3] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 28),
	.win_dly[ROCKCHIP_VOP2_CLUSTER0] = VOP_REG(RK3528_OVL_SYS_CLUSTER0_CTRL, 0xffff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART0] = VOP_REG(RK3528_OVL_SYS_ESMART0_CTRL, 0xff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART1] = VOP_REG(RK3528_OVL_SYS_ESMART1_CTRL, 0xff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART2] = VOP_REG(RK3528_OVL_SYS_ESMART2_CTRL, 0xff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART3] = VOP_REG(RK3528_OVL_SYS_ESMART3_CTRL, 0xff, 0),
};

static const struct vop_grf_ctrl rk3562_sys_grf_ctrl = {
	.grf_bt656_clk_inv = VOP_REG(RK3562_GRF_IOC_VO_IO_CON, 0x1, 3),
	.grf_bt1120_clk_inv = VOP_REG(RK3562_GRF_IOC_VO_IO_CON, 0x1, 3),
	.grf_dclk_inv = VOP_REG(RK3562_GRF_IOC_VO_IO_CON, 0x1, 3),
};

static const struct vop2_ctrl rk3562_vop_ctrl = {
	.cfg_done_en = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 15),
	.wb_cfg_done = VOP_REG_MASK(RK3568_REG_CFG_DONE, 0x1, 14),
	.auto_gating_en = VOP_REG(RK3568_SYS_AUTO_GATING_CTRL, 0x1, 31),
	.aclk_pre_auto_gating_en = VOP_REG(RK3568_SYS_AUTO_GATING_CTRL, 0x1, 7),
	.if_ctrl_cfg_done_imd = VOP_REG(RK3568_DSP_IF_POL, 0x1, 28),
	.version = VOP_REG(RK3568_VERSION_INFO, 0xffff, 16),
	.lut_dma_en = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0x1, 0),
	.rgb_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 0),
	.mipi0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 4),
	.lvds0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 5),
	.bt1120_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 6),
	.bt656_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 7),
	.rgb_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 8),
	.mipi0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 16),
	.lvds0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 18),
	.bt656_yc_swap = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 5),
	.bt656_dclk_pol = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 6),
	.bt1120_yc_swap = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 9),
	.bt1120_dclk_pol = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 10),
	.rgb_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 0),
	.lvds_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 0),
	.lvds_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 3),
	.mipi_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 12),
	.mipi_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 15),
	.gamma_port_sel = VOP_REG(RK3568_LUT_PORT_SEL, 0x3, 12),
	.esmart_lb_mode = VOP_REG(RK3568_LUT_PORT_SEL, 0x3, 26),
	.win_vp_id[ROCKCHIP_VOP2_ESMART0] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 16),
	.win_vp_id[ROCKCHIP_VOP2_ESMART1] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 20),
	.win_vp_id[ROCKCHIP_VOP2_ESMART2] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 24),
	.win_vp_id[ROCKCHIP_VOP2_ESMART3] = VOP_REG(RK3528_OVL_SYS_PORT_SEL_IMD, 0x3, 28),
	.win_dly[ROCKCHIP_VOP2_ESMART0] = VOP_REG(RK3528_OVL_SYS_ESMART0_CTRL, 0xff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART1] = VOP_REG(RK3528_OVL_SYS_ESMART1_CTRL, 0xff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART2] = VOP_REG(RK3528_OVL_SYS_ESMART2_CTRL, 0xff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART3] = VOP_REG(RK3528_OVL_SYS_ESMART3_CTRL, 0xff, 0),
};

static const struct vop_grf_ctrl rk3568_sys_grf_ctrl = {
	.grf_bt656_clk_inv = VOP_REG(RK3568_GRF_VO_CON1, 0x1, 1),
	.grf_bt1120_clk_inv = VOP_REG(RK3568_GRF_VO_CON1, 0x1, 2),
	.grf_dclk_inv = VOP_REG(RK3568_GRF_VO_CON1, 0x1, 3),
};

static const struct vop2_ctrl rk3568_vop_ctrl = {
	.cfg_done_en = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 15),
	.wb_cfg_done = VOP_REG_MASK(RK3568_REG_CFG_DONE, 0x1, 14),
	.auto_gating_en = VOP_REG(RK3568_SYS_AUTO_GATING_CTRL, 0x1, 31),
	.ovl_cfg_done_port = VOP_REG(RK3568_OVL_CTRL, 0x3, 30),
	.ovl_port_mux_cfg_done_imd = VOP_REG(RK3568_OVL_CTRL, 0x1, 28),
	.ovl_port_mux_cfg = VOP_REG(RK3568_OVL_PORT_SEL, 0xffff, 0),
	.if_ctrl_cfg_done_imd = VOP_REG(RK3568_DSP_IF_POL, 0x1, 28),
	.version = VOP_REG(RK3568_VERSION_INFO, 0xffff, 16),
	.lut_dma_en = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0x1, 0),
	.src_color_ctrl = VOP_REG(RK3568_MIX0_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3568_MIX0_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3568_MIX0_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3568_MIX0_DST_ALPHA_CTRL, 0xffffffff, 0),
	.rgb_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 0),
	.hdmi0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 1),
	.edp0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 3),
	.mipi0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 4),
	.mipi1_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 20),
	.lvds0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 5),
	.lvds1_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 24),
	.bt1120_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 6),
	.bt656_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 7),
	.rgb_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 8),
	.hdmi0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 10),
	.edp0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 14),
	.mipi0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 16),
	.mipi1_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 21),
	.lvds0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 18),
	.lvds1_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 25),
	.lvds_dual_en = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 0),
	.lvds_dual_mode = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 1),
	.lvds_dual_channel_swap = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 2),
	.bt656_yc_swap = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 5),
	.bt1120_yc_swap = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 9),
	.gamma_port_sel = VOP_REG(RK3568_LUT_PORT_SEL, 0x3, 0),
	.rgb_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 0),
	.lvds_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 0),
	.lvds_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 3),
	.hdmi_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 4),
	.hdmi_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 7),
	.edp_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x3, 12),
	.edp_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 15),
	.mipi_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 16),
	.mipi_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 19),
	.win_vp_id[ROCKCHIP_VOP2_CLUSTER0] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 16),
	.win_vp_id[ROCKCHIP_VOP2_CLUSTER1] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 18),
	.win_vp_id[ROCKCHIP_VOP2_ESMART0] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 24),
	.win_vp_id[ROCKCHIP_VOP2_ESMART1] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 26),
	.win_vp_id[ROCKCHIP_VOP2_SMART0] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 28),
	.win_vp_id[ROCKCHIP_VOP2_SMART1] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 30),
	.win_dly[ROCKCHIP_VOP2_CLUSTER0] = VOP_REG(RK3568_CLUSTER_DLY_NUM, 0xffff, 0),
	.win_dly[ROCKCHIP_VOP2_CLUSTER1] = VOP_REG(RK3568_CLUSTER_DLY_NUM, 0xffff, 16),
	.win_dly[ROCKCHIP_VOP2_ESMART0] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART1] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 8),
	.win_dly[ROCKCHIP_VOP2_SMART0] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 16),
	.win_dly[ROCKCHIP_VOP2_SMART1] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 24),
	.otp_en = VOP_REG(RK3568_OTP_WIN_EN, 0x1, 0),
};

static const struct vop_grf_ctrl rk3588_sys_grf_ctrl = {
	.grf_bt656_clk_inv = VOP_REG(RK3588_GRF_SOC_CON1, 0x1, 14),
	.grf_bt1120_clk_inv = VOP_REG(RK3588_GRF_SOC_CON1, 0x1, 14),
	.grf_dclk_inv = VOP_REG(RK3588_GRF_SOC_CON1, 0x1, 14),
};

static const struct vop_grf_ctrl rk3588_vop_grf_ctrl = {
	.grf_edp0_en = VOP_REG(RK3588_GRF_VOP_CON2, 0x1, 0),
	.grf_hdmi0_en = VOP_REG(RK3588_GRF_VOP_CON2, 0x1, 1),
	.grf_hdmi0_dsc_en = VOP_REG(RK3588_GRF_VOP_CON2, 0x1, 2),
	.grf_edp1_en = VOP_REG(RK3588_GRF_VOP_CON2, 0x1, 3),
	.grf_hdmi1_en = VOP_REG(RK3588_GRF_VOP_CON2, 0x1, 4),
	.grf_hdmi1_dsc_en = VOP_REG(RK3588_GRF_VOP_CON2, 0x1, 4),
};

static const struct vop_grf_ctrl rk3588_vo1_grf_ctrl = {
	.grf_hdmi0_pin_pol = VOP_REG(RK3588_GRF_VO1_CON0, 0x3, 5),
	.grf_hdmi1_pin_pol = VOP_REG(RK3588_GRF_VO1_CON0, 0x3, 7),
};

static const struct vop2_ctrl rk3588_vop_ctrl = {
	.cfg_done_en = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 15),
	.wb_cfg_done = VOP_REG_MASK(RK3568_REG_CFG_DONE, 0x1, 14),
	.auto_gating_en = VOP_REG(RK3568_SYS_AUTO_GATING_CTRL, 0x1, 31),
	.dma_finish_mode = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x3, 0),
	.axi_dma_finish_and_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 2),
	.wb_dma_finish_and_en = VOP_REG(RK3588_SYS_VAR_FREQ_CTRL, 0x1, 3),
	.ovl_cfg_done_port = VOP_REG(RK3568_OVL_CTRL, 0x3, 30),
	.ovl_port_mux_cfg_done_imd = VOP_REG(RK3568_OVL_CTRL, 0x1, 28),
	.ovl_port_mux_cfg = VOP_REG(RK3568_OVL_PORT_SEL, 0xffff, 0),
	.if_ctrl_cfg_done_imd = VOP_REG(RK3568_DSP_IF_POL, 0x1, 28),
	.version = VOP_REG(RK3568_VERSION_INFO, 0xffff, 16),
	.lut_dma_en = VOP_REG(RK3568_SYS_AXI_LUT_CTRL, 0x1, 0),
	.src_color_ctrl = VOP_REG(RK3568_MIX0_SRC_COLOR_CTRL, 0xffffffff, 0),
	.dst_color_ctrl = VOP_REG(RK3568_MIX0_DST_COLOR_CTRL, 0xffffffff, 0),
	.src_alpha_ctrl = VOP_REG(RK3568_MIX0_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctrl = VOP_REG(RK3568_MIX0_DST_ALPHA_CTRL, 0xffffffff, 0),
	.dp0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 0),
	.dp1_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 1),
	.edp0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 2),
	.hdmi0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 3),
	.edp1_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 4),
	.hdmi1_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 5),
	.mipi0_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 6),
	.mipi1_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 7),
	.bt1120_en = VOP_REG(RK3568_DSP_IF_EN, 0x3, 8),
	.bt656_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 9),
	.rgb_en = VOP_REG(RK3568_DSP_IF_EN, 0x1, 10),
	.dp0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 12),
	.dp1_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 14),
	.hdmi0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 16),
	.edp0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 16),
	.hdmi1_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 18),
	.edp1_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 18),
	.mipi0_mux = VOP_REG(RK3568_DSP_IF_EN, 0x1, 20),
	.mipi1_mux = VOP_REG(RK3568_DSP_IF_EN, 0x3, 21),
	.bt656_yc_swap = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 1),
	.bt1120_yc_swap = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 5),
	.hdmi_dual_en = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 8),
	.edp_dual_en = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 8),
	.dp_dual_en = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 9),
	.mipi_dual_en = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 10),
	.mipi0_ds_mode = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 11),
	.mipi1_ds_mode = VOP_REG(RK3568_DSP_IF_CTRL, 0x1, 12),
	.hdmi0_dclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 16),
	.hdmi0_pixclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 18),
	.hdmi1_dclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 20),
	.hdmi1_pixclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 22),
	.edp0_dclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 16),
	.edp0_pixclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 18),
	.edp1_dclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 20),
	.edp1_pixclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 22),

	.mipi0_pixclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 24),
	.mipi1_pixclk_div = VOP_REG(RK3568_DSP_IF_CTRL, 0x3, 26),
	/* HDMI pol control by GRF_VO1_CON0
	 * DP0/1 clk pol is fixed
	 * MIPI/eDP pol is fixed
	 */
	.rgb_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 0),
	.rgb_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 3),
	.dp0_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 8),
	.dp1_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 12),
	.gamma_port_sel = VOP_REG(RK3568_LUT_PORT_SEL, 0x3, 12),
	.pd_off_imd = VOP_REG(RK3568_SYS_PD_CTRL, 0x1, 31),
	.win_vp_id[ROCKCHIP_VOP2_CLUSTER0] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 16),
	.win_vp_id[ROCKCHIP_VOP2_CLUSTER1] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 18),
	.win_vp_id[ROCKCHIP_VOP2_CLUSTER2] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 20),
	.win_vp_id[ROCKCHIP_VOP2_CLUSTER3] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 22),
	.win_vp_id[ROCKCHIP_VOP2_ESMART0] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 24),
	.win_vp_id[ROCKCHIP_VOP2_ESMART1] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 26),
	.win_vp_id[ROCKCHIP_VOP2_ESMART2] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 28),
	.win_vp_id[ROCKCHIP_VOP2_ESMART3] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 30),
	.win_dly[ROCKCHIP_VOP2_CLUSTER0] = VOP_REG(RK3568_CLUSTER_DLY_NUM, 0xffff, 0),
	.win_dly[ROCKCHIP_VOP2_CLUSTER1] = VOP_REG(RK3568_CLUSTER_DLY_NUM, 0xffff, 16),
	.win_dly[ROCKCHIP_VOP2_CLUSTER2] = VOP_REG(RK3568_CLUSTER_DLY_NUM1, 0xffff, 0),
	.win_dly[ROCKCHIP_VOP2_CLUSTER3] = VOP_REG(RK3568_CLUSTER_DLY_NUM1, 0xffff, 16),
	.win_dly[ROCKCHIP_VOP2_ESMART0] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 0),
	.win_dly[ROCKCHIP_VOP2_ESMART1] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 8),
	.win_dly[ROCKCHIP_VOP2_ESMART2] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 16),
	.win_dly[ROCKCHIP_VOP2_ESMART3] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 24),
};

static const struct vop_dump_regs rk3528_dump_regs[] = {
	{ RK3568_REG_CFG_DONE, "SYS", {0}, 0 },
	{ RK3528_OVL_SYS, "OVL_SYS", {0}, 0 },
	{ RK3528_OVL_PORT0_CTRL, "OVL_VP0", VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31), 0 },
	{ RK3528_OVL_PORT1_CTRL, "OVL_VP1", VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_VP0_DSP_CTRL, "VP0", VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_VP1_DSP_CTRL, "VP1", VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_CLUSTER0_WIN0_CTRL0, "Cluster0", VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 0), 1 },
	{ RK3568_ESMART0_CTRL0, "Esmart0", VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_ESMART1_CTRL0, "Esmart1", VOP_REG(RK3568_ESMART1_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_SMART0_CTRL0, "Esmart2", VOP_REG(RK3568_SMART0_CTRL0, 0x1, 0), 1 },
	{ RK3568_SMART1_CTRL0, "Esmart3", VOP_REG(RK3568_SMART1_CTRL0, 0x1, 0), 1 },
	{ RK3528_HDR_LUT_CTRL, "HDR", {0}, 0 },
};

static const struct vop_dump_regs rk3562_dump_regs[] = {
	{ RK3568_REG_CFG_DONE, "SYS", {0}, 0 },
	{ RK3528_OVL_SYS, "OVL_SYS", {0}, 0 },
	{ RK3528_OVL_PORT0_CTRL, "OVL_VP0", VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31), 0 },
	{ RK3528_OVL_PORT1_CTRL, "OVL_VP1", VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_VP0_DSP_CTRL, "VP0", VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_VP1_DSP_CTRL, "VP1", VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_ESMART0_CTRL0, "Esmart0", VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_ESMART1_CTRL0, "Esmart1", VOP_REG(RK3568_ESMART1_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_SMART0_CTRL0, "Esmart2", VOP_REG(RK3568_SMART0_CTRL0, 0x1, 0), 1 },
	{ RK3568_SMART1_CTRL0, "Esmart3", VOP_REG(RK3568_SMART1_CTRL0, 0x1, 0), 1 },
};

static const struct vop_dump_regs rk3568_dump_regs[] = {
	{ RK3568_REG_CFG_DONE, "SYS", {0}, 0 },
	{ RK3568_OVL_CTRL, "OVL", {0}, 0 },
	{ RK3568_VP0_DSP_CTRL, "VP0", VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_VP1_DSP_CTRL, "VP1", VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_VP2_DSP_CTRL, "VP2", VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_CLUSTER0_WIN0_CTRL0, "Cluster0", VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 0), 1 },
	{ RK3568_CLUSTER1_WIN0_CTRL0, "Cluster1", VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 0), 1 },
	{ RK3568_ESMART0_CTRL0, "Esmart0", VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_ESMART1_CTRL0, "Esmart1", VOP_REG(RK3568_ESMART1_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_SMART0_CTRL0, "Smart0", VOP_REG(RK3568_SMART0_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_SMART1_CTRL0, "Smart1", VOP_REG(RK3568_SMART1_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_HDR_LUT_CTRL, "HDR", {0}, 0 },
};

static const struct vop_dump_regs rk3588_dump_regs[] = {
	{ RK3568_REG_CFG_DONE, "SYS", {0}, 0 },
	{ RK3568_OVL_CTRL, "OVL", {0}, 0 },
	{ RK3568_VP0_DSP_CTRL, "VP0", VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_VP1_DSP_CTRL, "VP1", VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_VP2_DSP_CTRL, "VP2", VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 31), 0 },
	{ RK3588_VP3_DSP_CTRL, "VP3", VOP_REG(RK3588_VP3_DSP_CTRL, 0x1, 31), 0 },
	{ RK3568_CLUSTER0_WIN0_CTRL0, "Cluster0", VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 0), 1 },
	{ RK3568_CLUSTER1_WIN0_CTRL0, "Cluster1", VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 0), 1 },
	{ RK3588_CLUSTER2_WIN0_CTRL0, "Cluster2", VOP_REG(RK3588_CLUSTER2_WIN0_CTRL0, 0x1, 0), 1 },
	{ RK3588_CLUSTER3_WIN0_CTRL0, "Cluster3", VOP_REG(RK3588_CLUSTER3_WIN0_CTRL0, 0x1, 0), 1 },
	{ RK3568_ESMART0_CTRL0, "Esmart0", VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_ESMART1_CTRL0, "Esmart1", VOP_REG(RK3568_ESMART1_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_SMART0_CTRL0, "Esmart2", VOP_REG(RK3568_SMART0_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_SMART1_CTRL0, "Esmart3", VOP_REG(RK3568_SMART1_REGION0_CTRL, 0x1, 0), 1 },
	{ RK3568_HDR_LUT_CTRL, "HDR", {0}, 0 },
};

#define RK3568_PLANE_MASK_BASE \
	(BIT(ROCKCHIP_VOP2_CLUSTER0) | BIT(ROCKCHIP_VOP2_CLUSTER1) | \
	 BIT(ROCKCHIP_VOP2_ESMART0)  | BIT(ROCKCHIP_VOP2_ESMART1)  | \
	 BIT(ROCKCHIP_VOP2_SMART0)   | BIT(ROCKCHIP_VOP2_SMART1))

#define RK3588_PLANE_MASK_BASE \
	(BIT(ROCKCHIP_VOP2_CLUSTER0) | BIT(ROCKCHIP_VOP2_CLUSTER1) | \
	 BIT(ROCKCHIP_VOP2_CLUSTER2) | BIT(ROCKCHIP_VOP2_CLUSTER3) | \
	 BIT(ROCKCHIP_VOP2_ESMART0)  | BIT(ROCKCHIP_VOP2_ESMART1)  | \
	 BIT(ROCKCHIP_VOP2_ESMART2)  | BIT(ROCKCHIP_VOP2_ESMART3))

static struct vop2_vp_plane_mask rk3568_vp_plane_mask[ROCKCHIP_MAX_CRTC][ROCKCHIP_MAX_CRTC] = {
	{ /* one display policy */
		{/* main display */
			.primary_plane_id = ROCKCHIP_VOP2_SMART0,
			.attached_layers_nr = 6,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0, ROCKCHIP_VOP2_SMART0,
				  ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART1, ROCKCHIP_VOP2_SMART1
				},
		},
		{/* second display */},
		{/* third  display */},
		{/* fourth display */},
	},

	{ /* two display policy */
		{/* main display */
			.primary_plane_id = ROCKCHIP_VOP2_SMART0,
			.attached_layers_nr = 3,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0, ROCKCHIP_VOP2_SMART0
				},
		},

		{/* second display */
			.primary_plane_id = ROCKCHIP_VOP2_SMART1,
			.attached_layers_nr = 3,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART1, ROCKCHIP_VOP2_SMART1
				},
		},
		{/* third  display */},
		{/* fourth display */},
	},

	{ /* three display policy */
		{/* main display */
			.primary_plane_id = ROCKCHIP_VOP2_SMART0,
			.attached_layers_nr = 3,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0, ROCKCHIP_VOP2_SMART0
				},
		},

		{/* second display */
			.primary_plane_id = ROCKCHIP_VOP2_SMART1,
			.attached_layers_nr = 2,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_SMART1
				},
		},

		{/* third  display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART1,
			.attached_layers_nr = 1,
			.attached_layers = { ROCKCHIP_VOP2_ESMART1 },
		},

		{/* fourth display */},
	},

	{/* reserved for four display policy */},
};

static struct vop2_vp_plane_mask rk3588_vp_plane_mask[ROCKCHIP_MAX_CRTC][ROCKCHIP_MAX_CRTC] = {
	{ /* one display policy */
		{/* main display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART0,
			.attached_layers_nr = 8,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0, ROCKCHIP_VOP2_ESMART2,
				  ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART1, ROCKCHIP_VOP2_ESMART3,
				  ROCKCHIP_VOP2_CLUSTER2, ROCKCHIP_VOP2_CLUSTER3
			},
		},
		{/* second display */},
		{/* third  display */},
		{/* fourth display */},
	},

	{ /* two display policy */
		{/* main display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART0,
			.attached_layers_nr = 4,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0,
				  ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART1
			},
		},

		{/* second display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART2,
			.attached_layers_nr = 4,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER2, ROCKCHIP_VOP2_ESMART2,
				  ROCKCHIP_VOP2_CLUSTER3, ROCKCHIP_VOP2_ESMART3
			},
		},
		{/* third  display */},
		{/* fourth display */},
	},

	{ /* three display policy */
		{/* main display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART0,
			.attached_layers_nr = 3,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART0
			},
		},

		{/* second display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART1,
			.attached_layers_nr = 3,
			.attached_layers = {
				  ROCKCHIP_VOP2_CLUSTER2, ROCKCHIP_VOP2_CLUSTER3, ROCKCHIP_VOP2_ESMART1
			},
		},

		{/* third  display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART2,
			.attached_layers_nr = 2,
			.attached_layers = { ROCKCHIP_VOP2_ESMART2, ROCKCHIP_VOP2_ESMART3 },
		},

		{/* fourth display */},
	},

	{ /* four display policy */
		{/* main display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART0,
			.attached_layers_nr = 2,
			.attached_layers = { ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0 },
		},

		{/* second display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART1,
			.attached_layers_nr = 2,
			.attached_layers = { ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART1 },
		},

		{/* third  display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART2,
			.attached_layers_nr = 2,
			.attached_layers = { ROCKCHIP_VOP2_CLUSTER2, ROCKCHIP_VOP2_ESMART2 },
		},

		{/* fourth display */
			.primary_plane_id = ROCKCHIP_VOP2_ESMART3,
			.attached_layers_nr = 2,
			.attached_layers = { ROCKCHIP_VOP2_CLUSTER3, ROCKCHIP_VOP2_ESMART3 },
		},
	},

};

static const struct vop2_data rk3528_vop = {
	.version = VOP_VERSION_RK3528,
	.nr_vps = 2,
	.nr_mixers = 4,
	.nr_layers = 4,
	.nr_gammas = 2,
	.esmart_lb_mode = VOP3_ESMART_4K_2K_2K_MODE,
	.max_input = { 4096, 4096 },
	.max_output = { 4096, 4096 },
	.ctrl = &rk3528_vop_ctrl,
	.axi_intr = rk3528_vop_axi_intr,
	.nr_axi_intr = ARRAY_SIZE(rk3528_vop_axi_intr),
	.vp = rk3528_vop_video_ports,
	.wb = &rk3568_vop_wb_data,
	.win = rk3528_vop_win_data,
	.win_size = ARRAY_SIZE(rk3528_vop_win_data),
	.dump_regs = rk3528_dump_regs,
	.dump_regs_size = ARRAY_SIZE(rk3528_dump_regs),
};

static const struct vop2_data rk3562_vop = {
	.version = VOP_VERSION_RK3562,
	.nr_vps = ARRAY_SIZE(rk3562_vop_video_ports),
	.nr_mixers = 3,
	.nr_layers = 4,
	.nr_gammas = 2,
	.esmart_lb_mode = VOP3_ESMART_2K_2K_2K_2K_MODE,
	.max_input = { 4096, 4096 },
	.max_output = { 4096, 4096 },
	.ctrl = &rk3562_vop_ctrl,
	.sys_grf = &rk3562_sys_grf_ctrl,
	.axi_intr = rk3528_vop_axi_intr,
	.nr_axi_intr = ARRAY_SIZE(rk3528_vop_axi_intr),
	.vp = rk3562_vop_video_ports,
	.wb = &rk3568_vop_wb_data,
	.win = rk3562_vop_win_data,
	.win_size = ARRAY_SIZE(rk3562_vop_win_data),
	.dump_regs = rk3562_dump_regs,
	.dump_regs_size = ARRAY_SIZE(rk3562_dump_regs),
};

static const struct vop2_data rk3568_vop = {
	.version = VOP_VERSION_RK3568,
	.nr_vps = 3,
	.nr_mixers = 5,
	.nr_layers = 6,
	.nr_gammas = 1,
	.max_input = { 4096, 2304 },
	.max_output = { 4096, 2304 },
	.ctrl = &rk3568_vop_ctrl,
	.sys_grf = &rk3568_sys_grf_ctrl,
	.axi_intr = rk3568_vop_axi_intr,
	.nr_axi_intr = ARRAY_SIZE(rk3568_vop_axi_intr),
	.vp = rk3568_vop_video_ports,
	.wb = &rk3568_vop_wb_data,
	.layer = rk3568_vop_layers,
	.win = rk3568_vop_win_data,
	.win_size = ARRAY_SIZE(rk3568_vop_win_data),
	.dump_regs = rk3568_dump_regs,
	.dump_regs_size = ARRAY_SIZE(rk3568_dump_regs),
	.plane_mask = rk3568_vp_plane_mask[0],
	.plane_mask_base = RK3568_PLANE_MASK_BASE,
};

static const struct vop2_data rk3588_vop = {
	.version = VOP_VERSION_RK3588,
	.feature = VOP_FEATURE_SPLICE,
	.nr_dscs = 2,
	.nr_dsc_ecw = ARRAY_SIZE(dsc_ecw),
	.nr_dsc_buffer_flow = ARRAY_SIZE(dsc_buffer_flow),
	.nr_vps = 4,
	.nr_mixers = 7,
	.nr_layers = 8,
	.nr_gammas = 4,
	.max_input = { 4096, 4320 },
	.max_output = { 4096, 4320 },
	.ctrl = &rk3588_vop_ctrl,
	.grf = &rk3588_vop_grf_ctrl,
	.sys_grf = &rk3588_sys_grf_ctrl,
	.vo1_grf = &rk3588_vo1_grf_ctrl,
	.axi_intr = rk3568_vop_axi_intr,
	.nr_axi_intr = ARRAY_SIZE(rk3568_vop_axi_intr),
	.dsc = rk3588_vop_dsc_data,
	.dsc_error_ecw = dsc_ecw,
	.dsc_error_buffer_flow = dsc_buffer_flow,
	.vp = rk3588_vop_video_ports,
	.conn = rk3588_conn_if_data,
	.nr_conns = ARRAY_SIZE(rk3588_conn_if_data),
	.wb = &rk3568_vop_wb_data,
	.layer = rk3568_vop_layers,
	.win = rk3588_vop_win_data,
	.win_size = ARRAY_SIZE(rk3588_vop_win_data),
	.pd = rk3588_vop_pd_data,
	.nr_pds = ARRAY_SIZE(rk3588_vop_pd_data),
	.mem_pg = rk3588_vop_mem_pg_data,
	.nr_mem_pgs = ARRAY_SIZE(rk3588_vop_mem_pg_data),
	.dump_regs = rk3588_dump_regs,
	.dump_regs_size = ARRAY_SIZE(rk3588_dump_regs),
	.plane_mask = rk3588_vp_plane_mask[0],
	.plane_mask_base = RK3588_PLANE_MASK_BASE,
};

static const struct of_device_id vop2_dt_match[] = {
	{ .compatible = "rockchip,rk3528-vop",
	  .data = &rk3528_vop },
	{ .compatible = "rockchip,rk3562-vop",
	  .data = &rk3562_vop },
	{ .compatible = "rockchip,rk3568-vop",
	  .data = &rk3568_vop },
	{ .compatible = "rockchip,rk3588-vop",
	  .data = &rk3588_vop },

	{},
};
MODULE_DEVICE_TABLE(of, vop2_dt_match);

static int vop2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!dev->of_node) {
		DRM_DEV_ERROR(dev, "can't find vop2 devices\n");
		return -ENODEV;
	}
	return component_add(dev, &vop2_component_ops);
}

static int vop2_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vop2_component_ops);

	return 0;
}

struct platform_driver vop2_platform_driver = {
	.probe = vop2_probe,
	.remove = vop2_remove,
	.driver = {
		.name = "rockchip-vop2",
		.of_match_table = of_match_ptr(vop2_dt_match),
	},
};
