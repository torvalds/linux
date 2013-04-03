/*
 * SMP support for R-Mobile / SH-Mobile - r8a7779 portion
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
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
#include <linux/irqchip/arm-gic.h>
#include <mach/common.h>
#include <mach/r8a7779.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/smp_twd.h>

#define AVECR IOMEM(0xfe700040)
#define R8A7779_SCU_BASE 0xf0000000

static struct r8a7779_pm_ch r8a7779_ch_cpu1 = {
	.chan_offs = 0x40, /* PWRSR0 .. PWRER0 */
	.chan_bit = 1, /* ARM1 */
	.isr_bit = 1, /* ARM1 */
};

static struct r8a7779_pm_ch r8a7779_ch_cpu2 = {
	.chan_offs = 0x40, /* PWRSR0 .. PWRER0 */
	.chan_bit = 2, /* ARM2 */
	.isr_bit = 2, /* ARM2 */
};

static struct r8a7779_pm_ch r8a7779_ch_cpu3 = {
	.chan_offs = 0x40, /* PWRSR0 .. PWRER0 */
	.chan_bit = 3, /* ARM3 */
	.isr_bit = 3, /* ARM3 */
};

static struct r8a7779_pm_ch *r8a7779_ch_cpu[4] = {
	[1] = &r8a7779_ch_cpu1,
	[2] = &r8a7779_ch_cpu2,
	[3] = &r8a7779_ch_cpu3,
};

#ifdef CONFIG_HAVE_ARM_TWD
static DEFINE_TWD_LOCAL_TIMER(twd_local_timer, R8A7779_SCU_BASE + 0x600, 29);
void __init r8a7779_register_twd(void)
{
	twd_local_timer_register(&twd_local_timer);
}
#endif

static int r8a7779_platform_cpu_kill(unsigned int cpu)
{
	struct r8a7779_pm_ch *ch = NULL;
	int ret = -EIO;

	cpu = cpu_logical_map(cpu);

	if (cpu < ARRAY_SIZE(r8a7779_ch_cpu))
		ch = r8a7779_ch_cpu[cpu];

	if (ch)
		ret = r8a7779_sysc_power_down(ch);

	return ret ? ret : 1;
}

static void __cpuinit r8a7779_secondary_init(unsigned int cpu)
{
	gic_secondary_init(0);
}

static int __cpuinit r8a7779_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	struct r8a7779_pm_ch *ch = NULL;
	int ret = -EIO;

	cpu = cpu_logical_map(cpu);

	if (cpu < ARRAY_SIZE(r8a7779_ch_cpu))
		ch = r8a7779_ch_cpu[cpu];

	if (ch)
		ret = r8a7779_sysc_power_up(ch);

	return ret;
}

static void __init r8a7779_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(shmobile_scu_base);

	/* Map the reset vector (in headsmp-scu.S) */
	__raw_writel(__pa(shmobile_secondary_vector_scu), AVECR);

	/* enable cache coherency on booting CPU */
	scu_power_mode(shmobile_scu_base, SCU_PM_NORMAL);

	r8a7779_pm_init();

	/* power off secondary CPUs */
	r8a7779_platform_cpu_kill(1);
	r8a7779_platform_cpu_kill(2);
	r8a7779_platform_cpu_kill(3);
}

static void __init r8a7779_smp_init_cpus(void)
{
	/* setup r8a7779 specific SCU base */
	shmobile_scu_base = IOMEM(R8A7779_SCU_BASE);

	shmobile_smp_init_cpus(scu_get_core_count(shmobile_scu_base));
}

#ifdef CONFIG_HOTPLUG_CPU
static int r8a7779_scu_psr_core_disabled(int cpu)
{
	unsigned long mask = 3 << (cpu * 8);

	if ((__raw_readl(shmobile_scu_base + 8) & mask) == mask)
		return 1;

	return 0;
}

static int r8a7779_cpu_kill(unsigned int cpu)
{
	int k;

	/* this function is running on another CPU than the offline target,
	 * here we need wait for shutdown code in platform_cpu_die() to
	 * finish before asking SoC-specific code to power off the CPU core.
	 */
	for (k = 0; k < 1000; k++) {
		if (r8a7779_scu_psr_core_disabled(cpu))
			return r8a7779_platform_cpu_kill(cpu);

		mdelay(1);
	}

	return 0;
}

static void r8a7779_cpu_die(unsigned int cpu)
{
	dsb();
	flush_cache_all();

	/* disable cache coherency */
	scu_power_mode(shmobile_scu_base, SCU_PM_POWEROFF);

	/* Endless loop until power off from r8a7779_cpu_kill() */
	while (1)
		cpu_do_idle();
}

static int r8a7779_cpu_disable(unsigned int cpu)
{
	/* only CPU1->3 have power domains, do not allow hotplug of CPU0 */
	return cpu == 0 ? -EPERM : 0;
}
#endif /* CONFIG_HOTPLUG_CPU */

struct smp_operations r8a7779_smp_ops  __initdata = {
	.smp_init_cpus		= r8a7779_smp_init_cpus,
	.smp_prepare_cpus	= r8a7779_smp_prepare_cpus,
	.smp_secondary_init	= r8a7779_secondary_init,
	.smp_boot_secondary	= r8a7779_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= r8a7779_cpu_kill,
	.cpu_die		= r8a7779_cpu_die,
	.cpu_disable		= r8a7779_cpu_disable,
#endif
};
