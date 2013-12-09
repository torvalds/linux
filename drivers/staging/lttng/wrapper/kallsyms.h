#ifndef _LTTNG_WRAPPER_KALLSYMS_H
#define _LTTNG_WRAPPER_KALLSYMS_H

/*
 * wrapper/kallsyms.h
 *
 * wrapper around kallsyms_lookup_name. Implements arch-dependent code for
 * arches where the address of the start of the function body is different
 * from the pointer which can be used to call the function, e.g. ARM THUMB2.
 *
 * Copyright (C) 2011 Avik Sil (avik.sil@linaro.org)
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

#include <linux/kallsyms.h>

static inline
unsigned long kallsyms_lookup_funcptr(const char *name)
{
	unsigned long addr;

	addr = kallsyms_lookup_name(name);
#ifdef CONFIG_ARM
#ifdef CONFIG_THUMB2_KERNEL
	if (addr)
		addr |= 1; /* set bit 0 in address for thumb mode */
#endif
#endif
	return addr;
}

static inline
unsigned long kallsyms_lookup_dataptr(const char *name)
{
	return kallsyms_lookup_name(name);
}
#endif /* _LTTNG_WRAPPER_KALLSYMS_H */
