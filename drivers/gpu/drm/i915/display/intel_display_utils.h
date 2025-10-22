/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_DISPLAY_UTILS__
#define __INTEL_DISPLAY_UTILS__

#include <linux/types.h>

struct intel_display;

#define KHz(x) (1000 * (x))
#define MHz(x) KHz(1000 * (x))

bool intel_display_run_as_guest(struct intel_display *display);
bool intel_display_vtd_active(struct intel_display *display);

#endif /* __INTEL_DISPLAY_UTILS__ */
