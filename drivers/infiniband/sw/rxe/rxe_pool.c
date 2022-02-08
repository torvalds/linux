// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"

#define RXE_POOL_ALIGN		(16)

static const struct rxe_type_info {
	const char *name;
	size_t size;
	size_t elem_offset;
	void (*cleanup)(struct rxe_pool_elem *obj);
	enum rxe_pool_flags flags;
	u32 min_index;
	u32 max_index;
} rxe_type_info[RXE_NUM_TYPES] = {
	[RXE_TYPE_UC] = {
		.name		= "rxe-uc",
		.size		= sizeof(struct rxe_ucontext),
		.elem_offset	= offsetof(struct rxe_ucontext, elem),
		.flags          = RXE_POOL_NO_ALLOC,
	},
	[RXE_TYPE_PD] = {
		.name		= "rxe-pd",
		.size		= sizeof(struct rxe_pd),
		.elem_offset	= offsetof(struct rxe_pd, elem),
		.flags		= RXE_POOL_NO_ALLOC,
	},
	[RXE_TYPE_AH] = {
		.name		= "rxe-ah",
		.size		= sizeof(struct rxe_ah),
		.elem_offset	= offsetof(struct rxe_ah, elem),
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_AH_INDEX,
		.max_index	= RXE_MAX_AH_INDEX,
	},
	[RXE_TYPE_SRQ] = {
		.name		= "rxe-srq",
		.size		= sizeof(struct rxe_srq),
		.elem_offset	= offsetof(struct rxe_srq, elem),
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_SRQ_INDEX,
		.max_index	= RXE_MAX_SRQ_INDEX,
	},
	[RXE_TYPE_QP] = {
		.name		= "rxe-qp",
		.size		= sizeof(struct rxe_qp),
		.elem_offset	= offsetof(struct rxe_qp, elem),
		.cleanup	= rxe_qp_cleanup,
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_QP_INDEX,
		.max_index	= RXE_MAX_QP_INDEX,
	},
	[RXE_TYPE_CQ] = {
		.name		= "rxe-cq",
		.size		= sizeof(struct rxe_cq),
		.elem_offset	= offsetof(struct rxe_cq, elem),
		.flags          = RXE_POOL_NO_ALLOC,
		.cleanup	= rxe_cq_cleanup,
	},
	[RXE_TYPE_MR] = {
		.name		= "rxe-mr",
		.size		= sizeof(struct rxe_mr),
		.elem_offset	= offsetof(struct rxe_mr, elem),
		.cleanup	= rxe_mr_cleanup,
		.flags		= RXE_POOL_INDEX,
		.min_index	= RXE_MIN_MR_INDEX,
		.max_index	= RXE_MAX_MR_INDEX,
	},
	[RXE_TYPE_MW] = {
		.name		= "rxe-mw",
		.size		= sizeof(struct rxe_mw),
		.elem_offset	= offsetof(struct rxe_mw, elem),
		.cleanup	= rxe_mw_cleanup,
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_MW_INDEX,
		.max_index	= RXE_MAX_MW_INDEX,
	},
	[RXE_TYPE_MC_GRP] = {
		.name		= "rxe-mc_grp",
		.size		= sizeof(struct rxe_mcg),
		.elem_offset	= offsetof(struct rxe_mcg, elem),
		.cleanup	= rxe_mc_cleanup,
		.flags		= RXE_POOL_KEY,
		.key_offset	= offsetof(struct rxe_mcg, mgid),
		.key_size	= sizeof(union ib_gid),
	},
};

static int rxe_pool_init_index(struct rxe_pool *pool, u32 max, u32 min)
{
	int err = 0;

	if ((max - min + 1) < pool->max_elem) {
		pr_warn("not enough indices for max_elem\n");
		err = -EINVAL;
		goto out;
	}

	pool->index.max_index = max;
	pool->index.min_index = min;

	pool->index.table = bitmap_zalloc(max - min + 1, GFP_KERNEL);
	if (!pool->index.table) {
		err = -ENOMEM;
		goto out;
	}

out:
	return err;
}

int rxe_pool_init(
	struct rxe_dev		*rxe,
	struct rxe_pool		*pool,
	enum rxe_elem_type	type,
	unsigned int		max_elem)
{
	const struct rxe_type_info *info = &rxe_type_info[type];
	int			err = 0;

	memset(pool, 0, sizeof(*pool));

	pool->rxe		= rxe;
	pool->name		= info->name;
	pool->type		= type;
	pool->max_elem		= max_elem;
	pool->elem_size		= ALIGN(info->size, RXE_POOL_ALIGN);
	pool->elem_offset	= info->elem_offset;
	pool->flags		= info->flags;
	pool->cleanup		= info->cleanup;

	atomic_set(&pool->num_elem, 0);

	rwlock_init(&pool->pool_lock);

	if (pool->flags & RXE_POOL_INDEX) {
		pool->index.tree = RB_ROOT;
		err = rxe_pool_init_index(pool, info->max_index,
					  info->min_index);
		if (err)
			goto out;
	}

out:
	return err;
}

void rxe_pool_cleanup(struct rxe_pool *pool)
{
	if (atomic_read(&pool->num_elem) > 0)
		pr_warn("%s pool destroyed with unfree'd elem\n",
			pool->name);

	if (pool->flags & RXE_POOL_INDEX)
		bitmap_free(pool->index.table);
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

static int rxe_insert_index(struct rxe_pool *pool, struct rxe_pool_elem *new)
{
	struct rb_node **link = &pool->index.tree.rb_node;
	struct rb_node *parent = NULL;
	struct rxe_pool_elem *elem;

	while (*link) {
		parent = *link;
		elem = rb_entry(parent, struct rxe_pool_elem, index_node);

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

int __rxe_add_index_locked(struct rxe_pool_elem *elem)
{
	struct rxe_pool *pool = elem->pool;
	int err;

	elem->index = alloc_index(pool);
	err = rxe_insert_index(pool, elem);

	return err;
}

int __rxe_add_index(struct rxe_pool_elem *elem)
{
	struct rxe_pool *pool = elem->pool;
	unsigned long flags;
	int err;

	write_lock_irqsave(&pool->pool_lock, flags);
	err = __rxe_add_index_locked(elem);
	write_unlock_irqrestore(&pool->pool_lock, flags);

	return err;
}

void __rxe_drop_index_locked(struct rxe_pool_elem *elem)
{
	struct rxe_pool *pool = elem->pool;

	clear_bit(elem->index - pool->index.min_index, pool->index.table);
	rb_erase(&elem->index_node, &pool->index.tree);
}

void __rxe_drop_index(struct rxe_pool_elem *elem)
{
	struct rxe_pool *pool = elem->pool;
	unsigned long flags;

	write_lock_irqsave(&pool->pool_lock, flags);
	__rxe_drop_index_locked(elem);
	write_unlock_irqrestore(&pool->pool_lock, flags);
}

void *rxe_alloc_locked(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;
	void *obj;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	obj = kzalloc(pool->elem_size, GFP_ATOMIC);
	if (!obj)
		goto out_cnt;

	elem = (struct rxe_pool_elem *)((u8 *)obj + pool->elem_offset);

	elem->pool = pool;
	elem->obj = obj;
	kref_init(&elem->ref_cnt);

	return obj;

out_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

void *rxe_alloc(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;
	void *obj;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	obj = kzalloc(pool->elem_size, GFP_KERNEL);
	if (!obj)
		goto out_cnt;

	elem = (struct rxe_pool_elem *)((u8 *)obj + pool->elem_offset);

	elem->pool = pool;
	elem->obj = obj;
	kref_init(&elem->ref_cnt);

	return obj;

out_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_elem *elem)
{
	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	elem->pool = pool;
	elem->obj = (u8 *)elem - pool->elem_offset;
	kref_init(&elem->ref_cnt);

	return 0;

out_cnt:
	atomic_dec(&pool->num_elem);
	return -EINVAL;
}

void rxe_elem_release(struct kref *kref)
{
	struct rxe_pool_elem *elem =
		container_of(kref, struct rxe_pool_elem, ref_cnt);
	struct rxe_pool *pool = elem->pool;
	void *obj;

	if (pool->cleanup)
		pool->cleanup(elem);

	if (!(pool->flags & RXE_POOL_NO_ALLOC)) {
		obj = elem->obj;
		kfree(obj);
	}

	atomic_dec(&pool->num_elem);
}

void *rxe_pool_get_index_locked(struct rxe_pool *pool, u32 index)
{
	struct rb_node *node;
	struct rxe_pool_elem *elem;
	void *obj;

	node = pool->index.tree.rb_node;

	while (node) {
		elem = rb_entry(node, struct rxe_pool_elem, index_node);

		if (elem->index > index)
			node = node->rb_left;
		else if (elem->index < index)
			node = node->rb_right;
		else
			break;
	}

	if (node) {
		kref_get(&elem->ref_cnt);
		obj = elem->obj;
	} else {
		obj = NULL;
	}

	return obj;
}

void *rxe_pool_get_index(struct rxe_pool *pool, u32 index)
{
	unsigned long flags;
	void *obj;

	read_lock_irqsave(&pool->pool_lock, flags);
	obj = rxe_pool_get_index_locked(pool, index);
	read_unlock_irqrestore(&pool->pool_lock, flags);

	return obj;
}
