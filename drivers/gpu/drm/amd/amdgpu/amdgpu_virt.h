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
#define AMDGPU_SRIOV_CAPS_RUNTIME      (1 << 4) /* is out of full access mode */

/**
 * struct amdgpu_virt_ops - amdgpu device virt operations
 */
struct amdgpu_virt_ops {
	int (*req_full_gpu)(struct amdgpu_device *adev, bool init);
	int (*rel_full_gpu)(struct amdgpu_device *adev, bool init);
	int (*reset_gpu)(struct amdgpu_device *adev);
};

/* GPU virtualization */
struct amdgpu_virt {
	uint32_t			caps;
	struct amdgpu_bo		*csa_obj;
	uint64_t			csa_vmid0_addr;
	uint32_t			reg_val_offs;
	struct mutex			lock;
	struct amdgpu_irq_src		ack_irq;
	struct amdgpu_irq_src		rcv_irq;
	struct delayed_work		flr_work;
	const struct amdgpu_virt_ops	*ops;
};

#define AMDGPU_CSA_SIZE    (8 * 1024)
#define AMDGPU_CSA_VADDR   (AMDGPU_VA_RESERVED_SIZE - AMDGPU_CSA_SIZE)

#define amdgpu_sriov_enabled(adev) \
((adev)->virt.caps & AMDGPU_SRIOV_CAPS_ENABLE_IOV)

#define amdgpu_sriov_vf(adev) \
((adev)->virt.caps & AMDGPU_SRIOV_CAPS_IS_VF)

#define amdgpu_sriov_bios(adev) \
((adev)->virt.caps & AMDGPU_SRIOV_CAPS_SRIOV_VBIOS)

#define amdgpu_sriov_runtime(adev) \
((adev)->virt.caps & AMDGPU_SRIOV_CAPS_RUNTIME)

#define amdgpu_passthrough(adev) \
((adev)->virt.caps & AMDGPU_PASSTHROUGH_MODE)

static inline bool is_virtual_machine(void)
{
#ifdef CONFIG_X86
	return boot_cpu_has(X86_FEATURE_HYPERVISOR);
#else
	return false;
#endif
}

struct amdgpu_vm;
int amdgpu_allocate_static_csa(struct amdgpu_device *adev);
int amdgpu_map_static_csa(struct amdgpu_device *adev, struct amdgpu_vm *vm);
void amdgpu_virt_init_setting(struct amdgpu_device *adev);
uint32_t amdgpu_virt_kiq_rreg(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_virt_kiq_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v);
int amdgpu_virt_request_full_gpu(struct amdgpu_device *adev, bool init);
int amdgpu_virt_release_full_gpu(struct amdgpu_device *adev, bool init);
int amdgpu_virt_reset_gpu(struct amdgpu_device *adev);

#endif
