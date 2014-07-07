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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __OBD_H
#define __OBD_H

#include <linux/obd.h>

#define IOC_OSC_TYPE	 'h'
#define IOC_OSC_MIN_NR       20
#define IOC_OSC_SET_ACTIVE   _IOWR(IOC_OSC_TYPE, 21, struct obd_device *)
#define IOC_OSC_MAX_NR       50

#define IOC_MDC_TYPE	 'i'
#define IOC_MDC_MIN_NR       20
#define IOC_MDC_MAX_NR       50

#include <lustre/lustre_idl.h>
#include <lustre_lib.h>
#include <linux/libcfs/bitmap.h>
#include <lu_ref.h>
#include <lustre_export.h>
#include <lustre_fid.h>
#include <lustre_fld.h>
#include <lustre_capa.h>

#define MAX_OBD_DEVICES 8192

struct osc_async_rc {
	int     ar_rc;
	int     ar_force_sync;
	__u64   ar_min_xid;
};

struct lov_oinfo {		 /* per-stripe data structure */
	struct ost_id   loi_oi;    /* object ID/Sequence on the target OST */
	int loi_ost_idx;	   /* OST stripe index in lov_tgt_desc->tgts */
	int loi_ost_gen;	   /* generation of this loi_ost_idx */

	unsigned long loi_kms_valid:1;
	__u64 loi_kms;	     /* known minimum size */
	struct ost_lvb loi_lvb;
	struct osc_async_rc     loi_ar;
};

static inline void loi_kms_set(struct lov_oinfo *oinfo, __u64 kms)
{
	oinfo->loi_kms = kms;
	oinfo->loi_kms_valid = 1;
}

static inline void loi_init(struct lov_oinfo *loi)
{
}

struct lov_stripe_md {
	atomic_t     lsm_refc;
	spinlock_t	lsm_lock;
	pid_t	    lsm_lock_owner; /* debugging */

	/* maximum possible file size, might change as OSTs status changes,
	 * e.g. disconnected, deactivated */
	__u64	    lsm_maxbytes;
	struct {
		/* Public members. */
		struct ost_id lw_object_oi; /* lov object id/seq */

		/* LOV-private members start here -- only for use in lov/. */
		__u32 lw_magic;
		__u32 lw_stripe_size;      /* size of the stripe */
		__u32 lw_pattern;	  /* striping pattern (RAID0, RAID1) */
		__u16 lw_stripe_count;  /* number of objects being striped over */
		__u16 lw_layout_gen;       /* generation of the layout */
		char  lw_pool_name[LOV_MAXPOOLNAME]; /* pool name */
	} lsm_wire;

	struct lov_oinfo *lsm_oinfo[0];
};

#define lsm_oi		 lsm_wire.lw_object_oi
#define lsm_magic	lsm_wire.lw_magic
#define lsm_layout_gen   lsm_wire.lw_layout_gen
#define lsm_stripe_size  lsm_wire.lw_stripe_size
#define lsm_pattern      lsm_wire.lw_pattern
#define lsm_stripe_count lsm_wire.lw_stripe_count
#define lsm_pool_name    lsm_wire.lw_pool_name

static inline bool lsm_is_released(struct lov_stripe_md *lsm)
{
	return !!(lsm->lsm_pattern & LOV_PATTERN_F_RELEASED);
}

static inline bool lsm_has_objects(struct lov_stripe_md *lsm)
{
	if (lsm == NULL)
		return false;
	if (lsm_is_released(lsm))
		return false;
	return true;
}

static inline int lov_stripe_md_size(unsigned int stripe_count)
{
	struct lov_stripe_md lsm;

	return sizeof(lsm) + stripe_count * sizeof(lsm.lsm_oinfo[0]);
}

struct obd_info;

typedef int (*obd_enqueue_update_f)(void *cookie, int rc);

/* obd info for a particular level (lov, osc). */
struct obd_info {
	/* Lock policy. It keeps an extent which is specific for a particular
	 * OSC. (e.g. lov_prep_enqueue_set initialises extent of the policy,
	 * and osc_enqueue passes it into ldlm_lock_match & ldlm_cli_enqueue. */
	ldlm_policy_data_t      oi_policy;
	/* Flags used for set request specific flags:
	   - while lock handling, the flags obtained on the enqueue
	   request are set here.
	   - while stats, the flags used for control delay/resend.
	   - while setattr, the flags used for distinguish punch operation
	 */
	__u64		   oi_flags;
	/* Lock handle specific for every OSC lock. */
	struct lustre_handle   *oi_lockh;
	/* lsm data specific for every OSC. */
	struct lov_stripe_md   *oi_md;
	/* obdo data specific for every OSC, if needed at all. */
	struct obdo	    *oi_oa;
	/* statfs data specific for every OSC, if needed at all. */
	struct obd_statfs      *oi_osfs;
	/* An update callback which is called to update some data on upper
	 * level. E.g. it is used for update lsm->lsm_oinfo at every received
	 * request in osc level for enqueue requests. It is also possible to
	 * update some caller data from LOV layer if needed. */
	obd_enqueue_update_f    oi_cb_up;
	/* oss capability, its type is obd_capa in client to avoid copy.
	 * in contrary its type is lustre_capa in OSS. */
	void		   *oi_capa;
	/* transfer jobid from ost_sync() to filter_sync()... */
	char		   *oi_jobid;
};

/* compare all relevant fields. */
static inline int lov_stripe_md_cmp(struct lov_stripe_md *m1,
				    struct lov_stripe_md *m2)
{
	/*
	 * ->lsm_wire contains padding, but it should be zeroed out during
	 * allocation.
	 */
	return memcmp(&m1->lsm_wire, &m2->lsm_wire, sizeof(m1->lsm_wire));
}

static inline int lov_lum_lsm_cmp(struct lov_user_md *lum,
				  struct lov_stripe_md  *lsm)
{
	if (lsm->lsm_magic != lum->lmm_magic)
		return 1;
	if ((lsm->lsm_stripe_count != 0) && (lum->lmm_stripe_count != 0) &&
	    (lsm->lsm_stripe_count != lum->lmm_stripe_count))
		return 2;
	if ((lsm->lsm_stripe_size != 0) && (lum->lmm_stripe_size != 0) &&
	    (lsm->lsm_stripe_size != lum->lmm_stripe_size))
		return 3;
	if ((lsm->lsm_pattern != 0) && (lum->lmm_pattern != 0) &&
	    (lsm->lsm_pattern != lum->lmm_pattern))
		return 4;
	if ((lsm->lsm_magic == LOV_MAGIC_V3) &&
	    (strncmp(lsm->lsm_pool_name,
		     ((struct lov_user_md_v3 *)lum)->lmm_pool_name,
		     LOV_MAXPOOLNAME) != 0))
		return 5;
	return 0;
}

static inline int lov_lum_swab_if_needed(struct lov_user_md_v3 *lumv3,
					 int *lmm_magic,
					 struct lov_user_md *lum)
{
	if (lum && copy_from_user(lumv3, lum,sizeof(struct lov_user_md_v1)))
		return -EFAULT;

	*lmm_magic = lumv3->lmm_magic;

	if (*lmm_magic == __swab32(LOV_USER_MAGIC_V1)) {
		lustre_swab_lov_user_md_v1((struct lov_user_md_v1 *)lumv3);
		*lmm_magic = LOV_USER_MAGIC_V1;
	} else if (*lmm_magic == LOV_USER_MAGIC_V3) {
		if (lum && copy_from_user(lumv3, lum, sizeof(*lumv3)))
			return -EFAULT;
	} else if (*lmm_magic == __swab32(LOV_USER_MAGIC_V3)) {
		if (lum && copy_from_user(lumv3, lum, sizeof(*lumv3)))
			return -EFAULT;
		lustre_swab_lov_user_md_v3(lumv3);
		*lmm_magic = LOV_USER_MAGIC_V3;
	} else if (*lmm_magic != LOV_USER_MAGIC_V1) {
		CDEBUG(D_IOCTL,
		       "bad userland LOV MAGIC: %#08x != %#08x nor %#08x\n",
		       *lmm_magic, LOV_USER_MAGIC_V1, LOV_USER_MAGIC_V3);
		       return -EINVAL;
	}
	return 0;
}

void lov_stripe_lock(struct lov_stripe_md *md);
void lov_stripe_unlock(struct lov_stripe_md *md);

struct obd_type {
	struct list_head typ_chain;
	struct obd_ops *typ_dt_ops;
	struct md_ops *typ_md_ops;
	struct proc_dir_entry *typ_procroot;
	char *typ_name;
	int  typ_refcnt;
	struct lu_device_type *typ_lu;
	spinlock_t obd_type_lock;
};

struct brw_page {
	obd_off  off;
	struct page *pg;
	int count;
	obd_flag flag;
};

/* llog contexts */
enum llog_ctxt_id {
	LLOG_CONFIG_ORIG_CTXT  =  0,
	LLOG_CONFIG_REPL_CTXT,
	LLOG_MDS_OST_ORIG_CTXT,
	LLOG_MDS_OST_REPL_CTXT,
	LLOG_SIZE_ORIG_CTXT,
	LLOG_SIZE_REPL_CTXT,
	LLOG_RD1_ORIG_CTXT,
	LLOG_RD1_REPL_CTXT,
	LLOG_TEST_ORIG_CTXT,
	LLOG_TEST_REPL_CTXT,
	LLOG_LOVEA_ORIG_CTXT,
	LLOG_LOVEA_REPL_CTXT,
	LLOG_CHANGELOG_ORIG_CTXT,	/**< changelog generation on mdd */
	LLOG_CHANGELOG_REPL_CTXT,	/**< changelog access on clients */
	LLOG_CHANGELOG_USER_ORIG_CTXT,	/**< for multiple changelog consumers */
	LLOG_AGENT_ORIG_CTXT,		/**< agent requests generation on cdt */
	LLOG_MAX_CTXTS
};

struct timeout_item {
	enum timeout_event ti_event;
	cfs_time_t	 ti_timeout;
	timeout_cb_t       ti_cb;
	void	      *ti_cb_data;
	struct list_head	 ti_obd_list;
	struct list_head	 ti_chain;
};

#define OSC_MAX_RIF_DEFAULT       8
#define MDS_OSC_MAX_RIF_DEFAULT   50
#define OSC_MAX_RIF_MAX	 256
#define OSC_MAX_DIRTY_DEFAULT  (OSC_MAX_RIF_DEFAULT * 4)
#define OSC_MAX_DIRTY_MB_MAX   2048     /* arbitrary, but < MAX_LONG bytes */
#define OSC_DEFAULT_RESENDS      10

/* possible values for fo_sync_lock_cancel */
enum {
	NEVER_SYNC_ON_CANCEL = 0,
	BLOCKING_SYNC_ON_CANCEL = 1,
	ALWAYS_SYNC_ON_CANCEL = 2,
	NUM_SYNC_ON_CANCEL_STATES
};

#define MDC_MAX_RIF_DEFAULT       8
#define MDC_MAX_RIF_MAX	 512

struct mdc_rpc_lock;
struct obd_import;
struct client_obd {
	struct rw_semaphore  cl_sem;
	struct obd_uuid	  cl_target_uuid;
	struct obd_import       *cl_import; /* ptlrpc connection state */
	int		      cl_conn_count;
	/* max_mds_easize is purely a performance thing so we don't have to
	 * call obd_size_diskmd() all the time. */
	int			 cl_default_mds_easize;
	int			 cl_max_mds_easize;
	int			 cl_default_mds_cookiesize;
	int			 cl_max_mds_cookiesize;

	enum lustre_sec_part     cl_sp_me;
	enum lustre_sec_part     cl_sp_to;
	struct sptlrpc_flavor    cl_flvr_mgc;   /* fixed flavor of mgc->mgs */

	/* the grant values are protected by loi_list_lock below */
	long		     cl_dirty;	 /* all _dirty_ in bytes */
	long		     cl_dirty_max;     /* allowed w/o rpc */
	long		     cl_dirty_transit; /* dirty synchronous */
	long		     cl_avail_grant;   /* bytes of credit for ost */
	long		     cl_lost_grant;    /* lost credits (trunc) */

	/* since we allocate grant by blocks, we don't know how many grant will
	 * be used to add a page into cache. As a solution, we reserve maximum
	 * grant before trying to dirty a page and unreserve the rest.
	 * See osc_{reserve|unreserve}_grant for details. */
	long		 cl_reserved_grant;
	struct list_head	   cl_cache_waiters; /* waiting for cache/grant */
	cfs_time_t	   cl_next_shrink_grant;   /* jiffies */
	struct list_head	   cl_grant_shrink_list;  /* Timeout event list */
	int		  cl_grant_shrink_interval; /* seconds */

	/* A chunk is an optimal size used by osc_extent to determine
	 * the extent size. A chunk is max(PAGE_CACHE_SIZE, OST block size) */
	int		  cl_chunkbits;
	int		  cl_chunk;
	int		  cl_extent_tax; /* extent overhead, by bytes */

	/* keep track of objects that have lois that contain pages which
	 * have been queued for async brw.  this lock also protects the
	 * lists of osc_client_pages that hang off of the loi */
	/*
	 * ->cl_loi_list_lock protects consistency of
	 * ->cl_loi_{ready,read,write}_list. ->ap_make_ready() and
	 * ->ap_completion() call-backs are executed under this lock. As we
	 * cannot guarantee that these call-backs never block on all platforms
	 * (as a matter of fact they do block on Mac OS X), type of
	 * ->cl_loi_list_lock is platform dependent: it's a spin-lock on Linux
	 * and blocking mutex on Mac OS X. (Alternative is to make this lock
	 * blocking everywhere, but we don't want to slow down fast-path of
	 * our main platform.)
	 *
	 * Exact type of ->cl_loi_list_lock is defined in arch/obd.h together
	 * with client_obd_list_{un,}lock() and
	 * client_obd_list_lock_{init,done}() functions.
	 *
	 * NB by Jinshan: though field names are still _loi_, but actually
	 * osc_object{}s are in the list.
	 */
	client_obd_lock_t	cl_loi_list_lock;
	struct list_head	       cl_loi_ready_list;
	struct list_head	       cl_loi_hp_ready_list;
	struct list_head	       cl_loi_write_list;
	struct list_head	       cl_loi_read_list;
	int		      cl_r_in_flight;
	int		      cl_w_in_flight;
	/* just a sum of the loi/lop pending numbers to be exported by /proc */
	atomic_t	     cl_pending_w_pages;
	atomic_t	     cl_pending_r_pages;
	__u32			 cl_max_pages_per_rpc;
	int		      cl_max_rpcs_in_flight;
	struct obd_histogram     cl_read_rpc_hist;
	struct obd_histogram     cl_write_rpc_hist;
	struct obd_histogram     cl_read_page_hist;
	struct obd_histogram     cl_write_page_hist;
	struct obd_histogram     cl_read_offset_hist;
	struct obd_histogram     cl_write_offset_hist;

	/* lru for osc caching pages */
	struct cl_client_cache	*cl_cache;
	struct list_head		 cl_lru_osc; /* member of cl_cache->ccc_lru */
	atomic_t		*cl_lru_left;
	atomic_t		 cl_lru_busy;
	atomic_t		 cl_lru_shrinkers;
	atomic_t		 cl_lru_in_list;
	struct list_head		 cl_lru_list; /* lru page list */
	client_obd_lock_t	 cl_lru_list_lock; /* page list protector */

	/* number of in flight destroy rpcs is limited to max_rpcs_in_flight */
	atomic_t	     cl_destroy_in_flight;
	wait_queue_head_t	      cl_destroy_waitq;

	struct mdc_rpc_lock     *cl_rpc_lock;
	struct mdc_rpc_lock     *cl_close_lock;

	/* mgc datastruct */
	struct mutex		 cl_mgc_mutex;
	struct local_oid_storage *cl_mgc_los;
	struct dt_object	*cl_mgc_configs_dir;
	atomic_t	     cl_mgc_refcount;
	struct obd_export       *cl_mgc_mgsexp;

	/* checksumming for data sent over the network */
	unsigned int	     cl_checksum:1; /* 0 = disabled, 1 = enabled */
	/* supported checksum types that are worked out at connect time */
	__u32		    cl_supp_cksum_types;
	/* checksum algorithm to be used */
	cksum_type_t	     cl_cksum_type;

	/* also protected by the poorly named _loi_list_lock lock above */
	struct osc_async_rc      cl_ar;

	/* used by quotacheck when the servers are older than 2.4 */
	int		      cl_qchk_stat; /* quotacheck stat of the peer */
#define CL_NOT_QUOTACHECKED 1   /* client->cl_qchk_stat init value */
#if LUSTRE_VERSION_CODE >= OBD_OCD_VERSION(2, 7, 50, 0)
#warning "please consider removing quotacheck compatibility code"
#endif

	/* sequence manager */
	struct lu_client_seq    *cl_seq;

	atomic_t	     cl_resends; /* resend count */

	/* ptlrpc work for writeback in ptlrpcd context */
	void		    *cl_writeback_work;
	/* hash tables for osc_quota_info */
	struct cfs_hash	      *cl_quota_hash[MAXQUOTAS];
};
#define obd2cli_tgt(obd) ((char *)(obd)->u.cli.cl_target_uuid.uuid)

struct obd_id_info {
	__u32   idx;
	obd_id  *data;
};

struct echo_client_obd {
	struct obd_export	*ec_exp;   /* the local connection to osc/lov */
	spinlock_t		ec_lock;
	struct list_head	   ec_objects;
	struct list_head	   ec_locks;
	int		  ec_nstripes;
	__u64		ec_unique;
};

struct lov_qos_oss {
	struct obd_uuid     lqo_uuid;       /* ptlrpc's c_remote_uuid */
	struct list_head	  lqo_oss_list;   /* link to lov_qos */
	__u64	       lqo_bavail;     /* total bytes avail on OSS */
	__u64	       lqo_penalty;    /* current penalty */
	__u64	       lqo_penalty_per_obj;/* penalty decrease every obj*/
	time_t	      lqo_used;       /* last used time, seconds */
	__u32	       lqo_ost_count;  /* number of osts on this oss */
};

struct ltd_qos {
	struct lov_qos_oss *ltq_oss;	 /* oss info */
	__u64	       ltq_penalty;     /* current penalty */
	__u64	       ltq_penalty_per_obj; /* penalty decrease every obj*/
	__u64	       ltq_weight;      /* net weighting */
	time_t	      ltq_used;	/* last used time, seconds */
	unsigned int	ltq_usable:1;    /* usable for striping */
};

/* Generic subset of OSTs */
struct ost_pool {
	__u32	      *op_array;      /* array of index of
						   lov_obd->lov_tgts */
	unsigned int	op_count;      /* number of OSTs in the array */
	unsigned int	op_size;       /* allocated size of lp_array */
	struct rw_semaphore op_rw_sem;     /* to protect ost_pool use */
};

/* Round-robin allocator data */
struct lov_qos_rr {
	__u32	       lqr_start_idx;   /* start index of new inode */
	__u32	       lqr_offset_idx;  /* aliasing for start_idx  */
	int		 lqr_start_count; /* reseed counter */
	struct ost_pool     lqr_pool;	/* round-robin optimized list */
	unsigned long       lqr_dirty:1;     /* recalc round-robin list */
};

/* allow statfs data caching for 1 second */
#define OBD_STATFS_CACHE_SECONDS 1

struct lov_statfs_data {
	struct obd_info   lsd_oi;
	struct obd_statfs lsd_statfs;
};
/* Stripe placement optimization */
struct lov_qos {
	struct list_head	  lq_oss_list; /* list of OSSs that targets use */
	struct rw_semaphore lq_rw_sem;
	__u32	       lq_active_oss_count;
	unsigned int	lq_prio_free;   /* priority for free space */
	unsigned int	lq_threshold_rr;/* priority for rr */
	struct lov_qos_rr   lq_rr;	  /* round robin qos data */
	unsigned long       lq_dirty:1,     /* recalc qos data */
			    lq_same_space:1,/* the ost's all have approx.
					       the same space avail */
			    lq_reset:1,     /* zero current penalties */
			    lq_statfs_in_progress:1; /* statfs op in
							progress */
	/* qos statfs data */
	struct lov_statfs_data *lq_statfs_data;
	wait_queue_head_t	 lq_statfs_waitq; /* waitqueue to notify statfs
					      * requests completion */
};

struct lov_tgt_desc {
	struct list_head	  ltd_kill;
	struct obd_uuid     ltd_uuid;
	struct obd_device  *ltd_obd;
	struct obd_export  *ltd_exp;
	struct ltd_qos      ltd_qos;     /* qos info per target */
	__u32	       ltd_gen;
	__u32	       ltd_index;   /* index in lov_obd->tgts */
	unsigned long       ltd_active:1,/* is this target up for requests */
			    ltd_activate:1,/* should  target be activated */
			    ltd_reap:1;  /* should this target be deleted */
};

/* Pool metadata */
#define pool_tgt_size(_p)   _p->pool_obds.op_size
#define pool_tgt_count(_p)  _p->pool_obds.op_count
#define pool_tgt_array(_p)  _p->pool_obds.op_array
#define pool_tgt_rw_sem(_p) _p->pool_obds.op_rw_sem

struct pool_desc {
	char		  pool_name[LOV_MAXPOOLNAME + 1]; /* name of pool */
	struct ost_pool       pool_obds;	      /* pool members */
	atomic_t	  pool_refcount;	  /* pool ref. counter */
	struct lov_qos_rr     pool_rr;		/* round robin qos */
	struct hlist_node      pool_hash;	      /* access by poolname */
	struct list_head	    pool_list;	      /* serial access */
	struct proc_dir_entry *pool_proc_entry;	/* file in /proc */
	struct obd_device    *pool_lobd;	      /* obd of the lov/lod to which
						       * this pool belongs */
};

struct lov_obd {
	struct lov_desc	 desc;
	struct lov_tgt_desc   **lov_tgts;	      /* sparse array */
	struct ost_pool	 lov_packed;	    /* all OSTs in a packed
							  array */
	struct mutex		lov_lock;
	struct obd_connect_data lov_ocd;
	atomic_t	    lov_refcount;
	__u32		   lov_tgt_count;	 /* how many OBD's */
	__u32		   lov_active_tgt_count;  /* how many active */
	__u32		   lov_death_row;/* tgts scheduled to be deleted */
	__u32		   lov_tgt_size;   /* size of tgts array */
	int		     lov_connects;
	int		     lov_pool_count;
	struct cfs_hash	     *lov_pools_hash_body; /* used for key access */
	struct list_head	      lov_pool_list; /* used for sequential access */
	struct proc_dir_entry   *lov_pool_proc_entry;
	enum lustre_sec_part    lov_sp_me;

	/* Cached LRU pages from upper layer */
	void		       *lov_cache;

	struct rw_semaphore     lov_notify_lock;
};

struct lmv_tgt_desc {
	struct obd_uuid		ltd_uuid;
	struct obd_export	*ltd_exp;
	int			ltd_idx;
	struct mutex		ltd_fid_mutex;
	unsigned long		ltd_active:1; /* target up for requests */
};

enum placement_policy {
	PLACEMENT_CHAR_POLICY   = 0,
	PLACEMENT_NID_POLICY    = 1,
	PLACEMENT_INVAL_POLICY  = 2,
	PLACEMENT_MAX_POLICY
};

typedef enum placement_policy placement_policy_t;

struct lmv_obd {
	int			refcount;
	struct lu_client_fld	lmv_fld;
	spinlock_t		lmv_lock;
	placement_policy_t	lmv_placement;
	struct lmv_desc		desc;
	struct obd_uuid		cluuid;
	struct obd_export	*exp;

	struct mutex		init_mutex;
	int			connected;
	int			max_easize;
	int			max_def_easize;
	int			max_cookiesize;
	int			max_def_cookiesize;
	int			server_timeout;

	int			tgts_size; /* size of tgts array */
	struct lmv_tgt_desc	**tgts;

	struct obd_connect_data	conn_data;
};

struct niobuf_local {
	__u64		lnb_file_offset;
	__u32		lnb_page_offset;
	__u32		len;
	__u32		flags;
	struct page	*page;
	struct dentry	*dentry;
	int		lnb_grant_used;
	int		rc;
};

#define LUSTRE_FLD_NAME	 "fld"
#define LUSTRE_SEQ_NAME	 "seq"

#define LUSTRE_MDD_NAME	 "mdd"
#define LUSTRE_OSD_LDISKFS_NAME	"osd-ldiskfs"
#define LUSTRE_OSD_ZFS_NAME     "osd-zfs"
#define LUSTRE_VVP_NAME	 "vvp"
#define LUSTRE_LMV_NAME	 "lmv"
#define LUSTRE_SLP_NAME	 "slp"
#define LUSTRE_LOD_NAME		"lod"
#define LUSTRE_OSP_NAME		"osp"
#define LUSTRE_LWP_NAME		"lwp"

/* obd device type names */
 /* FIXME all the references to LUSTRE_MDS_NAME should be swapped with LUSTRE_MDT_NAME */
#define LUSTRE_MDS_NAME	 "mds"
#define LUSTRE_MDT_NAME	 "mdt"
#define LUSTRE_MDC_NAME	 "mdc"
#define LUSTRE_OSS_NAME	 "ost"       /* FIXME change name to oss */
#define LUSTRE_OST_NAME	 "obdfilter" /* FIXME change name to ost */
#define LUSTRE_OSC_NAME	 "osc"
#define LUSTRE_LOV_NAME	 "lov"
#define LUSTRE_MGS_NAME	 "mgs"
#define LUSTRE_MGC_NAME	 "mgc"

#define LUSTRE_ECHO_NAME	"obdecho"
#define LUSTRE_ECHO_CLIENT_NAME "echo_client"
#define LUSTRE_QMT_NAME	 "qmt"

/* Constant obd names (post-rename) */
#define LUSTRE_MDS_OBDNAME "MDS"
#define LUSTRE_OSS_OBDNAME "OSS"
#define LUSTRE_MGS_OBDNAME "MGS"
#define LUSTRE_MGC_OBDNAME "MGC"

static inline int is_osp_on_mdt(char *name)
{
	char   *ptr;

	ptr = strrchr(name, '-');
	if (ptr == NULL) {
		CERROR("%s is not a obdname\n", name);
		return 0;
	}

	/* 1.8 OSC/OSP name on MDT is fsname-OSTxxxx-osc */
	if (strncmp(ptr + 1, "osc", 3) == 0)
		return 1;

	if (strncmp(ptr + 1, "MDT", 3) != 0)
		return 0;

	while (*(--ptr) != '-' && ptr != name);

	if (ptr == name)
		return 0;

	if (strncmp(ptr + 1, LUSTRE_OSP_NAME, strlen(LUSTRE_OSP_NAME)) != 0 &&
	    strncmp(ptr + 1, LUSTRE_OSC_NAME, strlen(LUSTRE_OSC_NAME)) != 0)
		return 0;

	return 1;
}

/* Don't conflict with on-wire flags OBD_BRW_WRITE, etc */
#define N_LOCAL_TEMP_PAGE 0x10000000

struct obd_trans_info {
	__u64		    oti_transno;
	__u64		    oti_xid;
	/* Only used on the server side for tracking acks. */
	struct oti_req_ack_lock {
		struct lustre_handle lock;
		__u32		mode;
	}			oti_ack_locks[4];
	void		    *oti_handle;
	struct llog_cookie       oti_onecookie;
	struct llog_cookie      *oti_logcookies;
	int		      oti_numcookies;
	/** synchronous write is needed */
	unsigned long		 oti_sync_write:1;

	/* initial thread handling transaction */
	struct ptlrpc_thread *   oti_thread;
	__u32		    oti_conn_cnt;
	/** VBR: versions */
	__u64		    oti_pre_version;
	/** JobID */
	char		    *oti_jobid;

	struct obd_uuid	 *oti_ost_uuid;
};

static inline void oti_init(struct obd_trans_info *oti,
			    struct ptlrpc_request *req)
{
	if (oti == NULL)
		return;
	memset(oti, 0, sizeof(*oti));

	if (req == NULL)
		return;

	oti->oti_xid = req->rq_xid;
	/** VBR: take versions from request */
	if (req->rq_reqmsg != NULL &&
	    lustre_msg_get_flags(req->rq_reqmsg) & MSG_REPLAY) {
		__u64 *pre_version = lustre_msg_get_versions(req->rq_reqmsg);
		oti->oti_pre_version = pre_version ? pre_version[0] : 0;
		oti->oti_transno = lustre_msg_get_transno(req->rq_reqmsg);
	}

	/** called from mds_create_objects */
	if (req->rq_repmsg != NULL)
		oti->oti_transno = lustre_msg_get_transno(req->rq_repmsg);
	oti->oti_thread = req->rq_svc_thread;
	if (req->rq_reqmsg != NULL)
		oti->oti_conn_cnt = lustre_msg_get_conn_cnt(req->rq_reqmsg);
}

static inline void oti_alloc_cookies(struct obd_trans_info *oti,int num_cookies)
{
	if (!oti)
		return;

	if (num_cookies == 1)
		oti->oti_logcookies = &oti->oti_onecookie;
	else
		OBD_ALLOC_LARGE(oti->oti_logcookies,
				num_cookies * sizeof(oti->oti_onecookie));

	oti->oti_numcookies = num_cookies;
}

static inline void oti_free_cookies(struct obd_trans_info *oti)
{
	if (!oti || !oti->oti_logcookies)
		return;

	if (oti->oti_logcookies == &oti->oti_onecookie)
		LASSERT(oti->oti_numcookies == 1);
	else
		OBD_FREE_LARGE(oti->oti_logcookies,
			       oti->oti_numcookies*sizeof(oti->oti_onecookie));
	oti->oti_logcookies = NULL;
	oti->oti_numcookies = 0;
}

/*
 * Events signalled through obd_notify() upcall-chain.
 */
enum obd_notify_event {
	/* target added */
	OBD_NOTIFY_CREATE,
	/* Device connect start */
	OBD_NOTIFY_CONNECT,
	/* Device activated */
	OBD_NOTIFY_ACTIVE,
	/* Device deactivated */
	OBD_NOTIFY_INACTIVE,
	/* Device disconnected */
	OBD_NOTIFY_DISCON,
	/* Connect data for import were changed */
	OBD_NOTIFY_OCD,
	/* Sync request */
	OBD_NOTIFY_SYNC_NONBLOCK,
	OBD_NOTIFY_SYNC,
	/* Configuration event */
	OBD_NOTIFY_CONFIG,
	/* Administratively deactivate/activate event */
	OBD_NOTIFY_DEACTIVATE,
	OBD_NOTIFY_ACTIVATE
};

/*
 * Data structure used to pass obd_notify()-event to non-obd listeners (llite
 * and liblustre being main examples).
 */
struct obd_notify_upcall {
	int (*onu_upcall)(struct obd_device *host, struct obd_device *watched,
			  enum obd_notify_event ev, void *owner, void *data);
	/* Opaque datum supplied by upper layer listener */
	void *onu_owner;
};

struct target_recovery_data {
	svc_handler_t		trd_recovery_handler;
	pid_t			trd_processing_task;
	struct completion	trd_starting;
	struct completion	trd_finishing;
};

struct obd_llog_group {
	int		olg_seq;
	struct llog_ctxt  *olg_ctxts[LLOG_MAX_CTXTS];
	wait_queue_head_t	olg_waitq;
	spinlock_t	   olg_lock;
	struct mutex	   olg_cat_processing;
};

/* corresponds to one of the obd's */
#define OBD_DEVICE_MAGIC	0XAB5CD6EF
#define OBD_DEV_BY_DEVNAME      0xffffd0de

struct obd_device {
	struct obd_type	*obd_type;
	__u32		   obd_magic;

	/* common and UUID name of this device */
	char		    obd_name[MAX_OBD_NAME];
	struct obd_uuid	 obd_uuid;

	struct lu_device       *obd_lu_dev;

	int		     obd_minor;
	/* bitfield modification is protected by obd_dev_lock */
	unsigned long obd_attached:1,      /* finished attach */
		      obd_set_up:1,	/* finished setup */
		      obd_recovering:1,    /* there are recoverable clients */
		      obd_abort_recovery:1,/* recovery expired */
		      obd_version_recov:1, /* obd uses version checking */
		      obd_replayable:1,    /* recovery is enabled; inform clients */
		      obd_no_transno:1,    /* no committed-transno notification */
		      obd_no_recov:1,      /* fail instead of retry messages */
		      obd_stopping:1,      /* started cleanup */
		      obd_starting:1,      /* started setup */
		      obd_force:1,	 /* cleanup with > 0 obd refcount */
		      obd_fail:1,	  /* cleanup with failover */
		      obd_async_recov:1,   /* allow asynchronous orphan cleanup */
		      obd_no_conn:1,       /* deny new connections */
		      obd_inactive:1,      /* device active/inactive
					   * (for /proc/status only!!) */
		      obd_no_ir:1,	 /* no imperative recovery. */
		      obd_process_conf:1;  /* device is processing mgs config */
	/* use separate field as it is set in interrupt to don't mess with
	 * protection of other bits using _bh lock */
	unsigned long obd_recovery_expired:1;
	/* uuid-export hash body */
	struct cfs_hash	     *obd_uuid_hash;
	/* nid-export hash body */
	struct cfs_hash	     *obd_nid_hash;
	/* nid stats body */
	struct cfs_hash	     *obd_nid_stats_hash;
	struct list_head	      obd_nid_stats;
	atomic_t	    obd_refcount;
	wait_queue_head_t	     obd_refcount_waitq;
	struct list_head	      obd_exports;
	struct list_head	      obd_unlinked_exports;
	struct list_head	      obd_delayed_exports;
	int		     obd_num_exports;
	spinlock_t		obd_nid_lock;
	struct ldlm_namespace  *obd_namespace;
	struct ptlrpc_client	obd_ldlm_client; /* XXX OST/MDS only */
	/* a spinlock is OK for what we do now, may need a semaphore later */
	spinlock_t		obd_dev_lock; /* protect OBD bitfield above */
	struct mutex		obd_dev_mutex;
	__u64			obd_last_committed;
	struct fsfilt_operations *obd_fsops;
	spinlock_t		obd_osfs_lock;
	struct obd_statfs	obd_osfs;       /* locked by obd_osfs_lock */
	__u64			obd_osfs_age;
	struct lvfs_run_ctxt	obd_lvfs_ctxt;
	struct obd_llog_group	obd_olg;	/* default llog group */
	struct obd_device	*obd_observer;
	struct rw_semaphore	obd_observer_link_sem;
	struct obd_notify_upcall obd_upcall;
	struct obd_export       *obd_self_export;
	/* list of exports in LRU order, for ping evictor, with obd_dev_lock */
	struct list_head	      obd_exports_timed;
	time_t		  obd_eviction_timer; /* for ping evictor */

	int			      obd_max_recoverable_clients;
	atomic_t		     obd_connected_clients;
	int			      obd_stale_clients;
	int			      obd_delayed_clients;
	/* this lock protects all recovery list_heads, timer and
	 * obd_next_recovery_transno value */
	spinlock_t			 obd_recovery_task_lock;
	__u64			    obd_next_recovery_transno;
	int			      obd_replayed_requests;
	int			      obd_requests_queued_for_recovery;
	wait_queue_head_t		      obd_next_transno_waitq;
	/* protected by obd_recovery_task_lock */
	struct timer_list	      obd_recovery_timer;
	time_t			   obd_recovery_start; /* seconds */
	time_t			   obd_recovery_end; /* seconds, for lprocfs_status */
	int			      obd_recovery_time_hard;
	int			      obd_recovery_timeout;
	int			      obd_recovery_ir_factor;

	/* new recovery stuff from CMD2 */
	struct target_recovery_data      obd_recovery_data;
	int			      obd_replayed_locks;
	atomic_t		     obd_req_replay_clients;
	atomic_t		     obd_lock_replay_clients;
	/* all lists are protected by obd_recovery_task_lock */
	struct list_head		       obd_req_replay_queue;
	struct list_head		       obd_lock_replay_queue;
	struct list_head		       obd_final_req_queue;
	int			      obd_recovery_stage;

	union {
		struct client_obd cli;
		struct echo_client_obd echo_client;
		struct lov_obd lov;
		struct lmv_obd lmv;
	} u;
	/* Fields used by LProcFS */
	unsigned int	   obd_cntr_base;
	struct lprocfs_stats  *obd_stats;

	unsigned int	   md_cntr_base;
	struct lprocfs_stats  *md_stats;

	struct proc_dir_entry  *obd_proc_entry;
	void		  *obd_proc_private; /* type private PDEs */
	struct proc_dir_entry  *obd_proc_exports_entry;
	struct proc_dir_entry  *obd_svc_procroot;
	struct lprocfs_stats  *obd_svc_stats;
	atomic_t	   obd_evict_inprogress;
	wait_queue_head_t	    obd_evict_inprogress_waitq;
	struct list_head	     obd_evict_list; /* protected with pet_lock */

	/**
	 * Ldlm pool part. Save last calculated SLV and Limit.
	 */
	rwlock_t		obd_pool_lock;
	int		    obd_pool_limit;
	__u64		  obd_pool_slv;

	/**
	 * A list of outstanding class_incref()'s against this obd. For
	 * debugging.
	 */
	struct lu_ref	  obd_reference;

	int		       obd_conn_inprogress;
};

#define OBD_LLOG_FL_SENDNOW     0x0001
#define OBD_LLOG_FL_EXIT	0x0002

enum obd_cleanup_stage {
/* Special case hack for MDS LOVs */
	OBD_CLEANUP_EARLY,
/* can be directly mapped to .ldto_device_fini() */
	OBD_CLEANUP_EXPORTS,
};

/* get/set_info keys */
#define KEY_ASYNC	       "async"
#define KEY_BLOCKSIZE_BITS      "blocksize_bits"
#define KEY_BLOCKSIZE	   "blocksize"
#define KEY_CAPA_KEY	    "capa_key"
#define KEY_CHANGELOG_CLEAR     "changelog_clear"
#define KEY_FID2PATH	    "fid2path"
#define KEY_CHECKSUM	    "checksum"
#define KEY_CLEAR_FS	    "clear_fs"
#define KEY_CONN_DATA	   "conn_data"
#define KEY_EVICT_BY_NID	"evict_by_nid"
#define KEY_FIEMAP	      "fiemap"
#define KEY_FLUSH_CTX	   "flush_ctx"
#define KEY_GRANT_SHRINK	"grant_shrink"
#define KEY_HSM_COPYTOOL_SEND   "hsm_send"
#define KEY_INIT_RECOV_BACKUP   "init_recov_bk"
#define KEY_INIT_RECOV	  "initial_recov"
#define KEY_INTERMDS	    "inter_mds"
#define KEY_LAST_ID	     "last_id"
#define KEY_LAST_FID		"last_fid"
#define KEY_LOCK_TO_STRIPE      "lock_to_stripe"
#define KEY_LOVDESC	     "lovdesc"
#define KEY_LOV_IDX	     "lov_idx"
#define KEY_MAX_EASIZE		"max_easize"
#define KEY_DEFAULT_EASIZE	"default_easize"
#define KEY_MAX_COOKIESIZE	"max_cookiesize"
#define KEY_DEFAULT_COOKIESIZE	"default_cookiesize"
#define KEY_MDS_CONN	    "mds_conn"
#define KEY_MGSSEC	      "mgssec"
#define KEY_NEXT_ID	     "next_id"
#define KEY_READ_ONLY	   "read-only"
#define KEY_REGISTER_TARGET     "register_target"
#define KEY_SET_FS	      "set_fs"
#define KEY_TGT_COUNT	   "tgt_count"
/*      KEY_SET_INFO in lustre_idl.h */
#define KEY_SPTLRPC_CONF	"sptlrpc_conf"
#define KEY_CONNECT_FLAG	"connect_flags"
#define KEY_SYNC_LOCK_CANCEL    "sync_lock_cancel"

#define KEY_CACHE_SET		"cache_set"
#define KEY_CACHE_LRU_SHRINK	"cache_lru_shrink"
#define KEY_CHANGELOG_INDEX	"changelog_index"

struct lu_context;

/* /!\ must be coherent with include/linux/namei.h on patched kernel */
#define IT_OPEN     (1 << 0)
#define IT_CREAT    (1 << 1)
#define IT_READDIR  (1 << 2)
#define IT_GETATTR  (1 << 3)
#define IT_LOOKUP   (1 << 4)
#define IT_UNLINK   (1 << 5)
#define IT_TRUNC    (1 << 6)
#define IT_GETXATTR (1 << 7)
#define IT_EXEC     (1 << 8)
#define IT_PIN      (1 << 9)
#define IT_LAYOUT   (1 << 10)
#define IT_QUOTA_DQACQ (1 << 11)
#define IT_QUOTA_CONN  (1 << 12)
#define IT_SETXATTR (1 << 13)

static inline int it_to_lock_mode(struct lookup_intent *it)
{
	/* CREAT needs to be tested before open (both could be set) */
	if (it->it_op & IT_CREAT)
		return LCK_CW;
	else if (it->it_op & (IT_READDIR | IT_GETATTR | IT_OPEN | IT_LOOKUP |
			      IT_LAYOUT))
		return LCK_CR;
	else if (it->it_op &  IT_GETXATTR)
		return LCK_PR;
	else if (it->it_op &  IT_SETXATTR)
		return LCK_PW;

	LASSERTF(0, "Invalid it_op: %d\n", it->it_op);
	return -EINVAL;
}

struct md_op_data {
	struct lu_fid	   op_fid1; /* operation fid1 (usually parent) */
	struct lu_fid	   op_fid2; /* operation fid2 (usually child) */
	struct lu_fid	   op_fid3; /* 2 extra fids to find conflicting */
	struct lu_fid	   op_fid4; /* to the operation locks. */
	mdsno_t		 op_mds;  /* what mds server open will go to */
	struct lustre_handle    op_handle;
	obd_time		op_mod_time;
	const char	     *op_name;
	int		     op_namelen;
	__u32		   op_mode;
	struct lmv_stripe_md   *op_mea1;
	struct lmv_stripe_md   *op_mea2;
	__u32		   op_suppgids[2];
	__u32		   op_fsuid;
	__u32		   op_fsgid;
	cfs_cap_t	       op_cap;
	void		   *op_data;

	/* iattr fields and blocks. */
	struct iattr	    op_attr;
	unsigned int	    op_attr_flags;
	__u64		   op_valid;
	loff_t		  op_attr_blocks;

	/* Size-on-MDS epoch and flags. */
	__u64		   op_ioepoch;
	__u32		   op_flags;

	/* Capa fields */
	struct obd_capa	*op_capa1;
	struct obd_capa	*op_capa2;

	/* Various operation flags. */
	enum mds_op_bias        op_bias;

	/* Operation type */
	__u32		   op_opc;

	/* Used by readdir */
	__u64		   op_offset;

	/* Used by readdir */
	__u32		   op_npages;

	/* used to transfer info between the stacks of MD client
	 * see enum op_cli_flags */
	__u32			op_cli_flags;

	/* File object data version for HSM release, on client */
	__u64			op_data_version;
	struct lustre_handle	op_lease_handle;
};

enum op_cli_flags {
	CLI_SET_MEA	= 1 << 0,
	CLI_RM_ENTRY	= 1 << 1,
};

struct md_enqueue_info;
/* metadata stat-ahead */
typedef int (* md_enqueue_cb_t)(struct ptlrpc_request *req,
				struct md_enqueue_info *minfo,
				int rc);

struct md_enqueue_info {
	struct md_op_data       mi_data;
	struct lookup_intent    mi_it;
	struct lustre_handle    mi_lockh;
	struct inode	   *mi_dir;
	md_enqueue_cb_t	 mi_cb;
	__u64		   mi_cbdata;
	unsigned int	    mi_generation;
};

struct obd_ops {
	struct module *o_owner;
	int (*o_iocontrol)(unsigned int cmd, struct obd_export *exp, int len,
			   void *karg, void *uarg);
	int (*o_get_info)(const struct lu_env *env, struct obd_export *,
			  __u32 keylen, void *key, __u32 *vallen, void *val,
			  struct lov_stripe_md *lsm);
	int (*o_set_info_async)(const struct lu_env *, struct obd_export *,
				__u32 keylen, void *key,
				__u32 vallen, void *val,
				struct ptlrpc_request_set *set);
	int (*o_attach)(struct obd_device *dev, obd_count len, void *data);
	int (*o_detach)(struct obd_device *dev);
	int (*o_setup) (struct obd_device *dev, struct lustre_cfg *cfg);
	int (*o_precleanup)(struct obd_device *dev,
			    enum obd_cleanup_stage cleanup_stage);
	int (*o_cleanup)(struct obd_device *dev);
	int (*o_process_config)(struct obd_device *dev, obd_count len,
				void *data);
	int (*o_postrecov)(struct obd_device *dev);
	int (*o_add_conn)(struct obd_import *imp, struct obd_uuid *uuid,
			  int priority);
	int (*o_del_conn)(struct obd_import *imp, struct obd_uuid *uuid);
	/* connect to the target device with given connection
	 * data. @ocd->ocd_connect_flags is modified to reflect flags actually
	 * granted by the target, which are guaranteed to be a subset of flags
	 * asked for. If @ocd == NULL, use default parameters. */
	int (*o_connect)(const struct lu_env *env,
			 struct obd_export **exp, struct obd_device *src,
			 struct obd_uuid *cluuid, struct obd_connect_data *ocd,
			 void *localdata);
	int (*o_reconnect)(const struct lu_env *env,
			   struct obd_export *exp, struct obd_device *src,
			   struct obd_uuid *cluuid,
			   struct obd_connect_data *ocd,
			   void *localdata);
	int (*o_disconnect)(struct obd_export *exp);

	/* Initialize/finalize fids infrastructure. */
	int (*o_fid_init)(struct obd_device *obd,
			  struct obd_export *exp, enum lu_cli_type type);
	int (*o_fid_fini)(struct obd_device *obd);

	/* Allocate new fid according to passed @hint. */
	int (*o_fid_alloc)(struct obd_export *exp, struct lu_fid *fid,
			   struct md_op_data *op_data);

	/*
	 * Object with @fid is getting deleted, we may want to do something
	 * about this.
	 */
	int (*o_statfs)(const struct lu_env *, struct obd_export *exp,
			struct obd_statfs *osfs, __u64 max_age, __u32 flags);
	int (*o_statfs_async)(struct obd_export *exp, struct obd_info *oinfo,
			      __u64 max_age, struct ptlrpc_request_set *set);
	int (*o_packmd)(struct obd_export *exp, struct lov_mds_md **disk_tgt,
			struct lov_stripe_md *mem_src);
	int (*o_unpackmd)(struct obd_export *exp,struct lov_stripe_md **mem_tgt,
			  struct lov_mds_md *disk_src, int disk_len);
	int (*o_preallocate)(struct lustre_handle *, obd_count *req,
			     obd_id *ids);
	/* FIXME: add fid capability support for create & destroy! */
	int (*o_precreate)(struct obd_export *exp);
	int (*o_create)(const struct lu_env *env, struct obd_export *exp,
			struct obdo *oa, struct lov_stripe_md **ea,
			struct obd_trans_info *oti);
	int (*o_create_async)(struct obd_export *exp,  struct obd_info *oinfo,
			      struct lov_stripe_md **ea,
			      struct obd_trans_info *oti);
	int (*o_destroy)(const struct lu_env *env, struct obd_export *exp,
			 struct obdo *oa, struct lov_stripe_md *ea,
			 struct obd_trans_info *oti, struct obd_export *md_exp,
			 void *capa);
	int (*o_setattr)(const struct lu_env *, struct obd_export *exp,
			 struct obd_info *oinfo, struct obd_trans_info *oti);
	int (*o_setattr_async)(struct obd_export *exp, struct obd_info *oinfo,
			       struct obd_trans_info *oti,
			       struct ptlrpc_request_set *rqset);
	int (*o_getattr)(const struct lu_env *env, struct obd_export *exp,
			 struct obd_info *oinfo);
	int (*o_getattr_async)(struct obd_export *exp, struct obd_info *oinfo,
			       struct ptlrpc_request_set *set);
	int (*o_brw)(int rw, struct obd_export *exp, struct obd_info *oinfo,
		     obd_count oa_bufs, struct brw_page *pgarr,
		     struct obd_trans_info *oti);
	int (*o_merge_lvb)(struct obd_export *exp, struct lov_stripe_md *lsm,
			   struct ost_lvb *lvb, int kms_only);
	int (*o_adjust_kms)(struct obd_export *exp, struct lov_stripe_md *lsm,
			    obd_off size, int shrink);
	int (*o_punch)(const struct lu_env *, struct obd_export *exp,
		       struct obd_info *oinfo, struct obd_trans_info *oti,
		       struct ptlrpc_request_set *rqset);
	int (*o_sync)(const struct lu_env *env, struct obd_export *exp,
		      struct obd_info *oinfo, obd_size start, obd_size end,
		      struct ptlrpc_request_set *set);
	int (*o_migrate)(struct lustre_handle *conn, struct lov_stripe_md *dst,
			 struct lov_stripe_md *src, obd_size start,
			 obd_size end, struct obd_trans_info *oti);
	int (*o_copy)(struct lustre_handle *dstconn, struct lov_stripe_md *dst,
		      struct lustre_handle *srconn, struct lov_stripe_md *src,
		      obd_size start, obd_size end, struct obd_trans_info *);
	int (*o_iterate)(struct lustre_handle *conn,
			 int (*)(obd_id, obd_seq, void *),
			 obd_id *startid, obd_seq seq, void *data);
	int (*o_preprw)(const struct lu_env *env, int cmd,
			struct obd_export *exp, struct obdo *oa, int objcount,
			struct obd_ioobj *obj, struct niobuf_remote *remote,
			int *nr_pages, struct niobuf_local *local,
			struct obd_trans_info *oti, struct lustre_capa *capa);
	int (*o_commitrw)(const struct lu_env *env, int cmd,
			  struct obd_export *exp, struct obdo *oa,
			  int objcount, struct obd_ioobj *obj,
			  struct niobuf_remote *remote, int pages,
			  struct niobuf_local *local,
			  struct obd_trans_info *oti, int rc);
	int (*o_enqueue)(struct obd_export *, struct obd_info *oinfo,
			 struct ldlm_enqueue_info *einfo,
			 struct ptlrpc_request_set *rqset);
	int (*o_change_cbdata)(struct obd_export *, struct lov_stripe_md *,
			       ldlm_iterator_t it, void *data);
	int (*o_find_cbdata)(struct obd_export *, struct lov_stripe_md *,
			     ldlm_iterator_t it, void *data);
	int (*o_cancel)(struct obd_export *, struct lov_stripe_md *md,
			__u32 mode, struct lustre_handle *);
	int (*o_cancel_unused)(struct obd_export *, struct lov_stripe_md *,
			       ldlm_cancel_flags_t flags, void *opaque);
	int (*o_init_export)(struct obd_export *exp);
	int (*o_destroy_export)(struct obd_export *exp);
	int (*o_extent_calc)(struct obd_export *, struct lov_stripe_md *,
			     int cmd, obd_off *);

	/* llog related obd_methods */
	int (*o_llog_init)(struct obd_device *obd, struct obd_llog_group *grp,
			   struct obd_device *disk_obd, int *idx);
	int (*o_llog_finish)(struct obd_device *obd, int count);
	int (*o_llog_connect)(struct obd_export *, struct llogd_conn_body *);

	/* metadata-only methods */
	int (*o_pin)(struct obd_export *, const struct lu_fid *fid,
		     struct obd_capa *, struct obd_client_handle *, int flag);
	int (*o_unpin)(struct obd_export *, struct obd_client_handle *, int);

	int (*o_import_event)(struct obd_device *, struct obd_import *,
			      enum obd_import_event);

	int (*o_notify)(struct obd_device *obd, struct obd_device *watched,
			enum obd_notify_event ev, void *data);

	int (*o_health_check)(const struct lu_env *env, struct obd_device *);
	struct obd_uuid *(*o_get_uuid) (struct obd_export *exp);

	/* quota methods */
	int (*o_quotacheck)(struct obd_device *, struct obd_export *,
			    struct obd_quotactl *);
	int (*o_quotactl)(struct obd_device *, struct obd_export *,
			  struct obd_quotactl *);

	int (*o_ping)(const struct lu_env *, struct obd_export *exp);

	/* pools methods */
	int (*o_pool_new)(struct obd_device *obd, char *poolname);
	int (*o_pool_del)(struct obd_device *obd, char *poolname);
	int (*o_pool_add)(struct obd_device *obd, char *poolname,
			  char *ostname);
	int (*o_pool_rem)(struct obd_device *obd, char *poolname,
			  char *ostname);
	void (*o_getref)(struct obd_device *obd);
	void (*o_putref)(struct obd_device *obd);
	/*
	 * NOTE: If adding ops, add another LPROCFS_OBD_OP_INIT() line
	 * to lprocfs_alloc_obd_stats() in obdclass/lprocfs_status.c.
	 * Also, add a wrapper function in include/linux/obd_class.h. */
};

enum {
	LUSTRE_OPC_MKDIR    = (1 << 0),
	LUSTRE_OPC_SYMLINK  = (1 << 1),
	LUSTRE_OPC_MKNOD    = (1 << 2),
	LUSTRE_OPC_CREATE   = (1 << 3),
	LUSTRE_OPC_ANY      = (1 << 4)
};

/* lmv structures */
#define MEA_MAGIC_LAST_CHAR      0xb2221ca1
#define MEA_MAGIC_ALL_CHARS      0xb222a11c
#define MEA_MAGIC_HASH_SEGMENT   0xb222a11b

#define MAX_HASH_SIZE_32	 0x7fffffffUL
#define MAX_HASH_SIZE	    0x7fffffffffffffffULL
#define MAX_HASH_HIGHEST_BIT     0x1000000000000000ULL

struct lustre_md {
	struct mdt_body	 *body;
	struct lov_stripe_md    *lsm;
	struct lmv_stripe_md    *mea;
#ifdef CONFIG_FS_POSIX_ACL
	struct posix_acl	*posix_acl;
#endif
	struct mdt_remote_perm  *remote_perm;
	struct obd_capa	 *mds_capa;
	struct obd_capa	 *oss_capa;
};

struct md_open_data {
	struct obd_client_handle *mod_och;
	struct ptlrpc_request    *mod_open_req;
	struct ptlrpc_request    *mod_close_req;
	atomic_t		  mod_refcount;
	bool			  mod_is_create;
};

struct lookup_intent;

struct md_ops {
	int (*m_getstatus)(struct obd_export *, struct lu_fid *,
			   struct obd_capa **);
	int (*m_null_inode)(struct obd_export *, const struct lu_fid *);
	int (*m_find_cbdata)(struct obd_export *, const struct lu_fid *,
			     ldlm_iterator_t, void *);
	int (*m_close)(struct obd_export *, struct md_op_data *,
		       struct md_open_data *, struct ptlrpc_request **);
	int (*m_create)(struct obd_export *, struct md_op_data *,
			const void *, int, int, __u32, __u32, cfs_cap_t,
			__u64, struct ptlrpc_request **);
	int (*m_done_writing)(struct obd_export *, struct md_op_data  *,
			      struct md_open_data *);
	int (*m_enqueue)(struct obd_export *, struct ldlm_enqueue_info *,
			 struct lookup_intent *, struct md_op_data *,
			 struct lustre_handle *, void *, int,
			 struct ptlrpc_request **, __u64);
	int (*m_getattr)(struct obd_export *, struct md_op_data *,
			 struct ptlrpc_request **);
	int (*m_getattr_name)(struct obd_export *, struct md_op_data *,
			      struct ptlrpc_request **);
	int (*m_intent_lock)(struct obd_export *, struct md_op_data *,
			     void *, int, struct lookup_intent *, int,
			     struct ptlrpc_request **,
			     ldlm_blocking_callback, __u64);
	int (*m_link)(struct obd_export *, struct md_op_data *,
		      struct ptlrpc_request **);
	int (*m_rename)(struct obd_export *, struct md_op_data *,
			const char *, int, const char *, int,
			struct ptlrpc_request **);
	int (*m_is_subdir)(struct obd_export *, const struct lu_fid *,
			   const struct lu_fid *,
			   struct ptlrpc_request **);
	int (*m_setattr)(struct obd_export *, struct md_op_data *, void *,
			 int , void *, int, struct ptlrpc_request **,
			 struct md_open_data **mod);
	int (*m_sync)(struct obd_export *, const struct lu_fid *,
		      struct obd_capa *, struct ptlrpc_request **);
	int (*m_readpage)(struct obd_export *, struct md_op_data *,
			  struct page **, struct ptlrpc_request **);

	int (*m_unlink)(struct obd_export *, struct md_op_data *,
			struct ptlrpc_request **);

	int (*m_setxattr)(struct obd_export *, const struct lu_fid *,
			  struct obd_capa *, obd_valid, const char *,
			  const char *, int, int, int, __u32,
			  struct ptlrpc_request **);

	int (*m_getxattr)(struct obd_export *, const struct lu_fid *,
			  struct obd_capa *, obd_valid, const char *,
			  const char *, int, int, int,
			  struct ptlrpc_request **);

	int (*m_init_ea_size)(struct obd_export *, int, int, int, int);

	int (*m_get_lustre_md)(struct obd_export *, struct ptlrpc_request *,
			       struct obd_export *, struct obd_export *,
			       struct lustre_md *);

	int (*m_free_lustre_md)(struct obd_export *, struct lustre_md *);

	int (*m_set_open_replay_data)(struct obd_export *,
				      struct obd_client_handle *,
				      struct lookup_intent *);
	int (*m_clear_open_replay_data)(struct obd_export *,
					struct obd_client_handle *);
	int (*m_set_lock_data)(struct obd_export *, __u64 *, void *, __u64 *);

	ldlm_mode_t (*m_lock_match)(struct obd_export *, __u64,
				    const struct lu_fid *, ldlm_type_t,
				    ldlm_policy_data_t *, ldlm_mode_t,
				    struct lustre_handle *);

	int (*m_cancel_unused)(struct obd_export *, const struct lu_fid *,
			       ldlm_policy_data_t *, ldlm_mode_t,
			       ldlm_cancel_flags_t flags, void *opaque);
	int (*m_renew_capa)(struct obd_export *, struct obd_capa *oc,
			    renew_capa_cb_t cb);
	int (*m_unpack_capa)(struct obd_export *, struct ptlrpc_request *,
			     const struct req_msg_field *, struct obd_capa **);

	int (*m_get_remote_perm)(struct obd_export *, const struct lu_fid *,
				 struct obd_capa *, __u32,
				 struct ptlrpc_request **);

	int (*m_intent_getattr_async)(struct obd_export *,
				      struct md_enqueue_info *,
				      struct ldlm_enqueue_info *);

	int (*m_revalidate_lock)(struct obd_export *, struct lookup_intent *,
				 struct lu_fid *, __u64 *bits);

	/*
	 * NOTE: If adding ops, add another LPROCFS_MD_OP_INIT() line to
	 * lprocfs_alloc_md_stats() in obdclass/lprocfs_status.c. Also, add a
	 * wrapper function in include/linux/obd_class.h.
	 */
};

struct lsm_operations {
	void (*lsm_free)(struct lov_stripe_md *);
	int (*lsm_destroy)(struct lov_stripe_md *, struct obdo *oa,
			   struct obd_export *md_exp);
	void (*lsm_stripe_by_index)(struct lov_stripe_md *, int *, obd_off *,
				    obd_off *);
	void (*lsm_stripe_by_offset)(struct lov_stripe_md *, int *, obd_off *,
				     obd_off *);
	int (*lsm_lmm_verify) (struct lov_mds_md *lmm, int lmm_bytes,
			       __u16 *stripe_count);
	int (*lsm_unpackmd) (struct lov_obd *lov, struct lov_stripe_md *lsm,
			     struct lov_mds_md *lmm);
};

extern const struct lsm_operations lsm_v1_ops;
extern const struct lsm_operations lsm_v3_ops;
static inline const struct lsm_operations *lsm_op_find(int magic)
{
	switch(magic) {
	case LOV_MAGIC_V1:
	       return &lsm_v1_ops;
	case LOV_MAGIC_V3:
	       return &lsm_v3_ops;
	default:
	       CERROR("Cannot recognize lsm_magic %08x\n", magic);
	       return NULL;
	}
}

/* Requests for obd_extent_calc() */
#define OBD_CALC_STRIPE_START   1
#define OBD_CALC_STRIPE_END     2

static inline struct lustre_capa *oinfo_capa(struct obd_info *oinfo)
{
	return oinfo->oi_capa;
}

static inline struct md_open_data *obd_mod_alloc(void)
{
	struct md_open_data *mod;
	OBD_ALLOC_PTR(mod);
	if (mod == NULL)
		return NULL;
	atomic_set(&mod->mod_refcount, 1);
	return mod;
}

#define obd_mod_get(mod) atomic_inc(&(mod)->mod_refcount)
#define obd_mod_put(mod)					\
({							      \
	if (atomic_dec_and_test(&(mod)->mod_refcount)) {	  \
		if ((mod)->mod_open_req)			  \
			ptlrpc_req_finished((mod)->mod_open_req);   \
		OBD_FREE_PTR(mod);			      \
	}						       \
})

void obdo_from_inode(struct obdo *dst, struct inode *src, obd_flag valid);
void obdo_set_parent_fid(struct obdo *dst, const struct lu_fid *parent);

/* return 1 if client should be resend request */
static inline int client_should_resend(int resend, struct client_obd *cli)
{
	return atomic_read(&cli->cl_resends) ?
	       atomic_read(&cli->cl_resends) > resend : 1;
}

/**
 * Return device name for this device
 *
 * XXX: lu_device is declared before obd_device, while a pointer pointing
 * back to obd_device in lu_device, so this helper function defines here
 * instead of in lu_object.h
 */
static inline const char *lu_dev_name(const struct lu_device *lu_dev)
{
	return lu_dev->ld_obd->obd_name;
}

static inline bool filename_is_volatile(const char *name, int namelen, int *idx)
{
	const char	*start;
	char		*end;

	if (strncmp(name, LUSTRE_VOLATILE_HDR, LUSTRE_VOLATILE_HDR_LEN) != 0)
		return false;

	/* caller does not care of idx */
	if (idx == NULL)
		return true;

	/* volatile file, the MDT can be set from name */
	/* name format is LUSTRE_VOLATILE_HDR:[idx]: */
	/* if no MDT is specified, use std way */
	if (namelen < LUSTRE_VOLATILE_HDR_LEN + 2)
		goto bad_format;
	/* test for no MDT idx case */
	if ((*(name + LUSTRE_VOLATILE_HDR_LEN) == ':') &&
	    (*(name + LUSTRE_VOLATILE_HDR_LEN + 1) == ':')) {
		*idx = -1;
		return true;
	}
	/* we have an idx, read it */
	start = name + LUSTRE_VOLATILE_HDR_LEN + 1;
	*idx = strtoul(start, &end, 0);
	/* error cases:
	 * no digit, no trailing :, negative value
	 */
	if (((*idx == 0) && (end == start)) ||
	    (*end != ':') || (*idx < 0))
		goto bad_format;

	return true;
bad_format:
	/* bad format of mdt idx, we cannot return an error
	 * to caller so we use hash algo */
	CERROR("Bad volatile file name format: %s\n",
	       name + LUSTRE_VOLATILE_HDR_LEN);
	return false;
}

static inline int cli_brw_size(struct obd_device *obd)
{
	LASSERT(obd != NULL);
	return obd->u.cli.cl_max_pages_per_rpc << PAGE_CACHE_SHIFT;
}

#endif /* __OBD_H */
