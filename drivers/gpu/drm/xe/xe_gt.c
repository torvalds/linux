// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt.h"

#include <linux/minmax.h>

#include <drm/drm_managed.h>

#include "regs/xe_gt_regs.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_engine.h"
#include "xe_execlist.h"
#include "xe_force_wake.h"
#include "xe_ggtt.h"
#include "xe_gt_clock.h"
#include "xe_gt_mcr.h"
#include "xe_gt_pagefault.h"
#include "xe_gt_sysfs.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_gt_topology.h"
#include "xe_hw_fence.h"
#include "xe_irq.h"
#include "xe_lrc.h"
#include "xe_map.h"
#include "xe_migrate.h"
#include "xe_mmio.h"
#include "xe_mocs.h"
#include "xe_reg_sr.h"
#include "xe_ring_ops.h"
#include "xe_sa.h"
#include "xe_sched_job.h"
#include "xe_ttm_gtt_mgr.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_tuning.h"
#include "xe_uc.h"
#include "xe_vm.h"
#include "xe_wa.h"
#include "xe_wopcm.h"

struct xe_gt *xe_find_full_gt(struct xe_gt *gt)
{
	struct xe_gt *search;
	u8 id;

	XE_BUG_ON(!xe_gt_is_media_type(gt));

	for_each_gt(search, gt_to_xe(gt), id) {
		if (search->info.vram_id == gt->info.vram_id)
			return search;
	}

	XE_BUG_ON("NOT POSSIBLE");
	return NULL;
}

int xe_gt_alloc(struct xe_device *xe, struct xe_gt *gt)
{
	struct drm_device *drm = &xe->drm;

	XE_BUG_ON(gt->info.type == XE_GT_TYPE_UNINITIALIZED);

	if (!xe_gt_is_media_type(gt)) {
		gt->mem.ggtt = drmm_kzalloc(drm, sizeof(*gt->mem.ggtt),
					    GFP_KERNEL);
		if (!gt->mem.ggtt)
			return -ENOMEM;

		gt->mem.vram_mgr = drmm_kzalloc(drm, sizeof(*gt->mem.vram_mgr),
						GFP_KERNEL);
		if (!gt->mem.vram_mgr)
			return -ENOMEM;

		gt->mem.gtt_mgr = drmm_kzalloc(drm, sizeof(*gt->mem.gtt_mgr),
					       GFP_KERNEL);
		if (!gt->mem.gtt_mgr)
			return -ENOMEM;
	} else {
		struct xe_gt *full_gt = xe_find_full_gt(gt);

		gt->mem.ggtt = full_gt->mem.ggtt;
		gt->mem.vram_mgr = full_gt->mem.vram_mgr;
		gt->mem.gtt_mgr = full_gt->mem.gtt_mgr;
	}

	gt->ordered_wq = alloc_ordered_workqueue("gt-ordered-wq", 0);

	return 0;
}

/* FIXME: These should be in a common file */
#define CHV_PPAT_SNOOP			REG_BIT(6)
#define GEN8_PPAT_AGE(x)		((x)<<4)
#define GEN8_PPAT_LLCeLLC		(3<<2)
#define GEN8_PPAT_LLCELLC		(2<<2)
#define GEN8_PPAT_LLC			(1<<2)
#define GEN8_PPAT_WB			(3<<0)
#define GEN8_PPAT_WT			(2<<0)
#define GEN8_PPAT_WC			(1<<0)
#define GEN8_PPAT_UC			(0<<0)
#define GEN8_PPAT_ELLC_OVERRIDE		(0<<2)
#define GEN8_PPAT(i, x)			((u64)(x) << ((i) * 8))
#define GEN12_PPAT_CLOS(x)              ((x)<<2)

static void tgl_setup_private_ppat(struct xe_gt *gt)
{
	/* TGL doesn't support LLC or AGE settings */
	xe_mmio_write32(gt, GEN12_PAT_INDEX(0).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(1).reg, GEN8_PPAT_WC);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(2).reg, GEN8_PPAT_WT);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(3).reg, GEN8_PPAT_UC);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(4).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(5).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(6).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(7).reg, GEN8_PPAT_WB);
}

static void pvc_setup_private_ppat(struct xe_gt *gt)
{
	xe_mmio_write32(gt, GEN12_PAT_INDEX(0).reg, GEN8_PPAT_UC);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(1).reg, GEN8_PPAT_WC);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(2).reg, GEN8_PPAT_WT);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(3).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(4).reg,
			GEN12_PPAT_CLOS(1) | GEN8_PPAT_WT);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(5).reg,
			GEN12_PPAT_CLOS(1) | GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(6).reg,
			GEN12_PPAT_CLOS(2) | GEN8_PPAT_WT);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(7).reg,
			GEN12_PPAT_CLOS(2) | GEN8_PPAT_WB);
}

#define MTL_PPAT_L4_CACHE_POLICY_MASK   REG_GENMASK(3, 2)
#define MTL_PAT_INDEX_COH_MODE_MASK     REG_GENMASK(1, 0)
#define MTL_PPAT_3_UC   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 3)
#define MTL_PPAT_1_WT   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 1)
#define MTL_PPAT_0_WB   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 0)
#define MTL_3_COH_2W    REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 3)
#define MTL_2_COH_1W    REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 2)
#define MTL_0_COH_NON   REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 0)

static void mtl_setup_private_ppat(struct xe_gt *gt)
{
	xe_mmio_write32(gt, GEN12_PAT_INDEX(0).reg, MTL_PPAT_0_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(1).reg,
			MTL_PPAT_1_WT | MTL_2_COH_1W);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(2).reg,
			MTL_PPAT_3_UC | MTL_2_COH_1W);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(3).reg,
			MTL_PPAT_0_WB | MTL_2_COH_1W);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(4).reg,
			MTL_PPAT_0_WB | MTL_3_COH_2W);
}

static void setup_private_ppat(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (xe->info.platform == XE_METEORLAKE)
		mtl_setup_private_ppat(gt);
	else if (xe->info.platform == XE_PVC)
		pvc_setup_private_ppat(gt);
	else
		tgl_setup_private_ppat(gt);
}

static int gt_ttm_mgr_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;
	struct sysinfo si;
	u64 gtt_size;

	si_meminfo(&si);
	gtt_size = (u64)si.totalram * si.mem_unit * 3/4;

	if (gt->mem.vram.size) {
		err = xe_ttm_vram_mgr_init(gt, gt->mem.vram_mgr);
		if (err)
			return err;
		gtt_size = min(max((XE_DEFAULT_GTT_SIZE_MB << 20),
				   (u64)gt->mem.vram.size),
			       gtt_size);
		xe->info.mem_region_mask |= BIT(gt->info.vram_id) << 1;
	}

	err = xe_ttm_gtt_mgr_init(gt, gt->mem.gtt_mgr, gtt_size);
	if (err)
		return err;

	return 0;
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

	batch_ofs = xe_bo_ggtt_addr(gt->kernel_bb_pool.bo);
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
	bb->cs[bb->len++] = MI_NOOP;
	bb->cs[bb->len++] = MI_BATCH_BUFFER_END;

	batch_ofs = xe_bo_ggtt_addr(gt->kernel_bb_pool.bo);
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
		xe_tuning_process_lrc(hwe);

		default_lrc = drmm_kzalloc(&xe->drm,
					   xe_lrc_size(xe, hwe->class),
					   GFP_KERNEL);
		if (!default_lrc)
			return -ENOMEM;

		vm = xe_migrate_get_vm(gt->migrate);
		e = xe_engine_create(xe, vm, BIT(hwe->logical_instance), 1,
				     hwe, ENGINE_FLAG_WA);
		if (IS_ERR(e)) {
			err = PTR_ERR(e);
			goto put_vm;
		}

		/* Prime golden LRC with known good state */
		err = emit_wa_job(gt, e);
		if (err)
			goto put_engine;

		nop_e = xe_engine_create(xe, vm, BIT(hwe->logical_instance),
					 1, hwe, ENGINE_FLAG_WA);
		if (IS_ERR(nop_e)) {
			err = PTR_ERR(nop_e);
			goto put_engine;
		}

		/* Switch to different LRC */
		err = emit_nop_job(gt, nop_e);
		if (err)
			goto put_nop_e;

		/* Reload golden LRC to record the effect of any indirect W/A */
		err = emit_nop_job(gt, e);
		if (err)
			goto put_nop_e;

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
	xe_wa_process_gt(gt);
	xe_tuning_process_gt(gt);

	return 0;
}

/**
 * xe_gt_init_noalloc - Init GT up to the point where allocations can happen.
 * @gt: The GT to initialize.
 *
 * This function prepares the GT to allow memory allocations to VRAM, but is not
 * allowed to allocate memory itself. This state is useful for display readout,
 * because the inherited display framebuffer will otherwise be overwritten as it
 * is usually put at the start of VRAM.
 *
 * Returns: 0 on success, negative error code on error.
 */
int xe_gt_init_noalloc(struct xe_gt *gt)
{
	int err, err2;

	if (xe_gt_is_media_type(gt))
		return 0;

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		goto err;

	err = gt_ttm_mgr_init(gt);
	if (err)
		goto err_force_wake;

	err = xe_ggtt_init_noalloc(gt, gt->mem.ggtt);

err_force_wake:
	err2 = xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
	XE_WARN_ON(err2);
	xe_device_mem_access_put(gt_to_xe(gt));
err:
	return err;
}

static int gt_fw_domain_init(struct xe_gt *gt)
{
	int err, i;

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		goto err_hw_fence_irq;

	setup_private_ppat(gt);

	if (!xe_gt_is_media_type(gt)) {
		err = xe_ggtt_init(gt, gt->mem.ggtt);
		if (err)
			goto err_force_wake;
	}

	/* Allow driver to load if uC init fails (likely missing firmware) */
	err = xe_uc_init(&gt->uc);
	XE_WARN_ON(err);

	err = xe_uc_init_hwconfig(&gt->uc);
	if (err)
		goto err_force_wake;

	/* XXX: Fake that we pull the engine mask from hwconfig blob */
	gt->info.engine_mask = gt->info.__engine_mask;

	/* Enables per hw engine IRQs */
	xe_gt_irq_postinstall(gt);

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

	/*
	 * FIXME: This should be ok as SA should only be used by gt->migrate and
	 * vm->gt->migrate and both should be pointing to a non-media GT. But to
	 * realy safe, convert gt->kernel_bb_pool to a pointer and point a media
	 * GT to the kernel_bb_pool on a real tile.
	 */
	if (!xe_gt_is_media_type(gt)) {
		err = xe_sa_bo_manager_init(gt, &gt->kernel_bb_pool, SZ_1M, 16);
		if (err)
			goto err_force_wake;

		/*
		 * USM has its only SA pool to non-block behind user operations
		 */
		if (gt_to_xe(gt)->info.supports_usm) {
			err = xe_sa_bo_manager_init(gt, &gt->usm.bb_pool,
						    SZ_1M, 16);
			if (err)
				goto err_force_wake;
		}
	}

	if (!xe_gt_is_media_type(gt)) {
		gt->migrate = xe_migrate_init(gt);
		if (IS_ERR(gt->migrate)) {
			err = PTR_ERR(gt->migrate);
			goto err_force_wake;
		}
	} else {
		gt->migrate = xe_find_full_gt(gt)->migrate;
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
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	xe_mmio_write32(gt, GEN6_GDRST.reg, GEN11_GRDOM_FULL);
	err = xe_mmio_wait32(gt, GEN6_GDRST.reg, 0, GEN11_GRDOM_FULL, 5000,
			     NULL, false);
	if (err)
		drm_err(&xe->drm,
			"GT reset failed to clear GEN11_GRDOM_FULL\n");

	return err;
}

static int do_gt_restart(struct xe_gt *gt)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	int err;

	setup_private_ppat(gt);

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
		xe_reg_sr_apply_whitelist(&hwe->reg_whitelist,
					  hwe->mmio_base, gt);
	}

	return 0;
}

static int gt_reset(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	/* We only support GT resets with GuC submission */
	if (!xe_device_guc_submission_enabled(gt_to_xe(gt)))
		return -ENODEV;

	drm_info(&xe->drm, "GT reset started\n");

	xe_gt_sanitize(gt);

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (err)
		goto err_msg;

	xe_uc_stop_prepare(&gt->uc);
	xe_gt_pagefault_reset(gt);
	xe_gt_tlb_invalidation_reset(gt);

	err = xe_uc_stop(&gt->uc);
	if (err)
		goto err_out;

	err = do_gt_reset(gt);
	if (err)
		goto err_out;

	err = do_gt_restart(gt);
	if (err)
		goto err_out;

	xe_device_mem_access_put(gt_to_xe(gt));
	err = xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	XE_WARN_ON(err);

	drm_info(&xe->drm, "GT reset done\n");

	return 0;

err_out:
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
err_msg:
	XE_WARN_ON(xe_uc_start(&gt->uc));
	xe_device_mem_access_put(gt_to_xe(gt));
	drm_err(&xe->drm, "GT reset failed, err=%d\n", err);

	return err;
}

static void gt_reset_worker(struct work_struct *w)
{
	struct xe_gt *gt = container_of(w, typeof(*gt), reset.worker);

	gt_reset(gt);
}

void xe_gt_reset_async(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	drm_info(&xe->drm, "Try GT reset\n");

	/* Don't do a reset while one is already in flight */
	if (xe_uc_reset_prepare(&gt->uc))
		return;

	drm_info(&xe->drm, "Doing GT reset\n");
	queue_work(gt->ordered_wq, &gt->reset.worker);
}

void xe_gt_suspend_prepare(struct xe_gt *gt)
{
	xe_device_mem_access_get(gt_to_xe(gt));
	XE_WARN_ON(xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL));

	xe_uc_stop_prepare(&gt->uc);

	xe_device_mem_access_put(gt_to_xe(gt));
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
}

int xe_gt_suspend(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
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

	xe_device_mem_access_put(gt_to_xe(gt));
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	drm_info(&xe->drm, "GT suspended\n");

	return 0;

err_force_wake:
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
err_msg:
	xe_device_mem_access_put(gt_to_xe(gt));
	drm_err(&xe->drm, "GT suspend failed: %d\n", err);

	return err;
}

int xe_gt_resume(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (err)
		goto err_msg;

	err = do_gt_restart(gt);
	if (err)
		goto err_force_wake;

	xe_device_mem_access_put(gt_to_xe(gt));
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	drm_info(&xe->drm, "GT resumed\n");

	return 0;

err_force_wake:
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
err_msg:
	xe_device_mem_access_put(gt_to_xe(gt));
	drm_err(&xe->drm, "GT resume failed: %d\n", err);

	return err;
}

void xe_gt_migrate_wait(struct xe_gt *gt)
{
	xe_migrate_wait(gt->migrate);
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
