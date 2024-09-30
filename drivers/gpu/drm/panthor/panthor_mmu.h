/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */

#ifndef __PANTHOR_MMU_H__
#define __PANTHOR_MMU_H__

#include <linux/dma-resv.h>

struct drm_exec;
struct drm_sched_job;
struct panthor_gem_object;
struct panthor_heap_pool;
struct panthor_vm;
struct panthor_vma;
struct panthor_mmu;

int panthor_mmu_init(struct panthor_device *ptdev);
void panthor_mmu_unplug(struct panthor_device *ptdev);
void panthor_mmu_pre_reset(struct panthor_device *ptdev);
void panthor_mmu_post_reset(struct panthor_device *ptdev);
void panthor_mmu_suspend(struct panthor_device *ptdev);
void panthor_mmu_resume(struct panthor_device *ptdev);

int panthor_vm_map_bo_range(struct panthor_vm *vm, struct panthor_gem_object *bo,
			    u64 offset, u64 size, u64 va, u32 flags);
int panthor_vm_unmap_range(struct panthor_vm *vm, u64 va, u64 size);
struct panthor_gem_object *
panthor_vm_get_bo_for_va(struct panthor_vm *vm, u64 va, u64 *bo_offset);

int panthor_vm_active(struct panthor_vm *vm);
void panthor_vm_idle(struct panthor_vm *vm);
int panthor_vm_as(struct panthor_vm *vm);
int panthor_vm_flush_all(struct panthor_vm *vm);

struct panthor_heap_pool *
panthor_vm_get_heap_pool(struct panthor_vm *vm, bool create);

struct panthor_vm *panthor_vm_get(struct panthor_vm *vm);
void panthor_vm_put(struct panthor_vm *vm);
struct panthor_vm *panthor_vm_create(struct panthor_device *ptdev, bool for_mcu,
				     u64 kernel_va_start, u64 kernel_va_size,
				     u64 kernel_auto_va_start,
				     u64 kernel_auto_va_size);

int panthor_vm_prepare_mapped_bos_resvs(struct drm_exec *exec,
					struct panthor_vm *vm,
					u32 slot_count);
int panthor_vm_add_bos_resvs_deps_to_job(struct panthor_vm *vm,
					 struct drm_sched_job *job);
void panthor_vm_add_job_fence_to_bos_resvs(struct panthor_vm *vm,
					   struct drm_sched_job *job);

struct dma_resv *panthor_vm_resv(struct panthor_vm *vm);
struct drm_gem_object *panthor_vm_root_gem(struct panthor_vm *vm);

void panthor_vm_pool_destroy(struct panthor_file *pfile);
int panthor_vm_pool_create(struct panthor_file *pfile);
int panthor_vm_pool_create_vm(struct panthor_device *ptdev,
			      struct panthor_vm_pool *pool,
			      struct drm_panthor_vm_create *args);
int panthor_vm_pool_destroy_vm(struct panthor_vm_pool *pool, u32 handle);
struct panthor_vm *panthor_vm_pool_get_vm(struct panthor_vm_pool *pool, u32 handle);

bool panthor_vm_has_unhandled_faults(struct panthor_vm *vm);
bool panthor_vm_is_unusable(struct panthor_vm *vm);

/*
 * PANTHOR_VM_KERNEL_AUTO_VA: Use this magic address when you want the GEM
 * logic to auto-allocate the virtual address in the reserved kernel VA range.
 */
#define PANTHOR_VM_KERNEL_AUTO_VA		~0ull

int panthor_vm_alloc_va(struct panthor_vm *vm, u64 va, u64 size,
			struct drm_mm_node *va_node);
void panthor_vm_free_va(struct panthor_vm *vm, struct drm_mm_node *va_node);

int panthor_vm_bind_exec_sync_op(struct drm_file *file,
				 struct panthor_vm *vm,
				 struct drm_panthor_vm_bind_op *op);

struct drm_sched_job *
panthor_vm_bind_job_create(struct drm_file *file,
			   struct panthor_vm *vm,
			   const struct drm_panthor_vm_bind_op *op);
void panthor_vm_bind_job_put(struct drm_sched_job *job);
int panthor_vm_bind_job_prepare_resvs(struct drm_exec *exec,
				      struct drm_sched_job *job);
void panthor_vm_bind_job_update_resvs(struct drm_exec *exec, struct drm_sched_job *job);

void panthor_vm_update_resvs(struct panthor_vm *vm, struct drm_exec *exec,
			     struct dma_fence *fence,
			     enum dma_resv_usage private_usage,
			     enum dma_resv_usage extobj_usage);

int panthor_mmu_pt_cache_init(void);
void panthor_mmu_pt_cache_fini(void);

#ifdef CONFIG_DEBUG_FS
void panthor_mmu_debugfs_init(struct drm_minor *minor);
#endif

#endif
