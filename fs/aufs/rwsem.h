/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2018 Junjiro R. Okajima
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
 * simple read-write semaphore wrappers
 */

#ifndef __AUFS_RWSEM_H__
#define __AUFS_RWSEM_H__

#ifdef __KERNEL__

#include "debug.h"

/* in the future, the name 'au_rwsem' will be totally gone */
#define au_rwsem	rw_semaphore

/* to debug easier, do not make them inlined functions */
#define AuRwMustNoWaiters(rw)	AuDebugOn(rwsem_is_contended(rw))
/* rwsem_is_locked() is unusable */
#define AuRwMustReadLock(rw)	AuDebugOn(!lockdep_recursing(current) \
					  && debug_locks \
					  && !lockdep_is_held_type(rw, 1))
#define AuRwMustWriteLock(rw)	AuDebugOn(!lockdep_recursing(current) \
					  && debug_locks \
					  && !lockdep_is_held_type(rw, 0))
#define AuRwMustAnyLock(rw)	AuDebugOn(!lockdep_recursing(current) \
					  && debug_locks \
					  && !lockdep_is_held(rw))
#define AuRwDestroy(rw)		AuDebugOn(!lockdep_recursing(current) \
					  && debug_locks \
					  && lockdep_is_held(rw))

#define au_rw_init(rw)	init_rwsem(rw)

#define au_rw_init_wlock(rw) do {		\
		au_rw_init(rw);			\
		down_write(rw);			\
	} while (0)

#define au_rw_init_wlock_nested(rw, lsc) do {	\
		au_rw_init(rw);			\
		down_write_nested(rw, lsc);	\
	} while (0)

#define au_rw_read_lock(rw)		down_read(rw)
#define au_rw_read_lock_nested(rw, lsc)	down_read_nested(rw, lsc)
#define au_rw_read_unlock(rw)		up_read(rw)
#define au_rw_dgrade_lock(rw)		downgrade_write(rw)
#define au_rw_write_lock(rw)		down_write(rw)
#define au_rw_write_lock_nested(rw, lsc) down_write_nested(rw, lsc)
#define au_rw_write_unlock(rw)		up_write(rw)
/* why is not _nested version defined? */
#define au_rw_read_trylock(rw)		down_read_trylock(rw)
#define au_rw_write_trylock(rw)		down_write_trylock(rw)

#endif /* __KERNEL__ */
#endif /* __AUFS_RWSEM_H__ */
