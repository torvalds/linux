/*
 * reset AT91SAM9G20 as per errata
 *
 * (C) BitBox Ltd 2010
 *
 * unless the SDRAM is cleanly shutdown before we hit the
 * reset register it can be left driving the data bus and
 * killing the chance of a subsequent boot from NAND
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define CP15_CR_I			(1 << 12)

#define SYS_VIRT_OFS			(-0x01000000)

#define SDRAMC_BASE			(SYS_VIRT_OFS + 0xffffea00)
#define  SDRAMC_TR			0x0004
#define  SDRAMC_LPR			0x0010
#define   SDRAMC_LPCB_POWER_DOWN	2

#define RSTC_BASE			(SYS_VIRT_OFS + 0xfffffd00)
#define  RSTC_CR			0x0000
#define   RSTC_PROCRST			(1 << 0)
#define   RSTC_PERRST			(1 << 2)
#define   RSTC_KEY			(0xa5 << 24)

			.arm

			.globl	at91sam9g20_reset

at91sam9g20_reset:	mov	r0, #0
			mcr	p15, 0, r0, c7, c5, 0		@ flush I-cache

			mrc	p15, 0, r0, c1, c0, 0
			orr	r0, r0, #CP15_CR_I
			mcr	p15, 0, r0, c1, c0, 0		@ enable I-cache

			ldr	r0, =SDRAMC_BASE		@ preload constants
			ldr	r1, =RSTC_BASE

			mov	r2, #1
			mov	r3, #SDRAMC_LPCB_POWER_DOWN
			ldr	r4, =RSTC_KEY | RSTC_PERRST | RSTC_PROCRST

			.balign	32				@ align to cache line

			str	r2, [r0, #SDRAMC_TR]		@ disable SDRAM access
			str	r3, [r0, #SDRAMC_LPR]		@ power down SDRAM
			str	r4, [r1, #RSTC_CR]		@ reset processor

			b	.
