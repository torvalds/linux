/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */
#ifndef _XE_SOC_REMAPPER_REGS_H_
#define _XE_SOC_REMAPPER_REGS_H_

#include "xe_regs.h"

#define SG_REMAP_INDEX1			XE_REG(SOC_BASE + 0x08)
#define   SG_REMAP_TELEM_MASK		REG_GENMASK(31, 24)
#define   SG_REMAP_SYSCTRL_MASK		REG_GENMASK(23, 16)

#endif
