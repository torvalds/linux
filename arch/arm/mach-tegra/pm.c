// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU complex suspend & resume functions for Tegra SoCs
 *
 * Copyright (c) 2009-2012, NVIDIA Corporation. All rights reserved.
 */

#include <linux/clk/tegra.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>

#include <linux/firmware/trusted_foundations.h>

#include <soc/tegra/flowctrl.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/pm.h>
#include <soc/tegra/pmc.h>

#include <asm/cacheflush.h>
#include <asm/firmware.h>
#include <asm/idmap.h>
#include <asm/proc-fns.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>

#include "iomap.h"
#include "pm.h"
#include "reset.h"
#include "sleep.h"

#ifdef CONFIG_PM_SLEEP
static DEFINE_SPINLOCK(tegra_lp2_lock);
static u32 iram_save_size;
static void *iram_save_addr;
struct tegra_lp1_iram tegra_lp1_iram;
void (*tegra_tear_down_cpu)(void);
void (*tegra_sleep_core_finish)(unsigned long v2p);
static int (*tegra_sleep_func)(unsigned long v2p);

static void tegra_tear_down_cpu_init(void)
{
	switch (tegra_get_chip_id()) {
	case TEGRA20:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC))
			tegra_tear_down_cpu = tegra20_tear_down_cpu;
		break;
	case TEGRA30:
	case TEGRA114:
	case TEGRA124:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_3x_SOC) ||
		    IS_ENABLED(CONFIG_ARCH_TEGRA_114_SOC) ||
		    IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC))
			tegra_tear_down_cpu = tegra30_tear_down_cpu;
		break;
	}
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

void tegra_pm_clear_cpu_in_lp2(void)
{
	int phy_cpu_id = cpu_logical_map(smp_processor_id());
	u32 *cpu_in_lp2 = tegra_cpu_lp2_mask;

	spin_lock(&tegra_lp2_lock);

	BUG_ON(!(*cpu_in_lp2 & BIT(phy_cpu_id)));
	*cpu_in_lp2 &= ~BIT(phy_cpu_id);

	spin_unlock(&tegra_lp2_lock);
}

void tegra_pm_set_cpu_in_lp2(void)
{
	int phy_cpu_id = cpu_logical_map(smp_processor_id());
	u32 *cpu_in_lp2 = tegra_cpu_lp2_mask;

	spin_lock(&tegra_lp2_lock);

	BUG_ON((*cpu_in_lp2 & BIT(phy_cpu_id)));
	*cpu_in_lp2 |= BIT(phy_cpu_id);

	spin_unlock(&tegra_lp2_lock);
}

static int tegra_sleep_cpu(unsigned long v2p)
{
	if (tegra_cpu_car_ops->rail_off_ready &&
	    WARN_ON(!tegra_cpu_rail_off_ready()))
		return -EBUSY;

	/*
	 * L2 cache disabling using kernel API only allowed when all
	 * secondary CPU's are offline. Cache have to be disabled with
	 * MMU-on if cache maintenance is done via Trusted Foundations
	 * firmware. Note that CPUIDLE won't ever enter powergate on Tegra30
	 * if any of secondary CPU's is online and this is the LP2-idle
	 * code-path only for Tegra20/30.
	 */
#ifdef CONFIG_OUTER_CACHE
	if (trusted_foundations_registered() && outer_cache.disable)
		outer_cache.disable();
#endif
	/*
	 * Note that besides of setting up CPU reset vector this firmware
	 * call may also do the following, depending on the FW version:
	 *  1) Disable L2. But this doesn't matter since we already
	 *     disabled the L2.
	 *  2) Disable D-cache. This need to be taken into account in
	 *     particular by the tegra_disable_clean_inv_dcache() which
	 *     shall avoid the re-disable.
	 */
	call_firmware_op(prepare_idle, TF_PM_MODE_LP2);

	setup_mm_for_reboot();
	tegra_sleep_cpu_finish(v2p);

	/* should never here */
	BUG();

	return 0;
}

static void tegra_pm_set(enum tegra_suspend_mode mode)
{
	u32 value;

	switch (tegra_get_chip_id()) {
	case TEGRA20:
	case TEGRA30:
		break;
	default:
		/* Turn off CRAIL */
		value = flowctrl_read_cpu_csr(0);
		value &= ~FLOW_CTRL_CSR_ENABLE_EXT_MASK;
		value |= FLOW_CTRL_CSR_ENABLE_EXT_CRAIL;
		flowctrl_write_cpu_csr(0, value);
		break;
	}

	tegra_pmc_enter_suspend_mode(mode);
}

int tegra_pm_enter_lp2(void)
{
	int err;

	tegra_pm_set(TEGRA_SUSPEND_LP2);

	cpu_cluster_pm_enter();
	suspend_cpu_complex();

	err = cpu_suspend(PHYS_OFFSET - PAGE_OFFSET, &tegra_sleep_cpu);

	/*
	 * Resume L2 cache if it wasn't re-enabled early during resume,
	 * which is the case for Tegra30 that has to re-enable the cache
	 * via firmware call. In other cases cache is already enabled and
	 * hence re-enabling is a no-op. This is always a no-op on Tegra114+.
	 */
	outer_resume();

	restore_cpu_complex();
	cpu_cluster_pm_exit();

	call_firmware_op(prepare_idle, TF_PM_MODE_NONE);

	return err;
}

enum tegra_suspend_mode tegra_pm_validate_suspend_mode(
				enum tegra_suspend_mode mode)
{
	/*
	 * The Tegra devices support suspending to LP1 or lower currently.
	 */
	if (mode > TEGRA_SUSPEND_LP1)
		return TEGRA_SUSPEND_LP1;

	return mode;
}

static int tegra_sleep_core(unsigned long v2p)
{
	/*
	 * Cache have to be disabled with MMU-on if cache maintenance is done
	 * via Trusted Foundations firmware. This is a no-op on Tegra114+.
	 */
	if (trusted_foundations_registered())
		outer_disable();

	call_firmware_op(prepare_idle, TF_PM_MODE_LP1);

	setup_mm_for_reboot();
	tegra_sleep_core_finish(v2p);

	/* should never here */
	BUG();

	return 0;
}

/*
 * tegra_lp1_iram_hook
 *
 * Hooking the address of LP1 reset vector and SDRAM self-refresh code in
 * SDRAM. These codes not be copied to IRAM in this fuction. We need to
 * copy these code to IRAM before LP0/LP1 suspend and restore the content
 * of IRAM after resume.
 */
static bool tegra_lp1_iram_hook(void)
{
	switch (tegra_get_chip_id()) {
	case TEGRA20:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC))
			tegra20_lp1_iram_hook();
		break;
	case TEGRA30:
	case TEGRA114:
	case TEGRA124:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_3x_SOC) ||
		    IS_ENABLED(CONFIG_ARCH_TEGRA_114_SOC) ||
		    IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC))
			tegra30_lp1_iram_hook();
		break;
	default:
		break;
	}

	if (!tegra_lp1_iram.start_addr || !tegra_lp1_iram.end_addr)
		return false;

	iram_save_size = tegra_lp1_iram.end_addr - tegra_lp1_iram.start_addr;
	iram_save_addr = kmalloc(iram_save_size, GFP_KERNEL);
	if (!iram_save_addr)
		return false;

	return true;
}

static bool tegra_sleep_core_init(void)
{
	switch (tegra_get_chip_id()) {
	case TEGRA20:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC))
			tegra20_sleep_core_init();
		break;
	case TEGRA30:
	case TEGRA114:
	case TEGRA124:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_3x_SOC) ||
		    IS_ENABLED(CONFIG_ARCH_TEGRA_114_SOC) ||
		    IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC))
			tegra30_sleep_core_init();
		break;
	default:
		break;
	}

	if (!tegra_sleep_core_finish)
		return false;

	return true;
}

static void tegra_suspend_enter_lp1(void)
{
	/* copy the reset vector & SDRAM shutdown code into IRAM */
	memcpy(iram_save_addr, IO_ADDRESS(TEGRA_IRAM_LPx_RESUME_AREA),
		iram_save_size);
	memcpy(IO_ADDRESS(TEGRA_IRAM_LPx_RESUME_AREA),
		tegra_lp1_iram.start_addr, iram_save_size);

	*((u32 *)tegra_cpu_lp1_mask) = 1;
}

static void tegra_suspend_exit_lp1(void)
{
	/* restore IRAM */
	memcpy(IO_ADDRESS(TEGRA_IRAM_LPx_RESUME_AREA), iram_save_addr,
		iram_save_size);

	*(u32 *)tegra_cpu_lp1_mask = 0;
}

static const char *lp_state[TEGRA_MAX_SUSPEND_MODE] = {
	[TEGRA_SUSPEND_NONE] = "none",
	[TEGRA_SUSPEND_LP2] = "LP2",
	[TEGRA_SUSPEND_LP1] = "LP1",
	[TEGRA_SUSPEND_LP0] = "LP0",
};

static int tegra_suspend_enter(suspend_state_t state)
{
	enum tegra_suspend_mode mode = tegra_pmc_get_suspend_mode();

	if (WARN_ON(mode < TEGRA_SUSPEND_NONE ||
		    mode >= TEGRA_MAX_SUSPEND_MODE))
		return -EINVAL;

	pr_info("Entering suspend state %s\n", lp_state[mode]);

	tegra_pm_set(mode);

	local_fiq_disable();

	suspend_cpu_complex();
	switch (mode) {
	case TEGRA_SUSPEND_LP1:
		tegra_suspend_enter_lp1();
		break;
	case TEGRA_SUSPEND_LP2:
		tegra_pm_set_cpu_in_lp2();
		break;
	default:
		break;
	}

	cpu_suspend(PHYS_OFFSET - PAGE_OFFSET, tegra_sleep_func);

	/*
	 * Resume L2 cache if it wasn't re-enabled early during resume,
	 * which is the case for Tegra30 that has to re-enable the cache
	 * via firmware call. In other cases cache is already enabled and
	 * hence re-enabling is a no-op.
	 */
	outer_resume();

	switch (mode) {
	case TEGRA_SUSPEND_LP1:
		tegra_suspend_exit_lp1();
		break;
	case TEGRA_SUSPEND_LP2:
		tegra_pm_clear_cpu_in_lp2();
		break;
	default:
		break;
	}
	restore_cpu_complex();

	local_fiq_enable();

	call_firmware_op(prepare_idle, TF_PM_MODE_NONE);

	return 0;
}

static const struct platform_suspend_ops tegra_suspend_ops = {
	.valid		= suspend_valid_only_mem,
	.enter		= tegra_suspend_enter,
};

void tegra_pm_init_suspend(void)
{
	enum tegra_suspend_mode mode = tegra_pmc_get_suspend_mode();

	if (mode == TEGRA_SUSPEND_NONE)
		return;

	tegra_tear_down_cpu_init();

	if (mode >= TEGRA_SUSPEND_LP1) {
		if (!tegra_lp1_iram_hook() || !tegra_sleep_core_init()) {
			pr_err("%s: unable to allocate memory for SDRAM"
			       "self-refresh -- LP0/LP1 unavailable\n",
			       __func__);
			tegra_pmc_set_suspend_mode(TEGRA_SUSPEND_LP2);
			mode = TEGRA_SUSPEND_LP2;
		}
	}

	/* set up sleep function for cpu_suspend */
	switch (mode) {
	case TEGRA_SUSPEND_LP1:
		tegra_sleep_func = tegra_sleep_core;
		break;
	case TEGRA_SUSPEND_LP2:
		tegra_sleep_func = tegra_sleep_cpu;
		break;
	default:
		break;
	}

	suspend_set_ops(&tegra_suspend_ops);
}

int tegra_pm_park_secondary_cpu(unsigned long cpu)
{
	if (cpu > 0) {
		tegra_disable_clean_inv_dcache(TEGRA_FLUSH_CACHE_LOUIS);

		if (tegra_get_chip_id() == TEGRA20)
			tegra20_hotplug_shutdown();
		else
			tegra30_hotplug_shutdown();
	}

	return -EINVAL;
}
#endif
