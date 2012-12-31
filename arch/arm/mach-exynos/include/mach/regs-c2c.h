/* linux/arch/arm/mach-exynos/include/mach/regs-c2c.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Register definition file for Samsung C2C
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARM_REGS_S5P_C2C_H
#define __ASM_ARM_REGS_S5P_C2C_H

#include <plat/map-base.h>

/***************************************************************/
/* C2C Registers part				*/
/***************************************************************/
#define EXYNOS_C2C_REVISION		0x0
#define EXYNOS_C2C_SYSCONFIG		0x4
#define EXYNOS_C2C_SYSSTATUS		0x8
#define EXYNOS_C2C_PORTCONFIG		0xc
#define EXYNOS_C2C_MIRRORMODE		0x10
#define EXYNOS_C2C_IRQ_RAW_STAT0	0x14
#define EXYNOS_C2C_IRQ_RAW_STAT1	0x18
#define EXYNOS_C2C_IRQ_EN_STAT0	0x1c
#define EXYNOS_C2C_IRQ_EN_STAT1	0x20
#define EXYNOS_C2C_IRQ_EN_SET0		0x24
#define EXYNOS_C2C_IRQ_EN_SET1		0x28
#define EXYNOS_C2C_IRQ_EN_CLEAR0	0x2c
#define EXYNOS_C2C_IRQ_EN_CLEAR1	0x30
#define EXYNOS_C2C_IRQ_EOI		0x34

#define EXYNOS_C2C_FCLK_FREQ		0x40
#define EXYNOS_C2C_RX_MAX_FREQ		0x44
#define EXYNOS_C2C_TX_MAX_FREQ		0x48
#define EXYNOS_C2C_RX_MAX_FREQ_ACK	0x4c
#define EXYNOS_C2C_WAKE_REQ		0x50
#define EXYNOS_C2C_WAKE_ACK		0x54
#define EXYNOS_C2C_STANDBY		0x60
#define EXYNOS_C2C_STANDBY_IN		0x64
#define EXYNOS_C2C_WAIT		0x68
#define EXYNOS_C2C_GENI_CONTROL	0x70
#define EXYNOS_C2C_GENI_MASK		0x74
#define EXYNOS_C2C_GENO_STATUS		0x80

#define EXYNOS_C2C_GENO_INT		0x84
#define EXYNOS_C2C_GENO_LEVEL		0x88

/***************************************************************/
/* C2C Bit definition part					*/
/***************************************************************/
/* SYSREG Bit definition */
#define C2C_SYSREG_CG		(31) /* C2C Clock Gating [31] */
#define C2C_SYSREG_MO		(30) /* Master On [30] */
#define C2C_SYSREG_FCLK		(20) /* Default Functional Clock Freq [29:20] */
#define C2C_SYSREG_TXW		(18) /* Default Tx Width [19:18] */
#define C2C_SYSREG_RXW		(16) /* Default Rx Width [17:16] */
#define C2C_SYSREG_RST		(15) /* Reset [15] */
#define C2C_SYSREG_MD		(14) /* Master On [14] */
#define C2C_SYSREG_RTRST	(13) /* Reset retention registers [13] */
#define C2C_SYSREG_BASE_ADDR	(3)  /* DRAM Base Address [12:3] */
#define C2C_SYSREG_DRAM_SIZE	(0)  /* DRAM Size [2:0] */

#define C2C_GENIO_LATENCY_INT	(26)
#define C2C_GENIO_OPP_INT	(27)
#define C2C_GENIO_OPP_MODE0	(28)
#define C2C_GENIO_OPP_MODE1	(29)
#define C2C_GENIO_LATENCY0	(30)
#define C2C_GENIO_LATENCY1	(31)
#endif /* __ASM_ARM_REGS_S5P_C2C_H */
