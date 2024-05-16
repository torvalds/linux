/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2023, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_REGS_H__
#define __INTEL_PXP_REGS_H__

#include "i915_reg_defs.h"

/* KCR subsystem register base address */
#define GEN12_KCR_BASE 0x32000
#define MTL_KCR_BASE 0x386000

/* KCR enable/disable control */
#define KCR_INIT(base) _MMIO((base) + 0xf0)

/* Setting KCR Init bit is required after system boot */
#define KCR_INIT_ALLOW_DISPLAY_ME_WRITES REG_BIT(14)

/* KCR hwdrm session in play status 0-31 */
#define KCR_SIP(base) _MMIO((base) + 0x260)

/* PXP global terminate register for session termination */
#define KCR_GLOBAL_TERMINATE(base) _MMIO((base) + 0xf8)

#endif /* __INTEL_PXP_REGS_H__ */
