/*
 * include/asm-xtensa/spinlock.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_SPINLOCK_H
#define _XTENSA_SPINLOCK_H

#include <asm/barrier.h>
#include <asm/qrwlock.h>
#include <asm/qspinlock.h>

#define smp_mb__after_spinlock()	smp_mb()

#endif	/* _XTENSA_SPINLOCK_H */
