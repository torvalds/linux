// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation.
 */

#include "xe_pxp.h"

#include <drm/drm_managed.h>
#include <uapi/drm/xe_drm.h>

#include "xe_bo.h"
#include "xe_bo_types.h"
#include "xe_device_types.h"
#include "xe_exec_queue.h"
#include "xe_force_wake.h"
#include "xe_guc_submit.h"
#include "xe_gsc_proxy.h"
#include "xe_gt.h"
#include "xe_gt_types.h"
#include "xe_huc.h"
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

/*
 * A submission to GSC can take up to 250ms to complete, so use a 300ms
 * timeout for activation where only one of those is involved. Termination
 * additionally requires a submission to VCS and an interaction with KCR, so
 * bump the timeout to 500ms for that.
 */
#define PXP_ACTIVATION_TIMEOUT_MS 300
#define PXP_TERMINATION_TIMEOUT_MS 500

bool xe_pxp_is_supported(const struct xe_device *xe)
{
	return xe->info.has_pxp && IS_ENABLED(CONFIG_INTEL_MEI_GSC_PROXY);
}

bool xe_pxp_is_enabled(const struct xe_pxp *pxp)
{
	return pxp;
}

static bool pxp_prerequisites_done(const struct xe_pxp *pxp)
{
	struct xe_gt *gt = pxp->gt;
	unsigned int fw_ref;
	bool ready;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);

	/*
	 * If force_wake fails we could falsely report the prerequisites as not
	 * done even if they are; the consequence of this would be that the
	 * callers won't go ahead with using PXP, but if force_wake doesn't work
	 * the GT is very likely in a bad state so not really a problem to abort
	 * PXP. Therefore, we can just log the force_wake error and not escalate
	 * it.
	 */
	XE_WARN_ON(!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL));

	/* PXP requires both HuC authentication via GSC and GSC proxy initialized */
	ready = xe_huc_is_authenticated(&gt->uc.huc, XE_HUC_AUTH_VIA_GSC) &&
		xe_gsc_proxy_init_done(&gt->uc.gsc);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	return ready;
}

/**
 * xe_pxp_get_readiness_status - check whether PXP is ready for userspace use
 * @pxp: the xe_pxp pointer (can be NULL if PXP is disabled)
 *
 * Returns: 0 if PXP is not ready yet, 1 if it is ready, a negative errno value
 * if PXP is not supported/enabled or if something went wrong in the
 * initialization of the prerequisites. Note that the return values of this
 * function follow the uapi (see drm_xe_query_pxp_status), so they can be used
 * directly in the query ioctl.
 */
int xe_pxp_get_readiness_status(struct xe_pxp *pxp)
{
	int ret = 0;

	if (!xe_pxp_is_enabled(pxp))
		return -ENODEV;

	/* if the GSC or HuC FW are in an error state, PXP will never work */
	if (xe_uc_fw_status_to_error(pxp->gt->uc.huc.fw.status) ||
	    xe_uc_fw_status_to_error(pxp->gt->uc.gsc.fw.status))
		return -EIO;

	xe_pm_runtime_get(pxp->xe);

	/* PXP requires both HuC loaded and GSC proxy initialized */
	if (pxp_prerequisites_done(pxp))
		ret = 1;

	xe_pm_runtime_put(pxp->xe);
	return ret;
}

static bool pxp_session_is_in_play(struct xe_pxp *pxp, u32 id)
{
	struct xe_gt *gt = pxp->gt;

	return xe_mmio_read32(&gt->mmio, KCR_SIP) & BIT(id);
}

static int pxp_wait_for_session_state(struct xe_pxp *pxp, u32 id, bool in_play)
{
	struct xe_gt *gt = pxp->gt;
	u32 mask = BIT(id);

	return xe_mmio_wait32(&gt->mmio, KCR_SIP, mask, in_play ? mask : 0,
			      250, NULL, false);
}

static void pxp_invalidate_queues(struct xe_pxp *pxp);

static int pxp_terminate_hw(struct xe_pxp *pxp)
{
	struct xe_gt *gt = pxp->gt;
	unsigned int fw_ref;
	int ret = 0;

	drm_dbg(&pxp->xe->drm, "Terminating PXP\n");

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
	return ret;
}

static void mark_termination_in_progress(struct xe_pxp *pxp)
{
	lockdep_assert_held(&pxp->mutex);

	reinit_completion(&pxp->termination);
	pxp->status = XE_PXP_TERMINATION_IN_PROGRESS;
}

static void pxp_terminate(struct xe_pxp *pxp)
{
	int ret = 0;
	struct xe_device *xe = pxp->xe;

	if (!wait_for_completion_timeout(&pxp->activation,
					 msecs_to_jiffies(PXP_ACTIVATION_TIMEOUT_MS)))
		drm_err(&xe->drm, "failed to wait for PXP start before termination\n");

	mutex_lock(&pxp->mutex);

	if (pxp->status == XE_PXP_ACTIVE)
		pxp->key_instance++;

	/*
	 * we'll mark the status as needing termination on resume, so no need to
	 * emit a termination now.
	 */
	if (pxp->status == XE_PXP_SUSPENDED) {
		mutex_unlock(&pxp->mutex);
		return;
	}

	/*
	 * If we have a termination already in progress, we need to wait for
	 * it to complete before queueing another one. Once the first
	 * termination is completed we'll set the state back to
	 * NEEDS_TERMINATION and leave it to the pxp start code to issue it.
	 */
	if (pxp->status == XE_PXP_TERMINATION_IN_PROGRESS) {
		pxp->status = XE_PXP_NEEDS_ADDITIONAL_TERMINATION;
		mutex_unlock(&pxp->mutex);
		return;
	}

	mark_termination_in_progress(pxp);

	mutex_unlock(&pxp->mutex);

	pxp_invalidate_queues(pxp);

	ret = pxp_terminate_hw(pxp);
	if (ret) {
		drm_err(&xe->drm, "PXP termination failed: %pe\n", ERR_PTR(ret));
		mutex_lock(&pxp->mutex);
		pxp->status = XE_PXP_ERROR;
		complete_all(&pxp->termination);
		mutex_unlock(&pxp->mutex);
	}
}

static void pxp_terminate_complete(struct xe_pxp *pxp)
{
	/*
	 * We expect PXP to be in one of 3 states when we get here:
	 * - XE_PXP_TERMINATION_IN_PROGRESS: a single termination event was
	 * requested and it is now completing, so we're ready to start.
	 * - XE_PXP_NEEDS_ADDITIONAL_TERMINATION: a second termination was
	 * requested while the first one was still being processed.
	 * - XE_PXP_SUSPENDED: PXP is now suspended, so we defer everything to
	 * when we come back on resume.
	 */
	mutex_lock(&pxp->mutex);

	switch (pxp->status) {
	case XE_PXP_TERMINATION_IN_PROGRESS:
		pxp->status = XE_PXP_READY_TO_START;
		break;
	case XE_PXP_NEEDS_ADDITIONAL_TERMINATION:
		pxp->status = XE_PXP_NEEDS_TERMINATION;
		break;
	case XE_PXP_SUSPENDED:
		/* Nothing to do */
		break;
	default:
		drm_err(&pxp->xe->drm,
			"PXP termination complete while status was %u\n",
			pxp->status);
	}

	complete_all(&pxp->termination);

	mutex_unlock(&pxp->mutex);
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

	if (!xe_pxp_is_enabled(pxp)) {
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
 * Returns 0 if PXP is not supported or if PXP initialization is successful,
 * other errno value if there is an error during the init.
 */
int xe_pxp_init(struct xe_device *xe)
{
	struct xe_gt *gt = xe->tiles[0].media_gt;
	struct xe_pxp *pxp;
	int err;

	if (!xe_pxp_is_supported(xe))
		return 0;

	/* we only support PXP on single tile devices with a media GT */
	if (xe->info.tile_count > 1 || !gt)
		return 0;

	/* The GSCCS is required for submissions to the GSC FW */
	if (!(gt->info.engine_mask & BIT(XE_HW_ENGINE_GSCCS0)))
		return 0;

	/* PXP requires both GSC and HuC firmwares to be available */
	if (!xe_uc_fw_is_loadable(&gt->uc.gsc.fw) ||
	    !xe_uc_fw_is_loadable(&gt->uc.huc.fw)) {
		drm_info(&xe->drm, "skipping PXP init due to missing FW dependencies");
		return 0;
	}

	pxp = drmm_kzalloc(&xe->drm, sizeof(struct xe_pxp), GFP_KERNEL);
	if (!pxp) {
		err = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&pxp->queues.list);
	spin_lock_init(&pxp->queues.lock);
	INIT_WORK(&pxp->irq.work, pxp_irq_work);
	pxp->xe = xe;
	pxp->gt = gt;

	pxp->key_instance = 1;
	pxp->last_suspend_key_instance = 1;

	/*
	 * we'll use the completions to check if there is an action pending,
	 * so we start them as completed and we reinit it when an action is
	 * triggered.
	 */
	init_completion(&pxp->activation);
	init_completion(&pxp->termination);
	complete_all(&pxp->termination);
	complete_all(&pxp->activation);

	mutex_init(&pxp->mutex);

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
out:
	drm_err(&xe->drm, "PXP initialization failed: %pe\n", ERR_PTR(err));
	return err;
}

static int __pxp_start_arb_session(struct xe_pxp *pxp)
{
	int ret;
	unsigned int fw_ref;

	fw_ref = xe_force_wake_get(gt_to_fw(pxp->gt), XE_FW_GT);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FW_GT))
		return -EIO;

	if (pxp_session_is_in_play(pxp, ARB_SESSION)) {
		ret = -EEXIST;
		goto out_force_wake;
	}

	ret = xe_pxp_submit_session_init(&pxp->gsc_res, ARB_SESSION);
	if (ret) {
		drm_err(&pxp->xe->drm, "Failed to init PXP arb session: %pe\n", ERR_PTR(ret));
		goto out_force_wake;
	}

	ret = pxp_wait_for_session_state(pxp, ARB_SESSION, true);
	if (ret) {
		drm_err(&pxp->xe->drm, "PXP ARB session failed to go in play%pe\n", ERR_PTR(ret));
		goto out_force_wake;
	}

	drm_dbg(&pxp->xe->drm, "PXP ARB session is active\n");

out_force_wake:
	xe_force_wake_put(gt_to_fw(pxp->gt), fw_ref);
	return ret;
}

/**
 * xe_pxp_exec_queue_set_type - Mark a queue as using PXP
 * @pxp: the xe->pxp pointer (it will be NULL if PXP is disabled)
 * @q: the queue to mark as using PXP
 * @type: the type of PXP session this queue will use
 *
 * Returns 0 if the selected PXP type is supported, -ENODEV otherwise.
 */
int xe_pxp_exec_queue_set_type(struct xe_pxp *pxp, struct xe_exec_queue *q, u8 type)
{
	if (!xe_pxp_is_enabled(pxp))
		return -ENODEV;

	/* we only support HWDRM sessions right now */
	xe_assert(pxp->xe, type == DRM_XE_PXP_TYPE_HWDRM);

	q->pxp.type = type;

	return 0;
}

static int __exec_queue_add(struct xe_pxp *pxp, struct xe_exec_queue *q)
{
	int ret = 0;

	/*
	 * A queue can be added to the list only if the PXP is in active status,
	 * otherwise the termination might not handle it correctly.
	 */
	mutex_lock(&pxp->mutex);

	if (pxp->status == XE_PXP_ACTIVE) {
		spin_lock_irq(&pxp->queues.lock);
		list_add_tail(&q->pxp.link, &pxp->queues.list);
		spin_unlock_irq(&pxp->queues.lock);
	} else if (pxp->status == XE_PXP_ERROR || pxp->status == XE_PXP_SUSPENDED) {
		ret = -EIO;
	} else {
		ret = -EBUSY; /* try again later */
	}

	mutex_unlock(&pxp->mutex);

	return ret;
}

static int pxp_start(struct xe_pxp *pxp, u8 type)
{
	int ret = 0;
	bool restart = false;

	if (!xe_pxp_is_enabled(pxp))
		return -ENODEV;

	/* we only support HWDRM sessions right now */
	xe_assert(pxp->xe, type == DRM_XE_PXP_TYPE_HWDRM);

	/* get_readiness_status() returns 0 for in-progress and 1 for done */
	ret = xe_pxp_get_readiness_status(pxp);
	if (ret <= 0)
		return ret ?: -EBUSY;

	ret = 0;

wait_for_idle:
	/*
	 * if there is an action in progress, wait for it. We need to wait
	 * outside the lock because the completion is done from within the lock.
	 * Note that the two actions should never be pending at the same time.
	 */
	if (!wait_for_completion_timeout(&pxp->termination,
					 msecs_to_jiffies(PXP_TERMINATION_TIMEOUT_MS)))
		return -ETIMEDOUT;

	if (!wait_for_completion_timeout(&pxp->activation,
					 msecs_to_jiffies(PXP_ACTIVATION_TIMEOUT_MS)))
		return -ETIMEDOUT;

	mutex_lock(&pxp->mutex);

	/* If PXP is not already active, turn it on */
	switch (pxp->status) {
	case XE_PXP_ERROR:
		ret = -EIO;
		goto out_unlock;
	case XE_PXP_ACTIVE:
		goto out_unlock;
	case XE_PXP_READY_TO_START:
		pxp->status = XE_PXP_START_IN_PROGRESS;
		reinit_completion(&pxp->activation);
		break;
	case XE_PXP_START_IN_PROGRESS:
		/* If a start is in progress then the completion must not be done */
		XE_WARN_ON(completion_done(&pxp->activation));
		restart = true;
		goto out_unlock;
	case XE_PXP_NEEDS_TERMINATION:
		mark_termination_in_progress(pxp);
		break;
	case XE_PXP_TERMINATION_IN_PROGRESS:
	case XE_PXP_NEEDS_ADDITIONAL_TERMINATION:
		/* If a termination is in progress then the completion must not be done */
		XE_WARN_ON(completion_done(&pxp->termination));
		restart = true;
		goto out_unlock;
	case XE_PXP_SUSPENDED:
	default:
		drm_err(&pxp->xe->drm, "unexpected state during PXP start: %u\n", pxp->status);
		ret = -EIO;
		goto out_unlock;
	}

	mutex_unlock(&pxp->mutex);

	if (!completion_done(&pxp->termination)) {
		ret = pxp_terminate_hw(pxp);
		if (ret) {
			drm_err(&pxp->xe->drm, "PXP termination failed before start\n");
			mutex_lock(&pxp->mutex);
			pxp->status = XE_PXP_ERROR;

			goto out_unlock;
		}

		goto wait_for_idle;
	}

	/* All the cases except for start should have exited earlier */
	XE_WARN_ON(completion_done(&pxp->activation));
	ret = __pxp_start_arb_session(pxp);

	mutex_lock(&pxp->mutex);

	complete_all(&pxp->activation);

	/*
	 * Any other process should wait until the state goes away from
	 * XE_PXP_START_IN_PROGRESS, so if the state is not that something went
	 * wrong. Mark the status as needing termination and try again.
	 */
	if (pxp->status != XE_PXP_START_IN_PROGRESS) {
		drm_err(&pxp->xe->drm, "unexpected state after PXP start: %u\n", pxp->status);
		pxp->status = XE_PXP_NEEDS_TERMINATION;
		restart = true;
		goto out_unlock;
	}

	/* If everything went ok, update the status and add the queue to the list */
	if (!ret)
		pxp->status = XE_PXP_ACTIVE;
	else
		pxp->status = XE_PXP_ERROR;

out_unlock:
	mutex_unlock(&pxp->mutex);

	if (restart)
		goto wait_for_idle;

	return ret;
}

/**
 * xe_pxp_exec_queue_add - add a queue to the PXP list
 * @pxp: the xe->pxp pointer (it will be NULL if PXP is disabled)
 * @q: the queue to add to the list
 *
 * If PXP is enabled and the prerequisites are done, start the PXP default
 * session (if not already running) and add the queue to the PXP list.
 *
 * Returns 0 if the PXP session is running and the queue is in the list,
 * -ENODEV if PXP is disabled, -EBUSY if the PXP prerequisites are not done,
 * other errno value if something goes wrong during the session start.
 */
int xe_pxp_exec_queue_add(struct xe_pxp *pxp, struct xe_exec_queue *q)
{
	int ret;

	if (!xe_pxp_is_enabled(pxp))
		return -ENODEV;

	/*
	 * Runtime suspend kills PXP, so we take a reference to prevent it from
	 * happening while we have active queues that use PXP
	 */
	xe_pm_runtime_get(pxp->xe);

start:
	ret = pxp_start(pxp, q->pxp.type);

	if (!ret) {
		ret = __exec_queue_add(pxp, q);
		if (ret == -EBUSY)
			goto start;
	}

	/*
	 * in the successful case the PM ref is released from
	 * xe_pxp_exec_queue_remove
	 */
	if (ret)
		xe_pm_runtime_put(pxp->xe);

	return ret;
}
ALLOW_ERROR_INJECTION(xe_pxp_exec_queue_add, ERRNO);

static void __pxp_exec_queue_remove(struct xe_pxp *pxp, struct xe_exec_queue *q, bool lock)
{
	bool need_pm_put = false;

	if (!xe_pxp_is_enabled(pxp))
		return;

	if (lock)
		spin_lock_irq(&pxp->queues.lock);

	if (!list_empty(&q->pxp.link)) {
		list_del_init(&q->pxp.link);
		need_pm_put = true;
	}

	q->pxp.type = DRM_XE_PXP_TYPE_NONE;

	if (lock)
		spin_unlock_irq(&pxp->queues.lock);

	if (need_pm_put)
		xe_pm_runtime_put(pxp->xe);
}

/**
 * xe_pxp_exec_queue_remove - remove a queue from the PXP list
 * @pxp: the xe->pxp pointer (it will be NULL if PXP is disabled)
 * @q: the queue to remove from the list
 *
 * If PXP is enabled and the exec_queue is in the list, the queue will be
 * removed from the list and its PM reference will be released. It is safe to
 * call this function multiple times for the same queue.
 */
void xe_pxp_exec_queue_remove(struct xe_pxp *pxp, struct xe_exec_queue *q)
{
	__pxp_exec_queue_remove(pxp, q, true);
}

static void pxp_invalidate_queues(struct xe_pxp *pxp)
{
	struct xe_exec_queue *tmp, *q;
	LIST_HEAD(to_clean);

	spin_lock_irq(&pxp->queues.lock);

	list_for_each_entry_safe(q, tmp, &pxp->queues.list, pxp.link) {
		q = xe_exec_queue_get_unless_zero(q);
		if (!q)
			continue;

		list_move_tail(&q->pxp.link, &to_clean);
	}
	spin_unlock_irq(&pxp->queues.lock);

	list_for_each_entry_safe(q, tmp, &to_clean, pxp.link) {
		xe_exec_queue_kill(q);

		/*
		 * We hold a ref to the queue so there is no risk of racing with
		 * the calls to exec_queue_remove coming from exec_queue_destroy.
		 */
		__pxp_exec_queue_remove(pxp, q, false);

		xe_exec_queue_put(q);
	}
}

/**
 * xe_pxp_key_assign - mark a BO as using the current PXP key iteration
 * @pxp: the xe->pxp pointer (it will be NULL if PXP is disabled)
 * @bo: the BO to mark
 *
 * Returns: -ENODEV if PXP is disabled, 0 otherwise.
 */
int xe_pxp_key_assign(struct xe_pxp *pxp, struct xe_bo *bo)
{
	if (!xe_pxp_is_enabled(pxp))
		return -ENODEV;

	xe_assert(pxp->xe, !bo->pxp_key_instance);

	/*
	 * Note that the PXP key handling is inherently racey, because the key
	 * can theoretically change at any time (although it's unlikely to do
	 * so without triggers), even right after we copy it. Taking a lock
	 * wouldn't help because the value might still change as soon as we
	 * release the lock.
	 * Userspace needs to handle the fact that their BOs can go invalid at
	 * any point.
	 */
	bo->pxp_key_instance = pxp->key_instance;

	return 0;
}

/**
 * xe_pxp_bo_key_check - check if the key used by a xe_bo is valid
 * @pxp: the xe->pxp pointer (it will be NULL if PXP is disabled)
 * @bo: the BO we want to check
 *
 * Checks whether a BO was encrypted with the current key or an obsolete one.
 *
 * Returns: 0 if the key is valid, -ENODEV if PXP is disabled, -EINVAL if the
 * BO is not using PXP,  -ENOEXEC if the key is not valid.
 */
int xe_pxp_bo_key_check(struct xe_pxp *pxp, struct xe_bo *bo)
{
	if (!xe_pxp_is_enabled(pxp))
		return -ENODEV;

	if (!xe_bo_is_protected(bo))
		return -EINVAL;

	xe_assert(pxp->xe, bo->pxp_key_instance);

	/*
	 * Note that the PXP key handling is inherently racey, because the key
	 * can theoretically change at any time (although it's unlikely to do
	 * so without triggers), even right after we check it. Taking a lock
	 * wouldn't help because the value might still change as soon as we
	 * release the lock.
	 * We mitigate the risk by checking the key at multiple points (on each
	 * submission involving the BO and right before flipping it on the
	 * display), but there is still a very small chance that we could
	 * operate on an invalid BO for a single submission or a single frame
	 * flip. This is a compromise made to protect the encrypted data (which
	 * is what the key termination is for).
	 */
	if (bo->pxp_key_instance != pxp->key_instance)
		return -ENOEXEC;

	return 0;
}

/**
 * xe_pxp_obj_key_check - check if the key used by a drm_gem_obj is valid
 * @obj: the drm_gem_obj we want to check
 *
 * Checks whether a drm_gem_obj was encrypted with the current key or an
 * obsolete one.
 *
 * Returns: 0 if the key is valid, -ENODEV if PXP is disabled, -EINVAL if the
 * obj is not using PXP,  -ENOEXEC if the key is not valid.
 */
int xe_pxp_obj_key_check(struct drm_gem_object *obj)
{
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_pxp *pxp = xe->pxp;

	return xe_pxp_bo_key_check(pxp, bo);
}

/**
 * xe_pxp_pm_suspend - prepare PXP for HW suspend
 * @pxp: the xe->pxp pointer (it will be NULL if PXP is disabled)
 *
 * Makes sure all PXP actions have completed and invalidates all PXP queues
 * and objects before we go into a suspend state.
 *
 * Returns: 0 if successful, a negative errno value otherwise.
 */
int xe_pxp_pm_suspend(struct xe_pxp *pxp)
{
	bool needs_queue_inval = false;
	int ret = 0;

	if (!xe_pxp_is_enabled(pxp))
		return 0;

wait_for_activation:
	if (!wait_for_completion_timeout(&pxp->activation,
					 msecs_to_jiffies(PXP_ACTIVATION_TIMEOUT_MS)))
		ret = -ETIMEDOUT;

	mutex_lock(&pxp->mutex);

	switch (pxp->status) {
	case XE_PXP_ERROR:
	case XE_PXP_READY_TO_START:
	case XE_PXP_SUSPENDED:
	case XE_PXP_TERMINATION_IN_PROGRESS:
	case XE_PXP_NEEDS_ADDITIONAL_TERMINATION:
		/*
		 * If PXP is not running there is nothing to cleanup. If there
		 * is a termination pending then no need to issue another one.
		 */
		break;
	case XE_PXP_START_IN_PROGRESS:
		mutex_unlock(&pxp->mutex);
		goto wait_for_activation;
	case XE_PXP_NEEDS_TERMINATION:
		/* If PXP was never used we can skip the cleanup */
		if (pxp->key_instance == pxp->last_suspend_key_instance)
			break;
		fallthrough;
	case XE_PXP_ACTIVE:
		pxp->key_instance++;
		needs_queue_inval = true;
		break;
	default:
		drm_err(&pxp->xe->drm, "unexpected state during PXP suspend: %u",
			pxp->status);
		ret = -EIO;
		goto out;
	}

	/*
	 * We set this even if we were in error state, hoping the suspend clears
	 * the error. Worse case we fail again and go in error state again.
	 */
	pxp->status = XE_PXP_SUSPENDED;

	mutex_unlock(&pxp->mutex);

	if (needs_queue_inval)
		pxp_invalidate_queues(pxp);

	/*
	 * if there is a termination in progress, wait for it.
	 * We need to wait outside the lock because the completion is done from
	 * within the lock
	 */
	if (!wait_for_completion_timeout(&pxp->termination,
					 msecs_to_jiffies(PXP_TERMINATION_TIMEOUT_MS)))
		ret = -ETIMEDOUT;

	pxp->last_suspend_key_instance = pxp->key_instance;

out:
	return ret;
}

/**
 * xe_pxp_pm_resume - re-init PXP after HW suspend
 * @pxp: the xe->pxp pointer (it will be NULL if PXP is disabled)
 */
void xe_pxp_pm_resume(struct xe_pxp *pxp)
{
	int err;

	if (!xe_pxp_is_enabled(pxp))
		return;

	err = kcr_pxp_enable(pxp);

	mutex_lock(&pxp->mutex);

	xe_assert(pxp->xe, pxp->status == XE_PXP_SUSPENDED);

	if (err)
		pxp->status = XE_PXP_ERROR;
	else
		pxp->status = XE_PXP_NEEDS_TERMINATION;

	mutex_unlock(&pxp->mutex);
}
