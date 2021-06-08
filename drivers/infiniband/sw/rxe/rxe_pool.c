// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"
#include "rxe_loc.h"

/* info about object pools
 */
struct rxe_type_info rxe_type_info[RXE_NUM_TYPES] = {
	[RXE_TYPE_UC] = {
		.name		= "rxe-uc",
		.size		= sizeof(struct rxe_ucontext),
		.elem_offset	= offsetof(struct rxe_ucontext, pelem),
		.flags          = RXE_POOL_NO_ALLOC,
	},
	[RXE_TYPE_PD] = {
		.name		= "rxe-pd",
		.size		= sizeof(struct rxe_pd),
		.elem_offset	= offsetof(struct rxe_pd, pelem),
		.flags		= RXE_POOL_NO_ALLOC,
	},
	[RXE_TYPE_AH] = {
		.name		= "rxe-ah",
		.size		= sizeof(struct rxe_ah),
		.elem_offset	= offsetof(struct rxe_ah, pelem),
		.flags		= RXE_POOL_NO_ALLOC,
	},
	[RXE_TYPE_SRQ] = {
		.name		= "rxe-srq",
		.size		= sizeof(struct rxe_srq),
		.elem_offset	= offsetof(struct rxe_srq, pelem),
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_SRQ_INDEX,
		.max_index	= RXE_MAX_SRQ_INDEX,
	},
	[RXE_TYPE_QP] = {
		.name		= "rxe-qp",
		.size		= sizeof(struct rxe_qp),
		.elem_offset	= offsetof(struct rxe_qp, pelem),
		.cleanup	= rxe_qp_cleanup,
		.flags		= RXE_POOL_INDEX,
		.min_index	= RXE_MIN_QP_INDEX,
		.max_index	= RXE_MAX_QP_INDEX,
	},
	[RXE_TYPE_CQ] = {
		.name		= "rxe-cq",
		.size		= sizeof(struct rxe_cq),
		.elem_offset	= offsetof(struct rxe_cq, pelem),
		.flags          = RXE_POOL_NO_ALLOC,
		.cleanup	= rxe_cq_cleanup,
	},
	[RXE_TYPE_MR] = {
		.name		= "rxe-mr",
		.size		= sizeof(struct rxe_mr),
		.elem_offset	= offsetof(struct rxe_mr, pelem),
		.cleanup	= rxe_mr_cleanup,
		.flags		= RXE_POOL_INDEX,
		.max_index	= RXE_MAX_MR_INDEX,
		.min_index	= RXE_MIN_MR_INDEX,
	},
	[RXE_TYPE_MW] = {
		.name		= "rxe-mw",
		.size		= sizeof(struct rxe_mw),
		.elem_offset	= offsetof(struct rxe_mw, pelem),
		.cleanup	= rxe_mw_cleanup,
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.max_index	= RXE_MAX_MW_INDEX,
		.min_index	= RXE_MIN_MW_INDEX,
	},
	[RXE_TYPE_MC_GRP] = {
		.name		= "rxe-mc_grp",
		.size		= sizeof(struct rxe_mc_grp),
		.elem_offset	= offsetof(struct rxe_mc_grp, pelem),
		.cleanup	= rxe_mc_cleanup,
		.flags		= RXE_POOL_KEY,
		.key_offset	= offsetof(struct rxe_mc_grp, mgid),
		.key_size	= sizeof(union ib_gid),
	},
	[RXE_TYPE_MC_ELEM] = {
		.name		= "rxe-mc_elem",
		.size		= sizeof(struct rxe_mc_elem),
		.elem_offset	= offsetof(struct rxe_mc_elem, pelem),
	},
};

static inline const char *pool_name(struct rxe_pool *pool)
{
	return rxe_type_info[pool->type].name;
}

static int rxe_pool_init_index(struct rxe_pool *pool, u32 max, u32 min)
{
	int err = 0;
	size_t size;

	if ((max - min + 1) < pool->max_elem) {
		pr_warn("not enough indices for max_elem\n");
		err = -EINVAL;
		goto out;
	}

	pool->index.max_index = max;
	pool->index.min_index = min;

	size = BITS_TO_LONGS(max - min + 1) * sizeof(long);
	pool->index.table = kmalloc(size, GFP_KERNEL);
	if (!pool->index.table) {
		err = -ENOMEM;
		goto out;
	}

	pool->index.table_size = size;
	bitmap_zero(pool->index.table, max - min + 1);

out:
	return err;
}

int rxe_pool_init(
	struct rxe_dev		*rxe,
	struct rxe_pool		*pool,
	enum rxe_elem_type	type,
	unsigned int		max_elem)
{
	int			err = 0;
	size_t			size = rxe_type_info[type].size;

	memset(pool, 0, sizeof(*pool));

	pool->rxe		= rxe;
	pool->type		= type;
	pool->max_elem		= max_elem;
	pool->elem_size		= ALIGN(size, RXE_POOL_ALIGN);
	pool->flags		= rxe_type_info[type].flags;
	pool->index.tree	= RB_ROOT;
	pool->key.tree		= RB_ROOT;
	pool->cleanup		= rxe_type_info[type].cleanup;

	atomic_set(&pool->num_elem, 0);

	rwlock_init(&pool->pool_lock);

	if (rxe_type_info[type].flags & RXE_POOL_INDEX) {
		err = rxe_pool_init_index(pool,
					  rxe_type_info[type].max_index,
					  rxe_type_info[type].min_index);
		if (err)
			goto out;
	}

	if (rxe_type_info[type].flags & RXE_POOL_KEY) {
		pool->key.key_offset = rxe_type_info[type].key_offset;
		pool->key.key_size = rxe_type_info[type].key_size;
	}

out:
	return err;
}

void rxe_pool_cleanup(struct rxe_pool *pool)
{
	if (atomic_read(&pool->num_elem) > 0)
		pr_warn("%s pool destroyed with unfree'd elem\n",
			pool_name(pool));

	kfree(pool->index.table);
}

static u32 alloc_index(struct rxe_pool *pool)
{
	u32 index;
	u32 range = pool->index.max_index - pool->index.min_index + 1;

	index = find_next_zero_bit(pool->index.table, range, pool->index.last);
	if (index >= range)
		index = find_first_zero_bit(pool->index.table, range);

	WARN_ON_ONCE(index >= range);
	set_bit(index, pool->index.table);
	pool->index.last = index;
	return index + pool->index.min_index;
}

static int rxe_insert_index(struct rxe_pool *pool, struct rxe_pool_entry *new)
{
	struct rb_node **link = &pool->index.tree.rb_node;
	struct rb_node *parent = NULL;
	struct rxe_pool_entry *elem;

	while (*link) {
		parent = *link;
		elem = rb_entry(parent, struct rxe_pool_entry, index_node);

		if (elem->index == new->index) {
			pr_warn("element already exists!\n");
			return -EINVAL;
		}

		if (elem->index > new->index)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&new->index_node, parent, link);
	rb_insert_color(&new->index_node, &pool->index.tree);

	return 0;
}

static int rxe_insert_key(struct rxe_pool *pool, struct rxe_pool_entry *new)
{
	struct rb_node **link = &pool->key.tree.rb_node;
	struct rb_node *parent = NULL;
	struct rxe_pool_entry *elem;
	int cmp;

	while (*link) {
		parent = *link;
		elem = rb_entry(parent, struct rxe_pool_entry, key_node);

		cmp = memcmp((u8 *)elem + pool->key.key_offset,
			     (u8 *)new + pool->key.key_offset, pool->key.key_size);

		if (cmp == 0) {
			pr_warn("key already exists!\n");
			return -EINVAL;
		}

		if (cmp > 0)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&new->key_node, parent, link);
	rb_insert_color(&new->key_node, &pool->key.tree);

	return 0;
}

int __rxe_add_key_locked(struct rxe_pool_entry *elem, void *key)
{
	struct rxe_pool *pool = elem->pool;
	int err;

	memcpy((u8 *)elem + pool->key.key_offset, key, pool->key.key_size);
	err = rxe_insert_key(pool, elem);

	return err;
}

int __rxe_add_key(struct rxe_pool_entry *elem, void *key)
{
	struct rxe_pool *pool = elem->pool;
	unsigned long flags;
	int err;

	write_lock_irqsave(&pool->pool_lock, flags);
	err = __rxe_add_key_locked(elem, key);
	write_unlock_irqrestore(&pool->pool_lock, flags);

	return err;
}

void __rxe_drop_key_locked(struct rxe_pool_entry *elem)
{
	struct rxe_pool *pool = elem->pool;

	rb_erase(&elem->key_node, &pool->key.tree);
}

void __rxe_drop_key(struct rxe_pool_entry *elem)
{
	struct rxe_pool *pool = elem->pool;
	unsigned long flags;

	write_lock_irqsave(&pool->pool_lock, flags);
	__rxe_drop_key_locked(elem);
	write_unlock_irqrestore(&pool->pool_lock, flags);
}

int __rxe_add_index_locked(struct rxe_pool_entry *elem)
{
	struct rxe_pool *pool = elem->pool;
	int err;

	elem->index = alloc_index(pool);
	err = rxe_insert_index(pool, elem);

	return err;
}

int __rxe_add_index(struct rxe_pool_entry *elem)
{
	struct rxe_pool *pool = elem->pool;
	unsigned long flags;
	int err;

	write_lock_irqsave(&pool->pool_lock, flags);
	err = __rxe_add_index_locked(elem);
	write_unlock_irqrestore(&pool->pool_lock, flags);

	return err;
}

void __rxe_drop_index_locked(struct rxe_pool_entry *elem)
{
	struct rxe_pool *pool = elem->pool;

	clear_bit(elem->index - pool->index.min_index, pool->index.table);
	rb_erase(&elem->index_node, &pool->index.tree);
}

void __rxe_drop_index(struct rxe_pool_entry *elem)
{
	struct rxe_pool *pool = elem->pool;
	unsigned long flags;

	write_lock_irqsave(&pool->pool_lock, flags);
	__rxe_drop_index_locked(elem);
	write_unlock_irqrestore(&pool->pool_lock, flags);
}

void *rxe_alloc_locked(struct rxe_pool *pool)
{
	struct rxe_type_info *info = &rxe_type_info[pool->type];
	struct rxe_pool_entry *elem;
	u8 *obj;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	obj = kzalloc(info->size, GFP_ATOMIC);
	if (!obj)
		goto out_cnt;

	elem = (struct rxe_pool_entry *)(obj + info->elem_offset);

	elem->pool = pool;
	kref_init(&elem->ref_cnt);

	return obj;

out_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

void *rxe_alloc(struct rxe_pool *pool)
{
	struct rxe_type_info *info = &rxe_type_info[pool->type];
	struct rxe_pool_entry *elem;
	u8 *obj;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	obj = kzalloc(info->size, GFP_KERNEL);
	if (!obj)
		goto out_cnt;

	elem = (struct rxe_pool_entry *)(obj + info->elem_offset);

	elem->pool = pool;
	kref_init(&elem->ref_cnt);

	return obj;

out_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_entry *elem)
{
	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	elem->pool = pool;
	kref_init(&elem->ref_cnt);

	return 0;

out_cnt:
	atomic_dec(&pool->num_elem);
	return -EINVAL;
}

void rxe_elem_release(struct kref *kref)
{
	struct rxe_pool_entry *elem =
		container_of(kref, struct rxe_pool_entry, ref_cnt);
	struct rxe_pool *pool = elem->pool;
	struct rxe_type_info *info = &rxe_type_info[pool->type];
	u8 *obj;

	if (pool->cleanup)
		pool->cleanup(elem);

	if (!(pool->flags & RXE_POOL_NO_ALLOC)) {
		obj = (u8 *)elem - info->elem_offset;
		kfree(obj);
	}

	atomic_dec(&pool->num_elem);
}

void *rxe_pool_get_index_locked(struct rxe_pool *pool, u32 index)
{
	struct rxe_type_info *info = &rxe_type_info[pool->type];
	struct rb_node *node;
	struct rxe_pool_entry *elem;
	u8 *obj;

	node = pool->index.tree.rb_node;

	while (node) {
		elem = rb_entry(node, struct rxe_pool_entry, index_node);

		if (elem->index > index)
			node = node->rb_left;
		else if (elem->index < index)
			node = node->rb_right;
		else
			break;
	}

	if (node) {
		kref_get(&elem->ref_cnt);
		obj = (u8 *)elem - info->elem_offset;
	} else {
		obj = NULL;
	}

	return obj;
}

void *rxe_pool_get_index(struct rxe_pool *pool, u32 index)
{
	u8 *obj;
	unsigned long flags;

	read_lock_irqsave(&pool->pool_lock, flags);
	obj = rxe_pool_get_index_locked(pool, index);
	read_unlock_irqrestore(&pool->pool_lock, flags);

	return obj;
}

void *rxe_pool_get_key_locked(struct rxe_pool *pool, void *key)
{
	struct rxe_type_info *info = &rxe_type_info[pool->type];
	struct rb_node *node;
	struct rxe_pool_entry *elem;
	u8 *obj;
	int cmp;

	node = pool->key.tree.rb_node;

	while (node) {
		elem = rb_entry(node, struct rxe_pool_entry, key_node);

		cmp = memcmp((u8 *)elem + pool->key.key_offset,
			     key, pool->key.key_size);

		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			break;
	}

	if (node) {
		kref_get(&elem->ref_cnt);
		obj = (u8 *)elem - info->elem_offset;
	} else {
		obj = NULL;
	}

	return obj;
}

void *rxe_pool_get_key(struct rxe_pool *pool, void *key)
{
	u8 *obj;
	unsigned long flags;

	read_lock_irqsave(&pool->pool_lock, flags);
	obj = rxe_pool_get_key_locked(pool, key);
	read_unlock_irqrestore(&pool->pool_lock, flags);

	return obj;
}
