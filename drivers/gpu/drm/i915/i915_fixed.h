/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2018 Intel Corporation
 */

#ifndef _I915_FIXED_H_
#define _I915_FIXED_H_

typedef struct {
	u32 val;
} uint_fixed_16_16_t;

#define FP_16_16_MAX ((uint_fixed_16_16_t){ .val = UINT_MAX })

static inline bool is_fixed16_zero(uint_fixed_16_16_t val)
{
	return val.val == 0;
}

static inline uint_fixed_16_16_t u32_to_fixed16(u32 val)
{
	uint_fixed_16_16_t fp;

	WARN_ON(val > U16_MAX);

	fp.val = val << 16;
	return fp;
}

static inline u32 fixed16_to_u32_round_up(uint_fixed_16_16_t fp)
{
	return DIV_ROUND_UP(fp.val, 1 << 16);
}

static inline u32 fixed16_to_u32(uint_fixed_16_16_t fp)
{
	return fp.val >> 16;
}

static inline uint_fixed_16_16_t min_fixed16(uint_fixed_16_16_t min1,
					     uint_fixed_16_16_t min2)
{
	uint_fixed_16_16_t min;

	min.val = min(min1.val, min2.val);
	return min;
}

static inline uint_fixed_16_16_t max_fixed16(uint_fixed_16_16_t max1,
					     uint_fixed_16_16_t max2)
{
	uint_fixed_16_16_t max;

	max.val = max(max1.val, max2.val);
	return max;
}

static inline uint_fixed_16_16_t clamp_u64_to_fixed16(u64 val)
{
	uint_fixed_16_16_t fp;
	WARN_ON(val > U32_MAX);
	fp.val = (u32)val;
	return fp;
}

static inline u32 div_round_up_fixed16(uint_fixed_16_16_t val,
				       uint_fixed_16_16_t d)
{
	return DIV_ROUND_UP(val.val, d.val);
}

static inline u32 mul_round_up_u32_fixed16(u32 val, uint_fixed_16_16_t mul)
{
	u64 intermediate_val;

	intermediate_val = (u64)val * mul.val;
	intermediate_val = DIV_ROUND_UP_ULL(intermediate_val, 1 << 16);
	WARN_ON(intermediate_val > U32_MAX);
	return (u32)intermediate_val;
}

static inline uint_fixed_16_16_t mul_fixed16(uint_fixed_16_16_t val,
					     uint_fixed_16_16_t mul)
{
	u64 intermediate_val;

	intermediate_val = (u64)val.val * mul.val;
	intermediate_val = intermediate_val >> 16;
	return clamp_u64_to_fixed16(intermediate_val);
}

static inline uint_fixed_16_16_t div_fixed16(u32 val, u32 d)
{
	u64 interm_val;

	interm_val = (u64)val << 16;
	interm_val = DIV_ROUND_UP_ULL(interm_val, d);
	return clamp_u64_to_fixed16(interm_val);
}

static inline u32 div_round_up_u32_fixed16(u32 val, uint_fixed_16_16_t d)
{
	u64 interm_val;

	interm_val = (u64)val << 16;
	interm_val = DIV_ROUND_UP_ULL(interm_val, d.val);
	WARN_ON(interm_val > U32_MAX);
	return (u32)interm_val;
}

static inline uint_fixed_16_16_t mul_u32_fixed16(u32 val, uint_fixed_16_16_t mul)
{
	u64 intermediate_val;

	intermediate_val = (u64)val * mul.val;
	return clamp_u64_to_fixed16(intermediate_val);
}

static inline uint_fixed_16_16_t add_fixed16(uint_fixed_16_16_t add1,
					     uint_fixed_16_16_t add2)
{
	u64 interm_sum;

	interm_sum = (u64)add1.val + add2.val;
	return clamp_u64_to_fixed16(interm_sum);
}

static inline uint_fixed_16_16_t add_fixed16_u32(uint_fixed_16_16_t add1,
						 u32 add2)
{
	u64 interm_sum;
	uint_fixed_16_16_t interm_add2 = u32_to_fixed16(add2);

	interm_sum = (u64)add1.val + interm_add2.val;
	return clamp_u64_to_fixed16(interm_sum);
}

#endif /* _I915_FIXED_H_ */
