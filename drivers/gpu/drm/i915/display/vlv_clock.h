/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __VLV_CLOCK_H__
#define __VLV_CLOCK_H__

struct drm_device;

#ifdef I915
int vlv_clock_get_hpll_vco(struct drm_device *drm);
int vlv_clock_get_hrawclk(struct drm_device *drm);
int vlv_clock_get_czclk(struct drm_device *drm);
int vlv_clock_get_cdclk(struct drm_device *drm);
int vlv_clock_get_gpll(struct drm_device *drm);
#else
static inline int vlv_clock_get_hpll_vco(struct drm_device *drm)
{
	return 0;
}
static inline int vlv_clock_get_hrawclk(struct drm_device *drm)
{
	return 0;
}
static inline int vlv_clock_get_czclk(struct drm_device *drm)
{
	return 0;
}
static inline int vlv_clock_get_cdclk(struct drm_device *drm)
{
	return 0;
}
static inline int vlv_clock_get_gpll(struct drm_device *drm)
{
	return 0;
}
#endif

#endif /* __VLV_CLOCK_H__ */
