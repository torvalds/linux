/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *  Cerf Yu <cerf.yu@rock-chips.com>
 */

#ifndef __LINUX_RKRGA_COMMON_H_
#define __LINUX_RKRGA_COMMON_H_

bool rga_is_rgb_format(uint32_t format);
bool rga_is_yuv_format(uint32_t format);
bool rga_is_alpha_format(uint32_t format);
bool rga_is_yuv420_packed_format(uint32_t format);
bool rga_is_yuv422_packed_format(uint32_t format);
bool rga_is_yuv8bit_format(uint32_t format);
bool rga_is_yuv10bit_format(uint32_t format);
bool rga_is_yuv422p_format(uint32_t format);

int rga_get_format_bits(uint32_t format);

const char *rga_get_format_name(uint32_t format);
const char *rga_get_render_mode_str(uint8_t mode);
const char *rga_get_rotate_mode_str(uint8_t mode);
const char *rga_get_blend_mode_str(uint16_t alpha_rop_flag,
				   uint16_t alpha_mode_0,
				   uint16_t alpha_mode_1);

void rga_convert_addr(struct rga_img_info_t *img, bool before_vir_get_channel);

#endif
