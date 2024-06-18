/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __CMNTYPES_H__
#define __CMNTYPES_H__

#ifdef __GNUC__
#if __GNUC__ == 4 && __GNUC_MINOR__ > 7
typedef unsigned int uint;
#endif
#endif

typedef signed char int8, *pint8;
typedef signed short int16, *pint16;
typedef signed int int32, *pint32;
typedef signed int64, *pint64;

typedef unsigned char uint8, *puint8;
typedef unsigned short uint16, *puint16;
typedef unsigned int uint32, *puint32;
typedef unsigned uint64, *puint64;

typedef unsigned long int ulong;
typedef unsigned char uchar;
typedef unsigned int uint;

typedef void *pvoid;
typedef char *pchar;
typedef const void *const_pvoid;
typedef const char *const_pchar;

typedef struct rgba_struct {
    uint8 a;
    uint8 r;
    uint8 g;
    uint8 b;
} rgba_t;

typedef struct {
    uint8 blue;
    uint8 green;
    uint8 red;
    uint8 alpha;
} gen_color_t;

typedef union {
	uint32		val;
	gen_color_t f;
} gen_color_u;

//
// Types to make it easy to get or set the bits of a float/double.
// Avoids automatic casting from int to float and back.
//
#if 0
typedef union {
	uint32 i;
	float f;
} uintfloat32;

typedef union {
	uint64 i;
	double f;
} uintfloat64;

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) x = x
#endif
#endif

#endif  //__CMNTYPES_H__
