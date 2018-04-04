/*
 * V9FS cache definitions.
 *
 *  Copyright (C) 2009 by Abhishek Kulkarni <adkulkar@umail.iu.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/jiffies.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <net/9p/9p.h>

#include "v9fs.h"
#include "cache.h"

#define CACHETAG_LEN  11

struct fscache_netfs v9fs_cache_netfs = {
	.name 		= "9p",
	.version 	= 0,
};

/**
 * v9fs_random_cachetag - Generate a random tag to be associated
 *			  with a new cache session.
 *
 * The value of jiffies is used for a fairly randomly cache tag.
 */

static
int v9fs_random_cachetag(struct v9fs_session_info *v9ses)
{
	v9ses->cachetag = kmalloc(CACHETAG_LEN, GFP_KERNEL);
	if (!v9ses->cachetag)
		return -ENOMEM;

	return scnprintf(v9ses->cachetag, CACHETAG_LEN, "%lu", jiffies);
}

const struct fscache_cookie_def v9fs_cache_session_index_def = {
	.name		= "9P.session",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
};

void v9fs_cache_session_get_cookie(struct v9fs_session_info *v9ses)
{
	/* If no cache session tag was specified, we generate a random one. */
	if (!v9ses->cachetag) {
		if (v9fs_random_cachetag(v9ses) < 0) {
			v9ses->fscache = NULL;
			return;
		}
	}

	v9ses->fscache = fscache_acquire_cookie(v9fs_cache_netfs.primary_index,
						&v9fs_cache_session_index_def,
						v9ses->cachetag,
						strlen(v9ses->cachetag),
						NULL, 0,
						v9ses, true);
	p9_debug(P9_DEBUG_FSC, "session %p get cookie %p\n",
		 v9ses, v9ses->fscache);
}

void v9fs_cache_session_put_cookie(struct v9fs_session_info *v9ses)
{
	p9_debug(P9_DEBUG_FSC, "session %p put cookie %p\n",
		 v9ses, v9ses->fscache);
	fscache_relinquish_cookie(v9ses->fscache, NULL, false);
	v9ses->fscache = NULL;
}

static void v9fs_cache_inode_get_attr(const void *cookie_netfs_data,
				      uint64_t *size)
{
	const struct v9fs_inode *v9inode = cookie_netfs_data;
	*size = i_size_read(&v9inode->vfs_inode);

	p9_debug(P9_DEBUG_FSC, "inode %p get attr %llu\n",
		 &v9inode->vfs_inode, *size);
}

static enum
fscache_checkaux v9fs_cache_inode_check_aux(void *cookie_netfs_data,
					    const void *buffer,
					    uint16_t buflen)
{
	const struct v9fs_inode *v9inode = cookie_netfs_data;

	if (buflen != sizeof(v9inode->qid.version))
		return FSCACHE_CHECKAUX_OBSOLETE;

	if (memcmp(buffer, &v9inode->qid.version,
		   sizeof(v9inode->qid.version)))
		return FSCACHE_CHECKAUX_OBSOLETE;

	return FSCACHE_CHECKAUX_OKAY;
}

const struct fscache_cookie_def v9fs_cache_inode_index_def = {
	.name		= "9p.inode",
	.type		= FSCACHE_COOKIE_TYPE_DATAFILE,
	.get_attr	= v9fs_cache_inode_get_attr,
	.check_aux	= v9fs_cache_inode_check_aux,
};

void v9fs_cache_inode_get_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode;
	struct v9fs_session_info *v9ses;

	if (!S_ISREG(inode->i_mode))
		return;

	v9inode = V9FS_I(inode);
	if (v9inode->fscache)
		return;

	v9ses = v9fs_inode2v9ses(inode);
	v9inode->fscache = fscache_acquire_cookie(v9ses->fscache,
						  &v9fs_cache_inode_index_def,
						  &v9inode->qid.path,
						  sizeof(v9inode->qid.path),
						  &v9inode->qid.version,
						  sizeof(v9inode->qid.version),
						  v9inode, true);

	p9_debug(P9_DEBUG_FSC, "inode %p get cookie %p\n",
		 inode, v9inode->fscache);
}

void v9fs_cache_inode_put_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);

	if (!v9inode->fscache)
		return;
	p9_debug(P9_DEBUG_FSC, "inode %p put cookie %p\n",
		 inode, v9inode->fscache);

	fscache_relinquish_cookie(v9inode->fscache, &v9inode->qid.version,
				  false);
	v9inode->fscache = NULL;
}

void v9fs_cache_inode_flush_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);

	if (!v9inode->fscache)
		return;
	p9_debug(P9_DEBUG_FSC, "inode %p flush cookie %p\n",
		 inode, v9inode->fscache);

	fscache_relinquish_cookie(v9inode->fscache, NULL, true);
	v9inode->fscache = NULL;
}

void v9fs_cache_inode_set_cookie(struct inode *inode, struct file *filp)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);

	if (!v9inode->fscache)
		return;

	mutex_lock(&v9inode->fscache_lock);

	if ((filp->f_flags & O_ACCMODE) != O_RDONLY)
		v9fs_cache_inode_flush_cookie(inode);
	else
		v9fs_cache_inode_get_cookie(inode);

	mutex_unlock(&v9inode->fscache_lock);
}

void v9fs_cache_inode_reset_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct v9fs_session_info *v9ses;
	struct fscache_cookie *old;

	if (!v9inode->fscache)
		return;

	old = v9inode->fscache;

	mutex_lock(&v9inode->fscache_lock);
	fscache_relinquish_cookie(v9inode->fscache, NULL, true);

	v9ses = v9fs_inode2v9ses(inode);
	v9inode->fscache = fscache_acquire_cookie(v9ses->fscache,
						  &v9fs_cache_inode_index_def,
						  &v9inode->qid.path,
						  sizeof(v9inode->qid.path),
						  &v9inode->qid.version,
						  sizeof(v9inode->qid.version),
						  v9inode, true);
	p9_debug(P9_DEBUG_FSC, "inode %p revalidating cookie old %p new %p\n",
		 inode, old, v9inode->fscache);

	mutex_unlock(&v9inode->fscache_lock);
}

int __v9fs_fscache_release_page(struct page *page, gfp_t gfp)
{
	struct inode *inode = page->mapping->host;
	struct v9fs_inode *v9inode = V9FS_I(inode);

	BUG_ON(!v9inode->fscache);

	return fscache_maybe_release_page(v9inode->fscache, page, gfp);
}

void __v9fs_fscache_invalidate_page(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct v9fs_inode *v9inode = V9FS_I(inode);

	BUG_ON(!v9inode->fscache);

	if (PageFsCache(page)) {
		fscache_wait_on_page_write(v9inode->fscache, page);
		BUG_ON(!PageLocked(page));
		fscache_uncache_page(v9inode->fscache, page);
	}
}

static void v9fs_vfs_readpage_complete(struct page *page, void *data,
				       int error)
{
	if (!error)
		SetPageUptodate(page);

	unlock_page(page);
}

/**
 * __v9fs_readpage_from_fscache - read a page from cache
 *
 * Returns 0 if the pages are in cache and a BIO is submitted,
 * 1 if the pages are not in cache and -error otherwise.
 */

int __v9fs_readpage_from_fscache(struct inode *inode, struct page *page)
{
	int ret;
	const struct v9fs_inode *v9inode = V9FS_I(inode);

	p9_debug(P9_DEBUG_FSC, "inode %p page %p\n", inode, page);
	if (!v9inode->fscache)
		return -ENOBUFS;

	ret = fscache_read_or_alloc_page(v9inode->fscache,
					 page,
					 v9fs_vfs_readpage_complete,
					 NULL,
					 GFP_KERNEL);
	switch (ret) {
	case -ENOBUFS:
	case -ENODATA:
		p9_debug(P9_DEBUG_FSC, "page/inode not in cache %d\n", ret);
		return 1;
	case 0:
		p9_debug(P9_DEBUG_FSC, "BIO submitted\n");
		return ret;
	default:
		p9_debug(P9_DEBUG_FSC, "ret %d\n", ret);
		return ret;
	}
}

/**
 * __v9fs_readpages_from_fscache - read multiple pages from cache
 *
 * Returns 0 if the pages are in cache and a BIO is submitted,
 * 1 if the pages are not in cache and -error otherwise.
 */

int __v9fs_readpages_from_fscache(struct inode *inode,
				  struct address_space *mapping,
				  struct list_head *pages,
				  unsigned *nr_pages)
{
	int ret;
	const struct v9fs_inode *v9inode = V9FS_I(inode);

	p9_debug(P9_DEBUG_FSC, "inode %p pages %u\n", inode, *nr_pages);
	if (!v9inode->fscache)
		return -ENOBUFS;

	ret = fscache_read_or_alloc_pages(v9inode->fscache,
					  mapping, pages, nr_pages,
					  v9fs_vfs_readpage_complete,
					  NULL,
					  mapping_gfp_mask(mapping));
	switch (ret) {
	case -ENOBUFS:
	case -ENODATA:
		p9_debug(P9_DEBUG_FSC, "pages/inodes not in cache %d\n", ret);
		return 1;
	case 0:
		BUG_ON(!list_empty(pages));
		BUG_ON(*nr_pages != 0);
		p9_debug(P9_DEBUG_FSC, "BIO submitted\n");
		return ret;
	default:
		p9_debug(P9_DEBUG_FSC, "ret %d\n", ret);
		return ret;
	}
}

/**
 * __v9fs_readpage_to_fscache - write a page to the cache
 *
 */

void __v9fs_readpage_to_fscache(struct inode *inode, struct page *page)
{
	int ret;
	const struct v9fs_inode *v9inode = V9FS_I(inode);

	p9_debug(P9_DEBUG_FSC, "inode %p page %p\n", inode, page);
	ret = fscache_write_page(v9inode->fscache, page, GFP_KERNEL);
	p9_debug(P9_DEBUG_FSC, "ret =  %d\n", ret);
	if (ret != 0)
		v9fs_uncache_page(inode, page);
}

/*
 * wait for a page to complete writing to the cache
 */
void __v9fs_fscache_wait_on_page_write(struct inode *inode, struct page *page)
{
	const struct v9fs_inode *v9inode = V9FS_I(inode);
	p9_debug(P9_DEBUG_FSC, "inode %p page %p\n", inode, page);
	if (PageFsCache(page))
		fscache_wait_on_page_write(v9inode->fscache, page);
}
