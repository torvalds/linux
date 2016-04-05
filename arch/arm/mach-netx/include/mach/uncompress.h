/*
 * arch/arm/mach-netx/include/mach/uncompress.h
 *
 * Copyright (C) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
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

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader.  We search for the first enabled
 * port in the most probable order.  If you didn't setup a port in
 * your bootloader then nothing will appear (which might be desired).
 *
 * This does not append a newline
 */

#define REG(x) (*(volatile unsigned long *)(x))

#define UART1_BASE 0x100a00
#define UART2_BASE 0x100a80

#define UART_DR 0x0

#define UART_CR 0x14
#define CR_UART_EN (1<<0)

#define UART_FR 0x18
#define FR_BUSY (1<<3)
#define FR_TXFF (1<<5)

static inline void putc(char c)
{
	unsigned long base;

	if (REG(UART1_BASE + UART_CR) & CR_UART_EN)
		base = UART1_BASE;
	else if (REG(UART2_BASE + UART_CR) & CR_UART_EN)
		base = UART2_BASE;
	else
		return;

	while (REG(base + UART_FR) & FR_TXFF);
	REG(base + UART_DR) = c;
}

static inline void flush(void)
{
	unsigned long base;

	if (REG(UART1_BASE + UART_CR) & CR_UART_EN)
		base = UART1_BASE;
	else if (REG(UART2_BASE + UART_CR) & CR_UART_EN)
		base = UART2_BASE;
	else
		return;

	while (REG(base + UART_FR) & FR_BUSY);
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
