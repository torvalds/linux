// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt.h"

#include <linux/minmax.h>

#include <drm/drm_managed.h>
#include <uapi/drm/xe_drm.h>

#include <generated/xe_wa_oob.h>

#include "instructions/xe_gfxpipe_commands.h"
#include "instructions/xe_mi_commands.h"
#include "regs/xe_gt_regs.h"
#include "xe_assert.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_eu_stall.h"
#include "xe_exec_queue.h"
#include "xe_execlist.h"
#include "xe_force_wake.h"
#include "xe_ggtt.h"
#include "xe_gsc.h"
#include "xe_gt_ccs_mode.h"
#include "xe_gt_clock.h"
#include "xe_gt_freq.h"
#include "xe_gt_idle.h"
#include "xe_gt_mcr.h"
#include "xe_gt_pagefault.h"
#include "xe_gt_printk.h"
#include "xe_gt_sriov_pf.h"
#include "xe_gt_sriov_vf.h"
#include "xe_gt_sysfs.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_gt_topology.h"
#include "xe_guc_exec_queue_types.h"
#include "xe_guc_pc.h"
#include "xe_hw_fence.h"
#include "xe_hw_engine_class_sysfs.h"
#include "xe_irq.h"
#include "xe_lmtt.h"
#include "xe_lrc.h"
#include "xe_map.h"
#include "xe_migrate.h"
#include "xe_mmio.h"
#include "xe_pat.h"
#include "xe_pm.h"
#include "xe_mocs.h"
#include "xe_reg_sr.h"
#include "xe_ring_ops.h"
#include "xe_sa.h"
#include "xe_sched_job.h"
#include "xe_sriov.h"
#include "xe_tuning.h"
#include "xe_uc.h"
#include "xe_uc_fw.h"
#include "xe_vm.h"
#include "xe_wa.h"
#include "xe_wopcm.h"

static void gt_fini(struct drm_device *drm, void *arg)
{
	struct xe_gt *gt = arg;

	destroy_workqueue(gt->ordered_wq);
}

struct xe_gt *xe_gt_alloc(struct xe_tile *tile)
{
	struct xe_gt *gt;
	int err;

	gt = drmm_kzalloc(&tile_to_xe(tile)->drm, sizeof(*gt), GFP_KERNEL);
	if (!gt)
		return ERR_PTR(-ENOMEM);

	gt->tile = tile;
	gt->ordered_wq = alloc_ordered_workqueue("gt-ordered-wq",
						 WQ_MEM_RECLAIM);

	err = drmm_add_action_or_reset(&gt_to_xe(gt)->drm, gt_fini, gt);
	if (err)
		return ERR_PTR(err);

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

static void xe_gt_enable_host_l2_vram(struct xe_gt *gt)
{
	unsigned int fw_ref;
	u32 reg;

	if (!XE_WA(gt, 16023588340))
		return;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return;

	if (!xe_gt_is_media_type(gt)) {
		reg = xe_gt_mcr_unicast_read_any(gt, XE2_GAMREQSTRM_CTRL);
		reg |= CG_DIS_CNTLBUS;
		xe_gt_mcr_multicast_write(gt, XE2_GAMREQSTRM_CTRL, reg);
	}

	xe_gt_mcr_multicast_write(gt, XEHPC_L3CLOS_MASK(3), 0x3);
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

static void xe_gt_disable_host_l2_vram(struct xe_gt *gt)
{
	unsigned int fw_ref;
	u32 reg;

	if (!XE_WA(gt, 16023588340))
		return;

	if (xe_gt_is_media_type(gt))
		return;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return;

	reg = xe_gt_mcr_unicast_read_any(gt, XE2_GAMREQSTRM_CTRL);
	reg &= ~CG_DIS_CNTLBUS;
	xe_gt_mcr_multicast_write(gt, XE2_GAMREQSTRM_CTRL, reg);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

static void gt_reset_worker(struct work_struct *w);

static int emit_nop_job(struct xe_gt *gt, struct xe_exec_queue *q)
{
	struct xe_sched_job *job;
	struct xe_bb *bb;
	struct dma_fence *fence;
	long timeout;

	bb = xe_bb_new(gt, 4, false);
	if (IS_ERR(bb))
		return PTR_ERR(bb);

	job = xe_bb_create_job(q, bb);
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

/*
 * Convert back from encoded value to type-safe, only to be used when reg.mcr
 * is true
 */
static struct xe_reg_mcr to_xe_reg_mcr(const struct xe_reg reg)
{
	return (const struct xe_reg_mcr){.__reg.raw = reg.raw };
}

static int emit_wa_job(struct xe_gt *gt, struct xe_exec_queue *q)
{
	struct xe_reg_sr *sr = &q->hwe->reg_lrc;
	struct xe_reg_sr_entry *entry;
	unsigned long idx;
	struct xe_sched_job *job;
	struct xe_bb *bb;
	struct dma_fence *fence;
	long timeout;
	int count = 0;

	if (q->hwe->class == XE_ENGINE_CLASS_RENDER)
		/* Big enough to emit all of the context's 3DSTATE */
		bb = xe_bb_new(gt, xe_gt_lrc_size(gt, q->hwe->class), false);
	else
		/* Just pick a large BB size */
		bb = xe_bb_new(gt, SZ_4K, false);

	if (IS_ERR(bb))
		return PTR_ERR(bb);

	xa_for_each(&sr->xa, idx, entry)
		++count;

	if (count) {
		xe_gt_dbg(gt, "LRC WA %s save-restore batch\n", sr->name);

		bb->cs[bb->len++] = MI_LOAD_REGISTER_IMM | MI_LRI_NUM_REGS(count);

		xa_for_each(&sr->xa, idx, entry) {
			struct xe_reg reg = entry->reg;
			struct xe_reg_mcr reg_mcr = to_xe_reg_mcr(reg);
			u32 val;

			/*
			 * Skip reading the register if it's not really needed
			 */
			if (reg.masked)
				val = entry->clr_bits << 16;
			else if (entry->clr_bits + 1)
				val = (reg.mcr ?
				       xe_gt_mcr_unicast_read_any(gt, reg_mcr) :
				       xe_mmio_read32(&gt->mmio, reg)) & (~entry->clr_bits);
			else
				val = 0;

			val |= entry->set_bits;

			bb->cs[bb->len++] = reg.addr;
			bb->cs[bb->len++] = val;
			xe_gt_dbg(gt, "REG[0x%x] = 0x%08x", reg.addr, val);
		}
	}

	xe_lrc_emit_hwe_state_instructions(q, bb);

	job = xe_bb_create_job(q, bb);
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
		struct xe_exec_queue *q, *nop_q;
		void *default_lrc;

		if (gt->default_lrc[hwe->class])
			continue;

		xe_reg_sr_init(&hwe->reg_lrc, hwe->name, xe);
		xe_wa_process_lrc(hwe);
		xe_hw_engine_setup_default_lrc_state(hwe);
		xe_tuning_process_lrc(hwe);

		default_lrc = drmm_kzalloc(&xe->drm,
					   xe_gt_lrc_size(gt, hwe->class),
					   GFP_KERNEL);
		if (!default_lrc)
			return -ENOMEM;

		q = xe_exec_queue_create(xe, NULL, BIT(hwe->logical_instance), 1,
					 hwe, EXEC_QUEUE_FLAG_KERNEL, 0);
		if (IS_ERR(q)) {
			err = PTR_ERR(q);
			xe_gt_err(gt, "hwe %s: xe_exec_queue_create failed (%pe)\n",
				  hwe->name, q);
			return err;
		}

		/* Prime golden LRC with known good state */
		err = emit_wa_job(gt, q);
		if (err) {
			xe_gt_err(gt, "hwe %s: emit_wa_job failed (%pe) guc_id=%u\n",
				  hwe->name, ERR_PTR(err), q->guc->id);
			goto put_exec_queue;
		}

		nop_q = xe_exec_queue_create(xe, NULL, BIT(hwe->logical_instance),
					     1, hwe, EXEC_QUEUE_FLAG_KERNEL, 0);
		if (IS_ERR(nop_q)) {
			err = PTR_ERR(nop_q);
			xe_gt_err(gt, "hwe %s: nop xe_exec_queue_create failed (%pe)\n",
				  hwe->name, nop_q);
			goto put_exec_queue;
		}

		/* Switch to different LRC */
		err = emit_nop_job(gt, nop_q);
		if (err) {
			xe_gt_err(gt, "hwe %s: nop emit_nop_job failed (%pe) guc_id=%u\n",
				  hwe->name, ERR_PTR(err), nop_q->guc->id);
			goto put_nop_q;
		}

		/* Reload golden LRC to record the effect of any indirect W/A */
		err = emit_nop_job(gt, q);
		if (err) {
			xe_gt_err(gt, "hwe %s: emit_nop_job failed (%pe) guc_id=%u\n",
				  hwe->name, ERR_PTR(err), q->guc->id);
			goto put_nop_q;
		}

		xe_map_memcpy_from(xe, default_lrc,
				   &q->lrc[0]->bo->vmap,
				   xe_lrc_pphwsp_offset(q->lrc[0]),
				   xe_gt_lrc_size(gt, hwe->class));

		gt->default_lrc[hwe->class] = default_lrc;
put_nop_q:
		xe_exec_queue_put(nop_q);
put_exec_queue:
		xe_exec_queue_put(q);
		if (err)
			break;
	}

	return err;
}

int xe_gt_init_early(struct xe_gt *gt)
{
	int err;

	if (IS_SRIOV_PF(gt_to_xe(gt))) {
		err = xe_gt_sriov_pf_init_early(gt);
		if (err)
			return err;
	}

	xe_reg_sr_init(&gt->reg_sr, "GT", gt_to_xe(gt));

	err = xe_wa_init(gt);
	if (err)
		return err;

	err = xe_tuning_init(gt);
	if (err)
		return err;

	xe_wa_process_oob(gt);

	xe_force_wake_init_gt(gt, gt_to_fw(gt));
	spin_lock_init(&gt->global_invl_lock);

	err = xe_gt_tlb_invalidation_init_early(gt);
	if (err)
		return err;

	return 0;
}

static void dump_pat_on_error(struct xe_gt *gt)
{
	struct drm_printer p;
	char prefix[32];

	snprintf(prefix, sizeof(prefix), "[GT%u Error]", gt->info.id);
	p = drm_dbg_printer(&gt_to_xe(gt)->drm, DRM_UT_DRIVER, prefix);

	xe_pat_dump(gt, &p);
}

static int gt_fw_domain_init(struct xe_gt *gt)
{
	unsigned int fw_ref;
	int err;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return -ETIMEDOUT;

	if (!xe_gt_is_media_type(gt)) {
		err = xe_ggtt_init(gt_to_tile(gt)->mem.ggtt);
		if (err)
			goto err_force_wake;
		if (IS_SRIOV_PF(gt_to_xe(gt)))
			xe_lmtt_init(&gt_to_tile(gt)->sriov.pf.lmtt);
	}

	/* Enable per hw engine IRQs */
	xe_irq_enable_hwe(gt);

	/* Rerun MCR init as we now have hw engine list */
	xe_gt_mcr_init(gt);

	err = xe_hw_engines_init_early(gt);
	if (err)
		goto err_force_wake;

	err = xe_hw_engine_class_sysfs_init(gt);
	if (err)
		goto err_force_wake;

	/* Initialize CCS mode sysfs after early initialization of HW engines */
	err = xe_gt_ccs_mode_sysfs_init(gt);
	if (err)
		goto err_force_wake;

	/*
	 * Stash hardware-reported version.  Since this register does not exist
	 * on pre-MTL platforms, reading it there will (correctly) return 0.
	 */
	gt->info.gmdid = xe_mmio_read32(&gt->mmio, GMD_ID);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	return 0;

err_force_wake:
	dump_pat_on_error(gt);
	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	return err;
}

static int all_fw_domain_init(struct xe_gt *gt)
{
	unsigned int fw_ref;
	int err;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL)) {
		err = -ETIMEDOUT;
		goto err_force_wake;
	}

	xe_gt_mcr_set_implicit_defaults(gt);
	xe_wa_process_gt(gt);
	xe_tuning_process_gt(gt);
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
		if (gt_to_xe(gt)->info.has_usm) {
			struct xe_device *xe = gt_to_xe(gt);

			gt->usm.bb_pool = xe_sa_bo_manager_init(gt_to_tile(gt),
								IS_DGFX(xe) ? SZ_1M : SZ_512K, 16);
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

	/* Configure default CCS mode of 1 engine with all resources */
	if (xe_gt_ccs_mode_enabled(gt)) {
		gt->ccs_mode = 1;
		xe_gt_apply_ccs_mode(gt);
	}

	if (IS_SRIOV_PF(gt_to_xe(gt)) && !xe_gt_is_media_type(gt))
		xe_lmtt_init_hw(&gt_to_tile(gt)->sriov.pf.lmtt);

	if (IS_SRIOV_PF(gt_to_xe(gt))) {
		xe_gt_sriov_pf_init(gt);
		xe_gt_sriov_pf_init_hw(gt);
	}

	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	return 0;

err_force_wake:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	return err;
}

/*
 * Initialize enough GT to be able to load GuC in order to obtain hwconfig and
 * enable CTB communication.
 */
int xe_gt_init_hwconfig(struct xe_gt *gt)
{
	unsigned int fw_ref;
	int err;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return -ETIMEDOUT;

	xe_gt_mcr_init_early(gt);
	xe_pat_init(gt);

	err = xe_uc_init(&gt->uc);
	if (err)
		goto out_fw;

	err = xe_uc_init_hwconfig(&gt->uc);
	if (err)
		goto out_fw;

	xe_gt_topology_init(gt);
	xe_gt_mcr_init(gt);
	xe_gt_enable_host_l2_vram(gt);

out_fw:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	return err;
}

static void xe_gt_fini(void *arg)
{
	struct xe_gt *gt = arg;
	int i;

	for (i = 0; i < XE_ENGINE_CLASS_MAX; ++i)
		xe_hw_fence_irq_finish(&gt->fence_irq[i]);

	xe_gt_disable_host_l2_vram(gt);
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

	err = devm_add_action_or_reset(gt_to_xe(gt)->drm.dev, xe_gt_fini, gt);
	if (err)
		return err;

	err = xe_gt_pagefault_init(gt);
	if (err)
		return err;

	xe_mocs_init_early(gt);

	err = xe_gt_sysfs_init(gt);
	if (err)
		return err;

	err = gt_fw_domain_init(gt);
	if (err)
		return err;

	err = xe_gt_idle_init(&gt->gtidle);
	if (err)
		return err;

	err = xe_gt_freq_init(gt);
	if (err)
		return err;

	xe_force_wake_init_engines(gt, gt_to_fw(gt));

	err = all_fw_domain_init(gt);
	if (err)
		return err;

	xe_gt_record_user_engines(gt);

	err = xe_eu_stall_init(gt);
	if (err)
		return err;

	return 0;
}

/**
 * xe_gt_mmio_init() - Initialize GT's MMIO access
 * @gt: the GT object
 *
 * Initialize GT's MMIO accessor, which will be used to access registers inside
 * this GT.
 */
void xe_gt_mmio_init(struct xe_gt *gt)
{
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = tile_to_xe(tile);

	xe_mmio_init(&gt->mmio, tile, tile->mmio.regs, tile->mmio.regs_size);

	if (gt->info.type == XE_GT_TYPE_MEDIA) {
		gt->mmio.adj_offset = MEDIA_GT_GSI_OFFSET;
		gt->mmio.adj_limit = MEDIA_GT_GSI_LENGTH;
	} else {
		gt->mmio.adj_offset = 0;
		gt->mmio.adj_limit = 0;
	}

	if (IS_SRIOV_VF(xe))
		gt->mmio.sriov_vf_gt = gt;
}

void xe_gt_record_user_engines(struct xe_gt *gt)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	gt->user_engines.mask = 0;
	memset(gt->user_engines.instances_per_class, 0,
	       sizeof(gt->user_engines.instances_per_class));

	for_each_hw_engine(hwe, gt, id) {
		if (xe_hw_engine_is_reserved(hwe))
			continue;

		gt->user_engines.mask |= BIT_ULL(id);
		gt->user_engines.instances_per_class[hwe->class]++;
	}

	xe_gt_assert(gt, (gt->user_engines.mask | gt->info.engine_mask)
		     == gt->info.engine_mask);
}

static int do_gt_reset(struct xe_gt *gt)
{
	int err;

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return xe_gt_sriov_vf_reset(gt);

	xe_gsc_wa_14015076503(gt, true);

	xe_mmio_write32(&gt->mmio, GDRST, GRDOM_FULL);
	err = xe_mmio_wait32(&gt->mmio, GDRST, GRDOM_FULL, 0, 5000, NULL, false);
	if (err)
		xe_gt_err(gt, "failed to clear GRDOM_FULL (%pe)\n",
			  ERR_PTR(err));

	xe_gsc_wa_14015076503(gt, false);

	return err;
}

static int vf_gt_restart(struct xe_gt *gt)
{
	int err;

	err = xe_uc_sanitize_reset(&gt->uc);
	if (err)
		return err;

	err = xe_uc_init_hw(&gt->uc);
	if (err)
		return err;

	err = xe_uc_start(&gt->uc);
	if (err)
		return err;

	return 0;
}

static int do_gt_restart(struct xe_gt *gt)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	int err;

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return vf_gt_restart(gt);

	xe_pat_init(gt);

	xe_gt_enable_host_l2_vram(gt);

	xe_gt_mcr_set_implicit_defaults(gt);
	xe_reg_sr_apply_mmio(&gt->reg_sr, gt);

	err = xe_wopcm_init(&gt->uc.wopcm);
	if (err)
		return err;

	for_each_hw_engine(hwe, gt, id)
		xe_hw_engine_enable_ring(hwe);

	err = xe_uc_sanitize_reset(&gt->uc);
	if (err)
		return err;

	err = xe_uc_init_hw(&gt->uc);
	if (err)
		return err;

	if (IS_SRIOV_PF(gt_to_xe(gt)) && !xe_gt_is_media_type(gt))
		xe_lmtt_init_hw(&gt_to_tile(gt)->sriov.pf.lmtt);

	if (IS_SRIOV_PF(gt_to_xe(gt)))
		xe_gt_sriov_pf_init_hw(gt);

	xe_mocs_init(gt);
	err = xe_uc_start(&gt->uc);
	if (err)
		return err;

	for_each_hw_engine(hwe, gt, id)
		xe_reg_sr_apply_mmio(&hwe->reg_sr, gt);

	/* Get CCS mode in sync between sw/hw */
	xe_gt_apply_ccs_mode(gt);

	/* Restore GT freq to expected values */
	xe_gt_sanitize_freq(gt);

	if (IS_SRIOV_PF(gt_to_xe(gt)))
		xe_gt_sriov_pf_restart(gt);

	return 0;
}

static int gt_reset(struct xe_gt *gt)
{
	unsigned int fw_ref;
	int err;

	if (xe_device_wedged(gt_to_xe(gt)))
		return -ECANCELED;

	/* We only support GT resets with GuC submission */
	if (!xe_device_uc_enabled(gt_to_xe(gt)))
		return -ENODEV;

	xe_gt_info(gt, "reset started\n");

	xe_pm_runtime_get(gt_to_xe(gt));

	if (xe_fault_inject_gt_reset()) {
		err = -ECANCELED;
		goto err_fail;
	}

	xe_gt_sanitize(gt);

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL)) {
		err = -ETIMEDOUT;
		goto err_out;
	}

	xe_uc_gucrc_disable(&gt->uc);
	xe_uc_stop_prepare(&gt->uc);
	xe_gt_pagefault_reset(gt);

	xe_uc_stop(&gt->uc);

	xe_gt_tlb_invalidation_reset(gt);

	err = do_gt_reset(gt);
	if (err)
		goto err_out;

	err = do_gt_restart(gt);
	if (err)
		goto err_out;

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	xe_pm_runtime_put(gt_to_xe(gt));

	xe_gt_info(gt, "reset done\n");

	return 0;

err_out:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	XE_WARN_ON(xe_uc_start(&gt->uc));
err_fail:
	xe_gt_err(gt, "reset failed (%pe)\n", ERR_PTR(err));

	xe_device_declare_wedged(gt_to_xe(gt));
	xe_pm_runtime_put(gt_to_xe(gt));

	return err;
}

static void gt_reset_worker(struct work_struct *w)
{
	struct xe_gt *gt = container_of(w, typeof(*gt), reset.worker);

	gt_reset(gt);
}

void xe_gt_reset_async(struct xe_gt *gt)
{
	xe_gt_info(gt, "trying reset from %ps\n", __builtin_return_address(0));

	/* Don't do a reset while one is already in flight */
	if (!xe_fault_inject_gt_reset() && xe_uc_reset_prepare(&gt->uc))
		return;

	xe_gt_info(gt, "reset queued\n");
	queue_work(gt->ordered_wq, &gt->reset.worker);
}

void xe_gt_suspend_prepare(struct xe_gt *gt)
{
	unsigned int fw_ref;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);

	xe_uc_stop_prepare(&gt->uc);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

int xe_gt_suspend(struct xe_gt *gt)
{
	unsigned int fw_ref;
	int err;

	xe_gt_dbg(gt, "suspending\n");
	xe_gt_sanitize(gt);

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL))
		goto err_msg;

	err = xe_uc_suspend(&gt->uc);
	if (err)
		goto err_force_wake;

	xe_gt_idle_disable_pg(gt);

	xe_gt_disable_host_l2_vram(gt);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	xe_gt_dbg(gt, "suspended\n");

	return 0;

err_msg:
	err = -ETIMEDOUT;
err_force_wake:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	xe_gt_err(gt, "suspend failed (%pe)\n", ERR_PTR(err));

	return err;
}

void xe_gt_shutdown(struct xe_gt *gt)
{
	unsigned int fw_ref;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	do_gt_reset(gt);
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

/**
 * xe_gt_sanitize_freq() - Restore saved frequencies if necessary.
 * @gt: the GT object
 *
 * Called after driver init/GSC load completes to restore GT frequencies if we
 * limited them for any WAs.
 */
int xe_gt_sanitize_freq(struct xe_gt *gt)
{
	int ret = 0;

	if ((!xe_uc_fw_is_available(&gt->uc.gsc.fw) ||
	     xe_uc_fw_is_loaded(&gt->uc.gsc.fw) ||
	     xe_uc_fw_is_in_error_state(&gt->uc.gsc.fw)) &&
	    XE_WA(gt, 22019338487))
		ret = xe_guc_pc_restore_stashed_freq(&gt->uc.guc.pc);

	return ret;
}

int xe_gt_resume(struct xe_gt *gt)
{
	unsigned int fw_ref;
	int err;

	xe_gt_dbg(gt, "resuming\n");
	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL))
		goto err_msg;

	err = do_gt_restart(gt);
	if (err)
		goto err_force_wake;

	xe_gt_idle_enable_pg(gt);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	xe_gt_dbg(gt, "resumed\n");

	return 0;

err_msg:
	err = -ETIMEDOUT;
err_force_wake:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
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

struct xe_hw_engine *xe_gt_any_hw_engine(struct xe_gt *gt)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	for_each_hw_engine(hwe, gt, id)
		return hwe;

	return NULL;
}

/**
 * xe_gt_declare_wedged() - Declare GT wedged
 * @gt: the GT object
 *
 * Wedge the GT which stops all submission, saves desired debug state, and
 * cleans up anything which could timeout.
 */
void xe_gt_declare_wedged(struct xe_gt *gt)
{
	xe_gt_assert(gt, gt_to_xe(gt)->wedged.mode);

	xe_uc_declare_wedged(&gt->uc);
	xe_gt_tlb_invalidation_reset(gt);
}
