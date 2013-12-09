#ifndef _LTTNG_WRAPPER_VMALLOC_H
#define _LTTNG_WRAPPER_VMALLOC_H

/*
 * wrapper/vmalloc.h
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

#ifdef CONFIG_KALLSYMS

#include <linux/kallsyms.h>
#include "kallsyms.h"

static inline
void wrapper_vmalloc_sync_all(void)
{
	void (*vmalloc_sync_all_sym)(void);

	vmalloc_sync_all_sym = (void *) kallsyms_lookup_funcptr("vmalloc_sync_all");
	if (vmalloc_sync_all_sym) {
		vmalloc_sync_all_sym();
	} else {
#ifdef CONFIG_X86
		/*
		 * Only x86 needs vmalloc_sync_all to make sure LTTng does not
		 * trigger recursive page faults.
		 */
		printk(KERN_WARNING "LTTng: vmalloc_sync_all symbol lookup failed.\n");
		printk(KERN_WARNING "Page fault handler and NMI tracing might trigger faults.\n");
#endif
	}
}
#else

#include <linux/vmalloc.h>

static inline
void wrapper_vmalloc_sync_all(void)
{
	return vmalloc_sync_all();
}
#endif

#endif /* _LTTNG_WRAPPER_VMALLOC_H */
