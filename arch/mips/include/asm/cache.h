/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 98, 99, 2000, 2003 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_CACHE_H
#define _ASM_CACHE_H

#include <kmalloc.h>

#define L1_CACHE_SHIFT		CONFIG_MIPS_L1_CACHE_SHIFT
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

#define __read_mostly __section(".data..read_mostly")

extern void cache_noop(void);
extern void r3k_cache_init(void);
extern unsigned long r3k_cache_size(unsigned long);
extern unsigned long r3k_cache_lsize(unsigned long);
extern void r4k_cache_init(void);
extern void octeon_cache_init(void);
extern void au1x00_fixup_config_od(void);

#endif /* _ASM_CACHE_H */
