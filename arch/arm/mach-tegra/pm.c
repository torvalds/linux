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
#include <linux/suspend.h>
#include <linux/err.h>
#include <linux/clk/tegra.h>

#include <asm/smp_plat.h>
#include <asm/cacheflush.h>
#include <asm/suspend.h>
#include <asm/idmap.h>
#include <asm/proc-fns.h>
#include <asm/tlbflush.h>

#include "iomap.h"
#include "reset.h"
#include "flowctrl.h"
#include "fuse.h"
#include "pmc.h"
#include "sleep.h"

#ifdef CONFIG_PM_SLEEP
static DEFINE_SPINLOCK(tegra_lp2_lock);
void (*tegra_tear_down_cpu)(void);

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
}

void tegra_clear_cpu_in_lp2(int phy_cpu_id)
{
	u32 *cpu_in_lp2 = tegra_cpu_lp2_mask;

	spin_lock(&tegra_lp2_lock);

	BUG_ON(!(*cpu_in_lp2 & BIT(phy_cpu_id)));
	*cpu_in_lp2 &= ~BIT(phy_cpu_id);

	spin_unlock(&tegra_lp2_lock);
}

bool tegra_set_cpu_in_lp2(int phy_cpu_id)
{
	bool last_cpu = false;
	cpumask_t *cpu_lp2_mask = tegra_cpu_lp2_mask;
	u32 *cpu_in_lp2 = tegra_cpu_lp2_mask;

	spin_lock(&tegra_lp2_lock);

	BUG_ON((*cpu_in_lp2 & BIT(phy_cpu_id)));
	*cpu_in_lp2 |= BIT(phy_cpu_id);

	if ((phy_cpu_id == 0) && cpumask_equal(cpu_lp2_mask, cpu_online_mask))
		last_cpu = true;
	else if (tegra_chip_id == TEGRA20 && phy_cpu_id == 1)
		tegra20_cpu_set_resettable_soon();

	spin_unlock(&tegra_lp2_lock);
	return last_cpu;
}

int tegra_cpu_do_idle(void)
{
	return cpu_do_idle();
}

static int tegra_sleep_cpu(unsigned long v2p)
{
	setup_mm_for_reboot();
	tegra_sleep_cpu_finish(v2p);

	/* should never here */
	BUG();

	return 0;
}

void tegra_idle_lp2_last(void)
{
	tegra_pmc_pm_set(TEGRA_SUSPEND_LP2);

	cpu_cluster_pm_enter();
	suspend_cpu_complex();

	cpu_suspend(PHYS_OFFSET - PAGE_OFFSET, &tegra_sleep_cpu);

	restore_cpu_complex();
	cpu_cluster_pm_exit();
}

enum tegra_suspend_mode tegra_pm_validate_suspend_mode(
				enum tegra_suspend_mode mode)
{
	/* Tegra114 didn't support any suspending mode yet. */
	if (tegra_chip_id == TEGRA114)
		return TEGRA_SUSPEND_NONE;

	/*
	 * The Tegra devices only support suspending to LP2 currently.
	 */
	if (mode > TEGRA_SUSPEND_LP2)
		return TEGRA_SUSPEND_LP2;

	return mode;
}

static const char *lp_state[TEGRA_MAX_SUSPEND_MODE] = {
	[TEGRA_SUSPEND_NONE] = "none",
	[TEGRA_SUSPEND_LP2] = "LP2",
	[TEGRA_SUSPEND_LP1] = "LP1",
	[TEGRA_SUSPEND_LP0] = "LP0",
};

static int __cpuinit tegra_suspend_enter(suspend_state_t state)
{
	enum tegra_suspend_mode mode = tegra_pmc_get_suspend_mode();

	if (WARN_ON(mode < TEGRA_SUSPEND_NONE ||
		    mode >= TEGRA_MAX_SUSPEND_MODE))
		return -EINVAL;

	pr_info("Entering suspend state %s\n", lp_state[mode]);

	tegra_pmc_pm_set(mode);

	local_fiq_disable();

	suspend_cpu_complex();
	switch (mode) {
	case TEGRA_SUSPEND_LP2:
		tegra_set_cpu_in_lp2(0);
		break;
	default:
		break;
	}

	cpu_suspend(PHYS_OFFSET - PAGE_OFFSET, &tegra_sleep_cpu);

	switch (mode) {
	case TEGRA_SUSPEND_LP2:
		tegra_clear_cpu_in_lp2(0);
		break;
	default:
		break;
	}
	restore_cpu_complex();

	local_fiq_enable();

	return 0;
}

static const struct platform_suspend_ops tegra_suspend_ops = {
	.valid		= suspend_valid_only_mem,
	.enter		= tegra_suspend_enter,
};

void __init tegra_init_suspend(void)
{
	if (tegra_pmc_get_suspend_mode() == TEGRA_SUSPEND_NONE)
		return;

	tegra_pmc_suspend_init();

	suspend_set_ops(&tegra_suspend_ops);
}
#endif
