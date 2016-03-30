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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2013, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Internal definitions for VVP layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#ifndef VVP_INTERNAL_H
#define VVP_INTERNAL_H

#include "../include/lustre/lustre_idl.h"
#include "../include/cl_object.h"

enum obd_notify_event;
struct inode;
struct lov_stripe_md;
struct lustre_md;
struct obd_capa;
struct obd_device;
struct obd_export;
struct page;

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

/* specific architecture can implement only part of this list */
enum vvp_io_subtype {
	/** normal IO */
	IO_NORMAL,
	/** io started from splice_{read|write} */
	IO_SPLICE
};

/**
 * IO state private to IO state private to VVP layer.
 */
struct vvp_io {
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
		struct vvp_fault_io {
			/**
			 * Inode modification time that is checked across DLM
			 * lock request.
			 */
			time64_t	    ft_mtime;
			struct vm_area_struct *ft_vma;
			/**
			 *  locked page returned from vvp_io
			 */
			struct page	    *ft_vmpage;
			/**
			 * kernel fault info
			 */
			struct vm_fault *ft_vmf;
			/**
			 * fault API used bitflags for return code.
			 */
			unsigned int    ft_flags;
			/**
			 * check that flags are from filemap_fault
			 */
			bool		ft_flags_valid;
		} fault;
		struct {
			enum ccc_setattr_lock_type cui_local_lock;
		} setattr;
		struct {
			struct pipe_inode_info	*cui_pipe;
			unsigned int		 cui_flags;
		} splice;
		struct {
			struct cl_page_list cui_queue;
			unsigned long cui_written;
			int cui_from;
			int cui_to;
		} write;
	} u;

	enum vvp_io_subtype	cui_io_subtype;

	/**
	 * Layout version when this IO is initialized
	 */
	__u32		cui_layout_gen;
	/**
	 * File descriptor against which IO is done.
	 */
	struct ll_file_data *cui_fd;
	struct kiocb *cui_iocb;

	/* Readahead state. */
	pgoff_t	cui_ra_start;
	pgoff_t	cui_ra_count;
	/* Set when cui_ra_{start,count} have been initialized. */
	bool		cui_ra_valid;
};

/**
 * True, if \a io is a normal io, False for other splice_{read,write}.
 * must be implemented in arch specific code.
 */
int cl_is_normalio(const struct lu_env *env, const struct cl_io *io);

extern struct lu_context_key ccc_key;
extern struct lu_context_key vvp_session_key;

extern struct kmem_cache *vvp_lock_kmem;
extern struct kmem_cache *vvp_object_kmem;

struct ccc_thread_info {
	struct cl_lock		cti_lock;
	struct cl_lock_descr	cti_descr;
	struct cl_io		cti_io;
	struct cl_attr		cti_attr;
};

static inline struct ccc_thread_info *ccc_env_info(const struct lu_env *env)
{
	struct ccc_thread_info      *info;

	info = lu_context_key_get(&env->le_ctx, &ccc_key);
	LASSERT(info);

	return info;
}

static inline struct cl_lock *ccc_env_lock(const struct lu_env *env)
{
	struct cl_lock *lock = &ccc_env_info(env)->cti_lock;

	memset(lock, 0, sizeof(*lock));
	return lock;
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

struct vvp_session {
	struct vvp_io cs_ios;
};

static inline struct vvp_session *vvp_env_session(const struct lu_env *env)
{
	struct vvp_session *ses;

	ses = lu_context_key_get(env->le_ses, &vvp_session_key);
	LASSERT(ses);

	return ses;
}

static inline struct vvp_io *vvp_env_io(const struct lu_env *env)
{
	return &vvp_env_session(env)->cs_ios;
}

/**
 * ccc-private object state.
 */
struct vvp_object {
	struct cl_object_header vob_header;
	struct cl_object        vob_cl;
	struct inode           *vob_inode;

	/**
	 * A list of dirty pages pending IO in the cache. Used by
	 * SOM. Protected by ll_inode_info::lli_lock.
	 *
	 * \see vvp_page::vpg_pending_linkage
	 */
	struct list_head	vob_pending_list;

	/**
	 * Access this counter is protected by inode->i_sem. Now that
	 * the lifetime of transient pages must be covered by inode sem,
	 * we don't need to hold any lock..
	 */
	int			vob_transient_pages;
	/**
	 * Number of outstanding mmaps on this file.
	 *
	 * \see ll_vm_open(), ll_vm_close().
	 */
	atomic_t                vob_mmap_cnt;

	/**
	 * various flags
	 * vob_discard_page_warned
	 *     if pages belonging to this object are discarded when a client
	 * is evicted, some debug info will be printed, this flag will be set
	 * during processing the first discarded page, then avoid flooding
	 * debug message for lots of discarded pages.
	 *
	 * \see ll_dirty_page_discard_warn.
	 */
	unsigned int		vob_discard_page_warned:1;
};

/**
 * VVP-private page state.
 */
struct vvp_page {
	struct cl_page_slice vpg_cl;
	int		  vpg_defer_uptodate;
	int		  vpg_ra_used;
	int		  vpg_write_queued;
	/**
	 * Non-empty iff this page is already counted in
	 * vvp_object::vob_pending_list. This list is only used as a flag,
	 * that is, never iterated through, only checked for list_empty(), but
	 * having a list is useful for debugging.
	 */
	struct list_head	   vpg_pending_linkage;
	/** VM page */
	struct page	  *vpg_page;
};

static inline struct vvp_page *cl2vvp_page(const struct cl_page_slice *slice)
{
	return container_of(slice, struct vvp_page, vpg_cl);
}

static inline pgoff_t vvp_index(struct vvp_page *vvp)
{
	return vvp->vpg_cl.cpl_index;
}

struct vvp_device {
	struct cl_device    vdv_cl;
	struct super_block *vdv_sb;
	struct cl_device   *vdv_next;
};

struct vvp_lock {
	struct cl_lock_slice vlk_cl;
};

struct ccc_req {
	struct cl_req_slice  crq_cl;
};

void *ccc_key_init(const struct lu_context *ctx,
		   struct lu_context_key *key);
void ccc_key_fini(const struct lu_context *ctx,
		  struct lu_context_key *key, void *data);

int ccc_req_init(const struct lu_env *env, struct cl_device *dev,
		 struct cl_req *req);
void ccc_umount(const struct lu_env *env, struct cl_device *dev);
int ccc_global_init(struct lu_device_type *device_type);
void ccc_global_fini(struct lu_device_type *device_type);

int vvp_io_one_lock_index(const struct lu_env *env, struct cl_io *io,
			  __u32 enqflags, enum cl_lock_mode mode,
			  pgoff_t start, pgoff_t end);
int vvp_io_one_lock(const struct lu_env *env, struct cl_io *io,
		    __u32 enqflags, enum cl_lock_mode mode,
		    loff_t start, loff_t end);
void vvp_io_end(const struct lu_env *env, const struct cl_io_slice *ios);
void vvp_io_advance(const struct lu_env *env, const struct cl_io_slice *ios,
		    size_t nob);
void vvp_io_update_iov(const struct lu_env *env, struct vvp_io *cio,
		       struct cl_io *io);
int ccc_prep_size(const struct lu_env *env, struct cl_object *obj,
		  struct cl_io *io, loff_t start, size_t count, int *exceed);
void ccc_req_completion(const struct lu_env *env,
			const struct cl_req_slice *slice, int ioret);
void ccc_req_attr_set(const struct lu_env *env,
		      const struct cl_req_slice *slice,
		      const struct cl_object *obj,
		      struct cl_req_attr *oa, u64 flags);

static inline struct lu_device *vvp2lu_dev(struct vvp_device *vdv)
{
	return &vdv->vdv_cl.cd_lu_dev;
}

static inline struct vvp_device *lu2vvp_dev(const struct lu_device *d)
{
	return container_of0(d, struct vvp_device, vdv_cl.cd_lu_dev);
}

static inline struct vvp_device *cl2vvp_dev(const struct cl_device *d)
{
	return container_of0(d, struct vvp_device, vdv_cl);
}

static inline struct vvp_object *cl2vvp(const struct cl_object *obj)
{
	return container_of0(obj, struct vvp_object, vob_cl);
}

static inline struct vvp_object *lu2vvp(const struct lu_object *obj)
{
	return container_of0(obj, struct vvp_object, vob_cl.co_lu);
}

static inline struct inode *vvp_object_inode(const struct cl_object *obj)
{
	return cl2vvp(obj)->vob_inode;
}

int vvp_object_invariant(const struct cl_object *obj);
struct vvp_object *cl_inode2vvp(struct inode *inode);

static inline struct page *cl2vm_page(const struct cl_page_slice *slice)
{
	return cl2vvp_page(slice)->vpg_page;
}

static inline struct vvp_lock *cl2vvp_lock(const struct cl_lock_slice *slice)
{
	return container_of(slice, struct vvp_lock, vlk_cl);
}

struct vvp_io *cl2vvp_io(const struct lu_env *env,
			 const struct cl_io_slice *slice);
struct ccc_req *cl2ccc_req(const struct cl_req_slice *slice);

int cl_setattr_ost(struct inode *inode, const struct iattr *attr);

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
	struct lu_env	*cg_env;
	struct cl_io	*cg_io;
	struct cl_lock	*cg_lock;
	unsigned long	 cg_gid;
};

int cl_get_grouplock(struct cl_object *obj, unsigned long gid, int nonblock,
		     struct ccc_grouplock *cg);
void cl_put_grouplock(struct ccc_grouplock *cg);

/**
 * New interfaces to get and put lov_stripe_md from lov layer. This violates
 * layering because lov_stripe_md is supposed to be a private data in lov.
 *
 * NB: If you find you have to use these interfaces for your new code, please
 * think about it again. These interfaces may be removed in the future for
 * better layering.
 */
struct lov_stripe_md *lov_lsm_get(struct cl_object *clobj);
void lov_lsm_put(struct cl_object *clobj, struct lov_stripe_md *lsm);
int lov_read_and_clear_async_rc(struct cl_object *clob);

struct lov_stripe_md *ccc_inode_lsm_get(struct inode *inode);
void ccc_inode_lsm_put(struct inode *inode, struct lov_stripe_md *lsm);

int vvp_io_init(const struct lu_env *env, struct cl_object *obj,
		struct cl_io *io);
int vvp_lock_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_lock *lock, const struct cl_io *io);
int vvp_page_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_page *page, pgoff_t index);
struct lu_object *vvp_object_alloc(const struct lu_env *env,
				   const struct lu_object_header *hdr,
				   struct lu_device *dev);

extern const struct file_operations vvp_dump_pgcache_file_ops;

#endif /* VVP_INTERNAL_H */
