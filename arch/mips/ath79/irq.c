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
#include <linux/irqchip.h>
#include <linux/of_irq.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "common.h"
#include "machtypes.h"

static void ath79_misc_irq_handler(struct irq_desc *desc)
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

static void ar934x_ip2_irq_dispatch(struct irq_desc *desc)
{
	u32 status;

	status = ath79_reset_rr(AR934X_RESET_REG_PCIE_WMAC_INT_STATUS);

	if (status & AR934X_PCIE_WMAC_INT_PCIE_ALL) {
		ath79_ddr_wb_flush(3);
		generic_handle_irq(ATH79_IP2_IRQ(0));
	} else if (status & AR934X_PCIE_WMAC_INT_WMAC_ALL) {
		ath79_ddr_wb_flush(4);
		generic_handle_irq(ATH79_IP2_IRQ(1));
	} else {
		spurious_interrupt();
	}
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

static void qca955x_ip2_irq_dispatch(struct irq_desc *desc)
{
	u32 status;

	status = ath79_reset_rr(QCA955X_RESET_REG_EXT_INT_STATUS);
	status &= QCA955X_EXT_INT_PCIE_RC1_ALL | QCA955X_EXT_INT_WMAC_ALL;

	if (status == 0) {
		spurious_interrupt();
		return;
	}

	if (status & QCA955X_EXT_INT_PCIE_RC1_ALL) {
		/* TODO: flush DDR? */
		generic_handle_irq(ATH79_IP2_IRQ(0));
	}

	if (status & QCA955X_EXT_INT_WMAC_ALL) {
		/* TODO: flush DDR? */
		generic_handle_irq(ATH79_IP2_IRQ(1));
	}
}

static void qca955x_ip3_irq_dispatch(struct irq_desc *desc)
{
	u32 status;

	status = ath79_reset_rr(QCA955X_RESET_REG_EXT_INT_STATUS);
	status &= QCA955X_EXT_INT_PCIE_RC2_ALL |
		  QCA955X_EXT_INT_USB1 |
		  QCA955X_EXT_INT_USB2;

	if (status == 0) {
		spurious_interrupt();
		return;
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

/*
 * The IP2/IP3 lines are tied to a PCI/WMAC/USB device. Drivers for
 * these devices typically allocate coherent DMA memory, however the
 * DMA controller may still have some unsynchronized data in the FIFO.
 * Issue a flush in the handlers to ensure that the driver sees
 * the update.
 *
 * This array map the interrupt lines to the DDR write buffer channels.
 */

static unsigned irq_wb_chan[8] = {
	-1, -1, -1, -1, -1, -1, -1, -1,
};

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long pending;
	int irq;

	pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (!pending) {
		spurious_interrupt();
		return;
	}

	pending >>= CAUSEB_IP;
	while (pending) {
		irq = fls(pending) - 1;
		if (irq < ARRAY_SIZE(irq_wb_chan) && irq_wb_chan[irq] != -1)
			ath79_ddr_wb_flush(irq_wb_chan[irq]);
		do_IRQ(MIPS_CPU_IRQ_BASE + irq);
		pending &= ~BIT(irq);
	}
}

#ifdef CONFIG_IRQCHIP
static int misc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &ath79_misc_irq_chip, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops misc_irq_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = misc_map,
};

static int __init ath79_misc_intc_of_init(
	struct device_node *node, struct device_node *parent)
{
	void __iomem *base = ath79_reset_base;
	struct irq_domain *domain;
	int irq;

	irq = irq_of_parse_and_map(node, 0);
	if (!irq)
		panic("Failed to get MISC IRQ");

	domain = irq_domain_add_legacy(node, ATH79_MISC_IRQ_COUNT,
			ATH79_MISC_IRQ_BASE, 0, &misc_irq_domain_ops, NULL);
	if (!domain)
		panic("Failed to add MISC irqdomain");

	/* Disable and clear all interrupts */
	__raw_writel(0, base + AR71XX_RESET_REG_MISC_INT_ENABLE);
	__raw_writel(0, base + AR71XX_RESET_REG_MISC_INT_STATUS);


	irq_set_chained_handler(irq, ath79_misc_irq_handler);

	return 0;
}

static int __init ar7100_misc_intc_of_init(
	struct device_node *node, struct device_node *parent)
{
	ath79_misc_irq_chip.irq_mask_ack = ar71xx_misc_irq_mask;
	return ath79_misc_intc_of_init(node, parent);
}

IRQCHIP_DECLARE(ar7100_misc_intc, "qca,ar7100-misc-intc",
		ar7100_misc_intc_of_init);

static int __init ar7240_misc_intc_of_init(
	struct device_node *node, struct device_node *parent)
{
	ath79_misc_irq_chip.irq_ack = ar724x_misc_irq_ack;
	return ath79_misc_intc_of_init(node, parent);
}

IRQCHIP_DECLARE(ar7240_misc_intc, "qca,ar7240-misc-intc",
		ar7240_misc_intc_of_init);

static int __init ar79_cpu_intc_of_init(
	struct device_node *node, struct device_node *parent)
{
	int err, i, count;

	/* Fill the irq_wb_chan table */
	count = of_count_phandle_with_args(
		node, "qca,ddr-wb-channels", "#qca,ddr-wb-channel-cells");

	for (i = 0; i < count; i++) {
		struct of_phandle_args args;
		u32 irq = i;

		of_property_read_u32_index(
			node, "qca,ddr-wb-channel-interrupts", i, &irq);
		if (irq >= ARRAY_SIZE(irq_wb_chan))
			continue;

		err = of_parse_phandle_with_args(
			node, "qca,ddr-wb-channels",
			"#qca,ddr-wb-channel-cells",
			i, &args);
		if (err)
			return err;

		irq_wb_chan[irq] = args.args[0];
		pr_info("IRQ: Set flush channel of IRQ%d to %d\n",
			irq, args.args[0]);
	}

	return mips_cpu_irq_of_init(node, parent);
}
IRQCHIP_DECLARE(ar79_cpu_intc, "qca,ar7100-cpu-intc",
		ar79_cpu_intc_of_init);

#endif

void __init arch_init_irq(void)
{
	if (mips_machtype == ATH79_MACH_GENERIC_OF) {
		irqchip_init();
		return;
	}

	if (soc_is_ar71xx() || soc_is_ar724x() ||
	    soc_is_ar913x() || soc_is_ar933x()) {
		irq_wb_chan[2] = 3;
		irq_wb_chan[3] = 2;
	} else if (soc_is_ar934x()) {
		irq_wb_chan[3] = 2;
	}

	mips_cpu_irq_init();
	ath79_misc_irq_init();

	if (soc_is_ar934x())
		ar934x_ip2_irq_init();
	else if (soc_is_qca955x())
		qca955x_irq_init();
}
