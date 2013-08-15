/*
 * Early serial debug output macro for Keystone SOCs
 *
 * Copyright 2013 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Based on RMKs low level debug code.
 *  Copyright (C) 1994-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/serial_reg.h>

#define UART_SHIFT 2
#if defined(CONFIG_DEBUG_KEYSTONE_UART0)
#define UART_PHYS		0x02530c00
#define UART_VIRT		0xfeb30c00
#elif defined(CONFIG_DEBUG_KEYSTONE_UART1)
#define UART_PHYS		0x02531000
#define UART_VIRT		0xfeb31000
#endif

	.macro	addruart, rp, rv, tmp
	ldr	\rv, =UART_VIRT			@ physical base address
	ldr	\rp, =UART_PHYS			@ virtual base address
	.endm

	.macro	senduart,rd,rx
	str	\rd, [\rx, #UART_TX << UART_SHIFT]
	.endm

	.macro	busyuart,rd,rx
1002:	ldr	\rd, [\rx, #UART_LSR << UART_SHIFT]
	and	\rd, \rd, #UART_LSR_TEMT | UART_LSR_THRE
	teq	\rd, #UART_LSR_TEMT | UART_LSR_THRE
	bne	1002b
	.endm

	.macro	waituart,rd,rx
	.endm
