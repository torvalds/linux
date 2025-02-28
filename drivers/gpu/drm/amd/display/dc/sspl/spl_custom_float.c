// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "spl_debug.h"
#include "spl_custom_float.h"

static bool spl_build_custom_float(struct spl_fixed31_32 value,
			       const struct spl_custom_float_format *format,
			       bool *negative,
			       uint32_t *mantissa,
			       uint32_t *exponenta)
{
	uint32_t exp_offset = (1 << (format->exponenta_bits - 1)) - 1;

	const struct spl_fixed31_32 mantissa_constant_plus_max_fraction =
		spl_fixpt_from_fraction((1LL << (format->mantissa_bits + 1)) - 1,
				       1LL << format->mantissa_bits);

	struct spl_fixed31_32 mantiss;

	if (spl_fixpt_eq(value, spl_fixpt_zero)) {
		*negative = false;
		*mantissa = 0;
		*exponenta = 0;
		return true;
	}

	if (spl_fixpt_lt(value, spl_fixpt_zero)) {
		*negative = format->sign;
		value = spl_fixpt_neg(value);
	} else {
		*negative = false;
	}

	if (spl_fixpt_lt(value, spl_fixpt_one)) {
		uint32_t i = 1;

		do {
			value = spl_fixpt_shl(value, 1);
			++i;
		} while (spl_fixpt_lt(value, spl_fixpt_one));

		--i;

		if (exp_offset <= i) {
			*mantissa = 0;
			*exponenta = 0;
			return true;
		}

		*exponenta = exp_offset - i;
	} else if (spl_fixpt_le(mantissa_constant_plus_max_fraction, value)) {
		uint32_t i = 1;

		do {
			value = spl_fixpt_shr(value, 1);
			++i;
		} while (spl_fixpt_lt(mantissa_constant_plus_max_fraction, value));

		*exponenta = exp_offset + i - 1;
	} else {
		*exponenta = exp_offset;
	}

	mantiss = spl_fixpt_sub(value, spl_fixpt_one);

	if (spl_fixpt_lt(mantiss, spl_fixpt_zero) ||
	    spl_fixpt_lt(spl_fixpt_one, mantiss))
		mantiss = spl_fixpt_zero;
	else
		mantiss = spl_fixpt_shl(mantiss, format->mantissa_bits);

	*mantissa = spl_fixpt_floor(mantiss);

	return true;
}

static bool spl_setup_custom_float(const struct spl_custom_float_format *format,
			       bool negative,
			       uint32_t mantissa,
			       uint32_t exponenta,
			       uint32_t *result)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t value = 0;

	/* verification code:
	 * once calculation is ok we can remove it
	 */

	const uint32_t mantissa_mask =
		(1 << (format->mantissa_bits + 1)) - 1;

	const uint32_t exponenta_mask =
		(1 << (format->exponenta_bits + 1)) - 1;

	if (mantissa & ~mantissa_mask) {
		SPL_BREAK_TO_DEBUGGER();
		mantissa = mantissa_mask;
	}

	if (exponenta & ~exponenta_mask) {
		SPL_BREAK_TO_DEBUGGER();
		exponenta = exponenta_mask;
	}

	/* end of verification code */

	while (i < format->mantissa_bits) {
		uint32_t mask = 1 << i;

		if (mantissa & mask)
			value |= mask;

		++i;
	}

	while (j < format->exponenta_bits) {
		uint32_t mask = 1 << j;

		if (exponenta & mask)
			value |= mask << i;

		++j;
	}

	if (negative && format->sign)
		value |= 1 << (i + j);

	*result = value;

	return true;
}

bool spl_convert_to_custom_float_format(struct spl_fixed31_32 value,
				    const struct spl_custom_float_format *format,
				    uint32_t *result)
{
	uint32_t mantissa;
	uint32_t exponenta;
	bool negative;

	return spl_build_custom_float(value, format, &negative, &mantissa, &exponenta) &&
				  spl_setup_custom_float(format,
						     negative,
						     mantissa,
						     exponenta,
						     result);
}
