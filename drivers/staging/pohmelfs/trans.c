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
#include <linux/crypto.h>
#include <linux/fs.h>
#include <linux/jhash.h>
#include <linux/hash.h>
#include <linux/ktime.h>
#include <linux/mempool.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/parser.h>
#include <linux/poll.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/writeback.h>

#include "netfs.h"

static struct kmem_cache *netfs_trans_dst;
static mempool_t *netfs_trans_dst_pool;

static void netfs_trans_init_static(struct netfs_trans *t, int num, int size)
{
	t->page_num = num;
	t->total_size = size;
	atomic_set(&t->refcnt, 1);

	spin_lock_init(&t->dst_lock);
	INIT_LIST_HEAD(&t->dst_list);
}

static int netfs_trans_send_pages(struct netfs_trans *t, struct netfs_state *st)
{
	int err = 0;
	unsigned int i, attached_pages = t->attached_pages, ci;
	struct msghdr msg;
	struct page **pages = (t->eng)?t->eng->pages:t->pages;
	struct page *p;
	unsigned int size;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_WAITALL | MSG_MORE;

	ci = 0;
	for (i=0; i<t->page_num; ++i) {
		struct page *page = pages[ci];
		struct netfs_cmd cmd;
		struct iovec io;

		p = t->pages[i];

		if (!p)
			continue;

		size = page_private(p);

		io.iov_base = &cmd;
		io.iov_len = sizeof(struct netfs_cmd);

		cmd.cmd = NETFS_WRITE_PAGE;
		cmd.ext = 0;
		cmd.id = 0;
		cmd.size = size;
		cmd.start = p->index;
		cmd.start <<= PAGE_CACHE_SHIFT;
		cmd.csize = 0;
		cmd.cpad = 0;
		cmd.iv = pohmelfs_gen_iv(t);

		netfs_convert_cmd(&cmd);

		msg.msg_iov = &io;
		msg.msg_iovlen = 1;
		msg.msg_flags = MSG_WAITALL | MSG_MORE;

		err = kernel_sendmsg(st->socket, &msg, (struct kvec *)msg.msg_iov, 1, sizeof(struct netfs_cmd));
		if (err <= 0) {
			printk("%s: %d/%d failed to send transaction header: t: %p, gen: %u, err: %d.\n",
					__func__, i, t->page_num, t, t->gen, err);
			if (err == 0)
				err = -ECONNRESET;
			goto err_out;
		}

		msg.msg_flags = MSG_WAITALL|(attached_pages == 1)?0:MSG_MORE;

		err = kernel_sendpage(st->socket, page, 0, size, msg.msg_flags);
		if (err <= 0) {
			printk("%s: %d/%d failed to send transaction page: t: %p, gen: %u, size: %u, err: %d.\n",
					__func__, i, t->page_num, t, t->gen, size, err);
			if (err == 0)
				err = -ECONNRESET;
			goto err_out;
		}

		dprintk("%s: %d/%d sent t: %p, gen: %u, page: %p/%p, size: %u.\n",
			__func__, i, t->page_num, t, t->gen, page, p, size);

		err = 0;
		attached_pages--;
		if (!attached_pages)
			break;
		ci++;

		continue;

err_out:
		printk("%s: t: %p, gen: %u, err: %d.\n", __func__, t, t->gen, err);
		netfs_state_exit(st);
		break;
	}

	return err;
}

int netfs_trans_send(struct netfs_trans *t, struct netfs_state *st)
{
	int err;
	struct msghdr msg;

	BUG_ON(!t->iovec.iov_len);
	BUG_ON(t->iovec.iov_len > 1024*1024*1024);

	netfs_state_lock_send(st);
	if (!st->socket) {
		err = netfs_state_init(st);
		if (err)
			goto err_out_unlock_return;
	}

	msg.msg_iov = &t->iovec;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_WAITALL;

	if (t->attached_pages)
		msg.msg_flags |= MSG_MORE;

	err = kernel_sendmsg(st->socket, &msg, (struct kvec *)msg.msg_iov, 1, t->iovec.iov_len);
	if (err <= 0) {
		printk("%s: failed to send contig transaction: t: %p, gen: %u, size: %zu, err: %d.\n",
				__func__, t, t->gen, t->iovec.iov_len, err);
		if (err == 0)
			err = -ECONNRESET;
		goto err_out_unlock_return;
	}

	dprintk("%s: sent %s transaction: t: %p, gen: %u, size: %zu, page_num: %u.\n",
			__func__, (t->page_num)?"partial":"full",
			t, t->gen, t->iovec.iov_len, t->page_num);

	err = 0;
	if (t->attached_pages)
		err = netfs_trans_send_pages(t, st);

err_out_unlock_return:

	if (st->need_reset) {
		netfs_state_exit(st);
	}
	netfs_state_unlock_send(st);

	dprintk("%s: t: %p, gen: %u, err: %d.\n",
		__func__, t, t->gen, err);

	t->result = err;
	return err;
}

static inline int netfs_trans_cmp(unsigned int gen, unsigned int new)
{
	if (gen < new)
		return 1;
	if (gen > new)
		return -1;
	return 0;
}

struct netfs_trans_dst *netfs_trans_search(struct netfs_state *st, unsigned int gen)
{
	struct rb_root *root = &st->trans_root;
	struct rb_node *n = root->rb_node;
	struct netfs_trans_dst *tmp, *ret = NULL;
	struct netfs_trans *t;
	int cmp;

	while (n) {
		tmp = rb_entry(n, struct netfs_trans_dst, state_entry);
		t = tmp->trans;

		cmp = netfs_trans_cmp(t->gen, gen);
		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else {
			ret = tmp;
			break;
		}
	}

	return ret;
}

static int netfs_trans_insert(struct netfs_trans_dst *ndst, struct netfs_state *st)
{
	struct rb_root *root = &st->trans_root;
	struct rb_node **n = &root->rb_node, *parent = NULL;
	struct netfs_trans_dst *ret = NULL, *tmp;
	struct netfs_trans *t = NULL, *new = ndst->trans;
	int cmp;

	while (*n) {
		parent = *n;

		tmp = rb_entry(parent, struct netfs_trans_dst, state_entry);
		t = tmp->trans;

		cmp = netfs_trans_cmp(t->gen, new->gen);
		if (cmp < 0)
			n = &parent->rb_left;
		else if (cmp > 0)
			n = &parent->rb_right;
		else {
			ret = tmp;
			break;
		}
	}

	if (ret) {
		printk("%s: exist: old: gen: %u, flags: %x, send_time: %lu, "
				"new: gen: %u, flags: %x, send_time: %lu.\n",
			__func__, t->gen, t->flags, ret->send_time,
			new->gen, new->flags, ndst->send_time);
		return -EEXIST;
	}

	rb_link_node(&ndst->state_entry, parent, n);
	rb_insert_color(&ndst->state_entry, root);
	ndst->send_time = jiffies;

	return 0;
}

int netfs_trans_remove_nolock(struct netfs_trans_dst *dst, struct netfs_state *st)
{
	if (dst && dst->state_entry.rb_parent_color) {
		rb_erase(&dst->state_entry, &st->trans_root);
		dst->state_entry.rb_parent_color = 0;
		return 1;
	}
	return 0;
}

static int netfs_trans_remove_state(struct netfs_trans_dst *dst)
{
	int ret;
	struct netfs_state *st = dst->state;

	mutex_lock(&st->trans_lock);
	ret = netfs_trans_remove_nolock(dst, st);
	mutex_unlock(&st->trans_lock);

	return ret;
}

/*
 * Create new destination for given transaction associated with given network state.
 * Transaction's reference counter is bumped and will be dropped when either
 * reply is received or when async timeout detection task will fail resending
 * and drop transaction.
 */
static int netfs_trans_push_dst(struct netfs_trans *t, struct netfs_state *st)
{
	struct netfs_trans_dst *dst;
	int err;

	dst = mempool_alloc(netfs_trans_dst_pool, GFP_KERNEL);
	if (!dst)
		return -ENOMEM;

	dst->retries = 0;
	dst->send_time = 0;
	dst->state = st;
	dst->trans = t;
	netfs_trans_get(t);

	mutex_lock(&st->trans_lock);
	err = netfs_trans_insert(dst, st);
	mutex_unlock(&st->trans_lock);

	if (err)
		goto err_out_free;

	spin_lock(&t->dst_lock);
	list_add_tail(&dst->trans_entry, &t->dst_list);
	spin_unlock(&t->dst_lock);

	return 0;

err_out_free:
	t->result = err;
	netfs_trans_put(t);
	mempool_free(dst, netfs_trans_dst_pool);
	return err;
}

static void netfs_trans_free_dst(struct netfs_trans_dst *dst)
{
	netfs_trans_put(dst->trans);
	mempool_free(dst, netfs_trans_dst_pool);
}

static void netfs_trans_remove_dst(struct netfs_trans_dst *dst)
{
	if (netfs_trans_remove_state(dst))
		netfs_trans_free_dst(dst);
}

/*
 * Drop destination transaction entry when we know it.
 */
void netfs_trans_drop_dst(struct netfs_trans_dst *dst)
{
	struct netfs_trans *t = dst->trans;

	spin_lock(&t->dst_lock);
	list_del_init(&dst->trans_entry);
	spin_unlock(&t->dst_lock);

	netfs_trans_remove_dst(dst);
}

/*
 * Drop destination transaction entry when we know it and when we
 * already removed dst from state tree.
 */
void netfs_trans_drop_dst_nostate(struct netfs_trans_dst *dst)
{
	struct netfs_trans *t = dst->trans;

	spin_lock(&t->dst_lock);
	list_del_init(&dst->trans_entry);
	spin_unlock(&t->dst_lock);

	netfs_trans_free_dst(dst);
}

/*
 * This drops destination transaction entry from appropriate network state
 * tree and drops related reference counter. It is possible that transaction
 * will be freed here if its reference counter hits zero.
 * Destination transaction entry will be freed.
 */
void netfs_trans_drop_trans(struct netfs_trans *t, struct netfs_state *st)
{
	struct netfs_trans_dst *dst, *tmp, *ret = NULL;

	spin_lock(&t->dst_lock);
	list_for_each_entry_safe(dst, tmp, &t->dst_list, trans_entry) {
		if (dst->state == st) {
			ret = dst;
			list_del(&dst->trans_entry);
			break;
		}
	}
	spin_unlock(&t->dst_lock);

	if (ret)
		netfs_trans_remove_dst(ret);
}

/*
 * This drops destination transaction entry from appropriate network state
 * tree and drops related reference counter. It is possible that transaction
 * will be freed here if its reference counter hits zero.
 * Destination transaction entry will be freed.
 */
void netfs_trans_drop_last(struct netfs_trans *t, struct netfs_state *st)
{
	struct netfs_trans_dst *dst, *tmp, *ret;

	spin_lock(&t->dst_lock);
	ret = list_entry(t->dst_list.prev, struct netfs_trans_dst, trans_entry);
	if (ret->state != st) {
		ret = NULL;
		list_for_each_entry_safe(dst, tmp, &t->dst_list, trans_entry) {
			if (dst->state == st) {
				ret = dst;
				list_del_init(&dst->trans_entry);
				break;
			}
		}
	} else {
		list_del(&ret->trans_entry);
	}
	spin_unlock(&t->dst_lock);

	if (ret)
		netfs_trans_remove_dst(ret);
}

static int netfs_trans_push(struct netfs_trans *t, struct netfs_state *st)
{
	int err;

	err = netfs_trans_push_dst(t, st);
	if (err)
		return err;

	err = netfs_trans_send(t, st);
	if (err)
		goto err_out_free;

	if (t->flags & NETFS_TRANS_SINGLE_DST)
		pohmelfs_switch_active(st->psb);

	return 0;

err_out_free:
	t->result = err;
	netfs_trans_drop_last(t, st);

	return err;
}

int netfs_trans_finish_send(struct netfs_trans *t, struct pohmelfs_sb *psb)
{
	struct pohmelfs_config *c;
	int err = -ENODEV;
	struct netfs_state *st;
#if 0
	dprintk("%s: t: %p, gen: %u, size: %u, page_num: %u, active: %p.\n",
		__func__, t, t->gen, t->iovec.iov_len, t->page_num, psb->active_state);
#endif
	mutex_lock(&psb->state_lock);
	list_for_each_entry(c, &psb->state_list, config_entry) {
		st = &c->state;

		if (t->flags & NETFS_TRANS_SINGLE_DST) {
			if (!(st->ctl.perm & POHMELFS_IO_PERM_READ))
				continue;
		} else {
			if (!(st->ctl.perm & POHMELFS_IO_PERM_WRITE))
				continue;
		}

		if (psb->active_state && (psb->active_state->state.ctl.prio >= st->ctl.prio))
			st = &psb->active_state->state;

		err = netfs_trans_push(t, st);
		if (!err && (t->flags & NETFS_TRANS_SINGLE_DST))
			break;
	}

	mutex_unlock(&psb->state_lock);
#if 0
	dprintk("%s: fully sent t: %p, gen: %u, size: %u, page_num: %u, err: %d.\n",
		__func__, t, t->gen, t->iovec.iov_len, t->page_num, err);
#endif
	if (err)
		t->result = err;
	return err;
}

int netfs_trans_finish(struct netfs_trans *t, struct pohmelfs_sb *psb)
{
	int err;
	struct netfs_cmd *cmd = t->iovec.iov_base;

	t->gen = atomic_inc_return(&psb->trans_gen);

	cmd->size = t->iovec.iov_len - sizeof(struct netfs_cmd) +
		t->attached_size + t->attached_pages * sizeof(struct netfs_cmd);
	cmd->cmd = NETFS_TRANS;
	cmd->start = t->gen;
	cmd->id = 0;

	if (psb->perform_crypto) {
		cmd->ext = psb->crypto_attached_size;
		cmd->csize = psb->crypto_attached_size;
	}

	dprintk("%s: t: %u, size: %u, iov_len: %zu, attached_size: %u, attached_pages: %u.\n",
			__func__, t->gen, cmd->size, t->iovec.iov_len, t->attached_size, t->attached_pages);
	err = pohmelfs_trans_crypt(t, psb);
	if (err) {
		t->result = err;
		netfs_convert_cmd(cmd);
		dprintk("%s: trans: %llu, crypto_attached_size: %u, attached_size: %u, attached_pages: %d, trans_size: %u, err: %d.\n",
			__func__, cmd->start, psb->crypto_attached_size, t->attached_size, t->attached_pages, cmd->size, err);
	}
	netfs_trans_put(t);
	return err;
}

/*
 * Resend transaction to remote server(s).
 * If new servers were added into superblock, we can try to send data
 * to them too.
 *
 * It is called under superblock's state_lock, so we can safely
 * dereference psb->state_list. Also, transaction's reference counter is
 * bumped, so it can not go away under us, thus we can safely access all
 * its members. State is locked.
 *
 * This function returns 0 if transaction was successfully sent to at
 * least one destination target.
 */
int netfs_trans_resend(struct netfs_trans *t, struct pohmelfs_sb *psb)
{
	struct netfs_trans_dst *dst;
	struct netfs_state *st;
	struct pohmelfs_config *c;
	int err, exist, error = -ENODEV;

	list_for_each_entry(c, &psb->state_list, config_entry) {
		st = &c->state;

		exist = 0;
		spin_lock(&t->dst_lock);
		list_for_each_entry(dst, &t->dst_list, trans_entry) {
			if (st == dst->state) {
				exist = 1;
				break;
			}
		}
		spin_unlock(&t->dst_lock);

		if (exist) {
			if (!(t->flags & NETFS_TRANS_SINGLE_DST) ||
					(c->config_entry.next == &psb->state_list)) {
				dprintk("%s: resending st: %p, t: %p, gen: %u.\n",
						__func__, st, t, t->gen);
				err = netfs_trans_send(t, st);
				if (!err)
					error = 0;
			}
			continue;
		}

		dprintk("%s: pushing/resending st: %p, t: %p, gen: %u.\n",
				__func__, st, t, t->gen);
		err = netfs_trans_push(t, st);
		if (err)
			continue;
		error = 0;
		if (t->flags & NETFS_TRANS_SINGLE_DST)
			break;
	}

	t->result = error;
	return error;
}

void *netfs_trans_add(struct netfs_trans *t, unsigned int size)
{
	struct iovec *io = &t->iovec;
	void *ptr;

	if (size > t->total_size) {
		ptr = ERR_PTR(-EINVAL);
		goto out;
	}

	if (io->iov_len + size > t->total_size) {
		dprintk("%s: too big size t: %p, gen: %u, iov_len: %zu, size: %u, total: %u.\n",
				__func__, t, t->gen, io->iov_len, size, t->total_size);
		ptr = ERR_PTR(-E2BIG);
		goto out;
	}

	ptr = io->iov_base + io->iov_len;
	io->iov_len += size;

out:
	dprintk("%s: t: %p, gen: %u, size: %u, total: %zu.\n",
		__func__, t, t->gen, size, io->iov_len);
	return ptr;
}

void netfs_trans_free(struct netfs_trans *t)
{
	if (t->eng)
		pohmelfs_crypto_thread_make_ready(t->eng->thread);
	kfree(t);
}

struct netfs_trans *netfs_trans_alloc(struct pohmelfs_sb *psb, unsigned int size,
		unsigned int flags, unsigned int nr)
{
	struct netfs_trans *t;
	unsigned int num, cont, pad, size_no_trans;
	unsigned int crypto_added = 0;
	struct netfs_cmd *cmd;

	if (psb->perform_crypto)
		crypto_added = psb->crypto_attached_size;

	/*
	 * |sizeof(struct netfs_trans)|
	 * |sizeof(struct netfs_cmd)| - transaction header
	 * |size| - buffer with requested size
	 * |padding| - crypto padding, zero bytes
	 * |nr * sizeof(struct page *)| - array of page pointers
	 *
	 * Overall size should be less than PAGE_SIZE for guaranteed allocation.
	 */

	cont = size;
	size = ALIGN(size, psb->crypto_align_size);
	pad = size - cont;

	size_no_trans = size + sizeof(struct netfs_cmd) * 2 + crypto_added;

	cont = sizeof(struct netfs_trans) + size_no_trans;

	num = (PAGE_SIZE - cont)/sizeof(struct page *);

	if (nr > num)
		nr = num;

	t = kzalloc(cont + nr*sizeof(struct page *), GFP_NOIO);
	if (!t)
		goto err_out_exit;

	t->iovec.iov_base = (void *)(t + 1);
	t->pages = (struct page **)(t->iovec.iov_base + size_no_trans);

	/*
	 * Reserving space for transaction header.
	 */
	t->iovec.iov_len = sizeof(struct netfs_cmd) + crypto_added;

	netfs_trans_init_static(t, nr, size_no_trans);

	t->flags = flags;
	t->psb = psb;

	cmd = (struct netfs_cmd *)t->iovec.iov_base;

	cmd->size = size;
	cmd->cpad = pad;
	cmd->csize = crypto_added;

	dprintk("%s: t: %p, gen: %u, size: %u, padding: %u, align_size: %u, flags: %x, "
			"page_num: %u, base: %p, pages: %p.\n",
			__func__, t, t->gen, size, pad, psb->crypto_align_size, flags, nr,
			t->iovec.iov_base, t->pages);

	return t;

err_out_exit:
	return NULL;
}

int netfs_trans_init(void)
{
	int err = -ENOMEM;

	netfs_trans_dst = kmem_cache_create("netfs_trans_dst", sizeof(struct netfs_trans_dst),
			0, 0, NULL);
	if (!netfs_trans_dst)
		goto err_out_exit;

	netfs_trans_dst_pool = mempool_create_slab_pool(256, netfs_trans_dst);
	if (!netfs_trans_dst_pool)
		goto err_out_free;

	return 0;

err_out_free:
	kmem_cache_destroy(netfs_trans_dst);
err_out_exit:
	return err;
}

void netfs_trans_exit(void)
{
	mempool_destroy(netfs_trans_dst_pool);
	kmem_cache_destroy(netfs_trans_dst);
}
