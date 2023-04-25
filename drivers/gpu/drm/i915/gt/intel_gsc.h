/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019-2022, Intel Corporation. All rights reserved.
 */
#ifndef __INTEL_GSC_DEV_H__
#define __INTEL_GSC_DEV_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_gt;
struct mei_aux_device;

#define INTEL_GSC_NUM_INTERFACES 2
/*
 * The HECI1 bit corresponds to bit15 and HECI2 to bit14.
 * The reason for this is to allow growth for more interfaces in the future.
 */
#define GSC_IRQ_INTF(_x)  BIT(15 - (_x))

/**
 * struct intel_gsc - graphics security controller
 *
 * @gem_obj: scratch memory GSC operations
 * @intf : gsc interface
 */
struct intel_gsc {
	struct intel_gsc_intf {
		struct mei_aux_device *adev;
		struct drm_i915_gem_object *gem_obj;
		int irq;
		unsigned int id;
	} intf[INTEL_GSC_NUM_INTERFACES];
};

void intel_gsc_init(struct intel_gsc *gsc, struct drm_i915_private *i915);
void intel_gsc_fini(struct intel_gsc *gsc);
void intel_gsc_irq_handler(struct intel_gt *gt, u32 iir);

#endif /* __INTEL_GSC_DEV_H__ */
