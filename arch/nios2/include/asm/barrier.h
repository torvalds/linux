/*
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_BARRIER_H
#define _ASM_NIOS2_BARRIER_H

#include <asm/cmpxchg.h>

#define nop()			(asm volatile ("nop" : : ))

#define mb()			barrier()
#define rmb()			mb()
#define wmb()			mb()

#define set_mb(var, value)	((void) xchg(&var, value))

#define smp_mb()		mb()
#define smp_rmb()		rmb()
#define smp_wmb()		wmb()

#define read_barrier_depends()		do { } while (0)
#define smp_read_barrier_depends()	do { } while (0)

#endif /* _ASM_NIOS2_BARRIER_H */
