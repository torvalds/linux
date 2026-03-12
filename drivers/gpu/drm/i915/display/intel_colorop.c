// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "intel_colorop.h"
#include "intel_display_types.h"

struct intel_colorop *to_intel_colorop(struct drm_colorop *colorop)
{
	return container_of(colorop, struct intel_colorop, base);
}

struct intel_colorop *intel_colorop_alloc(void)
{
	struct intel_colorop *colorop;

	colorop = kzalloc_obj(*colorop);
	if (!colorop)
		return ERR_PTR(-ENOMEM);

	return colorop;
}

struct intel_colorop *intel_colorop_create(enum intel_color_block id)
{
	struct intel_colorop *colorop;

	colorop = intel_colorop_alloc();

	if (IS_ERR(colorop))
		return colorop;

	colorop->id = id;

	return colorop;
}

void intel_colorop_destroy(struct drm_colorop *colorop)
{
	drm_colorop_cleanup(colorop);
	kfree(to_intel_colorop(colorop));
}
