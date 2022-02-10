// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Cerf Yu <cerf.yu@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_common: " fmt

#include "rga.h"

void rga_user_format_convert(uint32_t *df, uint32_t sf)
{
	switch (sf) {
	case 0x0:
		*df = RGA2_FORMAT_RGBA_8888;
		break;
	case 0x1:
		*df = RGA2_FORMAT_RGBX_8888;
		break;
	case 0x2:
		*df = RGA2_FORMAT_RGB_888;
		break;
	case 0x3:
		*df = RGA2_FORMAT_BGRA_8888;
		break;
	case 0x4:
		*df = RGA2_FORMAT_RGB_565;
		break;
	case 0x5:
		*df = RGA2_FORMAT_RGBA_5551;
		break;
	case 0x6:
		*df = RGA2_FORMAT_RGBA_4444;
		break;
	case 0x7:
		*df = RGA2_FORMAT_BGR_888;
		break;
	case 0x16:
		*df = RGA2_FORMAT_BGRX_8888;
		break;
	case 0x8:
		*df = RGA2_FORMAT_YCbCr_422_SP;
		break;
	case 0x9:
		*df = RGA2_FORMAT_YCbCr_422_P;
		break;
	case 0xa:
		*df = RGA2_FORMAT_YCbCr_420_SP;
		break;
	case 0xb:
		*df = RGA2_FORMAT_YCbCr_420_P;
		break;
	case 0xc:
		*df = RGA2_FORMAT_YCrCb_422_SP;
		break;
	case 0xd:
		*df = RGA2_FORMAT_YCrCb_422_P;
		break;
	case 0xe:
		*df = RGA2_FORMAT_YCrCb_420_SP;
		break;
	case 0xf:
		*df = RGA2_FORMAT_YCrCb_420_P;
		break;

	case 0x10:
		*df = RGA2_FORMAT_BPP_1;
		break;
	case 0x11:
		*df = RGA2_FORMAT_BPP_2;
		break;
	case 0x12:
		*df = RGA2_FORMAT_BPP_4;
		break;
	case 0x13:
		*df = RGA2_FORMAT_BPP_8;
		break;

	case 0x14:
		*df = RGA2_FORMAT_Y4;
		break;
	case 0x15:
		*df = RGA2_FORMAT_YCbCr_400;
		break;

	case 0x18:
		*df = RGA2_FORMAT_YVYU_422;
		break;
	case 0x19:
		*df = RGA2_FORMAT_YVYU_420;
		break;
	case 0x1a:
		*df = RGA2_FORMAT_VYUY_422;
		break;
	case 0x1b:
		*df = RGA2_FORMAT_VYUY_420;
		break;
	case 0x1c:
		*df = RGA2_FORMAT_YUYV_422;
		break;
	case 0x1d:
		*df = RGA2_FORMAT_YUYV_420;
		break;
	case 0x1e:
		*df = RGA2_FORMAT_UYVY_422;
		break;
	case 0x1f:
		*df = RGA2_FORMAT_UYVY_420;
		break;

	case 0x20:
		*df = RGA2_FORMAT_YCbCr_420_SP_10B;
		break;
	case 0x21:
		*df = RGA2_FORMAT_YCrCb_420_SP_10B;
		break;
	case 0x22:
		*df = RGA2_FORMAT_YCbCr_422_SP_10B;
		break;
	case 0x23:
		*df = RGA2_FORMAT_YCrCb_422_SP_10B;
		break;

	case 0x24:
		*df = RGA2_FORMAT_BGR_565;
		break;
	case 0x25:
		*df = RGA2_FORMAT_BGRA_5551;
		break;
	case 0x26:
		*df = RGA2_FORMAT_BGRA_4444;
		break;

	case 0x28:
		*df = RGA2_FORMAT_ARGB_8888;
		break;
	case 0x29:
		*df = RGA2_FORMAT_XRGB_8888;
		break;
	case 0x2a:
		*df = RGA2_FORMAT_ARGB_5551;
		break;
	case 0x2b:
		*df = RGA2_FORMAT_ARGB_4444;
		break;
	case 0x2c:
		*df = RGA2_FORMAT_ABGR_8888;
		break;
	case 0x2d:
		*df = RGA2_FORMAT_XBGR_8888;
		break;
	case 0x2e:
		*df = RGA2_FORMAT_ABGR_5551;
		break;
	case 0x2f:
		*df = RGA2_FORMAT_ABGR_4444;
		break;
	}
}

bool rga_is_yuv422p_format(u32 format)
{
	bool ret = false;

	switch (format) {
	case RGA2_FORMAT_YCbCr_422_P:
	case RGA2_FORMAT_YCrCb_422_P:
		ret = true;
		break;
	}
	return ret;
}

void rga_convert_addr(struct rga_img_info_t *img, bool before_vir_get_channel)
{
	uint32_t fmt = 0;

	rga_user_format_convert(&fmt, img->format);

	/*
	 * If it is not using dma fd, the virtual/phyical address is assigned
	 * to the address of the corresponding channel.
	 */

	//img->yrgb_addr = img->uv_addr;

	/*
	 * if before_vir_get_channel is true, then convert addr by default
	 * when has iova (before_vir_get_channel is false),
	 * need to consider whether fbc case
	 */
	if (img->rd_mode != RGA_FBC_MODE || before_vir_get_channel) {
		img->uv_addr = img->yrgb_addr + (img->vir_w * img->vir_h);

		//warning: rga3 may need /2 for all
		if (rga_is_yuv422p_format(fmt))
			img->v_addr =
				img->uv_addr + (img->vir_w * img->vir_h) / 2;
		else
			img->v_addr =
				img->uv_addr + (img->vir_w * img->vir_h) / 4;
	} else {
		img->uv_addr = img->yrgb_addr;
		img->v_addr = 0;
	}
}

int rga_get_format_bits(u32 format)
{
	int bits = 0;

	switch (format) {
	case RGA2_FORMAT_RGBA_8888:
	case RGA2_FORMAT_RGBX_8888:
	case RGA2_FORMAT_BGRA_8888:
	case RGA2_FORMAT_BGRX_8888:
	case RGA2_FORMAT_ARGB_8888:
	case RGA2_FORMAT_XRGB_8888:
	case RGA2_FORMAT_ABGR_8888:
	case RGA2_FORMAT_XBGR_8888:
		bits = 32;
		break;
	case RGA2_FORMAT_RGB_888:
	case RGA2_FORMAT_BGR_888:
		bits = 24;
		break;
	case RGA2_FORMAT_RGB_565:
	case RGA2_FORMAT_RGBA_5551:
	case RGA2_FORMAT_RGBA_4444:
	case RGA2_FORMAT_BGR_565:
	case RGA2_FORMAT_YCbCr_422_SP:
	case RGA2_FORMAT_YCbCr_422_P:
	case RGA2_FORMAT_YCrCb_422_SP:
	case RGA2_FORMAT_YCrCb_422_P:
	case RGA2_FORMAT_BGRA_5551:
	case RGA2_FORMAT_BGRA_4444:
	case RGA2_FORMAT_ARGB_5551:
	case RGA2_FORMAT_ARGB_4444:
	case RGA2_FORMAT_ABGR_5551:
	case RGA2_FORMAT_ABGR_4444:
		bits = 16;
		break;
	case RGA2_FORMAT_YCbCr_420_SP:
	case RGA2_FORMAT_YCbCr_420_P:
	case RGA2_FORMAT_YCrCb_420_SP:
	case RGA2_FORMAT_YCrCb_420_P:
		bits = 12;
		break;
	case RGA2_FORMAT_YCbCr_420_SP_10B:
	case RGA2_FORMAT_YCrCb_420_SP_10B:
	case RGA2_FORMAT_YCbCr_422_SP_10B:
	case RGA2_FORMAT_YCrCb_422_SP_10B:
		bits = 15;
		break;
	default:
		pr_err("unknown format [%d]\n", format);
		return -1;
	}

	return bits;
}
