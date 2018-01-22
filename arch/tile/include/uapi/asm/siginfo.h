/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_SIGINFO_H
#define _ASM_TILE_SIGINFO_H

#define __ARCH_SI_TRAPNO

#ifdef __LP64__
# define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#endif

#include <asm-generic/siginfo.h>

/*
 * Additional Tile-specific SIGILL si_codes
 */
#define ILL_DBLFLT	9	/* double fault */
#define ILL_HARDWALL	10	/* user networks hardwall violation */
#undef NSIGILL
#define NSIGILL		10

#endif /* _ASM_TILE_SIGINFO_H */
