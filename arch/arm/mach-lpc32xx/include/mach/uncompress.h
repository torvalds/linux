/*
 * arch/arm/mach-lpc32xx/include/mach/uncompress.h
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
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

#ifndef __ASM_ARM_ARCH_UNCOMPRESS_H
#define __ASM_ARM_ARCH_UNCOMPRESS_H

#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/platform.h>

/*
 * Uncompress output is hardcoded to standard UART 5
 */

#define UART_FIFO_CTL_TX_RESET	(1 << 2)
#define UART_STATUS_TX_MT	(1 << 6)

#define _UARTREG(x)		(void __iomem *)(LPC32XX_UART5_BASE + (x))

#define LPC32XX_UART_DLLFIFO_O	0x00
#define LPC32XX_UART_IIRFCR_O	0x08
#define LPC32XX_UART_LSR_O	0x14

static inline void putc(int ch)
{
	/* Wait for transmit FIFO to empty */
	while ((__raw_readl(_UARTREG(LPC32XX_UART_LSR_O)) &
		UART_STATUS_TX_MT) == 0)
		;

	__raw_writel((u32) ch, _UARTREG(LPC32XX_UART_DLLFIFO_O));
}

static inline void flush(void)
{
	__raw_writel(__raw_readl(_UARTREG(LPC32XX_UART_IIRFCR_O)) |
		UART_FIFO_CTL_TX_RESET, _UARTREG(LPC32XX_UART_IIRFCR_O));
}

/* NULL functions; we don't presently need them */
#define arch_decomp_setup()
#define arch_decomp_wdog()

#endif
