/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

/* amdgpu_amdkfd.h defines the private interface between amdgpu and amdkfd. */

#ifndef AMDGPU_AMDKFD_H_INCLUDED
#define AMDGPU_AMDKFD_H_INCLUDED

#include <linux/types.h>
#include <linux/mmu_context.h>
#include <kgd_kfd_interface.h>

struct amdgpu_device;

struct kgd_mem {
	struct amdgpu_bo *bo;
	uint64_t gpu_addr;
	void *cpu_ptr;
};

int amdgpu_amdkfd_init(void);
void amdgpu_amdkfd_fini(void);

void amdgpu_amdkfd_suspend(struct amdgpu_device *adev);
int amdgpu_amdkfd_resume(struct amdgpu_device *adev);
void amdgpu_amdkfd_interrupt(struct amdgpu_device *adev,
			const void *ih_ring_entry);
void amdgpu_amdkfd_device_probe(struct amdgpu_device *adev);
void amdgpu_amdkfd_device_init(struct amdgpu_device *adev);
void amdgpu_amdkfd_device_fini(struct amdgpu_device *adev);

struct kfd2kgd_calls *amdgpu_amdkfd_gfx_7_get_functions(void);
struct kfd2kgd_calls *amdgpu_amdkfd_gfx_8_0_get_functions(void);

/* Shared API */
int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr);
void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj);
void get_local_mem_info(struct kgd_dev *kgd,
			struct kfd_local_mem_info *mem_info);
uint64_t get_gpu_clock_counter(struct kgd_dev *kgd);

uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd);
void get_cu_info(struct kgd_dev *kgd, struct kfd_cu_info *cu_info);
uint64_t amdgpu_amdkfd_get_vram_usage(struct kgd_dev *kgd);

#define read_user_wptr(mmptr, wptr, dst)				\
	({								\
		bool valid = false;					\
		if ((mmptr) && (wptr)) {				\
			if ((mmptr) == current->mm) {			\
				valid = !get_user((dst), (wptr));	\
			} else if (current->mm == NULL) {		\
				use_mm(mmptr);				\
				valid = !get_user((dst), (wptr));	\
				unuse_mm(mmptr);			\
			}						\
		}							\
		valid;							\
	})

#endif /* AMDGPU_AMDKFD_H_INCLUDED */
