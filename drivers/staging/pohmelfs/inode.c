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
#include <linux/backing-dev.h>
#include <linux/crypto.h>
#include <linux/fs.h>
#include <linux/jhash.h>
#include <linux/hash.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/parser.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/writeback.h>
#include <linux/quotaops.h>

#include "netfs.h"

#define POHMELFS_MAGIC_NUM	0x504f482e

static struct kmem_cache *pohmelfs_inode_cache;

/*
 * Removes inode from all trees, drops local name cache and removes all queued
 * requests for object removal.
 */
void pohmelfs_inode_del_inode(struct pohmelfs_sb *psb, struct pohmelfs_inode *pi)
{
	mutex_lock(&pi->offset_lock);
	pohmelfs_free_names(pi);
	mutex_unlock(&pi->offset_lock);

	dprintk("%s: deleted stuff in ino: %llu.\n", __func__, pi->ino);
}

/*
 * Sync inode to server.
 * Returns zero in success and negative error value otherwise.
 * It will gather path to root directory into structures containing
 * creation mode, permissions and names, so that the whole path
 * to given inode could be created using only single network command.
 */
int pohmelfs_write_inode_create(struct inode *inode, struct netfs_trans *trans)
{
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	int err = -ENOMEM, size;
	struct netfs_cmd *cmd;
	void *data;
	int cur_len = netfs_trans_cur_len(trans);

	if (unlikely(cur_len < 0))
		return -ETOOSMALL;

	cmd = netfs_trans_current(trans);
	cur_len -= sizeof(struct netfs_cmd);

	data = (void *)(cmd + 1);

	err = pohmelfs_construct_path_string(pi, data, cur_len);
	if (err < 0)
		goto err_out_exit;

	size = err;

	cmd->start = i_size_read(inode);
	cmd->cmd = NETFS_CREATE;
	cmd->size = size;
	cmd->id = pi->ino;
	cmd->ext = inode->i_mode;

	netfs_convert_cmd(cmd);

	netfs_trans_update(cmd, trans, size);

	return 0;

err_out_exit:
	printk("%s: completed ino: %llu, err: %d.\n", __func__, pi->ino, err);
	return err;
}

static int pohmelfs_write_trans_complete(struct page **pages, unsigned int page_num,
		void *private, int err)
{
	unsigned i;

	dprintk("%s: pages: %lu-%lu, page_num: %u, err: %d.\n",
			__func__, pages[0]->index, pages[page_num-1]->index,
			page_num, err);

	for (i = 0; i < page_num; i++) {
		struct page *page = pages[i];

		if (!page)
			continue;

		end_page_writeback(page);

		if (err < 0) {
			SetPageError(page);
			set_page_dirty(page);
		}

		unlock_page(page);
		page_cache_release(page);

		/* dprintk("%s: %3u/%u: page: %p.\n", __func__, i, page_num, page); */
	}
	return err;
}

static int pohmelfs_inode_has_dirty_pages(struct address_space *mapping, pgoff_t index)
{
	int ret;
	struct page *page;

	rcu_read_lock();
	ret = radix_tree_gang_lookup_tag(&mapping->page_tree,
				(void **)&page, index, 1, PAGECACHE_TAG_DIRTY);
	rcu_read_unlock();
	return ret;
}

static int pohmelfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	int err = 0;
	int done = 0;
	int nr_pages;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	int scanned = 0;
	int range_whole = 0;

	if (wbc->nonblocking && bdi_write_congested(bdi)) {
		wbc->encountered_congestion = 1;
		return 0;
	}

	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		scanned = 1;
	}
retry:
	while (!done && (index <= end)) {
		unsigned int i = min(end - index, (pgoff_t)psb->trans_max_pages);
		int path_len;
		struct netfs_trans *trans;

		err = pohmelfs_inode_has_dirty_pages(mapping, index);
		if (!err)
			break;

		err = pohmelfs_path_length(pi);
		if (err < 0)
			break;

		path_len = err;

		if (path_len <= 2) {
			err = -ENOENT;
			break;
		}

		trans = netfs_trans_alloc(psb, path_len, 0, i);
		if (!trans) {
			err = -ENOMEM;
			break;
		}
		trans->complete = &pohmelfs_write_trans_complete;

		trans->page_num = nr_pages = find_get_pages_tag(mapping, &index,
				PAGECACHE_TAG_DIRTY, trans->page_num,
				trans->pages);

		dprintk("%s: t: %p, nr_pages: %u, end: %lu, index: %lu, max: %u.\n",
				__func__, trans, nr_pages, end, index, trans->page_num);

		if (!nr_pages)
			goto err_out_reset;

		err = pohmelfs_write_inode_create(inode, trans);
		if (err)
			goto err_out_reset;

		err = 0;
		scanned = 1;

		for (i = 0; i < trans->page_num; i++) {
			struct page *page = trans->pages[i];

			lock_page(page);

			if (unlikely(page->mapping != mapping))
				goto out_continue;

			if (!wbc->range_cyclic && page->index > end) {
				done = 1;
				goto out_continue;
			}

			if (wbc->sync_mode != WB_SYNC_NONE)
				wait_on_page_writeback(page);

			if (PageWriteback(page) ||
			    !clear_page_dirty_for_io(page)) {
				dprintk("%s: not clear for io page: %p, writeback: %d.\n",
						__func__, page, PageWriteback(page));
				goto out_continue;
			}

			set_page_writeback(page);

			trans->attached_size += page_private(page);
			trans->attached_pages++;
#if 0
			dprintk("%s: %u/%u added trans: %p, gen: %u, page: %p, [High: %d], size: %lu, idx: %lu.\n",
					__func__, i, trans->page_num, trans, trans->gen, page,
					!!PageHighMem(page), page_private(page), page->index);
#endif
			wbc->nr_to_write--;

			if (wbc->nr_to_write <= 0)
				done = 1;
			if (wbc->nonblocking && bdi_write_congested(bdi)) {
				wbc->encountered_congestion = 1;
				done = 1;
			}

			continue;
out_continue:
			unlock_page(page);
			trans->pages[i] = NULL;
		}

		err = netfs_trans_finish(trans, psb);
		if (err)
			break;

		continue;

err_out_reset:
		trans->result = err;
		netfs_trans_reset(trans);
		netfs_trans_put(trans);
		break;
	}

	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;
		goto retry;
	}

	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = index;

	return err;
}

/*
 * Inode writeback creation completion callback.
 * Only invoked for just created inodes, which do not have pages attached,
 * like dirs and empty files.
 */
static int pohmelfs_write_inode_complete(struct page **pages, unsigned int page_num,
		void *private, int err)
{
	struct inode *inode = private;
	struct pohmelfs_inode *pi = POHMELFS_I(inode);

	if (inode) {
		if (err) {
			mark_inode_dirty(inode);
			clear_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state);
		} else {
			set_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state);
		}

		pohmelfs_put_inode(pi);
	}

	return err;
}

int pohmelfs_write_create_inode(struct pohmelfs_inode *pi)
{
	struct netfs_trans *t;
	struct inode *inode = &pi->vfs_inode;
	struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);
	int err;

	if (test_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state))
		return 0;

	dprintk("%s: started ino: %llu.\n", __func__, pi->ino);

	err = pohmelfs_path_length(pi);
	if (err < 0)
		goto err_out_exit;

	t = netfs_trans_alloc(psb, err + 1, 0, 0);
	if (!t) {
		err = -ENOMEM;
		goto err_out_put;
	}
	t->complete = pohmelfs_write_inode_complete;
	t->private = igrab(inode);
	if (!t->private) {
		err = -ENOENT;
		goto err_out_put;
	}

	err = pohmelfs_write_inode_create(inode, t);
	if (err)
		goto err_out_put;

	netfs_trans_finish(t, POHMELFS_SB(inode->i_sb));

	return 0;

err_out_put:
	t->result = err;
	netfs_trans_put(t);
err_out_exit:
	return err;
}

/*
 * Sync all not-yet-created children in given directory to the server.
 */
static int pohmelfs_write_inode_create_children(struct inode *inode)
{
	struct pohmelfs_inode *parent = POHMELFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct pohmelfs_name *n;

	while (!list_empty(&parent->sync_create_list)) {
		n = NULL;
		mutex_lock(&parent->offset_lock);
		if (!list_empty(&parent->sync_create_list)) {
			n = list_first_entry(&parent->sync_create_list,
				struct pohmelfs_name, sync_create_entry);
			list_del_init(&n->sync_create_entry);
		}
		mutex_unlock(&parent->offset_lock);

		if (!n)
			break;

		inode = ilookup(sb, n->ino);

		dprintk("%s: parent: %llu, ino: %llu, inode: %p.\n",
				__func__, parent->ino, n->ino, inode);

		if (inode && (inode->i_state & I_DIRTY)) {
			struct pohmelfs_inode *pi = POHMELFS_I(inode);
			pohmelfs_write_create_inode(pi);
			/* pohmelfs_meta_command(pi, NETFS_INODE_INFO, 0, NULL, NULL, 0); */
			iput(inode);
		}
	}

	return 0;
}

/*
 * Removes given child from given inode on server.
 */
int pohmelfs_remove_child(struct pohmelfs_inode *pi, struct pohmelfs_name *n)
{
	return pohmelfs_meta_command_data(pi, pi->ino, NETFS_REMOVE, NULL, 0, NULL, NULL, 0);
}

/*
 * Writeback for given inode.
 */
static int pohmelfs_write_inode(struct inode *inode, int sync)
{
	struct pohmelfs_inode *pi = POHMELFS_I(inode);

	pohmelfs_write_create_inode(pi);
	pohmelfs_write_inode_create_children(inode);

	return 0;
}

/*
 * It is not exported, sorry...
 */
static inline wait_queue_head_t *page_waitqueue(struct page *page)
{
	const struct zone *zone = page_zone(page);

	return &zone->wait_table[hash_ptr(page, zone->wait_table_bits)];
}

static int pohmelfs_wait_on_page_locked(struct page *page)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(page->mapping->host->i_sb);
	long ret = psb->wait_on_page_timeout;
	DEFINE_WAIT_BIT(wait, &page->flags, PG_locked);
	int err = 0;

	if (!PageLocked(page))
		return 0;

	for (;;) {
		prepare_to_wait(page_waitqueue(page),
				&wait.wait, TASK_INTERRUPTIBLE);

		dprintk("%s: page: %p, locked: %d, uptodate: %d, error: %d, flags: %lx.\n",
				__func__, page, PageLocked(page), PageUptodate(page),
				PageError(page), page->flags);

		if (!PageLocked(page))
			break;

		if (!signal_pending(current)) {
			ret = schedule_timeout(ret);
			if (!ret)
				break;
			continue;
		}
		ret = -ERESTARTSYS;
		break;
	}
	finish_wait(page_waitqueue(page), &wait.wait);

	if (!ret)
		err = -ETIMEDOUT;


	if (!err)
		SetPageUptodate(page);

	if (err)
		printk("%s: page: %p, uptodate: %d, locked: %d, err: %d.\n",
			__func__, page, PageUptodate(page), PageLocked(page), err);

	return err;
}

static int pohmelfs_read_page_complete(struct page **pages, unsigned int page_num,
		void *private, int err)
{
	struct page *page = private;

	if (PageChecked(page))
		return err;

	if (err < 0) {
		dprintk("%s: page: %p, err: %d.\n", __func__, page, err);
		SetPageError(page);
	}

	unlock_page(page);

	return err;
}

/*
 * Read a page from remote server.
 * Function will wait until page is unlocked.
 */
static int pohmelfs_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	struct netfs_trans *t;
	struct netfs_cmd *cmd;
	int err, path_len;
	void *data;
	u64 isize;

	err = pohmelfs_data_lock(pi, page->index << PAGE_CACHE_SHIFT,
			PAGE_SIZE, POHMELFS_READ_LOCK);
	if (err)
		goto err_out_exit;

	isize = i_size_read(inode);
	if (isize <= page->index << PAGE_CACHE_SHIFT) {
		SetPageUptodate(page);
		unlock_page(page);
		return 0;
	}

	path_len = pohmelfs_path_length(pi);
	if (path_len < 0) {
		err = path_len;
		goto err_out_exit;
	}

	t = netfs_trans_alloc(psb, path_len, NETFS_TRANS_SINGLE_DST, 0);
	if (!t) {
		err = -ENOMEM;
		goto err_out_exit;
	}

	t->complete = pohmelfs_read_page_complete;
	t->private = page;

	cmd = netfs_trans_current(t);
	data = (void *)(cmd + 1);

	err = pohmelfs_construct_path_string(pi, data, path_len);
	if (err < 0)
		goto err_out_free;

	path_len = err;

	cmd->id = pi->ino;
	cmd->start = page->index;
	cmd->start <<= PAGE_CACHE_SHIFT;
	cmd->size = PAGE_CACHE_SIZE + path_len;
	cmd->cmd = NETFS_READ_PAGE;
	cmd->ext = path_len;

	dprintk("%s: path: '%s', page: %p, ino: %llu, start: %llu, size: %lu.\n",
			__func__, (char *)data, page, pi->ino, cmd->start, PAGE_CACHE_SIZE);

	netfs_convert_cmd(cmd);
	netfs_trans_update(cmd, t, path_len);

	err = netfs_trans_finish(t, psb);
	if (err)
		goto err_out_return;

	return pohmelfs_wait_on_page_locked(page);

err_out_free:
	t->result = err;
	netfs_trans_put(t);
err_out_exit:
	SetPageError(page);
	if (PageLocked(page))
		unlock_page(page);
err_out_return:
	printk("%s: page: %p, start: %lu, size: %lu, err: %d.\n",
		__func__, page, page->index << PAGE_CACHE_SHIFT, PAGE_CACHE_SIZE, err);

	return err;
}

/*
 * Write begin/end magic.
 * Allocates a page and writes inode if it was not synced to server before.
 */
static int pohmelfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct page *page;
	pgoff_t index;
	unsigned start, end;
	int err;

	*pagep = NULL;

	index = pos >> PAGE_CACHE_SHIFT;
	start = pos & (PAGE_CACHE_SIZE - 1);
	end = start + len;

	page = grab_cache_page(mapping, index);
#if 0
	dprintk("%s: page: %p pos: %llu, len: %u, index: %lu, start: %u, end: %u, uptodate: %d.\n",
			__func__, page,	pos, len, index, start, end, PageUptodate(page));
#endif
	if (!page) {
		err = -ENOMEM;
		goto err_out_exit;
	}

	while (!PageUptodate(page)) {
		if (start && test_bit(NETFS_INODE_REMOTE_SYNCED, &POHMELFS_I(inode)->state)) {
			err = pohmelfs_readpage(file, page);
			if (err)
				goto err_out_exit;

			lock_page(page);
			continue;
		}

		if (len != PAGE_CACHE_SIZE) {
			void *kaddr = kmap_atomic(page, KM_USER0);

			memset(kaddr + start, 0, PAGE_CACHE_SIZE - start);
			flush_dcache_page(page);
			kunmap_atomic(kaddr, KM_USER0);
		}
		SetPageUptodate(page);
	}

	set_page_private(page, end);

	*pagep = page;

	return 0;

err_out_exit:
	page_cache_release(page);
	*pagep = NULL;

	return err;
}

static int pohmelfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;

	if (copied != len) {
		unsigned from = pos & (PAGE_CACHE_SIZE - 1);
		void *kaddr = kmap_atomic(page, KM_USER0);

		memset(kaddr + from + copied, 0, len - copied);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
	}

	SetPageUptodate(page);
	set_page_dirty(page);
#if 0
	dprintk("%s: page: %p [U: %d, D: %d, L: %d], pos: %llu, len: %u, copied: %u.\n",
			__func__, page,
			PageUptodate(page), PageDirty(page), PageLocked(page),
			pos, len, copied);
#endif
	flush_dcache_page(page);

	unlock_page(page);
	page_cache_release(page);

	if (pos + copied > inode->i_size) {
		struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);

		psb->avail_size -= pos + copied - inode->i_size;

		i_size_write(inode, pos + copied);
	}

	return copied;
}

static int pohmelfs_readpages_trans_complete(struct page **__pages, unsigned int page_num,
		void *private, int err)
{
	struct pohmelfs_inode *pi = private;
	unsigned int i, num;
	struct page **pages, *page = (struct page *)__pages;
	loff_t index = page->index;

	pages = kzalloc(sizeof(void *) * page_num, GFP_NOIO);
	if (!pages)
		return -ENOMEM;

	num = find_get_pages_contig(pi->vfs_inode.i_mapping, index, page_num, pages);
	if (num <= 0) {
		err = num;
		goto err_out_free;
	}

	for (i=0; i<num; ++i) {
		page = pages[i];

		if (err)
			printk("%s: %u/%u: page: %p, index: %lu, uptodate: %d, locked: %d, err: %d.\n",
				__func__, i, num, page, page->index,
				PageUptodate(page), PageLocked(page), err);

		if (!PageChecked(page)) {
			if (err < 0)
				SetPageError(page);
			unlock_page(page);
		}
		page_cache_release(page);
		page_cache_release(page);
	}

err_out_free:
	kfree(pages);
	return err;
}

static int pohmelfs_send_readpages(struct pohmelfs_inode *pi, struct page *first, unsigned int num)
{
	struct netfs_trans *t;
	struct netfs_cmd *cmd;
	struct pohmelfs_sb *psb = POHMELFS_SB(pi->vfs_inode.i_sb);
	int err, path_len;
	void *data;

	err = pohmelfs_data_lock(pi, first->index << PAGE_CACHE_SHIFT,
			num * PAGE_SIZE, POHMELFS_READ_LOCK);
	if (err)
		goto err_out_exit;

	path_len = pohmelfs_path_length(pi);
	if (path_len < 0) {
		err = path_len;
		goto err_out_exit;
	}

	t = netfs_trans_alloc(psb, path_len, NETFS_TRANS_SINGLE_DST, 0);
	if (!t) {
		err = -ENOMEM;
		goto err_out_exit;
	}

	cmd = netfs_trans_current(t);
	data = (void *)(cmd + 1);

	t->complete = pohmelfs_readpages_trans_complete;
	t->private = pi;
	t->page_num = num;
	t->pages = (struct page **)first;

	err = pohmelfs_construct_path_string(pi, data, path_len);
	if (err < 0)
		goto err_out_put;

	path_len = err;

	cmd->cmd = NETFS_READ_PAGES;
	cmd->start = first->index;
	cmd->start <<= PAGE_CACHE_SHIFT;
	cmd->size = (num << 8 | PAGE_CACHE_SHIFT);
	cmd->id = pi->ino;
	cmd->ext = path_len;

	dprintk("%s: t: %p, gen: %u, path: '%s', path_len: %u, "
			"start: %lu, num: %u.\n",
			__func__, t, t->gen, (char *)data, path_len,
			first->index, num);

	netfs_convert_cmd(cmd);
	netfs_trans_update(cmd, t, path_len);

	return netfs_trans_finish(t, psb);

err_out_put:
	netfs_trans_free(t);
err_out_exit:
	pohmelfs_readpages_trans_complete((struct page **)first, num, pi, err);
	return err;
}

#define list_to_page(head) (list_entry((head)->prev, struct page, lru))

static int pohmelfs_readpages(struct file *file, struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages)
{
	unsigned int page_idx, num = 0;
	struct page *page = NULL, *first = NULL;

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		page = list_to_page(pages);

		prefetchw(&page->flags);
		list_del(&page->lru);

		if (!add_to_page_cache_lru(page, mapping,
					page->index, GFP_KERNEL)) {

			if (!num) {
				num = 1;
				first = page;
				continue;
			}

			dprintk("%s: added to lru page: %p, page_index: %lu, first_index: %lu.\n",
					__func__, page, page->index, first->index);

			if (unlikely(first->index + num != page->index) || (num > 500)) {
				pohmelfs_send_readpages(POHMELFS_I(mapping->host),
						first, num);
				first = page;
				num = 0;
			}

			num++;
		}
	}
	pohmelfs_send_readpages(POHMELFS_I(mapping->host), first, num);

	/*
	 * This will be sync read, so when last page is processed,
	 * all previous are alerady unlocked and ready to be used.
	 */
	return 0;
}

/*
 * Small addres space operations for POHMELFS.
 */
const struct address_space_operations pohmelfs_aops = {
	.readpage		= pohmelfs_readpage,
	.readpages		= pohmelfs_readpages,
	.writepages		= pohmelfs_writepages,
	.write_begin		= pohmelfs_write_begin,
	.write_end		= pohmelfs_write_end,
	.set_page_dirty 	= __set_page_dirty_nobuffers,
};

/*
 * ->detroy_inode() callback. Deletes inode from the caches
 *  and frees private data.
 */
static void pohmelfs_destroy_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct pohmelfs_sb *psb = POHMELFS_SB(sb);
	struct pohmelfs_inode *pi = POHMELFS_I(inode);

	/* pohmelfs_data_unlock(pi, 0, inode->i_size, POHMELFS_READ_LOCK); */

	pohmelfs_inode_del_inode(psb, pi);

	dprintk("%s: pi: %p, inode: %p, ino: %llu.\n",
		__func__, pi, &pi->vfs_inode, pi->ino);
	kmem_cache_free(pohmelfs_inode_cache, pi);
	atomic_long_dec(&psb->total_inodes);
}

/*
 * ->alloc_inode() callback. Allocates inode and initilizes private data.
 */
static struct inode *pohmelfs_alloc_inode(struct super_block *sb)
{
	struct pohmelfs_inode *pi;

	pi = kmem_cache_alloc(pohmelfs_inode_cache, GFP_NOIO);
	if (!pi)
		return NULL;

	pi->hash_root = RB_ROOT;
	mutex_init(&pi->offset_lock);

	INIT_LIST_HEAD(&pi->sync_create_list);

	INIT_LIST_HEAD(&pi->inode_entry);

	pi->lock_type = 0;
	pi->state = 0;
	pi->total_len = 0;
	pi->drop_count = 0;

	dprintk("%s: pi: %p, inode: %p.\n", __func__, pi, &pi->vfs_inode);

	atomic_long_inc(&POHMELFS_SB(sb)->total_inodes);

	return &pi->vfs_inode;
}

/*
 * We want fsync() to work on POHMELFS.
 */
static int pohmelfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct inode *inode = file->f_mapping->host;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,	/* sys_fsync did this */
	};

	return sync_inode(inode, &wbc);
}

ssize_t pohmelfs_write(struct file *file, const char __user *buf,
		size_t len, loff_t *ppos)
{
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
	struct kiocb kiocb;
	ssize_t ret;
	loff_t pos = *ppos;

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos;
	kiocb.ki_left = len;

	dprintk("%s: len: %zu, pos: %llu.\n", __func__, len, pos);

	mutex_lock(&inode->i_mutex);
	ret = pohmelfs_data_lock(pi, pos, len, POHMELFS_WRITE_LOCK);
	if (ret)
		goto err_out_unlock;

	ret = __generic_file_aio_write(&kiocb, &iov, 1, &kiocb.ki_pos);
	*ppos = kiocb.ki_pos;

	mutex_unlock(&inode->i_mutex);
	WARN_ON(ret < 0);

	if (ret > 0) {
		ssize_t err;

		err = generic_write_sync(file, pos, ret);
		if (err < 0)
			ret = err;
		WARN_ON(ret < 0);
	}

	return ret;

err_out_unlock:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

static const struct file_operations pohmelfs_file_ops = {
	.open		= generic_file_open,
	.fsync		= pohmelfs_fsync,

	.llseek		= generic_file_llseek,

	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,

	.mmap		= generic_file_mmap,

	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,

	.write		= pohmelfs_write,
	.aio_write	= generic_file_aio_write,
};

const struct inode_operations pohmelfs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
};

int pohmelfs_setattr_raw(struct inode *inode, struct iattr *attr)
{
	int err;

	err = inode_change_ok(inode, attr);
	if (err) {
		dprintk("%s: ino: %llu, inode changes are not allowed.\n", __func__, POHMELFS_I(inode)->ino);
		goto err_out_exit;
	}

	if ((attr->ia_valid & ATTR_UID && attr->ia_uid != inode->i_uid) ||
	    (attr->ia_valid & ATTR_GID && attr->ia_gid != inode->i_gid)) {
		err = vfs_dq_transfer(inode, attr) ? -EDQUOT : 0;
		if (err)
			goto err_out_exit;
	}

	err = inode_setattr(inode, attr);
	if (err) {
		dprintk("%s: ino: %llu, failed to set the attributes.\n", __func__, POHMELFS_I(inode)->ino);
		goto err_out_exit;
	}

	dprintk("%s: ino: %llu, mode: %o -> %o, uid: %u -> %u, gid: %u -> %u, size: %llu -> %llu.\n",
			__func__, POHMELFS_I(inode)->ino, inode->i_mode, attr->ia_mode,
			inode->i_uid, attr->ia_uid, inode->i_gid, attr->ia_gid, inode->i_size, attr->ia_size);

	return 0;

err_out_exit:
	return err;
}

int pohmelfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	int err;

	err = pohmelfs_data_lock(pi, 0, ~0, POHMELFS_WRITE_LOCK);
	if (err)
		goto err_out_exit;

	err = security_inode_setattr(dentry, attr);
	if (err)
		goto err_out_exit;

	err = pohmelfs_setattr_raw(inode, attr);
	if (err)
		goto err_out_exit;

	return 0;

err_out_exit:
	return err;
}

static int pohmelfs_send_xattr_req(struct pohmelfs_inode *pi, u64 id, u64 start,
		const char *name, const void *value, size_t attrsize, int command)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(pi->vfs_inode.i_sb);
	int err, path_len, namelen = strlen(name) + 1; /* 0-byte */
	struct netfs_trans *t;
	struct netfs_cmd *cmd;
	void *data;

	dprintk("%s: id: %llu, start: %llu, name: '%s', attrsize: %zu, cmd: %d.\n",
			__func__, id, start, name, attrsize, command);

	path_len = pohmelfs_path_length(pi);
	if (path_len < 0) {
		err = path_len;
		goto err_out_exit;
	}

	t = netfs_trans_alloc(psb, namelen + path_len + attrsize, 0, 0);
	if (!t) {
		err = -ENOMEM;
		goto err_out_exit;
	}

	cmd = netfs_trans_current(t);
	data = cmd + 1;

	path_len = pohmelfs_construct_path_string(pi, data, path_len);
	if (path_len < 0) {
		err = path_len;
		goto err_out_put;
	}
	data += path_len;

	/*
	 * 'name' is a NUL-terminated string already and
	 * 'namelen' includes 0-byte.
	 */
	memcpy(data, name, namelen);
	data += namelen;

	memcpy(data, value, attrsize);

	cmd->cmd = command;
	cmd->id = id;
	cmd->start = start;
	cmd->size = attrsize + namelen + path_len;
	cmd->ext = path_len;
	cmd->csize = 0;
	cmd->cpad = 0;

	netfs_convert_cmd(cmd);
	netfs_trans_update(cmd, t, namelen + path_len + attrsize);

	return netfs_trans_finish(t, psb);

err_out_put:
	t->result = err;
	netfs_trans_put(t);
err_out_exit:
	return err;
}

static int pohmelfs_setxattr(struct dentry *dentry, const char *name,
		const void *value, size_t attrsize, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);

	if (!(psb->state_flags & POHMELFS_FLAGS_XATTR))
		return -EOPNOTSUPP;

	return pohmelfs_send_xattr_req(pi, flags, attrsize, name,
			value, attrsize, NETFS_XATTR_SET);
}

static ssize_t pohmelfs_getxattr(struct dentry *dentry, const char *name,
		void *value, size_t attrsize)
{
	struct inode *inode = dentry->d_inode;
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);
	struct pohmelfs_mcache *m;
	int err;
	long timeout = psb->mcache_timeout;

	if (!(psb->state_flags & POHMELFS_FLAGS_XATTR))
		return -EOPNOTSUPP;

	m = pohmelfs_mcache_alloc(psb, 0, attrsize, value);
	if (IS_ERR(m))
		return PTR_ERR(m);

	dprintk("%s: ino: %llu, name: '%s', size: %zu.\n",
			__func__, pi->ino, name, attrsize);

	err = pohmelfs_send_xattr_req(pi, m->gen, attrsize, name, value, 0, NETFS_XATTR_GET);
	if (err)
		goto err_out_put;

	do {
		err = wait_for_completion_timeout(&m->complete, timeout);
		if (err) {
			err = m->err;
			break;
		}

		/*
		 * This loop is a bit ugly, since it waits until reference counter
		 * hits 1 and then put object here. Main goal is to prevent race with
		 * network thread, when it can start processing given request, i.e.
		 * increase its reference counter but yet not complete it, while
		 * we will exit from ->getxattr() with timeout, and although request
		 * will not be freed (its reference counter was increased by network
		 * thread), data pointer provided by user may be released, so we will
		 * overwrite already freed area in network thread.
		 *
		 * Now after timeout we remove request from the cache, so it can not be
		 * found by network thread, and wait for its reference counter to hit 1,
		 * i.e. if network thread already started to process this request, we wait
		 * it to finish, and then free object locally. If reference counter is
		 * already 1, i.e. request is not used by anyone else, we can free it without
		 * problem.
		 */
		err = -ETIMEDOUT;
		timeout = HZ;

		pohmelfs_mcache_remove_locked(psb, m);
	} while (atomic_read(&m->refcnt) != 1);

	pohmelfs_mcache_put(psb, m);

	dprintk("%s: ino: %llu, err: %d.\n", __func__, pi->ino, err);

	return err;

err_out_put:
	pohmelfs_mcache_put(psb, m);
	return err;
}

static int pohmelfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
#if 0
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	int err;

	err = pohmelfs_data_lock(pi, 0, ~0, POHMELFS_READ_LOCK);
	if (err)
		return err;
	dprintk("%s: ino: %llu, mode: %o, uid: %u, gid: %u, size: %llu.\n",
			__func__, pi->ino, inode->i_mode, inode->i_uid,
			inode->i_gid, inode->i_size);
#endif

	generic_fillattr(inode, stat);
	return 0;
}

const struct inode_operations pohmelfs_file_inode_operations = {
	.setattr	= pohmelfs_setattr,
	.getattr	= pohmelfs_getattr,
	.setxattr	= pohmelfs_setxattr,
	.getxattr	= pohmelfs_getxattr,
};

/*
 * Fill inode data: mode, size, operation callbacks and so on...
 */
void pohmelfs_fill_inode(struct inode *inode, struct netfs_inode_info *info)
{
	inode->i_mode = info->mode;
	inode->i_nlink = info->nlink;
	inode->i_uid = info->uid;
	inode->i_gid = info->gid;
	inode->i_blocks = info->blocks;
	inode->i_rdev = info->rdev;
	inode->i_size = info->size;
	inode->i_version = info->version;
	inode->i_blkbits = ffs(info->blocksize);

	dprintk("%s: inode: %p, num: %lu/%llu inode is regular: %d, dir: %d, link: %d, mode: %o, size: %llu.\n",
			__func__, inode, inode->i_ino, info->ino,
			S_ISREG(inode->i_mode), S_ISDIR(inode->i_mode),
			S_ISLNK(inode->i_mode), inode->i_mode, inode->i_size);

	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;

	/*
	 * i_mapping is a pointer to i_data during inode initialization.
	 */
	inode->i_data.a_ops = &pohmelfs_aops;

	if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &pohmelfs_file_ops;
		inode->i_op = &pohmelfs_file_inode_operations;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_fop = &pohmelfs_dir_fops;
		inode->i_op = &pohmelfs_dir_inode_ops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &pohmelfs_symlink_inode_operations;
		inode->i_fop = &pohmelfs_file_ops;
	} else {
		inode->i_fop = &generic_ro_fops;
	}
}

static void pohmelfs_drop_inode(struct inode *inode)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);
	struct pohmelfs_inode *pi = POHMELFS_I(inode);

	spin_lock(&psb->ino_lock);
	list_del_init(&pi->inode_entry);
	spin_unlock(&psb->ino_lock);

	generic_drop_inode(inode);
}

static struct pohmelfs_inode *pohmelfs_get_inode_from_list(struct pohmelfs_sb *psb,
		struct list_head *head, unsigned int *count)
{
	struct pohmelfs_inode *pi = NULL;

	spin_lock(&psb->ino_lock);
	if (!list_empty(head)) {
		pi = list_entry(head->next, struct pohmelfs_inode,
					inode_entry);
		list_del_init(&pi->inode_entry);
		*count = pi->drop_count;
		pi->drop_count = 0;
	}
	spin_unlock(&psb->ino_lock);

	return pi;
}

static void pohmelfs_flush_transactions(struct pohmelfs_sb *psb)
{
	struct pohmelfs_config *c;

	mutex_lock(&psb->state_lock);
	list_for_each_entry(c, &psb->state_list, config_entry) {
		pohmelfs_state_flush_transactions(&c->state);
	}
	mutex_unlock(&psb->state_lock);
}

/*
 * ->put_super() callback. Invoked before superblock is destroyed,
 *  so it has to clean all private data.
 */
static void pohmelfs_put_super(struct super_block *sb)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(sb);
	struct pohmelfs_inode *pi;
	unsigned int count;
	unsigned int in_drop_list = 0;
	struct inode *inode, *tmp;

	dprintk("%s.\n", __func__);

	/*
	 * Kill pending transactions, which could affect inodes in-flight.
	 */
	pohmelfs_flush_transactions(psb);

	while ((pi = pohmelfs_get_inode_from_list(psb, &psb->drop_list, &count))) {
		inode = &pi->vfs_inode;

		dprintk("%s: ino: %llu, pi: %p, inode: %p, count: %u.\n",
				__func__, pi->ino, pi, inode, count);

		if (atomic_read(&inode->i_count) != count) {
			printk("%s: ino: %llu, pi: %p, inode: %p, count: %u, i_count: %d.\n",
					__func__, pi->ino, pi, inode, count,
					atomic_read(&inode->i_count));
			count = atomic_read(&inode->i_count);
			in_drop_list++;
		}

		while (count--)
			iput(&pi->vfs_inode);
	}

	list_for_each_entry_safe(inode, tmp, &sb->s_inodes, i_sb_list) {
		pi = POHMELFS_I(inode);

		dprintk("%s: ino: %llu, pi: %p, inode: %p, i_count: %u.\n",
				__func__, pi->ino, pi, inode, atomic_read(&inode->i_count));

		/*
		 * These are special inodes, they were created during
		 * directory reading or lookup, and were not bound to dentry,
		 * so they live here with reference counter being 1 and prevent
		 * umount from succeed since it believes that they are busy.
		 */
		count = atomic_read(&inode->i_count);
		if (count) {
			list_del_init(&inode->i_sb_list);
			while (count--)
				iput(&pi->vfs_inode);
		}
	}

	psb->trans_scan_timeout = psb->drop_scan_timeout = 0;
	cancel_rearming_delayed_work(&psb->dwork);
	cancel_rearming_delayed_work(&psb->drop_dwork);
	flush_scheduled_work();

	dprintk("%s: stopped workqueues.\n", __func__);

	pohmelfs_crypto_exit(psb);
	pohmelfs_state_exit(psb);

	kfree(psb);
	sb->s_fs_info = NULL;
}

static int pohmelfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct pohmelfs_sb *psb = POHMELFS_SB(sb);

	/*
	 * There are no filesystem size limits yet.
	 */
	memset(buf, 0, sizeof(struct kstatfs));

	buf->f_type = POHMELFS_MAGIC_NUM; /* 'POH.' */
	buf->f_bsize = sb->s_blocksize;
	buf->f_files = psb->ino;
	buf->f_namelen = 255;
	buf->f_files = atomic_long_read(&psb->total_inodes);
	buf->f_bfree = buf->f_bavail = psb->avail_size >> PAGE_SHIFT;
	buf->f_blocks = psb->total_size >> PAGE_SHIFT;

	dprintk("%s: total: %llu, avail: %llu, inodes: %llu, bsize: %lu.\n",
		__func__, psb->total_size, psb->avail_size, buf->f_files, sb->s_blocksize);

	return 0;
}

static int pohmelfs_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(vfs->mnt_sb);

	seq_printf(seq, ",idx=%u", psb->idx);
	seq_printf(seq, ",trans_scan_timeout=%u", jiffies_to_msecs(psb->trans_scan_timeout));
	seq_printf(seq, ",drop_scan_timeout=%u", jiffies_to_msecs(psb->drop_scan_timeout));
	seq_printf(seq, ",wait_on_page_timeout=%u", jiffies_to_msecs(psb->wait_on_page_timeout));
	seq_printf(seq, ",trans_retries=%u", psb->trans_retries);
	seq_printf(seq, ",crypto_thread_num=%u", psb->crypto_thread_num);
	seq_printf(seq, ",trans_max_pages=%u", psb->trans_max_pages);
	seq_printf(seq, ",mcache_timeout=%u", jiffies_to_msecs(psb->mcache_timeout));
	if (psb->crypto_fail_unsupported)
		seq_printf(seq, ",crypto_fail_unsupported");

	return 0;
}

enum {
	pohmelfs_opt_idx,
	pohmelfs_opt_crypto_thread_num,
	pohmelfs_opt_trans_max_pages,
	pohmelfs_opt_crypto_fail_unsupported,

	/* Remountable options */
	pohmelfs_opt_trans_scan_timeout,
	pohmelfs_opt_drop_scan_timeout,
	pohmelfs_opt_wait_on_page_timeout,
	pohmelfs_opt_trans_retries,
	pohmelfs_opt_mcache_timeout,
};

static struct match_token pohmelfs_tokens[] = {
	{pohmelfs_opt_idx, "idx=%u"},
	{pohmelfs_opt_crypto_thread_num, "crypto_thread_num=%u"},
	{pohmelfs_opt_trans_max_pages, "trans_max_pages=%u"},
	{pohmelfs_opt_crypto_fail_unsupported, "crypto_fail_unsupported"},
	{pohmelfs_opt_trans_scan_timeout, "trans_scan_timeout=%u"},
	{pohmelfs_opt_drop_scan_timeout, "drop_scan_timeout=%u"},
	{pohmelfs_opt_wait_on_page_timeout, "wait_on_page_timeout=%u"},
	{pohmelfs_opt_trans_retries, "trans_retries=%u"},
	{pohmelfs_opt_mcache_timeout, "mcache_timeout=%u"},
};

static int pohmelfs_parse_options(char *options, struct pohmelfs_sb *psb, int remount)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option, err;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, pohmelfs_tokens, args);

		err = match_int(&args[0], &option);
		if (err)
			return err;

		if (remount && token <= pohmelfs_opt_crypto_fail_unsupported)
			continue;

		switch (token) {
			case pohmelfs_opt_idx:
				psb->idx = option;
				break;
			case pohmelfs_opt_trans_scan_timeout:
				psb->trans_scan_timeout = msecs_to_jiffies(option);
				break;
			case pohmelfs_opt_drop_scan_timeout:
				psb->drop_scan_timeout = msecs_to_jiffies(option);
				break;
			case pohmelfs_opt_wait_on_page_timeout:
				psb->wait_on_page_timeout = msecs_to_jiffies(option);
				break;
			case pohmelfs_opt_mcache_timeout:
				psb->mcache_timeout = msecs_to_jiffies(option);
				break;
			case pohmelfs_opt_trans_retries:
				psb->trans_retries = option;
				break;
			case pohmelfs_opt_crypto_thread_num:
				psb->crypto_thread_num = option;
				break;
			case pohmelfs_opt_trans_max_pages:
				psb->trans_max_pages = option;
				break;
			case pohmelfs_opt_crypto_fail_unsupported:
				psb->crypto_fail_unsupported = 1;
				break;
			default:
				return -EINVAL;
		}
	}

	return 0;
}

static int pohmelfs_remount(struct super_block *sb, int *flags, char *data)
{
	int err;
	struct pohmelfs_sb *psb = POHMELFS_SB(sb);
	unsigned long old_sb_flags = sb->s_flags;

	err = pohmelfs_parse_options(data, psb, 1);
	if (err)
		goto err_out_restore;

	if (!(*flags & MS_RDONLY))
		sb->s_flags &= ~MS_RDONLY;
	return 0;

err_out_restore:
	sb->s_flags = old_sb_flags;
	return err;
}

static void pohmelfs_flush_inode(struct pohmelfs_inode *pi, unsigned int count)
{
	struct inode *inode = &pi->vfs_inode;

	dprintk("%s: %p: ino: %llu, owned: %d.\n",
		__func__, inode, pi->ino, test_bit(NETFS_INODE_OWNED, &pi->state));

	mutex_lock(&inode->i_mutex);
	if (test_and_clear_bit(NETFS_INODE_OWNED, &pi->state)) {
		filemap_fdatawrite(inode->i_mapping);
		inode->i_sb->s_op->write_inode(inode, 0);
	}

#ifdef POHMELFS_TRUNCATE_ON_INODE_FLUSH
	truncate_inode_pages(inode->i_mapping, 0);
#endif

	pohmelfs_data_unlock(pi, 0, ~0, POHMELFS_WRITE_LOCK);
	mutex_unlock(&inode->i_mutex);
}

static void pohmelfs_put_inode_count(struct pohmelfs_inode *pi, unsigned int count)
{
	dprintk("%s: ino: %llu, pi: %p, inode: %p, count: %u.\n",
			__func__, pi->ino, pi, &pi->vfs_inode, count);

	if (test_and_clear_bit(NETFS_INODE_NEED_FLUSH, &pi->state))
		pohmelfs_flush_inode(pi, count);

	while (count--)
		iput(&pi->vfs_inode);
}

static void pohmelfs_drop_scan(struct work_struct *work)
{
	struct pohmelfs_sb *psb =
		container_of(work, struct pohmelfs_sb, drop_dwork.work);
	struct pohmelfs_inode *pi;
	unsigned int count = 0;

	while ((pi = pohmelfs_get_inode_from_list(psb, &psb->drop_list, &count)))
		pohmelfs_put_inode_count(pi, count);

	pohmelfs_check_states(psb);

	if (psb->drop_scan_timeout)
		schedule_delayed_work(&psb->drop_dwork, psb->drop_scan_timeout);
}

/*
 * Run through all transactions starting from the oldest,
 * drop transaction from current state and try to send it
 * to all remote nodes, which are currently installed.
 */
static void pohmelfs_trans_scan_state(struct netfs_state *st)
{
	struct rb_node *rb_node;
	struct netfs_trans_dst *dst;
	struct pohmelfs_sb *psb = st->psb;
	unsigned int timeout = psb->trans_scan_timeout;
	struct netfs_trans *t;
	int err;

	mutex_lock(&st->trans_lock);
	for (rb_node = rb_first(&st->trans_root); rb_node; ) {
		dst = rb_entry(rb_node, struct netfs_trans_dst, state_entry);
		t = dst->trans;

		if (timeout && time_after(dst->send_time + timeout, jiffies)
				&& dst->retries == 0)
			break;

		dprintk("%s: t: %p, gen: %u, st: %p, retries: %u, max: %u.\n",
			__func__, t, t->gen, st, dst->retries, psb->trans_retries);
		netfs_trans_get(t);

		rb_node = rb_next(rb_node);

		err = -ETIMEDOUT;
		if (timeout && (++dst->retries < psb->trans_retries))
			err = netfs_trans_resend(t, psb);

		if (err || (t->flags & NETFS_TRANS_SINGLE_DST)) {
			if (netfs_trans_remove_nolock(dst, st))
				netfs_trans_drop_dst_nostate(dst);
		}

		t->result = err;
		netfs_trans_put(t);
	}
	mutex_unlock(&st->trans_lock);
}

/*
 * Walk through all installed network states and resend all
 * transactions, which are old enough.
 */
static void pohmelfs_trans_scan(struct work_struct *work)
{
	struct pohmelfs_sb *psb =
		container_of(work, struct pohmelfs_sb, dwork.work);
	struct netfs_state *st;
	struct pohmelfs_config *c;

	mutex_lock(&psb->state_lock);
	list_for_each_entry(c, &psb->state_list, config_entry) {
		st = &c->state;

		pohmelfs_trans_scan_state(st);
	}
	mutex_unlock(&psb->state_lock);

	/*
	 * If no timeout specified then system is in the middle of umount process,
	 * so no need to reschedule scanning process again.
	 */
	if (psb->trans_scan_timeout)
		schedule_delayed_work(&psb->dwork, psb->trans_scan_timeout);
}

int pohmelfs_meta_command_data(struct pohmelfs_inode *pi, u64 id, unsigned int cmd_op, char *addon,
		unsigned int flags, netfs_trans_complete_t complete, void *priv, u64 start)
{
	struct inode *inode = &pi->vfs_inode;
	struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);
	int err = 0, sz;
	struct netfs_trans *t;
	int path_len, addon_len = 0;
	void *data;
	struct netfs_inode_info *info;
	struct netfs_cmd *cmd;

	dprintk("%s: ino: %llu, cmd: %u, addon: %p.\n", __func__, pi->ino, cmd_op, addon);

	path_len = pohmelfs_path_length(pi);
	if (path_len < 0) {
		err = path_len;
		goto err_out_exit;
	}

	if (addon)
		addon_len = strlen(addon) + 1; /* 0-byte */
	sz = addon_len;

	if (cmd_op == NETFS_INODE_INFO)
		sz += sizeof(struct netfs_inode_info);

	t = netfs_trans_alloc(psb, sz + path_len, flags, 0);
	if (!t) {
		err = -ENOMEM;
		goto err_out_exit;
	}
	t->complete = complete;
	t->private = priv;

	cmd = netfs_trans_current(t);
	data = (void *)(cmd + 1);

	if (cmd_op == NETFS_INODE_INFO) {
		info = (struct netfs_inode_info *)(cmd + 1);
		data = (void *)(info + 1);

		/*
		 * We are under i_mutex, can read and change whatever we want...
		 */
		info->mode = inode->i_mode;
		info->nlink = inode->i_nlink;
		info->uid = inode->i_uid;
		info->gid = inode->i_gid;
		info->blocks = inode->i_blocks;
		info->rdev = inode->i_rdev;
		info->size = inode->i_size;
		info->version = inode->i_version;

		netfs_convert_inode_info(info);
	}

	path_len = pohmelfs_construct_path_string(pi, data, path_len);
	if (path_len < 0)
		goto err_out_free;

	dprintk("%s: path_len: %d.\n", __func__, path_len);

	if (addon) {
		path_len--; /* Do not place null-byte before the addon */
		path_len += sprintf(data + path_len, "/%s", addon) + 1; /* 0 - byte */
	}

	sz += path_len;

	cmd->cmd = cmd_op;
	cmd->ext = path_len;
	cmd->size = sz;
	cmd->id = id;
	cmd->start = start;

	netfs_convert_cmd(cmd);
	netfs_trans_update(cmd, t, sz);

	/*
	 * Note, that it is possible to leak error here: transaction callback will not
	 * be invoked for allocation path failure.
	 */
	return netfs_trans_finish(t, psb);

err_out_free:
	netfs_trans_free(t);
err_out_exit:
	if (complete)
		complete(NULL, 0, priv, err);
	return err;
}

int pohmelfs_meta_command(struct pohmelfs_inode *pi, unsigned int cmd_op, unsigned int flags,
		netfs_trans_complete_t complete, void *priv, u64 start)
{
	return pohmelfs_meta_command_data(pi, pi->ino, cmd_op, NULL, flags, complete, priv, start);
}

/*
 * Send request and wait for POHMELFS root capabilities response,
 * which will update server's informaion about size of the export,
 * permissions, number of objects, available size and so on.
 */
static int pohmelfs_root_handshake(struct pohmelfs_sb *psb)
{
	struct netfs_trans *t;
	struct netfs_cmd *cmd;
	int err = -ENOMEM;

	t = netfs_trans_alloc(psb, 0, 0, 0);
	if (!t)
		goto err_out_exit;

	cmd = netfs_trans_current(t);

	cmd->cmd = NETFS_CAPABILITIES;
	cmd->id = POHMELFS_ROOT_CAPABILITIES;
	cmd->size = 0;
	cmd->start = 0;
	cmd->ext = 0;
	cmd->csize = 0;

	netfs_convert_cmd(cmd);
	netfs_trans_update(cmd, t, 0);

	err = netfs_trans_finish(t, psb);
	if (err)
		goto err_out_exit;

	psb->flags = ~0;
	err = wait_event_interruptible_timeout(psb->wait,
			(psb->flags != ~0),
			psb->wait_on_page_timeout);
	if (!err)
		err = -ETIMEDOUT;
	else if (err > 0)
		err = -psb->flags;

	if (err)
		goto err_out_exit;

	return 0;

err_out_exit:
	return err;
}

static int pohmelfs_show_stats(struct seq_file *m, struct vfsmount *mnt)
{
	struct netfs_state *st;
	struct pohmelfs_ctl *ctl;
	struct pohmelfs_sb *psb = POHMELFS_SB(mnt->mnt_sb);
	struct pohmelfs_config *c;

	mutex_lock(&psb->state_lock);

	seq_printf(m, "\nidx addr(:port) socket_type protocol active priority permissions\n");

	list_for_each_entry(c, &psb->state_list, config_entry) {
		st = &c->state;
		ctl = &st->ctl;

		seq_printf(m, "%u ", ctl->idx);
		if (ctl->addr.sa_family == AF_INET) {
			struct sockaddr_in *sin = (struct sockaddr_in *)&st->ctl.addr;
			/* seq_printf(m, "%pi4:%u", &sin->sin_addr.s_addr, ntohs(sin->sin_port)); */
			seq_printf(m, "%u.%u.%u.%u:%u", NIPQUAD(sin->sin_addr.s_addr), ntohs(sin->sin_port));
		} else if (ctl->addr.sa_family == AF_INET6) {
			struct sockaddr_in6 *sin = (struct sockaddr_in6 *)&st->ctl.addr;
			seq_printf(m, "%pi6:%u", &sin->sin6_addr, ntohs(sin->sin6_port));
		} else {
			unsigned int i;
			for (i=0; i<ctl->addrlen; ++i)
				seq_printf(m, "%02x.", ctl->addr.addr[i]);
		}

		seq_printf(m, " %u %u %d %u %x\n",
				ctl->type, ctl->proto,
				st->socket != NULL,
				ctl->prio, ctl->perm);
	}
	mutex_unlock(&psb->state_lock);

	return 0;
}

static const struct super_operations pohmelfs_sb_ops = {
	.alloc_inode	= pohmelfs_alloc_inode,
	.destroy_inode	= pohmelfs_destroy_inode,
	.drop_inode	= pohmelfs_drop_inode,
	.write_inode	= pohmelfs_write_inode,
	.put_super	= pohmelfs_put_super,
	.remount_fs	= pohmelfs_remount,
	.statfs		= pohmelfs_statfs,
	.show_options	= pohmelfs_show_options,
	.show_stats	= pohmelfs_show_stats,
};

/*
 * Allocate private superblock and create root dir.
 */
static int pohmelfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct pohmelfs_sb *psb;
	int err = -ENOMEM;
	struct inode *root;
	struct pohmelfs_inode *npi;
	struct qstr str;

	psb = kzalloc(sizeof(struct pohmelfs_sb), GFP_KERNEL);
	if (!psb)
		goto err_out_exit;

	sb->s_fs_info = psb;
	sb->s_op = &pohmelfs_sb_ops;
	sb->s_magic = POHMELFS_MAGIC_NUM;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_SIZE;

	psb->sb = sb;

	psb->ino = 2;
	psb->idx = 0;
	psb->active_state = NULL;
	psb->trans_retries = 5;
	psb->trans_data_size = PAGE_SIZE;
	psb->drop_scan_timeout = msecs_to_jiffies(1000);
	psb->trans_scan_timeout = msecs_to_jiffies(5000);
	psb->wait_on_page_timeout = msecs_to_jiffies(5000);
	init_waitqueue_head(&psb->wait);

	spin_lock_init(&psb->ino_lock);

	INIT_LIST_HEAD(&psb->drop_list);

	mutex_init(&psb->mcache_lock);
	psb->mcache_root = RB_ROOT;
	psb->mcache_timeout = msecs_to_jiffies(5000);
	atomic_long_set(&psb->mcache_gen, 0);

	psb->trans_max_pages = 100;

	psb->crypto_align_size = 16;
	psb->crypto_attached_size = 0;
	psb->hash_strlen = 0;
	psb->cipher_strlen = 0;
	psb->perform_crypto = 0;
	psb->crypto_thread_num = 2;
	psb->crypto_fail_unsupported = 0;
	mutex_init(&psb->crypto_thread_lock);
	INIT_LIST_HEAD(&psb->crypto_ready_list);
	INIT_LIST_HEAD(&psb->crypto_active_list);

	atomic_set(&psb->trans_gen, 1);
	atomic_long_set(&psb->total_inodes, 0);

	mutex_init(&psb->state_lock);
	INIT_LIST_HEAD(&psb->state_list);

	err = pohmelfs_parse_options((char *) data, psb, 0);
	if (err)
		goto err_out_free_sb;

	err = pohmelfs_copy_crypto(psb);
	if (err)
		goto err_out_free_sb;

	err = pohmelfs_state_init(psb);
	if (err)
		goto err_out_free_strings;

	err = pohmelfs_crypto_init(psb);
	if (err)
		goto err_out_state_exit;

	err = pohmelfs_root_handshake(psb);
	if (err)
		goto err_out_crypto_exit;

	str.name = "/";
	str.hash = jhash("/", 1, 0);
	str.len = 1;

	npi = pohmelfs_create_entry_local(psb, NULL, &str, 0, 0755|S_IFDIR);
	if (IS_ERR(npi)) {
		err = PTR_ERR(npi);
		goto err_out_crypto_exit;
	}
	set_bit(NETFS_INODE_REMOTE_SYNCED, &npi->state);
	clear_bit(NETFS_INODE_OWNED, &npi->state);

	root = &npi->vfs_inode;

	sb->s_root = d_alloc_root(root);
	if (!sb->s_root)
		goto err_out_put_root;

	INIT_DELAYED_WORK(&psb->drop_dwork, pohmelfs_drop_scan);
	schedule_delayed_work(&psb->drop_dwork, psb->drop_scan_timeout);

	INIT_DELAYED_WORK(&psb->dwork, pohmelfs_trans_scan);
	schedule_delayed_work(&psb->dwork, psb->trans_scan_timeout);

	return 0;

err_out_put_root:
	iput(root);
err_out_crypto_exit:
	pohmelfs_crypto_exit(psb);
err_out_state_exit:
	pohmelfs_state_exit(psb);
err_out_free_strings:
	kfree(psb->cipher_string);
	kfree(psb->hash_string);
err_out_free_sb:
	kfree(psb);
err_out_exit:

	dprintk("%s: err: %d.\n", __func__, err);
	return err;
}

/*
 * Some VFS magic here...
 */
static int pohmelfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, data, pohmelfs_fill_super,
				mnt);
}

/*
 * We need this to sync all inodes earlier, since when writeback
 * is invoked from the umount/mntput path dcache is already shrunk,
 * see generic_shutdown_super(), and no inodes can access the path.
 */
static void pohmelfs_kill_super(struct super_block *sb)
{
	sync_inodes_sb(sb);
	kill_anon_super(sb);
}

static struct file_system_type pohmel_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "pohmel",
	.get_sb		= pohmelfs_get_sb,
	.kill_sb 	= pohmelfs_kill_super,
};

/*
 * Cache and module initializations and freeing routings.
 */
static void pohmelfs_init_once(void *data)
{
	struct pohmelfs_inode *pi = data;

	inode_init_once(&pi->vfs_inode);
}

static int __init pohmelfs_init_inodecache(void)
{
	pohmelfs_inode_cache = kmem_cache_create("pohmelfs_inode_cache",
				sizeof(struct pohmelfs_inode),
				0, (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD),
				pohmelfs_init_once);
	if (!pohmelfs_inode_cache)
		return -ENOMEM;

	return 0;
}

static void pohmelfs_destroy_inodecache(void)
{
	kmem_cache_destroy(pohmelfs_inode_cache);
}

static int __init init_pohmel_fs(void)
{
	int err;

	err = pohmelfs_config_init();
	if (err)
		goto err_out_exit;

	err = pohmelfs_init_inodecache();
	if (err)
		goto err_out_config_exit;

	err = pohmelfs_mcache_init();
	if (err)
		goto err_out_destroy;

	err = netfs_trans_init();
	if (err)
		goto err_out_mcache_exit;

	err = register_filesystem(&pohmel_fs_type);
	if (err)
		goto err_out_trans;

	return 0;

err_out_trans:
	netfs_trans_exit();
err_out_mcache_exit:
	pohmelfs_mcache_exit();
err_out_destroy:
	pohmelfs_destroy_inodecache();
err_out_config_exit:
	pohmelfs_config_exit();
err_out_exit:
	return err;
}

static void __exit exit_pohmel_fs(void)
{
        unregister_filesystem(&pohmel_fs_type);
	pohmelfs_destroy_inodecache();
	pohmelfs_mcache_exit();
	pohmelfs_config_exit();
	netfs_trans_exit();
}

module_init(init_pohmel_fs);
module_exit(exit_pohmel_fs);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("Pohmel filesystem");
