/* mntpt.c: mountpoint management
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
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/mnt_namespace.h>
#include "super.h"
#include "cell.h"
#include "volume.h"
#include "vnode.h"
#include "internal.h"


static struct dentry *afs_mntpt_lookup(struct inode *dir,
				       struct dentry *dentry,
				       struct nameidata *nd);
static int afs_mntpt_open(struct inode *inode, struct file *file);
static void *afs_mntpt_follow_link(struct dentry *dentry, struct nameidata *nd);

const struct file_operations afs_mntpt_file_operations = {
	.open		= afs_mntpt_open,
};

const struct inode_operations afs_mntpt_inode_operations = {
	.lookup		= afs_mntpt_lookup,
	.follow_link	= afs_mntpt_follow_link,
	.readlink	= page_readlink,
	.getattr	= afs_inode_getattr,
};

static LIST_HEAD(afs_vfsmounts);

static void afs_mntpt_expiry_timed_out(struct afs_timer *timer);

struct afs_timer_ops afs_mntpt_expiry_timer_ops = {
	.timed_out	= afs_mntpt_expiry_timed_out,
};

struct afs_timer afs_mntpt_expiry_timer;

unsigned long afs_mntpt_expiry_timeout = 20;

/*****************************************************************************/
/*
 * check a symbolic link to see whether it actually encodes a mountpoint
 * - sets the AFS_VNODE_MOUNTPOINT flag on the vnode appropriately
 */
int afs_mntpt_check_symlink(struct afs_vnode *vnode)
{
	struct page *page;
	size_t size;
	char *buf;
	int ret;

	_enter("{%u,%u}", vnode->fid.vnode, vnode->fid.unique);

	/* read the contents of the symlink into the pagecache */
	page = read_mapping_page(AFS_VNODE_TO_I(vnode)->i_mapping, 0, NULL);
	if (IS_ERR(page)) {
		ret = PTR_ERR(page);
		goto out;
	}

	ret = -EIO;
	wait_on_page_locked(page);
	buf = kmap(page);
	if (!PageUptodate(page))
		goto out_free;
	if (PageError(page))
		goto out_free;

	/* examine the symlink's contents */
	size = vnode->status.size;
	_debug("symlink to %*.*s", size, (int) size, buf);

	if (size > 2 &&
	    (buf[0] == '%' || buf[0] == '#') &&
	    buf[size - 1] == '.'
	    ) {
		_debug("symlink is a mountpoint");
		spin_lock(&vnode->lock);
		vnode->flags |= AFS_VNODE_MOUNTPOINT;
		spin_unlock(&vnode->lock);
	}

	ret = 0;

 out_free:
	kunmap(page);
	page_cache_release(page);
 out:
	_leave(" = %d", ret);
	return ret;

} /* end afs_mntpt_check_symlink() */

/*****************************************************************************/
/*
 * no valid lookup procedure on this sort of dir
 */
static struct dentry *afs_mntpt_lookup(struct inode *dir,
				       struct dentry *dentry,
				       struct nameidata *nd)
{
	kenter("%p,%p{%p{%s},%s}",
	       dir,
	       dentry,
	       dentry->d_parent,
	       dentry->d_parent ?
	       dentry->d_parent->d_name.name : (const unsigned char *) "",
	       dentry->d_name.name);

	return ERR_PTR(-EREMOTE);
} /* end afs_mntpt_lookup() */

/*****************************************************************************/
/*
 * no valid open procedure on this sort of dir
 */
static int afs_mntpt_open(struct inode *inode, struct file *file)
{
	kenter("%p,%p{%p{%s},%s}",
	       inode, file,
	       file->f_path.dentry->d_parent,
	       file->f_path.dentry->d_parent ?
	       file->f_path.dentry->d_parent->d_name.name :
	       (const unsigned char *) "",
	       file->f_path.dentry->d_name.name);

	return -EREMOTE;
} /* end afs_mntpt_open() */

/*****************************************************************************/
/*
 * create a vfsmount to be automounted
 */
static struct vfsmount *afs_mntpt_do_automount(struct dentry *mntpt)
{
	struct afs_super_info *super;
	struct vfsmount *mnt;
	struct page *page = NULL;
	size_t size;
	char *buf, *devname = NULL, *options = NULL;
	int ret;

	kenter("{%s}", mntpt->d_name.name);

	BUG_ON(!mntpt->d_inode);

	ret = -EINVAL;
	size = mntpt->d_inode->i_size;
	if (size > PAGE_SIZE - 1)
		goto error;

	ret = -ENOMEM;
	devname = (char *) get_zeroed_page(GFP_KERNEL);
	if (!devname)
		goto error;

	options = (char *) get_zeroed_page(GFP_KERNEL);
	if (!options)
		goto error;

	/* read the contents of the AFS special symlink */
	page = read_mapping_page(mntpt->d_inode->i_mapping, 0, NULL);
	if (IS_ERR(page)) {
		ret = PTR_ERR(page);
		goto error;
	}

	ret = -EIO;
	wait_on_page_locked(page);
	if (!PageUptodate(page) || PageError(page))
		goto error;

	buf = kmap(page);
	memcpy(devname, buf, size);
	kunmap(page);
	page_cache_release(page);
	page = NULL;

	/* work out what options we want */
	super = AFS_FS_S(mntpt->d_sb);
	memcpy(options, "cell=", 5);
	strcpy(options + 5, super->volume->cell->name);
	if (super->volume->type == AFSVL_RWVOL)
		strcat(options, ",rwpath");

	/* try and do the mount */
	kdebug("--- attempting mount %s -o %s ---", devname, options);
	mnt = vfs_kern_mount(&afs_fs_type, 0, devname, options);
	kdebug("--- mount result %p ---", mnt);

	free_page((unsigned long) devname);
	free_page((unsigned long) options);
	kleave(" = %p", mnt);
	return mnt;

 error:
	if (page)
		page_cache_release(page);
	if (devname)
		free_page((unsigned long) devname);
	if (options)
		free_page((unsigned long) options);
	kleave(" = %d", ret);
	return ERR_PTR(ret);
} /* end afs_mntpt_do_automount() */

/*****************************************************************************/
/*
 * follow a link from a mountpoint directory, thus causing it to be mounted
 */
static void *afs_mntpt_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct vfsmount *newmnt;
	struct dentry *old_dentry;
	int err;

	kenter("%p{%s},{%s:%p{%s}}",
	       dentry,
	       dentry->d_name.name,
	       nd->mnt->mnt_devname,
	       dentry,
	       nd->dentry->d_name.name);

	newmnt = afs_mntpt_do_automount(dentry);
	if (IS_ERR(newmnt)) {
		path_release(nd);
		return (void *)newmnt;
	}

	old_dentry = nd->dentry;
	nd->dentry = dentry;
	err = do_add_mount(newmnt, nd, 0, &afs_vfsmounts);
	nd->dentry = old_dentry;

	path_release(nd);

	if (!err) {
		mntget(newmnt);
		nd->mnt = newmnt;
		dget(newmnt->mnt_root);
		nd->dentry = newmnt->mnt_root;
	}

	kleave(" = %d", err);
	return ERR_PTR(err);
} /* end afs_mntpt_follow_link() */

/*****************************************************************************/
/*
 * handle mountpoint expiry timer going off
 */
static void afs_mntpt_expiry_timed_out(struct afs_timer *timer)
{
	kenter("");

	mark_mounts_for_expiry(&afs_vfsmounts);

	afs_kafstimod_add_timer(&afs_mntpt_expiry_timer,
				afs_mntpt_expiry_timeout * HZ);

	kleave("");
} /* end afs_mntpt_expiry_timed_out() */
