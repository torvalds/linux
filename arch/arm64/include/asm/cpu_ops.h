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
 */
struct cpu_operations {
	const char	*name;
	int		(*cpu_init)(struct device_node *, unsigned int);
	int		(*cpu_prepare)(unsigned int);
	int		(*cpu_boot)(unsigned int);
	void		(*cpu_postboot)(void);
};

extern const struct cpu_operations *cpu_ops[NR_CPUS];
extern const struct cpu_operations * __init cpu_get_ops(const char *name);

#endif /* ifndef __ASM_CPU_OPS_H */
