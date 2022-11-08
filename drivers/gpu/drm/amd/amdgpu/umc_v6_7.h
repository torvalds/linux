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
/* one piece of normalizing address is mapped to 8 pieces of physical address */
#define UMC_V6_7_NA_MAP_PA_NUM	8
/* R14 bit shift should be considered, double the number */
#define UMC_V6_7_BAD_PAGE_NUM_PER_CHANNEL	(UMC_V6_7_NA_MAP_PA_NUM * 2)
/* The CH4 bit in SOC physical address */
#define UMC_V6_7_PA_CH4_BIT	12
/* The C2 bit in SOC physical address */
#define UMC_V6_7_PA_C2_BIT	17
/* The R14 bit in SOC physical address */
#define UMC_V6_7_PA_R14_BIT	34
/* UMC regiser per channel offset */
#define UMC_V6_7_PER_CHANNEL_OFFSET		0x400

/* XOR bit 20, 25, 34 of PA into CH4 bit (bit 12 of PA),
 * hash bit is only effective when related setting is enabled
 */
#define CHANNEL_HASH(channel_idx, pa) (((channel_idx) >> 4) ^ \
			(((pa)  >> 20) & 0x1ULL & adev->df.hash_status.hash_64k) ^ \
			(((pa)  >> 25) & 0x1ULL & adev->df.hash_status.hash_2m) ^ \
			(((pa)  >> 34) & 0x1ULL & adev->df.hash_status.hash_1g))
#define SET_CHANNEL_HASH(channel_idx, pa) do { \
		(pa) &= ~(0x1ULL << UMC_V6_7_PA_CH4_BIT); \
		(pa) |= (CHANNEL_HASH(channel_idx, pa) << UMC_V6_7_PA_CH4_BIT); \
	} while (0)

extern struct amdgpu_umc_ras umc_v6_7_ras;
extern const uint32_t
	umc_v6_7_channel_idx_tbl_second[UMC_V6_7_UMC_INSTANCE_NUM][UMC_V6_7_CHANNEL_INSTANCE_NUM];
extern const uint32_t
	umc_v6_7_channel_idx_tbl_first[UMC_V6_7_UMC_INSTANCE_NUM][UMC_V6_7_CHANNEL_INSTANCE_NUM];
void umc_v6_7_convert_error_address(struct amdgpu_device *adev,
                                    struct ras_err_data *err_data, uint64_t err_addr,
                                    uint32_t ch_inst, uint32_t umc_inst);
#endif
