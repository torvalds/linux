/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
#ifndef SI_H
#define SI_H

#define	CG_MULT_THERMAL_STATUS					0x714
#define		ASIC_MAX_TEMP(x)				((x) << 0)
#define		ASIC_MAX_TEMP_MASK				0x000001ff
#define		ASIC_MAX_TEMP_SHIFT				0
#define		CTF_TEMP(x)					((x) << 9)
#define		CTF_TEMP_MASK					0x0003fe00
#define		CTF_TEMP_SHIFT					9

#define MC_SHARED_CHMAP						0x2004
#define		NOOFCHAN_SHIFT					12
#define		NOOFCHAN_MASK					0x0000f000

#define	DC_LB_MEMORY_SPLIT					0x6b0c
#define		DC_LB_MEMORY_CONFIG(x)				((x) << 20)

#define	PRIORITY_A_CNT						0x6b18
#define		PRIORITY_MARK_MASK				0x7fff
#define		PRIORITY_OFF					(1 << 16)
#define		PRIORITY_ALWAYS_ON				(1 << 20)
#define	PRIORITY_B_CNT						0x6b1c

#define	DPG_PIPE_ARBITRATION_CONTROL3				0x6cc8
#       define LATENCY_WATERMARK_MASK(x)			((x) << 16)
#define	DPG_PIPE_LATENCY_CONTROL				0x6ccc
#       define LATENCY_LOW_WATERMARK(x)				((x) << 0)
#       define LATENCY_HIGH_WATERMARK(x)			((x) << 16)

#endif
