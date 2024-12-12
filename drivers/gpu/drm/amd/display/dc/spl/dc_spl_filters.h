/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef __DC_SPL_FILTERS_H__
#define __DC_SPL_FILTERS_H__

#include "dc_spl_types.h"

#define NUM_PHASES_COEFF 33

void convert_filter_s1_10_to_s1_12(const uint16_t *s1_10_filter,
	uint16_t *s1_12_filter, int num_taps);

#endif /* __DC_SPL_FILTERS_H__ */
