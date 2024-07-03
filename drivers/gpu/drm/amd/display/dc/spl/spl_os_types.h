/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
 * Copyright 2019 Raptor Engineering, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef _SPL_OS_TYPES_H_
#define _SPL_OS_TYPES_H_

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
// TODO: need backport
#define SPL_BREAK_TO_DEBUGGER() ASSERT(0)

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
