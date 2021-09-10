// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/component.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include "rockchip_drm_vop.h"
#include "rockchip_vop_reg.h"

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

static const uint32_t formats_win_full_10bit[] = {
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
	DRM_FORMAT_YUV420_8BIT, /* yuv420_8bit non-Linear mode only */
	DRM_FORMAT_YUV420_10BIT, /* yuv420_10bit non-Linear mode only */
	DRM_FORMAT_YVYU, /* yuv422_8bit[YVYU] linear mode or non-Linear mode */
	DRM_FORMAT_VYUY, /* yuv422_8bit[VYUY] linear mode or non-Linear mode */
	DRM_FORMAT_YUYV, /* yuv422_8bit[YUYV] linear mode or non-Linear mode */
	DRM_FORMAT_UYVY, /* yuv422_8bit[UYVY] linear mode or non-Linear mode */
	DRM_FORMAT_Y210, /* yuv422_10bit non-Linear mode only */
};

static const uint32_t formats_win_full_10bit_yuyv[] = {
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
	DRM_FORMAT_YVYU, /* yuv422_8bit[YVYU] linear mode or non-Linear mode */
	DRM_FORMAT_VYUY, /* yuv422_8bit[VYUY] linear mode or non-Linear mode */
	DRM_FORMAT_YUYV, /* yuv422_8bit[YUYV] linear mode or non-Linear mode */
	DRM_FORMAT_UYVY, /* yuv422_8bit[UYVY] linear mode or non-Linear mode */
};

static const uint32_t formats_win_lite[] = {
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
	.regs = &rk3568_vop_wb_regs,
};

static const struct vop2_video_port_regs rk3568_vop_vp0_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 0),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 0),
	.dsp_background = VOP_REG(RK3568_VP0_DSP_BG, 0x3fffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 0),
	.out_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0xf, 0),
	.standby = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 31),
	.core_dclk_div = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 4),
	.dclk_div2 = VOP_REG(RK3568_VP0_MIPI_CTRL, 0x1, 4),
	.dclk_div2_phase_lock = VOP_REG(RK3568_VP0_MIPI_CTRL, 0x1, 5),
	.p2i_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1f, 8),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 15),
	.pre_scan_htiming = VOP_REG(RK3568_VP0_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP0_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.htotal_pw = VOP_REG(RK3568_VP0_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP0_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP0_POST_SCL_CTRL, 0x3, 0),
	.hact_st_end = VOP_REG(RK3568_VP0_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3568_VP0_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP0_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP0_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP0_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.pre_dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP0_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP0_DSP_CTRL, 0x1, 20),
	.mipi_dual_en = VOP_REG(RK3568_VP0_MIPI_CTRL, 0x1, 20),
	.mipi_dual_channel_swap = VOP_REG(RK3568_VP0_MIPI_CTRL, 0x1, 21),
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
};

static const struct vop2_video_port_regs rk3568_vop_vp1_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 1),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 1),
	.dsp_background = VOP_REG(RK3568_VP1_DSP_BG, 0x3fffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 4),
	.out_mode = VOP_REG(RK3568_VP1_DSP_CTRL, 0xf, 0),
	.standby = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 31),
	.core_dclk_div = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 4),
	.dclk_div2 = VOP_REG(RK3568_VP1_MIPI_CTRL, 0x1, 4),
	.dclk_div2_phase_lock = VOP_REG(RK3568_VP1_MIPI_CTRL, 0x1, 5),
	.p2i_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1f, 8),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 15),
	.pre_scan_htiming = VOP_REG(RK3568_VP1_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP1_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP1_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP1_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.htotal_pw = VOP_REG(RK3568_VP1_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP1_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP1_POST_SCL_CTRL, 0x3, 0),
	.hact_st_end = VOP_REG(RK3568_VP1_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3568_VP1_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP1_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP1_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP1_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP1_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.pre_dither_down_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP1_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP1_DSP_CTRL, 0x1, 20),
	.mipi_dual_en = VOP_REG(RK3568_VP1_MIPI_CTRL, 0x1, 20),
	.mipi_dual_channel_swap = VOP_REG(RK3568_VP1_MIPI_CTRL, 0x1, 21),

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
};

static const struct vop2_video_port_regs rk3568_vop_vp2_regs = {
	.cfg_done = VOP_REG(RK3568_REG_CFG_DONE, 0x1, 2),
	.overlay_mode = VOP_REG(RK3568_OVL_CTRL, 0x1, 2),
	.dsp_background = VOP_REG(RK3568_VP2_DSP_BG, 0x3fffffff, 0),
	.port_mux = VOP_REG(RK3568_OVL_PORT_SEL, 0xf, 8),
	.out_mode = VOP_REG(RK3568_VP2_DSP_CTRL, 0xf, 0),
	.standby = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 31),
	.core_dclk_div = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 4),
	.dclk_div2 = VOP_REG(RK3568_VP2_MIPI_CTRL, 0x1, 4),
	.dclk_div2_phase_lock = VOP_REG(RK3568_VP2_MIPI_CTRL, 0x1, 5),
	.p2i_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 5),
	.dsp_filed_pol = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 6),
	.dsp_interlace = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 7),
	.dsp_data_swap = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1f, 8),
	.post_dsp_out_r2y = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 15),
	.pre_scan_htiming = VOP_REG(RK3568_VP2_PRE_SCAN_HTIMING, 0x1fff1fff, 0),
	.bg_dly = VOP_REG(RK3568_VP2_BG_MIX_CTRL, 0xff, 24),
	.hpost_st_end = VOP_REG(RK3568_VP2_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3568_VP2_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3568_VP2_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3568_VP2_POST_SCL_CTRL, 0x3, 0),
	.htotal_pw = VOP_REG(RK3568_VP2_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3568_VP2_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3568_VP2_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3568_VP2_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3568_VP2_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3568_VP2_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3568_VP2_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.pre_dither_down_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 16),
	.dither_down_en = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 17),
	.dither_down_sel = VOP_REG(RK3568_VP2_DSP_CTRL, 0x3, 18),
	.dither_down_mode = VOP_REG(RK3568_VP2_DSP_CTRL, 0x1, 20),
	.mipi_dual_en = VOP_REG(RK3568_VP2_MIPI_CTRL, 0x1, 20),
	.mipi_dual_channel_swap = VOP_REG(RK3568_VP2_MIPI_CTRL, 0x1, 21),

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
};

static const struct vop2_video_port_data rk3568_vop_video_ports[] = {
	{
	 .id = 0,
	 .soc_id = { 0x3568, 0x3566 },
	 .feature = VOP_FEATURE_OUTPUT_10BIT,
	 .gamma_lut_len = 1024,
	 .cubic_lut_len = 729, /* 9x9x9 */
	 .max_output = { 4096, 2304 },
	 .pre_scan_max_dly = { 69, 53, 53, 42 },
	 .intr = &rk3568_vp0_intr,
	 .hdr_table = &rk3568_vop_hdr_table,
	 .regs = &rk3568_vop_vp0_regs,
	},
	{
	 .id = 1,
	 .soc_id = { 0x3568, 0x3566 },
	 .gamma_lut_len = 1024,
	 .max_output = { 2048, 1536 },
	 .pre_scan_max_dly = { 40, 40, 40, 40 },
	 .intr = &rk3568_vp1_intr,
	 .regs = &rk3568_vop_vp1_regs,
	},
	{
	 .id = 2,
	 .soc_id = { 0x3568, 0x3566 },
	 .gamma_lut_len = 1024,
	 .max_output = { 1920, 1080 },
	 .pre_scan_max_dly = { 40, 40, 40, 40 },
	 .intr = &rk3568_vp2_intr,
	 .regs = &rk3568_vop_vp2_regs,
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
};

static const struct vop2_cluster_regs rk3568_vop_cluster0 =  {
	.afbc_enable = VOP_REG(RK3568_CLUSTER0_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3568_CLUSTER0_CTRL, 1, 0),
	.lb_mode = VOP_REG(RK3568_CLUSTER0_CTRL, 0xf, 4),
};

static const struct vop2_cluster_regs rk3568_vop_cluster1 =  {
	.afbc_enable = VOP_REG(RK3568_CLUSTER1_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3568_CLUSTER1_CTRL, 1, 0),
	.lb_mode = VOP_REG(RK3568_CLUSTER1_CTRL, 0xf, 4),
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

static const struct vop2_win_regs rk3568_cluster0_win_data = {
	.scl = &rk3568_cluster0_win_scl,
	.afbc = &rk3568_cluster0_afbc,
	.cluster = &rk3568_vop_cluster0,
	.enable = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1f, 1),
	.rb_swap = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 14),
	.dither_up = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 18),
	.act_info = VOP_REG(RK3568_CLUSTER0_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_CLUSTER0_WIN0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3568_CLUSTER0_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_CLUSTER0_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_CLUSTER0_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_CLUSTER0_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_CLUSTER0_WIN0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 8),
	.r2y_en = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x1, 9),
	.csc_mode = VOP_REG(RK3568_CLUSTER0_WIN0_CTRL0, 0x3, 10),
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
	.dsp_info = VOP_REG(RK3568_CLUSTER1_WIN0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3568_CLUSTER1_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_CLUSTER1_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_CLUSTER1_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_CLUSTER1_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_CLUSTER1_WIN0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 8),
	.r2y_en = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x1, 9),
	.csc_mode = VOP_REG(RK3568_CLUSTER1_WIN0_CTRL0, 0x3, 10),
};

static const struct vop2_win_regs rk3568_esmart_win_data = {
	.scl = &rk3568_esmart_win_scl,
	.enable = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 0),
	.format = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1f, 1),
	.dither_up = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 12),
	.rb_swap = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 14),
	.uv_swap = VOP_REG(RK3568_ESMART0_REGION0_CTRL, 0x1, 16),
	.act_info = VOP_REG(RK3568_ESMART0_REGION0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_ESMART0_REGION0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3568_ESMART0_REGION0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_ESMART0_REGION0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_ESMART0_REGION0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3568_ESMART0_REGION0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_ESMART0_REGION0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_ESMART0_CTRL0, 0x1, 0),
	.r2y_en = VOP_REG(RK3568_ESMART0_CTRL0, 0x1, 1),
	.csc_mode = VOP_REG(RK3568_ESMART0_CTRL0, 0x3, 2),
	.ymirror = VOP_REG(RK3568_ESMART0_CTRL1, 0x1, 31),
	.color_key = VOP_REG(RK3568_ESMART0_COLOR_KEY_CTRL, 0x3fffffff, 0),
	.color_key_en = VOP_REG(RK3568_ESMART0_COLOR_KEY_CTRL, 0x1, 31),
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
	  .phys_id = 4,
	  .base = 0x400,
	  .formats = formats_win_lite,
	  .nformats = ARRAY_SIZE(formats_win_lite),
	  .format_modifiers = format_modifiers,
	  .layer_sel_id = 3,
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
	  .phys_id = 5,
	  .formats = formats_win_lite,
	  .nformats = ARRAY_SIZE(formats_win_lite),
	  .format_modifiers = format_modifiers,
	  .base = 0x600,
	  .layer_sel_id = 7,
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
	  .phys_id = 3,
	  .formats = formats_win_full_10bit_yuyv,
	  .nformats = ARRAY_SIZE(formats_win_full_10bit_yuyv),
	  .format_modifiers = format_modifiers,
	  .base = 0x200,
	  .layer_sel_id = 6,
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
	  .phys_id = 2,
	  .formats = formats_win_full_10bit_yuyv,
	  .nformats = ARRAY_SIZE(formats_win_full_10bit_yuyv),
	  .format_modifiers = format_modifiers,
	  .base = 0x0,
	  .layer_sel_id = 2,
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
	  .phys_id = 0,
	  .base = 0x00,
	  .formats = formats_win_full_10bit,
	  .nformats = ARRAY_SIZE(formats_win_full_10bit),
	  .format_modifiers = format_modifiers_afbc,
	  .layer_sel_id = 0,
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
	  .phys_id = 0,
	  .base = 0x80,
	  .layer_sel_id = -1,
	  .formats = formats_win_full_10bit,
	  .nformats = ARRAY_SIZE(formats_win_full_10bit),
	  .format_modifiers = format_modifiers_afbc,
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
	  .phys_id = 1,
	  .base = 0x00,
	  .formats = formats_win_full_10bit,
	  .nformats = ARRAY_SIZE(formats_win_full_10bit),
	  .format_modifiers = format_modifiers_afbc,
	  .layer_sel_id = 1,
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
	  .phys_id = 1,
	  .layer_sel_id = -1,
	  .formats = formats_win_full_10bit,
	  .nformats = ARRAY_SIZE(formats_win_full_10bit),
	  .format_modifiers = format_modifiers_afbc,
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

static const struct vop_grf_ctrl rk3568_grf_ctrl = {
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
	.cluster0_src_color_ctrl = VOP_REG(RK3568_CLUSTER0_MIX_SRC_COLOR_CTRL, 0xffffffff, 0),
	.cluster0_dst_color_ctrl = VOP_REG(RK3568_CLUSTER0_MIX_DST_COLOR_CTRL, 0xffffffff, 0),
	.cluster0_src_alpha_ctrl = VOP_REG(RK3568_CLUSTER0_MIX_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.cluster0_dst_alpha_ctrl = VOP_REG(RK3568_CLUSTER0_MIX_DST_ALPHA_CTRL, 0xffffffff, 0),
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
	.lvds_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 0),
	.lvds_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 3),
	.hdmi_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 4),
	.hdmi_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 7),
	.edp_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x3, 12),
	.edp_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 15),
	.mipi_pin_pol = VOP_REG(RK3568_DSP_IF_POL, 0x7, 16),
	.mipi_dclk_pol = VOP_REG(RK3568_DSP_IF_POL, 0x1, 19),
	.win_vp_id[0] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 16),
	.win_vp_id[1] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 18),
	.win_vp_id[2] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 24),
	.win_vp_id[3] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 26),
	.win_vp_id[4] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 28),
	.win_vp_id[5] = VOP_REG(RK3568_OVL_PORT_SEL, 0x3, 30),
	.win_dly[0] = VOP_REG(RK3568_CLUSTER_DLY_NUM, 0xffff, 0),
	.win_dly[1] = VOP_REG(RK3568_CLUSTER_DLY_NUM, 0xffff, 16),
	.win_dly[2] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 0),
	.win_dly[3] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 8),
	.win_dly[4] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 16),
	.win_dly[5] = VOP_REG(RK3568_SMART_DLY_NUM, 0xff, 24),
	.otp_en = VOP_REG(RK3568_OTP_WIN_EN, 0x1, 0),
};

static const struct vop2_data rk3568_vop = {
	.version = VOP_VERSION(0x40, 0x15),
	.nr_vps = 3,
	.nr_mixers = 5,
	.nr_gammas = 1,
	.max_input = { 4096, 2304 },
	.max_output = { 4096, 2304 },
	.ctrl = &rk3568_vop_ctrl,
	.grf_ctrl = &rk3568_grf_ctrl,
	.axi_intr = rk3568_vop_axi_intr,
	.nr_axi_intr = ARRAY_SIZE(rk3568_vop_axi_intr),
	.vp = rk3568_vop_video_ports,
	.wb = &rk3568_vop_wb_data,
	.layer = rk3568_vop_layers,
	.nr_layers = ARRAY_SIZE(rk3568_vop_layers),
	.win = rk3568_vop_win_data,
	.win_size = ARRAY_SIZE(rk3568_vop_win_data),
};

static const struct of_device_id vop2_dt_match[] = {
	{ .compatible = "rockchip,rk3568-vop",
	  .data = &rk3568_vop },
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
