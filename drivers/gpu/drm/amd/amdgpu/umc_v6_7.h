/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#ifndef __UMC_V6_7_H__
#define __UMC_V6_7_H__

#include "soc15_common.h"
#include "amdgpu.h"

/* EccErrCnt max value */
#define UMC_V6_7_CE_CNT_MAX		0xffff
/* umc ce interrupt threshold */
#define UMC_V6_7_CE_INT_THRESHOLD	0xffff
/* umc ce count initial value */
#define UMC_V6_7_CE_CNT_INIT	(UMC_V6_7_CE_CNT_MAX - UMC_V6_7_CE_INT_THRESHOLD)

#define UMC_V6_7_INST_DIST	0x40000

/* number of umc channel instance with memory map register access */
#define UMC_V6_7_UMC_INSTANCE_NUM		4
/* number of umc instance with memory map register access */
#define UMC_V6_7_CHANNEL_INSTANCE_NUM		8
/* total channel instances in one umc block */
#define UMC_V6_7_TOTAL_CHANNEL_NUM	(UMC_V6_7_CHANNEL_INSTANCE_NUM * UMC_V6_7_UMC_INSTANCE_NUM)
/* UMC regiser per channel offset */
#define UMC_V6_7_PER_CHANNEL_OFFSET		0x400
extern const struct amdgpu_umc_ras_funcs umc_v6_7_ras_funcs;
extern const uint32_t
	umc_v6_7_channel_idx_tbl_second[UMC_V6_7_UMC_INSTANCE_NUM][UMC_V6_7_CHANNEL_INSTANCE_NUM];
extern const uint32_t
	umc_v6_7_channel_idx_tbl_first[UMC_V6_7_UMC_INSTANCE_NUM][UMC_V6_7_CHANNEL_INSTANCE_NUM];

#endif
