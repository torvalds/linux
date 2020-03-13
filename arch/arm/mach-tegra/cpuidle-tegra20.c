// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CPU idle driver for Tegra CPUs
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
 * Copyright (c) 2011 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *         Gary King <gking@nvidia.com>
 *
 * Rework for 3.3 by Peter De Schrijver <pdeschrijver@nvidia.com>
 */

#include <linux/clk/tegra.h>
#include <linux/tick.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <soc/tegra/flowctrl.h>
#include <soc/tegra/irq.h>
#include <soc/tegra/pm.h>

#include <asm/cpuidle.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>

#include "cpuidle.h"
#include "iomap.h"
#include "reset.h"
#include "sleep.h"

#ifdef CONFIG_PM_SLEEP
static atomic_t abort_flag;
static atomic_t abort_barrier;
static int tegra20_idle_lp2_coupled(struct cpuidle_device *dev,
				    struct cpuidle_driver *drv,
				    int index);
#define TEGRA20_MAX_STATES 2
#else
#define TEGRA20_MAX_STATES 1
#endif

static struct cpuidle_driver tegra_idle_driver = {
	.name = "tegra_idle",
	.owner = THIS_MODULE,
	.states = {
		ARM_CPUIDLE_WFI_STATE_PWR(600),
#ifdef CONFIG_PM_SLEEP
		{
			.enter            = tegra20_idle_lp2_coupled,
			.exit_latency     = 5000,
			.target_residency = 10000,
			.power_usage      = 0,
			.flags            = CPUIDLE_FLAG_COUPLED |
			                    CPUIDLE_FLAG_TIMER_STOP,
			.name             = "powered-down",
			.desc             = "CPU power gated",
		},
#endif
	},
	.state_count = TEGRA20_MAX_STATES,
	.safe_state_index = 0,
};

#ifdef CONFIG_PM_SLEEP
#ifdef CONFIG_SMP
static void tegra20_wake_cpu1_from_reset(void)
{
	/* enable cpu clock on cpu */
	tegra_enable_cpu_clock(1);

	/* take the CPU out of reset */
	tegra_cpu_out_of_reset(1);

	/* unhalt the cpu */
	flowctrl_write_cpu_halt(1, 0);
}
#else
static inline void tegra20_wake_cpu1_from_reset(void)
{
}
#endif

static void tegra20_report_cpus_state(void)
{
	unsigned long cpu, lcpu, csr;

	for_each_cpu(lcpu, cpu_possible_mask) {
		cpu = cpu_logical_map(lcpu);
		csr = flowctrl_read_cpu_csr(cpu);

		pr_err("cpu%lu: online=%d flowctrl_csr=0x%08lx\n",
		       cpu, cpu_online(lcpu), csr);
	}
}

static int tegra20_wait_for_secondary_cpu_parking(void)
{
	unsigned int retries = 3;

	while (retries--) {
		unsigned int delay_us = 10;
		unsigned int timeout_us = 500 * 1000 / delay_us;

		/*
		 * The primary CPU0 core shall wait for the secondaries
		 * shutdown in order to power-off CPU's cluster safely.
		 * The timeout value depends on the current CPU frequency,
		 * it takes about 40-150us  in average and over 1000us in
		 * a worst case scenario.
		 */
		do {
			if (tegra_cpu_rail_off_ready())
				return 0;

			udelay(delay_us);

		} while (timeout_us--);

		pr_err("secondary CPU taking too long to park\n");

		tegra20_report_cpus_state();
	}

	pr_err("timed out waiting secondaries to park\n");

	return -ETIMEDOUT;
}

static bool tegra20_cpu_cluster_power_down(struct cpuidle_device *dev,
					   struct cpuidle_driver *drv,
					   int index)
{
	bool ret;

	if (tegra20_wait_for_secondary_cpu_parking())
		return false;

	ret = !tegra_pm_enter_lp2();

	if (cpu_online(1))
		tegra20_wake_cpu1_from_reset();

	return ret;
}

#ifdef CONFIG_SMP
static bool tegra20_idle_enter_lp2_cpu_1(struct cpuidle_device *dev,
					 struct cpuidle_driver *drv,
					 int index)
{
	cpu_suspend(dev->cpu, tegra_pm_park_secondary_cpu);

	return true;
}
#else
static inline bool tegra20_idle_enter_lp2_cpu_1(struct cpuidle_device *dev,
						struct cpuidle_driver *drv,
						int index)
{
	return true;
}
#endif

static int tegra20_idle_lp2_coupled(struct cpuidle_device *dev,
				    struct cpuidle_driver *drv,
				    int index)
{
	bool entered_lp2 = false;

	if (tegra_pending_sgi())
		atomic_set(&abort_flag, 1);

	cpuidle_coupled_parallel_barrier(dev, &abort_barrier);

	if (atomic_read(&abort_flag)) {
		cpuidle_coupled_parallel_barrier(dev, &abort_barrier);
		/* clean flag for next coming */
		atomic_set(&abort_flag, 0);
		return -EINTR;
	}

	local_fiq_disable();

	tegra_pm_set_cpu_in_lp2();
	cpu_pm_enter();

	if (dev->cpu == 0)
		entered_lp2 = tegra20_cpu_cluster_power_down(dev, drv, index);
	else
		entered_lp2 = tegra20_idle_enter_lp2_cpu_1(dev, drv, index);

	cpu_pm_exit();
	tegra_pm_clear_cpu_in_lp2();

	local_fiq_enable();

	return entered_lp2 ? index : 0;
}
#endif

/*
 * Tegra20 HW appears to have a bug such that PCIe device interrupts, whether
 * they are legacy IRQs or MSI, are lost when LP2 is enabled. To work around
 * this, simply disable LP2 if the PCI driver and DT node are both enabled.
 */
void tegra20_cpuidle_pcie_irqs_in_use(void)
{
	pr_info_once(
		"Disabling cpuidle LP2 state, since PCIe IRQs are in use\n");
	cpuidle_driver_state_disabled(&tegra_idle_driver, 1, true);
}

int __init tegra20_cpuidle_init(void)
{
	return cpuidle_register(&tegra_idle_driver, cpu_possible_mask);
}
