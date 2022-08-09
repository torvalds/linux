// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "gt/intel_engine_regs.h"

#include "i915_drv.h"
#include "i915_gem.h"
#include "i915_ioctl.h"
#include "i915_reg.h"
#include "intel_runtime_pm.h"
#include "intel_uncore.h"

/*
 * This file is for small ioctl functions that are out of place everywhere else,
 * and not big enough to warrant a file of their own.
 *
 * This is not the dumping ground for random ioctls.
 */

struct reg_whitelist {
	i915_reg_t offset_ldw;
	i915_reg_t offset_udw;
	u8 min_graphics_ver;
	u8 max_graphics_ver;
	u8 size;
};

static const struct reg_whitelist reg_read_whitelist[] = {
	{
		.offset_ldw = RING_TIMESTAMP(RENDER_RING_BASE),
		.offset_udw = RING_TIMESTAMP_UDW(RENDER_RING_BASE),
		.min_graphics_ver = 4,
		.max_graphics_ver = 12,
		.size = 8
	}
};

int i915_reg_read_ioctl(struct drm_device *dev,
			void *data, struct drm_file *unused)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct drm_i915_reg_read *reg = data;
	struct reg_whitelist const *entry;
	intel_wakeref_t wakeref;
	unsigned int flags;
	int remain;
	int ret = 0;

	entry = reg_read_whitelist;
	remain = ARRAY_SIZE(reg_read_whitelist);
	while (remain) {
		u32 entry_offset = i915_mmio_reg_offset(entry->offset_ldw);

		GEM_BUG_ON(!is_power_of_2(entry->size));
		GEM_BUG_ON(entry->size > 8);
		GEM_BUG_ON(entry_offset & (entry->size - 1));

		if (IS_GRAPHICS_VER(i915, entry->min_graphics_ver, entry->max_graphics_ver) &&
		    entry_offset == (reg->offset & -entry->size))
			break;
		entry++;
		remain--;
	}

	if (!remain)
		return -EINVAL;

	flags = reg->offset & (entry->size - 1);

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		if (entry->size == 8 && flags == I915_REG_READ_8B_WA)
			reg->val = intel_uncore_read64_2x32(uncore,
							    entry->offset_ldw,
							    entry->offset_udw);
		else if (entry->size == 8 && flags == 0)
			reg->val = intel_uncore_read64(uncore,
						       entry->offset_ldw);
		else if (entry->size == 4 && flags == 0)
			reg->val = intel_uncore_read(uncore, entry->offset_ldw);
		else if (entry->size == 2 && flags == 0)
			reg->val = intel_uncore_read16(uncore,
						       entry->offset_ldw);
		else if (entry->size == 1 && flags == 0)
			reg->val = intel_uncore_read8(uncore,
						      entry->offset_ldw);
		else
			ret = -EINVAL;
	}

	return ret;
}
