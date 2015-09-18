/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _ROCKCHIP_DRM_VOP_H
#define _ROCKCHIP_DRM_VOP_H

/* register definition */
#define REG_CFG_DONE			0x0000
#define VERSION_INFO			0x0004
#define SYS_CTRL			0x0008
#define SYS_CTRL1			0x000c
#define DSP_CTRL0			0x0010
#define DSP_CTRL1			0x0014
#define DSP_BG				0x0018
#define MCU_CTRL			0x001c
#define INTR_CTRL0			0x0020
#define INTR_CTRL1			0x0024
#define WIN0_CTRL0			0x0030
#define WIN0_CTRL1			0x0034
#define WIN0_COLOR_KEY			0x0038
#define WIN0_VIR			0x003c
#define WIN0_YRGB_MST			0x0040
#define WIN0_CBR_MST			0x0044
#define WIN0_ACT_INFO			0x0048
#define WIN0_DSP_INFO			0x004c
#define WIN0_DSP_ST			0x0050
#define WIN0_SCL_FACTOR_YRGB		0x0054
#define WIN0_SCL_FACTOR_CBR		0x0058
#define WIN0_SCL_OFFSET			0x005c
#define WIN0_SRC_ALPHA_CTRL		0x0060
#define WIN0_DST_ALPHA_CTRL		0x0064
#define WIN0_FADING_CTRL		0x0068
/* win1 register */
#define WIN1_CTRL0			0x0070
#define WIN1_CTRL1			0x0074
#define WIN1_COLOR_KEY			0x0078
#define WIN1_VIR			0x007c
#define WIN1_YRGB_MST			0x0080
#define WIN1_CBR_MST			0x0084
#define WIN1_ACT_INFO			0x0088
#define WIN1_DSP_INFO			0x008c
#define WIN1_DSP_ST			0x0090
#define WIN1_SCL_FACTOR_YRGB		0x0094
#define WIN1_SCL_FACTOR_CBR		0x0098
#define WIN1_SCL_OFFSET			0x009c
#define WIN1_SRC_ALPHA_CTRL		0x00a0
#define WIN1_DST_ALPHA_CTRL		0x00a4
#define WIN1_FADING_CTRL		0x00a8
/* win2 register */
#define WIN2_CTRL0			0x00b0
#define WIN2_CTRL1			0x00b4
#define WIN2_VIR0_1			0x00b8
#define WIN2_VIR2_3			0x00bc
#define WIN2_MST0			0x00c0
#define WIN2_DSP_INFO0			0x00c4
#define WIN2_DSP_ST0			0x00c8
#define WIN2_COLOR_KEY			0x00cc
#define WIN2_MST1			0x00d0
#define WIN2_DSP_INFO1			0x00d4
#define WIN2_DSP_ST1			0x00d8
#define WIN2_SRC_ALPHA_CTRL		0x00dc
#define WIN2_MST2			0x00e0
#define WIN2_DSP_INFO2			0x00e4
#define WIN2_DSP_ST2			0x00e8
#define WIN2_DST_ALPHA_CTRL		0x00ec
#define WIN2_MST3			0x00f0
#define WIN2_DSP_INFO3			0x00f4
#define WIN2_DSP_ST3			0x00f8
#define WIN2_FADING_CTRL		0x00fc
/* win3 register */
#define WIN3_CTRL0			0x0100
#define WIN3_CTRL1			0x0104
#define WIN3_VIR0_1			0x0108
#define WIN3_VIR2_3			0x010c
#define WIN3_MST0			0x0110
#define WIN3_DSP_INFO0			0x0114
#define WIN3_DSP_ST0			0x0118
#define WIN3_COLOR_KEY			0x011c
#define WIN3_MST1			0x0120
#define WIN3_DSP_INFO1			0x0124
#define WIN3_DSP_ST1			0x0128
#define WIN3_SRC_ALPHA_CTRL		0x012c
#define WIN3_MST2			0x0130
#define WIN3_DSP_INFO2			0x0134
#define WIN3_DSP_ST2			0x0138
#define WIN3_DST_ALPHA_CTRL		0x013c
#define WIN3_MST3			0x0140
#define WIN3_DSP_INFO3			0x0144
#define WIN3_DSP_ST3			0x0148
#define WIN3_FADING_CTRL		0x014c
/* hwc register */
#define HWC_CTRL0			0x0150
#define HWC_CTRL1			0x0154
#define HWC_MST				0x0158
#define HWC_DSP_ST			0x015c
#define HWC_SRC_ALPHA_CTRL		0x0160
#define HWC_DST_ALPHA_CTRL		0x0164
#define HWC_FADING_CTRL			0x0168
/* post process register */
#define POST_DSP_HACT_INFO		0x0170
#define POST_DSP_VACT_INFO		0x0174
#define POST_SCL_FACTOR_YRGB		0x0178
#define POST_SCL_CTRL			0x0180
#define POST_DSP_VACT_INFO_F1		0x0184
#define DSP_HTOTAL_HS_END		0x0188
#define DSP_HACT_ST_END			0x018c
#define DSP_VTOTAL_VS_END		0x0190
#define DSP_VACT_ST_END			0x0194
#define DSP_VS_ST_END_F1		0x0198
#define DSP_VACT_ST_END_F1		0x019c
/* register definition end */

/* interrupt define */
#define DSP_HOLD_VALID_INTR		(1 << 0)
#define FS_INTR				(1 << 1)
#define LINE_FLAG_INTR			(1 << 2)
#define BUS_ERROR_INTR			(1 << 3)

#define INTR_MASK			(DSP_HOLD_VALID_INTR | FS_INTR | \
					 LINE_FLAG_INTR | BUS_ERROR_INTR)

#define DSP_HOLD_VALID_INTR_EN(x)	((x) << 4)
#define FS_INTR_EN(x)			((x) << 5)
#define LINE_FLAG_INTR_EN(x)		((x) << 6)
#define BUS_ERROR_INTR_EN(x)		((x) << 7)
#define DSP_HOLD_VALID_INTR_MASK	(1 << 4)
#define FS_INTR_MASK			(1 << 5)
#define LINE_FLAG_INTR_MASK		(1 << 6)
#define BUS_ERROR_INTR_MASK		(1 << 7)

#define INTR_CLR_SHIFT			8
#define DSP_HOLD_VALID_INTR_CLR		(1 << (INTR_CLR_SHIFT + 0))
#define FS_INTR_CLR			(1 << (INTR_CLR_SHIFT + 1))
#define LINE_FLAG_INTR_CLR		(1 << (INTR_CLR_SHIFT + 2))
#define BUS_ERROR_INTR_CLR		(1 << (INTR_CLR_SHIFT + 3))

#define DSP_LINE_NUM(x)			(((x) & 0x1fff) << 12)
#define DSP_LINE_NUM_MASK		(0x1fff << 12)

/* src alpha ctrl define */
#define SRC_FADING_VALUE(x)		(((x) & 0xff) << 24)
#define SRC_GLOBAL_ALPHA(x)		(((x) & 0xff) << 16)
#define SRC_FACTOR_M0(x)		(((x) & 0x7) << 6)
#define SRC_ALPHA_CAL_M0(x)		(((x) & 0x1) << 5)
#define SRC_BLEND_M0(x)			(((x) & 0x3) << 3)
#define SRC_ALPHA_M0(x)			(((x) & 0x1) << 2)
#define SRC_COLOR_M0(x)			(((x) & 0x1) << 1)
#define SRC_ALPHA_EN(x)			(((x) & 0x1) << 0)
/* dst alpha ctrl define */
#define DST_FACTOR_M0(x)		(((x) & 0x7) << 6)

/*
 * display output interface supported by rockchip lcdc
 */
#define ROCKCHIP_OUT_MODE_P888	0
#define ROCKCHIP_OUT_MODE_P666	1
#define ROCKCHIP_OUT_MODE_P565	2
/* for use special outface */
#define ROCKCHIP_OUT_MODE_AAAA	15

enum alpha_mode {
	ALPHA_STRAIGHT,
	ALPHA_INVERSE,
};

enum global_blend_mode {
	ALPHA_GLOBAL,
	ALPHA_PER_PIX,
	ALPHA_PER_PIX_GLOBAL,
};

enum alpha_cal_mode {
	ALPHA_SATURATION,
	ALPHA_NO_SATURATION,
};

enum color_mode {
	ALPHA_SRC_PRE_MUL,
	ALPHA_SRC_NO_PRE_MUL,
};

enum factor_mode {
	ALPHA_ZERO,
	ALPHA_ONE,
	ALPHA_SRC,
	ALPHA_SRC_INVERSE,
	ALPHA_SRC_GLOBAL,
};

enum scale_mode {
	SCALE_NONE = 0x0,
	SCALE_UP   = 0x1,
	SCALE_DOWN = 0x2
};

enum lb_mode {
	LB_YUV_3840X5 = 0x0,
	LB_YUV_2560X8 = 0x1,
	LB_RGB_3840X2 = 0x2,
	LB_RGB_2560X4 = 0x3,
	LB_RGB_1920X5 = 0x4,
	LB_RGB_1280X8 = 0x5
};

enum sacle_up_mode {
	SCALE_UP_BIL = 0x0,
	SCALE_UP_BIC = 0x1
};

enum scale_down_mode {
	SCALE_DOWN_BIL = 0x0,
	SCALE_DOWN_AVG = 0x1
};

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))
#define SCL_FT_DEFAULT_FIXPOINT_SHIFT	12
#define SCL_MAX_VSKIPLINES		4
#define MIN_SCL_FT_AFTER_VSKIP		1

static inline uint16_t scl_cal_scale(int src, int dst, int shift)
{
	return ((src * 2 - 3) << (shift - 1)) / (dst - 1);
}

#define GET_SCL_FT_BILI_DN(src, dst)	scl_cal_scale(src, dst, 12)
#define GET_SCL_FT_BILI_UP(src, dst)	scl_cal_scale(src, dst, 16)
#define GET_SCL_FT_BIC(src, dst)	scl_cal_scale(src, dst, 16)

static inline uint16_t scl_get_bili_dn_vskip(int src_h, int dst_h,
					     int vskiplines)
{
	int act_height;

	act_height = (src_h + vskiplines - 1) / vskiplines;

	return GET_SCL_FT_BILI_DN(act_height, dst_h);
}

static inline enum scale_mode scl_get_scl_mode(int src, int dst)
{
	if (src < dst)
		return SCALE_UP;
	else if (src > dst)
		return SCALE_DOWN;

	return SCALE_NONE;
}

static inline int scl_get_vskiplines(uint32_t srch, uint32_t dsth)
{
	uint32_t vskiplines;

	for (vskiplines = SCL_MAX_VSKIPLINES; vskiplines > 1; vskiplines /= 2)
		if (srch >= vskiplines * dsth * MIN_SCL_FT_AFTER_VSKIP)
			break;

	return vskiplines;
}

static inline int scl_vop_cal_lb_mode(int width, bool is_yuv)
{
	int lb_mode;

	if (width > 2560)
		lb_mode = LB_RGB_3840X2;
	else if (width > 1920)
		lb_mode = LB_RGB_2560X4;
	else if (!is_yuv)
		lb_mode = LB_RGB_1920X5;
	else if (width > 1280)
		lb_mode = LB_YUV_3840X5;
	else
		lb_mode = LB_YUV_2560X8;

	return lb_mode;
}

#endif /* _ROCKCHIP_DRM_VOP_H */
