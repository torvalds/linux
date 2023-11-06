/*
 * Populate sysfs with topology information
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
#include <linux/interrupt.h>
#include <linux/nodemask.h>
#include <linux/export.h>
#include <linux/mmzone.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/irq.h>
#include <asm/io_apic.h>
#include <asm/cpu.h>

static DEFINE_PER_CPU(struct x86_cpu, cpu_devices);

#ifdef CONFIG_HOTPLUG_CPU
int arch_register_cpu(int cpu)
{
	struct x86_cpu *xc = per_cpu_ptr(&cpu_devices, cpu);

	xc->cpu.hotpluggable = cpu > 0;
	return register_cpu(&xc->cpu, cpu);
}
EXPORT_SYMBOL(arch_register_cpu);

void arch_unregister_cpu(int num)
{
	unregister_cpu(&per_cpu(cpu_devices, num).cpu);
}
EXPORT_SYMBOL(arch_unregister_cpu);
#else /* CONFIG_HOTPLUG_CPU */

static int __init arch_register_cpu(int num)
{
	return register_cpu(&per_cpu(cpu_devices, num).cpu, num);
}
#endif /* CONFIG_HOTPLUG_CPU */

static int __init topology_init(void)
{
	int i;

	for_each_present_cpu(i)
		arch_register_cpu(i);

	return 0;
}
subsys_initcall(topology_init);
