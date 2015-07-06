/*
 * Architecture specific mm hooks
 *
 * Copyright (C) 2015, IBM Corporation
 * Author: Laurent Dufour <ldufour@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_POWERPC_MM_ARCH_HOOKS_H
#define _ASM_POWERPC_MM_ARCH_HOOKS_H

static inline void arch_remap(struct mm_struct *mm,
			      unsigned long old_start, unsigned long old_end,
			      unsigned long new_start, unsigned long new_end)
{
	/*
	 * mremap() doesn't allow moving multiple vmas so we can limit the
	 * check to old_start == vdso_base.
	 */
	if (old_start == mm->context.vdso_base)
		mm->context.vdso_base = new_start;
}
#define arch_remap arch_remap

#endif /* _ASM_POWERPC_MM_ARCH_HOOKS_H */
