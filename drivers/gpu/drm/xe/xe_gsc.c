// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gsc.h"

#include <linux/delay.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include <generated/xe_wa_oob.h>

#include "abi/gsc_mkhi_commands_abi.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_force_wake.h"
#include "xe_gsc_proxy.h"
#include "xe_gsc_submit.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_printk.h"
#include "xe_guc_pc.h"
#include "xe_huc.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_sched_job.h"
#include "xe_uc_fw.h"
#include "xe_wa.h"
#include "instructions/xe_gsc_commands.h"
#include "regs/xe_gsc_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_irq_regs.h"

static struct xe_gt *
gsc_to_gt(struct xe_gsc *gsc)
{
	return container_of(gsc, struct xe_gt, uc.gsc);
}

static int memcpy_fw(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_device *xe = gt_to_xe(gt);
	u32 fw_size = gsc->fw.size;
	void *storage;

	/*
	 * FIXME: xe_migrate_copy does not work with stolen mem yet, so we use
	 * a memcpy for now.
	 */
	storage = kmalloc(fw_size, GFP_KERNEL);
	if (!storage)
		return -ENOMEM;

	xe_map_memcpy_from(xe, storage, &gsc->fw.bo->vmap, 0, fw_size);
	xe_map_memcpy_to(xe, &gsc->private->vmap, 0, storage, fw_size);
	xe_map_memset(xe, &gsc->private->vmap, fw_size, 0, gsc->private->size - fw_size);

	kfree(storage);

	return 0;
}

static int emit_gsc_upload(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	u64 offset = xe_bo_ggtt_addr(gsc->private);
	struct xe_bb *bb;
	struct xe_sched_job *job;
	struct dma_fence *fence;
	long timeout;

	bb = xe_bb_new(gt, 4, false);
	if (IS_ERR(bb))
		return PTR_ERR(bb);

	bb->cs[bb->len++] = GSC_FW_LOAD;
	bb->cs[bb->len++] = lower_32_bits(offset);
	bb->cs[bb->len++] = upper_32_bits(offset);
	bb->cs[bb->len++] = (gsc->private->size / SZ_4K) | GSC_FW_LOAD_LIMIT_VALID;

	job = xe_bb_create_job(gsc->q, bb);
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

#define version_query_wr(xe_, map_, offset_, field_, val_) \
	xe_map_wr_field(xe_, map_, offset_, struct gsc_get_compatibility_version_in, field_, val_)
#define version_query_rd(xe_, map_, offset_, field_) \
	xe_map_rd_field(xe_, map_, offset_, struct gsc_get_compatibility_version_out, field_)

static u32 emit_version_query_msg(struct xe_device *xe, struct iosys_map *map, u32 wr_offset)
{
	xe_map_memset(xe, map, wr_offset, 0, sizeof(struct gsc_get_compatibility_version_in));

	version_query_wr(xe, map, wr_offset, header.group_id, MKHI_GROUP_ID_GFX_SRV);
	version_query_wr(xe, map, wr_offset, header.command,
			 MKHI_GFX_SRV_GET_HOST_COMPATIBILITY_VERSION);

	return wr_offset + sizeof(struct gsc_get_compatibility_version_in);
}

#define GSC_VER_PKT_SZ SZ_4K /* 4K each for input and output */
static int query_compatibility_version(struct xe_gsc *gsc)
{
	struct xe_uc_fw_version *compat = &gsc->fw.versions.found[XE_UC_FW_VER_COMPATIBILITY];
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_bo *bo;
	u32 wr_offset;
	u32 rd_offset;
	u64 ggtt_offset;
	int err;

	bo = xe_bo_create_pin_map(xe, tile, NULL, GSC_VER_PKT_SZ * 2,
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM |
				  XE_BO_FLAG_GGTT);
	if (IS_ERR(bo)) {
		xe_gt_err(gt, "failed to allocate bo for GSC version query\n");
		return PTR_ERR(bo);
	}

	ggtt_offset = xe_bo_ggtt_addr(bo);

	wr_offset = xe_gsc_emit_header(xe, &bo->vmap, 0, HECI_MEADDRESS_MKHI, 0,
				       sizeof(struct gsc_get_compatibility_version_in));
	wr_offset = emit_version_query_msg(xe, &bo->vmap, wr_offset);

	err = xe_gsc_pkt_submit_kernel(gsc, ggtt_offset, wr_offset,
				       ggtt_offset + GSC_VER_PKT_SZ,
				       GSC_VER_PKT_SZ);
	if (err) {
		xe_gt_err(gt,
			  "failed to submit GSC request for compatibility version: %d\n",
			  err);
		goto out_bo;
	}

	err = xe_gsc_read_out_header(xe, &bo->vmap, GSC_VER_PKT_SZ,
				     sizeof(struct gsc_get_compatibility_version_out),
				     &rd_offset);
	if (err) {
		xe_gt_err(gt, "HuC: invalid GSC reply for version query (err=%d)\n", err);
		return err;
	}

	compat->major = version_query_rd(xe, &bo->vmap, rd_offset, proj_major);
	compat->minor = version_query_rd(xe, &bo->vmap, rd_offset, compat_major);
	compat->patch = version_query_rd(xe, &bo->vmap, rd_offset, compat_minor);

	xe_gt_info(gt, "found GSC cv%u.%u.%u\n", compat->major, compat->minor, compat->patch);

out_bo:
	xe_bo_unpin_map_no_vm(bo);
	return err;
}

static int gsc_fw_is_loaded(struct xe_gt *gt)
{
	return xe_mmio_read32(&gt->mmio, HECI_FWSTS1(MTL_GSC_HECI1_BASE)) &
			      HECI1_FWSTS1_INIT_COMPLETE;
}

static int gsc_fw_wait(struct xe_gt *gt)
{
	/*
	 * GSC load can take up to 250ms from the moment the instruction is
	 * executed by the GSCCS. To account for possible submission delays or
	 * other issues, we use a 500ms timeout in the wait here.
	 */
	return xe_mmio_wait32(&gt->mmio, HECI_FWSTS1(MTL_GSC_HECI1_BASE),
			      HECI1_FWSTS1_INIT_COMPLETE,
			      HECI1_FWSTS1_INIT_COMPLETE,
			      500 * USEC_PER_MSEC, NULL, false);
}

static int gsc_upload(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	/* we should only be here if the init step were successful */
	xe_assert(xe, xe_uc_fw_is_loadable(&gsc->fw) && gsc->q);

	if (gsc_fw_is_loaded(gt)) {
		xe_gt_err(gt, "GSC already loaded at upload time\n");
		return -EEXIST;
	}

	err = memcpy_fw(gsc);
	if (err) {
		xe_gt_err(gt, "Failed to memcpy GSC FW\n");
		return err;
	}

	/*
	 * GSC is only killed by an FLR, so we need to trigger one on unload to
	 * make sure we stop it. This is because we assign a chunk of memory to
	 * the GSC as part of the FW load, so we need to make sure it stops
	 * using it when we release it to the system on driver unload. Note that
	 * this is not a problem of the unload per-se, because the GSC will not
	 * touch that memory unless there are requests for it coming from the
	 * driver; therefore, no accesses will happen while Xe is not loaded,
	 * but if we re-load the driver then the GSC might wake up and try to
	 * access that old memory location again.
	 * Given that an FLR is a very disruptive action (see the FLR function
	 * for details), we want to do it as the last action before releasing
	 * the access to the MMIO bar, which means we need to do it as part of
	 * mmio cleanup.
	 */
	xe->needs_flr_on_fini = true;

	err = emit_gsc_upload(gsc);
	if (err) {
		xe_gt_err(gt, "Failed to emit GSC FW upload (%pe)\n", ERR_PTR(err));
		return err;
	}

	err = gsc_fw_wait(gt);
	if (err) {
		xe_gt_err(gt, "Failed to wait for GSC load (%pe)\n", ERR_PTR(err));
		return err;
	}

	err = query_compatibility_version(gsc);
	if (err)
		return err;

	err = xe_uc_fw_check_version_requirements(&gsc->fw);
	if (err)
		return err;

	return 0;
}

static int gsc_upload_and_init(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_tile *tile = gt_to_tile(gt);
	unsigned int fw_ref;
	int ret;

	if (XE_WA(tile->primary_gt, 14018094691)) {
		fw_ref = xe_force_wake_get(gt_to_fw(tile->primary_gt), XE_FORCEWAKE_ALL);

		/*
		 * If the forcewake fails we want to keep going, because the worst
		 * case outcome in failing to apply the WA is that PXP won't work,
		 * which is not fatal. Forcewake get warns implicitly in case of failure
		 */
		xe_gt_mcr_multicast_write(tile->primary_gt,
					  EU_SYSTOLIC_LIC_THROTTLE_CTL_WITH_LOCK,
					  EU_SYSTOLIC_LIC_THROTTLE_CTL_LOCK_BIT);
	}

	ret = gsc_upload(gsc);

	if (XE_WA(tile->primary_gt, 14018094691))
		xe_force_wake_put(gt_to_fw(tile->primary_gt), fw_ref);

	if (ret)
		return ret;

	xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_TRANSFERRED);

	/* GSC load is done, restore expected GT frequencies */
	xe_gt_sanitize_freq(gt);

	xe_gt_dbg(gt, "GSC FW async load completed\n");

	/* HuC auth failure is not fatal */
	if (xe_huc_is_authenticated(&gt->uc.huc, XE_HUC_AUTH_VIA_GUC))
		xe_huc_auth(&gt->uc.huc, XE_HUC_AUTH_VIA_GSC);

	ret = xe_gsc_proxy_start(gsc);
	if (ret)
		return ret;

	xe_gt_dbg(gt, "GSC proxy init completed\n");

	return 0;
}

static int gsc_er_complete(struct xe_gt *gt)
{
	u32 er_status;

	if (!gsc_fw_is_loaded(gt))
		return 0;

	/*
	 * Starting on Xe2, the GSCCS engine reset is a 2-step process. When the
	 * driver or the GuC hit the GDRST register, the CS is immediately reset
	 * and a success is reported, but the GSC shim keeps resetting in the
	 * background. While the shim reset is ongoing, the CS is able to accept
	 * new context submission, but any commands that require the shim will
	 * be stalled until the reset is completed. This means that we can keep
	 * submitting to the GSCCS as long as we make sure that the preemption
	 * timeout is big enough to cover any delay introduced by the reset.
	 * When the shim reset completes, a specific CS interrupt is triggered,
	 * in response to which we need to check the GSCI_TIMER_STATUS register
	 * to see if the reset was successful or not.
	 * Note that the GSCI_TIMER_STATUS register is not power save/restored,
	 * so it gets reset on MC6 entry. However, a reset failure stops MC6,
	 * so in that scenario we're always guaranteed to find the correct
	 * value.
	 */
	er_status = xe_mmio_read32(&gt->mmio, GSCI_TIMER_STATUS) & GSCI_TIMER_STATUS_VALUE;

	if (er_status == GSCI_TIMER_STATUS_TIMER_EXPIRED) {
		/*
		 * XXX: we should trigger an FLR here, but we don't have support
		 * for that yet. Since we can't recover from the error, we
		 * declare the device as wedged.
		 */
		xe_gt_err(gt, "GSC ER timed out!\n");
		xe_device_declare_wedged(gt_to_xe(gt));
		return -EIO;
	}

	return 0;
}

static void gsc_work(struct work_struct *work)
{
	struct xe_gsc *gsc = container_of(work, typeof(*gsc), work);
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int fw_ref;
	u32 actions;
	int ret;

	spin_lock_irq(&gsc->lock);
	actions = gsc->work_actions;
	gsc->work_actions = 0;
	spin_unlock_irq(&gsc->lock);

	xe_pm_runtime_get(xe);
	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GSC);

	if (actions & GSC_ACTION_ER_COMPLETE) {
		ret = gsc_er_complete(gt);
		if (ret)
			goto out;
	}

	if (actions & GSC_ACTION_FW_LOAD) {
		ret = gsc_upload_and_init(gsc);
		if (ret && ret != -EEXIST)
			xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_LOAD_FAIL);
		else
			xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_RUNNING);
	}

	if (actions & GSC_ACTION_SW_PROXY)
		xe_gsc_proxy_request_handler(gsc);

out:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	xe_pm_runtime_put(xe);
}

void xe_gsc_hwe_irq_handler(struct xe_hw_engine *hwe, u16 intr_vec)
{
	struct xe_gt *gt = hwe->gt;
	struct xe_gsc *gsc = &gt->uc.gsc;

	if (unlikely(!intr_vec))
		return;

	if (intr_vec & GSC_ER_COMPLETE) {
		spin_lock(&gsc->lock);
		gsc->work_actions |= GSC_ACTION_ER_COMPLETE;
		spin_unlock(&gsc->lock);

		queue_work(gsc->wq, &gsc->work);
	}
}

int xe_gsc_init(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_tile *tile = gt_to_tile(gt);
	int ret;

	gsc->fw.type = XE_UC_FW_TYPE_GSC;
	INIT_WORK(&gsc->work, gsc_work);
	spin_lock_init(&gsc->lock);

	/* The GSC uC is only available on the media GT */
	if (tile->media_gt && (gt != tile->media_gt)) {
		xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_NOT_SUPPORTED);
		return 0;
	}

	/*
	 * Some platforms can have GuC but not GSC. That would cause
	 * xe_uc_fw_init(gsc) to return a "not supported" failure code and abort
	 * all firmware loading. So check for GSC being enabled before
	 * propagating the failure back up. That way the higher level will keep
	 * going and load GuC as appropriate.
	 */
	ret = xe_uc_fw_init(&gsc->fw);
	if (!xe_uc_fw_is_enabled(&gsc->fw))
		return 0;
	else if (ret)
		goto out;

	ret = xe_gsc_proxy_init(gsc);
	if (ret && ret != -ENODEV)
		goto out;

	return 0;

out:
	xe_gt_err(gt, "GSC init failed with %d", ret);
	return ret;
}

static void free_resources(void *arg)
{
	struct xe_gsc *gsc = arg;

	if (gsc->wq) {
		destroy_workqueue(gsc->wq);
		gsc->wq = NULL;
	}

	if (gsc->q) {
		xe_exec_queue_put(gsc->q);
		gsc->q = NULL;
	}
}

int xe_gsc_init_post_hwconfig(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_hw_engine *hwe = xe_gt_hw_engine(gt, XE_ENGINE_CLASS_OTHER, 0, true);
	struct xe_exec_queue *q;
	struct workqueue_struct *wq;
	struct xe_bo *bo;
	int err;

	if (!xe_uc_fw_is_available(&gsc->fw))
		return 0;

	if (!hwe)
		return -ENODEV;

	bo = xe_managed_bo_create_pin_map(xe, tile, SZ_4M,
					  XE_BO_FLAG_STOLEN |
					  XE_BO_FLAG_GGTT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	q = xe_exec_queue_create(xe, NULL,
				 BIT(hwe->logical_instance), 1, hwe,
				 EXEC_QUEUE_FLAG_KERNEL |
				 EXEC_QUEUE_FLAG_PERMANENT, 0);
	if (IS_ERR(q)) {
		xe_gt_err(gt, "Failed to create queue for GSC submission\n");
		err = PTR_ERR(q);
		goto out_bo;
	}

	wq = alloc_ordered_workqueue("gsc-ordered-wq", 0);
	if (!wq) {
		err = -ENOMEM;
		goto out_q;
	}

	gsc->private = bo;
	gsc->q = q;
	gsc->wq = wq;

	err = devm_add_action_or_reset(xe->drm.dev, free_resources, gsc);
	if (err)
		return err;

	xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_LOADABLE);

	return 0;

out_q:
	xe_exec_queue_put(q);
out_bo:
	xe_bo_unpin_map_no_vm(bo);
	return err;
}

void xe_gsc_load_start(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_device *xe = gt_to_xe(gt);

	if (!xe_uc_fw_is_loadable(&gsc->fw) || !gsc->q)
		return;

	/*
	 * The GSC HW is only reset by driver FLR or D3cold entry. We don't
	 * support the former at runtime, while the latter is only supported on
	 * DGFX, for which we don't support GSC. Therefore, if GSC failed to
	 * load previously there is no need to try again because the HW is
	 * stuck in the error state.
	 */
	xe_assert(xe, !IS_DGFX(xe));
	if (xe_uc_fw_is_in_error_state(&gsc->fw))
		return;

	/* GSC FW survives GT reset and D3Hot */
	if (gsc_fw_is_loaded(gt)) {
		if (xe_gsc_proxy_init_done(gsc))
			xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_RUNNING);
		else
			xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_TRANSFERRED);
		return;
	}

	spin_lock_irq(&gsc->lock);
	gsc->work_actions |= GSC_ACTION_FW_LOAD;
	spin_unlock_irq(&gsc->lock);

	queue_work(gsc->wq, &gsc->work);
}

void xe_gsc_wait_for_worker_completion(struct xe_gsc *gsc)
{
	if (xe_uc_fw_is_loadable(&gsc->fw) && gsc->wq)
		flush_work(&gsc->work);
}

/**
 * xe_gsc_remove() - Clean up the GSC structures before driver removal
 * @gsc: the GSC uC
 */
void xe_gsc_remove(struct xe_gsc *gsc)
{
	xe_gsc_proxy_remove(gsc);
}

/*
 * wa_14015076503: if the GSC FW is loaded, we need to alert it before doing a
 * GSC engine reset by writing a notification bit in the GS1 register and then
 * triggering an interrupt to GSC; from the interrupt it will take up to 200ms
 * for the FW to get prepare for the reset, so we need to wait for that amount
 * of time.
 * After the reset is complete we need to then clear the GS1 register.
 */
void xe_gsc_wa_14015076503(struct xe_gt *gt, bool prep)
{
	u32 gs1_set = prep ? HECI_H_GS1_ER_PREP : 0;
	u32 gs1_clr = prep ? 0 : HECI_H_GS1_ER_PREP;

	/* WA only applies if the GSC is loaded */
	if (!XE_WA(gt, 14015076503) || !gsc_fw_is_loaded(gt))
		return;

	xe_mmio_rmw32(&gt->mmio, HECI_H_GS1(MTL_GSC_HECI2_BASE), gs1_clr, gs1_set);

	if (prep) {
		/* make sure the reset bit is clear when writing the CSR reg */
		xe_mmio_rmw32(&gt->mmio, HECI_H_CSR(MTL_GSC_HECI2_BASE),
			      HECI_H_CSR_RST, HECI_H_CSR_IG);
		msleep(200);
	}
}

/**
 * xe_gsc_print_info - print info about GSC FW status
 * @gsc: the GSC structure
 * @p: the printer to be used to print the info
 */
void xe_gsc_print_info(struct xe_gsc *gsc, struct drm_printer *p)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_mmio *mmio = &gt->mmio;
	unsigned int fw_ref;

	xe_uc_fw_print(&gsc->fw, p);

	drm_printf(p, "\tfound security version %u\n", gsc->security_version);

	if (!xe_uc_fw_is_enabled(&gsc->fw))
		return;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GSC);
	if (!fw_ref)
		return;

	drm_printf(p, "\nHECI1 FWSTS: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			xe_mmio_read32(mmio, HECI_FWSTS1(MTL_GSC_HECI1_BASE)),
			xe_mmio_read32(mmio, HECI_FWSTS2(MTL_GSC_HECI1_BASE)),
			xe_mmio_read32(mmio, HECI_FWSTS3(MTL_GSC_HECI1_BASE)),
			xe_mmio_read32(mmio, HECI_FWSTS4(MTL_GSC_HECI1_BASE)),
			xe_mmio_read32(mmio, HECI_FWSTS5(MTL_GSC_HECI1_BASE)),
			xe_mmio_read32(mmio, HECI_FWSTS6(MTL_GSC_HECI1_BASE)));

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}
