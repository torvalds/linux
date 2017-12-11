/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_SPINLOCK_TYPES_H
#define __ASM_SPINLOCK_TYPES_H

#if !defined(__LINUX_SPINLOCK_TYPES_H) && !defined(__ASM_SPINLOCK_H)
# error "please don't include this file directly"
#endif

#include <linux/types.h>

#define TICKET_SHIFT	16

typedef struct {
#ifdef __AARCH64EB__
	u16 next;
	u16 owner;
#else
	u16 owner;
	u16 next;
#endif
} __aligned(4) arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 , 0 }

#include <asm-generic/qrwlock_types.h>

#endif
