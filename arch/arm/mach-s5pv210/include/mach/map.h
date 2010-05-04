/* linux/arch/arm/mach-s5pv210/include/mach/map.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV210 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>
#include <plat/map-s5p.h>

#define S5PV210_PA_CHIPID	(0xE0000000)
#define S5P_PA_CHIPID		S5PV210_PA_CHIPID

#define S5PV210_PA_SYSCON	(0xE0100000)
#define S5P_PA_SYSCON		S5PV210_PA_SYSCON

#define S5PV210_PA_GPIO		(0xE0200000)
#define S5P_PA_GPIO		S5PV210_PA_GPIO

#define S5PV210_PA_IIC0		(0xE1800000)

#define S5PV210_PA_TIMER	(0xE2500000)
#define S5P_PA_TIMER		S5PV210_PA_TIMER

#define S5PV210_PA_SYSTIMER	(0xE2600000)

#define S5PV210_PA_UART		(0xE2900000)

#define S5P_PA_UART0		(S5PV210_PA_UART + 0x0)
#define S5P_PA_UART1		(S5PV210_PA_UART + 0x400)
#define S5P_PA_UART2		(S5PV210_PA_UART + 0x800)
#define S5P_PA_UART3		(S5PV210_PA_UART + 0xC00)

#define S5P_SZ_UART		SZ_256

#define S5PV210_PA_SROMC	(0xE8000000)

#define S5PV210_PA_VIC0		(0xF2000000)
#define S5P_PA_VIC0		S5PV210_PA_VIC0

#define S5PV210_PA_VIC1		(0xF2100000)
#define S5P_PA_VIC1		S5PV210_PA_VIC1

#define S5PV210_PA_VIC2		(0xF2200000)
#define S5P_PA_VIC2		S5PV210_PA_VIC2

#define S5PV210_PA_VIC3		(0xF2300000)
#define S5P_PA_VIC3		S5PV210_PA_VIC3

#define S5PV210_PA_SDRAM	(0x20000000)
#define S5P_PA_SDRAM		S5PV210_PA_SDRAM

/* compatibiltiy defines. */
#define S3C_PA_UART		S5PV210_PA_UART
#define S3C_PA_IIC		S5PV210_PA_IIC0

#endif /* __ASM_ARCH_MAP_H */
