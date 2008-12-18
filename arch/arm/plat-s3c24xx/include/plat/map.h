/* linux/include/asm-arm/plat-s3c24xx/map.h
 *
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_S3C24XX_MAP_H
#define __ASM_PLAT_S3C24XX_MAP_H

/* interrupt controller is the first thing we put in, to make
 * the assembly code for the irq detection easier
 */
#define S3C24XX_VA_IRQ	   S3C_VA_IRQ
#define S3C2410_PA_IRQ	   (0x4A000000)
#define S3C24XX_SZ_IRQ	   SZ_1M

/* memory controller registers */
#define S3C24XX_VA_MEMCTRL S3C_VA_MEM
#define S3C2410_PA_MEMCTRL (0x48000000)
#define S3C24XX_SZ_MEMCTRL SZ_1M

/* UARTs */
#define S3C24XX_VA_UART	   S3C_VA_UART
#define S3C2410_PA_UART	   (0x50000000)
#define S3C24XX_SZ_UART	   SZ_1M
#define S3C_UART_OFFSET	   (0x4000)

/* Timers */
#define S3C24XX_VA_TIMER   S3C_VA_TIMER
#define S3C2410_PA_TIMER   (0x51000000)
#define S3C24XX_SZ_TIMER   SZ_1M

/* Clock and Power management */
#define S3C24XX_VA_CLKPWR  S3C_VA_SYS
#define S3C24XX_SZ_CLKPWR  SZ_1M

/* USB Device port */
#define S3C2410_PA_USBDEV  (0x52000000)
#define S3C24XX_SZ_USBDEV  SZ_1M

/* Watchdog */
#define S3C24XX_VA_WATCHDOG S3C_VA_WATCHDOG
#define S3C2410_PA_WATCHDOG (0x53000000)
#define S3C24XX_SZ_WATCHDOG SZ_1M

/* Standard size definitions for peripheral blocks. */

#define S3C24XX_SZ_IIS		SZ_1M
#define S3C24XX_SZ_ADC		SZ_1M
#define S3C24XX_SZ_SPI		SZ_1M
#define S3C24XX_SZ_SDI		SZ_1M
#define S3C24XX_SZ_NAND		SZ_1M
#define S3C24XX_SZ_USBHOST	SZ_1M

/* GPIO ports */

/* the calculation for the VA of this must ensure that
 * it is the same distance apart from the UART in the
 * phsyical address space, as the initial mapping for the IO
 * is done as a 1:1 maping. This puts it (currently) at
 * 0xFA800000, which is not in the way of any current mapping
 * by the base system.
*/

#define S3C2410_PA_GPIO	   (0x56000000)
#define S3C24XX_VA_GPIO	   ((S3C24XX_PA_GPIO - S3C24XX_PA_UART) + S3C24XX_VA_UART)
#define S3C24XX_SZ_GPIO	   SZ_1M


/* ISA style IO, for each machine to sort out mappings for, if it
 * implements it. We reserve two 16M regions for ISA.
 */

#define S3C24XX_VA_ISA_WORD  S3C2410_ADDR(0x02000000)
#define S3C24XX_VA_ISA_BYTE  S3C2410_ADDR(0x03000000)

/* deal with the registers that move under the 2412/2413 */

#if defined(CONFIG_CPU_S3C2412) || defined(CONFIG_CPU_S3C2413)
#ifndef __ASSEMBLY__
extern void __iomem *s3c24xx_va_gpio2;
#endif
#ifdef CONFIG_CPU_S3C2412_ONLY
#define S3C24XX_VA_GPIO2 (S3C24XX_VA_GPIO + 0x10)
#else
#define S3C24XX_VA_GPIO2 s3c24xx_va_gpio2
#endif
#else
#define s3c24xx_va_gpio2 S3C24XX_VA_GPIO
#define S3C24XX_VA_GPIO2 S3C24XX_VA_GPIO
#endif

#endif /* __ASM_PLAT_S3C24XX_MAP_H */
