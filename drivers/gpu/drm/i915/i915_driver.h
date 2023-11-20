/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_DRIVER_H__
#define __I915_DRIVER_H__

#include <linux/pm.h>

struct pci_dev;
struct pci_device_id;
struct drm_i915_private;
struct drm_printer;

#define DRIVER_NAME		"i915"
#define DRIVER_DESC		"Intel Graphics"
#define DRIVER_DATE		"20230929"
#define DRIVER_TIMESTAMP	1695980603

extern const struct dev_pm_ops i915_pm_ops;

int i915_driver_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
void i915_driver_remove(struct drm_i915_private *i915);
void i915_driver_shutdown(struct drm_i915_private *i915);

int i915_driver_resume_switcheroo(struct drm_i915_private *i915);
int i915_driver_suspend_switcheroo(struct drm_i915_private *i915, pm_message_t state);

void
i915_print_iommu_status(struct drm_i915_private *i915, struct drm_printer *p);

#endif /* __I915_DRIVER_H__ */
