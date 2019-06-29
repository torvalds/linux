// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Atheros AR71XX/AR724X/AR913X common routines
 *
 *  Copyright (C) 2010-2011 Jaiganesh Narayanan <jnarayanan@atheros.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15/2.6.31 BSP
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/spinlock.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "common.h"

static DEFINE_SPINLOCK(ath79_device_reset_lock);

u32 ath79_cpu_freq;
EXPORT_SYMBOL_GPL(ath79_cpu_freq);

u32 ath79_ahb_freq;
EXPORT_SYMBOL_GPL(ath79_ahb_freq);

u32 ath79_ddr_freq;
EXPORT_SYMBOL_GPL(ath79_ddr_freq);

enum ath79_soc_type ath79_soc;
unsigned int ath79_soc_rev;

void __iomem *ath79_pll_base;
void __iomem *ath79_reset_base;
EXPORT_SYMBOL_GPL(ath79_reset_base);
static void __iomem *ath79_ddr_base;
static void __iomem *ath79_ddr_wb_flush_base;
static void __iomem *ath79_ddr_pci_win_base;

void ath79_ddr_ctrl_init(void)
{
	ath79_ddr_base = ioremap_nocache(AR71XX_DDR_CTRL_BASE,
					 AR71XX_DDR_CTRL_SIZE);
	if (soc_is_ar913x() || soc_is_ar724x() || soc_is_ar933x()) {
		ath79_ddr_wb_flush_base = ath79_ddr_base + 0x7c;
		ath79_ddr_pci_win_base = 0;
	} else {
		ath79_ddr_wb_flush_base = ath79_ddr_base + 0x9c;
		ath79_ddr_pci_win_base = ath79_ddr_base + 0x7c;
	}
}
EXPORT_SYMBOL_GPL(ath79_ddr_ctrl_init);

void ath79_ddr_wb_flush(u32 reg)
{
	void __iomem *flush_reg = ath79_ddr_wb_flush_base + (reg * 4);

	/* Flush the DDR write buffer. */
	__raw_writel(0x1, flush_reg);
	while (__raw_readl(flush_reg) & 0x1)
		;

	/* It must be run twice. */
	__raw_writel(0x1, flush_reg);
	while (__raw_readl(flush_reg) & 0x1)
		;
}
EXPORT_SYMBOL_GPL(ath79_ddr_wb_flush);

void ath79_ddr_set_pci_windows(void)
{
	BUG_ON(!ath79_ddr_pci_win_base);

	__raw_writel(AR71XX_PCI_WIN0_OFFS, ath79_ddr_pci_win_base + 0x0);
	__raw_writel(AR71XX_PCI_WIN1_OFFS, ath79_ddr_pci_win_base + 0x4);
	__raw_writel(AR71XX_PCI_WIN2_OFFS, ath79_ddr_pci_win_base + 0x8);
	__raw_writel(AR71XX_PCI_WIN3_OFFS, ath79_ddr_pci_win_base + 0xc);
	__raw_writel(AR71XX_PCI_WIN4_OFFS, ath79_ddr_pci_win_base + 0x10);
	__raw_writel(AR71XX_PCI_WIN5_OFFS, ath79_ddr_pci_win_base + 0x14);
	__raw_writel(AR71XX_PCI_WIN6_OFFS, ath79_ddr_pci_win_base + 0x18);
	__raw_writel(AR71XX_PCI_WIN7_OFFS, ath79_ddr_pci_win_base + 0x1c);
}
EXPORT_SYMBOL_GPL(ath79_ddr_set_pci_windows);

void ath79_device_reset_set(u32 mask)
{
	unsigned long flags;
	u32 reg;
	u32 t;

	if (soc_is_ar71xx())
		reg = AR71XX_RESET_REG_RESET_MODULE;
	else if (soc_is_ar724x())
		reg = AR724X_RESET_REG_RESET_MODULE;
	else if (soc_is_ar913x())
		reg = AR913X_RESET_REG_RESET_MODULE;
	else if (soc_is_ar933x())
		reg = AR933X_RESET_REG_RESET_MODULE;
	else if (soc_is_ar934x())
		reg = AR934X_RESET_REG_RESET_MODULE;
	else if (soc_is_qca953x())
		reg = QCA953X_RESET_REG_RESET_MODULE;
	else if (soc_is_qca955x())
		reg = QCA955X_RESET_REG_RESET_MODULE;
	else if (soc_is_qca956x() || soc_is_tp9343())
		reg = QCA956X_RESET_REG_RESET_MODULE;
	else
		BUG();

	spin_lock_irqsave(&ath79_device_reset_lock, flags);
	t = ath79_reset_rr(reg);
	ath79_reset_wr(reg, t | mask);
	spin_unlock_irqrestore(&ath79_device_reset_lock, flags);
}
EXPORT_SYMBOL_GPL(ath79_device_reset_set);

void ath79_device_reset_clear(u32 mask)
{
	unsigned long flags;
	u32 reg;
	u32 t;

	if (soc_is_ar71xx())
		reg = AR71XX_RESET_REG_RESET_MODULE;
	else if (soc_is_ar724x())
		reg = AR724X_RESET_REG_RESET_MODULE;
	else if (soc_is_ar913x())
		reg = AR913X_RESET_REG_RESET_MODULE;
	else if (soc_is_ar933x())
		reg = AR933X_RESET_REG_RESET_MODULE;
	else if (soc_is_ar934x())
		reg = AR934X_RESET_REG_RESET_MODULE;
	else if (soc_is_qca953x())
		reg = QCA953X_RESET_REG_RESET_MODULE;
	else if (soc_is_qca955x())
		reg = QCA955X_RESET_REG_RESET_MODULE;
	else if (soc_is_qca956x() || soc_is_tp9343())
		reg = QCA956X_RESET_REG_RESET_MODULE;
	else
		BUG();

	spin_lock_irqsave(&ath79_device_reset_lock, flags);
	t = ath79_reset_rr(reg);
	ath79_reset_wr(reg, t & ~mask);
	spin_unlock_irqrestore(&ath79_device_reset_lock, flags);
}
EXPORT_SYMBOL_GPL(ath79_device_reset_clear);
