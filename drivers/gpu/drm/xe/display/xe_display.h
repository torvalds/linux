/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_DISPLAY_H_
#define _XE_DISPLAY_H_

#include "xe_device.h"

struct drm_driver;

#if IS_ENABLED(CONFIG_DRM_XE_DISPLAY)

bool xe_display_driver_probe_defer(struct pci_dev *pdev);
void xe_display_driver_set_hooks(struct drm_driver *driver);
void xe_display_driver_remove(struct xe_device *xe);

int xe_display_create(struct xe_device *xe);

int xe_display_probe(struct xe_device *xe);

int xe_display_init_nommio(struct xe_device *xe);
int xe_display_init_noirq(struct xe_device *xe);
int xe_display_init_noaccel(struct xe_device *xe);
int xe_display_init(struct xe_device *xe);
void xe_display_fini(struct xe_device *xe);

void xe_display_register(struct xe_device *xe);
void xe_display_unregister(struct xe_device *xe);

void xe_display_irq_handler(struct xe_device *xe, u32 master_ctl);
void xe_display_irq_enable(struct xe_device *xe, u32 gu_misc_iir);
void xe_display_irq_reset(struct xe_device *xe);
void xe_display_irq_postinstall(struct xe_device *xe, struct xe_gt *gt);

void xe_display_pm_suspend(struct xe_device *xe);
void xe_display_pm_shutdown(struct xe_device *xe);
void xe_display_pm_suspend_late(struct xe_device *xe);
void xe_display_pm_shutdown_late(struct xe_device *xe);
void xe_display_pm_resume_early(struct xe_device *xe);
void xe_display_pm_resume(struct xe_device *xe);
void xe_display_pm_runtime_suspend(struct xe_device *xe);
void xe_display_pm_runtime_suspend_late(struct xe_device *xe);
void xe_display_pm_runtime_resume(struct xe_device *xe);

#else

static inline int xe_display_driver_probe_defer(struct pci_dev *pdev) { return 0; }
static inline void xe_display_driver_set_hooks(struct drm_driver *driver) { }
static inline void xe_display_driver_remove(struct xe_device *xe) {}

static inline int xe_display_create(struct xe_device *xe) { return 0; }

static inline int xe_display_probe(struct xe_device *xe) { return 0; }

static inline int xe_display_init_nommio(struct xe_device *xe) { return 0; }
static inline int xe_display_init_noirq(struct xe_device *xe) { return 0; }
static inline int xe_display_init_noaccel(struct xe_device *xe) { return 0; }
static inline int xe_display_init(struct xe_device *xe) { return 0; }
static inline void xe_display_fini(struct xe_device *xe) {}

static inline void xe_display_register(struct xe_device *xe) {}
static inline void xe_display_unregister(struct xe_device *xe) {}

static inline void xe_display_irq_handler(struct xe_device *xe, u32 master_ctl) {}
static inline void xe_display_irq_enable(struct xe_device *xe, u32 gu_misc_iir) {}
static inline void xe_display_irq_reset(struct xe_device *xe) {}
static inline void xe_display_irq_postinstall(struct xe_device *xe, struct xe_gt *gt) {}

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
