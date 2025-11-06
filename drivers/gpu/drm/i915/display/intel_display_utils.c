// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <linux/device.h>

#include <drm/drm_device.h>

#ifdef CONFIG_X86
#include <asm/hypervisor.h>
#endif

#include "intel_display_core.h"
#include "intel_display_utils.h"

bool intel_display_run_as_guest(struct intel_display *display)
{
#if IS_ENABLED(CONFIG_X86)
	return !hypervisor_is_type(X86_HYPER_NATIVE);
#else
	/* Not supported yet */
	return false;
#endif
}

bool intel_display_vtd_active(struct intel_display *display)
{
	if (device_iommu_mapped(display->drm->dev))
		return true;

	/* Running as a guest, we assume the host is enforcing VT'd */
	return intel_display_run_as_guest(display);
}
