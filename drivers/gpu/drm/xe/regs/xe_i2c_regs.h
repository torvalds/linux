/* SPDX-License-Identifier: MIT */
#ifndef _XE_I2C_REGS_H_
#define _XE_I2C_REGS_H_

#include "xe_reg_defs.h"
#include "xe_regs.h"

#define I2C_BRIDGE_OFFSET		(SOC_BASE + 0xd9000)
#define I2C_CONFIG_SPACE_OFFSET		(SOC_BASE + 0xf6000)
#define I2C_MEM_SPACE_OFFSET		(SOC_BASE + 0xf7400)

#define REG_SG_REMAP_ADDR_PREFIX	XE_REG(SOC_BASE + 0x0164)
#define REG_SG_REMAP_ADDR_POSTFIX	XE_REG(SOC_BASE + 0x0168)

#endif /* _XE_I2C_REGS_H_ */
