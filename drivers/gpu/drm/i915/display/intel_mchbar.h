/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef __INTEL_MCHBAR_H__
#define __INTEL_MCHBAR_H__

#include <linux/types.h>

#include <drm/intel/mchbar_regs.h>

#include "intel_display_reg_defs.h"

struct intel_display;

u16 intel_mchbar_read16(struct intel_display *display, intel_reg_t reg);
u32 intel_mchbar_read(struct intel_display *display, intel_reg_t reg);
u64 intel_mchbar_read64_2x32(struct intel_display *display, intel_reg_t reg);

#endif /* __INTEL_MCHBAR_H__ */
