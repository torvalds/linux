// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "gt/intel_hwconfig.h"
#include "i915_drv.h"
#include "i915_memcpy.h"

/*
 * GuC has a blob containing hardware configuration information (HWConfig).
 * This is formatted as a simple and flexible KLV (Key/Length/Value) table.
 *
 * For example, a minimal version could be:
 *   enum device_attr {
 *     ATTR_SOME_VALUE = 0,
 *     ATTR_SOME_MASK  = 1,
 *   };
 *
 *   static const u32 hwconfig[] = {
 *     ATTR_SOME_VALUE,
 *     1,		// Value Length in DWords
 *     8,		// Value
 *
 *     ATTR_SOME_MASK,
 *     3,
 *     0x00FFFFFFFF, 0xFFFFFFFF, 0xFF000000,
 *   };
 *
 * The attribute ids are defined in a hardware spec.
 */

static int __guc_action_get_hwconfig(struct intel_guc *guc,
				     u32 ggtt_offset, u32 ggtt_size)
{
	u32 action[] = {
		INTEL_GUC_ACTION_GET_HWCONFIG,
		lower_32_bits(ggtt_offset),
		upper_32_bits(ggtt_offset),
		ggtt_size,
	};
	int ret;

	ret = intel_guc_send_mmio(guc, action, ARRAY_SIZE(action), NULL, 0);
	if (ret == -ENXIO)
		return -ENOENT;

	return ret;
}

static int guc_hwconfig_discover_size(struct intel_guc *guc, struct intel_hwconfig *hwconfig)
{
	int ret;

	/*
	 * Sending a query with zero offset and size will return the
	 * size of the blob.
	 */
	ret = __guc_action_get_hwconfig(guc, 0, 0);
	if (ret < 0)
		return ret;

	if (ret == 0)
		return -EINVAL;

	hwconfig->size = ret;
	return 0;
}

static int guc_hwconfig_fill_buffer(struct intel_guc *guc, struct intel_hwconfig *hwconfig)
{
	struct i915_vma *vma;
	u32 ggtt_offset;
	void *vaddr;
	int ret;

	GEM_BUG_ON(!hwconfig->size);

	ret = intel_guc_allocate_and_map_vma(guc, hwconfig->size, &vma, &vaddr);
	if (ret)
		return ret;

	ggtt_offset = intel_guc_ggtt_offset(guc, vma);

	ret = __guc_action_get_hwconfig(guc, ggtt_offset, hwconfig->size);
	if (ret >= 0)
		memcpy(hwconfig->ptr, vaddr, hwconfig->size);

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);

	return ret;
}

static bool has_table(struct drm_i915_private *i915)
{
	if (IS_ALDERLAKE_P(i915))
		return true;
	if (IS_DG2(i915))
		return true;

	return false;
}

/**
 * intel_guc_hwconfig_init - Initialize the HWConfig
 *
 * Retrieve the HWConfig table from the GuC and save it locally.
 * It can then be queried on demand by other users later on.
 */
static int guc_hwconfig_init(struct intel_gt *gt)
{
	struct intel_hwconfig *hwconfig = &gt->info.hwconfig;
	struct intel_guc *guc = &gt->uc.guc;
	int ret;

	if (!has_table(gt->i915))
		return 0;

	ret = guc_hwconfig_discover_size(guc, hwconfig);
	if (ret)
		return ret;

	hwconfig->ptr = kmalloc(hwconfig->size, GFP_KERNEL);
	if (!hwconfig->ptr) {
		hwconfig->size = 0;
		return -ENOMEM;
	}

	ret = guc_hwconfig_fill_buffer(guc, hwconfig);
	if (ret < 0) {
		intel_gt_fini_hwconfig(gt);
		return ret;
	}

	return 0;
}

/**
 * intel_gt_init_hwconfig - Initialize the HWConfig if available
 *
 * Retrieve the HWConfig table if available on the current platform.
 */
int intel_gt_init_hwconfig(struct intel_gt *gt)
{
	if (!intel_uc_uses_guc(&gt->uc))
		return 0;

	return guc_hwconfig_init(gt);
}

/**
 * intel_gt_fini_hwconfig - Finalize the HWConfig
 *
 * Free up the memory allocation holding the table.
 */
void intel_gt_fini_hwconfig(struct intel_gt *gt)
{
	struct intel_hwconfig *hwconfig = &gt->info.hwconfig;

	kfree(hwconfig->ptr);
	hwconfig->size = 0;
	hwconfig->ptr = NULL;
}
