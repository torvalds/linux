/* linux/arch/arm/mach-exynos/include/mach/regs-iem.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - IEM(INTELLIGENT ENERGY MANAGEMENT) register discription
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_IEM_H
#define __ASM_ARCH_REGS_IEM_H __FILE__

/* Register for IEC  */
#define EXYNOS4_IECDPCCR		(0x00000)

/* Register for APC */
#define EXYNOS4_APC_CONTROL		(0x10010)
#define EXYNOS4_APC_PREDLYSEL		(0x10024)
#define EXYNOS4_APC_DBG_DLYCODE		(0x100E0)

#define APC_HPM_EN			(1 << 4)
#define IEC_EN				(1 << 0)

#endif /* __ASM_ARCH_REGS_IEM_H */
