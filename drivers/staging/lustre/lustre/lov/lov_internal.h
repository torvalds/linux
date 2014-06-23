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

#ifndef LOV_INTERNAL_H
#define LOV_INTERNAL_H

#include <obd_class.h>
#include <lustre/lustre_user.h>

/* lov_do_div64(a, b) returns a % b, and a = a / b.
 * The 32-bit code is LOV-specific due to knowing about stripe limits in
 * order to reduce the divisor to a 32-bit number.  If the divisor is
 * already a 32-bit value the compiler handles this directly. */
#if BITS_PER_LONG == 64
# define lov_do_div64(n, base) ({					\
	uint64_t __base = (base);					\
	uint64_t __rem;							\
	__rem = ((uint64_t)(n)) % __base;				\
	(n) = ((uint64_t)(n)) / __base;					\
	__rem;								\
})
#elif BITS_PER_LONG == 32
# define lov_do_div64(n, base) ({					\
	uint64_t __rem;							\
	if ((sizeof(base) > 4) && (((base) & 0xffffffff00000000ULL) != 0)) {  \
		int __remainder;					      \
		LASSERTF(!((base) & (LOV_MIN_STRIPE_SIZE - 1)), "64 bit lov " \
			 "division %llu / %llu\n", (n), (uint64_t)(base));    \
		__remainder = (n) & (LOV_MIN_STRIPE_SIZE - 1);		\
		(n) >>= LOV_MIN_STRIPE_BITS;				\
		__rem = do_div(n, (base) >> LOV_MIN_STRIPE_BITS);	\
		__rem <<= LOV_MIN_STRIPE_BITS;				\
		__rem += __remainder;					\
	} else {							\
		__rem = do_div(n, base);				\
	}								\
	__rem;								\
})
#endif

struct lov_lock_handles {
	struct portals_handle   llh_handle;
	atomic_t	    llh_refcount;
	int		     llh_stripe_count;
	struct lustre_handle    llh_handles[0];
};

struct lov_request {
	struct obd_info	  rq_oi;
	struct lov_request_set  *rq_rqset;

	struct list_head	       rq_link;

	int		      rq_idx;	/* index in lov->tgts array */
	int		      rq_stripe;     /* stripe number */
	int		      rq_complete;
	int		      rq_rc;
	int		      rq_buflen;     /* length of sub_md */

	obd_count		rq_oabufs;
	obd_count		rq_pgaidx;
};

struct lov_request_set {
	struct ldlm_enqueue_info	*set_ei;
	struct obd_info			*set_oi;
	atomic_t			set_refcount;
	struct obd_export		*set_exp;
	/* XXX: There is @set_exp already, however obd_statfs gets obd_device
	   only. */
	struct obd_device		*set_obd;
	int				set_count;
	atomic_t			set_completes;
	atomic_t			set_success;
	atomic_t			set_finish_checked;
	struct llog_cookie		*set_cookies;
	int				set_cookie_sent;
	struct obd_trans_info		*set_oti;
	obd_count			set_oabufs;
	struct brw_page			*set_pga;
	struct lov_lock_handles		*set_lockh;
	struct list_head			set_list;
	wait_queue_head_t			set_waitq;
	spinlock_t			set_lock;
};

extern struct kmem_cache *lov_oinfo_slab;

extern struct lu_kmem_descr lov_caches[];

void lov_finish_set(struct lov_request_set *set);

static inline void lov_get_reqset(struct lov_request_set *set)
{
	LASSERT(set != NULL);
	LASSERT(atomic_read(&set->set_refcount) > 0);
	atomic_inc(&set->set_refcount);
}

static inline void lov_put_reqset(struct lov_request_set *set)
{
	if (atomic_dec_and_test(&set->set_refcount))
		lov_finish_set(set);
}

static inline struct lov_lock_handles *
lov_handle2llh(struct lustre_handle *handle)
{
	LASSERT(handle != NULL);
	return(class_handle2object(handle->cookie));
}

static inline void lov_llh_put(struct lov_lock_handles *llh)
{
	CDEBUG(D_INFO, "PUTting llh %p : new refcount %d\n", llh,
	       atomic_read(&llh->llh_refcount) - 1);
	LASSERT(atomic_read(&llh->llh_refcount) > 0 &&
		atomic_read(&llh->llh_refcount) < 0x5a5a);
	if (atomic_dec_and_test(&llh->llh_refcount)) {
		class_handle_unhash(&llh->llh_handle);
		/* The structure may be held by other threads because RCU.
		 *   -jxiong */
		if (atomic_read(&llh->llh_refcount))
			return;

		OBD_FREE_RCU(llh, sizeof(*llh) +
			     sizeof(*llh->llh_handles) * llh->llh_stripe_count,
			     &llh->llh_handle);
	}
}

#define lov_uuid2str(lv, index) \
	(char *)((lv)->lov_tgts[index]->ltd_uuid.uuid)

/* lov_merge.c */
void lov_merge_attrs(struct obdo *tgt, struct obdo *src, obd_valid valid,
		     struct lov_stripe_md *lsm, int stripeno, int *set);
int lov_merge_lvb(struct obd_export *exp, struct lov_stripe_md *lsm,
		  struct ost_lvb *lvb, int kms_only);
int lov_adjust_kms(struct obd_export *exp, struct lov_stripe_md *lsm,
		   obd_off size, int shrink);
int lov_merge_lvb_kms(struct lov_stripe_md *lsm,
		      struct ost_lvb *lvb, __u64 *kms_place);

/* lov_offset.c */
obd_size lov_stripe_size(struct lov_stripe_md *lsm, obd_size ost_size,
			 int stripeno);
int lov_stripe_offset(struct lov_stripe_md *lsm, obd_off lov_off,
		      int stripeno, obd_off *obd_off);
obd_off lov_size_to_stripe(struct lov_stripe_md *lsm, obd_off file_size,
			   int stripeno);
int lov_stripe_intersects(struct lov_stripe_md *lsm, int stripeno,
			  obd_off start, obd_off end,
			  obd_off *obd_start, obd_off *obd_end);
int lov_stripe_number(struct lov_stripe_md *lsm, obd_off lov_off);

/* lov_qos.c */
#define LOV_USES_ASSIGNED_STRIPE	0
#define LOV_USES_DEFAULT_STRIPE	 1
int qos_add_tgt(struct obd_device *obd, __u32 index);
int qos_del_tgt(struct obd_device *obd, struct lov_tgt_desc *tgt);
void qos_shrink_lsm(struct lov_request_set *set);
int qos_prep_create(struct obd_export *exp, struct lov_request_set *set);
void qos_update(struct lov_obd *lov);
void qos_statfs_done(struct lov_obd *lov);
void qos_statfs_update(struct obd_device *obd, __u64 max_age, int wait);
int qos_remedy_create(struct lov_request_set *set, struct lov_request *req);

/* lov_request.c */
void lov_set_add_req(struct lov_request *req, struct lov_request_set *set);
int lov_set_finished(struct lov_request_set *set, int idempotent);
void lov_update_set(struct lov_request_set *set,
		    struct lov_request *req, int rc);
int lov_update_common_set(struct lov_request_set *set,
			  struct lov_request *req, int rc);
int lov_check_and_wait_active(struct lov_obd *lov, int ost_idx);
int lov_prep_create_set(struct obd_export *exp, struct obd_info *oifo,
			struct lov_stripe_md **ea, struct obdo *src_oa,
			struct obd_trans_info *oti,
			struct lov_request_set **reqset);
int cb_create_update(void *cookie, int rc);
int lov_fini_create_set(struct lov_request_set *set, struct lov_stripe_md **ea);
int lov_prep_brw_set(struct obd_export *exp, struct obd_info *oinfo,
		     obd_count oa_bufs, struct brw_page *pga,
		     struct obd_trans_info *oti,
		     struct lov_request_set **reqset);
int lov_fini_brw_set(struct lov_request_set *set);
int lov_prep_getattr_set(struct obd_export *exp, struct obd_info *oinfo,
			 struct lov_request_set **reqset);
int lov_fini_getattr_set(struct lov_request_set *set);
int lov_prep_destroy_set(struct obd_export *exp, struct obd_info *oinfo,
			 struct obdo *src_oa, struct lov_stripe_md *lsm,
			 struct obd_trans_info *oti,
			 struct lov_request_set **reqset);
int lov_update_destroy_set(struct lov_request_set *set,
			   struct lov_request *req, int rc);
int lov_fini_destroy_set(struct lov_request_set *set);
int lov_prep_setattr_set(struct obd_export *exp, struct obd_info *oinfo,
			 struct obd_trans_info *oti,
			 struct lov_request_set **reqset);
int lov_update_setattr_set(struct lov_request_set *set,
			   struct lov_request *req, int rc);
int lov_fini_setattr_set(struct lov_request_set *set);
int lov_prep_punch_set(struct obd_export *exp, struct obd_info *oinfo,
		       struct obd_trans_info *oti,
		       struct lov_request_set **reqset);
int lov_fini_punch_set(struct lov_request_set *set);
int lov_prep_sync_set(struct obd_export *exp, struct obd_info *obd_info,
		      obd_off start, obd_off end,
		      struct lov_request_set **reqset);
int lov_fini_sync_set(struct lov_request_set *set);
int lov_prep_enqueue_set(struct obd_export *exp, struct obd_info *oinfo,
			 struct ldlm_enqueue_info *einfo,
			 struct lov_request_set **reqset);
int lov_fini_enqueue_set(struct lov_request_set *set, __u32 mode, int rc,
			 struct ptlrpc_request_set *rqset);
int lov_prep_match_set(struct obd_export *exp, struct obd_info *oinfo,
		       struct lov_stripe_md *lsm,
		       ldlm_policy_data_t *policy, __u32 mode,
		       struct lustre_handle *lockh,
		       struct lov_request_set **reqset);
int lov_fini_match_set(struct lov_request_set *set, __u32 mode, __u64 flags);
int lov_prep_cancel_set(struct obd_export *exp, struct obd_info *oinfo,
			struct lov_stripe_md *lsm,
			__u32 mode, struct lustre_handle *lockh,
			struct lov_request_set **reqset);
int lov_fini_cancel_set(struct lov_request_set *set);
int lov_prep_statfs_set(struct obd_device *obd, struct obd_info *oinfo,
			struct lov_request_set **reqset);
void lov_update_statfs(struct obd_statfs *osfs, struct obd_statfs *lov_sfs,
		       int success);
int lov_fini_statfs(struct obd_device *obd, struct obd_statfs *osfs,
		    int success);
int lov_fini_statfs_set(struct lov_request_set *set);
int lov_statfs_interpret(struct ptlrpc_request_set *rqset, void *data, int rc);

/* lov_obd.c */
void lov_fix_desc(struct lov_desc *desc);
void lov_fix_desc_stripe_size(__u64 *val);
void lov_fix_desc_stripe_count(__u32 *val);
void lov_fix_desc_pattern(__u32 *val);
void lov_fix_desc_qos_maxage(__u32 *val);
__u16 lov_get_stripecnt(struct lov_obd *lov, __u32 magic, __u16 stripe_count);
int lov_connect_obd(struct obd_device *obd, __u32 index, int activate,
		    struct obd_connect_data *data);
int lov_setup(struct obd_device *obd, struct lustre_cfg *lcfg);
int lov_process_config_base(struct obd_device *obd, struct lustre_cfg *lcfg,
			    __u32 *indexp, int *genp);
int lov_del_target(struct obd_device *obd, __u32 index,
		   struct obd_uuid *uuidp, int gen);

/* lov_pack.c */
int lov_packmd(struct obd_export *exp, struct lov_mds_md **lmm,
	       struct lov_stripe_md *lsm);
int lov_unpackmd(struct obd_export *exp, struct lov_stripe_md **lsmp,
		 struct lov_mds_md *lmm, int lmm_bytes);
int lov_setstripe(struct obd_export *exp, int max_lmm_size,
		  struct lov_stripe_md **lsmp, struct lov_user_md *lump);
int lov_setea(struct obd_export *exp, struct lov_stripe_md **lsmp,
	      struct lov_user_md *lump);
int lov_getstripe(struct obd_export *exp,
		  struct lov_stripe_md *lsm, struct lov_user_md *lump);
int lov_alloc_memmd(struct lov_stripe_md **lsmp, __u16 stripe_count,
		    int pattern, int magic);
int lov_free_memmd(struct lov_stripe_md **lsmp);

void lov_dump_lmm_v1(int level, struct lov_mds_md_v1 *lmm);
void lov_dump_lmm_v3(int level, struct lov_mds_md_v3 *lmm);
void lov_dump_lmm_common(int level, void *lmmp);
void lov_dump_lmm(int level, void *lmm);

/* lov_ea.c */
struct lov_stripe_md *lsm_alloc_plain(__u16 stripe_count, int *size);
void lsm_free_plain(struct lov_stripe_md *lsm);
void dump_lsm(unsigned int level, const struct lov_stripe_md *lsm);

int lovea_destroy_object(struct lov_obd *lov, struct lov_stripe_md *lsm,
			 struct obdo *oa, void *data);
/* lproc_lov.c */
#ifdef LPROCFS
extern const struct file_operations lov_proc_target_fops;
void lprocfs_lov_init_vars(struct lprocfs_static_vars *lvars);
#else
static inline void lprocfs_lov_init_vars(struct lprocfs_static_vars *lvars)
{
	memset(lvars, 0, sizeof(*lvars));
}
#endif

/* lov_cl.c */
extern struct lu_device_type lov_device_type;

/* pools */
extern cfs_hash_ops_t pool_hash_operations;
/* ost_pool methods */
int lov_ost_pool_init(struct ost_pool *op, unsigned int count);
int lov_ost_pool_extend(struct ost_pool *op, unsigned int min_count);
int lov_ost_pool_add(struct ost_pool *op, __u32 idx, unsigned int min_count);
int lov_ost_pool_remove(struct ost_pool *op, __u32 idx);
int lov_ost_pool_free(struct ost_pool *op);

/* high level pool methods */
int lov_pool_new(struct obd_device *obd, char *poolname);
int lov_pool_del(struct obd_device *obd, char *poolname);
int lov_pool_add(struct obd_device *obd, char *poolname, char *ostname);
int lov_pool_remove(struct obd_device *obd, char *poolname, char *ostname);
void lov_dump_pool(int level, struct pool_desc *pool);
struct pool_desc *lov_find_pool(struct lov_obd *lov, char *poolname);
int lov_check_index_in_pool(__u32 idx, struct pool_desc *pool);
void lov_pool_putref(struct pool_desc *pool);

static inline struct lov_stripe_md *lsm_addref(struct lov_stripe_md *lsm)
{
	LASSERT(atomic_read(&lsm->lsm_refc) > 0);
	atomic_inc(&lsm->lsm_refc);
	return lsm;
}

#endif
