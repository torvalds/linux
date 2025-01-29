// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation.
 */

#include "xe_pxp.h"

#include <drm/drm_managed.h>

#include "xe_device_types.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_types.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_pxp_submit.h"
#include "xe_pxp_types.h"
#include "xe_uc_fw.h"
#include "regs/xe_irq_regs.h"
#include "regs/xe_pxp_regs.h"

/**
 * DOC: PXP
 *
 * PXP (Protected Xe Path) allows execution and flip to display of protected
 * (i.e. encrypted) objects. This feature is currently only supported in
 * integrated parts.
 */

#define ARB_SESSION DRM_XE_PXP_HWDRM_DEFAULT_SESSION /* shorter define */

bool xe_pxp_is_supported(const struct xe_device *xe)
{
	return xe->info.has_pxp && IS_ENABLED(CONFIG_INTEL_MEI_GSC_PROXY);
}

static bool pxp_is_enabled(const struct xe_pxp *pxp)
{
	return pxp;
}

static int pxp_wait_for_session_state(struct xe_pxp *pxp, u32 id, bool in_play)
{
	struct xe_gt *gt = pxp->gt;
	u32 mask = BIT(id);

	return xe_mmio_wait32(&gt->mmio, KCR_SIP, mask, in_play ? mask : 0,
			      250, NULL, false);
}

static void pxp_terminate(struct xe_pxp *pxp)
{
	int ret = 0;
	struct xe_device *xe = pxp->xe;
	struct xe_gt *gt = pxp->gt;
	unsigned int fw_ref;

	drm_dbg(&xe->drm, "Terminating PXP\n");

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FW_GT)) {
		ret = -EIO;
		goto out;
	}

	/* terminate the hw session */
	ret = xe_pxp_submit_session_termination(pxp, ARB_SESSION);
	if (ret)
		goto out;

	ret = pxp_wait_for_session_state(pxp, ARB_SESSION, false);
	if (ret)
		goto out;

	/* Trigger full HW cleanup */
	xe_mmio_write32(&gt->mmio, KCR_GLOBAL_TERMINATE, 1);

	/* now we can tell the GSC to clean up its own state */
	ret = xe_pxp_submit_session_invalidation(&pxp->gsc_res, ARB_SESSION);

out:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	if (ret)
		drm_err(&xe->drm, "PXP termination failed: %pe\n", ERR_PTR(ret));
}

static void pxp_terminate_complete(struct xe_pxp *pxp)
{
	/* TODO mark the session as ready to start */
}

static void pxp_irq_work(struct work_struct *work)
{
	struct xe_pxp *pxp = container_of(work, typeof(*pxp), irq.work);
	struct xe_device *xe = pxp->xe;
	u32 events = 0;

	spin_lock_irq(&xe->irq.lock);
	events = pxp->irq.events;
	pxp->irq.events = 0;
	spin_unlock_irq(&xe->irq.lock);

	if (!events)
		return;

	/*
	 * If we're processing a termination irq while suspending then don't
	 * bother, we're going to re-init everything on resume anyway.
	 */
	if ((events & PXP_TERMINATION_REQUEST) && !xe_pm_runtime_get_if_active(xe))
		return;

	if (events & PXP_TERMINATION_REQUEST) {
		events &= ~PXP_TERMINATION_COMPLETE;
		pxp_terminate(pxp);
	}

	if (events & PXP_TERMINATION_COMPLETE)
		pxp_terminate_complete(pxp);

	if (events & PXP_TERMINATION_REQUEST)
		xe_pm_runtime_put(xe);
}

/**
 * xe_pxp_irq_handler - Handles PXP interrupts.
 * @xe: the xe_device structure
 * @iir: interrupt vector
 */
void xe_pxp_irq_handler(struct xe_device *xe, u16 iir)
{
	struct xe_pxp *pxp = xe->pxp;

	if (!pxp_is_enabled(pxp)) {
		drm_err(&xe->drm, "PXP irq 0x%x received with PXP disabled!\n", iir);
		return;
	}

	lockdep_assert_held(&xe->irq.lock);

	if (unlikely(!iir))
		return;

	if (iir & (KCR_PXP_STATE_TERMINATED_INTERRUPT |
		   KCR_APP_TERMINATED_PER_FW_REQ_INTERRUPT))
		pxp->irq.events |= PXP_TERMINATION_REQUEST;

	if (iir & KCR_PXP_STATE_RESET_COMPLETE_INTERRUPT)
		pxp->irq.events |= PXP_TERMINATION_COMPLETE;

	if (pxp->irq.events)
		queue_work(pxp->irq.wq, &pxp->irq.work);
}

static int kcr_pxp_set_status(const struct xe_pxp *pxp, bool enable)
{
	u32 val = enable ? _MASKED_BIT_ENABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES) :
		  _MASKED_BIT_DISABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES);
	unsigned int fw_ref;

	fw_ref = xe_force_wake_get(gt_to_fw(pxp->gt), XE_FW_GT);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FW_GT))
		return -EIO;

	xe_mmio_write32(&pxp->gt->mmio, KCR_INIT, val);
	xe_force_wake_put(gt_to_fw(pxp->gt), fw_ref);

	return 0;
}

static int kcr_pxp_enable(const struct xe_pxp *pxp)
{
	return kcr_pxp_set_status(pxp, true);
}

static int kcr_pxp_disable(const struct xe_pxp *pxp)
{
	return kcr_pxp_set_status(pxp, false);
}

static void pxp_fini(void *arg)
{
	struct xe_pxp *pxp = arg;

	destroy_workqueue(pxp->irq.wq);
	xe_pxp_destroy_execution_resources(pxp);

	/* no need to explicitly disable KCR since we're going to do an FLR */
}

/**
 * xe_pxp_init - initialize PXP support
 * @xe: the xe_device structure
 *
 * Initialize the HW state and allocate the objects required for PXP support.
 * Note that some of the requirement for PXP support (GSC proxy init, HuC auth)
 * are performed asynchronously as part of the GSC init. PXP can only be used
 * after both this function and the async worker have completed.
 *
 * Returns -EOPNOTSUPP if PXP is not supported, 0 if PXP initialization is
 * successful, other errno value if there is an error during the init.
 */
int xe_pxp_init(struct xe_device *xe)
{
	struct xe_gt *gt = xe->tiles[0].media_gt;
	struct xe_pxp *pxp;
	int err;

	if (!xe_pxp_is_supported(xe))
		return -EOPNOTSUPP;

	/* we only support PXP on single tile devices with a media GT */
	if (xe->info.tile_count > 1 || !gt)
		return -EOPNOTSUPP;

	/* The GSCCS is required for submissions to the GSC FW */
	if (!(gt->info.engine_mask & BIT(XE_HW_ENGINE_GSCCS0)))
		return -EOPNOTSUPP;

	/* PXP requires both GSC and HuC firmwares to be available */
	if (!xe_uc_fw_is_loadable(&gt->uc.gsc.fw) ||
	    !xe_uc_fw_is_loadable(&gt->uc.huc.fw)) {
		drm_info(&xe->drm, "skipping PXP init due to missing FW dependencies");
		return -EOPNOTSUPP;
	}

	pxp = drmm_kzalloc(&xe->drm, sizeof(struct xe_pxp), GFP_KERNEL);
	if (!pxp)
		return -ENOMEM;

	INIT_WORK(&pxp->irq.work, pxp_irq_work);
	pxp->xe = xe;
	pxp->gt = gt;

	pxp->irq.wq = alloc_ordered_workqueue("pxp-wq", 0);
	if (!pxp->irq.wq) {
		err = -ENOMEM;
		goto out_free;
	}

	err = kcr_pxp_enable(pxp);
	if (err)
		goto out_wq;

	err = xe_pxp_allocate_execution_resources(pxp);
	if (err)
		goto out_kcr_disable;

	xe->pxp = pxp;

	return devm_add_action_or_reset(xe->drm.dev, pxp_fini, pxp);

out_kcr_disable:
	kcr_pxp_disable(pxp);
out_wq:
	destroy_workqueue(pxp->irq.wq);
out_free:
	drmm_kfree(&xe->drm, pxp);
	return err;
}
