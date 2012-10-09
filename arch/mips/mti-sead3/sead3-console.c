/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/console.h>
#include <linux/serial_reg.h>
#include <linux/io.h>

#define SEAD_UART1_REGS_BASE    0xbf000800   /* ttyS1 = DB9 port */
#define SEAD_UART0_REGS_BASE    0xbf000900   /* ttyS0 = USB port   */
#define PORT(base_addr, offset) ((unsigned int __iomem *)(base_addr+(offset)*4))

static char console_port = 1;

static inline unsigned int serial_in(int offset, unsigned int base_addr)
{
	return __raw_readl(PORT(base_addr, offset)) & 0xff;
}

static inline void serial_out(int offset, int value, unsigned int base_addr)
{
	__raw_writel(value, PORT(base_addr, offset));
}

void __init prom_init_early_console(char port)
{
	console_port = port;
}

int prom_putchar(char c)
{
	unsigned int base_addr;

	base_addr = console_port ? SEAD_UART1_REGS_BASE : SEAD_UART0_REGS_BASE;

	while ((serial_in(UART_LSR, base_addr) & UART_LSR_THRE) == 0)
		;

	serial_out(UART_TX, c, base_addr);

	return 1;
}
