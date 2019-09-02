/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include "amdgpu_ras.h"

int amdgpu_mmhub_ras_late_init(struct amdgpu_device *adev)
{
	int r;
	struct ras_ih_if ih_info = {
		.cb = NULL,
	};
	struct ras_fs_if fs_info = {
		.sysfs_name = "mmhub_err_count",
		.debugfs_name = "mmhub_err_inject",
	};

	if (!adev->gmc.mmhub_ras_if) {
		adev->gmc.mmhub_ras_if = kmalloc(sizeof(struct ras_common_if), GFP_KERNEL);
		if (!adev->gmc.mmhub_ras_if)
			return -ENOMEM;
		adev->gmc.mmhub_ras_if->block = AMDGPU_RAS_BLOCK__MMHUB;
		adev->gmc.mmhub_ras_if->type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
		adev->gmc.mmhub_ras_if->sub_block_index = 0;
		strcpy(adev->gmc.mmhub_ras_if->name, "mmhub");
	}
	ih_info.head = fs_info.head = *adev->gmc.mmhub_ras_if;
	r = amdgpu_ras_late_init(adev, adev->gmc.mmhub_ras_if,
				 &fs_info, &ih_info);
	if (r || !amdgpu_ras_is_supported(adev, adev->gmc.mmhub_ras_if->block)) {
		kfree(adev->gmc.mmhub_ras_if);
		adev->gmc.mmhub_ras_if = NULL;
	}

	return r;
}
