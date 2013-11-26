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

#ifndef _ASM_TILE_UNALIGNED_H
#define _ASM_TILE_UNALIGNED_H

/*
 * We could implement faster get_unaligned_[be/le]64 using the ldna
 * instruction on tilegx; however, we need to either copy all of the
 * other generic functions to here (which is pretty ugly) or else
 * modify both the generic code and other arch code to allow arch
 * specific unaligned data access functions.  Given these functions
 * are not often called, we'll stick with the generic version.
 */
#include <asm-generic/unaligned.h>

/*
 * Is the kernel doing fixups of unaligned accesses?  If <0, no kernel
 * intervention occurs and SIGBUS is delivered with no data address
 * info.  If 0, the kernel single-steps the instruction to discover
 * the data address to provide with the SIGBUS.  If 1, the kernel does
 * a fixup.
 */
extern int unaligned_fixup;

/* Is the kernel printing on each unaligned fixup? */
extern int unaligned_printk;

/* Number of unaligned fixups performed */
extern unsigned int unaligned_fixup_count;

#endif /* _ASM_TILE_UNALIGNED_H */
