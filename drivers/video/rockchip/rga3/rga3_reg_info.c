// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga3_reg: " fmt

#include "rga3_reg_info.h"
#include "rga_common.h"

#define FACTOR_MAX ((int)(2 << 15))

static void RGA3_set_reg_win0_info(u8 *base, struct rga3_req *msg)
{
	u32 *bRGA3_WIN0_RD_CTRL;
	u32 *bRGA3_WIN0_Y_BASE, *bRGA3_WIN0_U_BASE, *bRGA3_WIN0_V_BASE;
	u32 *bRGA3_WIN0_VIR_STRIDE;
	u32 *bRGA3_WIN0_UV_VIR_STRIDE;
	u32 *bRGA3_WIN0_SRC_SIZE;
	u32 *bRGA3_WIN0_ACT_OFF;
	u32 *bRGA3_WIN0_ACT_SIZE;
	u32 *bRGA3_WIN0_DST_SIZE;

	u32 *bRGA3_WIN0_SCL_FAC;
	/* Not used yet. */
	// u32 *bRGA3_WIN0_FBC_OFF;

	u32 sw = 0, sh = 0;
	u32 dw = 0, dh = 0;
	u32 param_x = 0, param_y = 0;
	u8 x_up = 0, y_up = 0, x_by = 0, y_by = 0;

	u32 reg = 0;

	u8 win_format = 0;
	u8 win_yc_swp = 0;

	/* rb swap on RGB, uv swap on YUV */
	u8 win_pix_swp = 0;

	/*
	 * 1: Semi planar, for yuv 4:2:x
	 * 2: Interleaved (yuyv), for yuv422 8bit only ，RGB
	 */
	u8 win_interleaved = 1;

	/* enable r2y or y2r */
	u8 win_r2y = 0;
	u8 win_y2r = 0;

	u8 rotate_mode = 0;
	u8 xmirror = 0;
	u8 ymirror = 0;

	u8 pixel_width = 1;
	u8 yuv10 = 0;

	u32 stride = 0;
	u32 uv_stride = 0;

	bRGA3_WIN0_RD_CTRL = (u32 *) (base + RGA3_WIN0_RD_CTRL_OFFSET);

	bRGA3_WIN0_Y_BASE = (u32 *) (base + RGA3_WIN0_Y_BASE_OFFSET);
	bRGA3_WIN0_U_BASE = (u32 *) (base + RGA3_WIN0_U_BASE_OFFSET);
	bRGA3_WIN0_V_BASE = (u32 *) (base + RGA3_WIN0_V_BASE_OFFSET);

	bRGA3_WIN0_VIR_STRIDE = (u32 *) (base + RGA3_WIN0_VIR_STRIDE_OFFSET);
	bRGA3_WIN0_UV_VIR_STRIDE =
		(u32 *) (base + RGA3_WIN0_UV_VIR_STRIDE_OFFSET);

	/* Not used yet. */
	// bRGA3_WIN0_FBC_OFF = (u32 *) (base + RGA3_WIN0_FBC_OFF_OFFSET);
	bRGA3_WIN0_ACT_OFF = (u32 *) (base + RGA3_WIN0_ACT_OFF_OFFSET);
	bRGA3_WIN0_SRC_SIZE = (u32 *) (base + RGA3_WIN0_SRC_SIZE_OFFSET);
	bRGA3_WIN0_ACT_SIZE = (u32 *) (base + RGA3_WIN0_ACT_SIZE_OFFSET);
	bRGA3_WIN0_DST_SIZE = (u32 *) (base + RGA3_WIN0_DST_SIZE_OFFSET);

	bRGA3_WIN0_SCL_FAC = (u32 *) (base + RGA3_WIN0_SCL_FAC_OFFSET);

	if (msg->win0.rotate_mode != 0) {
		switch (msg->rotate_mode) {
			/* rot 90 */
		case 0x1:
			rotate_mode = 1;
			break;
			/* rot 180 */
		case 0x2:
			xmirror = 1;
			ymirror = 1;
			break;
			/* rot 270 or rot -90 */
		case 0x3:
			rotate_mode = 1;
			xmirror = 1;
			ymirror = 1;
			break;
			/* ymirror */
		case 0x4:
			ymirror = 1;
			break;
			/* xmirror */
		case 0x5:
			xmirror = 1;
			break;
			/* rot 90 + xmirror */
		case 0x6:
			rotate_mode = 1;
			xmirror = 1;
			break;
			/* rot 90 + ymirror */
		case 0x7:
			rotate_mode = 1;
			ymirror = 1;
			break;
			/* bypass */
		default:
			break;
		};
	}

	/* scale */
	dw = msg->win0.dst_act_w;
	dh = msg->win0.dst_act_h;

	if (msg->win0.rotate_mode != 0) {
		if (msg->rotate_mode == 1 || msg->rotate_mode == 3 ||
			msg->rotate_mode == 6 || msg->rotate_mode == 7) {
			sh = msg->win0.src_act_w;
			sw = msg->win0.src_act_h;
		} else {
			sw = msg->win0.src_act_w;
			sh = msg->win0.src_act_h;
		}
	}

	if (sw > dw) {
		x_up = 0;
		x_by = 0;
	} else if (sw < dw) {
		x_up = 1;
		x_by = 0;
	} else {
		x_up = 0;
		x_by = 1;
	}

	if (sh > dh) {
		y_up = 0;
		y_by = 0;
	} else if (sh < dh) {
		y_up = 1;
		y_by = 0;
	} else {
		y_up = 0;
		y_by = 1;
	}

	if (x_by == 1 && x_up == 0)
		param_x = 0;
	else if (x_up == 1 && x_by == 0) {
		param_x = FACTOR_MAX * (sw - 1) / (dw - 1);
		if ((sw - 1) % (dw - 1) == 0) {
			pr_err("hor_up_fac modify xxxx\n");
			param_x = param_x - 1;
		}
	} else
		param_x = FACTOR_MAX * (dw - 1) / (sw - 1) + 1;

	if (y_by == 1 && y_up == 0)
		param_y = 0;
	else if (y_up == 1 && y_by == 0) {
		param_y = FACTOR_MAX * (sh - 1) / (dh - 1);
		if ((sh - 1) % (dh - 1) == 0) {
			pr_err("ver_up_fac modify yyyy\n");
			param_y = param_y - 1;
		}
	} else
		param_y = FACTOR_MAX * (dh - 1) / (sh - 1) + 1;

	switch (msg->win0.format) {
	case RGA_FORMAT_RGBA_8888:
		win_format = 0x8;
		pixel_width = 4;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_BGRA_8888:
		win_format = 0x6;
		pixel_width = 4;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_ARGB_8888:
		win_format = 0x9;
		pixel_width = 4;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_ABGR_8888:
		win_format = 0x7;
		pixel_width = 4;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_RGB_888:
		win_format = 0x5;
		pixel_width = 3;
		win_interleaved = 2;
		win_pix_swp = 1;
		break;
	case RGA_FORMAT_BGR_888:
		win_format = 0x5;
		pixel_width = 3;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_RGB_565:
		win_format = 0x4;
		pixel_width = 2;
		win_interleaved = 2;
		win_pix_swp = 1;
		break;
	case RGA_FORMAT_BGR_565:
		win_format = 0x4;
		pixel_width = 2;
		win_interleaved = 2;
		break;

	case RGA_FORMAT_YVYU_422:
		win_format = 0x1;
		pixel_width = 2;
		win_pix_swp = 1;
		win_yc_swp = 1;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_VYUY_422:
		win_format = 0x1;
		pixel_width = 2;
		win_pix_swp = 1;
		win_yc_swp = 0;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_YUYV_422:
		win_format = 0x1;
		pixel_width = 2;
		win_pix_swp = 0;
		win_yc_swp = 1;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_UYVY_422:
		win_format = 0x1;
		pixel_width = 2;
		win_pix_swp = 0;
		win_yc_swp = 0;
		win_interleaved = 2;
		break;

	case RGA_FORMAT_YCbCr_422_SP:
		win_format = 0x1;
		break;
	case RGA_FORMAT_YCbCr_420_SP:
		win_format = 0x0;
		break;
	case RGA_FORMAT_YCrCb_422_SP:
		win_format = 0x1;
		win_pix_swp = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP:
		win_format = 0x0;
		win_pix_swp = 1;
		break;

	case RGA_FORMAT_YCbCr_420_SP_10B:
		win_format = 0x2;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP_10B:
		win_format = 0x2;
		yuv10 = 1;
		win_pix_swp = 1;
		break;
	case RGA_FORMAT_YCbCr_422_SP_10B:
		win_format = 0x3;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCrCb_422_SP_10B:
		win_format = 0x3;
		yuv10 = 1;
		win_pix_swp = 1;
		break;
	};

	if (rga_is_rgb_format(msg->win0.format) &&
	    rga_is_yuv_format(msg->wr.format))
		win_r2y = 1;
	if (rga_is_yuv_format(msg->win0.format) &&
	    rga_is_rgb_format(msg->wr.format))
		win_y2r = 1;

	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_R2Y_EN)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_R2Y_EN(win_r2y)));
	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_Y2R_EN)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_Y2R_EN(win_y2r)));

	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_PIC_FORMAT)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_PIC_FORMAT(win_format)));
	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_PIX_SWAP)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_PIX_SWAP(win_pix_swp)));
	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_YC_SWAP)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_YC_SWAP(win_yc_swp)));
	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_FORMAT)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_FORMAT(win_interleaved)));

	if (win_r2y == 1) {
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE)) |
			(s_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE(msg->win0.r2y_mode)));
	} else if (win_y2r == 1) {
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE)) |
			(s_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE(msg->win0.y2r_mode)));
	}

	/* rotate & mirror */
	if (msg->win1.yrgb_addr == 0) {
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_ROT)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_ROT(rotate_mode)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_XMIRROR)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_XMIRROR(xmirror)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_YMIRROR)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_YMIRROR(ymirror)));

		/* scale */
		*bRGA3_WIN0_SCL_FAC = param_x | param_y << 16;

		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_BY)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_BY(x_by)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_UP)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_UP(x_up)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_BY)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_BY(y_by)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_UP)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_UP(y_up)));
	} else {
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_BY)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_BY(1)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_UP)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_UP(0)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_BY)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_BY(1)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_UP)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_UP(0)));
	}

	/* rd_mode */
	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_MODE)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_MODE(msg->win0.rd_mode)));
	/* win0 enable */
	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_ENABLE)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_ENABLE(msg->win0.enable)));

	reg =
		((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_YUV10B_COMPACT)) |
		 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_YUV10B_COMPACT(1)));

	/* Only on roster mode, yuv 10bit can change to compact or set endian */
	if (msg->win0.rd_mode == RGA_RASTER_MODE && yuv10 == 1) {
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_YUV10B_COMPACT)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_YUV10B_COMPACT
			 (msg->win0.is_10b_compact)));
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_ENDIAN_MODE)) |
			 (s_RGA3_WIN0_RD_CTRL_SW_WIN0_ENDIAN_MODE
			 (msg->win0.is_10b_endian)));
	}

	*bRGA3_WIN0_RD_CTRL = reg;

	/* stride need align to 16 */
	if (msg->win0.rd_mode != 1)
		stride = (((msg->win0.vir_w * pixel_width) + 15) & ~15) >> 2;
	else
		stride = ((msg->win0.vir_w + 15) & ~15) >> 2;

	if (msg->win0.format == RGA_FORMAT_YCbCr_420_SP
		|| msg->win0.format == RGA_FORMAT_YCrCb_420_SP
		|| msg->win0.format == RGA_FORMAT_YCbCr_420_SP_10B
		|| msg->win0.format == RGA_FORMAT_YCrCb_420_SP_10B)
		uv_stride = ((msg->win0.vir_w + 15) & ~15) >> 2;
	else
		uv_stride = stride;

	*bRGA3_WIN0_Y_BASE = (u32) msg->win0.yrgb_addr;
	*bRGA3_WIN0_U_BASE = (u32) msg->win0.uv_addr;
	*bRGA3_WIN0_V_BASE = (u32) msg->win0.v_addr;

	*bRGA3_WIN0_VIR_STRIDE = stride;
	*bRGA3_WIN0_UV_VIR_STRIDE = uv_stride;

	*bRGA3_WIN0_ACT_OFF = msg->win0.x_offset | (msg->win0.y_offset << 16);
	/* fbcd offset */
	/*
	 *	*bRGA3_WIN0_FBC_OFF = msg->win0.fbc_x_offset |
	 *		 (msg->win0.fbc_y_offset << 16);
	 */

	/* do not use win0 src size except fbcd */
	*bRGA3_WIN0_SRC_SIZE = (msg->win0.src_act_w +
		msg->win0.x_offset) | ((msg->win0.y_offset +
		msg->win0.src_act_h) << 16);
	*bRGA3_WIN0_ACT_SIZE =
		msg->win0.src_act_w | (msg->win0.src_act_h << 16);
	*bRGA3_WIN0_DST_SIZE =
		msg->win0.dst_act_w | (msg->win0.dst_act_h << 16);
}

static void RGA3_set_reg_win1_info(u8 *base, struct rga3_req *msg)
{
	u32 *bRGA3_WIN1_RD_CTRL;
	u32 *bRGA3_WIN1_Y_BASE, *bRGA3_WIN1_U_BASE, *bRGA3_WIN1_V_BASE;
	u32 *bRGA3_WIN1_VIR_STRIDE;
	u32 *bRGA3_WIN1_UV_VIR_STRIDE;
	u32 *bRGA3_WIN1_SRC_SIZE;
	u32 *bRGA3_WIN1_ACT_OFF;
	u32 *bRGA3_WIN1_ACT_SIZE;
	u32 *bRGA3_WIN1_DST_SIZE;

	u32 *bRGA3_WIN1_SCL_FAC;
	/* Not used yet. */
	// u32 *bRGA3_WIN1_FBC_OFF;

	u32 sw = 0, sh = 0;
	u32 dw = 0, dh = 0;
	u32 param_x = 0, param_y = 0;
	u8 x_up = 0, y_up = 0, x_by = 0, y_by = 0;

	u32 reg = 0;

	u8 win_format = 0;
	u8 win_yc_swp = 0;

	/* rb swap on RGB, uv swap on YUV */
	u8 win_pix_swp = 0;

	/*
	 * 1: Semi planar, for yuv 4:2:x
	 * 2: Interleaved (yuyv), for yuv422 8bit only ，RGB
	 */
	u8 win_interleaved = 1;

	u8 pixel_width = 1;
	u8 yuv10 = 0;

	/* enable r2y or y2r */
	u8 win_r2y = 0;
	u8 win_y2r = 0;

	u8 rotate_mode = 0;
	u8 xmirror = 0;
	u8 ymirror = 0;

	u32 stride = 0;
	u32 uv_stride = 0;

	bRGA3_WIN1_RD_CTRL = (u32 *) (base + RGA3_WIN1_RD_CTRL_OFFSET);

	bRGA3_WIN1_Y_BASE = (u32 *) (base + RGA3_WIN1_Y_BASE_OFFSET);
	bRGA3_WIN1_U_BASE = (u32 *) (base + RGA3_WIN1_U_BASE_OFFSET);
	bRGA3_WIN1_V_BASE = (u32 *) (base + RGA3_WIN1_V_BASE_OFFSET);

	bRGA3_WIN1_VIR_STRIDE = (u32 *) (base + RGA3_WIN1_VIR_STRIDE_OFFSET);
	bRGA3_WIN1_UV_VIR_STRIDE =
		(u32 *) (base + RGA3_WIN1_UV_VIR_STRIDE_OFFSET);

	/* Not used yet. */
	// bRGA3_WIN1_FBC_OFF = (u32 *) (base + RGA3_WIN1_FBC_OFF_OFFSET);
	bRGA3_WIN1_ACT_OFF = (u32 *) (base + RGA3_WIN1_ACT_OFF_OFFSET);
	bRGA3_WIN1_SRC_SIZE = (u32 *) (base + RGA3_WIN1_SRC_SIZE_OFFSET);
	bRGA3_WIN1_ACT_SIZE = (u32 *) (base + RGA3_WIN1_ACT_SIZE_OFFSET);
	bRGA3_WIN1_DST_SIZE = (u32 *) (base + RGA3_WIN1_DST_SIZE_OFFSET);

	bRGA3_WIN1_SCL_FAC = (u32 *) (base + RGA3_WIN1_SCL_FAC_OFFSET);

	if (msg->win1.rotate_mode != 0) {
		switch (msg->rotate_mode) {
			/* rot 90 */
		case 0x1:
			rotate_mode = 1;
			break;
			/* rot 180 */
		case 0x2:
			xmirror = 1;
			ymirror = 1;
			break;
			/* rot 270 or rot -90 */
		case 0x3:
			rotate_mode = 1;
			xmirror = 1;
			ymirror = 1;
			break;
			/* ymirror */
		case 0x4:
			ymirror = 1;
			break;
			/* xmirror */
		case 0x5:
			xmirror = 1;
			break;
			/* rot 90 + xmirror */
		case 0x6:
			rotate_mode = 1;
			xmirror = 1;
			break;
			/* rot 90 + ymirror */
		case 0x7:
			rotate_mode = 1;
			ymirror = 1;
			break;
			/* bypass */
		};
	}

	/* scale */
	dw = msg->win1.dst_act_w;
	dh = msg->win1.dst_act_h;

	if (msg->rotate_mode == 1 || msg->rotate_mode == 3 ||
		msg->rotate_mode == 6 || msg->rotate_mode == 7) {
		sh = msg->win1.src_act_w;
		sw = msg->win1.src_act_h;
	} else {
		sw = msg->win1.src_act_w;
		sh = msg->win1.src_act_h;
	}

	if (sw > dw) {
		x_up = 0;
		x_by = 0;
	} else if (sw < dw) {
		x_up = 1;
		x_by = 0;
	} else {
		x_up = 0;
		x_by = 1;
	}

	if (sh > dh) {
		y_up = 0;
		y_by = 0;
	} else if (sh < dh) {
		y_up = 1;
		y_by = 0;
	} else {
		y_up = 0;
		y_by = 1;
	}

	if (x_by == 1)
		param_x = 0;
	else if (x_up == 1) {
		param_x = (FACTOR_MAX * (sw - 1)) / (dw - 1);
		if ((sw - 1) % (dw - 1) == 0) {
			pr_err("hor_up_fac modify xxxx\n");
			param_x = param_x - 1;
		}
	} else
		param_x = (FACTOR_MAX * (dw - 1)) / (sw - 1) + 1;

	if (y_by == 1)
		param_y = 0;
	else if (y_up == 1) {
		param_y = (FACTOR_MAX * (sh - 1)) / (dh - 1);
		if ((sh - 1) % (dh - 1) == 0) {
			pr_err("ver_up_fac modify yyyy\n");
			param_y = param_y - 1;
		}
	} else
		param_y = (FACTOR_MAX * (dh - 1)) / (sh - 1) + 1;

	switch (msg->win1.format) {
	case RGA_FORMAT_RGBA_8888:
		win_format = 0x8;
		pixel_width = 4;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_BGRA_8888:
		win_format = 0x6;
		pixel_width = 4;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_ARGB_8888:
		win_format = 0x9;
		pixel_width = 4;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_ABGR_8888:
		win_format = 0x7;
		pixel_width = 4;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_RGB_888:
		win_format = 0x5;
		pixel_width = 3;
		win_interleaved = 2;
		win_pix_swp = 1;
		break;
	case RGA_FORMAT_BGR_888:
		win_format = 0x5;
		pixel_width = 3;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_RGB_565:
		win_format = 0x4;
		pixel_width = 2;
		win_interleaved = 2;
		win_pix_swp = 1;
		break;
	case RGA_FORMAT_BGR_565:
		win_format = 0x4;
		pixel_width = 2;
		win_interleaved = 2;
		break;

	case RGA_FORMAT_YVYU_422:
		win_format = 0x1;
		pixel_width = 2;
		win_pix_swp = 1;
		win_yc_swp = 1;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_VYUY_422:
		win_format = 0x1;
		pixel_width = 2;
		win_pix_swp = 1;
		win_yc_swp = 0;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_YUYV_422:
		win_format = 0x1;
		pixel_width = 2;
		win_pix_swp = 0;
		win_yc_swp = 1;
		win_interleaved = 2;
		break;
	case RGA_FORMAT_UYVY_422:
		win_format = 0x1;
		pixel_width = 2;
		win_pix_swp = 0;
		win_yc_swp = 0;
		win_interleaved = 2;
		break;

	case RGA_FORMAT_YCbCr_422_SP:
		win_format = 0x1;
		break;
	case RGA_FORMAT_YCbCr_420_SP:
		win_format = 0x0;
		break;
	case RGA_FORMAT_YCrCb_422_SP:
		win_format = 0x1;
		win_pix_swp = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP:
		win_format = 0x0;
		win_pix_swp = 1;
		break;

	case RGA_FORMAT_YCbCr_420_SP_10B:
		win_format = 0x2;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP_10B:
		win_format = 0x2;
		win_pix_swp = 1;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCbCr_422_SP_10B:
		win_format = 0x3;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCrCb_422_SP_10B:
		win_format = 0x3;
		win_pix_swp = 1;
		yuv10 = 1;
		break;
	};

	if (rga_is_rgb_format(msg->win1.format) &&
	    rga_is_yuv_format(msg->wr.format))
		win_r2y = 1;
	if (rga_is_yuv_format(msg->win1.format) &&
	    rga_is_rgb_format(msg->wr.format))
		win_y2r = 1;

	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_R2Y_EN)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_R2Y_EN(win_r2y)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_Y2R_EN)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_Y2R_EN(win_y2r)));

	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_PIC_FORMAT)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_PIC_FORMAT(win_format)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_PIX_SWAP)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_PIX_SWAP(win_pix_swp)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_YC_SWAP)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_YC_SWAP(win_yc_swp)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_FORMAT)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_FORMAT(win_interleaved)));

	if (win_r2y == 1) {
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE)) |
			(s_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE(msg->win1.r2y_mode)));
	} else if (win_y2r == 1) {
		reg =
			((reg & (~m_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE)) |
			(s_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE(msg->win1.y2r_mode)));
	}

	/* rotate & mirror */
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_ROT)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_ROT(rotate_mode)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_XMIRROR)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_XMIRROR(xmirror)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_YMIRROR)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_YMIRROR(ymirror)));
	//warning: TRM not complete
	/* scale */
	*bRGA3_WIN1_SCL_FAC = param_x | param_y << 16;

	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_HOR_BY)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_HOR_BY(x_by)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_HOR_UP)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_HOR_UP(x_up)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_VER_BY)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_VER_BY(y_by)));
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_VER_UP)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_VER_UP(y_up)));

	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_YUV10B_COMPACT)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_YUV10B_COMPACT(1)));

	/* Only on roster mode, yuv 10bit can change to compact or set endian */
	if (msg->win1.rd_mode == RGA_RASTER_MODE && yuv10 == 1) {
		reg =
			((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_YUV10B_COMPACT)) |
			 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_YUV10B_COMPACT
			 (msg->win1.is_10b_compact)));
		reg =
			((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_ENDIAN_MODE)) |
			 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_ENDIAN_MODE
			 (msg->win1.is_10b_endian)));
	}

	/* rd_mode */
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_MODE)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_MODE(msg->win1.rd_mode)));
	/* win1 enable */
	reg =
		((reg & (~m_RGA3_WIN1_RD_CTRL_SW_WIN1_ENABLE)) |
		 (s_RGA3_WIN1_RD_CTRL_SW_WIN1_ENABLE(msg->win1.enable)));

	*bRGA3_WIN1_RD_CTRL = reg;

	/* stride need align to 16 */
	if (msg->win1.rd_mode != 1)
		stride = (((msg->win1.vir_w * pixel_width) + 15) & ~15) >> 2;
	else
		stride = ((msg->win1.vir_w + 15) & ~15) >> 2;

	if (msg->win1.format == RGA_FORMAT_YCbCr_420_SP
		|| msg->win1.format == RGA_FORMAT_YCrCb_420_SP
		|| msg->win1.format == RGA_FORMAT_YCbCr_420_SP_10B
		|| msg->win1.format == RGA_FORMAT_YCrCb_420_SP_10B)
		uv_stride = ((msg->win1.vir_w + 15) & ~15) >> 2;
	else
		uv_stride = stride;

	*bRGA3_WIN1_Y_BASE = (u32) msg->win1.yrgb_addr;
	*bRGA3_WIN1_U_BASE = (u32) msg->win1.uv_addr;
	*bRGA3_WIN1_V_BASE = (u32) msg->win1.v_addr;

	*bRGA3_WIN1_VIR_STRIDE = stride;
	*bRGA3_WIN1_UV_VIR_STRIDE = uv_stride;

	*bRGA3_WIN1_ACT_OFF = msg->win1.x_offset | (msg->win1.y_offset << 16);
	/* fbcd offset */
	/*
	 *		 *bRGA3_WIN1_FBC_OFF = msg->win1.fbc_x_offset |
	 *			(msg->win1.fbc_y_offset << 16);
	 */

	/* do not use win1 src size except fbcd */
	*bRGA3_WIN1_SRC_SIZE = (msg->win1.src_act_w +
		msg->win1.x_offset) | ((msg->win1.src_act_h +
		msg->win1.y_offset) << 16);
	*bRGA3_WIN1_ACT_SIZE =
		msg->win1.src_act_w | (msg->win1.src_act_h << 16);
	*bRGA3_WIN1_DST_SIZE =
		msg->win1.dst_act_w | (msg->win1.dst_act_h << 16);
}

static void RGA3_set_reg_wr_info(u8 *base, struct rga3_req *msg)
{
	u32 *bRGA3_WR_RD_CTRL;
	u32 *bRGA3_WR_Y_BASE, *bRGA3_WR_U_BASE, *bRGA3_WR_V_BASE;
	u32 *bRGA3_WR_VIR_STRIDE;
	u32 *bRGA3_WR_PL_VIR_STRIDE;
	u32 *bRGA3_WR_FBCD_CTRL;

	u32 reg = 0;
	u32 fbcd_reg = 0;

	u8 wr_format = 0;
	u8 wr_yc_swp = 0;

	/* rb swap on RGB, uv swap on YUV */
	u8 wr_pix_swp = 0;

	u8 pixel_width = 1;
	u8 yuv10 = 0;

	/*
	 * 1: Semi planar, for yuv 4:2:x
	 * 2: Interleaved (yuyv), for yuv422 8bit only ，RGB
	 */
	u8 wr_interleaved = 1;

	u32 stride = 0;
	u32 uv_stride = 0;

	u32 vir_h = 0;

	bRGA3_WR_RD_CTRL = (u32 *) (base + RGA3_WR_CTRL_OFFSET);
	bRGA3_WR_FBCD_CTRL = (u32 *) (base + RGA3_WR_FBCE_CTRL_OFFSET);

	bRGA3_WR_Y_BASE = (u32 *) (base + RGA3_WR_Y_BASE_OFFSET);
	bRGA3_WR_U_BASE = (u32 *) (base + RGA3_WR_U_BASE_OFFSET);
	bRGA3_WR_V_BASE = (u32 *) (base + RGA3_WR_V_BASE_OFFSET);

	bRGA3_WR_VIR_STRIDE = (u32 *) (base + RGA3_WR_VIR_STRIDE_OFFSET);
	bRGA3_WR_PL_VIR_STRIDE =
		(u32 *) (base + RGA3_WR_PL_VIR_STRIDE_OFFSET);

	switch (msg->wr.format) {
	case RGA_FORMAT_RGBA_8888:
		wr_format = 0x6;
		pixel_width = 4;
		wr_interleaved = 2;
		wr_pix_swp = 1;
		break;
	case RGA_FORMAT_BGRA_8888:
		wr_format = 0x6;
		pixel_width = 4;
		wr_interleaved = 2;
		break;
	case RGA_FORMAT_RGB_888:
		wr_format = 0x5;
		pixel_width = 3;
		wr_interleaved = 2;
		wr_pix_swp = 1;
		break;
	case RGA_FORMAT_BGR_888:
		wr_format = 0x5;
		pixel_width = 3;
		wr_interleaved = 2;
		break;
	case RGA_FORMAT_RGB_565:
		wr_format = 0x4;
		pixel_width = 2;
		wr_interleaved = 2;
		wr_pix_swp = 1;
		break;
	case RGA_FORMAT_BGR_565:
		wr_format = 0x4;
		pixel_width = 2;
		wr_interleaved = 2;
		break;

	case RGA_FORMAT_YVYU_422:
		wr_format = 0x1;
		pixel_width = 2;
		wr_pix_swp = 1;
		wr_yc_swp = 1;
		wr_interleaved = 2;
		break;
	case RGA_FORMAT_VYUY_422:
		wr_format = 0x1;
		pixel_width = 2;
		wr_pix_swp = 1;
		wr_yc_swp = 0;
		wr_interleaved = 2;
		break;
	case RGA_FORMAT_YUYV_422:
		wr_format = 0x1;
		pixel_width = 2;
		wr_pix_swp = 0;
		wr_yc_swp = 1;
		wr_interleaved = 2;
		break;
	case RGA_FORMAT_UYVY_422:
		wr_format = 0x1;
		pixel_width = 2;
		wr_pix_swp = 0;
		wr_yc_swp = 0;
		wr_interleaved = 2;
		break;

	case RGA_FORMAT_YCbCr_422_SP:
		wr_format = 0x1;
		break;
	case RGA_FORMAT_YCbCr_420_SP:
		wr_format = 0x0;
		break;
	case RGA_FORMAT_YCrCb_422_SP:
		wr_format = 0x1;
		wr_pix_swp = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP:
		wr_format = 0x0;
		wr_pix_swp = 1;
		break;

	case RGA_FORMAT_YCbCr_420_SP_10B:
		wr_format = 0x2;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP_10B:
		wr_format = 0x2;
		wr_pix_swp = 1;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCbCr_422_SP_10B:
		wr_format = 0x3;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCrCb_422_SP_10B:
		wr_format = 0x3;
		wr_pix_swp = 1;
		yuv10 = 1;
		break;
	};

	reg =
		((reg & (~m_RGA3_WR_CTRL_SW_WR_PIC_FORMAT)) |
		 (s_RGA3_WR_CTRL_SW_WR_PIC_FORMAT(wr_format)));
	reg =
		((reg & (~m_RGA3_WR_CTRL_SW_WR_PIX_SWAP)) |
		 (s_RGA3_WR_CTRL_SW_WR_PIX_SWAP(wr_pix_swp)));
	reg =
		((reg & (~m_RGA3_WR_CTRL_SW_WR_YC_SWAP)) |
		 (s_RGA3_WR_CTRL_SW_WR_YC_SWAP(wr_yc_swp)));
	reg =
		((reg & (~m_RGA3_WR_CTRL_SW_WR_FORMAT)) |
		 (s_RGA3_WR_CTRL_SW_WR_FORMAT(wr_interleaved)));
	reg =
		((reg & (~m_RGA3_WR_CTRL_SW_WR_FBCE_SPARSE_EN)) |
		 (s_RGA3_WR_CTRL_SW_WR_FBCE_SPARSE_EN(0)));

	reg =
		((reg & (~m_RGA3_WR_CTRL_SW_OUTSTANDING_MAX)) |
		 (s_RGA3_WR_CTRL_SW_OUTSTANDING_MAX(0xf)));

	reg =
		((reg & (~m_RGA3_WR_CTRL_SW_WR_YUV10B_COMPACT)) |
		 (s_RGA3_WR_CTRL_SW_WR_YUV10B_COMPACT(1)));

	/* Only on roster mode, yuv 10bit can change to compact or set endian */
	if (msg->wr.rd_mode == 0 && yuv10 == 1) {
		reg =
			((reg & (~m_RGA3_WR_CTRL_SW_WR_YUV10B_COMPACT)) |
			 (s_RGA3_WR_CTRL_SW_WR_YUV10B_COMPACT
			 (msg->wr.is_10b_compact)));
		reg =
			((reg & (~m_RGA3_WR_CTRL_SW_WR_ENDIAN_MODE)) |
			 (s_RGA3_WR_CTRL_SW_WR_ENDIAN_MODE
			 (msg->wr.is_10b_endian)));
	}

	/* rd_mode */
	reg =
		((reg & (~m_RGA3_WR_CTRL_SW_WR_MODE)) |
		 (s_RGA3_WR_CTRL_SW_WR_MODE(msg->wr.rd_mode)));

	fbcd_reg = ((fbcd_reg & (~m_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_HOFF_DISS)) |
		 (s_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_HOFF_DISS(0)));

	*bRGA3_WR_RD_CTRL = reg;
	*bRGA3_WR_FBCD_CTRL = fbcd_reg;

	/* stride need align to 16 */
	if (msg->wr.rd_mode != 1) {
		stride = (((msg->wr.vir_w * pixel_width) + 15) & ~15) >> 2;
		*bRGA3_WR_U_BASE = (u32) msg->wr.uv_addr;
		uv_stride = ((msg->wr.vir_w + 15) & ~15) >> 2;
	} else {
		stride = ((msg->wr.vir_w + 15) & ~15) >> 2;
		/* need to calculate fbcd header size */
		vir_h = ((msg->wr.vir_h + 15) & ~15);
		*bRGA3_WR_U_BASE = (u32) (msg->wr.uv_addr + ((stride * vir_h)>>2));
		/* RGBA8888 */
		if (wr_format == 0x6)
			uv_stride = ((msg->wr.vir_w + 15) & ~15);
		/* RGB888 */
		else if (wr_format == 0x5)
			uv_stride = (((msg->wr.vir_w + 15) & ~15) >> 2) * 3;
		/* RGB565, yuv422 8bit, yuv420 10bit */
		else if (wr_format == 0x4 || wr_format == 0x1 || wr_format == 0x2)
			uv_stride = ((msg->wr.vir_w + 15) & ~15) >> 1;
		/* yuv420 8bit */
		else if (wr_format == 0x0)
			uv_stride = (((msg->wr.vir_w + 15) & ~15) >> 3) * 3;
		/* yuv422 10bit */
		else if (wr_format == 0x3)
			uv_stride = (((msg->wr.vir_w + 15) & ~15) >> 3) * 5;
	}

	*bRGA3_WR_Y_BASE = (u32) msg->wr.yrgb_addr;
	*bRGA3_WR_V_BASE = (u32) msg->wr.v_addr;

	*bRGA3_WR_VIR_STRIDE = stride;
	*bRGA3_WR_PL_VIR_STRIDE = uv_stride;
}

static void RGA3_set_reg_overlap_info(u8 *base, struct rga3_req *msg)
{
	u32 *bRGA_OVERLAP_TOP_CTRL;
	u32 *bRGA_OVERLAP_BOT_CTRL;
	u32 *bRGA_OVERLAP_TOP_ALPHA;
	u32 *bRGA_OVERLAP_BOT_ALPHA;
	u32 *bRGA_OVERLAP_TOP_KEY_MIN;
	u32 *bRGA_OVERLAP_TOP_KEY_MAX;

	u32 *bRGA_OVERLAP_CTRL;
	u32 *bRGA3_OVLP_OFF;

	u32 reg;

	bRGA_OVERLAP_TOP_CTRL = (u32 *) (base + RGA3_OVLP_TOP_CTRL_OFFSET);
	bRGA_OVERLAP_BOT_CTRL = (u32 *) (base + RGA3_OVLP_BOT_CTRL_OFFSET);
	bRGA_OVERLAP_TOP_ALPHA = (u32 *) (base + RGA3_OVLP_TOP_ALPHA_OFFSET);
	bRGA_OVERLAP_BOT_ALPHA = (u32 *) (base + RGA3_OVLP_BOT_ALPHA_OFFSET);

	bRGA_OVERLAP_CTRL = (u32 *) (base + RGA3_OVLP_CTRL_OFFSET);
	bRGA3_OVLP_OFF = (u32 *) (base + RGA3_OVLP_OFF_OFFSET);

	/* Alpha blend */
	/*bot -> win0(dst), top -> win1(src). */
	reg = 0;
	reg =
		((reg & (~m_RGA3_OVLP_TOP_CTRL_SW_TOP_COLOR_M0)) |
		 (s_RGA3_OVLP_TOP_CTRL_SW_TOP_COLOR_M0
		 (msg->alpha_mode_0 >> 7)));
	reg =
		((reg & (~m_RGA3_OVLP_TOP_CTRL_SW_TOP_ALPHA_M0)) |
		 (s_RGA3_OVLP_TOP_CTRL_SW_TOP_ALPHA_M0
		 (msg->alpha_mode_0 >> 0)));
	reg =
		((reg & (~m_RGA3_OVLP_TOP_CTRL_SW_TOP_BLEND_M0)) |
		 (s_RGA3_OVLP_TOP_CTRL_SW_TOP_BLEND_M0
		 (msg->alpha_mode_0 >> 1)));
	reg =
		((reg & (~m_RGA3_OVLP_TOP_CTRL_SW_TOP_ALPHA_CAL_M0)) |
		 (s_RGA3_OVLP_TOP_CTRL_SW_TOP_ALPHA_CAL_M0
		 (msg->alpha_mode_0 >> 3)));
	reg =
		((reg & (~m_RGA3_OVLP_TOP_CTRL_SW_TOP_FACTOR_M0)) |
		 (s_RGA3_OVLP_TOP_CTRL_SW_TOP_FACTOR_M0
		 (msg->alpha_mode_0 >> 4)));
	reg =
		((reg & (~m_RGA3_OVLP_TOP_CTRL_SW_TOP_GLOBAL_ALPHA)) |
		 (s_RGA3_OVLP_TOP_CTRL_SW_TOP_GLOBAL_ALPHA
		 (msg->win1_a_global_val)));
	*bRGA_OVERLAP_TOP_CTRL = reg;

	reg = 0;
	reg =
		((reg & (~m_RGA3_OVLP_BOT_CTRL_SW_BOT_COLOR_M0)) |
		 (s_RGA3_OVLP_BOT_CTRL_SW_BOT_COLOR_M0
		 (msg->alpha_mode_0 >> 15)));
	reg =
		((reg & (~m_RGA3_OVLP_BOT_CTRL_SW_BOT_ALPHA_M0)) |
		 (s_RGA3_OVLP_BOT_CTRL_SW_BOT_ALPHA_M0
		 (msg->alpha_mode_0 >> 8)));
	reg =
		((reg & (~m_RGA3_OVLP_BOT_CTRL_SW_BOT_BLEND_M0)) |
		 (s_RGA3_OVLP_BOT_CTRL_SW_BOT_BLEND_M0
		 (msg->alpha_mode_0 >> 9)));
	reg =
		((reg & (~m_RGA3_OVLP_BOT_CTRL_SW_BOT_ALPHA_CAL_M0)) |
		 (s_RGA3_OVLP_BOT_CTRL_SW_BOT_ALPHA_CAL_M0
		 (msg->alpha_mode_0 >> 11)));
	reg =
		((reg & (~m_RGA3_OVLP_BOT_CTRL_SW_BOT_FACTOR_M0)) |
		 (s_RGA3_OVLP_BOT_CTRL_SW_BOT_FACTOR_M0
		 (msg->alpha_mode_0 >> 12)));
	reg =
		((reg & (~m_RGA3_OVLP_BOT_CTRL_SW_BOT_GLOBAL_ALPHA)) |
		 (s_RGA3_OVLP_BOT_CTRL_SW_BOT_GLOBAL_ALPHA
		 (msg->win0_a_global_val)));
	*bRGA_OVERLAP_BOT_CTRL = reg;

	reg = 0;
	reg =
		((reg & (~m_RGA3_OVLP_TOP_ALPHA_SW_TOP_ALPHA_M1)) |
		 (s_RGA3_OVLP_TOP_ALPHA_SW_TOP_ALPHA_M1
		 (msg->alpha_mode_1 >> 0)));
	reg =
		((reg & (~m_RGA3_OVLP_TOP_ALPHA_SW_TOP_BLEND_M1)) |
		 (s_RGA3_OVLP_TOP_ALPHA_SW_TOP_BLEND_M1
		 (msg->alpha_mode_1 >> 1)));
	reg =
		((reg & (~m_RGA3_OVLP_TOP_ALPHA_SW_TOP_ALPHA_CAL_M1)) |
		 (s_RGA3_OVLP_TOP_ALPHA_SW_TOP_ALPHA_CAL_M1
		 (msg->alpha_mode_1 >> 3)));
	reg =
		((reg & (~m_RGA3_OVLP_TOP_ALPHA_SW_TOP_FACTOR_M1)) |
		 (s_RGA3_OVLP_TOP_ALPHA_SW_TOP_FACTOR_M1
		 (msg->alpha_mode_1 >> 4)));
	*bRGA_OVERLAP_TOP_ALPHA = reg;

	reg = 0;
	reg =
		((reg & (~m_RGA3_OVLP_BOT_ALPHA_SW_BOT_ALPHA_M1)) |
		 (s_RGA3_OVLP_BOT_ALPHA_SW_BOT_ALPHA_M1
		 (msg->alpha_mode_1 >> 8)));
	reg =
		((reg & (~m_RGA3_OVLP_BOT_ALPHA_SW_BOT_BLEND_M1)) |
		 (s_RGA3_OVLP_BOT_ALPHA_SW_BOT_BLEND_M1
		 (msg->alpha_mode_1 >> 9)));
	reg =
		((reg & (~m_RGA3_OVLP_BOT_ALPHA_SW_BOT_ALPHA_CAL_M1)) |
		 (s_RGA3_OVLP_BOT_ALPHA_SW_BOT_ALPHA_CAL_M1
		 (msg->alpha_mode_1 >> 11)));
	reg =
		((reg & (~m_RGA3_OVLP_BOT_ALPHA_SW_BOT_FACTOR_M1)) |
		 (s_RGA3_OVLP_BOT_ALPHA_SW_BOT_FACTOR_M1
		 (msg->alpha_mode_1 >> 12)));

	*bRGA_OVERLAP_BOT_ALPHA = reg;

	/* set RGA_OVERLAP_CTRL */
	reg = 0;
	/* color key */
	bRGA_OVERLAP_TOP_KEY_MIN =
		(u32 *) (base + RGA3_OVLP_TOP_KEY_MIN_OFFSET);
	bRGA_OVERLAP_TOP_KEY_MAX =
		(u32 *) (base + RGA3_OVLP_TOP_KEY_MAX_OFFSET);

	/*
	 * YG : value		 (0:9)
	 * UB : value >> 10	 (10:19)
	 * VG : value >> 20	 (20:29)
	 */
	if (msg->color_key_min > 0 || msg->color_key_max > 0) {
		*bRGA_OVERLAP_TOP_KEY_MIN = msg->color_key_min;
		*bRGA_OVERLAP_TOP_KEY_MAX = msg->color_key_max;
		reg = ((reg & (~m_RGA3_OVLP_CTRL_SW_TOP_KEY_EN)) |
			 (s_RGA3_OVLP_CTRL_SW_TOP_KEY_EN(1)));
	}

	/* 1: ABB mode, 0: ABC mode， ABB cannot support fbc in&out */
	if (msg->win0.yrgb_addr == msg->wr.yrgb_addr)
		reg = ((reg & (~m_RGA3_OVLP_CTRL_SW_OVLP_MODE)) |
			(s_RGA3_OVLP_CTRL_SW_OVLP_MODE(1)));

	/* 1: yuv field, 0: rgb field */
	if (rga_is_yuv_format(msg->wr.format))
		reg = ((reg & (~m_RGA3_OVLP_CTRL_SW_OVLP_FIELD)) |
			 (s_RGA3_OVLP_CTRL_SW_OVLP_FIELD(1)));

	/*
	 * warning: if m1 & m0 need config split，need to redesign
	 * this judge, which consider RGBA8888 format
	 */
	if (msg->alpha_mode_1 > 0 && msg->alpha_mode_0 > 0)
		reg = ((reg & (~m_RGA3_OVLP_CTRL_SW_TOP_ALPHA_EN)) |
			 (s_RGA3_OVLP_CTRL_SW_TOP_ALPHA_EN(1)));

	*bRGA_OVERLAP_CTRL = reg;

	*bRGA3_OVLP_OFF = msg->wr.x_offset | (msg->wr.y_offset << 16);
}

int rga3_gen_reg_info(u8 *base, struct rga3_req *msg)
{
	switch (msg->render_mode) {
	case BITBLT_MODE:
		RGA3_set_reg_win0_info(base, msg);
		RGA3_set_reg_win1_info(base, msg);
		RGA3_set_reg_overlap_info(base, msg);
		RGA3_set_reg_wr_info(base, msg);
		break;
	default:
		pr_err("error msg render mode %d\n", msg->render_mode);
		break;
	}

	return 0;
}

static void addr_copy(struct rga_win_info_t *win, struct rga_img_info_t *img)
{
	win->yrgb_addr = img->yrgb_addr;
	win->uv_addr = img->uv_addr;
	win->v_addr = img->v_addr;
	win->enable = 1;
}

static void set_win_info(struct rga_win_info_t *win, struct rga_img_info_t *img)
{
	win->x_offset = img->x_offset;
	win->y_offset = img->y_offset;
	win->src_act_w = img->act_w;
	win->src_act_h = img->act_h;
	win->vir_w = img->vir_w;
	win->vir_h = img->vir_h;
	if (img->rd_mode == RGA_RASTER_MODE)
		win->rd_mode = 0;
	else if (img->rd_mode == RGA_FBC_MODE)
		win->rd_mode = 1;
	else if (img->rd_mode == RGA_TILE_MODE)
		win->rd_mode = 2;

	win->is_10b_compact = img->is_10b_compact;
	win->is_10b_endian = img->is_10b_endian;
}

static void set_wr_info(struct rga_req *req_rga, struct rga3_req *req)
{
	/* The output w/h are bound to the dst_act_w/h of win0. */
	req->wr.dst_act_w = req->win0.dst_act_w;
	req->wr.dst_act_h = req->win0.dst_act_h;

	/* Some configurations need to be all equal to the output w/h. */
	req->wr.vir_w = req_rga->dst.vir_w;
	req->wr.vir_h = req_rga->dst.vir_h;

	if (req_rga->dst.rd_mode == RGA_RASTER_MODE)
		req->wr.rd_mode = 0;
	else if (req_rga->dst.rd_mode == RGA_FBC_MODE)
		req->wr.rd_mode = 1;
	else if (req_rga->dst.rd_mode == RGA_TILE_MODE)
		req->wr.rd_mode = 2;

	req->wr.is_10b_compact = req_rga->dst.is_10b_compact;
	req->wr.is_10b_endian = req_rga->dst.is_10b_endian;
}

/* TODO: common part */
void rga_cmd_to_rga3_cmd(struct rga_req *req_rga, struct rga3_req *req)
{
	u16 alpha_mode_0, alpha_mode_1;

	req->render_mode = BITBLT_MODE;

	/* rotate & mirror */
	switch (req_rga->rotate_mode & 0x0f) {
	case 0x1:
		if (req_rga->sina == 65536 && req_rga->cosa == 0) {
			/* rot 90 */
			req->rotate_mode = 1;
		} else if (req_rga->sina == 0 && req_rga->cosa == -65536) {
			/* rot 180 */
			req->rotate_mode = 2;
		} else if (req_rga->sina == -65536 && req_rga->cosa == 0) {
			/* rot 270 or -90 */
			req->rotate_mode = 3;
		} else if (req_rga->sina == 0 && req_rga->cosa == 65536) {
			/* bypass */
			req->rotate_mode = 0;
		}
		break;
	case 0x2:
		/* xmirror */
		req->rotate_mode = 5;
		break;
	case 0x3:
		/* ymirror */
		req->rotate_mode = 4;
		break;
	case 0x4:
		/* x+y mirror = rot 180 */
		req->rotate_mode = 2;
		break;
	default:
		req->rotate_mode = 0;
		break;
	}

	switch ((req_rga->rotate_mode & 0xf0) >> 4) {
	/* xmirror */
	case 2:
		if (req->rotate_mode == 1) {
			/* xmirror + rot 90 */
			req->rotate_mode = 6;
		}
		break;
	/* ymirror */
	case 3:
		if (req->rotate_mode == 1) {
			/* ymirror + rot 90 */
			req->rotate_mode = 7;
		}
		break;
	}

	/* default use 2 reg, bot_blend_m1 && bot_alpha_cal_m1 */
	if (rga_is_alpha_format(req_rga->src.format))
		req->alpha_mode_1 = 0x0a00;

	/*
	 * Layer binding:
	 *     src => win1
	 *     src1/dst => win0
	 *     dst => wr
	 */
	/* simple win can not support dst offset */
	if ((!((req_rga->alpha_rop_flag) & 1)) &&
	    (req_rga->dst.x_offset == 0 && req_rga->dst.y_offset == 0)) {
		set_win_info(&req->win0, &req_rga->src);

		/* enable win0 rotate */
		req->win0.rotate_mode = 1;

		/* set win dst size */
		req->win0.dst_act_w = req_rga->dst.act_w;
		req->win0.dst_act_h = req_rga->dst.act_h;

		addr_copy(&req->win0, &req_rga->src);
		addr_copy(&req->wr, &req_rga->dst);

		req->win0.format = req_rga->src.format;
		req->wr.format = req_rga->dst.format;
	} else {
		set_win_info(&req->win1, &req_rga->src);

		/* enable win1 rotate */
		req->win1.rotate_mode = 1;

		addr_copy(&req->win1, &req_rga->src);
		addr_copy(&req->wr, &req_rga->dst);

		req->win1.format = req_rga->src.format;
		req->wr.format = req_rga->dst.format;

		if (req_rga->pat.yrgb_addr != 0) {
			/* A+B->C mode */
			set_win_info(&req->win0, &req_rga->pat);
			addr_copy(&req->win0, &req_rga->pat);
			req->win0.format = req_rga->pat.format;

			/* set win0 dst size */
			req->win0.dst_act_w = req_rga->pat.act_w;
			req->win0.dst_act_h = req_rga->pat.act_h;
			/* set win1 dst size */
			req->win1.dst_act_w = req_rga->pat.act_w;
			req->win1.dst_act_h = req_rga->pat.act_h;
		} else {
			/* A+B->B mode */
			set_win_info(&req->win0, &req_rga->dst);
			addr_copy(&req->win0, &req_rga->dst);
			req->win0.format = req_rga->dst.format;

			/* only win1 && wr support fbcd, win0 default raster */
			req->win0.rd_mode = 0;

			/* set win0 dst size */
			req->win0.dst_act_w = req_rga->dst.act_w;
			req->win0.dst_act_h = req_rga->dst.act_h;
			/* set win1 dst size */
			req->win1.dst_act_w = req_rga->dst.act_w;
			req->win1.dst_act_h = req_rga->dst.act_h;
		}

		/* dst offset need to config overlap offset */
		req->wr.x_offset = req_rga->dst.x_offset;
		req->wr.y_offset = req_rga->dst.y_offset;
	}
	set_wr_info(req_rga, req);

	if (req->rotate_mode == 1 || req->rotate_mode == 3 ||
		req->rotate_mode == 6 || req->rotate_mode == 7) {
		if (req->win1.yrgb_addr != 0) {
			/* ABB */
			if (req->win0.yrgb_addr == req->wr.yrgb_addr) {
				req->win1.dst_act_w = req_rga->dst.act_h;
				req->win1.dst_act_h = req_rga->dst.act_w;

				/* win0 do not need rotate, but net equal to wr */
				req->win0.dst_act_w = req_rga->dst.act_h;
				req->win0.dst_act_h = req_rga->dst.act_w;
				req->win0.src_act_w = req_rga->dst.act_h;
				req->win0.src_act_h = req_rga->dst.act_w;
			}
		} else {
			req->win0.rotate_mode = 1;
			req->win0.dst_act_w = req_rga->dst.act_h;
			req->win0.dst_act_h = req_rga->dst.act_w;
		}
	}

	/* overlap */
	/* Alpha blend mode */
	if (((req_rga->alpha_rop_flag) & 1)) {
		if ((req_rga->alpha_rop_flag >> 3) & 1) {
			/* porter duff alpha enable */
			switch (req_rga->PD_mode) {
			/* dst = 0 */
			case 0:
				break;
			/* dst = src */
			case 1:
				req->alpha_mode_0 = 0x0212;
				req->alpha_mode_1 = 0x0212;
				break;
			/* dst = dst */
			case 2:
				req->alpha_mode_0 = 0x1202;
				req->alpha_mode_1 = 0x1202;
				break;
			/* dst = (256*sc + (256 - sa)*dc) >> 8 */
			case 3:
				if ((req_rga->alpha_rop_mode & 3) == 0) {
					/* both use globalAlpha. */
					alpha_mode_0 = 0x3010;
					alpha_mode_1 = 0x3010;
				} else if ((req_rga->alpha_rop_mode & 3) == 1) {
					/* Do not use globalAlpha. */
					alpha_mode_0 = 0x3212;
					alpha_mode_1 = 0x3212;
				} else if ((req_rga->alpha_rop_mode & 3) == 2) {
					/*
					 * dst use globalAlpha,
					 * and dst has pixelAlpha.
					 */
					alpha_mode_0 = 0x3014;
					alpha_mode_1 = 0x3014;
				} else {
					/*
					 * dst use globalAlpha,
					 * and dst does not have pixelAlpha.
					 */
					alpha_mode_0 = 0x3012;
					alpha_mode_1 = 0x3012;
				}
				req->alpha_mode_0 = alpha_mode_0;
				req->alpha_mode_1 = alpha_mode_1;
				break;
			/* dst = (sc*(256-da) + 256*dc) >> 8 */
			case 4:
				/* Do not use globalAlpha. */
				req->alpha_mode_0 = 0x1232;
				req->alpha_mode_1 = 0x1232;
				break;
			/* dst = (da*sc) >> 8 */
			case 5:
				break;
			/* dst = (sa*dc) >> 8 */
			case 6:
				break;
			/* dst = ((256-da)*sc) >> 8 */
			case 7:
				break;
			/* dst = ((256-sa)*dc) >> 8 */
			case 8:
				break;
			/* dst = (da*sc + (256-sa)*dc) >> 8 */
			case 9:
				req->alpha_mode_0 = 0x3040;
				req->alpha_mode_1 = 0x3040;
				break;
			/* dst = ((256-da)*sc + (sa*dc)) >> 8 */
			case 10:
				break;
			/* dst = ((256-da)*sc + (256-sa)*dc) >> 8 */
			case 11:
				break;
			case 12:
				req->alpha_mode_0 = 0x0010;
				req->alpha_mode_1 = 0x0820;
				break;
			default:
				break;
			}
			/* Real color mode */
			if ((req_rga->alpha_rop_flag >> 9) & 1) {
				if (req->alpha_mode_0 & (0x01 << 1))
					req->alpha_mode_0 |= (1 << 7);
				if (req->alpha_mode_0 & (0x01 << 9))
					req->alpha_mode_0 |= (1 << 15);
			}
		} else {
			if ((req_rga->alpha_rop_mode & 3) == 0) {
				req->alpha_mode_0 = 0x3040;
				req->alpha_mode_1 = 0x3040;
			} else if ((req_rga->alpha_rop_mode & 3) == 1) {
				req->alpha_mode_0 = 0x3042;
				req->alpha_mode_1 = 0x3242;
			} else if ((req_rga->alpha_rop_mode & 3) == 2) {
				req->alpha_mode_0 = 0x3044;
				req->alpha_mode_1 = 0x3044;
			}
		}
	}

	req->win0_a_global_val = req_rga->alpha_global_value;
	req->win1_a_global_val = req_rga->alpha_global_value;

	/* yuv to rgb */
	/* 601 limit */
	if (req_rga->yuv2rgb_mode == 1) {
		req->win0.y2r_mode = 0;
		req->win1.y2r_mode = 0;
	/* 601 full */
	} else if (req_rga->yuv2rgb_mode == 2) {
		req->win0.y2r_mode = 2;
		req->win1.y2r_mode = 2;
	/* 709 limit */
	} else if (req_rga->yuv2rgb_mode == 3) {
		req->win0.y2r_mode = 1;
		req->win1.y2r_mode = 1;
	}

	/* rgb to yuv */
	/* 601 limit */
	if ((req_rga->yuv2rgb_mode >> 2) == 2) {
		req->win0.r2y_mode = 0;
		req->win1.r2y_mode = 0;
	/* 601 full */
	} else if ((req_rga->yuv2rgb_mode >> 2) == 1) {
		req->win0.r2y_mode = 2;
		req->win1.r2y_mode = 2;
	/* 709 limit */
	} else if ((req_rga->yuv2rgb_mode >> 2) == 3) {
		req->win0.r2y_mode = 1;
		req->win1.r2y_mode = 1;
	}

	/* color key */
	req->color_key_min = req_rga->color_key_min;
	req->color_key_max = req_rga->color_key_max;

	if (req_rga->mmu_info.mmu_en && (req_rga->mmu_info.mmu_flag & 1) == 1) {
		req->mmu_info.src0_mmu_flag = 1;
		req->mmu_info.src1_mmu_flag = 1;
		req->mmu_info.dst_mmu_flag = 1;
	}
}

void rga3_soft_reset(struct rga_scheduler_t *scheduler)
{
	u32 i;
	u32 reg;
	u32 mmu_addr;

	mmu_addr = rga_read(0xf00, scheduler);

	rga_write((1 << 3) | (1 << 4), RGA3_SYS_CTRL, scheduler);

	pr_err("soft reset sys_ctrl = %x, ro_rest = %x",
		rga_read(RGA3_SYS_CTRL, scheduler),
		rga_read(RGA3_RO_SRST, scheduler));

	mdelay(20);

	pr_err("soft reset sys_ctrl = %x, ro_rest = %x",
		rga_read(RGA3_SYS_CTRL, scheduler),
		rga_read(RGA3_RO_SRST, scheduler));

	rga_write((0 << 3) | (0 << 4), RGA3_SYS_CTRL, scheduler);

	pr_err("soft after reset sys_ctrl = %x, ro_rest = %x",
		rga_read(RGA3_SYS_CTRL, scheduler),
		rga_read(RGA3_RO_SRST, scheduler));

	rga_write(1, RGA3_INT_CLR, scheduler);

	rga_write(mmu_addr, 0xf00, scheduler);
	rga_write(0, 0xf08, scheduler);

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irq INT[%x], STATS0[%x], STATS1[%x]\n",
			rga_read(RGA3_INT_RAW, scheduler),
			rga_read(RGA3_STATUS0, scheduler),
			rga_read(RGA3_STATUS1, scheduler));

	for (i = 0; i < RGA_RESET_TIMEOUT; i++) {
		reg = rga_read(RGA3_SYS_CTRL, scheduler) & 1;

		if (reg == 0)
			break;

		udelay(1);
	}

	if (i == RGA_RESET_TIMEOUT)
		pr_err("soft reset timeout.\n");
}

static int rga3_scale_check(const struct rga3_req *req)
{
	u32 win0_saw, win0_sah, win0_daw, win0_dah;
	u32 win1_saw, win1_sah, win1_daw, win1_dah;

	win0_saw = req->win0.src_act_w;
	win0_sah = req->win0.src_act_h;
	win0_daw = req->win0.dst_act_w;
	win0_dah = req->win0.dst_act_h;

	if (((win0_saw >> 3) > win0_daw) || ((win0_sah >> 3) > win0_dah)) {
		pr_info("win0 unsupported to scaling less than 1/8 times.\n");
		return -EINVAL;
	}
	if (((win0_daw >> 3) > win0_saw) || ((win0_dah >> 3) > win0_sah)) {
		pr_info("win0 unsupported to scaling more than 8 times.\n");
		return -EINVAL;
	}

	if (req->win1.yrgb_addr != 0) {
		win1_saw = req->win1.src_act_w;
		win1_sah = req->win1.src_act_h;
		win1_daw = req->win1.dst_act_w;
		win1_dah = req->win1.dst_act_h;

		if (((win1_saw >> 3) > win1_daw) || ((win1_sah >> 3) > win1_dah)) {
			pr_info("win1 unsupported to scaling less than 1/8 times.\n");
			return -EINVAL;
		}
		if (((win1_daw >> 3) > win1_saw) || ((win1_dah >> 3) > win1_sah)) {
			pr_info("win1 unsupported to scaling more than 8 times.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int rga3_check_param(const struct rga3_req *req)
{
	if (!((req->render_mode == COLOR_FILL_MODE))) {
		if (unlikely((req->win0.src_act_w <= 0) ||
			(req->win0.src_act_w > 8176)
			 || (req->win0.src_act_h <= 0)
			 || (req->win0.src_act_h > 8176)
			 || (req->win0.dst_act_w <= 0)
			 || (req->win0.dst_act_w > 8128)
			 || (req->win0.dst_act_h <= 0)
			 || (req->win0.dst_act_h > 8128))) {
			pr_err("invalid win0 act sw = %d, sh = %d, dw = %d, dh = %d\n",
				 req->win0.src_act_w, req->win0.src_act_h,
				 req->win0.dst_act_w, req->win0.dst_act_h);
			return -EINVAL;
		}
	}

	if (req->win1.yrgb_addr != 0) {
		if (unlikely((req->win1.src_act_w <= 0) ||
			(req->win1.src_act_w > 8176)
			 || (req->win1.src_act_h <= 0)
			 || (req->win1.src_act_h > 8176)
			 || (req->win1.dst_act_w <= 0)
			 || (req->win1.dst_act_w > 8128)
			 || (req->win1.dst_act_h <= 0)
			 || (req->win1.dst_act_h > 8128))) {
			pr_err("invalid win1 act sw = %d, sh = %d, dw = %d, dh = %d\n",
				 req->win1.src_act_w, req->win1.src_act_h,
				 req->win1.dst_act_w, req->win1.dst_act_h);
			return -EINVAL;
		}

		if (unlikely
			((req->win1.vir_w <= 0) || (req->win1.vir_w > 8192 * 2)
			 || (req->win1.vir_h <= 0)
			 || (req->win1.vir_h > 8192 * 2))) {
			pr_err("invalid win1 stride vir_w = %d, vir_h = %d\n",
				 req->win1.vir_w, req->win1.vir_h);
			return -EINVAL;
		}

		/* warning: rotate mode skip this judge */
		if (req->rotate_mode == 0) {
			/* check win0 dst size > win1 dst size */
			if (unlikely
				((req->win1.dst_act_w > req->win0.dst_act_w)
				|| (req->win1.dst_act_h > req->win0.dst_act_h))) {
				pr_err("invalid win1.dst size = %d x %d\n",
					req->win1.dst_act_w, req->win1.dst_act_h);
				pr_err("invalid win0.dst size = %d x %d\n",
					req->win0.dst_act_w, req->win0.dst_act_h);
				return -EINVAL;
			}
		}
	}

	if (!((req->render_mode == COLOR_FILL_MODE))) {
		if (unlikely
			((req->win0.vir_w <= 0) || (req->win0.vir_w > 8192)
			 || (req->win0.vir_h <= 0)
			 || (req->win0.vir_h > 8192))) {
			pr_err("invalid win0 vir_w = %d, vir_h = %d\n",
				 req->win0.vir_w, req->win0.vir_h);
			return -EINVAL;
		}
	}

	if (unlikely
		((req->wr.vir_w <= 0) || (req->wr.vir_w > 8192 * 2)
		 || (req->wr.vir_h <= 0) || (req->wr.vir_h > 8192 * 2))) {
		pr_err("invalid wr vir_w = %d, vir_h = %d\n",
			 req->wr.vir_w, req->wr.vir_h);
		return -EINVAL;
	}

	if (rga3_scale_check(req) < 0)
		return -EINVAL;

	return 0;
}

static void print_debug_info(struct rga3_req *req)
{
	pr_info("render_mode:%s, bitblit_mode=%d, rotate_mode:%x\n",
		rga_get_render_mode_str(req->render_mode), req->bitblt_mode,
		req->rotate_mode);
	pr_info("win0: y = %lx uv = %lx v = %lx src_w = %d src_h = %d\n",
		 req->win0.yrgb_addr, req->win0.uv_addr, req->win0.v_addr,
		 req->win0.src_act_w, req->win0.src_act_h);
	pr_info("win0: vw = %d vh = %d xoff = %d yoff = %d format = %s\n",
		 req->win0.vir_w, req->win0.vir_h,
		 req->win0.x_offset, req->win0.y_offset,
		 rga_get_format_name(req->win0.format));
	pr_info("win0: dst_w = %d, dst_h = %d, rd_mode = %d\n",
		 req->win0.dst_act_w, req->win0.dst_act_h, req->win0.rd_mode);
	pr_info("win0: rot_mode = %d, en = %d, compact = %d, endian = %d\n",
		 req->win0.rotate_mode, req->win0.enable,
		 req->win0.is_10b_compact, req->win0.is_10b_endian);

	if (req->win1.yrgb_addr != 0 || req->win1.uv_addr != 0
		|| req->win1.v_addr != 0) {
		pr_info("win1: y = %lx uv = %lx v = %lx src_w = %d src_h = %d\n",
			 req->win1.yrgb_addr, req->win1.uv_addr,
			 req->win1.v_addr, req->win1.src_act_w,
			 req->win1.src_act_h);
		pr_info("win1: vw = %d vh = %d xoff = %d yoff = %d format = %s\n",
			 req->win1.vir_w, req->win1.vir_h,
			 req->win1.x_offset, req->win1.y_offset,
			 rga_get_format_name(req->win1.format));
		pr_info("win1: dst_w = %d, dst_h = %d, rd_mode = %d\n",
			 req->win1.dst_act_w, req->win1.dst_act_h,
			 req->win1.rd_mode);
		pr_info("win1: rot_mode = %d, en = %d, compact = %d, endian = %d\n",
			 req->win1.rotate_mode, req->win1.enable,
			 req->win1.is_10b_compact, req->win1.is_10b_endian);
	}

	pr_info("wr: y = %lx uv = %lx v = %lx vw = %d vh = %d\n",
		 req->wr.yrgb_addr, req->wr.uv_addr, req->wr.v_addr,
		 req->wr.vir_w, req->wr.vir_h);
	pr_info("wr: ovlp_xoff = %d ovlp_yoff = %d format = %s rdmode = %d\n",
		 req->wr.x_offset, req->wr.y_offset,
		 rga_get_format_name(req->wr.format), req->wr.rd_mode);

	pr_info("mmu: win0 = %.2x win1 = %.2x wr = %.2x\n",
		req->mmu_info.src0_mmu_flag, req->mmu_info.src1_mmu_flag,
		req->mmu_info.dst_mmu_flag);
	pr_info("alpha: flag %x mode0=%x mode1=%x\n", req->alpha_rop_flag,
		req->alpha_mode_0, req->alpha_mode_1);
	pr_info("blend mode is %s\n",
		rga_get_blend_mode_str(req->alpha_rop_flag, req->alpha_mode_0,
					req->alpha_mode_1));
	pr_info("yuv2rgb mode is %x\n", req->yuv2rgb_mode);
}

static int rga3_align_check(struct rga3_req *req)
{
	if (rga_is_yuv10bit_format(req->win0.format))
		if ((req->win0.vir_w % 64) || (req->win0.x_offset % 4) ||
			(req->win0.src_act_w % 4) || (req->win0.y_offset % 4) ||
			(req->win0.src_act_h % 4) || (req->win0.vir_h % 2))
			pr_info("yuv10bit err win0 wstride is not align\n");
	if (rga_is_yuv10bit_format(req->win1.format))
		if ((req->win1.vir_w % 64) || (req->win1.x_offset % 4) ||
			(req->win1.src_act_w % 4) || (req->win1.y_offset % 4) ||
			(req->win1.src_act_h % 4) || (req->win1.vir_h % 2))
			pr_info("yuv10bit err win1 wstride is not align\n");
	if (rga_is_yuv8bit_format(req->win0.format))
		if ((req->win0.vir_w % 16) || (req->win0.x_offset % 2) ||
			(req->win0.src_act_w % 2) || (req->win0.y_offset % 2) ||
			(req->win0.src_act_h % 2) || (req->win0.vir_h % 2))
			pr_info("yuv8bit err win0 wstride is not align\n");
	if (rga_is_yuv8bit_format(req->win1.format))
		if ((req->win1.vir_w % 16) || (req->win1.x_offset % 2) ||
			(req->win1.src_act_w % 2) || (req->win1.y_offset % 2) ||
			(req->win1.src_act_h % 2) || (req->win1.vir_h % 2))
			pr_info("yuv8bit err win1 wstride is not align\n");
	return 0;
}

int rga3_init_reg(struct rga_job *job)
{
	struct rga3_req req;
	int ret = 0;

	memset(&req, 0x0, sizeof(req));

	rga_cmd_to_rga3_cmd(&job->rga_command_base, &req);

	/* check value if legal */
	ret = rga3_check_param(&req);
	if (ret == -EINVAL) {
		pr_err("req argument is inval\n");
		return ret;
	}

	rga3_align_check(&req);

	/* for debug */
	if (DEBUGGER_EN(MSG))
		print_debug_info(&req);

	if (rga3_gen_reg_info((uint8_t *) job->cmd_reg, &req) == -1) {
		pr_err("RKA: gen reg info error\n");
		return -EINVAL;
	}

	return ret;
}

static void rga3_dump_read_back_reg(struct rga_scheduler_t *scheduler)
{
	int i;
	unsigned long flags;
	uint32_t cmd_reg[48] = {0};

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	for (i = 0; i < 48; i++)
		cmd_reg[i] = rga_read(0x100 + i * 4, scheduler);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	pr_info("CMD_READ_BACK_REG\n");
	for (i = 0; i < 12; i++)
		pr_info("i = %x : %.8x %.8x %.8x %.8x\n", i,
			cmd_reg[0 + i * 4], cmd_reg[1 + i * 4],
			cmd_reg[2 + i * 4], cmd_reg[3 + i * 4]);
}

int rga3_set_reg(struct rga_job *job, struct rga_scheduler_t *scheduler)
{
	ktime_t now = ktime_get();

	//rga_dma_flush_range(&job->cmd_reg[0], &job->cmd_reg[50], scheduler);

	rga_write(0x0, RGA3_SYS_CTRL, scheduler);

#if 0
	/* CMD buff */
	rga_write(virt_to_phys(job->cmd_reg), RGA3_CMD_ADDR, scheduler);
#else
	{
		int32_t m, *cmd;

		cmd = job->cmd_reg;
		for (m = 0; m <= 50; m++)
			rga_write(cmd[m], 0x100 + m * 4, scheduler);
	}
#endif

	if (DEBUGGER_EN(REG)) {
		int32_t i, *p;

		p = job->cmd_reg;
		pr_info("CMD_REG\n");
		for (i = 0; i < 12; i++)
			pr_info("i = %x : %.8x %.8x %.8x %.8x\n", i,
				p[0 + i * 4], p[1 + i * 4],
				p[2 + i * 4], p[3 + i * 4]);
	}

#if 0
	/* master mode */
	rga_write((0x1 << 1) | (0x1 << 2) | (0x1 << 5) | (0x1 << 6),
		 RGA3_SYS_CTRL, scheduler);
#else
	/* slave mode */
	//rga_write(1, 0xf08, scheduler);
#endif

	/* All CMD finish int */
	rga_write(1, RGA3_INT_EN, scheduler);

	if (DEBUGGER_EN(MSG)) {
		pr_err("sys_ctrl = %x, int_en = %x, int_raw = %x\n",
			 rga_read(RGA3_SYS_CTRL, scheduler),
			 rga_read(RGA3_INT_EN, scheduler),
			 rga_read(RGA3_INT_RAW, scheduler));

		pr_err("status0 = %x, status1 = %x\n",
			 rga_read(RGA3_STATUS0, scheduler),
			 rga_read(RGA3_STATUS1, scheduler));
	}

	if (DEBUGGER_EN(TIME))
		pr_err("set cmd use time = %lld\n", ktime_us_delta(now, job->timestamp));

	job->hw_running_time = now;
	job->hw_recoder_time = now;

	rga_write(1, RGA3_SYS_CTRL, scheduler);

	if (DEBUGGER_EN(REG))
		rga3_dump_read_back_reg(scheduler);

	return 0;
}

int rga3_get_version(struct rga_scheduler_t *scheduler)
{
	u32 major_version, minor_version, svn_version;
	u32 reg_version;

	if (!scheduler) {
		pr_err("scheduler is null\n");
		return -EINVAL;
	}

	reg_version = rga_read(RGA3_VERSION_NUM, scheduler);

	major_version = (reg_version & RGA3_MAJOR_VERSION_MASK) >> 28;
	minor_version = (reg_version & RGA3_MINOR_VERSION_MASK) >> 20;
	svn_version = (reg_version & RGA3_SVN_VERSION_MASK);

	snprintf(scheduler->version.str, 10, "%x.%01x.%05x", major_version,
		 minor_version, svn_version);

	scheduler->version.major = major_version;
	scheduler->version.minor = minor_version;
	scheduler->version.revision = svn_version;

	return 0;
}
