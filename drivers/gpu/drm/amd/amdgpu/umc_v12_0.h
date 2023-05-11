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
 */
#ifndef __UMC_V12_0_H__
#define __UMC_V12_0_H__

#include "soc15_common.h"
#include "amdgpu.h"

#define UMC_V12_0_NODE_DIST		0x40000000
#define UMC_V12_0_INST_DIST		0x40000

/* UMC register per channel offset */
#define UMC_V12_0_PER_CHANNEL_OFFSET	0x400

/* UMC cross node offset */
#define UMC_V12_0_CROSS_NODE_OFFSET		0x100000000

/* OdEccErrCnt max value */
#define UMC_V12_0_CE_CNT_MAX		0xffff
/* umc ce interrupt threshold */
#define UMC_V12_0_CE_INT_THRESHOLD	0xffff
/* umc ce count initial value */
#define UMC_V12_0_CE_CNT_INIT	(UMC_V12_0_CE_CNT_MAX - UMC_V12_0_CE_INT_THRESHOLD)

/* number of umc channel instance with memory map register access */
#define UMC_V12_0_CHANNEL_INSTANCE_NUM		8
/* number of umc instance with memory map register access */
#define UMC_V12_0_UMC_INSTANCE_NUM		4

/* Total channel instances for all available umc nodes */
#define UMC_V12_0_TOTAL_CHANNEL_NUM(adev) \
	(UMC_V12_0_CHANNEL_INSTANCE_NUM * (adev)->gmc.num_umc)

extern struct amdgpu_umc_ras umc_v12_0_ras;

#endif
