/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_RESET_H__
#define __INTEL_RESET_H__

#include <linux/types.h>

struct intel_display;

typedef void modeset_stuck_fn(void *context);

bool intel_display_reset_test(struct intel_display *display);
bool intel_display_reset_prepare(struct intel_display *display,
				 modeset_stuck_fn modeset_stuck, void *context);
void intel_display_reset_finish(struct intel_display *display, bool test_only);

#endif /* __INTEL_RESET_H__ */
