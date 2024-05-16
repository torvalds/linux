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
#ifndef __AMDGPU_SMUIO_H__
#define __AMDGPU_SMUIO_H__

struct amdgpu_smuio_funcs {
	u32 (*get_rom_index_offset)(struct amdgpu_device *adev);
	u32 (*get_rom_data_offset)(struct amdgpu_device *adev);
	void (*update_rom_clock_gating)(struct amdgpu_device *adev, bool enable);
	void (*get_clock_gating_state)(struct amdgpu_device *adev, u64 *flags);
	u32 (*get_die_id)(struct amdgpu_device *adev);
	u32 (*get_socket_id)(struct amdgpu_device *adev);
	enum amdgpu_pkg_type (*get_pkg_type)(struct amdgpu_device *adev);
	bool (*is_host_gpu_xgmi_supported)(struct amdgpu_device *adev);
};

struct amdgpu_smuio {
	const struct amdgpu_smuio_funcs		*funcs;
};

#endif /* __AMDGPU_SMUIO_H__ */
