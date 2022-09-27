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
#include "amdgpu.h"
#include "soc15.h"

#include "soc15_common.h"

void aqua_vanjaram_doorbell_index_init(struct amdgpu_device *adev)
{
	int i;

	adev->doorbell_index.kiq = AMDGPU_DOORBELL_LAYOUT1_KIQ_START;

	adev->doorbell_index.mec_ring0 = AMDGPU_DOORBELL_LAYOUT1_MEC_RING_START;

	adev->doorbell_index.userqueue_start = AMDGPU_DOORBELL_LAYOUT1_USERQUEUE_START;
	adev->doorbell_index.userqueue_end = AMDGPU_DOORBELL_LAYOUT1_USERQUEUE_END;

	adev->doorbell_index.sdma_doorbell_range = 20;
	for (i = 0; i < adev->sdma.num_instances; i++)
		adev->doorbell_index.sdma_engine[i] =
			AMDGPU_DOORBELL_LAYOUT1_sDMA_ENGINE_START +
			i * (adev->doorbell_index.sdma_doorbell_range >> 1);

	adev->doorbell_index.ih = AMDGPU_DOORBELL_LAYOUT1_IH;
	adev->doorbell_index.vcn.vcn_ring0_1 = AMDGPU_DOORBELL_LAYOUT1_VCN_START;

	adev->doorbell_index.first_non_cp = AMDGPU_DOORBELL_LAYOUT1_FIRST_NON_CP;
	adev->doorbell_index.last_non_cp = AMDGPU_DOORBELL_LAYOUT1_LAST_NON_CP;

	adev->doorbell_index.max_assignment = AMDGPU_DOORBELL_LAYOUT1_MAX_ASSIGNMENT << 1;
}

static int8_t aqua_vanjaram_logical_to_dev_inst(struct amdgpu_device *adev,
					 enum amd_hw_ip_block_type block,
					 int8_t inst)
{
	int8_t dev_inst;

	switch (block) {
	case GC_HWIP:
	case SDMA0_HWIP:
		dev_inst = adev->ip_map.dev_inst[block][inst];
		break;
	default:
		/* For rest of the IPs, no look up required.
		 * Assume 'logical instance == physical instance' for all configs. */
		dev_inst = inst;
		break;
	}

	return dev_inst;
}

void aqua_vanjaram_ip_map_init(struct amdgpu_device *adev)
{
	int xcc_mask, sdma_mask;
	int l, i;

	/* Map GC instances */
	l = 0;
	xcc_mask = adev->gfx.xcc_mask;
	while (xcc_mask) {
		i = ffs(xcc_mask) - 1;
		adev->ip_map.dev_inst[GC_HWIP][l++] = i;
		xcc_mask &= ~(1 << i);
	}
	for (; l < HWIP_MAX_INSTANCE; l++)
		adev->ip_map.dev_inst[GC_HWIP][l] = -1;

	l = 0;
	sdma_mask = adev->sdma.sdma_mask;
	while (sdma_mask) {
		i = ffs(sdma_mask) - 1;
		adev->ip_map.dev_inst[SDMA0_HWIP][l++] = i;
		sdma_mask &= ~(1 << i);
	}
	for (; l < HWIP_MAX_INSTANCE; l++)
		adev->ip_map.dev_inst[SDMA0_HWIP][l] = -1;

	adev->ip_map.logical_to_dev_inst = aqua_vanjaram_logical_to_dev_inst;
}

/* Fixed pattern for smn addressing on different AIDs:
 *   bit[34]: indicate cross AID access
 *   bit[33:32]: indicate target AID id
 * AID id range is 0 ~ 3 as maximum AID number is 4.
 */
u64 aqua_vanjaram_encode_ext_smn_addressing(int ext_id)
{
	u64 ext_offset;

	/* local routing and bit[34:32] will be zeros */
	if (ext_id == 0)
		return 0;

	/* Initiated from host, accessing to all non-zero aids are cross traffic */
	ext_offset = ((u64)(ext_id & 0x3) << 32) | (1ULL << 34);

	return ext_offset;
}
