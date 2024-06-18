/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 */
#ifndef __RGA_HW_H__
#define __RGA_HW_H__

#define RGA_CMDBUF_SIZE 0x20

/* Hardware limits */
#define MAX_WIDTH 8192
#define MAX_HEIGHT 8192

#define MIN_WIDTH 34
#define MIN_HEIGHT 34

#define DEFAULT_WIDTH 100
#define DEFAULT_HEIGHT 100

#define RGA_TIMEOUT 500

/* Registers address */
#define RGA_SYS_CTRL 0x0000
#define RGA_CMD_CTRL 0x0004
#define RGA_CMD_BASE 0x0008
#define RGA_INT 0x0010
#define RGA_MMU_CTRL0 0x0014
#define RGA_VERSION_INFO 0x0028

#define RGA_MODE_BASE_REG 0x0100
#define RGA_MODE_MAX_REG 0x017C

#define RGA_MODE_CTRL 0x0100
#define RGA_SRC_INFO 0x0104
#define RGA_SRC_Y_RGB_BASE_ADDR 0x0108
#define RGA_SRC_CB_BASE_ADDR 0x010c
#define RGA_SRC_CR_BASE_ADDR 0x0110
#define RGA_SRC1_RGB_BASE_ADDR 0x0114
#define RGA_SRC_VIR_INFO 0x0118
#define RGA_SRC_ACT_INFO 0x011c
#define RGA_SRC_X_FACTOR 0x0120
#define RGA_SRC_Y_FACTOR 0x0124
#define RGA_SRC_BG_COLOR 0x0128
#define RGA_SRC_FG_COLOR 0x012c
#define RGA_SRC_TR_COLOR0 0x0130
#define RGA_SRC_TR_COLOR1 0x0134

#define RGA_DST_INFO 0x0138
#define RGA_DST_Y_RGB_BASE_ADDR 0x013c
#define RGA_DST_CB_BASE_ADDR 0x0140
#define RGA_DST_CR_BASE_ADDR 0x0144
#define RGA_DST_VIR_INFO 0x0148
#define RGA_DST_ACT_INFO 0x014c

#define RGA_ALPHA_CTRL0 0x0150
#define RGA_ALPHA_CTRL1 0x0154
#define RGA_FADING_CTRL 0x0158
#define RGA_PAT_CON 0x015c
#define RGA_ROP_CON0 0x0160
#define RGA_ROP_CON1 0x0164
#define RGA_MASK_BASE 0x0168

#define RGA_MMU_CTRL1 0x016C
#define RGA_MMU_SRC_BASE 0x0170
#define RGA_MMU_SRC1_BASE 0x0174
#define RGA_MMU_DST_BASE 0x0178

/* Registers value */
#define RGA_MODE_RENDER_BITBLT 0
#define RGA_MODE_RENDER_COLOR_PALETTE 1
#define RGA_MODE_RENDER_RECTANGLE_FILL 2
#define RGA_MODE_RENDER_UPDATE_PALETTE_LUT_RAM 3

#define RGA_MODE_BITBLT_MODE_SRC_TO_DST 0
#define RGA_MODE_BITBLT_MODE_SRC_SRC1_TO_DST 1

#define RGA_MODE_CF_ROP4_SOLID 0
#define RGA_MODE_CF_ROP4_PATTERN 1

#define RGA_COLOR_FMT_ABGR8888 0
#define RGA_COLOR_FMT_XBGR8888 1
#define RGA_COLOR_FMT_RGB888 2
#define RGA_COLOR_FMT_BGR565 4
#define RGA_COLOR_FMT_ABGR1555 5
#define RGA_COLOR_FMT_ABGR4444 6
#define RGA_COLOR_FMT_YUV422SP 8
#define RGA_COLOR_FMT_YUV422P 9
#define RGA_COLOR_FMT_YUV420SP 10
#define RGA_COLOR_FMT_YUV420P 11
/* SRC_COLOR Palette */
#define RGA_COLOR_FMT_CP_1BPP 12
#define RGA_COLOR_FMT_CP_2BPP 13
#define RGA_COLOR_FMT_CP_4BPP 14
#define RGA_COLOR_FMT_CP_8BPP 15
#define RGA_COLOR_FMT_MASK 15

#define RGA_COLOR_FMT_IS_YUV(fmt) \
	(((fmt) >= RGA_COLOR_FMT_YUV422SP) && ((fmt) < RGA_COLOR_FMT_CP_1BPP))
#define RGA_COLOR_FMT_IS_RGB(fmt) \
	((fmt) < RGA_COLOR_FMT_YUV422SP)

#define RGA_COLOR_NONE_SWAP 0
#define RGA_COLOR_RB_SWAP 1
#define RGA_COLOR_ALPHA_SWAP 2
#define RGA_COLOR_UV_SWAP 4

#define RGA_SRC_CSC_MODE_BYPASS 0
#define RGA_SRC_CSC_MODE_BT601_R0 1
#define RGA_SRC_CSC_MODE_BT601_R1 2
#define RGA_SRC_CSC_MODE_BT709_R0 3
#define RGA_SRC_CSC_MODE_BT709_R1 4

#define RGA_SRC_ROT_MODE_0_DEGREE 0
#define RGA_SRC_ROT_MODE_90_DEGREE 1
#define RGA_SRC_ROT_MODE_180_DEGREE 2
#define RGA_SRC_ROT_MODE_270_DEGREE 3

#define RGA_SRC_MIRR_MODE_NO 0
#define RGA_SRC_MIRR_MODE_X 1
#define RGA_SRC_MIRR_MODE_Y 2
#define RGA_SRC_MIRR_MODE_X_Y 3

#define RGA_SRC_HSCL_MODE_NO 0
#define RGA_SRC_HSCL_MODE_DOWN 1
#define RGA_SRC_HSCL_MODE_UP 2

#define RGA_SRC_VSCL_MODE_NO 0
#define RGA_SRC_VSCL_MODE_DOWN 1
#define RGA_SRC_VSCL_MODE_UP 2

#define RGA_SRC_TRANS_ENABLE_R 1
#define RGA_SRC_TRANS_ENABLE_G 2
#define RGA_SRC_TRANS_ENABLE_B 4
#define RGA_SRC_TRANS_ENABLE_A 8

#define RGA_SRC_BIC_COE_SELEC_CATROM 0
#define RGA_SRC_BIC_COE_SELEC_MITCHELL 1
#define RGA_SRC_BIC_COE_SELEC_HERMITE 2
#define RGA_SRC_BIC_COE_SELEC_BSPLINE 3

#define RGA_DST_DITHER_MODE_888_TO_666 0
#define RGA_DST_DITHER_MODE_888_TO_565 1
#define RGA_DST_DITHER_MODE_888_TO_555 2
#define RGA_DST_DITHER_MODE_888_TO_444 3

#define RGA_DST_CSC_MODE_BYPASS 0
#define RGA_DST_CSC_MODE_BT601_R0 1
#define RGA_DST_CSC_MODE_BT601_R1 2
#define RGA_DST_CSC_MODE_BT709_R0 3

#define RGA_ALPHA_ROP_MODE_2 0
#define RGA_ALPHA_ROP_MODE_3 1
#define RGA_ALPHA_ROP_MODE_4 2

#define RGA_ALPHA_SELECT_ALPHA 0
#define RGA_ALPHA_SELECT_ROP 1

#define RGA_ALPHA_MASK_BIG_ENDIAN 0
#define RGA_ALPHA_MASK_LITTLE_ENDIAN 1

#define RGA_ALPHA_NORMAL 0
#define RGA_ALPHA_REVERSE 1

#define RGA_ALPHA_BLEND_GLOBAL 0
#define RGA_ALPHA_BLEND_NORMAL 1
#define RGA_ALPHA_BLEND_MULTIPLY 2

#define RGA_ALPHA_CAL_CUT 0
#define RGA_ALPHA_CAL_NORMAL 1

#define RGA_ALPHA_FACTOR_ZERO 0
#define RGA_ALPHA_FACTOR_ONE 1
#define RGA_ALPHA_FACTOR_OTHER 2
#define RGA_ALPHA_FACTOR_OTHER_REVERSE 3
#define RGA_ALPHA_FACTOR_SELF 4

#define RGA_ALPHA_COLOR_NORMAL 0
#define RGA_ALPHA_COLOR_MULTIPLY_CAL 1

/* Registers union */
union rga_mode_ctrl {
	unsigned int val;
	struct {
		/* [0:2] */
		unsigned int render:3;
		/* [3:6] */
		unsigned int bitblt:1;
		unsigned int cf_rop4_pat:1;
		unsigned int alpha_zero_key:1;
		unsigned int gradient_sat:1;
		/* [7:31] */
		unsigned int reserved:25;
	} data;
};

union rga_src_info {
	unsigned int val;
	struct {
		/* [0:3] */
		unsigned int format:4;
		/* [4:7] */
		unsigned int swap:3;
		unsigned int cp_endian:1;
		/* [8:17] */
		unsigned int csc_mode:2;
		unsigned int rot_mode:2;
		unsigned int mir_mode:2;
		unsigned int hscl_mode:2;
		unsigned int vscl_mode:2;
		/* [18:22] */
		unsigned int trans_mode:1;
		unsigned int trans_enable:4;
		/* [23:25] */
		unsigned int dither_up_en:1;
		unsigned int bic_coe_sel:2;
		/* [26:31] */
		unsigned int reserved:6;
	} data;
};

union rga_src_vir_info {
	unsigned int val;
	struct {
		/* [0:15] */
		unsigned int vir_width:15;
		unsigned int reserved:1;
		/* [16:25] */
		unsigned int vir_stride:10;
		/* [26:31] */
		unsigned int reserved1:6;
	} data;
};

union rga_src_act_info {
	unsigned int val;
	struct {
		/* [0:15] */
		unsigned int act_width:13;
		unsigned int reserved:3;
		/* [16:31] */
		unsigned int act_height:13;
		unsigned int reserved1:3;
	} data;
};

union rga_src_x_factor {
	unsigned int val;
	struct {
		/* [0:15] */
		unsigned int down_scale_factor:16;
		/* [16:31] */
		unsigned int up_scale_factor:16;
	} data;
};

union rga_src_y_factor {
	unsigned int val;
	struct {
		/* [0:15] */
		unsigned int down_scale_factor:16;
		/* [16:31] */
		unsigned int up_scale_factor:16;
	} data;
};

/* Alpha / Red / Green / Blue */
union rga_src_cp_gr_color {
	unsigned int val;
	struct {
		/* [0:15] */
		unsigned int gradient_x:16;
		/* [16:31] */
		unsigned int gradient_y:16;
	} data;
};

union rga_src_transparency_color0 {
	unsigned int val;
	struct {
		/* [0:7] */
		unsigned int trans_rmin:8;
		/* [8:15] */
		unsigned int trans_gmin:8;
		/* [16:23] */
		unsigned int trans_bmin:8;
		/* [24:31] */
		unsigned int trans_amin:8;
	} data;
};

union rga_src_transparency_color1 {
	unsigned int val;
	struct {
		/* [0:7] */
		unsigned int trans_rmax:8;
		/* [8:15] */
		unsigned int trans_gmax:8;
		/* [16:23] */
		unsigned int trans_bmax:8;
		/* [24:31] */
		unsigned int trans_amax:8;
	} data;
};

union rga_dst_info {
	unsigned int val;
	struct {
		/* [0:3] */
		unsigned int format:4;
		/* [4:6] */
		unsigned int swap:3;
		/* [7:9] */
		unsigned int src1_format:3;
		/* [10:11] */
		unsigned int src1_swap:2;
		/* [12:15] */
		unsigned int dither_up_en:1;
		unsigned int dither_down_en:1;
		unsigned int dither_down_mode:2;
		/* [16:18] */
		unsigned int csc_mode:2;
		unsigned int csc_clip:1;
		/* [19:31] */
		unsigned int reserved:13;
	} data;
};

union rga_dst_vir_info {
	unsigned int val;
	struct {
		/* [0:15] */
		unsigned int vir_stride:15;
		unsigned int reserved:1;
		/* [16:31] */
		unsigned int src1_vir_stride:15;
		unsigned int reserved1:1;
	} data;
};

union rga_dst_act_info {
	unsigned int val;
	struct {
		/* [0:15] */
		unsigned int act_width:12;
		unsigned int reserved:4;
		/* [16:31] */
		unsigned int act_height:12;
		unsigned int reserved1:4;
	} data;
};

union rga_alpha_ctrl0 {
	unsigned int val;
	struct {
		/* [0:3] */
		unsigned int rop_en:1;
		unsigned int rop_select:1;
		unsigned int rop_mode:2;
		/* [4:11] */
		unsigned int src_fading_val:8;
		/* [12:20] */
		unsigned int dst_fading_val:8;
		unsigned int mask_endian:1;
		/* [21:31] */
		unsigned int reserved:11;
	} data;
};

union rga_alpha_ctrl1 {
	unsigned int val;
	struct {
		/* [0:1] */
		unsigned int dst_color_m0:1;
		unsigned int src_color_m0:1;
		/* [2:7] */
		unsigned int dst_factor_m0:3;
		unsigned int src_factor_m0:3;
		/* [8:9] */
		unsigned int dst_alpha_cal_m0:1;
		unsigned int src_alpha_cal_m0:1;
		/* [10:13] */
		unsigned int dst_blend_m0:2;
		unsigned int src_blend_m0:2;
		/* [14:15] */
		unsigned int dst_alpha_m0:1;
		unsigned int src_alpha_m0:1;
		/* [16:21] */
		unsigned int dst_factor_m1:3;
		unsigned int src_factor_m1:3;
		/* [22:23] */
		unsigned int dst_alpha_cal_m1:1;
		unsigned int src_alpha_cal_m1:1;
		/* [24:27] */
		unsigned int dst_blend_m1:2;
		unsigned int src_blend_m1:2;
		/* [28:29] */
		unsigned int dst_alpha_m1:1;
		unsigned int src_alpha_m1:1;
		/* [30:31] */
		unsigned int reserved:2;
	} data;
};

union rga_fading_ctrl {
	unsigned int val;
	struct {
		/* [0:7] */
		unsigned int fading_offset_r:8;
		/* [8:15] */
		unsigned int fading_offset_g:8;
		/* [16:23] */
		unsigned int fading_offset_b:8;
		/* [24:31] */
		unsigned int fading_en:1;
		unsigned int reserved:7;
	} data;
};

union rga_pat_con {
	unsigned int val;
	struct {
		/* [0:7] */
		unsigned int width:8;
		/* [8:15] */
		unsigned int height:8;
		/* [16:23] */
		unsigned int offset_x:8;
		/* [24:31] */
		unsigned int offset_y:8;
	} data;
};

#endif
