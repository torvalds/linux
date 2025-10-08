/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Intel Corporation
 */

#ifndef __INTEL_CMTG_REGS_H__
#define __INTEL_CMTG_REGS_H__

#include "intel_display_reg_defs.h"

#define CMTG_CLK_SEL			_MMIO(0x46160)
#define CMTG_CLK_SEL_A_MASK		REG_GENMASK(31, 29)
#define CMTG_CLK_SEL_A_DISABLED		REG_FIELD_PREP(CMTG_CLK_SEL_A_MASK, 0)
#define CMTG_CLK_SEL_B_MASK		REG_GENMASK(15, 13)
#define CMTG_CLK_SEL_B_DISABLED		REG_FIELD_PREP(CMTG_CLK_SEL_B_MASK, 0)

#define TRANS_CMTG_CTL_A		_MMIO(0x6fa88)
#define TRANS_CMTG_CTL_B		_MMIO(0x6fb88)
#define  CMTG_ENABLE			REG_BIT(31)

#endif /* __INTEL_CMTG_REGS_H__ */
