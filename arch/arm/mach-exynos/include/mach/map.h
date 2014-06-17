/* linux/arch/arm/mach-exynos/include/mach/map.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>

/*
 * EXYNOS4 UART offset is 0x10000 but the older S5P SoCs are 0x400.
 * So need to define it, and here is to avoid redefinition warning.
 */
#define S3C_UART_OFFSET			(0x10000)

#include <plat/map-s5p.h>

#define EXYNOS_PA_CHIPID		0x10000000

#define EXYNOS4_PA_SYSCON		0x10010000
#define EXYNOS5_PA_SYSCON		0x10050100

#define EXYNOS4_PA_PMU			0x10020000
#define EXYNOS5_PA_PMU			0x10040000

#define EXYNOS4_PA_CMU			0x10030000
#define EXYNOS5_PA_CMU			0x10010000

#define EXYNOS4_PA_SYSTIMER		0x10050000

#define EXYNOS4_PA_WATCHDOG		0x10060000
#define EXYNOS5_PA_WATCHDOG		0x101D0000

#define EXYNOS4_PA_DMC0			0x10400000
#define EXYNOS4_PA_DMC1			0x10410000

#define EXYNOS4_PA_COMBINER		0x10440000
#define EXYNOS5_PA_COMBINER		0x10440000

#define EXYNOS4_PA_GIC_CPU		0x10480000
#define EXYNOS4_PA_GIC_DIST		0x10490000
#define EXYNOS5_PA_GIC_CPU		0x10482000
#define EXYNOS5_PA_GIC_DIST		0x10481000

#define EXYNOS4_PA_COREPERI		0x10500000
#define EXYNOS4_PA_L2CC			0x10502000

#define EXYNOS4_PA_SROMC		0x12570000
#define EXYNOS5_PA_SROMC		0x12250000

#define EXYNOS4_PA_HSPHY		0x125B0000

#define EXYNOS4_PA_UART			0x13800000
#define EXYNOS5_PA_UART			0x12C00000

#define EXYNOS4_PA_TIMER		0x139D0000
#define EXYNOS5_PA_TIMER		0x12DD0000

/* Compatibility UART */

#define EXYNOS5440_PA_UART0		0x000B0000

#define S3C_VA_UARTx(x)			(S3C_VA_UART + ((x) * S3C_UART_OFFSET))

#endif /* __ASM_ARCH_MAP_H */
