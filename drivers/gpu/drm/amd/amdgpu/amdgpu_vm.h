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
 * Authors: Christian König
 */
#ifndef __AMDGPU_VM_H__
#define __AMDGPU_VM_H__

#include <linux/rbtree.h>

#include "gpu_scheduler.h"
#include "amdgpu_sync.h"
#include "amdgpu_ring.h"

struct amdgpu_bo_va;
struct amdgpu_job;
struct amdgpu_bo_list_entry;

/*
 * GPUVM handling
 */

/* maximum number of VMIDs */
#define AMDGPU_NUM_VM	16

/* Maximum number of PTEs the hardware can write with one command */
#define AMDGPU_VM_MAX_UPDATE_SIZE	0x3FFFF

/* number of entries in page table */
#define AMDGPU_VM_PTE_COUNT(adev) (1 << (adev)->vm_manager.block_size)

/* PTBs (Page Table Blocks) need to be aligned to 32K */
#define AMDGPU_VM_PTB_ALIGN_SIZE   32768

/* LOG2 number of continuous pages for the fragment field */
#define AMDGPU_LOG2_PAGES_PER_FRAG 4

#define AMDGPU_PTE_VALID	(1ULL << 0)
#define AMDGPU_PTE_SYSTEM	(1ULL << 1)
#define AMDGPU_PTE_SNOOPED	(1ULL << 2)

/* VI only */
#define AMDGPU_PTE_EXECUTABLE	(1ULL << 4)

#define AMDGPU_PTE_READABLE	(1ULL << 5)
#define AMDGPU_PTE_WRITEABLE	(1ULL << 6)

#define AMDGPU_PTE_FRAG(x)	((x & 0x1fULL) << 7)

/* TILED for VEGA10, reserved for older ASICs  */
#define AMDGPU_PTE_PRT		(1ULL << 51)

/* VEGA10 only */
#define AMDGPU_PTE_MTYPE(a)    ((uint64_t)a << 57)
#define AMDGPU_PTE_MTYPE_MASK	AMDGPU_PTE_MTYPE(3ULL)

/* How to programm VM fault handling */
#define AMDGPU_VM_FAULT_STOP_NEVER	0
#define AMDGPU_VM_FAULT_STOP_FIRST	1
#define AMDGPU_VM_FAULT_STOP_ALWAYS	2

/* max number of VMHUB */
#define AMDGPU_MAX_VMHUBS			2
#define AMDGPU_GFXHUB				0
#define AMDGPU_MMHUB				1

/* hardcode that limit for now */
#define AMDGPU_VA_RESERVED_SIZE			(8 << 20)
/* max vmids dedicated for process */
#define AMDGPU_VM_MAX_RESERVED_VMID	1

struct amdgpu_vm_pt {
	struct amdgpu_bo	*bo;
	uint64_t		addr;

	/* array of page tables, one for each directory entry */
	struct amdgpu_vm_pt	*entries;
	unsigned		last_entry_used;
};

struct amdgpu_vm {
	/* tree of virtual addresses mapped */
	struct rb_root		va;

	/* protecting invalidated */
	spinlock_t		status_lock;

	/* BOs moved, but not yet updated in the PT */
	struct list_head	invalidated;

	/* BOs cleared in the PT because of a move */
	struct list_head	cleared;

	/* BO mappings freed, but not yet updated in the PT */
	struct list_head	freed;

	/* contains the page directory */
	struct amdgpu_vm_pt     root;
	struct dma_fence	*last_dir_update;
	uint64_t		last_eviction_counter;

	/* protecting freed */
	spinlock_t		freed_lock;

	/* Scheduler entity for page table updates */
	struct amd_sched_entity	entity;

	/* client id */
	u64                     client_id;
	/* dedicated to vm */
	struct amdgpu_vm_id	*reserved_vmid[AMDGPU_MAX_VMHUBS];
	/* each VM will map on CSA */
	struct amdgpu_bo_va *csa_bo_va;
};

struct amdgpu_vm_id {
	struct list_head	list;
	struct amdgpu_sync	active;
	struct dma_fence		*last_flush;
	atomic64_t		owner;

	uint64_t		pd_gpu_addr;
	/* last flushed PD/PT update */
	struct dma_fence		*flushed_updates;

	uint32_t                current_gpu_reset_count;

	uint32_t		gds_base;
	uint32_t		gds_size;
	uint32_t		gws_base;
	uint32_t		gws_size;
	uint32_t		oa_base;
	uint32_t		oa_size;
};

struct amdgpu_vm_id_manager {
	struct mutex		lock;
	unsigned		num_ids;
	struct list_head	ids_lru;
	struct amdgpu_vm_id	ids[AMDGPU_NUM_VM];
	atomic_t		reserved_vmid_num;
};

struct amdgpu_vm_manager {
	/* Handling of VMIDs */
	struct amdgpu_vm_id_manager		id_mgr[AMDGPU_MAX_VMHUBS];

	/* Handling of VM fences */
	u64					fence_context;
	unsigned				seqno[AMDGPU_MAX_RINGS];

	uint64_t				max_pfn;
	uint32_t				num_level;
	uint64_t				vm_size;
	uint32_t				block_size;
	/* vram base address for page table entry  */
	u64					vram_base_offset;
	/* vm pte handling */
	const struct amdgpu_vm_pte_funcs        *vm_pte_funcs;
	struct amdgpu_ring                      *vm_pte_rings[AMDGPU_MAX_RINGS];
	unsigned				vm_pte_num_rings;
	atomic_t				vm_pte_next_ring;
	/* client id counter */
	atomic64_t				client_counter;

	/* partial resident texture handling */
	spinlock_t				prt_lock;
	atomic_t				num_prt_users;
};

void amdgpu_vm_manager_init(struct amdgpu_device *adev);
void amdgpu_vm_manager_fini(struct amdgpu_device *adev);
int amdgpu_vm_init(struct amdgpu_device *adev, struct amdgpu_vm *vm);
void amdgpu_vm_fini(struct amdgpu_device *adev, struct amdgpu_vm *vm);
void amdgpu_vm_get_pd_bo(struct amdgpu_vm *vm,
			 struct list_head *validated,
			 struct amdgpu_bo_list_entry *entry);
int amdgpu_vm_validate_pt_bos(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			      int (*callback)(void *p, struct amdgpu_bo *bo),
			      void *param);
void amdgpu_vm_move_pt_bos_in_lru(struct amdgpu_device *adev,
				  struct amdgpu_vm *vm);
int amdgpu_vm_alloc_pts(struct amdgpu_device *adev,
			struct amdgpu_vm *vm,
			uint64_t saddr, uint64_t size);
int amdgpu_vm_grab_id(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		      struct amdgpu_sync *sync, struct dma_fence *fence,
		      struct amdgpu_job *job);
int amdgpu_vm_flush(struct amdgpu_ring *ring, struct amdgpu_job *job);
void amdgpu_vm_reset_id(struct amdgpu_device *adev, unsigned vmhub,
			unsigned vmid);
void amdgpu_vm_reset_all_ids(struct amdgpu_device *adev);
int amdgpu_vm_update_directories(struct amdgpu_device *adev,
				 struct amdgpu_vm *vm);
int amdgpu_vm_clear_freed(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm,
			  struct dma_fence **fence);
int amdgpu_vm_clear_invalids(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			     struct amdgpu_sync *sync);
int amdgpu_vm_bo_update(struct amdgpu_device *adev,
			struct amdgpu_bo_va *bo_va,
			bool clear);
void amdgpu_vm_bo_invalidate(struct amdgpu_device *adev,
			     struct amdgpu_bo *bo);
struct amdgpu_bo_va *amdgpu_vm_bo_find(struct amdgpu_vm *vm,
				       struct amdgpu_bo *bo);
struct amdgpu_bo_va *amdgpu_vm_bo_add(struct amdgpu_device *adev,
				      struct amdgpu_vm *vm,
				      struct amdgpu_bo *bo);
int amdgpu_vm_bo_map(struct amdgpu_device *adev,
		     struct amdgpu_bo_va *bo_va,
		     uint64_t addr, uint64_t offset,
		     uint64_t size, uint64_t flags);
int amdgpu_vm_bo_replace_map(struct amdgpu_device *adev,
			     struct amdgpu_bo_va *bo_va,
			     uint64_t addr, uint64_t offset,
			     uint64_t size, uint64_t flags);
int amdgpu_vm_bo_unmap(struct amdgpu_device *adev,
		       struct amdgpu_bo_va *bo_va,
		       uint64_t addr);
int amdgpu_vm_bo_clear_mappings(struct amdgpu_device *adev,
				struct amdgpu_vm *vm,
				uint64_t saddr, uint64_t size);
void amdgpu_vm_bo_rmv(struct amdgpu_device *adev,
		      struct amdgpu_bo_va *bo_va);
void amdgpu_vm_adjust_size(struct amdgpu_device *adev, uint64_t vm_size);
int amdgpu_vm_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
bool amdgpu_vm_need_pipeline_sync(struct amdgpu_ring *ring,
				  struct amdgpu_job *job);

#endif
