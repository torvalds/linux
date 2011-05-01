/*
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
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

#include "board.h"

static void tegra_mask(struct irq_data *d)
{
	if (d->irq >= 32)
		tegra_legacy_mask_irq(d->irq);
}

static void tegra_unmask(struct irq_data *d)
{
	if (d->irq >= 32)
		tegra_legacy_unmask_irq(d->irq);
}

static void tegra_ack(struct irq_data *d)
{
	if (d->irq >= 32)
		tegra_legacy_force_irq_clr(d->irq);
}

static int tegra_retrigger(struct irq_data *d)
{
	if (d->irq < 32)
		return 0;

	tegra_legacy_force_irq_set(d->irq);
	return 1;
}

void __init tegra_init_irq(void)
{
	tegra_init_legacy_irq();

	gic_arch_extn.irq_ack = tegra_ack;
	gic_arch_extn.irq_mask = tegra_mask;
	gic_arch_extn.irq_unmask = tegra_unmask;
	gic_arch_extn.irq_retrigger = tegra_retrigger;

	gic_init(0, 29, IO_ADDRESS(TEGRA_ARM_INT_DIST_BASE),
		 IO_ADDRESS(TEGRA_ARM_PERIF_BASE + 0x100));
}
