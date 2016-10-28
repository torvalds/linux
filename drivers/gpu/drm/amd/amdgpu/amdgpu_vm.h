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
#define AMDGPU_VM_PTE_COUNT (1 << amdgpu_vm_block_size)

/* PTBs (Page Table Blocks) need to be aligned to 32K */
#define AMDGPU_VM_PTB_ALIGN_SIZE   32768

/* LOG2 number of continuous pages for the fragment field */
#define AMDGPU_LOG2_PAGES_PER_FRAG 4

#define AMDGPU_PTE_VALID	(1 << 0)
#define AMDGPU_PTE_SYSTEM	(1 << 1)
#define AMDGPU_PTE_SNOOPED	(1 << 2)

/* VI only */
#define AMDGPU_PTE_EXECUTABLE	(1 << 4)

#define AMDGPU_PTE_READABLE	(1 << 5)
#define AMDGPU_PTE_WRITEABLE	(1 << 6)

#define AMDGPU_PTE_FRAG(x)	((x & 0x1f) << 7)

/* How to programm VM fault handling */
#define AMDGPU_VM_FAULT_STOP_NEVER	0
#define AMDGPU_VM_FAULT_STOP_FIRST	1
#define AMDGPU_VM_FAULT_STOP_ALWAYS	2

struct amdgpu_vm_pt {
	struct amdgpu_bo	*bo;
	uint64_t		addr;
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
	struct amdgpu_bo	*page_directory;
	unsigned		max_pde_used;
	struct fence		*page_directory_fence;
	uint64_t		last_eviction_counter;

	/* array of page tables, one for each page directory entry */
	struct amdgpu_vm_pt	*page_tables;

	/* for id and flush management per ring */
	struct amdgpu_vm_id	*ids[AMDGPU_MAX_RINGS];

	/* protecting freed */
	spinlock_t		freed_lock;

	/* Scheduler entity for page table updates */
	struct amd_sched_entity	entity;

	/* client id */
	u64                     client_id;
};

struct amdgpu_vm_id {
	struct list_head	list;
	struct fence		*first;
	struct amdgpu_sync	active;
	struct fence		*last_flush;
	atomic64_t		owner;

	uint64_t		pd_gpu_addr;
	/* last flushed PD/PT update */
	struct fence		*flushed_updates;

	uint32_t                current_gpu_reset_count;

	uint32_t		gds_base;
	uint32_t		gds_size;
	uint32_t		gws_base;
	uint32_t		gws_size;
	uint32_t		oa_base;
	uint32_t		oa_size;
};

struct amdgpu_vm_manager {
	/* Handling of VMIDs */
	struct mutex				lock;
	unsigned				num_ids;
	struct list_head			ids_lru;
	struct amdgpu_vm_id			ids[AMDGPU_NUM_VM];

	/* Handling of VM fences */
	u64					fence_context;
	unsigned				seqno[AMDGPU_MAX_RINGS];

	uint32_t				max_pfn;
	/* vram base address for page table entry  */
	u64					vram_base_offset;
	/* is vm enabled? */
	bool					enabled;
	/* vm pte handling */
	const struct amdgpu_vm_pte_funcs        *vm_pte_funcs;
	struct amdgpu_ring                      *vm_pte_rings[AMDGPU_MAX_RINGS];
	unsigned				vm_pte_num_rings;
	atomic_t				vm_pte_next_ring;
	/* client id counter */
	atomic64_t				client_counter;
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
int amdgpu_vm_grab_id(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		      struct amdgpu_sync *sync, struct fence *fence,
		      struct amdgpu_job *job);
int amdgpu_vm_flush(struct amdgpu_ring *ring, struct amdgpu_job *job);
void amdgpu_vm_reset_id(struct amdgpu_device *adev, unsigned vm_id);
int amdgpu_vm_update_page_directory(struct amdgpu_device *adev,
				    struct amdgpu_vm *vm);
int amdgpu_vm_clear_freed(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm);
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
		     uint64_t size, uint32_t flags);
int amdgpu_vm_bo_unmap(struct amdgpu_device *adev,
		       struct amdgpu_bo_va *bo_va,
		       uint64_t addr);
void amdgpu_vm_bo_rmv(struct amdgpu_device *adev,
		      struct amdgpu_bo_va *bo_va);

#endif
