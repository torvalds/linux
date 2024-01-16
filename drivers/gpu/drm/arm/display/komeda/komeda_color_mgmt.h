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
#define KOMEDA_N_RGB2YUV_COEFFS		12
#define KOMEDA_COLOR_PRECISION		12
#define KOMEDA_N_GAMMA_COEFFS		65
#define KOMEDA_COLOR_LUT_SIZE		BIT(KOMEDA_COLOR_PRECISION)
#define KOMEDA_N_CTM_COEFFS		9

void drm_lut_to_fgamma_coeffs(struct drm_property_blob *lut_blob, u32 *coeffs);
void drm_ctm_to_coeffs(struct drm_property_blob *ctm_blob, u32 *coeffs);

const s32 *komeda_select_yuv2rgb_coeffs(u32 color_encoding, u32 color_range);

#endif /*_KOMEDA_COLOR_MGMT_H_*/
