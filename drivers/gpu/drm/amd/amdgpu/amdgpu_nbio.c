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

int amdgpu_nbio_ras_sw_init(struct amdgpu_device *adev)
{
	int err;
	struct amdgpu_nbio_ras *ras;

	if (!adev->nbio.ras)
		return 0;

	ras = adev->nbio.ras;
	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register pcie_bif ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "pcie_bif");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__PCIE_BIF;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->nbio.ras_if = &ras->ras_block.ras_comm;

	return 0;
}

int amdgpu_nbio_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;
	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	if (amdgpu_ras_is_supported(adev, ras_block->block)) {
		r = amdgpu_irq_get(adev, &adev->nbio.ras_controller_irq, 0);
		if (r)
			goto late_fini;
		r = amdgpu_irq_get(adev, &adev->nbio.ras_err_event_athub_irq, 0);
		if (r)
			goto late_fini;
	}

	return 0;
late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);
	return r;
}
