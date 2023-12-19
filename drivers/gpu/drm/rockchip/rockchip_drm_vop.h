/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#ifndef _ROCKCHIP_DRM_VOP_H
#define _ROCKCHIP_DRM_VOP_H

/*
 * major: IP major version, used for IP structure
 * minor: big feature change under same structure
 */
#define VOP_VERSION(major, minor)	((major) << 8 | (minor))
#define VOP_MAJOR(version)		((version) >> 8)
#define VOP_MINOR(version)		((version) & 0xff)

#define NUM_YUV2YUV_COEFFICIENTS 12

/* AFBC supports a number of configurable modes. Relevant to us is block size
 * (16x16 or 32x8), storage modifiers (SPARSE, SPLIT), and the YUV-like
 * colourspace transform (YTR). 16x16 SPARSE mode is always used. SPLIT mode
 * could be enabled via the hreg_block_split register, but is not currently
 * handled. The colourspace transform is implicitly always assumed by the
 * decoder, so consumers must use this transform as well.
 *
 * Failure to match modifiers will cause errors displaying AFBC buffers
 * produced by conformant AFBC producers, including Mesa.
 */
#define ROCKCHIP_AFBC_MOD \
	DRM_FORMAT_MOD_ARM_AFBC( \
		AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_SPARSE \
			| AFBC_FORMAT_MOD_YTR \
	)

enum vop_data_format {
	VOP_FMT_ARGB8888 = 0,
	VOP_FMT_RGB888,
	VOP_FMT_RGB565,
	VOP_FMT_YUV420SP = 4,
	VOP_FMT_YUV422SP,
	VOP_FMT_YUV444SP,
};

struct vop_rect {
	int width;
	int height;
};

struct vop_reg {
	uint32_t mask;
	uint16_t offset;
	uint8_t shift;
	bool write_mask;
	bool relaxed;
};

struct vop_afbc {
	struct vop_reg enable;
	struct vop_reg win_sel;
	struct vop_reg format;
	struct vop_reg rb_swap;
	struct vop_reg uv_swap;
	struct vop_reg auto_gating_en;
	struct vop_reg block_split_en;
	struct vop_reg pic_vir_width;
	struct vop_reg tile_num;
	struct vop_reg hreg_block_split;
	struct vop_reg pic_offset;
	struct vop_reg pic_size;
	struct vop_reg dsp_offset;
	struct vop_reg transform_offset;
	struct vop_reg hdr_ptr;
	struct vop_reg half_block_en;
	struct vop_reg xmirror;
	struct vop_reg ymirror;
	struct vop_reg rotate_270;
	struct vop_reg rotate_90;
	struct vop_reg rstn;
};

struct vop_modeset {
	struct vop_reg htotal_pw;
	struct vop_reg hact_st_end;
	struct vop_reg hpost_st_end;
	struct vop_reg vtotal_pw;
	struct vop_reg vact_st_end;
	struct vop_reg vpost_st_end;
};

struct vop_output {
	struct vop_reg pin_pol;
	struct vop_reg dp_pin_pol;
	struct vop_reg dp_dclk_pol;
	struct vop_reg edp_pin_pol;
	struct vop_reg edp_dclk_pol;
	struct vop_reg hdmi_pin_pol;
	struct vop_reg hdmi_dclk_pol;
	struct vop_reg mipi_pin_pol;
	struct vop_reg mipi_dclk_pol;
	struct vop_reg rgb_pin_pol;
	struct vop_reg rgb_dclk_pol;
	struct vop_reg dp_en;
	struct vop_reg edp_en;
	struct vop_reg hdmi_en;
	struct vop_reg mipi_en;
	struct vop_reg mipi_dual_channel_en;
	struct vop_reg rgb_en;
};

struct vop_common {
	struct vop_reg cfg_done;
	struct vop_reg dsp_blank;
	struct vop_reg data_blank;
	struct vop_reg pre_dither_down;
	struct vop_reg dither_down_sel;
	struct vop_reg dither_down_mode;
	struct vop_reg dither_down_en;
	struct vop_reg dither_up;
	struct vop_reg dsp_lut_en;
	struct vop_reg update_gamma_lut;
	struct vop_reg lut_buffer_index;
	struct vop_reg gate_en;
	struct vop_reg mmu_en;
	struct vop_reg out_mode;
	struct vop_reg standby;
};

struct vop_misc {
	struct vop_reg global_regdone_en;
};

struct vop_intr {
	const int *intrs;
	uint32_t nintrs;

	struct vop_reg line_flag_num[2];
	struct vop_reg enable;
	struct vop_reg clear;
	struct vop_reg status;
};

struct vop_scl_extension {
	struct vop_reg cbcr_vsd_mode;
	struct vop_reg cbcr_vsu_mode;
	struct vop_reg cbcr_hsd_mode;
	struct vop_reg cbcr_ver_scl_mode;
	struct vop_reg cbcr_hor_scl_mode;
	struct vop_reg yrgb_vsd_mode;
	struct vop_reg yrgb_vsu_mode;
	struct vop_reg yrgb_hsd_mode;
	struct vop_reg yrgb_ver_scl_mode;
	struct vop_reg yrgb_hor_scl_mode;
	struct vop_reg line_load_mode;
	struct vop_reg cbcr_axi_gather_num;
	struct vop_reg yrgb_axi_gather_num;
	struct vop_reg vsd_cbcr_gt2;
	struct vop_reg vsd_cbcr_gt4;
	struct vop_reg vsd_yrgb_gt2;
	struct vop_reg vsd_yrgb_gt4;
	struct vop_reg bic_coe_sel;
	struct vop_reg cbcr_axi_gather_en;
	struct vop_reg yrgb_axi_gather_en;
	struct vop_reg lb_mode;
};

struct vop_scl_regs {
	const struct vop_scl_extension *ext;

	struct vop_reg scale_yrgb_x;
	struct vop_reg scale_yrgb_y;
	struct vop_reg scale_cbcr_x;
	struct vop_reg scale_cbcr_y;
};

struct vop_yuv2yuv_phy {
	struct vop_reg y2r_coefficients[NUM_YUV2YUV_COEFFICIENTS];
};

struct vop_win_phy {
	const struct vop_scl_regs *scl;
	const uint32_t *data_formats;
	uint32_t nformats;
	const uint64_t *format_modifiers;

	struct vop_reg enable;
	struct vop_reg gate;
	struct vop_reg format;
	struct vop_reg fmt_10;
	struct vop_reg rb_swap;
	struct vop_reg uv_swap;
	struct vop_reg act_info;
	struct vop_reg dsp_info;
	struct vop_reg dsp_st;
	struct vop_reg yrgb_mst;
	struct vop_reg uv_mst;
	struct vop_reg yrgb_vir;
	struct vop_reg uv_vir;
	struct vop_reg y_mir_en;
	struct vop_reg x_mir_en;

	struct vop_reg dst_alpha_ctl;
	struct vop_reg src_alpha_ctl;
	struct vop_reg alpha_pre_mul;
	struct vop_reg alpha_mode;
	struct vop_reg alpha_en;
	struct vop_reg channel;
};

struct vop_win_yuv2yuv_data {
	uint32_t base;
	const struct vop_yuv2yuv_phy *phy;
	struct vop_reg y2r_en;
};

struct vop_win_data {
	uint32_t base;
	const struct vop_win_phy *phy;
	enum drm_plane_type type;
};

struct vop_data {
	uint32_t version;
	const struct vop_intr *intr;
	const struct vop_common *common;
	const struct vop_misc *misc;
	const struct vop_modeset *modeset;
	const struct vop_output *output;
	const struct vop_afbc *afbc;
	const struct vop_win_yuv2yuv_data *win_yuv2yuv;
	const struct vop_win_data *win;
	unsigned int win_size;
	unsigned int lut_size;
	struct vop_rect max_output;

#define VOP_FEATURE_OUTPUT_RGB10	BIT(0)
#define VOP_FEATURE_INTERNAL_RGB	BIT(1)
	u64 feature;
};

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

/* output flags */
#define ROCKCHIP_OUTPUT_DSI_DUAL	BIT(0)

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

enum dither_down_mode {
	RGB888_TO_RGB565 = 0x0,
	RGB888_TO_RGB666 = 0x1
};

enum dither_down_mode_sel {
	DITHER_DOWN_ALLEGRO = 0x0,
	DITHER_DOWN_FRC = 0x1
};

enum vop_pol {
	HSYNC_POSITIVE = 0,
	VSYNC_POSITIVE = 1,
	DEN_NEGATIVE   = 2
};

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))
#define SCL_FT_DEFAULT_FIXPOINT_SHIFT	12
#define SCL_MAX_VSKIPLINES		4
#define MIN_SCL_FT_AFTER_VSKIP		1

static inline uint16_t scl_cal_scale(int src, int dst, int shift)
{
	return ((src * 2 - 3) << (shift - 1)) / (dst - 1);
}

static inline uint16_t scl_cal_scale2(int src, int dst)
{
	return ((src - 1) << 12) / (dst - 1);
}

#define GET_SCL_FT_BILI_DN(src, dst)	scl_cal_scale(src, dst, 12)
#define GET_SCL_FT_BILI_UP(src, dst)	scl_cal_scale(src, dst, 16)
#define GET_SCL_FT_BIC(src, dst)	scl_cal_scale(src, dst, 16)

static inline uint16_t scl_get_bili_dn_vskip(int src_h, int dst_h,
					     int vskiplines)
{
	int act_height;

	act_height = DIV_ROUND_UP(src_h, vskiplines);

	if (act_height == dst_h)
		return GET_SCL_FT_BILI_DN(src_h, dst_h) / vskiplines;

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

	if (is_yuv) {
		if (width > 1280)
			lb_mode = LB_YUV_3840X5;
		else
			lb_mode = LB_YUV_2560X8;
	} else {
		if (width > 2560)
			lb_mode = LB_RGB_3840X2;
		else if (width > 1920)
			lb_mode = LB_RGB_2560X4;
		else
			lb_mode = LB_RGB_1920X5;
	}

	return lb_mode;
}

extern const struct component_ops vop_component_ops;
#endif /* _ROCKCHIP_DRM_VOP_H */
