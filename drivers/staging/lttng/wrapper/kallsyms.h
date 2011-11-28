#ifndef _LTT_WRAPPER_KALLSYMS_H
#define _LTT_WRAPPER_KALLSYMS_H

/*
 * Copyright (C) 2011 Avik Sil (avik.sil@linaro.org)
 *
 * wrapper around kallsyms_lookup_name. Implements arch-dependent code for
 * arches where the address of the start of the function body is different
 * from the pointer which can be used to call the function, e.g. ARM THUMB2.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

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
#endif /* _LTT_WRAPPER_KALLSYMS_H */
