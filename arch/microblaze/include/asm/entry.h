/*
 * Definitions used by low-level trap handlers
 *
 * Copyright (C) 2008 Michal Simek
 * Copyright (C) 2007 - 2008 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_MICROBLAZE_ENTRY_H
#define _ASM_MICROBLAZE_ENTRY_H

#include <asm/percpu.h>
#include <asm/ptrace.h>

/*
 * These are per-cpu variables required in entry.S, among other
 * places
 */

#define PER_CPU(var) per_cpu__##var

# ifndef __ASSEMBLY__
DECLARE_PER_CPU(unsigned int, KSP); /* Saved kernel stack pointer */
DECLARE_PER_CPU(unsigned int, KM); /* Kernel/user mode */
DECLARE_PER_CPU(unsigned int, ENTRY_SP); /* Saved SP on kernel entry */
DECLARE_PER_CPU(unsigned int, R11_SAVE); /* Temp variable for entry */
DECLARE_PER_CPU(unsigned int, CURRENT_SAVE); /* Saved current pointer */
# endif /* __ASSEMBLY__ */

/* noMMU hasn't any space for args */
# define STATE_SAVE_ARG_SPACE	(0)

#endif /* _ASM_MICROBLAZE_ENTRY_H */
