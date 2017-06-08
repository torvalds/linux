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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/hash.c
 *
 * Implement a hash class for hash process in lustre system.
 *
 * Author: YuZhangyong <yzy@clusterfs.com>
 *
 * 2008-08-15: Brian Behlendorf <behlendorf1@llnl.gov>
 * - Simplified API and improved documentation
 * - Added per-hash feature flags:
 *   * CFS_HASH_DEBUG additional validation
 *   * CFS_HASH_REHASH dynamic rehashing
 * - Added per-hash statistics
 * - General performance enhancements
 *
 * 2009-07-31: Liang Zhen <zhen.liang@sun.com>
 * - move all stuff to libcfs
 * - don't allow cur_bits != max_bits without setting of CFS_HASH_REHASH
 * - ignore hs_rwlock if without CFS_HASH_REHASH setting
 * - buckets are allocated one by one(instead of contiguous memory),
 *   to avoid unnecessary cacheline conflict
 *
 * 2010-03-01: Liang Zhen <zhen.liang@sun.com>
 * - "bucket" is a group of hlist_head now, user can specify bucket size
 *   by bkt_bits of cfs_hash_create(), all hlist_heads in a bucket share
 *   one lock for reducing memory overhead.
 *
 * - support lockless hash, caller will take care of locks:
 *   avoid lock overhead for hash tables that are already protected
 *   by locking in the caller for another reason
 *
 * - support both spin_lock/rwlock for bucket:
 *   overhead of spinlock contention is lower than read/write
 *   contention of rwlock, so using spinlock to serialize operations on
 *   bucket is more reasonable for those frequently changed hash tables
 *
 * - support one-single lock mode:
 *   one lock to protect all hash operations to avoid overhead of
 *   multiple locks if hash table is always small
 *
 * - removed a lot of unnecessary addref & decref on hash element:
 *   addref & decref are atomic operations in many use-cases which
 *   are expensive.
 *
 * - support non-blocking cfs_hash_add() and cfs_hash_findadd():
 *   some lustre use-cases require these functions to be strictly
 *   non-blocking, we need to schedule required rehash on a different
 *   thread on those cases.
 *
 * - safer rehash on large hash table
 *   In old implementation, rehash function will exclusively lock the
 *   hash table and finish rehash in one batch, it's dangerous on SMP
 *   system because rehash millions of elements could take long time.
 *   New implemented rehash can release lock and relax CPU in middle
 *   of rehash, it's safe for another thread to search/change on the
 *   hash table even it's in rehasing.
 *
 * - support two different refcount modes
 *   . hash table has refcount on element
 *   . hash table doesn't change refcount on adding/removing element
 *
 * - support long name hash table (for param-tree)
 *
 * - fix a bug for cfs_hash_rehash_key:
 *   in old implementation, cfs_hash_rehash_key could screw up the
 *   hash-table because @key is overwritten without any protection.
 *   Now we need user to define hs_keycpy for those rehash enabled
 *   hash tables, cfs_hash_rehash_key will overwrite hash-key
 *   inside lock by calling hs_keycpy.
 *
 * - better hash iteration:
 *   Now we support both locked iteration & lockless iteration of hash
 *   table. Also, user can break the iteration by return 1 in callback.
 */
#include <linux/seq_file.h>
#include <linux/log2.h>

#include "../../include/linux/libcfs/libcfs.h"

#if CFS_HASH_DEBUG_LEVEL >= CFS_HASH_DEBUG_1
static unsigned int warn_on_depth = 8;
module_param(warn_on_depth, uint, 0644);
MODULE_PARM_DESC(warn_on_depth, "warning when hash depth is high.");
#endif

struct cfs_wi_sched *cfs_sched_rehash;

static inline void
cfs_hash_nl_lock(union cfs_hash_lock *lock, int exclusive) {}

static inline void
cfs_hash_nl_unlock(union cfs_hash_lock *lock, int exclusive) {}

static inline void
cfs_hash_spin_lock(union cfs_hash_lock *lock, int exclusive)
	__acquires(&lock->spin)
{
	spin_lock(&lock->spin);
}

static inline void
cfs_hash_spin_unlock(union cfs_hash_lock *lock, int exclusive)
	__releases(&lock->spin)
{
	spin_unlock(&lock->spin);
}

static inline void
cfs_hash_rw_lock(union cfs_hash_lock *lock, int exclusive)
	__acquires(&lock->rw)
{
	if (!exclusive)
		read_lock(&lock->rw);
	else
		write_lock(&lock->rw);
}

static inline void
cfs_hash_rw_unlock(union cfs_hash_lock *lock, int exclusive)
	__releases(&lock->rw)
{
	if (!exclusive)
		read_unlock(&lock->rw);
	else
		write_unlock(&lock->rw);
}

/** No lock hash */
static struct cfs_hash_lock_ops cfs_hash_nl_lops = {
	.hs_lock	= cfs_hash_nl_lock,
	.hs_unlock	= cfs_hash_nl_unlock,
	.hs_bkt_lock	= cfs_hash_nl_lock,
	.hs_bkt_unlock	= cfs_hash_nl_unlock,
};

/** no bucket lock, one spinlock to protect everything */
static struct cfs_hash_lock_ops cfs_hash_nbl_lops = {
	.hs_lock	= cfs_hash_spin_lock,
	.hs_unlock	= cfs_hash_spin_unlock,
	.hs_bkt_lock	= cfs_hash_nl_lock,
	.hs_bkt_unlock	= cfs_hash_nl_unlock,
};

/** spin bucket lock, rehash is enabled */
static struct cfs_hash_lock_ops cfs_hash_bkt_spin_lops = {
	.hs_lock	= cfs_hash_rw_lock,
	.hs_unlock	= cfs_hash_rw_unlock,
	.hs_bkt_lock	= cfs_hash_spin_lock,
	.hs_bkt_unlock	= cfs_hash_spin_unlock,
};

/** rw bucket lock, rehash is enabled */
static struct cfs_hash_lock_ops cfs_hash_bkt_rw_lops = {
	.hs_lock	= cfs_hash_rw_lock,
	.hs_unlock	= cfs_hash_rw_unlock,
	.hs_bkt_lock	= cfs_hash_rw_lock,
	.hs_bkt_unlock	= cfs_hash_rw_unlock,
};

/** spin bucket lock, rehash is disabled */
static struct cfs_hash_lock_ops cfs_hash_nr_bkt_spin_lops = {
	.hs_lock	= cfs_hash_nl_lock,
	.hs_unlock	= cfs_hash_nl_unlock,
	.hs_bkt_lock	= cfs_hash_spin_lock,
	.hs_bkt_unlock	= cfs_hash_spin_unlock,
};

/** rw bucket lock, rehash is disabled */
static struct cfs_hash_lock_ops cfs_hash_nr_bkt_rw_lops = {
	.hs_lock	= cfs_hash_nl_lock,
	.hs_unlock	= cfs_hash_nl_unlock,
	.hs_bkt_lock	= cfs_hash_rw_lock,
	.hs_bkt_unlock	= cfs_hash_rw_unlock,
};

static void
cfs_hash_lock_setup(struct cfs_hash *hs)
{
	if (cfs_hash_with_no_lock(hs)) {
		hs->hs_lops = &cfs_hash_nl_lops;

	} else if (cfs_hash_with_no_bktlock(hs)) {
		hs->hs_lops = &cfs_hash_nbl_lops;
		spin_lock_init(&hs->hs_lock.spin);

	} else if (cfs_hash_with_rehash(hs)) {
		rwlock_init(&hs->hs_lock.rw);

		if (cfs_hash_with_rw_bktlock(hs))
			hs->hs_lops = &cfs_hash_bkt_rw_lops;
		else if (cfs_hash_with_spin_bktlock(hs))
			hs->hs_lops = &cfs_hash_bkt_spin_lops;
		else
			LBUG();
	} else {
		if (cfs_hash_with_rw_bktlock(hs))
			hs->hs_lops = &cfs_hash_nr_bkt_rw_lops;
		else if (cfs_hash_with_spin_bktlock(hs))
			hs->hs_lops = &cfs_hash_nr_bkt_spin_lops;
		else
			LBUG();
	}
}

/**
 * Simple hash head without depth tracking
 * new element is always added to head of hlist
 */
struct cfs_hash_head {
	struct hlist_head	hh_head;	/**< entries list */
};

static int
cfs_hash_hh_hhead_size(struct cfs_hash *hs)
{
	return sizeof(struct cfs_hash_head);
}

static struct hlist_head *
cfs_hash_hh_hhead(struct cfs_hash *hs, struct cfs_hash_bd *bd)
{
	struct cfs_hash_head *head;

	head = (struct cfs_hash_head *)&bd->bd_bucket->hsb_head[0];
	return &head[bd->bd_offset].hh_head;
}

static int
cfs_hash_hh_hnode_add(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		      struct hlist_node *hnode)
{
	hlist_add_head(hnode, cfs_hash_hh_hhead(hs, bd));
	return -1; /* unknown depth */
}

static int
cfs_hash_hh_hnode_del(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		      struct hlist_node *hnode)
{
	hlist_del_init(hnode);
	return -1; /* unknown depth */
}

/**
 * Simple hash head with depth tracking
 * new element is always added to head of hlist
 */
struct cfs_hash_head_dep {
	struct hlist_head	hd_head;	/**< entries list */
	unsigned int		hd_depth;	/**< list length */
};

static int
cfs_hash_hd_hhead_size(struct cfs_hash *hs)
{
	return sizeof(struct cfs_hash_head_dep);
}

static struct hlist_head *
cfs_hash_hd_hhead(struct cfs_hash *hs, struct cfs_hash_bd *bd)
{
	struct cfs_hash_head_dep *head;

	head = (struct cfs_hash_head_dep *)&bd->bd_bucket->hsb_head[0];
	return &head[bd->bd_offset].hd_head;
}

static int
cfs_hash_hd_hnode_add(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		      struct hlist_node *hnode)
{
	struct cfs_hash_head_dep *hh;

	hh = container_of(cfs_hash_hd_hhead(hs, bd),
			  struct cfs_hash_head_dep, hd_head);
	hlist_add_head(hnode, &hh->hd_head);
	return ++hh->hd_depth;
}

static int
cfs_hash_hd_hnode_del(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		      struct hlist_node *hnode)
{
	struct cfs_hash_head_dep *hh;

	hh = container_of(cfs_hash_hd_hhead(hs, bd),
			  struct cfs_hash_head_dep, hd_head);
	hlist_del_init(hnode);
	return --hh->hd_depth;
}

/**
 * double links hash head without depth tracking
 * new element is always added to tail of hlist
 */
struct cfs_hash_dhead {
	struct hlist_head	dh_head;	/**< entries list */
	struct hlist_node	*dh_tail;	/**< the last entry */
};

static int
cfs_hash_dh_hhead_size(struct cfs_hash *hs)
{
	return sizeof(struct cfs_hash_dhead);
}

static struct hlist_head *
cfs_hash_dh_hhead(struct cfs_hash *hs, struct cfs_hash_bd *bd)
{
	struct cfs_hash_dhead *head;

	head = (struct cfs_hash_dhead *)&bd->bd_bucket->hsb_head[0];
	return &head[bd->bd_offset].dh_head;
}

static int
cfs_hash_dh_hnode_add(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		      struct hlist_node *hnode)
{
	struct cfs_hash_dhead *dh;

	dh = container_of(cfs_hash_dh_hhead(hs, bd),
			  struct cfs_hash_dhead, dh_head);
	if (dh->dh_tail) /* not empty */
		hlist_add_behind(hnode, dh->dh_tail);
	else /* empty list */
		hlist_add_head(hnode, &dh->dh_head);
	dh->dh_tail = hnode;
	return -1; /* unknown depth */
}

static int
cfs_hash_dh_hnode_del(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		      struct hlist_node *hnd)
{
	struct cfs_hash_dhead *dh;

	dh = container_of(cfs_hash_dh_hhead(hs, bd),
			  struct cfs_hash_dhead, dh_head);
	if (!hnd->next) { /* it's the tail */
		dh->dh_tail = (hnd->pprev == &dh->dh_head.first) ? NULL :
			      container_of(hnd->pprev, struct hlist_node, next);
	}
	hlist_del_init(hnd);
	return -1; /* unknown depth */
}

/**
 * double links hash head with depth tracking
 * new element is always added to tail of hlist
 */
struct cfs_hash_dhead_dep {
	struct hlist_head	dd_head;	/**< entries list */
	struct hlist_node	*dd_tail;	/**< the last entry */
	unsigned int		dd_depth;	/**< list length */
};

static int
cfs_hash_dd_hhead_size(struct cfs_hash *hs)
{
	return sizeof(struct cfs_hash_dhead_dep);
}

static struct hlist_head *
cfs_hash_dd_hhead(struct cfs_hash *hs, struct cfs_hash_bd *bd)
{
	struct cfs_hash_dhead_dep *head;

	head = (struct cfs_hash_dhead_dep *)&bd->bd_bucket->hsb_head[0];
	return &head[bd->bd_offset].dd_head;
}

static int
cfs_hash_dd_hnode_add(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		      struct hlist_node *hnode)
{
	struct cfs_hash_dhead_dep *dh;

	dh = container_of(cfs_hash_dd_hhead(hs, bd),
			  struct cfs_hash_dhead_dep, dd_head);
	if (dh->dd_tail) /* not empty */
		hlist_add_behind(hnode, dh->dd_tail);
	else /* empty list */
		hlist_add_head(hnode, &dh->dd_head);
	dh->dd_tail = hnode;
	return ++dh->dd_depth;
}

static int
cfs_hash_dd_hnode_del(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		      struct hlist_node *hnd)
{
	struct cfs_hash_dhead_dep *dh;

	dh = container_of(cfs_hash_dd_hhead(hs, bd),
			  struct cfs_hash_dhead_dep, dd_head);
	if (!hnd->next) { /* it's the tail */
		dh->dd_tail = (hnd->pprev == &dh->dd_head.first) ? NULL :
			      container_of(hnd->pprev, struct hlist_node, next);
	}
	hlist_del_init(hnd);
	return --dh->dd_depth;
}

static struct cfs_hash_hlist_ops cfs_hash_hh_hops = {
	.hop_hhead	= cfs_hash_hh_hhead,
	.hop_hhead_size	= cfs_hash_hh_hhead_size,
	.hop_hnode_add	= cfs_hash_hh_hnode_add,
	.hop_hnode_del	= cfs_hash_hh_hnode_del,
};

static struct cfs_hash_hlist_ops cfs_hash_hd_hops = {
	.hop_hhead	= cfs_hash_hd_hhead,
	.hop_hhead_size	= cfs_hash_hd_hhead_size,
	.hop_hnode_add	= cfs_hash_hd_hnode_add,
	.hop_hnode_del	= cfs_hash_hd_hnode_del,
};

static struct cfs_hash_hlist_ops cfs_hash_dh_hops = {
	.hop_hhead	= cfs_hash_dh_hhead,
	.hop_hhead_size	= cfs_hash_dh_hhead_size,
	.hop_hnode_add	= cfs_hash_dh_hnode_add,
	.hop_hnode_del	= cfs_hash_dh_hnode_del,
};

static struct cfs_hash_hlist_ops cfs_hash_dd_hops = {
	.hop_hhead	= cfs_hash_dd_hhead,
	.hop_hhead_size	= cfs_hash_dd_hhead_size,
	.hop_hnode_add	= cfs_hash_dd_hnode_add,
	.hop_hnode_del	= cfs_hash_dd_hnode_del,
};

static void
cfs_hash_hlist_setup(struct cfs_hash *hs)
{
	if (cfs_hash_with_add_tail(hs)) {
		hs->hs_hops = cfs_hash_with_depth(hs) ?
			      &cfs_hash_dd_hops : &cfs_hash_dh_hops;
	} else {
		hs->hs_hops = cfs_hash_with_depth(hs) ?
			      &cfs_hash_hd_hops : &cfs_hash_hh_hops;
	}
}

static void
cfs_hash_bd_from_key(struct cfs_hash *hs, struct cfs_hash_bucket **bkts,
		     unsigned int bits, const void *key, struct cfs_hash_bd *bd)
{
	unsigned int index = cfs_hash_id(hs, key, (1U << bits) - 1);

	LASSERT(bits == hs->hs_cur_bits || bits == hs->hs_rehash_bits);

	bd->bd_bucket = bkts[index & ((1U << (bits - hs->hs_bkt_bits)) - 1)];
	bd->bd_offset = index >> (bits - hs->hs_bkt_bits);
}

void
cfs_hash_bd_get(struct cfs_hash *hs, const void *key, struct cfs_hash_bd *bd)
{
	/* NB: caller should hold hs->hs_rwlock if REHASH is set */
	if (likely(!hs->hs_rehash_buckets)) {
		cfs_hash_bd_from_key(hs, hs->hs_buckets,
				     hs->hs_cur_bits, key, bd);
	} else {
		LASSERT(hs->hs_rehash_bits);
		cfs_hash_bd_from_key(hs, hs->hs_rehash_buckets,
				     hs->hs_rehash_bits, key, bd);
	}
}
EXPORT_SYMBOL(cfs_hash_bd_get);

static inline void
cfs_hash_bd_dep_record(struct cfs_hash *hs, struct cfs_hash_bd *bd, int dep_cur)
{
	if (likely(dep_cur <= bd->bd_bucket->hsb_depmax))
		return;

	bd->bd_bucket->hsb_depmax = dep_cur;
# if CFS_HASH_DEBUG_LEVEL >= CFS_HASH_DEBUG_1
	if (likely(!warn_on_depth ||
		   max(warn_on_depth, hs->hs_dep_max) >= dep_cur))
		return;

	spin_lock(&hs->hs_dep_lock);
	hs->hs_dep_max = dep_cur;
	hs->hs_dep_bkt = bd->bd_bucket->hsb_index;
	hs->hs_dep_off = bd->bd_offset;
	hs->hs_dep_bits = hs->hs_cur_bits;
	spin_unlock(&hs->hs_dep_lock);

	cfs_wi_schedule(cfs_sched_rehash, &hs->hs_dep_wi);
# endif
}

void
cfs_hash_bd_add_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		       struct hlist_node *hnode)
{
	int rc;

	rc = hs->hs_hops->hop_hnode_add(hs, bd, hnode);
	cfs_hash_bd_dep_record(hs, bd, rc);
	bd->bd_bucket->hsb_version++;
	if (unlikely(!bd->bd_bucket->hsb_version))
		bd->bd_bucket->hsb_version++;
	bd->bd_bucket->hsb_count++;

	if (cfs_hash_with_counter(hs))
		atomic_inc(&hs->hs_count);
	if (!cfs_hash_with_no_itemref(hs))
		cfs_hash_get(hs, hnode);
}
EXPORT_SYMBOL(cfs_hash_bd_add_locked);

void
cfs_hash_bd_del_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd,
		       struct hlist_node *hnode)
{
	hs->hs_hops->hop_hnode_del(hs, bd, hnode);

	LASSERT(bd->bd_bucket->hsb_count > 0);
	bd->bd_bucket->hsb_count--;
	bd->bd_bucket->hsb_version++;
	if (unlikely(!bd->bd_bucket->hsb_version))
		bd->bd_bucket->hsb_version++;

	if (cfs_hash_with_counter(hs)) {
		LASSERT(atomic_read(&hs->hs_count) > 0);
		atomic_dec(&hs->hs_count);
	}
	if (!cfs_hash_with_no_itemref(hs))
		cfs_hash_put_locked(hs, hnode);
}
EXPORT_SYMBOL(cfs_hash_bd_del_locked);

void
cfs_hash_bd_move_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd_old,
			struct cfs_hash_bd *bd_new, struct hlist_node *hnode)
{
	struct cfs_hash_bucket *obkt = bd_old->bd_bucket;
	struct cfs_hash_bucket *nbkt = bd_new->bd_bucket;
	int rc;

	if (!cfs_hash_bd_compare(bd_old, bd_new))
		return;

	/* use cfs_hash_bd_hnode_add/del, to avoid atomic & refcount ops
	 * in cfs_hash_bd_del/add_locked
	 */
	hs->hs_hops->hop_hnode_del(hs, bd_old, hnode);
	rc = hs->hs_hops->hop_hnode_add(hs, bd_new, hnode);
	cfs_hash_bd_dep_record(hs, bd_new, rc);

	LASSERT(obkt->hsb_count > 0);
	obkt->hsb_count--;
	obkt->hsb_version++;
	if (unlikely(!obkt->hsb_version))
		obkt->hsb_version++;
	nbkt->hsb_count++;
	nbkt->hsb_version++;
	if (unlikely(!nbkt->hsb_version))
		nbkt->hsb_version++;
}

enum {
	/** always set, for sanity (avoid ZERO intent) */
	CFS_HS_LOOKUP_MASK_FIND	= BIT(0),
	/** return entry with a ref */
	CFS_HS_LOOKUP_MASK_REF	= BIT(1),
	/** add entry if not existing */
	CFS_HS_LOOKUP_MASK_ADD	= BIT(2),
	/** delete entry, ignore other masks */
	CFS_HS_LOOKUP_MASK_DEL	= BIT(3),
};

enum cfs_hash_lookup_intent {
	/** return item w/o refcount */
	CFS_HS_LOOKUP_IT_PEEK	 = CFS_HS_LOOKUP_MASK_FIND,
	/** return item with refcount */
	CFS_HS_LOOKUP_IT_FIND	 = (CFS_HS_LOOKUP_MASK_FIND |
				    CFS_HS_LOOKUP_MASK_REF),
	/** return item w/o refcount if existed, otherwise add */
	CFS_HS_LOOKUP_IT_ADD	 = (CFS_HS_LOOKUP_MASK_FIND |
				    CFS_HS_LOOKUP_MASK_ADD),
	/** return item with refcount if existed, otherwise add */
	CFS_HS_LOOKUP_IT_FINDADD = (CFS_HS_LOOKUP_IT_FIND |
				    CFS_HS_LOOKUP_MASK_ADD),
	/** delete if existed */
	CFS_HS_LOOKUP_IT_FINDDEL = (CFS_HS_LOOKUP_MASK_FIND |
				    CFS_HS_LOOKUP_MASK_DEL)
};

static struct hlist_node *
cfs_hash_bd_lookup_intent(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			  const void *key, struct hlist_node *hnode,
			  enum cfs_hash_lookup_intent intent)

{
	struct hlist_head *hhead = cfs_hash_bd_hhead(hs, bd);
	struct hlist_node *ehnode;
	struct hlist_node *match;
	int intent_add = intent & CFS_HS_LOOKUP_MASK_ADD;

	/* with this function, we can avoid a lot of useless refcount ops,
	 * which are expensive atomic operations most time.
	 */
	match = intent_add ? NULL : hnode;
	hlist_for_each(ehnode, hhead) {
		if (!cfs_hash_keycmp(hs, key, ehnode))
			continue;

		if (match && match != ehnode) /* can't match */
			continue;

		/* match and ... */
		if (intent & CFS_HS_LOOKUP_MASK_DEL) {
			cfs_hash_bd_del_locked(hs, bd, ehnode);
			return ehnode;
		}

		/* caller wants refcount? */
		if (intent & CFS_HS_LOOKUP_MASK_REF)
			cfs_hash_get(hs, ehnode);
		return ehnode;
	}
	/* no match item */
	if (!intent_add)
		return NULL;

	LASSERT(hnode);
	cfs_hash_bd_add_locked(hs, bd, hnode);
	return hnode;
}

struct hlist_node *
cfs_hash_bd_lookup_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			  const void *key)
{
	return cfs_hash_bd_lookup_intent(hs, bd, key, NULL,
					 CFS_HS_LOOKUP_IT_FIND);
}
EXPORT_SYMBOL(cfs_hash_bd_lookup_locked);

struct hlist_node *
cfs_hash_bd_peek_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			const void *key)
{
	return cfs_hash_bd_lookup_intent(hs, bd, key, NULL,
					 CFS_HS_LOOKUP_IT_PEEK);
}
EXPORT_SYMBOL(cfs_hash_bd_peek_locked);

static void
cfs_hash_multi_bd_lock(struct cfs_hash *hs, struct cfs_hash_bd *bds,
		       unsigned int n, int excl)
{
	struct cfs_hash_bucket *prev = NULL;
	int i;

	/**
	 * bds must be ascendantly ordered by bd->bd_bucket->hsb_index.
	 * NB: it's possible that several bds point to the same bucket but
	 * have different bd::bd_offset, so need take care of deadlock.
	 */
	cfs_hash_for_each_bd(bds, n, i) {
		if (prev == bds[i].bd_bucket)
			continue;

		LASSERT(!prev || prev->hsb_index < bds[i].bd_bucket->hsb_index);
		cfs_hash_bd_lock(hs, &bds[i], excl);
		prev = bds[i].bd_bucket;
	}
}

static void
cfs_hash_multi_bd_unlock(struct cfs_hash *hs, struct cfs_hash_bd *bds,
			 unsigned int n, int excl)
{
	struct cfs_hash_bucket *prev = NULL;
	int i;

	cfs_hash_for_each_bd(bds, n, i) {
		if (prev != bds[i].bd_bucket) {
			cfs_hash_bd_unlock(hs, &bds[i], excl);
			prev = bds[i].bd_bucket;
		}
	}
}

static struct hlist_node *
cfs_hash_multi_bd_lookup_locked(struct cfs_hash *hs, struct cfs_hash_bd *bds,
				unsigned int n, const void *key)
{
	struct hlist_node *ehnode;
	unsigned int i;

	cfs_hash_for_each_bd(bds, n, i) {
		ehnode = cfs_hash_bd_lookup_intent(hs, &bds[i], key, NULL,
						   CFS_HS_LOOKUP_IT_FIND);
		if (ehnode)
			return ehnode;
	}
	return NULL;
}

static struct hlist_node *
cfs_hash_multi_bd_findadd_locked(struct cfs_hash *hs, struct cfs_hash_bd *bds,
				 unsigned int n, const void *key,
				 struct hlist_node *hnode, int noref)
{
	struct hlist_node *ehnode;
	int intent;
	unsigned int i;

	LASSERT(hnode);
	intent = (!noref * CFS_HS_LOOKUP_MASK_REF) | CFS_HS_LOOKUP_IT_PEEK;

	cfs_hash_for_each_bd(bds, n, i) {
		ehnode = cfs_hash_bd_lookup_intent(hs, &bds[i], key,
						   NULL, intent);
		if (ehnode)
			return ehnode;
	}

	if (i == 1) { /* only one bucket */
		cfs_hash_bd_add_locked(hs, &bds[0], hnode);
	} else {
		struct cfs_hash_bd mybd;

		cfs_hash_bd_get(hs, key, &mybd);
		cfs_hash_bd_add_locked(hs, &mybd, hnode);
	}

	return hnode;
}

static struct hlist_node *
cfs_hash_multi_bd_finddel_locked(struct cfs_hash *hs, struct cfs_hash_bd *bds,
				 unsigned int n, const void *key,
				 struct hlist_node *hnode)
{
	struct hlist_node *ehnode;
	unsigned int i;

	cfs_hash_for_each_bd(bds, n, i) {
		ehnode = cfs_hash_bd_lookup_intent(hs, &bds[i], key, hnode,
						   CFS_HS_LOOKUP_IT_FINDDEL);
		if (ehnode)
			return ehnode;
	}
	return NULL;
}

static void
cfs_hash_bd_order(struct cfs_hash_bd *bd1, struct cfs_hash_bd *bd2)
{
	int rc;

	if (!bd2->bd_bucket)
		return;

	if (!bd1->bd_bucket) {
		*bd1 = *bd2;
		bd2->bd_bucket = NULL;
		return;
	}

	rc = cfs_hash_bd_compare(bd1, bd2);
	if (!rc)
		bd2->bd_bucket = NULL;
	else if (rc > 0)
		swap(*bd1, *bd2); /* swap bd1 and bd2 */
}

void
cfs_hash_dual_bd_get(struct cfs_hash *hs, const void *key,
		     struct cfs_hash_bd *bds)
{
	/* NB: caller should hold hs_lock.rw if REHASH is set */
	cfs_hash_bd_from_key(hs, hs->hs_buckets,
			     hs->hs_cur_bits, key, &bds[0]);
	if (likely(!hs->hs_rehash_buckets)) {
		/* no rehash or not rehashing */
		bds[1].bd_bucket = NULL;
		return;
	}

	LASSERT(hs->hs_rehash_bits);
	cfs_hash_bd_from_key(hs, hs->hs_rehash_buckets,
			     hs->hs_rehash_bits, key, &bds[1]);

	cfs_hash_bd_order(&bds[0], &bds[1]);
}

void
cfs_hash_dual_bd_lock(struct cfs_hash *hs, struct cfs_hash_bd *bds, int excl)
{
	cfs_hash_multi_bd_lock(hs, bds, 2, excl);
}

void
cfs_hash_dual_bd_unlock(struct cfs_hash *hs, struct cfs_hash_bd *bds, int excl)
{
	cfs_hash_multi_bd_unlock(hs, bds, 2, excl);
}

struct hlist_node *
cfs_hash_dual_bd_lookup_locked(struct cfs_hash *hs, struct cfs_hash_bd *bds,
			       const void *key)
{
	return cfs_hash_multi_bd_lookup_locked(hs, bds, 2, key);
}

struct hlist_node *
cfs_hash_dual_bd_findadd_locked(struct cfs_hash *hs, struct cfs_hash_bd *bds,
				const void *key, struct hlist_node *hnode,
				int noref)
{
	return cfs_hash_multi_bd_findadd_locked(hs, bds, 2, key,
						hnode, noref);
}

struct hlist_node *
cfs_hash_dual_bd_finddel_locked(struct cfs_hash *hs, struct cfs_hash_bd *bds,
				const void *key, struct hlist_node *hnode)
{
	return cfs_hash_multi_bd_finddel_locked(hs, bds, 2, key, hnode);
}

static void
cfs_hash_buckets_free(struct cfs_hash_bucket **buckets,
		      int bkt_size, int prev_size, int size)
{
	int i;

	for (i = prev_size; i < size; i++) {
		if (buckets[i])
			LIBCFS_FREE(buckets[i], bkt_size);
	}

	LIBCFS_FREE(buckets, sizeof(buckets[0]) * size);
}

/*
 * Create or grow bucket memory. Return old_buckets if no allocation was
 * needed, the newly allocated buckets if allocation was needed and
 * successful, and NULL on error.
 */
static struct cfs_hash_bucket **
cfs_hash_buckets_realloc(struct cfs_hash *hs, struct cfs_hash_bucket **old_bkts,
			 unsigned int old_size, unsigned int new_size)
{
	struct cfs_hash_bucket **new_bkts;
	int i;

	LASSERT(!old_size || old_bkts);

	if (old_bkts && old_size == new_size)
		return old_bkts;

	LIBCFS_ALLOC(new_bkts, sizeof(new_bkts[0]) * new_size);
	if (!new_bkts)
		return NULL;

	if (old_bkts) {
		memcpy(new_bkts, old_bkts,
		       min(old_size, new_size) * sizeof(*old_bkts));
	}

	for (i = old_size; i < new_size; i++) {
		struct hlist_head *hhead;
		struct cfs_hash_bd bd;

		LIBCFS_ALLOC(new_bkts[i], cfs_hash_bkt_size(hs));
		if (!new_bkts[i]) {
			cfs_hash_buckets_free(new_bkts, cfs_hash_bkt_size(hs),
					      old_size, new_size);
			return NULL;
		}

		new_bkts[i]->hsb_index = i;
		new_bkts[i]->hsb_version = 1;	/* shouldn't be zero */
		new_bkts[i]->hsb_depmax = -1;	/* unknown */
		bd.bd_bucket = new_bkts[i];
		cfs_hash_bd_for_each_hlist(hs, &bd, hhead)
			INIT_HLIST_HEAD(hhead);

		if (cfs_hash_with_no_lock(hs) ||
		    cfs_hash_with_no_bktlock(hs))
			continue;

		if (cfs_hash_with_rw_bktlock(hs))
			rwlock_init(&new_bkts[i]->hsb_lock.rw);
		else if (cfs_hash_with_spin_bktlock(hs))
			spin_lock_init(&new_bkts[i]->hsb_lock.spin);
		else
			LBUG(); /* invalid use-case */
	}
	return new_bkts;
}

/**
 * Initialize new libcfs hash, where:
 * @name     - Descriptive hash name
 * @cur_bits - Initial hash table size, in bits
 * @max_bits - Maximum allowed hash table resize, in bits
 * @ops      - Registered hash table operations
 * @flags    - CFS_HASH_REHASH enable synamic hash resizing
 *	     - CFS_HASH_SORT enable chained hash sort
 */
static int cfs_hash_rehash_worker(struct cfs_workitem *wi);

#if CFS_HASH_DEBUG_LEVEL >= CFS_HASH_DEBUG_1
static int cfs_hash_dep_print(struct cfs_workitem *wi)
{
	struct cfs_hash *hs = container_of(wi, struct cfs_hash, hs_dep_wi);
	int dep;
	int bkt;
	int off;
	int bits;

	spin_lock(&hs->hs_dep_lock);
	dep = hs->hs_dep_max;
	bkt = hs->hs_dep_bkt;
	off = hs->hs_dep_off;
	bits = hs->hs_dep_bits;
	spin_unlock(&hs->hs_dep_lock);

	LCONSOLE_WARN("#### HASH %s (bits: %d): max depth %d at bucket %d/%d\n",
		      hs->hs_name, bits, dep, bkt, off);
	spin_lock(&hs->hs_dep_lock);
	hs->hs_dep_bits = 0; /* mark as workitem done */
	spin_unlock(&hs->hs_dep_lock);
	return 0;
}

static void cfs_hash_depth_wi_init(struct cfs_hash *hs)
{
	spin_lock_init(&hs->hs_dep_lock);
	cfs_wi_init(&hs->hs_dep_wi, hs, cfs_hash_dep_print);
}

static void cfs_hash_depth_wi_cancel(struct cfs_hash *hs)
{
	if (cfs_wi_deschedule(cfs_sched_rehash, &hs->hs_dep_wi))
		return;

	spin_lock(&hs->hs_dep_lock);
	while (hs->hs_dep_bits) {
		spin_unlock(&hs->hs_dep_lock);
		cond_resched();
		spin_lock(&hs->hs_dep_lock);
	}
	spin_unlock(&hs->hs_dep_lock);
}

#else /* CFS_HASH_DEBUG_LEVEL < CFS_HASH_DEBUG_1 */

static inline void cfs_hash_depth_wi_init(struct cfs_hash *hs) {}
static inline void cfs_hash_depth_wi_cancel(struct cfs_hash *hs) {}

#endif /* CFS_HASH_DEBUG_LEVEL >= CFS_HASH_DEBUG_1 */

struct cfs_hash *
cfs_hash_create(char *name, unsigned int cur_bits, unsigned int max_bits,
		unsigned int bkt_bits, unsigned int extra_bytes,
		unsigned int min_theta, unsigned int max_theta,
		struct cfs_hash_ops *ops, unsigned int flags)
{
	struct cfs_hash *hs;
	int len;

	BUILD_BUG_ON(CFS_HASH_THETA_BITS >= 15);

	LASSERT(name);
	LASSERT(ops->hs_key);
	LASSERT(ops->hs_hash);
	LASSERT(ops->hs_object);
	LASSERT(ops->hs_keycmp);
	LASSERT(ops->hs_get);
	LASSERT(ops->hs_put_locked);

	if (flags & CFS_HASH_REHASH)
		flags |= CFS_HASH_COUNTER; /* must have counter */

	LASSERT(cur_bits > 0);
	LASSERT(cur_bits >= bkt_bits);
	LASSERT(max_bits >= cur_bits && max_bits < 31);
	LASSERT(ergo(!(flags & CFS_HASH_REHASH), cur_bits == max_bits));
	LASSERT(ergo(flags & CFS_HASH_REHASH, !(flags & CFS_HASH_NO_LOCK)));
	LASSERT(ergo(flags & CFS_HASH_REHASH_KEY, ops->hs_keycpy));

	len = !(flags & CFS_HASH_BIGNAME) ?
	      CFS_HASH_NAME_LEN : CFS_HASH_BIGNAME_LEN;
	LIBCFS_ALLOC(hs, offsetof(struct cfs_hash, hs_name[len]));
	if (!hs)
		return NULL;

	strlcpy(hs->hs_name, name, len);
	hs->hs_flags = flags;

	atomic_set(&hs->hs_refcount, 1);
	atomic_set(&hs->hs_count, 0);

	cfs_hash_lock_setup(hs);
	cfs_hash_hlist_setup(hs);

	hs->hs_cur_bits = (u8)cur_bits;
	hs->hs_min_bits = (u8)cur_bits;
	hs->hs_max_bits = (u8)max_bits;
	hs->hs_bkt_bits = (u8)bkt_bits;

	hs->hs_ops = ops;
	hs->hs_extra_bytes = extra_bytes;
	hs->hs_rehash_bits = 0;
	cfs_wi_init(&hs->hs_rehash_wi, hs, cfs_hash_rehash_worker);
	cfs_hash_depth_wi_init(hs);

	if (cfs_hash_with_rehash(hs))
		__cfs_hash_set_theta(hs, min_theta, max_theta);

	hs->hs_buckets = cfs_hash_buckets_realloc(hs, NULL, 0,
						  CFS_HASH_NBKT(hs));
	if (hs->hs_buckets)
		return hs;

	LIBCFS_FREE(hs, offsetof(struct cfs_hash, hs_name[len]));
	return NULL;
}
EXPORT_SYMBOL(cfs_hash_create);

/**
 * Cleanup libcfs hash @hs.
 */
static void
cfs_hash_destroy(struct cfs_hash *hs)
{
	struct hlist_node *hnode;
	struct hlist_node *pos;
	struct cfs_hash_bd bd;
	int i;

	LASSERT(hs);
	LASSERT(!cfs_hash_is_exiting(hs) &&
		!cfs_hash_is_iterating(hs));

	/**
	 * prohibit further rehashes, don't need any lock because
	 * I'm the only (last) one can change it.
	 */
	hs->hs_exiting = 1;
	if (cfs_hash_with_rehash(hs))
		cfs_hash_rehash_cancel(hs);

	cfs_hash_depth_wi_cancel(hs);
	/* rehash should be done/canceled */
	LASSERT(hs->hs_buckets && !hs->hs_rehash_buckets);

	cfs_hash_for_each_bucket(hs, &bd, i) {
		struct hlist_head *hhead;

		LASSERT(bd.bd_bucket);
		/* no need to take this lock, just for consistent code */
		cfs_hash_bd_lock(hs, &bd, 1);

		cfs_hash_bd_for_each_hlist(hs, &bd, hhead) {
			hlist_for_each_safe(hnode, pos, hhead) {
				LASSERTF(!cfs_hash_with_assert_empty(hs),
					 "hash %s bucket %u(%u) is not empty: %u items left\n",
					 hs->hs_name, bd.bd_bucket->hsb_index,
					 bd.bd_offset, bd.bd_bucket->hsb_count);
				/* can't assert key valicate, because we
				 * can interrupt rehash
				 */
				cfs_hash_bd_del_locked(hs, &bd, hnode);
				cfs_hash_exit(hs, hnode);
			}
		}
		LASSERT(!bd.bd_bucket->hsb_count);
		cfs_hash_bd_unlock(hs, &bd, 1);
		cond_resched();
	}

	LASSERT(!atomic_read(&hs->hs_count));

	cfs_hash_buckets_free(hs->hs_buckets, cfs_hash_bkt_size(hs),
			      0, CFS_HASH_NBKT(hs));
	i = cfs_hash_with_bigname(hs) ?
	    CFS_HASH_BIGNAME_LEN : CFS_HASH_NAME_LEN;
	LIBCFS_FREE(hs, offsetof(struct cfs_hash, hs_name[i]));
}

struct cfs_hash *cfs_hash_getref(struct cfs_hash *hs)
{
	if (atomic_inc_not_zero(&hs->hs_refcount))
		return hs;
	return NULL;
}
EXPORT_SYMBOL(cfs_hash_getref);

void cfs_hash_putref(struct cfs_hash *hs)
{
	if (atomic_dec_and_test(&hs->hs_refcount))
		cfs_hash_destroy(hs);
}
EXPORT_SYMBOL(cfs_hash_putref);

static inline int
cfs_hash_rehash_bits(struct cfs_hash *hs)
{
	if (cfs_hash_with_no_lock(hs) ||
	    !cfs_hash_with_rehash(hs))
		return -EOPNOTSUPP;

	if (unlikely(cfs_hash_is_exiting(hs)))
		return -ESRCH;

	if (unlikely(cfs_hash_is_rehashing(hs)))
		return -EALREADY;

	if (unlikely(cfs_hash_is_iterating(hs)))
		return -EAGAIN;

	/* XXX: need to handle case with max_theta != 2.0
	 *      and the case with min_theta != 0.5
	 */
	if ((hs->hs_cur_bits < hs->hs_max_bits) &&
	    (__cfs_hash_theta(hs) > hs->hs_max_theta))
		return hs->hs_cur_bits + 1;

	if (!cfs_hash_with_shrink(hs))
		return 0;

	if ((hs->hs_cur_bits > hs->hs_min_bits) &&
	    (__cfs_hash_theta(hs) < hs->hs_min_theta))
		return hs->hs_cur_bits - 1;

	return 0;
}

/**
 * don't allow inline rehash if:
 * - user wants non-blocking change (add/del) on hash table
 * - too many elements
 */
static inline int
cfs_hash_rehash_inline(struct cfs_hash *hs)
{
	return !cfs_hash_with_nblk_change(hs) &&
	       atomic_read(&hs->hs_count) < CFS_HASH_LOOP_HOG;
}

/**
 * Add item @hnode to libcfs hash @hs using @key.  The registered
 * ops->hs_get function will be called when the item is added.
 */
void
cfs_hash_add(struct cfs_hash *hs, const void *key, struct hlist_node *hnode)
{
	struct cfs_hash_bd bd;
	int bits;

	LASSERT(hlist_unhashed(hnode));

	cfs_hash_lock(hs, 0);
	cfs_hash_bd_get_and_lock(hs, key, &bd, 1);

	cfs_hash_key_validate(hs, key, hnode);
	cfs_hash_bd_add_locked(hs, &bd, hnode);

	cfs_hash_bd_unlock(hs, &bd, 1);

	bits = cfs_hash_rehash_bits(hs);
	cfs_hash_unlock(hs, 0);
	if (bits > 0)
		cfs_hash_rehash(hs, cfs_hash_rehash_inline(hs));
}
EXPORT_SYMBOL(cfs_hash_add);

static struct hlist_node *
cfs_hash_find_or_add(struct cfs_hash *hs, const void *key,
		     struct hlist_node *hnode, int noref)
{
	struct hlist_node *ehnode;
	struct cfs_hash_bd bds[2];
	int bits = 0;

	LASSERTF(hlist_unhashed(hnode), "hnode = %p\n", hnode);

	cfs_hash_lock(hs, 0);
	cfs_hash_dual_bd_get_and_lock(hs, key, bds, 1);

	cfs_hash_key_validate(hs, key, hnode);
	ehnode = cfs_hash_dual_bd_findadd_locked(hs, bds, key,
						 hnode, noref);
	cfs_hash_dual_bd_unlock(hs, bds, 1);

	if (ehnode == hnode)	/* new item added */
		bits = cfs_hash_rehash_bits(hs);
	cfs_hash_unlock(hs, 0);
	if (bits > 0)
		cfs_hash_rehash(hs, cfs_hash_rehash_inline(hs));

	return ehnode;
}

/**
 * Add item @hnode to libcfs hash @hs using @key.  The registered
 * ops->hs_get function will be called if the item was added.
 * Returns 0 on success or -EALREADY on key collisions.
 */
int
cfs_hash_add_unique(struct cfs_hash *hs, const void *key,
		    struct hlist_node *hnode)
{
	return cfs_hash_find_or_add(hs, key, hnode, 1) != hnode ?
	       -EALREADY : 0;
}
EXPORT_SYMBOL(cfs_hash_add_unique);

/**
 * Add item @hnode to libcfs hash @hs using @key.  If this @key
 * already exists in the hash then ops->hs_get will be called on the
 * conflicting entry and that entry will be returned to the caller.
 * Otherwise ops->hs_get is called on the item which was added.
 */
void *
cfs_hash_findadd_unique(struct cfs_hash *hs, const void *key,
			struct hlist_node *hnode)
{
	hnode = cfs_hash_find_or_add(hs, key, hnode, 0);

	return cfs_hash_object(hs, hnode);
}
EXPORT_SYMBOL(cfs_hash_findadd_unique);

/**
 * Delete item @hnode from the libcfs hash @hs using @key.  The @key
 * is required to ensure the correct hash bucket is locked since there
 * is no direct linkage from the item to the bucket.  The object
 * removed from the hash will be returned and obs->hs_put is called
 * on the removed object.
 */
void *
cfs_hash_del(struct cfs_hash *hs, const void *key, struct hlist_node *hnode)
{
	void *obj = NULL;
	int bits = 0;
	struct cfs_hash_bd bds[2];

	cfs_hash_lock(hs, 0);
	cfs_hash_dual_bd_get_and_lock(hs, key, bds, 1);

	/* NB: do nothing if @hnode is not in hash table */
	if (!hnode || !hlist_unhashed(hnode)) {
		if (!bds[1].bd_bucket && hnode) {
			cfs_hash_bd_del_locked(hs, &bds[0], hnode);
		} else {
			hnode = cfs_hash_dual_bd_finddel_locked(hs, bds,
								key, hnode);
		}
	}

	if (hnode) {
		obj = cfs_hash_object(hs, hnode);
		bits = cfs_hash_rehash_bits(hs);
	}

	cfs_hash_dual_bd_unlock(hs, bds, 1);
	cfs_hash_unlock(hs, 0);
	if (bits > 0)
		cfs_hash_rehash(hs, cfs_hash_rehash_inline(hs));

	return obj;
}
EXPORT_SYMBOL(cfs_hash_del);

/**
 * Delete item given @key in libcfs hash @hs.  The first @key found in
 * the hash will be removed, if the key exists multiple times in the hash
 * @hs this function must be called once per key.  The removed object
 * will be returned and ops->hs_put is called on the removed object.
 */
void *
cfs_hash_del_key(struct cfs_hash *hs, const void *key)
{
	return cfs_hash_del(hs, key, NULL);
}
EXPORT_SYMBOL(cfs_hash_del_key);

/**
 * Lookup an item using @key in the libcfs hash @hs and return it.
 * If the @key is found in the hash hs->hs_get() is called and the
 * matching objects is returned.  It is the callers responsibility
 * to call the counterpart ops->hs_put using the cfs_hash_put() macro
 * when when finished with the object.  If the @key was not found
 * in the hash @hs NULL is returned.
 */
void *
cfs_hash_lookup(struct cfs_hash *hs, const void *key)
{
	void *obj = NULL;
	struct hlist_node *hnode;
	struct cfs_hash_bd bds[2];

	cfs_hash_lock(hs, 0);
	cfs_hash_dual_bd_get_and_lock(hs, key, bds, 0);

	hnode = cfs_hash_dual_bd_lookup_locked(hs, bds, key);
	if (hnode)
		obj = cfs_hash_object(hs, hnode);

	cfs_hash_dual_bd_unlock(hs, bds, 0);
	cfs_hash_unlock(hs, 0);

	return obj;
}
EXPORT_SYMBOL(cfs_hash_lookup);

static void
cfs_hash_for_each_enter(struct cfs_hash *hs)
{
	LASSERT(!cfs_hash_is_exiting(hs));

	if (!cfs_hash_with_rehash(hs))
		return;
	/*
	 * NB: it's race on cfs_has_t::hs_iterating, but doesn't matter
	 * because it's just an unreliable signal to rehash-thread,
	 * rehash-thread will try to finish rehash ASAP when seeing this.
	 */
	hs->hs_iterating = 1;

	cfs_hash_lock(hs, 1);
	hs->hs_iterators++;

	/* NB: iteration is mostly called by service thread,
	 * we tend to cancel pending rehash-request, instead of
	 * blocking service thread, we will relaunch rehash request
	 * after iteration
	 */
	if (cfs_hash_is_rehashing(hs))
		cfs_hash_rehash_cancel_locked(hs);
	cfs_hash_unlock(hs, 1);
}

static void
cfs_hash_for_each_exit(struct cfs_hash *hs)
{
	int remained;
	int bits;

	if (!cfs_hash_with_rehash(hs))
		return;
	cfs_hash_lock(hs, 1);
	remained = --hs->hs_iterators;
	bits = cfs_hash_rehash_bits(hs);
	cfs_hash_unlock(hs, 1);
	/* NB: it's race on cfs_has_t::hs_iterating, see above */
	if (!remained)
		hs->hs_iterating = 0;
	if (bits > 0) {
		cfs_hash_rehash(hs, atomic_read(&hs->hs_count) <
				    CFS_HASH_LOOP_HOG);
	}
}

/**
 * For each item in the libcfs hash @hs call the passed callback @func
 * and pass to it as an argument each hash item and the private @data.
 *
 * a) the function may sleep!
 * b) during the callback:
 *    . the bucket lock is held so the callback must never sleep.
 *    . if @removal_safe is true, use can remove current item by
 *      cfs_hash_bd_del_locked
 */
static u64
cfs_hash_for_each_tight(struct cfs_hash *hs, cfs_hash_for_each_cb_t func,
			void *data, int remove_safe)
{
	struct hlist_node *hnode;
	struct hlist_node *pos;
	struct cfs_hash_bd bd;
	u64 count = 0;
	int excl = !!remove_safe;
	int loop = 0;
	int i;

	cfs_hash_for_each_enter(hs);

	cfs_hash_lock(hs, 0);
	LASSERT(!cfs_hash_is_rehashing(hs));

	cfs_hash_for_each_bucket(hs, &bd, i) {
		struct hlist_head *hhead;

		cfs_hash_bd_lock(hs, &bd, excl);
		if (!func) { /* only glimpse size */
			count += bd.bd_bucket->hsb_count;
			cfs_hash_bd_unlock(hs, &bd, excl);
			continue;
		}

		cfs_hash_bd_for_each_hlist(hs, &bd, hhead) {
			hlist_for_each_safe(hnode, pos, hhead) {
				cfs_hash_bucket_validate(hs, &bd, hnode);
				count++;
				loop++;
				if (func(hs, &bd, hnode, data)) {
					cfs_hash_bd_unlock(hs, &bd, excl);
					goto out;
				}
			}
		}
		cfs_hash_bd_unlock(hs, &bd, excl);
		if (loop < CFS_HASH_LOOP_HOG)
			continue;
		loop = 0;
		cfs_hash_unlock(hs, 0);
		cond_resched();
		cfs_hash_lock(hs, 0);
	}
 out:
	cfs_hash_unlock(hs, 0);

	cfs_hash_for_each_exit(hs);
	return count;
}

struct cfs_hash_cond_arg {
	cfs_hash_cond_opt_cb_t	func;
	void			*arg;
};

static int
cfs_hash_cond_del_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			 struct hlist_node *hnode, void *data)
{
	struct cfs_hash_cond_arg *cond = data;

	if (cond->func(cfs_hash_object(hs, hnode), cond->arg))
		cfs_hash_bd_del_locked(hs, bd, hnode);
	return 0;
}

/**
 * Delete item from the libcfs hash @hs when @func return true.
 * The write lock being hold during loop for each bucket to avoid
 * any object be reference.
 */
void
cfs_hash_cond_del(struct cfs_hash *hs, cfs_hash_cond_opt_cb_t func, void *data)
{
	struct cfs_hash_cond_arg arg = {
		.func	= func,
		.arg	= data,
	};

	cfs_hash_for_each_tight(hs, cfs_hash_cond_del_locked, &arg, 1);
}
EXPORT_SYMBOL(cfs_hash_cond_del);

void
cfs_hash_for_each(struct cfs_hash *hs, cfs_hash_for_each_cb_t func,
		  void *data)
{
	cfs_hash_for_each_tight(hs, func, data, 0);
}
EXPORT_SYMBOL(cfs_hash_for_each);

void
cfs_hash_for_each_safe(struct cfs_hash *hs, cfs_hash_for_each_cb_t func,
		       void *data)
{
	cfs_hash_for_each_tight(hs, func, data, 1);
}
EXPORT_SYMBOL(cfs_hash_for_each_safe);

static int
cfs_hash_peek(struct cfs_hash *hs, struct cfs_hash_bd *bd,
	      struct hlist_node *hnode, void *data)
{
	*(int *)data = 0;
	return 1; /* return 1 to break the loop */
}

int
cfs_hash_is_empty(struct cfs_hash *hs)
{
	int empty = 1;

	cfs_hash_for_each_tight(hs, cfs_hash_peek, &empty, 0);
	return empty;
}
EXPORT_SYMBOL(cfs_hash_is_empty);

u64
cfs_hash_size_get(struct cfs_hash *hs)
{
	return cfs_hash_with_counter(hs) ?
	       atomic_read(&hs->hs_count) :
	       cfs_hash_for_each_tight(hs, NULL, NULL, 0);
}
EXPORT_SYMBOL(cfs_hash_size_get);

/*
 * cfs_hash_for_each_relax:
 * Iterate the hash table and call @func on each item without
 * any lock. This function can't guarantee to finish iteration
 * if these features are enabled:
 *
 *  a. if rehash_key is enabled, an item can be moved from
 *     one bucket to another bucket
 *  b. user can remove non-zero-ref item from hash-table,
 *     so the item can be removed from hash-table, even worse,
 *     it's possible that user changed key and insert to another
 *     hash bucket.
 * there's no way for us to finish iteration correctly on previous
 * two cases, so iteration has to be stopped on change.
 */
static int
cfs_hash_for_each_relax(struct cfs_hash *hs, cfs_hash_for_each_cb_t func,
			void *data, int start)
{
	struct hlist_node *hnode;
	struct hlist_node *tmp;
	struct cfs_hash_bd bd;
	u32 version;
	int count = 0;
	int stop_on_change;
	int end = -1;
	int rc = 0;
	int i;

	stop_on_change = cfs_hash_with_rehash_key(hs) ||
			 !cfs_hash_with_no_itemref(hs) ||
			 !hs->hs_ops->hs_put_locked;
	cfs_hash_lock(hs, 0);
again:
	LASSERT(!cfs_hash_is_rehashing(hs));

	cfs_hash_for_each_bucket(hs, &bd, i) {
		struct hlist_head *hhead;

		if (i < start)
			continue;
		else if (end > 0 && i >= end)
			break;

		cfs_hash_bd_lock(hs, &bd, 0);
		version = cfs_hash_bd_version_get(&bd);

		cfs_hash_bd_for_each_hlist(hs, &bd, hhead) {
			for (hnode = hhead->first; hnode;) {
				cfs_hash_bucket_validate(hs, &bd, hnode);
				cfs_hash_get(hs, hnode);
				cfs_hash_bd_unlock(hs, &bd, 0);
				cfs_hash_unlock(hs, 0);

				rc = func(hs, &bd, hnode, data);
				if (stop_on_change)
					cfs_hash_put(hs, hnode);
				cond_resched();
				count++;

				cfs_hash_lock(hs, 0);
				cfs_hash_bd_lock(hs, &bd, 0);
				if (!stop_on_change) {
					tmp = hnode->next;
					cfs_hash_put_locked(hs, hnode);
					hnode = tmp;
				} else { /* bucket changed? */
					if (version !=
					    cfs_hash_bd_version_get(&bd))
						break;
					/* safe to continue because no change */
					hnode = hnode->next;
				}
				if (rc) /* callback wants to break iteration */
					break;
			}
			if (rc) /* callback wants to break iteration */
				break;
		}
		cfs_hash_bd_unlock(hs, &bd, 0);
		if (rc) /* callback wants to break iteration */
			break;
	}
	if (start > 0 && !rc) {
		end = start;
		start = 0;
		goto again;
	}

	cfs_hash_unlock(hs, 0);
	return count;
}

int
cfs_hash_for_each_nolock(struct cfs_hash *hs, cfs_hash_for_each_cb_t func,
			 void *data, int start)
{
	if (cfs_hash_with_no_lock(hs) ||
	    cfs_hash_with_rehash_key(hs) ||
	    !cfs_hash_with_no_itemref(hs))
		return -EOPNOTSUPP;

	if (!hs->hs_ops->hs_get ||
	    (!hs->hs_ops->hs_put && !hs->hs_ops->hs_put_locked))
		return -EOPNOTSUPP;

	cfs_hash_for_each_enter(hs);
	cfs_hash_for_each_relax(hs, func, data, start);
	cfs_hash_for_each_exit(hs);

	return 0;
}
EXPORT_SYMBOL(cfs_hash_for_each_nolock);

/**
 * For each hash bucket in the libcfs hash @hs call the passed callback
 * @func until all the hash buckets are empty.  The passed callback @func
 * or the previously registered callback hs->hs_put must remove the item
 * from the hash.  You may either use the cfs_hash_del() or hlist_del()
 * functions.  No rwlocks will be held during the callback @func it is
 * safe to sleep if needed.  This function will not terminate until the
 * hash is empty.  Note it is still possible to concurrently add new
 * items in to the hash.  It is the callers responsibility to ensure
 * the required locking is in place to prevent concurrent insertions.
 */
int
cfs_hash_for_each_empty(struct cfs_hash *hs, cfs_hash_for_each_cb_t func,
			void *data)
{
	unsigned int i = 0;

	if (cfs_hash_with_no_lock(hs))
		return -EOPNOTSUPP;

	if (!hs->hs_ops->hs_get ||
	    (!hs->hs_ops->hs_put && !hs->hs_ops->hs_put_locked))
		return -EOPNOTSUPP;

	cfs_hash_for_each_enter(hs);
	while (cfs_hash_for_each_relax(hs, func, data, 0)) {
		CDEBUG(D_INFO, "Try to empty hash: %s, loop: %u\n",
		       hs->hs_name, i++);
	}
	cfs_hash_for_each_exit(hs);
	return 0;
}
EXPORT_SYMBOL(cfs_hash_for_each_empty);

void
cfs_hash_hlist_for_each(struct cfs_hash *hs, unsigned int hindex,
			cfs_hash_for_each_cb_t func, void *data)
{
	struct hlist_head *hhead;
	struct hlist_node *hnode;
	struct cfs_hash_bd bd;

	cfs_hash_for_each_enter(hs);
	cfs_hash_lock(hs, 0);
	if (hindex >= CFS_HASH_NHLIST(hs))
		goto out;

	cfs_hash_bd_index_set(hs, hindex, &bd);

	cfs_hash_bd_lock(hs, &bd, 0);
	hhead = cfs_hash_bd_hhead(hs, &bd);
	hlist_for_each(hnode, hhead) {
		if (func(hs, &bd, hnode, data))
			break;
	}
	cfs_hash_bd_unlock(hs, &bd, 0);
out:
	cfs_hash_unlock(hs, 0);
	cfs_hash_for_each_exit(hs);
}
EXPORT_SYMBOL(cfs_hash_hlist_for_each);

/*
 * For each item in the libcfs hash @hs which matches the @key call
 * the passed callback @func and pass to it as an argument each hash
 * item and the private @data. During the callback the bucket lock
 * is held so the callback must never sleep.
 */
void
cfs_hash_for_each_key(struct cfs_hash *hs, const void *key,
		      cfs_hash_for_each_cb_t func, void *data)
{
	struct hlist_node *hnode;
	struct cfs_hash_bd bds[2];
	unsigned int i;

	cfs_hash_lock(hs, 0);

	cfs_hash_dual_bd_get_and_lock(hs, key, bds, 0);

	cfs_hash_for_each_bd(bds, 2, i) {
		struct hlist_head *hlist = cfs_hash_bd_hhead(hs, &bds[i]);

		hlist_for_each(hnode, hlist) {
			cfs_hash_bucket_validate(hs, &bds[i], hnode);

			if (cfs_hash_keycmp(hs, key, hnode)) {
				if (func(hs, &bds[i], hnode, data))
					break;
			}
		}
	}

	cfs_hash_dual_bd_unlock(hs, bds, 0);
	cfs_hash_unlock(hs, 0);
}
EXPORT_SYMBOL(cfs_hash_for_each_key);

/**
 * Rehash the libcfs hash @hs to the given @bits.  This can be used
 * to grow the hash size when excessive chaining is detected, or to
 * shrink the hash when it is larger than needed.  When the CFS_HASH_REHASH
 * flag is set in @hs the libcfs hash may be dynamically rehashed
 * during addition or removal if the hash's theta value exceeds
 * either the hs->hs_min_theta or hs->max_theta values.  By default
 * these values are tuned to keep the chained hash depth small, and
 * this approach assumes a reasonably uniform hashing function.  The
 * theta thresholds for @hs are tunable via cfs_hash_set_theta().
 */
void
cfs_hash_rehash_cancel_locked(struct cfs_hash *hs)
{
	int i;

	/* need hold cfs_hash_lock(hs, 1) */
	LASSERT(cfs_hash_with_rehash(hs) &&
		!cfs_hash_with_no_lock(hs));

	if (!cfs_hash_is_rehashing(hs))
		return;

	if (cfs_wi_deschedule(cfs_sched_rehash, &hs->hs_rehash_wi)) {
		hs->hs_rehash_bits = 0;
		return;
	}

	for (i = 2; cfs_hash_is_rehashing(hs); i++) {
		cfs_hash_unlock(hs, 1);
		/* raise console warning while waiting too long */
		CDEBUG(is_power_of_2(i >> 3) ? D_WARNING : D_INFO,
		       "hash %s is still rehashing, rescheded %d\n",
		       hs->hs_name, i - 1);
		cond_resched();
		cfs_hash_lock(hs, 1);
	}
}

void
cfs_hash_rehash_cancel(struct cfs_hash *hs)
{
	cfs_hash_lock(hs, 1);
	cfs_hash_rehash_cancel_locked(hs);
	cfs_hash_unlock(hs, 1);
}

int
cfs_hash_rehash(struct cfs_hash *hs, int do_rehash)
{
	int rc;

	LASSERT(cfs_hash_with_rehash(hs) && !cfs_hash_with_no_lock(hs));

	cfs_hash_lock(hs, 1);

	rc = cfs_hash_rehash_bits(hs);
	if (rc <= 0) {
		cfs_hash_unlock(hs, 1);
		return rc;
	}

	hs->hs_rehash_bits = rc;
	if (!do_rehash) {
		/* launch and return */
		cfs_wi_schedule(cfs_sched_rehash, &hs->hs_rehash_wi);
		cfs_hash_unlock(hs, 1);
		return 0;
	}

	/* rehash right now */
	cfs_hash_unlock(hs, 1);

	return cfs_hash_rehash_worker(&hs->hs_rehash_wi);
}

static int
cfs_hash_rehash_bd(struct cfs_hash *hs, struct cfs_hash_bd *old)
{
	struct cfs_hash_bd new;
	struct hlist_head *hhead;
	struct hlist_node *hnode;
	struct hlist_node *pos;
	void *key;
	int c = 0;

	/* hold cfs_hash_lock(hs, 1), so don't need any bucket lock */
	cfs_hash_bd_for_each_hlist(hs, old, hhead) {
		hlist_for_each_safe(hnode, pos, hhead) {
			key = cfs_hash_key(hs, hnode);
			LASSERT(key);
			/* Validate hnode is in the correct bucket. */
			cfs_hash_bucket_validate(hs, old, hnode);
			/*
			 * Delete from old hash bucket; move to new bucket.
			 * ops->hs_key must be defined.
			 */
			cfs_hash_bd_from_key(hs, hs->hs_rehash_buckets,
					     hs->hs_rehash_bits, key, &new);
			cfs_hash_bd_move_locked(hs, old, &new, hnode);
			c++;
		}
	}

	return c;
}

static int
cfs_hash_rehash_worker(struct cfs_workitem *wi)
{
	struct cfs_hash *hs = container_of(wi, struct cfs_hash, hs_rehash_wi);
	struct cfs_hash_bucket **bkts;
	struct cfs_hash_bd bd;
	unsigned int old_size;
	unsigned int new_size;
	int bsize;
	int count = 0;
	int rc = 0;
	int i;

	LASSERT(hs && cfs_hash_with_rehash(hs));

	cfs_hash_lock(hs, 0);
	LASSERT(cfs_hash_is_rehashing(hs));

	old_size = CFS_HASH_NBKT(hs);
	new_size = CFS_HASH_RH_NBKT(hs);

	cfs_hash_unlock(hs, 0);

	/*
	 * don't need hs::hs_rwlock for hs::hs_buckets,
	 * because nobody can change bkt-table except me.
	 */
	bkts = cfs_hash_buckets_realloc(hs, hs->hs_buckets,
					old_size, new_size);
	cfs_hash_lock(hs, 1);
	if (!bkts) {
		rc = -ENOMEM;
		goto out;
	}

	if (bkts == hs->hs_buckets) {
		bkts = NULL; /* do nothing */
		goto out;
	}

	rc = __cfs_hash_theta(hs);
	if ((rc >= hs->hs_min_theta) && (rc <= hs->hs_max_theta)) {
		/* free the new allocated bkt-table */
		old_size = new_size;
		new_size = CFS_HASH_NBKT(hs);
		rc = -EALREADY;
		goto out;
	}

	LASSERT(!hs->hs_rehash_buckets);
	hs->hs_rehash_buckets = bkts;

	rc = 0;
	cfs_hash_for_each_bucket(hs, &bd, i) {
		if (cfs_hash_is_exiting(hs)) {
			rc = -ESRCH;
			/* someone wants to destroy the hash, abort now */
			if (old_size < new_size) /* OK to free old bkt-table */
				break;
			/* it's shrinking, need free new bkt-table */
			hs->hs_rehash_buckets = NULL;
			old_size = new_size;
			new_size = CFS_HASH_NBKT(hs);
			goto out;
		}

		count += cfs_hash_rehash_bd(hs, &bd);
		if (count < CFS_HASH_LOOP_HOG ||
		    cfs_hash_is_iterating(hs)) { /* need to finish ASAP */
			continue;
		}

		count = 0;
		cfs_hash_unlock(hs, 1);
		cond_resched();
		cfs_hash_lock(hs, 1);
	}

	hs->hs_rehash_count++;

	bkts = hs->hs_buckets;
	hs->hs_buckets = hs->hs_rehash_buckets;
	hs->hs_rehash_buckets = NULL;

	hs->hs_cur_bits = hs->hs_rehash_bits;
out:
	hs->hs_rehash_bits = 0;
	if (rc == -ESRCH) /* never be scheduled again */
		cfs_wi_exit(cfs_sched_rehash, wi);
	bsize = cfs_hash_bkt_size(hs);
	cfs_hash_unlock(hs, 1);
	/* can't refer to @hs anymore because it could be destroyed */
	if (bkts)
		cfs_hash_buckets_free(bkts, bsize, new_size, old_size);
	if (rc)
		CDEBUG(D_INFO, "early quit of rehashing: %d\n", rc);
	/* return 1 only if cfs_wi_exit is called */
	return rc == -ESRCH;
}

/**
 * Rehash the object referenced by @hnode in the libcfs hash @hs.  The
 * @old_key must be provided to locate the objects previous location
 * in the hash, and the @new_key will be used to reinsert the object.
 * Use this function instead of a cfs_hash_add() + cfs_hash_del()
 * combo when it is critical that there is no window in time where the
 * object is missing from the hash.  When an object is being rehashed
 * the registered cfs_hash_get() and cfs_hash_put() functions will
 * not be called.
 */
void cfs_hash_rehash_key(struct cfs_hash *hs, const void *old_key,
			 void *new_key, struct hlist_node *hnode)
{
	struct cfs_hash_bd bds[3];
	struct cfs_hash_bd old_bds[2];
	struct cfs_hash_bd new_bd;

	LASSERT(!hlist_unhashed(hnode));

	cfs_hash_lock(hs, 0);

	cfs_hash_dual_bd_get(hs, old_key, old_bds);
	cfs_hash_bd_get(hs, new_key, &new_bd);

	bds[0] = old_bds[0];
	bds[1] = old_bds[1];
	bds[2] = new_bd;

	/* NB: bds[0] and bds[1] are ordered already */
	cfs_hash_bd_order(&bds[1], &bds[2]);
	cfs_hash_bd_order(&bds[0], &bds[1]);

	cfs_hash_multi_bd_lock(hs, bds, 3, 1);
	if (likely(!old_bds[1].bd_bucket)) {
		cfs_hash_bd_move_locked(hs, &old_bds[0], &new_bd, hnode);
	} else {
		cfs_hash_dual_bd_finddel_locked(hs, old_bds, old_key, hnode);
		cfs_hash_bd_add_locked(hs, &new_bd, hnode);
	}
	/* overwrite key inside locks, otherwise may screw up with
	 * other operations, i.e: rehash
	 */
	cfs_hash_keycpy(hs, hnode, new_key);

	cfs_hash_multi_bd_unlock(hs, bds, 3, 1);
	cfs_hash_unlock(hs, 0);
}
EXPORT_SYMBOL(cfs_hash_rehash_key);

void cfs_hash_debug_header(struct seq_file *m)
{
	seq_printf(m, "%-*s   cur   min   max theta t-min t-max flags rehash   count  maxdep maxdepb distribution\n",
		   CFS_HASH_BIGNAME_LEN, "name");
}
EXPORT_SYMBOL(cfs_hash_debug_header);

static struct cfs_hash_bucket **
cfs_hash_full_bkts(struct cfs_hash *hs)
{
	/* NB: caller should hold hs->hs_rwlock if REHASH is set */
	if (!hs->hs_rehash_buckets)
		return hs->hs_buckets;

	LASSERT(hs->hs_rehash_bits);
	return hs->hs_rehash_bits > hs->hs_cur_bits ?
	       hs->hs_rehash_buckets : hs->hs_buckets;
}

static unsigned int
cfs_hash_full_nbkt(struct cfs_hash *hs)
{
	/* NB: caller should hold hs->hs_rwlock if REHASH is set */
	if (!hs->hs_rehash_buckets)
		return CFS_HASH_NBKT(hs);

	LASSERT(hs->hs_rehash_bits);
	return hs->hs_rehash_bits > hs->hs_cur_bits ?
	       CFS_HASH_RH_NBKT(hs) : CFS_HASH_NBKT(hs);
}

void cfs_hash_debug_str(struct cfs_hash *hs, struct seq_file *m)
{
	int dist[8] = { 0, };
	int maxdep = -1;
	int maxdepb = -1;
	int total = 0;
	int theta;
	int i;

	cfs_hash_lock(hs, 0);
	theta = __cfs_hash_theta(hs);

	seq_printf(m, "%-*s %5d %5d %5d %d.%03d %d.%03d %d.%03d  0x%02x %6d ",
		   CFS_HASH_BIGNAME_LEN, hs->hs_name,
		   1 << hs->hs_cur_bits, 1 << hs->hs_min_bits,
		   1 << hs->hs_max_bits,
		   __cfs_hash_theta_int(theta), __cfs_hash_theta_frac(theta),
		   __cfs_hash_theta_int(hs->hs_min_theta),
		   __cfs_hash_theta_frac(hs->hs_min_theta),
		   __cfs_hash_theta_int(hs->hs_max_theta),
		   __cfs_hash_theta_frac(hs->hs_max_theta),
		   hs->hs_flags, hs->hs_rehash_count);

	/*
	 * The distribution is a summary of the chained hash depth in
	 * each of the libcfs hash buckets.  Each buckets hsb_count is
	 * divided by the hash theta value and used to generate a
	 * histogram of the hash distribution.  A uniform hash will
	 * result in all hash buckets being close to the average thus
	 * only the first few entries in the histogram will be non-zero.
	 * If you hash function results in a non-uniform hash the will
	 * be observable by outlier bucks in the distribution histogram.
	 *
	 * Uniform hash distribution:		128/128/0/0/0/0/0/0
	 * Non-Uniform hash distribution:	128/125/0/0/0/0/2/1
	 */
	for (i = 0; i < cfs_hash_full_nbkt(hs); i++) {
		struct cfs_hash_bd bd;

		bd.bd_bucket = cfs_hash_full_bkts(hs)[i];
		cfs_hash_bd_lock(hs, &bd, 0);
		if (maxdep < bd.bd_bucket->hsb_depmax) {
			maxdep  = bd.bd_bucket->hsb_depmax;
			maxdepb = ffz(~maxdep);
		}
		total += bd.bd_bucket->hsb_count;
		dist[min(fls(bd.bd_bucket->hsb_count / max(theta, 1)), 7)]++;
		cfs_hash_bd_unlock(hs, &bd, 0);
	}

	seq_printf(m, "%7d %7d %7d ", total, maxdep, maxdepb);
	for (i = 0; i < 8; i++)
		seq_printf(m, "%d%c",  dist[i], (i == 7) ? '\n' : '/');

	cfs_hash_unlock(hs, 0);
}
EXPORT_SYMBOL(cfs_hash_debug_str);
