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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/libcfs_hash.h
 *
 * Hashing routines
 *
 */

#ifndef __LIBCFS_HASH_H__
#define __LIBCFS_HASH_H__
/*
 * Knuth recommends primes in approximately golden ratio to the maximum
 * integer representable by a machine word for multiplicative hashing.
 * Chuck Lever verified the effectiveness of this technique:
 * http://www.citi.umich.edu/techreports/reports/citi-tr-00-1.pdf
 *
 * These primes are chosen to be bit-sparse, that is operations on
 * them can use shifts and additions instead of multiplications for
 * machines where multiplications are slow.
 */
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define CFS_GOLDEN_RATIO_PRIME_32 0x9e370001UL
/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define CFS_GOLDEN_RATIO_PRIME_64 0x9e37fffffffc0001ULL

/*
 * Ideally we would use HAVE_HASH_LONG for this, but on linux we configure
 * the linux kernel and user space at the same time, so we need to differentiate
 * between them explicitly. If this is not needed on other architectures, then
 * we'll need to move the functions to architecture specific headers.
 */

#include <linux/hash.h>

/** disable debug */
#define CFS_HASH_DEBUG_NONE	 0
/** record hash depth and output to console when it's too deep,
 *  computing overhead is low but consume more memory */
#define CFS_HASH_DEBUG_1	    1
/** expensive, check key validation */
#define CFS_HASH_DEBUG_2	    2

#define CFS_HASH_DEBUG_LEVEL	CFS_HASH_DEBUG_NONE

struct cfs_hash_ops;
struct cfs_hash_lock_ops;
struct cfs_hash_hlist_ops;

union cfs_hash_lock {
	rwlock_t		rw;		/**< rwlock */
	spinlock_t		spin;		/**< spinlock */
};

/**
 * cfs_hash_bucket is a container of:
 * - lock, counter ...
 * - array of hash-head starting from hsb_head[0], hash-head can be one of
 *   . cfs_hash_head_t
 *   . cfs_hash_head_dep_t
 *   . cfs_hash_dhead_t
 *   . cfs_hash_dhead_dep_t
 *   which depends on requirement of user
 * - some extra bytes (caller can require it while creating hash)
 */
struct cfs_hash_bucket {
	union cfs_hash_lock	hsb_lock;	/**< bucket lock */
	__u32			hsb_count;	/**< current entries */
	__u32			hsb_version;	/**< change version */
	unsigned int		hsb_index;	/**< index of bucket */
	int			hsb_depmax;	/**< max depth on bucket */
	long			hsb_head[0];	/**< hash-head array */
};

/**
 * cfs_hash bucket descriptor, it's normally in stack of caller
 */
struct cfs_hash_bd {
	struct cfs_hash_bucket	*bd_bucket;      /**< address of bucket */
	unsigned int		bd_offset;      /**< offset in bucket */
};

#define CFS_HASH_NAME_LEN	   16      /**< default name length */
#define CFS_HASH_BIGNAME_LEN	64      /**< bigname for param tree */

#define CFS_HASH_BKT_BITS	   3       /**< default bits of bucket */
#define CFS_HASH_BITS_MAX	   30      /**< max bits of bucket */
#define CFS_HASH_BITS_MIN	   CFS_HASH_BKT_BITS

/**
 * common hash attributes.
 */
enum cfs_hash_tag {
	/**
	 * don't need any lock, caller will protect operations with it's
	 * own lock. With this flag:
	 *  . CFS_HASH_NO_BKTLOCK, CFS_HASH_RW_BKTLOCK, CFS_HASH_SPIN_BKTLOCK
	 *    will be ignored.
	 *  . Some functions will be disabled with this flag, i.e:
	 *    cfs_hash_for_each_empty, cfs_hash_rehash
	 */
	CFS_HASH_NO_LOCK	= 1 << 0,
	/** no bucket lock, use one spinlock to protect the whole hash */
	CFS_HASH_NO_BKTLOCK     = 1 << 1,
	/** rwlock to protect bucket */
	CFS_HASH_RW_BKTLOCK     = 1 << 2,
	/** spinlock to protect bucket */
	CFS_HASH_SPIN_BKTLOCK   = 1 << 3,
	/** always add new item to tail */
	CFS_HASH_ADD_TAIL       = 1 << 4,
	/** hash-table doesn't have refcount on item */
	CFS_HASH_NO_ITEMREF     = 1 << 5,
	/** big name for param-tree */
	CFS_HASH_BIGNAME	= 1 << 6,
	/** track global count */
	CFS_HASH_COUNTER	= 1 << 7,
	/** rehash item by new key */
	CFS_HASH_REHASH_KEY     = 1 << 8,
	/** Enable dynamic hash resizing */
	CFS_HASH_REHASH	 = 1 << 9,
	/** can shrink hash-size */
	CFS_HASH_SHRINK	 = 1 << 10,
	/** assert hash is empty on exit */
	CFS_HASH_ASSERT_EMPTY   = 1 << 11,
	/** record hlist depth */
	CFS_HASH_DEPTH	  = 1 << 12,
	/**
	 * rehash is always scheduled in a different thread, so current
	 * change on hash table is non-blocking
	 */
	CFS_HASH_NBLK_CHANGE    = 1 << 13,
	/** NB, we typed hs_flags as  __u16, please change it
	 * if you need to extend >=16 flags */
};

/** most used attributes */
#define CFS_HASH_DEFAULT       (CFS_HASH_RW_BKTLOCK | \
				CFS_HASH_COUNTER | CFS_HASH_REHASH)

/**
 * cfs_hash is a hash-table implementation for general purpose, it can support:
 *    . two refcount modes
 *      hash-table with & without refcount
 *    . four lock modes
 *      nolock, one-spinlock, rw-bucket-lock, spin-bucket-lock
 *    . general operations
 *      lookup, add(add_tail or add_head), delete
 *    . rehash
 *      grows or shrink
 *    . iteration
 *      locked iteration and unlocked iteration
 *    . bigname
 *      support long name hash
 *    . debug
 *      trace max searching depth
 *
 * Rehash:
 * When the htable grows or shrinks, a separate task (cfs_hash_rehash_worker)
 * is spawned to handle the rehash in the background, it's possible that other
 * processes can concurrently perform additions, deletions, and lookups
 * without being blocked on rehash completion, because rehash will release
 * the global wrlock for each bucket.
 *
 * rehash and iteration can't run at the same time because it's too tricky
 * to keep both of them safe and correct.
 * As they are relatively rare operations, so:
 *   . if iteration is in progress while we try to launch rehash, then
 *     it just giveup, iterator will launch rehash at the end.
 *   . if rehash is in progress while we try to iterate the hash table,
 *     then we just wait (shouldn't be very long time), anyway, nobody
 *     should expect iteration of whole hash-table to be non-blocking.
 *
 * During rehashing, a (key,object) pair may be in one of two buckets,
 * depending on whether the worker task has yet to transfer the object
 * to its new location in the table. Lookups and deletions need to search both
 * locations; additions must take care to only insert into the new bucket.
 */

struct cfs_hash {
	/** serialize with rehash, or serialize all operations if
	 * the hash-table has CFS_HASH_NO_BKTLOCK */
	union cfs_hash_lock	     hs_lock;
	/** hash operations */
	struct cfs_hash_ops	*hs_ops;
	/** hash lock operations */
	struct cfs_hash_lock_ops   *hs_lops;
	/** hash list operations */
	struct cfs_hash_hlist_ops  *hs_hops;
	/** hash buckets-table */
	struct cfs_hash_bucket	 **hs_buckets;
	/** total number of items on this hash-table */
	atomic_t		hs_count;
	/** hash flags, see cfs_hash_tag for detail */
	__u16		       hs_flags;
	/** # of extra-bytes for bucket, for user saving extended attributes */
	__u16		       hs_extra_bytes;
	/** wants to iterate */
	__u8			hs_iterating;
	/** hash-table is dying */
	__u8			hs_exiting;
	/** current hash bits */
	__u8			hs_cur_bits;
	/** min hash bits */
	__u8			hs_min_bits;
	/** max hash bits */
	__u8			hs_max_bits;
	/** bits for rehash */
	__u8			hs_rehash_bits;
	/** bits for each bucket */
	__u8			hs_bkt_bits;
	/** resize min threshold */
	__u16		       hs_min_theta;
	/** resize max threshold */
	__u16		       hs_max_theta;
	/** resize count */
	__u32		       hs_rehash_count;
	/** # of iterators (caller of cfs_hash_for_each_*) */
	__u32		       hs_iterators;
	/** rehash workitem */
	cfs_workitem_t	      hs_rehash_wi;
	/** refcount on this hash table */
	atomic_t		hs_refcount;
	/** rehash buckets-table */
	struct cfs_hash_bucket	 **hs_rehash_buckets;
#if CFS_HASH_DEBUG_LEVEL >= CFS_HASH_DEBUG_1
	/** serialize debug members */
	spinlock_t			hs_dep_lock;
	/** max depth */
	unsigned int		hs_dep_max;
	/** id of the deepest bucket */
	unsigned int		hs_dep_bkt;
	/** offset in the deepest bucket */
	unsigned int		hs_dep_off;
	/** bits when we found the max depth */
	unsigned int		hs_dep_bits;
	/** workitem to output max depth */
	cfs_workitem_t	      hs_dep_wi;
#endif
	/** name of htable */
	char			hs_name[0];
};

typedef struct cfs_hash_lock_ops {
	/** lock the hash table */
	void    (*hs_lock)(union cfs_hash_lock *lock, int exclusive);
	/** unlock the hash table */
	void    (*hs_unlock)(union cfs_hash_lock *lock, int exclusive);
	/** lock the hash bucket */
	void    (*hs_bkt_lock)(union cfs_hash_lock *lock, int exclusive);
	/** unlock the hash bucket */
	void    (*hs_bkt_unlock)(union cfs_hash_lock *lock, int exclusive);
} cfs_hash_lock_ops_t;

typedef struct cfs_hash_hlist_ops {
	/** return hlist_head of hash-head of @bd */
	struct hlist_head *(*hop_hhead)(struct cfs_hash *hs, struct cfs_hash_bd *bd);
	/** return hash-head size */
	int (*hop_hhead_size)(struct cfs_hash *hs);
	/** add @hnode to hash-head of @bd */
	int (*hop_hnode_add)(struct cfs_hash *hs,
			     struct cfs_hash_bd *bd, struct hlist_node *hnode);
	/** remove @hnode from hash-head of @bd */
	int (*hop_hnode_del)(struct cfs_hash *hs,
			     struct cfs_hash_bd *bd, struct hlist_node *hnode);
} cfs_hash_hlist_ops_t;

typedef struct cfs_hash_ops {
	/** return hashed value from @key */
	unsigned (*hs_hash)(struct cfs_hash *hs, const void *key, unsigned mask);
	/** return key address of @hnode */
	void *   (*hs_key)(struct hlist_node *hnode);
	/** copy key from @hnode to @key */
	void     (*hs_keycpy)(struct hlist_node *hnode, void *key);
	/**
	 *  compare @key with key of @hnode
	 *  returns 1 on a match
	 */
	int      (*hs_keycmp)(const void *key, struct hlist_node *hnode);
	/** return object address of @hnode, i.e: container_of(...hnode) */
	void *   (*hs_object)(struct hlist_node *hnode);
	/** get refcount of item, always called with holding bucket-lock */
	void     (*hs_get)(struct cfs_hash *hs, struct hlist_node *hnode);
	/** release refcount of item */
	void     (*hs_put)(struct cfs_hash *hs, struct hlist_node *hnode);
	/** release refcount of item, always called with holding bucket-lock */
	void     (*hs_put_locked)(struct cfs_hash *hs, struct hlist_node *hnode);
	/** it's called before removing of @hnode */
	void     (*hs_exit)(struct cfs_hash *hs, struct hlist_node *hnode);
} cfs_hash_ops_t;

/** total number of buckets in @hs */
#define CFS_HASH_NBKT(hs)       \
	(1U << ((hs)->hs_cur_bits - (hs)->hs_bkt_bits))

/** total number of buckets in @hs while rehashing */
#define CFS_HASH_RH_NBKT(hs)    \
	(1U << ((hs)->hs_rehash_bits - (hs)->hs_bkt_bits))

/** number of hlist for in bucket */
#define CFS_HASH_BKT_NHLIST(hs) (1U << (hs)->hs_bkt_bits)

/** total number of hlist in @hs */
#define CFS_HASH_NHLIST(hs)     (1U << (hs)->hs_cur_bits)

/** total number of hlist in @hs while rehashing */
#define CFS_HASH_RH_NHLIST(hs)  (1U << (hs)->hs_rehash_bits)

static inline int
cfs_hash_with_no_lock(struct cfs_hash *hs)
{
	/* caller will serialize all operations for this hash-table */
	return (hs->hs_flags & CFS_HASH_NO_LOCK) != 0;
}

static inline int
cfs_hash_with_no_bktlock(struct cfs_hash *hs)
{
	/* no bucket lock, one single lock to protect the hash-table */
	return (hs->hs_flags & CFS_HASH_NO_BKTLOCK) != 0;
}

static inline int
cfs_hash_with_rw_bktlock(struct cfs_hash *hs)
{
	/* rwlock to protect hash bucket */
	return (hs->hs_flags & CFS_HASH_RW_BKTLOCK) != 0;
}

static inline int
cfs_hash_with_spin_bktlock(struct cfs_hash *hs)
{
	/* spinlock to protect hash bucket */
	return (hs->hs_flags & CFS_HASH_SPIN_BKTLOCK) != 0;
}

static inline int
cfs_hash_with_add_tail(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_ADD_TAIL) != 0;
}

static inline int
cfs_hash_with_no_itemref(struct cfs_hash *hs)
{
	/* hash-table doesn't keep refcount on item,
	 * item can't be removed from hash unless it's
	 * ZERO refcount */
	return (hs->hs_flags & CFS_HASH_NO_ITEMREF) != 0;
}

static inline int
cfs_hash_with_bigname(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_BIGNAME) != 0;
}

static inline int
cfs_hash_with_counter(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_COUNTER) != 0;
}

static inline int
cfs_hash_with_rehash(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_REHASH) != 0;
}

static inline int
cfs_hash_with_rehash_key(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_REHASH_KEY) != 0;
}

static inline int
cfs_hash_with_shrink(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_SHRINK) != 0;
}

static inline int
cfs_hash_with_assert_empty(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_ASSERT_EMPTY) != 0;
}

static inline int
cfs_hash_with_depth(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_DEPTH) != 0;
}

static inline int
cfs_hash_with_nblk_change(struct cfs_hash *hs)
{
	return (hs->hs_flags & CFS_HASH_NBLK_CHANGE) != 0;
}

static inline int
cfs_hash_is_exiting(struct cfs_hash *hs)
{       /* cfs_hash_destroy is called */
	return hs->hs_exiting;
}

static inline int
cfs_hash_is_rehashing(struct cfs_hash *hs)
{       /* rehash is launched */
	return hs->hs_rehash_bits != 0;
}

static inline int
cfs_hash_is_iterating(struct cfs_hash *hs)
{       /* someone is calling cfs_hash_for_each_* */
	return hs->hs_iterating || hs->hs_iterators != 0;
}

static inline int
cfs_hash_bkt_size(struct cfs_hash *hs)
{
	return offsetof(struct cfs_hash_bucket, hsb_head[0]) +
	       hs->hs_hops->hop_hhead_size(hs) * CFS_HASH_BKT_NHLIST(hs) +
	       hs->hs_extra_bytes;
}

#define CFS_HOP(hs, op)	   (hs)->hs_ops->hs_ ## op

static inline unsigned
cfs_hash_id(struct cfs_hash *hs, const void *key, unsigned mask)
{
	return CFS_HOP(hs, hash)(hs, key, mask);
}

static inline void *
cfs_hash_key(struct cfs_hash *hs, struct hlist_node *hnode)
{
	return CFS_HOP(hs, key)(hnode);
}

static inline void
cfs_hash_keycpy(struct cfs_hash *hs, struct hlist_node *hnode, void *key)
{
	if (CFS_HOP(hs, keycpy) != NULL)
		CFS_HOP(hs, keycpy)(hnode, key);
}

/**
 * Returns 1 on a match,
 */
static inline int
cfs_hash_keycmp(struct cfs_hash *hs, const void *key, struct hlist_node *hnode)
{
	return CFS_HOP(hs, keycmp)(key, hnode);
}

static inline void *
cfs_hash_object(struct cfs_hash *hs, struct hlist_node *hnode)
{
	return CFS_HOP(hs, object)(hnode);
}

static inline void
cfs_hash_get(struct cfs_hash *hs, struct hlist_node *hnode)
{
	return CFS_HOP(hs, get)(hs, hnode);
}

static inline void
cfs_hash_put_locked(struct cfs_hash *hs, struct hlist_node *hnode)
{
	LASSERT(CFS_HOP(hs, put_locked) != NULL);

	return CFS_HOP(hs, put_locked)(hs, hnode);
}

static inline void
cfs_hash_put(struct cfs_hash *hs, struct hlist_node *hnode)
{
	LASSERT(CFS_HOP(hs, put) != NULL);

	return CFS_HOP(hs, put)(hs, hnode);
}

static inline void
cfs_hash_exit(struct cfs_hash *hs, struct hlist_node *hnode)
{
	if (CFS_HOP(hs, exit))
		CFS_HOP(hs, exit)(hs, hnode);
}

static inline void cfs_hash_lock(struct cfs_hash *hs, int excl)
{
	hs->hs_lops->hs_lock(&hs->hs_lock, excl);
}

static inline void cfs_hash_unlock(struct cfs_hash *hs, int excl)
{
	hs->hs_lops->hs_unlock(&hs->hs_lock, excl);
}

static inline int cfs_hash_dec_and_lock(struct cfs_hash *hs,
					atomic_t *condition)
{
	LASSERT(cfs_hash_with_no_bktlock(hs));
	return atomic_dec_and_lock(condition, &hs->hs_lock.spin);
}

static inline void cfs_hash_bd_lock(struct cfs_hash *hs,
				    struct cfs_hash_bd *bd, int excl)
{
	hs->hs_lops->hs_bkt_lock(&bd->bd_bucket->hsb_lock, excl);
}

static inline void cfs_hash_bd_unlock(struct cfs_hash *hs,
				      struct cfs_hash_bd *bd, int excl)
{
	hs->hs_lops->hs_bkt_unlock(&bd->bd_bucket->hsb_lock, excl);
}

/**
 * operations on cfs_hash bucket (bd: bucket descriptor),
 * they are normally for hash-table without rehash
 */
void cfs_hash_bd_get(struct cfs_hash *hs, const void *key, struct cfs_hash_bd *bd);

static inline void cfs_hash_bd_get_and_lock(struct cfs_hash *hs, const void *key,
					    struct cfs_hash_bd *bd, int excl)
{
	cfs_hash_bd_get(hs, key, bd);
	cfs_hash_bd_lock(hs, bd, excl);
}

static inline unsigned cfs_hash_bd_index_get(struct cfs_hash *hs, struct cfs_hash_bd *bd)
{
	return bd->bd_offset | (bd->bd_bucket->hsb_index << hs->hs_bkt_bits);
}

static inline void cfs_hash_bd_index_set(struct cfs_hash *hs,
					 unsigned index, struct cfs_hash_bd *bd)
{
	bd->bd_bucket = hs->hs_buckets[index >> hs->hs_bkt_bits];
	bd->bd_offset = index & (CFS_HASH_BKT_NHLIST(hs) - 1U);
}

static inline void *
cfs_hash_bd_extra_get(struct cfs_hash *hs, struct cfs_hash_bd *bd)
{
	return (void *)bd->bd_bucket +
	       cfs_hash_bkt_size(hs) - hs->hs_extra_bytes;
}

static inline __u32
cfs_hash_bd_version_get(struct cfs_hash_bd *bd)
{
	/* need hold cfs_hash_bd_lock */
	return bd->bd_bucket->hsb_version;
}

static inline __u32
cfs_hash_bd_count_get(struct cfs_hash_bd *bd)
{
	/* need hold cfs_hash_bd_lock */
	return bd->bd_bucket->hsb_count;
}

static inline int
cfs_hash_bd_depmax_get(struct cfs_hash_bd *bd)
{
	return bd->bd_bucket->hsb_depmax;
}

static inline int
cfs_hash_bd_compare(struct cfs_hash_bd *bd1, struct cfs_hash_bd *bd2)
{
	if (bd1->bd_bucket->hsb_index != bd2->bd_bucket->hsb_index)
		return bd1->bd_bucket->hsb_index - bd2->bd_bucket->hsb_index;

	if (bd1->bd_offset != bd2->bd_offset)
		return bd1->bd_offset - bd2->bd_offset;

	return 0;
}

void cfs_hash_bd_add_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			    struct hlist_node *hnode);
void cfs_hash_bd_del_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			    struct hlist_node *hnode);
void cfs_hash_bd_move_locked(struct cfs_hash *hs, struct cfs_hash_bd *bd_old,
			     struct cfs_hash_bd *bd_new, struct hlist_node *hnode);

static inline int cfs_hash_bd_dec_and_lock(struct cfs_hash *hs, struct cfs_hash_bd *bd,
					   atomic_t *condition)
{
	LASSERT(cfs_hash_with_spin_bktlock(hs));
	return atomic_dec_and_lock(condition,
				       &bd->bd_bucket->hsb_lock.spin);
}

static inline struct hlist_head *cfs_hash_bd_hhead(struct cfs_hash *hs,
						   struct cfs_hash_bd *bd)
{
	return hs->hs_hops->hop_hhead(hs, bd);
}

struct hlist_node *cfs_hash_bd_lookup_locked(struct cfs_hash *hs,
					     struct cfs_hash_bd *bd, const void *key);
struct hlist_node *cfs_hash_bd_peek_locked(struct cfs_hash *hs,
					   struct cfs_hash_bd *bd, const void *key);
struct hlist_node *cfs_hash_bd_findadd_locked(struct cfs_hash *hs,
					      struct cfs_hash_bd *bd, const void *key,
					     struct hlist_node *hnode,
					     int insist_add);
struct hlist_node *cfs_hash_bd_finddel_locked(struct cfs_hash *hs,
					      struct cfs_hash_bd *bd, const void *key,
					     struct hlist_node *hnode);

/**
 * operations on cfs_hash bucket (bd: bucket descriptor),
 * they are safe for hash-table with rehash
 */
void cfs_hash_dual_bd_get(struct cfs_hash *hs, const void *key, struct cfs_hash_bd *bds);
void cfs_hash_dual_bd_lock(struct cfs_hash *hs, struct cfs_hash_bd *bds, int excl);
void cfs_hash_dual_bd_unlock(struct cfs_hash *hs, struct cfs_hash_bd *bds, int excl);

static inline void cfs_hash_dual_bd_get_and_lock(struct cfs_hash *hs, const void *key,
						 struct cfs_hash_bd *bds, int excl)
{
	cfs_hash_dual_bd_get(hs, key, bds);
	cfs_hash_dual_bd_lock(hs, bds, excl);
}

struct hlist_node *cfs_hash_dual_bd_lookup_locked(struct cfs_hash *hs,
						  struct cfs_hash_bd *bds,
						 const void *key);
struct hlist_node *cfs_hash_dual_bd_findadd_locked(struct cfs_hash *hs,
						   struct cfs_hash_bd *bds,
						  const void *key,
						  struct hlist_node *hnode,
						  int insist_add);
struct hlist_node *cfs_hash_dual_bd_finddel_locked(struct cfs_hash *hs,
						   struct cfs_hash_bd *bds,
						  const void *key,
						  struct hlist_node *hnode);

/* Hash init/cleanup functions */
struct cfs_hash *cfs_hash_create(char *name, unsigned cur_bits, unsigned max_bits,
				 unsigned bkt_bits, unsigned extra_bytes,
			    unsigned min_theta, unsigned max_theta,
			    cfs_hash_ops_t *ops, unsigned flags);

struct cfs_hash *cfs_hash_getref(struct cfs_hash *hs);
void cfs_hash_putref(struct cfs_hash *hs);

/* Hash addition functions */
void cfs_hash_add(struct cfs_hash *hs, const void *key,
		  struct hlist_node *hnode);
int cfs_hash_add_unique(struct cfs_hash *hs, const void *key,
			struct hlist_node *hnode);
void *cfs_hash_findadd_unique(struct cfs_hash *hs, const void *key,
			      struct hlist_node *hnode);

/* Hash deletion functions */
void *cfs_hash_del(struct cfs_hash *hs, const void *key, struct hlist_node *hnode);
void *cfs_hash_del_key(struct cfs_hash *hs, const void *key);

/* Hash lookup/for_each functions */
#define CFS_HASH_LOOP_HOG       1024

typedef int (*cfs_hash_for_each_cb_t)(struct cfs_hash *hs, struct cfs_hash_bd *bd,
				      struct hlist_node *node, void *data);
void *cfs_hash_lookup(struct cfs_hash *hs, const void *key);
void cfs_hash_for_each(struct cfs_hash *hs, cfs_hash_for_each_cb_t, void *data);
void cfs_hash_for_each_safe(struct cfs_hash *hs, cfs_hash_for_each_cb_t, void *data);
int  cfs_hash_for_each_nolock(struct cfs_hash *hs,
			      cfs_hash_for_each_cb_t, void *data);
int  cfs_hash_for_each_empty(struct cfs_hash *hs,
			     cfs_hash_for_each_cb_t, void *data);
void cfs_hash_for_each_key(struct cfs_hash *hs, const void *key,
			   cfs_hash_for_each_cb_t, void *data);
typedef int (*cfs_hash_cond_opt_cb_t)(void *obj, void *data);
void cfs_hash_cond_del(struct cfs_hash *hs, cfs_hash_cond_opt_cb_t, void *data);

void cfs_hash_hlist_for_each(struct cfs_hash *hs, unsigned hindex,
			     cfs_hash_for_each_cb_t, void *data);
int  cfs_hash_is_empty(struct cfs_hash *hs);
__u64 cfs_hash_size_get(struct cfs_hash *hs);

/*
 * Rehash - Theta is calculated to be the average chained
 * hash depth assuming a perfectly uniform hash function.
 */
void cfs_hash_rehash_cancel_locked(struct cfs_hash *hs);
void cfs_hash_rehash_cancel(struct cfs_hash *hs);
int  cfs_hash_rehash(struct cfs_hash *hs, int do_rehash);
void cfs_hash_rehash_key(struct cfs_hash *hs, const void *old_key,
			 void *new_key, struct hlist_node *hnode);

#if CFS_HASH_DEBUG_LEVEL > CFS_HASH_DEBUG_1
/* Validate hnode references the correct key */
static inline void
cfs_hash_key_validate(struct cfs_hash *hs, const void *key,
		      struct hlist_node *hnode)
{
	LASSERT(cfs_hash_keycmp(hs, key, hnode));
}

/* Validate hnode is in the correct bucket */
static inline void
cfs_hash_bucket_validate(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			 struct hlist_node *hnode)
{
	struct cfs_hash_bd   bds[2];

	cfs_hash_dual_bd_get(hs, cfs_hash_key(hs, hnode), bds);
	LASSERT(bds[0].bd_bucket == bd->bd_bucket ||
		bds[1].bd_bucket == bd->bd_bucket);
}

#else /* CFS_HASH_DEBUG_LEVEL > CFS_HASH_DEBUG_1 */

static inline void
cfs_hash_key_validate(struct cfs_hash *hs, const void *key,
		      struct hlist_node *hnode) {}

static inline void
cfs_hash_bucket_validate(struct cfs_hash *hs, struct cfs_hash_bd *bd,
			 struct hlist_node *hnode) {}

#endif /* CFS_HASH_DEBUG_LEVEL */

#define CFS_HASH_THETA_BITS  10
#define CFS_HASH_MIN_THETA  (1U << (CFS_HASH_THETA_BITS - 1))
#define CFS_HASH_MAX_THETA  (1U << (CFS_HASH_THETA_BITS + 1))

/* Return integer component of theta */
static inline int __cfs_hash_theta_int(int theta)
{
	return (theta >> CFS_HASH_THETA_BITS);
}

/* Return a fractional value between 0 and 999 */
static inline int __cfs_hash_theta_frac(int theta)
{
	return ((theta * 1000) >> CFS_HASH_THETA_BITS) -
	       (__cfs_hash_theta_int(theta) * 1000);
}

static inline int __cfs_hash_theta(struct cfs_hash *hs)
{
	return (atomic_read(&hs->hs_count) <<
		CFS_HASH_THETA_BITS) >> hs->hs_cur_bits;
}

static inline void __cfs_hash_set_theta(struct cfs_hash *hs, int min, int max)
{
	LASSERT(min < max);
	hs->hs_min_theta = (__u16)min;
	hs->hs_max_theta = (__u16)max;
}

/* Generic debug formatting routines mainly for proc handler */
struct seq_file;
int cfs_hash_debug_header(struct seq_file *m);
int cfs_hash_debug_str(struct cfs_hash *hs, struct seq_file *m);

/*
 * Generic djb2 hash algorithm for character arrays.
 */
static inline unsigned
cfs_hash_djb2_hash(const void *key, size_t size, unsigned mask)
{
	unsigned i, hash = 5381;

	LASSERT(key != NULL);

	for (i = 0; i < size; i++)
		hash = hash * 33 + ((char *)key)[i];

	return (hash & mask);
}

/*
 * Generic u32 hash algorithm.
 */
static inline unsigned
cfs_hash_u32_hash(const __u32 key, unsigned mask)
{
	return ((key * CFS_GOLDEN_RATIO_PRIME_32) & mask);
}

/*
 * Generic u64 hash algorithm.
 */
static inline unsigned
cfs_hash_u64_hash(const __u64 key, unsigned mask)
{
	return ((unsigned)(key * CFS_GOLDEN_RATIO_PRIME_64) & mask);
}

/** iterate over all buckets in @bds (array of struct cfs_hash_bd) */
#define cfs_hash_for_each_bd(bds, n, i) \
	for (i = 0; i < n && (bds)[i].bd_bucket != NULL; i++)

/** iterate over all buckets of @hs */
#define cfs_hash_for_each_bucket(hs, bd, pos)		   \
	for (pos = 0;					   \
	     pos < CFS_HASH_NBKT(hs) &&			 \
	     ((bd)->bd_bucket = (hs)->hs_buckets[pos]) != NULL; pos++)

/** iterate over all hlist of bucket @bd */
#define cfs_hash_bd_for_each_hlist(hs, bd, hlist)	       \
	for ((bd)->bd_offset = 0;			       \
	     (bd)->bd_offset < CFS_HASH_BKT_NHLIST(hs) &&       \
	     (hlist = cfs_hash_bd_hhead(hs, bd)) != NULL;       \
	     (bd)->bd_offset++)

/* !__LIBCFS__HASH_H__ */
#endif
