// SPDX-License-Identifier: GPL-2.0
/*
 * 16550 compatible uart based serial debug support for zboot
 */

#include <linux/types.h>
#include <linux/serial_reg.h>

#include <asm/addrspace.h>

#if defined(CONFIG_MACH_LOONGSON64) || defined(CONFIG_MIPS_MALTA)
#define UART_BASE 0x1fd003f8
#define PORT(offset) (CKSEG1ADDR(UART_BASE) + (offset))
#endif

#ifdef CONFIG_AR7
#include <ar7.h>
#define PORT(offset) (CKSEG1ADDR(AR7_REGS_UART0) + (4 * offset))
#endif

#ifdef CONFIG_MACH_INGENIC
#define INGENIC_UART_BASE_ADDR	(0x10030000 + 0x1000 * CONFIG_ZBOOT_INGENIC_UART)
#define PORT(offset) (CKSEG1ADDR(INGENIC_UART_BASE_ADDR) + (4 * offset))
#endif

#ifndef IOTYPE
#define IOTYPE char
#endif

#ifndef PORT
#error please define the serial port address for your own machine
#endif

static inline unsigned int serial_in(int offset)
{
	return *((volatile IOTYPE *)PORT(offset)) & 0xFF;
}

static inline void serial_out(int offset, int value)
{
	*((volatile IOTYPE *)PORT(offset)) = value & 0xFF;
}

void putc(char c)
{
	int timeout = 1000000;

	while (((serial_in(UART_LSR) & UART_LSR_THRE) == 0) && (timeout-- > 0))
		;

	serial_out(UART_TX, c);
}
