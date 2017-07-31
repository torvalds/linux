/*
 * Copyright (C) 2012 Red Hat. All rights reserved.
 *
 * writeback cache policy supporting flushing out dirty cache blocks.
 *
 * This file is released under the GPL.
 */

#include "dm-cache-policy.h"
#include "dm.h"

#include <linux/hash.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/*----------------------------------------------------------------*/

#define DM_MSG_PREFIX "cache cleaner"

/* Cache entry struct. */
struct wb_cache_entry {
	struct list_head list;
	struct hlist_node hlist;

	dm_oblock_t oblock;
	dm_cblock_t cblock;
	bool dirty:1;
	bool pending:1;
};

struct hash {
	struct hlist_head *table;
	dm_block_t hash_bits;
	unsigned nr_buckets;
};

struct policy {
	struct dm_cache_policy policy;
	spinlock_t lock;

	struct list_head free;
	struct list_head clean;
	struct list_head clean_pending;
	struct list_head dirty;

	/*
	 * We know exactly how many cblocks will be needed,
	 * so we can allocate them up front.
	 */
	dm_cblock_t cache_size, nr_cblocks_allocated;
	struct wb_cache_entry *cblocks;
	struct hash chash;
};

/*----------------------------------------------------------------------------*/

/*
 * Low-level functions.
 */
static unsigned next_power(unsigned n, unsigned min)
{
	return roundup_pow_of_two(max(n, min));
}

static struct policy *to_policy(struct dm_cache_policy *p)
{
	return container_of(p, struct policy, policy);
}

static struct list_head *list_pop(struct list_head *q)
{
	struct list_head *r = q->next;

	list_del(r);

	return r;
}

/*----------------------------------------------------------------------------*/

/* Allocate/free various resources. */
static int alloc_hash(struct hash *hash, unsigned elts)
{
	hash->nr_buckets = next_power(elts >> 4, 16);
	hash->hash_bits = __ffs(hash->nr_buckets);
	hash->table = vzalloc(sizeof(*hash->table) * hash->nr_buckets);

	return hash->table ? 0 : -ENOMEM;
}

static void free_hash(struct hash *hash)
{
	vfree(hash->table);
}

static int alloc_cache_blocks_with_hash(struct policy *p, dm_cblock_t cache_size)
{
	int r = -ENOMEM;

	p->cblocks = vzalloc(sizeof(*p->cblocks) * from_cblock(cache_size));
	if (p->cblocks) {
		unsigned u = from_cblock(cache_size);

		while (u--)
			list_add(&p->cblocks[u].list, &p->free);

		p->nr_cblocks_allocated = 0;

		/* Cache entries hash. */
		r = alloc_hash(&p->chash, from_cblock(cache_size));
		if (r)
			vfree(p->cblocks);
	}

	return r;
}

static void free_cache_blocks_and_hash(struct policy *p)
{
	free_hash(&p->chash);
	vfree(p->cblocks);
}

static struct wb_cache_entry *alloc_cache_entry(struct policy *p)
{
	struct wb_cache_entry *e;

	BUG_ON(from_cblock(p->nr_cblocks_allocated) >= from_cblock(p->cache_size));

	e = list_entry(list_pop(&p->free), struct wb_cache_entry, list);
	p->nr_cblocks_allocated = to_cblock(from_cblock(p->nr_cblocks_allocated) + 1);

	return e;
}

/*----------------------------------------------------------------------------*/

/* Hash functions (lookup, insert, remove). */
static struct wb_cache_entry *lookup_cache_entry(struct policy *p, dm_oblock_t oblock)
{
	struct hash *hash = &p->chash;
	unsigned h = hash_64(from_oblock(oblock), hash->hash_bits);
	struct wb_cache_entry *cur;
	struct hlist_head *bucket = &hash->table[h];

	hlist_for_each_entry(cur, bucket, hlist) {
		if (cur->oblock == oblock) {
			/* Move upfront bucket for faster access. */
			hlist_del(&cur->hlist);
			hlist_add_head(&cur->hlist, bucket);
			return cur;
		}
	}

	return NULL;
}

static void insert_cache_hash_entry(struct policy *p, struct wb_cache_entry *e)
{
	unsigned h = hash_64(from_oblock(e->oblock), p->chash.hash_bits);

	hlist_add_head(&e->hlist, &p->chash.table[h]);
}

static void remove_cache_hash_entry(struct wb_cache_entry *e)
{
	hlist_del(&e->hlist);
}

/* Public interface (see dm-cache-policy.h */
static int wb_map(struct dm_cache_policy *pe, dm_oblock_t oblock,
		  bool can_block, bool can_migrate, bool discarded_oblock,
		  struct bio *bio, struct policy_locker *locker,
		  struct policy_result *result)
{
	struct policy *p = to_policy(pe);
	struct wb_cache_entry *e;
	unsigned long flags;

	result->op = POLICY_MISS;

	if (can_block)
		spin_lock_irqsave(&p->lock, flags);

	else if (!spin_trylock_irqsave(&p->lock, flags))
		return -EWOULDBLOCK;

	e = lookup_cache_entry(p, oblock);
	if (e) {
		result->op = POLICY_HIT;
		result->cblock = e->cblock;

	}

	spin_unlock_irqrestore(&p->lock, flags);

	return 0;
}

static int wb_lookup(struct dm_cache_policy *pe, dm_oblock_t oblock, dm_cblock_t *cblock)
{
	int r;
	struct policy *p = to_policy(pe);
	struct wb_cache_entry *e;
	unsigned long flags;

	if (!spin_trylock_irqsave(&p->lock, flags))
		return -EWOULDBLOCK;

	e = lookup_cache_entry(p, oblock);
	if (e) {
		*cblock = e->cblock;
		r = 0;

	} else
		r = -ENOENT;

	spin_unlock_irqrestore(&p->lock, flags);

	return r;
}

static void __set_clear_dirty(struct dm_cache_policy *pe, dm_oblock_t oblock, bool set)
{
	struct policy *p = to_policy(pe);
	struct wb_cache_entry *e;

	e = lookup_cache_entry(p, oblock);
	BUG_ON(!e);

	if (set) {
		if (!e->dirty) {
			e->dirty = true;
			list_move(&e->list, &p->dirty);
		}

	} else {
		if (e->dirty) {
			e->pending = false;
			e->dirty = false;
			list_move(&e->list, &p->clean);
		}
	}
}

static void wb_set_dirty(struct dm_cache_policy *pe, dm_oblock_t oblock)
{
	struct policy *p = to_policy(pe);
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);
	__set_clear_dirty(pe, oblock, true);
	spin_unlock_irqrestore(&p->lock, flags);
}

static void wb_clear_dirty(struct dm_cache_policy *pe, dm_oblock_t oblock)
{
	struct policy *p = to_policy(pe);
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);
	__set_clear_dirty(pe, oblock, false);
	spin_unlock_irqrestore(&p->lock, flags);
}

static void add_cache_entry(struct policy *p, struct wb_cache_entry *e)
{
	insert_cache_hash_entry(p, e);
	if (e->dirty)
		list_add(&e->list, &p->dirty);
	else
		list_add(&e->list, &p->clean);
}

static int wb_load_mapping(struct dm_cache_policy *pe,
			   dm_oblock_t oblock, dm_cblock_t cblock,
			   uint32_t hint, bool hint_valid)
{
	int r;
	struct policy *p = to_policy(pe);
	struct wb_cache_entry *e = alloc_cache_entry(p);

	if (e) {
		e->cblock = cblock;
		e->oblock = oblock;
		e->dirty = false; /* blocks default to clean */
		add_cache_entry(p, e);
		r = 0;

	} else
		r = -ENOMEM;

	return r;
}

static void wb_destroy(struct dm_cache_policy *pe)
{
	struct policy *p = to_policy(pe);

	free_cache_blocks_and_hash(p);
	kfree(p);
}

static struct wb_cache_entry *__wb_force_remove_mapping(struct policy *p, dm_oblock_t oblock)
{
	struct wb_cache_entry *r = lookup_cache_entry(p, oblock);

	BUG_ON(!r);

	remove_cache_hash_entry(r);
	list_del(&r->list);

	return r;
}

static void wb_remove_mapping(struct dm_cache_policy *pe, dm_oblock_t oblock)
{
	struct policy *p = to_policy(pe);
	struct wb_cache_entry *e;
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);
	e = __wb_force_remove_mapping(p, oblock);
	list_add_tail(&e->list, &p->free);
	BUG_ON(!from_cblock(p->nr_cblocks_allocated));
	p->nr_cblocks_allocated = to_cblock(from_cblock(p->nr_cblocks_allocated) - 1);
	spin_unlock_irqrestore(&p->lock, flags);
}

static void wb_force_mapping(struct dm_cache_policy *pe,
				dm_oblock_t current_oblock, dm_oblock_t oblock)
{
	struct policy *p = to_policy(pe);
	struct wb_cache_entry *e;
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);
	e = __wb_force_remove_mapping(p, current_oblock);
	e->oblock = oblock;
	add_cache_entry(p, e);
	spin_unlock_irqrestore(&p->lock, flags);
}

static struct wb_cache_entry *get_next_dirty_entry(struct policy *p)
{
	struct list_head *l;
	struct wb_cache_entry *r;

	if (list_empty(&p->dirty))
		return NULL;

	l = list_pop(&p->dirty);
	r = container_of(l, struct wb_cache_entry, list);
	list_add(l, &p->clean_pending);

	return r;
}

static int wb_writeback_work(struct dm_cache_policy *pe,
			     dm_oblock_t *oblock,
			     dm_cblock_t *cblock,
			     bool critical_only)
{
	int r = -ENOENT;
	struct policy *p = to_policy(pe);
	struct wb_cache_entry *e;
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);

	e = get_next_dirty_entry(p);
	if (e) {
		*oblock = e->oblock;
		*cblock = e->cblock;
		r = 0;
	}

	spin_unlock_irqrestore(&p->lock, flags);

	return r;
}

static dm_cblock_t wb_residency(struct dm_cache_policy *pe)
{
	return to_policy(pe)->nr_cblocks_allocated;
}

/* Init the policy plugin interface function pointers. */
static void init_policy_functions(struct policy *p)
{
	p->policy.destroy = wb_destroy;
	p->policy.map = wb_map;
	p->policy.lookup = wb_lookup;
	p->policy.set_dirty = wb_set_dirty;
	p->policy.clear_dirty = wb_clear_dirty;
	p->policy.load_mapping = wb_load_mapping;
	p->policy.get_hint = NULL;
	p->policy.remove_mapping = wb_remove_mapping;
	p->policy.writeback_work = wb_writeback_work;
	p->policy.force_mapping = wb_force_mapping;
	p->policy.residency = wb_residency;
	p->policy.tick = NULL;
}

static struct dm_cache_policy *wb_create(dm_cblock_t cache_size,
					 sector_t origin_size,
					 sector_t cache_block_size)
{
	int r;
	struct policy *p = kzalloc(sizeof(*p), GFP_KERNEL);

	if (!p)
		return NULL;

	init_policy_functions(p);
	INIT_LIST_HEAD(&p->free);
	INIT_LIST_HEAD(&p->clean);
	INIT_LIST_HEAD(&p->clean_pending);
	INIT_LIST_HEAD(&p->dirty);

	p->cache_size = cache_size;
	spin_lock_init(&p->lock);

	/* Allocate cache entry structs and add them to free list. */
	r = alloc_cache_blocks_with_hash(p, cache_size);
	if (!r)
		return &p->policy;

	kfree(p);

	return NULL;
}
/*----------------------------------------------------------------------------*/

static struct dm_cache_policy_type wb_policy_type = {
	.name = "cleaner",
	.version = {1, 0, 0},
	.hint_size = 4,
	.owner = THIS_MODULE,
	.create = wb_create
};

static int __init wb_init(void)
{
	int r = dm_cache_policy_register(&wb_policy_type);

	if (r < 0)
		DMERR("register failed %d", r);
	else
		DMINFO("version %u.%u.%u loaded",
		       wb_policy_type.version[0],
		       wb_policy_type.version[1],
		       wb_policy_type.version[2]);

	return r;
}

static void __exit wb_exit(void)
{
	dm_cache_policy_unregister(&wb_policy_type);
}

module_init(wb_init);
module_exit(wb_exit);

MODULE_AUTHOR("Heinz Mauelshagen <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("cleaner cache policy");
