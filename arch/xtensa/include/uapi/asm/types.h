/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/asm-xtensa/types.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _UAPI_XTENSA_TYPES_H
#define _UAPI_XTENSA_TYPES_H

#include <asm-generic/int-ll64.h>

#ifdef __ASSEMBLER__
# define __XTENSA_UL(x)		(x)
# define __XTENSA_UL_CONST(x)	x
#else
# define __XTENSA_UL(x)		((unsigned long)(x))
# define ___XTENSA_UL_CONST(x)	x##UL
# define __XTENSA_UL_CONST(x)	___XTENSA_UL_CONST(x)
#endif

#ifndef __ASSEMBLER__

#endif

#endif /* _UAPI_XTENSA_TYPES_H */
