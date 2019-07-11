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

#define ATOMIC_INIT(i)	{ (i) }

#define arch_atomic_read(v)			READ_ONCE((v)->counter)
#define arch_atomic_set(v, i)			WRITE_ONCE(((v)->counter), (i))

#define arch_atomic_add_return_relaxed		arch_atomic_add_return_relaxed
#define arch_atomic_add_return_acquire		arch_atomic_add_return_acquire
#define arch_atomic_add_return_release		arch_atomic_add_return_release
#define arch_atomic_add_return			arch_atomic_add_return

#define arch_atomic_sub_return_relaxed		arch_atomic_sub_return_relaxed
#define arch_atomic_sub_return_acquire		arch_atomic_sub_return_acquire
#define arch_atomic_sub_return_release		arch_atomic_sub_return_release
#define arch_atomic_sub_return			arch_atomic_sub_return

#define arch_atomic_fetch_add_relaxed		arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_add_acquire		arch_atomic_fetch_add_acquire
#define arch_atomic_fetch_add_release		arch_atomic_fetch_add_release
#define arch_atomic_fetch_add			arch_atomic_fetch_add

#define arch_atomic_fetch_sub_relaxed		arch_atomic_fetch_sub_relaxed
#define arch_atomic_fetch_sub_acquire		arch_atomic_fetch_sub_acquire
#define arch_atomic_fetch_sub_release		arch_atomic_fetch_sub_release
#define arch_atomic_fetch_sub			arch_atomic_fetch_sub

#define arch_atomic_fetch_and_relaxed		arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_and_acquire		arch_atomic_fetch_and_acquire
#define arch_atomic_fetch_and_release		arch_atomic_fetch_and_release
#define arch_atomic_fetch_and			arch_atomic_fetch_and

#define arch_atomic_fetch_andnot_relaxed	arch_atomic_fetch_andnot_relaxed
#define arch_atomic_fetch_andnot_acquire	arch_atomic_fetch_andnot_acquire
#define arch_atomic_fetch_andnot_release	arch_atomic_fetch_andnot_release
#define arch_atomic_fetch_andnot		arch_atomic_fetch_andnot

#define arch_atomic_fetch_or_relaxed		arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_or_acquire		arch_atomic_fetch_or_acquire
#define arch_atomic_fetch_or_release		arch_atomic_fetch_or_release
#define arch_atomic_fetch_or			arch_atomic_fetch_or

#define arch_atomic_fetch_xor_relaxed		arch_atomic_fetch_xor_relaxed
#define arch_atomic_fetch_xor_acquire		arch_atomic_fetch_xor_acquire
#define arch_atomic_fetch_xor_release		arch_atomic_fetch_xor_release
#define arch_atomic_fetch_xor			arch_atomic_fetch_xor

#define arch_atomic_xchg_relaxed(v, new) \
	arch_xchg_relaxed(&((v)->counter), (new))
#define arch_atomic_xchg_acquire(v, new) \
	arch_xchg_acquire(&((v)->counter), (new))
#define arch_atomic_xchg_release(v, new) \
	arch_xchg_release(&((v)->counter), (new))
#define arch_atomic_xchg(v, new) \
	arch_xchg(&((v)->counter), (new))

#define arch_atomic_cmpxchg_relaxed(v, old, new) \
	arch_cmpxchg_relaxed(&((v)->counter), (old), (new))
#define arch_atomic_cmpxchg_acquire(v, old, new) \
	arch_cmpxchg_acquire(&((v)->counter), (old), (new))
#define arch_atomic_cmpxchg_release(v, old, new) \
	arch_cmpxchg_release(&((v)->counter), (old), (new))
#define arch_atomic_cmpxchg(v, old, new) \
	arch_cmpxchg(&((v)->counter), (old), (new))

#define arch_atomic_andnot			arch_atomic_andnot

/*
 * 64-bit arch_atomic operations.
 */
#define ATOMIC64_INIT				ATOMIC_INIT
#define arch_atomic64_read			arch_atomic_read
#define arch_atomic64_set			arch_atomic_set

#define arch_atomic64_add_return_relaxed	arch_atomic64_add_return_relaxed
#define arch_atomic64_add_return_acquire	arch_atomic64_add_return_acquire
#define arch_atomic64_add_return_release	arch_atomic64_add_return_release
#define arch_atomic64_add_return		arch_atomic64_add_return

#define arch_atomic64_sub_return_relaxed	arch_atomic64_sub_return_relaxed
#define arch_atomic64_sub_return_acquire	arch_atomic64_sub_return_acquire
#define arch_atomic64_sub_return_release	arch_atomic64_sub_return_release
#define arch_atomic64_sub_return		arch_atomic64_sub_return

#define arch_atomic64_fetch_add_relaxed		arch_atomic64_fetch_add_relaxed
#define arch_atomic64_fetch_add_acquire		arch_atomic64_fetch_add_acquire
#define arch_atomic64_fetch_add_release		arch_atomic64_fetch_add_release
#define arch_atomic64_fetch_add			arch_atomic64_fetch_add

#define arch_atomic64_fetch_sub_relaxed		arch_atomic64_fetch_sub_relaxed
#define arch_atomic64_fetch_sub_acquire		arch_atomic64_fetch_sub_acquire
#define arch_atomic64_fetch_sub_release		arch_atomic64_fetch_sub_release
#define arch_atomic64_fetch_sub			arch_atomic64_fetch_sub

#define arch_atomic64_fetch_and_relaxed		arch_atomic64_fetch_and_relaxed
#define arch_atomic64_fetch_and_acquire		arch_atomic64_fetch_and_acquire
#define arch_atomic64_fetch_and_release		arch_atomic64_fetch_and_release
#define arch_atomic64_fetch_and			arch_atomic64_fetch_and

#define arch_atomic64_fetch_andnot_relaxed	arch_atomic64_fetch_andnot_relaxed
#define arch_atomic64_fetch_andnot_acquire	arch_atomic64_fetch_andnot_acquire
#define arch_atomic64_fetch_andnot_release	arch_atomic64_fetch_andnot_release
#define arch_atomic64_fetch_andnot		arch_atomic64_fetch_andnot

#define arch_atomic64_fetch_or_relaxed		arch_atomic64_fetch_or_relaxed
#define arch_atomic64_fetch_or_acquire		arch_atomic64_fetch_or_acquire
#define arch_atomic64_fetch_or_release		arch_atomic64_fetch_or_release
#define arch_atomic64_fetch_or			arch_atomic64_fetch_or

#define arch_atomic64_fetch_xor_relaxed		arch_atomic64_fetch_xor_relaxed
#define arch_atomic64_fetch_xor_acquire		arch_atomic64_fetch_xor_acquire
#define arch_atomic64_fetch_xor_release		arch_atomic64_fetch_xor_release
#define arch_atomic64_fetch_xor			arch_atomic64_fetch_xor

#define arch_atomic64_xchg_relaxed		arch_atomic_xchg_relaxed
#define arch_atomic64_xchg_acquire		arch_atomic_xchg_acquire
#define arch_atomic64_xchg_release		arch_atomic_xchg_release
#define arch_atomic64_xchg			arch_atomic_xchg

#define arch_atomic64_cmpxchg_relaxed		arch_atomic_cmpxchg_relaxed
#define arch_atomic64_cmpxchg_acquire		arch_atomic_cmpxchg_acquire
#define arch_atomic64_cmpxchg_release		arch_atomic_cmpxchg_release
#define arch_atomic64_cmpxchg			arch_atomic_cmpxchg

#define arch_atomic64_andnot			arch_atomic64_andnot

#define arch_atomic64_dec_if_positive		arch_atomic64_dec_if_positive

#include <asm-generic/atomic-instrumented.h>

#endif
#endif
