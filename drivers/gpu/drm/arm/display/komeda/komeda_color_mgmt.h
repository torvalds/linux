/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */

#ifndef _KOMEDA_COLOR_MGMT_H_
#define _KOMEDA_COLOR_MGMT_H_

#include <drm/drm_color_mgmt.h>

#define KOMEDA_N_YUV2RGB_COEFFS		12

const s32 *komeda_select_yuv2rgb_coeffs(u32 color_encoding, u32 color_range);

#endif
