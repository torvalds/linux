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

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/smp_twd.h>

#include "common.h"
#include "pm-rcar.h"
#include "r8a7779.h"

#define AVECR IOMEM(0xfe700040)
#define R8A7779_SCU_BASE 0xf0000000

static struct rcar_sysc_ch r8a7779_ch_cpu1 = {
	.chan_offs = 0x40, /* PWRSR0 .. PWRER0 */
	.chan_bit = 1, /* ARM1 */
	.isr_bit = 1, /* ARM1 */
};

static struct rcar_sysc_ch r8a7779_ch_cpu2 = {
	.chan_offs = 0x40, /* PWRSR0 .. PWRER0 */
	.chan_bit = 2, /* ARM2 */
	.isr_bit = 2, /* ARM2 */
};

static struct rcar_sysc_ch r8a7779_ch_cpu3 = {
	.chan_offs = 0x40, /* PWRSR0 .. PWRER0 */
	.chan_bit = 3, /* ARM3 */
	.isr_bit = 3, /* ARM3 */
};

static struct rcar_sysc_ch *r8a7779_ch_cpu[4] = {
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
	struct rcar_sysc_ch *ch = NULL;
	int ret = -EIO;

	cpu = cpu_logical_map(cpu);

	if (cpu < ARRAY_SIZE(r8a7779_ch_cpu))
		ch = r8a7779_ch_cpu[cpu];

	if (ch)
		ret = rcar_sysc_power_down(ch);

	return ret ? ret : 1;
}

static int r8a7779_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	struct rcar_sysc_ch *ch = NULL;
	unsigned int lcpu = cpu_logical_map(cpu);
	int ret;

	if (lcpu < ARRAY_SIZE(r8a7779_ch_cpu))
		ch = r8a7779_ch_cpu[lcpu];

	if (ch)
		ret = rcar_sysc_power_up(ch);
	else
		ret = -EIO;

	return ret;
}

static void __init r8a7779_smp_prepare_cpus(unsigned int max_cpus)
{
	/* Map the reset vector (in headsmp-scu.S, headsmp.S) */
	__raw_writel(__pa(shmobile_boot_vector), AVECR);
	shmobile_boot_fn = virt_to_phys(shmobile_boot_scu);
	shmobile_boot_arg = (unsigned long)shmobile_scu_base;

	/* setup r8a7779 specific SCU bits */
	shmobile_scu_base = IOMEM(R8A7779_SCU_BASE);
	shmobile_smp_scu_prepare_cpus(max_cpus);

	r8a7779_pm_init();

	/* power off secondary CPUs */
	r8a7779_platform_cpu_kill(1);
	r8a7779_platform_cpu_kill(2);
	r8a7779_platform_cpu_kill(3);
}

#ifdef CONFIG_HOTPLUG_CPU
static int r8a7779_cpu_kill(unsigned int cpu)
{
	if (shmobile_smp_scu_cpu_kill(cpu))
		return r8a7779_platform_cpu_kill(cpu);

	return 0;
}

static int r8a7779_cpu_disable(unsigned int cpu)
{
	/* only CPU1->3 have power domains, do not allow hotplug of CPU0 */
	return cpu == 0 ? -EPERM : 0;
}
#endif /* CONFIG_HOTPLUG_CPU */

struct smp_operations r8a7779_smp_ops  __initdata = {
	.smp_prepare_cpus	= r8a7779_smp_prepare_cpus,
	.smp_boot_secondary	= r8a7779_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= r8a7779_cpu_disable,
	.cpu_die		= shmobile_smp_scu_cpu_die,
	.cpu_kill		= r8a7779_cpu_kill,
#endif
};
