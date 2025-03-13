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
/* C2, C3, C4, R13, four bits in MCA address are looped in retirement */
#define UMC_V12_0_RETIRE_LOOP_BITS 4

/* column bits in SOC physical address */
#define UMC_V12_0_PA_C2_BIT 15
#define UMC_V12_0_PA_C3_BIT 16
#define UMC_V12_0_PA_C4_BIT 21
/* row bits in SOC physical address */
#define UMC_V12_0_PA_R0_BIT 22
#define UMC_V12_0_PA_R11_BIT 33
#define UMC_V12_0_PA_R13_BIT 35
/* channel bit in SOC physical address */
#define UMC_V12_0_PA_CH4_BIT 12
#define UMC_V12_0_PA_CH5_BIT 13
/* bank bit in SOC physical address */
#define UMC_V12_0_PA_B0_BIT 19
/* row bits in MCA address */
#define UMC_V12_0_MA_R0_BIT 10

#define MCA_UMC_HWID_V12_0     0x96
#define MCA_UMC_MCATYPE_V12_0  0x0

#define MCA_IPID_LO_2_UMC_CH(_ipid_lo) (((((_ipid_lo) >> 20) & 0x1) * 4) + \
			(((_ipid_lo) >> 12) & 0xF))
#define MCA_IPID_LO_2_UMC_INST(_ipid_lo) (((_ipid_lo) >> 21) & 0x7)

#define MCA_IPID_2_DIE_ID(ipid)  ((REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdHi) >> 2) & 0x03)

#define MCA_IPID_2_UMC_CH(ipid) \
	(MCA_IPID_LO_2_UMC_CH(REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo)))

#define MCA_IPID_2_UMC_INST(ipid) \
	(MCA_IPID_LO_2_UMC_INST(REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo)))

#define MCA_IPID_2_SOCKET_ID(ipid) \
	(((REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo) & 0x1) << 2) | \
	 (REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdHi) & 0x03))

bool umc_v12_0_is_deferred_error(struct amdgpu_device *adev, uint64_t mc_umc_status);
bool umc_v12_0_is_uncorrectable_error(struct amdgpu_device *adev, uint64_t mc_umc_status);
bool umc_v12_0_is_correctable_error(struct amdgpu_device *adev, uint64_t mc_umc_status);

typedef bool (*check_error_type_func)(struct amdgpu_device *adev, uint64_t mc_umc_status);

extern struct amdgpu_umc_ras umc_v12_0_ras;

#endif
