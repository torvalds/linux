/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2013-2021 Intel Corporation
 */

#ifndef _VLV_SIDEBAND_H_
#define _VLV_SIDEBAND_H_

#include <linux/types.h>

#include "vlv_sideband_reg.h"

enum pipe;
struct drm_i915_private;

enum {
	VLV_IOSF_SB_BUNIT,
	VLV_IOSF_SB_CCK,
	VLV_IOSF_SB_CCU,
	VLV_IOSF_SB_DPIO,
	VLV_IOSF_SB_FLISDSI,
	VLV_IOSF_SB_GPIO,
	VLV_IOSF_SB_NC,
	VLV_IOSF_SB_PUNIT,
};

static inline void vlv_iosf_sb_get(struct drm_i915_private *i915, unsigned long ports)
{
}
static inline u32 vlv_iosf_sb_read(struct drm_i915_private *i915, u8 port, u32 reg)
{
	return 0;
}
static inline void vlv_iosf_sb_write(struct drm_i915_private *i915,
				     u8 port, u32 reg, u32 val)
{
}
static inline void vlv_iosf_sb_put(struct drm_i915_private *i915, unsigned long ports)
{
}
static inline void vlv_bunit_get(struct drm_i915_private *i915)
{
}
static inline u32 vlv_bunit_read(struct drm_i915_private *i915, u32 reg)
{
	return 0;
}
static inline void vlv_bunit_write(struct drm_i915_private *i915, u32 reg, u32 val)
{
}
static inline void vlv_bunit_put(struct drm_i915_private *i915)
{
}
static inline void vlv_cck_get(struct drm_i915_private *i915)
{
}
static inline u32 vlv_cck_read(struct drm_i915_private *i915, u32 reg)
{
	return 0;
}
static inline void vlv_cck_write(struct drm_i915_private *i915, u32 reg, u32 val)
{
}
static inline void vlv_cck_put(struct drm_i915_private *i915)
{
}
static inline void vlv_ccu_get(struct drm_i915_private *i915)
{
}
static inline u32 vlv_ccu_read(struct drm_i915_private *i915, u32 reg)
{
	return 0;
}
static inline void vlv_ccu_write(struct drm_i915_private *i915, u32 reg, u32 val)
{
}
static inline void vlv_ccu_put(struct drm_i915_private *i915)
{
}
static inline void vlv_dpio_get(struct drm_i915_private *i915)
{
}
static inline u32 vlv_dpio_read(struct drm_i915_private *i915, int pipe, int reg)
{
	return 0;
}
static inline void vlv_dpio_write(struct drm_i915_private *i915,
				  int pipe, int reg, u32 val)
{
}
static inline void vlv_dpio_put(struct drm_i915_private *i915)
{
}
static inline void vlv_flisdsi_get(struct drm_i915_private *i915)
{
}
static inline u32 vlv_flisdsi_read(struct drm_i915_private *i915, u32 reg)
{
	return 0;
}
static inline void vlv_flisdsi_write(struct drm_i915_private *i915, u32 reg, u32 val)
{
}
static inline void vlv_flisdsi_put(struct drm_i915_private *i915)
{
}
static inline void vlv_nc_get(struct drm_i915_private *i915)
{
}
static inline u32 vlv_nc_read(struct drm_i915_private *i915, u8 addr)
{
	return 0;
}
static inline void vlv_nc_put(struct drm_i915_private *i915)
{
}
static inline void vlv_punit_get(struct drm_i915_private *i915)
{
}
static inline u32 vlv_punit_read(struct drm_i915_private *i915, u32 addr)
{
	return 0;
}
static inline int vlv_punit_write(struct drm_i915_private *i915, u32 addr, u32 val)
{
	return 0;
}
static inline void vlv_punit_put(struct drm_i915_private *i915)
{
}

#endif /* _VLV_SIDEBAND_H_ */
