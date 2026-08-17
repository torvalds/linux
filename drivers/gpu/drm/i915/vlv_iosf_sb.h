/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2013-2021 Intel Corporation
 */

#ifndef _VLV_IOSF_SB_H_
#define _VLV_IOSF_SB_H_

#include <linux/types.h>

#include <drm/intel/vlv_iosf_sb_regs.h>

struct drm_device;
struct drm_i915_private;

void vlv_iosf_sb_init(struct drm_i915_private *i915);
void vlv_iosf_sb_fini(struct drm_i915_private *i915);

void vlv_iosf_sb_get(struct drm_device *drm, unsigned long unit_mask);
void vlv_iosf_sb_put(struct drm_device *drm, unsigned long unit_mask);

u32 vlv_iosf_sb_read(struct drm_device *drm, enum vlv_iosf_sb_unit unit, u32 addr);
int vlv_iosf_sb_write(struct drm_device *drm, enum vlv_iosf_sb_unit unit, u32 addr, u32 val);

extern const struct intel_display_vlv_iosf_interface i915_display_vlv_iosf_interface;

#endif /* _VLV_IOSF_SB_H_ */
