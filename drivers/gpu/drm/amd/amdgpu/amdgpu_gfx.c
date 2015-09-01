/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
#include <drm/drmP.h>
#include "amdgpu.h"

/*
 * GPU scratch registers helpers function.
 */
/**
 * amdgpu_gfx_scratch_get - Allocate a scratch register
 *
 * @adev: amdgpu_device pointer
 * @reg: scratch register mmio offset
 *
 * Allocate a CP scratch register for use by the driver (all asics).
 * Returns 0 on success or -EINVAL on failure.
 */
int amdgpu_gfx_scratch_get(struct amdgpu_device *adev, uint32_t *reg)
{
	int i;

	for (i = 0; i < adev->gfx.scratch.num_reg; i++) {
		if (adev->gfx.scratch.free[i]) {
			adev->gfx.scratch.free[i] = false;
			*reg = adev->gfx.scratch.reg[i];
			return 0;
		}
	}
	return -EINVAL;
}

/**
 * amdgpu_gfx_scratch_free - Free a scratch register
 *
 * @adev: amdgpu_device pointer
 * @reg: scratch register mmio offset
 *
 * Free a CP scratch register allocated for use by the driver (all asics)
 */
void amdgpu_gfx_scratch_free(struct amdgpu_device *adev, uint32_t reg)
{
	int i;

	for (i = 0; i < adev->gfx.scratch.num_reg; i++) {
		if (adev->gfx.scratch.reg[i] == reg) {
			adev->gfx.scratch.free[i] = true;
			return;
		}
	}
}
