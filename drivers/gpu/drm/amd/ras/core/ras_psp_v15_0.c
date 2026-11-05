// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#include "ras.h"
#include "ta_if.h"
#include "ras_psp_v15_0.h"

#define regMPASP_SMN_C2PMSG_67                           0x0083
#define regMPASP_SMN_C2PMSG_67_BASE_IDX                  1
#define regMPASP_SMN_C2PMSG_127                          0x00bf
#define regMPASP_SMN_C2PMSG_127_BASE_IDX                 1

static struct ras_block_map   ras_block_maps_v15_0[] = {
	{RAS_BLOCK_ID__UMC,         RAS_TA_MPASP_BLOCK__UMC},
	{RAS_BLOCK_ID__SDMA,        RAS_TA_MPASP_BLOCK__SDMA},
	{RAS_BLOCK_ID__GFX,         RAS_TA_MPASP_BLOCK__GFX},
	{RAS_BLOCK_ID__MMHUB,       RAS_TA_MPASP_BLOCK__MMHUB},
	{RAS_BLOCK_ID__ATHUB,       RAS_TA_MPASP_BLOCK__ATHUB},
	{RAS_BLOCK_ID__PCIE_BIF,    RAS_TA_MPASP_BLOCK__PCIE},
	{RAS_BLOCK_ID__SMN,         RAS_TA_MPASP_BLOCK__SMN},
	{RAS_BLOCK_ID__MP0,         RAS_TA_MPASP_BLOCK__MP0},
	{RAS_BLOCK_ID__MP1,         RAS_TA_MPASP_BLOCK__MP1},
	{RAS_BLOCK_ID__VCN,         RAS_TA_MPASP_BLOCK__VCN},
	{RAS_BLOCK_ID__IH,          RAS_TA_MPASP_BLOCK__IH},
	{RAS_BLOCK_ID__MP5,         RAS_TA_MPASP_BLOCK__MP5},
	{RAS_BLOCK_ID__ATU,         RAS_TA_MPASP_BLOCK__ATU},
	{RAS_BLOCK_ID__DACC_BE,     RAS_TA_MPASP_BLOCK__DACC_BE},
	{RAS_BLOCK_ID__ECLR,        RAS_TA_MPASP_BLOCK__ECLR},
	{RAS_BLOCK_ID__KPX_SERDES,  RAS_TA_MPASP_BLOCK__KPX_SERDES},
	{RAS_BLOCK_ID__LSDMA,       RAS_TA_MPASP_BLOCK__LSDMA},
	{RAS_BLOCK_ID__MPART,       RAS_TA_MPASP_BLOCK__MPART},
	{RAS_BLOCK_ID__MPIFOE,      RAS_TA_MPASP_BLOCK__MPIFOE},
	{RAS_BLOCK_ID__MPRAS,       RAS_TA_MPASP_BLOCK__MPRAS},
	{RAS_BLOCK_ID__NBIF,        RAS_TA_MPASP_BLOCK__NBIF},
	{RAS_BLOCK_ID__NBIO,        RAS_TA_MPASP_BLOCK__NBIO},
	{RAS_BLOCK_ID__OXRP,        RAS_TA_MPASP_BLOCK__OXRP},
	{RAS_BLOCK_ID__PCIE_PL,     RAS_TA_MPASP_BLOCK__PCIE_PL},
	{RAS_BLOCK_ID__PCS_XGMI,    RAS_TA_MPASP_BLOCK__PCS_XGMI},
	{RAS_BLOCK_ID__PIE,         RAS_TA_MPASP_BLOCK__PIE},
	{RAS_BLOCK_ID__CS,          RAS_TA_MPASP_BLOCK__CS},
	{RAS_BLOCK_ID__SHUB,        RAS_TA_MPASP_BLOCK__SHUB},
	{RAS_BLOCK_ID__SSBDCI,      RAS_TA_MPASP_BLOCK__SSBDCI},
	{RAS_BLOCK_ID__UCIE_PCS,    RAS_TA_MPASP_BLOCK__UCIE_PCS},
};

static uint32_t ras_psp_v15_0_ring_wptr_get(struct ras_core_context *ras_core)
{
	return RAS_DEV_RREG32_SOC15(ras_core->dev, MP0, 0, regMPASP_SMN_C2PMSG_67);
}

static int ras_psp_v15_0_ring_wptr_set(struct ras_core_context *ras_core, uint32_t value)
{
	RAS_DEV_WREG32_SOC15(ras_core->dev, MP0, 0, regMPASP_SMN_C2PMSG_67, value);

	return 0;
}

static int ras_psp_v15_0_get_ras_block_maps(struct ras_core_context *ras_core,
			struct ras_block_map **blk_maps, uint32_t *maps_size)
{
	if (!blk_maps || !maps_size)
		return -EINVAL;

	*blk_maps = ras_block_maps_v15_0;
	*maps_size = ARRAY_SIZE(ras_block_maps_v15_0);

	return 0;
}

static int ras_psp_v15_0_get_ras_hw_caps(struct ras_core_context *ras_core,
				struct ras_hw_caps *ras_cap)
{
	uint32_t ras_hw_cap, bit;
	uint32_t i;

	if (!ras_cap)
		return -EINVAL;

	ras_hw_cap = RAS_DEV_RREG32_SOC15(ras_core->dev,
				MP0, 0, regMPASP_SMN_C2PMSG_127);

	ras_cap->poison_supported = (ras_hw_cap & BIT_ULL(31)) ? true : false;
	ras_cap->flex_mca_enabled = (ras_hw_cap & BIT_ULL(30)) ? true : false;

	ras_cap->features.value = 0;
	for (bit = 0; bit <= 29; bit++) {
		if (!(ras_hw_cap & BIT_ULL(bit)))
			continue;

		for (i = 0; i < ARRAY_SIZE(ras_block_maps_v15_0); i++) {
			if (ras_block_maps_v15_0[i].ta_id == bit) {
				if (ras_block_maps_v15_0[i].ras_id < MAX_SUPPORTED_RAS_BLOCK_ID)
					ras_cap->features.block_mask |=
						BIT_ULL(ras_block_maps_v15_0[i].ras_id);
				else
					RAS_DEV_WARN(ras_core->dev,
						"RAS block maps v15 error: invalid ras block id %u\n",
						ras_block_maps_v15_0[i].ras_id);
				break;
			}
		}
	}

	return 0;
}

const struct ras_psp_ip_func ras_psp_v15_0 = {
	.psp_ras_ring_wptr_get = ras_psp_v15_0_ring_wptr_get,
	.psp_ras_ring_wptr_set = ras_psp_v15_0_ring_wptr_set,
	.get_ras_block_maps = ras_psp_v15_0_get_ras_block_maps,
	.get_ras_hw_caps = ras_psp_v15_0_get_ras_hw_caps,
};
