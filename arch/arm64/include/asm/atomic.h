/*
 * Based on arch/arm/include/asm/atomic.h
 *
 * Copyright (C) 1996 Russell King.
 * Copyright (C) 2002 Deep Blue Solutions Ltd.
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
#ifndef __ASM_ATOMIC_H
#define __ASM_ATOMIC_H

#include <linux/compiler.h>
#include <linux/types.h>

#include <asm/barrier.h>
#include <asm/lse.h>

#ifdef __KERNEL__

#define __ARM64_IN_ATOMIC_IMPL

#if defined(CONFIG_ARM64_LSE_ATOMICS) && defined(CONFIG_AS_LSE)
#include <asm/atomic_lse.h>
#else
#include <asm/atomic_ll_sc.h>
#endif

#undef __ARM64_IN_ATOMIC_IMPL

#include <asm/cmpxchg.h>

#define ___atomic_add_unless(v, a, u, sfx)				\
({									\
	typeof((v)->counter) c, old;					\
									\
	c = atomic##sfx##_read(v);					\
	while (c != (u) &&						\
	      (old = atomic##sfx##_cmpxchg((v), c, c + (a))) != c)	\
		c = old;						\
	c;								\
 })

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)			READ_ONCE((v)->counter)
#define atomic_set(v, i)		WRITE_ONCE(((v)->counter), (i))

#define atomic_add_return_relaxed	atomic_add_return_relaxed
#define atomic_add_return_acquire	atomic_add_return_acquire
#define atomic_add_return_release	atomic_add_return_release
#define atomic_add_return		atomic_add_return

#define atomic_inc_return_relaxed(v)	atomic_add_return_relaxed(1, (v))
#define atomic_inc_return_acquire(v)	atomic_add_return_acquire(1, (v))
#define atomic_inc_return_release(v)	atomic_add_return_release(1, (v))
#define atomic_inc_return(v)		atomic_add_return(1, (v))

#define atomic_sub_return_relaxed	atomic_sub_return_relaxed
#define atomic_sub_return_acquire	atomic_sub_return_acquire
#define atomic_sub_return_release	atomic_sub_return_release
#define atomic_sub_return		atomic_sub_return

#define atomic_dec_return_relaxed(v)	atomic_sub_return_relaxed(1, (v))
#define atomic_dec_return_acquire(v)	atomic_sub_return_acquire(1, (v))
#define atomic_dec_return_release(v)	atomic_sub_return_release(1, (v))
#define atomic_dec_return(v)		atomic_sub_return(1, (v))

#define atomic_fetch_add_relaxed	atomic_fetch_add_relaxed
#define atomic_fetch_add_acquire	atomic_fetch_add_acquire
#define atomic_fetch_add_release	atomic_fetch_add_release
#define atomic_fetch_add		atomic_fetch_add

#define atomic_fetch_sub_relaxed	atomic_fetch_sub_relaxed
#define atomic_fetch_sub_acquire	atomic_fetch_sub_acquire
#define atomic_fetch_sub_release	atomic_fetch_sub_release
#define atomic_fetch_sub		atomic_fetch_sub

#define atomic_fetch_and_relaxed	atomic_fetch_and_relaxed
#define atomic_fetch_and_acquire	atomic_fetch_and_acquire
#define atomic_fetch_and_release	atomic_fetch_and_release
#define atomic_fetch_and		atomic_fetch_and

#define atomic_fetch_andnot_relaxed	atomic_fetch_andnot_relaxed
#define atomic_fetch_andnot_acquire	atomic_fetch_andnot_acquire
#define atomic_fetch_andnot_release	atomic_fetch_andnot_release
#define atomic_fetch_andnot		atomic_fetch_andnot

#define atomic_fetch_or_relaxed		atomic_fetch_or_relaxed
#define atomic_fetch_or_acquire		atomic_fetch_or_acquire
#define atomic_fetch_or_release		atomic_fetch_or_release
#define atomic_fetch_or			atomic_fetch_or

#define atomic_fetch_xor_relaxed	atomic_fetch_xor_relaxed
#define atomic_fetch_xor_acquire	atomic_fetch_xor_acquire
#define atomic_fetch_xor_release	atomic_fetch_xor_release
#define atomic_fetch_xor		atomic_fetch_xor

#define atomic_xchg_relaxed(v, new)	xchg_relaxed(&((v)->counter), (new))
#define atomic_xchg_acquire(v, new)	xchg_acquire(&((v)->counter), (new))
#define atomic_xchg_release(v, new)	xchg_release(&((v)->counter), (new))
#define atomic_xchg(v, new)		xchg(&((v)->counter), (new))

#define atomic_cmpxchg_relaxed(v, old, new)				\
	cmpxchg_relaxed(&((v)->counter), (old), (new))
#define atomic_cmpxchg_acquire(v, old, new)				\
	cmpxchg_acquire(&((v)->counter), (old), (new))
#define atomic_cmpxchg_release(v, old, new)				\
	cmpxchg_release(&((v)->counter), (old), (new))
#define atomic_cmpxchg(v, old, new)	cmpxchg(&((v)->counter), (old), (new))

#define atomic_inc(v)			atomic_add(1, (v))
#define atomic_dec(v)			atomic_sub(1, (v))
#define atomic_inc_and_test(v)		(atomic_inc_return(v) == 0)
#define atomic_dec_and_test(v)		(atomic_dec_return(v) == 0)
#define atomic_sub_and_test(i, v)	(atomic_sub_return((i), (v)) == 0)
#define atomic_add_negative(i, v)	(atomic_add_return((i), (v)) < 0)
#define __atomic_add_unless(v, a, u)	___atomic_add_unless(v, a, u,)
#define atomic_andnot			atomic_andnot

#define atomic_fetch_or atomic_fetch_or

/*
 * 64-bit atomic operations.
 */
#define ATOMIC64_INIT			ATOMIC_INIT
#define atomic64_read			atomic_read
#define atomic64_set			atomic_set

#define atomic64_add_return_relaxed	atomic64_add_return_relaxed
#define atomic64_add_return_acquire	atomic64_add_return_acquire
#define atomic64_add_return_release	atomic64_add_return_release
#define atomic64_add_return		atomic64_add_return

#define atomic64_inc_return_relaxed(v)	atomic64_add_return_relaxed(1, (v))
#define atomic64_inc_return_acquire(v)	atomic64_add_return_acquire(1, (v))
#define atomic64_inc_return_release(v)	atomic64_add_return_release(1, (v))
#define atomic64_inc_return(v)		atomic64_add_return(1, (v))

#define atomic64_sub_return_relaxed	atomic64_sub_return_relaxed
#define atomic64_sub_return_acquire	atomic64_sub_return_acquire
#define atomic64_sub_return_release	atomic64_sub_return_release
#define atomic64_sub_return		atomic64_sub_return

#define atomic64_dec_return_relaxed(v)	atomic64_sub_return_relaxed(1, (v))
#define atomic64_dec_return_acquire(v)	atomic64_sub_return_acquire(1, (v))
#define atomic64_dec_return_release(v)	atomic64_sub_return_release(1, (v))
#define atomic64_dec_return(v)		atomic64_sub_return(1, (v))

#define atomic64_fetch_add_relaxed	atomic64_fetch_add_relaxed
#define atomic64_fetch_add_acquire	atomic64_fetch_add_acquire
#define atomic64_fetch_add_release	atomic64_fetch_add_release
#define atomic64_fetch_add		atomic64_fetch_add

#define atomic64_fetch_sub_relaxed	atomic64_fetch_sub_relaxed
#define atomic64_fetch_sub_acquire	atomic64_fetch_sub_acquire
#define atomic64_fetch_sub_release	atomic64_fetch_sub_release
#define atomic64_fetch_sub		atomic64_fetch_sub

#define atomic64_fetch_and_relaxed	atomic64_fetch_and_relaxed
#define atomic64_fetch_and_acquire	atomic64_fetch_and_acquire
#define atomic64_fetch_and_release	atomic64_fetch_and_release
#define atomic64_fetch_and		atomic64_fetch_and

#define atomic64_fetch_andnot_relaxed	atomic64_fetch_andnot_relaxed
#define atomic64_fetch_andnot_acquire	atomic64_fetch_andnot_acquire
#define atomic64_fetch_andnot_release	atomic64_fetch_andnot_release
#define atomic64_fetch_andnot		atomic64_fetch_andnot

#define atomic64_fetch_or_relaxed	atomic64_fetch_or_relaxed
#define atomic64_fetch_or_acquire	atomic64_fetch_or_acquire
#define atomic64_fetch_or_release	atomic64_fetch_or_release
#define atomic64_fetch_or		atomic64_fetch_or

#define atomic64_fetch_xor_relaxed	atomic64_fetch_xor_relaxed
#define atomic64_fetch_xor_acquire	atomic64_fetch_xor_acquire
#define atomic64_fetch_xor_release	atomic64_fetch_xor_release
#define atomic64_fetch_xor		atomic64_fetch_xor

#define atomic64_xchg_relaxed		atomic_xchg_relaxed
#define atomic64_xchg_acquire		atomic_xchg_acquire
#define atomic64_xchg_release		atomic_xchg_release
#define atomic64_xchg			atomic_xchg

#define atomic64_cmpxchg_relaxed	atomic_cmpxchg_relaxed
#define atomic64_cmpxchg_acquire	atomic_cmpxchg_acquire
#define atomic64_cmpxchg_release	atomic_cmpxchg_release
#define atomic64_cmpxchg		atomic_cmpxchg

#define atomic64_inc(v)			atomic64_add(1, (v))
#define atomic64_dec(v)			atomic64_sub(1, (v))
#define atomic64_inc_and_test(v)	(atomic64_inc_return(v) == 0)
#define atomic64_dec_and_test(v)	(atomic64_dec_return(v) == 0)
#define atomic64_sub_and_test(i, v)	(atomic64_sub_return((i), (v)) == 0)
#define atomic64_add_negative(i, v)	(atomic64_add_return((i), (v)) < 0)
#define atomic64_add_unless(v, a, u)	(___atomic_add_unless(v, a, u, 64) != u)
#define atomic64_andnot			atomic64_andnot

#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1, 0)

#endif
#endif
