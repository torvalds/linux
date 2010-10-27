/* linux/arch/arm/mach-s5p6440/include/mach/regs-gpio.h
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P6440 - GPIO register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_REGS_GPIO_H
#define __ASM_ARCH_REGS_GPIO_H __FILE__

#include <mach/map.h>

/* Base addresses for each of the banks */
#define S5P6440_GPA_BASE		(S5P_VA_GPIO + 0x0000)
#define S5P6440_GPB_BASE		(S5P_VA_GPIO + 0x0020)
#define S5P6440_GPC_BASE		(S5P_VA_GPIO + 0x0040)
#define S5P6440_GPF_BASE		(S5P_VA_GPIO + 0x00A0)
#define S5P6440_GPG_BASE		(S5P_VA_GPIO + 0x00C0)
#define S5P6440_GPH_BASE		(S5P_VA_GPIO + 0x00E0)
#define S5P6440_GPI_BASE		(S5P_VA_GPIO + 0x0100)
#define S5P6440_GPJ_BASE		(S5P_VA_GPIO + 0x0120)
#define S5P6440_GPN_BASE		(S5P_VA_GPIO + 0x0830)
#define S5P6440_GPP_BASE		(S5P_VA_GPIO + 0x0160)
#define S5P6440_GPR_BASE		(S5P_VA_GPIO + 0x0290)
#define S5P6440_EINT0CON0		(S5P_VA_GPIO + 0x900)
#define S5P6440_EINT0FLTCON0		(S5P_VA_GPIO + 0x910)
#define S5P6440_EINT0FLTCON1		(S5P_VA_GPIO + 0x914)
#define S5P6440_EINT0MASK		(S5P_VA_GPIO + 0x920)
#define S5P6440_EINT0PEND		(S5P_VA_GPIO + 0x924)

/* for LCD */
#define S5P6440_SPCON_LCD_SEL_RGB	(1 << 0)
#define S5P6440_SPCON_LCD_SEL_MASK	(3 << 0)

/* These set of macros are not really useful for the
 * GPF/GPI/GPJ/GPN/GPP,
 * useful for others set of GPIO's (4 bit)
 */
#define S5P6440_GPIO_CONMASK(__gpio)	(0xf << ((__gpio) * 4))
#define S5P6440_GPIO_INPUT(__gpio)	(0x0 << ((__gpio) * 4))
#define S5P6440_GPIO_OUTPUT(__gpio)	(0x1 << ((__gpio) * 4))

/* Use these macros for GPF/GPI/GPJ/GPN/GPP set of GPIO (2 bit)
 * */
#define S5P6440_GPIO2_CONMASK(__gpio)	(0x3 << ((__gpio) * 2))
#define S5P6440_GPIO2_INPUT(__gpio)	(0x0 << ((__gpio) * 2))
#define S5P6440_GPIO2_OUTPUT(__gpio)	(0x1 << ((__gpio) * 2))

#endif /* __ASM_ARCH_REGS_GPIO_H */
