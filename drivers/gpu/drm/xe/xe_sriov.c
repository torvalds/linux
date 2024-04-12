// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_sriov.h"

/**
 * xe_sriov_mode_to_string - Convert enum value to string.
 * @mode: the &xe_sriov_mode to convert
 *
 * Returns: SR-IOV mode as a user friendly string.
 */
const char *xe_sriov_mode_to_string(enum xe_sriov_mode mode)
{
	switch (mode) {
	case XE_SRIOV_MODE_NONE:
		return "none";
	case XE_SRIOV_MODE_PF:
		return "SR-IOV PF";
	case XE_SRIOV_MODE_VF:
		return "SR-IOV VF";
	default:
		return "<invalid>";
	}
}

/**
 * xe_sriov_probe_early - Probe a SR-IOV mode.
 * @xe: the &xe_device to probe mode on
 * @has_sriov: flag indicating hardware support for SR-IOV
 *
 * This function should be called only once and as soon as possible during
 * driver probe to detect whether we are running a SR-IOV Physical Function
 * (PF) or a Virtual Function (VF) device.
 *
 * SR-IOV PF mode detection is based on PCI @dev_is_pf() function.
 * SR-IOV VF mode detection is based on dedicated MMIO register read.
 */
void xe_sriov_probe_early(struct xe_device *xe, bool has_sriov)
{
	enum xe_sriov_mode mode = XE_SRIOV_MODE_NONE;

	/* TODO: replace with proper mode detection */
	xe_assert(xe, !has_sriov);

	xe_assert(xe, !xe->sriov.__mode);
	xe->sriov.__mode = mode;
	xe_assert(xe, xe->sriov.__mode);

	if (has_sriov)
		drm_info(&xe->drm, "Running in %s mode\n",
			 xe_sriov_mode_to_string(xe_device_sriov_mode(xe)));
}

static void fini_sriov(struct drm_device *drm, void *arg)
{
	struct xe_device *xe = arg;

	destroy_workqueue(xe->sriov.wq);
	xe->sriov.wq = NULL;
}

/**
 * xe_sriov_init - Initialize SR-IOV specific data.
 * @xe: the &xe_device to initialize
 *
 * In this function we create dedicated workqueue that will be used
 * by the SR-IOV specific workers.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_init(struct xe_device *xe)
{
	if (!IS_SRIOV(xe))
		return 0;

	xe_assert(xe, !xe->sriov.wq);
	xe->sriov.wq = alloc_workqueue("xe-sriov-wq", 0, 0);
	if (!xe->sriov.wq)
		return -ENOMEM;

	return drmm_add_action_or_reset(&xe->drm, fini_sriov, xe);
}
