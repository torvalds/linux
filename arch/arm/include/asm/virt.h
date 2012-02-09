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
 * architecturally defined flag bit here (the N flag, as it happens)
 */
#define BOOT_CPU_MODE_MISMATCH (1<<31)

#ifndef __ASSEMBLY__

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

void __hyp_set_vectors(unsigned long phys_vector_base);
unsigned long __hyp_get_vectors(void);
#else
#define __boot_cpu_mode	(SVC_MODE)
#endif

#endif /* __ASSEMBLY__ */

#endif /* ! VIRT_H */
