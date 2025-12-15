/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 *
 * Helper macros for working with log2 values
 *
 */
#ifndef __GENERIC_PT_LOG2_H
#define __GENERIC_PT_LOG2_H
#include <linux/bitops.h>
#include <linux/limits.h>

/* Compute a */
#define log2_to_int_t(type, a_lg2) ((type)(((type)1) << (a_lg2)))
static_assert(log2_to_int_t(unsigned int, 0) == 1);

/* Compute a - 1 (aka all low bits set) */
#define log2_to_max_int_t(type, a_lg2) ((type)(log2_to_int_t(type, a_lg2) - 1))

/* Compute a / b */
#define log2_div_t(type, a, b_lg2) ((type)(((type)a) >> (b_lg2)))
static_assert(log2_div_t(unsigned int, 4, 2) == 1);

/*
 * Compute:
 *   a / c == b / c
 * aka the high bits are equal
 */
#define log2_div_eq_t(type, a, b, c_lg2) \
	(log2_div_t(type, (a) ^ (b), c_lg2) == 0)
static_assert(log2_div_eq_t(unsigned int, 1, 1, 2));

/* Compute a % b */
#define log2_mod_t(type, a, b_lg2) \
	((type)(((type)a) & log2_to_max_int_t(type, b_lg2)))
static_assert(log2_mod_t(unsigned int, 1, 2) == 1);

/*
 * Compute:
 *   a % b == b - 1
 * aka the low bits are all 1s
 */
#define log2_mod_eq_max_t(type, a, b_lg2) \
	(log2_mod_t(type, a, b_lg2) == log2_to_max_int_t(type, b_lg2))
static_assert(log2_mod_eq_max_t(unsigned int, 3, 2));

/*
 * Return a value such that:
 *    a / b == ret / b
 *    ret % b == val
 * aka set the low bits to val. val must be < b
 */
#define log2_set_mod_t(type, a, val, b_lg2) \
	((((type)(a)) & (~log2_to_max_int_t(type, b_lg2))) | ((type)(val)))
static_assert(log2_set_mod_t(unsigned int, 3, 1, 2) == 1);

/* Return a value such that:
 *    a / b == ret / b
 *    ret % b == b - 1
 * aka set the low bits to all 1s
 */
#define log2_set_mod_max_t(type, a, b_lg2) \
	(((type)(a)) | log2_to_max_int_t(type, b_lg2))
static_assert(log2_set_mod_max_t(unsigned int, 2, 2) == 3);

/* Compute a * b */
#define log2_mul_t(type, a, b_lg2) ((type)(((type)a) << (b_lg2)))
static_assert(log2_mul_t(unsigned int, 2, 2) == 8);

#define _dispatch_sz(type, fn, a) \
	(sizeof(type) == 4 ? fn##32((u32)a) : fn##64(a))

/*
 * Return the highest value such that:
 *    fls_t(u32, 0) == 0
 *    fls_t(u3, 1) == 1
 *    a >= log2_to_int(ret - 1)
 * aka find last set bit
 */
static inline unsigned int fls32(u32 a)
{
	return fls(a);
}
#define fls_t(type, a) _dispatch_sz(type, fls, a)

/*
 * Return the highest value such that:
 *    ffs_t(u32, 0) == UNDEFINED
 *    ffs_t(u32, 1) == 0
 *    log_mod(a, ret) == 0
 * aka find first set bit
 */
static inline unsigned int __ffs32(u32 a)
{
	return __ffs(a);
}
#define ffs_t(type, a) _dispatch_sz(type, __ffs, a)

/*
 * Return the highest value such that:
 *    ffz_t(u32, U32_MAX) == UNDEFINED
 *    ffz_t(u32, 0) == 0
 *    ffz_t(u32, 1) == 1
 *    log_mod(a, ret) == log_to_max_int(ret)
 * aka find first zero bit
 */
static inline unsigned int ffz32(u32 a)
{
	return ffz(a);
}
static inline unsigned int ffz64(u64 a)
{
	if (sizeof(u64) == sizeof(unsigned long))
		return ffz(a);

	if ((u32)a == U32_MAX)
		return ffz32(a >> 32) + 32;
	return ffz32(a);
}
#define ffz_t(type, a) _dispatch_sz(type, ffz, a)

#endif
