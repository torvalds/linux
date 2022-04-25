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
	for (i = 0; i < adev->num_aid * adev->sdma.num_inst_per_aid; i++)
		adev->doorbell_index.sdma_engine[i] =
			AMDGPU_DOORBELL_LAYOUT1_sDMA_ENGINE_START +
			i * (adev->doorbell_index.sdma_doorbell_range >> 1);

	adev->doorbell_index.ih = AMDGPU_DOORBELL_LAYOUT1_IH;
	adev->doorbell_index.vcn.vcn_ring0_1 = AMDGPU_DOORBELL_LAYOUT1_VCN_START;

	adev->doorbell_index.first_non_cp = AMDGPU_DOORBELL_LAYOUT1_FIRST_NON_CP;
	adev->doorbell_index.last_non_cp = AMDGPU_DOORBELL_LAYOUT1_LAST_NON_CP;

	adev->doorbell_index.max_assignment = AMDGPU_DOORBELL_LAYOUT1_MAX_ASSIGNMENT << 1;
}
