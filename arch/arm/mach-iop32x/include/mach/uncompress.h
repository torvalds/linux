/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/mach-iop32x/include/mach/uncompress.h
 */

#include <asm/types.h>
#include <asm/mach-types.h>
#include <linux/serial_reg.h>

#define uart_base ((volatile u8 *)0xfe800000)

#define TX_DONE		(UART_LSR_TEMT | UART_LSR_THRE)

static inline void putc(char c)
{
	while ((uart_base[UART_LSR] & TX_DONE) != TX_DONE)
		barrier();
	uart_base[UART_TX] = c;
}

static inline void flush(void)
{
}

#define arch_decomp_setup() do { } while (0)
