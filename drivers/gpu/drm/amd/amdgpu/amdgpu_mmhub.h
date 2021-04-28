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
#ifndef __AMDGPU_MMHUB_H__
#define __AMDGPU_MMHUB_H__

struct amdgpu_mmhub_ras_funcs {
	int (*ras_late_init)(struct amdgpu_device *adev);
	void (*ras_fini)(struct amdgpu_device *adev);
	void (*query_ras_error_count)(struct amdgpu_device *adev,
				      void *ras_error_status);
	void (*query_ras_error_status)(struct amdgpu_device *adev);
	void (*reset_ras_error_count)(struct amdgpu_device *adev);
};

struct amdgpu_mmhub_funcs {
	u64 (*get_fb_location)(struct amdgpu_device *adev);
	void (*init)(struct amdgpu_device *adev);
	int (*gart_enable)(struct amdgpu_device *adev);
	void (*set_fault_enable_default)(struct amdgpu_device *adev,
			bool value);
	void (*gart_disable)(struct amdgpu_device *adev);
	int (*set_clockgating)(struct amdgpu_device *adev,
			       enum amd_clockgating_state state);
	void (*get_clockgating)(struct amdgpu_device *adev, u32 *flags);
	void (*setup_vm_pt_regs)(struct amdgpu_device *adev, uint32_t vmid,
				uint64_t page_table_base);
	void (*update_power_gating)(struct amdgpu_device *adev,
                                bool enable);
};

struct amdgpu_mmhub {
	struct ras_common_if *ras_if;
	const struct amdgpu_mmhub_funcs *funcs;
	const struct amdgpu_mmhub_ras_funcs *ras_funcs;
};

int amdgpu_mmhub_ras_late_init(struct amdgpu_device *adev);
void amdgpu_mmhub_ras_fini(struct amdgpu_device *adev);
#endif

