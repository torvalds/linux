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

#include <linux/list.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/mmu_notifier.h>
#include <linux/memremap.h>
#include <kgd_kfd_interface.h>
#include "amdgpu_sync.h"
#include "amdgpu_vm.h"
#include "amdgpu_xcp.h"

extern uint64_t amdgpu_amdkfd_total_mem_size;

enum TLB_FLUSH_TYPE {
	TLB_FLUSH_LEGACY = 0,
	TLB_FLUSH_LIGHTWEIGHT,
	TLB_FLUSH_HEAVYWEIGHT
};

struct amdgpu_device;

enum kfd_mem_attachment_type {
	KFD_MEM_ATT_SHARED,	/* Share kgd_mem->bo or another attachment's */
	KFD_MEM_ATT_USERPTR,	/* SG bo to DMA map pages from a userptr bo */
	KFD_MEM_ATT_DMABUF,	/* DMAbuf to DMA map TTM BOs */
	KFD_MEM_ATT_SG		/* Tag to DMA map SG BOs */
};

struct kfd_mem_attachment {
	struct list_head list;
	enum kfd_mem_attachment_type type;
	bool is_mapped;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_device *adev;
	uint64_t va;
	uint64_t pte_flags;
};

struct kgd_mem {
	struct mutex lock;
	struct amdgpu_bo *bo;
	struct dma_buf *dmabuf;
	struct hmm_range *range;
	struct list_head attachments;
	/* protected by amdkfd_process_info.lock */
	struct list_head validate_list;
	uint32_t domain;
	unsigned int mapped_to_gpu_memory;
	uint64_t va;

	uint32_t alloc_flags;

	uint32_t invalid;
	struct amdkfd_process_info *process_info;

	struct amdgpu_sync sync;

	bool aql_queue;
	bool is_imported;
};

/* KFD Memory Eviction */
struct amdgpu_amdkfd_fence {
	struct dma_fence base;
	struct mm_struct *mm;
	spinlock_t lock;
	char timeline_name[TASK_COMM_LEN];
	struct svm_range_bo *svm_bo;
};

struct amdgpu_kfd_dev {
	struct kfd_dev *dev;
	int64_t vram_used[MAX_XCP];
	uint64_t vram_used_aligned[MAX_XCP];
	bool init_complete;
	struct work_struct reset_work;

	/* HMM page migration MEMORY_DEVICE_PRIVATE mapping */
	struct dev_pagemap pgmap;
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
	struct mutex notifier_lock;
	uint32_t evicted_bos;
	struct delayed_work restore_userptr_work;
	struct pid *pid;
	bool block_mmu_notifications;
};

int amdgpu_amdkfd_init(void);
void amdgpu_amdkfd_fini(void);

void amdgpu_amdkfd_suspend(struct amdgpu_device *adev, bool run_pm);
int amdgpu_amdkfd_resume(struct amdgpu_device *adev, bool run_pm);
void amdgpu_amdkfd_interrupt(struct amdgpu_device *adev,
			const void *ih_ring_entry);
void amdgpu_amdkfd_device_probe(struct amdgpu_device *adev);
void amdgpu_amdkfd_device_init(struct amdgpu_device *adev);
void amdgpu_amdkfd_device_fini_sw(struct amdgpu_device *adev);
int amdgpu_amdkfd_check_and_lock_kfd(struct amdgpu_device *adev);
void amdgpu_amdkfd_unlock_kfd(struct amdgpu_device *adev);
int amdgpu_amdkfd_submit_ib(struct amdgpu_device *adev,
				enum kgd_engine_type engine,
				uint32_t vmid, uint64_t gpu_addr,
				uint32_t *ib_cmd, uint32_t ib_len);
void amdgpu_amdkfd_set_compute_idle(struct amdgpu_device *adev, bool idle);
bool amdgpu_amdkfd_have_atomics_support(struct amdgpu_device *adev);
int amdgpu_amdkfd_flush_gpu_tlb_vmid(struct amdgpu_device *adev,
				uint16_t vmid);
int amdgpu_amdkfd_flush_gpu_tlb_pasid(struct amdgpu_device *adev,
				uint16_t pasid, enum TLB_FLUSH_TYPE flush_type,
				uint32_t inst);

bool amdgpu_amdkfd_is_kfd_vmid(struct amdgpu_device *adev, u32 vmid);

int amdgpu_amdkfd_pre_reset(struct amdgpu_device *adev);

int amdgpu_amdkfd_post_reset(struct amdgpu_device *adev);

void amdgpu_amdkfd_gpu_reset(struct amdgpu_device *adev);

int amdgpu_queue_mask_bit_to_set_resource_bit(struct amdgpu_device *adev,
					int queue_bit);

struct amdgpu_amdkfd_fence *amdgpu_amdkfd_fence_create(u64 context,
				struct mm_struct *mm,
				struct svm_range_bo *svm_bo);
#if defined(CONFIG_DEBUG_FS)
int kfd_debugfs_kfd_mem_limits(struct seq_file *m, void *data);
#endif
#if IS_ENABLED(CONFIG_HSA_AMD)
bool amdkfd_fence_check_mm(struct dma_fence *f, struct mm_struct *mm);
struct amdgpu_amdkfd_fence *to_amdgpu_amdkfd_fence(struct dma_fence *f);
int amdgpu_amdkfd_remove_fence_on_pt_pd_bos(struct amdgpu_bo *bo);
int amdgpu_amdkfd_evict_userptr(struct mmu_interval_notifier *mni,
				unsigned long cur_seq, struct kgd_mem *mem);
#else
static inline
bool amdkfd_fence_check_mm(struct dma_fence *f, struct mm_struct *mm)
{
	return false;
}

static inline
struct amdgpu_amdkfd_fence *to_amdgpu_amdkfd_fence(struct dma_fence *f)
{
	return NULL;
}

static inline
int amdgpu_amdkfd_remove_fence_on_pt_pd_bos(struct amdgpu_bo *bo)
{
	return 0;
}

static inline
int amdgpu_amdkfd_evict_userptr(struct mmu_interval_notifier *mni,
				unsigned long cur_seq, struct kgd_mem *mem)
{
	return 0;
}
#endif
/* Shared API */
int amdgpu_amdkfd_alloc_gtt_mem(struct amdgpu_device *adev, size_t size,
				void **mem_obj, uint64_t *gpu_addr,
				void **cpu_ptr, bool mqd_gfx9);
void amdgpu_amdkfd_free_gtt_mem(struct amdgpu_device *adev, void *mem_obj);
int amdgpu_amdkfd_alloc_gws(struct amdgpu_device *adev, size_t size,
				void **mem_obj);
void amdgpu_amdkfd_free_gws(struct amdgpu_device *adev, void *mem_obj);
int amdgpu_amdkfd_add_gws_to_process(void *info, void *gws, struct kgd_mem **mem);
int amdgpu_amdkfd_remove_gws_from_process(void *info, void *mem);
uint32_t amdgpu_amdkfd_get_fw_version(struct amdgpu_device *adev,
				      enum kgd_engine_type type);
void amdgpu_amdkfd_get_local_mem_info(struct amdgpu_device *adev,
				      struct kfd_local_mem_info *mem_info,
				      struct amdgpu_xcp *xcp);
uint64_t amdgpu_amdkfd_get_gpu_clock_counter(struct amdgpu_device *adev);

uint32_t amdgpu_amdkfd_get_max_engine_clock_in_mhz(struct amdgpu_device *adev);
int amdgpu_amdkfd_get_dmabuf_info(struct amdgpu_device *adev, int dma_buf_fd,
				  struct amdgpu_device **dmabuf_adev,
				  uint64_t *bo_size, void *metadata_buffer,
				  size_t buffer_size, uint32_t *metadata_size,
				  uint32_t *flags, int8_t *xcp_id);
uint8_t amdgpu_amdkfd_get_xgmi_hops_count(struct amdgpu_device *dst,
					  struct amdgpu_device *src);
int amdgpu_amdkfd_get_xgmi_bandwidth_mbytes(struct amdgpu_device *dst,
					    struct amdgpu_device *src,
					    bool is_min);
int amdgpu_amdkfd_get_pcie_bandwidth_mbytes(struct amdgpu_device *adev, bool is_min);
int amdgpu_amdkfd_send_close_event_drain_irq(struct amdgpu_device *adev,
					uint32_t *payload);
int amdgpu_amdkfd_unmap_hiq(struct amdgpu_device *adev, u32 doorbell_off,
				u32 inst);

/* Read user wptr from a specified user address space with page fault
 * disabled. The memory must be pinned and mapped to the hardware when
 * this is called in hqd_load functions, so it should never fault in
 * the first place. This resolves a circular lock dependency involving
 * four locks, including the DQM lock and mmap_lock.
 */
#define read_user_wptr(mmptr, wptr, dst)				\
	({								\
		bool valid = false;					\
		if ((mmptr) && (wptr)) {				\
			pagefault_disable();				\
			if ((mmptr) == current->mm) {			\
				valid = !get_user((dst), (wptr));	\
			} else if (current->flags & PF_KTHREAD) {	\
				kthread_use_mm(mmptr);			\
				valid = !get_user((dst), (wptr));	\
				kthread_unuse_mm(mmptr);		\
			}						\
			pagefault_enable();				\
		}							\
		valid;							\
	})

/* GPUVM API */
#define drm_priv_to_vm(drm_priv)					\
	(&((struct amdgpu_fpriv *)					\
		((struct drm_file *)(drm_priv))->driver_priv)->vm)

int amdgpu_amdkfd_gpuvm_set_vm_pasid(struct amdgpu_device *adev,
				     struct amdgpu_vm *avm, u32 pasid);
int amdgpu_amdkfd_gpuvm_acquire_process_vm(struct amdgpu_device *adev,
					struct amdgpu_vm *avm,
					void **process_info,
					struct dma_fence **ef);
void amdgpu_amdkfd_gpuvm_release_process_vm(struct amdgpu_device *adev,
					void *drm_priv);
uint64_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *drm_priv);
size_t amdgpu_amdkfd_get_available_memory(struct amdgpu_device *adev,
					uint8_t xcp_id);
int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct amdgpu_device *adev, uint64_t va, uint64_t size,
		void *drm_priv, struct kgd_mem **mem,
		uint64_t *offset, uint32_t flags, bool criu_resume);
int amdgpu_amdkfd_gpuvm_free_memory_of_gpu(
		struct amdgpu_device *adev, struct kgd_mem *mem, void *drm_priv,
		uint64_t *size);
int amdgpu_amdkfd_gpuvm_map_memory_to_gpu(struct amdgpu_device *adev,
					  struct kgd_mem *mem, void *drm_priv);
int amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
		struct amdgpu_device *adev, struct kgd_mem *mem, void *drm_priv);
void amdgpu_amdkfd_gpuvm_dmaunmap_mem(struct kgd_mem *mem, void *drm_priv);
int amdgpu_amdkfd_gpuvm_sync_memory(
		struct amdgpu_device *adev, struct kgd_mem *mem, bool intr);
int amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(struct kgd_mem *mem,
					     void **kptr, uint64_t *size);
void amdgpu_amdkfd_gpuvm_unmap_gtt_bo_from_kernel(struct kgd_mem *mem);

int amdgpu_amdkfd_map_gtt_bo_to_gart(struct amdgpu_device *adev, struct amdgpu_bo *bo);

int amdgpu_amdkfd_gpuvm_restore_process_bos(void *process_info,
					    struct dma_fence **ef);
int amdgpu_amdkfd_gpuvm_get_vm_fault_info(struct amdgpu_device *adev,
					      struct kfd_vm_fault_info *info);
int amdgpu_amdkfd_gpuvm_import_dmabuf(struct amdgpu_device *adev,
				      struct dma_buf *dmabuf,
				      uint64_t va, void *drm_priv,
				      struct kgd_mem **mem, uint64_t *size,
				      uint64_t *mmap_offset);
int amdgpu_amdkfd_gpuvm_export_dmabuf(struct kgd_mem *mem,
				      struct dma_buf **dmabuf);
void amdgpu_amdkfd_debug_mem_fence(struct amdgpu_device *adev);
int amdgpu_amdkfd_get_tile_config(struct amdgpu_device *adev,
				struct tile_config *config);
void amdgpu_amdkfd_ras_poison_consumption_handler(struct amdgpu_device *adev,
				bool reset);
bool amdgpu_amdkfd_bo_mapped_to_dev(struct amdgpu_device *adev, struct kgd_mem *mem);
void amdgpu_amdkfd_block_mmu_notifications(void *p);
int amdgpu_amdkfd_criu_resume(void *p);
bool amdgpu_amdkfd_ras_query_utcl2_poison_status(struct amdgpu_device *adev);
int amdgpu_amdkfd_reserve_mem_limit(struct amdgpu_device *adev,
		uint64_t size, u32 alloc_flag, int8_t xcp_id);
void amdgpu_amdkfd_unreserve_mem_limit(struct amdgpu_device *adev,
		uint64_t size, u32 alloc_flag, int8_t xcp_id);

u64 amdgpu_amdkfd_xcp_memory_size(struct amdgpu_device *adev, int xcp_id);

#define KFD_XCP_MEM_ID(adev, xcp_id) \
		((adev)->xcp_mgr && (xcp_id) >= 0 ?\
		(adev)->xcp_mgr->xcp[(xcp_id)].mem_id : -1)

#define KFD_XCP_MEMORY_SIZE(adev, xcp_id) amdgpu_amdkfd_xcp_memory_size((adev), (xcp_id))


#if IS_ENABLED(CONFIG_HSA_AMD)
void amdgpu_amdkfd_gpuvm_init_mem_limits(void);
void amdgpu_amdkfd_gpuvm_destroy_cb(struct amdgpu_device *adev,
				struct amdgpu_vm *vm);

/**
 * @amdgpu_amdkfd_release_notify() - Notify KFD when GEM object is released
 *
 * Allows KFD to release its resources associated with the GEM object.
 */
void amdgpu_amdkfd_release_notify(struct amdgpu_bo *bo);
void amdgpu_amdkfd_reserve_system_mem(uint64_t size);
#else
static inline
void amdgpu_amdkfd_gpuvm_init_mem_limits(void)
{
}

static inline
void amdgpu_amdkfd_gpuvm_destroy_cb(struct amdgpu_device *adev,
					struct amdgpu_vm *vm)
{
}

static inline
void amdgpu_amdkfd_release_notify(struct amdgpu_bo *bo)
{
}
#endif

#if IS_ENABLED(CONFIG_HSA_AMD_SVM)
int kgd2kfd_init_zone_device(struct amdgpu_device *adev);
#else
static inline
int kgd2kfd_init_zone_device(struct amdgpu_device *adev)
{
	return 0;
}
#endif

/* KGD2KFD callbacks */
int kgd2kfd_quiesce_mm(struct mm_struct *mm, uint32_t trigger);
int kgd2kfd_resume_mm(struct mm_struct *mm);
int kgd2kfd_schedule_evict_and_restore_process(struct mm_struct *mm,
						struct dma_fence *fence);
#if IS_ENABLED(CONFIG_HSA_AMD)
int kgd2kfd_init(void);
void kgd2kfd_exit(void);
struct kfd_dev *kgd2kfd_probe(struct amdgpu_device *adev, bool vf);
bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 const struct kgd2kfd_shared_resources *gpu_resources);
void kgd2kfd_device_exit(struct kfd_dev *kfd);
void kgd2kfd_suspend(struct kfd_dev *kfd, bool run_pm);
int kgd2kfd_resume(struct kfd_dev *kfd, bool run_pm);
int kgd2kfd_pre_reset(struct kfd_dev *kfd);
int kgd2kfd_post_reset(struct kfd_dev *kfd);
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry);
void kgd2kfd_set_sram_ecc_flag(struct kfd_dev *kfd);
void kgd2kfd_smi_event_throttle(struct kfd_dev *kfd, uint64_t throttle_bitmask);
int kgd2kfd_check_and_lock_kfd(void);
void kgd2kfd_unlock_kfd(void);
#else
static inline int kgd2kfd_init(void)
{
	return -ENOENT;
}

static inline void kgd2kfd_exit(void)
{
}

static inline
struct kfd_dev *kgd2kfd_probe(struct amdgpu_device *adev, bool vf)
{
	return NULL;
}

static inline
bool kgd2kfd_device_init(struct kfd_dev *kfd,
				const struct kgd2kfd_shared_resources *gpu_resources)
{
	return false;
}

static inline void kgd2kfd_device_exit(struct kfd_dev *kfd)
{
}

static inline void kgd2kfd_suspend(struct kfd_dev *kfd, bool run_pm)
{
}

static inline int kgd2kfd_resume(struct kfd_dev *kfd, bool run_pm)
{
	return 0;
}

static inline int kgd2kfd_pre_reset(struct kfd_dev *kfd)
{
	return 0;
}

static inline int kgd2kfd_post_reset(struct kfd_dev *kfd)
{
	return 0;
}

static inline
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry)
{
}

static inline
void kgd2kfd_set_sram_ecc_flag(struct kfd_dev *kfd)
{
}

static inline
void kgd2kfd_smi_event_throttle(struct kfd_dev *kfd, uint64_t throttle_bitmask)
{
}

static inline int kgd2kfd_check_and_lock_kfd(void)
{
	return 0;
}

static inline void kgd2kfd_unlock_kfd(void)
{
}
#endif
#endif /* AMDGPU_AMDKFD_H_INCLUDED */
