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

struct cpu_operations {
	const char	*name;
	int		(*cpu_init)(struct device_node *, unsigned int);
	int		(*cpu_prepare)(unsigned int);
};

extern const struct cpu_operations *cpu_ops[NR_CPUS];
extern const struct cpu_operations * __init cpu_get_ops(const char *name);

#endif /* ifndef __ASM_CPU_OPS_H */
