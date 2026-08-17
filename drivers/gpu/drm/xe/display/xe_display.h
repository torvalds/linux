/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_DISPLAY_H_
#define _XE_DISPLAY_H_

#include <linux/types.h>

struct drm_driver;
struct drm_fb_helper;
struct drm_fb_helper_surface_size;
struct pci_dev;
struct xe_device;

#if IS_ENABLED(CONFIG_DRM_XE_DISPLAY)

bool xe_display_driver_probe_defer(struct pci_dev *pdev);

int xe_display_driver_fbdev_probe(struct drm_fb_helper *fbh,
				  struct drm_fb_helper_surface_size *sizes);

int xe_display_probe(struct xe_device *xe);

int xe_display_init_early(struct xe_device *xe);
int xe_display_init(struct xe_device *xe);

void xe_display_register(struct xe_device *xe);
void xe_display_unregister(struct xe_device *xe);

void xe_display_irq_handler(struct xe_device *xe, u32 master_ctl);
void xe_display_irq_enable(struct xe_device *xe, u32 gu_misc_iir);
void xe_display_irq_reset(struct xe_device *xe);
void xe_display_irq_postinstall(struct xe_device *xe);

void xe_display_pm_suspend(struct xe_device *xe);
void xe_display_pm_shutdown(struct xe_device *xe);
void xe_display_pm_suspend_late(struct xe_device *xe);
void xe_display_pm_shutdown_late(struct xe_device *xe);
void xe_display_pm_resume_early(struct xe_device *xe);
void xe_display_pm_resume(struct xe_device *xe);
void xe_display_pm_runtime_suspend(struct xe_device *xe);
void xe_display_pm_runtime_suspend_late(struct xe_device *xe);
void xe_display_pm_runtime_resume(struct xe_device *xe);

#define XE_DISPLAY_DRIVER_FEATURES	(DRIVER_MODESET | DRIVER_ATOMIC)
#define XE_DISPLAY_DRIVER_OPS						\
	.fbdev_probe = PTR_IF(IS_ENABLED(CONFIG_DRM_FBDEV_EMULATION),	\
			      xe_display_driver_fbdev_probe)

#else

#define XE_DISPLAY_DRIVER_FEATURES	0
#define XE_DISPLAY_DRIVER_OPS \
	.fbdev_probe = NULL

static inline int xe_display_driver_probe_defer(struct pci_dev *pdev) { return 0; }

static inline int xe_display_probe(struct xe_device *xe) { return 0; }

static inline int xe_display_init_early(struct xe_device *xe) { return 0; }
static inline int xe_display_init(struct xe_device *xe) { return 0; }

static inline void xe_display_register(struct xe_device *xe) {}
static inline void xe_display_unregister(struct xe_device *xe) {}

static inline void xe_display_irq_handler(struct xe_device *xe, u32 master_ctl) {}
static inline void xe_display_irq_enable(struct xe_device *xe, u32 gu_misc_iir) {}
static inline void xe_display_irq_reset(struct xe_device *xe) {}
static inline void xe_display_irq_postinstall(struct xe_device *xe) {}

static inline void xe_display_pm_suspend(struct xe_device *xe) {}
static inline void xe_display_pm_shutdown(struct xe_device *xe) {}
static inline void xe_display_pm_suspend_late(struct xe_device *xe) {}
static inline void xe_display_pm_shutdown_late(struct xe_device *xe) {}
static inline void xe_display_pm_resume_early(struct xe_device *xe) {}
static inline void xe_display_pm_resume(struct xe_device *xe) {}
static inline void xe_display_pm_runtime_suspend(struct xe_device *xe) {}
static inline void xe_display_pm_runtime_suspend_late(struct xe_device *xe) {}
static inline void xe_display_pm_runtime_resume(struct xe_device *xe) {}

#endif /* CONFIG_DRM_XE_DISPLAY */
#endif /* _XE_DISPLAY_H_ */
