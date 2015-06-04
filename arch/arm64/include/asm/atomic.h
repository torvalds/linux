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
#define atomic_set(v, i)		(((v)->counter) = (i))
#define atomic_xchg(v, new)		xchg(&((v)->counter), (new))
#define atomic_cmpxchg(v, old, new)	cmpxchg(&((v)->counter), (old), (new))

#define atomic_inc(v)			atomic_add(1, (v))
#define atomic_dec(v)			atomic_sub(1, (v))
#define atomic_inc_return(v)		atomic_add_return(1, (v))
#define atomic_dec_return(v)		atomic_sub_return(1, (v))
#define atomic_inc_and_test(v)		(atomic_inc_return(v) == 0)
#define atomic_dec_and_test(v)		(atomic_dec_return(v) == 0)
#define atomic_sub_and_test(i, v)	(atomic_sub_return((i), (v)) == 0)
#define atomic_add_negative(i, v)	(atomic_add_return((i), (v)) < 0)
#define __atomic_add_unless(v, a, u)	___atomic_add_unless(v, a, u,)
#define atomic_andnot			atomic_andnot

/*
 * 64-bit atomic operations.
 */
#define ATOMIC64_INIT			ATOMIC_INIT
#define atomic64_read			atomic_read
#define atomic64_set			atomic_set
#define atomic64_xchg			atomic_xchg
#define atomic64_cmpxchg		atomic_cmpxchg

#define atomic64_inc(v)			atomic64_add(1, (v))
#define atomic64_dec(v)			atomic64_sub(1, (v))
#define atomic64_inc_return(v)		atomic64_add_return(1, (v))
#define atomic64_dec_return(v)		atomic64_sub_return(1, (v))
#define atomic64_inc_and_test(v)	(atomic64_inc_return(v) == 0)
#define atomic64_dec_and_test(v)	(atomic64_dec_return(v) == 0)
#define atomic64_sub_and_test(i, v)	(atomic64_sub_return((i), (v)) == 0)
#define atomic64_add_negative(i, v)	(atomic64_add_return((i), (v)) < 0)
#define atomic64_add_unless(v, a, u)	(___atomic_add_unless(v, a, u, 64) != u)
#define atomic64_andnot			atomic64_andnot

#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1, 0)

#endif
#endif
