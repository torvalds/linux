// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_bb.h"

#include "instructions/xe_mi_commands.h"
#include "xe_assert.h"
#include "xe_device.h"
#include "xe_exec_queue_types.h"
#include "xe_gt.h"
#include "xe_hw_fence.h"
#include "xe_sa.h"
#include "xe_sched_job.h"
#include "xe_vm_types.h"

static int bb_prefetch(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (GRAPHICS_VERx100(xe) >= 1250 && !xe_gt_is_media_type(gt))
		/*
		 * RCS and CCS require 1K, although other engines would be
		 * okay with 512.
		 */
		return SZ_1K;
	else
		return SZ_512;
}

struct xe_bb *xe_bb_new(struct xe_gt *gt, u32 dwords, bool usm)
{
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_bb *bb = kmalloc(sizeof(*bb), GFP_KERNEL);
	int err;

	if (!bb)
		return ERR_PTR(-ENOMEM);

	/*
	 * We need to allocate space for the requested number of dwords,
	 * one additional MI_BATCH_BUFFER_END dword, and additional buffer
	 * space to accomodate the platform-specific hardware prefetch
	 * requirements.
	 */
	bb->bo = xe_sa_bo_new(!usm ? tile->mem.kernel_bb_pool : gt->usm.bb_pool,
			      4 * (dwords + 1) + bb_prefetch(gt));
	if (IS_ERR(bb->bo)) {
		err = PTR_ERR(bb->bo);
		goto err;
	}

	bb->cs = xe_sa_bo_cpu_addr(bb->bo);
	bb->len = 0;

	return bb;
err:
	kfree(bb);
	return ERR_PTR(err);
}

static struct xe_sched_job *
__xe_bb_create_job(struct xe_exec_queue *q, struct xe_bb *bb, u64 *addr)
{
	u32 size = drm_suballoc_size(bb->bo);

	if (bb->len == 0 || bb->cs[bb->len - 1] != MI_BATCH_BUFFER_END)
		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;

	xe_gt_assert(q->gt, bb->len * 4 + bb_prefetch(q->gt) <= size);

	xe_sa_bo_flush_write(bb->bo);

	return xe_sched_job_create(q, addr);
}

struct xe_sched_job *xe_bb_create_migration_job(struct xe_exec_queue *q,
						struct xe_bb *bb,
						u64 batch_base_ofs,
						u32 second_idx)
{
	u64 addr[2] = {
		batch_base_ofs + drm_suballoc_soffset(bb->bo),
		batch_base_ofs + drm_suballoc_soffset(bb->bo) +
		4 * second_idx,
	};

	xe_gt_assert(q->gt, second_idx <= bb->len);
	xe_gt_assert(q->gt, xe_sched_job_is_migration(q));
	xe_gt_assert(q->gt, q->width == 1);

	return __xe_bb_create_job(q, bb, addr);
}

struct xe_sched_job *xe_bb_create_job(struct xe_exec_queue *q,
				      struct xe_bb *bb)
{
	u64 addr = xe_sa_bo_gpu_addr(bb->bo);

	xe_gt_assert(q->gt, !xe_sched_job_is_migration(q));
	xe_gt_assert(q->gt, q->width == 1);
	return __xe_bb_create_job(q, bb, &addr);
}

void xe_bb_free(struct xe_bb *bb, struct dma_fence *fence)
{
	if (!bb)
		return;

	xe_sa_bo_free(bb->bo, fence);
	kfree(bb);
}
