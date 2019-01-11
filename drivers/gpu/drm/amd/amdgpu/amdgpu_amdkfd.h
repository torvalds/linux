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
#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/workqueue.h>
#include <kgd_kfd_interface.h>
#include <drm/ttm/ttm_execbuf_util.h>
#include "amdgpu_sync.h"
#include "amdgpu_vm.h"

extern const struct kgd2kfd_calls *kgd2kfd;

struct amdgpu_device;

struct kfd_bo_va_list {
	struct list_head bo_list;
	struct amdgpu_bo_va *bo_va;
	void *kgd_dev;
	bool is_mapped;
	uint64_t va;
	uint64_t pte_flags;
};

struct kgd_mem {
	struct mutex lock;
	struct amdgpu_bo *bo;
	struct list_head bo_va_list;
	/* protected by amdkfd_process_info.lock */
	struct ttm_validate_buffer validate_list;
	struct ttm_validate_buffer resv_list;
	uint32_t domain;
	unsigned int mapped_to_gpu_memory;
	uint64_t va;

	uint32_t mapping_flags;

	atomic_t invalid;
	struct amdkfd_process_info *process_info;
	struct page **user_pages;

	struct amdgpu_sync sync;

	bool aql_queue;
};

/* KFD Memory Eviction */
struct amdgpu_amdkfd_fence {
	struct dma_fence base;
	struct mm_struct *mm;
	spinlock_t lock;
	char timeline_name[TASK_COMM_LEN];
};

struct amdgpu_amdkfd_fence *amdgpu_amdkfd_fence_create(u64 context,
						       struct mm_struct *mm);
bool amdkfd_fence_check_mm(struct dma_fence *f, struct mm_struct *mm);
struct amdgpu_amdkfd_fence *to_amdgpu_amdkfd_fence(struct dma_fence *f);

struct amdkfd_process_info {
	/* List head of all VMs that belong to a KFD process */
	struct list_head vm_list_head;
	/* List head for all KFD BOs that belong to a KFD process. */
	struct list_head kfd_bo_list;
	/* List of userptr BOs that are valid or invalid */
	struct list_head userptr_valid_list;
	struct list_head userptr_inval_list;
	/* Lock to protect kfd_bo_list */
	struct mutex lock;

	/* Number of VMs */
	unsigned int n_vms;
	/* Eviction Fence */
	struct amdgpu_amdkfd_fence *eviction_fence;

	/* MMU-notifier related fields */
	atomic_t evicted_bos;
	struct delayed_work restore_userptr_work;
	struct pid *pid;
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

int amdgpu_amdkfd_evict_userptr(struct kgd_mem *mem, struct mm_struct *mm);
int amdgpu_amdkfd_submit_ib(struct kgd_dev *kgd, enum kgd_engine_type engine,
				uint32_t vmid, uint64_t gpu_addr,
				uint32_t *ib_cmd, uint32_t ib_len);

struct kfd2kgd_calls *amdgpu_amdkfd_gfx_7_get_functions(void);
struct kfd2kgd_calls *amdgpu_amdkfd_gfx_8_0_get_functions(void);
struct kfd2kgd_calls *amdgpu_amdkfd_gfx_9_0_get_functions(void);

bool amdgpu_amdkfd_is_kfd_vmid(struct amdgpu_device *adev, u32 vmid);

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

/* GPUVM API */
int amdgpu_amdkfd_gpuvm_create_process_vm(struct kgd_dev *kgd, void **vm,
					void **process_info,
					struct dma_fence **ef);
int amdgpu_amdkfd_gpuvm_acquire_process_vm(struct kgd_dev *kgd,
					struct file *filp,
					void **vm, void **process_info,
					struct dma_fence **ef);
void amdgpu_amdkfd_gpuvm_destroy_cb(struct amdgpu_device *adev,
				struct amdgpu_vm *vm);
void amdgpu_amdkfd_gpuvm_destroy_process_vm(struct kgd_dev *kgd, void *vm);
uint32_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *vm);
int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct kgd_dev *kgd, uint64_t va, uint64_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, uint32_t flags);
int amdgpu_amdkfd_gpuvm_free_memory_of_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem);
int amdgpu_amdkfd_gpuvm_map_memory_to_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm);
int amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm);
int amdgpu_amdkfd_gpuvm_sync_memory(
		struct kgd_dev *kgd, struct kgd_mem *mem, bool intr);
int amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(struct kgd_dev *kgd,
		struct kgd_mem *mem, void **kptr, uint64_t *size);
int amdgpu_amdkfd_gpuvm_restore_process_bos(void *process_info,
					    struct dma_fence **ef);

void amdgpu_amdkfd_gpuvm_init_mem_limits(void);
void amdgpu_amdkfd_unreserve_system_memory_limit(struct amdgpu_bo *bo);

#endif /* AMDGPU_AMDKFD_H_INCLUDED */
