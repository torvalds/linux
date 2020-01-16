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
	return __set_page_dirty_yesbuffers(page);
}

/*
 * partly or wholly fill a page that's under preparation for writing
 */
static int afs_fill_page(struct afs_vyesde *vyesde, struct key *key,
			 loff_t pos, unsigned int len, struct page *page)
{
	struct afs_read *req;
	size_t p;
	void *data;
	int ret;

	_enter(",,%llu", (unsigned long long)pos);

	if (pos >= vyesde->vfs_iyesde.i_size) {
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

	ret = afs_fetch_data(vyesde, key, req);
	afs_put_read(req);
	if (ret < 0) {
		if (ret == -ENOENT) {
			_debug("got NOENT from server"
			       " - marking file deleted and stale");
			set_bit(AFS_VNODE_DELETED, &vyesde->flags);
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
		    struct page **pagep, void **fsdata)
{
	struct afs_vyesde *vyesde = AFS_FS_I(file_iyesde(file));
	struct page *page;
	struct key *key = afs_file_key(file);
	unsigned long priv;
	unsigned f, from = pos & (PAGE_SIZE - 1);
	unsigned t, to = from + len;
	pgoff_t index = pos >> PAGE_SHIFT;
	int ret;

	_enter("{%llx:%llu},{%lx},%u,%u",
	       vyesde->fid.vid, vyesde->fid.vyesde, index, from, to);

	/* We want to store information about how much of a page is altered in
	 * page->private.
	 */
	BUILD_BUG_ON(PAGE_SIZE > 32768 && sizeof(page->private) < 8);

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;

	if (!PageUptodate(page) && len != PAGE_SIZE) {
		ret = afs_fill_page(vyesde, key, pos & PAGE_MASK, PAGE_SIZE, page);
		if (ret < 0) {
			unlock_page(page);
			put_page(page);
			_leave(" = %d [prep]", ret);
			return ret;
		}
		SetPageUptodate(page);
	}

	/* page won't leak in error case: it eventually gets cleaned off LRU */
	*pagep = page;

try_again:
	/* See if this page is already partially written in a way that we can
	 * merge the new write with.
	 */
	t = f = 0;
	if (PagePrivate(page)) {
		priv = page_private(page);
		f = priv & AFS_PRIV_MAX;
		t = priv >> AFS_PRIV_SHIFT;
		ASSERTCMP(f, <=, t);
	}

	if (f != t) {
		if (PageWriteback(page)) {
			trace_afs_page_dirty(vyesde, tracepoint_string("alrdy"),
					     page->index, priv);
			goto flush_conflicting_write;
		}
		/* If the file is being filled locally, allow inter-write
		 * spaces to be merged into writes.  If it's yest, only write
		 * back what the user gives us.
		 */
		if (!test_bit(AFS_VNODE_NEW_CONTENT, &vyesde->flags) &&
		    (to < f || from > t))
			goto flush_conflicting_write;
		if (from < f)
			f = from;
		if (to > t)
			t = to;
	} else {
		f = from;
		t = to;
	}

	priv = (unsigned long)t << AFS_PRIV_SHIFT;
	priv |= f;
	trace_afs_page_dirty(vyesde, tracepoint_string("begin"),
			     page->index, priv);
	SetPagePrivate(page);
	set_page_private(page, priv);
	_leave(" = 0");
	return 0;

	/* The previous write and this write aren't adjacent or overlapping, so
	 * flush the page out.
	 */
flush_conflicting_write:
	_debug("flush conflict");
	ret = write_one_page(page);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}

	ret = lock_page_killable(page);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}
	goto try_again;
}

/*
 * finalise part of a write to a page
 */
int afs_write_end(struct file *file, struct address_space *mapping,
		  loff_t pos, unsigned len, unsigned copied,
		  struct page *page, void *fsdata)
{
	struct afs_vyesde *vyesde = AFS_FS_I(file_iyesde(file));
	struct key *key = afs_file_key(file);
	loff_t i_size, maybe_i_size;
	int ret;

	_enter("{%llx:%llu},{%lx}",
	       vyesde->fid.vid, vyesde->fid.vyesde, page->index);

	maybe_i_size = pos + copied;

	i_size = i_size_read(&vyesde->vfs_iyesde);
	if (maybe_i_size > i_size) {
		spin_lock(&vyesde->wb_lock);
		i_size = i_size_read(&vyesde->vfs_iyesde);
		if (maybe_i_size > i_size)
			i_size_write(&vyesde->vfs_iyesde, maybe_i_size);
		spin_unlock(&vyesde->wb_lock);
	}

	if (!PageUptodate(page)) {
		if (copied < len) {
			/* Try and load any missing data from the server.  The
			 * unmarshalling routine will take care of clearing any
			 * bits that are beyond the EOF.
			 */
			ret = afs_fill_page(vyesde, key, pos + copied,
					    len - copied, page);
			if (ret < 0)
				goto out;
		}
		SetPageUptodate(page);
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
	struct afs_vyesde *vyesde = AFS_FS_I(mapping->host);
	struct pagevec pv;
	unsigned count, loop;

	_enter("{%llx:%llu},%lx-%lx",
	       vyesde->fid.vid, vyesde->fid.vyesde, first, last);

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
	struct afs_vyesde *vyesde = AFS_FS_I(mapping->host);
	struct pagevec pv;
	unsigned count, loop;

	_enter("{%llx:%llu},%lx-%lx",
	       vyesde->fid.vid, vyesde->fid.vyesde, first, last);

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
static void afs_pages_written_back(struct afs_vyesde *vyesde,
				   pgoff_t first, pgoff_t last)
{
	struct pagevec pv;
	unsigned long priv;
	unsigned count, loop;

	_enter("{%llx:%llu},{%lx-%lx}",
	       vyesde->fid.vid, vyesde->fid.vyesde, first, last);

	pagevec_init(&pv);

	do {
		_debug("done %lx-%lx", first, last);

		count = last - first + 1;
		if (count > PAGEVEC_SIZE)
			count = PAGEVEC_SIZE;
		pv.nr = find_get_pages_contig(vyesde->vfs_iyesde.i_mapping,
					      first, count, pv.pages);
		ASSERTCMP(pv.nr, ==, count);

		for (loop = 0; loop < count; loop++) {
			priv = page_private(pv.pages[loop]);
			trace_afs_page_dirty(vyesde, tracepoint_string("clear"),
					     pv.pages[loop]->index, priv);
			set_page_private(pv.pages[loop], 0);
			end_page_writeback(pv.pages[loop]);
		}
		first += count;
		__pagevec_release(&pv);
	} while (first <= last);

	afs_prune_wb_keys(vyesde);
	_leave("");
}

/*
 * write to a file
 */
static int afs_store_data(struct address_space *mapping,
			  pgoff_t first, pgoff_t last,
			  unsigned offset, unsigned to)
{
	struct afs_vyesde *vyesde = AFS_FS_I(mapping->host);
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	struct afs_wb_key *wbk = NULL;
	struct list_head *p;
	int ret = -ENOKEY, ret2;

	_enter("%s{%llx:%llu.%u},%lx,%lx,%x,%x",
	       vyesde->volume->name,
	       vyesde->fid.vid,
	       vyesde->fid.vyesde,
	       vyesde->fid.unique,
	       first, last, offset, to);

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_NOFS);
	if (!scb)
		return -ENOMEM;

	spin_lock(&vyesde->wb_lock);
	p = vyesde->wb_keys.next;

	/* Iterate through the list looking for a valid key to use. */
try_next_key:
	while (p != &vyesde->wb_keys) {
		wbk = list_entry(p, struct afs_wb_key, vyesde_link);
		_debug("wbk %u", key_serial(wbk->key));
		ret2 = key_validate(wbk->key);
		if (ret2 == 0)
			goto found_key;
		if (ret == -ENOKEY)
			ret = ret2;
		p = p->next;
	}

	spin_unlock(&vyesde->wb_lock);
	afs_put_wb_key(wbk);
	kfree(scb);
	_leave(" = %d [yes keys]", ret);
	return ret;

found_key:
	refcount_inc(&wbk->usage);
	spin_unlock(&vyesde->wb_lock);

	_debug("USE WB KEY %u", key_serial(wbk->key));

	ret = -ERESTARTSYS;
	if (afs_begin_vyesde_operation(&fc, vyesde, wbk->key, false)) {
		afs_dataversion_t data_version = vyesde->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vyesde_cb_break(vyesde);
			afs_fs_store_data(&fc, mapping, first, last, offset, to, scb);
		}

		afs_check_for_remote_deletion(&fc, vyesde);
		afs_vyesde_commit_status(&fc, vyesde, fc.cb_break,
					&data_version, scb);
		if (fc.ac.error == 0)
			afs_pages_written_back(vyesde, first, last);
		ret = afs_end_vyesde_operation(&fc);
	}

	switch (ret) {
	case 0:
		afs_stat_v(vyesde, n_stores);
		atomic_long_add((last * PAGE_SIZE + to) -
				(first * PAGE_SIZE + offset),
				&afs_v2net(vyesde)->n_store_bytes);
		break;
	case -EACCES:
	case -EPERM:
	case -ENOKEY:
	case -EKEYEXPIRED:
	case -EKEYREJECTED:
	case -EKEYREVOKED:
		_debug("next");
		spin_lock(&vyesde->wb_lock);
		p = wbk->vyesde_link.next;
		afs_put_wb_key(wbk);
		goto try_next_key;
	}

	afs_put_wb_key(wbk);
	kfree(scb);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Synchroyesusly write back the locked page and any subsequent yesn-locked dirty
 * pages.
 */
static int afs_write_back_from_locked_page(struct address_space *mapping,
					   struct writeback_control *wbc,
					   struct page *primary_page,
					   pgoff_t final_page)
{
	struct afs_vyesde *vyesde = AFS_FS_I(mapping->host);
	struct page *pages[8], *page;
	unsigned long count, priv;
	unsigned n, offset, to, f, t;
	pgoff_t start, first, last;
	int loop, ret;

	_enter(",%lx", primary_page->index);

	count = 1;
	if (test_set_page_writeback(primary_page))
		BUG();

	/* Find all consecutive lockable dirty pages that have contiguous
	 * written regions, stopping when we find a page that is yest
	 * immediately lockable, is yest dirty or is missing, or we reach the
	 * end of the range.
	 */
	start = primary_page->index;
	priv = page_private(primary_page);
	offset = priv & AFS_PRIV_MAX;
	to = priv >> AFS_PRIV_SHIFT;
	trace_afs_page_dirty(vyesde, tracepoint_string("store"),
			     primary_page->index, priv);

	WARN_ON(offset == to);
	if (offset == to)
		trace_afs_page_dirty(vyesde, tracepoint_string("WARN"),
				     primary_page->index, priv);

	if (start >= final_page ||
	    (to < PAGE_SIZE && !test_bit(AFS_VNODE_NEW_CONTENT, &vyesde->flags)))
		goto yes_more;

	start++;
	do {
		_debug("more %lx [%lx]", start, count);
		n = final_page - start + 1;
		if (n > ARRAY_SIZE(pages))
			n = ARRAY_SIZE(pages);
		n = find_get_pages_contig(mapping, start, ARRAY_SIZE(pages), pages);
		_debug("fgpc %u", n);
		if (n == 0)
			goto yes_more;
		if (pages[0]->index != start) {
			do {
				put_page(pages[--n]);
			} while (n > 0);
			goto yes_more;
		}

		for (loop = 0; loop < n; loop++) {
			page = pages[loop];
			if (to != PAGE_SIZE &&
			    !test_bit(AFS_VNODE_NEW_CONTENT, &vyesde->flags))
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
			f = priv & AFS_PRIV_MAX;
			t = priv >> AFS_PRIV_SHIFT;
			if (f != 0 &&
			    !test_bit(AFS_VNODE_NEW_CONTENT, &vyesde->flags)) {
				unlock_page(page);
				break;
			}
			to = t;

			trace_afs_page_dirty(vyesde, tracepoint_string("store+"),
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
			goto yes_more;
		}

		start += loop;
	} while (start <= final_page && count < 65536);

yes_more:
	/* We yesw have a contiguous set of dirty pages, each with writeback
	 * set; the first page is still locked at this point, but all the rest
	 * have been unlocked.
	 */
	unlock_page(primary_page);

	first = primary_page->index;
	last = first + count - 1;

	_debug("write back %lx[%u..] to %lx[..%u]", first, offset, last, to);

	ret = afs_store_data(mapping, first, last, offset, to);
	switch (ret) {
	case 0:
		ret = count;
		break;

	default:
		pr_yestice("kAFS: Unexpected error from FS.StoreData %d\n", ret);
		/* Fall through */
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
		trace_afs_file_error(vyesde, ret, afs_file_error_writeback_fail);
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
		 * at this point we hold neither the i_pages lock yesr the
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
	pgoff_t start, end, next;
	int ret;

	_enter("");

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

	_leave(" = %d", ret);
	return ret;
}

/*
 * write to an AFS file
 */
ssize_t afs_file_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct afs_vyesde *vyesde = AFS_FS_I(file_iyesde(iocb->ki_filp));
	ssize_t result;
	size_t count = iov_iter_count(from);

	_enter("{%llx:%llu},{%zu},",
	       vyesde->fid.vid, vyesde->fid.vyesde, count);

	if (IS_SWAPFILE(&vyesde->vfs_iyesde)) {
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
	struct iyesde *iyesde = file_iyesde(file);
	struct afs_vyesde *vyesde = AFS_FS_I(iyesde);

	_enter("{%llx:%llu},{n=%pD},%d",
	       vyesde->fid.vid, vyesde->fid.vyesde, file,
	       datasync);

	return file_write_and_wait_range(file, start, end);
}

/*
 * yestification that a previously read-only page is about to become writable
 * - if it returns an error, the caller will deliver a bus error signal
 */
vm_fault_t afs_page_mkwrite(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct iyesde *iyesde = file_iyesde(file);
	struct afs_vyesde *vyesde = AFS_FS_I(iyesde);
	unsigned long priv;

	_enter("{{%llx:%llu}},{%lx}",
	       vyesde->fid.vid, vyesde->fid.vyesde, vmf->page->index);

	sb_start_pagefault(iyesde->i_sb);

	/* Wait for the page to be written to the cache before we allow it to
	 * be modified.  We then assume the entire page will need writing back.
	 */
#ifdef CONFIG_AFS_FSCACHE
	fscache_wait_on_page_write(vyesde->cache, vmf->page);
#endif

	if (PageWriteback(vmf->page) &&
	    wait_on_page_bit_killable(vmf->page, PG_writeback) < 0)
		return VM_FAULT_RETRY;

	if (lock_page_killable(vmf->page) < 0)
		return VM_FAULT_RETRY;

	/* We mustn't change page->private until writeback is complete as that
	 * details the portion of the page we need to write back and we might
	 * need to redirty the page if there's a problem.
	 */
	wait_on_page_writeback(vmf->page);

	priv = (unsigned long)PAGE_SIZE << AFS_PRIV_SHIFT; /* To */
	priv |= 0; /* From */
	trace_afs_page_dirty(vyesde, tracepoint_string("mkwrite"),
			     vmf->page->index, priv);
	SetPagePrivate(vmf->page);
	set_page_private(vmf->page, priv);

	sb_end_pagefault(iyesde->i_sb);
	return VM_FAULT_LOCKED;
}

/*
 * Prune the keys cached for writeback.  The caller must hold vyesde->wb_lock.
 */
void afs_prune_wb_keys(struct afs_vyesde *vyesde)
{
	LIST_HEAD(graveyard);
	struct afs_wb_key *wbk, *tmp;

	/* Discard unused keys */
	spin_lock(&vyesde->wb_lock);

	if (!mapping_tagged(&vyesde->vfs_iyesde.i_data, PAGECACHE_TAG_WRITEBACK) &&
	    !mapping_tagged(&vyesde->vfs_iyesde.i_data, PAGECACHE_TAG_DIRTY)) {
		list_for_each_entry_safe(wbk, tmp, &vyesde->wb_keys, vyesde_link) {
			if (refcount_read(&wbk->usage) == 1)
				list_move(&wbk->vyesde_link, &graveyard);
		}
	}

	spin_unlock(&vyesde->wb_lock);

	while (!list_empty(&graveyard)) {
		wbk = list_entry(graveyard.next, struct afs_wb_key, vyesde_link);
		list_del(&wbk->vyesde_link);
		afs_put_wb_key(wbk);
	}
}

/*
 * Clean up a page during invalidation.
 */
int afs_launder_page(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct afs_vyesde *vyesde = AFS_FS_I(mapping->host);
	unsigned long priv;
	unsigned int f, t;
	int ret = 0;

	_enter("{%lx}", page->index);

	priv = page_private(page);
	if (clear_page_dirty_for_io(page)) {
		f = 0;
		t = PAGE_SIZE;
		if (PagePrivate(page)) {
			f = priv & AFS_PRIV_MAX;
			t = priv >> AFS_PRIV_SHIFT;
		}

		trace_afs_page_dirty(vyesde, tracepoint_string("launder"),
				     page->index, priv);
		ret = afs_store_data(mapping, page->index, page->index, t, f);
	}

	trace_afs_page_dirty(vyesde, tracepoint_string("laundered"),
			     page->index, priv);
	set_page_private(page, 0);
	ClearPagePrivate(page);

#ifdef CONFIG_AFS_FSCACHE
	if (PageFsCache(page)) {
		fscache_wait_on_page_write(vyesde->cache, page);
		fscache_uncache_page(vyesde->cache, page);
	}
#endif
	return ret;
}
