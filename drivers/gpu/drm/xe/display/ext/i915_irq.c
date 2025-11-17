// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_uncore.h"

bool intel_irqs_enabled(struct xe_device *xe)
{
	return atomic_read(&xe->irq.enabled);
}

void intel_synchronize_irq(struct xe_device *xe)
{
	synchronize_irq(to_pci_dev(xe->drm.dev)->irq);
}
