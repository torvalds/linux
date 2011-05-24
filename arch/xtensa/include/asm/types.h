/*
 * include/asm-xtensa/types.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_TYPES_H
#define _XTENSA_TYPES_H

#include <asm-generic/int-ll64.h>

#ifdef __ASSEMBLY__
# define __XTENSA_UL(x)		(x)
# define __XTENSA_UL_CONST(x)	x
#else
# define __XTENSA_UL(x)		((unsigned long)(x))
# define __XTENSA_UL_CONST(x)	x##UL
#endif

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

#define BITS_PER_LONG 32

#endif	/* __KERNEL__ */
#endif

#endif	/* _XTENSA_TYPES_H */
