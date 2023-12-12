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

/* one piece of normalized address is mapped to 8 pieces of physical address */
#define UMC_V12_0_NA_MAP_PA_NUM        8
/* R13 bit shift should be considered, double the number */
#define UMC_V12_0_BAD_PAGE_NUM_PER_CHANNEL (UMC_V12_0_NA_MAP_PA_NUM * 2)
/* bank bits in MCA error address */
#define UMC_V12_0_MCA_B0_BIT 6
#define UMC_V12_0_MCA_B1_BIT 7
#define UMC_V12_0_MCA_B2_BIT 8
#define UMC_V12_0_MCA_B3_BIT 9
/* column bits in SOC physical address */
#define UMC_V12_0_PA_C2_BIT 15
#define UMC_V12_0_PA_C4_BIT 21
/* row bits in SOC physical address */
#define UMC_V12_0_PA_R13_BIT 35
/* channel index bits in SOC physical address */
#define UMC_V12_0_PA_CH4_BIT 12
#define UMC_V12_0_PA_CH5_BIT 13
#define UMC_V12_0_PA_CH6_BIT 14

/* bank hash settings */
#define UMC_V12_0_XOR_EN0 1
#define UMC_V12_0_XOR_EN1 1
#define UMC_V12_0_XOR_EN2 1
#define UMC_V12_0_XOR_EN3 1
#define UMC_V12_0_COL_XOR0 0x0
#define UMC_V12_0_COL_XOR1 0x0
#define UMC_V12_0_COL_XOR2 0x800
#define UMC_V12_0_COL_XOR3 0x1000
#define UMC_V12_0_ROW_XOR0 0x11111
#define UMC_V12_0_ROW_XOR1 0x22222
#define UMC_V12_0_ROW_XOR2 0x4444
#define UMC_V12_0_ROW_XOR3 0x8888

/* channel hash settings */
#define UMC_V12_0_HASH_4K 0
#define UMC_V12_0_HASH_64K 1
#define UMC_V12_0_HASH_2M 1
#define UMC_V12_0_HASH_1G 1
#define UMC_V12_0_HASH_1T 1

/* XOR some bits of PA into CH4~CH6 bits (bits 12~14 of PA),
 * hash bit is only effective when related setting is enabled
 */
#define UMC_V12_0_CHANNEL_HASH_CH4(channel_idx, pa) ((((channel_idx) >> 5) & 0x1) ^ \
				(((pa)  >> 20) & 0x1ULL & UMC_V12_0_HASH_64K) ^ \
				(((pa)  >> 27) & 0x1ULL & UMC_V12_0_HASH_2M) ^ \
				(((pa)  >> 34) & 0x1ULL & UMC_V12_0_HASH_1G) ^ \
				(((pa)  >> 41) & 0x1ULL & UMC_V12_0_HASH_1T))
#define UMC_V12_0_CHANNEL_HASH_CH5(channel_idx, pa) ((((channel_idx) >> 6) & 0x1) ^ \
				(((pa)  >> 21) & 0x1ULL & UMC_V12_0_HASH_64K) ^ \
				(((pa)  >> 28) & 0x1ULL & UMC_V12_0_HASH_2M) ^ \
				(((pa)  >> 35) & 0x1ULL & UMC_V12_0_HASH_1G) ^ \
				(((pa)  >> 42) & 0x1ULL & UMC_V12_0_HASH_1T))
#define UMC_V12_0_CHANNEL_HASH_CH6(channel_idx, pa) ((((channel_idx) >> 4) & 0x1) ^ \
				(((pa)  >> 19) & 0x1ULL & UMC_V12_0_HASH_64K) ^ \
				(((pa)  >> 26) & 0x1ULL & UMC_V12_0_HASH_2M) ^ \
				(((pa)  >> 33) & 0x1ULL & UMC_V12_0_HASH_1G) ^ \
				(((pa)  >> 40) & 0x1ULL & UMC_V12_0_HASH_1T) ^ \
				(((pa)  >> 47) & 0x1ULL & UMC_V12_0_HASH_4K))
#define UMC_V12_0_SET_CHANNEL_HASH(channel_idx, pa) do { \
		(pa) &= ~(0x7ULL << UMC_V12_0_PA_CH4_BIT); \
		(pa) |= (UMC_V12_0_CHANNEL_HASH_CH4(channel_idx, pa) << UMC_V12_0_PA_CH4_BIT); \
		(pa) |= (UMC_V12_0_CHANNEL_HASH_CH5(channel_idx, pa) << UMC_V12_0_PA_CH5_BIT); \
		(pa) |= (UMC_V12_0_CHANNEL_HASH_CH6(channel_idx, pa) << UMC_V12_0_PA_CH6_BIT); \
	} while (0)

#define MCA_IPID_LO_2_UMC_CH(_ipid_lo) (((((_ipid_lo) >> 20) & 0x1) * 4) + \
			(((_ipid_lo) >> 12) & 0xF))
#define MCA_IPID_LO_2_UMC_INST(_ipid_lo) (((_ipid_lo) >> 21) & 0x7)

bool umc_v12_0_is_uncorrectable_error(struct amdgpu_device *adev, uint64_t mc_umc_status);
bool umc_v12_0_is_correctable_error(struct amdgpu_device *adev, uint64_t mc_umc_status);

extern const uint32_t
	umc_v12_0_channel_idx_tbl[]
			[UMC_V12_0_UMC_INSTANCE_NUM]
			[UMC_V12_0_CHANNEL_INSTANCE_NUM];

extern struct amdgpu_umc_ras umc_v12_0_ras;

#endif
