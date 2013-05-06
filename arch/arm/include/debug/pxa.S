/*
 * Early serial output macro for Marvell PXA/MMP SoC
 *
 * Copyright (C) 1994-1999 Russell King
 * Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * Copyright (C) 2013 Haojian Zhuang
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#if defined(CONFIG_DEBUG_PXA_UART1)
#define PXA_UART_REG_PHYS_BASE	0x40100000
#define PXA_UART_REG_VIRT_BASE	0xf2100000
#elif defined(CONFIG_DEBUG_MMP_UART2)
#define PXA_UART_REG_PHYS_BASE	0xd4017000
#define PXA_UART_REG_VIRT_BASE	0xfe017000
#elif defined(CONFIG_DEBUG_MMP_UART3)
#define PXA_UART_REG_PHYS_BASE	0xd4018000
#define PXA_UART_REG_VIRT_BASE	0xfe018000
#else
#error "Select uart for DEBUG_LL"
#endif

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =PXA_UART_REG_PHYS_BASE
	ldr	\rv, =PXA_UART_REG_VIRT_BASE
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
