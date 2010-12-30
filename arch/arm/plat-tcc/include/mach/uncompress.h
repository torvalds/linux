/*
 * Copyright (C) 2009 Hans J. Koch <hjk@linutronix.de>
 *
 * This file is licensed under the terms of the GPL version 2.
 */

#include <linux/serial_reg.h>
#include <linux/types.h>

#include <mach/tcc8k-regs.h>

unsigned int system_rev;

#define ID_MASK			0x7fff

static void putc(int c)
{
	u32 *uart_lsr = (u32 *)(UART_BASE_PHYS + (UART_LSR << 2));
	u32 *uart_tx = (u32 *)(UART_BASE_PHYS + (UART_TX << 2));

	while (!(*uart_lsr & UART_LSR_THRE))
		barrier();
	*uart_tx = c;
}

static inline void flush(void)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
