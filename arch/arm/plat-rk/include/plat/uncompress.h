#ifndef __PLAT_UNCOMPRESS_H
#define __PLAT_UNCOMPRESS_H

#include <linux/serial_reg.h>
#include <mach/io.h>

#ifdef DEBUG_UART_PHYS
static volatile u32 *UART = (u32 *)DEBUG_UART_PHYS;

static void putc(int c)
{
	while (!(UART[UART_LSR] & UART_LSR_THRE))
		barrier();
	UART[UART_TX] = c;
}
#else
static inline void putc(int c)
{
}
#endif

static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
}

static inline void arch_decomp_wdog(void)
{
}

#endif
