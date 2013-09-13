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
#ifndef __ASM_BITOPS_H
#define __ASM_BITOPS_H

#include <linux/compiler.h>

#include <asm/barrier.h>

/*
 * clear_bit may not imply a memory barrier
 */
#ifndef smp_mb__before_clear_bit
#define smp_mb__before_clear_bit()	smp_mb()
#define smp_mb__after_clear_bit()	smp_mb()
#endif

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

/*
 * Little endian assembly atomic bitops.
 */
extern void set_bit(int nr, volatile unsigned long *p);
extern void clear_bit(int nr, volatile unsigned long *p);
extern void change_bit(int nr, volatile unsigned long *p);
extern int test_and_set_bit(int nr, volatile unsigned long *p);
extern int test_and_clear_bit(int nr, volatile unsigned long *p);
extern int test_and_change_bit(int nr, volatile unsigned long *p);

#include <asm-generic/bitops/builtin-__ffs.h>
#include <asm-generic/bitops/builtin-ffs.h>
#include <asm-generic/bitops/builtin-__fls.h>
#include <asm-generic/bitops/builtin-fls.h>

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/find.h>

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#include <asm-generic/bitops/non-atomic.h>
#include <asm-generic/bitops/le.h>

/*
 * Ext2 is defined to use little-endian byte ordering.
 */
#define ext2_set_bit_atomic(lock, nr, p)	test_and_set_bit_le(nr, p)
#define ext2_clear_bit_atomic(lock, nr, p)	test_and_clear_bit_le(nr, p)

#endif /* __ASM_BITOPS_H */
