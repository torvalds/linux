/*
 * arch/i386/kernel/topology.c - Populate driverfs with topology information
 *
 * Written by: Matthew Dobson, IBM Corporation
 * Original Code: Paul Dorwin, IBM Corporation, Patrick Mochel, OSDL
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.
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
 *
 * Send feedback to <colpatch@us.ibm.com>
 */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/nodemask.h>
#include <linux/mmzone.h>
#include <asm/cpu.h>

static struct i386_cpu cpu_devices[NR_CPUS];

int arch_register_cpu(int num)
{
	/*
	 * CPU0 cannot be offlined due to several
	 * restrictions and assumptions in kernel. This basically
	 * doesnt add a control file, one cannot attempt to offline
	 * BSP.
	 *
	 * Also certain PCI quirks require not to enable hotplug control
	 * for all CPU's.
	 */
	if (!num || !enable_cpu_hotplug)
		cpu_devices[num].cpu.no_control = 1;

	return register_cpu(&cpu_devices[num].cpu, num);
}

#ifdef CONFIG_HOTPLUG_CPU
int enable_cpu_hotplug = 1;

void arch_unregister_cpu(int num) {
	return unregister_cpu(&cpu_devices[num].cpu);
}
EXPORT_SYMBOL(arch_register_cpu);
EXPORT_SYMBOL(arch_unregister_cpu);
#endif /*CONFIG_HOTPLUG_CPU*/

static int __init topology_init(void)
{
	int i;

#ifdef CONFIG_NUMA
	for_each_online_node(i)
		register_one_node(i);
#endif /* CONFIG_NUMA */

	for_each_present_cpu(i)
		arch_register_cpu(i);
	return 0;
}

subsys_initcall(topology_init);
