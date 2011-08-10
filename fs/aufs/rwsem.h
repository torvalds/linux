/*
 * Copyright (C) 2005-2011 Junjiro R. Okajima
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * simple read-write semaphore wrappers
 */

#ifndef __AUFS_RWSEM_H__
#define __AUFS_RWSEM_H__

#ifdef __KERNEL__

#include <linux/rwsem.h>
#include "debug.h"

struct au_rwsem {
	struct rw_semaphore	rwsem;
#ifdef CONFIG_AUFS_DEBUG
	/* just for debugging, not almighty counter */
	atomic_t		rcnt, wcnt;
#endif
};

#ifdef CONFIG_AUFS_DEBUG
#define AuDbgCntInit(rw) do { \
	atomic_set(&(rw)->rcnt, 0); \
	atomic_set(&(rw)->wcnt, 0); \
	smp_mb(); /* atomic set */ \
} while (0)

#define AuDbgRcntInc(rw)	atomic_inc(&(rw)->rcnt)
#define AuDbgRcntDec(rw)	WARN_ON(atomic_dec_return(&(rw)->rcnt) < 0)
#define AuDbgWcntInc(rw)	atomic_inc(&(rw)->wcnt)
#define AuDbgWcntDec(rw)	WARN_ON(atomic_dec_return(&(rw)->wcnt) < 0)
#else
#define AuDbgCntInit(rw)	do {} while (0)
#define AuDbgRcntInc(rw)	do {} while (0)
#define AuDbgRcntDec(rw)	do {} while (0)
#define AuDbgWcntInc(rw)	do {} while (0)
#define AuDbgWcntDec(rw)	do {} while (0)
#endif /* CONFIG_AUFS_DEBUG */

/* to debug easier, do not make them inlined functions */
#define AuRwMustNoWaiters(rw)	AuDebugOn(!list_empty(&(rw)->rwsem.wait_list))
/* rwsem_is_locked() is unusable */
#define AuRwMustReadLock(rw)	AuDebugOn(atomic_read(&(rw)->rcnt) <= 0)
#define AuRwMustWriteLock(rw)	AuDebugOn(atomic_read(&(rw)->wcnt) <= 0)
#define AuRwMustAnyLock(rw)	AuDebugOn(atomic_read(&(rw)->rcnt) <= 0 \
					&& atomic_read(&(rw)->wcnt) <= 0)
#define AuRwDestroy(rw)		AuDebugOn(atomic_read(&(rw)->rcnt) \
					|| atomic_read(&(rw)->wcnt))

#define au_rw_class(rw, key)	lockdep_set_class(&(rw)->rwsem, key)

static inline void au_rw_init(struct au_rwsem *rw)
{
	AuDbgCntInit(rw);
	init_rwsem(&rw->rwsem);
}

static inline void au_rw_init_wlock(struct au_rwsem *rw)
{
	au_rw_init(rw);
	down_write(&rw->rwsem);
	AuDbgWcntInc(rw);
}

static inline void au_rw_init_wlock_nested(struct au_rwsem *rw,
					   unsigned int lsc)
{
	au_rw_init(rw);
	down_write_nested(&rw->rwsem, lsc);
	AuDbgWcntInc(rw);
}

static inline void au_rw_read_lock(struct au_rwsem *rw)
{
	down_read(&rw->rwsem);
	AuDbgRcntInc(rw);
}

static inline void au_rw_read_lock_nested(struct au_rwsem *rw, unsigned int lsc)
{
	down_read_nested(&rw->rwsem, lsc);
	AuDbgRcntInc(rw);
}

static inline void au_rw_read_unlock(struct au_rwsem *rw)
{
	AuRwMustReadLock(rw);
	AuDbgRcntDec(rw);
	up_read(&rw->rwsem);
}

static inline void au_rw_dgrade_lock(struct au_rwsem *rw)
{
	AuRwMustWriteLock(rw);
	AuDbgRcntInc(rw);
	AuDbgWcntDec(rw);
	downgrade_write(&rw->rwsem);
}

static inline void au_rw_write_lock(struct au_rwsem *rw)
{
	down_write(&rw->rwsem);
	AuDbgWcntInc(rw);
}

static inline void au_rw_write_lock_nested(struct au_rwsem *rw,
					   unsigned int lsc)
{
	down_write_nested(&rw->rwsem, lsc);
	AuDbgWcntInc(rw);
}

static inline void au_rw_write_unlock(struct au_rwsem *rw)
{
	AuRwMustWriteLock(rw);
	AuDbgWcntDec(rw);
	up_write(&rw->rwsem);
}

/* why is not _nested version defined */
static inline int au_rw_read_trylock(struct au_rwsem *rw)
{
	int ret = down_read_trylock(&rw->rwsem);
	if (ret)
		AuDbgRcntInc(rw);
	return ret;
}

static inline int au_rw_write_trylock(struct au_rwsem *rw)
{
	int ret = down_write_trylock(&rw->rwsem);
	if (ret)
		AuDbgWcntInc(rw);
	return ret;
}

#undef AuDbgCntInit
#undef AuDbgRcntInc
#undef AuDbgRcntDec
#undef AuDbgWcntInc
#undef AuDbgWcntDec

#define AuSimpleLockRwsemFuncs(prefix, param, rwsem) \
static inline void prefix##_read_lock(param) \
{ au_rw_read_lock(rwsem); } \
static inline void prefix##_write_lock(param) \
{ au_rw_write_lock(rwsem); } \
static inline int prefix##_read_trylock(param) \
{ return au_rw_read_trylock(rwsem); } \
static inline int prefix##_write_trylock(param) \
{ return au_rw_write_trylock(rwsem); }
/* why is not _nested version defined */
/* static inline void prefix##_read_trylock_nested(param, lsc)
{ au_rw_read_trylock_nested(rwsem, lsc)); }
static inline void prefix##_write_trylock_nestd(param, lsc)
{ au_rw_write_trylock_nested(rwsem, lsc); } */

#define AuSimpleUnlockRwsemFuncs(prefix, param, rwsem) \
static inline void prefix##_read_unlock(param) \
{ au_rw_read_unlock(rwsem); } \
static inline void prefix##_write_unlock(param) \
{ au_rw_write_unlock(rwsem); } \
static inline void prefix##_downgrade_lock(param) \
{ au_rw_dgrade_lock(rwsem); }

#define AuSimpleRwsemFuncs(prefix, param, rwsem) \
	AuSimpleLockRwsemFuncs(prefix, param, rwsem) \
	AuSimpleUnlockRwsemFuncs(prefix, param, rwsem)

#endif /* __KERNEL__ */
#endif /* __AUFS_RWSEM_H__ */
