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
#include <linux/delay.h>
#include <linux/cpu_pm.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/smp_plat.h>
#include <asm/cacheflush.h>
#include <asm/suspend.h>
#include <asm/idmap.h>
#include <asm/proc-fns.h>
#include <asm/tlbflush.h>

#include "iomap.h"
#include "reset.h"
#include "flowctrl.h"
#include "sleep.h"
#include "tegra_cpu_car.h"

#define TEGRA_POWER_CPU_PWRREQ_OE	(1 << 16)  /* CPU pwr req enable */

#define PMC_CTRL		0x0
#define PMC_CPUPWRGOOD_TIMER	0xc8
#define PMC_CPUPWROFF_TIMER	0xcc

#ifdef CONFIG_PM_SLEEP
static unsigned int g_diag_reg;
static DEFINE_SPINLOCK(tegra_lp2_lock);
static void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);
static struct clk *tegra_pclk;
void (*tegra_tear_down_cpu)(void);

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

static void set_power_timers(unsigned long us_on, unsigned long us_off)
{
	unsigned long long ticks;
	unsigned long long pclk;
	unsigned long rate;
	static unsigned long tegra_last_pclk;

	if (tegra_pclk == NULL) {
		tegra_pclk = clk_get_sys(NULL, "pclk");
		WARN_ON(IS_ERR(tegra_pclk));
	}

	rate = clk_get_rate(tegra_pclk);

	if (WARN_ON_ONCE(rate <= 0))
		pclk = 100000000;
	else
		pclk = rate;

	if ((rate != tegra_last_pclk)) {
		ticks = (us_on * pclk) + 999999ull;
		do_div(ticks, 1000000);
		writel((unsigned long)ticks, pmc + PMC_CPUPWRGOOD_TIMER);

		ticks = (us_off * pclk) + 999999ull;
		do_div(ticks, 1000000);
		writel((unsigned long)ticks, pmc + PMC_CPUPWROFF_TIMER);
		wmb();
	}
	tegra_last_pclk = pclk;
}

/*
 * restore_cpu_complex
 *
 * restores cpu clock setting, clears flow controller
 *
 * Always called on CPU 0.
 */
static void restore_cpu_complex(void)
{
	int cpu = smp_processor_id();

	BUG_ON(cpu != 0);

#ifdef CONFIG_SMP
	cpu = cpu_logical_map(cpu);
#endif

	/* Restore the CPU clock settings */
	tegra_cpu_clock_resume();

	flowctrl_cpu_suspend_exit(cpu);

	restore_cpu_arch_register();
}

/*
 * suspend_cpu_complex
 *
 * saves pll state for use by restart_plls, prepares flow controller for
 * transition to suspend state
 *
 * Must always be called on cpu 0.
 */
static void suspend_cpu_complex(void)
{
	int cpu = smp_processor_id();

	BUG_ON(cpu != 0);

#ifdef CONFIG_SMP
	cpu = cpu_logical_map(cpu);
#endif

	/* Save the CPU clock settings */
	tegra_cpu_clock_suspend();

	flowctrl_cpu_suspend_enter(cpu);

	save_cpu_arch_register();
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

static int tegra_sleep_cpu(unsigned long v2p)
{
	/* Switch to the identity mapping. */
	cpu_switch_mm(idmap_pgd, &init_mm);

	/* Flush the TLB. */
	local_flush_tlb_all();

	tegra_sleep_cpu_finish(v2p);

	/* should never here */
	BUG();

	return 0;
}

void tegra_idle_lp2_last(u32 cpu_on_time, u32 cpu_off_time)
{
	u32 mode;

	/* Only the last cpu down does the final suspend steps */
	mode = readl(pmc + PMC_CTRL);
	mode |= TEGRA_POWER_CPU_PWRREQ_OE;
	writel(mode, pmc + PMC_CTRL);

	set_power_timers(cpu_on_time, cpu_off_time);

	cpu_cluster_pm_enter();
	suspend_cpu_complex();

	cpu_suspend(PHYS_OFFSET - PAGE_OFFSET, &tegra_sleep_cpu);

	restore_cpu_complex();
	cpu_cluster_pm_exit();
}
#endif
