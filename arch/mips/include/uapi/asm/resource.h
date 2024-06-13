/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 98, 99, 2000 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_RESOURCE_H
#define _ASM_RESOURCE_H


/*
 * These five resource limit IDs have a MIPS/Linux-specific ordering,
 * the rest comes from the generic header:
 */
#define RLIMIT_NOFILE		5	/* max number of open files */
#define RLIMIT_AS		6	/* address space limit */
#define RLIMIT_RSS		7	/* max resident set size */
#define RLIMIT_NPROC		8	/* max number of processes */
#define RLIMIT_MEMLOCK		9	/* max locked-in-memory address space */

/*
 * SuS says limits have to be unsigned.
 * Which makes a ton more sense anyway,
 * but we keep the old value on MIPS32,
 * for compatibility:
 */
#ifndef __mips64
# define RLIM_INFINITY		0x7fffffffUL
#endif

#include <asm-generic/resource.h>

#endif /* _ASM_RESOURCE_H */
