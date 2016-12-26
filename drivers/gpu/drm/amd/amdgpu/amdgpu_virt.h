/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Monk.liu@amd.com
 */
#ifndef AMDGPU_VIRT_H
#define AMDGPU_VIRT_H

#define AMDGPU_SRIOV_CAPS_SRIOV_VBIOS  (1 << 0) /* vBIOS is sr-iov ready */
#define AMDGPU_SRIOV_CAPS_ENABLE_IOV   (1 << 1) /* sr-iov is enabled on this GPU */
#define AMDGPU_SRIOV_CAPS_IS_VF        (1 << 2) /* this GPU is a virtual function */
#define AMDGPU_PASSTHROUGH_MODE        (1 << 3) /* thw whole GPU is pass through for VM */
/* GPU virtualization */
struct amdgpu_virtualization {
	uint32_t virtual_caps;
};

#define amdgpu_sriov_enabled(adev) \
((adev)->virtualization.virtual_caps & AMDGPU_SRIOV_CAPS_ENABLE_IOV)

#define amdgpu_sriov_vf(adev) \
((adev)->virtualization.virtual_caps & AMDGPU_SRIOV_CAPS_IS_VF)

#define amdgpu_sriov_bios(adev) \
((adev)->virtualization.virtual_caps & AMDGPU_SRIOV_CAPS_SRIOV_VBIOS)

#define amdgpu_passthrough(adev) \
((adev)->virtualization.virtual_caps & AMDGPU_PASSTHROUGH_MODE)

static inline bool is_virtual_machine(void)
{
#ifdef CONFIG_X86
	return boot_cpu_has(X86_FEATURE_HYPERVISOR);
#else
	return false;
#endif
}

#endif