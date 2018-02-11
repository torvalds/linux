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

#if defined(CONFIG_MACH_JZ4740) || defined(CONFIG_MACH_JZ4780)
#include <asm/mach-jz4740/base.h>
#define PORT(offset) (CKSEG1ADDR(JZ4740_UART0_BASE_ADDR) + (4 * offset))
#endif

#ifdef CONFIG_CPU_XLR
#define UART0_BASE  0x1EF14000
#define PORT(offset) (CKSEG1ADDR(UART0_BASE) + (4 * offset))
#define IOTYPE unsigned int
#endif

#ifdef CONFIG_CPU_XLP
#define UART0_BASE  0x18030100
#define PORT(offset) (CKSEG1ADDR(UART0_BASE) + (4 * offset))
#define IOTYPE unsigned int
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
