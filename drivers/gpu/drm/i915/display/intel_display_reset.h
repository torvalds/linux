/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_RESET_H__
#define __INTEL_RESET_H__

#include <linux/types.h>

struct intel_display;

bool intel_display_reset_supported(struct intel_display *display);
bool intel_display_reset_test(struct intel_display *display);
void intel_display_reset_prepare(struct intel_display *display);
void intel_display_reset_finish(struct intel_display *display, bool test_only);

void intel_display_reset_debugfs_register(struct intel_display *display);

#endif /* __INTEL_RESET_H__ */
