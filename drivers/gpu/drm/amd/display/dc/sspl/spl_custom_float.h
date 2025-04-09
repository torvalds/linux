/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef SPL_CUSTOM_FLOAT_H_
#define SPL_CUSTOM_FLOAT_H_

#include "spl_os_types.h"
#include "spl_fixpt31_32.h"

struct spl_custom_float_format {
	uint32_t mantissa_bits;
	uint32_t exponenta_bits;
	bool sign;
};

struct spl_custom_float_value {
	uint32_t mantissa;
	uint32_t exponenta;
	uint32_t value;
	bool negative;
};

bool spl_convert_to_custom_float_format(
	struct spl_fixed31_32 value,
	const struct spl_custom_float_format *format,
	uint32_t *result);

#endif //SPL_CUSTOM_FLOAT_H_
