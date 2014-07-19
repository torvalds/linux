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
 * Definitions shared between vvp and liblustre, and other clients in the
 * future.
 *
 *   Author: Oleg Drokin <oleg.drokin@sun.com>
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#ifndef LCLIENT_H
#define LCLIENT_H

blkcnt_t dirty_cnt(struct inode *inode);

int cl_glimpse_size0(struct inode *inode, int agl);
int cl_glimpse_lock(const struct lu_env *env, struct cl_io *io,
		    struct inode *inode, struct cl_object *clob, int agl);

static inline int cl_glimpse_size(struct inode *inode)
{
	return cl_glimpse_size0(inode, 0);
}

static inline int cl_agl(struct inode *inode)
{
	return cl_glimpse_size0(inode, 1);
}

/**
 * Locking policy for setattr.
 */
enum ccc_setattr_lock_type {
	/** Locking is done by server */
	SETATTR_NOLOCK,
	/** Extent lock is enqueued */
	SETATTR_EXTENT_LOCK,
	/** Existing local extent lock is used */
	SETATTR_MATCH_LOCK
};


/**
 * IO state private to vvp or slp layers.
 */
struct ccc_io {
	/** super class */
	struct cl_io_slice     cui_cl;
	struct cl_io_lock_link cui_link;
	/**
	 * I/O vector information to or from which read/write is going.
	 */
	struct iov_iter *cui_iter;
	/**
	 * Total size for the left IO.
	 */
	size_t cui_tot_count;

	union {
		struct {
			enum ccc_setattr_lock_type cui_local_lock;
		} setattr;
	} u;
	/**
	 * True iff io is processing glimpse right now.
	 */
	int		  cui_glimpse;
	/**
	 * Layout version when this IO is initialized
	 */
	__u32		cui_layout_gen;
	/**
	 * File descriptor against which IO is done.
	 */
	struct ll_file_data *cui_fd;
	struct kiocb *cui_iocb;
};

/**
 * True, if \a io is a normal io, False for splice_{read,write}.
 * must be implemented in arch specific code.
 */
int cl_is_normalio(const struct lu_env *env, const struct cl_io *io);

extern struct lu_context_key ccc_key;
extern struct lu_context_key ccc_session_key;

struct ccc_thread_info {
	struct cl_lock_descr cti_descr;
	struct cl_io	 cti_io;
	struct cl_attr       cti_attr;
};

static inline struct ccc_thread_info *ccc_env_info(const struct lu_env *env)
{
	struct ccc_thread_info      *info;

	info = lu_context_key_get(&env->le_ctx, &ccc_key);
	LASSERT(info != NULL);
	return info;
}

static inline struct cl_attr *ccc_env_thread_attr(const struct lu_env *env)
{
	struct cl_attr *attr = &ccc_env_info(env)->cti_attr;
	memset(attr, 0, sizeof(*attr));
	return attr;
}

static inline struct cl_io *ccc_env_thread_io(const struct lu_env *env)
{
	struct cl_io *io = &ccc_env_info(env)->cti_io;
	memset(io, 0, sizeof(*io));
	return io;
}

struct ccc_session {
	struct ccc_io cs_ios;
};

static inline struct ccc_session *ccc_env_session(const struct lu_env *env)
{
	struct ccc_session *ses;

	ses = lu_context_key_get(env->le_ses, &ccc_session_key);
	LASSERT(ses != NULL);
	return ses;
}

static inline struct ccc_io *ccc_env_io(const struct lu_env *env)
{
	return &ccc_env_session(env)->cs_ios;
}

/**
 * ccc-private object state.
 */
struct ccc_object {
	struct cl_object_header cob_header;
	struct cl_object	cob_cl;
	struct inode	   *cob_inode;

	/**
	 * A list of dirty pages pending IO in the cache. Used by
	 * SOM. Protected by ll_inode_info::lli_lock.
	 *
	 * \see ccc_page::cpg_pending_linkage
	 */
	struct list_head	     cob_pending_list;

	/**
	 * Access this counter is protected by inode->i_sem. Now that
	 * the lifetime of transient pages must be covered by inode sem,
	 * we don't need to hold any lock..
	 */
	int		     cob_transient_pages;
	/**
	 * Number of outstanding mmaps on this file.
	 *
	 * \see ll_vm_open(), ll_vm_close().
	 */
	atomic_t	    cob_mmap_cnt;

	/**
	 * various flags
	 * cob_discard_page_warned
	 *     if pages belonging to this object are discarded when a client
	 * is evicted, some debug info will be printed, this flag will be set
	 * during processing the first discarded page, then avoid flooding
	 * debug message for lots of discarded pages.
	 *
	 * \see ll_dirty_page_discard_warn.
	 */
	unsigned int		cob_discard_page_warned:1;
};

/**
 * ccc-private page state.
 */
struct ccc_page {
	struct cl_page_slice cpg_cl;
	int		  cpg_defer_uptodate;
	int		  cpg_ra_used;
	int		  cpg_write_queued;
	/**
	 * Non-empty iff this page is already counted in
	 * ccc_object::cob_pending_list. Protected by
	 * ccc_object::cob_pending_guard. This list is only used as a flag,
	 * that is, never iterated through, only checked for list_empty(), but
	 * having a list is useful for debugging.
	 */
	struct list_head	   cpg_pending_linkage;
	/** VM page */
	struct page	  *cpg_page;
};

static inline struct ccc_page *cl2ccc_page(const struct cl_page_slice *slice)
{
	return container_of(slice, struct ccc_page, cpg_cl);
}

struct cl_page    *ccc_vmpage_page_transient(struct page *vmpage);

struct ccc_device {
	struct cl_device    cdv_cl;
	struct super_block *cdv_sb;
	struct cl_device   *cdv_next;
};

struct ccc_lock {
	struct cl_lock_slice clk_cl;
};

struct ccc_req {
	struct cl_req_slice  crq_cl;
};

void *ccc_key_init	(const struct lu_context *ctx,
			   struct lu_context_key *key);
void  ccc_key_fini	(const struct lu_context *ctx,
			   struct lu_context_key *key, void *data);
void *ccc_session_key_init(const struct lu_context *ctx,
			   struct lu_context_key *key);
void  ccc_session_key_fini(const struct lu_context *ctx,
			   struct lu_context_key *key, void *data);

int	      ccc_device_init  (const struct lu_env *env,
				   struct lu_device *d,
				   const char *name, struct lu_device *next);
struct lu_device *ccc_device_fini (const struct lu_env *env,
				   struct lu_device *d);
struct lu_device *ccc_device_alloc(const struct lu_env *env,
				   struct lu_device_type *t,
				   struct lustre_cfg *cfg,
				   const struct lu_device_operations *luops,
				   const struct cl_device_operations *clops);
struct lu_device *ccc_device_free (const struct lu_env *env,
				   struct lu_device *d);
struct lu_object *ccc_object_alloc(const struct lu_env *env,
				   const struct lu_object_header *hdr,
				   struct lu_device *dev,
				   const struct cl_object_operations *clops,
				   const struct lu_object_operations *luops);

int ccc_req_init(const struct lu_env *env, struct cl_device *dev,
		 struct cl_req *req);
void ccc_umount(const struct lu_env *env, struct cl_device *dev);
int ccc_global_init(struct lu_device_type *device_type);
void ccc_global_fini(struct lu_device_type *device_type);
int ccc_object_init0(const struct lu_env *env,struct ccc_object *vob,
		     const struct cl_object_conf *conf);
int ccc_object_init(const struct lu_env *env, struct lu_object *obj,
		    const struct lu_object_conf *conf);
void ccc_object_free(const struct lu_env *env, struct lu_object *obj);
int ccc_lock_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_lock *lock, const struct cl_io *io,
		  const struct cl_lock_operations *lkops);
int ccc_attr_set(const struct lu_env *env, struct cl_object *obj,
		 const struct cl_attr *attr, unsigned valid);
int ccc_object_glimpse(const struct lu_env *env,
		       const struct cl_object *obj, struct ost_lvb *lvb);
int ccc_conf_set(const struct lu_env *env, struct cl_object *obj,
		 const struct cl_object_conf *conf);
struct page *ccc_page_vmpage(const struct lu_env *env,
			    const struct cl_page_slice *slice);
int ccc_page_is_under_lock(const struct lu_env *env,
			   const struct cl_page_slice *slice, struct cl_io *io);
int ccc_fail(const struct lu_env *env, const struct cl_page_slice *slice);
void ccc_transient_page_verify(const struct cl_page *page);
int  ccc_transient_page_own(const struct lu_env *env,
			    const struct cl_page_slice *slice,
			    struct cl_io *io, int nonblock);
void ccc_transient_page_assume(const struct lu_env *env,
			       const struct cl_page_slice *slice,
			       struct cl_io *io);
void ccc_transient_page_unassume(const struct lu_env *env,
				 const struct cl_page_slice *slice,
				 struct cl_io *io);
void ccc_transient_page_disown(const struct lu_env *env,
			       const struct cl_page_slice *slice,
			       struct cl_io *io);
void ccc_transient_page_discard(const struct lu_env *env,
				const struct cl_page_slice *slice,
				struct cl_io *io);
int ccc_transient_page_prep(const struct lu_env *env,
			    const struct cl_page_slice *slice,
			    struct cl_io *io);
void ccc_lock_delete(const struct lu_env *env,
		     const struct cl_lock_slice *slice);
void ccc_lock_fini(const struct lu_env *env,struct cl_lock_slice *slice);
int ccc_lock_enqueue(const struct lu_env *env,const struct cl_lock_slice *slice,
		     struct cl_io *io, __u32 enqflags);
int ccc_lock_unuse(const struct lu_env *env,const struct cl_lock_slice *slice);
int ccc_lock_wait(const struct lu_env *env,const struct cl_lock_slice *slice);
int ccc_lock_fits_into(const struct lu_env *env,
		       const struct cl_lock_slice *slice,
		       const struct cl_lock_descr *need,
		       const struct cl_io *io);
void ccc_lock_state(const struct lu_env *env,
		    const struct cl_lock_slice *slice,
		    enum cl_lock_state state);

void ccc_io_fini(const struct lu_env *env, const struct cl_io_slice *ios);
int ccc_io_one_lock_index(const struct lu_env *env, struct cl_io *io,
			  __u32 enqflags, enum cl_lock_mode mode,
			  pgoff_t start, pgoff_t end);
int ccc_io_one_lock(const struct lu_env *env, struct cl_io *io,
		    __u32 enqflags, enum cl_lock_mode mode,
		    loff_t start, loff_t end);
void ccc_io_end(const struct lu_env *env, const struct cl_io_slice *ios);
void ccc_io_advance(const struct lu_env *env, const struct cl_io_slice *ios,
		    size_t nob);
void ccc_io_update_iov(const struct lu_env *env, struct ccc_io *cio,
		       struct cl_io *io);
int ccc_prep_size(const struct lu_env *env, struct cl_object *obj,
		  struct cl_io *io, loff_t start, size_t count, int *exceed);
void ccc_req_completion(const struct lu_env *env,
			const struct cl_req_slice *slice, int ioret);
void ccc_req_attr_set(const struct lu_env *env,const struct cl_req_slice *slice,
		      const struct cl_object *obj,
		      struct cl_req_attr *oa, obd_valid flags);

struct lu_device   *ccc2lu_dev      (struct ccc_device *vdv);
struct lu_object   *ccc2lu	  (struct ccc_object *vob);
struct ccc_device  *lu2ccc_dev      (const struct lu_device *d);
struct ccc_device  *cl2ccc_dev      (const struct cl_device *d);
struct ccc_object  *lu2ccc	  (const struct lu_object *obj);
struct ccc_object  *cl2ccc	  (const struct cl_object *obj);
struct ccc_lock    *cl2ccc_lock     (const struct cl_lock_slice *slice);
struct ccc_io      *cl2ccc_io       (const struct lu_env *env,
				     const struct cl_io_slice *slice);
struct ccc_req     *cl2ccc_req      (const struct cl_req_slice *slice);
struct page	 *cl2vm_page      (const struct cl_page_slice *slice);
struct inode       *ccc_object_inode(const struct cl_object *obj);
struct ccc_object  *cl_inode2ccc    (struct inode *inode);

int cl_setattr_ost(struct inode *inode, const struct iattr *attr,
		   struct obd_capa *capa);

struct cl_page *ccc_vmpage_page_transient(struct page *vmpage);
int ccc_object_invariant(const struct cl_object *obj);
int cl_file_inode_init(struct inode *inode, struct lustre_md *md);
void cl_inode_fini(struct inode *inode);
int cl_local_size(struct inode *inode);

__u16 ll_dirent_type_get(struct lu_dirent *ent);
__u64 cl_fid_build_ino(const struct lu_fid *fid, int api32);
__u32 cl_fid_build_gen(const struct lu_fid *fid);

# define CLOBINVRNT(env, clob, expr)					\
	((void)sizeof(env), (void)sizeof(clob), (void)sizeof(!!(expr)))

int cl_init_ea_size(struct obd_export *md_exp, struct obd_export *dt_exp);
int cl_ocd_update(struct obd_device *host,
		  struct obd_device *watched,
		  enum obd_notify_event ev, void *owner, void *data);

struct ccc_grouplock {
	struct lu_env   *cg_env;
	struct cl_io    *cg_io;
	struct cl_lock  *cg_lock;
	unsigned long    cg_gid;
};

int  cl_get_grouplock(struct cl_object *obj, unsigned long gid, int nonblock,
		      struct ccc_grouplock *cg);
void cl_put_grouplock(struct ccc_grouplock *cg);

/**
 * New interfaces to get and put lov_stripe_md from lov layer. This violates
 * layering because lov_stripe_md is supposed to be a private data in lov.
 *
 * NB: If you find you have to use these interfaces for your new code, please
 * think about it again. These interfaces may be removed in the future for
 * better layering. */
struct lov_stripe_md *lov_lsm_get(struct cl_object *clobj);
void lov_lsm_put(struct cl_object *clobj, struct lov_stripe_md *lsm);
int lov_read_and_clear_async_rc(struct cl_object *clob);

struct lov_stripe_md *ccc_inode_lsm_get(struct inode *inode);
void ccc_inode_lsm_put(struct inode *inode, struct lov_stripe_md *lsm);

/**
 * Data structure managing a client's cached clean pages. An LRU of
 * pages is maintained, along with other statistics.
 */
struct cl_client_cache {
	atomic_t	ccc_users;    /* # of users (OSCs) of this data */
	struct list_head	ccc_lru;      /* LRU list of cached clean pages */
	spinlock_t	ccc_lru_lock; /* lock for list */
	atomic_t	ccc_lru_left; /* # of LRU entries available */
	unsigned long	ccc_lru_max;  /* Max # of LRU entries possible */
	unsigned int	ccc_lru_shrinkers; /* # of threads reclaiming */
};

#endif /*LCLIENT_H */
