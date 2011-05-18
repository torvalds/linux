/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * Copyright (C) 2010, NVIDIA Corporation
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

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/gic.h>

#include <mach/iomap.h>
#include <mach/legacy_irq.h>
#include <mach/suspend.h>

#include "board.h"

#define PMC_CTRL		0x0
#define PMC_CTRL_LATCH_WAKEUPS	(1 << 5)
#define PMC_WAKE_MASK		0xc
#define PMC_WAKE_LEVEL		0x10
#define PMC_WAKE_STATUS		0x14
#define PMC_SW_WAKE_STATUS	0x18
#define PMC_DPD_SAMPLE		0x20

static void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);

static u32 tegra_lp0_wake_enb;
static u32 tegra_lp0_wake_level;
static u32 tegra_lp0_wake_level_any;

static void (*tegra_gic_mask_irq)(struct irq_data *d);
static void (*tegra_gic_unmask_irq)(struct irq_data *d);
static void (*tegra_gic_ack_irq)(struct irq_data *d);

/* ensures that sufficient time is passed for a register write to
 * serialize into the 32KHz domain */
static void pmc_32kwritel(u32 val, unsigned long offs)
{
	writel(val, pmc + offs);
	udelay(130);
}

int tegra_set_lp1_wake(int irq, int enable)
{
	return tegra_legacy_irq_set_wake(irq, enable);
}

void tegra_set_lp0_wake_pads(u32 wake_enb, u32 wake_level, u32 wake_any)
{
	u32 temp;
	u32 status;
	u32 lvl;

	wake_level &= wake_enb;
	wake_any &= wake_enb;

	wake_level |= (tegra_lp0_wake_level & tegra_lp0_wake_enb);
	wake_any |= (tegra_lp0_wake_level_any & tegra_lp0_wake_enb);

	wake_enb |= tegra_lp0_wake_enb;

	pmc_32kwritel(0, PMC_SW_WAKE_STATUS);
	temp = readl(pmc + PMC_CTRL);
	temp |= PMC_CTRL_LATCH_WAKEUPS;
	pmc_32kwritel(temp, PMC_CTRL);
	temp &= ~PMC_CTRL_LATCH_WAKEUPS;
	pmc_32kwritel(temp, PMC_CTRL);
	status = readl(pmc + PMC_SW_WAKE_STATUS);
	lvl = readl(pmc + PMC_WAKE_LEVEL);

	/* flip the wakeup trigger for any-edge triggered pads
	 * which are currently asserting as wakeups */
	lvl ^= status;
	lvl &= wake_any;

	wake_level |= lvl;

	writel(wake_level, pmc + PMC_WAKE_LEVEL);
	/* Enable DPD sample to trigger sampling pads data and direction
	 * in which pad will be driven during lp0 mode*/
	writel(0x1, pmc + PMC_DPD_SAMPLE);

	writel(wake_enb, pmc + PMC_WAKE_MASK);
}

static void tegra_mask(struct irq_data *d)
{
	tegra_gic_mask_irq(d);
	tegra_legacy_mask_irq(d->irq);
}

static void tegra_unmask(struct irq_data *d)
{
	tegra_gic_unmask_irq(d);
	tegra_legacy_unmask_irq(d->irq);
}

static void tegra_ack(struct irq_data *d)
{
	tegra_legacy_force_irq_clr(d->irq);
	tegra_gic_ack_irq(d);
}

static int tegra_retrigger(struct irq_data *d)
{
	tegra_legacy_force_irq_set(d->irq);
	return 1;
}

static struct irq_chip tegra_irq = {
	.name			= "PPI",
	.irq_ack		= tegra_ack,
	.irq_mask		= tegra_mask,
	.irq_unmask		= tegra_unmask,
	.irq_retrigger		= tegra_retrigger,
};

void __init tegra_init_irq(void)
{
	struct irq_chip *gic;
	unsigned int i;
	int irq;

	tegra_init_legacy_irq();

	gic_init(0, 29, IO_ADDRESS(TEGRA_ARM_INT_DIST_BASE),
		 IO_ADDRESS(TEGRA_ARM_PERIF_BASE + 0x100));

	gic = irq_get_chip(29);
	tegra_gic_unmask_irq = gic->irq_unmask;
	tegra_gic_mask_irq = gic->irq_mask;
	tegra_gic_ack_irq = gic->irq_ack;
#ifdef CONFIG_SMP
	tegra_irq.irq_set_affinity = gic->irq_set_affinity;
#endif

	for (i = 0; i < INT_MAIN_NR; i++) {
		irq = INT_PRI_BASE + i;
		irq_set_chip_and_handler(irq, &tegra_irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
}
