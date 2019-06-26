/*
 *  Atheros AR7XXX/AR9XXX SoC early printk support
 *
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/serial_reg.h>
#include <asm/addrspace.h>
#include <asm/setup.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include <asm/mach-ath79/ar933x_uart.h>

static void (*_prom_putchar)(char);

static inline void prom_putchar_wait(void __iomem *reg, u32 mask, u32 val)
{
	u32 t;

	do {
		t = __raw_readl(reg);
		if ((t & mask) == val)
			break;
	} while (1);
}

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static void prom_putchar_ar71xx(char ch)
{
	void __iomem *base = (void __iomem *)(KSEG1ADDR(AR71XX_UART_BASE));

	prom_putchar_wait(base + UART_LSR * 4, BOTH_EMPTY, BOTH_EMPTY);
	__raw_writel((unsigned char)ch, base + UART_TX * 4);
	prom_putchar_wait(base + UART_LSR * 4, BOTH_EMPTY, BOTH_EMPTY);
}

static void prom_putchar_ar933x(char ch)
{
	void __iomem *base = (void __iomem *)(KSEG1ADDR(AR933X_UART_BASE));

	prom_putchar_wait(base + AR933X_UART_DATA_REG, AR933X_UART_DATA_TX_CSR,
			  AR933X_UART_DATA_TX_CSR);
	__raw_writel(AR933X_UART_DATA_TX_CSR | (unsigned char)ch,
		     base + AR933X_UART_DATA_REG);
	prom_putchar_wait(base + AR933X_UART_DATA_REG, AR933X_UART_DATA_TX_CSR,
			  AR933X_UART_DATA_TX_CSR);
}

static void prom_putchar_dummy(char ch)
{
	/* nothing to do */
}

static void prom_enable_uart(u32 id)
{
	void __iomem *gpio_base;
	u32 uart_en;
	u32 t;

	switch (id) {
	case REV_ID_MAJOR_AR71XX:
		uart_en = AR71XX_GPIO_FUNC_UART_EN;
		break;

	case REV_ID_MAJOR_AR7240:
	case REV_ID_MAJOR_AR7241:
	case REV_ID_MAJOR_AR7242:
		uart_en = AR724X_GPIO_FUNC_UART_EN;
		break;

	case REV_ID_MAJOR_AR913X:
		uart_en = AR913X_GPIO_FUNC_UART_EN;
		break;

	case REV_ID_MAJOR_AR9330:
	case REV_ID_MAJOR_AR9331:
		uart_en = AR933X_GPIO_FUNC_UART_EN;
		break;

	case REV_ID_MAJOR_AR9341:
	case REV_ID_MAJOR_AR9342:
	case REV_ID_MAJOR_AR9344:
		/* TODO */
	default:
		return;
	}

	gpio_base = (void __iomem *)KSEG1ADDR(AR71XX_GPIO_BASE);
	t = __raw_readl(gpio_base + AR71XX_GPIO_REG_FUNC);
	t |= uart_en;
	__raw_writel(t, gpio_base + AR71XX_GPIO_REG_FUNC);
}

static void prom_putchar_init(void)
{
	void __iomem *base;
	u32 id;

	base = (void __iomem *)(KSEG1ADDR(AR71XX_RESET_BASE));
	id = __raw_readl(base + AR71XX_RESET_REG_REV_ID);
	id &= REV_ID_MAJOR_MASK;

	switch (id) {
	case REV_ID_MAJOR_AR71XX:
	case REV_ID_MAJOR_AR7240:
	case REV_ID_MAJOR_AR7241:
	case REV_ID_MAJOR_AR7242:
	case REV_ID_MAJOR_AR913X:
	case REV_ID_MAJOR_AR9341:
	case REV_ID_MAJOR_AR9342:
	case REV_ID_MAJOR_AR9344:
	case REV_ID_MAJOR_QCA9533:
	case REV_ID_MAJOR_QCA9533_V2:
	case REV_ID_MAJOR_QCA9556:
	case REV_ID_MAJOR_QCA9558:
	case REV_ID_MAJOR_TP9343:
	case REV_ID_MAJOR_QCA956X:
		_prom_putchar = prom_putchar_ar71xx;
		break;

	case REV_ID_MAJOR_AR9330:
	case REV_ID_MAJOR_AR9331:
		_prom_putchar = prom_putchar_ar933x;
		break;

	default:
		_prom_putchar = prom_putchar_dummy;
		return;
	}

	prom_enable_uart(id);
}

void prom_putchar(char ch)
{
	if (!_prom_putchar)
		prom_putchar_init();

	_prom_putchar(ch);
}
