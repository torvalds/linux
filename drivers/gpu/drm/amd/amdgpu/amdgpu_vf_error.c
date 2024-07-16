/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#include "amdgpu_vf_error.h"
#include "mxgpu_ai.h"

void amdgpu_vf_error_put(struct amdgpu_device *adev,
			 uint16_t sub_error_code,
			 uint16_t error_flags,
			 uint64_t error_data)
{
	int index;
	uint16_t error_code;

	if (!amdgpu_sriov_vf(adev))
		return;

	error_code = AMDGIM_ERROR_CODE(AMDGIM_ERROR_CATEGORY_VF, sub_error_code);

	mutex_lock(&adev->virt.vf_errors.lock);
	index = adev->virt.vf_errors.write_count % AMDGPU_VF_ERROR_ENTRY_SIZE;
	adev->virt.vf_errors.code [index] = error_code;
	adev->virt.vf_errors.flags [index] = error_flags;
	adev->virt.vf_errors.data [index] = error_data;
	adev->virt.vf_errors.write_count ++;
	mutex_unlock(&adev->virt.vf_errors.lock);
}


void amdgpu_vf_error_trans_all(struct amdgpu_device *adev)
{
	/* u32 pf2vf_flags = 0; */
	u32 data1, data2, data3;
	int index;

	if ((NULL == adev) || (!amdgpu_sriov_vf(adev)) ||
	    (!adev->virt.ops) || (!adev->virt.ops->trans_msg)) {
		return;
	}
/*
	TODO: Enable these code when pv2vf_info is merged
	AMDGPU_FW_VRAM_PF2VF_READ (adev, feature_flags, &pf2vf_flags);
	if (!(pf2vf_flags & AMDGIM_FEATURE_ERROR_LOG_COLLECT)) {
		return;
	}
*/

	mutex_lock(&adev->virt.vf_errors.lock);
	/* The errors are overlay of array, correct read_count as full. */
	if (adev->virt.vf_errors.write_count - adev->virt.vf_errors.read_count > AMDGPU_VF_ERROR_ENTRY_SIZE) {
		adev->virt.vf_errors.read_count = adev->virt.vf_errors.write_count - AMDGPU_VF_ERROR_ENTRY_SIZE;
	}

	while (adev->virt.vf_errors.read_count < adev->virt.vf_errors.write_count) {
		index =adev->virt.vf_errors.read_count % AMDGPU_VF_ERROR_ENTRY_SIZE;
		data1 = AMDGIM_ERROR_CODE_FLAGS_TO_MAILBOX(adev->virt.vf_errors.code[index],
							   adev->virt.vf_errors.flags[index]);
		data2 = adev->virt.vf_errors.data[index] & 0xFFFFFFFF;
		data3 = (adev->virt.vf_errors.data[index] >> 32) & 0xFFFFFFFF;

		adev->virt.ops->trans_msg(adev, IDH_LOG_VF_ERROR, data1, data2, data3);
		adev->virt.vf_errors.read_count ++;
	}
	mutex_unlock(&adev->virt.vf_errors.lock);
}
