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

#include "event2/event-config.h"
#include "evconfig-private.h"

#ifndef EVENT__DISABLE_THREAD_SUPPORT

#include "event2/thread.h"

#include <stdlib.h>
#include <string.h>

#include "log-internal.h"
#include "mm-internal.h"
#include "util-internal.h"
#include "evthread-internal.h"

#ifdef EVTHREAD_EXPOSE_STRUCTS
#define GLOBAL
#else
#define GLOBAL static
#endif

#ifndef EVENT__DISABLE_DEBUG_MODE
extern int event_debug_created_threadable_ctx_;                    
extern int event_debug_mode_on_;
#endif

/* globals */
GLOBAL int evthread_lock_debugging_enabled_ = 0;
GLOBAL struct evthread_lock_callbacks evthread_lock_fns_ = {
	0, 0, NULL, NULL, NULL, NULL
};
GLOBAL unsigned long (*evthread_id_fn_)(void) = NULL;
GLOBAL struct evthread_condition_callbacks evthread_cond_fns_ = {
	0, NULL, NULL, NULL, NULL
};

/* Used for debugging */
static struct evthread_lock_callbacks original_lock_fns_ = {
	0, 0, NULL, NULL, NULL, NULL
};
static struct evthread_condition_callbacks original_cond_fns_ = {
	0, NULL, NULL, NULL, NULL
};

void
evthread_set_id_callback(unsigned long (*id_fn)(void))
{
	evthread_id_fn_ = id_fn;
}

struct evthread_lock_callbacks *evthread_get_lock_callbacks()
{
	return evthread_lock_debugging_enabled_
	    ? &original_lock_fns_ : &evthread_lock_fns_;
}
struct evthread_condition_callbacks *evthread_get_condition_callbacks()
{
	return evthread_lock_debugging_enabled_
	    ? &original_cond_fns_ : &evthread_cond_fns_;
}
void evthreadimpl_disable_lock_debugging_(void)
{
	evthread_lock_debugging_enabled_ = 0;
}

int
evthread_set_lock_callbacks(const struct evthread_lock_callbacks *cbs)
{
	struct evthread_lock_callbacks *target = evthread_get_lock_callbacks();

#ifndef EVENT__DISABLE_DEBUG_MODE
	if (event_debug_mode_on_) {
		if (event_debug_created_threadable_ctx_) {
		    event_errx(1, "evthread initialization must be called BEFORE anything else!");
		}
	}
#endif

	if (!cbs) {
		if (target->alloc)
			event_warnx("Trying to disable lock functions after "
			    "they have been set up will probaby not work.");
		memset(target, 0, sizeof(evthread_lock_fns_));
		return 0;
	}
	if (target->alloc) {
		/* Uh oh; we already had locking callbacks set up.*/
		if (target->lock_api_version == cbs->lock_api_version &&
			target->supported_locktypes == cbs->supported_locktypes &&
			target->alloc == cbs->alloc &&
			target->free == cbs->free &&
			target->lock == cbs->lock &&
			target->unlock == cbs->unlock) {
			/* no change -- allow this. */
			return 0;
		}
		event_warnx("Can't change lock callbacks once they have been "
		    "initialized.");
		return -1;
	}
	if (cbs->alloc && cbs->free && cbs->lock && cbs->unlock) {
		memcpy(target, cbs, sizeof(evthread_lock_fns_));
		return event_global_setup_locks_(1);
	} else {
		return -1;
	}
}

int
evthread_set_condition_callbacks(const struct evthread_condition_callbacks *cbs)
{
	struct evthread_condition_callbacks *target = evthread_get_condition_callbacks();

#ifndef EVENT__DISABLE_DEBUG_MODE
	if (event_debug_mode_on_) {
		if (event_debug_created_threadable_ctx_) {
		    event_errx(1, "evthread initialization must be called BEFORE anything else!");
		}
	}
#endif

	if (!cbs) {
		if (target->alloc_condition)
			event_warnx("Trying to disable condition functions "
			    "after they have been set up will probaby not "
			    "work.");
		memset(target, 0, sizeof(evthread_cond_fns_));
		return 0;
	}
	if (target->alloc_condition) {
		/* Uh oh; we already had condition callbacks set up.*/
		if (target->condition_api_version == cbs->condition_api_version &&
			target->alloc_condition == cbs->alloc_condition &&
			target->free_condition == cbs->free_condition &&
			target->signal_condition == cbs->signal_condition &&
			target->wait_condition == cbs->wait_condition) {
			/* no change -- allow this. */
			return 0;
		}
		event_warnx("Can't change condition callbacks once they "
		    "have been initialized.");
		return -1;
	}
	if (cbs->alloc_condition && cbs->free_condition &&
	    cbs->signal_condition && cbs->wait_condition) {
		memcpy(target, cbs, sizeof(evthread_cond_fns_));
	}
	if (evthread_lock_debugging_enabled_) {
		evthread_cond_fns_.alloc_condition = cbs->alloc_condition;
		evthread_cond_fns_.free_condition = cbs->free_condition;
		evthread_cond_fns_.signal_condition = cbs->signal_condition;
	}
	return 0;
}

#define DEBUG_LOCK_SIG	0xdeb0b10c

struct debug_lock {
	unsigned signature;
	unsigned locktype;
	unsigned long held_by;
	/* XXXX if we ever use read-write locks, we will need a separate
	 * lock to protect count. */
	int count;
	void *lock;
};

static void *
debug_lock_alloc(unsigned locktype)
{
	struct debug_lock *result = mm_malloc(sizeof(struct debug_lock));
	if (!result)
		return NULL;
	if (original_lock_fns_.alloc) {
		if (!(result->lock = original_lock_fns_.alloc(
				locktype|EVTHREAD_LOCKTYPE_RECURSIVE))) {
			mm_free(result);
			return NULL;
		}
	} else {
		result->lock = NULL;
	}
	result->signature = DEBUG_LOCK_SIG;
	result->locktype = locktype;
	result->count = 0;
	result->held_by = 0;
	return result;
}

static void
debug_lock_free(void *lock_, unsigned locktype)
{
	struct debug_lock *lock = lock_;
	EVUTIL_ASSERT(lock->count == 0);
	EVUTIL_ASSERT(locktype == lock->locktype);
	EVUTIL_ASSERT(DEBUG_LOCK_SIG == lock->signature);
	if (original_lock_fns_.free) {
		original_lock_fns_.free(lock->lock,
		    lock->locktype|EVTHREAD_LOCKTYPE_RECURSIVE);
	}
	lock->lock = NULL;
	lock->count = -100;
	lock->signature = 0x12300fda;
	mm_free(lock);
}

static void
evthread_debug_lock_mark_locked(unsigned mode, struct debug_lock *lock)
{
	EVUTIL_ASSERT(DEBUG_LOCK_SIG == lock->signature);
	++lock->count;
	if (!(lock->locktype & EVTHREAD_LOCKTYPE_RECURSIVE))
		EVUTIL_ASSERT(lock->count == 1);
	if (evthread_id_fn_) {
		unsigned long me;
		me = evthread_id_fn_();
		if (lock->count > 1)
			EVUTIL_ASSERT(lock->held_by == me);
		lock->held_by = me;
	}
}

static int
debug_lock_lock(unsigned mode, void *lock_)
{
	struct debug_lock *lock = lock_;
	int res = 0;
	if (lock->locktype & EVTHREAD_LOCKTYPE_READWRITE)
		EVUTIL_ASSERT(mode & (EVTHREAD_READ|EVTHREAD_WRITE));
	else
		EVUTIL_ASSERT((mode & (EVTHREAD_READ|EVTHREAD_WRITE)) == 0);
	if (original_lock_fns_.lock)
		res = original_lock_fns_.lock(mode, lock->lock);
	if (!res) {
		evthread_debug_lock_mark_locked(mode, lock);
	}
	return res;
}

static void
evthread_debug_lock_mark_unlocked(unsigned mode, struct debug_lock *lock)
{
	EVUTIL_ASSERT(DEBUG_LOCK_SIG == lock->signature);
	if (lock->locktype & EVTHREAD_LOCKTYPE_READWRITE)
		EVUTIL_ASSERT(mode & (EVTHREAD_READ|EVTHREAD_WRITE));
	else
		EVUTIL_ASSERT((mode & (EVTHREAD_READ|EVTHREAD_WRITE)) == 0);
	if (evthread_id_fn_) {
		unsigned long me;
		me = evthread_id_fn_();
		EVUTIL_ASSERT(lock->held_by == me);
		if (lock->count == 1)
			lock->held_by = 0;
	}
	--lock->count;
	EVUTIL_ASSERT(lock->count >= 0);
}

static int
debug_lock_unlock(unsigned mode, void *lock_)
{
	struct debug_lock *lock = lock_;
	int res = 0;
	evthread_debug_lock_mark_unlocked(mode, lock);
	if (original_lock_fns_.unlock)
		res = original_lock_fns_.unlock(mode, lock->lock);
	return res;
}

static int
debug_cond_wait(void *cond_, void *lock_, const struct timeval *tv)
{
	int r;
	struct debug_lock *lock = lock_;
	EVUTIL_ASSERT(lock);
	EVUTIL_ASSERT(DEBUG_LOCK_SIG == lock->signature);
	EVLOCK_ASSERT_LOCKED(lock_);
	evthread_debug_lock_mark_unlocked(0, lock);
	r = original_cond_fns_.wait_condition(cond_, lock->lock, tv);
	evthread_debug_lock_mark_locked(0, lock);
	return r;
}

/* misspelled version for backward compatibility */
void
evthread_enable_lock_debuging(void)
{
	evthread_enable_lock_debugging();
}

void
evthread_enable_lock_debugging(void)
{
	struct evthread_lock_callbacks cbs = {
		EVTHREAD_LOCK_API_VERSION,
		EVTHREAD_LOCKTYPE_RECURSIVE,
		debug_lock_alloc,
		debug_lock_free,
		debug_lock_lock,
		debug_lock_unlock
	};
	if (evthread_lock_debugging_enabled_)
		return;
	memcpy(&original_lock_fns_, &evthread_lock_fns_,
	    sizeof(struct evthread_lock_callbacks));
	memcpy(&evthread_lock_fns_, &cbs,
	    sizeof(struct evthread_lock_callbacks));

	memcpy(&original_cond_fns_, &evthread_cond_fns_,
	    sizeof(struct evthread_condition_callbacks));
	evthread_cond_fns_.wait_condition = debug_cond_wait;
	evthread_lock_debugging_enabled_ = 1;

	/* XXX return value should get checked. */
	event_global_setup_locks_(0);
}

int
evthread_is_debug_lock_held_(void *lock_)
{
	struct debug_lock *lock = lock_;
	if (! lock->count)
		return 0;
	if (evthread_id_fn_) {
		unsigned long me = evthread_id_fn_();
		if (lock->held_by != me)
			return 0;
	}
	return 1;
}

void *
evthread_debug_get_real_lock_(void *lock_)
{
	struct debug_lock *lock = lock_;
	return lock->lock;
}

void *
evthread_setup_global_lock_(void *lock_, unsigned locktype, int enable_locks)
{
	/* there are four cases here:
	   1) we're turning on debugging; locking is not on.
	   2) we're turning on debugging; locking is on.
	   3) we're turning on locking; debugging is not on.
	   4) we're turning on locking; debugging is on. */

	if (!enable_locks && original_lock_fns_.alloc == NULL) {
		/* Case 1: allocate a debug lock. */
		EVUTIL_ASSERT(lock_ == NULL);
		return debug_lock_alloc(locktype);
	} else if (!enable_locks && original_lock_fns_.alloc != NULL) {
		/* Case 2: wrap the lock in a debug lock. */
		struct debug_lock *lock;
		EVUTIL_ASSERT(lock_ != NULL);

		if (!(locktype & EVTHREAD_LOCKTYPE_RECURSIVE)) {
			/* We can't wrap it: We need a recursive lock */
			original_lock_fns_.free(lock_, locktype);
			return debug_lock_alloc(locktype);
		}
		lock = mm_malloc(sizeof(struct debug_lock));
		if (!lock) {
			original_lock_fns_.free(lock_, locktype);
			return NULL;
		}
		lock->lock = lock_;
		lock->locktype = locktype;
		lock->count = 0;
		lock->held_by = 0;
		return lock;
	} else if (enable_locks && ! evthread_lock_debugging_enabled_) {
		/* Case 3: allocate a regular lock */
		EVUTIL_ASSERT(lock_ == NULL);
		return evthread_lock_fns_.alloc(locktype);
	} else {
		/* Case 4: Fill in a debug lock with a real lock */
		struct debug_lock *lock = lock_ ? lock_ : debug_lock_alloc(locktype);
		EVUTIL_ASSERT(enable_locks &&
		              evthread_lock_debugging_enabled_);
		EVUTIL_ASSERT(lock->locktype == locktype);
		if (!lock->lock) {
			lock->lock = original_lock_fns_.alloc(
				locktype|EVTHREAD_LOCKTYPE_RECURSIVE);
			if (!lock->lock) {
				lock->count = -200;
				mm_free(lock);
				return NULL;
			}
		}
		return lock;
	}
}


#ifndef EVTHREAD_EXPOSE_STRUCTS
unsigned long
evthreadimpl_get_id_()
{
	return evthread_id_fn_ ? evthread_id_fn_() : 1;
}
void *
evthreadimpl_lock_alloc_(unsigned locktype)
{
#ifndef EVENT__DISABLE_DEBUG_MODE
	if (event_debug_mode_on_) {
		event_debug_created_threadable_ctx_ = 1;
	}
#endif

	return evthread_lock_fns_.alloc ?
	    evthread_lock_fns_.alloc(locktype) : NULL;
}
void
evthreadimpl_lock_free_(void *lock, unsigned locktype)
{
	if (evthread_lock_fns_.free)
		evthread_lock_fns_.free(lock, locktype);
}
int
evthreadimpl_lock_lock_(unsigned mode, void *lock)
{
	if (evthread_lock_fns_.lock)
		return evthread_lock_fns_.lock(mode, lock);
	else
		return 0;
}
int
evthreadimpl_lock_unlock_(unsigned mode, void *lock)
{
	if (evthread_lock_fns_.unlock)
		return evthread_lock_fns_.unlock(mode, lock);
	else
		return 0;
}
void *
evthreadimpl_cond_alloc_(unsigned condtype)
{
#ifndef EVENT__DISABLE_DEBUG_MODE
	if (event_debug_mode_on_) {
		event_debug_created_threadable_ctx_ = 1;
	}
#endif

	return evthread_cond_fns_.alloc_condition ?
	    evthread_cond_fns_.alloc_condition(condtype) : NULL;
}
void
evthreadimpl_cond_free_(void *cond)
{
	if (evthread_cond_fns_.free_condition)
		evthread_cond_fns_.free_condition(cond);
}
int
evthreadimpl_cond_signal_(void *cond, int broadcast)
{
	if (evthread_cond_fns_.signal_condition)
		return evthread_cond_fns_.signal_condition(cond, broadcast);
	else
		return 0;
}
int
evthreadimpl_cond_wait_(void *cond, void *lock, const struct timeval *tv)
{
	if (evthread_cond_fns_.wait_condition)
		return evthread_cond_fns_.wait_condition(cond, lock, tv);
	else
		return 0;
}
int
evthreadimpl_is_lock_debugging_enabled_(void)
{
	return evthread_lock_debugging_enabled_;
}

int
evthreadimpl_locking_enabled_(void)
{
	return evthread_lock_fns_.lock != NULL;
}
#endif

#endif
