/*
 * Dummy Virtual Machine - does what it says on the tin.
 *
 * Copyright (C) 2012 ARM Ltd
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/of.h>

#include <asm/psci.h>
#include <asm/smp_plat.h>

extern void secondary_startup(void);

static void __init virt_smp_init_cpus(void)
{
}

static void __init virt_smp_prepare_cpus(unsigned int max_cpus)
{
}

static int __cpuinit virt_boot_secondary(unsigned int cpu,
					 struct task_struct *idle)
{
	if (psci_ops.cpu_on)
		return psci_ops.cpu_on(cpu_logical_map(cpu),
				       __pa(secondary_startup));
	return -ENODEV;
}

struct smp_operations __initdata virt_smp_ops = {
	.smp_init_cpus		= virt_smp_init_cpus,
	.smp_prepare_cpus	= virt_smp_prepare_cpus,
	.smp_boot_secondary	= virt_boot_secondary,
};
