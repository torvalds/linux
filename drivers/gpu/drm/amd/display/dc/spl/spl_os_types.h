/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */
/* Copyright 2019 Raptor Engineering, LLC */

#ifndef _SPL_OS_TYPES_H_
#define _SPL_OS_TYPES_H_

#include "spl_debug.h"

#include <linux/slab.h>
#include <linux/kgdb.h>
#include <linux/kref.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/mm.h>

/*
 *
 * general debug capabilities
 *
 */

static inline uint64_t spl_div_u64_rem(uint64_t dividend, uint32_t divisor, uint32_t *remainder)
{
	return div_u64_rem(dividend, divisor, remainder);
}

static inline uint64_t spl_div_u64(uint64_t dividend, uint32_t divisor)
{
	return div_u64(dividend, divisor);
}

static inline uint64_t spl_div64_u64(uint64_t dividend, uint64_t divisor)
{
	return div64_u64(dividend, divisor);
}

static inline uint64_t spl_div64_u64_rem(uint64_t dividend, uint64_t divisor, uint64_t *remainder)
{
	return div64_u64_rem(dividend, divisor, remainder);
}

static inline int64_t spl_div64_s64(int64_t dividend, int64_t divisor)
{
	return div64_s64(dividend, divisor);
}

#define spl_swap(a, b) \
	do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

#ifndef spl_min
#define spl_min(a, b)    (((a) < (b)) ? (a):(b))
#endif

#endif /* _SPL_OS_TYPES_H_ */
