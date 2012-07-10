/*
 * Marvell Armada SoC kernel uncompression UART routines
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <mach/armada-370-xp.h>

#define UART_THR ((volatile unsigned char *)(ARMADA_370_XP_REGS_PHYS_BASE\
								+ 0x12000))
#define UART_LSR ((volatile unsigned char *)(ARMADA_370_XP_REGS_PHYS_BASE\
								+ 0x12014))

#define LSR_THRE	0x20

static void putc(const char c)
{
	int i;

	for (i = 0; i < 0x1000; i++) {
		/* Transmit fifo not full? */
		if (*UART_LSR & LSR_THRE)
			break;
	}

	*UART_THR = c;
}

static void flush(void)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
