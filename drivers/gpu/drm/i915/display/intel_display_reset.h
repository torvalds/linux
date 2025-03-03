/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_RESET_H__
#define __INTEL_RESET_H__

struct intel_display;

void intel_display_reset_prepare(struct intel_display *display);
void intel_display_reset_finish(struct intel_display *display);

#endif /* __INTEL_RESET_H__ */
