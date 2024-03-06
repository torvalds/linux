// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gsc.h"

#include <drm/drm_managed.h>

#include "abi/gsc_mkhi_commands_abi.h"
#include "generated/xe_wa_oob.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_gsc_submit.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_huc.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_sched_job.h"
#include "xe_uc_fw.h"
#include "xe_wa.h"
#include "instructions/xe_gsc_commands.h"
#include "regs/xe_gsc_regs.h"

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
				  XE_BO_CREATE_SYSTEM_BIT |
				  XE_BO_CREATE_GGTT_BIT);
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

	compat->major = version_query_rd(xe, &bo->vmap, rd_offset, compat_major);
	compat->minor = version_query_rd(xe, &bo->vmap, rd_offset, compat_minor);

	xe_gt_info(gt, "found GSC cv%u.%u\n", compat->major, compat->minor);

out_bo:
	xe_bo_unpin_map_no_vm(bo);
	return err;
}

static int gsc_fw_is_loaded(struct xe_gt *gt)
{
	return xe_mmio_read32(gt, HECI_FWSTS1(MTL_GSC_HECI1_BASE)) &
			      HECI1_FWSTS1_INIT_COMPLETE;
}

static int gsc_fw_wait(struct xe_gt *gt)
{
	/*
	 * GSC load can take up to 250ms from the moment the instruction is
	 * executed by the GSCCS. To account for possible submission delays or
	 * other issues, we use a 500ms timeout in the wait here.
	 */
	return xe_mmio_wait32(gt, HECI_FWSTS1(MTL_GSC_HECI1_BASE),
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

	xe_gt_dbg(gt, "GSC FW async load completed\n");

	return 0;
}

static void gsc_work(struct work_struct *work)
{
	struct xe_gsc *gsc = container_of(work, typeof(*gsc), work);
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_device *xe = gt_to_xe(gt);
	int ret;

	xe_device_mem_access_get(xe);
	xe_force_wake_get(gt_to_fw(gt), XE_FW_GSC);

	ret = gsc_upload(gsc);
	if (ret && ret != -EEXIST) {
		xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_LOAD_FAIL);
		goto out;
	}

	xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_TRANSFERRED);

	/* HuC auth failure is not fatal */
	if (xe_huc_is_authenticated(&gt->uc.huc, XE_HUC_AUTH_VIA_GUC))
		xe_huc_auth(&gt->uc.huc, XE_HUC_AUTH_VIA_GSC);

out:
	xe_force_wake_put(gt_to_fw(gt), XE_FW_GSC);
	xe_device_mem_access_put(xe);
}

int xe_gsc_init(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_tile *tile = gt_to_tile(gt);
	int ret;

	gsc->fw.type = XE_UC_FW_TYPE_GSC;
	INIT_WORK(&gsc->work, gsc_work);

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

	return 0;

out:
	xe_gt_err(gt, "GSC init failed with %d", ret);
	return ret;
}

static void free_resources(struct drm_device *drm, void *arg)
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

	if (gsc->private) {
		xe_bo_unpin_map_no_vm(gsc->private);
		gsc->private = NULL;
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

	bo = xe_bo_create_pin_map(xe, tile, NULL, SZ_4M,
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_STOLEN_BIT |
				  XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	q = xe_exec_queue_create(xe, NULL,
				 BIT(hwe->logical_instance), 1, hwe,
				 EXEC_QUEUE_FLAG_KERNEL |
				 EXEC_QUEUE_FLAG_PERMANENT);
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

	err = drmm_add_action_or_reset(&xe->drm, free_resources, gsc);
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

	if (!xe_uc_fw_is_loadable(&gsc->fw) || !gsc->q)
		return;

	/* GSC FW survives GT reset and D3Hot */
	if (gsc_fw_is_loaded(gt)) {
		xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_TRANSFERRED);
		return;
	}

	queue_work(gsc->wq, &gsc->work);
}

void xe_gsc_wait_for_worker_completion(struct xe_gsc *gsc)
{
	if (xe_uc_fw_is_loadable(&gsc->fw) && gsc->wq)
		flush_work(&gsc->work);
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

	xe_mmio_rmw32(gt, HECI_H_GS1(MTL_GSC_HECI2_BASE), gs1_clr, gs1_set);

	if (prep) {
		/* make sure the reset bit is clear when writing the CSR reg */
		xe_mmio_rmw32(gt, HECI_H_CSR(MTL_GSC_HECI2_BASE),
			      HECI_H_CSR_RST, HECI_H_CSR_IG);
		msleep(200);
	}
}
