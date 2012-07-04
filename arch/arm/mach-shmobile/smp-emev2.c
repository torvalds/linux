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

static DEFINE_SPINLOCK(scu_lock);
static void __iomem *scu_base;

static void modify_scu_cpu_psr(unsigned long set, unsigned long clr)
{
	unsigned long tmp;

	/* we assume this code is running on a different cpu
	 * than the one that is changing coherency setting */
	spin_lock(&scu_lock);
	tmp = readl(scu_base + 8);
	tmp &= ~clr;
	tmp |= set;
	writel(tmp, scu_base + 8);
	spin_unlock(&scu_lock);

}

unsigned int __init emev2_get_core_count(void)
{
	if (!scu_base) {
		scu_base = ioremap(EMEV2_SCU_BASE, PAGE_SIZE);
		emev2_clock_init(); /* need ioremapped SMU */
	}

	WARN_ON_ONCE(!scu_base);

	return scu_base ? scu_get_core_count(scu_base) : 1;
}

int emev2_platform_cpu_kill(unsigned int cpu)
{
	return 0; /* not supported yet */
}

void __cpuinit emev2_secondary_init(unsigned int cpu)
{
	gic_secondary_init(0);
}

int __cpuinit emev2_boot_secondary(unsigned int cpu)
{
	cpu = cpu_logical_map(cpu);

	/* enable cache coherency */
	modify_scu_cpu_psr(0, 3 << (cpu * 8));

	/* Tell ROM loader about our vector (in headsmp.S) */
	emev2_set_boot_vector(__pa(shmobile_secondary_vector));

	gic_raise_softirq(cpumask_of(cpu), 1);
	return 0;
}

void __init emev2_smp_prepare_cpus(void)
{
	int cpu = cpu_logical_map(0);

	scu_enable(scu_base);

	/* enable cache coherency on CPU0 */
	modify_scu_cpu_psr(0, 3 << (cpu * 8));
}
