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

#include <linux/idr.h>
#include <linux/kfifo.h>
#include <linux/rbtree.h>
#include <drm/gpu_scheduler.h>
#include <drm/drm_file.h>

#include "amdgpu_sync.h"
#include "amdgpu_ring.h"
#include "amdgpu_ids.h"

struct amdgpu_bo_va;
struct amdgpu_job;
struct amdgpu_bo_list_entry;

/*
 * GPUVM handling
 */

/* Maximum number of PTEs the hardware can write with one command */
#define AMDGPU_VM_MAX_UPDATE_SIZE	0x3FFFF

/* number of entries in page table */
#define AMDGPU_VM_PTE_COUNT(adev) (1 << (adev)->vm_manager.block_size)

/* PTBs (Page Table Blocks) need to be aligned to 32K */
#define AMDGPU_VM_PTB_ALIGN_SIZE   32768

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

/* PDE is handled as PTE for VEGA10 */
#define AMDGPU_PDE_PTE		(1ULL << 54)

/* PTE is handled as PDE for VEGA10 (Translate Further) */
#define AMDGPU_PTE_TF		(1ULL << 56)

/* PDE Block Fragment Size for VEGA10 */
#define AMDGPU_PDE_BFS(a)	((uint64_t)a << 59)


/* For GFX9 */
#define AMDGPU_PTE_MTYPE(a)    ((uint64_t)a << 57)
#define AMDGPU_PTE_MTYPE_MASK	AMDGPU_PTE_MTYPE(3ULL)

#define AMDGPU_MTYPE_NC 0
#define AMDGPU_MTYPE_CC 2

#define AMDGPU_PTE_DEFAULT_ATC  (AMDGPU_PTE_SYSTEM      \
                                | AMDGPU_PTE_SNOOPED    \
                                | AMDGPU_PTE_EXECUTABLE \
                                | AMDGPU_PTE_READABLE   \
                                | AMDGPU_PTE_WRITEABLE  \
                                | AMDGPU_PTE_MTYPE(AMDGPU_MTYPE_CC))

/* How to programm VM fault handling */
#define AMDGPU_VM_FAULT_STOP_NEVER	0
#define AMDGPU_VM_FAULT_STOP_FIRST	1
#define AMDGPU_VM_FAULT_STOP_ALWAYS	2

/* max number of VMHUB */
#define AMDGPU_MAX_VMHUBS			2
#define AMDGPU_GFXHUB				0
#define AMDGPU_MMHUB				1

/* hardcode that limit for now */
#define AMDGPU_VA_RESERVED_SIZE			(1ULL << 20)

/* VA hole for 48bit addresses on Vega10 */
#define AMDGPU_VA_HOLE_START			0x0000800000000000ULL
#define AMDGPU_VA_HOLE_END			0xffff800000000000ULL

/*
 * Hardware is programmed as if the hole doesn't exists with start and end
 * address values.
 *
 * This mask is used to remove the upper 16bits of the VA and so come up with
 * the linear addr value.
 */
#define AMDGPU_VA_HOLE_MASK			0x0000ffffffffffffULL

/* max vmids dedicated for process */
#define AMDGPU_VM_MAX_RESERVED_VMID	1

#define AMDGPU_VM_CONTEXT_GFX 0
#define AMDGPU_VM_CONTEXT_COMPUTE 1

/* See vm_update_mode */
#define AMDGPU_VM_USE_CPU_FOR_GFX (1 << 0)
#define AMDGPU_VM_USE_CPU_FOR_COMPUTE (1 << 1)

/* VMPT level enumerate, and the hiberachy is:
 * PDB2->PDB1->PDB0->PTB
 */
enum amdgpu_vm_level {
	AMDGPU_VM_PDB2,
	AMDGPU_VM_PDB1,
	AMDGPU_VM_PDB0,
	AMDGPU_VM_PTB
};

/* base structure for tracking BO usage in a VM */
struct amdgpu_vm_bo_base {
	/* constant after initialization */
	struct amdgpu_vm		*vm;
	struct amdgpu_bo		*bo;

	/* protected by bo being reserved */
	struct list_head		bo_list;

	/* protected by spinlock */
	struct list_head		vm_status;

	/* protected by the BO being reserved */
	bool				moved;
};

struct amdgpu_vm_pt {
	struct amdgpu_vm_bo_base	base;
	bool				huge;

	/* array of page tables, one for each directory entry */
	struct amdgpu_vm_pt		*entries;
};

#define AMDGPU_VM_FAULT(pasid, addr) (((u64)(pasid) << 48) | (addr))
#define AMDGPU_VM_FAULT_PASID(fault) ((u64)(fault) >> 48)
#define AMDGPU_VM_FAULT_ADDR(fault)  ((u64)(fault) & 0xfffffffff000ULL)

struct amdgpu_vm {
	/* tree of virtual addresses mapped */
	struct rb_root_cached	va;

	/* BOs who needs a validation */
	struct list_head	evicted;

	/* PT BOs which relocated and their parent need an update */
	struct list_head	relocated;

	/* BOs moved, but not yet updated in the PT */
	struct list_head	moved;
	spinlock_t		moved_lock;

	/* All BOs of this VM not currently in the state machine */
	struct list_head	idle;

	/* BO mappings freed, but not yet updated in the PT */
	struct list_head	freed;

	/* contains the page directory */
	struct amdgpu_vm_pt     root;
	struct dma_fence	*last_update;

	/* Scheduler entity for page table updates */
	struct drm_sched_entity	entity;

	unsigned int		pasid;
	/* dedicated to vm */
	struct amdgpu_vmid	*reserved_vmid[AMDGPU_MAX_VMHUBS];

	/* Flag to indicate if VM tables are updated by CPU or GPU (SDMA) */
	bool                    use_cpu_for_update;

	/* Flag to indicate ATS support from PTE for GFX9 */
	bool			pte_support_ats;

	/* Up to 128 pending retry page faults */
	DECLARE_KFIFO(faults, u64, 128);

	/* Limit non-retry fault storms */
	unsigned int		fault_credit;

	/* Points to the KFD process VM info */
	struct amdkfd_process_info *process_info;

	/* List node in amdkfd_process_info.vm_list_head */
	struct list_head	vm_list_node;

	/* Valid while the PD is reserved or fenced */
	uint64_t		pd_phys_addr;
};

struct amdgpu_vm_manager {
	/* Handling of VMIDs */
	struct amdgpu_vmid_mgr			id_mgr[AMDGPU_MAX_VMHUBS];

	/* Handling of VM fences */
	u64					fence_context;
	unsigned				seqno[AMDGPU_MAX_RINGS];

	uint64_t				max_pfn;
	uint32_t				num_level;
	uint32_t				block_size;
	uint32_t				fragment_size;
	enum amdgpu_vm_level			root_level;
	/* vram base address for page table entry  */
	u64					vram_base_offset;
	/* vm pte handling */
	const struct amdgpu_vm_pte_funcs        *vm_pte_funcs;
	struct amdgpu_ring                      *vm_pte_rings[AMDGPU_MAX_RINGS];
	unsigned				vm_pte_num_rings;
	atomic_t				vm_pte_next_ring;

	/* partial resident texture handling */
	spinlock_t				prt_lock;
	atomic_t				num_prt_users;

	/* controls how VM page tables are updated for Graphics and Compute.
	 * BIT0[= 0] Graphics updated by SDMA [= 1] by CPU
	 * BIT1[= 0] Compute updated by SDMA [= 1] by CPU
	 */
	int					vm_update_mode;

	/* PASID to VM mapping, will be used in interrupt context to
	 * look up VM of a page fault
	 */
	struct idr				pasid_idr;
	spinlock_t				pasid_lock;
};

void amdgpu_vm_manager_init(struct amdgpu_device *adev);
void amdgpu_vm_manager_fini(struct amdgpu_device *adev);
int amdgpu_vm_init(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		   int vm_context, unsigned int pasid);
int amdgpu_vm_make_compute(struct amdgpu_device *adev, struct amdgpu_vm *vm);
void amdgpu_vm_fini(struct amdgpu_device *adev, struct amdgpu_vm *vm);
bool amdgpu_vm_pasid_fault_credit(struct amdgpu_device *adev,
				  unsigned int pasid);
void amdgpu_vm_get_pd_bo(struct amdgpu_vm *vm,
			 struct list_head *validated,
			 struct amdgpu_bo_list_entry *entry);
bool amdgpu_vm_ready(struct amdgpu_vm *vm);
int amdgpu_vm_validate_pt_bos(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			      int (*callback)(void *p, struct amdgpu_bo *bo),
			      void *param);
int amdgpu_vm_alloc_pts(struct amdgpu_device *adev,
			struct amdgpu_vm *vm,
			uint64_t saddr, uint64_t size);
int amdgpu_vm_flush(struct amdgpu_ring *ring, struct amdgpu_job *job, bool need_pipe_sync);
int amdgpu_vm_update_directories(struct amdgpu_device *adev,
				 struct amdgpu_vm *vm);
int amdgpu_vm_clear_freed(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm,
			  struct dma_fence **fence);
int amdgpu_vm_handle_moved(struct amdgpu_device *adev,
			   struct amdgpu_vm *vm);
int amdgpu_vm_bo_update(struct amdgpu_device *adev,
			struct amdgpu_bo_va *bo_va,
			bool clear);
void amdgpu_vm_bo_invalidate(struct amdgpu_device *adev,
			     struct amdgpu_bo *bo, bool evicted);
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
struct amdgpu_bo_va_mapping *amdgpu_vm_bo_lookup_mapping(struct amdgpu_vm *vm,
							 uint64_t addr);
void amdgpu_vm_bo_rmv(struct amdgpu_device *adev,
		      struct amdgpu_bo_va *bo_va);
void amdgpu_vm_adjust_size(struct amdgpu_device *adev, uint32_t vm_size,
			   uint32_t fragment_size_default, unsigned max_level,
			   unsigned max_bits);
int amdgpu_vm_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
bool amdgpu_vm_need_pipeline_sync(struct amdgpu_ring *ring,
				  struct amdgpu_job *job);
void amdgpu_vm_check_compute_bug(struct amdgpu_device *adev);

#endif
