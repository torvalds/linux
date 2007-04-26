/* dir.c: AFS filesystem directory handling
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
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "internal.h"

static struct dentry *afs_dir_lookup(struct inode *dir, struct dentry *dentry,
				     struct nameidata *nd);
static int afs_dir_open(struct inode *inode, struct file *file);
static int afs_dir_readdir(struct file *file, void *dirent, filldir_t filldir);
static int afs_d_revalidate(struct dentry *dentry, struct nameidata *nd);
static int afs_d_delete(struct dentry *dentry);
static int afs_dir_lookup_filldir(void *_cookie, const char *name, int nlen,
				  loff_t fpos, u64 ino, unsigned dtype);

const struct file_operations afs_dir_file_operations = {
	.open		= afs_dir_open,
	.readdir	= afs_dir_readdir,
};

const struct inode_operations afs_dir_inode_operations = {
	.lookup		= afs_dir_lookup,
	.getattr	= afs_inode_getattr,
#if 0 /* TODO */
	.create		= afs_dir_create,
	.link		= afs_dir_link,
	.unlink		= afs_dir_unlink,
	.symlink	= afs_dir_symlink,
	.mkdir		= afs_dir_mkdir,
	.rmdir		= afs_dir_rmdir,
	.mknod		= afs_dir_mknod,
	.rename		= afs_dir_rename,
#endif
};

static struct dentry_operations afs_fs_dentry_operations = {
	.d_revalidate	= afs_d_revalidate,
	.d_delete	= afs_d_delete,
};

#define AFS_DIR_HASHTBL_SIZE	128
#define AFS_DIR_DIRENT_SIZE	32
#define AFS_DIRENT_PER_BLOCK	64

union afs_dirent {
	struct {
		uint8_t		valid;
		uint8_t		unused[1];
		__be16		hash_next;
		__be32		vnode;
		__be32		unique;
		uint8_t		name[16];
		uint8_t		overflow[4];	/* if any char of the name (inc
						 * NUL) reaches here, consume
						 * the next dirent too */
	} u;
	uint8_t	extended_name[32];
};

/* AFS directory page header (one at the beginning of every 2048-byte chunk) */
struct afs_dir_pagehdr {
	__be16		npages;
	__be16		magic;
#define AFS_DIR_MAGIC htons(1234)
	uint8_t		nentries;
	uint8_t		bitmap[8];
	uint8_t		pad[19];
};

/* directory block layout */
union afs_dir_block {

	struct afs_dir_pagehdr pagehdr;

	struct {
		struct afs_dir_pagehdr	pagehdr;
		uint8_t			alloc_ctrs[128];
		/* dir hash table */
		uint16_t		hashtable[AFS_DIR_HASHTBL_SIZE];
	} hdr;

	union afs_dirent dirents[AFS_DIRENT_PER_BLOCK];
};

/* layout on a linux VM page */
struct afs_dir_page {
	union afs_dir_block blocks[PAGE_SIZE / sizeof(union afs_dir_block)];
};

struct afs_dir_lookup_cookie {
	struct afs_fid	fid;
	const char	*name;
	size_t		nlen;
	int		found;
};

/*
 * check that a directory page is valid
 */
static inline void afs_dir_check_page(struct inode *dir, struct page *page)
{
	struct afs_dir_page *dbuf;
	loff_t latter;
	int tmp, qty;

#if 0
	/* check the page count */
	qty = desc.size / sizeof(dbuf->blocks[0]);
	if (qty == 0)
		goto error;

	if (page->index == 0 && qty != ntohs(dbuf->blocks[0].pagehdr.npages)) {
		printk("kAFS: %s(%lu): wrong number of dir blocks %d!=%hu\n",
		       __FUNCTION__, dir->i_ino, qty,
		       ntohs(dbuf->blocks[0].pagehdr.npages));
		goto error;
	}
#endif

	/* determine how many magic numbers there should be in this page */
	latter = dir->i_size - page_offset(page);
	if (latter >= PAGE_SIZE)
		qty = PAGE_SIZE;
	else
		qty = latter;
	qty /= sizeof(union afs_dir_block);

	/* check them */
	dbuf = page_address(page);
	for (tmp = 0; tmp < qty; tmp++) {
		if (dbuf->blocks[tmp].pagehdr.magic != AFS_DIR_MAGIC) {
			printk("kAFS: %s(%lu): bad magic %d/%d is %04hx\n",
			       __FUNCTION__, dir->i_ino, tmp, qty,
			       ntohs(dbuf->blocks[tmp].pagehdr.magic));
			goto error;
		}
	}

	SetPageChecked(page);
	return;

error:
	SetPageChecked(page);
	SetPageError(page);
}

/*
 * discard a page cached in the pagecache
 */
static inline void afs_dir_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

/*
 * get a page into the pagecache
 */
static struct page *afs_dir_get_page(struct inode *dir, unsigned long index)
{
	struct page *page;

	_enter("{%lu},%lu", dir->i_ino, index);

	page = read_mapping_page(dir->i_mapping, index, NULL);
	if (!IS_ERR(page)) {
		wait_on_page_locked(page);
		kmap(page);
		if (!PageUptodate(page))
			goto fail;
		if (!PageChecked(page))
			afs_dir_check_page(dir, page);
		if (PageError(page))
			goto fail;
	}
	return page;

fail:
	afs_dir_put_page(page);
	_leave(" = -EIO");
	return ERR_PTR(-EIO);
}

/*
 * open an AFS directory file
 */
static int afs_dir_open(struct inode *inode, struct file *file)
{
	_enter("{%lu}", inode->i_ino);

	BUILD_BUG_ON(sizeof(union afs_dir_block) != 2048);
	BUILD_BUG_ON(sizeof(union afs_dirent) != 32);

	if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(inode)->flags))
		return -ENOENT;

	_leave(" = 0");
	return 0;
}

/*
 * deal with one block in an AFS directory
 */
static int afs_dir_iterate_block(unsigned *fpos,
				 union afs_dir_block *block,
				 unsigned blkoff,
				 void *cookie,
				 filldir_t filldir)
{
	union afs_dirent *dire;
	unsigned offset, next, curr;
	size_t nlen;
	int tmp, ret;

	_enter("%u,%x,%p,,",*fpos,blkoff,block);

	curr = (*fpos - blkoff) / sizeof(union afs_dirent);

	/* walk through the block, an entry at a time */
	for (offset = AFS_DIRENT_PER_BLOCK - block->pagehdr.nentries;
	     offset < AFS_DIRENT_PER_BLOCK;
	     offset = next
	     ) {
		next = offset + 1;

		/* skip entries marked unused in the bitmap */
		if (!(block->pagehdr.bitmap[offset / 8] &
		      (1 << (offset % 8)))) {
			_debug("ENT[%Zu.%u]: unused",
			       blkoff / sizeof(union afs_dir_block), offset);
			if (offset >= curr)
				*fpos = blkoff +
					next * sizeof(union afs_dirent);
			continue;
		}

		/* got a valid entry */
		dire = &block->dirents[offset];
		nlen = strnlen(dire->u.name,
			       sizeof(*block) -
			       offset * sizeof(union afs_dirent));

		_debug("ENT[%Zu.%u]: %s %Zu \"%s\"",
		       blkoff / sizeof(union afs_dir_block), offset,
		       (offset < curr ? "skip" : "fill"),
		       nlen, dire->u.name);

		/* work out where the next possible entry is */
		for (tmp = nlen; tmp > 15; tmp -= sizeof(union afs_dirent)) {
			if (next >= AFS_DIRENT_PER_BLOCK) {
				_debug("ENT[%Zu.%u]:"
				       " %u travelled beyond end dir block"
				       " (len %u/%Zu)",
				       blkoff / sizeof(union afs_dir_block),
				       offset, next, tmp, nlen);
				return -EIO;
			}
			if (!(block->pagehdr.bitmap[next / 8] &
			      (1 << (next % 8)))) {
				_debug("ENT[%Zu.%u]:"
				       " %u unmarked extension (len %u/%Zu)",
				       blkoff / sizeof(union afs_dir_block),
				       offset, next, tmp, nlen);
				return -EIO;
			}

			_debug("ENT[%Zu.%u]: ext %u/%Zu",
			       blkoff / sizeof(union afs_dir_block),
			       next, tmp, nlen);
			next++;
		}

		/* skip if starts before the current position */
		if (offset < curr)
			continue;

		/* found the next entry */
		ret = filldir(cookie,
			      dire->u.name,
			      nlen,
			      blkoff + offset * sizeof(union afs_dirent),
			      ntohl(dire->u.vnode),
			      filldir == afs_dir_lookup_filldir ?
			      ntohl(dire->u.unique) : DT_UNKNOWN);
		if (ret < 0) {
			_leave(" = 0 [full]");
			return 0;
		}

		*fpos = blkoff + next * sizeof(union afs_dirent);
	}

	_leave(" = 1 [more]");
	return 1;
}

/*
 * iterate through the data blob that lists the contents of an AFS directory
 */
static int afs_dir_iterate(struct inode *dir, unsigned *fpos, void *cookie,
			   filldir_t filldir)
{
	union afs_dir_block *dblock;
	struct afs_dir_page *dbuf;
	struct page *page;
	unsigned blkoff, limit;
	int ret;

	_enter("{%lu},%u,,", dir->i_ino, *fpos);

	if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(dir)->flags)) {
		_leave(" = -ESTALE");
		return -ESTALE;
	}

	/* round the file position up to the next entry boundary */
	*fpos += sizeof(union afs_dirent) - 1;
	*fpos &= ~(sizeof(union afs_dirent) - 1);

	/* walk through the blocks in sequence */
	ret = 0;
	while (*fpos < dir->i_size) {
		blkoff = *fpos & ~(sizeof(union afs_dir_block) - 1);

		/* fetch the appropriate page from the directory */
		page = afs_dir_get_page(dir, blkoff / PAGE_SIZE);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			break;
		}

		limit = blkoff & ~(PAGE_SIZE - 1);

		dbuf = page_address(page);

		/* deal with the individual blocks stashed on this page */
		do {
			dblock = &dbuf->blocks[(blkoff % PAGE_SIZE) /
					       sizeof(union afs_dir_block)];
			ret = afs_dir_iterate_block(fpos, dblock, blkoff,
						    cookie, filldir);
			if (ret != 1) {
				afs_dir_put_page(page);
				goto out;
			}

			blkoff += sizeof(union afs_dir_block);

		} while (*fpos < dir->i_size && blkoff < limit);

		afs_dir_put_page(page);
		ret = 0;
	}

out:
	_leave(" = %d", ret);
	return ret;
}

/*
 * read an AFS directory
 */
static int afs_dir_readdir(struct file *file, void *cookie, filldir_t filldir)
{
	unsigned fpos;
	int ret;

	_enter("{%Ld,{%lu}}",
	       file->f_pos, file->f_path.dentry->d_inode->i_ino);

	fpos = file->f_pos;
	ret = afs_dir_iterate(file->f_path.dentry->d_inode, &fpos,
			      cookie, filldir);
	file->f_pos = fpos;

	_leave(" = %d", ret);
	return ret;
}

/*
 * search the directory for a name
 * - if afs_dir_iterate_block() spots this function, it'll pass the FID
 *   uniquifier through dtype
 */
static int afs_dir_lookup_filldir(void *_cookie, const char *name, int nlen,
				  loff_t fpos, u64 ino, unsigned dtype)
{
	struct afs_dir_lookup_cookie *cookie = _cookie;

	_enter("{%s,%Zu},%s,%u,,%llu,%u",
	       cookie->name, cookie->nlen, name, nlen, ino, dtype);

	/* insanity checks first */
	BUILD_BUG_ON(sizeof(union afs_dir_block) != 2048);
	BUILD_BUG_ON(sizeof(union afs_dirent) != 32);

	if (cookie->nlen != nlen || memcmp(cookie->name, name, nlen) != 0) {
		_leave(" = 0 [no]");
		return 0;
	}

	cookie->fid.vnode = ino;
	cookie->fid.unique = dtype;
	cookie->found = 1;

	_leave(" = -1 [found]");
	return -1;
}

/*
 * do a lookup in a directory
 */
static int afs_do_lookup(struct inode *dir, struct dentry *dentry,
			 struct afs_fid *fid)
{
	struct afs_dir_lookup_cookie cookie;
	struct afs_super_info *as;
	unsigned fpos;
	int ret;

	_enter("{%lu},%p{%s},", dir->i_ino, dentry, dentry->d_name.name);

	as = dir->i_sb->s_fs_info;

	/* search the directory */
	cookie.name	= dentry->d_name.name;
	cookie.nlen	= dentry->d_name.len;
	cookie.fid.vid	= as->volume->vid;
	cookie.found	= 0;

	fpos = 0;
	ret = afs_dir_iterate(dir, &fpos, &cookie, afs_dir_lookup_filldir);
	if (ret < 0) {
		_leave(" = %d [iter]", ret);
		return ret;
	}

	ret = -ENOENT;
	if (!cookie.found) {
		_leave(" = -ENOENT [not found]");
		return -ENOENT;
	}

	*fid = cookie.fid;
	_leave(" = 0 { vn=%u u=%u }", fid->vnode, fid->unique);
	return 0;
}

/*
 * look up an entry in a directory
 */
static struct dentry *afs_dir_lookup(struct inode *dir, struct dentry *dentry,
				     struct nameidata *nd)
{
	struct afs_vnode *vnode;
	struct afs_fid fid;
	struct inode *inode;
	int ret;

	_enter("{%lu},%p{%s}", dir->i_ino, dentry, dentry->d_name.name);

	if (dentry->d_name.len > 255) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	vnode = AFS_FS_I(dir);
	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		_leave(" = -ESTALE");
		return ERR_PTR(-ESTALE);
	}

	ret = afs_do_lookup(dir, dentry, &fid);
	if (ret < 0) {
		_leave(" = %d [do]", ret);
		return ERR_PTR(ret);
	}

	/* instantiate the dentry */
	inode = afs_iget(dir->i_sb, &fid);
	if (IS_ERR(inode)) {
		_leave(" = %ld", PTR_ERR(inode));
		return ERR_PTR(PTR_ERR(inode));
	}

	dentry->d_op = &afs_fs_dentry_operations;

	d_add(dentry, inode);
	_leave(" = 0 { vn=%u u=%u } -> { ino=%lu v=%lu }",
	       fid.vnode,
	       fid.unique,
	       dentry->d_inode->i_ino,
	       dentry->d_inode->i_version);

	return NULL;
}

/*
 * propagate changed and modified flags on a directory to all the children of
 * that directory as they may indicate that the ACL on the dir has changed,
 * potentially rendering the child inaccessible or that a file has been deleted
 * or renamed
 */
static void afs_propagate_dir_changes(struct dentry *dir)
{
	struct dentry *child;
	bool c, m;

	c = test_bit(AFS_VNODE_CHANGED, &AFS_FS_I(dir->d_inode)->flags);
	m = test_bit(AFS_VNODE_MODIFIED, &AFS_FS_I(dir->d_inode)->flags);

	_enter("{%d,%d}", c, m);

	spin_lock(&dir->d_lock);

	list_for_each_entry(child, &dir->d_subdirs, d_u.d_child) {
		if (child->d_inode) {
			struct afs_vnode *vnode;

			_debug("tag %s", child->d_name.name);
			vnode = AFS_FS_I(child->d_inode);
			if (c)
				set_bit(AFS_VNODE_DIR_CHANGED, &vnode->flags);
			if (m)
				set_bit(AFS_VNODE_DIR_MODIFIED, &vnode->flags);
		}
	}

	spin_unlock(&dir->d_lock);
}

/*
 * check that a dentry lookup hit has found a valid entry
 * - NOTE! the hit can be a negative hit too, so we can't assume we have an
 *   inode
 * - there are several things we need to check
 *   - parent dir data changes (rm, rmdir, rename, mkdir, create, link,
 *     symlink)
 *   - parent dir metadata changed (security changes)
 *   - dentry data changed (write, truncate)
 *   - dentry metadata changed (security changes)
 */
static int afs_d_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct afs_vnode *vnode;
	struct afs_fid fid;
	struct dentry *parent;
	struct inode *inode, *dir;
	int ret;

	vnode = AFS_FS_I(dentry->d_inode);

	_enter("{sb=%p n=%s fl=%lx},",
	       dentry->d_sb, dentry->d_name.name, vnode->flags);

	/* lock down the parent dentry so we can peer at it */
	parent = dget_parent(dentry);

	dir = parent->d_inode;
	inode = dentry->d_inode;

	/* handle a negative dentry */
	if (!inode)
		goto out_bad;

	/* handle a bad inode */
	if (is_bad_inode(inode)) {
		printk("kAFS: afs_d_revalidate: %s/%s has bad inode\n",
		       parent->d_name.name, dentry->d_name.name);
		goto out_bad;
	}

	/* check that this dirent still exists if the directory's contents were
	 * modified */
	if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(dir)->flags)) {
		_debug("%s: parent dir deleted", dentry->d_name.name);
		goto out_bad;
	}

	if (test_and_clear_bit(AFS_VNODE_DIR_MODIFIED, &vnode->flags)) {
		/* rm/rmdir/rename may have occurred */
		_debug("dir modified");

		/* search the directory for this vnode */
		ret = afs_do_lookup(dir, dentry, &fid);
		if (ret == -ENOENT) {
			_debug("%s: dirent not found", dentry->d_name.name);
			goto not_found;
		}
		if (ret < 0) {
			_debug("failed to iterate dir %s: %d",
			       parent->d_name.name, ret);
			goto out_bad;
		}

		/* if the vnode ID has changed, then the dirent points to a
		 * different file */
		if (fid.vnode != vnode->fid.vnode) {
			_debug("%s: dirent changed [%u != %u]",
			       dentry->d_name.name, fid.vnode,
			       vnode->fid.vnode);
			goto not_found;
		}

		/* if the vnode ID uniqifier has changed, then the file has
		 * been deleted */
		if (fid.unique != vnode->fid.unique) {
			_debug("%s: file deleted (uq %u -> %u I:%lu)",
			       dentry->d_name.name, fid.unique,
			       vnode->fid.unique, inode->i_version);
			spin_lock(&vnode->lock);
			set_bit(AFS_VNODE_DELETED, &vnode->flags);
			spin_unlock(&vnode->lock);
			invalidate_remote_inode(inode);
			goto out_bad;
		}
	}

	/* if the directory's metadata were changed then the security may be
	 * different and we may no longer have access */
	mutex_lock(&vnode->cb_broken_lock);

	if (test_and_clear_bit(AFS_VNODE_DIR_CHANGED, &vnode->flags) ||
	    test_bit(AFS_VNODE_CB_BROKEN, &vnode->flags)) {
		_debug("%s: changed", dentry->d_name.name);
		set_bit(AFS_VNODE_CB_BROKEN, &vnode->flags);
		if (afs_vnode_fetch_status(vnode) < 0) {
			mutex_unlock(&vnode->cb_broken_lock);
			goto out_bad;
		}
	}

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		_debug("%s: file already deleted", dentry->d_name.name);
		mutex_unlock(&vnode->cb_broken_lock);
		goto out_bad;
	}

	/* if the vnode's data version number changed then its contents are
	 * different */
	if (test_and_clear_bit(AFS_VNODE_ZAP_DATA, &vnode->flags)) {
		_debug("zap data");
		invalidate_remote_inode(inode);
	}

	if (S_ISDIR(inode->i_mode) &&
	    (test_bit(AFS_VNODE_CHANGED, &vnode->flags) ||
	     test_bit(AFS_VNODE_MODIFIED, &vnode->flags)))
		afs_propagate_dir_changes(dentry);

	clear_bit(AFS_VNODE_CHANGED, &vnode->flags);
	clear_bit(AFS_VNODE_MODIFIED, &vnode->flags);
	mutex_unlock(&vnode->cb_broken_lock);

out_valid:
	dput(parent);
	_leave(" = 1 [valid]");
	return 1;

	/* the dirent, if it exists, now points to a different vnode */
not_found:
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_NFSFS_RENAMED;
	spin_unlock(&dentry->d_lock);

out_bad:
	if (inode) {
		/* don't unhash if we have submounts */
		if (have_submounts(dentry))
			goto out_valid;
	}

	_debug("dropping dentry %s/%s",
	       parent->d_name.name, dentry->d_name.name);
	shrink_dcache_parent(dentry);
	d_drop(dentry);
	dput(parent);

	_leave(" = 0 [bad]");
	return 0;
}

/*
 * allow the VFS to enquire as to whether a dentry should be unhashed (mustn't
 * sleep)
 * - called from dput() when d_count is going to 0.
 * - return 1 to request dentry be unhashed, 0 otherwise
 */
static int afs_d_delete(struct dentry *dentry)
{
	_enter("%s", dentry->d_name.name);

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		goto zap;

	if (dentry->d_inode &&
	    test_bit(AFS_VNODE_DELETED, &AFS_FS_I(dentry->d_inode)->flags))
			goto zap;

	_leave(" = 0 [keep]");
	return 0;

zap:
	_leave(" = 1 [zap]");
	return 1;
}
