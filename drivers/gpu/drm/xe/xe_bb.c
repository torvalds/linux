// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_bb.h"

#include "xe_device.h"
#include "xe_engine_types.h"
#include "xe_hw_fence.h"
#include "xe_sa.h"
#include "xe_sched_job.h"
#include "xe_vm_types.h"

#include "gt/intel_gpu_commands.h"

struct xe_bb *xe_bb_new(struct xe_gt *gt, u32 dwords, bool usm)
{
	struct xe_bb *bb = kmalloc(sizeof(*bb), GFP_KERNEL);
	int err;

	if (!bb)
		return ERR_PTR(-ENOMEM);

	bb->bo = xe_sa_bo_new(!usm ? &gt->kernel_bb_pool :
			      &gt->usm.bb_pool, 4 * dwords + 4);
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
__xe_bb_create_job(struct xe_engine *kernel_eng, struct xe_bb *bb, u64 *addr)
{
	u32 size = drm_suballoc_size(bb->bo);

	XE_BUG_ON((bb->len * 4 + 1) > size);

	bb->cs[bb->len++] = MI_BATCH_BUFFER_END;

	xe_sa_bo_flush_write(bb->bo);

	return xe_sched_job_create(kernel_eng, addr);
}

struct xe_sched_job *xe_bb_create_wa_job(struct xe_engine *wa_eng,
					 struct xe_bb *bb, u64 batch_base_ofs)
{
	u64 addr = batch_base_ofs + drm_suballoc_soffset(bb->bo);

	XE_BUG_ON(!(wa_eng->vm->flags & XE_VM_FLAG_MIGRATION));

	return __xe_bb_create_job(wa_eng, bb, &addr);
}

struct xe_sched_job *xe_bb_create_migration_job(struct xe_engine *kernel_eng,
						struct xe_bb *bb,
						u64 batch_base_ofs,
						u32 second_idx)
{
	u64 addr[2] = {
		batch_base_ofs + drm_suballoc_soffset(bb->bo),
		batch_base_ofs + drm_suballoc_soffset(bb->bo) +
		4 * second_idx,
	};

	BUG_ON(second_idx > bb->len);
	BUG_ON(!(kernel_eng->vm->flags & XE_VM_FLAG_MIGRATION));

	return __xe_bb_create_job(kernel_eng, bb, addr);
}

struct xe_sched_job *xe_bb_create_job(struct xe_engine *kernel_eng,
				      struct xe_bb *bb)
{
	u64 addr = xe_sa_bo_gpu_addr(bb->bo);

	BUG_ON(kernel_eng->vm && kernel_eng->vm->flags & XE_VM_FLAG_MIGRATION);
	return __xe_bb_create_job(kernel_eng, bb, &addr);
}

void xe_bb_free(struct xe_bb *bb, struct dma_fence *fence)
{
	if (!bb)
		return;

	xe_sa_bo_free(bb->bo, fence);
	kfree(bb);
}
