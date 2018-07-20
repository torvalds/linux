/*
 *  Atheros AR71XX/AR724X/AR913X common definitions
 *
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15 BSP
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef __ASM_MACH_ATH79_H
#define __ASM_MACH_ATH79_H

#include <linux/types.h>
#include <linux/io.h>

enum ath79_soc_type {
	ATH79_SOC_UNKNOWN,
	ATH79_SOC_AR7130,
	ATH79_SOC_AR7141,
	ATH79_SOC_AR7161,
	ATH79_SOC_AR7240,
	ATH79_SOC_AR7241,
	ATH79_SOC_AR7242,
	ATH79_SOC_AR9130,
	ATH79_SOC_AR9132,
	ATH79_SOC_AR9330,
	ATH79_SOC_AR9331,
	ATH79_SOC_AR9341,
	ATH79_SOC_AR9342,
	ATH79_SOC_AR9344,
	ATH79_SOC_QCA9533,
	ATH79_SOC_QCA9556,
	ATH79_SOC_QCA9558,
	ATH79_SOC_TP9343,
	ATH79_SOC_QCA956X,
};

extern enum ath79_soc_type ath79_soc;
extern unsigned int ath79_soc_rev;

static inline int soc_is_ar71xx(void)
{
	return (ath79_soc == ATH79_SOC_AR7130 ||
		ath79_soc == ATH79_SOC_AR7141 ||
		ath79_soc == ATH79_SOC_AR7161);
}

static inline int soc_is_ar724x(void)
{
	return (ath79_soc == ATH79_SOC_AR7240 ||
		ath79_soc == ATH79_SOC_AR7241 ||
		ath79_soc == ATH79_SOC_AR7242);
}

static inline int soc_is_ar7240(void)
{
	return (ath79_soc == ATH79_SOC_AR7240);
}

static inline int soc_is_ar7241(void)
{
	return (ath79_soc == ATH79_SOC_AR7241);
}

static inline int soc_is_ar7242(void)
{
	return (ath79_soc == ATH79_SOC_AR7242);
}

static inline int soc_is_ar913x(void)
{
	return (ath79_soc == ATH79_SOC_AR9130 ||
		ath79_soc == ATH79_SOC_AR9132);
}

static inline int soc_is_ar933x(void)
{
	return (ath79_soc == ATH79_SOC_AR9330 ||
		ath79_soc == ATH79_SOC_AR9331);
}

static inline int soc_is_ar9341(void)
{
	return (ath79_soc == ATH79_SOC_AR9341);
}

static inline int soc_is_ar9342(void)
{
	return (ath79_soc == ATH79_SOC_AR9342);
}

static inline int soc_is_ar9344(void)
{
	return (ath79_soc == ATH79_SOC_AR9344);
}

static inline int soc_is_ar934x(void)
{
	return soc_is_ar9341() || soc_is_ar9342() || soc_is_ar9344();
}

static inline int soc_is_qca9533(void)
{
	return ath79_soc == ATH79_SOC_QCA9533;
}

static inline int soc_is_qca953x(void)
{
	return soc_is_qca9533();
}

static inline int soc_is_qca9556(void)
{
	return ath79_soc == ATH79_SOC_QCA9556;
}

static inline int soc_is_qca9558(void)
{
	return ath79_soc == ATH79_SOC_QCA9558;
}

static inline int soc_is_qca955x(void)
{
	return soc_is_qca9556() || soc_is_qca9558();
}

static inline int soc_is_tp9343(void)
{
	return ath79_soc == ATH79_SOC_TP9343;
}

static inline int soc_is_qca9561(void)
{
	return ath79_soc == ATH79_SOC_QCA956X;
}

static inline int soc_is_qca9563(void)
{
	return ath79_soc == ATH79_SOC_QCA956X;
}

static inline int soc_is_qca956x(void)
{
	return soc_is_qca9561() || soc_is_qca9563();
}

void ath79_ddr_wb_flush(unsigned int reg);
void ath79_ddr_set_pci_windows(void);

extern void __iomem *ath79_pll_base;
extern void __iomem *ath79_reset_base;

static inline void ath79_pll_wr(unsigned reg, u32 val)
{
	__raw_writel(val, ath79_pll_base + reg);
}

static inline u32 ath79_pll_rr(unsigned reg)
{
	return __raw_readl(ath79_pll_base + reg);
}

static inline void ath79_reset_wr(unsigned reg, u32 val)
{
	__raw_writel(val, ath79_reset_base + reg);
	(void) __raw_readl(ath79_reset_base + reg); /* flush */
}

static inline u32 ath79_reset_rr(unsigned reg)
{
	return __raw_readl(ath79_reset_base + reg);
}

void ath79_device_reset_set(u32 mask);
void ath79_device_reset_clear(u32 mask);

void ath79_cpu_irq_init(unsigned irq_wb_chan2, unsigned irq_wb_chan3);
void ath79_misc_irq_init(void __iomem *regs, int irq,
			int irq_base, bool is_ar71xx);

#endif /* __ASM_MACH_ATH79_H */
