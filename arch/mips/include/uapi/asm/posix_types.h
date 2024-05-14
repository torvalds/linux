/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 98, 99, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_POSIX_TYPES_H
#define _ASM_POSIX_TYPES_H

#include <asm/sgidefs.h>

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef long		__kernel_daddr_t;
#define __kernel_daddr_t __kernel_daddr_t

#include <asm-generic/posix_types.h>

#endif /* _ASM_POSIX_TYPES_H */
