/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_HTI_H__
#define __INTEL_HTI_H__

#include <linux/types.h>

struct intel_display;
enum phy;

void intel_hti_init(struct intel_display *display);
bool intel_hti_uses_phy(struct intel_display *display, enum phy phy);
u32 intel_hti_dpll_mask(struct intel_display *display);

#endif /* __INTEL_HTI_H__ */
