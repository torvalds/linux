// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2018 Mellanox Technologies */

#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <linux/hashtable.h>
#include <linux/refcount.h>

#include "mapping.h"

#define MAPPING_GRACE_PERIOD 2000

static LIST_HEAD(shared_ctx_list);
static DEFINE_MUTEX(shared_ctx_lock);

struct mapping_ctx {
	struct xarray xarray;
	DECLARE_HASHTABLE(ht, 8);
	struct mutex lock; /* Guards hashtable and xarray */
	unsigned long max_id;
	size_t data_size;
	bool delayed_removal;
	struct delayed_work dwork;
	struct list_head pending_list;
	spinlock_t pending_list_lock; /* Guards pending list */
	u64 id;
	u8 type;
	struct list_head list;
	refcount_t refcount;
};

struct mapping_item {
	struct rcu_head rcu;
	struct list_head list;
	unsigned long timeout;
	struct hlist_node node;
	int cnt;
	u32 id;
	char data[];
};

int mapping_add(struct mapping_ctx *ctx, void *data, u32 *id)
{
	struct mapping_item *mi;
	int err = -ENOMEM;
	u32 hash_key;

	mutex_lock(&ctx->lock);

	hash_key = jhash(data, ctx->data_size, 0);
	hash_for_each_possible(ctx->ht, mi, node, hash_key) {
		if (!memcmp(data, mi->data, ctx->data_size))
			goto attach;
	}

	mi = kzalloc(sizeof(*mi) + ctx->data_size, GFP_KERNEL);
	if (!mi)
		goto err_alloc;

	memcpy(mi->data, data, ctx->data_size);
	hash_add(ctx->ht, &mi->node, hash_key);

	err = xa_alloc(&ctx->xarray, &mi->id, mi, XA_LIMIT(1, ctx->max_id),
		       GFP_KERNEL);
	if (err)
		goto err_assign;
attach:
	++mi->cnt;
	*id = mi->id;

	mutex_unlock(&ctx->lock);

	return 0;

err_assign:
	hash_del(&mi->node);
	kfree(mi);
err_alloc:
	mutex_unlock(&ctx->lock);

	return err;
}

static void mapping_remove_and_free(struct mapping_ctx *ctx,
				    struct mapping_item *mi)
{
	xa_erase(&ctx->xarray, mi->id);
	kfree_rcu(mi, rcu);
}

static void mapping_free_item(struct mapping_ctx *ctx,
			      struct mapping_item *mi)
{
	if (!ctx->delayed_removal) {
		mapping_remove_and_free(ctx, mi);
		return;
	}

	mi->timeout = jiffies + msecs_to_jiffies(MAPPING_GRACE_PERIOD);

	spin_lock(&ctx->pending_list_lock);
	list_add_tail(&mi->list, &ctx->pending_list);
	spin_unlock(&ctx->pending_list_lock);

	schedule_delayed_work(&ctx->dwork, MAPPING_GRACE_PERIOD);
}

int mapping_remove(struct mapping_ctx *ctx, u32 id)
{
	unsigned long index = id;
	struct mapping_item *mi;
	int err = -ENOENT;

	mutex_lock(&ctx->lock);
	mi = xa_load(&ctx->xarray, index);
	if (!mi)
		goto out;
	err = 0;

	if (--mi->cnt > 0)
		goto out;

	hash_del(&mi->node);
	mapping_free_item(ctx, mi);
out:
	mutex_unlock(&ctx->lock);

	return err;
}

int mapping_find(struct mapping_ctx *ctx, u32 id, void *data)
{
	unsigned long index = id;
	struct mapping_item *mi;
	int err = -ENOENT;

	rcu_read_lock();
	mi = xa_load(&ctx->xarray, index);
	if (!mi)
		goto err_find;

	memcpy(data, mi->data, ctx->data_size);
	err = 0;

err_find:
	rcu_read_unlock();
	return err;
}

static void
mapping_remove_and_free_list(struct mapping_ctx *ctx, struct list_head *list)
{
	struct mapping_item *mi;

	list_for_each_entry(mi, list, list)
		mapping_remove_and_free(ctx, mi);
}

static void mapping_work_handler(struct work_struct *work)
{
	unsigned long min_timeout = 0, now = jiffies;
	struct mapping_item *mi, *next;
	LIST_HEAD(pending_items);
	struct mapping_ctx *ctx;

	ctx = container_of(work, struct mapping_ctx, dwork.work);

	spin_lock(&ctx->pending_list_lock);
	list_for_each_entry_safe(mi, next, &ctx->pending_list, list) {
		if (time_after(now, mi->timeout))
			list_move(&mi->list, &pending_items);
		else if (!min_timeout ||
			 time_before(mi->timeout, min_timeout))
			min_timeout = mi->timeout;
	}
	spin_unlock(&ctx->pending_list_lock);

	mapping_remove_and_free_list(ctx, &pending_items);

	if (min_timeout)
		schedule_delayed_work(&ctx->dwork, abs(min_timeout - now));
}

static void mapping_flush_work(struct mapping_ctx *ctx)
{
	if (!ctx->delayed_removal)
		return;

	cancel_delayed_work_sync(&ctx->dwork);
	mapping_remove_and_free_list(ctx, &ctx->pending_list);
}

struct mapping_ctx *
mapping_create(size_t data_size, u32 max_id, bool delayed_removal)
{
	struct mapping_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->max_id = max_id ? max_id : UINT_MAX;
	ctx->data_size = data_size;

	if (delayed_removal) {
		INIT_DELAYED_WORK(&ctx->dwork, mapping_work_handler);
		INIT_LIST_HEAD(&ctx->pending_list);
		spin_lock_init(&ctx->pending_list_lock);
		ctx->delayed_removal = true;
	}

	mutex_init(&ctx->lock);
	xa_init_flags(&ctx->xarray, XA_FLAGS_ALLOC1);

	refcount_set(&ctx->refcount, 1);
	INIT_LIST_HEAD(&ctx->list);

	return ctx;
}

struct mapping_ctx *
mapping_create_for_id(u64 id, u8 type, size_t data_size, u32 max_id, bool delayed_removal)
{
	struct mapping_ctx *ctx;

	mutex_lock(&shared_ctx_lock);
	list_for_each_entry(ctx, &shared_ctx_list, list) {
		if (ctx->id == id && ctx->type == type) {
			if (refcount_inc_not_zero(&ctx->refcount))
				goto unlock;
			break;
		}
	}

	ctx = mapping_create(data_size, max_id, delayed_removal);
	if (IS_ERR(ctx))
		goto unlock;

	ctx->id = id;
	ctx->type = type;
	list_add(&ctx->list, &shared_ctx_list);

unlock:
	mutex_unlock(&shared_ctx_lock);
	return ctx;
}

void mapping_destroy(struct mapping_ctx *ctx)
{
	if (!refcount_dec_and_test(&ctx->refcount))
		return;

	mutex_lock(&shared_ctx_lock);
	list_del(&ctx->list);
	mutex_unlock(&shared_ctx_lock);

	mapping_flush_work(ctx);
	xa_destroy(&ctx->xarray);
	mutex_destroy(&ctx->lock);

	kfree(ctx);
}
