// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_uc.h"

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gsc.h"
#include "xe_gsc_proxy.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_gt_sriov_vf.h"
#include "xe_guc.h"
#include "xe_guc_pc.h"
#include "xe_guc_engine_activity.h"
#include "xe_huc.h"
#include "xe_sriov.h"
#include "xe_uc_fw.h"
#include "xe_wopcm.h"

static struct xe_gt *
uc_to_gt(struct xe_uc *uc)
{
	return container_of(uc, struct xe_gt, uc);
}

static struct xe_device *
uc_to_xe(struct xe_uc *uc)
{
	return gt_to_xe(uc_to_gt(uc));
}

/* Should be called once at driver load only */
int xe_uc_init(struct xe_uc *uc)
{
	int ret;

	/*
	 * We call the GuC/HuC/GSC init functions even if GuC submission is off
	 * to correctly move our tracking of the FW state to "disabled".
	 */
	ret = xe_guc_init(&uc->guc);
	if (ret)
		goto err;

	ret = xe_huc_init(&uc->huc);
	if (ret)
		goto err;

	ret = xe_gsc_init(&uc->gsc);
	if (ret)
		goto err;

	if (!xe_device_uc_enabled(uc_to_xe(uc)))
		return 0;

	if (IS_SRIOV_VF(uc_to_xe(uc)))
		return 0;

	ret = xe_wopcm_init(&uc->wopcm);
	if (ret)
		goto err;

	return 0;

err:
	xe_gt_err(uc_to_gt(uc), "Failed to initialize uC (%pe)\n", ERR_PTR(ret));
	return ret;
}

/**
 * xe_uc_init_post_hwconfig - init Uc post hwconfig load
 * @uc: The UC object
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_uc_init_post_hwconfig(struct xe_uc *uc)
{
	int err;

	/* GuC submission not enabled, nothing to do */
	if (!xe_device_uc_enabled(uc_to_xe(uc)))
		return 0;

	err = xe_uc_sanitize_reset(uc);
	if (err)
		return err;

	err = xe_guc_init_post_hwconfig(&uc->guc);
	if (err)
		return err;

	err = xe_huc_init_post_hwconfig(&uc->huc);
	if (err)
		return err;

	return xe_gsc_init_post_hwconfig(&uc->gsc);
}

static int uc_reset(struct xe_uc *uc)
{
	struct xe_device *xe = uc_to_xe(uc);
	int ret;

	ret = xe_guc_reset(&uc->guc);
	if (ret) {
		drm_err(&xe->drm, "Failed to reset GuC, ret = %d\n", ret);
		return ret;
	}

	return 0;
}

static void xe_uc_sanitize(struct xe_uc *uc)
{
	xe_huc_sanitize(&uc->huc);
	xe_guc_sanitize(&uc->guc);
}

int xe_uc_sanitize_reset(struct xe_uc *uc)
{
	xe_uc_sanitize(uc);

	return uc_reset(uc);
}

/**
 * xe_uc_init_hwconfig - minimally init Uc, read and parse hwconfig
 * @uc: The UC object
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_uc_init_hwconfig(struct xe_uc *uc)
{
	int ret;

	/* GuC submission not enabled, nothing to do */
	if (!xe_device_uc_enabled(uc_to_xe(uc)))
		return 0;

	ret = xe_guc_min_load_for_hwconfig(&uc->guc);
	if (ret)
		return ret;

	return 0;
}

static int vf_uc_init_hw(struct xe_uc *uc)
{
	int err;

	err = xe_uc_sanitize_reset(uc);
	if (err)
		return err;

	err = xe_guc_enable_communication(&uc->guc);
	if (err)
		return err;

	err = xe_gt_sriov_vf_connect(uc_to_gt(uc));
	if (err)
		return err;

	uc->guc.submission_state.enabled = true;

	err = xe_gt_record_default_lrcs(uc_to_gt(uc));
	if (err)
		return err;

	return 0;
}

/*
 * Should be called during driver load, after every GT reset, and after every
 * suspend to reload / auth the firmwares.
 */
int xe_uc_init_hw(struct xe_uc *uc)
{
	int ret;

	/* GuC submission not enabled, nothing to do */
	if (!xe_device_uc_enabled(uc_to_xe(uc)))
		return 0;

	if (IS_SRIOV_VF(uc_to_xe(uc)))
		return vf_uc_init_hw(uc);

	ret = xe_huc_upload(&uc->huc);
	if (ret)
		return ret;

	ret = xe_guc_upload(&uc->guc);
	if (ret)
		return ret;

	ret = xe_guc_enable_communication(&uc->guc);
	if (ret)
		return ret;

	ret = xe_gt_record_default_lrcs(uc_to_gt(uc));
	if (ret)
		return ret;

	ret = xe_guc_post_load_init(&uc->guc);
	if (ret)
		return ret;

	ret = xe_guc_pc_start(&uc->guc.pc);
	if (ret)
		return ret;

	xe_guc_engine_activity_enable_stats(&uc->guc);

	/* We don't fail the driver load if HuC fails to auth, but let's warn */
	ret = xe_huc_auth(&uc->huc, XE_HUC_AUTH_VIA_GUC);
	xe_gt_assert(uc_to_gt(uc), !ret);

	/* GSC load is async */
	xe_gsc_load_start(&uc->gsc);

	return 0;
}

int xe_uc_fini_hw(struct xe_uc *uc)
{
	return xe_uc_sanitize_reset(uc);
}

int xe_uc_reset_prepare(struct xe_uc *uc)
{
	/* GuC submission not enabled, nothing to do */
	if (!xe_device_uc_enabled(uc_to_xe(uc)))
		return 0;

	return xe_guc_reset_prepare(&uc->guc);
}

void xe_uc_gucrc_disable(struct xe_uc *uc)
{
	XE_WARN_ON(xe_guc_pc_gucrc_disable(&uc->guc.pc));
}

void xe_uc_stop_prepare(struct xe_uc *uc)
{
	xe_gsc_stop_prepare(&uc->gsc);
	xe_guc_stop_prepare(&uc->guc);
}

void xe_uc_stop(struct xe_uc *uc)
{
	/* GuC submission not enabled, nothing to do */
	if (!xe_device_uc_enabled(uc_to_xe(uc)))
		return;

	xe_guc_stop(&uc->guc);
}

int xe_uc_start(struct xe_uc *uc)
{
	/* GuC submission not enabled, nothing to do */
	if (!xe_device_uc_enabled(uc_to_xe(uc)))
		return 0;

	return xe_guc_start(&uc->guc);
}

static void uc_reset_wait(struct xe_uc *uc)
{
	int ret;

again:
	xe_guc_reset_wait(&uc->guc);

	ret = xe_uc_reset_prepare(uc);
	if (ret)
		goto again;
}

void xe_uc_suspend_prepare(struct xe_uc *uc)
{
	xe_gsc_wait_for_worker_completion(&uc->gsc);
	xe_guc_stop_prepare(&uc->guc);
}

int xe_uc_suspend(struct xe_uc *uc)
{
	/* GuC submission not enabled, nothing to do */
	if (!xe_device_uc_enabled(uc_to_xe(uc)))
		return 0;

	uc_reset_wait(uc);

	xe_uc_stop(uc);

	return xe_guc_suspend(&uc->guc);
}

/**
 * xe_uc_declare_wedged() - Declare UC wedged
 * @uc: the UC object
 *
 * Wedge the UC which stops all submission, saves desired debug state, and
 * cleans up anything which could timeout.
 */
void xe_uc_declare_wedged(struct xe_uc *uc)
{
	xe_gt_assert(uc_to_gt(uc), uc_to_xe(uc)->wedged.mode);

	xe_guc_declare_wedged(&uc->guc);
}
