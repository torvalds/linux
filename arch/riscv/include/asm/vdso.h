/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Limited
 * Copyright (C) 2014 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_VDSO_H
#define _ASM_RISCV_VDSO_H

/*
 * All systems with an MMU have a VDSO, but systems without an MMU don't
 * support shared libraries and therefore don't have one.
 */
#ifdef CONFIG_MMU

#define __VVAR_PAGES    2

#ifndef __ASSEMBLY__
#include <generated/vdso-offsets.h>

#define VDSO_SYMBOL(base, name)							\
	(void __user *)((unsigned long)(base) + __vdso_##name##_offset)

#ifdef CONFIG_COMPAT
#include <generated/compat_vdso-offsets.h>

#define COMPAT_VDSO_SYMBOL(base, name)						\
	(void __user *)((unsigned long)(base) + compat__vdso_##name##_offset)

#endif /* CONFIG_COMPAT */

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_MMU */

#endif /* _ASM_RISCV_VDSO_H */
