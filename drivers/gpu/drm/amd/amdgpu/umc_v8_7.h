/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#ifndef __UMC_V8_7_H__
#define __UMC_V8_7_H__

#include "soc15_common.h"
#include "amdgpu.h"

/* HBM  Memory Channel Width */
#define UMC_V8_7_HBM_MEMORY_CHANNEL_WIDTH	128
/* number of umc channel instance with memory map register access */
#define UMC_V8_7_CHANNEL_INSTANCE_NUM		2
/* number of umc instance with memory map register access */
#define UMC_V8_7_UMC_INSTANCE_NUM		8
/* total channel instances in one umc block */
#define UMC_V8_7_TOTAL_CHANNEL_NUM	(UMC_V8_7_CHANNEL_INSTANCE_NUM * UMC_V8_7_UMC_INSTANCE_NUM)
/* UMC regiser per channel offset */
#define UMC_V8_7_PER_CHANNEL_OFFSET_SIENNA	0x400

/* EccErrCnt max value */
#define UMC_V8_7_CE_CNT_MAX		0xffff
/* umc ce interrupt threshold */
#define UMC_V8_7_CE_INT_THRESHOLD	0xffff
/* umc ce count initial value */
#define UMC_V8_7_CE_CNT_INIT	(UMC_V8_7_CE_CNT_MAX - UMC_V8_7_CE_INT_THRESHOLD)

extern const struct amdgpu_umc_funcs umc_v8_7_funcs;
extern const uint32_t
	umc_v8_7_channel_idx_tbl[UMC_V8_7_UMC_INSTANCE_NUM][UMC_V8_7_CHANNEL_INSTANCE_NUM];

#endif
