/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *  Cerf Yu <cerf.yu@rock-chips.com>
 */

#ifndef __LINUX_RKRGA_COMMON_H_
#define __LINUX_RKRGA_COMMON_H_

#include "rga_drv.h"

#define RGA_GET_PAGE_COUNT(size) (((size) >> PAGE_SHIFT) + (((size) & (~PAGE_MASK)) ? 1 : 0))

bool rga_is_rgb_format(uint32_t format);
bool rga_is_yuv_format(uint32_t format);
bool rga_is_alpha_format(uint32_t format);
bool rga_is_yuv420_packed_format(uint32_t format);
bool rga_is_yuv422_packed_format(uint32_t format);
bool rga_is_yuv8bit_format(uint32_t format);
bool rga_is_yuv10bit_format(uint32_t format);
bool rga_is_yuv422p_format(uint32_t format);
bool rga_is_only_y_format(uint32_t format);

int rga_get_format_bits(uint32_t format);
uint32_t rga_get_pixel_stride_from_format(uint32_t format);

const char *rga_get_format_name(uint32_t format);
const char *rga_get_render_mode_str(uint8_t mode);
const char *rga_get_rotate_mode_str(uint8_t mode);
const char *rga_get_blend_mode_str(uint16_t alpha_rop_flag,
				   uint16_t alpha_mode_0,
				   uint16_t alpha_mode_1);
const char *rga_get_memory_type_str(uint8_t type);

void rga_convert_addr(struct rga_img_info_t *img, bool before_vir_get_channel);
void rga_swap_pd_mode(struct rga_req *req_rga);
int rga_image_size_cal(int w, int h, int format,
		       int *yrgb_size, int *uv_size, int *v_size);

#endif
