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

#include "amdgpu.h"
#include "amdgpu_ip.h"

static int8_t amdgpu_logical_to_dev_inst(struct amdgpu_device *adev,
					 enum amd_hw_ip_block_type block,
					 int8_t inst)
{
	int8_t dev_inst;

	switch (block) {
	case GC_HWIP:
	case SDMA0_HWIP:
	/* Both JPEG and VCN as JPEG is only alias of VCN */
	case VCN_HWIP:
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

static uint32_t amdgpu_logical_to_dev_mask(struct amdgpu_device *adev,
					   enum amd_hw_ip_block_type block,
					   uint32_t mask)
{
	uint32_t dev_mask = 0;
	int8_t log_inst, dev_inst;

	while (mask) {
		log_inst = ffs(mask) - 1;
		dev_inst = amdgpu_logical_to_dev_inst(adev, block, log_inst);
		dev_mask |= (1 << dev_inst);
		mask &= ~(1 << log_inst);
	}

	return dev_mask;
}

static void amdgpu_populate_ip_map(struct amdgpu_device *adev,
				   enum amd_hw_ip_block_type ip_block,
				   uint32_t inst_mask)
{
	int l = 0, i;

	while (inst_mask) {
		i = ffs(inst_mask) - 1;
		adev->ip_map.dev_inst[ip_block][l++] = i;
		inst_mask &= ~(1 << i);
	}
	for (; l < HWIP_MAX_INSTANCE; l++)
		adev->ip_map.dev_inst[ip_block][l] = -1;
}

void amdgpu_ip_map_init(struct amdgpu_device *adev)
{
	u32 ip_map[][2] = {
		{ GC_HWIP, adev->gfx.xcc_mask },
		{ SDMA0_HWIP, adev->sdma.sdma_mask },
		{ VCN_HWIP, adev->vcn.inst_mask },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(ip_map); ++i)
		amdgpu_populate_ip_map(adev, ip_map[i][0], ip_map[i][1]);

	adev->ip_map.logical_to_dev_inst = amdgpu_logical_to_dev_inst;
	adev->ip_map.logical_to_dev_mask = amdgpu_logical_to_dev_mask;
}
