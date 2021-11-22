/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#ifndef RXE_POOL_H
#define RXE_POOL_H

#define RXE_POOL_ALIGN		(16)
#define RXE_POOL_CACHE_FLAGS	(0)

enum rxe_pool_flags {
	RXE_POOL_INDEX		= BIT(1),
	RXE_POOL_KEY		= BIT(2),
	RXE_POOL_NO_ALLOC	= BIT(4),
};

enum rxe_elem_type {
	RXE_TYPE_UC,
	RXE_TYPE_PD,
	RXE_TYPE_AH,
	RXE_TYPE_SRQ,
	RXE_TYPE_QP,
	RXE_TYPE_CQ,
	RXE_TYPE_MR,
	RXE_TYPE_MW,
	RXE_TYPE_MC_GRP,
	RXE_TYPE_MC_ELEM,
	RXE_NUM_TYPES,		/* keep me last */
};

struct rxe_pool_entry;

struct rxe_pool_entry {
	struct rxe_pool		*pool;
	struct kref		ref_cnt;
	struct list_head	list;

	/* only used if keyed */
	struct rb_node		key_node;

	/* only used if indexed */
	struct rb_node		index_node;
	u32			index;
};

struct rxe_pool {
	struct rxe_dev		*rxe;
	rwlock_t		pool_lock; /* protects pool add/del/search */
	size_t			elem_size;
	void			(*cleanup)(struct rxe_pool_entry *obj);
	enum rxe_pool_flags	flags;
	enum rxe_elem_type	type;

	unsigned int		max_elem;
	atomic_t		num_elem;

	/* only used if indexed */
	struct {
		struct rb_root		tree;
		unsigned long		*table;
		u32			last;
		u32			max_index;
		u32			min_index;
	} index;

	/* only used if keyed */
	struct {
		struct rb_root		tree;
		size_t			key_offset;
		size_t			key_size;
	} key;
};

/* initialize a pool of objects with given limit on
 * number of elements. gets parameters from rxe_type_info
 * pool elements will be allocated out of a slab cache
 */
int rxe_pool_init(struct rxe_dev *rxe, struct rxe_pool *pool,
		  enum rxe_elem_type type, u32 max_elem);

/* free resources from object pool */
void rxe_pool_cleanup(struct rxe_pool *pool);

/* allocate an object from pool holding and not holding the pool lock */
void *rxe_alloc_locked(struct rxe_pool *pool);

void *rxe_alloc(struct rxe_pool *pool);

/* connect already allocated object to pool */
int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_entry *elem);

#define rxe_add_to_pool(pool, obj) __rxe_add_to_pool(pool, &(obj)->pelem)

/* assign an index to an indexed object and insert object into
 *  pool's rb tree holding and not holding the pool_lock
 */
int __rxe_add_index_locked(struct rxe_pool_entry *elem);

#define rxe_add_index_locked(obj) __rxe_add_index_locked(&(obj)->pelem)

int __rxe_add_index(struct rxe_pool_entry *elem);

#define rxe_add_index(obj) __rxe_add_index(&(obj)->pelem)

/* drop an index and remove object from rb tree
 * holding and not holding the pool_lock
 */
void __rxe_drop_index_locked(struct rxe_pool_entry *elem);

#define rxe_drop_index_locked(obj) __rxe_drop_index_locked(&(obj)->pelem)

void __rxe_drop_index(struct rxe_pool_entry *elem);

#define rxe_drop_index(obj) __rxe_drop_index(&(obj)->pelem)

/* assign a key to a keyed object and insert object into
 * pool's rb tree holding and not holding pool_lock
 */
int __rxe_add_key_locked(struct rxe_pool_entry *elem, void *key);

#define rxe_add_key_locked(obj, key) __rxe_add_key_locked(&(obj)->pelem, key)

int __rxe_add_key(struct rxe_pool_entry *elem, void *key);

#define rxe_add_key(obj, key) __rxe_add_key(&(obj)->pelem, key)

/* remove elem from rb tree holding and not holding the pool_lock */
void __rxe_drop_key_locked(struct rxe_pool_entry *elem);

#define rxe_drop_key_locked(obj) __rxe_drop_key_locked(&(obj)->pelem)

void __rxe_drop_key(struct rxe_pool_entry *elem);

#define rxe_drop_key(obj) __rxe_drop_key(&(obj)->pelem)

/* lookup an indexed object from index holding and not holding the pool_lock.
 * takes a reference on object
 */
void *rxe_pool_get_index_locked(struct rxe_pool *pool, u32 index);

void *rxe_pool_get_index(struct rxe_pool *pool, u32 index);

/* lookup keyed object from key holding and not holding the pool_lock.
 * takes a reference on the objecti
 */
void *rxe_pool_get_key_locked(struct rxe_pool *pool, void *key);

void *rxe_pool_get_key(struct rxe_pool *pool, void *key);

/* cleanup an object when all references are dropped */
void rxe_elem_release(struct kref *kref);

/* take a reference on an object */
#define rxe_add_ref(elem) kref_get(&(elem)->pelem.ref_cnt)

/* drop a reference on an object */
#define rxe_drop_ref(elem) kref_put(&(elem)->pelem.ref_cnt, rxe_elem_release)

#endif /* RXE_POOL_H */
