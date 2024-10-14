// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dc_spl_filters.h"

void convert_filter_s1_10_to_s1_12(const uint16_t *s1_10_filter,
	uint16_t *s1_12_filter, int num_taps)
{
	int num_entries = NUM_PHASES_COEFF * num_taps;
	int i;

	for (i = 0; i < num_entries; i++)
		*(s1_12_filter + i) = *(s1_10_filter + i) * 4;
}
