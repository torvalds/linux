/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************************
 * Copyright (c) 2009-2011, Intel Corporation.
 * All Rights Reserved.
 *
 * Authors:
 *    Benjamin Defnet <benjamin.r.defnet@intel.com>
 *    Rajesh Poornachandran <rajesh.poornachandran@intel.com>
 *
 **************************************************************************/

#ifndef _PSB_IRQ_H_
#define _PSB_IRQ_H_

struct drm_crtc;
struct drm_device;

bool sysirq_init(struct drm_device *dev);
void sysirq_uninit(struct drm_device *dev);

void psb_irq_preinstall(struct drm_device *dev);
void psb_irq_postinstall(struct drm_device *dev);
int  psb_irq_install(struct drm_device *dev, unsigned int irq);
void psb_irq_uninstall(struct drm_device *dev);

int  psb_enable_vblank(struct drm_crtc *crtc);
void psb_disable_vblank(struct drm_crtc *crtc);
u32  psb_get_vblank_counter(struct drm_crtc *crtc);

#endif /* _PSB_IRQ_H_ */
