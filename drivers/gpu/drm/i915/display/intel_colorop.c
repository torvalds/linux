// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */
#include "intel_colorop.h"

struct intel_colorop *to_intel_colorop(struct drm_colorop *colorop)
{
	return container_of(colorop, struct intel_colorop, base);
}
