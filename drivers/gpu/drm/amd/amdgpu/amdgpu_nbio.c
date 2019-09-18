/*
 * Copyright (C) 2019  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "amdgpu.h"
#include "amdgpu_ras.h"

int amdgpu_nbio_ras_late_init(struct amdgpu_device *adev)
{
	int r;
	struct ras_ih_if ih_info = {
		.cb = NULL,
	};
	struct ras_fs_if fs_info = {
		.sysfs_name = "pcie_bif_err_count",
		.debugfs_name = "pcie_bif_err_inject",
	};

	if (!adev->nbio.ras_if) {
		adev->nbio.ras_if = kmalloc(sizeof(struct ras_common_if), GFP_KERNEL);
		if (!adev->nbio.ras_if)
			return -ENOMEM;
		adev->nbio.ras_if->block = AMDGPU_RAS_BLOCK__PCIE_BIF;
		adev->nbio.ras_if->type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
		adev->nbio.ras_if->sub_block_index = 0;
		strcpy(adev->nbio.ras_if->name, "pcie_bif");
	}
	ih_info.head = fs_info.head = *adev->nbio.ras_if;
	r = amdgpu_ras_late_init(adev, adev->nbio.ras_if,
				 &fs_info, &ih_info);
	if (r)
		goto free;

	if (amdgpu_ras_is_supported(adev, adev->nbio.ras_if->block)) {
		r = amdgpu_irq_get(adev, &adev->nbio.ras_controller_irq, 0);
		if (r)
			goto late_fini;
		r = amdgpu_irq_get(adev, &adev->nbio.ras_err_event_athub_irq, 0);
		if (r)
			goto late_fini;
	} else {
		r = 0;
		goto free;
	}

	return 0;
late_fini:
	amdgpu_ras_late_fini(adev, adev->nbio.ras_if, &ih_info);
free:
	kfree(adev->nbio.ras_if);
	adev->nbio.ras_if = NULL;
	return r;
}

void amdgpu_nbio_ras_fini(struct amdgpu_device *adev)
{
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__PCIE_BIF) &&
			adev->nbio.ras_if) {
		struct ras_common_if *ras_if = adev->nbio.ras_if;
		struct ras_ih_if ih_info = {
			.cb = NULL,
		};

		amdgpu_ras_late_fini(adev, ras_if, &ih_info);
		kfree(ras_if);
	}
}
