/*
 * SMP support for Emma Mobile EV2
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 * Copyright (C) 2012  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <mach/common.h>
#include <mach/emev2.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/hardware/gic.h>
#include <asm/cacheflush.h>

#define EMEV2_SCU_BASE 0x1e000000

static void __iomem *scu_base;

static unsigned int __init emev2_get_core_count(void)
{
	if (!scu_base) {
		scu_base = ioremap(EMEV2_SCU_BASE, PAGE_SIZE);
		emev2_clock_init(); /* need ioremapped SMU */
	}

	WARN_ON_ONCE(!scu_base);

	return scu_base ? scu_get_core_count(scu_base) : 1;
}

static int emev2_platform_cpu_kill(unsigned int cpu)
{
	return 0; /* not supported yet */
}

static int __maybe_unused emev2_cpu_kill(unsigned int cpu)
{
	int k;

	/* this function is running on another CPU than the offline target,
	 * here we need wait for shutdown code in platform_cpu_die() to
	 * finish before asking SoC-specific code to power off the CPU core.
	 */
	for (k = 0; k < 1000; k++) {
		if (shmobile_cpu_is_dead(cpu))
			return emev2_platform_cpu_kill(cpu);
		mdelay(1);
	}

	return 0;
}


static void __cpuinit emev2_secondary_init(unsigned int cpu)
{
	gic_secondary_init(0);
}

static int __cpuinit emev2_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	cpu = cpu_logical_map(cpu);

	/* enable cache coherency */
	scu_power_mode(scu_base, 0);

	/* Tell ROM loader about our vector (in headsmp.S) */
	emev2_set_boot_vector(__pa(shmobile_secondary_vector));

	gic_raise_softirq(cpumask_of(cpu), 0);
	return 0;
}

static void __init emev2_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(scu_base);

	/* enable cache coherency on CPU0 */
	scu_power_mode(scu_base, 0);
}

static void __init emev2_smp_init_cpus(void)
{
	unsigned int ncores = emev2_get_core_count();

	shmobile_smp_init_cpus(ncores);
}

struct smp_operations emev2_smp_ops __initdata = {
	.smp_init_cpus		= emev2_smp_init_cpus,
	.smp_prepare_cpus	= emev2_smp_prepare_cpus,
	.smp_secondary_init	= emev2_secondary_init,
	.smp_boot_secondary	= emev2_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= emev2_cpu_kill,
	.cpu_die		= shmobile_cpu_die,
	.cpu_disable		= shmobile_cpu_disable,
#endif
};
