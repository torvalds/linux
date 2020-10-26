/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_GFXHUB_H__
#define __AMDGPU_GFXHUB_H__

struct amdgpu_gfxhub_funcs {
	u64 (*get_fb_location)(struct amdgpu_device *adev);
	u64 (*get_mc_fb_offset)(struct amdgpu_device *adev);
	void (*setup_vm_pt_regs)(struct amdgpu_device *adev, uint32_t vmid,
			uint64_t page_table_base);
	int (*gart_enable)(struct amdgpu_device *adev);

	void (*gart_disable)(struct amdgpu_device *adev);
	void (*set_fault_enable_default)(struct amdgpu_device *adev, bool value);
	void (*init)(struct amdgpu_device *adev);
	int (*get_xgmi_info)(struct amdgpu_device *adev);
};

struct amdgpu_gfxhub {
	const struct amdgpu_gfxhub_funcs *funcs;
};

#endif
