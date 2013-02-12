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
#include <linux/irqchip/arm-gic.h>
#include <mach/common.h>
#include <mach/emev2.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
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

static void __cpuinit emev2_secondary_init(unsigned int cpu)
{
	gic_secondary_init(0);
}

static int __cpuinit emev2_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	cpu = cpu_logical_map(cpu);

	/* enable cache coherency */
	modify_scu_cpu_psr(0, 3 << (cpu * 8));

	arch_send_wakeup_ipi_mask(cpumask_of(cpu));
	return 0;
}

static void __init emev2_smp_prepare_cpus(unsigned int max_cpus)
{
	int cpu = cpu_logical_map(0);

	scu_enable(scu_base);

	/* Tell ROM loader about our vector (in headsmp.S) */
	emev2_set_boot_vector(__pa(shmobile_secondary_vector));

	/* enable cache coherency on CPU0 */
	modify_scu_cpu_psr(0, 3 << (cpu * 8));
}

static void __init emev2_smp_init_cpus(void)
{
	unsigned int ncores;

	if (!scu_base) {
		scu_base = ioremap(EMEV2_SCU_BASE, PAGE_SIZE);
		emev2_clock_init(); /* need ioremapped SMU */
	}

	ncores = scu_base ? scu_get_core_count(scu_base) : 1;

	shmobile_smp_init_cpus(ncores);
}

struct smp_operations emev2_smp_ops __initdata = {
	.smp_init_cpus		= emev2_smp_init_cpus,
	.smp_prepare_cpus	= emev2_smp_prepare_cpus,
	.smp_secondary_init	= emev2_secondary_init,
	.smp_boot_secondary	= emev2_boot_secondary,
};
