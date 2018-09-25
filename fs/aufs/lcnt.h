/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * simple long counter wrapper
 */

#ifndef __AUFS_LCNT_H__
#define __AUFS_LCNT_H__

#ifdef __KERNEL__

#include "debug.h"

#define AuLCntATOMIC	1
#define AuLCntPCPUCNT	2
/*
 * why does percpu_refcount require extra synchronize_rcu()s in
 * au_br_do_free()
 */
#define AuLCntPCPUREF	3

/* #define AuLCntChosen	AuLCntATOMIC */
#define AuLCntChosen	AuLCntPCPUCNT
/* #define AuLCntChosen	AuLCntPCPUREF */

#if AuLCntChosen == AuLCntATOMIC
#include <linux/atomic.h>

typedef atomic_long_t au_lcnt_t;

static inline int au_lcnt_init(au_lcnt_t *cnt, void *release __maybe_unused)
{
	atomic_long_set(cnt, 0);
	return 0;
}

static inline void au_lcnt_wait_for_fin(au_lcnt_t *cnt __maybe_unused)
{
	/* empty */
}

static inline void au_lcnt_fin(au_lcnt_t *cnt __maybe_unused,
			       int do_sync __maybe_unused)
{
	/* empty */
}

static inline void au_lcnt_inc(au_lcnt_t *cnt)
{
	atomic_long_inc(cnt);
}

static inline void au_lcnt_dec(au_lcnt_t *cnt)
{
	atomic_long_dec(cnt);
}

static inline long au_lcnt_read(au_lcnt_t *cnt, int do_rev __maybe_unused)
{
	return atomic_long_read(cnt);
}
#endif

#if AuLCntChosen == AuLCntPCPUCNT
#include <linux/percpu_counter.h>

typedef struct percpu_counter au_lcnt_t;

static inline int au_lcnt_init(au_lcnt_t *cnt, void *release __maybe_unused)
{
	return percpu_counter_init(cnt, 0, GFP_NOFS);
}

static inline void au_lcnt_wait_for_fin(au_lcnt_t *cnt __maybe_unused)
{
	/* empty */
}

static inline void au_lcnt_fin(au_lcnt_t *cnt, int do_sync __maybe_unused)
{
	percpu_counter_destroy(cnt);
}

static inline void au_lcnt_inc(au_lcnt_t *cnt)
{
	percpu_counter_inc(cnt);
}

static inline void au_lcnt_dec(au_lcnt_t *cnt)
{
	percpu_counter_dec(cnt);
}

static inline long au_lcnt_read(au_lcnt_t *cnt, int do_rev __maybe_unused)
{
	s64 n;

	n = percpu_counter_sum(cnt);
	BUG_ON(n < 0);
	if (LONG_MAX != LLONG_MAX
	    && n > LONG_MAX)
		AuWarn1("%s\n", "wrap-around");

	return n;
}
#endif

#if AuLCntChosen == AuLCntPCPUREF
#include <linux/percpu-refcount.h>

typedef struct percpu_ref au_lcnt_t;

static inline int au_lcnt_init(au_lcnt_t *cnt, percpu_ref_func_t *release)
{
	if (!release)
		release = percpu_ref_exit;
	return percpu_ref_init(cnt, release, /*percpu mode*/0, GFP_NOFS);
}

static inline void au_lcnt_wait_for_fin(au_lcnt_t *cnt __maybe_unused)
{
	synchronize_rcu();
}

static inline void au_lcnt_fin(au_lcnt_t *cnt, int do_sync)
{
	percpu_ref_kill(cnt);
	if (do_sync)
		au_lcnt_wait_for_fin(cnt);
}

static inline void au_lcnt_inc(au_lcnt_t *cnt)
{
	percpu_ref_get(cnt);
}

static inline void au_lcnt_dec(au_lcnt_t *cnt)
{
	percpu_ref_put(cnt);
}

/*
 * avoid calling this func as possible.
 */
static inline long au_lcnt_read(au_lcnt_t *cnt, int do_rev)
{
	long l;

	percpu_ref_switch_to_atomic_sync(cnt);
	l = atomic_long_read(&cnt->count);
	if (do_rev)
		percpu_ref_switch_to_percpu(cnt);

	/* percpu_ref is initialized by 1 instead of 0 */
	return l - 1;
}
#endif

#ifdef CONFIG_AUFS_DEBUG
#define AuLCntZero(val) do {			\
	long l = val;				\
	if (l)					\
		AuDbg("%s = %ld\n", #val, l);	\
} while (0)
#else
#define AuLCntZero(val)		do {} while (0)
#endif

#endif /* __KERNEL__ */
#endif /* __AUFS_LCNT_H__ */
