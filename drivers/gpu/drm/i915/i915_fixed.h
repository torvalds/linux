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
	uint_fixed_16_16_t fp = { .val = val << 16 };

	WARN_ON(val > U16_MAX);

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
	uint_fixed_16_16_t min = { .val = min(min1.val, min2.val) };

	return min;
}

static inline uint_fixed_16_16_t max_fixed16(uint_fixed_16_16_t max1,
					     uint_fixed_16_16_t max2)
{
	uint_fixed_16_16_t max = { .val = max(max1.val, max2.val) };

	return max;
}

static inline uint_fixed_16_16_t clamp_u64_to_fixed16(u64 val)
{
	uint_fixed_16_16_t fp = { .val = (u32)val };

	WARN_ON(val > U32_MAX);

	return fp;
}

static inline u32 div_round_up_fixed16(uint_fixed_16_16_t val,
				       uint_fixed_16_16_t d)
{
	return DIV_ROUND_UP(val.val, d.val);
}

static inline u32 mul_round_up_u32_fixed16(u32 val, uint_fixed_16_16_t mul)
{
	u64 tmp;

	tmp = (u64)val * mul.val;
	tmp = DIV_ROUND_UP_ULL(tmp, 1 << 16);
	WARN_ON(tmp > U32_MAX);

	return (u32)tmp;
}

static inline uint_fixed_16_16_t mul_fixed16(uint_fixed_16_16_t val,
					     uint_fixed_16_16_t mul)
{
	u64 tmp;

	tmp = (u64)val.val * mul.val;
	tmp = tmp >> 16;

	return clamp_u64_to_fixed16(tmp);
}

static inline uint_fixed_16_16_t div_fixed16(u32 val, u32 d)
{
	u64 tmp;

	tmp = (u64)val << 16;
	tmp = DIV_ROUND_UP_ULL(tmp, d);

	return clamp_u64_to_fixed16(tmp);
}

static inline u32 div_round_up_u32_fixed16(u32 val, uint_fixed_16_16_t d)
{
	u64 tmp;

	tmp = (u64)val << 16;
	tmp = DIV_ROUND_UP_ULL(tmp, d.val);
	WARN_ON(tmp > U32_MAX);

	return (u32)tmp;
}

static inline uint_fixed_16_16_t mul_u32_fixed16(u32 val, uint_fixed_16_16_t mul)
{
	u64 tmp;

	tmp = (u64)val * mul.val;

	return clamp_u64_to_fixed16(tmp);
}

static inline uint_fixed_16_16_t add_fixed16(uint_fixed_16_16_t add1,
					     uint_fixed_16_16_t add2)
{
	u64 tmp;

	tmp = (u64)add1.val + add2.val;

	return clamp_u64_to_fixed16(tmp);
}

static inline uint_fixed_16_16_t add_fixed16_u32(uint_fixed_16_16_t add1,
						 u32 add2)
{
	uint_fixed_16_16_t tmp_add2 = u32_to_fixed16(add2);
	u64 tmp;

	tmp = (u64)add1.val + tmp_add2.val;

	return clamp_u64_to_fixed16(tmp);
}

#endif /* _I915_FIXED_H_ */
