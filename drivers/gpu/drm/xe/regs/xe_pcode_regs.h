/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_PCODE_REGS_H_
#define _XE_PCODE_REGS_H_

#include "regs/xe_reg_defs.h"

/*
 * This file contains addresses of PCODE registers visible through GT MMIO space.
 */

#define PVC_GT0_PACKAGE_ENERGY_STATUS           XE_REG(0x281004)
#define PVC_GT0_PACKAGE_RAPL_LIMIT              XE_REG(0x281008)
#define PVC_GT0_PACKAGE_POWER_SKU_UNIT          XE_REG(0x281068)
#define PVC_GT0_PLATFORM_ENERGY_STATUS          XE_REG(0x28106c)
#define PVC_GT0_PACKAGE_POWER_SKU               XE_REG(0x281080)

#define BMG_PACKAGE_POWER_SKU			XE_REG(0x138098)
#define BMG_PACKAGE_POWER_SKU_UNIT		XE_REG(0x1380dc)
#define BMG_PACKAGE_ENERGY_STATUS		XE_REG(0x138120)
#define BMG_VRAM_TEMPERATURE			XE_REG(0x1382c0)
#define BMG_PACKAGE_TEMPERATURE			XE_REG(0x138434)
#define BMG_PACKAGE_RAPL_LIMIT			XE_REG(0x138440)
#define BMG_PLATFORM_ENERGY_STATUS		XE_REG(0x138458)
#define BMG_PLATFORM_POWER_LIMIT		XE_REG(0x138460)

#endif /* _XE_PCODE_REGS_H_ */
