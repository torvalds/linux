/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __I9XX_WM_H__
#define __I9XX_WM_H__

#include <linux/types.h>

struct intel_crtc_state;
struct intel_display;
struct intel_plane_state;

#ifdef I915
bool ilk_disable_cxsr(struct intel_display *display);
void ilk_wm_sanitize(struct intel_display *display);
bool intel_set_memory_cxsr(struct intel_display *display, bool enable);
void i9xx_wm_init(struct intel_display *display);
#else
static inline bool ilk_disable_cxsr(struct intel_display *display)
{
	return false;
}
static inline void ilk_wm_sanitize(struct intel_display *display)
{
}
static inline bool intel_set_memory_cxsr(struct intel_display *display, bool enable)
{
	return false;
}
static inline void i9xx_wm_init(struct intel_display *display)
{
}
#endif

#endif /* __I9XX_WM_H__ */
