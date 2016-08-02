/*
 * Copyright (c) 2012 Linaro Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef VIRT_H
#define VIRT_H

#include <asm/ptrace.h>

/*
 * Flag indicating that the kernel was not entered in the same mode on every
 * CPU.  The zImage loader stashes this value in an SPSR, so we need an
 * architecturally defined flag bit here.
 */
#define BOOT_CPU_MODE_MISMATCH	PSR_N_BIT

#ifndef __ASSEMBLY__
#include <asm/cacheflush.h>

#ifdef CONFIG_ARM_VIRT_EXT
/*
 * __boot_cpu_mode records what mode the primary CPU was booted in.
 * A correctly-implemented bootloader must start all CPUs in the same mode:
 * if it fails to do this, the flag BOOT_CPU_MODE_MISMATCH is set to indicate
 * that some CPU(s) were booted in a different mode.
 *
 * This allows the kernel to flag an error when the secondaries have come up.
 */
extern int __boot_cpu_mode;

static inline void sync_boot_mode(void)
{
	/*
	 * As secondaries write to __boot_cpu_mode with caches disabled, we
	 * must flush the corresponding cache entries to ensure the visibility
	 * of their writes.
	 */
	sync_cache_r(&__boot_cpu_mode);
}

void __hyp_set_vectors(unsigned long phys_vector_base);
unsigned long __hyp_get_vectors(void);
#else
#define __boot_cpu_mode	(SVC_MODE)
#define sync_boot_mode()
#endif

#ifndef ZIMAGE
void hyp_mode_check(void);

/* Reports the availability of HYP mode */
static inline bool is_hyp_mode_available(void)
{
	return ((__boot_cpu_mode & MODE_MASK) == HYP_MODE &&
		!(__boot_cpu_mode & BOOT_CPU_MODE_MISMATCH));
}

/* Check if the bootloader has booted CPUs in different modes */
static inline bool is_hyp_mode_mismatched(void)
{
	return !!(__boot_cpu_mode & BOOT_CPU_MODE_MISMATCH);
}

static inline bool is_kernel_in_hyp_mode(void)
{
	return false;
}

/* The section containing the hypervisor idmap text */
extern char __hyp_idmap_text_start[];
extern char __hyp_idmap_text_end[];

/* The section containing the hypervisor text */
extern char __hyp_text_start[];
extern char __hyp_text_end[];
#endif

#endif /* __ASSEMBLY__ */

#endif /* ! VIRT_H */
