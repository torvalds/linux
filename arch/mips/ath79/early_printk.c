/*
 *  Atheros AR71XX/AR724X/AR913X SoC early printk support
 *
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/serial_reg.h>
#include <asm/addrspace.h>

#include <asm/mach-ath79/ar71xx_regs.h>

static inline void prom_wait_thre(void __iomem *base)
{
	u32 lsr;

	do {
		lsr = __raw_readl(base + UART_LSR * 4);
		if (lsr & UART_LSR_THRE)
			break;
	} while (1);
}

void prom_putchar(unsigned char ch)
{
	void __iomem *base = (void __iomem *)(KSEG1ADDR(AR71XX_UART_BASE));

	prom_wait_thre(base);
	__raw_writel(ch, base + UART_TX * 4);
	prom_wait_thre(base);
}
