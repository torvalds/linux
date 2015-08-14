/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_SPINLOCK_TYPES_H
#define __ASM_SPINLOCK_TYPES_H

typedef struct {
	volatile unsigned int slock;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED__	0
#define __ARCH_SPIN_LOCK_LOCKED__	1

#define __ARCH_SPIN_LOCK_UNLOCKED	{ __ARCH_SPIN_LOCK_UNLOCKED__ }
#define __ARCH_SPIN_LOCK_LOCKED		{ __ARCH_SPIN_LOCK_LOCKED__ }

/*
 * Unlocked     : 0x0100_0000
 * Read lock(s) : 0x00FF_FFFF to 0x01  (Multiple Readers decrement it)
 * Write lock   : 0x0, but only if prior value is "unlocked" 0x0100_0000
 */
typedef struct {
	volatile unsigned int	counter;
#ifndef CONFIG_ARC_HAS_LLSC
	arch_spinlock_t		lock_mutex;
#endif
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED__	0x01000000
#define __ARCH_RW_LOCK_UNLOCKED		{ .counter = __ARCH_RW_LOCK_UNLOCKED__ }

#endif
