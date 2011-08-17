/*
 * In-kernel transcendent memory (generic implementation)
 *
 * Copyright (c) 2009-2011, Dan Magenheimer, Oracle Corp.
 *
 * The primary purpose of Transcedent Memory ("tmem") is to map object-oriented
 * "handles" (triples containing a pool id, and object id, and an index), to
 * pages in a page-accessible memory (PAM).  Tmem references the PAM pages via
 * an abstract "pampd" (PAM page-descriptor), which can be operated on by a
 * set of functions (pamops).  Each pampd contains some representation of
 * PAGE_SIZE bytes worth of data. Tmem must support potentially millions of
 * pages and must be able to insert, find, and delete these pages at a
 * potential frequency of thousands per second concurrently across many CPUs,
 * (and, if used with KVM, across many vcpus across many guests).
 * Tmem is tracked with a hierarchy of data structures, organized by
 * the elements in a handle-tuple: pool_id, object_id, and page index.
 * One or more "clients" (e.g. guests) each provide one or more tmem_pools.
 * Each pool, contains a hash table of rb_trees of tmem_objs.  Each
 * tmem_obj contains a radix-tree-like tree of pointers, with intermediate
 * nodes called tmem_objnodes.  Each leaf pointer in this tree points to
 * a pampd, which is accessible only through a small set of callbacks
 * registered by the PAM implementation (see tmem_register_pamops). Tmem
 * does all memory allocation via a set of callbacks registered by the tmem
 * host implementation (e.g. see tmem_register_hostops).
 */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

#include "tmem.h"

/* data structure sentinels used for debugging... see tmem.h */
#define POOL_SENTINEL 0x87658765
#define OBJ_SENTINEL 0x12345678
#define OBJNODE_SENTINEL 0xfedcba09

/*
 * A tmem host implementation must use this function to register callbacks
 * for memory allocation.
 */
static struct tmem_hostops tmem_hostops;

static void tmem_objnode_tree_init(void);

void tmem_register_hostops(struct tmem_hostops *m)
{
	tmem_objnode_tree_init();
	tmem_hostops = *m;
}

/*
 * A tmem host implementation must use this function to register
 * callbacks for a page-accessible memory (PAM) implementation
 */
static struct tmem_pamops tmem_pamops;

void tmem_register_pamops(struct tmem_pamops *m)
{
	tmem_pamops = *m;
}

/*
 * Oid's are potentially very sparse and tmem_objs may have an indeterminately
 * short life, being added and deleted at a relatively high frequency.
 * So an rb_tree is an ideal data structure to manage tmem_objs.  But because
 * of the potentially huge number of tmem_objs, each pool manages a hashtable
 * of rb_trees to reduce search, insert, delete, and rebalancing time.
 * Each hashbucket also has a lock to manage concurrent access.
 *
 * The following routines manage tmem_objs.  When any tmem_obj is accessed,
 * the hashbucket lock must be held.
 */

/* searches for object==oid in pool, returns locked object if found */
static struct tmem_obj *tmem_obj_find(struct tmem_hashbucket *hb,
					struct tmem_oid *oidp)
{
	struct rb_node *rbnode;
	struct tmem_obj *obj;

	rbnode = hb->obj_rb_root.rb_node;
	while (rbnode) {
		BUG_ON(RB_EMPTY_NODE(rbnode));
		obj = rb_entry(rbnode, struct tmem_obj, rb_tree_node);
		switch (tmem_oid_compare(oidp, &obj->oid)) {
		case 0: /* equal */
			goto out;
		case -1:
			rbnode = rbnode->rb_left;
			break;
		case 1:
			rbnode = rbnode->rb_right;
			break;
		}
	}
	obj = NULL;
out:
	return obj;
}

static void tmem_pampd_destroy_all_in_obj(struct tmem_obj *);

/* free an object that has no more pampds in it */
static void tmem_obj_free(struct tmem_obj *obj, struct tmem_hashbucket *hb)
{
	struct tmem_pool *pool;

	BUG_ON(obj == NULL);
	ASSERT_SENTINEL(obj, OBJ);
	BUG_ON(obj->pampd_count > 0);
	pool = obj->pool;
	BUG_ON(pool == NULL);
	if (obj->objnode_tree_root != NULL) /* may be "stump" with no leaves */
		tmem_pampd_destroy_all_in_obj(obj);
	BUG_ON(obj->objnode_tree_root != NULL);
	BUG_ON((long)obj->objnode_count != 0);
	atomic_dec(&pool->obj_count);
	BUG_ON(atomic_read(&pool->obj_count) < 0);
	INVERT_SENTINEL(obj, OBJ);
	obj->pool = NULL;
	tmem_oid_set_invalid(&obj->oid);
	rb_erase(&obj->rb_tree_node, &hb->obj_rb_root);
}

/*
 * initialize, and insert an tmem_object_root (called only if find failed)
 */
static void tmem_obj_init(struct tmem_obj *obj, struct tmem_hashbucket *hb,
					struct tmem_pool *pool,
					struct tmem_oid *oidp)
{
	struct rb_root *root = &hb->obj_rb_root;
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct tmem_obj *this;

	BUG_ON(pool == NULL);
	atomic_inc(&pool->obj_count);
	obj->objnode_tree_height = 0;
	obj->objnode_tree_root = NULL;
	obj->pool = pool;
	obj->oid = *oidp;
	obj->objnode_count = 0;
	obj->pampd_count = 0;
	(*tmem_pamops.new_obj)(obj);
	SET_SENTINEL(obj, OBJ);
	while (*new) {
		BUG_ON(RB_EMPTY_NODE(*new));
		this = rb_entry(*new, struct tmem_obj, rb_tree_node);
		parent = *new;
		switch (tmem_oid_compare(oidp, &this->oid)) {
		case 0:
			BUG(); /* already present; should never happen! */
			break;
		case -1:
			new = &(*new)->rb_left;
			break;
		case 1:
			new = &(*new)->rb_right;
			break;
		}
	}
	rb_link_node(&obj->rb_tree_node, parent, new);
	rb_insert_color(&obj->rb_tree_node, root);
}

/*
 * Tmem is managed as a set of tmem_pools with certain attributes, such as
 * "ephemeral" vs "persistent".  These attributes apply to all tmem_objs
 * and all pampds that belong to a tmem_pool.  A tmem_pool is created
 * or deleted relatively rarely (for example, when a filesystem is
 * mounted or unmounted.
 */

/* flush all data from a pool and, optionally, free it */
static void tmem_pool_flush(struct tmem_pool *pool, bool destroy)
{
	struct rb_node *rbnode;
	struct tmem_obj *obj;
	struct tmem_hashbucket *hb = &pool->hashbucket[0];
	int i;

	BUG_ON(pool == NULL);
	for (i = 0; i < TMEM_HASH_BUCKETS; i++, hb++) {
		spin_lock(&hb->lock);
		rbnode = rb_first(&hb->obj_rb_root);
		while (rbnode != NULL) {
			obj = rb_entry(rbnode, struct tmem_obj, rb_tree_node);
			rbnode = rb_next(rbnode);
			tmem_pampd_destroy_all_in_obj(obj);
			tmem_obj_free(obj, hb);
			(*tmem_hostops.obj_free)(obj, pool);
		}
		spin_unlock(&hb->lock);
	}
	if (destroy)
		list_del(&pool->pool_list);
}

/*
 * A tmem_obj contains a radix-tree-like tree in which the intermediate
 * nodes are called tmem_objnodes.  (The kernel lib/radix-tree.c implementation
 * is very specialized and tuned for specific uses and is not particularly
 * suited for use from this code, though some code from the core algorithms has
 * been reused, thus the copyright notices below).  Each tmem_objnode contains
 * a set of pointers which point to either a set of intermediate tmem_objnodes
 * or a set of of pampds.
 *
 * Portions Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Portions Copyright (C) 2005 SGI, Christoph Lameter <clameter@sgi.com>
 */

struct tmem_objnode_tree_path {
	struct tmem_objnode *objnode;
	int offset;
};

/* objnode height_to_maxindex translation */
static unsigned long tmem_objnode_tree_h2max[OBJNODE_TREE_MAX_PATH + 1];

static void tmem_objnode_tree_init(void)
{
	unsigned int ht, tmp;

	for (ht = 0; ht < ARRAY_SIZE(tmem_objnode_tree_h2max); ht++) {
		tmp = ht * OBJNODE_TREE_MAP_SHIFT;
		if (tmp >= OBJNODE_TREE_INDEX_BITS)
			tmem_objnode_tree_h2max[ht] = ~0UL;
		else
			tmem_objnode_tree_h2max[ht] =
			    (~0UL >> (OBJNODE_TREE_INDEX_BITS - tmp - 1)) >> 1;
	}
}

static struct tmem_objnode *tmem_objnode_alloc(struct tmem_obj *obj)
{
	struct tmem_objnode *objnode;

	ASSERT_SENTINEL(obj, OBJ);
	BUG_ON(obj->pool == NULL);
	ASSERT_SENTINEL(obj->pool, POOL);
	objnode = (*tmem_hostops.objnode_alloc)(obj->pool);
	if (unlikely(objnode == NULL))
		goto out;
	objnode->obj = obj;
	SET_SENTINEL(objnode, OBJNODE);
	memset(&objnode->slots, 0, sizeof(objnode->slots));
	objnode->slots_in_use = 0;
	obj->objnode_count++;
out:
	return objnode;
}

static void tmem_objnode_free(struct tmem_objnode *objnode)
{
	struct tmem_pool *pool;
	int i;

	BUG_ON(objnode == NULL);
	for (i = 0; i < OBJNODE_TREE_MAP_SIZE; i++)
		BUG_ON(objnode->slots[i] != NULL);
	ASSERT_SENTINEL(objnode, OBJNODE);
	INVERT_SENTINEL(objnode, OBJNODE);
	BUG_ON(objnode->obj == NULL);
	ASSERT_SENTINEL(objnode->obj, OBJ);
	pool = objnode->obj->pool;
	BUG_ON(pool == NULL);
	ASSERT_SENTINEL(pool, POOL);
	objnode->obj->objnode_count--;
	objnode->obj = NULL;
	(*tmem_hostops.objnode_free)(objnode, pool);
}

/*
 * lookup index in object and return associated pampd (or NULL if not found)
 */
static void **__tmem_pampd_lookup_in_obj(struct tmem_obj *obj, uint32_t index)
{
	unsigned int height, shift;
	struct tmem_objnode **slot = NULL;

	BUG_ON(obj == NULL);
	ASSERT_SENTINEL(obj, OBJ);
	BUG_ON(obj->pool == NULL);
	ASSERT_SENTINEL(obj->pool, POOL);

	height = obj->objnode_tree_height;
	if (index > tmem_objnode_tree_h2max[obj->objnode_tree_height])
		goto out;
	if (height == 0 && obj->objnode_tree_root) {
		slot = &obj->objnode_tree_root;
		goto out;
	}
	shift = (height-1) * OBJNODE_TREE_MAP_SHIFT;
	slot = &obj->objnode_tree_root;
	while (height > 0) {
		if (*slot == NULL)
			goto out;
		slot = (struct tmem_objnode **)
			((*slot)->slots +
			 ((index >> shift) & OBJNODE_TREE_MAP_MASK));
		shift -= OBJNODE_TREE_MAP_SHIFT;
		height--;
	}
out:
	return slot != NULL ? (void **)slot : NULL;
}

static void *tmem_pampd_lookup_in_obj(struct tmem_obj *obj, uint32_t index)
{
	struct tmem_objnode **slot;

	slot = (struct tmem_objnode **)__tmem_pampd_lookup_in_obj(obj, index);
	return slot != NULL ? *slot : NULL;
}

static void *tmem_pampd_replace_in_obj(struct tmem_obj *obj, uint32_t index,
					void *new_pampd)
{
	struct tmem_objnode **slot;
	void *ret = NULL;

	slot = (struct tmem_objnode **)__tmem_pampd_lookup_in_obj(obj, index);
	if ((slot != NULL) && (*slot != NULL)) {
		void *old_pampd = *(void **)slot;
		*(void **)slot = new_pampd;
		(*tmem_pamops.free)(old_pampd, obj->pool, NULL, 0);
		ret = new_pampd;
	}
	return ret;
}

static int tmem_pampd_add_to_obj(struct tmem_obj *obj, uint32_t index,
					void *pampd)
{
	int ret = 0;
	struct tmem_objnode *objnode = NULL, *newnode, *slot;
	unsigned int height, shift;
	int offset = 0;

	/* if necessary, extend the tree to be higher  */
	if (index > tmem_objnode_tree_h2max[obj->objnode_tree_height]) {
		height = obj->objnode_tree_height + 1;
		if (index > tmem_objnode_tree_h2max[height])
			while (index > tmem_objnode_tree_h2max[height])
				height++;
		if (obj->objnode_tree_root == NULL) {
			obj->objnode_tree_height = height;
			goto insert;
		}
		do {
			newnode = tmem_objnode_alloc(obj);
			if (!newnode) {
				ret = -ENOMEM;
				goto out;
			}
			newnode->slots[0] = obj->objnode_tree_root;
			newnode->slots_in_use = 1;
			obj->objnode_tree_root = newnode;
			obj->objnode_tree_height++;
		} while (height > obj->objnode_tree_height);
	}
insert:
	slot = obj->objnode_tree_root;
	height = obj->objnode_tree_height;
	shift = (height-1) * OBJNODE_TREE_MAP_SHIFT;
	while (height > 0) {
		if (slot == NULL) {
			/* add a child objnode.  */
			slot = tmem_objnode_alloc(obj);
			if (!slot) {
				ret = -ENOMEM;
				goto out;
			}
			if (objnode) {

				objnode->slots[offset] = slot;
				objnode->slots_in_use++;
			} else
				obj->objnode_tree_root = slot;
		}
		/* go down a level */
		offset = (index >> shift) & OBJNODE_TREE_MAP_MASK;
		objnode = slot;
		slot = objnode->slots[offset];
		shift -= OBJNODE_TREE_MAP_SHIFT;
		height--;
	}
	BUG_ON(slot != NULL);
	if (objnode) {
		objnode->slots_in_use++;
		objnode->slots[offset] = pampd;
	} else
		obj->objnode_tree_root = pampd;
	obj->pampd_count++;
out:
	return ret;
}

static void *tmem_pampd_delete_from_obj(struct tmem_obj *obj, uint32_t index)
{
	struct tmem_objnode_tree_path path[OBJNODE_TREE_MAX_PATH + 1];
	struct tmem_objnode_tree_path *pathp = path;
	struct tmem_objnode *slot = NULL;
	unsigned int height, shift;
	int offset;

	BUG_ON(obj == NULL);
	ASSERT_SENTINEL(obj, OBJ);
	BUG_ON(obj->pool == NULL);
	ASSERT_SENTINEL(obj->pool, POOL);
	height = obj->objnode_tree_height;
	if (index > tmem_objnode_tree_h2max[height])
		goto out;
	slot = obj->objnode_tree_root;
	if (height == 0 && obj->objnode_tree_root) {
		obj->objnode_tree_root = NULL;
		goto out;
	}
	shift = (height - 1) * OBJNODE_TREE_MAP_SHIFT;
	pathp->objnode = NULL;
	do {
		if (slot == NULL)
			goto out;
		pathp++;
		offset = (index >> shift) & OBJNODE_TREE_MAP_MASK;
		pathp->offset = offset;
		pathp->objnode = slot;
		slot = slot->slots[offset];
		shift -= OBJNODE_TREE_MAP_SHIFT;
		height--;
	} while (height > 0);
	if (slot == NULL)
		goto out;
	while (pathp->objnode) {
		pathp->objnode->slots[pathp->offset] = NULL;
		pathp->objnode->slots_in_use--;
		if (pathp->objnode->slots_in_use) {
			if (pathp->objnode == obj->objnode_tree_root) {
				while (obj->objnode_tree_height > 0 &&
				  obj->objnode_tree_root->slots_in_use == 1 &&
				  obj->objnode_tree_root->slots[0]) {
					struct tmem_objnode *to_free =
						obj->objnode_tree_root;

					obj->objnode_tree_root =
							to_free->slots[0];
					obj->objnode_tree_height--;
					to_free->slots[0] = NULL;
					to_free->slots_in_use = 0;
					tmem_objnode_free(to_free);
				}
			}
			goto out;
		}
		tmem_objnode_free(pathp->objnode); /* 0 slots used, free it */
		pathp--;
	}
	obj->objnode_tree_height = 0;
	obj->objnode_tree_root = NULL;

out:
	if (slot != NULL)
		obj->pampd_count--;
	BUG_ON(obj->pampd_count < 0);
	return slot;
}

/* recursively walk the objnode_tree destroying pampds and objnodes */
static void tmem_objnode_node_destroy(struct tmem_obj *obj,
					struct tmem_objnode *objnode,
					unsigned int ht)
{
	int i;

	if (ht == 0)
		return;
	for (i = 0; i < OBJNODE_TREE_MAP_SIZE; i++) {
		if (objnode->slots[i]) {
			if (ht == 1) {
				obj->pampd_count--;
				(*tmem_pamops.free)(objnode->slots[i],
						obj->pool, NULL, 0);
				objnode->slots[i] = NULL;
				continue;
			}
			tmem_objnode_node_destroy(obj, objnode->slots[i], ht-1);
			tmem_objnode_free(objnode->slots[i]);
			objnode->slots[i] = NULL;
		}
	}
}

static void tmem_pampd_destroy_all_in_obj(struct tmem_obj *obj)
{
	if (obj->objnode_tree_root == NULL)
		return;
	if (obj->objnode_tree_height == 0) {
		obj->pampd_count--;
		(*tmem_pamops.free)(obj->objnode_tree_root, obj->pool, NULL, 0);
	} else {
		tmem_objnode_node_destroy(obj, obj->objnode_tree_root,
					obj->objnode_tree_height);
		tmem_objnode_free(obj->objnode_tree_root);
		obj->objnode_tree_height = 0;
	}
	obj->objnode_tree_root = NULL;
	(*tmem_pamops.free_obj)(obj->pool, obj);
}

/*
 * Tmem is operated on by a set of well-defined actions:
 * "put", "get", "flush", "flush_object", "new pool" and "destroy pool".
 * (The tmem ABI allows for subpages and exchanges but these operations
 * are not included in this implementation.)
 *
 * These "tmem core" operations are implemented in the following functions.
 */

/*
 * "Put" a page, e.g. copy a page from the kernel into newly allocated
 * PAM space (if such space is available).  Tmem_put is complicated by
 * a corner case: What if a page with matching handle already exists in
 * tmem?  To guarantee coherency, one of two actions is necessary: Either
 * the data for the page must be overwritten, or the page must be
 * "flushed" so that the data is not accessible to a subsequent "get".
 * Since these "duplicate puts" are relatively rare, this implementation
 * always flushes for simplicity.
 */
int tmem_put(struct tmem_pool *pool, struct tmem_oid *oidp, uint32_t index,
		char *data, size_t size, bool raw, bool ephemeral)
{
	struct tmem_obj *obj = NULL, *objfound = NULL, *objnew = NULL;
	void *pampd = NULL, *pampd_del = NULL;
	int ret = -ENOMEM;
	struct tmem_hashbucket *hb;

	hb = &pool->hashbucket[tmem_oid_hash(oidp)];
	spin_lock(&hb->lock);
	obj = objfound = tmem_obj_find(hb, oidp);
	if (obj != NULL) {
		pampd = tmem_pampd_lookup_in_obj(objfound, index);
		if (pampd != NULL) {
			/* if found, is a dup put, flush the old one */
			pampd_del = tmem_pampd_delete_from_obj(obj, index);
			BUG_ON(pampd_del != pampd);
			(*tmem_pamops.free)(pampd, pool, oidp, index);
			if (obj->pampd_count == 0) {
				objnew = obj;
				objfound = NULL;
			}
			pampd = NULL;
		}
	} else {
		obj = objnew = (*tmem_hostops.obj_alloc)(pool);
		if (unlikely(obj == NULL)) {
			ret = -ENOMEM;
			goto out;
		}
		tmem_obj_init(obj, hb, pool, oidp);
	}
	BUG_ON(obj == NULL);
	BUG_ON(((objnew != obj) && (objfound != obj)) || (objnew == objfound));
	pampd = (*tmem_pamops.create)(data, size, raw, ephemeral,
					obj->pool, &obj->oid, index);
	if (unlikely(pampd == NULL))
		goto free;
	ret = tmem_pampd_add_to_obj(obj, index, pampd);
	if (unlikely(ret == -ENOMEM))
		/* may have partially built objnode tree ("stump") */
		goto delete_and_free;
	goto out;

delete_and_free:
	(void)tmem_pampd_delete_from_obj(obj, index);
free:
	if (pampd)
		(*tmem_pamops.free)(pampd, pool, NULL, 0);
	if (objnew) {
		tmem_obj_free(objnew, hb);
		(*tmem_hostops.obj_free)(objnew, pool);
	}
out:
	spin_unlock(&hb->lock);
	return ret;
}

/*
 * "Get" a page, e.g. if one can be found, copy the tmem page with the
 * matching handle from PAM space to the kernel.  By tmem definition,
 * when a "get" is successful on an ephemeral page, the page is "flushed",
 * and when a "get" is successful on a persistent page, the page is retained
 * in tmem.  Note that to preserve
 * coherency, "get" can never be skipped if tmem contains the data.
 * That is, if a get is done with a certain handle and fails, any
 * subsequent "get" must also fail (unless of course there is a
 * "put" done with the same handle).

 */
int tmem_get(struct tmem_pool *pool, struct tmem_oid *oidp, uint32_t index,
		char *data, size_t *size, bool raw, int get_and_free)
{
	struct tmem_obj *obj;
	void *pampd;
	bool ephemeral = is_ephemeral(pool);
	uint32_t ret = -1;
	struct tmem_hashbucket *hb;
	bool free = (get_and_free == 1) || ((get_and_free == 0) && ephemeral);
	bool lock_held = false;

	hb = &pool->hashbucket[tmem_oid_hash(oidp)];
	spin_lock(&hb->lock);
	lock_held = true;
	obj = tmem_obj_find(hb, oidp);
	if (obj == NULL)
		goto out;
	if (free)
		pampd = tmem_pampd_delete_from_obj(obj, index);
	else
		pampd = tmem_pampd_lookup_in_obj(obj, index);
	if (pampd == NULL)
		goto out;
	if (free) {
		if (obj->pampd_count == 0) {
			tmem_obj_free(obj, hb);
			(*tmem_hostops.obj_free)(obj, pool);
			obj = NULL;
		}
	}
	if (tmem_pamops.is_remote(pampd)) {
		lock_held = false;
		spin_unlock(&hb->lock);
	}
	if (free)
		ret = (*tmem_pamops.get_data_and_free)(
				data, size, raw, pampd, pool, oidp, index);
	else
		ret = (*tmem_pamops.get_data)(
				data, size, raw, pampd, pool, oidp, index);
	if (ret < 0)
		goto out;
	ret = 0;
out:
	if (lock_held)
		spin_unlock(&hb->lock);
	return ret;
}

/*
 * If a page in tmem matches the handle, "flush" this page from tmem such
 * that any subsequent "get" does not succeed (unless, of course, there
 * was another "put" with the same handle).
 */
int tmem_flush_page(struct tmem_pool *pool,
				struct tmem_oid *oidp, uint32_t index)
{
	struct tmem_obj *obj;
	void *pampd;
	int ret = -1;
	struct tmem_hashbucket *hb;

	hb = &pool->hashbucket[tmem_oid_hash(oidp)];
	spin_lock(&hb->lock);
	obj = tmem_obj_find(hb, oidp);
	if (obj == NULL)
		goto out;
	pampd = tmem_pampd_delete_from_obj(obj, index);
	if (pampd == NULL)
		goto out;
	(*tmem_pamops.free)(pampd, pool, oidp, index);
	if (obj->pampd_count == 0) {
		tmem_obj_free(obj, hb);
		(*tmem_hostops.obj_free)(obj, pool);
	}
	ret = 0;

out:
	spin_unlock(&hb->lock);
	return ret;
}

/*
 * If a page in tmem matches the handle, replace the page so that any
 * subsequent "get" gets the new page.  Returns 0 if
 * there was a page to replace, else returns -1.
 */
int tmem_replace(struct tmem_pool *pool, struct tmem_oid *oidp,
			uint32_t index, void *new_pampd)
{
	struct tmem_obj *obj;
	int ret = -1;
	struct tmem_hashbucket *hb;

	hb = &pool->hashbucket[tmem_oid_hash(oidp)];
	spin_lock(&hb->lock);
	obj = tmem_obj_find(hb, oidp);
	if (obj == NULL)
		goto out;
	new_pampd = tmem_pampd_replace_in_obj(obj, index, new_pampd);
	ret = (*tmem_pamops.replace_in_obj)(new_pampd, obj);
out:
	spin_unlock(&hb->lock);
	return ret;
}

/*
 * "Flush" all pages in tmem matching this oid.
 */
int tmem_flush_object(struct tmem_pool *pool, struct tmem_oid *oidp)
{
	struct tmem_obj *obj;
	struct tmem_hashbucket *hb;
	int ret = -1;

	hb = &pool->hashbucket[tmem_oid_hash(oidp)];
	spin_lock(&hb->lock);
	obj = tmem_obj_find(hb, oidp);
	if (obj == NULL)
		goto out;
	tmem_pampd_destroy_all_in_obj(obj);
	tmem_obj_free(obj, hb);
	(*tmem_hostops.obj_free)(obj, pool);
	ret = 0;

out:
	spin_unlock(&hb->lock);
	return ret;
}

/*
 * "Flush" all pages (and tmem_objs) from this tmem_pool and disable
 * all subsequent access to this tmem_pool.
 */
int tmem_destroy_pool(struct tmem_pool *pool)
{
	int ret = -1;

	if (pool == NULL)
		goto out;
	tmem_pool_flush(pool, 1);
	ret = 0;
out:
	return ret;
}

static LIST_HEAD(tmem_global_pool_list);

/*
 * Create a new tmem_pool with the provided flag and return
 * a pool id provided by the tmem host implementation.
 */
void tmem_new_pool(struct tmem_pool *pool, uint32_t flags)
{
	int persistent = flags & TMEM_POOL_PERSIST;
	int shared = flags & TMEM_POOL_SHARED;
	struct tmem_hashbucket *hb = &pool->hashbucket[0];
	int i;

	for (i = 0; i < TMEM_HASH_BUCKETS; i++, hb++) {
		hb->obj_rb_root = RB_ROOT;
		spin_lock_init(&hb->lock);
	}
	INIT_LIST_HEAD(&pool->pool_list);
	atomic_set(&pool->obj_count, 0);
	SET_SENTINEL(pool, POOL);
	list_add_tail(&pool->pool_list, &tmem_global_pool_list);
	pool->persistent = persistent;
	pool->shared = shared;
}
