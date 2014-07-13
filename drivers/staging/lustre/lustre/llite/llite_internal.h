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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef LLITE_INTERNAL_H
#define LLITE_INTERNAL_H
#include "../include/lustre_debug.h"
#include "../include/lustre_ver.h"
#include "../include/lustre_disk.h"	/* for s2sbi */
#include "../include/lustre_eacl.h"

/* for struct cl_lock_descr and struct cl_io */
#include "../include/cl_object.h"
#include "../include/lclient.h"
#include "../include/lustre_mdc.h"
#include "../include/linux/lustre_intent.h"
#include <linux/compat.h>
#include <linux/posix_acl_xattr.h>

#ifndef FMODE_EXEC
#define FMODE_EXEC 0
#endif

#ifndef VM_FAULT_RETRY
#define VM_FAULT_RETRY 0
#endif

/* Kernel 3.1 kills LOOKUP_CONTINUE, LOOKUP_PARENT is equivalent to it.
 * seem kernel commit 49084c3bb2055c401f3493c13edae14d49128ca0 */
#ifndef LOOKUP_CONTINUE
#define LOOKUP_CONTINUE LOOKUP_PARENT
#endif

/** Only used on client-side for indicating the tail of dir hash/offset. */
#define LL_DIR_END_OFF	  0x7fffffffffffffffULL
#define LL_DIR_END_OFF_32BIT    0x7fffffffUL

#define LL_IT2STR(it) ((it) ? ldlm_it2str((it)->it_op) : "0")
#define LUSTRE_FPRIVATE(file) ((file)->private_data)

struct ll_dentry_data {
	struct lookup_intent		*lld_it;
	unsigned int			lld_sa_generation;
	unsigned int			lld_invalid:1;
	struct rcu_head			lld_rcu_head;
};

#define ll_d2d(de) ((struct ll_dentry_data*)((de)->d_fsdata))

#define LLI_INODE_MAGIC		 0x111d0de5
#define LLI_INODE_DEAD		  0xdeadd00d

/* remote client permission cache */
#define REMOTE_PERM_HASHSIZE 16

struct ll_getname_data {
	struct dir_context ctx;
	char	    *lgd_name;      /* points to a buffer with NAME_MAX+1 size */
	struct lu_fid    lgd_fid;       /* target fid we are looking for */
	int	      lgd_found;     /* inode matched? */
};

/* llite setxid/access permission for user on remote client */
struct ll_remote_perm {
	struct hlist_node	lrp_list;
	uid_t		   lrp_uid;
	gid_t		   lrp_gid;
	uid_t		   lrp_fsuid;
	gid_t		   lrp_fsgid;
	int		     lrp_access_perm; /* MAY_READ/WRITE/EXEC, this
						    is access permission with
						    lrp_fsuid/lrp_fsgid. */
};

enum lli_flags {
	/* MDS has an authority for the Size-on-MDS attributes. */
	LLIF_MDS_SIZE_LOCK      = (1 << 0),
	/* Epoch close is postponed. */
	LLIF_EPOCH_PENDING      = (1 << 1),
	/* DONE WRITING is allowed. */
	LLIF_DONE_WRITING       = (1 << 2),
	/* Sizeon-on-MDS attributes are changed. An attribute update needs to
	 * be sent to MDS. */
	LLIF_SOM_DIRTY	  = (1 << 3),
	/* File data is modified. */
	LLIF_DATA_MODIFIED      = (1 << 4),
	/* File is being restored */
	LLIF_FILE_RESTORING	= (1 << 5),
	/* Xattr cache is attached to the file */
	LLIF_XATTR_CACHE	= (1 << 6),
};

struct ll_inode_info {
	__u32				lli_inode_magic;
	__u32				lli_flags;
	__u64				lli_ioepoch;

	spinlock_t			lli_lock;
	struct posix_acl		*lli_posix_acl;

	struct hlist_head		*lli_remote_perms;
	struct mutex				lli_rmtperm_mutex;

	/* identifying fields for both metadata and data stacks. */
	struct lu_fid		   lli_fid;
	/* Parent fid for accessing default stripe data on parent directory
	 * for allocating OST objects after a mknod() and later open-by-FID. */
	struct lu_fid		   lli_pfid;

	struct list_head		      lli_close_list;
	struct list_head		      lli_oss_capas;
	/* open count currently used by capability only, indicate whether
	 * capability needs renewal */
	atomic_t		    lli_open_count;
	struct obd_capa		*lli_mds_capa;
	unsigned long		      lli_rmtperm_time;

	/* handle is to be sent to MDS later on done_writing and setattr.
	 * Open handle data are needed for the recovery to reconstruct
	 * the inode state on the MDS. XXX: recovery is not ready yet. */
	struct obd_client_handle       *lli_pending_och;

	/* We need all three because every inode may be opened in different
	 * modes */
	struct obd_client_handle       *lli_mds_read_och;
	struct obd_client_handle       *lli_mds_write_och;
	struct obd_client_handle       *lli_mds_exec_och;
	__u64			   lli_open_fd_read_count;
	__u64			   lli_open_fd_write_count;
	__u64			   lli_open_fd_exec_count;
	/* Protects access to och pointers and their usage counters */
	struct mutex			lli_och_mutex;

	struct inode			lli_vfs_inode;

	/* the most recent timestamps obtained from mds */
	struct ost_lvb			lli_lvb;
	spinlock_t			lli_agl_lock;

	/* Try to make the d::member and f::member are aligned. Before using
	 * these members, make clear whether it is directory or not. */
	union {
		/* for directory */
		struct {
			/* serialize normal readdir and statahead-readdir. */
			struct mutex			d_readdir_mutex;

			/* metadata statahead */
			/* since parent-child threads can share the same @file
			 * struct, "opendir_key" is the token when dir close for
			 * case of parent exit before child -- it is me should
			 * cleanup the dir readahead. */
			void			   *d_opendir_key;
			struct ll_statahead_info       *d_sai;
			/* protect statahead stuff. */
			spinlock_t			d_sa_lock;
			/* "opendir_pid" is the token when lookup/revalid
			 * -- I am the owner of dir statahead. */
			pid_t			   d_opendir_pid;
		} d;

#define lli_readdir_mutex       u.d.d_readdir_mutex
#define lli_opendir_key	 u.d.d_opendir_key
#define lli_sai		 u.d.d_sai
#define lli_sa_lock	     u.d.d_sa_lock
#define lli_opendir_pid	 u.d.d_opendir_pid

		/* for non-directory */
		struct {
			struct mutex			f_size_mutex;
			char				*f_symlink_name;
			__u64				f_maxbytes;
			/*
			 * struct rw_semaphore {
			 *    signed long	count;     // align d.d_def_acl
			 *    spinlock_t	wait_lock; // align d.d_sa_lock
			 *    struct list_head wait_list;
			 * }
			 */
			struct rw_semaphore		f_trunc_sem;
			struct mutex			f_write_mutex;

			struct rw_semaphore		f_glimpse_sem;
			unsigned long			f_glimpse_time;
			struct list_head			f_agl_list;
			__u64				f_agl_index;

			/* for writepage() only to communicate to fsync */
			int				f_async_rc;

			/*
			 * whenever a process try to read/write the file, the
			 * jobid of the process will be saved here, and it'll
			 * be packed into the write PRC when flush later.
			 *
			 * so the read/write statistics for jobid will not be
			 * accurate if the file is shared by different jobs.
			 */
			char		     f_jobid[JOBSTATS_JOBID_SIZE];
		} f;

#define lli_size_mutex          u.f.f_size_mutex
#define lli_symlink_name	u.f.f_symlink_name
#define lli_maxbytes	    u.f.f_maxbytes
#define lli_trunc_sem	   u.f.f_trunc_sem
#define lli_write_mutex	 u.f.f_write_mutex
#define lli_glimpse_sem		u.f.f_glimpse_sem
#define lli_glimpse_time	u.f.f_glimpse_time
#define lli_agl_list		u.f.f_agl_list
#define lli_agl_index		u.f.f_agl_index
#define lli_async_rc		u.f.f_async_rc
#define lli_jobid		u.f.f_jobid

	} u;

	/* XXX: For following frequent used members, although they maybe special
	 *      used for non-directory object, it is some time-wasting to check
	 *      whether the object is directory or not before using them. On the
	 *      other hand, currently, sizeof(f) > sizeof(d), it cannot reduce
	 *      the "ll_inode_info" size even if moving those members into u.f.
	 *      So keep them out side.
	 *
	 *      In the future, if more members are added only for directory,
	 *      some of the following members can be moved into u.f.
	 */
	bool			    lli_has_smd;
	struct cl_object	       *lli_clob;

	/* mutex to request for layout lock exclusively. */
	struct mutex			lli_layout_mutex;
	/* Layout version, protected by lli_layout_lock */
	__u32				lli_layout_gen;
	spinlock_t			lli_layout_lock;

	struct rw_semaphore		lli_xattrs_list_rwsem;
	struct mutex			lli_xattrs_enq_lock;
	struct list_head		lli_xattrs;/* ll_xattr_entry->xe_list */
};

static inline __u32 ll_layout_version_get(struct ll_inode_info *lli)
{
	__u32 gen;

	spin_lock(&lli->lli_layout_lock);
	gen = lli->lli_layout_gen;
	spin_unlock(&lli->lli_layout_lock);

	return gen;
}

static inline void ll_layout_version_set(struct ll_inode_info *lli, __u32 gen)
{
	spin_lock(&lli->lli_layout_lock);
	lli->lli_layout_gen = gen;
	spin_unlock(&lli->lli_layout_lock);
}

int ll_xattr_cache_destroy(struct inode *inode);

int ll_xattr_cache_get(struct inode *inode,
			const char *name,
			char *buffer,
			size_t size,
			__u64 valid);

/*
 * Locking to guarantee consistency of non-atomic updates to long long i_size,
 * consistency between file size and KMS.
 *
 * Implemented by ->lli_size_mutex and ->lsm_lock, nested in that order.
 */

void ll_inode_size_lock(struct inode *inode);
void ll_inode_size_unlock(struct inode *inode);

// FIXME: replace the name of this with LL_I to conform to kernel stuff
// static inline struct ll_inode_info *LL_I(struct inode *inode)
static inline struct ll_inode_info *ll_i2info(struct inode *inode)
{
	return container_of(inode, struct ll_inode_info, lli_vfs_inode);
}

/* default to about 40meg of readahead on a given system.  That much tied
 * up in 512k readahead requests serviced at 40ms each is about 1GB/s. */
#define SBI_DEFAULT_READAHEAD_MAX (40UL << (20 - PAGE_CACHE_SHIFT))

/* default to read-ahead full files smaller than 2MB on the second read */
#define SBI_DEFAULT_READAHEAD_WHOLE_MAX (2UL << (20 - PAGE_CACHE_SHIFT))

enum ra_stat {
	RA_STAT_HIT = 0,
	RA_STAT_MISS,
	RA_STAT_DISTANT_READPAGE,
	RA_STAT_MISS_IN_WINDOW,
	RA_STAT_FAILED_GRAB_PAGE,
	RA_STAT_FAILED_MATCH,
	RA_STAT_DISCARDED,
	RA_STAT_ZERO_LEN,
	RA_STAT_ZERO_WINDOW,
	RA_STAT_EOF,
	RA_STAT_MAX_IN_FLIGHT,
	RA_STAT_WRONG_GRAB_PAGE,
	_NR_RA_STAT,
};

struct ll_ra_info {
	atomic_t	      ra_cur_pages;
	unsigned long	     ra_max_pages;
	unsigned long	     ra_max_pages_per_file;
	unsigned long	     ra_max_read_ahead_whole_pages;
};

/* ra_io_arg will be filled in the beginning of ll_readahead with
 * ras_lock, then the following ll_read_ahead_pages will read RA
 * pages according to this arg, all the items in this structure are
 * counted by page index.
 */
struct ra_io_arg {
	unsigned long ria_start;  /* start offset of read-ahead*/
	unsigned long ria_end;    /* end offset of read-ahead*/
	/* If stride read pattern is detected, ria_stoff means where
	 * stride read is started. Note: for normal read-ahead, the
	 * value here is meaningless, and also it will not be accessed*/
	pgoff_t ria_stoff;
	/* ria_length and ria_pages are the length and pages length in the
	 * stride I/O mode. And they will also be used to check whether
	 * it is stride I/O read-ahead in the read-ahead pages*/
	unsigned long ria_length;
	unsigned long ria_pages;
};

/* LL_HIST_MAX=32 causes an overflow */
#define LL_HIST_MAX 28
#define LL_HIST_START 12 /* buckets start at 2^12 = 4k */
#define LL_PROCESS_HIST_MAX 10
struct per_process_info {
	pid_t pid;
	struct obd_histogram pp_r_hist;
	struct obd_histogram pp_w_hist;
};

/* pp_extents[LL_PROCESS_HIST_MAX] will hold the combined process info */
struct ll_rw_extents_info {
	struct per_process_info pp_extents[LL_PROCESS_HIST_MAX + 1];
};

#define LL_OFFSET_HIST_MAX 100
struct ll_rw_process_info {
	pid_t		     rw_pid;
	int		       rw_op;
	loff_t		    rw_range_start;
	loff_t		    rw_range_end;
	loff_t		    rw_last_file_pos;
	loff_t		    rw_offset;
	size_t		    rw_smallest_extent;
	size_t		    rw_largest_extent;
	struct ll_file_data      *rw_last_file;
};

enum stats_track_type {
	STATS_TRACK_ALL = 0,  /* track all processes */
	STATS_TRACK_PID,      /* track process with this pid */
	STATS_TRACK_PPID,     /* track processes with this ppid */
	STATS_TRACK_GID,      /* track processes with this gid */
	STATS_TRACK_LAST,
};

/* flags for sbi->ll_flags */
#define LL_SBI_NOLCK	     0x01 /* DLM locking disabled (directio-only) */
#define LL_SBI_CHECKSUM	  0x02 /* checksum each page as it's written */
#define LL_SBI_FLOCK	     0x04
#define LL_SBI_USER_XATTR	0x08 /* support user xattr */
#define LL_SBI_ACL	       0x10 /* support ACL */
#define LL_SBI_RMT_CLIENT	0x40 /* remote client */
#define LL_SBI_MDS_CAPA	  0x80 /* support mds capa */
#define LL_SBI_OSS_CAPA	 0x100 /* support oss capa */
#define LL_SBI_LOCALFLOCK       0x200 /* Local flocks support by kernel */
#define LL_SBI_LRU_RESIZE       0x400 /* lru resize support */
#define LL_SBI_LAZYSTATFS       0x800 /* lazystatfs mount option */
#define LL_SBI_SOM_PREVIEW     0x1000 /* SOM preview mount option */
#define LL_SBI_32BIT_API       0x2000 /* generate 32 bit inodes. */
#define LL_SBI_64BIT_HASH      0x4000 /* support 64-bits dir hash/offset */
#define LL_SBI_AGL_ENABLED     0x8000 /* enable agl */
#define LL_SBI_VERBOSE	0x10000 /* verbose mount/umount */
#define LL_SBI_LAYOUT_LOCK    0x20000 /* layout lock support */
#define LL_SBI_USER_FID2PATH  0x40000 /* allow fid2path by unprivileged users */
#define LL_SBI_XATTR_CACHE    0x80000 /* support for xattr cache */

#define LL_SBI_FLAGS {	\
	"nolck",	\
	"checksum",	\
	"flock",	\
	"xattr",	\
	"acl",		\
	"???",		\
	"rmt_client",	\
	"mds_capa",	\
	"oss_capa",	\
	"flock",	\
	"lru_resize",	\
	"lazy_statfs",	\
	"som",		\
	"32bit_api",	\
	"64bit_hash",	\
	"agl",		\
	"verbose",	\
	"layout",	\
	"user_fid2path",\
	"xattr",	\
}

#define RCE_HASHES      32

struct rmtacl_ctl_entry {
	struct list_head       rce_list;
	pid_t	    rce_key; /* hash key */
	int	      rce_ops; /* acl operation type */
};

struct rmtacl_ctl_table {
	spinlock_t	rct_lock;
	struct list_head	rct_entries[RCE_HASHES];
};

#define EE_HASHES       32

struct eacl_table {
	spinlock_t	et_lock;
	struct list_head	et_entries[EE_HASHES];
};

struct ll_sb_info {
	struct list_head		  ll_list;
	/* this protects pglist and ra_info.  It isn't safe to
	 * grab from interrupt contexts */
	spinlock_t		  ll_lock;
	spinlock_t		  ll_pp_extent_lock; /* pp_extent entry*/
	spinlock_t		  ll_process_lock; /* ll_rw_process_info */
	struct obd_uuid	   ll_sb_uuid;
	struct obd_export	*ll_md_exp;
	struct obd_export	*ll_dt_exp;
	struct proc_dir_entry*    ll_proc_root;
	struct lu_fid	     ll_root_fid; /* root object fid */

	int		       ll_flags;
	unsigned int		  ll_umounting:1,
				  ll_xattr_cache_enabled:1;
	struct list_head		ll_conn_chain; /* per-conn chain of SBs */
	struct lustre_client_ocd  ll_lco;

	struct list_head		ll_orphan_dentry_list; /*please don't ask -p*/
	struct ll_close_queue    *ll_lcq;

	struct lprocfs_stats     *ll_stats; /* lprocfs stats counter */

	struct cl_client_cache    ll_cache;

	struct lprocfs_stats     *ll_ra_stats;

	struct ll_ra_info	 ll_ra_info;
	unsigned int	      ll_namelen;
	struct file_operations   *ll_fop;

	/* =0 - hold lock over whole read/write
	 * >0 - max. chunk to be read/written w/o lock re-acquiring */
	unsigned long	     ll_max_rw_chunk;
	unsigned int	      ll_md_brw_size; /* used by readdir */

	struct lu_site	   *ll_site;
	struct cl_device	 *ll_cl;
	/* Statistics */
	struct ll_rw_extents_info ll_rw_extents_info;
	int		       ll_extent_process_count;
	struct ll_rw_process_info ll_rw_process_info[LL_PROCESS_HIST_MAX];
	unsigned int	      ll_offset_process_count;
	struct ll_rw_process_info ll_rw_offset_info[LL_OFFSET_HIST_MAX];
	unsigned int	      ll_rw_offset_entry_count;
	int		       ll_stats_track_id;
	enum stats_track_type     ll_stats_track_type;
	int		       ll_rw_stats_on;

	/* metadata stat-ahead */
	unsigned int	      ll_sa_max;     /* max statahead RPCs */
	atomic_t		  ll_sa_total;   /* statahead thread started
						  * count */
	atomic_t		  ll_sa_wrong;   /* statahead thread stopped for
						  * low hit ratio */
	atomic_t		  ll_agl_total;  /* AGL thread started count */

	dev_t		     ll_sdev_orig; /* save s_dev before assign for
						 * clustered nfs */
	struct rmtacl_ctl_table   ll_rct;
	struct eacl_table	 ll_et;
	__kernel_fsid_t		  ll_fsid;
};

#define LL_DEFAULT_MAX_RW_CHUNK      (32 * 1024 * 1024)

struct ll_ra_read {
	pgoff_t	     lrr_start;
	pgoff_t	     lrr_count;
	struct task_struct *lrr_reader;
	struct list_head	  lrr_linkage;
};

/*
 * per file-descriptor read-ahead data.
 */
struct ll_readahead_state {
	spinlock_t  ras_lock;
	/*
	 * index of the last page that read(2) needed and that wasn't in the
	 * cache. Used by ras_update() to detect seeks.
	 *
	 * XXX nikita: if access seeks into cached region, Lustre doesn't see
	 * this.
	 */
	unsigned long   ras_last_readpage;
	/*
	 * number of pages read after last read-ahead window reset. As window
	 * is reset on each seek, this is effectively a number of consecutive
	 * accesses. Maybe ->ras_accessed_in_window is better name.
	 *
	 * XXX nikita: window is also reset (by ras_update()) when Lustre
	 * believes that memory pressure evicts read-ahead pages. In that
	 * case, it probably doesn't make sense to expand window to
	 * PTLRPC_MAX_BRW_PAGES on the third access.
	 */
	unsigned long   ras_consecutive_pages;
	/*
	 * number of read requests after the last read-ahead window reset
	 * As window is reset on each seek, this is effectively the number
	 * on consecutive read request and is used to trigger read-ahead.
	 */
	unsigned long   ras_consecutive_requests;
	/*
	 * Parameters of current read-ahead window. Handled by
	 * ras_update(). On the initial access to the file or after a seek,
	 * window is reset to 0. After 3 consecutive accesses, window is
	 * expanded to PTLRPC_MAX_BRW_PAGES. Afterwards, window is enlarged by
	 * PTLRPC_MAX_BRW_PAGES chunks up to ->ra_max_pages.
	 */
	unsigned long   ras_window_start, ras_window_len;
	/*
	 * Where next read-ahead should start at. This lies within read-ahead
	 * window. Read-ahead window is read in pieces rather than at once
	 * because: 1. lustre limits total number of pages under read-ahead by
	 * ->ra_max_pages (see ll_ra_count_get()), 2. client cannot read pages
	 * not covered by DLM lock.
	 */
	unsigned long   ras_next_readahead;
	/*
	 * Total number of ll_file_read requests issued, reads originating
	 * due to mmap are not counted in this total.  This value is used to
	 * trigger full file read-ahead after multiple reads to a small file.
	 */
	unsigned long   ras_requests;
	/*
	 * Page index with respect to the current request, these value
	 * will not be accurate when dealing with reads issued via mmap.
	 */
	unsigned long   ras_request_index;
	/*
	 * list of struct ll_ra_read's one per read(2) call current in
	 * progress against this file descriptor. Used by read-ahead code,
	 * protected by ->ras_lock.
	 */
	struct list_head      ras_read_beads;
	/*
	 * The following 3 items are used for detecting the stride I/O
	 * mode.
	 * In stride I/O mode,
	 * ...............|-----data-----|****gap*****|--------|******|....
	 *    offset      |-stride_pages-|-stride_gap-|
	 * ras_stride_offset = offset;
	 * ras_stride_length = stride_pages + stride_gap;
	 * ras_stride_pages = stride_pages;
	 * Note: all these three items are counted by pages.
	 */
	unsigned long   ras_stride_length;
	unsigned long   ras_stride_pages;
	pgoff_t	 ras_stride_offset;
	/*
	 * number of consecutive stride request count, and it is similar as
	 * ras_consecutive_requests, but used for stride I/O mode.
	 * Note: only more than 2 consecutive stride request are detected,
	 * stride read-ahead will be enable
	 */
	unsigned long   ras_consecutive_stride_requests;
};

extern struct kmem_cache *ll_file_data_slab;
struct lustre_handle;
struct ll_file_data {
	struct ll_readahead_state fd_ras;
	struct ccc_grouplock fd_grouplock;
	__u64 lfd_pos;
	__u32 fd_flags;
	fmode_t fd_omode;
	/* openhandle if lease exists for this file.
	 * Borrow lli->lli_och_mutex to protect assignment */
	struct obd_client_handle *fd_lease_och;
	struct obd_client_handle *fd_och;
	struct file *fd_file;
	/* Indicate whether need to report failure when close.
	 * true: failure is known, not report again.
	 * false: unknown failure, should report. */
	bool fd_write_failed;
};

struct lov_stripe_md;

extern spinlock_t inode_lock;

extern struct proc_dir_entry *proc_lustre_fs_root;

static inline struct inode *ll_info2i(struct ll_inode_info *lli)
{
	return &lli->lli_vfs_inode;
}

__u32 ll_i2suppgid(struct inode *i);
void ll_i2gids(__u32 *suppgids, struct inode *i1,struct inode *i2);

static inline int ll_need_32bit_api(struct ll_sb_info *sbi)
{
#if BITS_PER_LONG == 32
	return 1;
#elif defined(CONFIG_COMPAT)
	return unlikely(is_compat_task() || (sbi->ll_flags & LL_SBI_32BIT_API));
#else
	return unlikely(sbi->ll_flags & LL_SBI_32BIT_API);
#endif
}

void ll_ra_read_in(struct file *f, struct ll_ra_read *rar);
void ll_ra_read_ex(struct file *f, struct ll_ra_read *rar);
struct ll_ra_read *ll_ra_read_get(struct file *f);

/* llite/lproc_llite.c */
#ifdef LPROCFS
int lprocfs_register_mountpoint(struct proc_dir_entry *parent,
				struct super_block *sb, char *osc, char *mdc);
void lprocfs_unregister_mountpoint(struct ll_sb_info *sbi);
void ll_stats_ops_tally(struct ll_sb_info *sbi, int op, int count);
void lprocfs_llite_init_vars(struct lprocfs_static_vars *lvars);
void ll_rw_stats_tally(struct ll_sb_info *sbi, pid_t pid,
		       struct ll_file_data *file, loff_t pos,
		       size_t count, int rw);
#else
static inline int lprocfs_register_mountpoint(struct proc_dir_entry *parent,
			struct super_block *sb, char *osc, char *mdc){return 0;}
static inline void lprocfs_unregister_mountpoint(struct ll_sb_info *sbi) {}
static inline
void ll_stats_ops_tally(struct ll_sb_info *sbi, int op, int count) {}
static inline void lprocfs_llite_init_vars(struct lprocfs_static_vars *lvars)
{
	memset(lvars, 0, sizeof(*lvars));
}
static inline void ll_rw_stats_tally(struct ll_sb_info *sbi, pid_t pid,
				     struct ll_file_data *file, loff_t pos,
				     size_t count, int rw) {}
#endif


/* llite/dir.c */
void ll_release_page(struct page *page, int remove);
extern const struct file_operations ll_dir_operations;
extern const struct inode_operations ll_dir_inode_operations;
struct page *ll_get_dir_page(struct inode *dir, __u64 hash,
			     struct ll_dir_chain *chain);
int ll_dir_read(struct inode *inode, struct dir_context *ctx);

int ll_get_mdt_idx(struct inode *inode);
/* llite/namei.c */
extern const struct inode_operations ll_special_inode_operations;

int ll_objects_destroy(struct ptlrpc_request *request,
		       struct inode *dir);
struct inode *ll_iget(struct super_block *sb, ino_t hash,
		      struct lustre_md *lic);
int ll_md_blocking_ast(struct ldlm_lock *, struct ldlm_lock_desc *,
		       void *data, int flag);
struct dentry *ll_splice_alias(struct inode *inode, struct dentry *de);
int ll_rmdir_entry(struct inode *dir, char *name, int namelen);

/* llite/rw.c */
int ll_prepare_write(struct file *, struct page *, unsigned from, unsigned to);
int ll_commit_write(struct file *, struct page *, unsigned from, unsigned to);
int ll_writepage(struct page *page, struct writeback_control *wbc);
int ll_writepages(struct address_space *, struct writeback_control *wbc);
int ll_readpage(struct file *file, struct page *page);
void ll_readahead_init(struct inode *inode, struct ll_readahead_state *ras);
int ll_readahead(const struct lu_env *env, struct cl_io *io,
		 struct ll_readahead_state *ras, struct address_space *mapping,
		 struct cl_page_list *queue, int flags);

#ifndef MS_HAS_NEW_AOPS
extern const struct address_space_operations ll_aops;
#else
extern const struct address_space_operations_ext ll_aops;
#endif

/* llite/file.c */
extern struct file_operations ll_file_operations;
extern struct file_operations ll_file_operations_flock;
extern struct file_operations ll_file_operations_noflock;
extern struct inode_operations ll_file_inode_operations;
extern int ll_have_md_lock(struct inode *inode, __u64 *bits,
			   ldlm_mode_t l_req_mode);
extern ldlm_mode_t ll_take_md_lock(struct inode *inode, __u64 bits,
				   struct lustre_handle *lockh, __u64 flags,
				   ldlm_mode_t mode);
int ll_file_open(struct inode *inode, struct file *file);
int ll_file_release(struct inode *inode, struct file *file);
int ll_glimpse_ioctl(struct ll_sb_info *sbi,
		     struct lov_stripe_md *lsm, lstat_t *st);
void ll_ioepoch_open(struct ll_inode_info *lli, __u64 ioepoch);
int ll_release_openhandle(struct dentry *, struct lookup_intent *);
int ll_md_real_close(struct inode *inode, fmode_t fmode);
void ll_ioepoch_close(struct inode *inode, struct md_op_data *op_data,
		      struct obd_client_handle **och, unsigned long flags);
void ll_done_writing_attr(struct inode *inode, struct md_op_data *op_data);
int ll_som_update(struct inode *inode, struct md_op_data *op_data);
int ll_inode_getattr(struct inode *inode, struct obdo *obdo,
		     __u64 ioepoch, int sync);
void ll_pack_inode2opdata(struct inode *inode, struct md_op_data *op_data,
			  struct lustre_handle *fh);
int ll_getattr(struct vfsmount *mnt, struct dentry *de, struct kstat *stat);
struct posix_acl *ll_get_acl(struct inode *inode, int type);

int ll_inode_permission(struct inode *inode, int mask);

int ll_lov_setstripe_ea_info(struct inode *inode, struct file *file,
			     int flags, struct lov_user_md *lum,
			     int lum_size);
int ll_lov_getstripe_ea_info(struct inode *inode, const char *filename,
			     struct lov_mds_md **lmm, int *lmm_size,
			     struct ptlrpc_request **request);
int ll_dir_setstripe(struct inode *inode, struct lov_user_md *lump,
		     int set_default);
int ll_dir_getstripe(struct inode *inode, struct lov_mds_md **lmmp,
		     int *lmm_size, struct ptlrpc_request **request);
int ll_fsync(struct file *file, loff_t start, loff_t end, int data);
int ll_merge_lvb(const struct lu_env *env, struct inode *inode);
int ll_fid2path(struct inode *inode, void *arg);
int ll_data_version(struct inode *inode, __u64 *data_version, int extent_lock);
int ll_hsm_release(struct inode *inode);

/* llite/dcache.c */

int ll_d_init(struct dentry *de);
extern const struct dentry_operations ll_d_ops;
void ll_intent_drop_lock(struct lookup_intent *);
void ll_intent_release(struct lookup_intent *);
void ll_invalidate_aliases(struct inode *);
void ll_lookup_finish_locks(struct lookup_intent *it, struct dentry *dentry);
int ll_revalidate_it_finish(struct ptlrpc_request *request,
			    struct lookup_intent *it, struct dentry *de);

/* llite/llite_lib.c */
extern struct super_operations lustre_super_operations;

void ll_lli_init(struct ll_inode_info *lli);
int ll_fill_super(struct super_block *sb, struct vfsmount *mnt);
void ll_put_super(struct super_block *sb);
void ll_kill_super(struct super_block *sb);
struct inode *ll_inode_from_resource_lock(struct ldlm_lock *lock);
void ll_clear_inode(struct inode *inode);
int ll_setattr_raw(struct dentry *dentry, struct iattr *attr, bool hsm_import);
int ll_setattr(struct dentry *de, struct iattr *attr);
int ll_statfs(struct dentry *de, struct kstatfs *sfs);
int ll_statfs_internal(struct super_block *sb, struct obd_statfs *osfs,
		       __u64 max_age, __u32 flags);
void ll_update_inode(struct inode *inode, struct lustre_md *md);
void ll_read_inode2(struct inode *inode, void *opaque);
void ll_delete_inode(struct inode *inode);
int ll_iocontrol(struct inode *inode, struct file *file,
		 unsigned int cmd, unsigned long arg);
int ll_flush_ctx(struct inode *inode);
void ll_umount_begin(struct super_block *sb);
int ll_remount_fs(struct super_block *sb, int *flags, char *data);
int ll_show_options(struct seq_file *seq, struct dentry *dentry);
void ll_dirty_page_discard_warn(struct page *page, int ioret);
int ll_prep_inode(struct inode **inode, struct ptlrpc_request *req,
		  struct super_block *, struct lookup_intent *);
void lustre_dump_dentry(struct dentry *, int recur);
int ll_obd_statfs(struct inode *inode, void *arg);
int ll_get_max_mdsize(struct ll_sb_info *sbi, int *max_mdsize);
int ll_get_default_mdsize(struct ll_sb_info *sbi, int *default_mdsize);
int ll_get_max_cookiesize(struct ll_sb_info *sbi, int *max_cookiesize);
int ll_get_default_cookiesize(struct ll_sb_info *sbi, int *default_cookiesize);
int ll_process_config(struct lustre_cfg *lcfg);
struct md_op_data *ll_prep_md_op_data(struct md_op_data *op_data,
				      struct inode *i1, struct inode *i2,
				      const char *name, int namelen,
				      int mode, __u32 opc, void *data);
void ll_finish_md_op_data(struct md_op_data *op_data);
int ll_get_obd_name(struct inode *inode, unsigned int cmd, unsigned long arg);
char *ll_get_fsname(struct super_block *sb, char *buf, int buflen);

/* llite/llite_nfs.c */
extern struct export_operations lustre_export_operations;
__u32 get_uuid2int(const char *name, int len);
void get_uuid2fsid(const char *name, int len, __kernel_fsid_t *fsid);
struct inode *search_inode_for_lustre(struct super_block *sb,
				      const struct lu_fid *fid);

/* llite/symlink.c */
extern struct inode_operations ll_fast_symlink_inode_operations;

/* llite/llite_close.c */
struct ll_close_queue {
	spinlock_t		lcq_lock;
	struct list_head		lcq_head;
	wait_queue_head_t		lcq_waitq;
	struct completion	lcq_comp;
	atomic_t		lcq_stop;
};

struct ccc_object *cl_inode2ccc(struct inode *inode);


void vvp_write_pending (struct ccc_object *club, struct ccc_page *page);
void vvp_write_complete(struct ccc_object *club, struct ccc_page *page);

/* specific architecture can implement only part of this list */
enum vvp_io_subtype {
	/** normal IO */
	IO_NORMAL,
	/** io started from splice_{read|write} */
	IO_SPLICE
};

/* IO subtypes */
struct vvp_io {
	/** io subtype */
	enum vvp_io_subtype    cui_io_subtype;

	union {
		struct {
			struct pipe_inode_info *cui_pipe;
			unsigned int	    cui_flags;
		} splice;
		struct vvp_fault_io {
			/**
			 * Inode modification time that is checked across DLM
			 * lock request.
			 */
			time_t		 ft_mtime;
			struct vm_area_struct *ft_vma;
			/**
			 *  locked page returned from vvp_io
			 */
			struct page	    *ft_vmpage;
			struct vm_fault_api {
				/**
				 * kernel fault info
				 */
				struct vm_fault *ft_vmf;
				/**
				 * fault API used bitflags for return code.
				 */
				unsigned int    ft_flags;
			} fault;
		} fault;
	} u;
	/**
	 * Read-ahead state used by read and page-fault IO contexts.
	 */
	struct ll_ra_read    cui_bead;
	/**
	 * Set when cui_bead has been initialized.
	 */
	int		  cui_ra_window_set;
};

/**
 * IO arguments for various VFS I/O interfaces.
 */
struct vvp_io_args {
	/** normal/splice */
	enum vvp_io_subtype via_io_subtype;

	union {
		struct {
			struct kiocb      *via_iocb;
			struct iov_iter   *via_iter;
		} normal;
		struct {
			struct pipe_inode_info  *via_pipe;
			unsigned int       via_flags;
		} splice;
	} u;
};

struct ll_cl_context {
	void	   *lcc_cookie;
	struct cl_io   *lcc_io;
	struct cl_page *lcc_page;
	struct lu_env  *lcc_env;
	int	     lcc_refcheck;
};

struct vvp_thread_info {
	struct iovec	 vti_local_iov;
	struct vvp_io_args   vti_args;
	struct ra_io_arg     vti_ria;
	struct kiocb	 vti_kiocb;
	struct ll_cl_context vti_io_ctx;
};

static inline struct vvp_thread_info *vvp_env_info(const struct lu_env *env)
{
	extern struct lu_context_key vvp_key;
	struct vvp_thread_info      *info;

	info = lu_context_key_get(&env->le_ctx, &vvp_key);
	LASSERT(info != NULL);
	return info;
}

static inline struct vvp_io_args *vvp_env_args(const struct lu_env *env,
					       enum vvp_io_subtype type)
{
	struct vvp_io_args *ret = &vvp_env_info(env)->vti_args;

	ret->via_io_subtype = type;

	return ret;
}

struct vvp_session {
	struct vvp_io	 vs_ios;
};

static inline struct vvp_session *vvp_env_session(const struct lu_env *env)
{
	extern struct lu_context_key vvp_session_key;
	struct vvp_session *ses;

	ses = lu_context_key_get(env->le_ses, &vvp_session_key);
	LASSERT(ses != NULL);
	return ses;
}

static inline struct vvp_io *vvp_env_io(const struct lu_env *env)
{
	return &vvp_env_session(env)->vs_ios;
}

int vvp_global_init(void);
void vvp_global_fini(void);

void ll_queue_done_writing(struct inode *inode, unsigned long flags);
void ll_close_thread_shutdown(struct ll_close_queue *lcq);
int ll_close_thread_start(struct ll_close_queue **lcq_ret);

/* llite/llite_mmap.c */

int ll_teardown_mmaps(struct address_space *mapping, __u64 first, __u64 last);
int ll_file_mmap(struct file * file, struct vm_area_struct * vma);
void policy_from_vma(ldlm_policy_data_t *policy,
		struct vm_area_struct *vma, unsigned long addr, size_t count);
struct vm_area_struct *our_vma(struct mm_struct *mm, unsigned long addr,
			       size_t count);

static inline void ll_invalidate_page(struct page *vmpage)
{
	struct address_space *mapping = vmpage->mapping;
	loff_t offset = vmpage->index << PAGE_CACHE_SHIFT;

	LASSERT(PageLocked(vmpage));
	if (mapping == NULL)
		return;

	ll_teardown_mmaps(mapping, offset, offset + PAGE_CACHE_SIZE);
	truncate_complete_page(mapping, vmpage);
}

#define    ll_s2sbi(sb)	(s2lsi(sb)->lsi_llsbi)

/* don't need an addref as the sb_info should be holding one */
static inline struct obd_export *ll_s2dtexp(struct super_block *sb)
{
	return ll_s2sbi(sb)->ll_dt_exp;
}

/* don't need an addref as the sb_info should be holding one */
static inline struct obd_export *ll_s2mdexp(struct super_block *sb)
{
	return ll_s2sbi(sb)->ll_md_exp;
}

static inline struct client_obd *sbi2mdc(struct ll_sb_info *sbi)
{
	struct obd_device *obd = sbi->ll_md_exp->exp_obd;
	if (obd == NULL)
		LBUG();
	return &obd->u.cli;
}

// FIXME: replace the name of this with LL_SB to conform to kernel stuff
static inline struct ll_sb_info *ll_i2sbi(struct inode *inode)
{
	return ll_s2sbi(inode->i_sb);
}

static inline struct obd_export *ll_i2dtexp(struct inode *inode)
{
	return ll_s2dtexp(inode->i_sb);
}

static inline struct obd_export *ll_i2mdexp(struct inode *inode)
{
	return ll_s2mdexp(inode->i_sb);
}

static inline struct lu_fid *ll_inode2fid(struct inode *inode)
{
	struct lu_fid *fid;

	LASSERT(inode != NULL);
	fid = &ll_i2info(inode)->lli_fid;

	return fid;
}

static inline __u64 ll_file_maxbytes(struct inode *inode)
{
	return ll_i2info(inode)->lli_maxbytes;
}

/* llite/xattr.c */
int ll_setxattr(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags);
ssize_t ll_getxattr(struct dentry *dentry, const char *name,
		    void *buffer, size_t size);
ssize_t ll_listxattr(struct dentry *dentry, char *buffer, size_t size);
int ll_removexattr(struct dentry *dentry, const char *name);

/* llite/remote_perm.c */
extern struct kmem_cache *ll_remote_perm_cachep;
extern struct kmem_cache *ll_rmtperm_hash_cachep;

void free_rmtperm_hash(struct hlist_head *hash);
int ll_update_remote_perm(struct inode *inode, struct mdt_remote_perm *perm);
int lustre_check_remote_perm(struct inode *inode, int mask);

/* llite/llite_capa.c */
extern struct timer_list ll_capa_timer;

int ll_capa_thread_start(void);
void ll_capa_thread_stop(void);
void ll_capa_timer_callback(unsigned long unused);

struct obd_capa *ll_add_capa(struct inode *inode, struct obd_capa *ocapa);

void ll_capa_open(struct inode *inode);
void ll_capa_close(struct inode *inode);

struct obd_capa *ll_mdscapa_get(struct inode *inode);
struct obd_capa *ll_osscapa_get(struct inode *inode, __u64 opc);

void ll_truncate_free_capa(struct obd_capa *ocapa);
void ll_clear_inode_capas(struct inode *inode);
void ll_print_capa_stat(struct ll_sb_info *sbi);

/* llite/llite_cl.c */
extern struct lu_device_type vvp_device_type;

/**
 * Common IO arguments for various VFS I/O interfaces.
 */
int cl_sb_init(struct super_block *sb);
int cl_sb_fini(struct super_block *sb);
void ll_io_init(struct cl_io *io, const struct file *file, int write);

void ras_update(struct ll_sb_info *sbi, struct inode *inode,
		struct ll_readahead_state *ras, unsigned long index,
		unsigned hit);
void ll_ra_count_put(struct ll_sb_info *sbi, unsigned long len);
void ll_ra_stats_inc(struct address_space *mapping, enum ra_stat which);

/* llite/llite_rmtacl.c */
#ifdef CONFIG_FS_POSIX_ACL
struct eacl_entry {
	struct list_head	    ee_list;
	pid_t		 ee_key; /* hash key */
	struct lu_fid	 ee_fid;
	int		   ee_type; /* ACL type for ACCESS or DEFAULT */
	ext_acl_xattr_header *ee_acl;
};

obd_valid rce_ops2valid(int ops);
struct rmtacl_ctl_entry *rct_search(struct rmtacl_ctl_table *rct, pid_t key);
int rct_add(struct rmtacl_ctl_table *rct, pid_t key, int ops);
int rct_del(struct rmtacl_ctl_table *rct, pid_t key);
void rct_init(struct rmtacl_ctl_table *rct);
void rct_fini(struct rmtacl_ctl_table *rct);

void ee_free(struct eacl_entry *ee);
int ee_add(struct eacl_table *et, pid_t key, struct lu_fid *fid, int type,
	   ext_acl_xattr_header *header);
struct eacl_entry *et_search_del(struct eacl_table *et, pid_t key,
				 struct lu_fid *fid, int type);
void et_search_free(struct eacl_table *et, pid_t key);
void et_init(struct eacl_table *et);
void et_fini(struct eacl_table *et);
#else
static inline obd_valid rce_ops2valid(int ops)
{
	return 0;
}
#endif

/* statahead.c */

#define LL_SA_RPC_MIN	   2
#define LL_SA_RPC_DEF	   32
#define LL_SA_RPC_MAX	   8192

#define LL_SA_CACHE_BIT	 5
#define LL_SA_CACHE_SIZE	(1 << LL_SA_CACHE_BIT)
#define LL_SA_CACHE_MASK	(LL_SA_CACHE_SIZE - 1)

/* per inode struct, for dir only */
struct ll_statahead_info {
	struct inode	   *sai_inode;
	atomic_t	    sai_refcount;   /* when access this struct, hold
						 * refcount */
	unsigned int	    sai_generation; /* generation for statahead */
	unsigned int	    sai_max;	/* max ahead of lookup */
	__u64		   sai_sent;       /* stat requests sent count */
	__u64		   sai_replied;    /* stat requests which received
						 * reply */
	__u64		   sai_index;      /* index of statahead entry */
	__u64		   sai_index_wait; /* index of entry which is the
						 * caller is waiting for */
	__u64		   sai_hit;	/* hit count */
	__u64		   sai_miss;       /* miss count:
						 * for "ls -al" case, it includes
						 * hidden dentry miss;
						 * for "ls -l" case, it does not
						 * include hidden dentry miss.
						 * "sai_miss_hidden" is used for
						 * the later case.
						 */
	unsigned int	    sai_consecutive_miss; /* consecutive miss */
	unsigned int	    sai_miss_hidden;/* "ls -al", but first dentry
						 * is not a hidden one */
	unsigned int	    sai_skip_hidden;/* skipped hidden dentry count */
	unsigned int	    sai_ls_all:1,   /* "ls -al", do stat-ahead for
						 * hidden entries */
				sai_agl_valid:1;/* AGL is valid for the dir */
	wait_queue_head_t	     sai_waitq;      /* stat-ahead wait queue */
	struct ptlrpc_thread    sai_thread;     /* stat-ahead thread */
	struct ptlrpc_thread    sai_agl_thread; /* AGL thread */
	struct list_head	      sai_entries;    /* entry list */
	struct list_head	      sai_entries_received; /* entries returned */
	struct list_head	      sai_entries_stated;   /* entries stated */
	struct list_head	      sai_entries_agl; /* AGL entries to be sent */
	struct list_head	      sai_cache[LL_SA_CACHE_SIZE];
	spinlock_t		sai_cache_lock[LL_SA_CACHE_SIZE];
	atomic_t		sai_cache_count; /* entry count in cache */
};

int do_statahead_enter(struct inode *dir, struct dentry **dentry,
		       int only_unplug);
void ll_stop_statahead(struct inode *dir, void *key);

static inline int ll_glimpse_size(struct inode *inode)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	int rc;

	down_read(&lli->lli_glimpse_sem);
	rc = cl_glimpse_size(inode);
	lli->lli_glimpse_time = cfs_time_current();
	up_read(&lli->lli_glimpse_sem);
	return rc;
}

static inline void
ll_statahead_mark(struct inode *dir, struct dentry *dentry)
{
	struct ll_inode_info     *lli = ll_i2info(dir);
	struct ll_statahead_info *sai = lli->lli_sai;
	struct ll_dentry_data    *ldd = ll_d2d(dentry);

	/* not the same process, don't mark */
	if (lli->lli_opendir_pid != current_pid())
		return;

	LASSERT(ldd != NULL);
	if (sai != NULL)
		ldd->lld_sa_generation = sai->sai_generation;
}

static inline int
d_need_statahead(struct inode *dir, struct dentry *dentryp)
{
	struct ll_inode_info  *lli;
	struct ll_dentry_data *ldd;

	if (ll_i2sbi(dir)->ll_sa_max == 0)
		return -EAGAIN;

	lli = ll_i2info(dir);
	/* not the same process, don't statahead */
	if (lli->lli_opendir_pid != current_pid())
		return -EAGAIN;

	/* statahead has been stopped */
	if (lli->lli_opendir_key == NULL)
		return -EAGAIN;

	ldd = ll_d2d(dentryp);
	/*
	 * When stats a dentry, the system trigger more than once "revalidate"
	 * or "lookup", for "getattr", for "getxattr", and maybe for others.
	 * Under patchless client mode, the operation intent is not accurate,
	 * which maybe misguide the statahead thread. For example:
	 * The "revalidate" call for "getattr" and "getxattr" of a dentry maybe
	 * have the same operation intent -- "IT_GETATTR".
	 * In fact, one dentry should has only one chance to interact with the
	 * statahead thread, otherwise the statahead windows will be confused.
	 * The solution is as following:
	 * Assign "lld_sa_generation" with "sai_generation" when a dentry
	 * "IT_GETATTR" for the first time, and the subsequent "IT_GETATTR"
	 * will bypass interacting with statahead thread for checking:
	 * "lld_sa_generation == lli_sai->sai_generation"
	 */
	if (ldd && lli->lli_sai &&
	    ldd->lld_sa_generation == lli->lli_sai->sai_generation)
		return -EAGAIN;

	return 1;
}

static inline int
ll_statahead_enter(struct inode *dir, struct dentry **dentryp, int only_unplug)
{
	int ret;

	ret = d_need_statahead(dir, *dentryp);
	if (ret <= 0)
		return ret;

	return do_statahead_enter(dir, dentryp, only_unplug);
}

/* llite ioctl register support routine */
enum llioc_iter {
	LLIOC_CONT = 0,
	LLIOC_STOP
};

#define LLIOC_MAX_CMD	   256

/*
 * Rules to write a callback function:
 *
 * Parameters:
 *  @magic: Dynamic ioctl call routine will feed this value with the pointer
 *      returned to ll_iocontrol_register.  Callback functions should use this
 *      data to check the potential collasion of ioctl cmd. If collasion is
 *      found, callback function should return LLIOC_CONT.
 *  @rcp: The result of ioctl command.
 *
 *  Return values:
 *      If @magic matches the pointer returned by ll_iocontrol_data, the
 *      callback should return LLIOC_STOP; return LLIOC_STOP otherwise.
 */
typedef enum llioc_iter (*llioc_callback_t)(struct inode *inode,
		struct file *file, unsigned int cmd, unsigned long arg,
		void *magic, int *rcp);

/* export functions */
/* Register ioctl block dynamatically for a regular file.
 *
 * @cmd: the array of ioctl command set
 * @count: number of commands in the @cmd
 * @cb: callback function, it will be called if an ioctl command is found to
 *      belong to the command list @cmd.
 *
 * Return value:
 *      A magic pointer will be returned if success;
 *      otherwise, NULL will be returned.
 * */
void *ll_iocontrol_register(llioc_callback_t cb, int count, unsigned int *cmd);
void ll_iocontrol_unregister(void *magic);


/* lclient compat stuff */
#define cl_inode_info ll_inode_info
#define cl_i2info(info) ll_i2info(info)
#define cl_inode_mode(inode) ((inode)->i_mode)
#define cl_i2sbi ll_i2sbi

static inline struct ll_file_data *cl_iattr2fd(struct inode *inode,
					       const struct iattr *attr)
{
	LASSERT(attr->ia_valid & ATTR_FILE);
	return LUSTRE_FPRIVATE(attr->ia_file);
}

static inline void cl_isize_lock(struct inode *inode)
{
	ll_inode_size_lock(inode);
}

static inline void cl_isize_unlock(struct inode *inode)
{
	ll_inode_size_unlock(inode);
}

static inline void cl_isize_write_nolock(struct inode *inode, loff_t kms)
{
	LASSERT(mutex_is_locked(&ll_i2info(inode)->lli_size_mutex));
	i_size_write(inode, kms);
}

static inline void cl_isize_write(struct inode *inode, loff_t kms)
{
	ll_inode_size_lock(inode);
	i_size_write(inode, kms);
	ll_inode_size_unlock(inode);
}

#define cl_isize_read(inode)	     i_size_read(inode)

static inline int cl_merge_lvb(const struct lu_env *env, struct inode *inode)
{
	return ll_merge_lvb(env, inode);
}

#define cl_inode_atime(inode) LTIME_S((inode)->i_atime)
#define cl_inode_ctime(inode) LTIME_S((inode)->i_ctime)
#define cl_inode_mtime(inode) LTIME_S((inode)->i_mtime)

struct obd_capa *cl_capa_lookup(struct inode *inode, enum cl_req_type crt);

int cl_sync_file_range(struct inode *inode, loff_t start, loff_t end,
		       enum cl_fsync_mode mode, int ignore_layout);

/** direct write pages */
struct ll_dio_pages {
	/** page array to be written. we don't support
	 * partial pages except the last one. */
	struct page **ldp_pages;
	/* offset of each page */
	loff_t       *ldp_offsets;
	/** if ldp_offsets is NULL, it means a sequential
	 * pages to be written, then this is the file offset
	 * of the * first page. */
	loff_t	ldp_start_offset;
	/** how many bytes are to be written. */
	size_t	ldp_size;
	/** # of pages in the array. */
	int	   ldp_nr;
};

static inline void cl_stats_tally(struct cl_device *dev, enum cl_req_type crt,
				  int rc)
{
	int opc = (crt == CRT_READ) ? LPROC_LL_OSC_READ :
				      LPROC_LL_OSC_WRITE;

	ll_stats_ops_tally(ll_s2sbi(cl2ccc_dev(dev)->cdv_sb), opc, rc);
}

extern ssize_t ll_direct_rw_pages(const struct lu_env *env, struct cl_io *io,
				  int rw, struct inode *inode,
				  struct ll_dio_pages *pv);

static inline int ll_file_nolock(const struct file *file)
{
	struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	struct inode *inode = file->f_dentry->d_inode;

	LASSERT(fd != NULL);
	return ((fd->fd_flags & LL_FILE_IGNORE_LOCK) ||
		(ll_i2sbi(inode)->ll_flags & LL_SBI_NOLCK));
}

static inline void ll_set_lock_data(struct obd_export *exp, struct inode *inode,
				    struct lookup_intent *it, __u64 *bits)
{
	if (!it->d.lustre.it_lock_set) {
		struct lustre_handle handle;

		/* If this inode is a remote object, it will get two
		 * separate locks in different namespaces, Master MDT,
		 * where the name entry is, will grant LOOKUP lock,
		 * remote MDT, where the object is, will grant
		 * UPDATE|PERM lock. The inode will be attached to both
		 * LOOKUP and PERM locks, so revoking either locks will
		 * case the dcache being cleared */
		if (it->d.lustre.it_remote_lock_mode) {
			handle.cookie = it->d.lustre.it_remote_lock_handle;
			CDEBUG(D_DLMTRACE, "setting l_data to inode %p"
			       "(%lu/%u) for remote lock %#llx\n", inode,
			       inode->i_ino, inode->i_generation,
			       handle.cookie);
			md_set_lock_data(exp, &handle.cookie, inode, NULL);
		}

		handle.cookie = it->d.lustre.it_lock_handle;

		CDEBUG(D_DLMTRACE, "setting l_data to inode %p (%lu/%u)"
		       " for lock %#llx\n", inode, inode->i_ino,
		       inode->i_generation, handle.cookie);

		md_set_lock_data(exp, &handle.cookie, inode,
				 &it->d.lustre.it_lock_bits);
		it->d.lustre.it_lock_set = 1;
	}

	if (bits != NULL)
		*bits = it->d.lustre.it_lock_bits;
}

static inline void ll_lock_dcache(struct inode *inode)
{
	spin_lock(&inode->i_lock);
}

static inline void ll_unlock_dcache(struct inode *inode)
{
	spin_unlock(&inode->i_lock);
}

static inline int d_lustre_invalid(const struct dentry *dentry)
{
	struct ll_dentry_data *lld = ll_d2d(dentry);

	return (lld == NULL) || lld->lld_invalid;
}

static inline void __d_lustre_invalidate(struct dentry *dentry)
{
	struct ll_dentry_data *lld = ll_d2d(dentry);

	if (lld != NULL)
		lld->lld_invalid = 1;
}

/*
 * Mark dentry INVALID, if dentry refcount is zero (this is normally case for
 * ll_md_blocking_ast), unhash this dentry, and let dcache to reclaim it later;
 * else dput() of the last refcount will unhash this dentry and kill it.
 */
static inline void d_lustre_invalidate(struct dentry *dentry, int nested)
{
	CDEBUG(D_DENTRY, "invalidate dentry %.*s (%p) parent %p inode %p "
	       "refc %d\n", dentry->d_name.len, dentry->d_name.name, dentry,
	       dentry->d_parent, dentry->d_inode, d_count(dentry));

	spin_lock_nested(&dentry->d_lock,
			 nested ? DENTRY_D_LOCK_NESTED : DENTRY_D_LOCK_NORMAL);
	__d_lustre_invalidate(dentry);
	if (d_count(dentry) == 0)
		__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
}

static inline void d_lustre_revalidate(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	LASSERT(ll_d2d(dentry) != NULL);
	ll_d2d(dentry)->lld_invalid = 0;
	spin_unlock(&dentry->d_lock);
}

#if LUSTRE_VERSION_CODE < OBD_OCD_VERSION(2, 7, 50, 0)
/* Compatibility for old (1.8) compiled userspace quota code */
struct if_quotactl_18 {
	__u32		   qc_cmd;
	__u32		   qc_type;
	__u32		   qc_id;
	__u32		   qc_stat;
	struct obd_dqinfo       qc_dqinfo;
	struct obd_dqblk	qc_dqblk;
	char		    obd_type[16];
	struct obd_uuid	 obd_uuid;
};
#define LL_IOC_QUOTACTL_18	      _IOWR('f', 162, struct if_quotactl_18 *)
/* End compatibility for old (1.8) compiled userspace quota code */
#else
#warning "remove old LL_IOC_QUOTACTL_18 compatibility code"
#endif /* LUSTRE_VERSION_CODE < OBD_OCD_VERSION(2, 7, 50, 0) */

enum {
	LL_LAYOUT_GEN_NONE  = ((__u32)-2),	/* layout lock was cancelled */
	LL_LAYOUT_GEN_EMPTY = ((__u32)-1)	/* for empty layout */
};

int ll_layout_conf(struct inode *inode, const struct cl_object_conf *conf);
int ll_layout_refresh(struct inode *inode, __u32 *gen);
int ll_layout_restore(struct inode *inode);

int ll_xattr_init(void);
void ll_xattr_fini(void);

#endif /* LLITE_INTERNAL_H */
