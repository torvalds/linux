/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Debugging macro include header
 *
 *  Copyright (C) 2009 ST-Ericsson
 */


#if CONFIG_UX500_DEBUG_UART > 2
#error Invalid Ux500 debug UART
#endif

/*
 * DEBUG_LL only works if only one SOC is built in.  We don't use #else below
 * in order to get "__UX500_UART redefined" warnings if more than one SOC is
 * built, so that there's some hint during the build that something is wrong.
 */

#ifdef CONFIG_UX500_SOC_DB8500
#define U8500_UART0_PHYS_BASE	(0x80120000)
#define U8500_UART1_PHYS_BASE	(0x80121000)
#define U8500_UART2_PHYS_BASE	(0x80007000)
#define __UX500_PHYS_UART(n)	U8500_UART##n##_PHYS_BASE
#endif

#if !defined(__UX500_PHYS_UART)
#error Unknown SOC
#endif

#define UX500_PHYS_UART(n)	__UX500_PHYS_UART(n)
#define UART_PHYS_BASE	UX500_PHYS_UART(CONFIG_UX500_DEBUG_UART)
#define UART_VIRT_BASE	(0xfff07000)

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =UART_PHYS_BASE		@ no, physical address
	ldr	\rv, =UART_VIRT_BASE		@ yes, virtual address
	.endm

#include <debug/pl01x.S>
