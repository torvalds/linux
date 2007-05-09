/* AFS filesystem file handling
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
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
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include "internal.h"

static int afs_readpage(struct file *file, struct page *page);
static void afs_invalidatepage(struct page *page, unsigned long offset);
static int afs_releasepage(struct page *page, gfp_t gfp_flags);
static int afs_launder_page(struct page *page);

const struct file_operations afs_file_operations = {
	.open		= afs_open,
	.release	= afs_release,
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= afs_file_write,
	.mmap		= generic_file_readonly_mmap,
	.sendfile	= generic_file_sendfile,
	.fsync		= afs_fsync,
};

const struct inode_operations afs_file_inode_operations = {
	.getattr	= afs_getattr,
	.setattr	= afs_setattr,
	.permission	= afs_permission,
};

const struct address_space_operations afs_fs_aops = {
	.readpage	= afs_readpage,
	.set_page_dirty	= afs_set_page_dirty,
	.launder_page	= afs_launder_page,
	.releasepage	= afs_releasepage,
	.invalidatepage	= afs_invalidatepage,
	.prepare_write	= afs_prepare_write,
	.commit_write	= afs_commit_write,
	.writepage	= afs_writepage,
	.writepages	= afs_writepages,
};

/*
 * open an AFS file or directory and attach a key to it
 */
int afs_open(struct inode *inode, struct file *file)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct key *key;
	int ret;

	_enter("{%x:%u},", vnode->fid.vid, vnode->fid.vnode);

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key)) {
		_leave(" = %ld [key]", PTR_ERR(key));
		return PTR_ERR(key);
	}

	ret = afs_validate(vnode, key);
	if (ret < 0) {
		_leave(" = %d [val]", ret);
		return ret;
	}

	file->private_data = key;
	_leave(" = 0");
	return 0;
}

/*
 * release an AFS file or directory and discard its key
 */
int afs_release(struct inode *inode, struct file *file)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);

	_enter("{%x:%u},", vnode->fid.vid, vnode->fid.vnode);

	key_put(file->private_data);
	_leave(" = 0");
	return 0;
}

/*
 * deal with notification that a page was read from the cache
 */
#ifdef AFS_CACHING_SUPPORT
static void afs_readpage_read_complete(void *cookie_data,
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

}
#endif

/*
 * deal with notification that a page was written to the cache
 */
#ifdef AFS_CACHING_SUPPORT
static void afs_readpage_write_complete(void *cookie_data,
					struct page *page,
					void *data,
					int error)
{
	_enter("%p,%p,%p,%d", cookie_data, page, data, error);

	unlock_page(page);
}
#endif

/*
 * AFS read page from file, directory or symlink
 */
static int afs_readpage(struct file *file, struct page *page)
{
	struct afs_vnode *vnode;
	struct inode *inode;
	struct key *key;
	size_t len;
	off_t offset;
	int ret;

	inode = page->mapping->host;

	ASSERT(file != NULL);
	key = file->private_data;
	ASSERT(key != NULL);

	_enter("{%x},{%lu},{%lu}", key_serial(key), inode->i_ino, page->index);

	vnode = AFS_FS_I(inode);

	BUG_ON(!PageLocked(page));

	ret = -ESTALE;
	if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
		goto error;

#ifdef AFS_CACHING_SUPPORT
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
		offset = page->index << PAGE_CACHE_SHIFT;
		len = min_t(size_t, i_size_read(inode) - offset, PAGE_SIZE);

		/* read the contents of the file from the server into the
		 * page */
		ret = afs_vnode_fetch_data(vnode, key, offset, len, page);
		if (ret < 0) {
			if (ret == -ENOENT) {
				_debug("got NOENT from server"
				       " - marking file deleted and stale");
				set_bit(AFS_VNODE_DELETED, &vnode->flags);
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
}

/*
 * invalidate part or all of a page
 */
static void afs_invalidatepage(struct page *page, unsigned long offset)
{
	int ret = 1;

	kenter("{%lu},%lu", page->index, offset);

	BUG_ON(!PageLocked(page));

	if (PagePrivate(page)) {
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
}

/*
 * write back a dirty page
 */
static int afs_launder_page(struct page *page)
{
	_enter("{%lu}", page->index);

	return 0;
}

/*
 * release a page and cleanup its private data
 */
static int afs_releasepage(struct page *page, gfp_t gfp_flags)
{
	struct afs_vnode *vnode = AFS_FS_I(page->mapping->host);
	struct afs_writeback *wb;

	_enter("{{%x:%u}[%lu],%lx},%x",
	       vnode->fid.vid, vnode->fid.vnode, page->index, page->flags,
	       gfp_flags);

	if (PagePrivate(page)) {
		wb = (struct afs_writeback *) page_private(page);
		ASSERT(wb != NULL);
		set_page_private(page, 0);
		ClearPagePrivate(page);
		afs_put_writeback(wb);
	}

	_leave(" = 0");
	return 0;
}
