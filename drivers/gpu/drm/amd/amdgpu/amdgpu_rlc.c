
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

#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_rlc.h"

/**
 * amdgpu_gfx_rlc_fini - Free BO which used for RLC
 *
 * @adev: amdgpu_device pointer
 *
 * Free three BO which is used for rlc_save_restore_block, rlc_clear_state_block
 * and rlc_jump_table_block.
 */
void amdgpu_gfx_rlc_fini(struct amdgpu_device *adev)
{
	/* save restore block */
	if (adev->gfx.rlc.save_restore_obj) {
		amdgpu_bo_free_kernel(&adev->gfx.rlc.save_restore_obj,
				      &adev->gfx.rlc.save_restore_gpu_addr,
				      (void **)&adev->gfx.rlc.sr_ptr);
	}

	/* clear state block */
	amdgpu_bo_free_kernel(&adev->gfx.rlc.clear_state_obj,
			      &adev->gfx.rlc.clear_state_gpu_addr,
			      (void **)&adev->gfx.rlc.cs_ptr);

	/* jump table block */
	amdgpu_bo_free_kernel(&adev->gfx.rlc.cp_table_obj,
			      &adev->gfx.rlc.cp_table_gpu_addr,
			      (void **)&adev->gfx.rlc.cp_table_ptr);
}
