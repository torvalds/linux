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
 * Implementation of cl_lock for OSC layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_OSC

#include "../../include/linux/libcfs/libcfs.h"
/* fid_build_reg_res_name() */
#include "../include/lustre_fid.h"

#include "osc_cl_internal.h"

/** \addtogroup osc
 *  @{
 */

#define _PAGEREF_MAGIC  (-10000000)

/*****************************************************************************
 *
 * Type conversions.
 *
 */

static const struct cl_lock_operations osc_lock_ops;
static const struct cl_lock_operations osc_lock_lockless_ops;
static void osc_lock_to_lockless(const struct lu_env *env,
				 struct osc_lock *ols, int force);
static int osc_lock_has_pages(struct osc_lock *olck);

int osc_lock_is_lockless(const struct osc_lock *olck)
{
	return (olck->ols_cl.cls_ops == &osc_lock_lockless_ops);
}

/**
 * Returns a weak pointer to the ldlm lock identified by a handle. Returned
 * pointer cannot be dereferenced, as lock is not protected from concurrent
 * reclaim. This function is a helper for osc_lock_invariant().
 */
static struct ldlm_lock *osc_handle_ptr(struct lustre_handle *handle)
{
	struct ldlm_lock *lock;

	lock = ldlm_handle2lock(handle);
	if (lock != NULL)
		LDLM_LOCK_PUT(lock);
	return lock;
}

/**
 * Invariant that has to be true all of the time.
 */
static int osc_lock_invariant(struct osc_lock *ols)
{
	struct ldlm_lock *lock = osc_handle_ptr(&ols->ols_handle);
	struct ldlm_lock *olock = ols->ols_lock;
	int handle_used = lustre_handle_is_used(&ols->ols_handle);

	if (ergo(osc_lock_is_lockless(ols),
		 ols->ols_locklessable && ols->ols_lock == NULL))
		return 1;

	/*
	 * If all the following "ergo"s are true, return 1, otherwise 0
	 */
	if (!ergo(olock != NULL, handle_used))
		return 0;

	if (!ergo(olock != NULL,
		   olock->l_handle.h_cookie == ols->ols_handle.cookie))
		return 0;

	if (!ergo(handle_used,
		   ergo(lock != NULL && olock != NULL, lock == olock) &&
		   ergo(lock == NULL, olock == NULL)))
		return 0;
	/*
	 * Check that ->ols_handle and ->ols_lock are consistent, but
	 * take into account that they are set at the different time.
	 */
	if (!ergo(ols->ols_state == OLS_CANCELLED,
		   olock == NULL && !handle_used))
		return 0;
	/*
	 * DLM lock is destroyed only after we have seen cancellation
	 * ast.
	 */
	if (!ergo(olock != NULL && ols->ols_state < OLS_CANCELLED,
		   ((olock->l_flags & LDLM_FL_DESTROYED) == 0)))
		return 0;

	if (!ergo(ols->ols_state == OLS_GRANTED,
		   olock != NULL &&
		   olock->l_req_mode == olock->l_granted_mode &&
		   ols->ols_hold))
		return 0;
	return 1;
}

/*****************************************************************************
 *
 * Lock operations.
 *
 */

/**
 * Breaks a link between osc_lock and dlm_lock.
 */
static void osc_lock_detach(const struct lu_env *env, struct osc_lock *olck)
{
	struct ldlm_lock *dlmlock;

	spin_lock(&osc_ast_guard);
	dlmlock = olck->ols_lock;
	if (dlmlock == NULL) {
		spin_unlock(&osc_ast_guard);
		return;
	}

	olck->ols_lock = NULL;
	/* wb(); --- for all who checks (ols->ols_lock != NULL) before
	 * call to osc_lock_detach() */
	dlmlock->l_ast_data = NULL;
	olck->ols_handle.cookie = 0ULL;
	spin_unlock(&osc_ast_guard);

	lock_res_and_lock(dlmlock);
	if (dlmlock->l_granted_mode == dlmlock->l_req_mode) {
		struct cl_object *obj = olck->ols_cl.cls_obj;
		struct cl_attr *attr = &osc_env_info(env)->oti_attr;
		__u64 old_kms;

		cl_object_attr_lock(obj);
		/* Must get the value under the lock to avoid possible races. */
		old_kms = cl2osc(obj)->oo_oinfo->loi_kms;
		/* Update the kms. Need to loop all granted locks.
		 * Not a problem for the client */
		attr->cat_kms = ldlm_extent_shift_kms(dlmlock, old_kms);

		cl_object_attr_set(env, obj, attr, CAT_KMS);
		cl_object_attr_unlock(obj);
	}
	unlock_res_and_lock(dlmlock);

	/* release a reference taken in osc_lock_upcall0(). */
	LASSERT(olck->ols_has_ref);
	lu_ref_del(&dlmlock->l_reference, "osc_lock", olck);
	LDLM_LOCK_RELEASE(dlmlock);
	olck->ols_has_ref = 0;
}

static int osc_lock_unhold(struct osc_lock *ols)
{
	int result = 0;

	if (ols->ols_hold) {
		ols->ols_hold = 0;
		result = osc_cancel_base(&ols->ols_handle,
					 ols->ols_einfo.ei_mode);
	}
	return result;
}

static int osc_lock_unuse(const struct lu_env *env,
			  const struct cl_lock_slice *slice)
{
	struct osc_lock *ols = cl2osc_lock(slice);

	LINVRNT(osc_lock_invariant(ols));

	switch (ols->ols_state) {
	case OLS_NEW:
		LASSERT(!ols->ols_hold);
		LASSERT(ols->ols_agl);
		return 0;
	case OLS_UPCALL_RECEIVED:
		osc_lock_unhold(ols);
	case OLS_ENQUEUED:
		LASSERT(!ols->ols_hold);
		osc_lock_detach(env, ols);
		ols->ols_state = OLS_NEW;
		return 0;
	case OLS_GRANTED:
		LASSERT(!ols->ols_glimpse);
		LASSERT(ols->ols_hold);
		/*
		 * Move lock into OLS_RELEASED state before calling
		 * osc_cancel_base() so that possible synchronous cancellation
		 * (that always happens e.g., for liblustre) sees that lock is
		 * released.
		 */
		ols->ols_state = OLS_RELEASED;
		return osc_lock_unhold(ols);
	default:
		CERROR("Impossible state: %d\n", ols->ols_state);
		LBUG();
	}
}

static void osc_lock_fini(const struct lu_env *env,
			  struct cl_lock_slice *slice)
{
	struct osc_lock *ols = cl2osc_lock(slice);

	LINVRNT(osc_lock_invariant(ols));
	/*
	 * ->ols_hold can still be true at this point if, for example, a
	 * thread that requested a lock was killed (and released a reference
	 * to the lock), before reply from a server was received. In this case
	 * lock is destroyed immediately after upcall.
	 */
	osc_lock_unhold(ols);
	LASSERT(ols->ols_lock == NULL);
	LASSERT(atomic_read(&ols->ols_pageref) == 0 ||
		atomic_read(&ols->ols_pageref) == _PAGEREF_MAGIC);

	OBD_SLAB_FREE_PTR(ols, osc_lock_kmem);
}

static void osc_lock_build_policy(const struct lu_env *env,
				  const struct cl_lock *lock,
				  ldlm_policy_data_t *policy)
{
	const struct cl_lock_descr *d = &lock->cll_descr;

	osc_index2policy(policy, d->cld_obj, d->cld_start, d->cld_end);
	policy->l_extent.gid = d->cld_gid;
}

static __u64 osc_enq2ldlm_flags(__u32 enqflags)
{
	__u64 result = 0;

	LASSERT((enqflags & ~CEF_MASK) == 0);

	if (enqflags & CEF_NONBLOCK)
		result |= LDLM_FL_BLOCK_NOWAIT;
	if (enqflags & CEF_ASYNC)
		result |= LDLM_FL_HAS_INTENT;
	if (enqflags & CEF_DISCARD_DATA)
		result |= LDLM_FL_AST_DISCARD_DATA;
	return result;
}

/**
 * Global spin-lock protecting consistency of ldlm_lock::l_ast_data
 * pointers. Initialized in osc_init().
 */
spinlock_t osc_ast_guard;

static struct osc_lock *osc_ast_data_get(struct ldlm_lock *dlm_lock)
{
	struct osc_lock *olck;

	lock_res_and_lock(dlm_lock);
	spin_lock(&osc_ast_guard);
	olck = dlm_lock->l_ast_data;
	if (olck != NULL) {
		struct cl_lock *lock = olck->ols_cl.cls_lock;
		/*
		 * If osc_lock holds a reference on ldlm lock, return it even
		 * when cl_lock is in CLS_FREEING state. This way
		 *
		 *	 osc_ast_data_get(dlmlock) == NULL
		 *
		 * guarantees that all osc references on dlmlock were
		 * released. osc_dlm_blocking_ast0() relies on that.
		 */
		if (lock->cll_state < CLS_FREEING || olck->ols_has_ref) {
			cl_lock_get_trust(lock);
			lu_ref_add_atomic(&lock->cll_reference,
					  "ast", current);
		} else
			olck = NULL;
	}
	spin_unlock(&osc_ast_guard);
	unlock_res_and_lock(dlm_lock);
	return olck;
}

static void osc_ast_data_put(const struct lu_env *env, struct osc_lock *olck)
{
	struct cl_lock *lock;

	lock = olck->ols_cl.cls_lock;
	lu_ref_del(&lock->cll_reference, "ast", current);
	cl_lock_put(env, lock);
}

/**
 * Updates object attributes from a lock value block (lvb) received together
 * with the DLM lock reply from the server. Copy of osc_update_enqueue()
 * logic.
 *
 * This can be optimized to not update attributes when lock is a result of a
 * local match.
 *
 * Called under lock and resource spin-locks.
 */
static void osc_lock_lvb_update(const struct lu_env *env, struct osc_lock *olck,
				int rc)
{
	struct ost_lvb *lvb;
	struct cl_object *obj;
	struct lov_oinfo *oinfo;
	struct cl_attr *attr;
	unsigned valid;

	if (!(olck->ols_flags & LDLM_FL_LVB_READY))
		return;

	lvb = &olck->ols_lvb;
	obj = olck->ols_cl.cls_obj;
	oinfo = cl2osc(obj)->oo_oinfo;
	attr = &osc_env_info(env)->oti_attr;
	valid = CAT_BLOCKS | CAT_ATIME | CAT_CTIME | CAT_MTIME | CAT_SIZE;
	cl_lvb2attr(attr, lvb);

	cl_object_attr_lock(obj);
	if (rc == 0) {
		struct ldlm_lock *dlmlock;
		__u64 size;

		dlmlock = olck->ols_lock;
		LASSERT(dlmlock != NULL);

		/* re-grab LVB from a dlm lock under DLM spin-locks. */
		*lvb = *(struct ost_lvb *)dlmlock->l_lvb_data;
		size = lvb->lvb_size;
		/* Extend KMS up to the end of this lock and no further
		 * A lock on [x,y] means a KMS of up to y + 1 bytes! */
		if (size > dlmlock->l_policy_data.l_extent.end)
			size = dlmlock->l_policy_data.l_extent.end + 1;
		if (size >= oinfo->loi_kms) {
			LDLM_DEBUG(dlmlock, "lock acquired, setting rss=%llu, kms=%llu",
				   lvb->lvb_size, size);
			valid |= CAT_KMS;
			attr->cat_kms = size;
		} else {
			LDLM_DEBUG(dlmlock, "lock acquired, setting rss=%llu; leaving kms=%llu, end=%llu",
				   lvb->lvb_size, oinfo->loi_kms,
				   dlmlock->l_policy_data.l_extent.end);
		}
		ldlm_lock_allow_match_locked(dlmlock);
	} else if (rc == -ENAVAIL && olck->ols_glimpse) {
		CDEBUG(D_INODE, "glimpsed, setting rss=%llu; leaving kms=%llu\n",
		       lvb->lvb_size, oinfo->loi_kms);
	} else
		valid = 0;

	if (valid != 0)
		cl_object_attr_set(env, obj, attr, valid);

	cl_object_attr_unlock(obj);
}

/**
 * Called when a lock is granted, from an upcall (when server returned a
 * granted lock), or from completion AST, when server returned a blocked lock.
 *
 * Called under lock and resource spin-locks, that are released temporarily
 * here.
 */
static void osc_lock_granted(const struct lu_env *env, struct osc_lock *olck,
			     struct ldlm_lock *dlmlock, int rc)
{
	struct ldlm_extent *ext;
	struct cl_lock *lock;
	struct cl_lock_descr *descr;

	LASSERT(dlmlock->l_granted_mode == dlmlock->l_req_mode);

	if (olck->ols_state < OLS_GRANTED) {
		lock = olck->ols_cl.cls_lock;
		ext = &dlmlock->l_policy_data.l_extent;
		descr = &osc_env_info(env)->oti_descr;
		descr->cld_obj = lock->cll_descr.cld_obj;

		/* XXX check that ->l_granted_mode is valid. */
		descr->cld_mode = osc_ldlm2cl_lock(dlmlock->l_granted_mode);
		descr->cld_start = cl_index(descr->cld_obj, ext->start);
		descr->cld_end = cl_index(descr->cld_obj, ext->end);
		descr->cld_gid = ext->gid;
		/*
		 * tell upper layers the extent of the lock that was actually
		 * granted
		 */
		olck->ols_state = OLS_GRANTED;
		osc_lock_lvb_update(env, olck, rc);

		/* release DLM spin-locks to allow cl_lock_{modify,signal}()
		 * to take a semaphore on a parent lock. This is safe, because
		 * spin-locks are needed to protect consistency of
		 * dlmlock->l_*_mode and LVB, and we have finished processing
		 * them. */
		unlock_res_and_lock(dlmlock);
		cl_lock_modify(env, lock, descr);
		cl_lock_signal(env, lock);
		LINVRNT(osc_lock_invariant(olck));
		lock_res_and_lock(dlmlock);
	}
}

static void osc_lock_upcall0(const struct lu_env *env, struct osc_lock *olck)

{
	struct ldlm_lock *dlmlock;

	dlmlock = ldlm_handle2lock_long(&olck->ols_handle, 0);
	LASSERT(dlmlock != NULL);

	lock_res_and_lock(dlmlock);
	spin_lock(&osc_ast_guard);
	LASSERT(dlmlock->l_ast_data == olck);
	LASSERT(olck->ols_lock == NULL);
	olck->ols_lock = dlmlock;
	spin_unlock(&osc_ast_guard);

	/*
	 * Lock might be not yet granted. In this case, completion ast
	 * (osc_ldlm_completion_ast()) comes later and finishes lock
	 * granting.
	 */
	if (dlmlock->l_granted_mode == dlmlock->l_req_mode)
		osc_lock_granted(env, olck, dlmlock, 0);
	unlock_res_and_lock(dlmlock);

	/*
	 * osc_enqueue_interpret() decrefs asynchronous locks, counter
	 * this.
	 */
	ldlm_lock_addref(&olck->ols_handle, olck->ols_einfo.ei_mode);
	olck->ols_hold = 1;

	/* lock reference taken by ldlm_handle2lock_long() is owned by
	 * osc_lock and released in osc_lock_detach() */
	lu_ref_add(&dlmlock->l_reference, "osc_lock", olck);
	olck->ols_has_ref = 1;
}

/**
 * Lock upcall function that is executed either when a reply to ENQUEUE rpc is
 * received from a server, or after osc_enqueue_base() matched a local DLM
 * lock.
 */
static int osc_lock_upcall(void *cookie, int errcode)
{
	struct osc_lock	*olck = cookie;
	struct cl_lock_slice *slice = &olck->ols_cl;
	struct cl_lock *lock = slice->cls_lock;
	struct lu_env *env;
	struct cl_env_nest nest;

	env = cl_env_nested_get(&nest);
	if (!IS_ERR(env)) {
		int rc;

		cl_lock_mutex_get(env, lock);

		LASSERT(lock->cll_state >= CLS_QUEUING);
		if (olck->ols_state == OLS_ENQUEUED) {
			olck->ols_state = OLS_UPCALL_RECEIVED;
			rc = ldlm_error2errno(errcode);
		} else if (olck->ols_state == OLS_CANCELLED) {
			rc = -EIO;
		} else {
			CERROR("Impossible state: %d\n", olck->ols_state);
			LBUG();
		}
		if (rc) {
			struct ldlm_lock *dlmlock;

			dlmlock = ldlm_handle2lock(&olck->ols_handle);
			if (dlmlock != NULL) {
				lock_res_and_lock(dlmlock);
				spin_lock(&osc_ast_guard);
				LASSERT(olck->ols_lock == NULL);
				dlmlock->l_ast_data = NULL;
				olck->ols_handle.cookie = 0ULL;
				spin_unlock(&osc_ast_guard);
				ldlm_lock_fail_match_locked(dlmlock);
				unlock_res_and_lock(dlmlock);
				LDLM_LOCK_PUT(dlmlock);
			}
		} else {
			if (olck->ols_glimpse)
				olck->ols_glimpse = 0;
			osc_lock_upcall0(env, olck);
		}

		/* Error handling, some errors are tolerable. */
		if (olck->ols_locklessable && rc == -EUSERS) {
			/* This is a tolerable error, turn this lock into
			 * lockless lock.
			 */
			osc_object_set_contended(cl2osc(slice->cls_obj));
			LASSERT(slice->cls_ops == &osc_lock_ops);

			/* Change this lock to ldlmlock-less lock. */
			osc_lock_to_lockless(env, olck, 1);
			olck->ols_state = OLS_GRANTED;
			rc = 0;
		} else if (olck->ols_glimpse && rc == -ENAVAIL) {
			osc_lock_lvb_update(env, olck, rc);
			cl_lock_delete(env, lock);
			/* Hide the error. */
			rc = 0;
		}

		if (rc == 0) {
			/* For AGL case, the RPC sponsor may exits the cl_lock
			*  processing without wait() called before related OSC
			*  lock upcall(). So update the lock status according
			*  to the enqueue result inside AGL upcall(). */
			if (olck->ols_agl) {
				lock->cll_flags |= CLF_FROM_UPCALL;
				cl_wait_try(env, lock);
				lock->cll_flags &= ~CLF_FROM_UPCALL;
				if (!olck->ols_glimpse)
					olck->ols_agl = 0;
			}
			cl_lock_signal(env, lock);
			/* del user for lock upcall cookie */
			cl_unuse_try(env, lock);
		} else {
			/* del user for lock upcall cookie */
			cl_lock_user_del(env, lock);
			cl_lock_error(env, lock, rc);
		}

		/* release cookie reference, acquired by osc_lock_enqueue() */
		cl_lock_hold_release(env, lock, "upcall", lock);
		cl_lock_mutex_put(env, lock);

		lu_ref_del(&lock->cll_reference, "upcall", lock);
		/* This maybe the last reference, so must be called after
		 * cl_lock_mutex_put(). */
		cl_lock_put(env, lock);

		cl_env_nested_put(&nest, env);
	} else {
		/* should never happen, similar to osc_ldlm_blocking_ast(). */
		LBUG();
	}
	return errcode;
}

/**
 * Core of osc_dlm_blocking_ast() logic.
 */
static void osc_lock_blocking(const struct lu_env *env,
			      struct ldlm_lock *dlmlock,
			      struct osc_lock *olck, int blocking)
{
	struct cl_lock *lock = olck->ols_cl.cls_lock;

	LASSERT(olck->ols_lock == dlmlock);
	CLASSERT(OLS_BLOCKED < OLS_CANCELLED);
	LASSERT(!osc_lock_is_lockless(olck));

	/*
	 * Lock might be still addref-ed here, if e.g., blocking ast
	 * is sent for a failed lock.
	 */
	osc_lock_unhold(olck);

	if (blocking && olck->ols_state < OLS_BLOCKED)
		/*
		 * Move osc_lock into OLS_BLOCKED before canceling the lock,
		 * because it recursively re-enters osc_lock_blocking(), with
		 * the state set to OLS_CANCELLED.
		 */
		olck->ols_state = OLS_BLOCKED;
	/*
	 * cancel and destroy lock at least once no matter how blocking ast is
	 * entered (see comment above osc_ldlm_blocking_ast() for use
	 * cases). cl_lock_cancel() and cl_lock_delete() are idempotent.
	 */
	cl_lock_cancel(env, lock);
	cl_lock_delete(env, lock);
}

/**
 * Helper for osc_dlm_blocking_ast() handling discrepancies between cl_lock
 * and ldlm_lock caches.
 */
static int osc_dlm_blocking_ast0(const struct lu_env *env,
				 struct ldlm_lock *dlmlock,
				 void *data, int flag)
{
	struct osc_lock *olck;
	struct cl_lock *lock;
	int result;
	int cancel;

	LASSERT(flag == LDLM_CB_BLOCKING || flag == LDLM_CB_CANCELING);

	cancel = 0;
	olck = osc_ast_data_get(dlmlock);
	if (olck != NULL) {
		lock = olck->ols_cl.cls_lock;
		cl_lock_mutex_get(env, lock);
		LINVRNT(osc_lock_invariant(olck));
		if (olck->ols_ast_wait) {
			/* wake up osc_lock_use() */
			cl_lock_signal(env, lock);
			olck->ols_ast_wait = 0;
		}
		/*
		 * Lock might have been canceled while this thread was
		 * sleeping for lock mutex, but olck is pinned in memory.
		 */
		if (olck == dlmlock->l_ast_data) {
			/*
			 * NOTE: DLM sends blocking AST's for failed locks
			 *       (that are still in pre-OLS_GRANTED state)
			 *       too, and they have to be canceled otherwise
			 *       DLM lock is never destroyed and stuck in
			 *       the memory.
			 *
			 *       Alternatively, ldlm_cli_cancel() can be
			 *       called here directly for osc_locks with
			 *       ols_state < OLS_GRANTED to maintain an
			 *       invariant that ->clo_cancel() is only called
			 *       for locks that were granted.
			 */
			LASSERT(data == olck);
			osc_lock_blocking(env, dlmlock,
					  olck, flag == LDLM_CB_BLOCKING);
		} else
			cancel = 1;
		cl_lock_mutex_put(env, lock);
		osc_ast_data_put(env, olck);
	} else
		/*
		 * DLM lock exists, but there is no cl_lock attached to it.
		 * This is a `normal' race. cl_object and its cl_lock's can be
		 * removed by memory pressure, together with all pages.
		 */
		cancel = (flag == LDLM_CB_BLOCKING);

	if (cancel) {
		struct lustre_handle *lockh;

		lockh = &osc_env_info(env)->oti_handle;
		ldlm_lock2handle(dlmlock, lockh);
		result = ldlm_cli_cancel(lockh, LCF_ASYNC);
	} else
		result = 0;
	return result;
}

/**
 * Blocking ast invoked by ldlm when dlm lock is either blocking progress of
 * some other lock, or is canceled. This function is installed as a
 * ldlm_lock::l_blocking_ast() for client extent locks.
 *
 * Control flow is tricky, because ldlm uses the same call-back
 * (ldlm_lock::l_blocking_ast()) for both blocking and cancellation ast's.
 *
 * \param dlmlock lock for which ast occurred.
 *
 * \param new description of a conflicting lock in case of blocking ast.
 *
 * \param data value of dlmlock->l_ast_data
 *
 * \param flag LDLM_CB_BLOCKING or LDLM_CB_CANCELING. Used to distinguish
 *	     cancellation and blocking ast's.
 *
 * Possible use cases:
 *
 *     - ldlm calls dlmlock->l_blocking_ast(..., LDLM_CB_CANCELING) to cancel
 *       lock due to lock lru pressure, or explicit user request to purge
 *       locks.
 *
 *     - ldlm calls dlmlock->l_blocking_ast(..., LDLM_CB_BLOCKING) to notify
 *       us that dlmlock conflicts with another lock that some client is
 *       enqueing. Lock is canceled.
 *
 *	   - cl_lock_cancel() is called. osc_lock_cancel() calls
 *	     ldlm_cli_cancel() that calls
 *
 *		  dlmlock->l_blocking_ast(..., LDLM_CB_CANCELING)
 *
 *	     recursively entering osc_ldlm_blocking_ast().
 *
 *     - client cancels lock voluntary (e.g., as a part of early cancellation):
 *
 *	   cl_lock_cancel()->
 *	     osc_lock_cancel()->
 *	       ldlm_cli_cancel()->
 *		 dlmlock->l_blocking_ast(..., LDLM_CB_CANCELING)
 *
 */
static int osc_ldlm_blocking_ast(struct ldlm_lock *dlmlock,
				 struct ldlm_lock_desc *new, void *data,
				 int flag)
{
	struct lu_env *env;
	struct cl_env_nest nest;
	int result;

	/*
	 * This can be called in the context of outer IO, e.g.,
	 *
	 *     cl_enqueue()->...
	 *       ->osc_enqueue_base()->...
	 *	 ->ldlm_prep_elc_req()->...
	 *	   ->ldlm_cancel_callback()->...
	 *	     ->osc_ldlm_blocking_ast()
	 *
	 * new environment has to be created to not corrupt outer context.
	 */
	env = cl_env_nested_get(&nest);
	if (!IS_ERR(env)) {
		result = osc_dlm_blocking_ast0(env, dlmlock, data, flag);
		cl_env_nested_put(&nest, env);
	} else {
		result = PTR_ERR(env);
		/*
		 * XXX This should never happen, as cl_lock is
		 * stuck. Pre-allocated environment a la vvp_inode_fini_env
		 * should be used.
		 */
		LBUG();
	}
	if (result != 0) {
		if (result == -ENODATA)
			result = 0;
		else
			CERROR("BAST failed: %d\n", result);
	}
	return result;
}

static int osc_ldlm_completion_ast(struct ldlm_lock *dlmlock,
				   __u64 flags, void *data)
{
	struct cl_env_nest nest;
	struct lu_env *env;
	struct osc_lock *olck;
	struct cl_lock *lock;
	int result;
	int dlmrc;

	/* first, do dlm part of the work */
	dlmrc = ldlm_completion_ast_async(dlmlock, flags, data);
	/* then, notify cl_lock */
	env = cl_env_nested_get(&nest);
	if (!IS_ERR(env)) {
		olck = osc_ast_data_get(dlmlock);
		if (olck != NULL) {
			lock = olck->ols_cl.cls_lock;
			cl_lock_mutex_get(env, lock);
			/*
			 * ldlm_handle_cp_callback() copied LVB from request
			 * to lock->l_lvb_data, store it in osc_lock.
			 */
			LASSERT(dlmlock->l_lvb_data != NULL);
			lock_res_and_lock(dlmlock);
			olck->ols_lvb = *(struct ost_lvb *)dlmlock->l_lvb_data;
			if (olck->ols_lock == NULL) {
				/*
				 * upcall (osc_lock_upcall()) hasn't yet been
				 * called. Do nothing now, upcall will bind
				 * olck to dlmlock and signal the waiters.
				 *
				 * This maintains an invariant that osc_lock
				 * and ldlm_lock are always bound when
				 * osc_lock is in OLS_GRANTED state.
				 */
			} else if (dlmlock->l_granted_mode ==
				   dlmlock->l_req_mode) {
				osc_lock_granted(env, olck, dlmlock, dlmrc);
			}
			unlock_res_and_lock(dlmlock);

			if (dlmrc != 0) {
				CL_LOCK_DEBUG(D_ERROR, env, lock,
					      "dlmlock returned %d\n", dlmrc);
				cl_lock_error(env, lock, dlmrc);
			}
			cl_lock_mutex_put(env, lock);
			osc_ast_data_put(env, olck);
			result = 0;
		} else
			result = -ELDLM_NO_LOCK_DATA;
		cl_env_nested_put(&nest, env);
	} else
		result = PTR_ERR(env);
	return dlmrc ?: result;
}

static int osc_ldlm_glimpse_ast(struct ldlm_lock *dlmlock, void *data)
{
	struct ptlrpc_request *req = data;
	struct osc_lock	*olck;
	struct cl_lock *lock;
	struct cl_object *obj;
	struct cl_env_nest nest;
	struct lu_env *env;
	struct ost_lvb *lvb;
	struct req_capsule *cap;
	int result;

	LASSERT(lustre_msg_get_opc(req->rq_reqmsg) == LDLM_GL_CALLBACK);

	env = cl_env_nested_get(&nest);
	if (!IS_ERR(env)) {
		/* osc_ast_data_get() has to go after environment is
		 * allocated, because osc_ast_data() acquires a
		 * reference to a lock, and it can only be released in
		 * environment.
		 */
		olck = osc_ast_data_get(dlmlock);
		if (olck != NULL) {
			lock = olck->ols_cl.cls_lock;
			/* Do not grab the mutex of cl_lock for glimpse.
			 * See LU-1274 for details.
			 * BTW, it's okay for cl_lock to be cancelled during
			 * this period because server can handle this race.
			 * See ldlm_server_glimpse_ast() for details.
			 * cl_lock_mutex_get(env, lock); */
			cap = &req->rq_pill;
			req_capsule_extend(cap, &RQF_LDLM_GL_CALLBACK);
			req_capsule_set_size(cap, &RMF_DLM_LVB, RCL_SERVER,
					     sizeof(*lvb));
			result = req_capsule_server_pack(cap);
			if (result == 0) {
				lvb = req_capsule_server_get(cap, &RMF_DLM_LVB);
				obj = lock->cll_descr.cld_obj;
				result = cl_object_glimpse(env, obj, lvb);
			}
			if (!exp_connect_lvb_type(req->rq_export))
				req_capsule_shrink(&req->rq_pill,
						   &RMF_DLM_LVB,
						   sizeof(struct ost_lvb_v1),
						   RCL_SERVER);
			osc_ast_data_put(env, olck);
		} else {
			/*
			 * These errors are normal races, so we don't want to
			 * fill the console with messages by calling
			 * ptlrpc_error()
			 */
			lustre_pack_reply(req, 1, NULL, NULL);
			result = -ELDLM_NO_LOCK_DATA;
		}
		cl_env_nested_put(&nest, env);
	} else
		result = PTR_ERR(env);
	req->rq_status = result;
	return result;
}

static unsigned long osc_lock_weigh(const struct lu_env *env,
				    const struct cl_lock_slice *slice)
{
	/*
	 * don't need to grab coh_page_guard since we don't care the exact #
	 * of pages..
	 */
	return cl_object_header(slice->cls_obj)->coh_pages;
}

static void osc_lock_build_einfo(const struct lu_env *env,
				 const struct cl_lock *clock,
				 struct osc_lock *lock,
				 struct ldlm_enqueue_info *einfo)
{
	enum cl_lock_mode mode;

	mode = clock->cll_descr.cld_mode;
	if (mode == CLM_PHANTOM)
		/*
		 * For now, enqueue all glimpse locks in read mode. In the
		 * future, client might choose to enqueue LCK_PW lock for
		 * glimpse on a file opened for write.
		 */
		mode = CLM_READ;

	einfo->ei_type = LDLM_EXTENT;
	einfo->ei_mode = osc_cl_lock2ldlm(mode);
	einfo->ei_cb_bl = osc_ldlm_blocking_ast;
	einfo->ei_cb_cp = osc_ldlm_completion_ast;
	einfo->ei_cb_gl = osc_ldlm_glimpse_ast;
	einfo->ei_cbdata = lock; /* value to be put into ->l_ast_data */
}

/**
 * Determine if the lock should be converted into a lockless lock.
 *
 * Steps to check:
 * - if the lock has an explicit requirement for a non-lockless lock;
 * - if the io lock request type ci_lockreq;
 * - send the enqueue rpc to ost to make the further decision;
 * - special treat to truncate lockless lock
 *
 *  Additional policy can be implemented here, e.g., never do lockless-io
 *  for large extents.
 */
static void osc_lock_to_lockless(const struct lu_env *env,
				 struct osc_lock *ols, int force)
{
	struct cl_lock_slice *slice = &ols->ols_cl;

	LASSERT(ols->ols_state == OLS_NEW ||
		ols->ols_state == OLS_UPCALL_RECEIVED);

	if (force) {
		ols->ols_locklessable = 1;
		slice->cls_ops = &osc_lock_lockless_ops;
	} else {
		struct osc_io *oio = osc_env_io(env);
		struct cl_io *io = oio->oi_cl.cis_io;
		struct cl_object *obj = slice->cls_obj;
		struct osc_object *oob = cl2osc(obj);
		const struct osc_device *osd = lu2osc_dev(obj->co_lu.lo_dev);
		struct obd_connect_data *ocd;

		LASSERT(io->ci_lockreq == CILR_MANDATORY ||
			io->ci_lockreq == CILR_MAYBE ||
			io->ci_lockreq == CILR_NEVER);

		ocd = &class_exp2cliimp(osc_export(oob))->imp_connect_data;
		ols->ols_locklessable = (io->ci_type != CIT_SETATTR) &&
				(io->ci_lockreq == CILR_MAYBE) &&
				(ocd->ocd_connect_flags & OBD_CONNECT_SRVLOCK);
		if (io->ci_lockreq == CILR_NEVER ||
			/* lockless IO */
		    (ols->ols_locklessable && osc_object_is_contended(oob)) ||
			/* lockless truncate */
		    (cl_io_is_trunc(io) &&
		     (ocd->ocd_connect_flags & OBD_CONNECT_TRUNCLOCK) &&
		      osd->od_lockless_truncate)) {
			ols->ols_locklessable = 1;
			slice->cls_ops = &osc_lock_lockless_ops;
		}
	}
	LASSERT(ergo(ols->ols_glimpse, !osc_lock_is_lockless(ols)));
}

static int osc_lock_compatible(const struct osc_lock *qing,
			       const struct osc_lock *qed)
{
	enum cl_lock_mode qing_mode;
	enum cl_lock_mode qed_mode;

	qing_mode = qing->ols_cl.cls_lock->cll_descr.cld_mode;
	if (qed->ols_glimpse &&
	    (qed->ols_state >= OLS_UPCALL_RECEIVED || qing_mode == CLM_READ))
		return 1;

	qed_mode = qed->ols_cl.cls_lock->cll_descr.cld_mode;
	return ((qing_mode == CLM_READ) && (qed_mode == CLM_READ));
}

/**
 * Cancel all conflicting locks and wait for them to be destroyed.
 *
 * This function is used for two purposes:
 *
 *     - early cancel all conflicting locks before starting IO, and
 *
 *     - guarantee that pages added to the page cache by lockless IO are never
 *       covered by locks other than lockless IO lock, and, hence, are not
 *       visible to other threads.
 */
static int osc_lock_enqueue_wait(const struct lu_env *env,
				 const struct osc_lock *olck)
{
	struct cl_lock *lock = olck->ols_cl.cls_lock;
	struct cl_lock_descr *descr = &lock->cll_descr;
	struct cl_object_header *hdr = cl_object_header(descr->cld_obj);
	struct cl_lock *scan;
	struct cl_lock *conflict = NULL;
	int lockless = osc_lock_is_lockless(olck);
	int rc = 0;

	LASSERT(cl_lock_is_mutexed(lock));

	/* make it enqueue anyway for glimpse lock, because we actually
	 * don't need to cancel any conflicting locks. */
	if (olck->ols_glimpse)
		return 0;

	spin_lock(&hdr->coh_lock_guard);
	list_for_each_entry(scan, &hdr->coh_locks, cll_linkage) {
		struct cl_lock_descr *cld = &scan->cll_descr;
		const struct osc_lock *scan_ols;

		if (scan == lock)
			break;

		if (scan->cll_state < CLS_QUEUING ||
		    scan->cll_state == CLS_FREEING ||
		    cld->cld_start > descr->cld_end ||
		    cld->cld_end < descr->cld_start)
			continue;

		/* overlapped and living locks. */

		/* We're not supposed to give up group lock. */
		if (scan->cll_descr.cld_mode == CLM_GROUP) {
			LASSERT(descr->cld_mode != CLM_GROUP ||
				descr->cld_gid != scan->cll_descr.cld_gid);
			continue;
		}

		scan_ols = osc_lock_at(scan);

		/* We need to cancel the compatible locks if we're enqueuing
		 * a lockless lock, for example:
		 * imagine that client has PR lock on [0, 1000], and thread T0
		 * is doing lockless IO in [500, 1500] region. Concurrent
		 * thread T1 can see lockless data in [500, 1000], which is
		 * wrong, because these data are possibly stale. */
		if (!lockless && osc_lock_compatible(olck, scan_ols))
			continue;

		cl_lock_get_trust(scan);
		conflict = scan;
		break;
	}
	spin_unlock(&hdr->coh_lock_guard);

	if (conflict) {
		if (lock->cll_descr.cld_mode == CLM_GROUP) {
			/* we want a group lock but a previous lock request
			 * conflicts, we do not wait but return 0 so the
			 * request is send to the server
			 */
			CDEBUG(D_DLMTRACE, "group lock %p is conflicted with %p, no wait, send to server\n",
			       lock, conflict);
			cl_lock_put(env, conflict);
			rc = 0;
		} else {
			CDEBUG(D_DLMTRACE, "lock %p is conflicted with %p, will wait\n",
			       lock, conflict);
			LASSERT(lock->cll_conflict == NULL);
			lu_ref_add(&conflict->cll_reference, "cancel-wait",
				   lock);
			lock->cll_conflict = conflict;
			rc = CLO_WAIT;
		}
	}
	return rc;
}

/**
 * Implementation of cl_lock_operations::clo_enqueue() method for osc
 * layer. This initiates ldlm enqueue:
 *
 *     - cancels conflicting locks early (osc_lock_enqueue_wait());
 *
 *     - calls osc_enqueue_base() to do actual enqueue.
 *
 * osc_enqueue_base() is supplied with an upcall function that is executed
 * when lock is received either after a local cached ldlm lock is matched, or
 * when a reply from the server is received.
 *
 * This function does not wait for the network communication to complete.
 */
static int osc_lock_enqueue(const struct lu_env *env,
			    const struct cl_lock_slice *slice,
			    struct cl_io *unused, __u32 enqflags)
{
	struct osc_lock *ols = cl2osc_lock(slice);
	struct cl_lock *lock = ols->ols_cl.cls_lock;
	int result;

	LASSERT(cl_lock_is_mutexed(lock));
	LASSERTF(ols->ols_state == OLS_NEW,
		 "Impossible state: %d\n", ols->ols_state);

	LASSERTF(ergo(ols->ols_glimpse, lock->cll_descr.cld_mode <= CLM_READ),
		"lock = %p, ols = %p\n", lock, ols);

	result = osc_lock_enqueue_wait(env, ols);
	if (result == 0) {
		if (!osc_lock_is_lockless(ols)) {
			struct osc_object *obj = cl2osc(slice->cls_obj);
			struct osc_thread_info *info = osc_env_info(env);
			struct ldlm_res_id *resname = &info->oti_resname;
			ldlm_policy_data_t *policy = &info->oti_policy;
			struct ldlm_enqueue_info *einfo = &ols->ols_einfo;

			/* lock will be passed as upcall cookie,
			 * hold ref to prevent to be released. */
			cl_lock_hold_add(env, lock, "upcall", lock);
			/* a user for lock also */
			cl_lock_user_add(env, lock);
			ols->ols_state = OLS_ENQUEUED;

			/*
			 * XXX: this is possible blocking point as
			 * ldlm_lock_match(LDLM_FL_LVB_READY) waits for
			 * LDLM_CP_CALLBACK.
			 */
			ostid_build_res_name(&obj->oo_oinfo->loi_oi, resname);
			osc_lock_build_policy(env, lock, policy);
			result = osc_enqueue_base(osc_export(obj), resname,
					  &ols->ols_flags, policy,
					  &ols->ols_lvb,
					  obj->oo_oinfo->loi_kms_valid,
					  osc_lock_upcall,
					  ols, einfo, &ols->ols_handle,
					  PTLRPCD_SET, 1, ols->ols_agl);
			if (result != 0) {
				cl_lock_user_del(env, lock);
				cl_lock_unhold(env, lock, "upcall", lock);
				if (unlikely(result == -ECANCELED)) {
					ols->ols_state = OLS_NEW;
					result = 0;
				}
			}
		} else {
			ols->ols_state = OLS_GRANTED;
			ols->ols_owner = osc_env_io(env);
		}
	}
	LASSERT(ergo(ols->ols_glimpse, !osc_lock_is_lockless(ols)));
	return result;
}

static int osc_lock_wait(const struct lu_env *env,
			 const struct cl_lock_slice *slice)
{
	struct osc_lock *olck = cl2osc_lock(slice);
	struct cl_lock *lock = olck->ols_cl.cls_lock;

	LINVRNT(osc_lock_invariant(olck));

	if (olck->ols_glimpse && olck->ols_state >= OLS_UPCALL_RECEIVED) {
		if (olck->ols_flags & LDLM_FL_LVB_READY) {
			return 0;
		} else if (olck->ols_agl) {
			if (lock->cll_flags & CLF_FROM_UPCALL)
				/* It is from enqueue RPC reply upcall for
				 * updating state. Do not re-enqueue. */
				return -ENAVAIL;
			olck->ols_state = OLS_NEW;
		} else {
			LASSERT(lock->cll_error);
			return lock->cll_error;
		}
	}

	if (olck->ols_state == OLS_NEW) {
		int rc;

		LASSERT(olck->ols_agl);
		olck->ols_agl = 0;
		olck->ols_flags &= ~LDLM_FL_BLOCK_NOWAIT;
		rc = osc_lock_enqueue(env, slice, NULL, CEF_ASYNC | CEF_MUST);
		if (rc != 0)
			return rc;
		else
			return CLO_REENQUEUED;
	}

	LASSERT(equi(olck->ols_state >= OLS_UPCALL_RECEIVED &&
		     lock->cll_error == 0, olck->ols_lock != NULL));

	return lock->cll_error ?: olck->ols_state >= OLS_GRANTED ? 0 : CLO_WAIT;
}

/**
 * An implementation of cl_lock_operations::clo_use() method that pins cached
 * lock.
 */
static int osc_lock_use(const struct lu_env *env,
			const struct cl_lock_slice *slice)
{
	struct osc_lock *olck = cl2osc_lock(slice);
	int rc;

	LASSERT(!olck->ols_hold);

	/*
	 * Atomically check for LDLM_FL_CBPENDING and addref a lock if this
	 * flag is not set. This protects us from a concurrent blocking ast.
	 */
	rc = ldlm_lock_addref_try(&olck->ols_handle, olck->ols_einfo.ei_mode);
	if (rc == 0) {
		olck->ols_hold = 1;
		olck->ols_state = OLS_GRANTED;
	} else {
		struct cl_lock *lock;

		/*
		 * Lock is being cancelled somewhere within
		 * ldlm_handle_bl_callback(): LDLM_FL_CBPENDING is already
		 * set, but osc_ldlm_blocking_ast() hasn't yet acquired
		 * cl_lock mutex.
		 */
		lock = slice->cls_lock;
		LASSERT(lock->cll_state == CLS_INTRANSIT);
		LASSERT(lock->cll_users > 0);
		/* set a flag for osc_dlm_blocking_ast0() to signal the
		 * lock.*/
		olck->ols_ast_wait = 1;
		rc = CLO_WAIT;
	}
	return rc;
}

static int osc_lock_flush(struct osc_lock *ols, int discard)
{
	struct cl_lock *lock = ols->ols_cl.cls_lock;
	struct cl_env_nest nest;
	struct lu_env *env;
	int result = 0;

	env = cl_env_nested_get(&nest);
	if (!IS_ERR(env)) {
		struct osc_object *obj = cl2osc(ols->ols_cl.cls_obj);
		struct cl_lock_descr *descr = &lock->cll_descr;
		int rc = 0;

		if (descr->cld_mode >= CLM_WRITE) {
			result = osc_cache_writeback_range(env, obj,
					descr->cld_start, descr->cld_end,
					1, discard);
			LDLM_DEBUG(ols->ols_lock,
				"lock %p: %d pages were %s.\n", lock, result,
				discard ? "discarded" : "written");
			if (result > 0)
				result = 0;
		}

		rc = cl_lock_discard_pages(env, lock);
		if (result == 0 && rc < 0)
			result = rc;

		cl_env_nested_put(&nest, env);
	} else
		result = PTR_ERR(env);
	if (result == 0) {
		ols->ols_flush = 1;
		LINVRNT(!osc_lock_has_pages(ols));
	}
	return result;
}

/**
 * Implements cl_lock_operations::clo_cancel() method for osc layer. This is
 * called (as part of cl_lock_cancel()) when lock is canceled either voluntary
 * (LRU pressure, early cancellation, umount, etc.) or due to the conflict
 * with some other lock some where in the cluster. This function does the
 * following:
 *
 *     - invalidates all pages protected by this lock (after sending dirty
 *       ones to the server, as necessary);
 *
 *     - decref's underlying ldlm lock;
 *
 *     - cancels ldlm lock (ldlm_cli_cancel()).
 */
static void osc_lock_cancel(const struct lu_env *env,
			    const struct cl_lock_slice *slice)
{
	struct cl_lock *lock = slice->cls_lock;
	struct osc_lock *olck = cl2osc_lock(slice);
	struct ldlm_lock *dlmlock = olck->ols_lock;
	int result = 0;
	int discard;

	LASSERT(cl_lock_is_mutexed(lock));
	LINVRNT(osc_lock_invariant(olck));

	if (dlmlock != NULL) {
		int do_cancel;

		discard = !!(dlmlock->l_flags & LDLM_FL_DISCARD_DATA);
		if (olck->ols_state >= OLS_GRANTED)
			result = osc_lock_flush(olck, discard);
		osc_lock_unhold(olck);

		lock_res_and_lock(dlmlock);
		/* Now that we're the only user of dlm read/write reference,
		 * mostly the ->l_readers + ->l_writers should be zero.
		 * However, there is a corner case.
		 * See bug 18829 for details.*/
		do_cancel = (dlmlock->l_readers == 0 &&
			     dlmlock->l_writers == 0);
		dlmlock->l_flags |= LDLM_FL_CBPENDING;
		unlock_res_and_lock(dlmlock);
		if (do_cancel)
			result = ldlm_cli_cancel(&olck->ols_handle, LCF_ASYNC);
		if (result < 0)
			CL_LOCK_DEBUG(D_ERROR, env, lock,
				      "lock %p cancel failure with error(%d)\n",
				      lock, result);
	}
	olck->ols_state = OLS_CANCELLED;
	olck->ols_flags &= ~LDLM_FL_LVB_READY;
	osc_lock_detach(env, olck);
}

static int osc_lock_has_pages(struct osc_lock *olck)
{
	return 0;
}

static void osc_lock_delete(const struct lu_env *env,
			    const struct cl_lock_slice *slice)
{
	struct osc_lock *olck;

	olck = cl2osc_lock(slice);
	if (olck->ols_glimpse) {
		LASSERT(!olck->ols_hold);
		LASSERT(!olck->ols_lock);
		return;
	}

	LINVRNT(osc_lock_invariant(olck));
	LINVRNT(!osc_lock_has_pages(olck));

	osc_lock_unhold(olck);
	osc_lock_detach(env, olck);
}

/**
 * Implements cl_lock_operations::clo_state() method for osc layer.
 *
 * Maintains osc_lock::ols_owner field.
 *
 * This assumes that lock always enters CLS_HELD (from some other state) in
 * the same IO context as one that requested the lock. This should not be a
 * problem, because context is by definition shared by all activity pertaining
 * to the same high-level IO.
 */
static void osc_lock_state(const struct lu_env *env,
			   const struct cl_lock_slice *slice,
			   enum cl_lock_state state)
{
	struct osc_lock *lock = cl2osc_lock(slice);

	/*
	 * XXX multiple io contexts can use the lock at the same time.
	 */
	LINVRNT(osc_lock_invariant(lock));
	if (state == CLS_HELD && slice->cls_lock->cll_state != CLS_HELD) {
		struct osc_io *oio = osc_env_io(env);

		LASSERT(lock->ols_owner == NULL);
		lock->ols_owner = oio;
	} else if (state != CLS_HELD)
		lock->ols_owner = NULL;
}

static int osc_lock_print(const struct lu_env *env, void *cookie,
			  lu_printer_t p, const struct cl_lock_slice *slice)
{
	struct osc_lock *lock = cl2osc_lock(slice);

	/*
	 * XXX print ldlm lock and einfo properly.
	 */
	(*p)(env, cookie, "%p %#16llx %#llx %d %p ",
	     lock->ols_lock, lock->ols_flags, lock->ols_handle.cookie,
	     lock->ols_state, lock->ols_owner);
	osc_lvb_print(env, cookie, p, &lock->ols_lvb);
	return 0;
}

static int osc_lock_fits_into(const struct lu_env *env,
			      const struct cl_lock_slice *slice,
			      const struct cl_lock_descr *need,
			      const struct cl_io *io)
{
	struct osc_lock *ols = cl2osc_lock(slice);

	if (need->cld_enq_flags & CEF_NEVER)
		return 0;

	if (ols->ols_state >= OLS_CANCELLED)
		return 0;

	if (need->cld_mode == CLM_PHANTOM) {
		if (ols->ols_agl)
			return !(ols->ols_state > OLS_RELEASED);

		/*
		 * Note: the QUEUED lock can't be matched here, otherwise
		 * it might cause the deadlocks.
		 * In read_process,
		 * P1: enqueued read lock, create sublock1
		 * P2: enqueued write lock, create sublock2(conflicted
		 *     with sublock1).
		 * P1: Grant read lock.
		 * P1: enqueued glimpse lock(with holding sublock1_read),
		 *     matched with sublock2, waiting sublock2 to be granted.
		 *     But sublock2 can not be granted, because P1
		 *     will not release sublock1. Bang!
		 */
		if (ols->ols_state < OLS_GRANTED ||
		    ols->ols_state > OLS_RELEASED)
			return 0;
	} else if (need->cld_enq_flags & CEF_MUST) {
		/*
		 * If the lock hasn't ever enqueued, it can't be matched
		 * because enqueue process brings in many information
		 * which can be used to determine things such as lockless,
		 * CEF_MUST, etc.
		 */
		if (ols->ols_state < OLS_UPCALL_RECEIVED &&
		    ols->ols_locklessable)
			return 0;
	}
	return 1;
}

static const struct cl_lock_operations osc_lock_ops = {
	.clo_fini    = osc_lock_fini,
	.clo_enqueue = osc_lock_enqueue,
	.clo_wait    = osc_lock_wait,
	.clo_unuse   = osc_lock_unuse,
	.clo_use     = osc_lock_use,
	.clo_delete  = osc_lock_delete,
	.clo_state   = osc_lock_state,
	.clo_cancel  = osc_lock_cancel,
	.clo_weigh   = osc_lock_weigh,
	.clo_print   = osc_lock_print,
	.clo_fits_into = osc_lock_fits_into,
};

static int osc_lock_lockless_unuse(const struct lu_env *env,
				   const struct cl_lock_slice *slice)
{
	struct osc_lock *ols = cl2osc_lock(slice);
	struct cl_lock *lock = slice->cls_lock;

	LASSERT(ols->ols_state == OLS_GRANTED);
	LINVRNT(osc_lock_invariant(ols));

	cl_lock_cancel(env, lock);
	cl_lock_delete(env, lock);
	return 0;
}

static void osc_lock_lockless_cancel(const struct lu_env *env,
				     const struct cl_lock_slice *slice)
{
	struct osc_lock *ols = cl2osc_lock(slice);
	int result;

	result = osc_lock_flush(ols, 0);
	if (result)
		CERROR("Pages for lockless lock %p were not purged(%d)\n",
		       ols, result);
	ols->ols_state = OLS_CANCELLED;
}

static int osc_lock_lockless_wait(const struct lu_env *env,
				  const struct cl_lock_slice *slice)
{
	struct osc_lock *olck = cl2osc_lock(slice);
	struct cl_lock *lock = olck->ols_cl.cls_lock;

	LINVRNT(osc_lock_invariant(olck));
	LASSERT(olck->ols_state >= OLS_UPCALL_RECEIVED);

	return lock->cll_error;
}

static void osc_lock_lockless_state(const struct lu_env *env,
				    const struct cl_lock_slice *slice,
				    enum cl_lock_state state)
{
	struct osc_lock *lock = cl2osc_lock(slice);

	LINVRNT(osc_lock_invariant(lock));
	if (state == CLS_HELD) {
		struct osc_io *oio = osc_env_io(env);

		LASSERT(ergo(lock->ols_owner, lock->ols_owner == oio));
		lock->ols_owner = oio;

		/* set the io to be lockless if this lock is for io's
		 * host object */
		if (cl_object_same(oio->oi_cl.cis_obj, slice->cls_obj))
			oio->oi_lockless = 1;
	}
}

static int osc_lock_lockless_fits_into(const struct lu_env *env,
				       const struct cl_lock_slice *slice,
				       const struct cl_lock_descr *need,
				       const struct cl_io *io)
{
	struct osc_lock *lock = cl2osc_lock(slice);

	if (!(need->cld_enq_flags & CEF_NEVER))
		return 0;

	/* lockless lock should only be used by its owning io. b22147 */
	return (lock->ols_owner == osc_env_io(env));
}

static const struct cl_lock_operations osc_lock_lockless_ops = {
	.clo_fini      = osc_lock_fini,
	.clo_enqueue   = osc_lock_enqueue,
	.clo_wait      = osc_lock_lockless_wait,
	.clo_unuse     = osc_lock_lockless_unuse,
	.clo_state     = osc_lock_lockless_state,
	.clo_fits_into = osc_lock_lockless_fits_into,
	.clo_cancel    = osc_lock_lockless_cancel,
	.clo_print     = osc_lock_print
};

int osc_lock_init(const struct lu_env *env,
		  struct cl_object *obj, struct cl_lock *lock,
		  const struct cl_io *unused)
{
	struct osc_lock *clk;
	int result;

	OBD_SLAB_ALLOC_PTR_GFP(clk, osc_lock_kmem, GFP_NOFS);
	if (clk != NULL) {
		__u32 enqflags = lock->cll_descr.cld_enq_flags;

		osc_lock_build_einfo(env, lock, clk, &clk->ols_einfo);
		atomic_set(&clk->ols_pageref, 0);
		clk->ols_state = OLS_NEW;

		clk->ols_flags = osc_enq2ldlm_flags(enqflags);
		clk->ols_agl = !!(enqflags & CEF_AGL);
		if (clk->ols_agl)
			clk->ols_flags |= LDLM_FL_BLOCK_NOWAIT;
		if (clk->ols_flags & LDLM_FL_HAS_INTENT)
			clk->ols_glimpse = 1;

		cl_lock_slice_add(lock, &clk->ols_cl, obj, &osc_lock_ops);

		if (!(enqflags & CEF_MUST))
			/* try to convert this lock to a lockless lock */
			osc_lock_to_lockless(env, clk, (enqflags & CEF_NEVER));
		if (clk->ols_locklessable && !(enqflags & CEF_DISCARD_DATA))
			clk->ols_flags |= LDLM_FL_DENY_ON_CONTENTION;

		LDLM_DEBUG_NOLOCK("lock %p, osc lock %p, flags %llx\n",
				lock, clk, clk->ols_flags);

		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

int osc_dlm_lock_pageref(struct ldlm_lock *dlm)
{
	struct osc_lock *olock;
	int rc = 0;

	spin_lock(&osc_ast_guard);
	olock = dlm->l_ast_data;
	/*
	 * there's a very rare race with osc_page_addref_lock(), but that
	 * doesn't matter because in the worst case we don't cancel a lock
	 * which we actually can, that's no harm.
	 */
	if (olock != NULL &&
	    atomic_add_return(_PAGEREF_MAGIC,
				  &olock->ols_pageref) != _PAGEREF_MAGIC) {
		atomic_sub(_PAGEREF_MAGIC, &olock->ols_pageref);
		rc = 1;
	}
	spin_unlock(&osc_ast_guard);
	return rc;
}

/** @} osc */
