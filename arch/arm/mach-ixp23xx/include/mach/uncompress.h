/*
 * arch/arm/mach-ixp23xx/include/mach/uncompress.h
 *
 * Copyright (C) 2002-2004 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <mach/ixp23xx.h>
#include <linux/serial_reg.h>

#define UART_BASE	((volatile u32 *)IXP23XX_UART1_PHYS)

static inline void putc(char c)
{
	int j;

	for (j = 0; j < 0x1000; j++) {
		if (UART_BASE[UART_LSR] & UART_LSR_THRE)
			break;
		barrier();
	}

	UART_BASE[UART_TX] = c;
}

static inline void flush(void)
{
}

#define arch_decomp_setup()
#define arch_decomp_wdog()


#endif
