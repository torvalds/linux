/* linux/include/asm-arm/plat-s3c/map.h
 *
 * Copyright 2003, 2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C - Memory map definitions (virtual addresses)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_MAP_H
#define __ASM_PLAT_MAP_H __FILE__

/* Fit all our registers in at CONFIG_S3C_BASE_ADDR upwards, trying to
 * use as little of the VA space as possible so vmalloc and friends
 * have a better chance of getting memory.
 *
 * we try to ensure stuff like the IRQ registers are available for
 * an single MOVS instruction (ie, only 8 bits of set data)
 */

#define S3C_ADDR_BASE	CONFIG_S3C_ADDR_BASE

#ifndef __ASSEMBLY__
#define S3C_ADDR(x)	((void __iomem __force *)S3C_ADDR_BASE + (x))
#else
#define S3C_ADDR(x)	(S3C_ADDR_BASE + (x))
#endif

#define S3C_VA_IRQ	S3C_ADDR(0x00000000)	/* irq controller(s) */
#define S3C_VA_SYS	S3C_ADDR(0x00100000)	/* system control */
#define S3C_VA_MEM	S3C_ADDR(0x00200000)	/* memory control */
#define S3C_VA_TIMER	S3C_ADDR(0x00300000)	/* timer block */
#define S3C_VA_WATCHDOG	S3C_ADDR(0x00400000)	/* watchdog */
#define S3C_VA_HSOTG	S3C_ADDR(0x00E00000)    /* OTG */
#define S3C_VA_HSPHY	S3C_ADDR(0x00F00000)    /* OTG PHY */
#define S3C_VA_UART	S3C_ADDR(0x01000000)	/* UART */

#define S3C_VA_KLOG_BUF	S3C_ADDR(0x01100000)	/* non-cached log buf */
#define S3C_VA_SLOG_BUF	S3C_ADDR(0x01400000)	/* non-cached sched log buf */
#define S3C_VA_AUXLOG_BUF	S3C_ADDR(0x01600000)	/* auxiliary log buf */

/* This is used for the CPU specific mappings that may be needed, so that
 * they do not need to directly used S3C_ADDR() and thus make it easier to
 * modify the space for mapping.
 */
#define S3C_ADDR_CPU(x)	S3C_ADDR(0x00500000 + (x))

#endif /* __ASM_PLAT_MAP_H */
