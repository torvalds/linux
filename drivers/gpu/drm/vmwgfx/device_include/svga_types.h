/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 * Copyright 2015 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/
#ifndef _VM_BASIC_TYPES_H_
#define _VM_BASIC_TYPES_H_
#include <linux/kernel.h>

typedef u32 uint32;
typedef s32 int32;
typedef u64 uint64;
typedef u16 uint16;
typedef s16 int16;
typedef u8  uint8;
typedef s8  int8;

typedef uint64 PA;
typedef uint32 PPN;
typedef uint64 PPN64;

typedef bool Bool;

#define MAX_UINT64 U64_MAX
#define MAX_UINT32 U32_MAX
#define MAX_UINT16 U16_MAX

#define CONST64U(x) x##ULL

#endif
