/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ROCKCHIP_AV1_FILMGRAIN_H_
#define _ROCKCHIP_AV1_FILMGRAIN_H_

#include <linux/types.h>

void rockchip_av1_generate_luma_grain_block(s32 (*luma_grain_block)[73][82],
					    s32 bitdepth,
					    u8 num_y_points,
					    s32 grain_scale_shift,
					    s32 ar_coeff_lag,
					    s32 (*ar_coeffs_y)[24],
					    s32 ar_coeff_shift,
					    s32 grain_min,
					    s32 grain_max,
					    u16 random_seed);

void rockchip_av1_generate_chroma_grain_block(s32 (*luma_grain_block)[73][82],
					      s32 (*cb_grain_block)[38][44],
					      s32 (*cr_grain_block)[38][44],
					      s32 bitdepth,
					      u8 num_y_points,
					      u8 num_cb_points,
					      u8 num_cr_points,
					      s32 grain_scale_shift,
					      s32 ar_coeff_lag,
					      s32 (*ar_coeffs_cb)[25],
					      s32 (*ar_coeffs_cr)[25],
					      s32 ar_coeff_shift,
					      s32 grain_min,
					      s32 grain_max,
					      u8 chroma_scaling_from_luma,
					      u16 random_seed);

#endif
