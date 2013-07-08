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

#define EMEV2_SCU_BASE 0x1e000000

static int __cpuinit emev2_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	arch_send_wakeup_ipi_mask(cpumask_of(cpu_logical_map(cpu)));
	return 0;
}

static void __init emev2_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(shmobile_scu_base);

	/* Tell ROM loader about our vector (in headsmp-scu.S, headsmp.S) */
	emev2_set_boot_vector(__pa(shmobile_boot_vector));
	shmobile_boot_fn = virt_to_phys(shmobile_boot_scu);
	shmobile_boot_arg = (unsigned long)shmobile_scu_base;

	/* enable cache coherency on booting CPU */
	scu_power_mode(shmobile_scu_base, SCU_PM_NORMAL);
}

static void __init emev2_smp_init_cpus(void)
{
	unsigned int ncores;

	/* setup EMEV2 specific SCU base */
	shmobile_scu_base = ioremap(EMEV2_SCU_BASE, PAGE_SIZE);
	emev2_clock_init(); /* need ioremapped SMU */

	ncores = shmobile_scu_base ? scu_get_core_count(shmobile_scu_base) : 1;

	shmobile_smp_init_cpus(ncores);
}

struct smp_operations emev2_smp_ops __initdata = {
	.smp_init_cpus		= emev2_smp_init_cpus,
	.smp_prepare_cpus	= emev2_smp_prepare_cpus,
	.smp_boot_secondary	= emev2_boot_secondary,
};
