/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#ifndef NI_H
#define NI_H

#define MC_SHARED_BLACKOUT_CNTL           		0x20ac
#define MC_SEQ_SUP_CNTL           			0x28c8
#define		RUN_MASK      				(1 << 0)
#define MC_SEQ_SUP_PGM           			0x28cc
#define MC_IO_PAD_CNTL_D0           			0x29d0
#define		MEM_FALL_OUT_CMD      			(1 << 8)
#define MC_SEQ_MISC0           				0x2a00
#define		MC_SEQ_MISC0_GDDR5_SHIFT      		28
#define		MC_SEQ_MISC0_GDDR5_MASK      		0xf0000000
#define		MC_SEQ_MISC0_GDDR5_VALUE      		5
#define MC_SEQ_IO_DEBUG_INDEX           		0x2a44
#define MC_SEQ_IO_DEBUG_DATA           			0x2a48

#endif

