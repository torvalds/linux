/*
 * arch/arm/mach-tegra/include/mach/legacy_irq.h
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

#ifndef _ARCH_ARM_MACH_TEGRA_LEGARY_IRQ_H
#define _ARCH_ARM_MACH_TEGRA_LEGARY_IRQ_H

void tegra_legacy_mask_irq(unsigned int irq);
void tegra_legacy_unmask_irq(unsigned int irq);
void tegra_legacy_select_fiq(unsigned int irq, bool fiq);
void tegra_legacy_force_irq_set(unsigned int irq);
void tegra_legacy_force_irq_clr(unsigned int irq);
int tegra_legacy_force_irq_status(unsigned int irq);
void tegra_legacy_select_fiq(unsigned int irq, bool fiq);
unsigned long tegra_legacy_vfiq(int nr);
unsigned long tegra_legacy_class(int nr);
int tegra_legacy_irq_set_wake(int irq, int enable);
void tegra_legacy_irq_set_lp1_wake_mask(void);
void tegra_legacy_irq_restore_mask(void);
void tegra_init_legacy_irq(void);
void tegra_legacy_irq_suspend(void);
void tegra_legacy_irq_resume(void);
#endif
