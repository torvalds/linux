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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003 Hewlett-Packard Development Company LP.
 * Developed under the sponsorship of the US Government under
 * Subcontract No. B514193
 *
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2010, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

/**
 * This file implements POSIX lock type for Lustre.
 * Its policy properties are start and end of extent and PID.
 *
 * These locks are only done through MDS due to POSIX semantics requiring
 * e.g. that locks could be only partially released and as such split into
 * two parts, and also that two adjacent locks from the same process may be
 * merged into a single wider lock.
 *
 * Lock modes are mapped like this:
 * PR and PW for READ and WRITE locks
 * NL to request a releasing of a portion of the lock
 *
 * These flock locks never timeout.
 */

#define DEBUG_SUBSYSTEM S_LDLM

#include "../include/lustre_dlm.h"
#include "../include/obd_support.h"
#include "../include/obd_class.h"
#include "../include/lustre_lib.h"
#include <linux/list.h>
#include "ldlm_internal.h"

/**
 * list_for_remaining_safe - iterate over the remaining entries in a list
 *	      and safeguard against removal of a list entry.
 * \param pos   the &struct list_head to use as a loop counter. pos MUST
 *	      have been initialized prior to using it in this macro.
 * \param n     another &struct list_head to use as temporary storage
 * \param head  the head for your list.
 */
#define list_for_remaining_safe(pos, n, head) \
	for (n = pos->next; pos != (head); pos = n, n = pos->next)

static inline int
ldlm_same_flock_owner(struct ldlm_lock *lock, struct ldlm_lock *new)
{
	return((new->l_policy_data.l_flock.owner ==
		lock->l_policy_data.l_flock.owner) &&
	       (new->l_export == lock->l_export));
}

static inline int
ldlm_flocks_overlap(struct ldlm_lock *lock, struct ldlm_lock *new)
{
	return((new->l_policy_data.l_flock.start <=
		lock->l_policy_data.l_flock.end) &&
	       (new->l_policy_data.l_flock.end >=
		lock->l_policy_data.l_flock.start));
}

static inline void
ldlm_flock_destroy(struct ldlm_lock *lock, enum ldlm_mode mode, __u64 flags)
{
	LDLM_DEBUG(lock, "ldlm_flock_destroy(mode: %d, flags: 0x%llx)",
		   mode, flags);

	/* Safe to not lock here, since it should be empty anyway */
	LASSERT(hlist_unhashed(&lock->l_exp_flock_hash));

	list_del_init(&lock->l_res_link);
	if (flags == LDLM_FL_WAIT_NOREPROC) {
		/* client side - set a flag to prevent sending a CANCEL */
		lock->l_flags |= LDLM_FL_LOCAL_ONLY | LDLM_FL_CBPENDING;

		/* when reaching here, it is under lock_res_and_lock(). Thus,
		 * need call the nolock version of ldlm_lock_decref_internal
		 */
		ldlm_lock_decref_internal_nolock(lock, mode);
	}

	ldlm_lock_destroy_nolock(lock);
}

/**
 * Process a granting attempt for flock lock.
 * Must be called under ns lock held.
 *
 * This function looks for any conflicts for \a lock in the granted or
 * waiting queues. The lock is granted if no conflicts are found in
 * either queue.
 *
 * It is also responsible for splitting a lock if a portion of the lock
 * is released.
 *
 * If \a first_enq is 0 (ie, called from ldlm_reprocess_queue):
 *   - blocking ASTs have already been sent
 *
 * If \a first_enq is 1 (ie, called from ldlm_lock_enqueue):
 *   - blocking ASTs have not been sent yet, so list of conflicting locks
 *     would be collected and ASTs sent.
 */
static int ldlm_process_flock_lock(struct ldlm_lock *req, __u64 *flags,
				   int first_enq, enum ldlm_error *err,
				   struct list_head *work_list)
{
	struct ldlm_resource *res = req->l_resource;
	struct ldlm_namespace *ns = ldlm_res_to_ns(res);
	struct list_head *tmp;
	struct list_head *ownlocks = NULL;
	struct ldlm_lock *lock = NULL;
	struct ldlm_lock *new = req;
	struct ldlm_lock *new2 = NULL;
	enum ldlm_mode mode = req->l_req_mode;
	int added = (mode == LCK_NL);
	int overlaps = 0;
	int splitted = 0;
	const struct ldlm_callback_suite null_cbs = { NULL };

	CDEBUG(D_DLMTRACE,
	       "flags %#llx owner %llu pid %u mode %u start %llu end %llu\n",
	       *flags, new->l_policy_data.l_flock.owner,
	       new->l_policy_data.l_flock.pid, mode,
	       req->l_policy_data.l_flock.start,
	       req->l_policy_data.l_flock.end);

	*err = ELDLM_OK;

	/* No blocking ASTs are sent to the clients for
	 * Posix file & record locks
	 */
	req->l_blocking_ast = NULL;

reprocess:
	if ((*flags == LDLM_FL_WAIT_NOREPROC) || (mode == LCK_NL)) {
		/* This loop determines where this processes locks start
		 * in the resource lr_granted list.
		 */
		list_for_each(tmp, &res->lr_granted) {
			lock = list_entry(tmp, struct ldlm_lock,
					  l_res_link);
			if (ldlm_same_flock_owner(lock, req)) {
				ownlocks = tmp;
				break;
			}
		}
	} else {
		int reprocess_failed = 0;

		lockmode_verify(mode);

		/* This loop determines if there are existing locks
		 * that conflict with the new lock request.
		 */
		list_for_each(tmp, &res->lr_granted) {
			lock = list_entry(tmp, struct ldlm_lock,
					  l_res_link);

			if (ldlm_same_flock_owner(lock, req)) {
				if (!ownlocks)
					ownlocks = tmp;
				continue;
			}

			/* locks are compatible, overlap doesn't matter */
			if (lockmode_compat(lock->l_granted_mode, mode))
				continue;

			if (!ldlm_flocks_overlap(lock, req))
				continue;

			if (!first_enq) {
				reprocess_failed = 1;
				continue;
			}

			if (*flags & LDLM_FL_BLOCK_NOWAIT) {
				ldlm_flock_destroy(req, mode, *flags);
				*err = -EAGAIN;
				return LDLM_ITER_STOP;
			}

			if (*flags & LDLM_FL_TEST_LOCK) {
				ldlm_flock_destroy(req, mode, *flags);
				req->l_req_mode = lock->l_granted_mode;
				req->l_policy_data.l_flock.pid =
					lock->l_policy_data.l_flock.pid;
				req->l_policy_data.l_flock.start =
					lock->l_policy_data.l_flock.start;
				req->l_policy_data.l_flock.end =
					lock->l_policy_data.l_flock.end;
				*flags |= LDLM_FL_LOCK_CHANGED;
				return LDLM_ITER_STOP;
			}

			ldlm_resource_add_lock(res, &res->lr_waiting, req);
			*flags |= LDLM_FL_BLOCK_GRANTED;
			return LDLM_ITER_STOP;
		}
		if (reprocess_failed)
			return LDLM_ITER_CONTINUE;
	}

	if (*flags & LDLM_FL_TEST_LOCK) {
		ldlm_flock_destroy(req, mode, *flags);
		req->l_req_mode = LCK_NL;
		*flags |= LDLM_FL_LOCK_CHANGED;
		return LDLM_ITER_STOP;
	}

	/* Scan the locks owned by this process that overlap this request.
	 * We may have to merge or split existing locks.
	 */
	if (!ownlocks)
		ownlocks = &res->lr_granted;

	list_for_remaining_safe(ownlocks, tmp, &res->lr_granted) {
		lock = list_entry(ownlocks, struct ldlm_lock, l_res_link);

		if (!ldlm_same_flock_owner(lock, new))
			break;

		if (lock->l_granted_mode == mode) {
			/* If the modes are the same then we need to process
			 * locks that overlap OR adjoin the new lock. The extra
			 * logic condition is necessary to deal with arithmetic
			 * overflow and underflow.
			 */
			if ((new->l_policy_data.l_flock.start >
			     (lock->l_policy_data.l_flock.end + 1)) &&
			    (lock->l_policy_data.l_flock.end != OBD_OBJECT_EOF))
				continue;

			if ((new->l_policy_data.l_flock.end <
			     (lock->l_policy_data.l_flock.start - 1)) &&
			    (lock->l_policy_data.l_flock.start != 0))
				break;

			if (new->l_policy_data.l_flock.start <
			    lock->l_policy_data.l_flock.start) {
				lock->l_policy_data.l_flock.start =
					new->l_policy_data.l_flock.start;
			} else {
				new->l_policy_data.l_flock.start =
					lock->l_policy_data.l_flock.start;
			}

			if (new->l_policy_data.l_flock.end >
			    lock->l_policy_data.l_flock.end) {
				lock->l_policy_data.l_flock.end =
					new->l_policy_data.l_flock.end;
			} else {
				new->l_policy_data.l_flock.end =
					lock->l_policy_data.l_flock.end;
			}

			if (added) {
				ldlm_flock_destroy(lock, mode, *flags);
			} else {
				new = lock;
				added = 1;
			}
			continue;
		}

		if (new->l_policy_data.l_flock.start >
		    lock->l_policy_data.l_flock.end)
			continue;

		if (new->l_policy_data.l_flock.end <
		    lock->l_policy_data.l_flock.start)
			break;

		++overlaps;

		if (new->l_policy_data.l_flock.start <=
		    lock->l_policy_data.l_flock.start) {
			if (new->l_policy_data.l_flock.end <
			    lock->l_policy_data.l_flock.end) {
				lock->l_policy_data.l_flock.start =
					new->l_policy_data.l_flock.end + 1;
				break;
			}
			ldlm_flock_destroy(lock, lock->l_req_mode, *flags);
			continue;
		}
		if (new->l_policy_data.l_flock.end >=
		    lock->l_policy_data.l_flock.end) {
			lock->l_policy_data.l_flock.end =
				new->l_policy_data.l_flock.start - 1;
			continue;
		}

		/* split the existing lock into two locks */

		/* if this is an F_UNLCK operation then we could avoid
		 * allocating a new lock and use the req lock passed in
		 * with the request but this would complicate the reply
		 * processing since updates to req get reflected in the
		 * reply. The client side replays the lock request so
		 * it must see the original lock data in the reply.
		 */

		/* XXX - if ldlm_lock_new() can sleep we should
		 * release the lr_lock, allocate the new lock,
		 * and restart processing this lock.
		 */
		if (!new2) {
			unlock_res_and_lock(req);
			new2 = ldlm_lock_create(ns, &res->lr_name, LDLM_FLOCK,
						lock->l_granted_mode, &null_cbs,
						NULL, 0, LVB_T_NONE);
			lock_res_and_lock(req);
			if (IS_ERR(new2)) {
				ldlm_flock_destroy(req, lock->l_granted_mode,
						   *flags);
				*err = PTR_ERR(new2);
				return LDLM_ITER_STOP;
			}
			goto reprocess;
		}

		splitted = 1;

		new2->l_granted_mode = lock->l_granted_mode;
		new2->l_policy_data.l_flock.pid =
			new->l_policy_data.l_flock.pid;
		new2->l_policy_data.l_flock.owner =
			new->l_policy_data.l_flock.owner;
		new2->l_policy_data.l_flock.start =
			lock->l_policy_data.l_flock.start;
		new2->l_policy_data.l_flock.end =
			new->l_policy_data.l_flock.start - 1;
		lock->l_policy_data.l_flock.start =
			new->l_policy_data.l_flock.end + 1;
		new2->l_conn_export = lock->l_conn_export;
		if (lock->l_export) {
			new2->l_export = class_export_lock_get(lock->l_export,
							       new2);
			if (new2->l_export->exp_lock_hash &&
			    hlist_unhashed(&new2->l_exp_hash))
				cfs_hash_add(new2->l_export->exp_lock_hash,
					     &new2->l_remote_handle,
					     &new2->l_exp_hash);
		}
		if (*flags == LDLM_FL_WAIT_NOREPROC)
			ldlm_lock_addref_internal_nolock(new2,
							 lock->l_granted_mode);

		/* insert new2 at lock */
		ldlm_resource_add_lock(res, ownlocks, new2);
		LDLM_LOCK_RELEASE(new2);
		break;
	}

	/* if new2 is created but never used, destroy it*/
	if (splitted == 0 && new2)
		ldlm_lock_destroy_nolock(new2);

	/* At this point we're granting the lock request. */
	req->l_granted_mode = req->l_req_mode;

	if (!added) {
		list_del_init(&req->l_res_link);
		/* insert new lock before ownlocks in list. */
		ldlm_resource_add_lock(res, ownlocks, req);
	}

	if (*flags != LDLM_FL_WAIT_NOREPROC) {
		/* The only one possible case for client-side calls flock
		 * policy function is ldlm_flock_completion_ast inside which
		 * carries LDLM_FL_WAIT_NOREPROC flag.
		 */
		CERROR("Illegal parameter for client-side-only module.\n");
		LBUG();
	}

	/* In case we're reprocessing the requested lock we can't destroy
	 * it until after calling ldlm_add_ast_work_item() above so that laawi()
	 * can bump the reference count on \a req. Otherwise \a req
	 * could be freed before the completion AST can be sent.
	 */
	if (added)
		ldlm_flock_destroy(req, mode, *flags);

	ldlm_resource_dump(D_INFO, res);
	return LDLM_ITER_CONTINUE;
}

struct ldlm_flock_wait_data {
	struct ldlm_lock *fwd_lock;
	int	       fwd_generation;
};

static void
ldlm_flock_interrupted_wait(void *data)
{
	struct ldlm_lock *lock;

	lock = ((struct ldlm_flock_wait_data *)data)->fwd_lock;

	lock_res_and_lock(lock);

	/* client side - set flag to prevent lock from being put on LRU list */
	ldlm_set_cbpending(lock);
	unlock_res_and_lock(lock);
}

/**
 * Flock completion callback function.
 *
 * \param lock [in,out]: A lock to be handled
 * \param flags    [in]: flags
 * \param *data    [in]: ldlm_work_cp_ast_lock() will use ldlm_cb_set_arg
 *
 * \retval 0    : success
 * \retval <0   : failure
 */
int
ldlm_flock_completion_ast(struct ldlm_lock *lock, __u64 flags, void *data)
{
	struct file_lock		*getlk = lock->l_ast_data;
	struct obd_device	      *obd;
	struct obd_import	      *imp = NULL;
	struct ldlm_flock_wait_data     fwd;
	struct l_wait_info	      lwi;
	enum ldlm_error		    err;
	int			     rc = 0;

	OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_CP_CB_WAIT2, 4);
	if (OBD_FAIL_PRECHECK(OBD_FAIL_LDLM_CP_CB_WAIT3)) {
		lock_res_and_lock(lock);
		lock->l_flags |= LDLM_FL_FAIL_LOC;
		unlock_res_and_lock(lock);
		OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_CP_CB_WAIT3, 4);
	}
	CDEBUG(D_DLMTRACE, "flags: 0x%llx data: %p getlk: %p\n",
	       flags, data, getlk);

	LASSERT(flags != LDLM_FL_WAIT_NOREPROC);

	if (flags & LDLM_FL_FAILED)
		goto granted;

	if (!(flags & LDLM_FL_BLOCKED_MASK)) {
		if (!data)
			/* mds granted the lock in the reply */
			goto granted;
		/* CP AST RPC: lock get granted, wake it up */
		wake_up(&lock->l_waitq);
		return 0;
	}

	LDLM_DEBUG(lock, "client-side enqueue returned a blocked lock, sleeping");
	fwd.fwd_lock = lock;
	obd = class_exp2obd(lock->l_conn_export);

	/* if this is a local lock, there is no import */
	if (obd)
		imp = obd->u.cli.cl_import;

	if (imp) {
		spin_lock(&imp->imp_lock);
		fwd.fwd_generation = imp->imp_generation;
		spin_unlock(&imp->imp_lock);
	}

	lwi = LWI_TIMEOUT_INTR(0, NULL, ldlm_flock_interrupted_wait, &fwd);

	/* Go to sleep until the lock is granted. */
	rc = l_wait_event(lock->l_waitq, is_granted_or_cancelled(lock), &lwi);

	if (rc) {
		LDLM_DEBUG(lock, "client-side enqueue waking up: failed (%d)",
			   rc);
		return rc;
	}

granted:
	OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_CP_CB_WAIT, 10);

	if (OBD_FAIL_PRECHECK(OBD_FAIL_LDLM_CP_CB_WAIT4)) {
		lock_res_and_lock(lock);
		/* DEADLOCK is always set with CBPENDING */
		lock->l_flags |= LDLM_FL_FLOCK_DEADLOCK | LDLM_FL_CBPENDING;
		unlock_res_and_lock(lock);
		OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_CP_CB_WAIT4, 4);
	}
	if (OBD_FAIL_PRECHECK(OBD_FAIL_LDLM_CP_CB_WAIT5)) {
		lock_res_and_lock(lock);
		/* DEADLOCK is always set with CBPENDING */
		lock->l_flags |= LDLM_FL_FAIL_LOC |
				 LDLM_FL_FLOCK_DEADLOCK | LDLM_FL_CBPENDING;
		unlock_res_and_lock(lock);
		OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_CP_CB_WAIT5, 4);
	}

	lock_res_and_lock(lock);

	/*
	 * Protect against race where lock could have been just destroyed
	 * due to overlap in ldlm_process_flock_lock().
	 */
	if (ldlm_is_destroyed(lock)) {
		unlock_res_and_lock(lock);
		LDLM_DEBUG(lock, "client-side enqueue waking up: destroyed");
		/*
		 * An error is still to be returned, to propagate it up to
		 * ldlm_cli_enqueue_fini() caller.
		 */
		return -EIO;
	}

	/* ldlm_lock_enqueue() has already placed lock on the granted list. */
	ldlm_resource_unlink_lock(lock);

	/*
	 * Import invalidation. We need to actually release the lock
	 * references being held, so that it can go away. No point in
	 * holding the lock even if app still believes it has it, since
	 * server already dropped it anyway. Only for granted locks too.
	 */
	/* Do the same for DEADLOCK'ed locks. */
	if (ldlm_is_failed(lock) || ldlm_is_flock_deadlock(lock)) {
		int mode;

		if (flags & LDLM_FL_TEST_LOCK)
			LASSERT(ldlm_is_test_lock(lock));

		if (ldlm_is_test_lock(lock) || ldlm_is_flock_deadlock(lock))
			mode = getlk->fl_type;
		else
			mode = lock->l_granted_mode;

		if (ldlm_is_flock_deadlock(lock)) {
			LDLM_DEBUG(lock, "client-side enqueue deadlock received");
			rc = -EDEADLK;
		}
		ldlm_flock_destroy(lock, mode, LDLM_FL_WAIT_NOREPROC);
		unlock_res_and_lock(lock);

		/* Need to wake up the waiter if we were evicted */
		wake_up(&lock->l_waitq);

		/*
		 * An error is still to be returned, to propagate it up to
		 * ldlm_cli_enqueue_fini() caller.
		 */
		return rc ? : -EIO;
	}

	LDLM_DEBUG(lock, "client-side enqueue granted");

	if (flags & LDLM_FL_TEST_LOCK) {
		/* fcntl(F_GETLK) request */
		/* The old mode was saved in getlk->fl_type so that if the mode
		 * in the lock changes we can decref the appropriate refcount.
		 */
		LASSERT(ldlm_is_test_lock(lock));
		ldlm_flock_destroy(lock, getlk->fl_type, LDLM_FL_WAIT_NOREPROC);
		switch (lock->l_granted_mode) {
		case LCK_PR:
			getlk->fl_type = F_RDLCK;
			break;
		case LCK_PW:
			getlk->fl_type = F_WRLCK;
			break;
		default:
			getlk->fl_type = F_UNLCK;
		}
		getlk->fl_pid = (pid_t)lock->l_policy_data.l_flock.pid;
		getlk->fl_start = (loff_t)lock->l_policy_data.l_flock.start;
		getlk->fl_end = (loff_t)lock->l_policy_data.l_flock.end;
	} else {
		__u64 noreproc = LDLM_FL_WAIT_NOREPROC;

		/* We need to reprocess the lock to do merges or splits
		 * with existing locks owned by this process.
		 */
		ldlm_process_flock_lock(lock, &noreproc, 1, &err, NULL);
	}
	unlock_res_and_lock(lock);
	return rc;
}
EXPORT_SYMBOL(ldlm_flock_completion_ast);

void ldlm_flock_policy_wire18_to_local(const ldlm_wire_policy_data_t *wpolicy,
				       ldlm_policy_data_t *lpolicy)
{
	memset(lpolicy, 0, sizeof(*lpolicy));
	lpolicy->l_flock.start = wpolicy->l_flock.lfw_start;
	lpolicy->l_flock.end = wpolicy->l_flock.lfw_end;
	lpolicy->l_flock.pid = wpolicy->l_flock.lfw_pid;
	/* Compat code, old clients had no idea about owner field and
	 * relied solely on pid for ownership. Introduced in LU-104, 2.1,
	 * April 2011
	 */
	lpolicy->l_flock.owner = wpolicy->l_flock.lfw_pid;
}

void ldlm_flock_policy_wire21_to_local(const ldlm_wire_policy_data_t *wpolicy,
				       ldlm_policy_data_t *lpolicy)
{
	memset(lpolicy, 0, sizeof(*lpolicy));
	lpolicy->l_flock.start = wpolicy->l_flock.lfw_start;
	lpolicy->l_flock.end = wpolicy->l_flock.lfw_end;
	lpolicy->l_flock.pid = wpolicy->l_flock.lfw_pid;
	lpolicy->l_flock.owner = wpolicy->l_flock.lfw_owner;
}

void ldlm_flock_policy_local_to_wire(const ldlm_policy_data_t *lpolicy,
				     ldlm_wire_policy_data_t *wpolicy)
{
	memset(wpolicy, 0, sizeof(*wpolicy));
	wpolicy->l_flock.lfw_start = lpolicy->l_flock.start;
	wpolicy->l_flock.lfw_end = lpolicy->l_flock.end;
	wpolicy->l_flock.lfw_pid = lpolicy->l_flock.pid;
	wpolicy->l_flock.lfw_owner = lpolicy->l_flock.owner;
}
