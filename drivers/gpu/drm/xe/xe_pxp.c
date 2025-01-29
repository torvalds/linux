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
#include "xe_pxp_submit.h"
#include "xe_pxp_types.h"
#include "xe_uc_fw.h"
#include "regs/xe_pxp_regs.h"

/**
 * DOC: PXP
 *
 * PXP (Protected Xe Path) allows execution and flip to display of protected
 * (i.e. encrypted) objects. This feature is currently only supported in
 * integrated parts.
 */

static bool pxp_is_supported(const struct xe_device *xe)
{
	return xe->info.has_pxp && IS_ENABLED(CONFIG_INTEL_MEI_GSC_PROXY);
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

	if (!pxp_is_supported(xe))
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

	pxp->xe = xe;
	pxp->gt = gt;

	err = kcr_pxp_enable(pxp);
	if (err)
		goto out_free;

	err = xe_pxp_allocate_execution_resources(pxp);
	if (err)
		goto out_kcr_disable;

	xe->pxp = pxp;

	return devm_add_action_or_reset(xe->drm.dev, pxp_fini, pxp);

out_kcr_disable:
	kcr_pxp_disable(pxp);
out_free:
	drmm_kfree(&xe->drm, pxp);
	return err;
}
