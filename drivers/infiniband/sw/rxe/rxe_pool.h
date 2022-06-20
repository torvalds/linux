/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#ifndef RXE_POOL_H
#define RXE_POOL_H

enum rxe_elem_type {
	RXE_TYPE_UC,
	RXE_TYPE_PD,
	RXE_TYPE_AH,
	RXE_TYPE_SRQ,
	RXE_TYPE_QP,
	RXE_TYPE_CQ,
	RXE_TYPE_MR,
	RXE_TYPE_MW,
	RXE_NUM_TYPES,		/* keep me last */
};

struct rxe_pool_elem {
	struct rxe_pool		*pool;
	void			*obj;
	struct kref		ref_cnt;
	struct list_head	list;
	u32			index;
};

struct rxe_pool {
	struct rxe_dev		*rxe;
	const char		*name;
	void			(*cleanup)(struct rxe_pool_elem *elem);
	enum rxe_elem_type	type;

	unsigned int		max_elem;
	atomic_t		num_elem;
	size_t			elem_size;
	size_t			elem_offset;

	struct xarray		xa;
	struct xa_limit		limit;
	u32			next;
};

/* initialize a pool of objects with given limit on
 * number of elements. gets parameters from rxe_type_info
 * pool elements will be allocated out of a slab cache
 */
void rxe_pool_init(struct rxe_dev *rxe, struct rxe_pool *pool,
		  enum rxe_elem_type type);

/* free resources from object pool */
void rxe_pool_cleanup(struct rxe_pool *pool);

/* allocate an object from pool */
void *rxe_alloc(struct rxe_pool *pool);

/* connect already allocated object to pool */
int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_elem *elem);

#define rxe_add_to_pool(pool, obj) __rxe_add_to_pool(pool, &(obj)->elem)

/* lookup an indexed object from index. takes a reference on object */
void *rxe_pool_get_index(struct rxe_pool *pool, u32 index);

int __rxe_get(struct rxe_pool_elem *elem);

#define rxe_get(obj) __rxe_get(&(obj)->elem)

int __rxe_put(struct rxe_pool_elem *elem);

#define rxe_put(obj) __rxe_put(&(obj)->elem)

#define rxe_read(obj) kref_read(&(obj)->elem.ref_cnt)

#endif /* RXE_POOL_H */
