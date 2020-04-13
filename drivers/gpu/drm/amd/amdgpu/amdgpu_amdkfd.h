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
#include <linux/workqueue.h>
#include <kgd_kfd_interface.h>
#include <drm/ttm/ttm_execbuf_util.h>
#include "amdgpu_sync.h"
#include "amdgpu_vm.h"

extern uint64_t amdgpu_amdkfd_total_mem_size;

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

	uint32_t alloc_flags;

	atomic_t invalid;
	struct amdkfd_process_info *process_info;

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

struct amdgpu_kfd_dev {
	struct kfd_dev *dev;
	uint64_t vram_used;
};

enum kgd_engine_type {
	KGD_ENGINE_PFP = 1,
	KGD_ENGINE_ME,
	KGD_ENGINE_CE,
	KGD_ENGINE_MEC1,
	KGD_ENGINE_MEC2,
	KGD_ENGINE_RLC,
	KGD_ENGINE_SDMA1,
	KGD_ENGINE_SDMA2,
	KGD_ENGINE_MAX
};

struct amdgpu_amdkfd_fence *amdgpu_amdkfd_fence_create(u64 context,
						       struct mm_struct *mm);
bool amdkfd_fence_check_mm(struct dma_fence *f, struct mm_struct *mm);
struct amdgpu_amdkfd_fence *to_amdgpu_amdkfd_fence(struct dma_fence *f);
int amdgpu_amdkfd_remove_fence_on_pt_pd_bos(struct amdgpu_bo *bo);

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

void amdgpu_amdkfd_suspend(struct amdgpu_device *adev, bool run_pm);
int amdgpu_amdkfd_resume(struct amdgpu_device *adev, bool run_pm);
void amdgpu_amdkfd_interrupt(struct amdgpu_device *adev,
			const void *ih_ring_entry);
void amdgpu_amdkfd_device_probe(struct amdgpu_device *adev);
void amdgpu_amdkfd_device_init(struct amdgpu_device *adev);
void amdgpu_amdkfd_device_fini(struct amdgpu_device *adev);

int amdgpu_amdkfd_evict_userptr(struct kgd_mem *mem, struct mm_struct *mm);
int amdgpu_amdkfd_submit_ib(struct kgd_dev *kgd, enum kgd_engine_type engine,
				uint32_t vmid, uint64_t gpu_addr,
				uint32_t *ib_cmd, uint32_t ib_len);
void amdgpu_amdkfd_set_compute_idle(struct kgd_dev *kgd, bool idle);
bool amdgpu_amdkfd_have_atomics_support(struct kgd_dev *kgd);
int amdgpu_amdkfd_flush_gpu_tlb_vmid(struct kgd_dev *kgd, uint16_t vmid);
int amdgpu_amdkfd_flush_gpu_tlb_pasid(struct kgd_dev *kgd, uint16_t pasid);

bool amdgpu_amdkfd_is_kfd_vmid(struct amdgpu_device *adev, u32 vmid);

int amdgpu_amdkfd_pre_reset(struct amdgpu_device *adev);

int amdgpu_amdkfd_post_reset(struct amdgpu_device *adev);

void amdgpu_amdkfd_gpu_reset(struct kgd_dev *kgd);

/* Shared API */
int amdgpu_amdkfd_alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
				void **mem_obj, uint64_t *gpu_addr,
				void **cpu_ptr, bool mqd_gfx9);
void amdgpu_amdkfd_free_gtt_mem(struct kgd_dev *kgd, void *mem_obj);
int amdgpu_amdkfd_alloc_gws(struct kgd_dev *kgd, size_t size, void **mem_obj);
void amdgpu_amdkfd_free_gws(struct kgd_dev *kgd, void *mem_obj);
int amdgpu_amdkfd_add_gws_to_process(void *info, void *gws, struct kgd_mem **mem);
int amdgpu_amdkfd_remove_gws_from_process(void *info, void *mem);
uint32_t amdgpu_amdkfd_get_fw_version(struct kgd_dev *kgd,
				      enum kgd_engine_type type);
void amdgpu_amdkfd_get_local_mem_info(struct kgd_dev *kgd,
				      struct kfd_local_mem_info *mem_info);
uint64_t amdgpu_amdkfd_get_gpu_clock_counter(struct kgd_dev *kgd);

uint32_t amdgpu_amdkfd_get_max_engine_clock_in_mhz(struct kgd_dev *kgd);
void amdgpu_amdkfd_get_cu_info(struct kgd_dev *kgd, struct kfd_cu_info *cu_info);
int amdgpu_amdkfd_get_dmabuf_info(struct kgd_dev *kgd, int dma_buf_fd,
				  struct kgd_dev **dmabuf_kgd,
				  uint64_t *bo_size, void *metadata_buffer,
				  size_t buffer_size, uint32_t *metadata_size,
				  uint32_t *flags);
uint64_t amdgpu_amdkfd_get_vram_usage(struct kgd_dev *kgd);
uint64_t amdgpu_amdkfd_get_hive_id(struct kgd_dev *kgd);
uint64_t amdgpu_amdkfd_get_unique_id(struct kgd_dev *kgd);
uint64_t amdgpu_amdkfd_get_mmio_remap_phys_addr(struct kgd_dev *kgd);
uint32_t amdgpu_amdkfd_get_num_gws(struct kgd_dev *kgd);
uint8_t amdgpu_amdkfd_get_xgmi_hops_count(struct kgd_dev *dst, struct kgd_dev *src);

/* Read user wptr from a specified user address space with page fault
 * disabled. The memory must be pinned and mapped to the hardware when
 * this is called in hqd_load functions, so it should never fault in
 * the first place. This resolves a circular lock dependency involving
 * four locks, including the DQM lock and mmap_sem.
 */
#define read_user_wptr(mmptr, wptr, dst)				\
	({								\
		bool valid = false;					\
		if ((mmptr) && (wptr)) {				\
			pagefault_disable();				\
			if ((mmptr) == current->mm) {			\
				valid = !get_user((dst), (wptr));	\
			} else if (current->mm == NULL) {		\
				use_mm(mmptr);				\
				valid = !get_user((dst), (wptr));	\
				unuse_mm(mmptr);			\
			}						\
			pagefault_enable();				\
		}							\
		valid;							\
	})

/* GPUVM API */
int amdgpu_amdkfd_gpuvm_create_process_vm(struct kgd_dev *kgd, unsigned int pasid,
					void **vm, void **process_info,
					struct dma_fence **ef);
int amdgpu_amdkfd_gpuvm_acquire_process_vm(struct kgd_dev *kgd,
					struct file *filp, unsigned int pasid,
					void **vm, void **process_info,
					struct dma_fence **ef);
void amdgpu_amdkfd_gpuvm_destroy_cb(struct amdgpu_device *adev,
				struct amdgpu_vm *vm);
void amdgpu_amdkfd_gpuvm_destroy_process_vm(struct kgd_dev *kgd, void *vm);
void amdgpu_amdkfd_gpuvm_release_process_vm(struct kgd_dev *kgd, void *vm);
uint64_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *vm);
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

int amdgpu_amdkfd_gpuvm_get_vm_fault_info(struct kgd_dev *kgd,
					      struct kfd_vm_fault_info *info);

int amdgpu_amdkfd_gpuvm_import_dmabuf(struct kgd_dev *kgd,
				      struct dma_buf *dmabuf,
				      uint64_t va, void *vm,
				      struct kgd_mem **mem, uint64_t *size,
				      uint64_t *mmap_offset);

void amdgpu_amdkfd_gpuvm_init_mem_limits(void);
void amdgpu_amdkfd_unreserve_memory_limit(struct amdgpu_bo *bo);

int amdgpu_amdkfd_get_tile_config(struct kgd_dev *kgd,
				struct tile_config *config);

/* KGD2KFD callbacks */
int kgd2kfd_init(void);
void kgd2kfd_exit(void);
struct kfd_dev *kgd2kfd_probe(struct kgd_dev *kgd, struct pci_dev *pdev,
			      unsigned int asic_type, bool vf);
bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 struct drm_device *ddev,
			 const struct kgd2kfd_shared_resources *gpu_resources);
void kgd2kfd_device_exit(struct kfd_dev *kfd);
void kgd2kfd_suspend(struct kfd_dev *kfd, bool run_pm);
int kgd2kfd_resume(struct kfd_dev *kfd, bool run_pm);
int kgd2kfd_pre_reset(struct kfd_dev *kfd);
int kgd2kfd_post_reset(struct kfd_dev *kfd);
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry);
int kgd2kfd_quiesce_mm(struct mm_struct *mm);
int kgd2kfd_resume_mm(struct mm_struct *mm);
int kgd2kfd_schedule_evict_and_restore_process(struct mm_struct *mm,
					       struct dma_fence *fence);
void kgd2kfd_set_sram_ecc_flag(struct kfd_dev *kfd);

#endif /* AMDGPU_AMDKFD_H_INCLUDED */
