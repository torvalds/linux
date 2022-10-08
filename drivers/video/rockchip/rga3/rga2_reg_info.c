// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga2_reg: " fmt

#include "rga_job.h"
#include "rga2_reg_info.h"
#include "rga_dma_buf.h"
#include "rga_iommu.h"
#include "rga_common.h"
#include "rga_hw_config.h"
#include "rga_debugger.h"

unsigned int rga2_rop_code[256] = {
	0x00000007, 0x00000451, 0x00006051, 0x00800051,
	0x00007041, 0x00800041, 0x00804830, 0x000004f0,//0
	0x00800765, 0x000004b0, 0x00000065, 0x000004f4,
	0x00000075, 0x000004e6, 0x00804850, 0x00800005,

	0x00006850, 0x00800050, 0x00805028, 0x00000568,
	0x00804031, 0x00000471, 0x002b6071, 0x018037aa,//1
	0x008007aa, 0x00036071, 0x00002c6a, 0x00803631,
	0x00002d68, 0x00802721, 0x008002d0, 0x000006d0,

	0x0080066e, 0x00000528, 0x00000066, 0x0000056c,
	0x018007aa, 0x0002e06a, 0x00003471, 0x00834031,//2
	0x00800631, 0x0002b471, 0x00006071, 0x008037aa,
	0x000036d0, 0x008002d4, 0x00002d28, 0x000006d4,

	0x0000006e, 0x00000565, 0x00003451, 0x00800006,
	0x000034f0, 0x00834830, 0x00800348, 0x00000748,//3
	0x00002f48, 0x0080034c, 0x000034b0, 0x0000074c,
	0x00000031, 0x00834850, 0x000034e6, 0x00800071,

	0x008006f4, 0x00000431, 0x018007a1, 0x00b6e870,
	0x00000074, 0x0000046e, 0x00002561, 0x00802f28,//4
	0x00800728, 0x0002a561, 0x000026c2, 0x008002c6,
	0x00007068, 0x018035aa, 0x00002c2a, 0x000006c6,

	0x0000006c, 0x00000475, 0x000024e2, 0x008036b0,
	0x00804051, 0x00800004, 0x00800251, 0x00000651,
	0x00002e4a, 0x0080024e, 0x00000028, 0x00824842,
	0x000024a2, 0x0000064e, 0x000024f4, 0x00800068,//5

	0x008006b0, 0x000234f0, 0x00002741, 0x00800345,
	0x00003651, 0x00800255, 0x00000030, 0x00834051,
	0x00a34842, 0x000002b0, 0x00800271, 0x0002b651,
	0x00800368, 0x0002a741, 0x0000364e, 0x00806830,//6

	0x00006870, 0x008037a2, 0x00003431, 0x00000745,
	0x00002521, 0x00000655, 0x0000346e, 0x00800062,
	0x008002f0, 0x000236d0, 0x000026d4, 0x00807028,
	0x000036c6, 0x00806031, 0x008005aa, 0x00000671,//7

	0x00800671, 0x000005aa, 0x00006031, 0x008036c6,
	0x00007028, 0x00802e55, 0x008236d0, 0x000002f0,
	0x00000070, 0x0080346e, 0x00800655, 0x00802521,
	0x00800745, 0x00803431, 0x000037a2, 0x00806870,//8

	0x00006830, 0x0080364e, 0x00822f48, 0x00000361,
	0x0082b651, 0x00000271, 0x00800231, 0x002b4051,
	0x00034051, 0x00800030, 0x0080026e, 0x00803651,
	0x0080036c, 0x00802741, 0x008234f0, 0x000006b0,//9

	0x00000068, 0x00802c75, 0x0080064e, 0x008024a2,
	0x0002c04a, 0x00800021, 0x00800275, 0x00802e51,
	0x00800651, 0x00000251, 0x00800000, 0x00004051,
	0x000036b0, 0x008024e2, 0x00800475, 0x00000045,//a

	0x008006c6, 0x00802c2a, 0x000035aa, 0x00807068,
	0x008002f4, 0x008026c2, 0x00822d68, 0x00000728,
	0x00002f28, 0x00802561, 0x0080046e, 0x00000046,
	0x00836870, 0x000007a2, 0x00800431, 0x00004071,//b

	0x00000071, 0x008034e6, 0x00034850, 0x00800031,
	0x0080074c, 0x008034b0, 0x00800365, 0x00802f48,
	0x00800748, 0x00000341, 0x000026a2, 0x008034f0,
	0x00800002, 0x00005048, 0x00800565, 0x00000055,//c

	0x008006d4, 0x00802d28, 0x008002e6, 0x008036d0,
	0x000037aa, 0x00806071, 0x0082b471, 0x00000631,
	0x00002e2a, 0x00803471, 0x00826862, 0x010007aa,
	0x0080056c, 0x00000054, 0x00800528, 0x00005068,//d

	0x008006d0, 0x000002d0, 0x00002721, 0x00802d68,
	0x00003631, 0x00802c6a, 0x00836071, 0x000007aa,
	0x010037aa, 0x00a36870, 0x00800471, 0x00004031,
	0x00800568, 0x00005028, 0x00000050, 0x00800545,//e

	0x00800001, 0x00004850, 0x008004e6, 0x0000004e,
	0x008004f4, 0x0000004c, 0x008004b0, 0x00004870,
	0x008004f0, 0x00004830, 0x00000048, 0x0080044e,
	0x00000051, 0x008004d4, 0x00800451, 0x00800007,//f
};

static void RGA2_reg_get_param(unsigned char *base, struct rga2_req *msg)
{
	u32 *bRGA_SRC_X_FACTOR;
	u32 *bRGA_SRC_Y_FACTOR;
	u32 sw, sh;
	u32 dw, dh;
	u32 param_x, param_y;

	bRGA_SRC_X_FACTOR = (u32 *) (base + RGA2_SRC_X_FACTOR_OFFSET);
	bRGA_SRC_Y_FACTOR = (u32 *) (base + RGA2_SRC_Y_FACTOR_OFFSET);

	if (((msg->rotate_mode & 0x3) == 1) ||
		((msg->rotate_mode & 0x3) == 3)) {
		dw = msg->dst.act_h;
		dh = msg->dst.act_w;
	} else {
		dw = msg->dst.act_w;
		dh = msg->dst.act_h;
	}

	sw = msg->src.act_w;
	sh = msg->src.act_h;

	if (sw > dw) {
#if SCALE_DOWN_LARGE
		param_x = ((dw) << 16) / (sw) + 1;
#else
		param_x = ((dw) << 16) / (sw);
#endif
		*bRGA_SRC_X_FACTOR |= ((param_x & 0xffff) << 0);
	} else if (sw < dw) {
#if SCALE_UP_LARGE
		param_x = ((sw - 1) << 16) / (dw - 1);
#else
		param_x = ((sw) << 16) / (dw);
#endif
		*bRGA_SRC_X_FACTOR |= ((param_x & 0xffff) << 16);
	} else {
		*bRGA_SRC_X_FACTOR = 0;	//((1 << 14) << 16) | (1 << 14);
	}

	if (sh > dh) {
#if SCALE_DOWN_LARGE
		param_y = ((dh) << 16) / (sh) + 1;
#else
		param_y = ((dh) << 16) / (sh);
#endif
		*bRGA_SRC_Y_FACTOR |= ((param_y & 0xffff) << 0);
	} else if (sh < dh) {
#if SCALE_UP_LARGE
		param_y = ((sh - 1) << 16) / (dh - 1);
#else
		param_y = ((sh) << 16) / (dh);
#endif
		*bRGA_SRC_Y_FACTOR |= ((param_y & 0xffff) << 16);
	} else {
		*bRGA_SRC_Y_FACTOR = 0;	//((1 << 14) << 16) | (1 << 14);
	}
}

static void RGA2_set_mode_ctrl(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_MODE_CTL;
	u32 reg = 0;
	u32 render_mode = msg->render_mode;

	bRGA_MODE_CTL = (u32 *) (base + RGA2_MODE_CTRL_OFFSET);

	if (msg->render_mode == 4)
		render_mode = 3;

	/* In slave mode, the current frame completion interrupt must be enabled. */
	if (!RGA2_USE_MASTER_MODE)
		msg->CMD_fin_int_enable = 1;

	reg =
		((reg & (~m_RGA2_MODE_CTRL_SW_RENDER_MODE)) |
		 (s_RGA2_MODE_CTRL_SW_RENDER_MODE(render_mode)));
	reg =
		((reg & (~m_RGA2_MODE_CTRL_SW_BITBLT_MODE)) |
		 (s_RGA2_MODE_CTRL_SW_BITBLT_MODE(msg->bitblt_mode)));
	reg =
		((reg & (~m_RGA2_MODE_CTRL_SW_CF_ROP4_PAT)) |
		 (s_RGA2_MODE_CTRL_SW_CF_ROP4_PAT(msg->color_fill_mode)));
	reg =
		((reg & (~m_RGA2_MODE_CTRL_SW_ALPHA_ZERO_KET)) |
		 (s_RGA2_MODE_CTRL_SW_ALPHA_ZERO_KET(msg->alpha_zero_key)));
	reg =
		((reg & (~m_RGA2_MODE_CTRL_SW_GRADIENT_SAT)) |
		 (s_RGA2_MODE_CTRL_SW_GRADIENT_SAT(msg->alpha_rop_flag >> 7)));
	reg =
		((reg & (~m_RGA2_MODE_CTRL_SW_INTR_CF_E)) |
		 (s_RGA2_MODE_CTRL_SW_INTR_CF_E(msg->CMD_fin_int_enable)));

	reg = ((reg & (~m_RGA2_MODE_CTRL_SW_MOSAIC_EN)) |
	       (s_RGA2_MODE_CTRL_SW_MOSAIC_EN(msg->mosaic_info.enable)));

	reg = ((reg & (~m_RGA2_MODE_CTRL_SW_YIN_YOUT_EN)) |
	       (s_RGA2_MODE_CTRL_SW_YIN_YOUT_EN(msg->yin_yout_en)));

	reg = ((reg & (~m_RGA2_MODE_CTRL_SW_OSD_E)) |
	       (s_RGA2_MODE_CTRL_SW_OSD_E(msg->osd_info.enable)));

	*bRGA_MODE_CTL = reg;
}

static void RGA2_set_reg_src_info(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_SRC_INFO;
	u32 *bRGA_SRC_BASE0, *bRGA_SRC_BASE1, *bRGA_SRC_BASE2;
	u32 *bRGA_SRC_VIR_INFO;
	u32 *bRGA_SRC_ACT_INFO;
	u32 *bRGA_MASK_ADDR;
	u32 *bRGA_SRC_TR_COLOR0, *bRGA_SRC_TR_COLOR1;

	u8 disable_uv_channel_en = 0;

	u32 reg = 0;
	u8 src0_format = 0;

	u8 src0_rb_swp = 0;
	u8 src0_alpha_swp = 0;

	u8 src0_cbcr_swp = 0;
	u8 pixel_width = 1;
	u32 stride = 0;
	u32 uv_stride = 0;
	u32 mask_stride = 0;
	u32 ydiv = 1, xdiv = 2;
	u8 yuv10 = 0;

	u32 sw, sh;
	u32 dw, dh;
	u8 rotate_mode;
	u8 scale_w_flag, scale_h_flag;

	bRGA_SRC_INFO = (u32 *) (base + RGA2_SRC_INFO_OFFSET);

	bRGA_SRC_BASE0 = (u32 *) (base + RGA2_SRC_BASE0_OFFSET);
	bRGA_SRC_BASE1 = (u32 *) (base + RGA2_SRC_BASE1_OFFSET);
	bRGA_SRC_BASE2 = (u32 *) (base + RGA2_SRC_BASE2_OFFSET);

	bRGA_SRC_VIR_INFO = (u32 *) (base + RGA2_SRC_VIR_INFO_OFFSET);
	bRGA_SRC_ACT_INFO = (u32 *) (base + RGA2_SRC_ACT_INFO_OFFSET);

	bRGA_MASK_ADDR = (u32 *) (base + RGA2_MASK_BASE_OFFSET);

	bRGA_SRC_TR_COLOR0 = (u32 *) (base + RGA2_SRC_TR_COLOR0_OFFSET);
	bRGA_SRC_TR_COLOR1 = (u32 *) (base + RGA2_SRC_TR_COLOR1_OFFSET);

	if (msg->src.format == RGA_FORMAT_YCbCr_420_SP_10B ||
		msg->src.format == RGA_FORMAT_YCrCb_420_SP_10B) {
		if ((msg->src.act_w == msg->dst.act_w) &&
			(msg->src.act_h == msg->dst.act_h) &&
			(msg->rotate_mode == 0))
			msg->rotate_mode = 1 << 6;
	}

	{
		rotate_mode = msg->rotate_mode & 0x3;

		sw = msg->src.act_w;
		sh = msg->src.act_h;

		if ((rotate_mode == 1) | (rotate_mode == 3)) {
			dw = msg->dst.act_h;
			dh = msg->dst.act_w;
		} else {
			dw = msg->dst.act_w;
			dh = msg->dst.act_h;
		}

		if (sw > dw)
			scale_w_flag = 1;
		else if (sw < dw)
			scale_w_flag = 2;
		else {
			scale_w_flag = 0;
			if (msg->rotate_mode >> 6)
				scale_w_flag = 3;
		}

		if (sh > dh)
			scale_h_flag = 1;
		else if (sh < dh)
			scale_h_flag = 2;
		else {
			scale_h_flag = 0;
			if (msg->rotate_mode >> 6)
				scale_h_flag = 3;
		}

		/* uvvds need to force tile mode. */
		if (msg->uvvds_mode && scale_w_flag == 0)
			scale_w_flag = 3;
	}

	switch (msg->src.format) {
	case RGA_FORMAT_RGBA_8888:
		src0_format = 0x0;
		pixel_width = 4;
		break;
	case RGA_FORMAT_BGRA_8888:
		src0_format = 0x0;
		src0_rb_swp = 0x1;
		pixel_width = 4;
		break;
	case RGA_FORMAT_RGBX_8888:
		src0_format = 0x1;
		pixel_width = 4;
		msg->src_trans_mode &= 0x07;
		break;
	case RGA_FORMAT_BGRX_8888:
		src0_format = 0x1;
		src0_rb_swp = 0x1;
		pixel_width = 4;
		msg->src_trans_mode &= 0x07;
		break;
	case RGA_FORMAT_RGB_888:
		src0_format = 0x2;
		pixel_width = 3;
		msg->src_trans_mode &= 0x07;
		break;
	case RGA_FORMAT_BGR_888:
		src0_format = 0x2;
		src0_rb_swp = 1;
		pixel_width = 3;
		msg->src_trans_mode &= 0x07;
		break;
	case RGA_FORMAT_RGB_565:
		src0_format = 0x4;
		pixel_width = 2;
		msg->src_trans_mode &= 0x07;
		break;
	case RGA_FORMAT_RGBA_5551:
		src0_format = 0x5;
		pixel_width = 2;
		break;
	case RGA_FORMAT_RGBA_4444:
		src0_format = 0x6;
		pixel_width = 2;
		break;
	case RGA_FORMAT_BGR_565:
		src0_format = 0x4;
		pixel_width = 2;
		msg->src_trans_mode &= 0x07;
		src0_rb_swp = 0x1;
		break;
	case RGA_FORMAT_BGRA_5551:
		src0_format = 0x5;
		pixel_width = 2;
		src0_rb_swp = 0x1;
		break;
	case RGA_FORMAT_BGRA_4444:
		src0_format = 0x6;
		pixel_width = 2;
		src0_rb_swp = 0x1;
		break;

		/* ARGB */
		/*
		 * In colorkey mode, xrgb/xbgr does not
		 * need to enable the alpha channel
		 */
	case RGA_FORMAT_ARGB_8888:
		src0_format = 0x0;
		pixel_width = 4;
		src0_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_8888:
		src0_format = 0x0;
		pixel_width = 4;
		src0_alpha_swp = 1;
		src0_rb_swp = 0x1;
		break;
	case RGA_FORMAT_XRGB_8888:
		src0_format = 0x1;
		pixel_width = 4;
		src0_alpha_swp = 1;
		msg->src_trans_mode &= 0x07;
		break;
	case RGA_FORMAT_XBGR_8888:
		src0_format = 0x1;
		pixel_width = 4;
		src0_alpha_swp = 1;
		src0_rb_swp = 0x1;
		msg->src_trans_mode &= 0x07;
		break;
	case RGA_FORMAT_ARGB_5551:
		src0_format = 0x5;
		pixel_width = 2;
		src0_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_5551:
		src0_format = 0x5;
		pixel_width = 2;
		src0_alpha_swp = 1;
		src0_rb_swp = 0x1;
		break;
	case RGA_FORMAT_ARGB_4444:
		src0_format = 0x6;
		pixel_width = 2;
		src0_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_4444:
		src0_format = 0x6;
		pixel_width = 2;
		src0_alpha_swp = 1;
		src0_rb_swp = 0x1;
		break;

	case RGA_FORMAT_YVYU_422:
		src0_format = 0x7;
		pixel_width = 2;
		src0_cbcr_swp = 1;
		src0_rb_swp = 0x1;
		break;		//rbswap=ycswap
	case RGA_FORMAT_VYUY_422:
		src0_format = 0x7;
		pixel_width = 2;
		src0_cbcr_swp = 1;
		src0_rb_swp = 0x0;
		break;
	case RGA_FORMAT_YUYV_422:
		src0_format = 0x7;
		pixel_width = 2;
		src0_cbcr_swp = 0;
		src0_rb_swp = 0x1;
		break;
	case RGA_FORMAT_UYVY_422:
		src0_format = 0x7;
		pixel_width = 2;
		src0_cbcr_swp = 0;
		src0_rb_swp = 0x0;
		break;

	case RGA_FORMAT_YCbCr_422_SP:
		src0_format = 0x8;
		xdiv = 1;
		ydiv = 1;
		break;
	case RGA_FORMAT_YCbCr_422_P:
		src0_format = 0x9;
		xdiv = 2;
		ydiv = 1;
		break;
	case RGA_FORMAT_YCbCr_420_SP:
		src0_format = 0xa;
		xdiv = 1;
		ydiv = 2;
		break;
	case RGA_FORMAT_YCbCr_420_P:
		src0_format = 0xb;
		xdiv = 2;
		ydiv = 2;
		break;
	case RGA_FORMAT_YCrCb_422_SP:
		src0_format = 0x8;
		xdiv = 1;
		ydiv = 1;
		src0_cbcr_swp = 1;
		break;
	case RGA_FORMAT_YCrCb_422_P:
		src0_format = 0x9;
		xdiv = 2;
		ydiv = 1;
		src0_cbcr_swp = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP:
		src0_format = 0xa;
		xdiv = 1;
		ydiv = 2;
		src0_cbcr_swp = 1;
		break;
	case RGA_FORMAT_YCrCb_420_P:
		src0_format = 0xb;
		xdiv = 2;
		ydiv = 2;
		src0_cbcr_swp = 1;
		break;

	case RGA_FORMAT_YCbCr_420_SP_10B:
		src0_format = 0xa;
		xdiv = 1;
		ydiv = 2;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP_10B:
		src0_format = 0xa;
		xdiv = 1;
		ydiv = 2;
		src0_cbcr_swp = 1;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCbCr_422_SP_10B:
		src0_format = 0x8;
		xdiv = 1;
		ydiv = 1;
		yuv10 = 1;
		break;
	case RGA_FORMAT_YCrCb_422_SP_10B:
		src0_format = 0x8;
		xdiv = 1;
		ydiv = 1;
		src0_cbcr_swp = 1;
		yuv10 = 1;
		break;

	case RGA_FORMAT_YCbCr_400:
		src0_format = 0x8;
		/* When Yin_Yout is enabled, no need to go through the software. */
		disable_uv_channel_en = msg->yin_yout_en ? false : true;
		xdiv = 1;
		ydiv = 1;
		break;
	};

	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SRC_FMT)) |
		 (s_RGA2_SRC_INFO_SW_SRC_FMT(src0_format)));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_RB_SWAP)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_RB_SWAP(src0_rb_swp)));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_ALPHA_SWAP)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_ALPHA_SWAP(src0_alpha_swp)));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_UV_SWAP)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_UV_SWAP(src0_cbcr_swp)));

	if (msg->src1.format == RGA_FORMAT_RGBA_2BPP)
		reg = ((reg & (~m_RGA2_SRC_INFO_SW_SW_CP_ENDIAN)) |
		       (s_RGA2_SRC_INFO_SW_SW_CP_ENDAIN(msg->osd_info.bpp2_info.endian_swap & 1)));

	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_CSC_MODE)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_CSC_MODE(msg->yuv2rgb_mode)));

	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_ROT_MODE)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_ROT_MODE(msg->rotate_mode & 0x3)));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_MIR_MODE)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_MIR_MODE
		 ((msg->rotate_mode >> 4) & 0x3)));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_HSCL_MODE)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_HSCL_MODE((scale_w_flag))));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_VSCL_MODE)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_VSCL_MODE((scale_h_flag))));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_SCL_FILTER)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_SCL_FILTER((
			msg->scale_bicu_mode))));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_TRANS_MODE)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_TRANS_MODE(msg->src_trans_mode)));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_TRANS_E)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_TRANS_E(msg->src_trans_mode >> 1)));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_SRC_DITHER_UP_E)) |
		 (s_RGA2_SRC_INFO_SW_SW_SRC_DITHER_UP_E
		 ((msg->alpha_rop_flag >> 4) & 0x1)));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_VSP_MODE_SEL)) |
		 (s_RGA2_SRC_INFO_SW_SW_VSP_MODE_SEL((
			 msg->scale_bicu_mode >> 4))));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_YUV10_E)) |
		 (s_RGA2_SRC_INFO_SW_SW_YUV10_E((yuv10))));

	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_YUV10_ROUND_E)) |
		 (s_RGA2_SRC_INFO_SW_SW_YUV10_ROUND_E((yuv10))));

	RGA2_reg_get_param(base, msg);

	stride = (((msg->src.vir_w * pixel_width) + 3) & ~3) >> 2;
	uv_stride = ((msg->src.vir_w / xdiv + 3) & ~3);

	if (disable_uv_channel_en == 1) {
		/*
		 * When Y400 as the input format, because the current
		 * RGA does not support closing
		 * the access of the UV channel, the address of the UV
		 * channel access is equal to
		 * the address of the Y channel access to ensure that
		 * the UV channel can access,
		 * preventing the RGA hardware from reporting errors.
		 */
		*bRGA_SRC_BASE0 =
			(u32) (msg->src.yrgb_addr +
				 msg->src.y_offset * (stride << 2) +
				 msg->src.x_offset * pixel_width);
		*bRGA_SRC_BASE1 = *bRGA_SRC_BASE0;
		*bRGA_SRC_BASE2 = *bRGA_SRC_BASE0;
	} else {
		*bRGA_SRC_BASE0 =
			(u32) (msg->src.yrgb_addr +
				 msg->src.y_offset * (stride << 2) +
				 msg->src.x_offset * pixel_width);
		*bRGA_SRC_BASE1 =
			(u32) (msg->src.uv_addr +
				 (msg->src.y_offset / ydiv) * uv_stride +
				 (msg->src.x_offset / xdiv));
		*bRGA_SRC_BASE2 =
			(u32) (msg->src.v_addr +
				 (msg->src.y_offset / ydiv) * uv_stride +
				 (msg->src.x_offset / xdiv));
	}

	//mask_stride = ((msg->src0_act.width + 31) & ~31) >> 5;
	mask_stride = msg->rop_mask_stride;

	*bRGA_SRC_VIR_INFO = stride | (mask_stride << 16);

	*bRGA_SRC_ACT_INFO =
		(msg->src.act_w - 1) | ((msg->src.act_h - 1) << 16);

	*bRGA_MASK_ADDR = (u32) msg->rop_mask_addr;

	*bRGA_SRC_INFO = reg;

	*bRGA_SRC_TR_COLOR0 = msg->color_key_min;
	*bRGA_SRC_TR_COLOR1 = msg->color_key_max;
}

static void RGA2_set_reg_dst_info(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_DST_INFO;
	u32 *bRGA_DST_BASE0, *bRGA_DST_BASE1, *bRGA_DST_BASE2,
		*bRGA_SRC_BASE3;
	u32 *bRGA_DST_VIR_INFO;
	u32 *bRGA_DST_ACT_INFO;

	u32 *RGA_DST_Y4MAP_LUT0;	//Y4 LUT0
	u32 *RGA_DST_Y4MAP_LUT1;	//Y4 LUT1
	u32 *RGA_DST_NN_QUANTIZE_SCALE;
	u32 *RGA_DST_NN_QUANTIZE_OFFSET;

	u32 line_width_real;

	u8 ydither_en = 0;

	u8 src1_format = 0;
	u8 src1_rb_swp = 0;
	u8 src1_alpha_swp = 0;

	u8 dst_format = 0;
	u8 dst_rb_swp = 0;
	u8 dst_cbcr_swp = 0;
	u8 dst_alpha_swp = 0;

	u8 dst_fmt_yuv400_en = 0;
	u8 dst_fmt_y4_en = 0;
	u8 dst_nn_quantize_en = 0;

	u32 reg = 0;
	u8 spw, dpw;
	u8 bbp_shift = 0;
	u32 s_stride, d_stride;
	u32 x_mirr, y_mirr, rot_90_flag;
	u32 yrgb_addr, u_addr, v_addr, s_yrgb_addr;
	u32 d_uv_stride, x_div, y_div;
	u32 y_lt_addr, y_ld_addr, y_rt_addr, y_rd_addr;
	u32 u_lt_addr, u_ld_addr, u_rt_addr, u_rd_addr;
	u32 v_lt_addr, v_ld_addr, v_rt_addr, v_rd_addr;

	dpw = 1;
	x_div = y_div = 1;

	dst_nn_quantize_en = (msg->alpha_rop_flag >> 8) & 0x1;

	bRGA_DST_INFO = (u32 *) (base + RGA2_DST_INFO_OFFSET);
	bRGA_DST_BASE0 = (u32 *) (base + RGA2_DST_BASE0_OFFSET);
	bRGA_DST_BASE1 = (u32 *) (base + RGA2_DST_BASE1_OFFSET);
	bRGA_DST_BASE2 = (u32 *) (base + RGA2_DST_BASE2_OFFSET);

	bRGA_SRC_BASE3 = (u32 *) (base + RGA2_SRC_BASE3_OFFSET);

	bRGA_DST_VIR_INFO = (u32 *) (base + RGA2_DST_VIR_INFO_OFFSET);
	bRGA_DST_ACT_INFO = (u32 *) (base + RGA2_DST_ACT_INFO_OFFSET);

	RGA_DST_Y4MAP_LUT0 = (u32 *) (base + RGA2_DST_Y4MAP_LUT0_OFFSET);
	RGA_DST_Y4MAP_LUT1 = (u32 *) (base + RGA2_DST_Y4MAP_LUT1_OFFSET);
	RGA_DST_NN_QUANTIZE_SCALE =
		(u32 *) (base + RGA2_DST_QUANTIZE_SCALE_OFFSET);
	RGA_DST_NN_QUANTIZE_OFFSET =
		(u32 *) (base + RGA2_DST_QUANTIZE_OFFSET_OFFSET);

	switch (msg->src1.format) {
	case RGA_FORMAT_RGBA_8888:
		src1_format = 0x0;
		spw = 4;
		break;
	case RGA_FORMAT_BGRA_8888:
		src1_format = 0x0;
		src1_rb_swp = 0x1;
		spw = 4;
		break;
	case RGA_FORMAT_RGBX_8888:
		src1_format = 0x1;
		spw = 4;
		break;
	case RGA_FORMAT_BGRX_8888:
		src1_format = 0x1;
		src1_rb_swp = 0x1;
		spw = 4;
		break;
	case RGA_FORMAT_RGB_888:
		src1_format = 0x2;
		spw = 3;
		break;
	case RGA_FORMAT_BGR_888:
		src1_format = 0x2;
		src1_rb_swp = 1;
		spw = 3;
		break;
	case RGA_FORMAT_RGB_565:
		src1_format = 0x4;
		spw = 2;
		break;
	case RGA_FORMAT_RGBA_5551:
		src1_format = 0x5;
		spw = 2;
		break;
	case RGA_FORMAT_RGBA_4444:
		src1_format = 0x6;
		spw = 2;
		break;
	case RGA_FORMAT_BGR_565:
		src1_format = 0x4;
		spw = 2;
		src1_rb_swp = 0x1;
		break;
	case RGA_FORMAT_BGRA_5551:
		src1_format = 0x5;
		spw = 2;
		src1_rb_swp = 0x1;
		break;
	case RGA_FORMAT_BGRA_4444:
		src1_format = 0x6;
		spw = 2;
		src1_rb_swp = 0x1;
		break;

		/* ARGB */
	case RGA_FORMAT_ARGB_8888:
		src1_format = 0x0;
		spw = 4;
		src1_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_8888:
		src1_format = 0x0;
		spw = 4;
		src1_alpha_swp = 1;
		src1_rb_swp = 0x1;
		break;
	case RGA_FORMAT_XRGB_8888:
		src1_format = 0x1;
		spw = 4;
		src1_alpha_swp = 1;
		break;
	case RGA_FORMAT_XBGR_8888:
		src1_format = 0x1;
		spw = 4;
		src1_alpha_swp = 1;
		src1_rb_swp = 0x1;
		break;
	case RGA_FORMAT_ARGB_5551:
		src1_format = 0x5;
		spw = 2;
		src1_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_5551:
		src1_format = 0x5;
		spw = 2;
		src1_alpha_swp = 1;
		src1_rb_swp = 0x1;
		break;
	case RGA_FORMAT_ARGB_4444:
		src1_format = 0x6;
		spw = 2;
		src1_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_4444:
		src1_format = 0x6;
		spw = 2;
		src1_alpha_swp = 1;
		src1_rb_swp = 0x1;
		break;
	case RGA_FORMAT_RGBA_2BPP:
		src1_format = 0x0;
		spw = 1;
		/* 2BPP = 8 >> 2 = 2bit */
		bbp_shift = 2;
		src1_alpha_swp = msg->osd_info.bpp2_info.ac_swap;
		break;
	default:
		spw = 4;
		break;
	};

	reg =
		((reg & (~m_RGA2_DST_INFO_SW_SRC1_FMT)) |
		 (s_RGA2_DST_INFO_SW_SRC1_FMT(src1_format)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_SRC1_RB_SWP)) |
		 (s_RGA2_DST_INFO_SW_SRC1_RB_SWP(src1_rb_swp)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_SRC1_ALPHA_SWP)) |
		 (s_RGA2_DST_INFO_SW_SRC1_ALPHA_SWP(src1_alpha_swp)));

	switch (msg->dst.format) {
	case RGA_FORMAT_RGBA_8888:
		dst_format = 0x0;
		dpw = 4;
		break;
	case RGA_FORMAT_BGRA_8888:
		dst_format = 0x0;
		dst_rb_swp = 0x1;
		dpw = 4;
		break;
	case RGA_FORMAT_RGBX_8888:
		dst_format = 0x1;
		dpw = 4;
		break;
	case RGA_FORMAT_BGRX_8888:
		dst_format = 0x1;
		dst_rb_swp = 0x1;
		dpw = 4;
		break;
	case RGA_FORMAT_RGB_888:
		dst_format = 0x2;
		dpw = 3;
		break;
	case RGA_FORMAT_BGR_888:
		dst_format = 0x2;
		dst_rb_swp = 1;
		dpw = 3;
		break;
	case RGA_FORMAT_RGB_565:
		dst_format = 0x4;
		dpw = 2;
		break;
	case RGA_FORMAT_RGBA_5551:
		dst_format = 0x5;
		dpw = 2;
		break;
	case RGA_FORMAT_RGBA_4444:
		dst_format = 0x6;
		dpw = 2;
		break;
	case RGA_FORMAT_BGR_565:
		dst_format = 0x4;
		dpw = 2;
		dst_rb_swp = 0x1;
		break;
	case RGA_FORMAT_BGRA_5551:
		dst_format = 0x5;
		dpw = 2;
		dst_rb_swp = 0x1;
		break;
	case RGA_FORMAT_BGRA_4444:
		dst_format = 0x6;
		dpw = 2;
		dst_rb_swp = 0x1;
		break;

		/* ARGB */
	case RGA_FORMAT_ARGB_8888:
		dst_format = 0x0;
		dpw = 4;
		dst_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_8888:
		dst_format = 0x0;
		dpw = 4;
		dst_alpha_swp = 1;
		dst_rb_swp = 0x1;
		break;
	case RGA_FORMAT_XRGB_8888:
		dst_format = 0x1;
		dpw = 4;
		dst_alpha_swp = 1;
		break;
	case RGA_FORMAT_XBGR_8888:
		dst_format = 0x1;
		dpw = 4;
		dst_alpha_swp = 1;
		dst_rb_swp = 0x1;
		break;
	case RGA_FORMAT_ARGB_5551:
		dst_format = 0x5;
		dpw = 2;
		dst_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_5551:
		dst_format = 0x5;
		dpw = 2;
		dst_alpha_swp = 1;
		dst_rb_swp = 0x1;
		break;
	case RGA_FORMAT_ARGB_4444:
		dst_format = 0x6;
		dpw = 2;
		dst_alpha_swp = 1;
		break;
	case RGA_FORMAT_ABGR_4444:
		dst_format = 0x6;
		dpw = 2;
		dst_alpha_swp = 1;
		dst_rb_swp = 0x1;
		break;

	case RGA_FORMAT_YCbCr_422_SP:
		dst_format = 0x8;
		x_div = 1;
		y_div = 1;
		break;
	case RGA_FORMAT_YCbCr_422_P:
		dst_format = 0x9;
		x_div = 2;
		y_div = 1;
		break;
	case RGA_FORMAT_YCbCr_420_SP:
		dst_format = 0xa;
		x_div = 1;
		y_div = 2;
		break;
	case RGA_FORMAT_YCbCr_420_P:
		dst_format = 0xb;
		dst_cbcr_swp = 1;
		x_div = 2;
		y_div = 2;
		break;
	case RGA_FORMAT_YCrCb_422_SP:
		dst_format = 0x8;
		dst_cbcr_swp = 1;
		x_div = 1;
		y_div = 1;
		break;
	case RGA_FORMAT_YCrCb_422_P:
		dst_format = 0x9;
		dst_cbcr_swp = 1;
		x_div = 2;
		y_div = 1;
		break;
	case RGA_FORMAT_YCrCb_420_SP:
		dst_format = 0xa;
		dst_cbcr_swp = 1;
		x_div = 1;
		y_div = 2;
		break;
	case RGA_FORMAT_YCrCb_420_P:
		dst_format = 0xb;
		x_div = 2;
		y_div = 2;
		break;

	case RGA_FORMAT_YCbCr_400:
		dst_format = 0x8;
		dst_fmt_yuv400_en = 1;
		x_div = 1;
		y_div = 1;
		break;
	case RGA_FORMAT_Y4:
		dst_format = 0x8;
		dst_fmt_y4_en = 1;
		dst_fmt_yuv400_en = 1;
		x_div = 1;
		y_div = 1;
		break;

	case RGA_FORMAT_YUYV_422:
		dst_format = 0xe;
		dpw = 2;
		dst_cbcr_swp = 1;
		break;
	case RGA_FORMAT_YVYU_422:
		dst_format = 0xe;
		dpw = 2;
		break;
	case RGA_FORMAT_YUYV_420:
		dst_format = 0xf;
		dpw = 2;
		dst_cbcr_swp = 1;
		break;
	case RGA_FORMAT_YVYU_420:
		dst_format = 0xf;
		dpw = 2;
		break;
	case RGA_FORMAT_UYVY_422:
		dst_format = 0xc;
		dpw = 2;
		dst_cbcr_swp = 1;
		break;
	case RGA_FORMAT_VYUY_422:
		dst_format = 0xc;
		dpw = 2;
		break;
	case RGA_FORMAT_UYVY_420:
		dst_format = 0xd;
		dpw = 2;
		dst_cbcr_swp = 1;
		break;
	case RGA_FORMAT_VYUY_420:
		dst_format = 0xd;
		dpw = 2;
		break;
	};

	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DST_FMT)) |
		 (s_RGA2_DST_INFO_SW_DST_FMT(dst_format)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DST_RB_SWAP)) |
		 (s_RGA2_DST_INFO_SW_DST_RB_SWAP(dst_rb_swp)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_ALPHA_SWAP)) |
		 (s_RGA2_DST_INFO_SW_ALPHA_SWAP(dst_alpha_swp)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DST_UV_SWAP)) |
		 (s_RGA2_DST_INFO_SW_DST_UV_SWAP(dst_cbcr_swp)));

	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DST_FMT_YUV400_EN)) |
		 (s_RGA2_DST_INFO_SW_DST_FMT_YUV400_EN(dst_fmt_yuv400_en)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DST_FMT_Y4_EN)) |
		 (s_RGA2_DST_INFO_SW_DST_FMT_Y4_EN(dst_fmt_y4_en)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DST_NN_QUANTIZE_EN)) |
		 (s_RGA2_DST_INFO_SW_DST_NN_QUANTIZE_EN(dst_nn_quantize_en)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DITHER_UP_E)) |
		 (s_RGA2_DST_INFO_SW_DITHER_UP_E(msg->alpha_rop_flag >> 5)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DITHER_DOWN_E)) |
		 (s_RGA2_DST_INFO_SW_DITHER_DOWN_E(msg->alpha_rop_flag >> 6)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DITHER_MODE)) |
		 (s_RGA2_DST_INFO_SW_DITHER_MODE(msg->dither_mode)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DST_CSC_MODE)) |
		 (s_RGA2_DST_INFO_SW_DST_CSC_MODE(msg->yuv2rgb_mode >> 2)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_CSC_CLIP_MODE)) |
		 (s_RGA2_DST_INFO_SW_CSC_CLIP_MODE(msg->yuv2rgb_mode >> 4)));
	/* full csc enable */
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_DST_CSC_MODE_2)) |
		 (s_RGA2_DST_INFO_SW_DST_CSC_MODE_2(msg->full_csc_en)));
	/*
	 * Some older chips do not support src1 csc mode,
	 * they do not have these two registers.
	 */
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_SRC1_CSC_MODE)) |
		 (s_RGA2_DST_INFO_SW_SRC1_CSC_MODE(msg->yuv2rgb_mode >> 5)));
	reg =
		((reg & (~m_RGA2_DST_INFO_SW_SRC1_CSC_CLIP_MODE)) |
		 (s_RGA2_DST_INFO_SW_SRC1_CSC_CLIP_MODE(
			msg->yuv2rgb_mode >> 7)));

	reg = ((reg & (~m_RGA2_DST_INFO_SW_DST_UVHDS_MODE)) |
	       (s_RGA2_DST_INFO_SW_DST_UVHDS_MODE(msg->uvhds_mode)));
	reg = ((reg & (~m_RGA2_DST_INFO_SW_DST_UVVDS_MODE)) |
	       (s_RGA2_DST_INFO_SW_DST_UVVDS_MODE(msg->uvvds_mode)));

	ydither_en = (msg->dst.format == RGA_FORMAT_Y4)
		&& ((msg->alpha_rop_flag >> 6) & 0x1);

	*bRGA_DST_INFO = reg;

	s_stride = (((msg->src1.vir_w * spw >> bbp_shift) + 3) & ~3) >> 2;
	d_stride = ((msg->dst.vir_w * dpw + 3) & ~3) >> 2;

	if (dst_fmt_y4_en) {
		/* Y4 output will HALF */
		d_stride = ((d_stride + 1) & ~1) >> 1;
	}

	d_uv_stride = (d_stride << 2) / x_div;

	*bRGA_DST_VIR_INFO = d_stride | (s_stride << 16);
	if ((msg->dst.vir_w % 2 != 0) &&
		(msg->dst.act_w == msg->src.act_w)
		&& (msg->dst.act_h == msg->src.act_h)
		&& (msg->dst.format == RGA_FORMAT_BGR_888
		|| msg->dst.format == RGA_FORMAT_RGB_888))
		*bRGA_DST_ACT_INFO =
			(msg->dst.act_w) | ((msg->dst.act_h - 1) << 16);
	else
		*bRGA_DST_ACT_INFO =
			(msg->dst.act_w - 1) | ((msg->dst.act_h - 1) << 16);
	s_stride <<= 2;
	d_stride <<= 2;

	if (((msg->rotate_mode & 0xf) == 0) ||
		((msg->rotate_mode & 0xf) == 1)) {
		x_mirr = 0;
		y_mirr = 0;
	} else {
		x_mirr = 1;
		y_mirr = 1;
	}

	rot_90_flag = msg->rotate_mode & 1;
	x_mirr = (x_mirr + ((msg->rotate_mode >> 4) & 1)) & 1;
	y_mirr = (y_mirr + ((msg->rotate_mode >> 5) & 1)) & 1;

	if (ydither_en) {
		if (x_mirr && y_mirr) {
			pr_err("ydither mode do not support rotate x_mirr=%d,y_mirr=%d\n",
				x_mirr, y_mirr);
		}

		if (msg->dst.act_w != msg->src.act_w)
			pr_err("ydither mode do not support x dir scale\n");

		if (msg->dst.act_h != msg->src.act_h)
			pr_err("ydither mode do not support y dir scale\n");
	}

	if (dst_fmt_y4_en) {
		*RGA_DST_Y4MAP_LUT0 = (msg->gr_color.gr_x_r & 0xffff) |
			(msg->gr_color.gr_x_g << 16);
		*RGA_DST_Y4MAP_LUT1 = (msg->gr_color.gr_y_r & 0xffff) |
			(msg->gr_color.gr_y_g << 16);
	}

	if (dst_nn_quantize_en) {
		*RGA_DST_NN_QUANTIZE_SCALE = (msg->gr_color.gr_x_r & 0xffff) |
			(msg->gr_color.gr_x_g << 10) |
			(msg->gr_color.gr_x_b << 20);
		*RGA_DST_NN_QUANTIZE_OFFSET = (msg->gr_color.gr_y_r & 0xffff) |
			(msg->gr_color.gr_y_g << 10) |
			(msg->gr_color.gr_y_b << 20);
	}

	s_yrgb_addr =
		(u32) msg->src1.yrgb_addr + (msg->src1.y_offset * s_stride) +
		(msg->src1.x_offset * spw >> bbp_shift);

	*bRGA_SRC_BASE3 = s_yrgb_addr;

	if (dst_fmt_y4_en) {
		yrgb_addr = (u32) msg->dst.yrgb_addr +
			(msg->dst.y_offset * d_stride) +
			((msg->dst.x_offset * dpw) >> 1);
	} else {
		yrgb_addr = (u32) msg->dst.yrgb_addr +
			(msg->dst.y_offset * d_stride) +
			(msg->dst.x_offset * dpw);
	}
	u_addr = (u32) msg->dst.uv_addr +
		(msg->dst.y_offset / y_div) * d_uv_stride +
		msg->dst.x_offset / x_div;
	v_addr = (u32) msg->dst.v_addr +
		(msg->dst.y_offset / y_div) * d_uv_stride +
		msg->dst.x_offset / x_div;

	y_lt_addr = yrgb_addr;
	u_lt_addr = u_addr;
	v_lt_addr = v_addr;

	/* Warning */
	line_width_real =
		dst_fmt_y4_en ? ((msg->dst.act_w) >> 1) : msg->dst.act_w;

	/*
	 * YUV packet mode is a new format, and the write behavior during
	 * rotation is different from the old format.
	 */
	if (rga_is_yuv422_packed_format(msg->dst.format)) {
		y_ld_addr = yrgb_addr + (msg->dst.act_h - 1) * (d_stride);
		y_rt_addr = yrgb_addr + (msg->dst.act_w * 2 - 1);
		y_rd_addr = y_ld_addr + (msg->dst.act_w * 2 - 1);
	} else if (rga_is_yuv420_packed_format(msg->dst.format)) {
		y_ld_addr = (u32)msg->dst.yrgb_addr +
			    ((msg->dst.y_offset + (msg->dst.act_h - 1)) * d_stride) +
			    msg->dst.x_offset;
		y_rt_addr = yrgb_addr + (msg->dst.act_w * 2 - 1);
		y_rd_addr = y_ld_addr + (msg->dst.act_w - 1);
	} else {
		/* 270 degree & Mirror V */
		y_ld_addr = yrgb_addr + (msg->dst.act_h - 1) * (d_stride);
		/* 90 degree & Mirror H */
		y_rt_addr = yrgb_addr + (line_width_real - 1) * dpw;
		/* 180 degree */
		y_rd_addr = y_ld_addr + (line_width_real - 1) * dpw;
	}

	u_ld_addr = u_addr + ((msg->dst.act_h / y_div) - 1) * (d_uv_stride);
	v_ld_addr = v_addr + ((msg->dst.act_h / y_div) - 1) * (d_uv_stride);

	u_rt_addr = u_addr + (msg->dst.act_w / x_div) - 1;
	v_rt_addr = v_addr + (msg->dst.act_w / x_div) - 1;

	u_rd_addr = u_ld_addr + (msg->dst.act_w / x_div) - 1;
	v_rd_addr = v_ld_addr + (msg->dst.act_w / x_div) - 1;

	if (rot_90_flag == 0) {
		if (y_mirr == 1) {
			if (x_mirr == 1) {
				yrgb_addr = y_rd_addr;
				u_addr = u_rd_addr;
				v_addr = v_rd_addr;
			} else {
				yrgb_addr = y_ld_addr;
				u_addr = u_ld_addr;
				v_addr = v_ld_addr;
			}
		} else {
			if (x_mirr == 1) {
				yrgb_addr = y_rt_addr;
				u_addr = u_rt_addr;
				v_addr = v_rt_addr;
			} else {
				yrgb_addr = y_lt_addr;
				u_addr = u_lt_addr;
				v_addr = v_lt_addr;
			}
		}
	} else {
		if (y_mirr == 1) {
			if (x_mirr == 1) {
				yrgb_addr = y_ld_addr;
				u_addr = u_ld_addr;
				v_addr = v_ld_addr;
			} else {
				yrgb_addr = y_rd_addr;
				u_addr = u_rd_addr;
				v_addr = v_rd_addr;
			}
		} else {
			if (x_mirr == 1) {
				yrgb_addr = y_lt_addr;
				u_addr = u_lt_addr;
				v_addr = v_lt_addr;
			} else {
				yrgb_addr = y_rt_addr;
				u_addr = u_rt_addr;
				v_addr = v_rt_addr;
			}
		}
	}

	*bRGA_DST_BASE0 = (u32) yrgb_addr;

	if ((msg->dst.format == RGA_FORMAT_YCbCr_420_P)
		|| (msg->dst.format == RGA_FORMAT_YCrCb_420_P)) {
		if (dst_cbcr_swp == 0) {
			*bRGA_DST_BASE1 = (u32) v_addr;
			*bRGA_DST_BASE2 = (u32) u_addr;
		} else {
			*bRGA_DST_BASE1 = (u32) u_addr;
			*bRGA_DST_BASE2 = (u32) v_addr;
		}
	} else {
		*bRGA_DST_BASE1 = (u32) u_addr;
		*bRGA_DST_BASE2 = (u32) v_addr;
	}
}

static void RGA2_set_reg_alpha_info(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_ALPHA_CTRL0;
	u32 *bRGA_ALPHA_CTRL1;
	u32 *bRGA_FADING_CTRL;
	u32 reg0 = 0;
	u32 reg1 = 0;

	bRGA_ALPHA_CTRL0 = (u32 *) (base + RGA2_ALPHA_CTRL0_OFFSET);
	bRGA_ALPHA_CTRL1 = (u32 *) (base + RGA2_ALPHA_CTRL1_OFFSET);
	bRGA_FADING_CTRL = (u32 *) (base + RGA2_FADING_CTRL_OFFSET);

	reg0 =
		((reg0 & (~m_RGA2_ALPHA_CTRL0_SW_ALPHA_ROP_0)) |
		 (s_RGA2_ALPHA_CTRL0_SW_ALPHA_ROP_0(msg->alpha_rop_flag)));
	reg0 =
		((reg0 & (~m_RGA2_ALPHA_CTRL0_SW_ALPHA_ROP_SEL)) |
		 (s_RGA2_ALPHA_CTRL0_SW_ALPHA_ROP_SEL
		 (msg->alpha_rop_flag >> 1)));
	reg0 =
		((reg0 & (~m_RGA2_ALPHA_CTRL0_SW_ROP_MODE)) |
		 (s_RGA2_ALPHA_CTRL0_SW_ROP_MODE(msg->rop_mode)));
	reg0 =
		((reg0 & (~m_RGA2_ALPHA_CTRL0_SW_SRC_GLOBAL_ALPHA)) |
		 (s_RGA2_ALPHA_CTRL0_SW_SRC_GLOBAL_ALPHA
		 (msg->src_a_global_val)));
	reg0 =
		((reg0 & (~m_RGA2_ALPHA_CTRL0_SW_DST_GLOBAL_ALPHA)) |
		 (s_RGA2_ALPHA_CTRL0_SW_DST_GLOBAL_ALPHA
		 (msg->dst_a_global_val)));

	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_COLOR_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_COLOR_M0
		 (msg->alpha_mode_0 >> 15)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_COLOR_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_COLOR_M0
		 (msg->alpha_mode_0 >> 7)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_FACTOR_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_FACTOR_M0
		 (msg->alpha_mode_0 >> 12)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_FACTOR_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_FACTOR_M0
		 (msg->alpha_mode_0 >> 4)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_CAL_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_CAL_M0
		 (msg->alpha_mode_0 >> 11)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_CAL_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_CAL_M0
		 (msg->alpha_mode_0 >> 3)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_BLEND_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_BLEND_M0
		 (msg->alpha_mode_0 >> 9)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_BLEND_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_BLEND_M0
		 (msg->alpha_mode_0 >> 1)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_M0
		 (msg->alpha_mode_0 >> 8)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_M0)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_M0
		 (msg->alpha_mode_0 >> 0)));

	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_FACTOR_M1)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_FACTOR_M1
		 (msg->alpha_mode_1 >> 12)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_FACTOR_M1)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_FACTOR_M1
		 (msg->alpha_mode_1 >> 4)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_CAL_M1)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_CAL_M1
		 (msg->alpha_mode_1 >> 11)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_CAL_M1)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_CAL_M1
		 (msg->alpha_mode_1 >> 3)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_BLEND_M1)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_BLEND_M1(msg->alpha_mode_1 >> 9)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_BLEND_M1)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_BLEND_M1(msg->alpha_mode_1 >> 1)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_M1)) |
		 (s_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_M1(msg->alpha_mode_1 >> 8)));
	reg1 =
		((reg1 & (~m_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_M1)) |
		 (s_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_M1(msg->alpha_mode_1 >> 0)));

	*bRGA_ALPHA_CTRL0 = reg0;
	*bRGA_ALPHA_CTRL1 = reg1;

	if ((msg->alpha_rop_flag >> 2) & 1) {
		*bRGA_FADING_CTRL = (1 << 24) | (msg->fading_b_value << 16) |
			(msg->fading_g_value << 8) | (msg->fading_r_value);
	}
}

static void RGA2_set_reg_rop_info(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_ALPHA_CTRL0;
	u32 *bRGA_ROP_CTRL0;
	u32 *bRGA_ROP_CTRL1;
	u32 *bRGA_MASK_ADDR;
	u32 *bRGA_FG_COLOR;
	u32 *bRGA_PAT_CON;

	u32 rop_code0 = 0;
	u32 rop_code1 = 0;

	bRGA_ALPHA_CTRL0 = (u32 *) (base + RGA2_ALPHA_CTRL0_OFFSET);
	bRGA_ROP_CTRL0 = (u32 *) (base + RGA2_ROP_CTRL0_OFFSET);
	bRGA_ROP_CTRL1 = (u32 *) (base + RGA2_ROP_CTRL1_OFFSET);
	bRGA_MASK_ADDR = (u32 *) (base + RGA2_MASK_BASE_OFFSET);
	bRGA_FG_COLOR = (u32 *) (base + RGA2_SRC_FG_COLOR_OFFSET);
	bRGA_PAT_CON = (u32 *) (base + RGA2_PAT_CON_OFFSET);

	if (msg->rop_mode == 0) {
		rop_code0 = rga2_rop_code[(msg->rop_code & 0xff)];
	} else if (msg->rop_mode == 1) {
		rop_code0 = rga2_rop_code[(msg->rop_code & 0xff)];
	} else if (msg->rop_mode == 2) {
		rop_code0 = rga2_rop_code[(msg->rop_code & 0xff)];
		rop_code1 = rga2_rop_code[(msg->rop_code & 0xff00) >> 8];
	}

	*bRGA_ROP_CTRL0 = rop_code0;
	*bRGA_ROP_CTRL1 = rop_code1;
	*bRGA_FG_COLOR = msg->fg_color;
	*bRGA_MASK_ADDR = (u32) msg->rop_mask_addr;
	*bRGA_PAT_CON = (msg->pat.act_w - 1) | ((msg->pat.act_h - 1) << 8)
		| (msg->pat.x_offset << 16) | (msg->pat.y_offset << 24);
	*bRGA_ALPHA_CTRL0 =
		*bRGA_ALPHA_CTRL0 | (((msg->endian_mode >> 1) & 1) << 20);

}

static void RGA_set_reg_mosaic(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_MOSAIC_MODE;

	bRGA_MOSAIC_MODE = (u32 *)(base + RGA2_MOSAIC_MODE_OFFSET);

	*bRGA_MOSAIC_MODE = (u32)(msg->mosaic_info.mode & 0x7);
}

static void RGA2_set_reg_osd(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_OSD_CTRL0;
	u32 *bRGA_OSD_CTRL1;
	u32 *bRGA_OSD_INVERTSION_CAL0;
	u32 *bRGA_OSD_INVERTSION_CAL1;
	u32 *bRGA_OSD_COLOR0;
	u32 *bRGA_OSD_COLOR1;
	u32 *bRGA_OSD_LAST_FLAGS0;
	u32 *bRGA_OSD_LAST_FLAGS1;
	u32 reg;
	u8 rgba2bpp_en = 0;
	u8 block_num;
	u16 fix_width;


	bRGA_OSD_CTRL0 = (u32 *)(base + RGA2_OSD_CTRL0_OFFSET);
	bRGA_OSD_CTRL1 = (u32 *)(base + RGA2_OSD_CTRL1_OFFSET);
	bRGA_OSD_INVERTSION_CAL0 = (u32 *)(base + RGA2_OSD_INVERTSION_CAL0_OFFSET);
	bRGA_OSD_INVERTSION_CAL1 = (u32 *)(base + RGA2_OSD_INVERTSION_CAL1_OFFSET);
	bRGA_OSD_COLOR0 = (u32 *)(base + RGA2_OSD_COLOR0_OFFSET);
	bRGA_OSD_COLOR1 = (u32 *)(base + RGA2_OSD_COLOR1_OFFSET);
	bRGA_OSD_LAST_FLAGS0 = (u32 *)(base + RGA2_OSD_LAST_FLAGS0_OFFSET);
	bRGA_OSD_LAST_FLAGS1 = (u32 *)(base + RGA2_OSD_LAST_FLAGS1_OFFSET);

	/* To save the number of register bits. */
	fix_width = msg->osd_info.mode_ctrl.block_fix_width / 2 - 1;

	/* The register is '0' as the first. */
	block_num = msg->osd_info.mode_ctrl.block_num - 1;

	if (msg->src1.format == RGA_FORMAT_RGBA_2BPP)
		rgba2bpp_en = 1;

	reg = 0;
	reg = ((reg & (~m_RGA2_OSD_CTRL0_SW_OSD_MODE)) |
	       (s_RGA2_OSD_CTRL0_SW_OSD_MODE(msg->osd_info.mode_ctrl.mode)));
	reg = ((reg & (~m_RGA2_OSD_CTRL0_SW_OSD_VER_MODE)) |
	       (s_RGA2_OSD_CTRL0_SW_OSD_VER_MODE(msg->osd_info.mode_ctrl.direction_mode)));
	reg = ((reg & (~m_RGA2_OSD_CTRL0_SW_OSD_WIDTH_MODE)) |
	       (s_RGA2_OSD_CTRL0_SW_OSD_WIDTH_MODE(msg->osd_info.mode_ctrl.width_mode)));
	reg = ((reg & (~m_RGA2_OSD_CTRL0_SW_OSD_BLK_NUM)) |
	       (s_RGA2_OSD_CTRL0_SW_OSD_BLK_NUM(block_num)));
	reg = ((reg & (~m_RGA2_OSD_CTRL0_SW_OSD_FLAGS_INDEX)) |
	       (s_RGA2_OSD_CTRL0_SW_OSD_FLAGS_INDEX(msg->osd_info.mode_ctrl.flags_index)));
	reg = ((reg & (~m_RGA2_OSD_CTRL0_SW_OSD_FIX_WIDTH)) |
	       (s_RGA2_OSD_CTRL0_SW_OSD_FIX_WIDTH(fix_width)));
	reg = ((reg & (~m_RGA2_OSD_CTRL0_SW_OSD_2BPP_MODE)) |
	       (s_RGA2_OSD_CTRL0_SW_OSD_2BPP_MODE(rgba2bpp_en)));
	*bRGA_OSD_CTRL0 = reg;

	reg = 0;
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_COLOR_SEL)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_COLOR_SEL(msg->osd_info.mode_ctrl.color_mode)));
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_FLAG_SEL)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_FLAG_SEL(msg->osd_info.mode_ctrl.invert_flags_mode)));
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_DEFAULT_COLOR)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_DEFAULT_COLOR(msg->osd_info.mode_ctrl.default_color_sel)));
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_AUTO_INVERST_MODE)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_AUTO_INVERST_MODE(msg->osd_info.mode_ctrl.invert_mode)));
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_THRESH)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_THRESH(msg->osd_info.mode_ctrl.invert_thresh)));
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_INVERT_A_EN)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_INVERT_A_EN(msg->osd_info.mode_ctrl.invert_enable)));
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_INVERT_Y_DIS)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_INVERT_Y_DIS(msg->osd_info.mode_ctrl.invert_enable >> 1)));
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_INVERT_C_DIS)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_INVERT_C_DIS(msg->osd_info.mode_ctrl.invert_enable >> 2)));
	reg = ((reg & (~m_RGA2_OSD_CTRL1_SW_OSD_UNFIX_INDEX)) |
	       (s_RGA2_OSD_CTRL1_SW_OSD_UNFIX_INDEX(msg->osd_info.mode_ctrl.unfix_index)));
	*bRGA_OSD_CTRL1 = reg;

	*bRGA_OSD_INVERTSION_CAL0 = ((msg->osd_info.cal_factor.crb_max) << 24) |
				    ((msg->osd_info.cal_factor.crb_min) << 16) |
				    ((msg->osd_info.cal_factor.yg_max) << 8) |
				    ((msg->osd_info.cal_factor.yg_min) << 0);
	*bRGA_OSD_INVERTSION_CAL1 = ((msg->osd_info.cal_factor.alpha_max) << 8) |
				    ((msg->osd_info.cal_factor.alpha_min) << 0);

	*bRGA_OSD_LAST_FLAGS0 = (msg->osd_info.last_flags0);
	*bRGA_OSD_LAST_FLAGS1 = (msg->osd_info.last_flags1);

	if (msg->osd_info.mode_ctrl.color_mode == 1) {
		*bRGA_OSD_COLOR0 = (msg->osd_info.bpp2_info.color0.value & 0xffffff);
		*bRGA_OSD_COLOR1 = (msg->osd_info.bpp2_info.color1.value & 0xffffff);
	}

	if (rgba2bpp_en) {
		*bRGA_OSD_COLOR0 = msg->osd_info.bpp2_info.color0.value;
		*bRGA_OSD_COLOR1 = msg->osd_info.bpp2_info.color1.value;
	}
}

static void RGA2_set_reg_color_palette(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_SRC_BASE0, *bRGA_SRC_INFO, *bRGA_SRC_VIR_INFO,
		*bRGA_SRC_ACT_INFO, *bRGA_SRC_FG_COLOR, *bRGA_SRC_BG_COLOR;
	u32 *p;
	short x_off, y_off;
	u16 src_stride;
	u8 shift;
	u32 sw;
	u32 byte_num;
	u32 reg;

	bRGA_SRC_BASE0 = (u32 *) (base + RGA2_SRC_BASE0_OFFSET);
	bRGA_SRC_INFO = (u32 *) (base + RGA2_SRC_INFO_OFFSET);
	bRGA_SRC_VIR_INFO = (u32 *) (base + RGA2_SRC_VIR_INFO_OFFSET);
	bRGA_SRC_ACT_INFO = (u32 *) (base + RGA2_SRC_ACT_INFO_OFFSET);
	bRGA_SRC_FG_COLOR = (u32 *) (base + RGA2_SRC_FG_COLOR_OFFSET);
	bRGA_SRC_BG_COLOR = (u32 *) (base + RGA2_SRC_BG_COLOR_OFFSET);

	reg = 0;

	shift = 3 - msg->palette_mode;

	x_off = msg->src.x_offset;
	y_off = msg->src.y_offset;

	sw = msg->src.vir_w;
	byte_num = sw >> shift;

	src_stride = (byte_num + 3) & (~3);

	p = (u32 *) ((unsigned long)msg->src.yrgb_addr);

	p = p + (x_off >> shift) + y_off * src_stride;

	*bRGA_SRC_BASE0 = (unsigned long)p;

	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SRC_FMT)) |
		 (s_RGA2_SRC_INFO_SW_SRC_FMT((msg->palette_mode | 0xc))));
	reg =
		((reg & (~m_RGA2_SRC_INFO_SW_SW_CP_ENDIAN)) |
		 (s_RGA2_SRC_INFO_SW_SW_CP_ENDAIN(msg->endian_mode & 1)));
	*bRGA_SRC_VIR_INFO = src_stride >> 2;
	*bRGA_SRC_ACT_INFO =
		(msg->src.act_w - 1) | ((msg->src.act_h - 1) << 16);
	*bRGA_SRC_INFO = reg;

	*bRGA_SRC_FG_COLOR = msg->fg_color;
	*bRGA_SRC_BG_COLOR = msg->bg_color;

}

static void RGA2_set_reg_color_fill(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_CF_GR_A;
	u32 *bRGA_CF_GR_B;
	u32 *bRGA_CF_GR_G;
	u32 *bRGA_CF_GR_R;
	u32 *bRGA_SRC_FG_COLOR;
	u32 *bRGA_MASK_ADDR;
	u32 *bRGA_PAT_CON;

	u32 mask_stride;
	u32 *bRGA_SRC_VIR_INFO;

	bRGA_SRC_FG_COLOR = (u32 *) (base + RGA2_SRC_FG_COLOR_OFFSET);

	bRGA_CF_GR_A = (u32 *) (base + RGA2_CF_GR_A_OFFSET);
	bRGA_CF_GR_B = (u32 *) (base + RGA2_CF_GR_B_OFFSET);
	bRGA_CF_GR_G = (u32 *) (base + RGA2_CF_GR_G_OFFSET);
	bRGA_CF_GR_R = (u32 *) (base + RGA2_CF_GR_R_OFFSET);

	bRGA_MASK_ADDR = (u32 *) (base + RGA2_MASK_BASE_OFFSET);
	bRGA_PAT_CON = (u32 *) (base + RGA2_PAT_CON_OFFSET);

	bRGA_SRC_VIR_INFO = (u32 *) (base + RGA2_SRC_VIR_INFO_OFFSET);

	mask_stride = msg->rop_mask_stride;

	if (msg->color_fill_mode == 0) {
		/* solid color */
		*bRGA_CF_GR_A = (msg->gr_color.gr_x_a & 0xffff) |
			(msg->gr_color.gr_y_a << 16);
		*bRGA_CF_GR_B = (msg->gr_color.gr_x_b & 0xffff) |
			(msg->gr_color.gr_y_b << 16);
		*bRGA_CF_GR_G = (msg->gr_color.gr_x_g & 0xffff) |
			(msg->gr_color.gr_y_g << 16);
		*bRGA_CF_GR_R = (msg->gr_color.gr_x_r & 0xffff) |
			(msg->gr_color.gr_y_r << 16);

		*bRGA_SRC_FG_COLOR = msg->fg_color;
	} else {
		/* pattern color */
		*bRGA_MASK_ADDR = (u32) msg->pat.yrgb_addr;
		*bRGA_PAT_CON =
			(msg->pat.act_w - 1) | ((msg->pat.act_h - 1) << 8)
			| (msg->pat.x_offset << 16) | (msg->pat.y_offset << 24);
	}
	*bRGA_SRC_VIR_INFO = mask_stride << 16;
}

static void RGA2_set_reg_update_palette_table(u8 *base,
						 struct rga2_req *msg)
{
	u32 *bRGA_MASK_BASE;
	u32 *bRGA_FADING_CTRL;

	bRGA_MASK_BASE = (u32 *) (base + RGA2_MASK_BASE_OFFSET);
	bRGA_FADING_CTRL = (u32 *) (base + RGA2_FADING_CTRL_OFFSET);

	*bRGA_FADING_CTRL = msg->fading_g_value << 8;
	*bRGA_MASK_BASE = (u32) msg->pat.yrgb_addr;
}

static void RGA2_set_reg_update_patten_buff(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_PAT_MST;
	u32 *bRGA_PAT_CON;
	u32 *bRGA_PAT_START_POINT;
	u32 *bRGA_FADING_CTRL;
	u32 reg = 0;
	struct rga_img_info_t *pat;

	u32 num, offset;

	pat = &msg->pat;

	num = (pat->act_w * pat->act_h) - 1;

	offset = pat->act_w * pat->y_offset + pat->x_offset;

	bRGA_PAT_START_POINT = (u32 *) (base + RGA2_FADING_CTRL_OFFSET);
	bRGA_PAT_MST = (u32 *) (base + RGA2_MASK_BASE_OFFSET);
	bRGA_PAT_CON = (u32 *) (base + RGA2_PAT_CON_OFFSET);
	bRGA_FADING_CTRL = (u32 *) (base + RGA2_FADING_CTRL_OFFSET);

	*bRGA_PAT_MST = (u32) msg->pat.yrgb_addr;
	*bRGA_PAT_START_POINT = (pat->act_w * pat->y_offset) + pat->x_offset;

	reg = (pat->act_w - 1) | ((pat->act_h - 1) << 8) |
		(pat->x_offset << 16) | (pat->y_offset << 24);
	*bRGA_PAT_CON = reg;

	*bRGA_FADING_CTRL = (num << 8) | offset;
}

static void RGA2_set_pat_info(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_PAT_CON;
	u32 *bRGA_FADING_CTRL;
	u32 reg = 0;
	struct rga_img_info_t *pat;

	u32 num, offset;

	pat = &msg->pat;

	num = ((pat->act_w * pat->act_h) - 1) & 0xff;

	offset = (pat->act_w * pat->y_offset) + pat->x_offset;

	bRGA_PAT_CON = (u32 *) (base + RGA2_PAT_CON_OFFSET);
	bRGA_FADING_CTRL = (u32 *) (base + RGA2_FADING_CTRL_OFFSET);

	reg = (pat->act_w - 1) | ((pat->act_h - 1) << 8) |
		(pat->x_offset << 16) | (pat->y_offset << 24);
	*bRGA_PAT_CON = reg;
	*bRGA_FADING_CTRL = (num << 8) | offset;
}

static void RGA2_set_mmu_reg_info(u8 *base, struct rga2_req *msg)
{
	u32 *bRGA_MMU_CTRL1;
	u32 *bRGA_MMU_SRC_BASE;
	u32 *bRGA_MMU_SRC1_BASE;
	u32 *bRGA_MMU_DST_BASE;
	u32 *bRGA_MMU_ELS_BASE;

	u32 reg;

	bRGA_MMU_CTRL1 = (u32 *) (base + RGA2_MMU_CTRL1_OFFSET);
	bRGA_MMU_SRC_BASE = (u32 *) (base + RGA2_MMU_SRC_BASE_OFFSET);
	bRGA_MMU_SRC1_BASE = (u32 *) (base + RGA2_MMU_SRC1_BASE_OFFSET);
	bRGA_MMU_DST_BASE = (u32 *) (base + RGA2_MMU_DST_BASE_OFFSET);
	bRGA_MMU_ELS_BASE = (u32 *) (base + RGA2_MMU_ELS_BASE_OFFSET);

	reg = (msg->mmu_info.src0_mmu_flag & 0xf) |
		((msg->mmu_info.src1_mmu_flag & 0xf) << 4) |
		((msg->mmu_info.dst_mmu_flag & 0xf) << 8) |
		((msg->mmu_info.els_mmu_flag & 0x3) << 12);

	*bRGA_MMU_CTRL1 = reg;
	*bRGA_MMU_SRC_BASE = (u32) (msg->mmu_info.src0_base_addr) >> 4;
	*bRGA_MMU_SRC1_BASE = (u32) (msg->mmu_info.src1_base_addr) >> 4;
	*bRGA_MMU_DST_BASE = (u32) (msg->mmu_info.dst_base_addr) >> 4;
	*bRGA_MMU_ELS_BASE = (u32) (msg->mmu_info.els_base_addr) >> 4;
}

int rga2_gen_reg_info(u8 *base, struct rga2_req *msg)
{
	u8 dst_nn_quantize_en = 0;

	RGA2_set_mode_ctrl(base, msg);

	RGA2_set_pat_info(base, msg);

	switch (msg->render_mode) {
	case BITBLT_MODE:
		RGA2_set_reg_src_info(base, msg);
		RGA2_set_reg_dst_info(base, msg);
		dst_nn_quantize_en = (msg->alpha_rop_flag >> 8) & 0x1;
		if (dst_nn_quantize_en != 1) {
			if ((msg->dst.format !=
				RGA_FORMAT_Y4)) {
				RGA2_set_reg_alpha_info(base, msg);
				RGA2_set_reg_rop_info(base, msg);
			}
		}
		if (msg->mosaic_info.enable)
			RGA_set_reg_mosaic(base, msg);
		if (msg->osd_info.enable)
			RGA2_set_reg_osd(base, msg);

		break;
	case COLOR_FILL_MODE:
		RGA2_set_reg_color_fill(base, msg);
		RGA2_set_reg_dst_info(base, msg);
		RGA2_set_reg_alpha_info(base, msg);
		break;
	case COLOR_PALETTE_MODE:
		RGA2_set_reg_color_palette(base, msg);
		RGA2_set_reg_dst_info(base, msg);
		break;
	case UPDATE_PALETTE_TABLE_MODE:
		RGA2_set_reg_update_palette_table(base, msg);
		break;
	case UPDATE_PATTEN_BUF_MODE:
		RGA2_set_reg_update_patten_buff(base, msg);
		break;
	default:
		pr_err("ERROR msg render mode %d\n", msg->render_mode);
		break;
	}

	RGA2_set_mmu_reg_info(base, msg);

	return 0;
}

static void rga_cmd_to_rga2_cmd(struct rga_scheduler_t *scheduler,
				struct rga_req *req_rga, struct rga2_req *req)
{
	u16 alpha_mode_0, alpha_mode_1;

	if (req_rga->render_mode == 6)
		req->render_mode = UPDATE_PALETTE_TABLE_MODE;
	else if (req_rga->render_mode == 7)
		req->render_mode = UPDATE_PATTEN_BUF_MODE;
	else if (req_rga->render_mode == 5)
		req->render_mode = BITBLT_MODE;
	else
		req->render_mode = req_rga->render_mode;

	memcpy(&req->src, &req_rga->src, sizeof(req_rga->src));
	memcpy(&req->dst, &req_rga->dst, sizeof(req_rga->dst));
	/* The application will only import pat or src1. */
	if (req->render_mode == UPDATE_PALETTE_TABLE_MODE)
		memcpy(&req->pat, &req_rga->pat, sizeof(req_rga->pat));
	else
		memcpy(&req->src1, &req_rga->pat, sizeof(req_rga->pat));

	req->src.format = req_rga->src.format;
	req->dst.format = req_rga->dst.format;
	req->src1.format = req_rga->pat.format;

	switch (req_rga->rotate_mode & 0x0F) {
	case 1:
		if (req_rga->sina == 0 && req_rga->cosa == 65536) {
			/* rotate 0 */
			req->rotate_mode = 0;
		} else if (req_rga->sina == 65536 && req_rga->cosa == 0) {
			/* rotate 90 */
			req->rotate_mode = 1;
			req->dst.x_offset = req_rga->dst.x_offset;
			req->dst.act_w = req_rga->dst.act_h;
			req->dst.act_h = req_rga->dst.act_w;
		} else if (req_rga->sina == 0 && req_rga->cosa == -65536) {
			/* rotate 180 */
			req->rotate_mode = 2;
			req->dst.x_offset = req_rga->dst.x_offset;
			req->dst.y_offset = req_rga->dst.y_offset;
		} else if (req_rga->sina == -65536 && req_rga->cosa == 0) {
			/* totate 270 */
			req->rotate_mode = 3;
			req->dst.y_offset = req_rga->dst.y_offset;
			req->dst.act_w = req_rga->dst.act_h;
			req->dst.act_h = req_rga->dst.act_w;
		}
		break;
	case 2:
		//x_mirror
		req->rotate_mode |= (1 << 4);
		break;
	case 3:
		//y_mirror
		req->rotate_mode |= (2 << 4);
		break;
	case 4:
		//x_mirror+y_mirror
		req->rotate_mode |= (3 << 4);
		break;
	default:
		req->rotate_mode = 0;
		break;
	}

	switch ((req_rga->rotate_mode & 0xF0) >> 4) {
	case 2:
		//x_mirror
		req->rotate_mode |= (1 << 4);
		break;
	case 3:
		//y_mirror
		req->rotate_mode |= (2 << 4);
		break;
	case 4:
		//x_mirror+y_mirror
		req->rotate_mode |= (3 << 4);
		break;
	}

	if ((req->dst.act_w > 2048) && (req->src.act_h < req->dst.act_h))
		req->scale_bicu_mode |= (1 << 4);

	req->LUT_addr = req_rga->LUT_addr;
	req->rop_mask_addr = req_rga->rop_mask_addr;

	req->bitblt_mode = req_rga->bsfilter_flag;

	req->src_a_global_val = req_rga->alpha_global_value;
	req->dst_a_global_val = req_rga->alpha_global_value;
	req->rop_code = req_rga->rop_code;
	req->rop_mode = req_rga->alpha_rop_mode;

	req->color_fill_mode = req_rga->color_fill_mode;
	req->alpha_zero_key = req_rga->alpha_rop_mode >> 4;
	req->src_trans_mode = req_rga->src_trans_mode;
	req->color_key_min = req_rga->color_key_min;
	req->color_key_max = req_rga->color_key_max;

	req->fg_color = req_rga->fg_color;
	req->bg_color = req_rga->bg_color;
	memcpy(&req->gr_color, &req_rga->gr_color, sizeof(req_rga->gr_color));

	req->palette_mode = req_rga->palette_mode;
	req->yuv2rgb_mode = req_rga->yuv2rgb_mode;
	req->endian_mode = req_rga->endian_mode;
	req->rgb2yuv_mode = 0;

	req->fading_alpha_value = 0;
	req->fading_r_value = req_rga->fading.r;
	req->fading_g_value = req_rga->fading.g;
	req->fading_b_value = req_rga->fading.b;

	/* alpha mode set */
	req->alpha_rop_flag = 0;
	/* alpha_rop_enable */
	req->alpha_rop_flag |= (((req_rga->alpha_rop_flag & 1)));
	/* rop_enable */
	req->alpha_rop_flag |= (((req_rga->alpha_rop_flag >> 1) & 1) << 1);
	/* fading_enable */
	req->alpha_rop_flag |= (((req_rga->alpha_rop_flag >> 2) & 1) << 2);
	/* alpha_cal_mode_sel */
	req->alpha_rop_flag |= (((req_rga->alpha_rop_flag >> 4) & 1) << 3);
	/* dst_dither_down */
	req->alpha_rop_flag |= (((req_rga->alpha_rop_flag >> 5) & 1) << 6);
	/* gradient fill mode sel */
	req->alpha_rop_flag |= (((req_rga->alpha_rop_flag >> 6) & 1) << 7);
	/* RGA_NN_QUANTIZE */
	req->alpha_rop_flag |= (((req_rga->alpha_rop_flag >> 8) & 1) << 8);
	req->dither_mode = req_rga->dither_mode;

	/* RGA2 1106 add */
	memcpy(&req->mosaic_info, &req_rga->mosaic_info, sizeof(req_rga->mosaic_info));

	if ((scheduler->data->feature & RGA_YIN_YOUT) &&
	    rga_is_only_y_format(req->src.format) &&
	    rga_is_only_y_format(req->dst.format))
		req->yin_yout_en = true;

	req->uvhds_mode = req_rga->uvhds_mode;
	req->uvvds_mode = req_rga->uvvds_mode;

	memcpy(&req->osd_info, &req_rga->osd_info, sizeof(req_rga->osd_info));

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
					 * dst use globalAlpha, and
					 * dst does not have pixelAlpha.
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

			if (req->osd_info.enable) {
				/* set dst(osd_block) real color mode */
				if (req->alpha_mode_0 & (0x01 << 9))
					req->alpha_mode_0 |= (1 << 15);
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

	if (req_rga->mmu_info.mmu_en && (req_rga->mmu_info.mmu_flag & 1) == 1) {
		req->mmu_info.src0_mmu_flag = 1;
		req->mmu_info.dst_mmu_flag = 1;

		if (req_rga->mmu_info.mmu_flag >> 31) {
			req->mmu_info.src0_mmu_flag =
				((req_rga->mmu_info.mmu_flag >> 8) & 1);
			req->mmu_info.src1_mmu_flag =
				((req_rga->mmu_info.mmu_flag >> 9) & 1);
			req->mmu_info.dst_mmu_flag =
				((req_rga->mmu_info.mmu_flag >> 10) & 1);
			req->mmu_info.els_mmu_flag =
				((req_rga->mmu_info.mmu_flag >> 11) & 1);
		} else {
			if (req_rga->src.yrgb_addr >= 0xa0000000) {
				req->mmu_info.src0_mmu_flag = 0;
				req->src.yrgb_addr =
					req_rga->src.yrgb_addr - 0x60000000;
				req->src.uv_addr =
					req_rga->src.uv_addr - 0x60000000;
				req->src.v_addr =
					req_rga->src.v_addr - 0x60000000;
			}

			if (req_rga->dst.yrgb_addr >= 0xa0000000) {
				req->mmu_info.dst_mmu_flag = 0;
				req->dst.yrgb_addr =
					req_rga->dst.yrgb_addr - 0x60000000;
			}

			if (req_rga->pat.yrgb_addr >= 0xa0000000) {
				req->mmu_info.src1_mmu_flag = 0;
				req->src1.yrgb_addr =
					req_rga->pat.yrgb_addr - 0x60000000;
			}
		}
	}
}

void rga2_soft_reset(struct rga_scheduler_t *scheduler)
{
	u32 i;
	u32 reg;
	u32 iommu_dte_addr;

	if (scheduler->data->mmu == RGA_IOMMU)
		iommu_dte_addr = rga_read(0xf00, scheduler);

	rga_write((1 << 3) | (1 << 4) | (1 << 6), RGA2_SYS_CTRL, scheduler);

	for (i = 0; i < RGA_RESET_TIMEOUT; i++) {
		/* RGA_SYS_CTRL */
		reg = rga_read(RGA2_SYS_CTRL, scheduler) & 1;

		if (reg == 0)
			break;

		udelay(1);
	}

	if (scheduler->data->mmu == RGA_IOMMU) {
		rga_write(iommu_dte_addr, 0xf00, scheduler);
		/* enable iommu */
		rga_write(0, 0xf08, scheduler);
	}

	if (i == RGA_RESET_TIMEOUT)
		pr_err("soft reset timeout.\n");
}

static int rga2_check_param(const struct rga_hw_data *data, const struct rga2_req *req)
{
	if (!((req->render_mode == COLOR_FILL_MODE))) {
		if (unlikely(rga_hw_out_of_range(&data->input_range,
						 req->src.act_w, req->src.act_h))) {
			pr_err("invalid src resolution act_w = %d, act_h = %d\n",
				 req->src.act_w, req->src.act_h);
			return -EINVAL;
		}

		if (unlikely(req->src.vir_w * rga_get_pixel_stride_from_format(req->src.format) >
			     data->max_byte_stride * 8)) {
			pr_err("invalid src stride, stride = %d, max_byte_stride = %d\n",
			       req->src.vir_w, data->max_byte_stride);
			return -EINVAL;
		}

		if (unlikely(req->src.vir_w < req->src.act_w)) {
			pr_err("invalid src_vir_w act_w = %d, vir_w = %d\n",
			       req->src.act_w, req->src.vir_w);
			return -EINVAL;
		}
	}

	if (unlikely(rga_hw_out_of_range(&data->output_range, req->dst.act_w, req->dst.act_h))) {
		pr_err("invalid dst resolution act_w = %d, act_h = %d\n",
		       req->dst.act_w, req->dst.act_h);
		return -EINVAL;
	}

	if (unlikely(req->dst.vir_w * rga_get_pixel_stride_from_format(req->dst.format) >
		     data->max_byte_stride * 8)) {
		pr_err("invalid dst stride, stride = %d, max_byte_stride = %d\n",
		       req->dst.vir_w, data->max_byte_stride);
		return -EINVAL;
	}

	if (unlikely(req->dst.vir_w < req->dst.act_w)) {
		if (req->rotate_mode != 1) {
			pr_err("invalid dst_vir_w act_h = %d, vir_h = %d\n",
			       req->dst.act_w, req->dst.vir_w);
			return -EINVAL;
		}
	}

	return 0;
}

static int rga2_align_check(struct rga2_req *req)
{
	if (rga_is_yuv10bit_format(req->src.format))
		if ((req->src.vir_w % 16) || (req->src.x_offset % 2) ||
			(req->src.act_w % 2) || (req->src.y_offset % 2) ||
			(req->src.act_h % 2) || (req->src.vir_h % 2))
			pr_info("err src wstride, 10bit yuv\n");
	if (rga_is_yuv10bit_format(req->dst.format))
		if ((req->dst.vir_w % 16) || (req->dst.x_offset % 2) ||
			(req->dst.act_w % 2) || (req->dst.y_offset % 2) ||
			(req->dst.act_h % 2) || (req->dst.vir_h % 2))
			pr_info("err dst wstride, 10bit yuv\n");
	if (rga_is_yuv8bit_format(req->src.format))
		if ((req->src.vir_w % 4) || (req->src.x_offset % 2) ||
			(req->src.act_w % 2) || (req->src.y_offset % 2) ||
			(req->src.act_h % 2) || (req->src.vir_h % 2))
			pr_info("err src wstride, 8bit yuv\n");
	if (rga_is_yuv8bit_format(req->dst.format))
		if ((req->dst.vir_w % 4) || (req->dst.x_offset % 2) ||
			(req->dst.act_w % 2) || (req->dst.y_offset % 2) ||
			(req->dst.act_h % 2) || (req->dst.vir_h % 2))
			pr_info("err dst wstride, 8bit yuv\n");

	return 0;
}

static void print_debug_info(struct rga2_req *req)
{
	pr_info("render_mode:%s,bitblit_mode=%d,rotate_mode:%s\n",
		rga_get_render_mode_str(req->render_mode), req->bitblt_mode,
		rga_get_rotate_mode_str(req->rotate_mode));

	pr_info("src: y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d\n",
		 (unsigned long)req->src.yrgb_addr,
		 (unsigned long)req->src.uv_addr,
		 (unsigned long)req->src.v_addr,
		 req->src.act_w, req->src.act_h,
		 req->src.vir_w, req->src.vir_h);
	pr_info("src: xoff=%d yoff=%d format=%s\n",
		req->src.x_offset, req->src.y_offset,
		 rga_get_format_name(req->src.format));

	if (req->src1.yrgb_addr != 0 || req->src1.uv_addr != 0
		|| req->src1.v_addr != 0) {
		pr_info("src1: y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d\n",
			 (unsigned long)req->src1.yrgb_addr,
			 (unsigned long)req->src1.uv_addr,
			 (unsigned long)req->src1.v_addr,
			 req->src1.act_w, req->src1.act_h,
			 req->src1.vir_w, req->src1.vir_h);
		pr_info("src1: xoff=%d yoff=%d format=%s\n",
			req->src1.x_offset, req->src1.y_offset,
			 rga_get_format_name(req->src1.format));
	}

	pr_info("dst: y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d\n",
		 (unsigned long)req->dst.yrgb_addr,
		 (unsigned long)req->dst.uv_addr,
		 (unsigned long)req->dst.v_addr,
		 req->dst.act_w, req->dst.act_h,
		 req->dst.vir_w, req->dst.vir_h);
	pr_info("dst: xoff=%d yoff=%d format=%s\n",
		req->dst.x_offset, req->dst.y_offset,
		 rga_get_format_name(req->dst.format));

	pr_info("mmu: src=%.2x src1=%.2x dst=%.2x els=%.2x\n",
		req->mmu_info.src0_mmu_flag, req->mmu_info.src1_mmu_flag,
		req->mmu_info.dst_mmu_flag, req->mmu_info.els_mmu_flag);
	pr_info("alpha: flag %x mode0=%x mode1=%x\n", req->alpha_rop_flag,
		req->alpha_mode_0, req->alpha_mode_1);
	pr_info("blend mode is %s\n",
		rga_get_blend_mode_str(req->alpha_rop_flag, req->alpha_mode_0,
					req->alpha_mode_1));
	pr_info("yuv2rgb mode is %x\n", req->yuv2rgb_mode);
}

int rga2_init_reg(struct rga_job *job)
{
	struct rga2_req req;
	int ret = 0;
	struct rga_scheduler_t *scheduler = NULL;

	scheduler = job->scheduler;
	if (unlikely(scheduler == NULL)) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);
		return -EINVAL;
	}

	memset(&req, 0x0, sizeof(req));

	rga_cmd_to_rga2_cmd(scheduler, &job->rga_command_base, &req);
	memcpy(&job->full_csc, &job->rga_command_base.full_csc, sizeof(job->full_csc));
	memcpy(&job->pre_intr_info, &job->rga_command_base.pre_intr_info,
	       sizeof(job->pre_intr_info));

	/* check value if legal */
	ret = rga2_check_param(scheduler->data, &req);
	if (ret == -EINVAL) {
		pr_err("req argument is inval\n");
		return ret;
	}

	rga2_align_check(&req);

	/* for debug */
	if (DEBUGGER_EN(MSG))
		print_debug_info(&req);

	/* RGA2 mmu set */
	if ((req.mmu_info.src0_mmu_flag & 1) || (req.mmu_info.src1_mmu_flag & 1) ||
	    (req.mmu_info.dst_mmu_flag & 1) || (req.mmu_info.els_mmu_flag & 1)) {
		if (scheduler->data->mmu != RGA_MMU) {
			pr_err("core[%d] has no MMU, please use physically contiguous memory.\n",
			       scheduler->core);
			pr_err("mmu_flag[src, src1, dst, els] = [0x%x, 0x%x, 0x%x, 0x%x]\n",
			       req.mmu_info.src0_mmu_flag, req.mmu_info.src1_mmu_flag,
			       req.mmu_info.dst_mmu_flag, req.mmu_info.els_mmu_flag);
			return -EINVAL;
		}

		ret = rga_set_mmu_base(job, &req);
		if (ret < 0) {
			pr_err("%s, [%d] set mmu info error\n", __func__,
				 __LINE__);
			return -EFAULT;
		}
	}

	if (rga2_gen_reg_info((uint8_t *)job->cmd_reg, &req) == -1) {
		pr_err("gen reg info error\n");
		return -EINVAL;
	}

	return ret;
}

static void rga2_dump_read_back_sys_reg(struct rga_scheduler_t *scheduler)
{
	int i;
	unsigned long flags;
	uint32_t sys_reg[24] = {0};

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	for (i = 0; i < 24; i++)
		sys_reg[i] = rga_read(RGA2_SYS_REG_BASE + i * 4, scheduler);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	pr_info("SYS_READ_BACK_REG\n");
	for (i = 0; i < 6; i++)
		pr_info("0x%04x : %.8x %.8x %.8x %.8x\n",
			RGA2_SYS_REG_BASE + i * 0x10,
			sys_reg[0 + i * 4], sys_reg[1 + i * 4],
			sys_reg[2 + i * 4], sys_reg[3 + i * 4]);
}

static void rga2_dump_read_back_csc_reg(struct rga_scheduler_t *scheduler)
{
	int i;
	unsigned long flags;
	uint32_t csc_reg[12] = {0};

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	for (i = 0; i < 12; i++)
		csc_reg[i] = rga_read(RGA2_CSC_REG_BASE + i * 4, scheduler);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	pr_info("CSC_READ_BACK_REG\n");
	for (i = 0; i < 3; i++)
		pr_info("0x%04x : %.8x %.8x %.8x %.8x\n",
			RGA2_CSC_REG_BASE + i * 0x10,
			csc_reg[0 + i * 4], csc_reg[1 + i * 4],
			csc_reg[2 + i * 4], csc_reg[3 + i * 4]);
}

static void rga2_dump_read_back_cmd_reg(struct rga_scheduler_t *scheduler)
{
	int i;
	unsigned long flags;
	uint32_t cmd_reg[32] = {0};

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	for (i = 0; i < 32; i++)
		cmd_reg[i] = rga_read(RGA2_CMD_REG_BASE + i * 4, scheduler);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	pr_info("CMD_READ_BACK_REG\n");
	for (i = 0; i < 8; i++)
		pr_info("0x%04x : %.8x %.8x %.8x %.8x\n",
			RGA2_CMD_REG_BASE + i * 0x10,
			cmd_reg[0 + i * 4], cmd_reg[1 + i * 4],
			cmd_reg[2 + i * 4], cmd_reg[3 + i * 4]);
}

void rga2_dump_read_back_reg(struct rga_scheduler_t *scheduler)
{
	rga2_dump_read_back_sys_reg(scheduler);
	rga2_dump_read_back_csc_reg(scheduler);
	rga2_dump_read_back_cmd_reg(scheduler);
}

static void rga2_set_pre_intr_reg(struct rga_job *job, struct rga_scheduler_t *scheduler)
{
	uint32_t reg;

	if (job->pre_intr_info.read_intr_en) {
		reg = s_RGA2_READ_LINE_SW_INTR_LINE_RD_TH(job->pre_intr_info.read_threshold);
		rga_write(reg, RGA2_READ_LINE_CNT_OFFSET, scheduler);
	}

	if (job->pre_intr_info.write_intr_en) {
		reg = s_RGA2_WRITE_LINE_SW_INTR_LINE_WR_START(job->pre_intr_info.write_start);
		reg = ((reg & (~m_RGA2_WRITE_LINE_SW_INTR_LINE_WR_STEP)) |
		       (s_RGA2_WRITE_LINE_SW_INTR_LINE_WR_STEP(job->pre_intr_info.write_step)));
		rga_write(reg, RGA2_WRITE_LINE_CNT_OFFSET, scheduler);
	}

	reg = rga_read(RGA2_SYS_CTRL_OFFSET, scheduler);
	reg = ((reg & (~m_RGA2_SYS_HOLD_MODE_EN)) |
	       (s_RGA2_SYS_HOLD_MODE_EN(job->pre_intr_info.read_hold_en)));
	rga_write(reg, RGA2_SYS_CTRL_OFFSET, scheduler);

	reg = rga_read(RGA2_INT_OFFSET, scheduler);
	reg = (reg | s_RGA2_INT_LINE_RD_CLEAR(0x1) | s_RGA2_INT_LINE_WR_CLEAR(0x1));
	reg = ((reg & (~m_RGA2_INT_LINE_RD_EN)) |
	       (s_RGA2_INT_LINE_RD_EN(job->pre_intr_info.read_intr_en)));
	reg = ((reg & (~m_RGA2_INT_LINE_WR_EN)) |
	       (s_RGA2_INT_LINE_WR_EN(job->pre_intr_info.write_intr_en)));
	rga_write(reg, RGA2_INT_OFFSET, scheduler);
}

static void rga2_set_reg_full_csc(struct rga_job *job, struct rga_scheduler_t *scheduler)
{
	uint8_t clip_y_max, clip_y_min;
	uint8_t clip_uv_max, clip_uv_min;

	clip_y_max = 0xff;
	clip_y_min = 0x0;
	clip_uv_max = 0xff;
	clip_uv_min = 0;

	/* full csc coefficient */
	/* Y coefficient */
	rga_write(job->full_csc.coe_y.r_v | (clip_y_max << 16) | (clip_y_min << 24),
		  RGA2_DST_CSC_00_OFFSET, scheduler);
	rga_write(job->full_csc.coe_y.g_y | (clip_uv_max << 16) | (clip_uv_min << 24),
		  RGA2_DST_CSC_01_OFFSET, scheduler);
	rga_write(job->full_csc.coe_y.b_u, RGA2_DST_CSC_02_OFFSET, scheduler);
	rga_write(job->full_csc.coe_y.off, RGA2_DST_CSC_OFF0_OFFSET, scheduler);

	/* U coefficient */
	rga_write(job->full_csc.coe_u.r_v, RGA2_DST_CSC_10_OFFSET, scheduler);
	rga_write(job->full_csc.coe_u.g_y, RGA2_DST_CSC_11_OFFSET, scheduler);
	rga_write(job->full_csc.coe_u.b_u, RGA2_DST_CSC_12_OFFSET, scheduler);
	rga_write(job->full_csc.coe_u.off, RGA2_DST_CSC_OFF1_OFFSET, scheduler);

	/* V coefficient */
	rga_write(job->full_csc.coe_v.r_v, RGA2_DST_CSC_20_OFFSET, scheduler);
	rga_write(job->full_csc.coe_v.g_y, RGA2_DST_CSC_21_OFFSET, scheduler);
	rga_write(job->full_csc.coe_v.b_u, RGA2_DST_CSC_22_OFFSET, scheduler);
	rga_write(job->full_csc.coe_v.off, RGA2_DST_CSC_OFF2_OFFSET, scheduler);
}

int rga2_set_reg(struct rga_job *job, struct rga_scheduler_t *scheduler)
{
	ktime_t now = ktime_get();
	int i;

	if (job->pre_intr_info.enable)
		rga2_set_pre_intr_reg(job, scheduler);

	if (job->full_csc.flag)
		rga2_set_reg_full_csc(job, scheduler);

	if (DEBUGGER_EN(REG)) {
		int32_t *p;

		rga2_dump_read_back_sys_reg(scheduler);
		rga2_dump_read_back_csc_reg(scheduler);

		p = job->cmd_reg;
		pr_info("CMD_REG\n");
		for (i = 0; i < 8; i++)
			pr_info("i = %x : %.8x %.8x %.8x %.8x\n", i,
				p[0 + i * 4], p[1 + i * 4],
				p[2 + i * 4], p[3 + i * 4]);
	}

	/* All CMD finish int */
	rga_write(rga_read(RGA2_INT, scheduler) |
		  (0x1 << 10) | (0x1 << 9) | (0x1 << 8), RGA2_INT, scheduler);

	/* sys_reg init */
	rga_write((0x1 << 2) | (0x1 << 5) | (0x1 << 6) | (0x1 << 11) | (0x1 << 12),
		  RGA2_SYS_CTRL, scheduler);

	if (RGA2_USE_MASTER_MODE) {
		/* master mode */
		rga_write(rga_read(RGA2_SYS_CTRL, scheduler) | (0x1 << 1),
			  RGA2_SYS_CTRL, scheduler);

		/* cmd buffer flush cache to ddr */
		rga_dma_sync_flush_range(&job->cmd_reg[0], &job->cmd_reg[32], scheduler);

		/* set cmd_addr */
		rga_write(virt_to_phys(job->cmd_reg), RGA2_CMD_BASE, scheduler);

		rga_write(1, RGA2_CMD_CTRL, scheduler);
	} else {
		/* slave mode */
		rga_write(rga_read(RGA2_SYS_CTRL, scheduler) | (0x0 << 1),
			  RGA2_SYS_CTRL, scheduler);

		/* set cmd_reg */
		for (i = 0; i <= 32; i++)
			rga_write(job->cmd_reg[i], 0x100 + i * 4, scheduler);

		rga_write(rga_read(RGA2_SYS_CTRL, scheduler) | 0x1, RGA2_SYS_CTRL, scheduler);
	}

	if (DEBUGGER_EN(TIME)) {
		pr_info("sys_ctrl = %x, int = %x, set cmd use time = %lld\n",
			rga_read(RGA2_SYS_CTRL, scheduler),
			rga_read(RGA2_INT, scheduler),
			ktime_us_delta(now, job->timestamp));
	}

	job->hw_running_time = now;
	job->hw_recoder_time = now;

	if (DEBUGGER_EN(REG))
		rga2_dump_read_back_reg(scheduler);

	return 0;
}

int rga2_get_version(struct rga_scheduler_t *scheduler)
{
	u32 major_version, minor_version, svn_version;
	u32 reg_version;

	if (!scheduler) {
		pr_err("scheduler is null\n");
		return -EINVAL;
	}

	reg_version = rga_read(RGA2_VERSION_NUM, scheduler);

	major_version = (reg_version & RGA2_MAJOR_VERSION_MASK) >> 24;
	minor_version = (reg_version & RGA2_MINOR_VERSION_MASK) >> 20;
	svn_version = (reg_version & RGA2_SVN_VERSION_MASK);

	/*
	 * some old rga ip has no rga version register, so force set to 2.00
	 */
	if (!major_version && !minor_version)
		major_version = 2;

	snprintf(scheduler->version.str, 10, "%x.%01x.%05x", major_version,
		 minor_version, svn_version);

	scheduler->version.major = major_version;
	scheduler->version.minor = minor_version;
	scheduler->version.revision = svn_version;

	return 0;
}
