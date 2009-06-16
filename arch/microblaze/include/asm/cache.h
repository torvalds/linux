/*
 * Cache operations
 *
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2003 John Williams <jwilliams@itee.uq.edu.au>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_MICROBLAZE_CACHE_H
#define _ASM_MICROBLAZE_CACHE_H

#include <asm/registers.h>

#define L1_CACHE_SHIFT	2
/* word-granular cache in microblaze */
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)

#define SMP_CACHE_BYTES	L1_CACHE_BYTES

void _enable_icache(void);
void _disable_icache(void);
void _invalidate_icache(unsigned int addr);

#define __enable_icache()		_enable_icache()
#define __disable_icache()		_disable_icache()
#define __invalidate_icache(addr)	_invalidate_icache(addr)

void _enable_dcache(void);
void _disable_dcache(void);
void _invalidate_dcache(unsigned int addr);

#define __enable_dcache()		_enable_dcache()
#define __disable_dcache()		_disable_dcache()
#define __invalidate_dcache(addr)	_invalidate_dcache(addr)

#endif /* _ASM_MICROBLAZE_CACHE_H */
