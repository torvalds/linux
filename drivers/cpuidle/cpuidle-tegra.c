// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU idle driver for Tegra CPUs
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 * Copyright (c) 2011 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *         Gary King <gking@nvidia.com>
 *
 * Rework for 3.3 by Peter De Schrijver <pdeschrijver@nvidia.com>
 *
 * Tegra20/124 driver unification by Dmitry Osipenko <digetx@gmail.com>
 */

#define pr_fmt(fmt)	"tegra-cpuidle: " fmt

#include <linux/atomic.h>
#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/clk/tegra.h>
#include <linux/firmware/trusted_foundations.h>

#include <soc/tegra/cpuidle.h>
#include <soc/tegra/flowctrl.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/irq.h>
#include <soc/tegra/pm.h>
#include <soc/tegra/pmc.h>

#include <asm/cpuidle.h>
#include <asm/firmware.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>

enum tegra_state {
	TEGRA_C1,
	TEGRA_C7,
	TEGRA_CC6,
	TEGRA_STATE_COUNT,
};

static atomic_t tegra_idle_barrier;
static atomic_t tegra_abort_flag;

static inline bool tegra_cpuidle_using_firmware(void)
{
	return firmware_ops->prepare_idle && firmware_ops->do_idle;
}

static void tegra_cpuidle_report_cpus_state(void)
{
	unsigned long cpu, lcpu, csr;

	for_each_cpu(lcpu, cpu_possible_mask) {
		cpu = cpu_logical_map(lcpu);
		csr = flowctrl_read_cpu_csr(cpu);

		pr_err("cpu%lu: online=%d flowctrl_csr=0x%08lx\n",
		       cpu, cpu_online(lcpu), csr);
	}
}

static int tegra_cpuidle_wait_for_secondary_cpus_parking(void)
{
	unsigned int retries = 3;

	while (retries--) {
		unsigned int delay_us = 10;
		unsigned int timeout_us = 500 * 1000 / delay_us;

		/*
		 * The primary CPU0 core shall wait for the secondaries
		 * shutdown in order to power-off CPU's cluster safely.
		 * The timeout value depends on the current CPU frequency,
		 * it takes about 40-150us in average and over 1000us in
		 * a worst case scenario.
		 */
		do {
			if (tegra_cpu_rail_off_ready())
				return 0;

			udelay(delay_us);

		} while (timeout_us--);

		pr_err("secondary CPU taking too long to park\n");

		tegra_cpuidle_report_cpus_state();
	}

	pr_err("timed out waiting secondaries to park\n");

	return -ETIMEDOUT;
}

static void tegra_cpuidle_unpark_secondary_cpus(void)
{
	unsigned int cpu, lcpu;

	for_each_cpu(lcpu, cpu_online_mask) {
		cpu = cpu_logical_map(lcpu);

		if (cpu > 0) {
			tegra_enable_cpu_clock(cpu);
			tegra_cpu_out_of_reset(cpu);
			flowctrl_write_cpu_halt(cpu, 0);
		}
	}
}

static int tegra_cpuidle_cc6_enter(unsigned int cpu)
{
	int ret;

	if (cpu > 0) {
		ret = cpu_suspend(cpu, tegra_pm_park_secondary_cpu);
	} else {
		ret = tegra_cpuidle_wait_for_secondary_cpus_parking();
		if (!ret)
			ret = tegra_pm_enter_lp2();

		tegra_cpuidle_unpark_secondary_cpus();
	}

	return ret;
}

static int tegra_cpuidle_c7_enter(void)
{
	int err;

	err = call_firmware_op(prepare_idle, TF_PM_MODE_LP2_NOFLUSH_L2);
	if (err && err != -ENOSYS)
		return err;

	err = call_firmware_op(do_idle, 0);
	if (err != -ENOSYS)
		return err;

	return cpu_suspend(0, tegra30_pm_secondary_cpu_suspend);
}

static int tegra_cpuidle_coupled_barrier(struct cpuidle_device *dev)
{
	if (tegra_pending_sgi()) {
		/*
		 * CPU got local interrupt that will be lost after GIC's
		 * shutdown because GIC driver doesn't save/restore the
		 * pending SGI state across CPU cluster PM.  Abort and retry
		 * next time.
		 */
		atomic_set(&tegra_abort_flag, 1);
	}

	cpuidle_coupled_parallel_barrier(dev, &tegra_idle_barrier);

	if (atomic_read(&tegra_abort_flag)) {
		cpuidle_coupled_parallel_barrier(dev, &tegra_idle_barrier);
		atomic_set(&tegra_abort_flag, 0);
		return -EINTR;
	}

	return 0;
}

static int tegra_cpuidle_state_enter(struct cpuidle_device *dev,
				     int index, unsigned int cpu)
{
	int err;

	/*
	 * CC6 state is the "CPU cluster power-off" state.  In order to
	 * enter this state, at first the secondary CPU cores need to be
	 * parked into offline mode, then the last CPU should clean out
	 * remaining dirty cache lines into DRAM and trigger Flow Controller
	 * logic that turns off the cluster's power domain (which includes
	 * CPU cores, GIC and L2 cache).
	 */
	if (index == TEGRA_CC6) {
		err = tegra_cpuidle_coupled_barrier(dev);
		if (err)
			return err;
	}

	local_fiq_disable();
	RCU_NONIDLE(tegra_pm_set_cpu_in_lp2());
	cpu_pm_enter();

	switch (index) {
	case TEGRA_C7:
		err = tegra_cpuidle_c7_enter();
		break;

	case TEGRA_CC6:
		err = tegra_cpuidle_cc6_enter(cpu);
		break;

	default:
		err = -EINVAL;
		break;
	}

	cpu_pm_exit();
	RCU_NONIDLE(tegra_pm_clear_cpu_in_lp2());
	local_fiq_enable();

	return err ?: index;
}

static int tegra_cpuidle_adjust_state_index(int index, unsigned int cpu)
{
	/*
	 * On Tegra30 CPU0 can't be power-gated separately from secondary
	 * cores because it gates the whole CPU cluster.
	 */
	if (cpu > 0 || index != TEGRA_C7 || tegra_get_chip_id() != TEGRA30)
		return index;

	/* put CPU0 into C1 if C7 is requested and secondaries are online */
	if (!IS_ENABLED(CONFIG_PM_SLEEP) || num_online_cpus() > 1)
		index = TEGRA_C1;
	else
		index = TEGRA_CC6;

	return index;
}

static int tegra_cpuidle_enter(struct cpuidle_device *dev,
			       struct cpuidle_driver *drv,
			       int index)
{
	unsigned int cpu = cpu_logical_map(dev->cpu);
	int ret;

	index = tegra_cpuidle_adjust_state_index(index, cpu);
	if (dev->states_usage[index].disable)
		return -1;

	if (index == TEGRA_C1)
		ret = arm_cpuidle_simple_enter(dev, drv, index);
	else
		ret = tegra_cpuidle_state_enter(dev, index, cpu);

	if (ret < 0) {
		if (ret != -EINTR || index != TEGRA_CC6)
			pr_err_once("failed to enter state %d err: %d\n",
				    index, ret);
		index = -1;
	} else {
		index = ret;
	}

	return index;
}

static int tegra114_enter_s2idle(struct cpuidle_device *dev,
				 struct cpuidle_driver *drv,
				 int index)
{
	tegra_cpuidle_enter(dev, drv, index);

	return 0;
}

/*
 * The previous versions of Tegra CPUIDLE driver used a different "legacy"
 * terminology for naming of the idling states, while this driver uses the
 * new terminology.
 *
 * Mapping of the old terms into the new ones:
 *
 * Old | New
 * ---------
 * LP3 | C1	(CPU core clock gating)
 * LP2 | C7	(CPU core power gating)
 * LP2 | CC6	(CPU cluster power gating)
 *
 * Note that that the older CPUIDLE driver versions didn't explicitly
 * differentiate the LP2 states because these states either used the same
 * code path or because CC6 wasn't supported.
 */
static struct cpuidle_driver tegra_idle_driver = {
	.name = "tegra_idle",
	.states = {
		[TEGRA_C1] = ARM_CPUIDLE_WFI_STATE_PWR(600),
		[TEGRA_C7] = {
			.enter			= tegra_cpuidle_enter,
			.exit_latency		= 2000,
			.target_residency	= 2200,
			.power_usage		= 100,
			.flags			= CPUIDLE_FLAG_TIMER_STOP,
			.name			= "C7",
			.desc			= "CPU core powered off",
		},
		[TEGRA_CC6] = {
			.enter			= tegra_cpuidle_enter,
			.exit_latency		= 5000,
			.target_residency	= 10000,
			.power_usage		= 0,
			.flags			= CPUIDLE_FLAG_TIMER_STOP |
						  CPUIDLE_FLAG_COUPLED,
			.name			= "CC6",
			.desc			= "CPU cluster powered off",
		},
	},
	.state_count = TEGRA_STATE_COUNT,
	.safe_state_index = TEGRA_C1,
};

static inline void tegra_cpuidle_disable_state(enum tegra_state state)
{
	cpuidle_driver_state_disabled(&tegra_idle_driver, state, true);
}

/*
 * Tegra20 HW appears to have a bug such that PCIe device interrupts, whether
 * they are legacy IRQs or MSI, are lost when CC6 is enabled.  To work around
 * this, simply disable CC6 if the PCI driver and DT node are both enabled.
 */
void tegra_cpuidle_pcie_irqs_in_use(void)
{
	struct cpuidle_state *state_cc6 = &tegra_idle_driver.states[TEGRA_CC6];

	if ((state_cc6->flags & CPUIDLE_FLAG_UNUSABLE) ||
	    tegra_get_chip_id() != TEGRA20)
		return;

	pr_info("disabling CC6 state, since PCIe IRQs are in use\n");
	tegra_cpuidle_disable_state(TEGRA_CC6);
}

static void tegra_cpuidle_setup_tegra114_c7_state(void)
{
	struct cpuidle_state *s = &tegra_idle_driver.states[TEGRA_C7];

	s->enter_s2idle = tegra114_enter_s2idle;
	s->target_residency = 1000;
	s->exit_latency = 500;
}

static int tegra_cpuidle_probe(struct platform_device *pdev)
{
	/* LP2 could be disabled in device-tree */
	if (tegra_pmc_get_suspend_mode() < TEGRA_SUSPEND_LP2)
		tegra_cpuidle_disable_state(TEGRA_CC6);

	/*
	 * Required suspend-resume functionality, which is provided by the
	 * Tegra-arch core and PMC driver, is unavailable if PM-sleep option
	 * is disabled.
	 */
	if (!IS_ENABLED(CONFIG_PM_SLEEP)) {
		if (!tegra_cpuidle_using_firmware())
			tegra_cpuidle_disable_state(TEGRA_C7);

		tegra_cpuidle_disable_state(TEGRA_CC6);
	}

	/*
	 * Generic WFI state (also known as C1 or LP3) and the coupled CPU
	 * cluster power-off (CC6 or LP2) states are common for all Tegra SoCs.
	 */
	switch (tegra_get_chip_id()) {
	case TEGRA20:
		/* Tegra20 isn't capable to power-off individual CPU cores */
		tegra_cpuidle_disable_state(TEGRA_C7);
		break;

	case TEGRA30:
		break;

	case TEGRA114:
	case TEGRA124:
		tegra_cpuidle_setup_tegra114_c7_state();

		/* coupled CC6 (LP2) state isn't implemented yet */
		tegra_cpuidle_disable_state(TEGRA_CC6);
		break;

	default:
		return -EINVAL;
	}

	return cpuidle_register(&tegra_idle_driver, cpu_possible_mask);
}

static struct platform_driver tegra_cpuidle_driver = {
	.probe = tegra_cpuidle_probe,
	.driver = {
		.name = "tegra-cpuidle",
	},
};
builtin_platform_driver(tegra_cpuidle_driver);
