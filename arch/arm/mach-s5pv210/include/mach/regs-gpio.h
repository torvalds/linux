/* linux/arch/arm/mach-s5pv210/include/mach/regs-gpio.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV210 - GPIO (including EINT) register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_GPIO_H
#define __ASM_ARCH_REGS_GPIO_H __FILE__

#include <mach/map.h>

#define S5PV210_EINT30CON		(S5P_VA_GPIO + 0xE00)
#define S5P_EINT_CON(x)			(S5PV210_EINT30CON + ((x) * 0x4))

#define S5PV210_EINT30FLTCON0		(S5P_VA_GPIO + 0xE80)
#define S5P_EINT_FLTCON(x)		(S5PV210_EINT30FLTCON0 + ((x) * 0x4))

#define S5PV210_EINT30MASK		(S5P_VA_GPIO + 0xF00)
#define S5P_EINT_MASK(x)		(S5PV210_EINT30MASK + ((x) * 0x4))

#define S5PV210_EINT30PEND		(S5P_VA_GPIO + 0xF40)
#define S5P_EINT_PEND(x)		(S5PV210_EINT30PEND + ((x) * 0x4))

#define EINT_REG_NR(x)			(EINT_OFFSET(x) >> 3)

#define eint_irq_to_bit(irq)		(1 << (EINT_OFFSET(irq) & 0x7))

#define EINT_MODE		S3C_GPIO_SFN(0xf)

#define EINT_GPIO_0(x)		S5PV210_GPH0(x)
#define EINT_GPIO_1(x)		S5PV210_GPH1(x)
#define EINT_GPIO_2(x)		S5PV210_GPH2(x)
#define EINT_GPIO_3(x)		S5PV210_GPH3(x)

#endif /* __ASM_ARCH_REGS_GPIO_H */
