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
#include <linux/netfs.h>
#include "internal.h"

static int afs_writepages_region(struct address_space *mapping,
				 struct writeback_control *wbc,
				 loff_t start, loff_t end, loff_t *_next,
				 bool max_one_loop);

static void afs_write_to_cache(struct afs_vnode *vnode, loff_t start, size_t len,
			       loff_t i_size, bool caching);

#ifdef CONFIG_AFS_FSCACHE
/*
 * Mark a page as having been made dirty and thus needing writeback.  We also
 * need to pin the cache object to write back to.
 */
bool afs_dirty_folio(struct address_space *mapping, struct folio *folio)
{
	return fscache_dirty_folio(mapping, folio,
				afs_vnode_cache(AFS_FS_I(mapping->host)));
}
static void afs_folio_start_fscache(bool caching, struct folio *folio)
{
	if (caching)
		folio_start_fscache(folio);
}
#else
static void afs_folio_start_fscache(bool caching, struct folio *folio)
{
}
#endif

/*
 * Flush out a conflicting write.  This may extend the write to the surrounding
 * pages if also dirty and contiguous to the conflicting region..
 */
static int afs_flush_conflicting_write(struct address_space *mapping,
				       struct folio *folio)
{
	struct writeback_control wbc = {
		.sync_mode	= WB_SYNC_ALL,
		.nr_to_write	= LONG_MAX,
		.range_start	= folio_pos(folio),
		.range_end	= LLONG_MAX,
	};
	loff_t next;

	return afs_writepages_region(mapping, &wbc, folio_pos(folio), LLONG_MAX,
				     &next, true);
}

/*
 * prepare to perform part of a write to a page
 */
int afs_write_begin(struct file *file, struct address_space *mapping,
		    loff_t pos, unsigned len,
		    struct page **_page, void **fsdata)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(file));
	struct folio *folio;
	unsigned long priv;
	unsigned f, from;
	unsigned t, to;
	pgoff_t index;
	int ret;

	_enter("{%llx:%llu},%llx,%x",
	       vnode->fid.vid, vnode->fid.vnode, pos, len);

	/* Prefetch area to be written into the cache if we're caching this
	 * file.  We need to do this before we get a lock on the page in case
	 * there's more than one writer competing for the same cache block.
	 */
	ret = netfs_write_begin(&vnode->netfs, file, mapping, pos, len, &folio, fsdata);
	if (ret < 0)
		return ret;

	index = folio_index(folio);
	from = pos - index * PAGE_SIZE;
	to = from + len;

try_again:
	/* See if this page is already partially written in a way that we can
	 * merge the new write with.
	 */
	if (folio_test_private(folio)) {
		priv = (unsigned long)folio_get_private(folio);
		f = afs_folio_dirty_from(folio, priv);
		t = afs_folio_dirty_to(folio, priv);
		ASSERTCMP(f, <=, t);

		if (folio_test_writeback(folio)) {
			trace_afs_folio_dirty(vnode, tracepoint_string("alrdy"), folio);
			folio_unlock(folio);
			goto wait_for_writeback;
		}
		/* If the file is being filled locally, allow inter-write
		 * spaces to be merged into writes.  If it's not, only write
		 * back what the user gives us.
		 */
		if (!test_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags) &&
		    (to < f || from > t))
			goto flush_conflicting_write;
	}

	*_page = folio_file_page(folio, pos / PAGE_SIZE);
	_leave(" = 0");
	return 0;

	/* The previous write and this write aren't adjacent or overlapping, so
	 * flush the page out.
	 */
flush_conflicting_write:
	trace_afs_folio_dirty(vnode, tracepoint_string("confl"), folio);
	folio_unlock(folio);

	ret = afs_flush_conflicting_write(mapping, folio);
	if (ret < 0)
		goto error;

wait_for_writeback:
	ret = folio_wait_writeback_killable(folio);
	if (ret < 0)
		goto error;

	ret = folio_lock_killable(folio);
	if (ret < 0)
		goto error;
	goto try_again;

error:
	folio_put(folio);
	_leave(" = %d", ret);
	return ret;
}

/*
 * finalise part of a write to a page
 */
int afs_write_end(struct file *file, struct address_space *mapping,
		  loff_t pos, unsigned len, unsigned copied,
		  struct page *subpage, void *fsdata)
{
	struct folio *folio = page_folio(subpage);
	struct afs_vnode *vnode = AFS_FS_I(file_inode(file));
	unsigned long priv;
	unsigned int f, from = offset_in_folio(folio, pos);
	unsigned int t, to = from + copied;
	loff_t i_size, write_end_pos;

	_enter("{%llx:%llu},{%lx}",
	       vnode->fid.vid, vnode->fid.vnode, folio_index(folio));

	if (!folio_test_uptodate(folio)) {
		if (copied < len) {
			copied = 0;
			goto out;
		}

		folio_mark_uptodate(folio);
	}

	if (copied == 0)
		goto out;

	write_end_pos = pos + copied;

	i_size = i_size_read(&vnode->netfs.inode);
	if (write_end_pos > i_size) {
		write_seqlock(&vnode->cb_lock);
		i_size = i_size_read(&vnode->netfs.inode);
		if (write_end_pos > i_size)
			afs_set_i_size(vnode, write_end_pos);
		write_sequnlock(&vnode->cb_lock);
		fscache_update_cookie(afs_vnode_cache(vnode), NULL, &write_end_pos);
	}

	if (folio_test_private(folio)) {
		priv = (unsigned long)folio_get_private(folio);
		f = afs_folio_dirty_from(folio, priv);
		t = afs_folio_dirty_to(folio, priv);
		if (from < f)
			f = from;
		if (to > t)
			t = to;
		priv = afs_folio_dirty(folio, f, t);
		folio_change_private(folio, (void *)priv);
		trace_afs_folio_dirty(vnode, tracepoint_string("dirty+"), folio);
	} else {
		priv = afs_folio_dirty(folio, from, to);
		folio_attach_private(folio, (void *)priv);
		trace_afs_folio_dirty(vnode, tracepoint_string("dirty"), folio);
	}

	if (folio_mark_dirty(folio))
		_debug("dirtied %lx", folio_index(folio));

out:
	folio_unlock(folio);
	folio_put(folio);
	return copied;
}

/*
 * kill all the pages in the given range
 */
static void afs_kill_pages(struct address_space *mapping,
			   loff_t start, loff_t len)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct folio *folio;
	pgoff_t index = start / PAGE_SIZE;
	pgoff_t last = (start + len - 1) / PAGE_SIZE, next;

	_enter("{%llx:%llu},%llx @%llx",
	       vnode->fid.vid, vnode->fid.vnode, len, start);

	do {
		_debug("kill %lx (to %lx)", index, last);

		folio = filemap_get_folio(mapping, index);
		if (IS_ERR(folio)) {
			next = index + 1;
			continue;
		}

		next = folio_next_index(folio);

		folio_clear_uptodate(folio);
		folio_end_writeback(folio);
		folio_lock(folio);
		generic_error_remove_page(mapping, &folio->page);
		folio_unlock(folio);
		folio_put(folio);

	} while (index = next, index <= last);

	_leave("");
}

/*
 * Redirty all the pages in a given range.
 */
static void afs_redirty_pages(struct writeback_control *wbc,
			      struct address_space *mapping,
			      loff_t start, loff_t len)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct folio *folio;
	pgoff_t index = start / PAGE_SIZE;
	pgoff_t last = (start + len - 1) / PAGE_SIZE, next;

	_enter("{%llx:%llu},%llx @%llx",
	       vnode->fid.vid, vnode->fid.vnode, len, start);

	do {
		_debug("redirty %llx @%llx", len, start);

		folio = filemap_get_folio(mapping, index);
		if (IS_ERR(folio)) {
			next = index + 1;
			continue;
		}

		next = index + folio_nr_pages(folio);
		folio_redirty_for_writepage(wbc, folio);
		folio_end_writeback(folio);
		folio_put(folio);
	} while (index = next, index <= last);

	_leave("");
}

/*
 * completion of write to server
 */
static void afs_pages_written_back(struct afs_vnode *vnode, loff_t start, unsigned int len)
{
	struct address_space *mapping = vnode->netfs.inode.i_mapping;
	struct folio *folio;
	pgoff_t end;

	XA_STATE(xas, &mapping->i_pages, start / PAGE_SIZE);

	_enter("{%llx:%llu},{%x @%llx}",
	       vnode->fid.vid, vnode->fid.vnode, len, start);

	rcu_read_lock();

	end = (start + len - 1) / PAGE_SIZE;
	xas_for_each(&xas, folio, end) {
		if (!folio_test_writeback(folio)) {
			kdebug("bad %x @%llx page %lx %lx",
			       len, start, folio_index(folio), end);
			ASSERT(folio_test_writeback(folio));
		}

		trace_afs_folio_dirty(vnode, tracepoint_string("clear"), folio);
		folio_detach_private(folio);
		folio_end_writeback(folio);
	}

	rcu_read_unlock();

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
	if (!afs_op_error(op)) {
		if (!op->store.laundering)
			afs_pages_written_back(vnode, op->store.pos, op->store.size);
		afs_stat_v(vnode, n_stores);
		atomic_long_add(op->store.size, &afs_v2net(vnode)->n_store_bytes);
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
static int afs_store_data(struct afs_vnode *vnode, struct iov_iter *iter, loff_t pos,
			  bool laundering)
{
	struct afs_operation *op;
	struct afs_wb_key *wbk = NULL;
	loff_t size = iov_iter_count(iter);
	int ret = -ENOKEY;

	_enter("%s{%llx:%llu.%u},%llx,%llx",
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       size, pos);

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
	op->file[0].modification = true;
	op->store.pos = pos;
	op->store.size = size;
	op->store.laundering = laundering;
	op->flags |= AFS_OPERATION_UNINTR;
	op->ops = &afs_store_data_operation;

try_next_key:
	afs_begin_vnode_operation(op);

	op->store.write_iter = iter;
	op->store.i_size = max(pos + size, vnode->netfs.remote_i_size);
	op->mtime = inode_get_mtime(&vnode->netfs.inode);

	afs_wait_for_operation(op);

	switch (afs_op_error(op)) {
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
	_leave(" = %d", afs_op_error(op));
	return afs_put_operation(op);
}

/*
 * Extend the region to be written back to include subsequent contiguously
 * dirty pages if possible, but don't sleep while doing so.
 *
 * If this page holds new content, then we can include filler zeros in the
 * writeback.
 */
static void afs_extend_writeback(struct address_space *mapping,
				 struct afs_vnode *vnode,
				 long *_count,
				 loff_t start,
				 loff_t max_len,
				 bool new_content,
				 bool caching,
				 unsigned int *_len)
{
	struct folio_batch fbatch;
	struct folio *folio;
	unsigned long priv;
	unsigned int psize, filler = 0;
	unsigned int f, t;
	loff_t len = *_len;
	pgoff_t index = (start + len) / PAGE_SIZE;
	bool stop = true;
	unsigned int i;

	XA_STATE(xas, &mapping->i_pages, index);
	folio_batch_init(&fbatch);

	do {
		/* Firstly, we gather up a batch of contiguous dirty pages
		 * under the RCU read lock - but we can't clear the dirty flags
		 * there if any of those pages are mapped.
		 */
		rcu_read_lock();

		xas_for_each(&xas, folio, ULONG_MAX) {
			stop = true;
			if (xas_retry(&xas, folio))
				continue;
			if (xa_is_value(folio))
				break;
			if (folio_index(folio) != index)
				break;

			if (!folio_try_get_rcu(folio)) {
				xas_reset(&xas);
				continue;
			}

			/* Has the page moved or been split? */
			if (unlikely(folio != xas_reload(&xas))) {
				folio_put(folio);
				break;
			}

			if (!folio_trylock(folio)) {
				folio_put(folio);
				break;
			}
			if (!folio_test_dirty(folio) ||
			    folio_test_writeback(folio) ||
			    folio_test_fscache(folio)) {
				folio_unlock(folio);
				folio_put(folio);
				break;
			}

			psize = folio_size(folio);
			priv = (unsigned long)folio_get_private(folio);
			f = afs_folio_dirty_from(folio, priv);
			t = afs_folio_dirty_to(folio, priv);
			if (f != 0 && !new_content) {
				folio_unlock(folio);
				folio_put(folio);
				break;
			}

			len += filler + t;
			filler = psize - t;
			if (len >= max_len || *_count <= 0)
				stop = true;
			else if (t == psize || new_content)
				stop = false;

			index += folio_nr_pages(folio);
			if (!folio_batch_add(&fbatch, folio))
				break;
			if (stop)
				break;
		}

		if (!stop)
			xas_pause(&xas);
		rcu_read_unlock();

		/* Now, if we obtained any folios, we can shift them to being
		 * writable and mark them for caching.
		 */
		if (!folio_batch_count(&fbatch))
			break;

		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			folio = fbatch.folios[i];
			trace_afs_folio_dirty(vnode, tracepoint_string("store+"), folio);

			if (!folio_clear_dirty_for_io(folio))
				BUG();
			if (folio_start_writeback(folio))
				BUG();
			afs_folio_start_fscache(caching, folio);

			*_count -= folio_nr_pages(folio);
			folio_unlock(folio);
		}

		folio_batch_release(&fbatch);
		cond_resched();
	} while (!stop);

	*_len = len;
}

/*
 * Synchronously write back the locked page and any subsequent non-locked dirty
 * pages.
 */
static ssize_t afs_write_back_from_locked_folio(struct address_space *mapping,
						struct writeback_control *wbc,
						struct folio *folio,
						loff_t start, loff_t end)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct iov_iter iter;
	unsigned long priv;
	unsigned int offset, to, len, max_len;
	loff_t i_size = i_size_read(&vnode->netfs.inode);
	bool new_content = test_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags);
	bool caching = fscache_cookie_enabled(afs_vnode_cache(vnode));
	long count = wbc->nr_to_write;
	int ret;

	_enter(",%lx,%llx-%llx", folio_index(folio), start, end);

	if (folio_start_writeback(folio))
		BUG();
	afs_folio_start_fscache(caching, folio);

	count -= folio_nr_pages(folio);

	/* Find all consecutive lockable dirty pages that have contiguous
	 * written regions, stopping when we find a page that is not
	 * immediately lockable, is not dirty or is missing, or we reach the
	 * end of the range.
	 */
	priv = (unsigned long)folio_get_private(folio);
	offset = afs_folio_dirty_from(folio, priv);
	to = afs_folio_dirty_to(folio, priv);
	trace_afs_folio_dirty(vnode, tracepoint_string("store"), folio);

	len = to - offset;
	start += offset;
	if (start < i_size) {
		/* Trim the write to the EOF; the extra data is ignored.  Also
		 * put an upper limit on the size of a single storedata op.
		 */
		max_len = 65536 * 4096;
		max_len = min_t(unsigned long long, max_len, end - start + 1);
		max_len = min_t(unsigned long long, max_len, i_size - start);

		if (len < max_len &&
		    (to == folio_size(folio) || new_content))
			afs_extend_writeback(mapping, vnode, &count,
					     start, max_len, new_content,
					     caching, &len);
		len = min_t(loff_t, len, max_len);
	}

	/* We now have a contiguous set of dirty pages, each with writeback
	 * set; the first page is still locked at this point, but all the rest
	 * have been unlocked.
	 */
	folio_unlock(folio);

	if (start < i_size) {
		_debug("write back %x @%llx [%llx]", len, start, i_size);

		/* Speculatively write to the cache.  We have to fix this up
		 * later if the store fails.
		 */
		afs_write_to_cache(vnode, start, len, i_size, caching);

		iov_iter_xarray(&iter, ITER_SOURCE, &mapping->i_pages, start, len);
		ret = afs_store_data(vnode, &iter, start, false);
	} else {
		_debug("write discard %x @%llx [%llx]", len, start, i_size);

		/* The dirty region was entirely beyond the EOF. */
		fscache_clear_page_bits(mapping, start, len, caching);
		afs_pages_written_back(vnode, start, len);
		ret = 0;
	}

	switch (ret) {
	case 0:
		wbc->nr_to_write = count;
		ret = len;
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
	case -ENETRESET:
		afs_redirty_pages(wbc, mapping, start, len);
		mapping_set_error(mapping, ret);
		break;

	case -EDQUOT:
	case -ENOSPC:
		afs_redirty_pages(wbc, mapping, start, len);
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
		afs_kill_pages(mapping, start, len);
		mapping_set_error(mapping, ret);
		break;
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * write a region of pages back to the server
 */
static int afs_writepages_region(struct address_space *mapping,
				 struct writeback_control *wbc,
				 loff_t start, loff_t end, loff_t *_next,
				 bool max_one_loop)
{
	struct folio *folio;
	struct folio_batch fbatch;
	ssize_t ret;
	unsigned int i;
	int n, skips = 0;

	_enter("%llx,%llx,", start, end);
	folio_batch_init(&fbatch);

	do {
		pgoff_t index = start / PAGE_SIZE;

		n = filemap_get_folios_tag(mapping, &index, end / PAGE_SIZE,
					PAGECACHE_TAG_DIRTY, &fbatch);

		if (!n)
			break;
		for (i = 0; i < n; i++) {
			folio = fbatch.folios[i];
			start = folio_pos(folio); /* May regress with THPs */

			_debug("wback %lx", folio_index(folio));

			/* At this point we hold neither the i_pages lock nor the
			 * page lock: the page may be truncated or invalidated
			 * (changing page->mapping to NULL), or even swizzled
			 * back from swapper_space to tmpfs file mapping
			 */
try_again:
			if (wbc->sync_mode != WB_SYNC_NONE) {
				ret = folio_lock_killable(folio);
				if (ret < 0) {
					folio_batch_release(&fbatch);
					return ret;
				}
			} else {
				if (!folio_trylock(folio))
					continue;
			}

			if (folio->mapping != mapping ||
			    !folio_test_dirty(folio)) {
				start += folio_size(folio);
				folio_unlock(folio);
				continue;
			}

			if (folio_test_writeback(folio) ||
			    folio_test_fscache(folio)) {
				folio_unlock(folio);
				if (wbc->sync_mode != WB_SYNC_NONE) {
					folio_wait_writeback(folio);
#ifdef CONFIG_AFS_FSCACHE
					folio_wait_fscache(folio);
#endif
					goto try_again;
				}

				start += folio_size(folio);
				if (wbc->sync_mode == WB_SYNC_NONE) {
					if (skips >= 5 || need_resched()) {
						*_next = start;
						folio_batch_release(&fbatch);
						_leave(" = 0 [%llx]", *_next);
						return 0;
					}
					skips++;
				}
				continue;
			}

			if (!folio_clear_dirty_for_io(folio))
				BUG();
			ret = afs_write_back_from_locked_folio(mapping, wbc,
					folio, start, end);
			if (ret < 0) {
				_leave(" = %zd", ret);
				folio_batch_release(&fbatch);
				return ret;
			}

			start += ret;
		}

		folio_batch_release(&fbatch);
		cond_resched();
	} while (wbc->nr_to_write > 0);

	*_next = start;
	_leave(" = 0 [%llx]", *_next);
	return 0;
}

/*
 * write some of the pending data back to the server
 */
int afs_writepages(struct address_space *mapping,
		   struct writeback_control *wbc)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	loff_t start, next;
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
		start = mapping->writeback_index * PAGE_SIZE;
		ret = afs_writepages_region(mapping, wbc, start, LLONG_MAX,
					    &next, false);
		if (ret == 0) {
			mapping->writeback_index = next / PAGE_SIZE;
			if (start > 0 && wbc->nr_to_write > 0) {
				ret = afs_writepages_region(mapping, wbc, 0,
							    start, &next, false);
				if (ret == 0)
					mapping->writeback_index =
						next / PAGE_SIZE;
			}
		}
	} else if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX) {
		ret = afs_writepages_region(mapping, wbc, 0, LLONG_MAX,
					    &next, false);
		if (wbc->nr_to_write > 0 && ret == 0)
			mapping->writeback_index = next / PAGE_SIZE;
	} else {
		ret = afs_writepages_region(mapping, wbc,
					    wbc->range_start, wbc->range_end,
					    &next, false);
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
	struct afs_file *af = iocb->ki_filp->private_data;
	ssize_t result;
	size_t count = iov_iter_count(from);

	_enter("{%llx:%llu},{%zu},",
	       vnode->fid.vid, vnode->fid.vnode, count);

	if (IS_SWAPFILE(&vnode->netfs.inode)) {
		printk(KERN_INFO
		       "AFS: Attempt to write to active swap file!\n");
		return -EBUSY;
	}

	if (!count)
		return 0;

	result = afs_validate(vnode, af->key);
	if (result < 0)
		return result;

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
	struct afs_vnode *vnode = AFS_FS_I(file_inode(file));
	struct afs_file *af = file->private_data;
	int ret;

	_enter("{%llx:%llu},{n=%pD},%d",
	       vnode->fid.vid, vnode->fid.vnode, file,
	       datasync);

	ret = afs_validate(vnode, af->key);
	if (ret < 0)
		return ret;

	return file_write_and_wait_range(file, start, end);
}

/*
 * notification that a previously read-only page is about to become writable
 * - if it returns an error, the caller will deliver a bus error signal
 */
vm_fault_t afs_page_mkwrite(struct vm_fault *vmf)
{
	struct folio *folio = page_folio(vmf->page);
	struct file *file = vmf->vma->vm_file;
	struct inode *inode = file_inode(file);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_file *af = file->private_data;
	unsigned long priv;
	vm_fault_t ret = VM_FAULT_RETRY;

	_enter("{{%llx:%llu}},{%lx}", vnode->fid.vid, vnode->fid.vnode, folio_index(folio));

	afs_validate(vnode, af->key);

	sb_start_pagefault(inode->i_sb);

	/* Wait for the page to be written to the cache before we allow it to
	 * be modified.  We then assume the entire page will need writing back.
	 */
#ifdef CONFIG_AFS_FSCACHE
	if (folio_test_fscache(folio) &&
	    folio_wait_fscache_killable(folio) < 0)
		goto out;
#endif

	if (folio_wait_writeback_killable(folio))
		goto out;

	if (folio_lock_killable(folio) < 0)
		goto out;

	/* We mustn't change folio->private until writeback is complete as that
	 * details the portion of the page we need to write back and we might
	 * need to redirty the page if there's a problem.
	 */
	if (folio_wait_writeback_killable(folio) < 0) {
		folio_unlock(folio);
		goto out;
	}

	priv = afs_folio_dirty(folio, 0, folio_size(folio));
	priv = afs_folio_dirty_mmapped(priv);
	if (folio_test_private(folio)) {
		folio_change_private(folio, (void *)priv);
		trace_afs_folio_dirty(vnode, tracepoint_string("mkwrite+"), folio);
	} else {
		folio_attach_private(folio, (void *)priv);
		trace_afs_folio_dirty(vnode, tracepoint_string("mkwrite"), folio);
	}
	file_update_time(file);

	ret = VM_FAULT_LOCKED;
out:
	sb_end_pagefault(inode->i_sb);
	return ret;
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

	if (!mapping_tagged(&vnode->netfs.inode.i_data, PAGECACHE_TAG_WRITEBACK) &&
	    !mapping_tagged(&vnode->netfs.inode.i_data, PAGECACHE_TAG_DIRTY)) {
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
int afs_launder_folio(struct folio *folio)
{
	struct afs_vnode *vnode = AFS_FS_I(folio_inode(folio));
	struct iov_iter iter;
	struct bio_vec bv;
	unsigned long priv;
	unsigned int f, t;
	int ret = 0;

	_enter("{%lx}", folio->index);

	priv = (unsigned long)folio_get_private(folio);
	if (folio_clear_dirty_for_io(folio)) {
		f = 0;
		t = folio_size(folio);
		if (folio_test_private(folio)) {
			f = afs_folio_dirty_from(folio, priv);
			t = afs_folio_dirty_to(folio, priv);
		}

		bvec_set_folio(&bv, folio, t - f, f);
		iov_iter_bvec(&iter, ITER_SOURCE, &bv, 1, bv.bv_len);

		trace_afs_folio_dirty(vnode, tracepoint_string("launder"), folio);
		ret = afs_store_data(vnode, &iter, folio_pos(folio) + f, true);
	}

	trace_afs_folio_dirty(vnode, tracepoint_string("laundered"), folio);
	folio_detach_private(folio);
	folio_wait_fscache(folio);
	return ret;
}

/*
 * Deal with the completion of writing the data to the cache.
 */
static void afs_write_to_cache_done(void *priv, ssize_t transferred_or_error,
				    bool was_async)
{
	struct afs_vnode *vnode = priv;

	if (IS_ERR_VALUE(transferred_or_error) &&
	    transferred_or_error != -ENOBUFS)
		afs_invalidate_cache(vnode, 0);
}

/*
 * Save the write to the cache also.
 */
static void afs_write_to_cache(struct afs_vnode *vnode,
			       loff_t start, size_t len, loff_t i_size,
			       bool caching)
{
	fscache_write_to_cache(afs_vnode_cache(vnode),
			       vnode->netfs.inode.i_mapping, start, len, i_size,
			       afs_write_to_cache_done, vnode, caching);
}
