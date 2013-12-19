/*
 *  Atheros AR71xx/AR724x/AR913x specific interrupt handling
 *
 *  Copyright (C) 2010-2011 Jaiganesh Narayanan <jnarayanan@atheros.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15/2.6.31 BSP
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

static void (*ath79_ip2_handler)(void);
static void (*ath79_ip3_handler)(void);

static void ath79_misc_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	void __iomem *base = ath79_reset_base;
	u32 pending;

	pending = __raw_readl(base + AR71XX_RESET_REG_MISC_INT_STATUS) &
		  __raw_readl(base + AR71XX_RESET_REG_MISC_INT_ENABLE);

	if (!pending) {
		spurious_interrupt();
		return;
	}

	while (pending) {
		int bit = __ffs(pending);

		generic_handle_irq(ATH79_MISC_IRQ(bit));
		pending &= ~BIT(bit);
	}
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
	else if (soc_is_ar724x() ||
		 soc_is_ar933x() ||
		 soc_is_ar934x() ||
		 soc_is_qca955x())
		ath79_misc_irq_chip.irq_ack = ar724x_misc_irq_ack;
	else
		BUG();

	for (i = ATH79_MISC_IRQ_BASE;
	     i < ATH79_MISC_IRQ_BASE + ATH79_MISC_IRQ_COUNT; i++) {
		irq_set_chip_and_handler(i, &ath79_misc_irq_chip,
					 handle_level_irq);
	}

	irq_set_chained_handler(ATH79_CPU_IRQ(6), ath79_misc_irq_handler);
}

static void ar934x_ip2_irq_dispatch(unsigned int irq, struct irq_desc *desc)
{
	u32 status;

	disable_irq_nosync(irq);

	status = ath79_reset_rr(AR934X_RESET_REG_PCIE_WMAC_INT_STATUS);

	if (status & AR934X_PCIE_WMAC_INT_PCIE_ALL) {
		ath79_ddr_wb_flush(AR934X_DDR_REG_FLUSH_PCIE);
		generic_handle_irq(ATH79_IP2_IRQ(0));
	} else if (status & AR934X_PCIE_WMAC_INT_WMAC_ALL) {
		ath79_ddr_wb_flush(AR934X_DDR_REG_FLUSH_WMAC);
		generic_handle_irq(ATH79_IP2_IRQ(1));
	} else {
		spurious_interrupt();
	}

	enable_irq(irq);
}

static void ar934x_ip2_irq_init(void)
{
	int i;

	for (i = ATH79_IP2_IRQ_BASE;
	     i < ATH79_IP2_IRQ_BASE + ATH79_IP2_IRQ_COUNT; i++)
		irq_set_chip_and_handler(i, &dummy_irq_chip,
					 handle_level_irq);

	irq_set_chained_handler(ATH79_CPU_IRQ(2), ar934x_ip2_irq_dispatch);
}

static void qca955x_ip2_irq_dispatch(unsigned int irq, struct irq_desc *desc)
{
	u32 status;

	disable_irq_nosync(irq);

	status = ath79_reset_rr(QCA955X_RESET_REG_EXT_INT_STATUS);
	status &= QCA955X_EXT_INT_PCIE_RC1_ALL | QCA955X_EXT_INT_WMAC_ALL;

	if (status == 0) {
		spurious_interrupt();
		goto enable;
	}

	if (status & QCA955X_EXT_INT_PCIE_RC1_ALL) {
		/* TODO: flush DDR? */
		generic_handle_irq(ATH79_IP2_IRQ(0));
	}

	if (status & QCA955X_EXT_INT_WMAC_ALL) {
		/* TODO: flush DDR? */
		generic_handle_irq(ATH79_IP2_IRQ(1));
	}

enable:
	enable_irq(irq);
}

static void qca955x_ip3_irq_dispatch(unsigned int irq, struct irq_desc *desc)
{
	u32 status;

	disable_irq_nosync(irq);

	status = ath79_reset_rr(QCA955X_RESET_REG_EXT_INT_STATUS);
	status &= QCA955X_EXT_INT_PCIE_RC2_ALL |
		  QCA955X_EXT_INT_USB1 |
		  QCA955X_EXT_INT_USB2;

	if (status == 0) {
		spurious_interrupt();
		goto enable;
	}

	if (status & QCA955X_EXT_INT_USB1) {
		/* TODO: flush DDR? */
		generic_handle_irq(ATH79_IP3_IRQ(0));
	}

	if (status & QCA955X_EXT_INT_USB2) {
		/* TODO: flush DDR? */
		generic_handle_irq(ATH79_IP3_IRQ(1));
	}

	if (status & QCA955X_EXT_INT_PCIE_RC2_ALL) {
		/* TODO: flush DDR? */
		generic_handle_irq(ATH79_IP3_IRQ(2));
	}

enable:
	enable_irq(irq);
}

static void qca955x_irq_init(void)
{
	int i;

	for (i = ATH79_IP2_IRQ_BASE;
	     i < ATH79_IP2_IRQ_BASE + ATH79_IP2_IRQ_COUNT; i++)
		irq_set_chip_and_handler(i, &dummy_irq_chip,
					 handle_level_irq);

	irq_set_chained_handler(ATH79_CPU_IRQ(2), qca955x_ip2_irq_dispatch);

	for (i = ATH79_IP3_IRQ_BASE;
	     i < ATH79_IP3_IRQ_BASE + ATH79_IP3_IRQ_COUNT; i++)
		irq_set_chip_and_handler(i, &dummy_irq_chip,
					 handle_level_irq);

	irq_set_chained_handler(ATH79_CPU_IRQ(3), qca955x_ip3_irq_dispatch);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long pending;

	pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (pending & STATUSF_IP7)
		do_IRQ(ATH79_CPU_IRQ(7));

	else if (pending & STATUSF_IP2)
		ath79_ip2_handler();

	else if (pending & STATUSF_IP4)
		do_IRQ(ATH79_CPU_IRQ(4));

	else if (pending & STATUSF_IP5)
		do_IRQ(ATH79_CPU_IRQ(5));

	else if (pending & STATUSF_IP3)
		ath79_ip3_handler();

	else if (pending & STATUSF_IP6)
		do_IRQ(ATH79_CPU_IRQ(6));

	else
		spurious_interrupt();
}

/*
 * The IP2/IP3 lines are tied to a PCI/WMAC/USB device. Drivers for
 * these devices typically allocate coherent DMA memory, however the
 * DMA controller may still have some unsynchronized data in the FIFO.
 * Issue a flush in the handlers to ensure that the driver sees
 * the update.
 */

static void ath79_default_ip2_handler(void)
{
	do_IRQ(ATH79_CPU_IRQ(2));
}

static void ath79_default_ip3_handler(void)
{
	do_IRQ(ATH79_CPU_IRQ(3));
}

static void ar71xx_ip2_handler(void)
{
	ath79_ddr_wb_flush(AR71XX_DDR_REG_FLUSH_PCI);
	do_IRQ(ATH79_CPU_IRQ(2));
}

static void ar724x_ip2_handler(void)
{
	ath79_ddr_wb_flush(AR724X_DDR_REG_FLUSH_PCIE);
	do_IRQ(ATH79_CPU_IRQ(2));
}

static void ar913x_ip2_handler(void)
{
	ath79_ddr_wb_flush(AR913X_DDR_REG_FLUSH_WMAC);
	do_IRQ(ATH79_CPU_IRQ(2));
}

static void ar933x_ip2_handler(void)
{
	ath79_ddr_wb_flush(AR933X_DDR_REG_FLUSH_WMAC);
	do_IRQ(ATH79_CPU_IRQ(2));
}

static void ar71xx_ip3_handler(void)
{
	ath79_ddr_wb_flush(AR71XX_DDR_REG_FLUSH_USB);
	do_IRQ(ATH79_CPU_IRQ(3));
}

static void ar724x_ip3_handler(void)
{
	ath79_ddr_wb_flush(AR724X_DDR_REG_FLUSH_USB);
	do_IRQ(ATH79_CPU_IRQ(3));
}

static void ar913x_ip3_handler(void)
{
	ath79_ddr_wb_flush(AR913X_DDR_REG_FLUSH_USB);
	do_IRQ(ATH79_CPU_IRQ(3));
}

static void ar933x_ip3_handler(void)
{
	ath79_ddr_wb_flush(AR933X_DDR_REG_FLUSH_USB);
	do_IRQ(ATH79_CPU_IRQ(3));
}

static void ar934x_ip3_handler(void)
{
	ath79_ddr_wb_flush(AR934X_DDR_REG_FLUSH_USB);
	do_IRQ(ATH79_CPU_IRQ(3));
}

void __init arch_init_irq(void)
{
	if (soc_is_ar71xx()) {
		ath79_ip2_handler = ar71xx_ip2_handler;
		ath79_ip3_handler = ar71xx_ip3_handler;
	} else if (soc_is_ar724x()) {
		ath79_ip2_handler = ar724x_ip2_handler;
		ath79_ip3_handler = ar724x_ip3_handler;
	} else if (soc_is_ar913x()) {
		ath79_ip2_handler = ar913x_ip2_handler;
		ath79_ip3_handler = ar913x_ip3_handler;
	} else if (soc_is_ar933x()) {
		ath79_ip2_handler = ar933x_ip2_handler;
		ath79_ip3_handler = ar933x_ip3_handler;
	} else if (soc_is_ar934x()) {
		ath79_ip2_handler = ath79_default_ip2_handler;
		ath79_ip3_handler = ar934x_ip3_handler;
	} else if (soc_is_qca955x()) {
		ath79_ip2_handler = ath79_default_ip2_handler;
		ath79_ip3_handler = ath79_default_ip3_handler;
	} else {
		BUG();
	}

	cp0_perfcount_irq = ATH79_MISC_IRQ(5);
	mips_cpu_irq_init();
	ath79_misc_irq_init();

	if (soc_is_ar934x())
		ar934x_ip2_irq_init();
	else if (soc_is_qca955x())
		qca955x_irq_init();
}
