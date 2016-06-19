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

void __init arch_init_irq(void)
{
	unsigned irq_wb_chan2 = -1;
	unsigned irq_wb_chan3 = -1;
	bool misc_is_ar71xx;

	if (mips_machtype == ATH79_MACH_GENERIC_OF) {
		irqchip_init();
		return;
	}

	if (soc_is_ar71xx() || soc_is_ar724x() ||
	    soc_is_ar913x() || soc_is_ar933x()) {
		irq_wb_chan2 = 3;
		irq_wb_chan3 = 2;
	} else if (soc_is_ar934x()) {
		irq_wb_chan3 = 2;
	}

	ath79_cpu_irq_init(irq_wb_chan2, irq_wb_chan3);

	if (soc_is_ar71xx() || soc_is_ar913x())
		misc_is_ar71xx = true;
	else if (soc_is_ar724x() ||
		 soc_is_ar933x() ||
		 soc_is_ar934x() ||
		 soc_is_qca955x())
		misc_is_ar71xx = false;
	else
		BUG();
	ath79_misc_irq_init(
		ath79_reset_base + AR71XX_RESET_REG_MISC_INT_STATUS,
		ATH79_CPU_IRQ(6), ATH79_MISC_IRQ_BASE, misc_is_ar71xx);

	if (soc_is_ar934x())
		ar934x_ip2_irq_init();
	else if (soc_is_qca955x())
		qca955x_irq_init();
}
