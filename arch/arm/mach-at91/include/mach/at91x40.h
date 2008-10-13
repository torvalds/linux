/*
 * arch/arm/mach-at91/include/mach/at91x40.h
 *
 * (C) Copyright 2007, Greg Ungerer <gerg@snapgear.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91X40_H
#define AT91X40_H

/*
 *	IRQ list.
 */
#define AT91_ID_FIQ		0	/* FIQ */
#define AT91_ID_SYS		1	/* System Peripheral */
#define AT91X40_ID_USART0	2	/* USART port 0 */
#define AT91X40_ID_USART1	3	/* USART port 1 */
#define AT91X40_ID_TC0		4	/* Timer/Counter 0 */
#define AT91X40_ID_TC1		5	/* Timer/Counter 1*/
#define AT91X40_ID_TC2		6	/* Timer/Counter 2*/
#define AT91X40_ID_WD		7	/* Watchdog? */
#define AT91X40_ID_PIOA		8	/* Parallel IO Controller A */

#define AT91X40_ID_IRQ0		16	/* External IRQ 0 */
#define AT91X40_ID_IRQ1		17	/* External IRQ 1 */
#define AT91X40_ID_IRQ2		18	/* External IRQ 2 */

/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
#define AT91_BASE_SYS	0xffc00000

#define AT91_EBI	(0xffe00000 - AT91_BASE_SYS)	/* External Bus Interface */
#define AT91_SF		(0xfff00000 - AT91_BASE_SYS)	/* Special Function */
#define AT91_USART1	(0xfffcc000 - AT91_BASE_SYS)	/* USART 1 */
#define AT91_USART0	(0xfffd0000 - AT91_BASE_SYS)	/* USART 0 */
#define AT91_TC		(0xfffe0000 - AT91_BASE_SYS)	/* Timer Counter */
#define AT91_PIOA	(0xffff0000 - AT91_BASE_SYS)	/* PIO Controller A */
#define AT91_PS		(0xffff4000 - AT91_BASE_SYS)	/* Power Save */
#define AT91_WD		(0xffff8000 - AT91_BASE_SYS)	/* Watchdog Timer */
#define AT91_AIC	(0xfffff000 - AT91_BASE_SYS)	/* Advanced Interrupt Controller */

/*
 * The AT91x40 series doesn't have a debug unit like the other AT91 parts.
 * But it does have a chip identify register and extension ID, so define at
 * least these here.
 */
#define AT91_DBGU_CIDR	(AT91_SF + 0)	/* CIDR in PS segment */
#define AT91_DBGU_EXID	(AT91_SF + 4)	/* EXID in PS segment */

#endif /* AT91X40_H */
