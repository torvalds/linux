/*
 *	linux/arch/arm/include/debug/nspire.S
 *
 *	Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#ifdef CONFIG_DEBUG_NSPIRE_CX_UART
#define NSPIRE_EARLY_UART_PHYS_BASE	   0x90020000
#define NSPIRE_EARLY_UART_VIRT_BASE	   0xfee20000

.macro	addruart, rp, rv, tmp
	ldr \rp, =(NSPIRE_EARLY_UART_PHYS_BASE)		@ physical base address
	ldr \rv, =(NSPIRE_EARLY_UART_VIRT_BASE)		@ virtual base address
.endm

#include <asm/hardware/debug-pl01x.S>
#endif

#ifdef CONFIG_DEBUG_NSPIRE_CLASSIC_UART
#include <asm/hardware/debug-8250.S>
#endif
