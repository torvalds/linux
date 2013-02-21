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

#include <linux/linkage.h>
#include <mach/hardware.h>
#include <mach/at91_ramc.h>
#include "at91_rstc.h"

			.arm

			.globl	at91sam9_alt_restart

at91sam9_alt_restart:	ldr	r0, =at91_ramc_base		@ preload constants
			ldr	r0, [r0]
			ldr	r4, =at91_rstc_base
			ldr	r1, [r4]

			mov	r2, #1
			mov	r3, #AT91_SDRAMC_LPCB_POWER_DOWN
			ldr	r4, =AT91_RSTC_KEY | AT91_RSTC_PERRST | AT91_RSTC_PROCRST

			.balign	32				@ align to cache line

			str	r2, [r0, #AT91_SDRAMC_TR]	@ disable SDRAM access
			str	r3, [r0, #AT91_SDRAMC_LPR]	@ power down SDRAM
			str	r4, [r1, #AT91_RSTC_CR]		@ reset processor

			b	.
