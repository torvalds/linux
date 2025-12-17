// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_drv.h>
#include <linux/pm_runtime.h>

#include "amdxdna_pm.h"

#define AMDXDNA_AUTOSUSPEND_DELAY	5000 /* milliseconds */

int amdxdna_pm_suspend(struct device *dev)
{
	struct amdxdna_dev *xdna = to_xdna_dev(dev_get_drvdata(dev));
	int ret = -EOPNOTSUPP;
	bool rpm;

	if (xdna->dev_info->ops->suspend) {
		rpm = xdna->rpm_on;
		xdna->rpm_on = false;
		ret = xdna->dev_info->ops->suspend(xdna);
		xdna->rpm_on = rpm;
	}

	XDNA_DBG(xdna, "Suspend done ret %d", ret);
	return ret;
}

int amdxdna_pm_resume(struct device *dev)
{
	struct amdxdna_dev *xdna = to_xdna_dev(dev_get_drvdata(dev));
	int ret = -EOPNOTSUPP;
	bool rpm;

	if (xdna->dev_info->ops->resume) {
		rpm = xdna->rpm_on;
		xdna->rpm_on = false;
		ret = xdna->dev_info->ops->resume(xdna);
		xdna->rpm_on = rpm;
	}

	XDNA_DBG(xdna, "Resume done ret %d", ret);
	return ret;
}

int amdxdna_pm_resume_get(struct amdxdna_dev *xdna)
{
	struct device *dev = xdna->ddev.dev;
	int ret;

	if (!xdna->rpm_on)
		return 0;

	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		XDNA_ERR(xdna, "Resume failed: %d", ret);
		pm_runtime_set_suspended(dev);
	}

	return ret;
}

void amdxdna_pm_suspend_put(struct amdxdna_dev *xdna)
{
	struct device *dev = xdna->ddev.dev;

	if (!xdna->rpm_on)
		return;

	pm_runtime_put_autosuspend(dev);
}

void amdxdna_pm_init(struct amdxdna_dev *xdna)
{
	struct device *dev = xdna->ddev.dev;

	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, AMDXDNA_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);
	pm_runtime_put_autosuspend(dev);
	xdna->rpm_on = true;
}

void amdxdna_pm_fini(struct amdxdna_dev *xdna)
{
	struct device *dev = xdna->ddev.dev;

	xdna->rpm_on = false;
	pm_runtime_get_noresume(dev);
	pm_runtime_forbid(dev);
}
