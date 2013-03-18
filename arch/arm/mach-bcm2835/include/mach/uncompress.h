/*
 * Copyright (C) 2010 Broadcom
 * Copyright (C) 2003 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/amba/serial.h>
#include <mach/bcm2835_soc.h>

#define UART0_BASE BCM2835_DEBUG_PHYS

#define BCM2835_UART_DR IOMEM(UART0_BASE + UART01x_DR)
#define BCM2835_UART_FR IOMEM(UART0_BASE + UART01x_FR)
#define BCM2835_UART_CR IOMEM(UART0_BASE + UART011_CR)

static inline void putc(int c)
{
	while (__raw_readl(BCM2835_UART_FR) & UART01x_FR_TXFF)
		barrier();

	__raw_writel(c, BCM2835_UART_DR);
}

static inline void flush(void)
{
	int fr;

	do {
		fr = __raw_readl(BCM2835_UART_FR);
		barrier();
	} while ((fr & (UART011_FR_TXFE | UART01x_FR_BUSY)) != UART011_FR_TXFE);
}

#define arch_decomp_setup()
