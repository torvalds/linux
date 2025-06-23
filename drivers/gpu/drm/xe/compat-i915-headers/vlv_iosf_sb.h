/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2013-2021 Intel Corporation
 */

#ifndef _VLV_IOSF_SB_H_
#define _VLV_IOSF_SB_H_

#include <linux/types.h>

#include "vlv_iosf_sb_reg.h"

struct drm_device;

enum vlv_iosf_sb_unit {
	VLV_IOSF_SB_BUNIT,
	VLV_IOSF_SB_CCK,
	VLV_IOSF_SB_CCU,
	VLV_IOSF_SB_DPIO,
	VLV_IOSF_SB_DPIO_2,
	VLV_IOSF_SB_FLISDSI,
	VLV_IOSF_SB_GPIO,
	VLV_IOSF_SB_NC,
	VLV_IOSF_SB_PUNIT,
};

static inline void vlv_iosf_sb_get(struct drm_device *drm, unsigned long ports)
{
}
static inline u32 vlv_iosf_sb_read(struct drm_device *drm, enum vlv_iosf_sb_unit unit, u32 addr)
{
	return 0;
}
static inline int vlv_iosf_sb_write(struct drm_device *drm, enum vlv_iosf_sb_unit unit, u32 addr, u32 val)
{
	return 0;
}
static inline void vlv_iosf_sb_put(struct drm_device *drm, unsigned long ports)
{
}

#endif /* _VLV_IOSF_SB_H_ */
