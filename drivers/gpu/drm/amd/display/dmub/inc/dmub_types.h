/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef _DMUB_TYPES_H_
#define _DMUB_TYPES_H_

/* Basic type definitions. */
#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <stdarg.h>

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef dmub_memcpy
#define dmub_memcpy(dest, source, bytes) memcpy((dest), (source), (bytes))
#endif

#ifndef dmub_memset
#define dmub_memset(dest, val, bytes) memset((dest), (val), (bytes))
#endif

#ifndef dmub_udelay
#define dmub_udelay(microseconds) udelay(microseconds)
#endif

union dmub_addr {
	struct {
		uint32_t low_part;
		uint32_t high_part;
	} u;
	uint64_t quad_part;
};

#if defined(__cplusplus)
}
#endif

#endif /* _DMUB_TYPES_H_ */
