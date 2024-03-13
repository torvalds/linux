/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * X86 specific ACPICA environments and implementation
 *
 * Copyright (C) 2014, Intel Corporation
 *   Author: Lv Zheng <lv.zheng@intel.com>
 */

#ifndef _ASM_X86_ACENV_H
#define _ASM_X86_ACENV_H

#include <asm/special_insns.h>

/* Asm macros */

/*
 * ACPI_FLUSH_CPU_CACHE() flushes caches on entering sleep states.
 * It is required to prevent data loss.
 *
 * While running inside virtual machine, the kernel can bypass cache flushing.
 * Changing sleep state in a virtual machine doesn't affect the host system
 * sleep state and cannot lead to data loss.
 */
#define ACPI_FLUSH_CPU_CACHE()					\
do {								\
	if (!cpu_feature_enabled(X86_FEATURE_HYPERVISOR))	\
		wbinvd();					\
} while (0)

int __acpi_acquire_global_lock(unsigned int *lock);
int __acpi_release_global_lock(unsigned int *lock);

#define ACPI_ACQUIRE_GLOBAL_LOCK(facs, Acq) \
	((Acq) = __acpi_acquire_global_lock(&facs->global_lock))

#define ACPI_RELEASE_GLOBAL_LOCK(facs, Acq) \
	((Acq) = __acpi_release_global_lock(&facs->global_lock))

/*
 * Math helper asm macros
 */
#define ACPI_DIV_64_BY_32(n_hi, n_lo, d32, q32, r32) \
	asm("divl %2;"				     \
	    : "=a"(q32), "=d"(r32)		     \
	    : "r"(d32),				     \
	     "0"(n_lo), "1"(n_hi))

#define ACPI_SHIFT_RIGHT_64(n_hi, n_lo) \
	asm("shrl   $1,%2	;"	\
	    "rcrl   $1,%3;"		\
	    : "=r"(n_hi), "=r"(n_lo)	\
	    : "0"(n_hi), "1"(n_lo))

#endif /* _ASM_X86_ACENV_H */
