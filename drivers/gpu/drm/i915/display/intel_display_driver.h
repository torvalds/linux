/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DRIVER_H__
#define __INTEL_DISPLAY_DRIVER_H__

#include <linux/types.h>

struct drm_atomic_state;
struct drm_modeset_acquire_ctx;
struct intel_display;
struct pci_dev;

bool intel_display_driver_probe_defer(struct pci_dev *pdev);
void intel_display_driver_init_hw(struct intel_display *display);
void intel_display_driver_early_probe(struct intel_display *display);
int intel_display_driver_probe_noirq(struct intel_display *display);
int intel_display_driver_probe_nogem(struct intel_display *display);
int intel_display_driver_probe(struct intel_display *display);
void intel_display_driver_register(struct intel_display *display);
void intel_display_driver_remove(struct intel_display *display);
void intel_display_driver_remove_noirq(struct intel_display *display);
void intel_display_driver_remove_nogem(struct intel_display *display);
void intel_display_driver_unregister(struct intel_display *display);
int intel_display_driver_suspend(struct intel_display *display);
void intel_display_driver_resume(struct intel_display *display);

/* interface for intel_display_reset.c */
int __intel_display_driver_resume(struct intel_display *display,
				  struct drm_atomic_state *state,
				  struct drm_modeset_acquire_ctx *ctx);

void intel_display_driver_enable_user_access(struct intel_display *display);
void intel_display_driver_disable_user_access(struct intel_display *display);
void intel_display_driver_suspend_access(struct intel_display *display);
void intel_display_driver_resume_access(struct intel_display *display);
bool intel_display_driver_check_access(struct intel_display *display);

#endif /* __INTEL_DISPLAY_DRIVER_H__ */

