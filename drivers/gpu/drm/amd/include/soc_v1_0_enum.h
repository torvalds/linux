/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
 */
#ifndef __SOC_V1_0_ENUM_H__
#define __SOC_V1_0_ENUM_H__

typedef enum MTYPE {
	MTYPE_NC			= 0x00000000,
	MTYPE_RESERVED_1		= 0x00000001,
	MTYPE_RW			= 0x00000002,
	MTYPE_UC			= 0x00000003,
} MTYPE;

typedef enum SH_MEM_ALIGNMENT_MODE {
	SH_MEM_ALIGNMENT_MODE_DWORD              = 0x00000000,
	SH_MEM_ALIGNMENT_MODE_UNALIGNED          = 0x00000001,
} SH_MEM_ALIGNMENT_MODE;

#endif
