/*
 * arch/arm/mach-tegra/legacy_irq.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/legacy_irq.h>

#define INT_SYS_NR	(INT_GPIO_BASE - INT_PRI_BASE)
#define INT_SYS_SZ	(INT_SEC_BASE - INT_PRI_BASE)
#define PPI_NR		((INT_SYS_NR+INT_SYS_SZ-1)/INT_SYS_SZ)

#define ICTLR_CPU_IEP_VFIQ	0x08
#define ICTLR_CPU_IEP_FIR	0x14
#define ICTLR_CPU_IEP_FIR_SET	0x18
#define ICTLR_CPU_IEP_FIR_CLR	0x1c

#define ICTLR_CPU_IER		0x20
#define ICTLR_CPU_IER_SET	0x24
#define ICTLR_CPU_IER_CLR	0x28
#define ICTLR_CPU_IEP_CLASS	0x2C

#define ICTLR_COP_IER		0x30
#define ICTLR_COP_IER_SET	0x34
#define ICTLR_COP_IER_CLR	0x38
#define ICTLR_COP_IEP_CLASS	0x3c

#define NUM_ICTLRS 4

static void __iomem *ictlr_reg_base[] = {
	IO_ADDRESS(TEGRA_PRIMARY_ICTLR_BASE),
	IO_ADDRESS(TEGRA_SECONDARY_ICTLR_BASE),
	IO_ADDRESS(TEGRA_TERTIARY_ICTLR_BASE),
	IO_ADDRESS(TEGRA_QUATERNARY_ICTLR_BASE),
};

/* When going into deep sleep, the CPU is powered down, taking the GIC with it
   In order to wake, the wake interrupts need to be enabled in the legacy
   interrupt controller. */
void tegra_legacy_unmask_irq(unsigned int irq)
{
	void __iomem *base;
	pr_debug("%s: %d\n", __func__, irq);

	irq -= 32;
	base = ictlr_reg_base[irq>>5];
	writel(1 << (irq & 31), base + ICTLR_CPU_IER_SET);
}

void tegra_legacy_mask_irq(unsigned int irq)
{
	void __iomem *base;
	pr_debug("%s: %d\n", __func__, irq);

	irq -= 32;
	base = ictlr_reg_base[irq>>5];
	writel(1 << (irq & 31), base + ICTLR_CPU_IER_CLR);
}

void tegra_legacy_force_irq_set(unsigned int irq)
{
	void __iomem *base;
	pr_debug("%s: %d\n", __func__, irq);

	irq -= 32;
	base = ictlr_reg_base[irq>>5];
	writel(1 << (irq & 31), base + ICTLR_CPU_IEP_FIR_SET);
}

void tegra_legacy_force_irq_clr(unsigned int irq)
{
	void __iomem *base;
	pr_debug("%s: %d\n", __func__, irq);

	irq -= 32;
	base = ictlr_reg_base[irq>>5];
	writel(1 << (irq & 31), base + ICTLR_CPU_IEP_FIR_CLR);
}

int tegra_legacy_force_irq_status(unsigned int irq)
{
	void __iomem *base;
	pr_debug("%s: %d\n", __func__, irq);

	irq -= 32;
	base = ictlr_reg_base[irq>>5];
	return !!(readl(base + ICTLR_CPU_IEP_FIR) & (1 << (irq & 31)));
}

void tegra_legacy_select_fiq(unsigned int irq, bool fiq)
{
	void __iomem *base;
	pr_debug("%s: %d\n", __func__, irq);

	irq -= 32;
	base = ictlr_reg_base[irq>>5];
	writel(fiq << (irq & 31), base + ICTLR_CPU_IEP_CLASS);
}

unsigned long tegra_legacy_vfiq(int nr)
{
	void __iomem *base;
	base = ictlr_reg_base[nr];
	return readl(base + ICTLR_CPU_IEP_VFIQ);
}

unsigned long tegra_legacy_class(int nr)
{
	void __iomem *base;
	base = ictlr_reg_base[nr];
	return readl(base + ICTLR_CPU_IEP_CLASS);
}

void tegra_init_legacy_irq(void)
{
	int i;

	for (i = 0; i < NUM_ICTLRS; i++) {
		void __iomem *ictlr = ictlr_reg_base[i];
		writel(~0, ictlr + ICTLR_CPU_IER_CLR);
		writel(0, ictlr + ICTLR_CPU_IEP_CLASS);
	}
}
