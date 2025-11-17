/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_PARENT_H__
#define __INTEL_PARENT_H__

#include <linux/types.h>

struct intel_display;

bool intel_parent_irq_enabled(struct intel_display *display);
void intel_parent_irq_synchronize(struct intel_display *display);

bool intel_parent_vgpu_active(struct intel_display *display);

#endif /* __INTEL_PARENT_H__ */
