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
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Internal interfaces of OSC layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Jinshan Xiong <jinshan.xiong@whamcloud.com>
 */

#ifndef OSC_CL_INTERNAL_H
#define OSC_CL_INTERNAL_H

#include <linux/libcfs/libcfs.h>

#include <obd.h>
/* osc_build_res_name() */
#include <cl_object.h>
#include "osc_internal.h"

/** \defgroup osc osc
 *  @{
 */

struct osc_extent;

/**
 * State maintained by osc layer for each IO context.
 */
struct osc_io {
	/** super class */
	struct cl_io_slice oi_cl;
	/** true if this io is lockless. */
	unsigned int		oi_lockless:1,
	/** true if this io is counted as active IO */
				oi_is_active:1;
	/** how many LRU pages are reserved for this IO */
	unsigned long		oi_lru_reserved;

	/** active extents, we know how many bytes is going to be written,
	 * so having an active extent will prevent it from being fragmented
	 */
	struct osc_extent *oi_active;
	/** partially truncated extent, we need to hold this extent to prevent
	 * page writeback from happening.
	 */
	struct osc_extent *oi_trunc;

	/** write osc_lock for this IO, used by osc_extent_find(). */
	struct osc_lock   *oi_write_osclock;
	struct obdo	oi_oa;
	struct osc_async_cbargs {
		bool		  opc_rpc_sent;
		int	       opc_rc;
		struct completion	opc_sync;
	} oi_cbarg;
};

/**
 * State maintained by osc layer for the duration of a system call.
 */
struct osc_session {
	struct osc_io       os_io;
};

#define OTI_PVEC_SIZE 256
struct osc_thread_info {
	struct ldlm_res_id      oti_resname;
	union ldlm_policy_data	oti_policy;
	struct cl_lock_descr    oti_descr;
	struct cl_attr	  oti_attr;
	struct lustre_handle    oti_handle;
	struct cl_page_list     oti_plist;
	struct cl_io		oti_io;
	void			*oti_pvec[OTI_PVEC_SIZE];
	/**
	 * Fields used by cl_lock_discard_pages().
	 */
	pgoff_t			oti_next_index;
	pgoff_t			oti_fn_index; /* first non-overlapped index */
	struct cl_sync_io	oti_anchor;
	struct cl_req_attr	oti_req_attr;
};

struct osc_object {
	struct cl_object   oo_cl;
	struct lov_oinfo  *oo_oinfo;
	/**
	 * True if locking against this stripe got -EUSERS.
	 */
	int		oo_contended;
	unsigned long	 oo_contention_time;
	/**
	 * used by the osc to keep track of what objects to build into rpcs.
	 * Protected by client_obd->cli_loi_list_lock.
	 */
	struct list_head	   oo_ready_item;
	struct list_head	   oo_hp_ready_item;
	struct list_head	   oo_write_item;
	struct list_head	   oo_read_item;

	/**
	 * extent is a red black tree to manage (async) dirty pages.
	 */
	struct rb_root       oo_root;
	/**
	 * Manage write(dirty) extents.
	 */
	struct list_head	   oo_hp_exts; /* list of hp extents */
	struct list_head	   oo_urgent_exts; /* list of writeback extents */
	struct list_head	   oo_rpc_exts;

	struct list_head	   oo_reading_exts;

	atomic_t	 oo_nr_reads;
	atomic_t	 oo_nr_writes;

	/** Protect extent tree. Will be used to protect
	 * oo_{read|write}_pages soon.
	 */
	spinlock_t	    oo_lock;

	/**
	 * Radix tree for caching pages
	 */
	struct radix_tree_root	oo_tree;
	spinlock_t		oo_tree_lock;
	unsigned long		oo_npages;

	/* Protect osc_lock this osc_object has */
	spinlock_t		oo_ol_spin;
	struct list_head	oo_ol_list;

	/** number of active IOs of this object */
	atomic_t		oo_nr_ios;
	wait_queue_head_t	oo_io_waitq;
};

static inline void osc_object_lock(struct osc_object *obj)
{
	spin_lock(&obj->oo_lock);
}

static inline int osc_object_trylock(struct osc_object *obj)
{
	return spin_trylock(&obj->oo_lock);
}

static inline void osc_object_unlock(struct osc_object *obj)
{
	spin_unlock(&obj->oo_lock);
}

static inline int osc_object_is_locked(struct osc_object *obj)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	return spin_is_locked(&obj->oo_lock);
#else
	/*
	 * It is not perfect to return true all the time.
	 * But since this function is only used for assertion
	 * and checking, it seems OK.
	 */
	return 1;
#endif
}

/*
 * Lock "micro-states" for osc layer.
 */
enum osc_lock_state {
	OLS_NEW,
	OLS_ENQUEUED,
	OLS_UPCALL_RECEIVED,
	OLS_GRANTED,
	OLS_CANCELLED
};

/**
 * osc-private state of cl_lock.
 *
 * Interaction with DLM.
 *
 * Once receive upcall is invoked, osc_lock remembers a handle of DLM lock in
 * osc_lock::ols_handle and a pointer to that lock in osc_lock::ols_dlmlock.
 *
 * This pointer is protected through a reference, acquired by
 * osc_lock_upcall0(). Also, an additional reference is acquired by
 * ldlm_lock_addref() call protecting the lock from cancellation, until
 * osc_lock_unuse() releases it.
 *
 * Below is a description of how lock references are acquired and released
 * inside of DLM.
 *
 * - When new lock is created and enqueued to the server (ldlm_cli_enqueue())
 *      - ldlm_lock_create()
 *	  - ldlm_lock_new(): initializes a lock with 2 references. One for
 *	    the caller (released when reply from the server is received, or on
 *	    error), and another for the hash table.
 *      - ldlm_lock_addref_internal(): protects the lock from cancellation.
 *
 * - When reply is received from the server (osc_enqueue_interpret())
 *      - ldlm_cli_enqueue_fini()
 *	  - LDLM_LOCK_PUT(): releases caller reference acquired by
 *	    ldlm_lock_new().
 *	  - if (rc != 0)
 *		ldlm_lock_decref(): error case: matches ldlm_cli_enqueue().
 *      - ldlm_lock_decref(): for async locks, matches ldlm_cli_enqueue().
 *
 * - When lock is being cancelled (ldlm_lock_cancel())
 *      - ldlm_lock_destroy()
 *	  - LDLM_LOCK_PUT(): releases hash-table reference acquired by
 *	    ldlm_lock_new().
 *
 * osc_lock is detached from ldlm_lock by osc_lock_detach() that is called
 * either when lock is cancelled (osc_lock_blocking()), or when locks is
 * deleted without cancellation (e.g., from cl_locks_prune()). In the latter
 * case ldlm lock remains in memory, and can be re-attached to osc_lock in the
 * future.
 */
struct osc_lock {
	struct cl_lock_slice     ols_cl;
	/** Internal lock to protect states, etc. */
	spinlock_t		ols_lock;
	/** Owner sleeps on this channel for state change */
	struct cl_sync_io	*ols_owner;
	/** waiting list for this lock to be cancelled */
	struct list_head	ols_waiting_list;
	/** wait entry of ols_waiting_list */
	struct list_head	ols_wait_entry;
	/** list entry for osc_object::oo_ol_list */
	struct list_head	ols_nextlock_oscobj;

	/** underlying DLM lock */
	struct ldlm_lock	*ols_dlmlock;
	/** DLM flags with which osc_lock::ols_lock was enqueued */
	__u64		    ols_flags;
	/** osc_lock::ols_lock handle */
	struct lustre_handle     ols_handle;
	struct ldlm_enqueue_info ols_einfo;
	enum osc_lock_state      ols_state;
	/** lock value block */
	struct ost_lvb		ols_lvb;

	/**
	 * true, if ldlm_lock_addref() was called against
	 * osc_lock::ols_lock. This is used for sanity checking.
	 *
	 * \see osc_lock::ols_has_ref
	 */
	unsigned		  ols_hold :1,
	/**
	 * this is much like osc_lock::ols_hold, except that this bit is
	 * cleared _after_ reference in released in osc_lock_unuse(). This
	 * fine distinction is needed because:
	 *
	 *     - if ldlm lock still has a reference, osc_ast_data_get() needs
	 *       to return associated cl_lock (so that a flag is needed that is
	 *       cleared after ldlm_lock_decref() returned), and
	 *
	 *     - ldlm_lock_decref() can invoke blocking ast (for a
	 *       LDLM_FL_CBPENDING lock), and osc_lock functions like
	 *       osc_lock_cancel() called from there need to know whether to
	 *       release lock reference (so that a flag is needed that is
	 *       cleared before ldlm_lock_decref() is called).
	 */
				 ols_has_ref:1,
	/**
	 * inherit the lockless attribute from top level cl_io.
	 * If true, osc_lock_enqueue is able to tolerate the -EUSERS error.
	 */
				 ols_locklessable:1,
	/**
	 * if set, the osc_lock is a glimpse lock. For glimpse locks, we treat
	 * the EVAVAIL error as tolerable, this will make upper logic happy
	 * to wait all glimpse locks to each OSTs to be completed.
	 * Glimpse lock converts to normal lock if the server lock is
	 * granted.
	 * Glimpse lock should be destroyed immediately after use.
	 */
				 ols_glimpse:1,
	/**
	 * For async glimpse lock.
	 */
				 ols_agl:1;
};

/**
 * Page state private for osc layer.
 */
struct osc_page {
	struct cl_page_slice  ops_cl;
	/**
	 * Page queues used by osc to detect when RPC can be formed.
	 */
	struct osc_async_page ops_oap;
	/**
	 * An offset within page from which next transfer starts. This is used
	 * by cl_page_clip() to submit partial page transfers.
	 */
	int		   ops_from;
	/**
	 * An offset within page at which next transfer ends.
	 *
	 * \see osc_page::ops_from.
	 */
	int		   ops_to;
	/**
	 * Boolean, true iff page is under transfer. Used for sanity checking.
	 */
	unsigned	      ops_transfer_pinned:1,
	/**
	 * in LRU?
	 */
			      ops_in_lru:1,
	/**
	 * Set if the page must be transferred with OBD_BRW_SRVLOCK.
	 */
			      ops_srvlock:1;
	/**
	 * lru page list. See osc_lru_{del|use}() in osc_page.c for usage.
	 */
	struct list_head	      ops_lru;
	/**
	 * Submit time - the time when the page is starting RPC. For debugging.
	 */
	unsigned long	    ops_submit_time;
};

extern struct kmem_cache *osc_lock_kmem;
extern struct kmem_cache *osc_object_kmem;
extern struct kmem_cache *osc_thread_kmem;
extern struct kmem_cache *osc_session_kmem;
extern struct kmem_cache *osc_extent_kmem;

extern struct lu_device_type osc_device_type;
extern struct lu_context_key osc_key;
extern struct lu_context_key osc_session_key;

#define OSC_FLAGS (ASYNC_URGENT | ASYNC_READY)

int osc_lock_init(const struct lu_env *env,
		  struct cl_object *obj, struct cl_lock *lock,
		  const struct cl_io *io);
int osc_io_init(const struct lu_env *env,
		struct cl_object *obj, struct cl_io *io);
struct lu_object *osc_object_alloc(const struct lu_env *env,
				   const struct lu_object_header *hdr,
				   struct lu_device *dev);
int osc_page_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_page *page, pgoff_t ind);

void osc_index2policy(union ldlm_policy_data *policy,
		      const struct cl_object *obj,
		      pgoff_t start, pgoff_t end);
int osc_lvb_print(const struct lu_env *env, void *cookie,
		  lu_printer_t p, const struct ost_lvb *lvb);

void osc_lru_add_batch(struct client_obd *cli, struct list_head *list);
void osc_page_submit(const struct lu_env *env, struct osc_page *opg,
		     enum cl_req_type crt, int brw_flags);
int osc_cancel_async_page(const struct lu_env *env, struct osc_page *ops);
int osc_set_async_flags(struct osc_object *obj, struct osc_page *opg,
			u32 async_flags);
int osc_prep_async_page(struct osc_object *osc, struct osc_page *ops,
			struct page *page, loff_t offset);
int osc_queue_async_io(const struct lu_env *env, struct cl_io *io,
		       struct osc_page *ops);
int osc_page_cache_add(const struct lu_env *env,
		       const struct cl_page_slice *slice, struct cl_io *io);
int osc_teardown_async_page(const struct lu_env *env, struct osc_object *obj,
			    struct osc_page *ops);
int osc_flush_async_page(const struct lu_env *env, struct cl_io *io,
			 struct osc_page *ops);
int osc_queue_sync_pages(const struct lu_env *env, struct osc_object *obj,
			 struct list_head *list, int cmd, int brw_flags);
int osc_cache_truncate_start(const struct lu_env *env, struct osc_object *obj,
			     u64 size, struct osc_extent **extp);
void osc_cache_truncate_end(const struct lu_env *env, struct osc_extent *ext);
int osc_cache_writeback_range(const struct lu_env *env, struct osc_object *obj,
			      pgoff_t start, pgoff_t end, int hp, int discard);
int osc_cache_wait_range(const struct lu_env *env, struct osc_object *obj,
			 pgoff_t start, pgoff_t end);
void osc_io_unplug(const struct lu_env *env, struct client_obd *cli,
		   struct osc_object *osc);
int lru_queue_work(const struct lu_env *env, void *data);

void osc_object_set_contended(struct osc_object *obj);
void osc_object_clear_contended(struct osc_object *obj);
int  osc_object_is_contended(struct osc_object *obj);

int  osc_lock_is_lockless(const struct osc_lock *olck);

/*****************************************************************************
 *
 * Accessors.
 *
 */

static inline struct osc_thread_info *osc_env_info(const struct lu_env *env)
{
	struct osc_thread_info *info;

	info = lu_context_key_get(&env->le_ctx, &osc_key);
	LASSERT(info);
	return info;
}

static inline struct osc_session *osc_env_session(const struct lu_env *env)
{
	struct osc_session *ses;

	ses = lu_context_key_get(env->le_ses, &osc_session_key);
	LASSERT(ses);
	return ses;
}

static inline struct osc_io *osc_env_io(const struct lu_env *env)
{
	return &osc_env_session(env)->os_io;
}

static inline int osc_is_object(const struct lu_object *obj)
{
	return obj->lo_dev->ld_type == &osc_device_type;
}

static inline struct osc_device *lu2osc_dev(const struct lu_device *d)
{
	LINVRNT(d->ld_type == &osc_device_type);
	return container_of0(d, struct osc_device, od_cl.cd_lu_dev);
}

static inline struct obd_export *osc_export(const struct osc_object *obj)
{
	return lu2osc_dev(obj->oo_cl.co_lu.lo_dev)->od_exp;
}

static inline struct client_obd *osc_cli(const struct osc_object *obj)
{
	return &osc_export(obj)->exp_obd->u.cli;
}

static inline struct osc_object *cl2osc(const struct cl_object *obj)
{
	LINVRNT(osc_is_object(&obj->co_lu));
	return container_of0(obj, struct osc_object, oo_cl);
}

static inline struct cl_object *osc2cl(const struct osc_object *obj)
{
	return (struct cl_object *)&obj->oo_cl;
}

static inline enum ldlm_mode osc_cl_lock2ldlm(enum cl_lock_mode mode)
{
	LASSERT(mode == CLM_READ || mode == CLM_WRITE || mode == CLM_GROUP);
	if (mode == CLM_READ)
		return LCK_PR;
	else if (mode == CLM_WRITE)
		return LCK_PW;
	else
		return LCK_GROUP;
}

static inline enum cl_lock_mode osc_ldlm2cl_lock(enum ldlm_mode mode)
{
	LASSERT(mode == LCK_PR || mode == LCK_PW || mode == LCK_GROUP);
	if (mode == LCK_PR)
		return CLM_READ;
	else if (mode == LCK_PW)
		return CLM_WRITE;
	else
		return CLM_GROUP;
}

static inline struct osc_page *cl2osc_page(const struct cl_page_slice *slice)
{
	LINVRNT(osc_is_object(&slice->cpl_obj->co_lu));
	return container_of0(slice, struct osc_page, ops_cl);
}

static inline struct osc_page *oap2osc(struct osc_async_page *oap)
{
	return container_of0(oap, struct osc_page, ops_oap);
}

static inline pgoff_t osc_index(struct osc_page *opg)
{
	return opg->ops_cl.cpl_index;
}

static inline struct cl_page *oap2cl_page(struct osc_async_page *oap)
{
	return oap2osc(oap)->ops_cl.cpl_page;
}

static inline struct osc_page *oap2osc_page(struct osc_async_page *oap)
{
	return (struct osc_page *)container_of(oap, struct osc_page, ops_oap);
}

static inline struct osc_page *
osc_cl_page_osc(struct cl_page *page, struct osc_object *osc)
{
	const struct cl_page_slice *slice;

	LASSERT(osc);
	slice = cl_object_page_slice(&osc->oo_cl, page);
	return cl2osc_page(slice);
}

static inline struct osc_lock *cl2osc_lock(const struct cl_lock_slice *slice)
{
	LINVRNT(osc_is_object(&slice->cls_obj->co_lu));
	return container_of0(slice, struct osc_lock, ols_cl);
}

static inline struct osc_lock *osc_lock_at(const struct cl_lock *lock)
{
	return cl2osc_lock(cl_lock_at(lock, &osc_device_type));
}

static inline int osc_io_srvlock(struct osc_io *oio)
{
	return (oio->oi_lockless && !oio->oi_cl.cis_io->ci_no_srvlock);
}

enum osc_extent_state {
	OES_INV       = 0, /** extent is just initialized or destroyed */
	OES_ACTIVE    = 1, /** process is using this extent */
	OES_CACHE     = 2, /** extent is ready for IO */
	OES_LOCKING   = 3, /** locking page to prepare IO */
	OES_LOCK_DONE = 4, /** locking finished, ready to send */
	OES_RPC       = 5, /** in RPC */
	OES_TRUNC     = 6, /** being truncated */
	OES_STATE_MAX
};

/**
 * osc_extent data to manage dirty pages.
 * osc_extent has the following attributes:
 * 1. all pages in the same must be in one RPC in write back;
 * 2. # of pages must be less than max_pages_per_rpc - implied by 1;
 * 3. must be covered by only 1 osc_lock;
 * 4. exclusive. It's impossible to have overlapped osc_extent.
 *
 * The lifetime of an extent is from when the 1st page is dirtied to when
 * all pages inside it are written out.
 *
 * LOCKING ORDER
 * =============
 * page lock -> cl_loi_list_lock -> object lock(osc_object::oo_lock)
 */
struct osc_extent {
	/** red-black tree node */
	struct rb_node     oe_node;
	/** osc_object of this extent */
	struct osc_object *oe_obj;
	/** refcount, removed from red-black tree if reaches zero. */
	atomic_t       oe_refc;
	/** busy if non-zero */
	atomic_t       oe_users;
	/** link list of osc_object's oo_{hp|urgent|locking}_exts. */
	struct list_head	 oe_link;
	/** state of this extent */
	enum osc_extent_state	oe_state;
	/** flags for this extent. */
	unsigned int       oe_intree:1,
	/** 0 is write, 1 is read */
			   oe_rw:1,
	/** sync extent, queued by osc_queue_sync_pages() */
				oe_sync:1,
	/** set if this extent has partial, sync pages.
	 * Extents with partial page(s) can't merge with others in RPC
	 */
				oe_no_merge:1,
			   oe_srvlock:1,
			   oe_memalloc:1,
	/** an ACTIVE extent is going to be truncated, so when this extent
	 * is released, it will turn into TRUNC state instead of CACHE.
	 */
			   oe_trunc_pending:1,
	/** this extent should be written asap and someone may wait for the
	 * write to finish. This bit is usually set along with urgent if
	 * the extent was CACHE state.
	 * fsync_wait extent can't be merged because new extent region may
	 * exceed fsync range.
	 */
			   oe_fsync_wait:1,
	/** covering lock is being canceled */
			   oe_hp:1,
	/** this extent should be written back asap. set if one of pages is
	 * called by page WB daemon, or sync write or reading requests.
	 */
			   oe_urgent:1;
	/** how many grants allocated for this extent.
	 *  Grant allocated for this extent. There is no grant allocated
	 *  for reading extents and sync write extents.
	 */
	unsigned int       oe_grants;
	/** # of dirty pages in this extent */
	unsigned int       oe_nr_pages;
	/** list of pending oap pages. Pages in this list are NOT sorted. */
	struct list_head	 oe_pages;
	/** Since an extent has to be written out in atomic, this is used to
	 * remember the next page need to be locked to write this extent out.
	 * Not used right now.
	 */
	struct osc_page   *oe_next_page;
	/** start and end index of this extent, include start and end
	 * themselves. Page offset here is the page index of osc_pages.
	 * oe_start is used as keyword for red-black tree.
	 */
	pgoff_t	    oe_start;
	pgoff_t	    oe_end;
	/** maximum ending index of this extent, this is limited by
	 * max_pages_per_rpc, lock extent and chunk size.
	 */
	pgoff_t	    oe_max_end;
	/** waitqueue - for those who want to be notified if this extent's
	 * state has changed.
	 */
	wait_queue_head_t	oe_waitq;
	/** lock covering this extent */
	struct ldlm_lock	*oe_dlmlock;
	/** terminator of this extent. Must be true if this extent is in IO. */
	struct task_struct	*oe_owner;
	/** return value of writeback. If somebody is waiting for this extent,
	 * this value can be known by outside world.
	 */
	int		oe_rc;
	/** max pages per rpc when this extent was created */
	unsigned int       oe_mppr;
};

int osc_extent_finish(const struct lu_env *env, struct osc_extent *ext,
		      int sent, int rc);
void osc_extent_release(const struct lu_env *env, struct osc_extent *ext);

int osc_lock_discard_pages(const struct lu_env *env, struct osc_object *osc,
			   pgoff_t start, pgoff_t end, enum cl_lock_mode mode);

typedef int (*osc_page_gang_cbt)(const struct lu_env *, struct cl_io *,
				 struct osc_page *, void *);
int osc_page_gang_lookup(const struct lu_env *env, struct cl_io *io,
			 struct osc_object *osc, pgoff_t start, pgoff_t end,
			 osc_page_gang_cbt cb, void *cbdata);
/** @} osc */

#endif /* OSC_CL_INTERNAL_H */
