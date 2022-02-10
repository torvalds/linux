/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *  Cerf Yu <cerf.yu@rock-chips.com>
 */

#ifndef __LINUX_RKRGA_COMMON_H_
#define __LINUX_RKRGA_COMMON_H_

void rga_user_format_convert(uint32_t *df, uint32_t sf);

bool rga_is_yuv422p_format(u32 format);
void rga_convert_addr(struct rga_img_info_t *img, bool before_vir_get_channel);
int rga_get_format_bits(u32 format);

#endif
