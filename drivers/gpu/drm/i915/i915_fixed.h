/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2018 Intel Corporation
 */

#ifndef _I915_FIXED_H_
#define _I915_FIXED_H_

typedef struct {
	uint32_t val;
} uint_fixed_16_16_t;

#define FP_16_16_MAX ({ \
	uint_fixed_16_16_t fp; \
	fp.val = UINT_MAX; \
	fp; \
})

static inline bool is_fixed16_zero(uint_fixed_16_16_t val)
{
	if (val.val == 0)
		return true;
	return false;
}

static inline uint_fixed_16_16_t u32_to_fixed16(uint32_t val)
{
	uint_fixed_16_16_t fp;

	WARN_ON(val > U16_MAX);

	fp.val = val << 16;
	return fp;
}

static inline uint32_t fixed16_to_u32_round_up(uint_fixed_16_16_t fp)
{
	return DIV_ROUND_UP(fp.val, 1 << 16);
}

static inline uint32_t fixed16_to_u32(uint_fixed_16_16_t fp)
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

static inline uint_fixed_16_16_t clamp_u64_to_fixed16(uint64_t val)
{
	uint_fixed_16_16_t fp;
	WARN_ON(val > U32_MAX);
	fp.val = (uint32_t) val;
	return fp;
}

static inline uint32_t div_round_up_fixed16(uint_fixed_16_16_t val,
					    uint_fixed_16_16_t d)
{
	return DIV_ROUND_UP(val.val, d.val);
}

static inline uint32_t mul_round_up_u32_fixed16(uint32_t val,
						uint_fixed_16_16_t mul)
{
	uint64_t intermediate_val;

	intermediate_val = (uint64_t) val * mul.val;
	intermediate_val = DIV_ROUND_UP_ULL(intermediate_val, 1 << 16);
	WARN_ON(intermediate_val > U32_MAX);
	return (uint32_t) intermediate_val;
}

static inline uint_fixed_16_16_t mul_fixed16(uint_fixed_16_16_t val,
					     uint_fixed_16_16_t mul)
{
	uint64_t intermediate_val;

	intermediate_val = (uint64_t) val.val * mul.val;
	intermediate_val = intermediate_val >> 16;
	return clamp_u64_to_fixed16(intermediate_val);
}

static inline uint_fixed_16_16_t div_fixed16(uint32_t val, uint32_t d)
{
	uint64_t interm_val;

	interm_val = (uint64_t)val << 16;
	interm_val = DIV_ROUND_UP_ULL(interm_val, d);
	return clamp_u64_to_fixed16(interm_val);
}

static inline uint32_t div_round_up_u32_fixed16(uint32_t val,
						uint_fixed_16_16_t d)
{
	uint64_t interm_val;

	interm_val = (uint64_t)val << 16;
	interm_val = DIV_ROUND_UP_ULL(interm_val, d.val);
	WARN_ON(interm_val > U32_MAX);
	return (uint32_t) interm_val;
}

static inline uint_fixed_16_16_t mul_u32_fixed16(uint32_t val,
						 uint_fixed_16_16_t mul)
{
	uint64_t intermediate_val;

	intermediate_val = (uint64_t) val * mul.val;
	return clamp_u64_to_fixed16(intermediate_val);
}

static inline uint_fixed_16_16_t add_fixed16(uint_fixed_16_16_t add1,
					     uint_fixed_16_16_t add2)
{
	uint64_t interm_sum;

	interm_sum = (uint64_t) add1.val + add2.val;
	return clamp_u64_to_fixed16(interm_sum);
}

static inline uint_fixed_16_16_t add_fixed16_u32(uint_fixed_16_16_t add1,
						 uint32_t add2)
{
	uint64_t interm_sum;
	uint_fixed_16_16_t interm_add2 = u32_to_fixed16(add2);

	interm_sum = (uint64_t) add1.val + interm_add2.val;
	return clamp_u64_to_fixed16(interm_sum);
}

#endif /* _I915_FIXED_H_ */
