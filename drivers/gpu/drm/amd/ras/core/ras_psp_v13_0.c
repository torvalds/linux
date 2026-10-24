// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include "ras_psp_v13_0.h"

#define regMP0_SMN_C2PMSG_67                           0x0083
#define regMP0_SMN_C2PMSG_67_BASE_IDX                  0
#define regMP0_SMN_C2PMSG_127                          0x00bf
#define regMP0_SMN_C2PMSG_127_BASE_IDX                 0

static struct ras_block_map   ras_block_maps_v13_0[] = {
	{RAS_BLOCK_ID__UMC,       RAS_TA_BLOCK__UMC},
	{RAS_BLOCK_ID__SDMA,      RAS_TA_BLOCK__SDMA},
	{RAS_BLOCK_ID__GFX,       RAS_TA_BLOCK__GFX},
	{RAS_BLOCK_ID__MMHUB,     RAS_TA_BLOCK__MMHUB},
	{RAS_BLOCK_ID__ATHUB,     RAS_TA_BLOCK__ATHUB},
	{RAS_BLOCK_ID__PCIE_BIF,  RAS_TA_BLOCK__PCIE_BIF},
	{RAS_BLOCK_ID__HDP,       RAS_TA_BLOCK__HDP},
	{RAS_BLOCK_ID__XGMI_WAFL, RAS_TA_BLOCK__XGMI_WAFL},
	{RAS_BLOCK_ID__DF,        RAS_TA_BLOCK__DF},
	{RAS_BLOCK_ID__SMN,       RAS_TA_BLOCK__SMN},
	{RAS_BLOCK_ID__SEM,       RAS_TA_BLOCK__SEM},
	{RAS_BLOCK_ID__MP0,       RAS_TA_BLOCK__MP0},
	{RAS_BLOCK_ID__MP1,       RAS_TA_BLOCK__MP1},
	{RAS_BLOCK_ID__FUSE,      RAS_TA_BLOCK__FUSE},
	{RAS_BLOCK_ID__MCA,       RAS_TA_BLOCK__MCA},
	{RAS_BLOCK_ID__VCN,       RAS_TA_BLOCK__VCN},
	{RAS_BLOCK_ID__JPEG,      RAS_TA_BLOCK__JPEG},
	{RAS_BLOCK_ID__IH,        RAS_TA_BLOCK__IH},
	{RAS_BLOCK_ID__MPIO,      RAS_TA_BLOCK__MPIO},
	{RAS_BLOCK_ID__MMSCH,     RAS_TA_BLOCK__MMSCH},
};

static uint32_t ras_psp_v13_0_ring_wptr_get(struct ras_core_context *ras_core)
{
	return RAS_DEV_RREG32_SOC15(ras_core->dev, MP0, 0, regMP0_SMN_C2PMSG_67);
}

static int ras_psp_v13_0_ring_wptr_set(struct ras_core_context *ras_core, uint32_t value)
{
	RAS_DEV_WREG32_SOC15(ras_core->dev, MP0, 0, regMP0_SMN_C2PMSG_67, value);

	return 0;
}

static int ras_psp_v13_0_get_ras_block_maps(struct ras_core_context *ras_core,
			struct ras_block_map **blk_maps, uint32_t *maps_size)
{
	if (!blk_maps || !maps_size)
		return -EINVAL;

	*blk_maps = ras_block_maps_v13_0;
	*maps_size = ARRAY_SIZE(ras_block_maps_v13_0);

	return 0;
}

static int ras_psp_v13_0_get_ras_hw_caps(struct ras_core_context *ras_core,
				struct ras_hw_caps *ras_cap)
{
	uint32_t ras_hw_cap, bit;
	uint32_t i;

	if (!ras_cap)
		return -EINVAL;

	ras_hw_cap = RAS_DEV_RREG32_SOC15(ras_core->dev, MP0, 0, regMP0_SMN_C2PMSG_127);

	ras_cap->poison_supported = true;
	ras_cap->flex_mca_enabled = false;

	ras_cap->features.value = 0;
	for (bit = 0; bit <= 29; bit++) {
		if (!(ras_hw_cap & BIT_ULL(bit)))
			continue;

		for (i = 0; i < ARRAY_SIZE(ras_block_maps_v13_0); i++) {
			if (ras_block_maps_v13_0[i].ta_id == bit) {
				if (ras_block_maps_v13_0[i].ras_id < MAX_SUPPORTED_RAS_BLOCK_ID)
					ras_cap->features.block_mask |=
						BIT_ULL(ras_block_maps_v13_0[i].ras_id);
				else
					RAS_DEV_WARN(ras_core->dev,
						"RAS block maps v13 error: invalid ras block id %u\n",
						ras_block_maps_v13_0[i].ras_id);

				break;
			}
		}
	}

	return 0;
}

const struct ras_psp_ip_func ras_psp_v13_0 = {
	.psp_ras_ring_wptr_get = ras_psp_v13_0_ring_wptr_get,
	.psp_ras_ring_wptr_set = ras_psp_v13_0_ring_wptr_set,
	.get_ras_block_maps = ras_psp_v13_0_get_ras_block_maps,
	.get_ras_hw_caps = ras_psp_v13_0_get_ras_hw_caps,
};
