/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef _VLV_SIDEBAND_H_
#define _VLV_SIDEBAND_H_

#include <linux/bitops.h>
#include <linux/types.h>

#include "vlv_iosf_sb.h"
#include "vlv_iosf_sb_reg.h"

enum dpio_phy;
struct drm_device;

static inline void vlv_bunit_get(struct drm_device *drm)
{
	vlv_iosf_sb_get(drm, BIT(VLV_IOSF_SB_BUNIT));
}

static inline u32 vlv_bunit_read(struct drm_device *drm, u32 reg)
{
	return vlv_iosf_sb_read(drm, VLV_IOSF_SB_BUNIT, reg);
}

static inline void vlv_bunit_write(struct drm_device *drm, u32 reg, u32 val)
{
	vlv_iosf_sb_write(drm, VLV_IOSF_SB_BUNIT, reg, val);
}

static inline void vlv_bunit_put(struct drm_device *drm)
{
	vlv_iosf_sb_put(drm, BIT(VLV_IOSF_SB_BUNIT));
}

static inline void vlv_cck_get(struct drm_device *drm)
{
	vlv_iosf_sb_get(drm, BIT(VLV_IOSF_SB_CCK));
}

static inline u32 vlv_cck_read(struct drm_device *drm, u32 reg)
{
	return vlv_iosf_sb_read(drm, VLV_IOSF_SB_CCK, reg);
}

static inline void vlv_cck_write(struct drm_device *drm, u32 reg, u32 val)
{
	vlv_iosf_sb_write(drm, VLV_IOSF_SB_CCK, reg, val);
}

static inline void vlv_cck_put(struct drm_device *drm)
{
	vlv_iosf_sb_put(drm, BIT(VLV_IOSF_SB_CCK));
}

static inline void vlv_ccu_get(struct drm_device *drm)
{
	vlv_iosf_sb_get(drm, BIT(VLV_IOSF_SB_CCU));
}

static inline u32 vlv_ccu_read(struct drm_device *drm, u32 reg)
{
	return vlv_iosf_sb_read(drm, VLV_IOSF_SB_CCU, reg);
}

static inline void vlv_ccu_write(struct drm_device *drm, u32 reg, u32 val)
{
	vlv_iosf_sb_write(drm, VLV_IOSF_SB_CCU, reg, val);
}

static inline void vlv_ccu_put(struct drm_device *drm)
{
	vlv_iosf_sb_put(drm, BIT(VLV_IOSF_SB_CCU));
}

static inline void vlv_dpio_get(struct drm_device *drm)
{
	vlv_iosf_sb_get(drm, BIT(VLV_IOSF_SB_DPIO) | BIT(VLV_IOSF_SB_DPIO_2));
}

#ifdef I915
u32 vlv_dpio_read(struct drm_device *drm, enum dpio_phy phy, int reg);
void vlv_dpio_write(struct drm_device *drm,
		    enum dpio_phy phy, int reg, u32 val);
#else
static inline u32 vlv_dpio_read(struct drm_device *drm, int phy, int reg)
{
	return 0;
}
static inline void vlv_dpio_write(struct drm_device *drm,
				  int phy, int reg, u32 val)
{
}
#endif

static inline void vlv_dpio_put(struct drm_device *drm)
{
	vlv_iosf_sb_put(drm, BIT(VLV_IOSF_SB_DPIO) | BIT(VLV_IOSF_SB_DPIO_2));
}

static inline void vlv_flisdsi_get(struct drm_device *drm)
{
	vlv_iosf_sb_get(drm, BIT(VLV_IOSF_SB_FLISDSI));
}

static inline u32 vlv_flisdsi_read(struct drm_device *drm, u32 reg)
{
	return vlv_iosf_sb_read(drm, VLV_IOSF_SB_FLISDSI, reg);
}

static inline void vlv_flisdsi_write(struct drm_device *drm, u32 reg, u32 val)
{
	vlv_iosf_sb_write(drm, VLV_IOSF_SB_FLISDSI, reg, val);
}

static inline void vlv_flisdsi_put(struct drm_device *drm)
{
	vlv_iosf_sb_put(drm, BIT(VLV_IOSF_SB_FLISDSI));
}

static inline void vlv_nc_get(struct drm_device *drm)
{
	vlv_iosf_sb_get(drm, BIT(VLV_IOSF_SB_NC));
}

static inline u32 vlv_nc_read(struct drm_device *drm, u8 addr)
{
	return vlv_iosf_sb_read(drm, VLV_IOSF_SB_NC, addr);
}

static inline void vlv_nc_put(struct drm_device *drm)
{
	vlv_iosf_sb_put(drm, BIT(VLV_IOSF_SB_NC));
}

static inline void vlv_punit_get(struct drm_device *drm)
{
	vlv_iosf_sb_get(drm, BIT(VLV_IOSF_SB_PUNIT));
}

static inline u32 vlv_punit_read(struct drm_device *drm, u32 addr)
{
	return vlv_iosf_sb_read(drm, VLV_IOSF_SB_PUNIT, addr);
}

static inline int vlv_punit_write(struct drm_device *drm, u32 addr, u32 val)
{
	return vlv_iosf_sb_write(drm, VLV_IOSF_SB_PUNIT, addr, val);
}

static inline void vlv_punit_put(struct drm_device *drm)
{
	vlv_iosf_sb_put(drm, BIT(VLV_IOSF_SB_PUNIT));
}

#endif /* _VLV_SIDEBAND_H_ */
