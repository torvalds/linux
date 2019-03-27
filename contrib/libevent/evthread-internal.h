/*
 * Copyright (c) 2008-2012 Niels Provos, Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EVTHREAD_INTERNAL_H_INCLUDED_
#define EVTHREAD_INTERNAL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include "event2/event-config.h"
#include "evconfig-private.h"

#include "event2/thread.h"
#include "util-internal.h"

struct event_base;

#ifndef _WIN32
/* On Windows, the way we currently make DLLs, it's not allowed for us to
 * have shared global structures.  Thus, we only do the direct-call-to-function
 * code path if we know that the local shared library system supports it.
 */
#define EVTHREAD_EXPOSE_STRUCTS
#endif

#if ! defined(EVENT__DISABLE_THREAD_SUPPORT) && defined(EVTHREAD_EXPOSE_STRUCTS)
/* Global function pointers to lock-related functions. NULL if locking isn't
   enabled. */
extern struct evthread_lock_callbacks evthread_lock_fns_;
extern struct evthread_condition_callbacks evthread_cond_fns_;
extern unsigned long (*evthread_id_fn_)(void);
extern int evthread_lock_debugging_enabled_;

/** Return the ID of the current thread, or 1 if threading isn't enabled. */
#define EVTHREAD_GET_ID() \
	(evthread_id_fn_ ? evthread_id_fn_() : 1)

/** Return true iff we're in the thread that is currently (or most recently)
 * running a given event_base's loop. Requires lock. */
#define EVBASE_IN_THREAD(base)				 \
	(evthread_id_fn_ == NULL ||			 \
	(base)->th_owner_id == evthread_id_fn_())

/** Return true iff we need to notify the base's main thread about changes to
 * its state, because it's currently running the main loop in another
 * thread. Requires lock. */
#define EVBASE_NEED_NOTIFY(base)			 \
	(evthread_id_fn_ != NULL &&			 \
	    (base)->running_loop &&			 \
	    (base)->th_owner_id != evthread_id_fn_())

/** Allocate a new lock, and store it in lockvar, a void*.  Sets lockvar to
    NULL if locking is not enabled. */
#define EVTHREAD_ALLOC_LOCK(lockvar, locktype)		\
	((lockvar) = evthread_lock_fns_.alloc ?		\
	    evthread_lock_fns_.alloc(locktype) : NULL)

/** Free a given lock, if it is present and locking is enabled. */
#define EVTHREAD_FREE_LOCK(lockvar, locktype)				\
	do {								\
		void *lock_tmp_ = (lockvar);				\
		if (lock_tmp_ && evthread_lock_fns_.free)		\
			evthread_lock_fns_.free(lock_tmp_, (locktype)); \
	} while (0)

/** Acquire a lock. */
#define EVLOCK_LOCK(lockvar,mode)					\
	do {								\
		if (lockvar)						\
			evthread_lock_fns_.lock(mode, lockvar);		\
	} while (0)

/** Release a lock */
#define EVLOCK_UNLOCK(lockvar,mode)					\
	do {								\
		if (lockvar)						\
			evthread_lock_fns_.unlock(mode, lockvar);	\
	} while (0)

/** Helper: put lockvar1 and lockvar2 into pointerwise ascending order. */
#define EVLOCK_SORTLOCKS_(lockvar1, lockvar2)				\
	do {								\
		if (lockvar1 && lockvar2 && lockvar1 > lockvar2) {	\
			void *tmp = lockvar1;				\
			lockvar1 = lockvar2;				\
			lockvar2 = tmp;					\
		}							\
	} while (0)

/** Lock an event_base, if it is set up for locking.  Acquires the lock
    in the base structure whose field is named 'lockvar'. */
#define EVBASE_ACQUIRE_LOCK(base, lockvar) do {				\
		EVLOCK_LOCK((base)->lockvar, 0);			\
	} while (0)

/** Unlock an event_base, if it is set up for locking. */
#define EVBASE_RELEASE_LOCK(base, lockvar) do {				\
		EVLOCK_UNLOCK((base)->lockvar, 0);			\
	} while (0)

/** If lock debugging is enabled, and lock is non-null, assert that 'lock' is
 * locked and held by us. */
#define EVLOCK_ASSERT_LOCKED(lock)					\
	do {								\
		if ((lock) && evthread_lock_debugging_enabled_) {	\
			EVUTIL_ASSERT(evthread_is_debug_lock_held_(lock)); \
		}							\
	} while (0)

/** Try to grab the lock for 'lockvar' without blocking, and return 1 if we
 * manage to get it. */
static inline int EVLOCK_TRY_LOCK_(void *lock);
static inline int
EVLOCK_TRY_LOCK_(void *lock)
{
	if (lock && evthread_lock_fns_.lock) {
		int r = evthread_lock_fns_.lock(EVTHREAD_TRY, lock);
		return !r;
	} else {
		/* Locking is disabled either globally or for this thing;
		 * of course we count as having the lock. */
		return 1;
	}
}

/** Allocate a new condition variable and store it in the void *, condvar */
#define EVTHREAD_ALLOC_COND(condvar)					\
	do {								\
		(condvar) = evthread_cond_fns_.alloc_condition ?	\
		    evthread_cond_fns_.alloc_condition(0) : NULL;	\
	} while (0)
/** Deallocate and free a condition variable in condvar */
#define EVTHREAD_FREE_COND(cond)					\
	do {								\
		if (cond)						\
			evthread_cond_fns_.free_condition((cond));	\
	} while (0)
/** Signal one thread waiting on cond */
#define EVTHREAD_COND_SIGNAL(cond)					\
	( (cond) ? evthread_cond_fns_.signal_condition((cond), 0) : 0 )
/** Signal all threads waiting on cond */
#define EVTHREAD_COND_BROADCAST(cond)					\
	( (cond) ? evthread_cond_fns_.signal_condition((cond), 1) : 0 )
/** Wait until the condition 'cond' is signalled.  Must be called while
 * holding 'lock'.  The lock will be released until the condition is
 * signalled, at which point it will be acquired again.  Returns 0 for
 * success, -1 for failure. */
#define EVTHREAD_COND_WAIT(cond, lock)					\
	( (cond) ? evthread_cond_fns_.wait_condition((cond), (lock), NULL) : 0 )
/** As EVTHREAD_COND_WAIT, but gives up after 'tv' has elapsed.  Returns 1
 * on timeout. */
#define EVTHREAD_COND_WAIT_TIMED(cond, lock, tv)			\
	( (cond) ? evthread_cond_fns_.wait_condition((cond), (lock), (tv)) : 0 )

/** True iff locking functions have been configured. */
#define EVTHREAD_LOCKING_ENABLED()		\
	(evthread_lock_fns_.lock != NULL)

#elif ! defined(EVENT__DISABLE_THREAD_SUPPORT)

unsigned long evthreadimpl_get_id_(void);
int evthreadimpl_is_lock_debugging_enabled_(void);
void *evthreadimpl_lock_alloc_(unsigned locktype);
void evthreadimpl_lock_free_(void *lock, unsigned locktype);
int evthreadimpl_lock_lock_(unsigned mode, void *lock);
int evthreadimpl_lock_unlock_(unsigned mode, void *lock);
void *evthreadimpl_cond_alloc_(unsigned condtype);
void evthreadimpl_cond_free_(void *cond);
int evthreadimpl_cond_signal_(void *cond, int broadcast);
int evthreadimpl_cond_wait_(void *cond, void *lock, const struct timeval *tv);
int evthreadimpl_locking_enabled_(void);

#define EVTHREAD_GET_ID() evthreadimpl_get_id_()
#define EVBASE_IN_THREAD(base)				\
	((base)->th_owner_id == evthreadimpl_get_id_())
#define EVBASE_NEED_NOTIFY(base)			 \
	((base)->running_loop &&			 \
	    ((base)->th_owner_id != evthreadimpl_get_id_()))

#define EVTHREAD_ALLOC_LOCK(lockvar, locktype)		\
	((lockvar) = evthreadimpl_lock_alloc_(locktype))

#define EVTHREAD_FREE_LOCK(lockvar, locktype)				\
	do {								\
		void *lock_tmp_ = (lockvar);				\
		if (lock_tmp_)						\
			evthreadimpl_lock_free_(lock_tmp_, (locktype)); \
	} while (0)

/** Acquire a lock. */
#define EVLOCK_LOCK(lockvar,mode)					\
	do {								\
		if (lockvar)						\
			evthreadimpl_lock_lock_(mode, lockvar);		\
	} while (0)

/** Release a lock */
#define EVLOCK_UNLOCK(lockvar,mode)					\
	do {								\
		if (lockvar)						\
			evthreadimpl_lock_unlock_(mode, lockvar);	\
	} while (0)

/** Lock an event_base, if it is set up for locking.  Acquires the lock
    in the base structure whose field is named 'lockvar'. */
#define EVBASE_ACQUIRE_LOCK(base, lockvar) do {				\
		EVLOCK_LOCK((base)->lockvar, 0);			\
	} while (0)

/** Unlock an event_base, if it is set up for locking. */
#define EVBASE_RELEASE_LOCK(base, lockvar) do {				\
		EVLOCK_UNLOCK((base)->lockvar, 0);			\
	} while (0)

/** If lock debugging is enabled, and lock is non-null, assert that 'lock' is
 * locked and held by us. */
#define EVLOCK_ASSERT_LOCKED(lock)					\
	do {								\
		if ((lock) && evthreadimpl_is_lock_debugging_enabled_()) { \
			EVUTIL_ASSERT(evthread_is_debug_lock_held_(lock)); \
		}							\
	} while (0)

/** Try to grab the lock for 'lockvar' without blocking, and return 1 if we
 * manage to get it. */
static inline int EVLOCK_TRY_LOCK_(void *lock);
static inline int
EVLOCK_TRY_LOCK_(void *lock)
{
	if (lock) {
		int r = evthreadimpl_lock_lock_(EVTHREAD_TRY, lock);
		return !r;
	} else {
		/* Locking is disabled either globally or for this thing;
		 * of course we count as having the lock. */
		return 1;
	}
}

/** Allocate a new condition variable and store it in the void *, condvar */
#define EVTHREAD_ALLOC_COND(condvar)					\
	do {								\
		(condvar) = evthreadimpl_cond_alloc_(0);		\
	} while (0)
/** Deallocate and free a condition variable in condvar */
#define EVTHREAD_FREE_COND(cond)					\
	do {								\
		if (cond)						\
			evthreadimpl_cond_free_((cond));		\
	} while (0)
/** Signal one thread waiting on cond */
#define EVTHREAD_COND_SIGNAL(cond)					\
	( (cond) ? evthreadimpl_cond_signal_((cond), 0) : 0 )
/** Signal all threads waiting on cond */
#define EVTHREAD_COND_BROADCAST(cond)					\
	( (cond) ? evthreadimpl_cond_signal_((cond), 1) : 0 )
/** Wait until the condition 'cond' is signalled.  Must be called while
 * holding 'lock'.  The lock will be released until the condition is
 * signalled, at which point it will be acquired again.  Returns 0 for
 * success, -1 for failure. */
#define EVTHREAD_COND_WAIT(cond, lock)					\
	( (cond) ? evthreadimpl_cond_wait_((cond), (lock), NULL) : 0 )
/** As EVTHREAD_COND_WAIT, but gives up after 'tv' has elapsed.  Returns 1
 * on timeout. */
#define EVTHREAD_COND_WAIT_TIMED(cond, lock, tv)			\
	( (cond) ? evthreadimpl_cond_wait_((cond), (lock), (tv)) : 0 )

#define EVTHREAD_LOCKING_ENABLED()		\
	(evthreadimpl_locking_enabled_())

#else /* EVENT__DISABLE_THREAD_SUPPORT */

#define EVTHREAD_GET_ID()	1
#define EVTHREAD_ALLOC_LOCK(lockvar, locktype) EVUTIL_NIL_STMT_
#define EVTHREAD_FREE_LOCK(lockvar, locktype) EVUTIL_NIL_STMT_

#define EVLOCK_LOCK(lockvar, mode) EVUTIL_NIL_STMT_
#define EVLOCK_UNLOCK(lockvar, mode) EVUTIL_NIL_STMT_
#define EVLOCK_LOCK2(lock1,lock2,mode1,mode2) EVUTIL_NIL_STMT_
#define EVLOCK_UNLOCK2(lock1,lock2,mode1,mode2) EVUTIL_NIL_STMT_

#define EVBASE_IN_THREAD(base)	1
#define EVBASE_NEED_NOTIFY(base) 0
#define EVBASE_ACQUIRE_LOCK(base, lock) EVUTIL_NIL_STMT_
#define EVBASE_RELEASE_LOCK(base, lock) EVUTIL_NIL_STMT_
#define EVLOCK_ASSERT_LOCKED(lock) EVUTIL_NIL_STMT_

#define EVLOCK_TRY_LOCK_(lock) 1

#define EVTHREAD_ALLOC_COND(condvar) EVUTIL_NIL_STMT_
#define EVTHREAD_FREE_COND(cond) EVUTIL_NIL_STMT_
#define EVTHREAD_COND_SIGNAL(cond) EVUTIL_NIL_STMT_
#define EVTHREAD_COND_BROADCAST(cond) EVUTIL_NIL_STMT_
#define EVTHREAD_COND_WAIT(cond, lock) EVUTIL_NIL_STMT_
#define EVTHREAD_COND_WAIT_TIMED(cond, lock, howlong) EVUTIL_NIL_STMT_

#define EVTHREAD_LOCKING_ENABLED() 0

#endif

/* This code is shared between both lock impls */
#if ! defined(EVENT__DISABLE_THREAD_SUPPORT)
/** Helper: put lockvar1 and lockvar2 into pointerwise ascending order. */
#define EVLOCK_SORTLOCKS_(lockvar1, lockvar2)				\
	do {								\
		if (lockvar1 && lockvar2 && lockvar1 > lockvar2) {	\
			void *tmp = lockvar1;				\
			lockvar1 = lockvar2;				\
			lockvar2 = tmp;					\
		}							\
	} while (0)

/** Acquire both lock1 and lock2.  Always allocates locks in the same order,
 * so that two threads locking two locks with LOCK2 will not deadlock. */
#define EVLOCK_LOCK2(lock1,lock2,mode1,mode2)				\
	do {								\
		void *lock1_tmplock_ = (lock1);				\
		void *lock2_tmplock_ = (lock2);				\
		EVLOCK_SORTLOCKS_(lock1_tmplock_,lock2_tmplock_);	\
		EVLOCK_LOCK(lock1_tmplock_,mode1);			\
		if (lock2_tmplock_ != lock1_tmplock_)			\
			EVLOCK_LOCK(lock2_tmplock_,mode2);		\
	} while (0)
/** Release both lock1 and lock2.  */
#define EVLOCK_UNLOCK2(lock1,lock2,mode1,mode2)				\
	do {								\
		void *lock1_tmplock_ = (lock1);				\
		void *lock2_tmplock_ = (lock2);				\
		EVLOCK_SORTLOCKS_(lock1_tmplock_,lock2_tmplock_);	\
		if (lock2_tmplock_ != lock1_tmplock_)			\
			EVLOCK_UNLOCK(lock2_tmplock_,mode2);		\
		EVLOCK_UNLOCK(lock1_tmplock_,mode1);			\
	} while (0)

int evthread_is_debug_lock_held_(void *lock);
void *evthread_debug_get_real_lock_(void *lock);

void *evthread_setup_global_lock_(void *lock_, unsigned locktype,
    int enable_locks);

#define EVTHREAD_SETUP_GLOBAL_LOCK(lockvar, locktype)			\
	do {								\
		lockvar = evthread_setup_global_lock_(lockvar,		\
		    (locktype), enable_locks);				\
		if (!lockvar) {						\
			event_warn("Couldn't allocate %s", #lockvar);	\
			return -1;					\
		}							\
	} while (0);

int event_global_setup_locks_(const int enable_locks);
int evsig_global_setup_locks_(const int enable_locks);
int evutil_global_setup_locks_(const int enable_locks);
int evutil_secure_rng_global_setup_locks_(const int enable_locks);

/** Return current evthread_lock_callbacks */
struct evthread_lock_callbacks *evthread_get_lock_callbacks(void);
/** Return current evthread_condition_callbacks */
struct evthread_condition_callbacks *evthread_get_condition_callbacks(void);
/** Disable locking for internal usage (like global shutdown) */
void evthreadimpl_disable_lock_debugging_(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* EVTHREAD_INTERNAL_H_INCLUDED_ */
