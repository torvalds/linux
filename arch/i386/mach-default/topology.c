/*
 * arch/i386/mach-generic/topology.c - Populate driverfs with topology information
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
#include <asm/cpu.h>

static struct i386_cpu cpu_devices[NR_CPUS];

int arch_register_cpu(int num){
	struct node *parent = NULL;
	
#ifdef CONFIG_NUMA
	int node = cpu_to_node(num);
	if (node_online(node))
		parent = &node_devices[node].node;
#endif /* CONFIG_NUMA */

	return register_cpu(&cpu_devices[num].cpu, num, parent);
}

#ifdef CONFIG_HOTPLUG_CPU

void arch_unregister_cpu(int num) {
	struct node *parent = NULL;

#ifdef CONFIG_NUMA
	int node = cpu_to_node(num);
	if (node_online(node))
		parent = &node_devices[node].node;
#endif /* CONFIG_NUMA */

	return unregister_cpu(&cpu_devices[num].cpu, parent);
}
EXPORT_SYMBOL(arch_register_cpu);
EXPORT_SYMBOL(arch_unregister_cpu);
#endif /*CONFIG_HOTPLUG_CPU*/



#ifdef CONFIG_NUMA
#include <linux/mmzone.h>
#include <asm/node.h>

struct i386_node node_devices[MAX_NUMNODES];

static int __init topology_init(void)
{
	int i;

	for_each_online_node(i)
		arch_register_node(i);

	for_each_present_cpu(i)
		arch_register_cpu(i);
	return 0;
}

#else /* !CONFIG_NUMA */

static int __init topology_init(void)
{
	int i;

	for_each_present_cpu(i)
		arch_register_cpu(i);
	return 0;
}

#endif /* CONFIG_NUMA */

subsys_initcall(topology_init);
