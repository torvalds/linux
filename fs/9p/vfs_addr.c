// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contians vfs address (mmap) ops for 9P2000.
 *
 *  Copyright (C) 2005 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/pagemap.h>
#include <linux/idr.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/netfs.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "cache.h"
#include "fid.h"

/**
 * v9fs_req_issue_op - Issue a read from 9P
 * @subreq: The read to make
 */
static void v9fs_req_issue_op(struct netfs_read_subrequest *subreq)
{
	struct netfs_read_request *rreq = subreq->rreq;
	struct p9_fid *fid = rreq->netfs_priv;
	struct iov_iter to;
	loff_t pos = subreq->start + subreq->transferred;
	size_t len = subreq->len   - subreq->transferred;
	int total, err;

	iov_iter_xarray(&to, READ, &rreq->mapping->i_pages, pos, len);

	total = p9_client_read(fid, pos, &to, &err);
	netfs_subreq_terminated(subreq, err ?: total, false);
}

/**
 * v9fs_init_rreq - Initialise a read request
 * @rreq: The read request
 * @file: The file being read from
 */
static void v9fs_init_rreq(struct netfs_read_request *rreq, struct file *file)
{
	struct p9_fid *fid = file->private_data;

	refcount_inc(&fid->count);
	rreq->netfs_priv = fid;
}

/**
 * v9fs_req_cleanup - Cleanup request initialized by v9fs_init_rreq
 * @mapping: unused mapping of request to cleanup
 * @priv: private data to cleanup, a fid, guaranted non-null.
 */
static void v9fs_req_cleanup(struct address_space *mapping, void *priv)
{
	struct p9_fid *fid = priv;

	p9_client_clunk(fid);
}

/**
 * v9fs_is_cache_enabled - Determine if caching is enabled for an inode
 * @inode: The inode to check
 */
static bool v9fs_is_cache_enabled(struct inode *inode)
{
	struct fscache_cookie *cookie = v9fs_inode_cookie(V9FS_I(inode));

	return fscache_cookie_enabled(cookie) && !hlist_empty(&cookie->backing_objects);
}

/**
 * v9fs_begin_cache_operation - Begin a cache operation for a read
 * @rreq: The read request
 */
static int v9fs_begin_cache_operation(struct netfs_read_request *rreq)
{
	struct fscache_cookie *cookie = v9fs_inode_cookie(V9FS_I(rreq->inode));

	return fscache_begin_read_operation(rreq, cookie);
}

static const struct netfs_read_request_ops v9fs_req_ops = {
	.init_rreq		= v9fs_init_rreq,
	.is_cache_enabled	= v9fs_is_cache_enabled,
	.begin_cache_operation	= v9fs_begin_cache_operation,
	.issue_op		= v9fs_req_issue_op,
	.cleanup		= v9fs_req_cleanup,
};

/**
 * v9fs_vfs_readpage - read an entire page in from 9P
 * @file: file being read
 * @page: structure to page
 *
 */
static int v9fs_vfs_readpage(struct file *file, struct page *page)
{
	return netfs_readpage(file, page, &v9fs_req_ops, NULL);
}

/**
 * v9fs_vfs_readahead - read a set of pages from 9P
 * @ractl: The readahead parameters
 */
static void v9fs_vfs_readahead(struct readahead_control *ractl)
{
	netfs_readahead(ractl, &v9fs_req_ops, NULL);
}

/**
 * v9fs_release_page - release the private state associated with a page
 * @page: The page to be released
 * @gfp: The caller's allocation restrictions
 *
 * Returns 1 if the page can be released, false otherwise.
 */

static int v9fs_release_page(struct page *page, gfp_t gfp)
{
	if (PagePrivate(page))
		return 0;
#ifdef CONFIG_9P_FSCACHE
	if (PageFsCache(page)) {
		if (!(gfp & __GFP_DIRECT_RECLAIM) || !(gfp & __GFP_FS))
			return 0;
		wait_on_page_fscache(page);
	}
#endif
	return 1;
}

/**
 * v9fs_invalidate_page - Invalidate a page completely or partially
 * @page: The page to be invalidated
 * @offset: offset of the invalidated region
 * @length: length of the invalidated region
 */

static void v9fs_invalidate_page(struct page *page, unsigned int offset,
				 unsigned int length)
{
	wait_on_page_fscache(page);
}

static int v9fs_vfs_writepage_locked(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct v9fs_inode *v9inode = V9FS_I(inode);
	loff_t start = page_offset(page);
	loff_t size = i_size_read(inode);
	struct iov_iter from;
	int err, len;

	if (page->index == size >> PAGE_SHIFT)
		len = size & ~PAGE_MASK;
	else
		len = PAGE_SIZE;

	iov_iter_xarray(&from, WRITE, &page->mapping->i_pages, start, len);

	/* We should have writeback_fid always set */
	BUG_ON(!v9inode->writeback_fid);

	set_page_writeback(page);

	p9_client_write(v9inode->writeback_fid, start, &from, &err);

	end_page_writeback(page);
	return err;
}

static int v9fs_vfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int retval;

	p9_debug(P9_DEBUG_VFS, "page %p\n", page);

	retval = v9fs_vfs_writepage_locked(page);
	if (retval < 0) {
		if (retval == -EAGAIN) {
			redirty_page_for_writepage(wbc, page);
			retval = 0;
		} else {
			SetPageError(page);
			mapping_set_error(page->mapping, retval);
		}
	} else
		retval = 0;

	unlock_page(page);
	return retval;
}

/**
 * v9fs_launder_page - Writeback a dirty page
 * @page: The page to be cleaned up
 *
 * Returns 0 on success.
 */

static int v9fs_launder_page(struct page *page)
{
	int retval;

	if (clear_page_dirty_for_io(page)) {
		retval = v9fs_vfs_writepage_locked(page);
		if (retval)
			return retval;
	}
	wait_on_page_fscache(page);
	return 0;
}

/**
 * v9fs_direct_IO - 9P address space operation for direct I/O
 * @iocb: target I/O control block
 * @iter: The data/buffer to use
 *
 * The presence of v9fs_direct_IO() in the address space ops vector
 * allowes open() O_DIRECT flags which would have failed otherwise.
 *
 * In the non-cached mode, we shunt off direct read and write requests before
 * the VFS gets them, so this method should never be called.
 *
 * Direct IO is not 'yet' supported in the cached mode. Hence when
 * this routine is called through generic_file_aio_read(), the read/write fails
 * with an error.
 *
 */
static ssize_t
v9fs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	loff_t pos = iocb->ki_pos;
	ssize_t n;
	int err = 0;

	if (iov_iter_rw(iter) == WRITE) {
		n = p9_client_write(file->private_data, pos, iter, &err);
		if (n) {
			struct inode *inode = file_inode(file);
			loff_t i_size = i_size_read(inode);

			if (pos + n > i_size)
				inode_add_bytes(inode, pos + n - i_size);
		}
	} else {
		n = p9_client_read(file->private_data, pos, iter, &err);
	}
	return n ? n : err;
}

static int v9fs_write_begin(struct file *filp, struct address_space *mapping,
			    loff_t pos, unsigned int len, unsigned int flags,
			    struct page **pagep, void **fsdata)
{
	int retval;
	struct page *page;
	struct v9fs_inode *v9inode = V9FS_I(mapping->host);

	p9_debug(P9_DEBUG_VFS, "filp %p, mapping %p\n", filp, mapping);

	BUG_ON(!v9inode->writeback_fid);

	/* Prefetch area to be written into the cache if we're caching this
	 * file.  We need to do this before we get a lock on the page in case
	 * there's more than one writer competing for the same cache block.
	 */
	retval = netfs_write_begin(filp, mapping, pos, len, flags, &page, fsdata,
				   &v9fs_req_ops, NULL);
	if (retval < 0)
		return retval;

	*pagep = find_subpage(page, pos / PAGE_SIZE);
	return retval;
}

static int v9fs_write_end(struct file *filp, struct address_space *mapping,
			  loff_t pos, unsigned int len, unsigned int copied,
			  struct page *page, void *fsdata)
{
	loff_t last_pos = pos + copied;
	struct inode *inode = page->mapping->host;

	p9_debug(P9_DEBUG_VFS, "filp %p, mapping %p\n", filp, mapping);

	if (!PageUptodate(page)) {
		if (unlikely(copied < len)) {
			copied = 0;
			goto out;
		}

		SetPageUptodate(page);
	}

	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size) {
		inode_add_bytes(inode, last_pos - inode->i_size);
		i_size_write(inode, last_pos);
	}
	set_page_dirty(page);
out:
	unlock_page(page);
	put_page(page);

	return copied;
}


const struct address_space_operations v9fs_addr_operations = {
	.readpage = v9fs_vfs_readpage,
	.readahead = v9fs_vfs_readahead,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.writepage = v9fs_vfs_writepage,
	.write_begin = v9fs_write_begin,
	.write_end = v9fs_write_end,
	.releasepage = v9fs_release_page,
	.invalidatepage = v9fs_invalidate_page,
	.launder_page = v9fs_launder_page,
	.direct_IO = v9fs_direct_IO,
};
