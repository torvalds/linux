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
#define EARLY_UART_BASE		0x300c00
#define CHIPID_BASE		0x300004
#elif defined(CONFIG_SOC_MT7621)
#define EARLY_UART_BASE		0x1E000c00
#define CHIPID_BASE		0x1E000004
#else
#define EARLY_UART_BASE		0x10000c00
#define CHIPID_BASE		0x10000004
#endif

#define MT7628_CHIP_NAME1	0x20203832

#define UART_REG_TX		0x04
#define UART_REG_LCR		0x0c
#define UART_REG_LSR		0x14
#define UART_REG_LSR_RT2880	0x1c

static __iomem void *uart_membase = (__iomem void *) KSEG1ADDR(EARLY_UART_BASE);
static __iomem void *chipid_membase = (__iomem void *) KSEG1ADDR(CHIPID_BASE);
static int init_complete;

static inline void uart_w32(u32 val, unsigned reg)
{
	__raw_writel(val, uart_membase + reg);
}

static inline u32 uart_r32(unsigned reg)
{
	return __raw_readl(uart_membase + reg);
}

static inline int soc_is_mt7628(void)
{
	return IS_ENABLED(CONFIG_SOC_MT7620) &&
		(__raw_readl(chipid_membase) == MT7628_CHIP_NAME1);
}

static void find_uart_base(void)
{
	int i;

	if (!soc_is_mt7628())
		return;

	for (i = 0; i < 3; i++) {
		u32 reg = uart_r32(UART_REG_LCR + (0x100 * i));

		if (!reg)
			continue;

		uart_membase = (__iomem void *) KSEG1ADDR(EARLY_UART_BASE +
							  (0x100 * i));
		break;
	}
}

void prom_putchar(unsigned char ch)
{
	if (!init_complete) {
		find_uart_base();
		init_complete = 1;
	}

	if (IS_ENABLED(CONFIG_SOC_MT7621) || soc_is_mt7628()) {
		uart_w32(ch, UART_TX);
		while ((uart_r32(UART_REG_LSR) & UART_LSR_THRE) == 0)
			;
	} else {
		while ((uart_r32(UART_REG_LSR_RT2880) & UART_LSR_THRE) == 0)
			;
		uart_w32(ch, UART_REG_TX);
		while ((uart_r32(UART_REG_LSR_RT2880) & UART_LSR_THRE) == 0)
			;
	}
}
