// SPDX-License-Identifier: GPL-2.0-or-later
/* handling of writes to regular files and writing back to the server
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/backing-dev.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include "internal.h"

/*
 * mark a page as having been made dirty and thus needing writeback
 */
int afs_set_page_dirty(struct page *page)
{
	_enter("");
	return __set_page_dirty_nobuffers(page);
}

/*
 * partly or wholly fill a page that's under preparation for writing
 */
static int afs_fill_page(struct afs_vnode *vnode, struct key *key,
			 loff_t pos, unsigned int len, struct page *page)
{
	struct afs_read *req;
	size_t p;
	void *data;
	int ret;

	_enter(",,%llu", (unsigned long long)pos);

	if (pos >= vnode->vfs_inode.i_size) {
		p = pos & ~PAGE_MASK;
		ASSERTCMP(p + len, <=, PAGE_SIZE);
		data = kmap(page);
		memset(data + p, 0, len);
		kunmap(page);
		return 0;
	}

	req = kzalloc(struct_size(req, array, 1), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	refcount_set(&req->usage, 1);
	req->pos = pos;
	req->len = len;
	req->nr_pages = 1;
	req->pages = req->array;
	req->pages[0] = page;
	get_page(page);

	ret = afs_fetch_data(vnode, key, req);
	afs_put_read(req);
	if (ret < 0) {
		if (ret == -ENOENT) {
			_debug("got NOENT from server"
			       " - marking file deleted and stale");
			set_bit(AFS_VNODE_DELETED, &vnode->flags);
			ret = -ESTALE;
		}
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * prepare to perform part of a write to a page
 */
int afs_write_begin(struct file *file, struct address_space *mapping,
		    loff_t pos, unsigned len, unsigned flags,
		    struct page **_page, void **fsdata)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(file));
	struct page *page;
	struct key *key = afs_file_key(file);
	unsigned long priv;
	unsigned f, from = pos & (PAGE_SIZE - 1);
	unsigned t, to = from + len;
	pgoff_t index = pos >> PAGE_SHIFT;
	int ret;

	_enter("{%llx:%llu},{%lx},%u,%u",
	       vnode->fid.vid, vnode->fid.vnode, index, from, to);

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;

	if (!PageUptodate(page) && len != PAGE_SIZE) {
		ret = afs_fill_page(vnode, key, pos & PAGE_MASK, PAGE_SIZE, page);
		if (ret < 0) {
			unlock_page(page);
			put_page(page);
			_leave(" = %d [prep]", ret);
			return ret;
		}
		SetPageUptodate(page);
	}

try_again:
	/* See if this page is already partially written in a way that we can
	 * merge the new write with.
	 */
	t = f = 0;
	if (PagePrivate(page)) {
		priv = page_private(page);
		f = afs_page_dirty_from(priv);
		t = afs_page_dirty_to(priv);
		ASSERTCMP(f, <=, t);
	}

	if (f != t) {
		if (PageWriteback(page)) {
			trace_afs_page_dirty(vnode, tracepoint_string("alrdy"),
					     page->index, priv);
			goto flush_conflicting_write;
		}
		/* If the file is being filled locally, allow inter-write
		 * spaces to be merged into writes.  If it's not, only write
		 * back what the user gives us.
		 */
		if (!test_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags) &&
		    (to < f || from > t))
			goto flush_conflicting_write;
	}

	*_page = page;
	_leave(" = 0");
	return 0;

	/* The previous write and this write aren't adjacent or overlapping, so
	 * flush the page out.
	 */
flush_conflicting_write:
	_debug("flush conflict");
	ret = write_one_page(page);
	if (ret < 0)
		goto error;

	ret = lock_page_killable(page);
	if (ret < 0)
		goto error;
	goto try_again;

error:
	put_page(page);
	_leave(" = %d", ret);
	return ret;
}

/*
 * finalise part of a write to a page
 */
int afs_write_end(struct file *file, struct address_space *mapping,
		  loff_t pos, unsigned len, unsigned copied,
		  struct page *page, void *fsdata)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(file));
	struct key *key = afs_file_key(file);
	unsigned long priv;
	unsigned int f, from = pos & (PAGE_SIZE - 1);
	unsigned int t, to = from + copied;
	loff_t i_size, maybe_i_size;
	int ret = 0;

	_enter("{%llx:%llu},{%lx}",
	       vnode->fid.vid, vnode->fid.vnode, page->index);

	if (copied == 0)
		goto out;

	maybe_i_size = pos + copied;

	i_size = i_size_read(&vnode->vfs_inode);
	if (maybe_i_size > i_size) {
		write_seqlock(&vnode->cb_lock);
		i_size = i_size_read(&vnode->vfs_inode);
		if (maybe_i_size > i_size)
			i_size_write(&vnode->vfs_inode, maybe_i_size);
		write_sequnlock(&vnode->cb_lock);
	}

	if (!PageUptodate(page)) {
		if (copied < len) {
			/* Try and load any missing data from the server.  The
			 * unmarshalling routine will take care of clearing any
			 * bits that are beyond the EOF.
			 */
			ret = afs_fill_page(vnode, key, pos + copied,
					    len - copied, page);
			if (ret < 0)
				goto out;
		}
		SetPageUptodate(page);
	}

	if (PagePrivate(page)) {
		priv = page_private(page);
		f = afs_page_dirty_from(priv);
		t = afs_page_dirty_to(priv);
		if (from < f)
			f = from;
		if (to > t)
			t = to;
		priv = afs_page_dirty(f, t);
		set_page_private(page, priv);
		trace_afs_page_dirty(vnode, tracepoint_string("dirty+"),
				     page->index, priv);
	} else {
		priv = afs_page_dirty(from, to);
		attach_page_private(page, (void *)priv);
		trace_afs_page_dirty(vnode, tracepoint_string("dirty"),
				     page->index, priv);
	}

	set_page_dirty(page);
	if (PageDirty(page))
		_debug("dirtied");
	ret = copied;

out:
	unlock_page(page);
	put_page(page);
	return ret;
}

/*
 * kill all the pages in the given range
 */
static void afs_kill_pages(struct address_space *mapping,
			   pgoff_t first, pgoff_t last)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct pagevec pv;
	unsigned count, loop;

	_enter("{%llx:%llu},%lx-%lx",
	       vnode->fid.vid, vnode->fid.vnode, first, last);

	pagevec_init(&pv);

	do {
		_debug("kill %lx-%lx", first, last);

		count = last - first + 1;
		if (count > PAGEVEC_SIZE)
			count = PAGEVEC_SIZE;
		pv.nr = find_get_pages_contig(mapping, first, count, pv.pages);
		ASSERTCMP(pv.nr, ==, count);

		for (loop = 0; loop < count; loop++) {
			struct page *page = pv.pages[loop];
			ClearPageUptodate(page);
			SetPageError(page);
			end_page_writeback(page);
			if (page->index >= first)
				first = page->index + 1;
			lock_page(page);
			generic_error_remove_page(mapping, page);
			unlock_page(page);
		}

		__pagevec_release(&pv);
	} while (first <= last);

	_leave("");
}

/*
 * Redirty all the pages in a given range.
 */
static void afs_redirty_pages(struct writeback_control *wbc,
			      struct address_space *mapping,
			      pgoff_t first, pgoff_t last)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct pagevec pv;
	unsigned count, loop;

	_enter("{%llx:%llu},%lx-%lx",
	       vnode->fid.vid, vnode->fid.vnode, first, last);

	pagevec_init(&pv);

	do {
		_debug("redirty %lx-%lx", first, last);

		count = last - first + 1;
		if (count > PAGEVEC_SIZE)
			count = PAGEVEC_SIZE;
		pv.nr = find_get_pages_contig(mapping, first, count, pv.pages);
		ASSERTCMP(pv.nr, ==, count);

		for (loop = 0; loop < count; loop++) {
			struct page *page = pv.pages[loop];

			redirty_page_for_writepage(wbc, page);
			end_page_writeback(page);
			if (page->index >= first)
				first = page->index + 1;
		}

		__pagevec_release(&pv);
	} while (first <= last);

	_leave("");
}

/*
 * completion of write to server
 */
static void afs_pages_written_back(struct afs_vnode *vnode,
				   pgoff_t first, pgoff_t last)
{
	struct pagevec pv;
	unsigned long priv;
	unsigned count, loop;

	_enter("{%llx:%llu},{%lx-%lx}",
	       vnode->fid.vid, vnode->fid.vnode, first, last);

	pagevec_init(&pv);

	do {
		_debug("done %lx-%lx", first, last);

		count = last - first + 1;
		if (count > PAGEVEC_SIZE)
			count = PAGEVEC_SIZE;
		pv.nr = find_get_pages_contig(vnode->vfs_inode.i_mapping,
					      first, count, pv.pages);
		ASSERTCMP(pv.nr, ==, count);

		for (loop = 0; loop < count; loop++) {
			priv = (unsigned long)detach_page_private(pv.pages[loop]);
			trace_afs_page_dirty(vnode, tracepoint_string("clear"),
					     pv.pages[loop]->index, priv);
			end_page_writeback(pv.pages[loop]);
		}
		first += count;
		__pagevec_release(&pv);
	} while (first <= last);

	afs_prune_wb_keys(vnode);
	_leave("");
}

/*
 * Find a key to use for the writeback.  We cached the keys used to author the
 * writes on the vnode.  *_wbk will contain the last writeback key used or NULL
 * and we need to start from there if it's set.
 */
static int afs_get_writeback_key(struct afs_vnode *vnode,
				 struct afs_wb_key **_wbk)
{
	struct afs_wb_key *wbk = NULL;
	struct list_head *p;
	int ret = -ENOKEY, ret2;

	spin_lock(&vnode->wb_lock);
	if (*_wbk)
		p = (*_wbk)->vnode_link.next;
	else
		p = vnode->wb_keys.next;

	while (p != &vnode->wb_keys) {
		wbk = list_entry(p, struct afs_wb_key, vnode_link);
		_debug("wbk %u", key_serial(wbk->key));
		ret2 = key_validate(wbk->key);
		if (ret2 == 0) {
			refcount_inc(&wbk->usage);
			_debug("USE WB KEY %u", key_serial(wbk->key));
			break;
		}

		wbk = NULL;
		if (ret == -ENOKEY)
			ret = ret2;
		p = p->next;
	}

	spin_unlock(&vnode->wb_lock);
	if (*_wbk)
		afs_put_wb_key(*_wbk);
	*_wbk = wbk;
	return 0;
}

static void afs_store_data_success(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;

	op->ctime = op->file[0].scb.status.mtime_client;
	afs_vnode_commit_status(op, &op->file[0]);
	if (op->error == 0) {
		if (!op->store.laundering)
			afs_pages_written_back(vnode, op->store.first, op->store.last);
		afs_stat_v(vnode, n_stores);
		atomic_long_add((op->store.last * PAGE_SIZE + op->store.last_to) -
				(op->store.first * PAGE_SIZE + op->store.first_offset),
				&afs_v2net(vnode)->n_store_bytes);
	}
}

static const struct afs_operation_ops afs_store_data_operation = {
	.issue_afs_rpc	= afs_fs_store_data,
	.issue_yfs_rpc	= yfs_fs_store_data,
	.success	= afs_store_data_success,
};

/*
 * write to a file
 */
static int afs_store_data(struct address_space *mapping,
			  pgoff_t first, pgoff_t last,
			  unsigned offset, unsigned to, bool laundering)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct afs_operation *op;
	struct afs_wb_key *wbk = NULL;
	int ret;

	_enter("%s{%llx:%llu.%u},%lx,%lx,%x,%x",
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       first, last, offset, to);

	ret = afs_get_writeback_key(vnode, &wbk);
	if (ret) {
		_leave(" = %d [no keys]", ret);
		return ret;
	}

	op = afs_alloc_operation(wbk->key, vnode->volume);
	if (IS_ERR(op)) {
		afs_put_wb_key(wbk);
		return -ENOMEM;
	}

	afs_op_set_vnode(op, 0, vnode);
	op->file[0].dv_delta = 1;
	op->store.mapping = mapping;
	op->store.first = first;
	op->store.last = last;
	op->store.first_offset = offset;
	op->store.last_to = to;
	op->store.laundering = laundering;
	op->mtime = vnode->vfs_inode.i_mtime;
	op->flags |= AFS_OPERATION_UNINTR;
	op->ops = &afs_store_data_operation;

try_next_key:
	afs_begin_vnode_operation(op);
	afs_wait_for_operation(op);

	switch (op->error) {
	case -EACCES:
	case -EPERM:
	case -ENOKEY:
	case -EKEYEXPIRED:
	case -EKEYREJECTED:
	case -EKEYREVOKED:
		_debug("next");

		ret = afs_get_writeback_key(vnode, &wbk);
		if (ret == 0) {
			key_put(op->key);
			op->key = key_get(wbk->key);
			goto try_next_key;
		}
		break;
	}

	afs_put_wb_key(wbk);
	_leave(" = %d", op->error);
	return afs_put_operation(op);
}

/*
 * Synchronously write back the locked page and any subsequent non-locked dirty
 * pages.
 */
static int afs_write_back_from_locked_page(struct address_space *mapping,
					   struct writeback_control *wbc,
					   struct page *primary_page,
					   pgoff_t final_page)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct page *pages[8], *page;
	unsigned long count, priv;
	unsigned n, offset, to, f, t;
	pgoff_t start, first, last;
	loff_t i_size, end;
	int loop, ret;

	_enter(",%lx", primary_page->index);

	count = 1;
	if (test_set_page_writeback(primary_page))
		BUG();

	/* Find all consecutive lockable dirty pages that have contiguous
	 * written regions, stopping when we find a page that is not
	 * immediately lockable, is not dirty or is missing, or we reach the
	 * end of the range.
	 */
	start = primary_page->index;
	priv = page_private(primary_page);
	offset = afs_page_dirty_from(priv);
	to = afs_page_dirty_to(priv);
	trace_afs_page_dirty(vnode, tracepoint_string("store"),
			     primary_page->index, priv);

	WARN_ON(offset == to);
	if (offset == to)
		trace_afs_page_dirty(vnode, tracepoint_string("WARN"),
				     primary_page->index, priv);

	if (start >= final_page ||
	    (to < PAGE_SIZE && !test_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags)))
		goto no_more;

	start++;
	do {
		_debug("more %lx [%lx]", start, count);
		n = final_page - start + 1;
		if (n > ARRAY_SIZE(pages))
			n = ARRAY_SIZE(pages);
		n = find_get_pages_contig(mapping, start, ARRAY_SIZE(pages), pages);
		_debug("fgpc %u", n);
		if (n == 0)
			goto no_more;
		if (pages[0]->index != start) {
			do {
				put_page(pages[--n]);
			} while (n > 0);
			goto no_more;
		}

		for (loop = 0; loop < n; loop++) {
			page = pages[loop];
			if (to != PAGE_SIZE &&
			    !test_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags))
				break;
			if (page->index > final_page)
				break;
			if (!trylock_page(page))
				break;
			if (!PageDirty(page) || PageWriteback(page)) {
				unlock_page(page);
				break;
			}

			priv = page_private(page);
			f = afs_page_dirty_from(priv);
			t = afs_page_dirty_to(priv);
			if (f != 0 &&
			    !test_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags)) {
				unlock_page(page);
				break;
			}
			to = t;

			trace_afs_page_dirty(vnode, tracepoint_string("store+"),
					     page->index, priv);

			if (!clear_page_dirty_for_io(page))
				BUG();
			if (test_set_page_writeback(page))
				BUG();
			unlock_page(page);
			put_page(page);
		}
		count += loop;
		if (loop < n) {
			for (; loop < n; loop++)
				put_page(pages[loop]);
			goto no_more;
		}

		start += loop;
	} while (start <= final_page && count < 65536);

no_more:
	/* We now have a contiguous set of dirty pages, each with writeback
	 * set; the first page is still locked at this point, but all the rest
	 * have been unlocked.
	 */
	unlock_page(primary_page);

	first = primary_page->index;
	last = first + count - 1;

	end = (loff_t)last * PAGE_SIZE + to;
	i_size = i_size_read(&vnode->vfs_inode);

	_debug("write back %lx[%u..] to %lx[..%u]", first, offset, last, to);
	if (end > i_size)
		to = i_size & ~PAGE_MASK;

	ret = afs_store_data(mapping, first, last, offset, to, false);
	switch (ret) {
	case 0:
		ret = count;
		break;

	default:
		pr_notice("kAFS: Unexpected error from FS.StoreData %d\n", ret);
		fallthrough;
	case -EACCES:
	case -EPERM:
	case -ENOKEY:
	case -EKEYEXPIRED:
	case -EKEYREJECTED:
	case -EKEYREVOKED:
		afs_redirty_pages(wbc, mapping, first, last);
		mapping_set_error(mapping, ret);
		break;

	case -EDQUOT:
	case -ENOSPC:
		afs_redirty_pages(wbc, mapping, first, last);
		mapping_set_error(mapping, -ENOSPC);
		break;

	case -EROFS:
	case -EIO:
	case -EREMOTEIO:
	case -EFBIG:
	case -ENOENT:
	case -ENOMEDIUM:
	case -ENXIO:
		trace_afs_file_error(vnode, ret, afs_file_error_writeback_fail);
		afs_kill_pages(mapping, first, last);
		mapping_set_error(mapping, ret);
		break;
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * write a page back to the server
 * - the caller locked the page for us
 */
int afs_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret;

	_enter("{%lx},", page->index);

	ret = afs_write_back_from_locked_page(page->mapping, wbc, page,
					      wbc->range_end >> PAGE_SHIFT);
	if (ret < 0) {
		_leave(" = %d", ret);
		return 0;
	}

	wbc->nr_to_write -= ret;

	_leave(" = 0");
	return 0;
}

/*
 * write a region of pages back to the server
 */
static int afs_writepages_region(struct address_space *mapping,
				 struct writeback_control *wbc,
				 pgoff_t index, pgoff_t end, pgoff_t *_next)
{
	struct page *page;
	int ret, n;

	_enter(",,%lx,%lx,", index, end);

	do {
		n = find_get_pages_range_tag(mapping, &index, end,
					PAGECACHE_TAG_DIRTY, 1, &page);
		if (!n)
			break;

		_debug("wback %lx", page->index);

		/*
		 * at this point we hold neither the i_pages lock nor the
		 * page lock: the page may be truncated or invalidated
		 * (changing page->mapping to NULL), or even swizzled
		 * back from swapper_space to tmpfs file mapping
		 */
		ret = lock_page_killable(page);
		if (ret < 0) {
			put_page(page);
			_leave(" = %d", ret);
			return ret;
		}

		if (page->mapping != mapping || !PageDirty(page)) {
			unlock_page(page);
			put_page(page);
			continue;
		}

		if (PageWriteback(page)) {
			unlock_page(page);
			if (wbc->sync_mode != WB_SYNC_NONE)
				wait_on_page_writeback(page);
			put_page(page);
			continue;
		}

		if (!clear_page_dirty_for_io(page))
			BUG();
		ret = afs_write_back_from_locked_page(mapping, wbc, page, end);
		put_page(page);
		if (ret < 0) {
			_leave(" = %d", ret);
			return ret;
		}

		wbc->nr_to_write -= ret;

		cond_resched();
	} while (index < end && wbc->nr_to_write > 0);

	*_next = index;
	_leave(" = 0 [%lx]", *_next);
	return 0;
}

/*
 * write some of the pending data back to the server
 */
int afs_writepages(struct address_space *mapping,
		   struct writeback_control *wbc)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	pgoff_t start, end, next;
	int ret;

	_enter("");

	/* We have to be careful as we can end up racing with setattr()
	 * truncating the pagecache since the caller doesn't take a lock here
	 * to prevent it.
	 */
	if (wbc->sync_mode == WB_SYNC_ALL)
		down_read(&vnode->validate_lock);
	else if (!down_read_trylock(&vnode->validate_lock))
		return 0;

	if (wbc->range_cyclic) {
		start = mapping->writeback_index;
		end = -1;
		ret = afs_writepages_region(mapping, wbc, start, end, &next);
		if (start > 0 && wbc->nr_to_write > 0 && ret == 0)
			ret = afs_writepages_region(mapping, wbc, 0, start,
						    &next);
		mapping->writeback_index = next;
	} else if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX) {
		end = (pgoff_t)(LLONG_MAX >> PAGE_SHIFT);
		ret = afs_writepages_region(mapping, wbc, 0, end, &next);
		if (wbc->nr_to_write > 0)
			mapping->writeback_index = next;
	} else {
		start = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		ret = afs_writepages_region(mapping, wbc, start, end, &next);
	}

	up_read(&vnode->validate_lock);
	_leave(" = %d", ret);
	return ret;
}

/*
 * write to an AFS file
 */
ssize_t afs_file_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(iocb->ki_filp));
	ssize_t result;
	size_t count = iov_iter_count(from);

	_enter("{%llx:%llu},{%zu},",
	       vnode->fid.vid, vnode->fid.vnode, count);

	if (IS_SWAPFILE(&vnode->vfs_inode)) {
		printk(KERN_INFO
		       "AFS: Attempt to write to active swap file!\n");
		return -EBUSY;
	}

	if (!count)
		return 0;

	result = generic_file_write_iter(iocb, from);

	_leave(" = %zd", result);
	return result;
}

/*
 * flush any dirty pages for this process, and check for write errors.
 * - the return status from this call provides a reliable indication of
 *   whether any write errors occurred for this process.
 */
int afs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file_inode(file);
	struct afs_vnode *vnode = AFS_FS_I(inode);

	_enter("{%llx:%llu},{n=%pD},%d",
	       vnode->fid.vid, vnode->fid.vnode, file,
	       datasync);

	return file_write_and_wait_range(file, start, end);
}

/*
 * notification that a previously read-only page is about to become writable
 * - if it returns an error, the caller will deliver a bus error signal
 */
vm_fault_t afs_page_mkwrite(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct inode *inode = file_inode(file);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	unsigned long priv;

	_enter("{{%llx:%llu}},{%lx}",
	       vnode->fid.vid, vnode->fid.vnode, vmf->page->index);

	sb_start_pagefault(inode->i_sb);

	/* Wait for the page to be written to the cache before we allow it to
	 * be modified.  We then assume the entire page will need writing back.
	 */
#ifdef CONFIG_AFS_FSCACHE
	fscache_wait_on_page_write(vnode->cache, vmf->page);
#endif

	if (wait_on_page_writeback_killable(vmf->page))
		return VM_FAULT_RETRY;

	if (lock_page_killable(vmf->page) < 0)
		return VM_FAULT_RETRY;

	/* We mustn't change page->private until writeback is complete as that
	 * details the portion of the page we need to write back and we might
	 * need to redirty the page if there's a problem.
	 */
	wait_on_page_writeback(vmf->page);

	priv = afs_page_dirty(0, PAGE_SIZE);
	priv = afs_page_dirty_mmapped(priv);
	trace_afs_page_dirty(vnode, tracepoint_string("mkwrite"),
			     vmf->page->index, priv);
	if (PagePrivate(vmf->page))
		set_page_private(vmf->page, priv);
	else
		attach_page_private(vmf->page, (void *)priv);
	file_update_time(file);

	sb_end_pagefault(inode->i_sb);
	return VM_FAULT_LOCKED;
}

/*
 * Prune the keys cached for writeback.  The caller must hold vnode->wb_lock.
 */
void afs_prune_wb_keys(struct afs_vnode *vnode)
{
	LIST_HEAD(graveyard);
	struct afs_wb_key *wbk, *tmp;

	/* Discard unused keys */
	spin_lock(&vnode->wb_lock);

	if (!mapping_tagged(&vnode->vfs_inode.i_data, PAGECACHE_TAG_WRITEBACK) &&
	    !mapping_tagged(&vnode->vfs_inode.i_data, PAGECACHE_TAG_DIRTY)) {
		list_for_each_entry_safe(wbk, tmp, &vnode->wb_keys, vnode_link) {
			if (refcount_read(&wbk->usage) == 1)
				list_move(&wbk->vnode_link, &graveyard);
		}
	}

	spin_unlock(&vnode->wb_lock);

	while (!list_empty(&graveyard)) {
		wbk = list_entry(graveyard.next, struct afs_wb_key, vnode_link);
		list_del(&wbk->vnode_link);
		afs_put_wb_key(wbk);
	}
}

/*
 * Clean up a page during invalidation.
 */
int afs_launder_page(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	unsigned long priv;
	unsigned int f, t;
	int ret = 0;

	_enter("{%lx}", page->index);

	priv = page_private(page);
	if (clear_page_dirty_for_io(page)) {
		f = 0;
		t = PAGE_SIZE;
		if (PagePrivate(page)) {
			f = afs_page_dirty_from(priv);
			t = afs_page_dirty_to(priv);
		}

		trace_afs_page_dirty(vnode, tracepoint_string("launder"),
				     page->index, priv);
		ret = afs_store_data(mapping, page->index, page->index, t, f, true);
	}

	priv = (unsigned long)detach_page_private(page);
	trace_afs_page_dirty(vnode, tracepoint_string("laundered"),
			     page->index, priv);

#ifdef CONFIG_AFS_FSCACHE
	if (PageFsCache(page)) {
		fscache_wait_on_page_write(vnode->cache, page);
		fscache_uncache_page(vnode->cache, page);
	}
#endif
	return ret;
}
