/*
 * arch/arm/mach-w90x900/include/mach/uncompress.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/uncompress.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

/* Defines for UART registers */

#include <mach/regs-serial.h>
#include <mach/map.h>
#include <linux/serial_reg.h>

#define TX_DONE	(UART_LSR_TEMT | UART_LSR_THRE)
static volatile u32 * const uart_base = (u32 *)UART0_PA;

static inline void putc(int ch)
{
	/* Check THRE and TEMT bits before we transmit the character.
	 */
	while ((uart_base[UART_LSR] & TX_DONE) != TX_DONE)
		barrier();

	*uart_base = ch;
}

static inline void flush(void)
{
}

static void arch_decomp_setup(void)
{
}

#endif/* __ASM_W90X900_UNCOMPRESS_H */
