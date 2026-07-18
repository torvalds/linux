/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_PTL_H__
#define __AMDGPU_PTL_H__

enum amdgpu_ptl_fmt {
	AMDGPU_PTL_FMT_I8   = 0,
	AMDGPU_PTL_FMT_F16  = 1,
	AMDGPU_PTL_FMT_BF16 = 2,
	AMDGPU_PTL_FMT_F32  = 3,
	AMDGPU_PTL_FMT_F64  = 4,
	AMDGPU_PTL_FMT_F8   = 5,
	AMDGPU_PTL_FMT_VECTOR  = 6,
	AMDGPU_PTL_FMT_INVALID = 7,
};

enum amdgpu_ptl_disable_source {
	AMDGPU_PTL_DISABLE_SYSFS = 0,
	AMDGPU_PTL_DISABLE_PROFILER,
	AMDGPU_PTL_DISABLE_MAX,
};
struct amdgpu_ptl {
	enum amdgpu_ptl_fmt		fmt1;
	enum amdgpu_ptl_fmt		fmt2;
	bool				enabled;
	bool				hw_supported;
	bool				permanently_disabled;
	/* PTL disable reference counting */
	atomic_t			disable_ref;
	struct mutex			mutex;
	DECLARE_BITMAP(disable_bitmap, AMDGPU_PTL_DISABLE_MAX);
	bool				ptl_sysfs_created;
};

int amdgpu_ptl_perf_monitor_ctrl(struct amdgpu_device *adev, u32 req_code,
		u32 *ptl_state,
		enum amdgpu_ptl_fmt *fmt1,
		enum amdgpu_ptl_fmt *fmt2);

int amdgpu_ptl_sysfs_init(struct amdgpu_device *adev);
void amdgpu_ptl_sysfs_fini(struct amdgpu_device *adev);

extern const struct attribute_group amdgpu_ptl_attr_group;
#endif /* __AMDGPU_PTL_H__ */
