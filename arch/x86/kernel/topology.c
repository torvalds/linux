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

#ifdef CONFIG_BOOTPARAM_HOTPLUG_CPU0
static int cpu0_hotpluggable = 1;
#else
static int cpu0_hotpluggable;
static int __init enable_cpu0_hotplug(char *str)
{
	cpu0_hotpluggable = 1;
	return 1;
}

__setup("cpu0_hotplug", enable_cpu0_hotplug);
#endif

#ifdef CONFIG_DEBUG_HOTPLUG_CPU0
/*
 * This function offlines a CPU as early as possible and allows userspace to
 * boot up without the CPU. The CPU can be onlined back by user after boot.
 *
 * This is only called for debugging CPU offline/online feature.
 */
int _debug_hotplug_cpu(int cpu, int action)
{
	int ret;

	if (!cpu_is_hotpluggable(cpu))
		return -EINVAL;

	switch (action) {
	case 0:
		ret = remove_cpu(cpu);
		if (!ret)
			pr_info("DEBUG_HOTPLUG_CPU0: CPU %u is now offline\n", cpu);
		else
			pr_debug("Can't offline CPU%d.\n", cpu);
		break;
	case 1:
		ret = add_cpu(cpu);
		if (ret)
			pr_debug("Can't online CPU%d.\n", cpu);

		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int __init debug_hotplug_cpu(void)
{
	_debug_hotplug_cpu(0, 0);
	return 0;
}

late_initcall_sync(debug_hotplug_cpu);
#endif /* CONFIG_DEBUG_HOTPLUG_CPU0 */

int arch_register_cpu(int num)
{
	struct cpuinfo_x86 *c = &cpu_data(num);

	/*
	 * Currently CPU0 is only hotpluggable on Intel platforms. Other
	 * vendors can add hotplug support later.
	 * Xen PV guests don't support CPU0 hotplug at all.
	 */
	if (c->x86_vendor != X86_VENDOR_INTEL ||
	    boot_cpu_has(X86_FEATURE_XENPV))
		cpu0_hotpluggable = 0;

	/*
	 * Two known BSP/CPU0 dependencies: Resume from suspend/hibernate
	 * depends on BSP. PIC interrupts depend on BSP.
	 *
	 * If the BSP depencies are under control, one can tell kernel to
	 * enable BSP hotplug. This basically adds a control file and
	 * one can attempt to offline BSP.
	 */
	if (num == 0 && cpu0_hotpluggable) {
		unsigned int irq;
		/*
		 * We won't take down the boot processor on i386 if some
		 * interrupts only are able to be serviced by the BSP in PIC.
		 */
		for_each_active_irq(irq) {
			if (!IO_APIC_IRQ(irq) && irq_has_action(irq)) {
				cpu0_hotpluggable = 0;
				break;
			}
		}
	}
	if (num || cpu0_hotpluggable)
		per_cpu(cpu_devices, num).cpu.hotpluggable = 1;

	return register_cpu(&per_cpu(cpu_devices, num).cpu, num);
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

#ifdef CONFIG_NUMA
	for_each_online_node(i)
		register_one_node(i);
#endif

	for_each_present_cpu(i)
		arch_register_cpu(i);

	return 0;
}
subsys_initcall(topology_init);
