/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1999 by Ralf Baechle
 * Copyright (C) 2008 Wind River Systems,
 *   written by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_TYPES_H
#define _ASM_TYPES_H

# include <asm-generic/int-ll64.h>
#include <uapi/asm/types.h>

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifndef __ASSEMBLY__

/*
 * Don't use phys_t.  You've been warned.
 */
#ifdef CONFIG_64BIT_PHYS_ADDR
typedef unsigned long long phys_t;
#else
typedef unsigned long phys_t;
#endif

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TYPES_H */
