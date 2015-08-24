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
 * Implementation of cl_lock for LOV layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_LOV

#include "lov_cl_internal.h"

/** \addtogroup lov
 *  @{
 */

static struct cl_lock_closure *lov_closure_get(const struct lu_env *env,
					       struct cl_lock *parent);

static int lov_lock_unuse(const struct lu_env *env,
			  const struct cl_lock_slice *slice);
/*****************************************************************************
 *
 * Lov lock operations.
 *
 */

static struct lov_sublock_env *lov_sublock_env_get(const struct lu_env *env,
						   struct cl_lock *parent,
						   struct lov_lock_sub *lls)
{
	struct lov_sublock_env *subenv;
	struct lov_io	  *lio    = lov_env_io(env);
	struct cl_io	   *io     = lio->lis_cl.cis_io;
	struct lov_io_sub      *sub;

	subenv = &lov_env_session(env)->ls_subenv;

	/*
	 * FIXME: We tend to use the subio's env & io to call the sublock
	 * lock operations because osc lock sometimes stores some control
	 * variables in thread's IO information(Now only lockless information).
	 * However, if the lock's host(object) is different from the object
	 * for current IO, we have no way to get the subenv and subio because
	 * they are not initialized at all. As a temp fix, in this case,
	 * we still borrow the parent's env to call sublock operations.
	 */
	if (!io || !cl_object_same(io->ci_obj, parent->cll_descr.cld_obj)) {
		subenv->lse_env = env;
		subenv->lse_io  = io;
		subenv->lse_sub = NULL;
	} else {
		sub = lov_sub_get(env, lio, lls->sub_stripe);
		if (!IS_ERR(sub)) {
			subenv->lse_env = sub->sub_env;
			subenv->lse_io  = sub->sub_io;
			subenv->lse_sub = sub;
		} else {
			subenv = (void *)sub;
		}
	}
	return subenv;
}

static void lov_sublock_env_put(struct lov_sublock_env *subenv)
{
	if (subenv && subenv->lse_sub)
		lov_sub_put(subenv->lse_sub);
}

static void lov_sublock_adopt(const struct lu_env *env, struct lov_lock *lck,
			      struct cl_lock *sublock, int idx,
			      struct lov_lock_link *link)
{
	struct lovsub_lock *lsl;
	struct cl_lock     *parent = lck->lls_cl.cls_lock;
	int		 rc;

	LASSERT(cl_lock_is_mutexed(parent));
	LASSERT(cl_lock_is_mutexed(sublock));

	lsl = cl2sub_lock(sublock);
	/*
	 * check that sub-lock doesn't have lock link to this top-lock.
	 */
	LASSERT(lov_lock_link_find(env, lck, lsl) == NULL);
	LASSERT(idx < lck->lls_nr);

	lck->lls_sub[idx].sub_lock = lsl;
	lck->lls_nr_filled++;
	LASSERT(lck->lls_nr_filled <= lck->lls_nr);
	list_add_tail(&link->lll_list, &lsl->lss_parents);
	link->lll_idx = idx;
	link->lll_super = lck;
	cl_lock_get(parent);
	lu_ref_add(&parent->cll_reference, "lov-child", sublock);
	lck->lls_sub[idx].sub_flags |= LSF_HELD;
	cl_lock_user_add(env, sublock);

	rc = lov_sublock_modify(env, lck, lsl, &sublock->cll_descr, idx);
	LASSERT(rc == 0); /* there is no way this can fail, currently */
}

static struct cl_lock *lov_sublock_alloc(const struct lu_env *env,
					 const struct cl_io *io,
					 struct lov_lock *lck,
					 int idx, struct lov_lock_link **out)
{
	struct cl_lock       *sublock;
	struct cl_lock       *parent;
	struct lov_lock_link *link;

	LASSERT(idx < lck->lls_nr);

	OBD_SLAB_ALLOC_PTR_GFP(link, lov_lock_link_kmem, GFP_NOFS);
	if (link != NULL) {
		struct lov_sublock_env *subenv;
		struct lov_lock_sub  *lls;
		struct cl_lock_descr *descr;

		parent = lck->lls_cl.cls_lock;
		lls    = &lck->lls_sub[idx];
		descr  = &lls->sub_got;

		subenv = lov_sublock_env_get(env, parent, lls);
		if (!IS_ERR(subenv)) {
			/* CAVEAT: Don't try to add a field in lov_lock_sub
			 * to remember the subio. This is because lock is able
			 * to be cached, but this is not true for IO. This
			 * further means a sublock might be referenced in
			 * different io context. -jay */

			sublock = cl_lock_hold(subenv->lse_env, subenv->lse_io,
					       descr, "lov-parent", parent);
			lov_sublock_env_put(subenv);
		} else {
			/* error occurs. */
			sublock = (void *)subenv;
		}

		if (!IS_ERR(sublock))
			*out = link;
		else
			OBD_SLAB_FREE_PTR(link, lov_lock_link_kmem);
	} else
		sublock = ERR_PTR(-ENOMEM);
	return sublock;
}

static void lov_sublock_unlock(const struct lu_env *env,
			       struct lovsub_lock *lsl,
			       struct cl_lock_closure *closure,
			       struct lov_sublock_env *subenv)
{
	lov_sublock_env_put(subenv);
	lsl->lss_active = NULL;
	cl_lock_disclosure(env, closure);
}

static int lov_sublock_lock(const struct lu_env *env,
			    struct lov_lock *lck,
			    struct lov_lock_sub *lls,
			    struct cl_lock_closure *closure,
			    struct lov_sublock_env **lsep)
{
	struct lovsub_lock *sublock;
	struct cl_lock     *child;
	int		 result = 0;

	LASSERT(list_empty(&closure->clc_list));

	sublock = lls->sub_lock;
	child = sublock->lss_cl.cls_lock;
	result = cl_lock_closure_build(env, child, closure);
	if (result == 0) {
		struct cl_lock *parent = closure->clc_origin;

		LASSERT(cl_lock_is_mutexed(child));
		sublock->lss_active = parent;

		if (unlikely((child->cll_state == CLS_FREEING) ||
			     (child->cll_flags & CLF_CANCELLED))) {
			struct lov_lock_link *link;
			/*
			 * we could race with lock deletion which temporarily
			 * put the lock in freeing state, bug 19080.
			 */
			LASSERT(!(lls->sub_flags & LSF_HELD));

			link = lov_lock_link_find(env, lck, sublock);
			LASSERT(link != NULL);
			lov_lock_unlink(env, link, sublock);
			lov_sublock_unlock(env, sublock, closure, NULL);
			lck->lls_cancel_race = 1;
			result = CLO_REPEAT;
		} else if (lsep) {
			struct lov_sublock_env *subenv;
			subenv = lov_sublock_env_get(env, parent, lls);
			if (IS_ERR(subenv)) {
				lov_sublock_unlock(env, sublock,
						   closure, NULL);
				result = PTR_ERR(subenv);
			} else {
				*lsep = subenv;
			}
		}
	}
	return result;
}

/**
 * Updates the result of a top-lock operation from a result of sub-lock
 * sub-operations. Top-operations like lov_lock_{enqueue,use,unuse}() iterate
 * over sub-locks and lov_subresult() is used to calculate return value of a
 * top-operation. To this end, possible return values of sub-operations are
 * ordered as
 *
 *     - 0		  success
 *     - CLO_WAIT	   wait for event
 *     - CLO_REPEAT	 repeat top-operation
 *     - -ne		fundamental error
 *
 * Top-level return code can only go down through this list. CLO_REPEAT
 * overwrites CLO_WAIT, because lock mutex was released and sleeping condition
 * has to be rechecked by the upper layer.
 */
static int lov_subresult(int result, int rc)
{
	int result_rank;
	int rc_rank;

	LASSERTF(result <= 0 || result == CLO_REPEAT || result == CLO_WAIT,
		 "result = %d", result);
	LASSERTF(rc <= 0 || rc == CLO_REPEAT || rc == CLO_WAIT,
		 "rc = %d\n", rc);
	CLASSERT(CLO_WAIT < CLO_REPEAT);

	/* calculate ranks in the ordering above */
	result_rank = result < 0 ? 1 + CLO_REPEAT : result;
	rc_rank = rc < 0 ? 1 + CLO_REPEAT : rc;

	if (result_rank < rc_rank)
		result = rc;
	return result;
}

/**
 * Creates sub-locks for a given lov_lock for the first time.
 *
 * Goes through all sub-objects of top-object, and creates sub-locks on every
 * sub-object intersecting with top-lock extent. This is complicated by the
 * fact that top-lock (that is being created) can be accessed concurrently
 * through already created sub-locks (possibly shared with other top-locks).
 */
static int lov_lock_sub_init(const struct lu_env *env,
			     struct lov_lock *lck, const struct cl_io *io)
{
	int result = 0;
	int i;
	int nr;
	u64 start;
	u64 end;
	u64 file_start;
	u64 file_end;

	struct lov_object       *loo    = cl2lov(lck->lls_cl.cls_obj);
	struct lov_layout_raid0 *r0     = lov_r0(loo);
	struct cl_lock	  *parent = lck->lls_cl.cls_lock;

	lck->lls_orig = parent->cll_descr;
	file_start = cl_offset(lov2cl(loo), parent->cll_descr.cld_start);
	file_end   = cl_offset(lov2cl(loo), parent->cll_descr.cld_end + 1) - 1;

	for (i = 0, nr = 0; i < r0->lo_nr; i++) {
		/*
		 * XXX for wide striping smarter algorithm is desirable,
		 * breaking out of the loop, early.
		 */
		if (likely(r0->lo_sub[i] != NULL) &&
		    lov_stripe_intersects(loo->lo_lsm, i,
					  file_start, file_end, &start, &end))
			nr++;
	}
	LASSERT(nr > 0);
	lck->lls_sub = libcfs_kvzalloc(nr * sizeof(lck->lls_sub[0]), GFP_NOFS);
	if (lck->lls_sub == NULL)
		return -ENOMEM;

	lck->lls_nr = nr;
	/*
	 * First, fill in sub-lock descriptions in
	 * lck->lls_sub[].sub_descr. They are used by lov_sublock_alloc()
	 * (called below in this function, and by lov_lock_enqueue()) to
	 * create sub-locks. At this moment, no other thread can access
	 * top-lock.
	 */
	for (i = 0, nr = 0; i < r0->lo_nr; ++i) {
		if (likely(r0->lo_sub[i] != NULL) &&
		    lov_stripe_intersects(loo->lo_lsm, i,
					  file_start, file_end, &start, &end)) {
			struct cl_lock_descr *descr;

			descr = &lck->lls_sub[nr].sub_descr;

			LASSERT(descr->cld_obj == NULL);
			descr->cld_obj   = lovsub2cl(r0->lo_sub[i]);
			descr->cld_start = cl_index(descr->cld_obj, start);
			descr->cld_end   = cl_index(descr->cld_obj, end);
			descr->cld_mode  = parent->cll_descr.cld_mode;
			descr->cld_gid   = parent->cll_descr.cld_gid;
			descr->cld_enq_flags   = parent->cll_descr.cld_enq_flags;
			/* XXX has no effect */
			lck->lls_sub[nr].sub_got = *descr;
			lck->lls_sub[nr].sub_stripe = i;
			nr++;
		}
	}
	LASSERT(nr == lck->lls_nr);

	/*
	 * Some sub-locks can be missing at this point. This is not a problem,
	 * because enqueue will create them anyway. Main duty of this function
	 * is to fill in sub-lock descriptions in a race free manner.
	 */
	return result;
}

static int lov_sublock_release(const struct lu_env *env, struct lov_lock *lck,
			       int i, int deluser, int rc)
{
	struct cl_lock *parent = lck->lls_cl.cls_lock;

	LASSERT(cl_lock_is_mutexed(parent));

	if (lck->lls_sub[i].sub_flags & LSF_HELD) {
		struct cl_lock    *sublock;
		int dying;

		LASSERT(lck->lls_sub[i].sub_lock != NULL);
		sublock = lck->lls_sub[i].sub_lock->lss_cl.cls_lock;
		LASSERT(cl_lock_is_mutexed(sublock));

		lck->lls_sub[i].sub_flags &= ~LSF_HELD;
		if (deluser)
			cl_lock_user_del(env, sublock);
		/*
		 * If the last hold is released, and cancellation is pending
		 * for a sub-lock, release parent mutex, to avoid keeping it
		 * while sub-lock is being paged out.
		 */
		dying = (sublock->cll_descr.cld_mode == CLM_PHANTOM ||
			 sublock->cll_descr.cld_mode == CLM_GROUP ||
			 (sublock->cll_flags & (CLF_CANCELPEND|CLF_DOOMED))) &&
			sublock->cll_holds == 1;
		if (dying)
			cl_lock_mutex_put(env, parent);
		cl_lock_unhold(env, sublock, "lov-parent", parent);
		if (dying) {
			cl_lock_mutex_get(env, parent);
			rc = lov_subresult(rc, CLO_REPEAT);
		}
		/*
		 * From now on lck->lls_sub[i].sub_lock is a "weak" pointer,
		 * not backed by a reference on a
		 * sub-lock. lovsub_lock_delete() will clear
		 * lck->lls_sub[i].sub_lock under semaphores, just before
		 * sub-lock is destroyed.
		 */
	}
	return rc;
}

static void lov_sublock_hold(const struct lu_env *env, struct lov_lock *lck,
			     int i)
{
	struct cl_lock *parent = lck->lls_cl.cls_lock;

	LASSERT(cl_lock_is_mutexed(parent));

	if (!(lck->lls_sub[i].sub_flags & LSF_HELD)) {
		struct cl_lock *sublock;

		LASSERT(lck->lls_sub[i].sub_lock != NULL);
		sublock = lck->lls_sub[i].sub_lock->lss_cl.cls_lock;
		LASSERT(cl_lock_is_mutexed(sublock));
		LASSERT(sublock->cll_state != CLS_FREEING);

		lck->lls_sub[i].sub_flags |= LSF_HELD;

		cl_lock_get_trust(sublock);
		cl_lock_hold_add(env, sublock, "lov-parent", parent);
		cl_lock_user_add(env, sublock);
		cl_lock_put(env, sublock);
	}
}

static void lov_lock_fini(const struct lu_env *env,
			  struct cl_lock_slice *slice)
{
	struct lov_lock *lck;
	int i;

	lck = cl2lov_lock(slice);
	LASSERT(lck->lls_nr_filled == 0);
	if (lck->lls_sub != NULL) {
		for (i = 0; i < lck->lls_nr; ++i)
			/*
			 * No sub-locks exists at this point, as sub-lock has
			 * a reference on its parent.
			 */
			LASSERT(lck->lls_sub[i].sub_lock == NULL);
		kvfree(lck->lls_sub);
	}
	OBD_SLAB_FREE_PTR(lck, lov_lock_kmem);
}

static int lov_lock_enqueue_wait(const struct lu_env *env,
				 struct lov_lock *lck,
				 struct cl_lock *sublock)
{
	struct cl_lock *lock = lck->lls_cl.cls_lock;
	int	     result;

	LASSERT(cl_lock_is_mutexed(lock));

	cl_lock_mutex_put(env, lock);
	result = cl_lock_enqueue_wait(env, sublock, 0);
	cl_lock_mutex_get(env, lock);
	return result ?: CLO_REPEAT;
}

/**
 * Tries to advance a state machine of a given sub-lock toward enqueuing of
 * the top-lock.
 *
 * \retval 0 if state-transition can proceed
 * \retval -ve otherwise.
 */
static int lov_lock_enqueue_one(const struct lu_env *env, struct lov_lock *lck,
				struct cl_lock *sublock,
				struct cl_io *io, __u32 enqflags, int last)
{
	int result;

	/* first, try to enqueue a sub-lock ... */
	result = cl_enqueue_try(env, sublock, io, enqflags);
	if ((sublock->cll_state == CLS_ENQUEUED) && !(enqflags & CEF_AGL)) {
		/* if it is enqueued, try to `wait' on it---maybe it's already
		 * granted */
		result = cl_wait_try(env, sublock);
		if (result == CLO_REENQUEUED)
			result = CLO_WAIT;
	}
	/*
	 * If CEF_ASYNC flag is set, then all sub-locks can be enqueued in
	 * parallel, otherwise---enqueue has to wait until sub-lock is granted
	 * before proceeding to the next one.
	 */
	if ((result == CLO_WAIT) && (sublock->cll_state <= CLS_HELD) &&
	    (enqflags & CEF_ASYNC) && (!last || (enqflags & CEF_AGL)))
		result = 0;
	return result;
}

/**
 * Helper function for lov_lock_enqueue() that creates missing sub-lock.
 */
static int lov_sublock_fill(const struct lu_env *env, struct cl_lock *parent,
			    struct cl_io *io, struct lov_lock *lck, int idx)
{
	struct lov_lock_link *link = NULL;
	struct cl_lock       *sublock;
	int		   result;

	LASSERT(parent->cll_depth == 1);
	cl_lock_mutex_put(env, parent);
	sublock = lov_sublock_alloc(env, io, lck, idx, &link);
	if (!IS_ERR(sublock))
		cl_lock_mutex_get(env, sublock);
	cl_lock_mutex_get(env, parent);

	if (!IS_ERR(sublock)) {
		cl_lock_get_trust(sublock);
		if (parent->cll_state == CLS_QUEUING &&
		    lck->lls_sub[idx].sub_lock == NULL) {
			lov_sublock_adopt(env, lck, sublock, idx, link);
		} else {
			OBD_SLAB_FREE_PTR(link, lov_lock_link_kmem);
			/* other thread allocated sub-lock, or enqueue is no
			 * longer going on */
			cl_lock_mutex_put(env, parent);
			cl_lock_unhold(env, sublock, "lov-parent", parent);
			cl_lock_mutex_get(env, parent);
		}
		cl_lock_mutex_put(env, sublock);
		cl_lock_put(env, sublock);
		result = CLO_REPEAT;
	} else
		result = PTR_ERR(sublock);
	return result;
}

/**
 * Implementation of cl_lock_operations::clo_enqueue() for lov layer. This
 * function is rather subtle, as it enqueues top-lock (i.e., advances top-lock
 * state machine from CLS_QUEUING to CLS_ENQUEUED states) by juggling sub-lock
 * state machines in the face of sub-locks sharing (by multiple top-locks),
 * and concurrent sub-lock cancellations.
 */
static int lov_lock_enqueue(const struct lu_env *env,
			    const struct cl_lock_slice *slice,
			    struct cl_io *io, __u32 enqflags)
{
	struct cl_lock	 *lock    = slice->cls_lock;
	struct lov_lock	*lck     = cl2lov_lock(slice);
	struct cl_lock_closure *closure = lov_closure_get(env, lock);
	int i;
	int result;
	enum cl_lock_state minstate;

	for (result = 0, minstate = CLS_FREEING, i = 0; i < lck->lls_nr; ++i) {
		int rc;
		struct lovsub_lock     *sub;
		struct lov_lock_sub    *lls;
		struct cl_lock	 *sublock;
		struct lov_sublock_env *subenv;

		if (lock->cll_state != CLS_QUEUING) {
			/*
			 * Lock might have left QUEUING state if previous
			 * iteration released its mutex. Stop enqueing in this
			 * case and let the upper layer to decide what to do.
			 */
			LASSERT(i > 0 && result != 0);
			break;
		}

		lls = &lck->lls_sub[i];
		sub = lls->sub_lock;
		/*
		 * Sub-lock might have been canceled, while top-lock was
		 * cached.
		 */
		if (sub == NULL) {
			result = lov_sublock_fill(env, lock, io, lck, i);
			/* lov_sublock_fill() released @lock mutex,
			 * restart. */
			break;
		}
		sublock = sub->lss_cl.cls_lock;
		rc = lov_sublock_lock(env, lck, lls, closure, &subenv);
		if (rc == 0) {
			lov_sublock_hold(env, lck, i);
			rc = lov_lock_enqueue_one(subenv->lse_env, lck, sublock,
						  subenv->lse_io, enqflags,
						  i == lck->lls_nr - 1);
			minstate = min(minstate, sublock->cll_state);
			if (rc == CLO_WAIT) {
				switch (sublock->cll_state) {
				case CLS_QUEUING:
					/* take recursive mutex, the lock is
					 * released in lov_lock_enqueue_wait.
					 */
					cl_lock_mutex_get(env, sublock);
					lov_sublock_unlock(env, sub, closure,
							   subenv);
					rc = lov_lock_enqueue_wait(env, lck,
								   sublock);
					break;
				case CLS_CACHED:
					cl_lock_get(sublock);
					/* take recursive mutex of sublock */
					cl_lock_mutex_get(env, sublock);
					/* need to release all locks in closure
					 * otherwise it may deadlock. LU-2683.*/
					lov_sublock_unlock(env, sub, closure,
							   subenv);
					/* sublock and parent are held. */
					rc = lov_sublock_release(env, lck, i,
								 1, rc);
					cl_lock_mutex_put(env, sublock);
					cl_lock_put(env, sublock);
					break;
				default:
					lov_sublock_unlock(env, sub, closure,
							   subenv);
					break;
				}
			} else {
				LASSERT(sublock->cll_conflict == NULL);
				lov_sublock_unlock(env, sub, closure, subenv);
			}
		}
		result = lov_subresult(result, rc);
		if (result != 0)
			break;
	}
	cl_lock_closure_fini(closure);
	return result ?: minstate >= CLS_ENQUEUED ? 0 : CLO_WAIT;
}

static int lov_lock_unuse(const struct lu_env *env,
			  const struct cl_lock_slice *slice)
{
	struct lov_lock	*lck     = cl2lov_lock(slice);
	struct cl_lock_closure *closure = lov_closure_get(env, slice->cls_lock);
	int i;
	int result;

	for (result = 0, i = 0; i < lck->lls_nr; ++i) {
		int rc;
		struct lovsub_lock     *sub;
		struct cl_lock	 *sublock;
		struct lov_lock_sub    *lls;
		struct lov_sublock_env *subenv;

		/* top-lock state cannot change concurrently, because single
		 * thread (one that released the last hold) carries unlocking
		 * to the completion. */
		LASSERT(slice->cls_lock->cll_state == CLS_INTRANSIT);
		lls = &lck->lls_sub[i];
		sub = lls->sub_lock;
		if (sub == NULL)
			continue;

		sublock = sub->lss_cl.cls_lock;
		rc = lov_sublock_lock(env, lck, lls, closure, &subenv);
		if (rc == 0) {
			if (lls->sub_flags & LSF_HELD) {
				LASSERT(sublock->cll_state == CLS_HELD ||
					sublock->cll_state == CLS_ENQUEUED);
				rc = cl_unuse_try(subenv->lse_env, sublock);
				rc = lov_sublock_release(env, lck, i, 0, rc);
			}
			lov_sublock_unlock(env, sub, closure, subenv);
		}
		result = lov_subresult(result, rc);
	}

	if (result == 0 && lck->lls_cancel_race) {
		lck->lls_cancel_race = 0;
		result = -ESTALE;
	}
	cl_lock_closure_fini(closure);
	return result;
}


static void lov_lock_cancel(const struct lu_env *env,
			   const struct cl_lock_slice *slice)
{
	struct lov_lock	*lck     = cl2lov_lock(slice);
	struct cl_lock_closure *closure = lov_closure_get(env, slice->cls_lock);
	int i;
	int result;

	for (result = 0, i = 0; i < lck->lls_nr; ++i) {
		int rc;
		struct lovsub_lock     *sub;
		struct cl_lock	 *sublock;
		struct lov_lock_sub    *lls;
		struct lov_sublock_env *subenv;

		/* top-lock state cannot change concurrently, because single
		 * thread (one that released the last hold) carries unlocking
		 * to the completion. */
		lls = &lck->lls_sub[i];
		sub = lls->sub_lock;
		if (sub == NULL)
			continue;

		sublock = sub->lss_cl.cls_lock;
		rc = lov_sublock_lock(env, lck, lls, closure, &subenv);
		if (rc == 0) {
			if (!(lls->sub_flags & LSF_HELD)) {
				lov_sublock_unlock(env, sub, closure, subenv);
				continue;
			}

			switch (sublock->cll_state) {
			case CLS_HELD:
				rc = cl_unuse_try(subenv->lse_env, sublock);
				lov_sublock_release(env, lck, i, 0, 0);
				break;
			default:
				lov_sublock_release(env, lck, i, 1, 0);
				break;
			}
			lov_sublock_unlock(env, sub, closure, subenv);
		}

		if (rc == CLO_REPEAT) {
			--i;
			continue;
		}

		result = lov_subresult(result, rc);
	}

	if (result)
		CL_LOCK_DEBUG(D_ERROR, env, slice->cls_lock,
			      "lov_lock_cancel fails with %d.\n", result);

	cl_lock_closure_fini(closure);
}

static int lov_lock_wait(const struct lu_env *env,
			 const struct cl_lock_slice *slice)
{
	struct lov_lock	*lck     = cl2lov_lock(slice);
	struct cl_lock_closure *closure = lov_closure_get(env, slice->cls_lock);
	enum cl_lock_state      minstate;
	int		     reenqueued;
	int		     result;
	int		     i;

again:
	for (result = 0, minstate = CLS_FREEING, i = 0, reenqueued = 0;
	     i < lck->lls_nr; ++i) {
		int rc;
		struct lovsub_lock     *sub;
		struct cl_lock	 *sublock;
		struct lov_lock_sub    *lls;
		struct lov_sublock_env *subenv;

		lls = &lck->lls_sub[i];
		sub = lls->sub_lock;
		LASSERT(sub != NULL);
		sublock = sub->lss_cl.cls_lock;
		rc = lov_sublock_lock(env, lck, lls, closure, &subenv);
		if (rc == 0) {
			LASSERT(sublock->cll_state >= CLS_ENQUEUED);
			if (sublock->cll_state < CLS_HELD)
				rc = cl_wait_try(env, sublock);

			minstate = min(minstate, sublock->cll_state);
			lov_sublock_unlock(env, sub, closure, subenv);
		}
		if (rc == CLO_REENQUEUED) {
			reenqueued++;
			rc = 0;
		}
		result = lov_subresult(result, rc);
		if (result != 0)
			break;
	}
	/* Each sublock only can be reenqueued once, so will not loop for
	 * ever. */
	if (result == 0 && reenqueued != 0)
		goto again;
	cl_lock_closure_fini(closure);
	return result ?: minstate >= CLS_HELD ? 0 : CLO_WAIT;
}

static int lov_lock_use(const struct lu_env *env,
			const struct cl_lock_slice *slice)
{
	struct lov_lock	*lck     = cl2lov_lock(slice);
	struct cl_lock_closure *closure = lov_closure_get(env, slice->cls_lock);
	int		     result;
	int		     i;

	LASSERT(slice->cls_lock->cll_state == CLS_INTRANSIT);

	for (result = 0, i = 0; i < lck->lls_nr; ++i) {
		int rc;
		struct lovsub_lock     *sub;
		struct cl_lock	 *sublock;
		struct lov_lock_sub    *lls;
		struct lov_sublock_env *subenv;

		LASSERT(slice->cls_lock->cll_state == CLS_INTRANSIT);

		lls = &lck->lls_sub[i];
		sub = lls->sub_lock;
		if (sub == NULL) {
			/*
			 * Sub-lock might have been canceled, while top-lock was
			 * cached.
			 */
			result = -ESTALE;
			break;
		}

		sublock = sub->lss_cl.cls_lock;
		rc = lov_sublock_lock(env, lck, lls, closure, &subenv);
		if (rc == 0) {
			LASSERT(sublock->cll_state != CLS_FREEING);
			lov_sublock_hold(env, lck, i);
			if (sublock->cll_state == CLS_CACHED) {
				rc = cl_use_try(subenv->lse_env, sublock, 0);
				if (rc != 0)
					rc = lov_sublock_release(env, lck,
								 i, 1, rc);
			} else if (sublock->cll_state == CLS_NEW) {
				/* Sub-lock might have been canceled, while
				 * top-lock was cached. */
				result = -ESTALE;
				lov_sublock_release(env, lck, i, 1, result);
			}
			lov_sublock_unlock(env, sub, closure, subenv);
		}
		result = lov_subresult(result, rc);
		if (result != 0)
			break;
	}

	if (lck->lls_cancel_race) {
		/*
		 * If there is unlocking happened at the same time, then
		 * sublock_lock state should be FREEING, and lov_sublock_lock
		 * should return CLO_REPEAT. In this case, it should return
		 * ESTALE, and up layer should reset the lock state to be NEW.
		 */
		lck->lls_cancel_race = 0;
		LASSERT(result != 0);
		result = -ESTALE;
	}
	cl_lock_closure_fini(closure);
	return result;
}

#if 0
static int lock_lock_multi_match()
{
	struct cl_lock	  *lock    = slice->cls_lock;
	struct cl_lock_descr    *subneed = &lov_env_info(env)->lti_ldescr;
	struct lov_object       *loo     = cl2lov(lov->lls_cl.cls_obj);
	struct lov_layout_raid0 *r0      = lov_r0(loo);
	struct lov_lock_sub     *sub;
	struct cl_object	*subobj;
	u64  fstart;
	u64  fend;
	u64  start;
	u64  end;
	int i;

	fstart = cl_offset(need->cld_obj, need->cld_start);
	fend   = cl_offset(need->cld_obj, need->cld_end + 1) - 1;
	subneed->cld_mode = need->cld_mode;
	cl_lock_mutex_get(env, lock);
	for (i = 0; i < lov->lls_nr; ++i) {
		sub = &lov->lls_sub[i];
		if (sub->sub_lock == NULL)
			continue;
		subobj = sub->sub_descr.cld_obj;
		if (!lov_stripe_intersects(loo->lo_lsm, sub->sub_stripe,
					   fstart, fend, &start, &end))
			continue;
		subneed->cld_start = cl_index(subobj, start);
		subneed->cld_end   = cl_index(subobj, end);
		subneed->cld_obj   = subobj;
		if (!cl_lock_ext_match(&sub->sub_got, subneed)) {
			result = 0;
			break;
		}
	}
	cl_lock_mutex_put(env, lock);
}
#endif

/**
 * Check if the extent region \a descr is covered by \a child against the
 * specific \a stripe.
 */
static int lov_lock_stripe_is_matching(const struct lu_env *env,
				       struct lov_object *lov, int stripe,
				       const struct cl_lock_descr *child,
				       const struct cl_lock_descr *descr)
{
	struct lov_stripe_md *lsm = lov->lo_lsm;
	u64 start;
	u64 end;
	int result;

	if (lov_r0(lov)->lo_nr == 1)
		return cl_lock_ext_match(child, descr);

	/*
	 * For a multi-stripes object:
	 * - make sure the descr only covers child's stripe, and
	 * - check if extent is matching.
	 */
	start = cl_offset(&lov->lo_cl, descr->cld_start);
	end   = cl_offset(&lov->lo_cl, descr->cld_end + 1) - 1;
	result = 0;
	/* glimpse should work on the object with LOV EA hole. */
	if (end - start <= lsm->lsm_stripe_size) {
		int idx;

		idx = lov_stripe_number(lsm, start);
		if (idx == stripe ||
		    unlikely(lov_r0(lov)->lo_sub[idx] == NULL)) {
			idx = lov_stripe_number(lsm, end);
			if (idx == stripe ||
			    unlikely(lov_r0(lov)->lo_sub[idx] == NULL))
				result = 1;
		}
	}

	if (result != 0) {
		struct cl_lock_descr *subd = &lov_env_info(env)->lti_ldescr;
		u64 sub_start;
		u64 sub_end;

		subd->cld_obj  = NULL;   /* don't need sub object at all */
		subd->cld_mode = descr->cld_mode;
		subd->cld_gid  = descr->cld_gid;
		result = lov_stripe_intersects(lsm, stripe, start, end,
					       &sub_start, &sub_end);
		LASSERT(result);
		subd->cld_start = cl_index(child->cld_obj, sub_start);
		subd->cld_end   = cl_index(child->cld_obj, sub_end);
		result = cl_lock_ext_match(child, subd);
	}
	return result;
}

/**
 * An implementation of cl_lock_operations::clo_fits_into() method.
 *
 * Checks whether a lock (given by \a slice) is suitable for \a
 * io. Multi-stripe locks can be used only for "quick" io, like truncate, or
 * O_APPEND write.
 *
 * \see ccc_lock_fits_into().
 */
static int lov_lock_fits_into(const struct lu_env *env,
			      const struct cl_lock_slice *slice,
			      const struct cl_lock_descr *need,
			      const struct cl_io *io)
{
	struct lov_lock   *lov = cl2lov_lock(slice);
	struct lov_object *obj = cl2lov(slice->cls_obj);
	int result;

	LASSERT(cl_object_same(need->cld_obj, slice->cls_obj));
	LASSERT(lov->lls_nr > 0);

	/* for top lock, it's necessary to match enq flags otherwise it will
	 * run into problem if a sublock is missing and reenqueue. */
	if (need->cld_enq_flags != lov->lls_orig.cld_enq_flags)
		return 0;

	if (need->cld_mode == CLM_GROUP)
		/*
		 * always allow to match group lock.
		 */
		result = cl_lock_ext_match(&lov->lls_orig, need);
	else if (lov->lls_nr == 1) {
		struct cl_lock_descr *got = &lov->lls_sub[0].sub_got;
		result = lov_lock_stripe_is_matching(env,
						     cl2lov(slice->cls_obj),
						     lov->lls_sub[0].sub_stripe,
						     got, need);
	} else if (io->ci_type != CIT_SETATTR && io->ci_type != CIT_MISC &&
		   !cl_io_is_append(io) && need->cld_mode != CLM_PHANTOM)
		/*
		 * Multi-stripe locks are only suitable for `quick' IO and for
		 * glimpse.
		 */
		result = 0;
	else
		/*
		 * Most general case: multi-stripe existing lock, and
		 * (potentially) multi-stripe @need lock. Check that @need is
		 * covered by @lov's sub-locks.
		 *
		 * For now, ignore lock expansions made by the server, and
		 * match against original lock extent.
		 */
		result = cl_lock_ext_match(&lov->lls_orig, need);
	CDEBUG(D_DLMTRACE, DDESCR"/"DDESCR" %d %d/%d: %d\n",
	       PDESCR(&lov->lls_orig), PDESCR(&lov->lls_sub[0].sub_got),
	       lov->lls_sub[0].sub_stripe, lov->lls_nr, lov_r0(obj)->lo_nr,
	       result);
	return result;
}

void lov_lock_unlink(const struct lu_env *env,
		     struct lov_lock_link *link, struct lovsub_lock *sub)
{
	struct lov_lock *lck    = link->lll_super;
	struct cl_lock  *parent = lck->lls_cl.cls_lock;

	LASSERT(cl_lock_is_mutexed(parent));
	LASSERT(cl_lock_is_mutexed(sub->lss_cl.cls_lock));

	list_del_init(&link->lll_list);
	LASSERT(lck->lls_sub[link->lll_idx].sub_lock == sub);
	/* yank this sub-lock from parent's array */
	lck->lls_sub[link->lll_idx].sub_lock = NULL;
	LASSERT(lck->lls_nr_filled > 0);
	lck->lls_nr_filled--;
	lu_ref_del(&parent->cll_reference, "lov-child", sub->lss_cl.cls_lock);
	cl_lock_put(env, parent);
	OBD_SLAB_FREE_PTR(link, lov_lock_link_kmem);
}

struct lov_lock_link *lov_lock_link_find(const struct lu_env *env,
					 struct lov_lock *lck,
					 struct lovsub_lock *sub)
{
	struct lov_lock_link *scan;

	LASSERT(cl_lock_is_mutexed(sub->lss_cl.cls_lock));

	list_for_each_entry(scan, &sub->lss_parents, lll_list) {
		if (scan->lll_super == lck)
			return scan;
	}
	return NULL;
}

/**
 * An implementation of cl_lock_operations::clo_delete() method. This is
 * invoked for "top-to-bottom" delete, when lock destruction starts from the
 * top-lock, e.g., as a result of inode destruction.
 *
 * Unlinks top-lock from all its sub-locks. Sub-locks are not deleted there:
 * this is done separately elsewhere:
 *
 *     - for inode destruction, lov_object_delete() calls cl_object_kill() for
 *       each sub-object, purging its locks;
 *
 *     - in other cases (e.g., a fatal error with a top-lock) sub-locks are
 *       left in the cache.
 */
static void lov_lock_delete(const struct lu_env *env,
			    const struct cl_lock_slice *slice)
{
	struct lov_lock	*lck     = cl2lov_lock(slice);
	struct cl_lock_closure *closure = lov_closure_get(env, slice->cls_lock);
	struct lov_lock_link   *link;
	int		     rc;
	int		     i;

	LASSERT(slice->cls_lock->cll_state == CLS_FREEING);

	for (i = 0; i < lck->lls_nr; ++i) {
		struct lov_lock_sub *lls = &lck->lls_sub[i];
		struct lovsub_lock  *lsl = lls->sub_lock;

		if (lsl == NULL) /* already removed */
			continue;

		rc = lov_sublock_lock(env, lck, lls, closure, NULL);
		if (rc == CLO_REPEAT) {
			--i;
			continue;
		}

		LASSERT(rc == 0);
		LASSERT(lsl->lss_cl.cls_lock->cll_state < CLS_FREEING);

		if (lls->sub_flags & LSF_HELD)
			lov_sublock_release(env, lck, i, 1, 0);

		link = lov_lock_link_find(env, lck, lsl);
		LASSERT(link != NULL);
		lov_lock_unlink(env, link, lsl);
		LASSERT(lck->lls_sub[i].sub_lock == NULL);

		lov_sublock_unlock(env, lsl, closure, NULL);
	}

	cl_lock_closure_fini(closure);
}

static int lov_lock_print(const struct lu_env *env, void *cookie,
			  lu_printer_t p, const struct cl_lock_slice *slice)
{
	struct lov_lock *lck = cl2lov_lock(slice);
	int	      i;

	(*p)(env, cookie, "%d\n", lck->lls_nr);
	for (i = 0; i < lck->lls_nr; ++i) {
		struct lov_lock_sub *sub;

		sub = &lck->lls_sub[i];
		(*p)(env, cookie, "    %d %x: ", i, sub->sub_flags);
		if (sub->sub_lock != NULL)
			cl_lock_print(env, cookie, p,
				      sub->sub_lock->lss_cl.cls_lock);
		else
			(*p)(env, cookie, "---\n");
	}
	return 0;
}

static const struct cl_lock_operations lov_lock_ops = {
	.clo_fini      = lov_lock_fini,
	.clo_enqueue   = lov_lock_enqueue,
	.clo_wait      = lov_lock_wait,
	.clo_use       = lov_lock_use,
	.clo_unuse     = lov_lock_unuse,
	.clo_cancel    = lov_lock_cancel,
	.clo_fits_into = lov_lock_fits_into,
	.clo_delete    = lov_lock_delete,
	.clo_print     = lov_lock_print
};

int lov_lock_init_raid0(const struct lu_env *env, struct cl_object *obj,
			struct cl_lock *lock, const struct cl_io *io)
{
	struct lov_lock *lck;
	int result;

	OBD_SLAB_ALLOC_PTR_GFP(lck, lov_lock_kmem, GFP_NOFS);
	if (lck != NULL) {
		cl_lock_slice_add(lock, &lck->lls_cl, obj, &lov_lock_ops);
		result = lov_lock_sub_init(env, lck, io);
	} else
		result = -ENOMEM;
	return result;
}

static void lov_empty_lock_fini(const struct lu_env *env,
				struct cl_lock_slice *slice)
{
	struct lov_lock *lck = cl2lov_lock(slice);
	OBD_SLAB_FREE_PTR(lck, lov_lock_kmem);
}

static int lov_empty_lock_print(const struct lu_env *env, void *cookie,
			lu_printer_t p, const struct cl_lock_slice *slice)
{
	(*p)(env, cookie, "empty\n");
	return 0;
}

/* XXX: more methods will be added later. */
static const struct cl_lock_operations lov_empty_lock_ops = {
	.clo_fini  = lov_empty_lock_fini,
	.clo_print = lov_empty_lock_print
};

int lov_lock_init_empty(const struct lu_env *env, struct cl_object *obj,
		struct cl_lock *lock, const struct cl_io *io)
{
	struct lov_lock *lck;
	int result = -ENOMEM;

	OBD_SLAB_ALLOC_PTR_GFP(lck, lov_lock_kmem, GFP_NOFS);
	if (lck != NULL) {
		cl_lock_slice_add(lock, &lck->lls_cl, obj, &lov_empty_lock_ops);
		lck->lls_orig = lock->cll_descr;
		result = 0;
	}
	return result;
}

static struct cl_lock_closure *lov_closure_get(const struct lu_env *env,
					       struct cl_lock *parent)
{
	struct cl_lock_closure *closure;

	closure = &lov_env_info(env)->lti_closure;
	LASSERT(list_empty(&closure->clc_list));
	cl_lock_closure_init(env, closure, parent, 1);
	return closure;
}


/** @} lov */
