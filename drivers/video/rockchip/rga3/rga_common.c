// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Cerf Yu <cerf.yu@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_common: " fmt

#include "rga.h"
#include "rga_common.h"

bool rga_is_rgb_format(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_RGBA_8888:
	case RGA_FORMAT_RGBX_8888:
	case RGA_FORMAT_RGB_888:
	case RGA_FORMAT_BGRA_8888:
	case RGA_FORMAT_BGRX_8888:
	case RGA_FORMAT_BGR_888:
	case RGA_FORMAT_RGB_565:
	case RGA_FORMAT_RGBA_5551:
	case RGA_FORMAT_RGBA_4444:
	case RGA_FORMAT_BGR_565:
	case RGA_FORMAT_BGRA_5551:
	case RGA_FORMAT_BGRA_4444:
	case RGA_FORMAT_ARGB_8888:
	case RGA_FORMAT_XRGB_8888:
	case RGA_FORMAT_ARGB_5551:
	case RGA_FORMAT_ARGB_4444:
	case RGA_FORMAT_ABGR_8888:
	case RGA_FORMAT_XBGR_8888:
	case RGA_FORMAT_ABGR_5551:
	case RGA_FORMAT_ABGR_4444:
		return true;
	default:
		return false;
	}
}

bool rga_is_yuv_format(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_Y4:
	case RGA_FORMAT_YCbCr_400:

	case RGA_FORMAT_YCbCr_422_SP:
	case RGA_FORMAT_YCbCr_422_P:
	case RGA_FORMAT_YCbCr_420_SP:
	case RGA_FORMAT_YCbCr_420_P:
	case RGA_FORMAT_YCrCb_422_SP:
	case RGA_FORMAT_YCrCb_422_P:
	case RGA_FORMAT_YCrCb_420_SP:
	case RGA_FORMAT_YCrCb_420_P:

	case RGA_FORMAT_YVYU_422:
	case RGA_FORMAT_YVYU_420:
	case RGA_FORMAT_VYUY_422:
	case RGA_FORMAT_VYUY_420:
	case RGA_FORMAT_YUYV_422:
	case RGA_FORMAT_YUYV_420:
	case RGA_FORMAT_UYVY_422:
	case RGA_FORMAT_UYVY_420:

	case RGA_FORMAT_YCbCr_420_SP_10B:
	case RGA_FORMAT_YCrCb_420_SP_10B:
	case RGA_FORMAT_YCbCr_422_SP_10B:
	case RGA_FORMAT_YCrCb_422_SP_10B:
		return true;
	default:
		return false;
	}
}

bool rga_is_alpha_format(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_RGBA_8888:
	case RGA_FORMAT_BGRA_8888:
	case RGA_FORMAT_RGBA_5551:
	case RGA_FORMAT_RGBA_4444:
	case RGA_FORMAT_BGRA_5551:
	case RGA_FORMAT_BGRA_4444:
	case RGA_FORMAT_ARGB_8888:
	case RGA_FORMAT_ARGB_5551:
	case RGA_FORMAT_ARGB_4444:
	case RGA_FORMAT_ABGR_8888:
	case RGA_FORMAT_ABGR_5551:
	case RGA_FORMAT_ABGR_4444:
		return true;
	default:
		return false;
	}
}

bool rga_is_yuv420_packed_format(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_YVYU_420:
	case RGA_FORMAT_VYUY_420:
	case RGA_FORMAT_YUYV_420:
	case RGA_FORMAT_UYVY_420:
		return true;
	default:
		return false;
	}
}

bool rga_is_yuv422_packed_format(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_YVYU_422:
	case RGA_FORMAT_VYUY_422:
	case RGA_FORMAT_YUYV_422:
	case RGA_FORMAT_UYVY_422:
		return true;
	default:
		return false;
	}
}

bool rga_is_yuv8bit_format(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_Y4:
	case RGA_FORMAT_YCbCr_400:

	case RGA_FORMAT_YCbCr_422_SP:
	case RGA_FORMAT_YCbCr_422_P:
	case RGA_FORMAT_YCbCr_420_SP:
	case RGA_FORMAT_YCbCr_420_P:
	case RGA_FORMAT_YCrCb_422_SP:
	case RGA_FORMAT_YCrCb_422_P:
	case RGA_FORMAT_YCrCb_420_SP:
	case RGA_FORMAT_YCrCb_420_P:

	case RGA_FORMAT_YVYU_422:
	case RGA_FORMAT_YVYU_420:
	case RGA_FORMAT_VYUY_422:
	case RGA_FORMAT_VYUY_420:
	case RGA_FORMAT_YUYV_422:
	case RGA_FORMAT_YUYV_420:
	case RGA_FORMAT_UYVY_422:
	case RGA_FORMAT_UYVY_420:
		return true;
	default:
		return false;
	}
}

bool rga_is_yuv10bit_format(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_YCbCr_420_SP_10B:
	case RGA_FORMAT_YCrCb_420_SP_10B:
	case RGA_FORMAT_YCbCr_422_SP_10B:
	case RGA_FORMAT_YCrCb_422_SP_10B:
		return true;
	default:
		return false;
	}
}

bool rga_is_yuv422p_format(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_YCbCr_422_P:
	case RGA_FORMAT_YCrCb_422_P:
		return true;
	default:
		return false;
	}
}

int rga_get_format_bits(uint32_t format)
{
	int bits = 0;

	switch (format) {
	case RGA_FORMAT_RGBA_8888:
	case RGA_FORMAT_RGBX_8888:
	case RGA_FORMAT_BGRA_8888:
	case RGA_FORMAT_BGRX_8888:
	case RGA_FORMAT_ARGB_8888:
	case RGA_FORMAT_XRGB_8888:
	case RGA_FORMAT_ABGR_8888:
	case RGA_FORMAT_XBGR_8888:
		bits = 32;
		break;
	case RGA_FORMAT_RGB_888:
	case RGA_FORMAT_BGR_888:
		bits = 24;
		break;
	case RGA_FORMAT_RGB_565:
	case RGA_FORMAT_RGBA_5551:
	case RGA_FORMAT_RGBA_4444:
	case RGA_FORMAT_BGR_565:
	case RGA_FORMAT_YCbCr_422_SP:
	case RGA_FORMAT_YCbCr_422_P:
	case RGA_FORMAT_YCrCb_422_SP:
	case RGA_FORMAT_YCrCb_422_P:
	case RGA_FORMAT_BGRA_5551:
	case RGA_FORMAT_BGRA_4444:
	case RGA_FORMAT_ARGB_5551:
	case RGA_FORMAT_ARGB_4444:
	case RGA_FORMAT_ABGR_5551:
	case RGA_FORMAT_ABGR_4444:
		bits = 16;
		break;
	case RGA_FORMAT_YCbCr_420_SP:
	case RGA_FORMAT_YCbCr_420_P:
	case RGA_FORMAT_YCrCb_420_SP:
	case RGA_FORMAT_YCrCb_420_P:
		bits = 12;
		break;
	case RGA_FORMAT_YCbCr_420_SP_10B:
	case RGA_FORMAT_YCrCb_420_SP_10B:
	case RGA_FORMAT_YCbCr_422_SP_10B:
	case RGA_FORMAT_YCrCb_422_SP_10B:
		bits = 15;
		break;
	default:
		pr_err("unknown format [%d]\n", format);
		return -1;
	}

	return bits;
}

const char *rga_get_format_name(uint32_t format)
{
	switch (format) {
	case RGA_FORMAT_RGBA_8888:
		return "RGBA8888";
	case RGA_FORMAT_RGBX_8888:
		return "RGBX8888";
	case RGA_FORMAT_RGB_888:
		return "RGB888";
	case RGA_FORMAT_BGRA_8888:
		return "BGRA8888";
	case RGA_FORMAT_BGRX_8888:
		return "BGRX8888";
	case RGA_FORMAT_BGR_888:
		return "BGR888";
	case RGA_FORMAT_RGB_565:
		return "RGB565";
	case RGA_FORMAT_RGBA_5551:
		return "RGBA5551";
	case RGA_FORMAT_RGBA_4444:
		return "RGBA4444";
	case RGA_FORMAT_BGR_565:
		return "BGR565";
	case RGA_FORMAT_BGRA_5551:
		return "BGRA5551";
	case RGA_FORMAT_BGRA_4444:
		return "BGRA4444";

	case RGA_FORMAT_YCbCr_422_SP:
		return "YCbCr422SP";
	case RGA_FORMAT_YCbCr_422_P:
		return "YCbCr422P";
	case RGA_FORMAT_YCbCr_420_SP:
		return "YCbCr420SP";
	case RGA_FORMAT_YCbCr_420_P:
		return "YCbCr420P";
	case RGA_FORMAT_YCrCb_422_SP:
		return "YCrCb422SP";
	case RGA_FORMAT_YCrCb_422_P:
		return "YCrCb422P";
	case RGA_FORMAT_YCrCb_420_SP:
		return "YCrCb420SP";
	case RGA_FORMAT_YCrCb_420_P:
		return "YCrCb420P";

	case RGA_FORMAT_YVYU_422:
		return "YVYU422";
	case RGA_FORMAT_YVYU_420:
		return "YVYU420";
	case RGA_FORMAT_VYUY_422:
		return "VYUY422";
	case RGA_FORMAT_VYUY_420:
		return "VYUY420";
	case RGA_FORMAT_YUYV_422:
		return "YUYV422";
	case RGA_FORMAT_YUYV_420:
		return "YUYV420";
	case RGA_FORMAT_UYVY_422:
		return "UYVY422";
	case RGA_FORMAT_UYVY_420:
		return "UYVY420";

	case RGA_FORMAT_YCbCr_420_SP_10B:
		return "YCrCb420SP10B";
	case RGA_FORMAT_YCrCb_420_SP_10B:
		return "YCbCr420SP10B";
	case RGA_FORMAT_YCbCr_422_SP_10B:
		return "YCbCr422SP10B";
	case RGA_FORMAT_YCrCb_422_SP_10B:
		return "YCrCb422SP10B";
	case RGA_FORMAT_BPP1:
		return "BPP1";
	case RGA_FORMAT_BPP2:
		return "BPP2";
	case RGA_FORMAT_BPP4:
		return "BPP4";
	case RGA_FORMAT_BPP8:
		return "BPP8";
	case RGA_FORMAT_YCbCr_400:
		return "YCbCr400";
	case RGA_FORMAT_Y4:
		return "y4";

	case RGA_FORMAT_ARGB_8888:
		return "ARGB8888";
	case RGA_FORMAT_XRGB_8888:
		return "XRGB8888";
	case RGA_FORMAT_ARGB_5551:
		return "ARGB5551";
	case RGA_FORMAT_ARGB_4444:
		return "ARGB4444";
	case RGA_FORMAT_ABGR_8888:
		return "ABGR8888";
	case RGA_FORMAT_XBGR_8888:
		return "XBGR8888";
	case RGA_FORMAT_ABGR_5551:
		return "ABGR5551";
	case RGA_FORMAT_ABGR_4444:
		return "ABGR4444";
	default:
		return "UNF";
	}
}

const char *rga_get_render_mode_str(uint8_t mode)
{
	switch (mode) {
	case 0x0:
		return "bitblt";
	case 0x1:
		return "RGA_COLOR_PALETTE";
	case 0x2:
		return "RGA_COLOR_FILL";
	case 0x3:
		return "update_palette_table";
	case 0x4:
		return "update_patten_buff";
	default:
		return "UNF";
	}
}

const char *rga_get_rotate_mode_str(uint8_t mode)
{
	switch (mode) {
	case 0x0:
		return "0";
	case 0x1:
		return "90 degree";
	case 0x2:
		return "180 degree";
	case 0x3:
		return "270 degree";
	case 0x10:
		return "xmirror";
	case 0x20:
		return "ymirror";
	case 0x30:
		return "xymirror";
	default:
		return "UNF";
	}
}

const char *rga_get_blend_mode_str(uint16_t alpha_rop_flag,
				   uint16_t alpha_mode_0,
				   uint16_t alpha_mode_1)
{
	if (alpha_rop_flag == 0) {
		return "no blend";
	} else if (alpha_rop_flag == 0x9) {
		if (alpha_mode_0 == 0x381A && alpha_mode_1 == 0x381A)
			return "105 src + (1-src.a)*dst";
		else if (alpha_mode_0 == 0x483A && alpha_mode_1 == 0x483A)
			return "405 src.a * src + (1-src.a) * dst";
		else
			return "check reg for more imformation";
	} else {
		return "check reg for more imformation";
	}
}

void rga_convert_addr(struct rga_img_info_t *img, bool before_vir_get_channel)
{
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
		if (rga_is_yuv422p_format(img->format))
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
