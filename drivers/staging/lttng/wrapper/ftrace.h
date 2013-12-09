#ifndef _LTTNG_WRAPPER_FTRACE_H
#define _LTTNG_WRAPPER_FTRACE_H

/*
 * wrapper/ftrace.h
 *
 * wrapper around vmalloc_sync_all. Using KALLSYMS to get its address when
 * available, else we need to have a kernel that exports this function to GPL
 * modules.
 *
 * Copyright (C) 2011-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/ftrace.h>

#ifdef CONFIG_KALLSYMS

#include <linux/kallsyms.h>
#include "kallsyms.h"

static inline
int wrapper_register_ftrace_function_probe(char *glob,
		struct ftrace_probe_ops *ops, void *data)
{
	int (*register_ftrace_function_probe_sym)(char *glob,
			struct ftrace_probe_ops *ops, void *data);

	register_ftrace_function_probe_sym = (void *) kallsyms_lookup_funcptr("register_ftrace_function_probe");
	if (register_ftrace_function_probe_sym) {
		return register_ftrace_function_probe_sym(glob, ops, data);
	} else {
		printk(KERN_WARNING "LTTng: register_ftrace_function_probe symbol lookup failed.\n");
		return -EINVAL;
	}
}

static inline
void wrapper_unregister_ftrace_function_probe(char *glob,
		struct ftrace_probe_ops *ops, void *data)
{
	void (*unregister_ftrace_function_probe_sym)(char *glob,
			struct ftrace_probe_ops *ops, void *data);

	unregister_ftrace_function_probe_sym = (void *) kallsyms_lookup_funcptr("unregister_ftrace_function_probe");
	if (unregister_ftrace_function_probe_sym) {
		unregister_ftrace_function_probe_sym(glob, ops, data);
	} else {
		printk(KERN_WARNING "LTTng: unregister_ftrace_function_probe symbol lookup failed.\n");
		WARN_ON(1);
	}
}

#else

static inline
int wrapper_register_ftrace_function_probe(char *glob,
		struct ftrace_probe_ops *ops, void *data)
{
	return register_ftrace_function_probe(glob, ops, data);
}

static inline
void wrapper_unregister_ftrace_function_probe(char *glob,
		struct ftrace_probe_ops *ops, void *data)
{
	return unregister_ftrace_function_probe(glob, ops, data);
}
#endif

#endif /* _LTTNG_WRAPPER_FTRACE_H */
