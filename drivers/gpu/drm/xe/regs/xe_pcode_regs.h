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

#define BMG_FAN_1_SPEED				XE_REG(0x138140)
#define BMG_FAN_2_SPEED				XE_REG(0x138170)
#define BMG_FAN_3_SPEED				XE_REG(0x1381a0)
#define BMG_VRAM_TEMPERATURE			XE_REG(0x1382c0)
#define BMG_PACKAGE_TEMPERATURE			XE_REG(0x138434)

#endif /* _XE_PCODE_REGS_H_ */
