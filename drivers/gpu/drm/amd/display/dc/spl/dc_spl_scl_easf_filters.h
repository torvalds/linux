/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef __DC_SPL_SCL_EASF_FILTERS_H__
#define __DC_SPL_SCL_EASF_FILTERS_H__

#include "dc_spl_types.h"

struct scale_ratio_to_reg_value_lookup {
	int numer;
	int denom;
	const uint32_t reg_value;
};

void spl_init_easf_filter_coeffs(void);
uint16_t *spl_get_easf_filter_3tap_64p(struct spl_fixed31_32 ratio);
uint16_t *spl_get_easf_filter_4tap_64p(struct spl_fixed31_32 ratio);
uint16_t *spl_get_easf_filter_6tap_64p(struct spl_fixed31_32 ratio);
uint16_t *spl_dscl_get_easf_filter_coeffs_64p(int taps, struct spl_fixed31_32 ratio);
void spl_set_filters_data(struct dscl_prog_data *dscl_prog_data,
	const struct spl_scaler_data *data, bool enable_easf_v,
	bool enable_easf_h);

uint32_t spl_get_v_bf3_mode(struct spl_fixed31_32 ratio);
uint32_t spl_get_h_bf3_mode(struct spl_fixed31_32 ratio);
uint32_t spl_get_reducer_gain6(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_reducer_gain4(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_gainRing6(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_gainRing4(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_3tap_dntilt_uptilt_offset(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_3tap_uptilt_maxval(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_3tap_dntilt_slope(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_3tap_uptilt1_slope(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_3tap_uptilt2_slope(int taps, struct spl_fixed31_32 ratio);
uint32_t spl_get_3tap_uptilt2_offset(int taps, struct spl_fixed31_32 ratio);

#endif /* __DC_SPL_SCL_EASF_FILTERS_H__ */
