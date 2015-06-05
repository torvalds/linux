/*
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
#ifndef __ASM_CPU_OPS_H
#define __ASM_CPU_OPS_H

#include <linux/init.h>
#include <linux/threads.h>

struct device_node;

/**
 * struct cpu_operations - Callback operations for hotplugging CPUs.
 *
 * @name:	Name of the property as appears in a devicetree cpu node's
 *		enable-method property.
 * @cpu_init:	Reads any data necessary for a specific enable-method from the
 *		devicetree, for a given cpu node and proposed logical id.
 * @cpu_prepare: Early one-time preparation step for a cpu. If there is a
 *		mechanism for doing so, tests whether it is possible to boot
 *		the given CPU.
 * @cpu_boot:	Boots a cpu into the kernel.
 * @cpu_postboot: Optionally, perform any post-boot cleanup or necesary
 *		synchronisation. Called from the cpu being booted.
 * @cpu_disable: Prepares a cpu to die. May fail for some mechanism-specific
 * 		reason, which will cause the hot unplug to be aborted. Called
 * 		from the cpu to be killed.
 * @cpu_die:	Makes a cpu leave the kernel. Must not fail. Called from the
 *		cpu being killed.
 * @cpu_kill:  Ensures a cpu has left the kernel. Called from another cpu.
 * @cpu_init_idle: Reads any data necessary to initialize CPU idle states from
 *		devicetree, for a given cpu node and proposed logical id.
 * @cpu_suspend: Suspends a cpu and saves the required context. May fail owing
 *               to wrong parameters or error conditions. Called from the
 *               CPU being suspended. Must be called with IRQs disabled.
 */
struct cpu_operations {
	const char	*name;
	int		(*cpu_init)(struct device_node *, unsigned int);
	int		(*cpu_prepare)(unsigned int);
	int		(*cpu_boot)(unsigned int);
	void		(*cpu_postboot)(void);
#ifdef CONFIG_HOTPLUG_CPU
	int		(*cpu_disable)(unsigned int cpu);
	void		(*cpu_die)(unsigned int cpu);
	int		(*cpu_kill)(unsigned int cpu);
#endif
#ifdef CONFIG_CPU_IDLE
	int		(*cpu_init_idle)(struct device_node *, unsigned int);
	int		(*cpu_suspend)(unsigned long);
#endif
};

extern const struct cpu_operations *cpu_ops[NR_CPUS];
int __init cpu_read_ops(struct device_node *dn, int cpu);
void __init cpu_read_bootcpu_ops(void);
const struct cpu_operations *cpu_get_ops(const char *name);

#endif /* ifndef __ASM_CPU_OPS_H */
