/* linux/arch/arm/mach-s5p64x0/include/mach/regs-gpio.h
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - GPIO register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_GPIO_H
#define __ASM_ARCH_REGS_GPIO_H __FILE__

#include <mach/map.h>

/* Base addresses for each of the banks */

#define S5P64X0_GPA_BASE		(S5P_VA_GPIO + 0x0000)
#define S5P64X0_GPB_BASE		(S5P_VA_GPIO + 0x0020)
#define S5P64X0_GPC_BASE		(S5P_VA_GPIO + 0x0040)
#define S5P64X0_GPF_BASE		(S5P_VA_GPIO + 0x00A0)
#define S5P64X0_GPG_BASE		(S5P_VA_GPIO + 0x00C0)
#define S5P64X0_GPH_BASE		(S5P_VA_GPIO + 0x00E0)
#define S5P64X0_GPI_BASE		(S5P_VA_GPIO + 0x0100)
#define S5P64X0_GPJ_BASE		(S5P_VA_GPIO + 0x0120)
#define S5P64X0_GPN_BASE		(S5P_VA_GPIO + 0x0830)
#define S5P64X0_GPP_BASE		(S5P_VA_GPIO + 0x0160)
#define S5P64X0_GPR_BASE		(S5P_VA_GPIO + 0x0290)

#define S5P6450_GPD_BASE		(S5P_VA_GPIO + 0x0060)
#define S5P6450_GPK_BASE		(S5P_VA_GPIO + 0x0140)
#define S5P6450_GPQ_BASE		(S5P_VA_GPIO + 0x0180)
#define S5P6450_GPS_BASE		(S5P_VA_GPIO + 0x0300)

/* External interrupt control registers for group0 */

#define EINT0CON0_OFFSET		(0x900)
#define EINT0MASK_OFFSET		(0x920)
#define EINT0PEND_OFFSET		(0x924)

#define S5P64X0_EINT0CON0		(S5P_VA_GPIO + EINT0CON0_OFFSET)
#define S5P64X0_EINT0MASK		(S5P_VA_GPIO + EINT0MASK_OFFSET)
#define S5P64X0_EINT0PEND		(S5P_VA_GPIO + EINT0PEND_OFFSET)

#endif /* __ASM_ARCH_REGS_GPIO_H */
