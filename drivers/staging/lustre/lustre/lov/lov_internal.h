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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef LOV_INTERNAL_H
#define LOV_INTERNAL_H

#include <obd_class.h>
#include <uapi/linux/lustre/lustre_idl.h>

/*
 * If we are unable to get the maximum object size from the OST in
 * ocd_maxbytes using OBD_CONNECT_MAXBYTES, then we fall back to using
 * the old maximum object size from ext3.
 */
#define LUSTRE_EXT3_STRIPE_MAXBYTES 0x1fffffff000ULL

struct lov_stripe_md {
	atomic_t	lsm_refc;
	spinlock_t	lsm_lock;
	pid_t		lsm_lock_owner; /* debugging */

	/*
	 * maximum possible file size, might change as OSTs status changes,
	 * e.g. disconnected, deactivated
	 */
	loff_t		lsm_maxbytes;
	struct ost_id	lsm_oi;
	u32		lsm_magic;
	u32		lsm_stripe_size;
	u32		lsm_pattern; /* RAID0, RAID1, released, ... */
	u16		lsm_stripe_count;
	u16		lsm_layout_gen;
	char		lsm_pool_name[LOV_MAXPOOLNAME + 1];
	struct lov_oinfo	*lsm_oinfo[0];
};

static inline bool lsm_is_released(struct lov_stripe_md *lsm)
{
	return !!(lsm->lsm_pattern & LOV_PATTERN_F_RELEASED);
}

static inline bool lsm_has_objects(struct lov_stripe_md *lsm)
{
	if (!lsm)
		return false;

	if (lsm_is_released(lsm))
		return false;

	return true;
}

struct lsm_operations {
	void (*lsm_free)(struct lov_stripe_md *);
	void (*lsm_stripe_by_index)(struct lov_stripe_md *, int *, loff_t *,
				    loff_t *);
	void (*lsm_stripe_by_offset)(struct lov_stripe_md *, int *, loff_t *,
				     loff_t *);
	int (*lsm_lmm_verify)(struct lov_mds_md *lmm, int lmm_bytes,
			      u16 *stripe_count);
	int (*lsm_unpackmd)(struct lov_obd *lov, struct lov_stripe_md *lsm,
			    struct lov_mds_md *lmm);
};

extern const struct lsm_operations lsm_v1_ops;
extern const struct lsm_operations lsm_v3_ops;

static inline const struct lsm_operations *lsm_op_find(int magic)
{
	switch (magic) {
	case LOV_MAGIC_V1:
		return &lsm_v1_ops;
	case LOV_MAGIC_V3:
		return &lsm_v3_ops;
	default:
		CERROR("unrecognized lsm_magic %08x\n", magic);
		return NULL;
	}
}

/* lov_do_div64(a, b) returns a % b, and a = a / b.
 * The 32-bit code is LOV-specific due to knowing about stripe limits in
 * order to reduce the divisor to a 32-bit number.  If the divisor is
 * already a 32-bit value the compiler handles this directly.
 */
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

#define pool_tgt_size(p)	((p)->pool_obds.op_size)
#define pool_tgt_count(p)	((p)->pool_obds.op_count)
#define pool_tgt_array(p)	((p)->pool_obds.op_array)
#define pool_tgt_rw_sem(p)	((p)->pool_obds.op_rw_sem)

struct pool_desc {
	char			 pool_name[LOV_MAXPOOLNAME + 1];
	struct ost_pool		 pool_obds;
	atomic_t		 pool_refcount;
	struct hlist_node	 pool_hash;		/* access by poolname */
	struct list_head	 pool_list;		/* serial access */
	struct dentry		*pool_debugfs_entry;	/* file in debugfs */
	struct obd_device	*pool_lobd;		/* owner */
};

struct lov_request {
	struct obd_info	  rq_oi;
	struct lov_request_set  *rq_rqset;

	struct list_head	       rq_link;

	int		      rq_idx;	/* index in lov->tgts array */
};

struct lov_request_set {
	struct obd_info			*set_oi;
	struct obd_device		*set_obd;
	int				set_count;
	atomic_t			set_completes;
	atomic_t			set_success;
	struct list_head			set_list;
};

extern struct kmem_cache *lov_oinfo_slab;

extern struct lu_kmem_descr lov_caches[];

#define lov_uuid2str(lv, index) \
	(char *)((lv)->lov_tgts[index]->ltd_uuid.uuid)

/* lov_merge.c */
int lov_merge_lvb_kms(struct lov_stripe_md *lsm,
		      struct ost_lvb *lvb, __u64 *kms_place);

/* lov_offset.c */
u64 lov_stripe_size(struct lov_stripe_md *lsm, u64 ost_size, int stripeno);
int lov_stripe_offset(struct lov_stripe_md *lsm, u64 lov_off,
		      int stripeno, u64 *u64);
u64 lov_size_to_stripe(struct lov_stripe_md *lsm, u64 file_size, int stripeno);
int lov_stripe_intersects(struct lov_stripe_md *lsm, int stripeno,
			  u64 start, u64 end,
			  u64 *obd_start, u64 *obd_end);
int lov_stripe_number(struct lov_stripe_md *lsm, u64 lov_off);
pgoff_t lov_stripe_pgoff(struct lov_stripe_md *lsm, pgoff_t stripe_index,
			 int stripe);

/* lov_request.c */
int lov_prep_statfs_set(struct obd_device *obd, struct obd_info *oinfo,
			struct lov_request_set **reqset);
int lov_fini_statfs_set(struct lov_request_set *set);

/* lov_obd.c */
void lov_stripe_lock(struct lov_stripe_md *md);
void lov_stripe_unlock(struct lov_stripe_md *md);
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
ssize_t lov_lsm_pack(const struct lov_stripe_md *lsm, void *buf,
		     size_t buf_size);
struct lov_stripe_md *lov_unpackmd(struct lov_obd *lov, struct lov_mds_md *lmm,
				   size_t lmm_size);
int lov_free_memmd(struct lov_stripe_md **lsmp);

void lov_dump_lmm_v1(int level, struct lov_mds_md_v1 *lmm);
void lov_dump_lmm_v3(int level, struct lov_mds_md_v3 *lmm);
void lov_dump_lmm_common(int level, void *lmmp);

/* lov_ea.c */
struct lov_stripe_md *lsm_alloc_plain(u16 stripe_count);
void lsm_free_plain(struct lov_stripe_md *lsm);
void dump_lsm(unsigned int level, const struct lov_stripe_md *lsm);

/* lproc_lov.c */
extern const struct file_operations lov_proc_target_fops;
void lprocfs_lov_init_vars(struct lprocfs_static_vars *lvars);

/* lov_cl.c */
extern struct lu_device_type lov_device_type;

/* pools */
extern struct cfs_hash_ops pool_hash_operations;
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
void lov_pool_putref(struct pool_desc *pool);

static inline struct lov_stripe_md *lsm_addref(struct lov_stripe_md *lsm)
{
	LASSERT(atomic_read(&lsm->lsm_refc) > 0);
	atomic_inc(&lsm->lsm_refc);
	return lsm;
}

static inline bool lov_oinfo_is_dummy(const struct lov_oinfo *loi)
{
	if (unlikely(loi->loi_oi.oi.oi_id == 0 &&
		     loi->loi_oi.oi.oi_seq == 0 &&
		     loi->loi_ost_idx == 0 &&
		     loi->loi_ost_gen == 0))
		return true;

	return false;
}

static inline struct obd_device *lov2obd(const struct lov_obd *lov)
{
	return container_of0(lov, struct obd_device, u.lov);
}

#endif
