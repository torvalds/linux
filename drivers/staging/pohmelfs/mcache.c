/*
 * 2007+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mempool.h>

#include "netfs.h"

static struct kmem_cache *pohmelfs_mcache_cache;
static mempool_t *pohmelfs_mcache_pool;

static inline int pohmelfs_mcache_cmp(u64 gen, u64 new)
{
	if (gen < new)
		return 1;
	if (gen > new)
		return -1;
	return 0;
}

struct pohmelfs_mcache *pohmelfs_mcache_search(struct pohmelfs_sb *psb, u64 gen)
{
	struct rb_root *root = &psb->mcache_root;
	struct rb_node *n = root->rb_node;
	struct pohmelfs_mcache *tmp, *ret = NULL;
	int cmp;

	while (n) {
		tmp = rb_entry(n, struct pohmelfs_mcache, mcache_entry);

		cmp = pohmelfs_mcache_cmp(tmp->gen, gen);
		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else {
			ret = tmp;
			pohmelfs_mcache_get(ret);
			break;
		}
	}

	return ret;
}

static int pohmelfs_mcache_insert(struct pohmelfs_sb *psb, struct pohmelfs_mcache *m)
{
	struct rb_root *root = &psb->mcache_root;
	struct rb_node **n = &root->rb_node, *parent = NULL;
	struct pohmelfs_mcache *ret = NULL, *tmp;
	int cmp;

	while (*n) {
		parent = *n;

		tmp = rb_entry(parent, struct pohmelfs_mcache, mcache_entry);

		cmp = pohmelfs_mcache_cmp(tmp->gen, m->gen);
		if (cmp < 0)
			n = &parent->rb_left;
		else if (cmp > 0)
			n = &parent->rb_right;
		else {
			ret = tmp;
			break;
		}
	}

	if (ret)
		return -EEXIST;

	rb_link_node(&m->mcache_entry, parent, n);
	rb_insert_color(&m->mcache_entry, root);

	return 0;
}

static int pohmelfs_mcache_remove(struct pohmelfs_sb *psb, struct pohmelfs_mcache *m)
{
	if (m && m->mcache_entry.rb_parent_color) {
		rb_erase(&m->mcache_entry, &psb->mcache_root);
		m->mcache_entry.rb_parent_color = 0;
		return 1;
	}
	return 0;
}

void pohmelfs_mcache_remove_locked(struct pohmelfs_sb *psb, struct pohmelfs_mcache *m)
{
	mutex_lock(&psb->mcache_lock);
	pohmelfs_mcache_remove(psb, m);
	mutex_unlock(&psb->mcache_lock);
}

struct pohmelfs_mcache *pohmelfs_mcache_alloc(struct pohmelfs_sb *psb, u64 start,
		unsigned int size, void *data)
{
	struct pohmelfs_mcache *m;
	int err = -ENOMEM;

	m = mempool_alloc(pohmelfs_mcache_pool, GFP_KERNEL);
	if (!m)
		goto err_out_exit;

	init_completion(&m->complete);
	m->err = 0;
	atomic_set(&m->refcnt, 1);
	m->data = data;
	m->start = start;
	m->size = size;
	m->gen = atomic_long_inc_return(&psb->mcache_gen);

	mutex_lock(&psb->mcache_lock);
	err = pohmelfs_mcache_insert(psb, m);
	mutex_unlock(&psb->mcache_lock);
	if (err)
		goto err_out_free;

	return m;

err_out_free:
	mempool_free(m, pohmelfs_mcache_pool);
err_out_exit:
	return ERR_PTR(err);
}

void pohmelfs_mcache_free(struct pohmelfs_sb *psb, struct pohmelfs_mcache *m)
{
	pohmelfs_mcache_remove_locked(psb, m);

	mempool_free(m, pohmelfs_mcache_pool);
}

int __init pohmelfs_mcache_init(void)
{
	pohmelfs_mcache_cache = kmem_cache_create("pohmelfs_mcache_cache",
				sizeof(struct pohmelfs_mcache),
				0, (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD), NULL);
	if (!pohmelfs_mcache_cache)
		goto err_out_exit;

	pohmelfs_mcache_pool = mempool_create_slab_pool(256, pohmelfs_mcache_cache);
	if (!pohmelfs_mcache_pool)
		goto err_out_free;

	return 0;

err_out_free:
	kmem_cache_destroy(pohmelfs_mcache_cache);
err_out_exit:
	return -ENOMEM;
}

void pohmelfs_mcache_exit(void)
{
	mempool_destroy(pohmelfs_mcache_pool);
	kmem_cache_destroy(pohmelfs_mcache_cache);
}
