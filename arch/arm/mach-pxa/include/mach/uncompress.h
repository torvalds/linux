/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-pxa/include/mach/uncompress.h
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 */

#include <linux/serial_reg.h>
#include <asm/mach-types.h>

#define FFUART_BASE	(0x40100000)
#define BTUART_BASE	(0x40200000)
#define STUART_BASE	(0x40700000)

unsigned long uart_base;
unsigned int uart_shift;
unsigned int uart_is_pxa;

static inline unsigned char uart_read(int offset)
{
	return *(volatile unsigned char *)(uart_base + (offset << uart_shift));
}

static inline void uart_write(unsigned char val, int offset)
{
	*(volatile unsigned char *)(uart_base + (offset << uart_shift)) = val;
}

static inline int uart_is_enabled(void)
{
	/* assume enabled by default for non-PXA uarts */
	return uart_is_pxa ? uart_read(UART_IER) & UART_IER_UUE : 1;
}

static inline void putc(char c)
{
	if (!uart_is_enabled())
		return;

	while (!(uart_read(UART_LSR) & UART_LSR_THRE))
		barrier();

	uart_write(c, UART_TX);
}

/*
 * This does not append a newline
 */
static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
	/* initialize to default */
	uart_base = FFUART_BASE;
	uart_shift = 2;
	uart_is_pxa = 1;

	if (machine_is_littleton() || machine_is_intelmote2()
	    || machine_is_csb726() || machine_is_stargate2()
	    || machine_is_cm_x300() || machine_is_balloon3())
		uart_base = STUART_BASE;

	if (machine_is_arcom_zeus()) {
		uart_base = 0x10000000;	/* nCS4 */
		uart_shift = 1;
		uart_is_pxa = 0;
	}
}
