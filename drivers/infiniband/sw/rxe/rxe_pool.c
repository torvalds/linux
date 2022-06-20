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
	void (*cleanup)(struct rxe_pool_elem *elem);
	u32 min_index;
	u32 max_index;
	u32 max_elem;
} rxe_type_info[RXE_NUM_TYPES] = {
	[RXE_TYPE_UC] = {
		.name		= "uc",
		.size		= sizeof(struct rxe_ucontext),
		.elem_offset	= offsetof(struct rxe_ucontext, elem),
		.min_index	= 1,
		.max_index	= UINT_MAX,
		.max_elem	= UINT_MAX,
	},
	[RXE_TYPE_PD] = {
		.name		= "pd",
		.size		= sizeof(struct rxe_pd),
		.elem_offset	= offsetof(struct rxe_pd, elem),
		.min_index	= 1,
		.max_index	= UINT_MAX,
		.max_elem	= UINT_MAX,
	},
	[RXE_TYPE_AH] = {
		.name		= "ah",
		.size		= sizeof(struct rxe_ah),
		.elem_offset	= offsetof(struct rxe_ah, elem),
		.min_index	= RXE_MIN_AH_INDEX,
		.max_index	= RXE_MAX_AH_INDEX,
		.max_elem	= RXE_MAX_AH_INDEX - RXE_MIN_AH_INDEX + 1,
	},
	[RXE_TYPE_SRQ] = {
		.name		= "srq",
		.size		= sizeof(struct rxe_srq),
		.elem_offset	= offsetof(struct rxe_srq, elem),
		.cleanup	= rxe_srq_cleanup,
		.min_index	= RXE_MIN_SRQ_INDEX,
		.max_index	= RXE_MAX_SRQ_INDEX,
		.max_elem	= RXE_MAX_SRQ_INDEX - RXE_MIN_SRQ_INDEX + 1,
	},
	[RXE_TYPE_QP] = {
		.name		= "qp",
		.size		= sizeof(struct rxe_qp),
		.elem_offset	= offsetof(struct rxe_qp, elem),
		.cleanup	= rxe_qp_cleanup,
		.min_index	= RXE_MIN_QP_INDEX,
		.max_index	= RXE_MAX_QP_INDEX,
		.max_elem	= RXE_MAX_QP_INDEX - RXE_MIN_QP_INDEX + 1,
	},
	[RXE_TYPE_CQ] = {
		.name		= "cq",
		.size		= sizeof(struct rxe_cq),
		.elem_offset	= offsetof(struct rxe_cq, elem),
		.cleanup	= rxe_cq_cleanup,
		.min_index	= 1,
		.max_index	= UINT_MAX,
		.max_elem	= UINT_MAX,
	},
	[RXE_TYPE_MR] = {
		.name		= "mr",
		.size		= sizeof(struct rxe_mr),
		.elem_offset	= offsetof(struct rxe_mr, elem),
		.cleanup	= rxe_mr_cleanup,
		.min_index	= RXE_MIN_MR_INDEX,
		.max_index	= RXE_MAX_MR_INDEX,
		.max_elem	= RXE_MAX_MR_INDEX - RXE_MIN_MR_INDEX + 1,
	},
	[RXE_TYPE_MW] = {
		.name		= "mw",
		.size		= sizeof(struct rxe_mw),
		.elem_offset	= offsetof(struct rxe_mw, elem),
		.cleanup	= rxe_mw_cleanup,
		.min_index	= RXE_MIN_MW_INDEX,
		.max_index	= RXE_MAX_MW_INDEX,
		.max_elem	= RXE_MAX_MW_INDEX - RXE_MIN_MW_INDEX + 1,
	},
};

void rxe_pool_init(struct rxe_dev *rxe, struct rxe_pool *pool,
		   enum rxe_elem_type type)
{
	const struct rxe_type_info *info = &rxe_type_info[type];

	memset(pool, 0, sizeof(*pool));

	pool->rxe		= rxe;
	pool->name		= info->name;
	pool->type		= type;
	pool->max_elem		= info->max_elem;
	pool->elem_size		= ALIGN(info->size, RXE_POOL_ALIGN);
	pool->elem_offset	= info->elem_offset;
	pool->cleanup		= info->cleanup;

	atomic_set(&pool->num_elem, 0);

	xa_init_flags(&pool->xa, XA_FLAGS_ALLOC);
	pool->limit.min = info->min_index;
	pool->limit.max = info->max_index;
}

void rxe_pool_cleanup(struct rxe_pool *pool)
{
	WARN_ON(!xa_empty(&pool->xa));
}

void *rxe_alloc(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;
	void *obj;
	int err;

	if (WARN_ON(!(pool->type == RXE_TYPE_MR)))
		return NULL;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto err_cnt;

	obj = kzalloc(pool->elem_size, GFP_KERNEL);
	if (!obj)
		goto err_cnt;

	elem = (struct rxe_pool_elem *)((u8 *)obj + pool->elem_offset);

	elem->pool = pool;
	elem->obj = obj;
	kref_init(&elem->ref_cnt);

	err = xa_alloc_cyclic(&pool->xa, &elem->index, elem, pool->limit,
			      &pool->next, GFP_KERNEL);
	if (err)
		goto err_free;

	return obj;

err_free:
	kfree(obj);
err_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_elem *elem)
{
	int err;

	if (WARN_ON(pool->type == RXE_TYPE_MR))
		return -EINVAL;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto err_cnt;

	elem->pool = pool;
	elem->obj = (u8 *)elem - pool->elem_offset;
	kref_init(&elem->ref_cnt);

	err = xa_alloc_cyclic(&pool->xa, &elem->index, elem, pool->limit,
			      &pool->next, GFP_KERNEL);
	if (err)
		goto err_cnt;

	return 0;

err_cnt:
	atomic_dec(&pool->num_elem);
	return -EINVAL;
}

void *rxe_pool_get_index(struct rxe_pool *pool, u32 index)
{
	struct rxe_pool_elem *elem;
	struct xarray *xa = &pool->xa;
	unsigned long flags;
	void *obj;

	xa_lock_irqsave(xa, flags);
	elem = xa_load(xa, index);
	if (elem && kref_get_unless_zero(&elem->ref_cnt))
		obj = elem->obj;
	else
		obj = NULL;
	xa_unlock_irqrestore(xa, flags);

	return obj;
}

static void rxe_elem_release(struct kref *kref)
{
	struct rxe_pool_elem *elem = container_of(kref, typeof(*elem), ref_cnt);
	struct rxe_pool *pool = elem->pool;

	xa_erase(&pool->xa, elem->index);

	if (pool->cleanup)
		pool->cleanup(elem);

	if (pool->type == RXE_TYPE_MR)
		kfree(elem->obj);

	atomic_dec(&pool->num_elem);
}

int __rxe_get(struct rxe_pool_elem *elem)
{
	return kref_get_unless_zero(&elem->ref_cnt);
}

int __rxe_put(struct rxe_pool_elem *elem)
{
	return kref_put(&elem->ref_cnt, rxe_elem_release);
}
