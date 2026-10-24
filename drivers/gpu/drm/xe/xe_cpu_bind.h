/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_CPU_BIND_H_
#define _XE_CPU_BIND_H_

#include <linux/types.h>

struct dma_fence;
struct iosys_map;
struct xe_cpu_bind;
struct xe_cpu_bind_pt_update;
struct xe_device;
struct xe_tlb_inval_job;
struct xe_tile;
struct xe_vm;
struct xe_vm_pgtable_update;
struct xe_vm_pgtable_update_op;
struct xe_vma_ops;

/**
 * struct xe_cpu_bind_pt_update_ops - Callbacks for the
 * xe_cpu_bind_update_pgtables() function.
 */
struct xe_cpu_bind_pt_update_ops {
	/**
	 * @populate: Populate a page-table with ptes.
	 * @tile: The tile for the current operation.
	 * @map: struct iosys_map into the memory to be populated.
	 * @update: Information about the PTEs to be inserted.
	 *
	 * This interface is intended to be used as a callback into the
	 * page-table system to populate shared page-tables with PTEs.
	 */
	void (*populate)(struct xe_tile *tile, struct iosys_map *map,
			 const struct xe_vm_pgtable_update *update);
	/**
	 * @clear: Clear a page-table's ptes.
	 * @vm: VM being updated
	 * @tile: The tile for the current operation.
	 * @map: struct iosys_map into the page-table to be cleared.
	 * @update: Information about the PTEs to be cleared.
	 *
	 * This interface is intended to be used as a callback into the
	 * page-table system to clear PTEs from shared page-tables.
	 */
	void (*clear)(struct xe_vm *vm, struct xe_tile *tile,
		      struct iosys_map *map,
		      const struct xe_vm_pgtable_update *update);

	/**
	 * @pre_commit: Callback to be called just before arming the
	 * sched_job.
	 * @pt_update: Pointer to embeddable callback argument.
	 *
	 * Return: 0 on success, negative error code on error.
	 */
	int (*pre_commit)(struct xe_cpu_bind_pt_update *pt_update);
};

/**
 * struct xe_cpu_bind_pt_update - Argument to the struct
 * xe_cpu_bind_pt_update_ops callbacks.
 *
 * Intended to be subclassed to support additional arguments if necessary.
 */
struct xe_cpu_bind_pt_update {
	/** @ops: Pointer to the struct xe_cpu_bind_pt_update_ops callbacks */
	const struct xe_cpu_bind_pt_update_ops *ops;
	/** @vops: VMA operations */
	struct xe_vma_ops *vops;
	/** @job: The bind job, or NULL if the update is issued immediatel */
	struct xe_sched_job *job;
	/**
	 * @ijobs: The TLB invalidation jobs, individual instances can be NULL
	 */
#define XE_CPU_BIND_INVAL_JOB_COUNT	4
	struct xe_tlb_inval_job *ijobs[XE_CPU_BIND_INVAL_JOB_COUNT];
};

int xe_cpu_bind_init(struct xe_device *xe);

struct xe_exec_queue *xe_cpu_bind_queue(struct xe_cpu_bind *cpu_bind);

void
xe_cpu_bind_update_pgtables_execute(struct xe_vm *vm, struct xe_tile *tile,
				    const struct xe_cpu_bind_pt_update_ops *ops,
				    struct xe_vm_pgtable_update_op *pt_op,
				    u32 num_ops, bool force_clear);

struct dma_fence *
xe_cpu_bind_update_pgtables(struct xe_cpu_bind *cpu_bind,
			    struct xe_cpu_bind_pt_update *pt_update);

void xe_cpu_bind_job_lock(struct xe_cpu_bind *cpu_bind,
			  struct xe_exec_queue *q);

void xe_cpu_bind_job_unlock(struct xe_cpu_bind *cpu_bind,
			    struct xe_exec_queue *q);

#if IS_ENABLED(CONFIG_PROVE_LOCKING)
void xe_cpu_bind_job_lock_assert(struct xe_exec_queue *q);
#else
static inline void xe_cpu_bind_job_lock_assert(struct xe_exec_queue *q)
{
}
#endif

#endif
