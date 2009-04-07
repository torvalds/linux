/*
 * arch/arm/mach-pxa/include/mach/uncompress.h
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/serial_reg.h>
#include <mach/regs-uart.h>
#include <asm/mach-types.h>

#define __REG(x)       ((volatile unsigned long *)x)

static volatile unsigned long *UART = FFUART;

static inline void putc(char c)
{
	if (!(UART[UART_IER] & IER_UUE))
		return;
	while (!(UART[UART_LSR] & LSR_TDRQ))
		barrier();
	UART[UART_TX] = c;
}

/*
 * This does not append a newline
 */
static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
	if (machine_is_littleton() || machine_is_intelmote2()
			|| machine_is_csb726())
		UART = STUART;
}

/*
 * nothing to do
 */
#define arch_decomp_wdog()
