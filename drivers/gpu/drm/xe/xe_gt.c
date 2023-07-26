// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt.h"

#include <linux/minmax.h>

#include <drm/drm_managed.h>
#include <drm/xe_drm.h>

#include "regs/xe_gt_regs.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_engine.h"
#include "xe_execlist.h"
#include "xe_force_wake.h"
#include "xe_ggtt.h"
#include "xe_gt_clock.h"
#include "xe_gt_idle_sysfs.h"
#include "xe_gt_mcr.h"
#include "xe_gt_pagefault.h"
#include "xe_gt_printk.h"
#include "xe_gt_sysfs.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_gt_topology.h"
#include "xe_guc_engine_types.h"
#include "xe_hw_fence.h"
#include "xe_irq.h"
#include "xe_lrc.h"
#include "xe_map.h"
#include "xe_migrate.h"
#include "xe_mmio.h"
#include "xe_pat.h"
#include "xe_mocs.h"
#include "xe_reg_sr.h"
#include "xe_ring_ops.h"
#include "xe_sa.h"
#include "xe_sched_job.h"
#include "xe_tuning.h"
#include "xe_uc.h"
#include "xe_vm.h"
#include "xe_wa.h"
#include "xe_wopcm.h"

struct xe_gt *xe_gt_alloc(struct xe_tile *tile)
{
	struct xe_gt *gt;

	gt = drmm_kzalloc(&tile_to_xe(tile)->drm, sizeof(*gt), GFP_KERNEL);
	if (!gt)
		return ERR_PTR(-ENOMEM);

	gt->tile = tile;
	gt->ordered_wq = alloc_ordered_workqueue("gt-ordered-wq", 0);

	return gt;
}

void xe_gt_sanitize(struct xe_gt *gt)
{
	/*
	 * FIXME: if xe_uc_sanitize is called here, on TGL driver will not
	 * reload
	 */
	gt->uc.guc.submission_state.enabled = false;
}

static void gt_fini(struct drm_device *drm, void *arg)
{
	struct xe_gt *gt = arg;
	int i;

	destroy_workqueue(gt->ordered_wq);

	for (i = 0; i < XE_ENGINE_CLASS_MAX; ++i)
		xe_hw_fence_irq_finish(&gt->fence_irq[i]);
}

static void gt_reset_worker(struct work_struct *w);

static int emit_nop_job(struct xe_gt *gt, struct xe_engine *e)
{
	struct xe_sched_job *job;
	struct xe_bb *bb;
	struct dma_fence *fence;
	u64 batch_ofs;
	long timeout;

	bb = xe_bb_new(gt, 4, false);
	if (IS_ERR(bb))
		return PTR_ERR(bb);

	batch_ofs = xe_bo_ggtt_addr(gt_to_tile(gt)->mem.kernel_bb_pool->bo);
	job = xe_bb_create_wa_job(e, bb, batch_ofs);
	if (IS_ERR(job)) {
		xe_bb_free(bb, NULL);
		return PTR_ERR(job);
	}

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	timeout = dma_fence_wait_timeout(fence, false, HZ);
	dma_fence_put(fence);
	xe_bb_free(bb, NULL);
	if (timeout < 0)
		return timeout;
	else if (!timeout)
		return -ETIME;

	return 0;
}

static int emit_wa_job(struct xe_gt *gt, struct xe_engine *e)
{
	struct xe_reg_sr *sr = &e->hwe->reg_lrc;
	struct xe_reg_sr_entry *entry;
	unsigned long reg;
	struct xe_sched_job *job;
	struct xe_bb *bb;
	struct dma_fence *fence;
	u64 batch_ofs;
	long timeout;
	int count = 0;

	bb = xe_bb_new(gt, SZ_4K, false);	/* Just pick a large BB size */
	if (IS_ERR(bb))
		return PTR_ERR(bb);

	xa_for_each(&sr->xa, reg, entry)
		++count;

	if (count) {
		bb->cs[bb->len++] = MI_LOAD_REGISTER_IMM(count);
		xa_for_each(&sr->xa, reg, entry) {
			bb->cs[bb->len++] = reg;
			bb->cs[bb->len++] = entry->set_bits;
		}
	}

	batch_ofs = xe_bo_ggtt_addr(gt_to_tile(gt)->mem.kernel_bb_pool->bo);
	job = xe_bb_create_wa_job(e, bb, batch_ofs);
	if (IS_ERR(job)) {
		xe_bb_free(bb, NULL);
		return PTR_ERR(job);
	}

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	timeout = dma_fence_wait_timeout(fence, false, HZ);
	dma_fence_put(fence);
	xe_bb_free(bb, NULL);
	if (timeout < 0)
		return timeout;
	else if (!timeout)
		return -ETIME;

	return 0;
}

int xe_gt_record_default_lrcs(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	int err = 0;

	for_each_hw_engine(hwe, gt, id) {
		struct xe_engine *e, *nop_e;
		struct xe_vm *vm;
		void *default_lrc;

		if (gt->default_lrc[hwe->class])
			continue;

		xe_reg_sr_init(&hwe->reg_lrc, hwe->name, xe);
		xe_wa_process_lrc(hwe);
		xe_hw_engine_setup_default_lrc_state(hwe);
		xe_tuning_process_lrc(hwe);

		default_lrc = drmm_kzalloc(&xe->drm,
					   xe_lrc_size(xe, hwe->class),
					   GFP_KERNEL);
		if (!default_lrc)
			return -ENOMEM;

		vm = xe_migrate_get_vm(tile->migrate);
		e = xe_engine_create(xe, vm, BIT(hwe->logical_instance), 1,
				     hwe, ENGINE_FLAG_WA);
		if (IS_ERR(e)) {
			err = PTR_ERR(e);
			xe_gt_err(gt, "hwe %s: xe_engine_create failed (%pe)\n",
				  hwe->name, e);
			goto put_vm;
		}

		/* Prime golden LRC with known good state */
		err = emit_wa_job(gt, e);
		if (err) {
			xe_gt_err(gt, "hwe %s: emit_wa_job failed (%pe) guc_id=%u\n",
				  hwe->name, ERR_PTR(err), e->guc->id);
			goto put_engine;
		}

		nop_e = xe_engine_create(xe, vm, BIT(hwe->logical_instance),
					 1, hwe, ENGINE_FLAG_WA);
		if (IS_ERR(nop_e)) {
			err = PTR_ERR(nop_e);
			xe_gt_err(gt, "hwe %s: nop xe_engine_create failed (%pe)\n",
				  hwe->name, nop_e);
			goto put_engine;
		}

		/* Switch to different LRC */
		err = emit_nop_job(gt, nop_e);
		if (err) {
			xe_gt_err(gt, "hwe %s: nop emit_nop_job failed (%pe) guc_id=%u\n",
				  hwe->name, ERR_PTR(err), nop_e->guc->id);
			goto put_nop_e;
		}

		/* Reload golden LRC to record the effect of any indirect W/A */
		err = emit_nop_job(gt, e);
		if (err) {
			xe_gt_err(gt, "hwe %s: emit_nop_job failed (%pe) guc_id=%u\n",
				  hwe->name, ERR_PTR(err), e->guc->id);
			goto put_nop_e;
		}

		xe_map_memcpy_from(xe, default_lrc,
				   &e->lrc[0].bo->vmap,
				   xe_lrc_pphwsp_offset(&e->lrc[0]),
				   xe_lrc_size(xe, hwe->class));

		gt->default_lrc[hwe->class] = default_lrc;
put_nop_e:
		xe_engine_put(nop_e);
put_engine:
		xe_engine_put(e);
put_vm:
		xe_vm_put(vm);
		if (err)
			break;
	}

	return err;
}

int xe_gt_init_early(struct xe_gt *gt)
{
	int err;

	xe_force_wake_init_gt(gt, gt_to_fw(gt));

	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		return err;

	xe_gt_topology_init(gt);
	xe_gt_mcr_init(gt);

	err = xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
	if (err)
		return err;

	xe_reg_sr_init(&gt->reg_sr, "GT", gt_to_xe(gt));

	err = xe_wa_init(gt);
	if (err)
		return err;

	xe_wa_process_gt(gt);
	xe_wa_process_oob(gt);
	xe_tuning_process_gt(gt);

	return 0;
}

static int gt_fw_domain_init(struct xe_gt *gt)
{
	int err, i;

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		goto err_hw_fence_irq;

	xe_pat_init(gt);

	if (!xe_gt_is_media_type(gt)) {
		err = xe_ggtt_init(gt_to_tile(gt)->mem.ggtt);
		if (err)
			goto err_force_wake;
	}

	err = xe_uc_init(&gt->uc);
	if (err)
		goto err_force_wake;

	err = xe_uc_init_hwconfig(&gt->uc);
	if (err)
		goto err_force_wake;

	xe_gt_idle_sysfs_init(&gt->gtidle);

	/* XXX: Fake that we pull the engine mask from hwconfig blob */
	gt->info.engine_mask = gt->info.__engine_mask;

	/* Enable per hw engine IRQs */
	xe_irq_enable_hwe(gt);

	/* Rerun MCR init as we now have hw engine list */
	xe_gt_mcr_init(gt);

	err = xe_hw_engines_init_early(gt);
	if (err)
		goto err_force_wake;

	err = xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
	XE_WARN_ON(err);
	xe_device_mem_access_put(gt_to_xe(gt));

	return 0;

err_force_wake:
	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
err_hw_fence_irq:
	for (i = 0; i < XE_ENGINE_CLASS_MAX; ++i)
		xe_hw_fence_irq_finish(&gt->fence_irq[i]);
	xe_device_mem_access_put(gt_to_xe(gt));

	return err;
}

static int all_fw_domain_init(struct xe_gt *gt)
{
	int err, i;

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (err)
		goto err_hw_fence_irq;

	xe_gt_mcr_set_implicit_defaults(gt);
	xe_reg_sr_apply_mmio(&gt->reg_sr, gt);

	err = xe_gt_clock_init(gt);
	if (err)
		goto err_force_wake;

	xe_mocs_init(gt);
	err = xe_execlist_init(gt);
	if (err)
		goto err_force_wake;

	err = xe_hw_engines_init(gt);
	if (err)
		goto err_force_wake;

	err = xe_uc_init_post_hwconfig(&gt->uc);
	if (err)
		goto err_force_wake;

	if (!xe_gt_is_media_type(gt)) {
		/*
		 * USM has its only SA pool to non-block behind user operations
		 */
		if (gt_to_xe(gt)->info.supports_usm) {
			gt->usm.bb_pool = xe_sa_bo_manager_init(gt_to_tile(gt), SZ_1M, 16);
			if (IS_ERR(gt->usm.bb_pool)) {
				err = PTR_ERR(gt->usm.bb_pool);
				goto err_force_wake;
			}
		}
	}

	if (!xe_gt_is_media_type(gt)) {
		struct xe_tile *tile = gt_to_tile(gt);

		tile->migrate = xe_migrate_init(tile);
		if (IS_ERR(tile->migrate)) {
			err = PTR_ERR(tile->migrate);
			goto err_force_wake;
		}
	}

	err = xe_uc_init_hw(&gt->uc);
	if (err)
		goto err_force_wake;

	err = xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	XE_WARN_ON(err);
	xe_device_mem_access_put(gt_to_xe(gt));

	return 0;

err_force_wake:
	xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL);
err_hw_fence_irq:
	for (i = 0; i < XE_ENGINE_CLASS_MAX; ++i)
		xe_hw_fence_irq_finish(&gt->fence_irq[i]);
	xe_device_mem_access_put(gt_to_xe(gt));

	return err;
}

int xe_gt_init(struct xe_gt *gt)
{
	int err;
	int i;

	INIT_WORK(&gt->reset.worker, gt_reset_worker);

	for (i = 0; i < XE_ENGINE_CLASS_MAX; ++i) {
		gt->ring_ops[i] = xe_ring_ops_get(gt, i);
		xe_hw_fence_irq_init(&gt->fence_irq[i]);
	}

	err = xe_gt_tlb_invalidation_init(gt);
	if (err)
		return err;

	err = xe_gt_pagefault_init(gt);
	if (err)
		return err;

	xe_mocs_init_early(gt);

	xe_gt_sysfs_init(gt);

	err = gt_fw_domain_init(gt);
	if (err)
		return err;

	xe_force_wake_init_engines(gt, gt_to_fw(gt));

	err = all_fw_domain_init(gt);
	if (err)
		return err;

	err = drmm_add_action_or_reset(&gt_to_xe(gt)->drm, gt_fini, gt);
	if (err)
		return err;

	return 0;
}

static int do_gt_reset(struct xe_gt *gt)
{
	int err;

	xe_mmio_write32(gt, GDRST, GRDOM_FULL);
	err = xe_mmio_wait32(gt, GDRST, GRDOM_FULL, 0, 5000, NULL, false);
	if (err)
		xe_gt_err(gt, "failed to clear GEN11_GRDOM_FULL (%pe)\n",
			  ERR_PTR(err));

	return err;
}

static int do_gt_restart(struct xe_gt *gt)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	int err;

	xe_pat_init(gt);

	xe_gt_mcr_set_implicit_defaults(gt);
	xe_reg_sr_apply_mmio(&gt->reg_sr, gt);

	err = xe_wopcm_init(&gt->uc.wopcm);
	if (err)
		return err;

	for_each_hw_engine(hwe, gt, id)
		xe_hw_engine_enable_ring(hwe);

	err = xe_uc_init_hw(&gt->uc);
	if (err)
		return err;

	xe_mocs_init(gt);
	err = xe_uc_start(&gt->uc);
	if (err)
		return err;

	for_each_hw_engine(hwe, gt, id) {
		xe_reg_sr_apply_mmio(&hwe->reg_sr, gt);
		xe_reg_sr_apply_whitelist(hwe);
	}

	return 0;
}

static void xe_uevent_gt_reset_failure(struct pci_dev *pdev, u8 tile_id, u8 gt_id)
{
	char *reset_event[4];

	reset_event[0] = XE_RESET_FAILED_UEVENT "=NEEDS_RESET";
	reset_event[1] = kasprintf(GFP_KERNEL, "TILE_ID=%d", tile_id);
	reset_event[2] = kasprintf(GFP_KERNEL, "GT_ID=%d", gt_id);
	reset_event[3] = NULL;
	kobject_uevent_env(&pdev->dev.kobj, KOBJ_CHANGE, reset_event);

	kfree(reset_event[1]);
	kfree(reset_event[2]);
}

static int gt_reset(struct xe_gt *gt)
{
	int err;

	/* We only support GT resets with GuC submission */
	if (!xe_device_guc_submission_enabled(gt_to_xe(gt)))
		return -ENODEV;

	xe_gt_info(gt, "reset started\n");

	if (xe_fault_inject_gt_reset()) {
		err = -ECANCELED;
		goto err_fail;
	}

	xe_gt_sanitize(gt);

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (err)
		goto err_msg;

	xe_uc_gucrc_disable(&gt->uc);
	xe_uc_stop_prepare(&gt->uc);
	xe_gt_pagefault_reset(gt);

	err = xe_uc_stop(&gt->uc);
	if (err)
		goto err_out;

	err = do_gt_reset(gt);
	if (err)
		goto err_out;

	xe_gt_tlb_invalidation_reset(gt);

	err = do_gt_restart(gt);
	if (err)
		goto err_out;

	err = xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	xe_device_mem_access_put(gt_to_xe(gt));
	XE_WARN_ON(err);

	xe_gt_info(gt, "reset done\n");

	return 0;

err_out:
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
err_msg:
	XE_WARN_ON(xe_uc_start(&gt->uc));
	xe_device_mem_access_put(gt_to_xe(gt));
err_fail:
	xe_gt_err(gt, "reset failed (%pe)\n", ERR_PTR(err));

	/* Notify userspace about gt reset failure */
	xe_uevent_gt_reset_failure(to_pci_dev(gt_to_xe(gt)->drm.dev),
				   gt_to_tile(gt)->id, gt->info.id);

	return err;
}

static void gt_reset_worker(struct work_struct *w)
{
	struct xe_gt *gt = container_of(w, typeof(*gt), reset.worker);

	gt_reset(gt);
}

void xe_gt_reset_async(struct xe_gt *gt)
{
	xe_gt_info(gt, "trying reset\n");

	/* Don't do a reset while one is already in flight */
	if (!xe_fault_inject_gt_reset() && xe_uc_reset_prepare(&gt->uc))
		return;

	xe_gt_info(gt, "reset queued\n");
	queue_work(gt->ordered_wq, &gt->reset.worker);
}

void xe_gt_suspend_prepare(struct xe_gt *gt)
{
	xe_device_mem_access_get(gt_to_xe(gt));
	XE_WARN_ON(xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL));

	xe_uc_stop_prepare(&gt->uc);

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	xe_device_mem_access_put(gt_to_xe(gt));
}

int xe_gt_suspend(struct xe_gt *gt)
{
	int err;

	/* For now suspend/resume is only allowed with GuC */
	if (!xe_device_guc_submission_enabled(gt_to_xe(gt)))
		return -ENODEV;

	xe_gt_sanitize(gt);

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (err)
		goto err_msg;

	err = xe_uc_suspend(&gt->uc);
	if (err)
		goto err_force_wake;

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	xe_device_mem_access_put(gt_to_xe(gt));
	xe_gt_info(gt, "suspended\n");

	return 0;

err_force_wake:
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
err_msg:
	xe_device_mem_access_put(gt_to_xe(gt));
	xe_gt_err(gt, "suspend failed (%pe)\n", ERR_PTR(err));

	return err;
}

int xe_gt_resume(struct xe_gt *gt)
{
	int err;

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (err)
		goto err_msg;

	err = do_gt_restart(gt);
	if (err)
		goto err_force_wake;

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	xe_device_mem_access_put(gt_to_xe(gt));
	xe_gt_info(gt, "resumed\n");

	return 0;

err_force_wake:
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
err_msg:
	xe_device_mem_access_put(gt_to_xe(gt));
	xe_gt_err(gt, "resume failed (%pe)\n", ERR_PTR(err));

	return err;
}

struct xe_hw_engine *xe_gt_hw_engine(struct xe_gt *gt,
				     enum xe_engine_class class,
				     u16 instance, bool logical)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	for_each_hw_engine(hwe, gt, id)
		if (hwe->class == class &&
		    ((!logical && hwe->instance == instance) ||
		    (logical && hwe->logical_instance == instance)))
			return hwe;

	return NULL;
}

struct xe_hw_engine *xe_gt_any_hw_engine_by_reset_domain(struct xe_gt *gt,
							 enum xe_engine_class class)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	for_each_hw_engine(hwe, gt, id) {
		switch (class) {
		case XE_ENGINE_CLASS_RENDER:
		case XE_ENGINE_CLASS_COMPUTE:
			if (hwe->class == XE_ENGINE_CLASS_RENDER ||
			    hwe->class == XE_ENGINE_CLASS_COMPUTE)
				return hwe;
			break;
		default:
			if (hwe->class == class)
				return hwe;
		}
	}

	return NULL;
}
