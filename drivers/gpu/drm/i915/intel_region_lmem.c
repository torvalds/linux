// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_memory_region.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "intel_region_lmem.h"

const struct intel_memory_region_ops intel_region_lmem_ops = {
	.init = intel_memory_region_init_buddy,
	.release = intel_memory_region_release_buddy,
	.create_object = __i915_gem_lmem_object_create,
};
