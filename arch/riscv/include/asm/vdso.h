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

#define __VDSO_PAGES    4

#ifndef __ASSEMBLER__
#include <generated/vdso-offsets.h>
#ifdef CONFIG_RISCV_USER_CFI
#include <generated/vdso-cfi-offsets.h>
#endif

#ifdef CONFIG_RISCV_USER_CFI
#define VDSO_SYMBOL(base, name)							\
	  (riscv_has_extension_unlikely(RISCV_ISA_EXT_ZIMOP) ?			\
	  (void __user *)((unsigned long)(base) + __vdso_##name##_cfi_offset) :	\
	  (void __user *)((unsigned long)(base) + __vdso_##name##_offset))
#else
#define VDSO_SYMBOL(base, name)							\
	  ((void __user *)((unsigned long)(base) + __vdso_##name##_offset))
#endif

#ifdef CONFIG_COMPAT
#include <generated/compat_vdso-offsets.h>

#define COMPAT_VDSO_SYMBOL(base, name)						\
	(void __user *)((unsigned long)(base) + compat__vdso_##name##_offset)

extern char compat_vdso_start[], compat_vdso_end[];

#endif /* CONFIG_COMPAT */

extern char vdso_start[], vdso_end[];
extern char vdso_cfi_start[], vdso_cfi_end[];

#endif /* !__ASSEMBLER__ */

#endif /* CONFIG_MMU */

#endif /* _ASM_RISCV_VDSO_H */
