/* linux/arch/arm/mach-s5pc100/include/mach/map.h
 *
 * Copyright 2009 Samsung Electronics Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * S5PC100 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>
#include <plat/map-s5p.h>

#define S5PC100_PA_CHIPID	(0xE0000000)
#define S5P_PA_CHIPID		S5PC100_PA_CHIPID

#define S5PC100_PA_SYSCON	(0xE0100000)
#define S5P_PA_SYSCON		S5PC100_PA_SYSCON

#define S5PC100_PA_OTHERS	(0xE0200000)
#define S5PC100_VA_OTHERS	(S3C_VA_SYS + 0x10000)

#define S5PC100_PA_GPIO		(0xE0300000)
#define S5P_PA_GPIO		S5PC100_PA_GPIO

#define S5PC100_PA_VIC0		(0xE4000000)
#define S5P_PA_VIC0		S5PC100_PA_VIC0

#define S5PC100_PA_VIC1		(0xE4100000)
#define S5P_PA_VIC1		S5PC100_PA_VIC1

#define S5PC100_PA_VIC2		(0xE4200000)
#define S5P_PA_VIC2		S5PC100_PA_VIC2

#define S5PC100_PA_TIMER	(0xEA000000)
#define S5P_PA_TIMER		S5PC100_PA_TIMER

#define S5PC100_PA_SYSTIMER	(0xEA100000)

#define S5PC100_PA_UART		(0xEC000000)

#define S5P_PA_UART0		(S5PC100_PA_UART + 0x0)
#define S5P_PA_UART1		(S5PC100_PA_UART + 0x400)
#define S5P_PA_UART2		(S5PC100_PA_UART + 0x800)
#define S5P_PA_UART3		(S5PC100_PA_UART + 0xC00)
#define S5P_SZ_UART		SZ_256

#define S5PC100_PA_IIC0		(0xEC100000)
#define S5PC100_PA_IIC1		(0xEC200000)

#define S5PC100_PA_FB		(0xEE000000)

#define S5PC100_PA_AC97		0xF2300000

/* PCM */
#define S5PC100_PA_PCM0		0xF2400000
#define S5PC100_PA_PCM1		0xF2500000

/* KEYPAD */
#define S5PC100_PA_KEYPAD	(0xF3100000)
>>>>>>> for-2635-4/s5p-devs:arch/arm/mach-s5pc100/include/mach/map.h

#define S5PC100_PA_HSMMC(x)	(0xED800000 + ((x) * 0x100000))

#define S5PC100_PA_SDRAM	(0x20000000)
#define S5P_PA_SDRAM		S5PC100_PA_SDRAM

/* compatibiltiy defines. */
#define S3C_PA_UART		S5PC100_PA_UART
#define S3C_PA_IIC		S5PC100_PA_IIC0
#define S3C_PA_IIC1		S5PC100_PA_IIC1
#define S3C_PA_FB		S5PC100_PA_FB
#define S3C_PA_HSMMC0		S5PC100_PA_HSMMC(0)
#define S3C_PA_HSMMC1		S5PC100_PA_HSMMC(1)
#define S3C_PA_HSMMC2		S5PC100_PA_HSMMC(2)

#endif /* __ASM_ARCH_MAP_H */
