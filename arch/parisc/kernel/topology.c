/*
 * arch/parisc/kernel/topology.c - Populate driverfs with topology information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cpu.h>

static struct cpu cpu_devices[NR_CPUS];

static int __init topology_init(void)
{
	struct node *parent = NULL;
	int num;

	for_each_present_cpu(num) {
		register_cpu(&cpu_devices[num], num, parent);
	}
	return 0;
}

subsys_initcall(topology_init);
