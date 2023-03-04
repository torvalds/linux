/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

int amdgpu_hdp_ras_sw_init(struct amdgpu_device *adev)
{
	int err;
	struct amdgpu_hdp_ras *ras;

	if (!adev->hdp.ras)
		return 0;

	ras = adev->hdp.ras;
	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register hdp ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "hdp");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__HDP;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->hdp.ras_if = &ras->ras_block.ras_comm;

	/* hdp ras follows amdgpu_ras_block_late_init_default for late init */
	return 0;
}
