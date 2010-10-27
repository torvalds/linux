/*
 *  Copyright (C) 2009 ST-Ericsson
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <asm/setup.h>
#include <linux/io.h>
#include <mach/hardware.h>

#define U8500_UART_DR		0x80007000
#define U8500_UART_LCRH		0x8000702c
#define U8500_UART_CR		0x80007030
#define U8500_UART_FR		0x80007018

static void putc(const char c)
{
	/* Do nothing if the UART is not enabled. */
	if (!(__raw_readb(U8500_UART_CR) & 0x1))
		return;

	if (c == '\n')
		putc('\r');

	while (__raw_readb(U8500_UART_FR) & (1 << 5))
		barrier();
	__raw_writeb(c, U8500_UART_DR);
}

static void flush(void)
{
	if (!(__raw_readb(U8500_UART_CR) & 0x1))
		return;
	while (__raw_readb(U8500_UART_FR) & (1 << 3))
		barrier();
}

static inline void arch_decomp_setup(void)
{
}

#define arch_decomp_wdog() /* nothing to do here */

#endif /* __ASM_ARCH_UNCOMPRESS_H */
