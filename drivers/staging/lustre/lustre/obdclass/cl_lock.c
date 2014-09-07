/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Client Extent Lock.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include "../include/obd_class.h"
#include "../include/obd_support.h"
#include "../include/lustre_fid.h"
#include <linux/list.h>
#include "../include/cl_object.h"
#include "cl_internal.h"

/** Lock class of cl_lock::cll_guard */
static struct lock_class_key cl_lock_guard_class;
static struct kmem_cache *cl_lock_kmem;

static struct lu_kmem_descr cl_lock_caches[] = {
	{
		.ckd_cache = &cl_lock_kmem,
		.ckd_name  = "cl_lock_kmem",
		.ckd_size  = sizeof (struct cl_lock)
	},
	{
		.ckd_cache = NULL
	}
};

#define CS_LOCK_INC(o, item)
#define CS_LOCK_DEC(o, item)
#define CS_LOCKSTATE_INC(o, state)
#define CS_LOCKSTATE_DEC(o, state)

/**
 * Basic lock invariant that is maintained at all times. Caller either has a
 * reference to \a lock, or somehow assures that \a lock cannot be freed.
 *
 * \see cl_lock_invariant()
 */
static int cl_lock_invariant_trusted(const struct lu_env *env,
				     const struct cl_lock *lock)
{
	return  ergo(lock->cll_state == CLS_FREEING, lock->cll_holds == 0) &&
		atomic_read(&lock->cll_ref) >= lock->cll_holds &&
		lock->cll_holds >= lock->cll_users &&
		lock->cll_holds >= 0 &&
		lock->cll_users >= 0 &&
		lock->cll_depth >= 0;
}

/**
 * Stronger lock invariant, checking that caller has a reference on a lock.
 *
 * \see cl_lock_invariant_trusted()
 */
static int cl_lock_invariant(const struct lu_env *env,
			     const struct cl_lock *lock)
{
	int result;

	result = atomic_read(&lock->cll_ref) > 0 &&
		cl_lock_invariant_trusted(env, lock);
	if (!result && env != NULL)
		CL_LOCK_DEBUG(D_ERROR, env, lock, "invariant broken");
	return result;
}

/**
 * Returns lock "nesting": 0 for a top-lock and 1 for a sub-lock.
 */
static enum clt_nesting_level cl_lock_nesting(const struct cl_lock *lock)
{
	return cl_object_header(lock->cll_descr.cld_obj)->coh_nesting;
}

/**
 * Returns a set of counters for this lock, depending on a lock nesting.
 */
static struct cl_thread_counters *cl_lock_counters(const struct lu_env *env,
						   const struct cl_lock *lock)
{
	struct cl_thread_info *info;
	enum clt_nesting_level nesting;

	info = cl_env_info(env);
	nesting = cl_lock_nesting(lock);
	LASSERT(nesting < ARRAY_SIZE(info->clt_counters));
	return &info->clt_counters[nesting];
}

static void cl_lock_trace0(int level, const struct lu_env *env,
			   const char *prefix, const struct cl_lock *lock,
			   const char *func, const int line)
{
	struct cl_object_header *h = cl_object_header(lock->cll_descr.cld_obj);
	CDEBUG(level, "%s: %p@(%d %p %d %d %d %d %d %lx)"
		      "(%p/%d/%d) at %s():%d\n",
	       prefix, lock, atomic_read(&lock->cll_ref),
	       lock->cll_guarder, lock->cll_depth,
	       lock->cll_state, lock->cll_error, lock->cll_holds,
	       lock->cll_users, lock->cll_flags,
	       env, h->coh_nesting, cl_lock_nr_mutexed(env),
	       func, line);
}
#define cl_lock_trace(level, env, prefix, lock)			 \
	cl_lock_trace0(level, env, prefix, lock, __func__, __LINE__)

#define RETIP ((unsigned long)__builtin_return_address(0))

#ifdef CONFIG_LOCKDEP
static struct lock_class_key cl_lock_key;

static void cl_lock_lockdep_init(struct cl_lock *lock)
{
	lockdep_set_class_and_name(lock, &cl_lock_key, "EXT");
}

static void cl_lock_lockdep_acquire(const struct lu_env *env,
				    struct cl_lock *lock, __u32 enqflags)
{
	cl_lock_counters(env, lock)->ctc_nr_locks_acquired++;
	lock_map_acquire(&lock->dep_map);
}

static void cl_lock_lockdep_release(const struct lu_env *env,
				    struct cl_lock *lock)
{
	cl_lock_counters(env, lock)->ctc_nr_locks_acquired--;
	lock_release(&lock->dep_map, 0, RETIP);
}

#else /* !CONFIG_LOCKDEP */

static void cl_lock_lockdep_init(struct cl_lock *lock)
{}
static void cl_lock_lockdep_acquire(const struct lu_env *env,
				    struct cl_lock *lock, __u32 enqflags)
{}
static void cl_lock_lockdep_release(const struct lu_env *env,
				    struct cl_lock *lock)
{}

#endif /* !CONFIG_LOCKDEP */

/**
 * Adds lock slice to the compound lock.
 *
 * This is called by cl_object_operations::coo_lock_init() methods to add a
 * per-layer state to the lock. New state is added at the end of
 * cl_lock::cll_layers list, that is, it is at the bottom of the stack.
 *
 * \see cl_req_slice_add(), cl_page_slice_add(), cl_io_slice_add()
 */
void cl_lock_slice_add(struct cl_lock *lock, struct cl_lock_slice *slice,
		       struct cl_object *obj,
		       const struct cl_lock_operations *ops)
{
	slice->cls_lock = lock;
	list_add_tail(&slice->cls_linkage, &lock->cll_layers);
	slice->cls_obj = obj;
	slice->cls_ops = ops;
}
EXPORT_SYMBOL(cl_lock_slice_add);

/**
 * Returns true iff a lock with the mode \a has provides at least the same
 * guarantees as a lock with the mode \a need.
 */
int cl_lock_mode_match(enum cl_lock_mode has, enum cl_lock_mode need)
{
	LINVRNT(need == CLM_READ || need == CLM_WRITE ||
		need == CLM_PHANTOM || need == CLM_GROUP);
	LINVRNT(has == CLM_READ || has == CLM_WRITE ||
		has == CLM_PHANTOM || has == CLM_GROUP);
	CLASSERT(CLM_PHANTOM < CLM_READ);
	CLASSERT(CLM_READ < CLM_WRITE);
	CLASSERT(CLM_WRITE < CLM_GROUP);

	if (has != CLM_GROUP)
		return need <= has;
	else
		return need == has;
}
EXPORT_SYMBOL(cl_lock_mode_match);

/**
 * Returns true iff extent portions of lock descriptions match.
 */
int cl_lock_ext_match(const struct cl_lock_descr *has,
		      const struct cl_lock_descr *need)
{
	return
		has->cld_start <= need->cld_start &&
		has->cld_end >= need->cld_end &&
		cl_lock_mode_match(has->cld_mode, need->cld_mode) &&
		(has->cld_mode != CLM_GROUP || has->cld_gid == need->cld_gid);
}
EXPORT_SYMBOL(cl_lock_ext_match);

/**
 * Returns true iff a lock with the description \a has provides at least the
 * same guarantees as a lock with the description \a need.
 */
int cl_lock_descr_match(const struct cl_lock_descr *has,
			const struct cl_lock_descr *need)
{
	return
		cl_object_same(has->cld_obj, need->cld_obj) &&
		cl_lock_ext_match(has, need);
}
EXPORT_SYMBOL(cl_lock_descr_match);

static void cl_lock_free(const struct lu_env *env, struct cl_lock *lock)
{
	struct cl_object *obj = lock->cll_descr.cld_obj;

	LINVRNT(!cl_lock_is_mutexed(lock));

	cl_lock_trace(D_DLMTRACE, env, "free lock", lock);
	might_sleep();
	while (!list_empty(&lock->cll_layers)) {
		struct cl_lock_slice *slice;

		slice = list_entry(lock->cll_layers.next,
				       struct cl_lock_slice, cls_linkage);
		list_del_init(lock->cll_layers.next);
		slice->cls_ops->clo_fini(env, slice);
	}
	CS_LOCK_DEC(obj, total);
	CS_LOCKSTATE_DEC(obj, lock->cll_state);
	lu_object_ref_del_at(&obj->co_lu, &lock->cll_obj_ref, "cl_lock", lock);
	cl_object_put(env, obj);
	lu_ref_fini(&lock->cll_reference);
	lu_ref_fini(&lock->cll_holders);
	mutex_destroy(&lock->cll_guard);
	OBD_SLAB_FREE_PTR(lock, cl_lock_kmem);
}

/**
 * Releases a reference on a lock.
 *
 * When last reference is released, lock is returned to the cache, unless it
 * is in cl_lock_state::CLS_FREEING state, in which case it is destroyed
 * immediately.
 *
 * \see cl_object_put(), cl_page_put()
 */
void cl_lock_put(const struct lu_env *env, struct cl_lock *lock)
{
	struct cl_object	*obj;

	LINVRNT(cl_lock_invariant(env, lock));
	obj = lock->cll_descr.cld_obj;
	LINVRNT(obj != NULL);

	CDEBUG(D_TRACE, "releasing reference: %d %p %lu\n",
	       atomic_read(&lock->cll_ref), lock, RETIP);

	if (atomic_dec_and_test(&lock->cll_ref)) {
		if (lock->cll_state == CLS_FREEING) {
			LASSERT(list_empty(&lock->cll_linkage));
			cl_lock_free(env, lock);
		}
		CS_LOCK_DEC(obj, busy);
	}
}
EXPORT_SYMBOL(cl_lock_put);

/**
 * Acquires an additional reference to a lock.
 *
 * This can be called only by caller already possessing a reference to \a
 * lock.
 *
 * \see cl_object_get(), cl_page_get()
 */
void cl_lock_get(struct cl_lock *lock)
{
	LINVRNT(cl_lock_invariant(NULL, lock));
	CDEBUG(D_TRACE, "acquiring reference: %d %p %lu\n",
	       atomic_read(&lock->cll_ref), lock, RETIP);
	atomic_inc(&lock->cll_ref);
}
EXPORT_SYMBOL(cl_lock_get);

/**
 * Acquires a reference to a lock.
 *
 * This is much like cl_lock_get(), except that this function can be used to
 * acquire initial reference to the cached lock. Caller has to deal with all
 * possible races. Use with care!
 *
 * \see cl_page_get_trust()
 */
void cl_lock_get_trust(struct cl_lock *lock)
{
	CDEBUG(D_TRACE, "acquiring trusted reference: %d %p %lu\n",
	       atomic_read(&lock->cll_ref), lock, RETIP);
	if (atomic_inc_return(&lock->cll_ref) == 1)
		CS_LOCK_INC(lock->cll_descr.cld_obj, busy);
}
EXPORT_SYMBOL(cl_lock_get_trust);

/**
 * Helper function destroying the lock that wasn't completely initialized.
 *
 * Other threads can acquire references to the top-lock through its
 * sub-locks. Hence, it cannot be cl_lock_free()-ed immediately.
 */
static void cl_lock_finish(const struct lu_env *env, struct cl_lock *lock)
{
	cl_lock_mutex_get(env, lock);
	cl_lock_cancel(env, lock);
	cl_lock_delete(env, lock);
	cl_lock_mutex_put(env, lock);
	cl_lock_put(env, lock);
}

static struct cl_lock *cl_lock_alloc(const struct lu_env *env,
				     struct cl_object *obj,
				     const struct cl_io *io,
				     const struct cl_lock_descr *descr)
{
	struct cl_lock	  *lock;
	struct lu_object_header *head;

	OBD_SLAB_ALLOC_PTR_GFP(lock, cl_lock_kmem, GFP_NOFS);
	if (lock != NULL) {
		atomic_set(&lock->cll_ref, 1);
		lock->cll_descr = *descr;
		lock->cll_state = CLS_NEW;
		cl_object_get(obj);
		lu_object_ref_add_at(&obj->co_lu, &lock->cll_obj_ref, "cl_lock",
				     lock);
		INIT_LIST_HEAD(&lock->cll_layers);
		INIT_LIST_HEAD(&lock->cll_linkage);
		INIT_LIST_HEAD(&lock->cll_inclosure);
		lu_ref_init(&lock->cll_reference);
		lu_ref_init(&lock->cll_holders);
		mutex_init(&lock->cll_guard);
		lockdep_set_class(&lock->cll_guard, &cl_lock_guard_class);
		init_waitqueue_head(&lock->cll_wq);
		head = obj->co_lu.lo_header;
		CS_LOCKSTATE_INC(obj, CLS_NEW);
		CS_LOCK_INC(obj, total);
		CS_LOCK_INC(obj, create);
		cl_lock_lockdep_init(lock);
		list_for_each_entry(obj, &head->loh_layers,
					co_lu.lo_linkage) {
			int err;

			err = obj->co_ops->coo_lock_init(env, obj, lock, io);
			if (err != 0) {
				cl_lock_finish(env, lock);
				lock = ERR_PTR(err);
				break;
			}
		}
	} else
		lock = ERR_PTR(-ENOMEM);
	return lock;
}

/**
 * Transfer the lock into INTRANSIT state and return the original state.
 *
 * \pre  state: CLS_CACHED, CLS_HELD or CLS_ENQUEUED
 * \post state: CLS_INTRANSIT
 * \see CLS_INTRANSIT
 */
enum cl_lock_state cl_lock_intransit(const struct lu_env *env,
				     struct cl_lock *lock)
{
	enum cl_lock_state state = lock->cll_state;

	LASSERT(cl_lock_is_mutexed(lock));
	LASSERT(state != CLS_INTRANSIT);
	LASSERTF(state >= CLS_ENQUEUED && state <= CLS_CACHED,
		 "Malformed lock state %d.\n", state);

	cl_lock_state_set(env, lock, CLS_INTRANSIT);
	lock->cll_intransit_owner = current;
	cl_lock_hold_add(env, lock, "intransit", current);
	return state;
}
EXPORT_SYMBOL(cl_lock_intransit);

/**
 *  Exit the intransit state and restore the lock state to the original state
 */
void cl_lock_extransit(const struct lu_env *env, struct cl_lock *lock,
		       enum cl_lock_state state)
{
	LASSERT(cl_lock_is_mutexed(lock));
	LASSERT(lock->cll_state == CLS_INTRANSIT);
	LASSERT(state != CLS_INTRANSIT);
	LASSERT(lock->cll_intransit_owner == current);

	lock->cll_intransit_owner = NULL;
	cl_lock_state_set(env, lock, state);
	cl_lock_unhold(env, lock, "intransit", current);
}
EXPORT_SYMBOL(cl_lock_extransit);

/**
 * Checking whether the lock is intransit state
 */
int cl_lock_is_intransit(struct cl_lock *lock)
{
	LASSERT(cl_lock_is_mutexed(lock));
	return lock->cll_state == CLS_INTRANSIT &&
	       lock->cll_intransit_owner != current;
}
EXPORT_SYMBOL(cl_lock_is_intransit);
/**
 * Returns true iff lock is "suitable" for given io. E.g., locks acquired by
 * truncate and O_APPEND cannot be reused for read/non-append-write, as they
 * cover multiple stripes and can trigger cascading timeouts.
 */
static int cl_lock_fits_into(const struct lu_env *env,
			     const struct cl_lock *lock,
			     const struct cl_lock_descr *need,
			     const struct cl_io *io)
{
	const struct cl_lock_slice *slice;

	LINVRNT(cl_lock_invariant_trusted(env, lock));
	list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
		if (slice->cls_ops->clo_fits_into != NULL &&
		    !slice->cls_ops->clo_fits_into(env, slice, need, io))
			return 0;
	}
	return 1;
}

static struct cl_lock *cl_lock_lookup(const struct lu_env *env,
				      struct cl_object *obj,
				      const struct cl_io *io,
				      const struct cl_lock_descr *need)
{
	struct cl_lock	  *lock;
	struct cl_object_header *head;

	head = cl_object_header(obj);
	assert_spin_locked(&head->coh_lock_guard);
	CS_LOCK_INC(obj, lookup);
	list_for_each_entry(lock, &head->coh_locks, cll_linkage) {
		int matched;

		matched = cl_lock_ext_match(&lock->cll_descr, need) &&
			  lock->cll_state < CLS_FREEING &&
			  lock->cll_error == 0 &&
			  !(lock->cll_flags & CLF_CANCELLED) &&
			  cl_lock_fits_into(env, lock, need, io);
		CDEBUG(D_DLMTRACE, "has: "DDESCR"(%d) need: "DDESCR": %d\n",
		       PDESCR(&lock->cll_descr), lock->cll_state, PDESCR(need),
		       matched);
		if (matched) {
			cl_lock_get_trust(lock);
			CS_LOCK_INC(obj, hit);
			return lock;
		}
	}
	return NULL;
}

/**
 * Returns a lock matching description \a need.
 *
 * This is the main entry point into the cl_lock caching interface. First, a
 * cache (implemented as a per-object linked list) is consulted. If lock is
 * found there, it is returned immediately. Otherwise new lock is allocated
 * and returned. In any case, additional reference to lock is acquired.
 *
 * \see cl_object_find(), cl_page_find()
 */
static struct cl_lock *cl_lock_find(const struct lu_env *env,
				    const struct cl_io *io,
				    const struct cl_lock_descr *need)
{
	struct cl_object_header *head;
	struct cl_object	*obj;
	struct cl_lock	  *lock;

	obj  = need->cld_obj;
	head = cl_object_header(obj);

	spin_lock(&head->coh_lock_guard);
	lock = cl_lock_lookup(env, obj, io, need);
	spin_unlock(&head->coh_lock_guard);

	if (lock == NULL) {
		lock = cl_lock_alloc(env, obj, io, need);
		if (!IS_ERR(lock)) {
			struct cl_lock *ghost;

			spin_lock(&head->coh_lock_guard);
			ghost = cl_lock_lookup(env, obj, io, need);
			if (ghost == NULL) {
				cl_lock_get_trust(lock);
				list_add_tail(&lock->cll_linkage,
						  &head->coh_locks);
				spin_unlock(&head->coh_lock_guard);
				CS_LOCK_INC(obj, busy);
			} else {
				spin_unlock(&head->coh_lock_guard);
				/*
				 * Other threads can acquire references to the
				 * top-lock through its sub-locks. Hence, it
				 * cannot be cl_lock_free()-ed immediately.
				 */
				cl_lock_finish(env, lock);
				lock = ghost;
			}
		}
	}
	return lock;
}

/**
 * Returns existing lock matching given description. This is similar to
 * cl_lock_find() except that no new lock is created, and returned lock is
 * guaranteed to be in enum cl_lock_state::CLS_HELD state.
 */
struct cl_lock *cl_lock_peek(const struct lu_env *env, const struct cl_io *io,
			     const struct cl_lock_descr *need,
			     const char *scope, const void *source)
{
	struct cl_object_header *head;
	struct cl_object	*obj;
	struct cl_lock	  *lock;

	obj  = need->cld_obj;
	head = cl_object_header(obj);

	do {
		spin_lock(&head->coh_lock_guard);
		lock = cl_lock_lookup(env, obj, io, need);
		spin_unlock(&head->coh_lock_guard);
		if (lock == NULL)
			return NULL;

		cl_lock_mutex_get(env, lock);
		if (lock->cll_state == CLS_INTRANSIT)
			/* Don't care return value. */
			cl_lock_state_wait(env, lock);
		if (lock->cll_state == CLS_FREEING) {
			cl_lock_mutex_put(env, lock);
			cl_lock_put(env, lock);
			lock = NULL;
		}
	} while (lock == NULL);

	cl_lock_hold_add(env, lock, scope, source);
	cl_lock_user_add(env, lock);
	if (lock->cll_state == CLS_CACHED)
		cl_use_try(env, lock, 1);
	if (lock->cll_state == CLS_HELD) {
		cl_lock_mutex_put(env, lock);
		cl_lock_lockdep_acquire(env, lock, 0);
		cl_lock_put(env, lock);
	} else {
		cl_unuse_try(env, lock);
		cl_lock_unhold(env, lock, scope, source);
		cl_lock_mutex_put(env, lock);
		cl_lock_put(env, lock);
		lock = NULL;
	}

	return lock;
}
EXPORT_SYMBOL(cl_lock_peek);

/**
 * Returns a slice within a lock, corresponding to the given layer in the
 * device stack.
 *
 * \see cl_page_at()
 */
const struct cl_lock_slice *cl_lock_at(const struct cl_lock *lock,
				       const struct lu_device_type *dtype)
{
	const struct cl_lock_slice *slice;

	LINVRNT(cl_lock_invariant_trusted(NULL, lock));

	list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
		if (slice->cls_obj->co_lu.lo_dev->ld_type == dtype)
			return slice;
	}
	return NULL;
}
EXPORT_SYMBOL(cl_lock_at);

static void cl_lock_mutex_tail(const struct lu_env *env, struct cl_lock *lock)
{
	struct cl_thread_counters *counters;

	counters = cl_lock_counters(env, lock);
	lock->cll_depth++;
	counters->ctc_nr_locks_locked++;
	lu_ref_add(&counters->ctc_locks_locked, "cll_guard", lock);
	cl_lock_trace(D_TRACE, env, "got mutex", lock);
}

/**
 * Locks cl_lock object.
 *
 * This is used to manipulate cl_lock fields, and to serialize state
 * transitions in the lock state machine.
 *
 * \post cl_lock_is_mutexed(lock)
 *
 * \see cl_lock_mutex_put()
 */
void cl_lock_mutex_get(const struct lu_env *env, struct cl_lock *lock)
{
	LINVRNT(cl_lock_invariant(env, lock));

	if (lock->cll_guarder == current) {
		LINVRNT(cl_lock_is_mutexed(lock));
		LINVRNT(lock->cll_depth > 0);
	} else {
		struct cl_object_header *hdr;
		struct cl_thread_info   *info;
		int i;

		LINVRNT(lock->cll_guarder != current);
		hdr = cl_object_header(lock->cll_descr.cld_obj);
		/*
		 * Check that mutices are taken in the bottom-to-top order.
		 */
		info = cl_env_info(env);
		for (i = 0; i < hdr->coh_nesting; ++i)
			LASSERT(info->clt_counters[i].ctc_nr_locks_locked == 0);
		mutex_lock_nested(&lock->cll_guard, hdr->coh_nesting);
		lock->cll_guarder = current;
		LINVRNT(lock->cll_depth == 0);
	}
	cl_lock_mutex_tail(env, lock);
}
EXPORT_SYMBOL(cl_lock_mutex_get);

/**
 * Try-locks cl_lock object.
 *
 * \retval 0 \a lock was successfully locked
 *
 * \retval -EBUSY \a lock cannot be locked right now
 *
 * \post ergo(result == 0, cl_lock_is_mutexed(lock))
 *
 * \see cl_lock_mutex_get()
 */
int cl_lock_mutex_try(const struct lu_env *env, struct cl_lock *lock)
{
	int result;

	LINVRNT(cl_lock_invariant_trusted(env, lock));

	result = 0;
	if (lock->cll_guarder == current) {
		LINVRNT(lock->cll_depth > 0);
		cl_lock_mutex_tail(env, lock);
	} else if (mutex_trylock(&lock->cll_guard)) {
		LINVRNT(lock->cll_depth == 0);
		lock->cll_guarder = current;
		cl_lock_mutex_tail(env, lock);
	} else
		result = -EBUSY;
	return result;
}
EXPORT_SYMBOL(cl_lock_mutex_try);

/**
 {* Unlocks cl_lock object.
 *
 * \pre cl_lock_is_mutexed(lock)
 *
 * \see cl_lock_mutex_get()
 */
void cl_lock_mutex_put(const struct lu_env *env, struct cl_lock *lock)
{
	struct cl_thread_counters *counters;

	LINVRNT(cl_lock_invariant(env, lock));
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(lock->cll_guarder == current);
	LINVRNT(lock->cll_depth > 0);

	counters = cl_lock_counters(env, lock);
	LINVRNT(counters->ctc_nr_locks_locked > 0);

	cl_lock_trace(D_TRACE, env, "put mutex", lock);
	lu_ref_del(&counters->ctc_locks_locked, "cll_guard", lock);
	counters->ctc_nr_locks_locked--;
	if (--lock->cll_depth == 0) {
		lock->cll_guarder = NULL;
		mutex_unlock(&lock->cll_guard);
	}
}
EXPORT_SYMBOL(cl_lock_mutex_put);

/**
 * Returns true iff lock's mutex is owned by the current thread.
 */
int cl_lock_is_mutexed(struct cl_lock *lock)
{
	return lock->cll_guarder == current;
}
EXPORT_SYMBOL(cl_lock_is_mutexed);

/**
 * Returns number of cl_lock mutices held by the current thread (environment).
 */
int cl_lock_nr_mutexed(const struct lu_env *env)
{
	struct cl_thread_info *info;
	int i;
	int locked;

	/*
	 * NOTE: if summation across all nesting levels (currently 2) proves
	 *       too expensive, a summary counter can be added to
	 *       struct cl_thread_info.
	 */
	info = cl_env_info(env);
	for (i = 0, locked = 0; i < ARRAY_SIZE(info->clt_counters); ++i)
		locked += info->clt_counters[i].ctc_nr_locks_locked;
	return locked;
}
EXPORT_SYMBOL(cl_lock_nr_mutexed);

static void cl_lock_cancel0(const struct lu_env *env, struct cl_lock *lock)
{
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));
	if (!(lock->cll_flags & CLF_CANCELLED)) {
		const struct cl_lock_slice *slice;

		lock->cll_flags |= CLF_CANCELLED;
		list_for_each_entry_reverse(slice, &lock->cll_layers,
						cls_linkage) {
			if (slice->cls_ops->clo_cancel != NULL)
				slice->cls_ops->clo_cancel(env, slice);
		}
	}
}

static void cl_lock_delete0(const struct lu_env *env, struct cl_lock *lock)
{
	struct cl_object_header    *head;
	const struct cl_lock_slice *slice;

	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));

	if (lock->cll_state < CLS_FREEING) {
		bool in_cache;

		LASSERT(lock->cll_state != CLS_INTRANSIT);
		cl_lock_state_set(env, lock, CLS_FREEING);

		head = cl_object_header(lock->cll_descr.cld_obj);

		spin_lock(&head->coh_lock_guard);
		in_cache = !list_empty(&lock->cll_linkage);
		if (in_cache)
			list_del_init(&lock->cll_linkage);
		spin_unlock(&head->coh_lock_guard);

		if (in_cache) /* coh_locks cache holds a refcount. */
			cl_lock_put(env, lock);

		/*
		 * From now on, no new references to this lock can be acquired
		 * by cl_lock_lookup().
		 */
		list_for_each_entry_reverse(slice, &lock->cll_layers,
						cls_linkage) {
			if (slice->cls_ops->clo_delete != NULL)
				slice->cls_ops->clo_delete(env, slice);
		}
		/*
		 * From now on, no new references to this lock can be acquired
		 * by layer-specific means (like a pointer from struct
		 * ldlm_lock in osc, or a pointer from top-lock to sub-lock in
		 * lov).
		 *
		 * Lock will be finally freed in cl_lock_put() when last of
		 * existing references goes away.
		 */
	}
}

/**
 * Mod(ifie)s cl_lock::cll_holds counter for a given lock. Also, for a
 * top-lock (nesting == 0) accounts for this modification in the per-thread
 * debugging counters. Sub-lock holds can be released by a thread different
 * from one that acquired it.
 */
static void cl_lock_hold_mod(const struct lu_env *env, struct cl_lock *lock,
			     int delta)
{
	struct cl_thread_counters *counters;
	enum clt_nesting_level     nesting;

	lock->cll_holds += delta;
	nesting = cl_lock_nesting(lock);
	if (nesting == CNL_TOP) {
		counters = &cl_env_info(env)->clt_counters[CNL_TOP];
		counters->ctc_nr_held += delta;
		LASSERT(counters->ctc_nr_held >= 0);
	}
}

/**
 * Mod(ifie)s cl_lock::cll_users counter for a given lock. See
 * cl_lock_hold_mod() for the explanation of the debugging code.
 */
static void cl_lock_used_mod(const struct lu_env *env, struct cl_lock *lock,
			     int delta)
{
	struct cl_thread_counters *counters;
	enum clt_nesting_level     nesting;

	lock->cll_users += delta;
	nesting = cl_lock_nesting(lock);
	if (nesting == CNL_TOP) {
		counters = &cl_env_info(env)->clt_counters[CNL_TOP];
		counters->ctc_nr_used += delta;
		LASSERT(counters->ctc_nr_used >= 0);
	}
}

void cl_lock_hold_release(const struct lu_env *env, struct cl_lock *lock,
			  const char *scope, const void *source)
{
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));
	LASSERT(lock->cll_holds > 0);

	cl_lock_trace(D_DLMTRACE, env, "hold release lock", lock);
	lu_ref_del(&lock->cll_holders, scope, source);
	cl_lock_hold_mod(env, lock, -1);
	if (lock->cll_holds == 0) {
		CL_LOCK_ASSERT(lock->cll_state != CLS_HELD, env, lock);
		if (lock->cll_descr.cld_mode == CLM_PHANTOM ||
		    lock->cll_descr.cld_mode == CLM_GROUP ||
		    lock->cll_state != CLS_CACHED)
			/*
			 * If lock is still phantom or grouplock when user is
			 * done with it---destroy the lock.
			 */
			lock->cll_flags |= CLF_CANCELPEND|CLF_DOOMED;
		if (lock->cll_flags & CLF_CANCELPEND) {
			lock->cll_flags &= ~CLF_CANCELPEND;
			cl_lock_cancel0(env, lock);
		}
		if (lock->cll_flags & CLF_DOOMED) {
			/* no longer doomed: it's dead... Jim. */
			lock->cll_flags &= ~CLF_DOOMED;
			cl_lock_delete0(env, lock);
		}
	}
}
EXPORT_SYMBOL(cl_lock_hold_release);

/**
 * Waits until lock state is changed.
 *
 * This function is called with cl_lock mutex locked, atomically releases
 * mutex and goes to sleep, waiting for a lock state change (signaled by
 * cl_lock_signal()), and re-acquires the mutex before return.
 *
 * This function is used to wait until lock state machine makes some progress
 * and to emulate synchronous operations on top of asynchronous lock
 * interface.
 *
 * \retval -EINTR wait was interrupted
 *
 * \retval 0 wait wasn't interrupted
 *
 * \pre cl_lock_is_mutexed(lock)
 *
 * \see cl_lock_signal()
 */
int cl_lock_state_wait(const struct lu_env *env, struct cl_lock *lock)
{
	wait_queue_t waiter;
	sigset_t blocked;
	int result;

	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));
	LASSERT(lock->cll_depth == 1);
	LASSERT(lock->cll_state != CLS_FREEING); /* too late to wait */

	cl_lock_trace(D_DLMTRACE, env, "state wait lock", lock);
	result = lock->cll_error;
	if (result == 0) {
		/* To avoid being interrupted by the 'non-fatal' signals
		 * (SIGCHLD, for instance), we'd block them temporarily.
		 * LU-305 */
		blocked = cfs_block_sigsinv(LUSTRE_FATAL_SIGS);

		init_waitqueue_entry(&waiter, current);
		add_wait_queue(&lock->cll_wq, &waiter);
		set_current_state(TASK_INTERRUPTIBLE);
		cl_lock_mutex_put(env, lock);

		LASSERT(cl_lock_nr_mutexed(env) == 0);

		/* Returning ERESTARTSYS instead of EINTR so syscalls
		 * can be restarted if signals are pending here */
		result = -ERESTARTSYS;
		if (likely(!OBD_FAIL_CHECK(OBD_FAIL_LOCK_STATE_WAIT_INTR))) {
			schedule();
			if (!cfs_signal_pending())
				result = 0;
		}

		cl_lock_mutex_get(env, lock);
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&lock->cll_wq, &waiter);

		/* Restore old blocked signals */
		cfs_restore_sigs(blocked);
	}
	return result;
}
EXPORT_SYMBOL(cl_lock_state_wait);

static void cl_lock_state_signal(const struct lu_env *env, struct cl_lock *lock,
				 enum cl_lock_state state)
{
	const struct cl_lock_slice *slice;

	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));

	list_for_each_entry(slice, &lock->cll_layers, cls_linkage)
		if (slice->cls_ops->clo_state != NULL)
			slice->cls_ops->clo_state(env, slice, state);
	wake_up_all(&lock->cll_wq);
}

/**
 * Notifies waiters that lock state changed.
 *
 * Wakes up all waiters sleeping in cl_lock_state_wait(), also notifies all
 * layers about state change by calling cl_lock_operations::clo_state()
 * top-to-bottom.
 */
void cl_lock_signal(const struct lu_env *env, struct cl_lock *lock)
{
	cl_lock_trace(D_DLMTRACE, env, "state signal lock", lock);
	cl_lock_state_signal(env, lock, lock->cll_state);
}
EXPORT_SYMBOL(cl_lock_signal);

/**
 * Changes lock state.
 *
 * This function is invoked to notify layers that lock state changed, possible
 * as a result of an asynchronous event such as call-back reception.
 *
 * \post lock->cll_state == state
 *
 * \see cl_lock_operations::clo_state()
 */
void cl_lock_state_set(const struct lu_env *env, struct cl_lock *lock,
		       enum cl_lock_state state)
{
	LASSERT(lock->cll_state <= state ||
		(lock->cll_state == CLS_CACHED &&
		 (state == CLS_HELD || /* lock found in cache */
		  state == CLS_NEW  ||   /* sub-lock canceled */
		  state == CLS_INTRANSIT)) ||
		/* lock is in transit state */
		lock->cll_state == CLS_INTRANSIT);

	if (lock->cll_state != state) {
		CS_LOCKSTATE_DEC(lock->cll_descr.cld_obj, lock->cll_state);
		CS_LOCKSTATE_INC(lock->cll_descr.cld_obj, state);

		cl_lock_state_signal(env, lock, state);
		lock->cll_state = state;
	}
}
EXPORT_SYMBOL(cl_lock_state_set);

static int cl_unuse_try_internal(const struct lu_env *env, struct cl_lock *lock)
{
	const struct cl_lock_slice *slice;
	int result;

	do {
		result = 0;

		LINVRNT(cl_lock_is_mutexed(lock));
		LINVRNT(cl_lock_invariant(env, lock));
		LASSERT(lock->cll_state == CLS_INTRANSIT);

		result = -ENOSYS;
		list_for_each_entry_reverse(slice, &lock->cll_layers,
						cls_linkage) {
			if (slice->cls_ops->clo_unuse != NULL) {
				result = slice->cls_ops->clo_unuse(env, slice);
				if (result != 0)
					break;
			}
		}
		LASSERT(result != -ENOSYS);
	} while (result == CLO_REPEAT);

	return result;
}

/**
 * Yanks lock from the cache (cl_lock_state::CLS_CACHED state) by calling
 * cl_lock_operations::clo_use() top-to-bottom to notify layers.
 * @atomic = 1, it must unuse the lock to recovery the lock to keep the
 *  use process atomic
 */
int cl_use_try(const struct lu_env *env, struct cl_lock *lock, int atomic)
{
	const struct cl_lock_slice *slice;
	int result;
	enum cl_lock_state state;

	cl_lock_trace(D_DLMTRACE, env, "use lock", lock);

	LASSERT(lock->cll_state == CLS_CACHED);
	if (lock->cll_error)
		return lock->cll_error;

	result = -ENOSYS;
	state = cl_lock_intransit(env, lock);
	list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
		if (slice->cls_ops->clo_use != NULL) {
			result = slice->cls_ops->clo_use(env, slice);
			if (result != 0)
				break;
		}
	}
	LASSERT(result != -ENOSYS);

	LASSERTF(lock->cll_state == CLS_INTRANSIT, "Wrong state %d.\n",
		 lock->cll_state);

	if (result == 0) {
		state = CLS_HELD;
	} else {
		if (result == -ESTALE) {
			/*
			 * ESTALE means sublock being cancelled
			 * at this time, and set lock state to
			 * be NEW here and ask the caller to repeat.
			 */
			state = CLS_NEW;
			result = CLO_REPEAT;
		}

		/* @atomic means back-off-on-failure. */
		if (atomic) {
			int rc;
			rc = cl_unuse_try_internal(env, lock);
			/* Vet the results. */
			if (rc < 0 && result > 0)
				result = rc;
		}

	}
	cl_lock_extransit(env, lock, state);
	return result;
}
EXPORT_SYMBOL(cl_use_try);

/**
 * Helper for cl_enqueue_try() that calls ->clo_enqueue() across all layers
 * top-to-bottom.
 */
static int cl_enqueue_kick(const struct lu_env *env,
			   struct cl_lock *lock,
			   struct cl_io *io, __u32 flags)
{
	int result;
	const struct cl_lock_slice *slice;

	result = -ENOSYS;
	list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
		if (slice->cls_ops->clo_enqueue != NULL) {
			result = slice->cls_ops->clo_enqueue(env,
							     slice, io, flags);
			if (result != 0)
				break;
		}
	}
	LASSERT(result != -ENOSYS);
	return result;
}

/**
 * Tries to enqueue a lock.
 *
 * This function is called repeatedly by cl_enqueue() until either lock is
 * enqueued, or error occurs. This function does not block waiting for
 * networking communication to complete.
 *
 * \post ergo(result == 0, lock->cll_state == CLS_ENQUEUED ||
 *			 lock->cll_state == CLS_HELD)
 *
 * \see cl_enqueue() cl_lock_operations::clo_enqueue()
 * \see cl_lock_state::CLS_ENQUEUED
 */
int cl_enqueue_try(const struct lu_env *env, struct cl_lock *lock,
		   struct cl_io *io, __u32 flags)
{
	int result;

	cl_lock_trace(D_DLMTRACE, env, "enqueue lock", lock);
	do {
		LINVRNT(cl_lock_is_mutexed(lock));

		result = lock->cll_error;
		if (result != 0)
			break;

		switch (lock->cll_state) {
		case CLS_NEW:
			cl_lock_state_set(env, lock, CLS_QUEUING);
			/* fall-through */
		case CLS_QUEUING:
			/* kick layers. */
			result = cl_enqueue_kick(env, lock, io, flags);
			/* For AGL case, the cl_lock::cll_state may
			 * become CLS_HELD already. */
			if (result == 0 && lock->cll_state == CLS_QUEUING)
				cl_lock_state_set(env, lock, CLS_ENQUEUED);
			break;
		case CLS_INTRANSIT:
			LASSERT(cl_lock_is_intransit(lock));
			result = CLO_WAIT;
			break;
		case CLS_CACHED:
			/* yank lock from the cache. */
			result = cl_use_try(env, lock, 0);
			break;
		case CLS_ENQUEUED:
		case CLS_HELD:
			result = 0;
			break;
		default:
		case CLS_FREEING:
			/*
			 * impossible, only held locks with increased
			 * ->cll_holds can be enqueued, and they cannot be
			 * freed.
			 */
			LBUG();
		}
	} while (result == CLO_REPEAT);
	return result;
}
EXPORT_SYMBOL(cl_enqueue_try);

/**
 * Cancel the conflicting lock found during previous enqueue.
 *
 * \retval 0 conflicting lock has been canceled.
 * \retval -ve error code.
 */
int cl_lock_enqueue_wait(const struct lu_env *env,
			 struct cl_lock *lock,
			 int keep_mutex)
{
	struct cl_lock  *conflict;
	int	      rc = 0;

	LASSERT(cl_lock_is_mutexed(lock));
	LASSERT(lock->cll_state == CLS_QUEUING);
	LASSERT(lock->cll_conflict != NULL);

	conflict = lock->cll_conflict;
	lock->cll_conflict = NULL;

	cl_lock_mutex_put(env, lock);
	LASSERT(cl_lock_nr_mutexed(env) == 0);

	cl_lock_mutex_get(env, conflict);
	cl_lock_trace(D_DLMTRACE, env, "enqueue wait", conflict);
	cl_lock_cancel(env, conflict);
	cl_lock_delete(env, conflict);

	while (conflict->cll_state != CLS_FREEING) {
		rc = cl_lock_state_wait(env, conflict);
		if (rc != 0)
			break;
	}
	cl_lock_mutex_put(env, conflict);
	lu_ref_del(&conflict->cll_reference, "cancel-wait", lock);
	cl_lock_put(env, conflict);

	if (keep_mutex)
		cl_lock_mutex_get(env, lock);

	LASSERT(rc <= 0);
	return rc;
}
EXPORT_SYMBOL(cl_lock_enqueue_wait);

static int cl_enqueue_locked(const struct lu_env *env, struct cl_lock *lock,
			     struct cl_io *io, __u32 enqflags)
{
	int result;

	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));
	LASSERT(lock->cll_holds > 0);

	cl_lock_user_add(env, lock);
	do {
		result = cl_enqueue_try(env, lock, io, enqflags);
		if (result == CLO_WAIT) {
			if (lock->cll_conflict != NULL)
				result = cl_lock_enqueue_wait(env, lock, 1);
			else
				result = cl_lock_state_wait(env, lock);
			if (result == 0)
				continue;
		}
		break;
	} while (1);
	if (result != 0)
		cl_unuse_try(env, lock);
	LASSERT(ergo(result == 0 && !(enqflags & CEF_AGL),
		     lock->cll_state == CLS_ENQUEUED ||
		     lock->cll_state == CLS_HELD));
	return result;
}

/**
 * Enqueues a lock.
 *
 * \pre current thread or io owns a hold on lock.
 *
 * \post ergo(result == 0, lock->users increased)
 * \post ergo(result == 0, lock->cll_state == CLS_ENQUEUED ||
 *			 lock->cll_state == CLS_HELD)
 */
int cl_enqueue(const struct lu_env *env, struct cl_lock *lock,
	       struct cl_io *io, __u32 enqflags)
{
	int result;

	cl_lock_lockdep_acquire(env, lock, enqflags);
	cl_lock_mutex_get(env, lock);
	result = cl_enqueue_locked(env, lock, io, enqflags);
	cl_lock_mutex_put(env, lock);
	if (result != 0)
		cl_lock_lockdep_release(env, lock);
	LASSERT(ergo(result == 0, lock->cll_state == CLS_ENQUEUED ||
		     lock->cll_state == CLS_HELD));
	return result;
}
EXPORT_SYMBOL(cl_enqueue);

/**
 * Tries to unlock a lock.
 *
 * This function is called to release underlying resource:
 * 1. for top lock, the resource is sublocks it held;
 * 2. for sublock, the resource is the reference to dlmlock.
 *
 * cl_unuse_try is a one-shot operation, so it must NOT return CLO_WAIT.
 *
 * \see cl_unuse() cl_lock_operations::clo_unuse()
 * \see cl_lock_state::CLS_CACHED
 */
int cl_unuse_try(const struct lu_env *env, struct cl_lock *lock)
{
	int			 result;
	enum cl_lock_state	  state = CLS_NEW;

	cl_lock_trace(D_DLMTRACE, env, "unuse lock", lock);

	if (lock->cll_users > 1) {
		cl_lock_user_del(env, lock);
		return 0;
	}

	/* Only if the lock is in CLS_HELD or CLS_ENQUEUED state, it can hold
	 * underlying resources. */
	if (!(lock->cll_state == CLS_HELD || lock->cll_state == CLS_ENQUEUED)) {
		cl_lock_user_del(env, lock);
		return 0;
	}

	/*
	 * New lock users (->cll_users) are not protecting unlocking
	 * from proceeding. From this point, lock eventually reaches
	 * CLS_CACHED, is reinitialized to CLS_NEW or fails into
	 * CLS_FREEING.
	 */
	state = cl_lock_intransit(env, lock);

	result = cl_unuse_try_internal(env, lock);
	LASSERT(lock->cll_state == CLS_INTRANSIT);
	LASSERT(result != CLO_WAIT);
	cl_lock_user_del(env, lock);
	if (result == 0 || result == -ESTALE) {
		/*
		 * Return lock back to the cache. This is the only
		 * place where lock is moved into CLS_CACHED state.
		 *
		 * If one of ->clo_unuse() methods returned -ESTALE, lock
		 * cannot be placed into cache and has to be
		 * re-initialized. This happens e.g., when a sub-lock was
		 * canceled while unlocking was in progress.
		 */
		if (state == CLS_HELD && result == 0)
			state = CLS_CACHED;
		else
			state = CLS_NEW;
		cl_lock_extransit(env, lock, state);

		/*
		 * Hide -ESTALE error.
		 * If the lock is a glimpse lock, and it has multiple
		 * stripes. Assuming that one of its sublock returned -ENAVAIL,
		 * and other sublocks are matched write locks. In this case,
		 * we can't set this lock to error because otherwise some of
		 * its sublocks may not be canceled. This causes some dirty
		 * pages won't be written to OSTs. -jay
		 */
		result = 0;
	} else {
		CERROR("result = %d, this is unlikely!\n", result);
		state = CLS_NEW;
		cl_lock_extransit(env, lock, state);
	}
	return result ?: lock->cll_error;
}
EXPORT_SYMBOL(cl_unuse_try);

static void cl_unuse_locked(const struct lu_env *env, struct cl_lock *lock)
{
	int result;

	result = cl_unuse_try(env, lock);
	if (result)
		CL_LOCK_DEBUG(D_ERROR, env, lock, "unuse return %d\n", result);
}

/**
 * Unlocks a lock.
 */
void cl_unuse(const struct lu_env *env, struct cl_lock *lock)
{
	cl_lock_mutex_get(env, lock);
	cl_unuse_locked(env, lock);
	cl_lock_mutex_put(env, lock);
	cl_lock_lockdep_release(env, lock);
}
EXPORT_SYMBOL(cl_unuse);

/**
 * Tries to wait for a lock.
 *
 * This function is called repeatedly by cl_wait() until either lock is
 * granted, or error occurs. This function does not block waiting for network
 * communication to complete.
 *
 * \see cl_wait() cl_lock_operations::clo_wait()
 * \see cl_lock_state::CLS_HELD
 */
int cl_wait_try(const struct lu_env *env, struct cl_lock *lock)
{
	const struct cl_lock_slice *slice;
	int			 result;

	cl_lock_trace(D_DLMTRACE, env, "wait lock try", lock);
	do {
		LINVRNT(cl_lock_is_mutexed(lock));
		LINVRNT(cl_lock_invariant(env, lock));
		LASSERTF(lock->cll_state == CLS_QUEUING ||
			 lock->cll_state == CLS_ENQUEUED ||
			 lock->cll_state == CLS_HELD ||
			 lock->cll_state == CLS_INTRANSIT,
			 "lock state: %d\n", lock->cll_state);
		LASSERT(lock->cll_users > 0);
		LASSERT(lock->cll_holds > 0);

		result = lock->cll_error;
		if (result != 0)
			break;

		if (cl_lock_is_intransit(lock)) {
			result = CLO_WAIT;
			break;
		}

		if (lock->cll_state == CLS_HELD)
			/* nothing to do */
			break;

		result = -ENOSYS;
		list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
			if (slice->cls_ops->clo_wait != NULL) {
				result = slice->cls_ops->clo_wait(env, slice);
				if (result != 0)
					break;
			}
		}
		LASSERT(result != -ENOSYS);
		if (result == 0) {
			LASSERT(lock->cll_state != CLS_INTRANSIT);
			cl_lock_state_set(env, lock, CLS_HELD);
		}
	} while (result == CLO_REPEAT);
	return result;
}
EXPORT_SYMBOL(cl_wait_try);

/**
 * Waits until enqueued lock is granted.
 *
 * \pre current thread or io owns a hold on the lock
 * \pre ergo(result == 0, lock->cll_state == CLS_ENQUEUED ||
 *			lock->cll_state == CLS_HELD)
 *
 * \post ergo(result == 0, lock->cll_state == CLS_HELD)
 */
int cl_wait(const struct lu_env *env, struct cl_lock *lock)
{
	int result;

	cl_lock_mutex_get(env, lock);

	LINVRNT(cl_lock_invariant(env, lock));
	LASSERTF(lock->cll_state == CLS_ENQUEUED || lock->cll_state == CLS_HELD,
		 "Wrong state %d \n", lock->cll_state);
	LASSERT(lock->cll_holds > 0);

	do {
		result = cl_wait_try(env, lock);
		if (result == CLO_WAIT) {
			result = cl_lock_state_wait(env, lock);
			if (result == 0)
				continue;
		}
		break;
	} while (1);
	if (result < 0) {
		cl_unuse_try(env, lock);
		cl_lock_lockdep_release(env, lock);
	}
	cl_lock_trace(D_DLMTRACE, env, "wait lock", lock);
	cl_lock_mutex_put(env, lock);
	LASSERT(ergo(result == 0, lock->cll_state == CLS_HELD));
	return result;
}
EXPORT_SYMBOL(cl_wait);

/**
 * Executes cl_lock_operations::clo_weigh(), and sums results to estimate lock
 * value.
 */
unsigned long cl_lock_weigh(const struct lu_env *env, struct cl_lock *lock)
{
	const struct cl_lock_slice *slice;
	unsigned long pound;
	unsigned long ounce;

	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));

	pound = 0;
	list_for_each_entry_reverse(slice, &lock->cll_layers, cls_linkage) {
		if (slice->cls_ops->clo_weigh != NULL) {
			ounce = slice->cls_ops->clo_weigh(env, slice);
			pound += ounce;
			if (pound < ounce) /* over-weight^Wflow */
				pound = ~0UL;
		}
	}
	return pound;
}
EXPORT_SYMBOL(cl_lock_weigh);

/**
 * Notifies layers that lock description changed.
 *
 * The server can grant client a lock different from one that was requested
 * (e.g., larger in extent). This method is called when actually granted lock
 * description becomes known to let layers to accommodate for changed lock
 * description.
 *
 * \see cl_lock_operations::clo_modify()
 */
int cl_lock_modify(const struct lu_env *env, struct cl_lock *lock,
		   const struct cl_lock_descr *desc)
{
	const struct cl_lock_slice *slice;
	struct cl_object	   *obj = lock->cll_descr.cld_obj;
	struct cl_object_header    *hdr = cl_object_header(obj);
	int result;

	cl_lock_trace(D_DLMTRACE, env, "modify lock", lock);
	/* don't allow object to change */
	LASSERT(obj == desc->cld_obj);
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));

	list_for_each_entry_reverse(slice, &lock->cll_layers, cls_linkage) {
		if (slice->cls_ops->clo_modify != NULL) {
			result = slice->cls_ops->clo_modify(env, slice, desc);
			if (result != 0)
				return result;
		}
	}
	CL_LOCK_DEBUG(D_DLMTRACE, env, lock, " -> "DDESCR"@"DFID"\n",
		      PDESCR(desc), PFID(lu_object_fid(&desc->cld_obj->co_lu)));
	/*
	 * Just replace description in place. Nothing more is needed for
	 * now. If locks were indexed according to their extent and/or mode,
	 * that index would have to be updated here.
	 */
	spin_lock(&hdr->coh_lock_guard);
	lock->cll_descr = *desc;
	spin_unlock(&hdr->coh_lock_guard);
	return 0;
}
EXPORT_SYMBOL(cl_lock_modify);

/**
 * Initializes lock closure with a given origin.
 *
 * \see cl_lock_closure
 */
void cl_lock_closure_init(const struct lu_env *env,
			  struct cl_lock_closure *closure,
			  struct cl_lock *origin, int wait)
{
	LINVRNT(cl_lock_is_mutexed(origin));
	LINVRNT(cl_lock_invariant(env, origin));

	INIT_LIST_HEAD(&closure->clc_list);
	closure->clc_origin = origin;
	closure->clc_wait   = wait;
	closure->clc_nr     = 0;
}
EXPORT_SYMBOL(cl_lock_closure_init);

/**
 * Builds a closure of \a lock.
 *
 * Building of a closure consists of adding initial lock (\a lock) into it,
 * and calling cl_lock_operations::clo_closure() methods of \a lock. These
 * methods might call cl_lock_closure_build() recursively again, adding more
 * locks to the closure, etc.
 *
 * \see cl_lock_closure
 */
int cl_lock_closure_build(const struct lu_env *env, struct cl_lock *lock,
			  struct cl_lock_closure *closure)
{
	const struct cl_lock_slice *slice;
	int result;

	LINVRNT(cl_lock_is_mutexed(closure->clc_origin));
	LINVRNT(cl_lock_invariant(env, closure->clc_origin));

	result = cl_lock_enclosure(env, lock, closure);
	if (result == 0) {
		list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
			if (slice->cls_ops->clo_closure != NULL) {
				result = slice->cls_ops->clo_closure(env, slice,
								     closure);
				if (result != 0)
					break;
			}
		}
	}
	if (result != 0)
		cl_lock_disclosure(env, closure);
	return result;
}
EXPORT_SYMBOL(cl_lock_closure_build);

/**
 * Adds new lock to a closure.
 *
 * Try-locks \a lock and if succeeded, adds it to the closure (never more than
 * once). If try-lock failed, returns CLO_REPEAT, after optionally waiting
 * until next try-lock is likely to succeed.
 */
int cl_lock_enclosure(const struct lu_env *env, struct cl_lock *lock,
		      struct cl_lock_closure *closure)
{
	int result = 0;

	cl_lock_trace(D_DLMTRACE, env, "enclosure lock", lock);
	if (!cl_lock_mutex_try(env, lock)) {
		/*
		 * If lock->cll_inclosure is not empty, lock is already in
		 * this closure.
		 */
		if (list_empty(&lock->cll_inclosure)) {
			cl_lock_get_trust(lock);
			lu_ref_add(&lock->cll_reference, "closure", closure);
			list_add(&lock->cll_inclosure, &closure->clc_list);
			closure->clc_nr++;
		} else
			cl_lock_mutex_put(env, lock);
		result = 0;
	} else {
		cl_lock_disclosure(env, closure);
		if (closure->clc_wait) {
			cl_lock_get_trust(lock);
			lu_ref_add(&lock->cll_reference, "closure-w", closure);
			cl_lock_mutex_put(env, closure->clc_origin);

			LASSERT(cl_lock_nr_mutexed(env) == 0);
			cl_lock_mutex_get(env, lock);
			cl_lock_mutex_put(env, lock);

			cl_lock_mutex_get(env, closure->clc_origin);
			lu_ref_del(&lock->cll_reference, "closure-w", closure);
			cl_lock_put(env, lock);
		}
		result = CLO_REPEAT;
	}
	return result;
}
EXPORT_SYMBOL(cl_lock_enclosure);

/** Releases mutices of enclosed locks. */
void cl_lock_disclosure(const struct lu_env *env,
			struct cl_lock_closure *closure)
{
	struct cl_lock *scan;
	struct cl_lock *temp;

	cl_lock_trace(D_DLMTRACE, env, "disclosure lock", closure->clc_origin);
	list_for_each_entry_safe(scan, temp, &closure->clc_list,
				     cll_inclosure){
		list_del_init(&scan->cll_inclosure);
		cl_lock_mutex_put(env, scan);
		lu_ref_del(&scan->cll_reference, "closure", closure);
		cl_lock_put(env, scan);
		closure->clc_nr--;
	}
	LASSERT(closure->clc_nr == 0);
}
EXPORT_SYMBOL(cl_lock_disclosure);

/** Finalizes a closure. */
void cl_lock_closure_fini(struct cl_lock_closure *closure)
{
	LASSERT(closure->clc_nr == 0);
	LASSERT(list_empty(&closure->clc_list));
}
EXPORT_SYMBOL(cl_lock_closure_fini);

/**
 * Destroys this lock. Notifies layers (bottom-to-top) that lock is being
 * destroyed, then destroy the lock. If there are holds on the lock, postpone
 * destruction until all holds are released. This is called when a decision is
 * made to destroy the lock in the future. E.g., when a blocking AST is
 * received on it, or fatal communication error happens.
 *
 * Caller must have a reference on this lock to prevent a situation, when
 * deleted lock lingers in memory for indefinite time, because nobody calls
 * cl_lock_put() to finish it.
 *
 * \pre atomic_read(&lock->cll_ref) > 0
 * \pre ergo(cl_lock_nesting(lock) == CNL_TOP,
 *	   cl_lock_nr_mutexed(env) == 1)
 *      [i.e., if a top-lock is deleted, mutices of no other locks can be
 *      held, as deletion of sub-locks might require releasing a top-lock
 *      mutex]
 *
 * \see cl_lock_operations::clo_delete()
 * \see cl_lock::cll_holds
 */
void cl_lock_delete(const struct lu_env *env, struct cl_lock *lock)
{
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));
	LASSERT(ergo(cl_lock_nesting(lock) == CNL_TOP,
		     cl_lock_nr_mutexed(env) == 1));

	cl_lock_trace(D_DLMTRACE, env, "delete lock", lock);
	if (lock->cll_holds == 0)
		cl_lock_delete0(env, lock);
	else
		lock->cll_flags |= CLF_DOOMED;
}
EXPORT_SYMBOL(cl_lock_delete);

/**
 * Mark lock as irrecoverably failed, and mark it for destruction. This
 * happens when, e.g., server fails to grant a lock to us, or networking
 * time-out happens.
 *
 * \pre atomic_read(&lock->cll_ref) > 0
 *
 * \see clo_lock_delete()
 * \see cl_lock::cll_holds
 */
void cl_lock_error(const struct lu_env *env, struct cl_lock *lock, int error)
{
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));

	if (lock->cll_error == 0 && error != 0) {
		cl_lock_trace(D_DLMTRACE, env, "set lock error", lock);
		lock->cll_error = error;
		cl_lock_signal(env, lock);
		cl_lock_cancel(env, lock);
		cl_lock_delete(env, lock);
	}
}
EXPORT_SYMBOL(cl_lock_error);

/**
 * Cancels this lock. Notifies layers
 * (bottom-to-top) that lock is being cancelled, then destroy the lock. If
 * there are holds on the lock, postpone cancellation until
 * all holds are released.
 *
 * Cancellation notification is delivered to layers at most once.
 *
 * \see cl_lock_operations::clo_cancel()
 * \see cl_lock::cll_holds
 */
void cl_lock_cancel(const struct lu_env *env, struct cl_lock *lock)
{
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));

	cl_lock_trace(D_DLMTRACE, env, "cancel lock", lock);
	if (lock->cll_holds == 0)
		cl_lock_cancel0(env, lock);
	else
		lock->cll_flags |= CLF_CANCELPEND;
}
EXPORT_SYMBOL(cl_lock_cancel);

/**
 * Finds an existing lock covering given index and optionally different from a
 * given \a except lock.
 */
struct cl_lock *cl_lock_at_pgoff(const struct lu_env *env,
				 struct cl_object *obj, pgoff_t index,
				 struct cl_lock *except,
				 int pending, int canceld)
{
	struct cl_object_header *head;
	struct cl_lock	  *scan;
	struct cl_lock	  *lock;
	struct cl_lock_descr    *need;

	head = cl_object_header(obj);
	need = &cl_env_info(env)->clt_descr;
	lock = NULL;

	need->cld_mode = CLM_READ; /* CLM_READ matches both READ & WRITE, but
				    * not PHANTOM */
	need->cld_start = need->cld_end = index;
	need->cld_enq_flags = 0;

	spin_lock(&head->coh_lock_guard);
	/* It is fine to match any group lock since there could be only one
	 * with a uniq gid and it conflicts with all other lock modes too */
	list_for_each_entry(scan, &head->coh_locks, cll_linkage) {
		if (scan != except &&
		    (scan->cll_descr.cld_mode == CLM_GROUP ||
		    cl_lock_ext_match(&scan->cll_descr, need)) &&
		    scan->cll_state >= CLS_HELD &&
		    scan->cll_state < CLS_FREEING &&
		    /*
		     * This check is racy as the lock can be canceled right
		     * after it is done, but this is fine, because page exists
		     * already.
		     */
		    (canceld || !(scan->cll_flags & CLF_CANCELLED)) &&
		    (pending || !(scan->cll_flags & CLF_CANCELPEND))) {
			/* Don't increase cs_hit here since this
			 * is just a helper function. */
			cl_lock_get_trust(scan);
			lock = scan;
			break;
		}
	}
	spin_unlock(&head->coh_lock_guard);
	return lock;
}
EXPORT_SYMBOL(cl_lock_at_pgoff);

/**
 * Calculate the page offset at the layer of @lock.
 * At the time of this writing, @page is top page and @lock is sub lock.
 */
static pgoff_t pgoff_at_lock(struct cl_page *page, struct cl_lock *lock)
{
	struct lu_device_type *dtype;
	const struct cl_page_slice *slice;

	dtype = lock->cll_descr.cld_obj->co_lu.lo_dev->ld_type;
	slice = cl_page_at(page, dtype);
	LASSERT(slice != NULL);
	return slice->cpl_page->cp_index;
}

/**
 * Check if page @page is covered by an extra lock or discard it.
 */
static int check_and_discard_cb(const struct lu_env *env, struct cl_io *io,
				struct cl_page *page, void *cbdata)
{
	struct cl_thread_info *info = cl_env_info(env);
	struct cl_lock *lock = cbdata;
	pgoff_t index = pgoff_at_lock(page, lock);

	if (index >= info->clt_fn_index) {
		struct cl_lock *tmp;

		/* refresh non-overlapped index */
		tmp = cl_lock_at_pgoff(env, lock->cll_descr.cld_obj, index,
					lock, 1, 0);
		if (tmp != NULL) {
			/* Cache the first-non-overlapped index so as to skip
			 * all pages within [index, clt_fn_index). This
			 * is safe because if tmp lock is canceled, it will
			 * discard these pages. */
			info->clt_fn_index = tmp->cll_descr.cld_end + 1;
			if (tmp->cll_descr.cld_end == CL_PAGE_EOF)
				info->clt_fn_index = CL_PAGE_EOF;
			cl_lock_put(env, tmp);
		} else if (cl_page_own(env, io, page) == 0) {
			/* discard the page */
			cl_page_unmap(env, io, page);
			cl_page_discard(env, io, page);
			cl_page_disown(env, io, page);
		} else {
			LASSERT(page->cp_state == CPS_FREEING);
		}
	}

	info->clt_next_index = index + 1;
	return CLP_GANG_OKAY;
}

static int discard_cb(const struct lu_env *env, struct cl_io *io,
		      struct cl_page *page, void *cbdata)
{
	struct cl_thread_info *info = cl_env_info(env);
	struct cl_lock *lock   = cbdata;

	LASSERT(lock->cll_descr.cld_mode >= CLM_WRITE);
	KLASSERT(ergo(page->cp_type == CPT_CACHEABLE,
		      !PageWriteback(cl_page_vmpage(env, page))));
	KLASSERT(ergo(page->cp_type == CPT_CACHEABLE,
		      !PageDirty(cl_page_vmpage(env, page))));

	info->clt_next_index = pgoff_at_lock(page, lock) + 1;
	if (cl_page_own(env, io, page) == 0) {
		/* discard the page */
		cl_page_unmap(env, io, page);
		cl_page_discard(env, io, page);
		cl_page_disown(env, io, page);
	} else {
		LASSERT(page->cp_state == CPS_FREEING);
	}

	return CLP_GANG_OKAY;
}

/**
 * Discard pages protected by the given lock. This function traverses radix
 * tree to find all covering pages and discard them. If a page is being covered
 * by other locks, it should remain in cache.
 *
 * If error happens on any step, the process continues anyway (the reasoning
 * behind this being that lock cancellation cannot be delayed indefinitely).
 */
int cl_lock_discard_pages(const struct lu_env *env, struct cl_lock *lock)
{
	struct cl_thread_info *info  = cl_env_info(env);
	struct cl_io	  *io    = &info->clt_io;
	struct cl_lock_descr  *descr = &lock->cll_descr;
	cl_page_gang_cb_t      cb;
	int res;
	int result;

	LINVRNT(cl_lock_invariant(env, lock));

	io->ci_obj = cl_object_top(descr->cld_obj);
	io->ci_ignore_layout = 1;
	result = cl_io_init(env, io, CIT_MISC, io->ci_obj);
	if (result != 0)
		goto out;

	cb = descr->cld_mode == CLM_READ ? check_and_discard_cb : discard_cb;
	info->clt_fn_index = info->clt_next_index = descr->cld_start;
	do {
		res = cl_page_gang_lookup(env, descr->cld_obj, io,
					  info->clt_next_index, descr->cld_end,
					  cb, (void *)lock);
		if (info->clt_next_index > descr->cld_end)
			break;

		if (res == CLP_GANG_RESCHED)
			cond_resched();
	} while (res != CLP_GANG_OKAY);
out:
	cl_io_fini(env, io);
	return result;
}
EXPORT_SYMBOL(cl_lock_discard_pages);

/**
 * Eliminate all locks for a given object.
 *
 * Caller has to guarantee that no lock is in active use.
 *
 * \param cancel when this is set, cl_locks_prune() cancels locks before
 *	       destroying.
 */
void cl_locks_prune(const struct lu_env *env, struct cl_object *obj, int cancel)
{
	struct cl_object_header *head;
	struct cl_lock	  *lock;

	head = cl_object_header(obj);
	/*
	 * If locks are destroyed without cancellation, all pages must be
	 * already destroyed (as otherwise they will be left unprotected).
	 */
	LASSERT(ergo(!cancel,
		     head->coh_tree.rnode == NULL && head->coh_pages == 0));

	spin_lock(&head->coh_lock_guard);
	while (!list_empty(&head->coh_locks)) {
		lock = container_of(head->coh_locks.next,
				    struct cl_lock, cll_linkage);
		cl_lock_get_trust(lock);
		spin_unlock(&head->coh_lock_guard);
		lu_ref_add(&lock->cll_reference, "prune", current);

again:
		cl_lock_mutex_get(env, lock);
		if (lock->cll_state < CLS_FREEING) {
			LASSERT(lock->cll_users <= 1);
			if (unlikely(lock->cll_users == 1)) {
				struct l_wait_info lwi = { 0 };

				cl_lock_mutex_put(env, lock);
				l_wait_event(lock->cll_wq,
					     lock->cll_users == 0,
					     &lwi);
				goto again;
			}

			if (cancel)
				cl_lock_cancel(env, lock);
			cl_lock_delete(env, lock);
		}
		cl_lock_mutex_put(env, lock);
		lu_ref_del(&lock->cll_reference, "prune", current);
		cl_lock_put(env, lock);
		spin_lock(&head->coh_lock_guard);
	}
	spin_unlock(&head->coh_lock_guard);
}
EXPORT_SYMBOL(cl_locks_prune);

static struct cl_lock *cl_lock_hold_mutex(const struct lu_env *env,
					  const struct cl_io *io,
					  const struct cl_lock_descr *need,
					  const char *scope, const void *source)
{
	struct cl_lock *lock;

	while (1) {
		lock = cl_lock_find(env, io, need);
		if (IS_ERR(lock))
			break;
		cl_lock_mutex_get(env, lock);
		if (lock->cll_state < CLS_FREEING &&
		    !(lock->cll_flags & CLF_CANCELLED)) {
			cl_lock_hold_mod(env, lock, +1);
			lu_ref_add(&lock->cll_holders, scope, source);
			lu_ref_add(&lock->cll_reference, scope, source);
			break;
		}
		cl_lock_mutex_put(env, lock);
		cl_lock_put(env, lock);
	}
	return lock;
}

/**
 * Returns a lock matching \a need description with a reference and a hold on
 * it.
 *
 * This is much like cl_lock_find(), except that cl_lock_hold() additionally
 * guarantees that lock is not in the CLS_FREEING state on return.
 */
struct cl_lock *cl_lock_hold(const struct lu_env *env, const struct cl_io *io,
			     const struct cl_lock_descr *need,
			     const char *scope, const void *source)
{
	struct cl_lock *lock;

	lock = cl_lock_hold_mutex(env, io, need, scope, source);
	if (!IS_ERR(lock))
		cl_lock_mutex_put(env, lock);
	return lock;
}
EXPORT_SYMBOL(cl_lock_hold);

/**
 * Main high-level entry point of cl_lock interface that finds existing or
 * enqueues new lock matching given description.
 */
struct cl_lock *cl_lock_request(const struct lu_env *env, struct cl_io *io,
				const struct cl_lock_descr *need,
				const char *scope, const void *source)
{
	struct cl_lock       *lock;
	int		   rc;
	__u32		 enqflags = need->cld_enq_flags;

	do {
		lock = cl_lock_hold_mutex(env, io, need, scope, source);
		if (IS_ERR(lock))
			break;

		rc = cl_enqueue_locked(env, lock, io, enqflags);
		if (rc == 0) {
			if (cl_lock_fits_into(env, lock, need, io)) {
				if (!(enqflags & CEF_AGL)) {
					cl_lock_mutex_put(env, lock);
					cl_lock_lockdep_acquire(env, lock,
								enqflags);
					break;
				}
				rc = 1;
			}
			cl_unuse_locked(env, lock);
		}
		cl_lock_trace(D_DLMTRACE, env,
			      rc <= 0 ? "enqueue failed" : "agl succeed", lock);
		cl_lock_hold_release(env, lock, scope, source);
		cl_lock_mutex_put(env, lock);
		lu_ref_del(&lock->cll_reference, scope, source);
		cl_lock_put(env, lock);
		if (rc > 0) {
			LASSERT(enqflags & CEF_AGL);
			lock = NULL;
		} else if (rc != 0) {
			lock = ERR_PTR(rc);
		}
	} while (rc == 0);
	return lock;
}
EXPORT_SYMBOL(cl_lock_request);

/**
 * Adds a hold to a known lock.
 */
void cl_lock_hold_add(const struct lu_env *env, struct cl_lock *lock,
		      const char *scope, const void *source)
{
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));
	LASSERT(lock->cll_state != CLS_FREEING);

	cl_lock_hold_mod(env, lock, +1);
	cl_lock_get(lock);
	lu_ref_add(&lock->cll_holders, scope, source);
	lu_ref_add(&lock->cll_reference, scope, source);
}
EXPORT_SYMBOL(cl_lock_hold_add);

/**
 * Releases a hold and a reference on a lock, on which caller acquired a
 * mutex.
 */
void cl_lock_unhold(const struct lu_env *env, struct cl_lock *lock,
		    const char *scope, const void *source)
{
	LINVRNT(cl_lock_invariant(env, lock));
	cl_lock_hold_release(env, lock, scope, source);
	lu_ref_del(&lock->cll_reference, scope, source);
	cl_lock_put(env, lock);
}
EXPORT_SYMBOL(cl_lock_unhold);

/**
 * Releases a hold and a reference on a lock, obtained by cl_lock_hold().
 */
void cl_lock_release(const struct lu_env *env, struct cl_lock *lock,
		     const char *scope, const void *source)
{
	LINVRNT(cl_lock_invariant(env, lock));
	cl_lock_trace(D_DLMTRACE, env, "release lock", lock);
	cl_lock_mutex_get(env, lock);
	cl_lock_hold_release(env, lock, scope, source);
	cl_lock_mutex_put(env, lock);
	lu_ref_del(&lock->cll_reference, scope, source);
	cl_lock_put(env, lock);
}
EXPORT_SYMBOL(cl_lock_release);

void cl_lock_user_add(const struct lu_env *env, struct cl_lock *lock)
{
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));

	cl_lock_used_mod(env, lock, +1);
}
EXPORT_SYMBOL(cl_lock_user_add);

void cl_lock_user_del(const struct lu_env *env, struct cl_lock *lock)
{
	LINVRNT(cl_lock_is_mutexed(lock));
	LINVRNT(cl_lock_invariant(env, lock));
	LASSERT(lock->cll_users > 0);

	cl_lock_used_mod(env, lock, -1);
	if (lock->cll_users == 0)
		wake_up_all(&lock->cll_wq);
}
EXPORT_SYMBOL(cl_lock_user_del);

const char *cl_lock_mode_name(const enum cl_lock_mode mode)
{
	static const char *names[] = {
		[CLM_PHANTOM] = "P",
		[CLM_READ]    = "R",
		[CLM_WRITE]   = "W",
		[CLM_GROUP]   = "G"
	};
	if (0 <= mode && mode < ARRAY_SIZE(names))
		return names[mode];
	else
		return "U";
}
EXPORT_SYMBOL(cl_lock_mode_name);

/**
 * Prints human readable representation of a lock description.
 */
void cl_lock_descr_print(const struct lu_env *env, void *cookie,
		       lu_printer_t printer,
		       const struct cl_lock_descr *descr)
{
	const struct lu_fid  *fid;

	fid = lu_object_fid(&descr->cld_obj->co_lu);
	(*printer)(env, cookie, DDESCR"@"DFID, PDESCR(descr), PFID(fid));
}
EXPORT_SYMBOL(cl_lock_descr_print);

/**
 * Prints human readable representation of \a lock to the \a f.
 */
void cl_lock_print(const struct lu_env *env, void *cookie,
		   lu_printer_t printer, const struct cl_lock *lock)
{
	const struct cl_lock_slice *slice;
	(*printer)(env, cookie, "lock@%p[%d %d %d %d %d %08lx] ",
		   lock, atomic_read(&lock->cll_ref),
		   lock->cll_state, lock->cll_error, lock->cll_holds,
		   lock->cll_users, lock->cll_flags);
	cl_lock_descr_print(env, cookie, printer, &lock->cll_descr);
	(*printer)(env, cookie, " {\n");

	list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
		(*printer)(env, cookie, "    %s@%p: ",
			   slice->cls_obj->co_lu.lo_dev->ld_type->ldt_name,
			   slice);
		if (slice->cls_ops->clo_print != NULL)
			slice->cls_ops->clo_print(env, cookie, printer, slice);
		(*printer)(env, cookie, "\n");
	}
	(*printer)(env, cookie, "} lock@%p\n", lock);
}
EXPORT_SYMBOL(cl_lock_print);

int cl_lock_init(void)
{
	return lu_kmem_init(cl_lock_caches);
}

void cl_lock_fini(void)
{
	lu_kmem_fini(cl_lock_caches);
}
