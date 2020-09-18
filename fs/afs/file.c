// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS filesystem file handling
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/gfp.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/mm.h>
#include "internal.h"

static int afs_file_mmap(struct file *file, struct vm_area_struct *vma);
static int afs_readpage(struct file *file, struct page *page);
static void afs_invalidatepage(struct page *page, unsigned int offset,
			       unsigned int length);
static int afs_releasepage(struct page *page, gfp_t gfp_flags);

static int afs_readpages(struct file *filp, struct address_space *mapping,
			 struct list_head *pages, unsigned nr_pages);

const struct file_operations afs_file_operations = {
	.open		= afs_open,
	.release	= afs_release,
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= afs_file_write,
	.mmap		= afs_file_mmap,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fsync		= afs_fsync,
	.lock		= afs_lock,
	.flock		= afs_flock,
};

const struct inode_operations afs_file_inode_operations = {
	.getattr	= afs_getattr,
	.setattr	= afs_setattr,
	.permission	= afs_permission,
};

const struct address_space_operations afs_fs_aops = {
	.readpage	= afs_readpage,
	.readpages	= afs_readpages,
	.set_page_dirty	= afs_set_page_dirty,
	.launder_page	= afs_launder_page,
	.releasepage	= afs_releasepage,
	.invalidatepage	= afs_invalidatepage,
	.write_begin	= afs_write_begin,
	.write_end	= afs_write_end,
	.writepage	= afs_writepage,
	.writepages	= afs_writepages,
};

static const struct vm_operations_struct afs_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= afs_page_mkwrite,
};

/*
 * Discard a pin on a writeback key.
 */
void afs_put_wb_key(struct afs_wb_key *wbk)
{
	if (wbk && refcount_dec_and_test(&wbk->usage)) {
		key_put(wbk->key);
		kfree(wbk);
	}
}

/*
 * Cache key for writeback.
 */
int afs_cache_wb_key(struct afs_vnode *vnode, struct afs_file *af)
{
	struct afs_wb_key *wbk, *p;

	wbk = kzalloc(sizeof(struct afs_wb_key), GFP_KERNEL);
	if (!wbk)
		return -ENOMEM;
	refcount_set(&wbk->usage, 2);
	wbk->key = af->key;

	spin_lock(&vnode->wb_lock);
	list_for_each_entry(p, &vnode->wb_keys, vnode_link) {
		if (p->key == wbk->key)
			goto found;
	}

	key_get(wbk->key);
	list_add_tail(&wbk->vnode_link, &vnode->wb_keys);
	spin_unlock(&vnode->wb_lock);
	af->wb = wbk;
	return 0;

found:
	refcount_inc(&p->usage);
	spin_unlock(&vnode->wb_lock);
	af->wb = p;
	kfree(wbk);
	return 0;
}

/*
 * open an AFS file or directory and attach a key to it
 */
int afs_open(struct inode *inode, struct file *file)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_file *af;
	struct key *key;
	int ret;

	_enter("{%llx:%llu},", vnode->fid.vid, vnode->fid.vnode);

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	af = kzalloc(sizeof(*af), GFP_KERNEL);
	if (!af) {
		ret = -ENOMEM;
		goto error_key;
	}
	af->key = key;

	ret = afs_validate(vnode, key);
	if (ret < 0)
		goto error_af;

	if (file->f_mode & FMODE_WRITE) {
		ret = afs_cache_wb_key(vnode, af);
		if (ret < 0)
			goto error_af;
	}

	if (file->f_flags & O_TRUNC)
		set_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags);
	
	file->private_data = af;
	_leave(" = 0");
	return 0;

error_af:
	kfree(af);
error_key:
	key_put(key);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * release an AFS file or directory and discard its key
 */
int afs_release(struct inode *inode, struct file *file)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_file *af = file->private_data;
	int ret = 0;

	_enter("{%llx:%llu},", vnode->fid.vid, vnode->fid.vnode);

	if ((file->f_mode & FMODE_WRITE))
		ret = vfs_fsync(file, 0);

	file->private_data = NULL;
	if (af->wb)
		afs_put_wb_key(af->wb);
	key_put(af->key);
	kfree(af);
	afs_prune_wb_keys(vnode);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Handle completion of a read operation.
 */
static void afs_file_read_done(struct afs_read *req)
{
	struct afs_vnode *vnode = req->vnode;
	struct page *page;
	pgoff_t index = req->pos >> PAGE_SHIFT;
	pgoff_t last = index + req->nr_pages - 1;

	XA_STATE(xas, &vnode->vfs_inode.i_mapping->i_pages, index);

	if (iov_iter_count(req->iter) > 0) {
		/* The read was short - clear the excess buffer. */
		_debug("afterclear %zx %zx %llx/%llx",
		       req->iter->iov_offset,
		       iov_iter_count(req->iter),
		       req->actual_len, req->len);
		iov_iter_zero(iov_iter_count(req->iter), req->iter);
	}

	rcu_read_lock();
	xas_for_each(&xas, page, last) {
		page_endio(page, false, 0);
		put_page(page);
	}
	rcu_read_unlock();

	task_io_account_read(req->len);
	req->cleanup = NULL;
}

/*
 * Dispose of our locks and refs on the pages if the read failed.
 */
static void afs_file_read_cleanup(struct afs_read *req)
{
	struct page *page;
	pgoff_t index = req->pos >> PAGE_SHIFT;
	pgoff_t last = index + req->nr_pages - 1;

	if (req->iter) {
		XA_STATE(xas, &req->vnode->vfs_inode.i_mapping->i_pages, index);

		_enter("%lu,%u,%zu", index, req->nr_pages, iov_iter_count(req->iter));

		rcu_read_lock();
		xas_for_each(&xas, page, last) {
			BUG_ON(xa_is_value(page));
			BUG_ON(PageCompound(page));

			page_endio(page, false, req->error);
			put_page(page);
		}
		rcu_read_unlock();
	}
}

/*
 * Dispose of a ref to a read record.
 */
void afs_put_read(struct afs_read *req)
{
	if (refcount_dec_and_test(&req->usage)) {
		if (req->cleanup)
			req->cleanup(req);
		key_put(req->key);
		kfree(req);
	}
}

static void afs_fetch_data_notify(struct afs_operation *op)
{
	struct afs_read *req = op->fetch.req;
	int error = op->error;

	if (error == -ECONNABORTED)
		error = afs_abort_to_error(op->ac.abort_code);
	req->error = error;

	if (req->done)
		req->done(req);
}

static void afs_fetch_data_success(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;

	_enter("op=%08x", op->debug_id);
	afs_vnode_commit_status(op, &op->file[0]);
	afs_stat_v(vnode, n_fetches);
	atomic_long_add(op->fetch.req->actual_len, &op->net->n_fetch_bytes);
	afs_fetch_data_notify(op);
}

static void afs_fetch_data_put(struct afs_operation *op)
{
	op->fetch.req->error = op->error;
	afs_put_read(op->fetch.req);
}

static const struct afs_operation_ops afs_fetch_data_operation = {
	.issue_afs_rpc	= afs_fs_fetch_data,
	.issue_yfs_rpc	= yfs_fs_fetch_data,
	.success	= afs_fetch_data_success,
	.aborted	= afs_check_for_remote_deletion,
	.failed		= afs_fetch_data_notify,
	.put		= afs_fetch_data_put,
};

/*
 * Fetch file data from the volume.
 */
int afs_fetch_data(struct afs_vnode *vnode, struct afs_read *req)
{
	struct afs_operation *op;

	_enter("%s{%llx:%llu.%u},%x,,,",
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(req->key));

	op = afs_alloc_operation(req->key, vnode->volume);
	if (IS_ERR(op))
		return PTR_ERR(op);

	afs_op_set_vnode(op, 0, vnode);

	op->fetch.req	= afs_get_read(req);
	op->ops		= &afs_fetch_data_operation;
	return afs_do_sync_operation(op);
}

/*
 * read page from file, directory or symlink, given a key to use
 */
static int afs_page_filler(struct key *key, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_read *req;
	int ret;

	_enter("{%x},{%lu},{%lu}", key_serial(key), inode->i_ino, page->index);

	BUG_ON(!PageLocked(page));

	ret = -ESTALE;
	if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
		goto error;

	req = kzalloc(sizeof(struct afs_read), GFP_KERNEL);
	if (!req)
		goto enomem;

	refcount_set(&req->usage, 1);
	req->vnode		= vnode;
	req->key		= key_get(key);
	req->pos		= (loff_t)page->index << PAGE_SHIFT;
	req->len		= thp_size(page);
	req->nr_pages		= thp_nr_pages(page);
	req->done		= afs_file_read_done;
	req->cleanup		= afs_file_read_cleanup;

	get_page(page);
	iov_iter_xarray(&req->def_iter, READ, &page->mapping->i_pages,
			req->pos, req->len);
	req->iter = &req->def_iter;

	ret = afs_fetch_data(vnode, req);
	if (ret < 0)
		goto fetch_error;

	afs_put_read(req);
	_leave(" = 0");
	return 0;

fetch_error:
	switch (ret) {
	case -EINTR:
	case -ENOMEM:
	case -ERESTARTSYS:
	case -EAGAIN:
		afs_put_read(req);
		goto error;
	case -ENOENT:
		_debug("got NOENT from server - marking file deleted and stale");
		set_bit(AFS_VNODE_DELETED, &vnode->flags);
		ret = -ESTALE;
		/* Fall through */
	default:
		page_endio(page, false, ret);
		afs_put_read(req);
		_leave(" = %d", ret);
		return ret;
	}

enomem:
	ret = -ENOMEM;
error:
	unlock_page(page);
	_leave(" = %d", ret);
	return ret;
}

/*
 * read page from file, directory or symlink, given a file to nominate the key
 * to be used
 */
static int afs_readpage(struct file *file, struct page *page)
{
	struct key *key;
	int ret;

	if (file) {
		key = afs_file_key(file);
		ASSERT(key != NULL);
		ret = afs_page_filler(key, page);
	} else {
		struct inode *inode = page->mapping->host;
		key = afs_request_key(AFS_FS_S(inode->i_sb)->cell);
		if (IS_ERR(key)) {
			ret = PTR_ERR(key);
		} else {
			ret = afs_page_filler(key, page);
			key_put(key);
		}
	}
	return ret;
}

/*
 * Read a contiguous set of pages.
 */
static int afs_readpages_one(struct file *file, struct address_space *mapping,
			     struct list_head *pages)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct afs_read *req;
	struct list_head *p;
	struct page *first, *page;
	pgoff_t index;
	int ret, n;

	/* Count the number of contiguous pages at the front of the list.  Note
	 * that the list goes prev-wards rather than next-wards.
	 */
	first = lru_to_page(pages);
	index = first->index + 1;
	n = 1;
	for (p = first->lru.prev; p != pages; p = p->prev) {
		page = list_entry(p, struct page, lru);
		if (page->index != index)
			break;
		index++;
		n++;
	}

	req = kzalloc(sizeof(struct afs_read), GFP_NOFS);
	if (!req)
		return -ENOMEM;

	refcount_set(&req->usage, 1);
	req->vnode = vnode;
	req->key = key_get(afs_file_key(file));
	req->done = afs_file_read_done;
	req->cleanup = afs_file_read_cleanup;
	req->pos = first->index;
	req->pos <<= PAGE_SHIFT;

	/* Add pages to the LRU until it fails.  We keep the pages ref'd and
	 * locked until the read is complete.
	 *
	 * Note that it's possible for the file size to change whilst we're
	 * doing this, but we rely on the server returning less than we asked
	 * for if the file shrank.  We also rely on this to deal with a partial
	 * page at the end of the file.
	 */
	do {
		page = lru_to_page(pages);
		list_del(&page->lru);
		index = page->index;
		if (add_to_page_cache_lru(page, mapping, index,
					  readahead_gfp_mask(mapping))) {
			put_page(page);
			break;
		}

		req->nr_pages++;
	} while (req->nr_pages < n);

	if (req->nr_pages == 0) {
		afs_put_read(req);
		return 0;
	}

	req->len = req->nr_pages * PAGE_SIZE;
	iov_iter_xarray(&req->def_iter, READ, &file->f_mapping->i_pages,
			req->pos, req->len);
	req->iter = &req->def_iter;

	ret = afs_fetch_data(vnode, req);
	if (ret < 0)
		goto error;

	afs_put_read(req);
	return 0;

error:
	if (ret == -ENOENT) {
		_debug("got NOENT from server - marking file deleted and stale");
		set_bit(AFS_VNODE_DELETED, &vnode->flags);
		ret = -ESTALE;
	}

	afs_put_read(req);
	return ret;
}

/*
 * read a set of pages
 */
static int afs_readpages(struct file *file, struct address_space *mapping,
			 struct list_head *pages, unsigned nr_pages)
{
	struct key *key = afs_file_key(file);
	struct afs_vnode *vnode;
	int ret = 0;

	_enter("{%d},{%lu},,%d",
	       key_serial(key), mapping->host->i_ino, nr_pages);

	ASSERT(key != NULL);

	vnode = AFS_FS_I(mapping->host);
	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		_leave(" = -ESTALE");
		return -ESTALE;
	}

	/* attempt to read as many of the pages as possible */
	while (!list_empty(pages)) {
		ret = afs_readpages_one(file, mapping, pages);
		if (ret < 0)
			break;
	}

	_leave(" = %d [netting]", ret);
	return ret;
}

/*
 * Adjust the dirty region of the page on truncation or full invalidation,
 * getting rid of the markers altogether if the region is entirely invalidated.
 */
static void afs_invalidate_dirty(struct page *page, unsigned int offset,
				 unsigned int length)
{
	struct afs_vnode *vnode = AFS_FS_I(page->mapping->host);
	unsigned long priv;
	unsigned int f, t, end = offset + length;

	priv = page_private(page);

	/* we clean up only if the entire page is being invalidated */
	if (offset == 0 && length == thp_size(page))
		goto full_invalidate;

	 /* If the page was dirtied by page_mkwrite(), the PTE stays writable
	  * and we don't get another notification to tell us to expand it
	  * again.
	  */
	if (afs_is_page_dirty_mmapped(priv))
		return;

	/* We may need to shorten the dirty region */
	f = afs_page_dirty_from(page, priv);
	t = afs_page_dirty_to(page, priv);

	if (t <= offset || f >= end)
		return; /* Doesn't overlap */

	if (f < offset && t > end)
		return; /* Splits the dirty region - just absorb it */

	if (f >= offset && t <= end)
		goto undirty;

	if (f < offset)
		t = offset;
	else
		f = end;
	if (f == t)
		goto undirty;

	priv = afs_page_dirty(page, f, t);
	set_page_private(page, priv);
	trace_afs_page_dirty(vnode, tracepoint_string("trunc"), page);
	return;

undirty:
	trace_afs_page_dirty(vnode, tracepoint_string("undirty"), page);
	clear_page_dirty_for_io(page);
full_invalidate:
	trace_afs_page_dirty(vnode, tracepoint_string("inval"), page);
	detach_page_private(page);
}

/*
 * invalidate part or all of a page
 * - release a page and clean up its private data if offset is 0 (indicating
 *   the entire page)
 */
static void afs_invalidatepage(struct page *page, unsigned int offset,
			       unsigned int length)
{
	_enter("{%lu},%u,%u", page->index, offset, length);

	BUG_ON(!PageLocked(page));

	if (PagePrivate(page))
		afs_invalidate_dirty(page, offset, length);

	wait_on_page_fscache(page);
	_leave("");
}

/*
 * release a page and clean up its private state if it's not busy
 * - return true if the page can now be released, false if not
 */
static int afs_releasepage(struct page *page, gfp_t gfp_flags)
{
	struct afs_vnode *vnode = AFS_FS_I(page->mapping->host);

	_enter("{{%llx:%llu}[%lu],%lx},%x",
	       vnode->fid.vid, vnode->fid.vnode, page->index, page->flags,
	       gfp_flags);

	/* deny if page is being written to the cache and the caller hasn't
	 * elected to wait */
#ifdef CONFIG_AFS_FSCACHE
	if (PageFsCache(page)) {
		if (!(gfp_flags & __GFP_DIRECT_RECLAIM) || !(gfp_flags & __GFP_FS))
			return false;
		wait_on_page_fscache(page);
	}
#endif

	if (PagePrivate(page)) {
		trace_afs_page_dirty(vnode, tracepoint_string("rel"), page);
		detach_page_private(page);
	}

	/* indicate that the page can be released */
	_leave(" = T");
	return 1;
}

/*
 * Handle setting up a memory mapping on an AFS file.
 */
static int afs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;

	ret = generic_file_mmap(file, vma);
	if (ret == 0)
		vma->vm_ops = &afs_vm_ops;
	return ret;
}
