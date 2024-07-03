// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_SPL_ISHARP_FILTERS_H__
#define __DC_SPL_ISHARP_FILTERS_H__

#include "dc_spl_types.h"

const uint32_t *spl_get_filter_isharp_1D_lut_0(void);
const uint32_t *spl_get_filter_isharp_1D_lut_0p5x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_1p0x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_1p5x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_2p0x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_3p0x(void);
uint16_t *spl_get_filter_isharp_bs_4tap_in_6_64p(void);
uint16_t *spl_get_filter_isharp_bs_4tap_64p(void);
uint16_t *spl_get_filter_isharp_bs_3tap_64p(void);
const uint16_t *spl_get_filter_isharp_wide_6tap_64p(void);
uint16_t *spl_dscl_get_blur_scale_coeffs_64p(int taps);

struct scale_ratio_to_sharpness_level_lookup {
	unsigned int ratio_numer;
	unsigned int ratio_denom;
	unsigned int sharpness_numer;
	unsigned int sharpness_denom;
};

struct sharpness_level_mapping {
	unsigned int level;
	unsigned int level_numer;
	unsigned int level_denom;
};

enum system_setup {
	SDR_NL = 0,
	SDR_L,
	HDR_NL,
	HDR_L
};

void spl_init_blur_scale_coeffs(void);
void spl_set_blur_scale_data(struct dscl_prog_data *dscl_prog_data,
	const struct spl_scaler_data *data);

void spl_build_isharp_1dlut_from_reference_curve(struct spl_fixed31_32 ratio, enum system_setup setup);
uint32_t *spl_get_pregen_filter_isharp_1D_lut(enum explicit_sharpness sharpness);
#endif /* __DC_SPL_ISHARP_FILTERS_H__ */
