/* linux/arch/arm/mach-s5p6442/include/mach/map.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P6442 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>
#include <plat/map-s5p.h>

#define S5P6442_PA_CHIPID	(0xE0000000)
#define S5P_PA_CHIPID		S5P6442_PA_CHIPID

#define S5P6442_PA_SYSCON	(0xE0100000)
#define S5P_PA_SYSCON		S5P6442_PA_SYSCON

#define S5P6442_PA_GPIO		(0xE0200000)
#define S5P_PA_GPIO		S5P6442_PA_GPIO

#define S5P6442_PA_VIC0		(0xE4000000)
#define S5P_PA_VIC0		S5P6442_PA_VIC0

#define S5P6442_PA_VIC1		(0xE4100000)
#define S5P_PA_VIC1		S5P6442_PA_VIC1

#define S5P6442_PA_VIC2		(0xE4200000)
#define S5P_PA_VIC2		S5P6442_PA_VIC2

#define S5P6442_PA_TIMER	(0xEA000000)
#define S5P_PA_TIMER		S5P6442_PA_TIMER

#define S5P6442_PA_SYSTIMER   	(0xEA100000)

#define S5P6442_PA_UART		(0xEC000000)

#define S5P_PA_UART0		(S5P6442_PA_UART + 0x0)
#define S5P_PA_UART1		(S5P6442_PA_UART + 0x400)
#define S5P_PA_UART2		(S5P6442_PA_UART + 0x800)
#define S5P_SZ_UART		SZ_256

#define S5P6442_PA_IIC0		(0xEC100000)

#define S5P6442_PA_SDRAM	(0x20000000)
#define S5P_PA_SDRAM		S5P6442_PA_SDRAM

/* compatibiltiy defines. */
#define S3C_PA_UART		S5P6442_PA_UART
#define S3C_PA_IIC		S5P6442_PA_IIC0

#endif /* __ASM_ARCH_MAP_H */
