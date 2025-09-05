// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/component.h>
#include <linux/delay.h>

#include <drm/drm_managed.h>
#include <drm/intel/i915_component.h>
#include <drm/intel/intel_lb_mei_interface.h>
#include <drm/drm_print.h>

#include "xe_device.h"
#include "xe_late_bind_fw.h"

static struct xe_device *
late_bind_to_xe(struct xe_late_bind *late_bind)
{
	return container_of(late_bind, struct xe_device, late_bind);
}

static int xe_late_bind_component_bind(struct device *xe_kdev,
				       struct device *mei_kdev, void *data)
{
	struct xe_device *xe = kdev_to_xe_device(xe_kdev);
	struct xe_late_bind *late_bind = &xe->late_bind;

	late_bind->component.ops = data;
	late_bind->component.mei_dev = mei_kdev;

	return 0;
}

static void xe_late_bind_component_unbind(struct device *xe_kdev,
					  struct device *mei_kdev, void *data)
{
	struct xe_device *xe = kdev_to_xe_device(xe_kdev);
	struct xe_late_bind *late_bind = &xe->late_bind;

	late_bind->component.ops = NULL;
}

static const struct component_ops xe_late_bind_component_ops = {
	.bind   = xe_late_bind_component_bind,
	.unbind = xe_late_bind_component_unbind,
};

static void xe_late_bind_remove(void *arg)
{
	struct xe_late_bind *late_bind = arg;
	struct xe_device *xe = late_bind_to_xe(late_bind);

	component_del(xe->drm.dev, &xe_late_bind_component_ops);
}

/**
 * xe_late_bind_init() - add xe mei late binding component
 * @late_bind: pointer to late bind structure.
 *
 * Return: 0 if the initialization was successful, a negative errno otherwise.
 */
int xe_late_bind_init(struct xe_late_bind *late_bind)
{
	struct xe_device *xe = late_bind_to_xe(late_bind);
	int err;

	if (!xe->info.has_late_bind)
		return 0;

	if (!IS_ENABLED(CONFIG_INTEL_MEI_LB) || !IS_ENABLED(CONFIG_INTEL_MEI_GSC)) {
		drm_info(&xe->drm, "Can't init xe mei late bind missing mei component\n");
		return 0;
	}

	err = component_add_typed(xe->drm.dev, &xe_late_bind_component_ops,
				  INTEL_COMPONENT_LB);
	if (err < 0) {
		drm_err(&xe->drm, "Failed to add mei late bind component (%pe)\n", ERR_PTR(err));
		return err;
	}

	return devm_add_action_or_reset(xe->drm.dev, xe_late_bind_remove, late_bind);
}
