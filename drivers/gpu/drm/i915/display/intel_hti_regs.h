/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_HTI_REGS_H__
#define __INTEL_HTI_REGS_H__

#include "i915_reg_defs.h"

#define HDPORT_STATE			_MMIO(0x45050)
#define   HDPORT_DPLL_USED_MASK		REG_GENMASK(15, 12)
#define   HDPORT_DDI_USED(phy)		REG_BIT(2 * (phy) + 1)
#define   HDPORT_ENABLED		REG_BIT(0)

#endif /* __INTEL_HTI_REGS_H__ */
