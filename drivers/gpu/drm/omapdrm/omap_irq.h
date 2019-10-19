/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_irq.h -- OMAP DRM IRQ Handling
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 */

#ifndef __OMAPDRM_IRQ_H__
#define __OMAPDRM_IRQ_H__

#include <linux/types.h>

struct drm_crtc;
struct drm_device;
struct omap_irq_wait;

int omap_irq_enable_vblank(struct drm_crtc *crtc);
int omap_irq_enable_framedone(struct drm_crtc *crtc, bool enable);
void omap_irq_disable_vblank(struct drm_crtc *crtc);
void omap_drm_irq_uninstall(struct drm_device *dev);
int omap_drm_irq_install(struct drm_device *dev);

struct omap_irq_wait *omap_irq_wait_init(struct drm_device *dev,
		u32 irqmask, int count);
int omap_irq_wait(struct drm_device *dev, struct omap_irq_wait *wait,
		unsigned long timeout);

#endif /* __OMAPDRM_IRQ_H__ */
