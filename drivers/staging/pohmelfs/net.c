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

#include <linux/fsnotify.h>
#include <linux/jhash.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>

#include "netfs.h"

/*
 * Async machinery lives here.
 * All commands being sent to server do _not_ require sync reply,
 * instead, if it is really needed, like readdir or readpage, caller
 * sleeps waiting for data, which will be placed into provided buffer
 * and caller will be awakened.
 *
 * Every command response can come without some listener. For example
 * readdir response will add new objects into cache without appropriate
 * request from userspace. This is used in cache coherency.
 *
 * If object is not found for given data, it is discarded.
 *
 * All requests are received by dedicated kernel thread.
 */

/*
 * Basic network sending/receiving functions.
 * Blocked mode is used.
 */
static int netfs_data_recv(struct netfs_state *st, void *buf, u64 size)
{
	struct msghdr msg;
	struct kvec iov;
	int err;

	BUG_ON(!size);

	iov.iov_base = buf;
	iov.iov_len = size;

	msg.msg_iov = (struct iovec *)&iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_DONTWAIT;

	err = kernel_recvmsg(st->socket, &msg, &iov, 1, iov.iov_len,
			msg.msg_flags);
	if (err <= 0) {
		printk("%s: failed to recv data: size: %llu, err: %d.\n", __func__, size, err);
		if (err == 0)
			err = -ECONNRESET;
	}

	return err;
}

static int pohmelfs_data_recv(struct netfs_state *st, void *data, unsigned int size)
{
	unsigned int revents = 0;
	unsigned int err_mask = POLLERR | POLLHUP | POLLRDHUP;
	unsigned int mask = err_mask | POLLIN;
	int err = 0;

	while (size && !err) {
		revents = netfs_state_poll(st);

		if (!(revents & mask)) {
			DEFINE_WAIT(wait);

			for (;;) {
				prepare_to_wait(&st->thread_wait, &wait, TASK_INTERRUPTIBLE);
				if (kthread_should_stop())
					break;

				revents = netfs_state_poll(st);

				if (revents & mask)
					break;

				if (signal_pending(current))
					break;

				schedule();
				continue;
			}
			finish_wait(&st->thread_wait, &wait);
		}

		err = 0;
		netfs_state_lock(st);
		if (st->socket && (st->read_socket == st->socket) && (revents & POLLIN)) {
			err = netfs_data_recv(st, data, size);
			if (err > 0) {
				data += err;
				size -= err;
				err = 0;
			} else if (err == 0)
				err = -ECONNRESET;
		}

		if (revents & err_mask) {
			printk("%s: revents: %x, socket: %p, size: %u, err: %d.\n",
					__func__, revents, st->socket, size, err);
			err = -ECONNRESET;
		}
		netfs_state_unlock(st);

		if (err < 0) {
			if (netfs_state_trylock_send(st)) {
				netfs_state_exit(st);
				err = netfs_state_init(st);
				if (!err)
					err = -EAGAIN;
				netfs_state_unlock_send(st);
			} else {
				st->need_reset = 1;
			}
		}

		if (kthread_should_stop())
			err = -ENODEV;

		if (err)
			printk("%s: socket: %p, read_socket: %p, revents: %x, rev_error: %d, "
					"should_stop: %d, size: %u, err: %d.\n",
				__func__, st->socket, st->read_socket,
				revents, revents & err_mask, kthread_should_stop(), size, err);
	}

	return err;
}

int pohmelfs_data_recv_and_check(struct netfs_state *st, void *data, unsigned int size)
{
	struct netfs_cmd *cmd = &st->cmd;
	int err;

	err = pohmelfs_data_recv(st, data, size);
	if (err)
		return err;

	return pohmelfs_crypto_process_input_data(&st->eng, cmd->iv, data, NULL, size);
}

/*
 * Polling machinery.
 */

struct netfs_poll_helper {
	poll_table 		pt;
	struct netfs_state	*st;
};

static int netfs_queue_wake(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	struct netfs_state *st = container_of(wait, struct netfs_state, wait);

	wake_up(&st->thread_wait);
	return 1;
}

static void netfs_queue_func(struct file *file, wait_queue_head_t *whead,
				 poll_table *pt)
{
	struct netfs_state *st = container_of(pt, struct netfs_poll_helper, pt)->st;

	st->whead = whead;
	init_waitqueue_func_entry(&st->wait, netfs_queue_wake);
	add_wait_queue(whead, &st->wait);
}

static void netfs_poll_exit(struct netfs_state *st)
{
	if (st->whead) {
		remove_wait_queue(st->whead, &st->wait);
		st->whead = NULL;
	}
}

static int netfs_poll_init(struct netfs_state *st)
{
	struct netfs_poll_helper ph;

	ph.st = st;
	init_poll_funcptr(&ph.pt, &netfs_queue_func);

	st->socket->ops->poll(NULL, st->socket, &ph.pt);
	return 0;
}

/*
 * Get response for readpage command. We search inode and page in its mapping
 * and copy data into. If it was async request, then we queue page into shared
 * data and wakeup listener, who will copy it to userspace.
 *
 * There is a work in progress of allowing to call copy_to_user() directly from
 * async receiving kernel thread.
 */
static int pohmelfs_read_page_response(struct netfs_state *st)
{
	struct pohmelfs_sb *psb = st->psb;
	struct netfs_cmd *cmd = &st->cmd;
	struct inode *inode;
	struct page *page;
	int err = 0;

	if (cmd->size > PAGE_CACHE_SIZE) {
		err = -EINVAL;
		goto err_out_exit;
	}

	inode = ilookup(st->psb->sb, cmd->id);
	if (!inode) {
		printk("%s: failed to find inode: id: %llu.\n", __func__, cmd->id);
		err = -ENOENT;
		goto err_out_exit;
	}

	page = find_get_page(inode->i_mapping, cmd->start >> PAGE_CACHE_SHIFT);
	if (!page || !PageLocked(page)) {
		printk("%s: failed to find/lock page: page: %p, id: %llu, start: %llu, index: %llu.\n",
				__func__, page, cmd->id, cmd->start, cmd->start >> PAGE_CACHE_SHIFT);

		while (cmd->size) {
			unsigned int sz = min(cmd->size, st->size);

			err = pohmelfs_data_recv(st, st->data, sz);
			if (err)
				break;

			cmd->size -= sz;
		}

		err = -ENODEV;
		if (page)
			goto err_out_page_put;
		goto err_out_put;
	}

	if (cmd->size) {
		void *addr;

		addr = kmap(page);
		err = pohmelfs_data_recv(st, addr, cmd->size);
		kunmap(page);

		if (err)
			goto err_out_page_unlock;
	}

	dprintk("%s: page: %p, start: %llu, size: %u, locked: %d.\n",
		__func__, page, cmd->start, cmd->size, PageLocked(page));

	SetPageChecked(page);
	if ((psb->hash_string || psb->cipher_string) && psb->perform_crypto && cmd->size) {
		err = pohmelfs_crypto_process_input_page(&st->eng, page, cmd->size, cmd->iv);
		if (err < 0)
			goto err_out_page_unlock;
	} else {
		SetPageUptodate(page);
		unlock_page(page);
		page_cache_release(page);
	}

	pohmelfs_put_inode(POHMELFS_I(inode));
	wake_up(&st->psb->wait);

	return 0;

err_out_page_unlock:
	SetPageError(page);
	unlock_page(page);
err_out_page_put:
	page_cache_release(page);
err_out_put:
	pohmelfs_put_inode(POHMELFS_I(inode));
err_out_exit:
	wake_up(&st->psb->wait);
	return err;
}

static int pohmelfs_check_name(struct pohmelfs_inode *parent, struct qstr *str,
		struct netfs_inode_info *info)
{
	struct inode *inode;
	struct pohmelfs_name *n;
	int err = 0;
	u64 ino = 0;

	mutex_lock(&parent->offset_lock);
	n = pohmelfs_search_hash(parent, str->hash);
	if (n)
		ino = n->ino;
	mutex_unlock(&parent->offset_lock);

	if (!ino)
		goto out;

	inode = ilookup(parent->vfs_inode.i_sb, ino);
	if (!inode)
		goto out;

	dprintk("%s: parent: %llu, inode: %llu.\n", __func__, parent->ino, ino);

	pohmelfs_fill_inode(inode, info);
	pohmelfs_put_inode(POHMELFS_I(inode));
	err = -EEXIST;
out:
	return err;
}

/*
 * Readdir response from server. If special field is set, we wakeup
 * listener (readdir() call), which will copy data to userspace.
 */
static int pohmelfs_readdir_response(struct netfs_state *st)
{
	struct inode *inode;
	struct netfs_cmd *cmd = &st->cmd;
	struct netfs_inode_info *info;
	struct pohmelfs_inode *parent = NULL, *npi;
	int err = 0, last = cmd->ext;
	struct qstr str;

	if (cmd->size > st->size)
		return -EINVAL;

	inode = ilookup(st->psb->sb, cmd->id);
	if (!inode) {
		printk("%s: failed to find inode: id: %llu.\n", __func__, cmd->id);
		return -ENOENT;
	}
	parent = POHMELFS_I(inode);

	if (!cmd->size && cmd->start) {
		err = -cmd->start;
		goto out;
	}

	if (cmd->size) {
		char *name;

		err = pohmelfs_data_recv_and_check(st, st->data, cmd->size);
		if (err)
			goto err_out_put;

		info = (struct netfs_inode_info *)(st->data);

		name = (char *)(info + 1);
		str.len = cmd->size - sizeof(struct netfs_inode_info) - 1 - cmd->cpad;
		name[str.len] = 0;
		str.name = name;
		str.hash = jhash(str.name, str.len, 0);

		netfs_convert_inode_info(info);

		if (parent) {
			err = pohmelfs_check_name(parent, &str, info);
			if (err) {
				if (err == -EEXIST)
					err = 0;
				goto out;
			}
		}

		info->ino = cmd->start;
		if (!info->ino)
			info->ino = pohmelfs_new_ino(st->psb);

		dprintk("%s: parent: %llu, ino: %llu, name: '%s', hash: %x, len: %u, mode: %o.\n",
				__func__, parent->ino, info->ino, str.name, str.hash, str.len,
				info->mode);

		npi = pohmelfs_new_inode(st->psb, parent, &str, info, 0);
		if (IS_ERR(npi)) {
			err = PTR_ERR(npi);

			if (err != -EEXIST)
				goto err_out_put;
		} else {
			struct dentry *dentry, *alias, *pd;

			set_bit(NETFS_INODE_REMOTE_SYNCED, &npi->state);
			clear_bit(NETFS_INODE_OWNED, &npi->state);

			pd = d_find_alias(&parent->vfs_inode);
			if (pd) {
				str.hash = full_name_hash(str.name, str.len);
				dentry = d_alloc(pd, &str);
				if (dentry) {
					alias = d_materialise_unique(dentry, &npi->vfs_inode);
					if (alias)
						dput(dentry);
				}

				dput(dentry);
				dput(pd);
			}
		}
	}
out:
	if (last) {
		set_bit(NETFS_INODE_REMOTE_DIR_SYNCED, &parent->state);
		set_bit(NETFS_INODE_REMOTE_SYNCED, &parent->state);
		wake_up(&st->psb->wait);
	}
	pohmelfs_put_inode(parent);

	return err;

err_out_put:
	clear_bit(NETFS_INODE_REMOTE_DIR_SYNCED, &parent->state);
	printk("%s: parent: %llu, ino: %llu, cmd_id: %llu.\n", __func__, parent->ino, cmd->start, cmd->id);
	pohmelfs_put_inode(parent);
	wake_up(&st->psb->wait);
	return err;
}

/*
 * Lookup command response.
 * It searches for inode to be looked at (if it exists) and substitutes
 * its inode information (size, permission, mode and so on), if inode does
 * not exist, new one will be created and inserted into caches.
 */
static int pohmelfs_lookup_response(struct netfs_state *st)
{
	struct inode *inode = NULL;
	struct netfs_cmd *cmd = &st->cmd;
	struct netfs_inode_info *info;
	struct pohmelfs_inode *parent = NULL, *npi;
	int err = -EINVAL;
	char *name;

	inode = ilookup(st->psb->sb, cmd->id);
	if (!inode) {
		printk("%s: lookup response: id: %llu, start: %llu, size: %u.\n",
				__func__, cmd->id, cmd->start, cmd->size);
		err = -ENOENT;
		goto err_out_exit;
	}
	parent = POHMELFS_I(inode);

	if (!cmd->size) {
		err = -cmd->start;
		goto err_out_put;
	}

	if (cmd->size < sizeof(struct netfs_inode_info)) {
		printk("%s: broken lookup response: id: %llu, start: %llu, size: %u.\n",
				__func__, cmd->id, cmd->start, cmd->size);
		err = -EINVAL;
		goto err_out_put;
	}

	err = pohmelfs_data_recv_and_check(st, st->data, cmd->size);
	if (err)
		goto err_out_put;

	info = (struct netfs_inode_info *)(st->data);
	name = (char *)(info + 1);

	netfs_convert_inode_info(info);

	info->ino = cmd->start;
	if (!info->ino)
		info->ino = pohmelfs_new_ino(st->psb);

	dprintk("%s: parent: %llu, ino: %llu, name: '%s', start: %llu.\n",
			__func__, parent->ino, info->ino, name, cmd->start);

	if (cmd->start)
		npi = pohmelfs_new_inode(st->psb, parent, NULL, info, 0);
	else {
		struct qstr str;

		str.name = name;
		str.len = cmd->size - sizeof(struct netfs_inode_info) - 1 - cmd->cpad;
		str.hash = jhash(name, str.len, 0);

		npi = pohmelfs_new_inode(st->psb, parent, &str, info, 0);
	}
	if (IS_ERR(npi)) {
		err = PTR_ERR(npi);

		if (err != -EEXIST)
			goto err_out_put;
	} else {
		set_bit(NETFS_INODE_REMOTE_SYNCED, &npi->state);
		clear_bit(NETFS_INODE_OWNED, &npi->state);
	}

	clear_bit(NETFS_COMMAND_PENDING, &parent->state);
	pohmelfs_put_inode(parent);

	wake_up(&st->psb->wait);

	return 0;

err_out_put:
	pohmelfs_put_inode(parent);
err_out_exit:
	clear_bit(NETFS_COMMAND_PENDING, &parent->state);
	wake_up(&st->psb->wait);
	printk("%s: inode: %p, id: %llu, start: %llu, size: %u, err: %d.\n",
			__func__, inode, cmd->id, cmd->start, cmd->size, err);
	return err;
}

/*
 * Create response, just marks local inode as 'created', so that writeback
 * for any of its children (or own) would not try to sync it again.
 */
static int pohmelfs_create_response(struct netfs_state *st)
{
	struct inode *inode;
	struct netfs_cmd *cmd = &st->cmd;
	struct pohmelfs_inode *pi;

	inode = ilookup(st->psb->sb, cmd->id);
	if (!inode) {
		printk("%s: failed to find inode: id: %llu, start: %llu.\n",
				__func__, cmd->id, cmd->start);
		goto err_out_exit;
	}

	pi = POHMELFS_I(inode);

	/*
	 * To lock or not to lock?
	 * We actually do not care if it races...
	 */
	if (cmd->start)
		make_bad_inode(inode);
	set_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state);

	pohmelfs_put_inode(pi);

	wake_up(&st->psb->wait);
	return 0;

err_out_exit:
	wake_up(&st->psb->wait);
	return -ENOENT;
}

/*
 * Object remove response. Just says that remove request has been received.
 * Used in cache coherency protocol.
 */
static int pohmelfs_remove_response(struct netfs_state *st)
{
	struct netfs_cmd *cmd = &st->cmd;
	int err;

	err = pohmelfs_data_recv_and_check(st, st->data, cmd->size);
	if (err)
		return err;

	dprintk("%s: parent: %llu, path: '%s'.\n", __func__, cmd->id, (char *)st->data);

	return 0;
}

/*
 * Transaction reply processing.
 *
 * Find transaction based on its generation number, bump its reference counter,
 * so that none could free it under us, drop from the trees and lists and
 * drop reference counter. When it hits zero (when all destinations replied
 * and all timeout handled by async scanning code), completion will be called
 * and transaction will be freed.
 */
static int pohmelfs_transaction_response(struct netfs_state *st)
{
	struct netfs_trans_dst *dst;
	struct netfs_trans *t = NULL;
	struct netfs_cmd *cmd = &st->cmd;
	short err = (signed)cmd->ext;

	mutex_lock(&st->trans_lock);
	dst = netfs_trans_search(st, cmd->start);
	if (dst) {
		netfs_trans_remove_nolock(dst, st);
		t = dst->trans;
	}
	mutex_unlock(&st->trans_lock);

	if (!t) {
		printk("%s: failed to find transaction: start: %llu: id: %llu, size: %u, ext: %u.\n",
				__func__, cmd->start, cmd->id, cmd->size, cmd->ext);
		err = -EINVAL;
		goto out;
	}

	t->result = err;
	netfs_trans_drop_dst_nostate(dst);

out:
	wake_up(&st->psb->wait);
	return err;
}

/*
 * Inode metadata cache coherency message.
 */
static int pohmelfs_page_cache_response(struct netfs_state *st)
{
	struct netfs_cmd *cmd = &st->cmd;
	struct inode *inode;

	dprintk("%s: st: %p, id: %llu, start: %llu, size: %u.\n", __func__, st, cmd->id, cmd->start, cmd->size);

	inode = ilookup(st->psb->sb, cmd->id);
	if (!inode) {
		printk("%s: failed to find inode: id: %llu.\n", __func__, cmd->id);
		return -ENOENT;
	}

	set_bit(NETFS_INODE_NEED_FLUSH, &POHMELFS_I(inode)->state);
	pohmelfs_put_inode(POHMELFS_I(inode));

	return 0;
}

/*
 * Root capabilities response: export statistics
 * like used and available size, number of files and dirs,
 * permissions.
 */
static int pohmelfs_root_cap_response(struct netfs_state *st)
{
	struct netfs_cmd *cmd = &st->cmd;
	struct netfs_root_capabilities *cap;
	struct pohmelfs_sb *psb = st->psb;

	if (cmd->size != sizeof(struct netfs_root_capabilities)) {
		psb->flags = EPROTO;
		wake_up(&psb->wait);
		return -EPROTO;
	}

	cap = st->data;

	netfs_convert_root_capabilities(cap);

	if (psb->total_size < cap->used + cap->avail)
		psb->total_size = cap->used + cap->avail;
	if (cap->avail)
		psb->avail_size = cap->avail;
	psb->state_flags = cap->flags;

	if (psb->state_flags & POHMELFS_FLAGS_RO) {
		psb->sb->s_flags |= MS_RDONLY;
		printk(KERN_INFO "Mounting POHMELFS (%d) read-only.\n", psb->idx);
	}

	if (psb->state_flags & POHMELFS_FLAGS_XATTR)
		printk(KERN_INFO "Mounting POHMELFS (%d) "
			"with extended attributes support.\n", psb->idx);

	if (atomic_long_read(&psb->total_inodes) <= 1)
		atomic_long_set(&psb->total_inodes, cap->nr_files);

	dprintk("%s: total: %llu, avail: %llu, flags: %llx, inodes: %llu.\n",
		__func__, psb->total_size, psb->avail_size, psb->state_flags, cap->nr_files);

	psb->flags = 0;
	wake_up(&psb->wait);
	return 0;
}

/*
 * Crypto capabilities of the server, where it says that
 * it supports or does not requested hash/cipher algorithms.
 */
static int pohmelfs_crypto_cap_response(struct netfs_state *st)
{
	struct netfs_cmd *cmd = &st->cmd;
	struct netfs_crypto_capabilities *cap;
	struct pohmelfs_sb *psb = st->psb;
	int err = 0;

	if (cmd->size != sizeof(struct netfs_crypto_capabilities)) {
		psb->flags = EPROTO;
		wake_up(&psb->wait);
		return -EPROTO;
	}

	cap = st->data;

	dprintk("%s: cipher '%s': %s, hash: '%s': %s.\n",
			__func__,
			psb->cipher_string, (cap->cipher_strlen)?"SUPPORTED":"NOT SUPPORTED",
			psb->hash_string, (cap->hash_strlen)?"SUPPORTED":"NOT SUPPORTED");

	if (!cap->hash_strlen) {
		if (psb->hash_strlen && psb->crypto_fail_unsupported)
			err = -ENOTSUPP;
		psb->hash_strlen = 0;
		kfree(psb->hash_string);
		psb->hash_string = NULL;
	}

	if (!cap->cipher_strlen) {
		if (psb->cipher_strlen && psb->crypto_fail_unsupported)
			err = -ENOTSUPP;
		psb->cipher_strlen = 0;
		kfree(psb->cipher_string);
		psb->cipher_string = NULL;
	}

	return err;
}

/*
 * Capabilities handshake response.
 */
static int pohmelfs_capabilities_response(struct netfs_state *st)
{
	struct netfs_cmd *cmd = &st->cmd;
	int err = 0;

	err = pohmelfs_data_recv(st, st->data, cmd->size);
	if (err)
		return err;

	switch (cmd->id) {
		case POHMELFS_CRYPTO_CAPABILITIES:
			return pohmelfs_crypto_cap_response(st);
		case POHMELFS_ROOT_CAPABILITIES:
			return pohmelfs_root_cap_response(st);
		default:
			break;
	}
	return -EINVAL;
}

/*
 * Receiving extended attribute.
 * Does not work properly if received size is more than requested one,
 * it should not happen with current request/reply model though.
 */
static int pohmelfs_getxattr_response(struct netfs_state *st)
{
	struct pohmelfs_sb *psb = st->psb;
	struct netfs_cmd *cmd = &st->cmd;
	struct pohmelfs_mcache *m;
	short error = (signed short)cmd->ext, err;
	unsigned int sz, total_size;

	m = pohmelfs_mcache_search(psb, cmd->id);

	dprintk("%s: id: %llu, gen: %llu, err: %d.\n",
		__func__, cmd->id, (m)?m->gen:0, error);

	if (!m) {
		printk("%s: failed to find getxattr cache entry: id: %llu.\n", __func__, cmd->id);
		return -ENOENT;
	}

	if (cmd->size) {
		sz = min_t(unsigned int, cmd->size, m->size);
		err = pohmelfs_data_recv_and_check(st, m->data, sz);
		if (err) {
			error = err;
			goto out;
		}

		m->size = sz;
		total_size = cmd->size - sz;

		while (total_size) {
			sz = min(total_size, st->size);

			err = pohmelfs_data_recv_and_check(st, st->data, sz);
			if (err) {
				error = err;
				break;
			}

			total_size -= sz;
		}
	}

out:
	m->err = error;
	complete(&m->complete);
	pohmelfs_mcache_put(psb, m);

	return error;
}

int pohmelfs_data_lock_response(struct netfs_state *st)
{
	struct pohmelfs_sb *psb = st->psb;
	struct netfs_cmd *cmd = &st->cmd;
	struct pohmelfs_mcache *m;
	short err = (signed short)cmd->ext;
	u64 id = cmd->id;

	m = pohmelfs_mcache_search(psb, id);

	dprintk("%s: id: %llu, gen: %llu, err: %d.\n",
		__func__, cmd->id, (m)?m->gen:0, err);

	if (!m) {
		pohmelfs_data_recv(st, st->data, cmd->size);
		printk("%s: failed to find data lock response: id: %llu.\n", __func__, cmd->id);
		return -ENOENT;
	}

	if (cmd->size)
		err = pohmelfs_data_recv_and_check(st, &m->info, cmd->size);

	m->err = err;
	complete(&m->complete);
	pohmelfs_mcache_put(psb, m);

	return err;
}

static void __inline__ netfs_state_reset(struct netfs_state *st)
{
	netfs_state_lock_send(st);
	netfs_state_exit(st);
	netfs_state_init(st);
	netfs_state_unlock_send(st);
}

/*
 * Main receiving function, called from dedicated kernel thread.
 */
static int pohmelfs_recv(void *data)
{
	int err = -EINTR;
	struct netfs_state *st = data;
	struct netfs_cmd *cmd = &st->cmd;

	while (!kthread_should_stop()) {
		/*
		 * If socket will be reset after this statement, then
		 * pohmelfs_data_recv() will just fail and loop will
		 * start again, so it can be done without any locks.
		 *
		 * st->read_socket is needed to prevents state machine
		 * breaking between this data reading and subsequent one
		 * in protocol specific functions during connection reset.
		 * In case of reset we have to read next command and do
		 * not expect data for old command to magically appear in
		 * new connection.
		 */
		st->read_socket = st->socket;
		err = pohmelfs_data_recv(st, cmd, sizeof(struct netfs_cmd));
		if (err) {
			msleep(1000);
			continue;
		}

		netfs_convert_cmd(cmd);

		dprintk("%s: cmd: %u, id: %llu, start: %llu, size: %u, "
				"ext: %u, csize: %u, cpad: %u.\n",
				__func__, cmd->cmd, cmd->id, cmd->start,
				cmd->size, cmd->ext, cmd->csize, cmd->cpad);

		if (cmd->csize) {
			struct pohmelfs_crypto_engine *e = &st->eng;

			if (unlikely(cmd->csize > e->size/2)) {
				netfs_state_reset(st);
				continue;
			}

			if (e->hash && unlikely(cmd->csize != st->psb->crypto_attached_size)) {
				dprintk("%s: cmd: cmd: %u, id: %llu, start: %llu, size: %u, "
						"csize: %u != digest size %u.\n",
						__func__, cmd->cmd, cmd->id, cmd->start, cmd->size,
						cmd->csize, st->psb->crypto_attached_size);
				netfs_state_reset(st);
				continue;
			}

			err = pohmelfs_data_recv(st, e->data, cmd->csize);
			if (err) {
				netfs_state_reset(st);
				continue;
			}

#ifdef CONFIG_POHMELFS_DEBUG
			{
				unsigned int i;
				unsigned char *hash = e->data;

				dprintk("%s: received hash: ", __func__);
				for (i=0; i<cmd->csize; ++i)
					printk("%02x ", hash[i]);

				printk("\n");
			}
#endif
			cmd->size -= cmd->csize;
		}

		/*
		 * This should catch protocol breakage and random garbage instead of commands.
		 */
		if (unlikely((cmd->size > st->size) && (cmd->cmd != NETFS_XATTR_GET))) {
			netfs_state_reset(st);
			continue;
		}

		switch (cmd->cmd) {
			case NETFS_READ_PAGE:
				err = pohmelfs_read_page_response(st);
				break;
			case NETFS_READDIR:
				err = pohmelfs_readdir_response(st);
				break;
			case NETFS_LOOKUP:
				err = pohmelfs_lookup_response(st);
				break;
			case NETFS_CREATE:
				err = pohmelfs_create_response(st);
				break;
			case NETFS_REMOVE:
				err = pohmelfs_remove_response(st);
				break;
			case NETFS_TRANS:
				err = pohmelfs_transaction_response(st);
				break;
			case NETFS_PAGE_CACHE:
				err = pohmelfs_page_cache_response(st);
				break;
			case NETFS_CAPABILITIES:
				err = pohmelfs_capabilities_response(st);
				break;
			case NETFS_LOCK:
				err = pohmelfs_data_lock_response(st);
				break;
			case NETFS_XATTR_GET:
				err = pohmelfs_getxattr_response(st);
				break;
			default:
				printk("%s: wrong cmd: %u, id: %llu, start: %llu, size: %u, ext: %u.\n",
					__func__, cmd->cmd, cmd->id, cmd->start, cmd->size, cmd->ext);
				netfs_state_reset(st);
				break;
		}
	}

	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(msecs_to_jiffies(10));

	return err;
}

int netfs_state_init(struct netfs_state *st)
{
	int err;
	struct pohmelfs_ctl *ctl = &st->ctl;

	err = sock_create(ctl->addr.sa_family, ctl->type, ctl->proto, &st->socket);
	if (err) {
		printk("%s: failed to create a socket: family: %d, type: %d, proto: %d, err: %d.\n",
				__func__, ctl->addr.sa_family, ctl->type, ctl->proto, err);
		goto err_out_exit;
	}

	st->socket->sk->sk_allocation = GFP_NOIO;
	st->socket->sk->sk_sndtimeo = st->socket->sk->sk_rcvtimeo = msecs_to_jiffies(60000);

	err = kernel_connect(st->socket, (struct sockaddr *)&ctl->addr, ctl->addrlen, 0);
	if (err) {
		printk("%s: failed to connect to server: idx: %u, err: %d.\n",
				__func__, st->psb->idx, err);
		goto err_out_release;
	}
	st->socket->sk->sk_sndtimeo = st->socket->sk->sk_rcvtimeo = msecs_to_jiffies(60000);

	err = netfs_poll_init(st);
	if (err)
		goto err_out_release;

	if (st->socket->ops->family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&ctl->addr;
		printk(KERN_INFO "%s: (re)connected to peer %pi4:%d.\n", __func__,
			&sin->sin_addr.s_addr, ntohs(sin->sin_port));
	} else if (st->socket->ops->family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)&ctl->addr;
		printk(KERN_INFO "%s: (re)connected to peer %pi6:%d", __func__,
				&sin->sin6_addr, ntohs(sin->sin6_port));
	}

	return 0;

err_out_release:
	sock_release(st->socket);
err_out_exit:
	st->socket = NULL;
	return err;
}

void netfs_state_exit(struct netfs_state *st)
{
	if (st->socket) {
		netfs_poll_exit(st);
		st->socket->ops->shutdown(st->socket, 2);

		if (st->socket->ops->family == AF_INET) {
			struct sockaddr_in *sin = (struct sockaddr_in *)&st->ctl.addr;
			printk(KERN_INFO "%s: disconnected from peer %pi4:%d.\n", __func__,
				&sin->sin_addr.s_addr, ntohs(sin->sin_port));
		} else if (st->socket->ops->family == AF_INET6) {
			struct sockaddr_in6 *sin = (struct sockaddr_in6 *)&st->ctl.addr;
			printk(KERN_INFO "%s: disconnected from peer %pi6:%d", __func__,
				&sin->sin6_addr, ntohs(sin->sin6_port));
		}

		sock_release(st->socket);
		st->socket = NULL;
		st->read_socket = NULL;
		st->need_reset = 0;
	}
}

int pohmelfs_state_init_one(struct pohmelfs_sb *psb, struct pohmelfs_config *conf)
{
	struct netfs_state *st = &conf->state;
	int err = -ENOMEM;

	mutex_init(&st->__state_lock);
	mutex_init(&st->__state_send_lock);
	init_waitqueue_head(&st->thread_wait);

	st->psb = psb;
	st->trans_root = RB_ROOT;
	mutex_init(&st->trans_lock);

	st->size = psb->trans_data_size;
	st->data = kmalloc(st->size, GFP_KERNEL);
	if (!st->data)
		goto err_out_exit;

	if (psb->perform_crypto) {
		err = pohmelfs_crypto_engine_init(&st->eng, psb);
		if (err)
			goto err_out_free_data;
	}

	err = netfs_state_init(st);
	if (err)
		goto err_out_free_engine;

	st->thread = kthread_run(pohmelfs_recv, st, "pohmelfs/%u", psb->idx);
	if (IS_ERR(st->thread)) {
		err = PTR_ERR(st->thread);
		goto err_out_netfs_exit;
	}

	if (!psb->active_state)
		psb->active_state = conf;

	dprintk("%s: conf: %p, st: %p, socket: %p.\n",
			__func__, conf, st, st->socket);
	return 0;

err_out_netfs_exit:
	netfs_state_exit(st);
err_out_free_engine:
	pohmelfs_crypto_engine_exit(&st->eng);
err_out_free_data:
	kfree(st->data);
err_out_exit:
	return err;

}

void pohmelfs_state_flush_transactions(struct netfs_state *st)
{
	struct rb_node *rb_node;
	struct netfs_trans_dst *dst;

	mutex_lock(&st->trans_lock);
	for (rb_node = rb_first(&st->trans_root); rb_node; ) {
		dst = rb_entry(rb_node, struct netfs_trans_dst, state_entry);
		rb_node = rb_next(rb_node);

		dst->trans->result = -EINVAL;
		netfs_trans_remove_nolock(dst, st);
		netfs_trans_drop_dst_nostate(dst);
	}
	mutex_unlock(&st->trans_lock);
}

static void pohmelfs_state_exit_one(struct pohmelfs_config *c)
{
	struct netfs_state *st = &c->state;

	dprintk("%s: exiting, st: %p.\n", __func__, st);
	if (st->thread) {
		kthread_stop(st->thread);
		st->thread = NULL;
	}

	netfs_state_lock_send(st);
	netfs_state_exit(st);
	netfs_state_unlock_send(st);

	pohmelfs_state_flush_transactions(st);

	pohmelfs_crypto_engine_exit(&st->eng);
	kfree(st->data);

	kfree(c);
}

/*
 * Initialize network stack. It searches for given ID in global
 * configuration table, this contains information of the remote server
 * (address (any supported by socket interface) and port, protocol and so on).
 */
int pohmelfs_state_init(struct pohmelfs_sb *psb)
{
	int err = -ENOMEM;

	err = pohmelfs_copy_config(psb);
	if (err) {
		pohmelfs_state_exit(psb);
		return err;
	}

	return 0;
}

void pohmelfs_state_exit(struct pohmelfs_sb *psb)
{
	struct pohmelfs_config *c, *tmp;

	list_for_each_entry_safe(c, tmp, &psb->state_list, config_entry) {
		list_del(&c->config_entry);
		pohmelfs_state_exit_one(c);
	}
}

void pohmelfs_switch_active(struct pohmelfs_sb *psb)
{
	struct pohmelfs_config *c = psb->active_state;

	if (!list_empty(&psb->state_list)) {
		if (c->config_entry.next != &psb->state_list) {
			psb->active_state = list_entry(c->config_entry.next,
				struct pohmelfs_config, config_entry);
		} else {
			psb->active_state = list_entry(psb->state_list.next,
				struct pohmelfs_config, config_entry);
		}

		dprintk("%s: empty: %d, active %p -> %p.\n",
			__func__, list_empty(&psb->state_list), c,
			psb->active_state);
	} else
		psb->active_state = NULL;
}

void pohmelfs_check_states(struct pohmelfs_sb *psb)
{
	struct pohmelfs_config *c, *tmp;
	LIST_HEAD(delete_list);

	mutex_lock(&psb->state_lock);
	list_for_each_entry_safe(c, tmp, &psb->state_list, config_entry) {
		if (pohmelfs_config_check(c, psb->idx)) {

			if (psb->active_state == c)
				pohmelfs_switch_active(psb);
			list_move(&c->config_entry, &delete_list);
		}
	}
	pohmelfs_copy_config(psb);
	mutex_unlock(&psb->state_lock);

	list_for_each_entry_safe(c, tmp, &delete_list, config_entry) {
		list_del(&c->config_entry);
		pohmelfs_state_exit_one(c);
	}
}
