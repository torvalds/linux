/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#ifndef __UMC_V8_10_H__
#define __UMC_V8_10_H__

#include "soc15_common.h"
#include "amdgpu.h"

/* number of umc channel instance with memory map register access */
#define UMC_V8_10_CHANNEL_INSTANCE_NUM		2
/* number of umc instance with memory map register access */
#define UMC_V8_10_UMC_INSTANCE_NUM		2

/* Total channel instances for all umc nodes */
#define UMC_V8_10_TOTAL_CHANNEL_NUM(adev) \
	(UMC_V8_10_CHANNEL_INSTANCE_NUM * UMC_V8_10_UMC_INSTANCE_NUM * (adev)->umc.node_inst_num)

/* UMC regiser per channel offset */
#define UMC_V8_10_PER_CHANNEL_OFFSET	0x400

/* EccErrCnt max value */
#define UMC_V8_10_CE_CNT_MAX		0xffff
/* umc ce interrupt threshold */
#define UUMC_V8_10_CE_INT_THRESHOLD	0xffff
/* umc ce count initial value */
#define UMC_V8_10_CE_CNT_INIT	(UMC_V8_10_CE_CNT_MAX - UUMC_V8_10_CE_INT_THRESHOLD)

#define UMC_V8_10_NA_COL_2BITS_POWER_OF_2_NUM	 4

/* The C5 bit in NA  address */
#define UMC_V8_10_NA_C5_BIT	14

/* Map to swizzle mode address */
#define SWIZZLE_MODE_TMP_ADDR(na, ch_num, ch_idx) \
		((((na) >> 10) * (ch_num) + (ch_idx)) << 10)
#define SWIZZLE_MODE_ADDR_HI(addr, col_bit)  \
		(((addr) >> ((col_bit) + 2)) << ((col_bit) + 2))
#define SWIZZLE_MODE_ADDR_MID(na, col_bit) ((((na) >> 8) & 0x3) << (col_bit))
#define SWIZZLE_MODE_ADDR_LOW(addr, col_bit) \
		((((addr) >> 10) & ((0x1ULL << (col_bit - 8)) - 1)) << 8)
#define SWIZZLE_MODE_ADDR_LSB(na) ((na) & 0xFF)

extern struct amdgpu_umc_ras umc_v8_10_ras;
extern const uint32_t
	umc_v8_10_channel_idx_tbl[]
				[UMC_V8_10_UMC_INSTANCE_NUM]
				[UMC_V8_10_CHANNEL_INSTANCE_NUM];

#endif

