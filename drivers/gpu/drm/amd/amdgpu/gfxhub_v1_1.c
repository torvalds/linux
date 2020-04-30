/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#include "gfxhub_v1_1.h"

#include "gc/gc_9_2_1_offset.h"
#include "gc/gc_9_2_1_sh_mask.h"

#include "soc15_common.h"

#define mmMC_VM_XGMI_LFB_CNTL_ALDE			0x0978
#define mmMC_VM_XGMI_LFB_CNTL_ALDE_BASE_IDX		0
#define mmMC_VM_XGMI_LFB_SIZE_ALDE			0x0979
#define mmMC_VM_XGMI_LFB_SIZE_ALDE_BASE_IDX		0
//MC_VM_XGMI_LFB_CNTL
#define MC_VM_XGMI_LFB_CNTL_ALDE__PF_LFB_REGION__SHIFT	0x0
#define MC_VM_XGMI_LFB_CNTL_ALDE__PF_MAX_REGION__SHIFT	0x4
#define MC_VM_XGMI_LFB_CNTL_ALDE__PF_LFB_REGION_MASK	0x0000000FL
#define MC_VM_XGMI_LFB_CNTL_ALDE__PF_MAX_REGION_MASK	0x000000F0L
//MC_VM_XGMI_LFB_SIZE
#define MC_VM_XGMI_LFB_SIZE_ALDE__PF_LFB_SIZE__SHIFT	0x0
#define MC_VM_XGMI_LFB_SIZE_ALDE__PF_LFB_SIZE_MASK	0x0001FFFFL

int gfxhub_v1_1_get_xgmi_info(struct amdgpu_device *adev)
{
	u32 max_num_physical_nodes;
	u32 max_physical_node_id;
	u32 xgmi_lfb_cntl;
	u32 max_region;
	u64 seg_size;

	if (adev->asic_type == CHIP_ALDEBARAN) {
		xgmi_lfb_cntl = RREG32_SOC15(GC, 0, mmMC_VM_XGMI_LFB_CNTL_ALDE);
		seg_size = REG_GET_FIELD(
			RREG32_SOC15(GC, 0, mmMC_VM_XGMI_LFB_SIZE_ALDE),
			MC_VM_XGMI_LFB_SIZE, PF_LFB_SIZE) << 24;
	} else {
		xgmi_lfb_cntl = RREG32_SOC15(GC, 0, mmMC_VM_XGMI_LFB_CNTL);
		seg_size = REG_GET_FIELD(
			RREG32_SOC15(GC, 0, mmMC_VM_XGMI_LFB_SIZE),
			MC_VM_XGMI_LFB_SIZE, PF_LFB_SIZE) << 24;
	}

	max_region =
		REG_GET_FIELD(xgmi_lfb_cntl, MC_VM_XGMI_LFB_CNTL, PF_MAX_REGION);


	switch (adev->asic_type) {
	case CHIP_VEGA20:
		max_num_physical_nodes   = 4;
		max_physical_node_id     = 3;
		break;
	case CHIP_ARCTURUS:
		max_num_physical_nodes   = 8;
		max_physical_node_id     = 7;
		break;
	case CHIP_ALDEBARAN:
		/* just using duplicates for Aldebaran support, revisit later */
		max_num_physical_nodes   = 8;
		max_physical_node_id     = 7;
		break;
	default:
		return -EINVAL;
	}

	/* PF_MAX_REGION=0 means xgmi is disabled */
	if (max_region || adev->gmc.xgmi.connected_to_cpu) {
		adev->gmc.xgmi.num_physical_nodes = max_region + 1;

		if (adev->gmc.xgmi.num_physical_nodes > max_num_physical_nodes)
		return -EINVAL;

		adev->gmc.xgmi.physical_node_id =
		REG_GET_FIELD(xgmi_lfb_cntl, MC_VM_XGMI_LFB_CNTL,
			      PF_LFB_REGION);

		if (adev->gmc.xgmi.physical_node_id > max_physical_node_id)
		return -EINVAL;

		adev->gmc.xgmi.node_segment_size = seg_size;
	}

	return 0;
}
