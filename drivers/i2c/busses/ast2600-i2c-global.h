/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Copyright (C) ASPEED Technology Inc.
 *	Ryan Chen <ryan_chen@aspeedtech.com>
 */

#ifndef AST2600_I2C_GLOBAL_H
#define AST2600_I2C_GLOBAL_H

#include <linux/bits.h>

#define ASPEED_I2CG_ISR				0x00
#define ASPEED_I2CG_SLAVE_ISR		0x04	/* ast2600 */
#define ASPEED_I2CG_OWNER			0x08
#define ASPEED_I2CG_CTRL			0x0C
#define ASPEED_I2CG_CLK_DIV_CTRL	0x10	/* ast2600 */

/* 0x0C : I2CG SRAM Buffer Enable  */
#define ASPEED_I2CG_SRAM_BUFFER_ENABLE		BIT(0)

/* ast2600 */
#define ASPEED_I2CG_SLAVE_PKT_NAK		BIT(4)
#define ASPEED_I2CG_M_S_SEPARATE_INTR	BIT(3)
#define ASPEED_I2CG_CTRL_NEW_REG		BIT(2)
#define ASPEED_I2CG_CTRL_NEW_CLK_DIV	BIT(1)

#endif /* AST2600_I2C_GLOBAL_H */
