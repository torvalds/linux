/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IA64 specific ACPICA environments and implementation
 *
 * Copyright (C) 2014, Intel Corporation
 *   Author: Lv Zheng <lv.zheng@intel.com>
 */

#ifndef _ASM_IA64_ACENV_H
#define _ASM_IA64_ACENV_H

#include <asm/intrinsics.h>

#define COMPILER_DEPENDENT_INT64	long
#define COMPILER_DEPENDENT_UINT64	unsigned long

/* Asm macros */

static inline int
ia64_acpi_acquire_global_lock(unsigned int *lock)
{
	unsigned int old, new, val;
	do {
		old = *lock;
		new = (((old & ~0x3) + 2) + ((old >> 1) & 0x1));
		val = ia64_cmpxchg4_acq(lock, new, old);
	} while (unlikely (val != old));
	return (new < 3) ? -1 : 0;
}

static inline int
ia64_acpi_release_global_lock(unsigned int *lock)
{
	unsigned int old, new, val;
	do {
		old = *lock;
		new = old & ~0x3;
		val = ia64_cmpxchg4_acq(lock, new, old);
	} while (unlikely (val != old));
	return old & 0x1;
}

#define ACPI_ACQUIRE_GLOBAL_LOCK(facs, Acq)				\
	((Acq) = ia64_acpi_acquire_global_lock(&facs->global_lock))

#define ACPI_RELEASE_GLOBAL_LOCK(facs, Acq)				\
	((Acq) = ia64_acpi_release_global_lock(&facs->global_lock))

#endif /* _ASM_IA64_ACENV_H */
