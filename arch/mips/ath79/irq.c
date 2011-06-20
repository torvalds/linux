/*
 *  Atheros AR71xx/AR724x/AR913x specific interrupt handling
 *
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15 BSP
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "common.h"

static unsigned int ath79_ip2_flush_reg;
static unsigned int ath79_ip3_flush_reg;

static void ath79_misc_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	void __iomem *base = ath79_reset_base;
	u32 pending;

	pending = __raw_readl(base + AR71XX_RESET_REG_MISC_INT_STATUS) &
		  __raw_readl(base + AR71XX_RESET_REG_MISC_INT_ENABLE);

	if (pending & MISC_INT_UART)
		generic_handle_irq(ATH79_MISC_IRQ_UART);

	else if (pending & MISC_INT_DMA)
		generic_handle_irq(ATH79_MISC_IRQ_DMA);

	else if (pending & MISC_INT_PERFC)
		generic_handle_irq(ATH79_MISC_IRQ_PERFC);

	else if (pending & MISC_INT_TIMER)
		generic_handle_irq(ATH79_MISC_IRQ_TIMER);

	else if (pending & MISC_INT_TIMER2)
		generic_handle_irq(ATH79_MISC_IRQ_TIMER2);

	else if (pending & MISC_INT_TIMER3)
		generic_handle_irq(ATH79_MISC_IRQ_TIMER3);

	else if (pending & MISC_INT_TIMER4)
		generic_handle_irq(ATH79_MISC_IRQ_TIMER4);

	else if (pending & MISC_INT_OHCI)
		generic_handle_irq(ATH79_MISC_IRQ_OHCI);

	else if (pending & MISC_INT_ERROR)
		generic_handle_irq(ATH79_MISC_IRQ_ERROR);

	else if (pending & MISC_INT_GPIO)
		generic_handle_irq(ATH79_MISC_IRQ_GPIO);

	else if (pending & MISC_INT_WDOG)
		generic_handle_irq(ATH79_MISC_IRQ_WDOG);

	else if (pending & MISC_INT_ETHSW)
		generic_handle_irq(ATH79_MISC_IRQ_ETHSW);

	else
		spurious_interrupt();
}

static void ar71xx_misc_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq - ATH79_MISC_IRQ_BASE;
	void __iomem *base = ath79_reset_base;
	u32 t;

	t = __raw_readl(base + AR71XX_RESET_REG_MISC_INT_ENABLE);
	__raw_writel(t | (1 << irq), base + AR71XX_RESET_REG_MISC_INT_ENABLE);

	/* flush write */
	__raw_readl(base + AR71XX_RESET_REG_MISC_INT_ENABLE);
}

static void ar71xx_misc_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq - ATH79_MISC_IRQ_BASE;
	void __iomem *base = ath79_reset_base;
	u32 t;

	t = __raw_readl(base + AR71XX_RESET_REG_MISC_INT_ENABLE);
	__raw_writel(t & ~(1 << irq), base + AR71XX_RESET_REG_MISC_INT_ENABLE);

	/* flush write */
	__raw_readl(base + AR71XX_RESET_REG_MISC_INT_ENABLE);
}

static void ar724x_misc_irq_ack(struct irq_data *d)
{
	unsigned int irq = d->irq - ATH79_MISC_IRQ_BASE;
	void __iomem *base = ath79_reset_base;
	u32 t;

	t = __raw_readl(base + AR71XX_RESET_REG_MISC_INT_STATUS);
	__raw_writel(t & ~(1 << irq), base + AR71XX_RESET_REG_MISC_INT_STATUS);

	/* flush write */
	__raw_readl(base + AR71XX_RESET_REG_MISC_INT_STATUS);
}

static struct irq_chip ath79_misc_irq_chip = {
	.name		= "MISC",
	.irq_unmask	= ar71xx_misc_irq_unmask,
	.irq_mask	= ar71xx_misc_irq_mask,
};

static void __init ath79_misc_irq_init(void)
{
	void __iomem *base = ath79_reset_base;
	int i;

	__raw_writel(0, base + AR71XX_RESET_REG_MISC_INT_ENABLE);
	__raw_writel(0, base + AR71XX_RESET_REG_MISC_INT_STATUS);

	if (soc_is_ar71xx() || soc_is_ar913x())
		ath79_misc_irq_chip.irq_mask_ack = ar71xx_misc_irq_mask;
	else if (soc_is_ar724x() || soc_is_ar933x())
		ath79_misc_irq_chip.irq_ack = ar724x_misc_irq_ack;
	else
		BUG();

	for (i = ATH79_MISC_IRQ_BASE;
	     i < ATH79_MISC_IRQ_BASE + ATH79_MISC_IRQ_COUNT; i++) {
		irq_set_chip_and_handler(i, &ath79_misc_irq_chip,
					 handle_level_irq);
	}

	irq_set_chained_handler(ATH79_CPU_IRQ_MISC, ath79_misc_irq_handler);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long pending;

	pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (pending & STATUSF_IP7)
		do_IRQ(ATH79_CPU_IRQ_TIMER);

	else if (pending & STATUSF_IP2) {
		ath79_ddr_wb_flush(ath79_ip2_flush_reg);
		do_IRQ(ATH79_CPU_IRQ_IP2);
	}

	else if (pending & STATUSF_IP4)
		do_IRQ(ATH79_CPU_IRQ_GE0);

	else if (pending & STATUSF_IP5)
		do_IRQ(ATH79_CPU_IRQ_GE1);

	else if (pending & STATUSF_IP3) {
		ath79_ddr_wb_flush(ath79_ip3_flush_reg);
		do_IRQ(ATH79_CPU_IRQ_USB);
	}

	else if (pending & STATUSF_IP6)
		do_IRQ(ATH79_CPU_IRQ_MISC);

	else
		spurious_interrupt();
}

void __init arch_init_irq(void)
{
	if (soc_is_ar71xx()) {
		ath79_ip2_flush_reg = AR71XX_DDR_REG_FLUSH_PCI;
		ath79_ip3_flush_reg = AR71XX_DDR_REG_FLUSH_USB;
	} else if (soc_is_ar724x()) {
		ath79_ip2_flush_reg = AR724X_DDR_REG_FLUSH_PCIE;
		ath79_ip3_flush_reg = AR724X_DDR_REG_FLUSH_USB;
	} else if (soc_is_ar913x()) {
		ath79_ip2_flush_reg = AR913X_DDR_REG_FLUSH_WMAC;
		ath79_ip3_flush_reg = AR913X_DDR_REG_FLUSH_USB;
	} else if (soc_is_ar933x()) {
		ath79_ip2_flush_reg = AR933X_DDR_REG_FLUSH_WMAC;
		ath79_ip3_flush_reg = AR933X_DDR_REG_FLUSH_USB;
	} else
		BUG();

	cp0_perfcount_irq = ATH79_MISC_IRQ_PERFC;
	mips_cpu_irq_init();
	ath79_misc_irq_init();
}
