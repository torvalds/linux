// SPDX-License-Identifier: GPL-2.0
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
 *   Author: Jinshan Xiong <jinshan.xiong@intel.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <obd_class.h>
#include <obd_support.h>
#include <lustre_fid.h>
#include <linux/list.h>
#include <cl_object.h>
#include "cl_internal.h"

static void cl_lock_trace0(int level, const struct lu_env *env,
			   const char *prefix, const struct cl_lock *lock,
			   const char *func, const int line)
{
	struct cl_object_header *h = cl_object_header(lock->cll_descr.cld_obj);

	CDEBUG(level, "%s: %p (%p/%d) at %s():%d\n",
	       prefix, lock, env, h->coh_nesting, func, line);
}
#define cl_lock_trace(level, env, prefix, lock)				\
	cl_lock_trace0(level, env, prefix, lock, __func__, __LINE__)

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

void cl_lock_fini(const struct lu_env *env, struct cl_lock *lock)
{
	cl_lock_trace(D_DLMTRACE, env, "destroy lock", lock);

	while (!list_empty(&lock->cll_layers)) {
		struct cl_lock_slice *slice;

		slice = list_entry(lock->cll_layers.next,
				   struct cl_lock_slice, cls_linkage);
		list_del_init(lock->cll_layers.next);
		slice->cls_ops->clo_fini(env, slice);
	}
	POISON(lock, 0x5a, sizeof(*lock));
}
EXPORT_SYMBOL(cl_lock_fini);

int cl_lock_init(const struct lu_env *env, struct cl_lock *lock,
		 const struct cl_io *io)
{
	struct cl_object *obj = lock->cll_descr.cld_obj;
	struct cl_object *scan;
	int result = 0;

	/* Make sure cl_lock::cll_descr is initialized. */
	LASSERT(obj);

	INIT_LIST_HEAD(&lock->cll_layers);
	list_for_each_entry(scan, &obj->co_lu.lo_header->loh_layers,
			    co_lu.lo_linkage) {
		result = scan->co_ops->coo_lock_init(env, scan, lock, io);
		if (result != 0) {
			cl_lock_fini(env, lock);
			break;
		}
	}

	return result;
}
EXPORT_SYMBOL(cl_lock_init);

/**
 * Returns a slice with a lock, corresponding to the given layer in the
 * device stack.
 *
 * \see cl_page_at()
 */
const struct cl_lock_slice *cl_lock_at(const struct cl_lock *lock,
				       const struct lu_device_type *dtype)
{
	const struct cl_lock_slice *slice;

	list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
		if (slice->cls_obj->co_lu.lo_dev->ld_type == dtype)
			return slice;
	}
	return NULL;
}
EXPORT_SYMBOL(cl_lock_at);

void cl_lock_cancel(const struct lu_env *env, struct cl_lock *lock)
{
	const struct cl_lock_slice *slice;

	cl_lock_trace(D_DLMTRACE, env, "cancel lock", lock);
	list_for_each_entry_reverse(slice, &lock->cll_layers, cls_linkage) {
		if (slice->cls_ops->clo_cancel)
			slice->cls_ops->clo_cancel(env, slice);
	}
}
EXPORT_SYMBOL(cl_lock_cancel);

/**
 * Enqueue a lock.
 * \param anchor: if we need to wait for resources before getting the lock,
 *		  use @anchor for the purpose.
 * \retval 0  enqueue successfully
 * \retval <0 error code
 */
int cl_lock_enqueue(const struct lu_env *env, struct cl_io *io,
		    struct cl_lock *lock, struct cl_sync_io *anchor)
{
	const struct cl_lock_slice *slice;
	int rc = -ENOSYS;

	list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
		if (!slice->cls_ops->clo_enqueue)
			continue;

		rc = slice->cls_ops->clo_enqueue(env, slice, io, anchor);
		if (rc != 0)
			break;
		}
	return rc;
}
EXPORT_SYMBOL(cl_lock_enqueue);

/**
 * Main high-level entry point of cl_lock interface that finds existing or
 * enqueues new lock matching given description.
 */
int cl_lock_request(const struct lu_env *env, struct cl_io *io,
		    struct cl_lock *lock)
{
	struct cl_sync_io *anchor = NULL;
	__u32 enq_flags = lock->cll_descr.cld_enq_flags;
	int rc;

	rc = cl_lock_init(env, lock, io);
	if (rc < 0)
		return rc;

	if ((enq_flags & CEF_ASYNC) && !(enq_flags & CEF_AGL)) {
		anchor = &cl_env_info(env)->clt_anchor;
		cl_sync_io_init(anchor, 1, cl_sync_io_end);
	}

	rc = cl_lock_enqueue(env, io, lock, anchor);

	if (anchor) {
		int rc2;

		/* drop the reference count held at initialization time */
		cl_sync_io_note(env, anchor, 0);
		rc2 = cl_sync_io_wait(env, anchor, 0);
		if (rc2 < 0 && rc == 0)
			rc = rc2;
	}

	if (rc < 0)
		cl_lock_release(env, lock);

	return rc;
}
EXPORT_SYMBOL(cl_lock_request);

/**
 * Releases a hold and a reference on a lock, obtained by cl_lock_hold().
 */
void cl_lock_release(const struct lu_env *env, struct cl_lock *lock)
{
	cl_lock_trace(D_DLMTRACE, env, "release lock", lock);
	cl_lock_cancel(env, lock);
	cl_lock_fini(env, lock);
}
EXPORT_SYMBOL(cl_lock_release);

const char *cl_lock_mode_name(const enum cl_lock_mode mode)
{
	static const char *names[] = {
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
	(*printer)(env, cookie, DDESCR "@" DFID, PDESCR(descr), PFID(fid));
}
EXPORT_SYMBOL(cl_lock_descr_print);

/**
 * Prints human readable representation of \a lock to the \a f.
 */
void cl_lock_print(const struct lu_env *env, void *cookie,
		   lu_printer_t printer, const struct cl_lock *lock)
{
	const struct cl_lock_slice *slice;

	(*printer)(env, cookie, "lock@%p", lock);
	cl_lock_descr_print(env, cookie, printer, &lock->cll_descr);
	(*printer)(env, cookie, " {\n");

	list_for_each_entry(slice, &lock->cll_layers, cls_linkage) {
		(*printer)(env, cookie, "    %s@%p: ",
			   slice->cls_obj->co_lu.lo_dev->ld_type->ldt_name,
			   slice);
		if (slice->cls_ops->clo_print)
			slice->cls_ops->clo_print(env, cookie, printer, slice);
		(*printer)(env, cookie, "\n");
	}
	(*printer)(env, cookie, "} lock@%p\n", lock);
}
EXPORT_SYMBOL(cl_lock_print);
