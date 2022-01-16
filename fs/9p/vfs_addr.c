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
#include <linux/swap.h>
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

	/* if we just extended the file size, any portion not in
	 * cache won't be on server and is zeroes */
	__set_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags);

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

	return fscache_cookie_enabled(cookie) && cookie->cache_priv;
}

/**
 * v9fs_begin_cache_operation - Begin a cache operation for a read
 * @rreq: The read request
 */
static int v9fs_begin_cache_operation(struct netfs_read_request *rreq)
{
#ifdef CONFIG_9P_FSCACHE
	struct fscache_cookie *cookie = v9fs_inode_cookie(V9FS_I(rreq->inode));

	return fscache_begin_read_operation(&rreq->cache_resources, cookie);
#else
	return -ENOBUFS;
#endif
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
	struct folio *folio = page_folio(page);

	return netfs_readpage(file, folio, &v9fs_req_ops, NULL);
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
	struct folio *folio = page_folio(page);
	struct inode *inode = folio_inode(folio);

	if (folio_test_private(folio))
		return 0;
#ifdef CONFIG_9P_FSCACHE
	if (folio_test_fscache(folio)) {
		if (current_is_kswapd() || !(gfp & __GFP_FS))
			return 0;
		folio_wait_fscache(folio);
	}
#endif
	fscache_note_page_release(v9fs_inode_cookie(V9FS_I(inode)));
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
	struct folio *folio = page_folio(page);

	folio_wait_fscache(folio);
}

static void v9fs_write_to_cache_done(void *priv, ssize_t transferred_or_error,
				     bool was_async)
{
	struct v9fs_inode *v9inode = priv;
	__le32 version;

	if (IS_ERR_VALUE(transferred_or_error) &&
	    transferred_or_error != -ENOBUFS) {
		version = cpu_to_le32(v9inode->qid.version);
		fscache_invalidate(v9fs_inode_cookie(v9inode), &version,
				   i_size_read(&v9inode->vfs_inode), 0);
	}
}

static int v9fs_vfs_write_folio_locked(struct folio *folio)
{
	struct inode *inode = folio_inode(folio);
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct fscache_cookie *cookie = v9fs_inode_cookie(v9inode);
	loff_t start = folio_pos(folio);
	loff_t i_size = i_size_read(inode);
	struct iov_iter from;
	size_t len = folio_size(folio);
	int err;

	if (start >= i_size)
		return 0; /* Simultaneous truncation occurred */

	len = min_t(loff_t, i_size - start, len);

	iov_iter_xarray(&from, WRITE, &folio_mapping(folio)->i_pages, start, len);

	/* We should have writeback_fid always set */
	BUG_ON(!v9inode->writeback_fid);

	folio_wait_fscache(folio);
	folio_start_writeback(folio);

	p9_client_write(v9inode->writeback_fid, start, &from, &err);

	if (err == 0 &&
	    fscache_cookie_enabled(cookie) &&
	    test_bit(FSCACHE_COOKIE_IS_CACHING, &cookie->flags)) {
		folio_start_fscache(folio);
		fscache_write_to_cache(v9fs_inode_cookie(v9inode),
				       folio_mapping(folio), start, len, i_size,
				       v9fs_write_to_cache_done, v9inode,
				       true);
	}

	folio_end_writeback(folio);
	return err;
}

static int v9fs_vfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct folio *folio = page_folio(page);
	int retval;

	p9_debug(P9_DEBUG_VFS, "folio %p\n", folio);

	retval = v9fs_vfs_write_folio_locked(folio);
	if (retval < 0) {
		if (retval == -EAGAIN) {
			folio_redirty_for_writepage(wbc, folio);
			retval = 0;
		} else {
			mapping_set_error(folio_mapping(folio), retval);
		}
	} else
		retval = 0;

	folio_unlock(folio);
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
	struct folio *folio = page_folio(page);
	int retval;

	if (folio_clear_dirty_for_io(folio)) {
		retval = v9fs_vfs_write_folio_locked(folio);
		if (retval)
			return retval;
	}
	folio_wait_fscache(folio);
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
			    struct page **subpagep, void **fsdata)
{
	int retval;
	struct folio *folio;
	struct v9fs_inode *v9inode = V9FS_I(mapping->host);

	p9_debug(P9_DEBUG_VFS, "filp %p, mapping %p\n", filp, mapping);

	BUG_ON(!v9inode->writeback_fid);

	/* Prefetch area to be written into the cache if we're caching this
	 * file.  We need to do this before we get a lock on the page in case
	 * there's more than one writer competing for the same cache block.
	 */
	retval = netfs_write_begin(filp, mapping, pos, len, flags, &folio, fsdata,
				   &v9fs_req_ops, NULL);
	if (retval < 0)
		return retval;

	*subpagep = &folio->page;
	return retval;
}

static int v9fs_write_end(struct file *filp, struct address_space *mapping,
			  loff_t pos, unsigned int len, unsigned int copied,
			  struct page *subpage, void *fsdata)
{
	loff_t last_pos = pos + copied;
	struct folio *folio = page_folio(subpage);
	struct inode *inode = mapping->host;
	struct v9fs_inode *v9inode = V9FS_I(inode);

	p9_debug(P9_DEBUG_VFS, "filp %p, mapping %p\n", filp, mapping);

	if (!folio_test_uptodate(folio)) {
		if (unlikely(copied < len)) {
			copied = 0;
			goto out;
		}

		folio_mark_uptodate(folio);
	}

	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size) {
		inode_add_bytes(inode, last_pos - inode->i_size);
		i_size_write(inode, last_pos);
		fscache_update_cookie(v9fs_inode_cookie(v9inode), NULL, &last_pos);
	}
	folio_mark_dirty(folio);
out:
	folio_unlock(folio);
	folio_put(folio);

	return copied;
}

#ifdef CONFIG_9P_FSCACHE
/*
 * Mark a page as having been made dirty and thus needing writeback.  We also
 * need to pin the cache object to write back to.
 */
static int v9fs_set_page_dirty(struct page *page)
{
	struct v9fs_inode *v9inode = V9FS_I(page->mapping->host);

	return fscache_set_page_dirty(page, v9fs_inode_cookie(v9inode));
}
#else
#define v9fs_set_page_dirty __set_page_dirty_nobuffers
#endif

const struct address_space_operations v9fs_addr_operations = {
	.readpage = v9fs_vfs_readpage,
	.readahead = v9fs_vfs_readahead,
	.set_page_dirty = v9fs_set_page_dirty,
	.writepage = v9fs_vfs_writepage,
	.write_begin = v9fs_write_begin,
	.write_end = v9fs_write_end,
	.releasepage = v9fs_release_page,
	.invalidatepage = v9fs_invalidate_page,
	.launder_page = v9fs_launder_page,
	.direct_IO = v9fs_direct_IO,
};
