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
 */

#include <linux/device.h>

#include <drm/amd_asic_type.h>

#include "amdgpu.h"
#include "amdgpu_tmz.h"


/**
 * amdgpu_is_tmz - validate trust memory zone
 *
 * @adev: amdgpu_device pointer
 *
 * Return true if @dev supports trusted memory zones (TMZ), and return false if
 * @dev does not support TMZ.
 */
bool amdgpu_is_tmz(struct amdgpu_device *adev)
{
	if (!amdgpu_tmz)
		return false;

	if (adev->asic_type < CHIP_RAVEN || adev->asic_type == CHIP_ARCTURUS) {
		dev_warn(adev->dev, "doesn't support trusted memory zones (TMZ)\n");
		return false;
	}

	dev_info(adev->dev, "TMZ feature is enabled\n");

	return true;
}
