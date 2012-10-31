/*
 * CPU complex suspend & resume functions for Tegra SoCs
 *
 * Copyright (c) 2009-2012, NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/cpumask.h>

#include "iomap.h"
#include "reset.h"

#ifdef CONFIG_PM_SLEEP
static unsigned int g_diag_reg;
static DEFINE_SPINLOCK(tegra_lp2_lock);

void save_cpu_arch_register(void)
{
	/* read diagnostic register */
	asm("mrc p15, 0, %0, c15, c0, 1" : "=r"(g_diag_reg) : : "cc");
	return;
}

void restore_cpu_arch_register(void)
{
	/* write diagnostic register */
	asm("mcr p15, 0, %0, c15, c0, 1" : : "r"(g_diag_reg) : "cc");
	return;
}

void __cpuinit tegra_clear_cpu_in_lp2(int phy_cpu_id)
{
	u32 *cpu_in_lp2 = tegra_cpu_lp2_mask;

	spin_lock(&tegra_lp2_lock);

	BUG_ON(!(*cpu_in_lp2 & BIT(phy_cpu_id)));
	*cpu_in_lp2 &= ~BIT(phy_cpu_id);

	spin_unlock(&tegra_lp2_lock);
}

bool __cpuinit tegra_set_cpu_in_lp2(int phy_cpu_id)
{
	bool last_cpu = false;
	cpumask_t *cpu_lp2_mask = tegra_cpu_lp2_mask;
	u32 *cpu_in_lp2 = tegra_cpu_lp2_mask;

	spin_lock(&tegra_lp2_lock);

	BUG_ON((*cpu_in_lp2 & BIT(phy_cpu_id)));
	*cpu_in_lp2 |= BIT(phy_cpu_id);

	if ((phy_cpu_id == 0) && cpumask_equal(cpu_lp2_mask, cpu_online_mask))
		last_cpu = true;

	spin_unlock(&tegra_lp2_lock);
	return last_cpu;
}
#endif
