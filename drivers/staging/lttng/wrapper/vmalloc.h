#ifndef _LTT_WRAPPER_VMALLOC_H
#define _LTT_WRAPPER_VMALLOC_H

/*
 * Copyright (C) 2011 Mathieu Desnoyers (mathieu.desnoyers@efficios.com)
 *
 * wrapper around vmalloc_sync_all. Using KALLSYMS to get its address when
 * available, else we need to have a kernel that exports this function to GPL
 * modules.
 *
 * Dual LGPL v2.1/GPL v2 license.
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

#endif /* _LTT_WRAPPER_VMALLOC_H */
