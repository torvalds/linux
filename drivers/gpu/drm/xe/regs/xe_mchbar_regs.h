/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_MCHBAR_REGS_H_
#define _XE_MCHBAR_REGS_H_

#include "regs/xe_reg_defs.h"

/*
 * MCHBAR mirror.
 *
 * This mirrors the MCHBAR MMIO space whose location is determined by
 * device 0 function 0's pci config register 0x44 or 0x48 and matches it in
 * every way.
 */

#define MCHBAR_MIRROR_BASE_SNB			0x140000

#define PCU_CR_PACKAGE_POWER_SKU		XE_REG(MCHBAR_MIRROR_BASE_SNB + 0x5930)
#define   PKG_TDP				GENMASK_ULL(14, 0)
#define   PKG_MIN_PWR				GENMASK_ULL(30, 16)
#define   PKG_MAX_PWR				GENMASK_ULL(46, 32)
#define   PKG_MAX_WIN				GENMASK_ULL(54, 48)
#define     PKG_MAX_WIN_X			GENMASK_ULL(54, 53)
#define     PKG_MAX_WIN_Y			GENMASK_ULL(52, 48)


#define PCU_CR_PACKAGE_POWER_SKU_UNIT		XE_REG(MCHBAR_MIRROR_BASE_SNB + 0x5938)
#define   PKG_PWR_UNIT				REG_GENMASK(3, 0)
#define   PKG_ENERGY_UNIT			REG_GENMASK(12, 8)
#define   PKG_TIME_UNIT				REG_GENMASK(19, 16)

#define PCU_CR_PACKAGE_ENERGY_STATUS		XE_REG(MCHBAR_MIRROR_BASE_SNB + 0x593c)

#define PCU_CR_PACKAGE_TEMPERATURE		XE_REG(MCHBAR_MIRROR_BASE_SNB + 0x5978)
#define   TEMP_MASK				REG_GENMASK(7, 0)

#define PCU_CR_PACKAGE_RAPL_LIMIT		XE_REG(MCHBAR_MIRROR_BASE_SNB + 0x59a0)
#define   PWR_LIM_VAL				REG_GENMASK(14, 0)
#define   PWR_LIM_EN				REG_BIT(15)
#define   PWR_LIM				REG_GENMASK(15, 0)
#define   PWR_LIM_TIME				REG_GENMASK(23, 17)
#define   PWR_LIM_TIME_X			REG_GENMASK(23, 22)
#define   PWR_LIM_TIME_Y			REG_GENMASK(21, 17)

#endif /* _XE_MCHBAR_REGS_H_ */
