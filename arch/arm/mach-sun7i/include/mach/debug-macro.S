/* arch/arm/mach-sun7i/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 * Copyright (c) Allwinner.  All rights reserved.
 * Benn Huang (benn@allwinnertech.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/platform.h>
#include <mach/uart.h>

#ifdef CONFIG_DEBUG_AW_UART0
#define UART_PADDR	SW_PA_UART0_IO_BASE
#define UART_VADDR	SW_VA_UART0_IO_BASE
#else
#error "Please select a uart port"
#endif

	.macro	addruart, rp, rv, tmp
	ldr	\rv, =UART_VADDR
	ldr	\rp, =UART_PADDR
	.endm


        .macro  senduart, rd, rx
	str	\rd, [\rx, #AW_UART_THR]
        .endm

        .macro  busyuart, rd, rx
        .endm

        .macro  waituart, rd, rx
1002:	ldr	\rd, [\rx, #AW_UART_USR]
	tst	\rd, #0x2
	beq	1002b
        .endm



