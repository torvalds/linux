// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/types.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_print.h"
#include "intel_gsc_uc.h"
#include "intel_gsc_fw.h"
#include "i915_drv.h"
#include "intel_gsc_proxy.h"

static void gsc_work(struct work_struct *work)
{
	struct intel_gsc_uc *gsc = container_of(work, typeof(*gsc), work);
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	intel_wakeref_t wakeref;
	u32 actions;
	int ret;

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	spin_lock_irq(gt->irq_lock);
	actions = gsc->gsc_work_actions;
	gsc->gsc_work_actions = 0;
	spin_unlock_irq(gt->irq_lock);

	if (actions & GSC_ACTION_FW_LOAD) {
		ret = intel_gsc_uc_fw_upload(gsc);
		if (!ret)
			/* setup proxy on a new load */
			actions |= GSC_ACTION_SW_PROXY;
		else if (ret != -EEXIST)
			goto out_put;

		/*
		 * The HuC auth can be done both before or after the proxy init;
		 * if done after, a proxy request will be issued and must be
		 * serviced before the authentication can complete.
		 * Since this worker also handles proxy requests, we can't
		 * perform an action that requires the proxy from within it and
		 * then stall waiting for it, because we'd be blocking the
		 * service path. Therefore, it is easier for us to load HuC
		 * first and do proxy later. The GSC will ack the HuC auth and
		 * then send the HuC proxy request as part of the proxy init
		 * flow.
		 * Note that we can only do the GSC auth if the GuC auth was
		 * successful.
		 */
		if (intel_uc_uses_huc(&gt->uc) &&
		    intel_huc_is_authenticated(&gt->uc.huc, INTEL_HUC_AUTH_BY_GUC))
			intel_huc_auth(&gt->uc.huc, INTEL_HUC_AUTH_BY_GSC);
	}

	if (actions & GSC_ACTION_SW_PROXY) {
		if (!intel_gsc_uc_fw_init_done(gsc)) {
			gt_err(gt, "Proxy request received with GSC not loaded!\n");
			goto out_put;
		}

		ret = intel_gsc_proxy_request_handler(gsc);
		if (ret)
			goto out_put;

		/* mark the GSC FW init as done the first time we run this */
		if (actions & GSC_ACTION_FW_LOAD) {
			/*
			 * If there is a proxy establishment error, the GSC might still
			 * complete the request handling cleanly, so we need to check the
			 * status register to check if the proxy init was actually successful
			 */
			if (intel_gsc_uc_fw_proxy_init_done(gsc)) {
				drm_dbg(&gt->i915->drm, "GSC Proxy initialized\n");
				intel_uc_fw_change_status(&gsc->fw, INTEL_UC_FIRMWARE_RUNNING);
			} else {
				drm_err(&gt->i915->drm,
					"GSC status reports proxy init not complete\n");
			}
		}
	}

out_put:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
}

static bool gsc_engine_supported(struct intel_gt *gt)
{
	intel_engine_mask_t mask;

	/*
	 * We reach here from i915_driver_early_probe for the primary GT before
	 * its engine mask is set, so we use the device info engine mask for it.
	 * For other GTs we expect the GT-specific mask to be set before we
	 * call this function.
	 */
	GEM_BUG_ON(!gt_is_root(gt) && !gt->info.engine_mask);

	if (gt_is_root(gt))
		mask = RUNTIME_INFO(gt->i915)->platform_engine_mask;
	else
		mask = gt->info.engine_mask;

	return __HAS_ENGINE(mask, GSC0);
}

void intel_gsc_uc_init_early(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);

	/*
	 * GSC FW needs to be copied to a dedicated memory allocations for
	 * loading (see gsc->local), so we don't need to GGTT map the FW image
	 * itself into GGTT.
	 */
	intel_uc_fw_init_early(&gsc->fw, INTEL_UC_FW_TYPE_GSC, false);
	INIT_WORK(&gsc->work, gsc_work);

	/* we can arrive here from i915_driver_early_probe for primary
	 * GT with it being not fully setup hence check device info's
	 * engine mask
	 */
	if (!gsc_engine_supported(gt)) {
		intel_uc_fw_change_status(&gsc->fw, INTEL_UC_FIRMWARE_NOT_SUPPORTED);
		return;
	}

	gsc->wq = alloc_ordered_workqueue("i915_gsc", 0);
	if (!gsc->wq) {
		gt_err(gt, "failed to allocate WQ for GSC, disabling FW\n");
		intel_uc_fw_change_status(&gsc->fw, INTEL_UC_FIRMWARE_NOT_SUPPORTED);
	}
}

int intel_gsc_uc_init(struct intel_gsc_uc *gsc)
{
	static struct lock_class_key gsc_lock;
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct intel_engine_cs *engine = gt->engine[GSC0];
	struct intel_context *ce;
	struct i915_vma *vma;
	int err;

	err = intel_uc_fw_init(&gsc->fw);
	if (err)
		goto out;

	vma = intel_guc_allocate_vma(&gt->uc.guc, SZ_8M);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_fw;
	}

	gsc->local = vma;

	ce = intel_engine_create_pinned_context(engine, engine->gt->vm, SZ_4K,
						I915_GEM_HWS_GSC_ADDR,
						&gsc_lock, "gsc_context");
	if (IS_ERR(ce)) {
		gt_err(gt, "failed to create GSC CS ctx for FW communication\n");
		err =  PTR_ERR(ce);
		goto out_vma;
	}

	gsc->ce = ce;

	/* if we fail to init proxy we still want to load GSC for PM */
	intel_gsc_proxy_init(gsc);

	intel_uc_fw_change_status(&gsc->fw, INTEL_UC_FIRMWARE_LOADABLE);

	return 0;

out_vma:
	i915_vma_unpin_and_release(&gsc->local, 0);
out_fw:
	intel_uc_fw_fini(&gsc->fw);
out:
	gt_probe_error(gt, "GSC init failed %pe\n", ERR_PTR(err));
	return err;
}

void intel_gsc_uc_fini(struct intel_gsc_uc *gsc)
{
	if (!intel_uc_fw_is_loadable(&gsc->fw))
		return;

	flush_work(&gsc->work);
	if (gsc->wq) {
		destroy_workqueue(gsc->wq);
		gsc->wq = NULL;
	}

	intel_gsc_proxy_fini(gsc);

	if (gsc->ce)
		intel_engine_destroy_pinned_context(fetch_and_zero(&gsc->ce));

	i915_vma_unpin_and_release(&gsc->local, 0);

	intel_uc_fw_fini(&gsc->fw);
}

void intel_gsc_uc_flush_work(struct intel_gsc_uc *gsc)
{
	if (!intel_uc_fw_is_loadable(&gsc->fw))
		return;

	flush_work(&gsc->work);
}

void intel_gsc_uc_resume(struct intel_gsc_uc *gsc)
{
	if (!intel_uc_fw_is_loadable(&gsc->fw))
		return;

	/*
	 * we only want to start the GSC worker from here in the actual resume
	 * flow and not during driver load. This is because GSC load is slow and
	 * therefore we want to make sure that the default state init completes
	 * first to not slow down the init thread. A separate call to
	 * intel_gsc_uc_load_start will ensure that the GSC is loaded during
	 * driver load.
	 */
	if (!gsc_uc_to_gt(gsc)->engine[GSC0]->default_state)
		return;

	intel_gsc_uc_load_start(gsc);
}

void intel_gsc_uc_load_start(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);

	if (!intel_uc_fw_is_loadable(&gsc->fw))
		return;

	if (intel_gsc_uc_fw_init_done(gsc))
		return;

	spin_lock_irq(gt->irq_lock);
	gsc->gsc_work_actions |= GSC_ACTION_FW_LOAD;
	spin_unlock_irq(gt->irq_lock);

	queue_work(gsc->wq, &gsc->work);
}
