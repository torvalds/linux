/*
 * Spin Table SMP initialisation
 *
 * Copyright (C) 2013 ARM Ltd.
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
#include <linux/of.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>

static phys_addr_t cpu_release_addr[NR_CPUS];

static int __init smp_spin_table_init_cpu(struct device_node *dn, int cpu)
{
	/*
	 * Determine the address from which the CPU is polling.
	 */
	if (of_property_read_u64(dn, "cpu-release-addr",
				 &cpu_release_addr[cpu])) {
		pr_err("CPU %d: missing or invalid cpu-release-addr property\n",
		       cpu);

		return -1;
	}

	return 0;
}

static int __init smp_spin_table_prepare_cpu(int cpu)
{
	void **release_addr;

	if (!cpu_release_addr[cpu])
		return -ENODEV;

	release_addr = __va(cpu_release_addr[cpu]);
	release_addr[0] = (void *)__pa(secondary_holding_pen);
	__flush_dcache_area(release_addr, sizeof(release_addr[0]));

	/*
	 * Send an event to wake up the secondary CPU.
	 */
	sev();

	return 0;
}

const struct smp_enable_ops smp_spin_table_ops __initconst = {
	.name		= "spin-table",
	.init_cpu 	= smp_spin_table_init_cpu,
	.prepare_cpu	= smp_spin_table_prepare_cpu,
};
