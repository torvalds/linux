/*
 * Debugging macro include header
 *
 *  Copyright (C) 2009 ST-Ericsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <mach/hardware.h>

#if CONFIG_UX500_DEBUG_UART > 2
#error Invalid Ux500 debug UART
#endif

/*
 * DEBUG_LL only works if only one SOC is built in.  We don't use #else below
 * in order to get "__UX500_UART redefined" warnings if more than one SOC is
 * built, so that there's some hint during the build that something is wrong.
 */

#ifdef CONFIG_UX500_SOC_DB5500
#define __UX500_UART(n)	U5500_UART##n##_BASE
#endif

#ifdef CONFIG_UX500_SOC_DB8500
#define __UX500_UART(n)	U8500_UART##n##_BASE
#endif

#ifndef __UX500_UART
#error Unknown SOC
#endif

#define UX500_UART(n)	__UX500_UART(n)
#define UART_BASE	UX500_UART(CONFIG_UX500_DEBUG_UART)

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =UART_BASE				@ no, physical address
	ldr	\rv, =IO_ADDRESS(UART_BASE)		@ yes, virtual address
	.endm

#include <asm/hardware/debug-pl01x.S>
