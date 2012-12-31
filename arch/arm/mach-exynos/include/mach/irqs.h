/* linux/arch/arm/mach-exynos/include/mach/irqs.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - IRQ definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H __FILE__

#include <plat/irqs.h>

/* SGI: Software Generated Interrupt */

#define IRQ_SGI(x)		S5P_IRQ(x)

/* PPI: Private Peripheral Interrupt */

#define IRQ_PPI(x)		S5P_IRQ(x+16)

#define IRQ_PPI_MCT_L		IRQ_PPI(12)

/* SPI: Shared Peripheral Interrupt */

#define IRQ_SPI(x)		S5P_IRQ(x+32)

#if defined(CONFIG_ARCH_EXYNOS4)
#include "irqs-exynos4.h"
#elif defined(CONFIG_ARCH_EXYNOS5)
#include "irqs-exynos5.h"
#else
#error "ARCH_EXYNOS* is not defined"
#endif

#endif /* __ASM_ARCH_IRQS_H */
