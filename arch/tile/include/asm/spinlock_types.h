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

#ifndef _ASM_TILE_SPINLOCK_TYPES_H
#define _ASM_TILE_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

#ifdef __tilegx__

/* Low 15 bits are "next"; high 15 bits are "current". */
typedef struct arch_spinlock {
	unsigned int lock;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 }

/* High bit is "writer owns"; low 31 bits are a count of readers. */
typedef struct arch_rwlock {
	unsigned int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ 0 }

#else

typedef struct arch_spinlock {
	/* Next ticket number to hand out. */
	int next_ticket;
	/* The ticket number that currently owns this lock. */
	int current_ticket;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0, 0 }

/*
 * Byte 0 for tns (only the low bit is used), byte 1 for ticket-lock "next",
 * byte 2 for ticket-lock "current", byte 3 for reader count.
 */
typedef struct arch_rwlock {
	unsigned int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ 0 }

#endif
#endif /* _ASM_TILE_SPINLOCK_TYPES_H */
