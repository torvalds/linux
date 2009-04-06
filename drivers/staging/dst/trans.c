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

#include <linux/bio.h>
#include <linux/dst.h>
#include <linux/slab.h>
#include <linux/mempool.h>

/*
 * Transaction memory pool size.
 */
static int dst_mempool_num = 32;
module_param(dst_mempool_num, int, 0644);

/*
 * Transaction tree management.
 */
static inline int dst_trans_cmp(dst_gen_t gen, dst_gen_t new)
{
	if (gen < new)
		return 1;
	if (gen > new)
		return -1;
	return 0;
}

struct dst_trans *dst_trans_search(struct dst_node *node, dst_gen_t gen)
{
	struct rb_root *root = &node->trans_root;
	struct rb_node *n = root->rb_node;
	struct dst_trans *t, *ret = NULL;
	int cmp;

	while (n) {
		t = rb_entry(n, struct dst_trans, trans_entry);

		cmp = dst_trans_cmp(t->gen, gen);
		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else {
			ret = t;
			break;
		}
	}

	dprintk("%s: %s transaction: id: %llu.\n", __func__,
			(ret)?"found":"not found", gen);

	return ret;
}

static int dst_trans_insert(struct dst_trans *new)
{
	struct rb_root *root = &new->n->trans_root;
	struct rb_node **n = &root->rb_node, *parent = NULL;
	struct dst_trans *ret = NULL, *t;
	int cmp;

	while (*n) {
		parent = *n;

		t = rb_entry(parent, struct dst_trans, trans_entry);

		cmp = dst_trans_cmp(t->gen, new->gen);
		if (cmp < 0)
			n = &parent->rb_left;
		else if (cmp > 0)
			n = &parent->rb_right;
		else {
			ret = t;
			break;
		}
	}

	new->send_time = jiffies;
	if (ret) {
		printk("%s: exist: old: gen: %llu, bio: %llu/%u, send_time: %lu, "
				"new: gen: %llu, bio: %llu/%u, send_time: %lu.\n",
			__func__,
			ret->gen, (u64)ret->bio->bi_sector,
			ret->bio->bi_size, ret->send_time,
			new->gen, (u64)new->bio->bi_sector,
			new->bio->bi_size, new->send_time);
		return -EEXIST;
	}

	rb_link_node(&new->trans_entry, parent, n);
	rb_insert_color(&new->trans_entry, root);

	dprintk("%s: inserted: gen: %llu, bio: %llu/%u, send_time: %lu.\n",
		__func__, new->gen, (u64)new->bio->bi_sector,
		new->bio->bi_size, new->send_time);

	return 0;
}

int dst_trans_remove_nolock(struct dst_trans *t)
{
	struct dst_node *n = t->n;

	if (t->trans_entry.rb_parent_color) {
		rb_erase(&t->trans_entry, &n->trans_root);
		t->trans_entry.rb_parent_color = 0;
	}
	return 0;
}

int dst_trans_remove(struct dst_trans *t)
{
	int ret;
	struct dst_node *n = t->n;

	mutex_lock(&n->trans_lock);
	ret = dst_trans_remove_nolock(t);
	mutex_unlock(&n->trans_lock);

	return ret;
}

/*
 * When transaction is completed and there are no more users,
 * we complete appriate block IO request with given error status.
 */
void dst_trans_put(struct dst_trans *t)
{
	if (atomic_dec_and_test(&t->refcnt)) {
		struct bio *bio = t->bio;

		dprintk("%s: completed t: %p, gen: %llu, bio: %p.\n",
				__func__, t, t->gen, bio);

		bio_endio(bio, t->error);
		bio_put(bio);

		dst_node_put(t->n);
		mempool_free(t, t->n->trans_pool);
	}
}

/*
 * Process given block IO request: allocate transaction, insert it into the tree
 * and send/schedule crypto processing.
 */
int dst_process_bio(struct dst_node *n, struct bio *bio)
{
	struct dst_trans *t;
	int err = -ENOMEM;

	t = mempool_alloc(n->trans_pool, GFP_NOFS);
	if (!t)
		goto err_out_exit;

	t->n = dst_node_get(n);
	t->bio = bio;
	t->error = 0;
	t->retries = 0;
	atomic_set(&t->refcnt, 1);
	t->gen = atomic_long_inc_return(&n->gen);

	t->enc = bio_data_dir(bio);
	dst_bio_to_cmd(bio, &t->cmd, DST_IO, t->gen);

	mutex_lock(&n->trans_lock);
	err = dst_trans_insert(t);
	mutex_unlock(&n->trans_lock);
	if (err)
		goto err_out_free;

	dprintk("%s: gen: %llu, bio: %llu/%u, dir/enc: %d, need_crypto: %d.\n",
			__func__, t->gen, (u64)bio->bi_sector,
			bio->bi_size, t->enc, dst_need_crypto(n));

	if (dst_need_crypto(n) && t->enc)
		dst_trans_crypto(t);
	else
		dst_trans_send(t);

	return 0;

err_out_free:
	dst_node_put(n);
	mempool_free(t, n->trans_pool);
err_out_exit:
	bio_endio(bio, err);
	bio_put(bio);
	return err;
}

/*
 * Scan for timeout/stale transactions.
 * Each transaction is being resent multiple times before error completion.
 */
static void dst_trans_scan(struct work_struct *work)
{
	struct dst_node *n = container_of(work, struct dst_node, trans_work.work);
	struct rb_node *rb_node;
	struct dst_trans *t;
	unsigned long timeout = n->trans_scan_timeout;
	int num = 10 * n->trans_max_retries;

	mutex_lock(&n->trans_lock);

	for (rb_node = rb_first(&n->trans_root); rb_node; ) {
		t = rb_entry(rb_node, struct dst_trans, trans_entry);

		if (timeout && time_after(t->send_time + timeout, jiffies)
				&& t->retries == 0)
			break;
#if 0
		dprintk("%s: t: %p, gen: %llu, n: %s, retries: %u, max: %u.\n",
			__func__, t, t->gen, n->name,
			t->retries, n->trans_max_retries);
#endif
		if (--num == 0)
			break;

		dst_trans_get(t);

		rb_node = rb_next(rb_node);

		if (timeout && (++t->retries < n->trans_max_retries)) {
			dst_trans_send(t);
		} else {
			t->error = -ETIMEDOUT;
			dst_trans_remove_nolock(t);
			dst_trans_put(t);
		}

		dst_trans_put(t);
	}

	mutex_unlock(&n->trans_lock);

	/*
	 * If no timeout specified then system is in the middle of exiting process,
	 * so no need to reschedule scanning process again.
	 */
	if (timeout) {
		if (!num)
			timeout = HZ;
		schedule_delayed_work(&n->trans_work, timeout);
	}
}

/*
 * Flush all transactions and mark them as timed out.
 * Destroy transaction pools.
 */
void dst_node_trans_exit(struct dst_node *n)
{
	struct dst_trans *t;
	struct rb_node *rb_node;

	if (!n->trans_cache)
		return;

	dprintk("%s: n: %p, cancelling the work.\n", __func__, n);
	cancel_delayed_work_sync(&n->trans_work);
	flush_scheduled_work();
	dprintk("%s: n: %p, work has been cancelled.\n", __func__, n);

	for (rb_node = rb_first(&n->trans_root); rb_node; ) {
		t = rb_entry(rb_node, struct dst_trans, trans_entry);

		dprintk("%s: t: %p, gen: %llu, n: %s.\n",
			__func__, t, t->gen, n->name);

		rb_node = rb_next(rb_node);

		t->error = -ETIMEDOUT;
		dst_trans_remove_nolock(t);
		dst_trans_put(t);
	}

	mempool_destroy(n->trans_pool);
	kmem_cache_destroy(n->trans_cache);
}

/*
 * Initialize transaction storage for given node.
 * Transaction stores not only control information,
 * but also network command and crypto data (if needed)
 * to reduce number of allocations. Thus transaction size
 * differs from node to node.
 */
int dst_node_trans_init(struct dst_node *n, unsigned int size)
{
	/*
	 * We need this, since node with given name can be dropped from the
	 * hash table, but be still alive, so subsequent creation of the node
	 * with the same name may collide with existing cache name.
	 */

	snprintf(n->cache_name, sizeof(n->cache_name), "%s-%p", n->name, n);

	n->trans_cache = kmem_cache_create(n->cache_name,
			size + n->crypto.crypto_attached_size,
			0, 0, NULL);
	if (!n->trans_cache)
		goto err_out_exit;

	n->trans_pool = mempool_create_slab_pool(dst_mempool_num, n->trans_cache);
	if (!n->trans_pool)
		goto err_out_cache_destroy;

	mutex_init(&n->trans_lock);
	n->trans_root = RB_ROOT;

	INIT_DELAYED_WORK(&n->trans_work, dst_trans_scan);
	schedule_delayed_work(&n->trans_work, n->trans_scan_timeout);

	dprintk("%s: n: %p, size: %u, crypto: %u.\n",
		__func__, n, size, n->crypto.crypto_attached_size);

	return 0;

err_out_cache_destroy:
	kmem_cache_destroy(n->trans_cache);
err_out_exit:
	return -ENOMEM;
}
