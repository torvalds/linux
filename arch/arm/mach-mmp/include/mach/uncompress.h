/*
 * arch/arm/mach-mmp/include/mach/uncompress.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/serial_reg.h>
#include <mach/addr-map.h>
#include <asm/mach-types.h>

#define UART1_BASE	(APB_PHYS_BASE + 0x36000)
#define UART2_BASE	(APB_PHYS_BASE + 0x17000)
#define UART3_BASE	(APB_PHYS_BASE + 0x18000)

static volatile unsigned long *UART = (unsigned long *)UART2_BASE;

static inline void putc(char c)
{
	/* UART enabled? */
	if (!(UART[UART_IER] & UART_IER_UUE))
		return;

	while (!(UART[UART_LSR] & UART_LSR_THRE))
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
	if (machine_is_avengers_lite())
		UART = (unsigned long *)UART3_BASE;
}

/*
 * nothing to do
 */

#define arch_decomp_wdog()
