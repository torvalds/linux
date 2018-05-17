/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX - Memory map definitions
 */

#ifndef __ASM_PLAT_MAP_S3C_H
#define __ASM_PLAT_MAP_S3C_H __FILE__

#define S3C24XX_VA_IRQ		S3C_VA_IRQ
#define S3C24XX_VA_MEMCTRL	S3C_VA_MEM
#define S3C24XX_VA_UART		S3C_VA_UART

#define S3C24XX_VA_TIMER	S3C_VA_TIMER
#define S3C24XX_VA_CLKPWR	S3C_VA_SYS
#define S3C24XX_VA_WATCHDOG	S3C_VA_WATCHDOG

#define S3C2412_VA_SSMC		S3C_ADDR_CPU(0x00000000)
#define S3C2412_VA_EBI		S3C_ADDR_CPU(0x00100000)

#define S3C2410_PA_UART		(0x50000000)
#define S3C24XX_PA_UART		S3C2410_PA_UART

/*
 * GPIO ports
 *
 * the calculation for the VA of this must ensure that
 * it is the same distance apart from the UART in the
 * phsyical address space, as the initial mapping for the IO
 * is done as a 1:1 mapping. This puts it (currently) at
 * 0xFA800000, which is not in the way of any current mapping
 * by the base system.
*/

#define S3C2410_PA_GPIO		(0x56000000)
#define S3C24XX_PA_GPIO		S3C2410_PA_GPIO

#define S3C24XX_VA_GPIO		((S3C24XX_PA_GPIO - S3C24XX_PA_UART) + S3C24XX_VA_UART)
#define S3C64XX_VA_GPIO		S3C_ADDR_CPU(0x00000000)

#define S3C64XX_VA_MODEM	S3C_ADDR_CPU(0x00100000)
#define S3C64XX_VA_USB_HSPHY	S3C_ADDR_CPU(0x00200000)

#define S3C_VA_USB_HSPHY	S3C64XX_VA_USB_HSPHY

/*
 * ISA style IO, for each machine to sort out mappings for,
 * if it implements it. We reserve two 16M regions for ISA.
 */

#define S3C2410_ADDR(x)		S3C_ADDR(x)

#define S3C24XX_VA_ISA_WORD	S3C2410_ADDR(0x02000000)
#define S3C24XX_VA_ISA_BYTE	S3C2410_ADDR(0x03000000)

/* deal with the registers that move under the 2412/2413 */

#if defined(CONFIG_CPU_S3C2412)
#ifndef __ASSEMBLY__
extern void __iomem *s3c24xx_va_gpio2;
#endif
#ifdef CONFIG_CPU_S3C2412_ONLY
#define S3C24XX_VA_GPIO2	(S3C24XX_VA_GPIO + 0x10)
#else
#define S3C24XX_VA_GPIO2 s3c24xx_va_gpio2
#endif
#else
#define s3c24xx_va_gpio2 S3C24XX_VA_GPIO
#define S3C24XX_VA_GPIO2 S3C24XX_VA_GPIO
#endif

#include <plat/map-s5p.h>

#endif /* __ASM_PLAT_MAP_S3C_H */
