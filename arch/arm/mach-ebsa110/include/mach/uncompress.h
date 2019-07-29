/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/mach-ebsa110/include/mach/uncompress.h
 *
 *  Copyright (C) 1996,1997,1998 Russell King
 */

#include <linux/serial_reg.h>

#define SERIAL_BASE	((unsigned char *)0xf0000be0)

/*
 * This does not append a newline
 */
static inline void putc(int c)
{
	unsigned char v, *base = SERIAL_BASE;

	do {
		v = base[UART_LSR << 2];
		barrier();
	} while (!(v & UART_LSR_THRE));

	base[UART_TX << 2] = c;
}

static inline void flush(void)
{
	unsigned char v, *base = SERIAL_BASE;

	do {
		v = base[UART_LSR << 2];
		barrier();
	} while ((v & (UART_LSR_TEMT|UART_LSR_THRE)) !=
		 (UART_LSR_TEMT|UART_LSR_THRE));
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
