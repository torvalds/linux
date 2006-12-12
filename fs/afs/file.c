/* file.c: AFS filesystem file handling
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "volume.h"
#include "vnode.h"
#include <rxrpc/call.h>
#include "internal.h"

#if 0
static int afs_file_open(struct inode *inode, struct file *file);
static int afs_file_release(struct inode *inode, struct file *file);
#endif

static int afs_file_readpage(struct file *file, struct page *page);
static void afs_file_invalidatepage(struct page *page, unsigned long offset);
static int afs_file_releasepage(struct page *page, gfp_t gfp_flags);

struct inode_operations afs_file_inode_operations = {
	.getattr	= afs_inode_getattr,
};

const struct address_space_operations afs_fs_aops = {
	.readpage	= afs_file_readpage,
	.set_page_dirty	= __set_page_dirty_nobuffers,
	.releasepage	= afs_file_releasepage,
	.invalidatepage	= afs_file_invalidatepage,
};

/*****************************************************************************/
/*
 * deal with notification that a page was read from the cache
 */
#ifdef AFS_CACHING_SUPPORT
static void afs_file_readpage_read_complete(void *cookie_data,
					    struct page *page,
					    void *data,
					    int error)
{
	_enter("%p,%p,%p,%d", cookie_data, page, data, error);

	if (error)
		SetPageError(page);
	else
		SetPageUptodate(page);
	unlock_page(page);

} /* end afs_file_readpage_read_complete() */
#endif

/*****************************************************************************/
/*
 * deal with notification that a page was written to the cache
 */
#ifdef AFS_CACHING_SUPPORT
static void afs_file_readpage_write_complete(void *cookie_data,
					     struct page *page,
					     void *data,
					     int error)
{
	_enter("%p,%p,%p,%d", cookie_data, page, data, error);

	unlock_page(page);

} /* end afs_file_readpage_write_complete() */
#endif

/*****************************************************************************/
/*
 * AFS read page from file (or symlink)
 */
static int afs_file_readpage(struct file *file, struct page *page)
{
	struct afs_rxfs_fetch_descriptor desc;
#ifdef AFS_CACHING_SUPPORT
	struct cachefs_page *pageio;
#endif
	struct afs_vnode *vnode;
	struct inode *inode;
	int ret;

	inode = page->mapping->host;

	_enter("{%lu},{%lu}", inode->i_ino, page->index);

	vnode = AFS_FS_I(inode);

	BUG_ON(!PageLocked(page));

	ret = -ESTALE;
	if (vnode->flags & AFS_VNODE_DELETED)
		goto error;

#ifdef AFS_CACHING_SUPPORT
	ret = cachefs_page_get_private(page, &pageio, GFP_NOIO);
	if (ret < 0)
		goto error;

	/* is it cached? */
	ret = cachefs_read_or_alloc_page(vnode->cache,
					 page,
					 afs_file_readpage_read_complete,
					 NULL,
					 GFP_KERNEL);
#else
	ret = -ENOBUFS;
#endif

	switch (ret) {
		/* read BIO submitted and wb-journal entry found */
	case 1:
		BUG(); // TODO - handle wb-journal match

		/* read BIO submitted (page in cache) */
	case 0:
		break;

		/* no page available in cache */
	case -ENOBUFS:
	case -ENODATA:
	default:
		desc.fid	= vnode->fid;
		desc.offset	= page->index << PAGE_CACHE_SHIFT;
		desc.size	= min((size_t) (inode->i_size - desc.offset),
				      (size_t) PAGE_SIZE);
		desc.buffer	= kmap(page);

		clear_page(desc.buffer);

		/* read the contents of the file from the server into the
		 * page */
		ret = afs_vnode_fetch_data(vnode, &desc);
		kunmap(page);
		if (ret < 0) {
			if (ret==-ENOENT) {
				_debug("got NOENT from server"
				       " - marking file deleted and stale");
				vnode->flags |= AFS_VNODE_DELETED;
				ret = -ESTALE;
			}

#ifdef AFS_CACHING_SUPPORT
			cachefs_uncache_page(vnode->cache, page);
#endif
			goto error;
		}

		SetPageUptodate(page);

#ifdef AFS_CACHING_SUPPORT
		if (cachefs_write_page(vnode->cache,
				       page,
				       afs_file_readpage_write_complete,
				       NULL,
				       GFP_KERNEL) != 0
		    ) {
			cachefs_uncache_page(vnode->cache, page);
			unlock_page(page);
		}
#else
		unlock_page(page);
#endif
	}

	_leave(" = 0");
	return 0;

 error:
	SetPageError(page);
	unlock_page(page);

	_leave(" = %d", ret);
	return ret;

} /* end afs_file_readpage() */

/*****************************************************************************/
/*
 * get a page cookie for the specified page
 */
#ifdef AFS_CACHING_SUPPORT
int afs_cache_get_page_cookie(struct page *page,
			      struct cachefs_page **_page_cookie)
{
	int ret;

	_enter("");
	ret = cachefs_page_get_private(page,_page_cookie, GFP_NOIO);

	_leave(" = %d", ret);
	return ret;
} /* end afs_cache_get_page_cookie() */
#endif

/*****************************************************************************/
/*
 * invalidate part or all of a page
 */
static void afs_file_invalidatepage(struct page *page, unsigned long offset)
{
	int ret = 1;

	_enter("{%lu},%lu", page->index, offset);

	BUG_ON(!PageLocked(page));

	if (PagePrivate(page)) {
#ifdef AFS_CACHING_SUPPORT
		struct afs_vnode *vnode = AFS_FS_I(page->mapping->host);
		cachefs_uncache_page(vnode->cache,page);
#endif

		/* We release buffers only if the entire page is being
		 * invalidated.
		 * The get_block cached value has been unconditionally
		 * invalidated, so real IO is not possible anymore.
		 */
		if (offset == 0) {
			BUG_ON(!PageLocked(page));

			ret = 0;
			if (!PageWriteback(page))
				ret = page->mapping->a_ops->releasepage(page,
									0);
			/* possibly should BUG_ON(!ret); - neilb */
		}
	}

	_leave(" = %d", ret);
} /* end afs_file_invalidatepage() */

/*****************************************************************************/
/*
 * release a page and cleanup its private data
 */
static int afs_file_releasepage(struct page *page, gfp_t gfp_flags)
{
	struct cachefs_page *pageio;

	_enter("{%lu},%x", page->index, gfp_flags);

	if (PagePrivate(page)) {
#ifdef AFS_CACHING_SUPPORT
		struct afs_vnode *vnode = AFS_FS_I(page->mapping->host);
		cachefs_uncache_page(vnode->cache, page);
#endif

		pageio = (struct cachefs_page *) page_private(page);
		set_page_private(page, 0);
		ClearPagePrivate(page);

		kfree(pageio);
	}

	_leave(" = 0");
	return 0;
} /* end afs_file_releasepage() */
