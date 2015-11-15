/*
 * Symmetric Multi Processing (SMP) support for Marvell EBU Cortex-A9
 * based SOCs (Armada 375/38x).
 *
 * Copyright (C) 2014 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/mbus.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>
#include "common.h"
#include "pmsu.h"

extern void mvebu_cortex_a9_secondary_startup(void);

static int mvebu_cortex_a9_boot_secondary(unsigned int cpu,
						    struct task_struct *idle)
{
	int ret, hw_cpu;

	pr_info("Booting CPU %d\n", cpu);

	/*
	 * Write the address of secondary startup into the system-wide
	 * flags register. The boot monitor waits until it receives a
	 * soft interrupt, and then the secondary CPU branches to this
	 * address.
	 */
	hw_cpu = cpu_logical_map(cpu);
	if (of_machine_is_compatible("marvell,armada375"))
		mvebu_system_controller_set_cpu_boot_addr(mvebu_cortex_a9_secondary_startup);
	else
		mvebu_pmsu_set_cpu_boot_addr(hw_cpu, mvebu_cortex_a9_secondary_startup);
	smp_wmb();

	/*
	 * Doing this before deasserting the CPUs is needed to wake up CPUs
	 * in the offline state after using CPU hotplug.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	ret = mvebu_cpu_reset_deassert(hw_cpu);
	if (ret) {
		pr_err("Could not start the secondary CPU: %d\n", ret);
		return ret;
	}

	return 0;
}
/*
 * When a CPU is brought back online, either through CPU hotplug, or
 * because of the boot of a kexec'ed kernel, the PMSU configuration
 * for this CPU might be in the deep idle state, preventing this CPU
 * from receiving interrupts. Here, we therefore take out the current
 * CPU from this state, which was entered by armada_38x_cpu_die()
 * below.
 */
static void armada_38x_secondary_init(unsigned int cpu)
{
	mvebu_v7_pmsu_idle_exit();
}

#ifdef CONFIG_HOTPLUG_CPU
static void armada_38x_cpu_die(unsigned int cpu)
{
	/*
	 * CPU hotplug is implemented by putting offline CPUs into the
	 * deep idle sleep state.
	 */
	armada_38x_do_cpu_suspend(true);
}

/*
 * We need a dummy function, so that platform_can_cpu_hotplug() knows
 * we support CPU hotplug. However, the function does not need to do
 * anything, because CPUs going offline can enter the deep idle state
 * by themselves, without any help from a still alive CPU.
 */
static int armada_38x_cpu_kill(unsigned int cpu)
{
	return 1;
}
#endif

static const struct smp_operations mvebu_cortex_a9_smp_ops __initconst = {
	.smp_boot_secondary	= mvebu_cortex_a9_boot_secondary,
};

static const struct smp_operations armada_38x_smp_ops __initconst = {
	.smp_boot_secondary	= mvebu_cortex_a9_boot_secondary,
	.smp_secondary_init     = armada_38x_secondary_init,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= armada_38x_cpu_die,
	.cpu_kill               = armada_38x_cpu_kill,
#endif
};

CPU_METHOD_OF_DECLARE(mvebu_armada_375_smp, "marvell,armada-375-smp",
		      &mvebu_cortex_a9_smp_ops);
CPU_METHOD_OF_DECLARE(mvebu_armada_380_smp, "marvell,armada-380-smp",
		      &armada_38x_smp_ops);
CPU_METHOD_OF_DECLARE(mvebu_armada_390_smp, "marvell,armada-390-smp",
		      &armada_38x_smp_ops);
