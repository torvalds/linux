// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/types.h>

#include "intel_display_core.h"
#include "intel_display_regs.h"
#include "intel_gvt_api.h"

u32 intel_display_device_pipe_offset(struct intel_display *display, enum pipe pipe)
{
	return INTEL_DISPLAY_DEVICE_PIPE_OFFSET(display, pipe);
}
EXPORT_SYMBOL_NS_GPL(intel_display_device_pipe_offset, "I915_GVT");

u32 intel_display_device_trans_offset(struct intel_display *display, enum transcoder trans)
{
	return INTEL_DISPLAY_DEVICE_TRANS_OFFSET(display, trans);
}
EXPORT_SYMBOL_NS_GPL(intel_display_device_trans_offset, "I915_GVT");

u32 intel_display_device_cursor_offset(struct intel_display *display, enum pipe pipe)
{
	return INTEL_DISPLAY_DEVICE_CURSOR_OFFSET(display, pipe);
}
EXPORT_SYMBOL_NS_GPL(intel_display_device_cursor_offset, "I915_GVT");

u32 intel_display_device_mmio_base(struct intel_display *display)
{
	return DISPLAY_MMIO_BASE(display);
}
EXPORT_SYMBOL_NS_GPL(intel_display_device_mmio_base, "I915_GVT");

bool intel_display_device_pipe_valid(struct intel_display *display, enum pipe pipe)
{
	if (pipe < PIPE_A || pipe >= I915_MAX_PIPES)
		return false;

	return DISPLAY_RUNTIME_INFO(display)->pipe_mask & BIT(pipe);
}
EXPORT_SYMBOL_NS_GPL(intel_display_device_pipe_valid, "I915_GVT");
