/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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
#ifndef _UAPI_ASM_TYPES_H
#define _UAPI_ASM_TYPES_H

/*
 * We don't use int-l64.h for the kernel anymore but still use it for
 * userspace to avoid code changes.
 *
 * However, some user programs (e.g. perf) may not want this. They can
 * flag __SANE_USERSPACE_TYPES__ to get int-ll64.h here.
 */
#ifndef __KERNEL__
# if _MIPS_SZLONG == 64 && !defined(__SANE_USERSPACE_TYPES__)
#  include <asm-generic/int-l64.h>
# else
#  include <asm-generic/int-ll64.h>
# endif
#endif


#endif /* _UAPI_ASM_TYPES_H */
