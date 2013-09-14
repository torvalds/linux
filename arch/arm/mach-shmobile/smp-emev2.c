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
#define EMEV2_SMU_BASE 0xe0110000
#define SMU_GENERAL_REG0 0x7c0

static int __cpuinit emev2_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	arch_send_wakeup_ipi_mask(cpumask_of(cpu_logical_map(cpu)));
	return 0;
}

static void __init emev2_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *smu;

	/* Tell ROM loader about our vector (in headsmp.S) */
	smu = ioremap(EMEV2_SMU_BASE, PAGE_SIZE);
	if (smu) {
		iowrite32(__pa(shmobile_boot_vector), smu + SMU_GENERAL_REG0);
		iounmap(smu);
	}

	/* setup EMEV2 specific SCU bits */
	shmobile_scu_base = ioremap(EMEV2_SCU_BASE, PAGE_SIZE);
	shmobile_smp_scu_prepare_cpus(max_cpus);
}

struct smp_operations emev2_smp_ops __initdata = {
	.smp_prepare_cpus	= emev2_smp_prepare_cpus,
	.smp_boot_secondary	= emev2_boot_secondary,
};
