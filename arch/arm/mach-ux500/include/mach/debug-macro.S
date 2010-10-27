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

#define __UX500_UART(n)	UX500_UART##n##_BASE
#define UX500_UART(n)	__UX500_UART(n)
#define UART_BASE	UX500_UART(CONFIG_UX500_DEBUG_UART)

	.macro	addruart, rx, tmp
	mrc	p15, 0, \rx, c1, c0
	tst	\rx, #1					@ MMU enabled?
	ldreq	\rx, =UART_BASE				@ no, physical address
	ldrne	\rx, =IO_ADDRESS(UART_BASE)		@ yes, virtual address
	.endm

#include <asm/hardware/debug-pl01x.S>
