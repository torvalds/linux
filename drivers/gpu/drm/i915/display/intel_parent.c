// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

/*
 * Convenience wrapper functions to call the parent interface functions:
 *
 * - display->parent->SUBSTRUCT->FUNCTION()
 * - display->parent->FUNCTION()
 *
 * All functions here should be named accordingly:
 *
 * - intel_parent_SUBSTRUCT_FUNCTION()
 * - intel_parent_FUNCTION()
 *
 * These functions may use display driver specific types for parameters and
 * return values, translating them to and from the generic types used in the
 * function pointer interface.
 */

#include <drm/intel/display_parent_interface.h>

#include "intel_display_core.h"
#include "intel_parent.h"

bool intel_parent_irq_enabled(struct intel_display *display)
{
	return display->parent->irq->enabled(display->drm);
}

void intel_parent_irq_synchronize(struct intel_display *display)
{
	display->parent->irq->synchronize(display->drm);
}
