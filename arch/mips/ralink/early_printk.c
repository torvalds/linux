/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 */

#include <linux/io.h>
#include <linux/serial_reg.h>

#include <asm/addrspace.h>

#ifdef CONFIG_SOC_RT288X
#define EARLY_UART_BASE         0x300c00
#else
#define EARLY_UART_BASE         0x10000c00
#endif

#define UART_REG_RX             0x00
#define UART_REG_TX             0x04
#define UART_REG_IER            0x08
#define UART_REG_IIR            0x0c
#define UART_REG_FCR            0x10
#define UART_REG_LCR            0x14
#define UART_REG_MCR            0x18
#define UART_REG_LSR            0x1c

static __iomem void *uart_membase = (__iomem void *) KSEG1ADDR(EARLY_UART_BASE);

static inline void uart_w32(u32 val, unsigned reg)
{
	__raw_writel(val, uart_membase + reg);
}

static inline u32 uart_r32(unsigned reg)
{
	return __raw_readl(uart_membase + reg);
}

void prom_putchar(unsigned char ch)
{
	while ((uart_r32(UART_REG_LSR) & UART_LSR_THRE) == 0)
		;
	uart_w32(ch, UART_REG_TX);
	while ((uart_r32(UART_REG_LSR) & UART_LSR_THRE) == 0)
		;
}
