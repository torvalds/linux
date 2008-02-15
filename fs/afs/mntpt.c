/* mountpoint management
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
#include "internal.h"


static struct dentry *afs_mntpt_lookup(struct inode *dir,
				       struct dentry *dentry,
				       struct nameidata *nd);
static int afs_mntpt_open(struct inode *inode, struct file *file);
static void *afs_mntpt_follow_link(struct dentry *dentry, struct nameidata *nd);
static void afs_mntpt_expiry_timed_out(struct work_struct *work);

const struct file_operations afs_mntpt_file_operations = {
	.open		= afs_mntpt_open,
};

const struct inode_operations afs_mntpt_inode_operations = {
	.lookup		= afs_mntpt_lookup,
	.follow_link	= afs_mntpt_follow_link,
	.readlink	= page_readlink,
	.getattr	= afs_getattr,
};

static LIST_HEAD(afs_vfsmounts);
static DECLARE_DELAYED_WORK(afs_mntpt_expiry_timer, afs_mntpt_expiry_timed_out);

static unsigned long afs_mntpt_expiry_timeout = 10 * 60;

/*
 * check a symbolic link to see whether it actually encodes a mountpoint
 * - sets the AFS_VNODE_MOUNTPOINT flag on the vnode appropriately
 */
int afs_mntpt_check_symlink(struct afs_vnode *vnode, struct key *key)
{
	struct file file = {
		.private_data = key,
	};
	struct page *page;
	size_t size;
	char *buf;
	int ret;

	_enter("{%x:%u,%u}",
	       vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique);

	/* read the contents of the symlink into the pagecache */
	page = read_mapping_page(AFS_VNODE_TO_I(vnode)->i_mapping, 0, &file);
	if (IS_ERR(page)) {
		ret = PTR_ERR(page);
		goto out;
	}

	ret = -EIO;
	if (PageError(page))
		goto out_free;

	buf = kmap(page);

	/* examine the symlink's contents */
	size = vnode->status.size;
	_debug("symlink to %*.*s", (int) size, (int) size, buf);

	if (size > 2 &&
	    (buf[0] == '%' || buf[0] == '#') &&
	    buf[size - 1] == '.'
	    ) {
		_debug("symlink is a mountpoint");
		spin_lock(&vnode->lock);
		set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);
		spin_unlock(&vnode->lock);
	}

	ret = 0;

	kunmap(page);
out_free:
	page_cache_release(page);
out:
	_leave(" = %d", ret);
	return ret;
}

/*
 * no valid lookup procedure on this sort of dir
 */
static struct dentry *afs_mntpt_lookup(struct inode *dir,
				       struct dentry *dentry,
				       struct nameidata *nd)
{
	_enter("%p,%p{%p{%s},%s}",
	       dir,
	       dentry,
	       dentry->d_parent,
	       dentry->d_parent ?
	       dentry->d_parent->d_name.name : (const unsigned char *) "",
	       dentry->d_name.name);

	return ERR_PTR(-EREMOTE);
}

/*
 * no valid open procedure on this sort of dir
 */
static int afs_mntpt_open(struct inode *inode, struct file *file)
{
	_enter("%p,%p{%p{%s},%s}",
	       inode, file,
	       file->f_path.dentry->d_parent,
	       file->f_path.dentry->d_parent ?
	       file->f_path.dentry->d_parent->d_name.name :
	       (const unsigned char *) "",
	       file->f_path.dentry->d_name.name);

	return -EREMOTE;
}

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

	_enter("{%s}", mntpt->d_name.name);

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
	if (PageError(page))
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
	_debug("--- attempting mount %s -o %s ---", devname, options);
	mnt = vfs_kern_mount(&afs_fs_type, 0, devname, options);
	_debug("--- mount result %p ---", mnt);

	free_page((unsigned long) devname);
	free_page((unsigned long) options);
	_leave(" = %p", mnt);
	return mnt;

error:
	if (page)
		page_cache_release(page);
	if (devname)
		free_page((unsigned long) devname);
	if (options)
		free_page((unsigned long) options);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * follow a link from a mountpoint directory, thus causing it to be mounted
 */
static void *afs_mntpt_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct vfsmount *newmnt;
	int err;

	_enter("%p{%s},{%s:%p{%s},}",
	       dentry,
	       dentry->d_name.name,
	       nd->path.mnt->mnt_devname,
	       dentry,
	       nd->path.dentry->d_name.name);

	dput(nd->path.dentry);
	nd->path.dentry = dget(dentry);

	newmnt = afs_mntpt_do_automount(nd->path.dentry);
	if (IS_ERR(newmnt)) {
		path_put(&nd->path);
		return (void *)newmnt;
	}

	mntget(newmnt);
	err = do_add_mount(newmnt, nd, MNT_SHRINKABLE, &afs_vfsmounts);
	switch (err) {
	case 0:
		path_put(&nd->path);
		nd->path.mnt = newmnt;
		nd->path.dentry = dget(newmnt->mnt_root);
		schedule_delayed_work(&afs_mntpt_expiry_timer,
				      afs_mntpt_expiry_timeout * HZ);
		break;
	case -EBUSY:
		/* someone else made a mount here whilst we were busy */
		while (d_mountpoint(nd->path.dentry) &&
		       follow_down(&nd->path.mnt, &nd->path.dentry))
			;
		err = 0;
	default:
		mntput(newmnt);
		break;
	}

	_leave(" = %d", err);
	return ERR_PTR(err);
}

/*
 * handle mountpoint expiry timer going off
 */
static void afs_mntpt_expiry_timed_out(struct work_struct *work)
{
	_enter("");

	if (!list_empty(&afs_vfsmounts)) {
		mark_mounts_for_expiry(&afs_vfsmounts);
		schedule_delayed_work(&afs_mntpt_expiry_timer,
				      afs_mntpt_expiry_timeout * HZ);
	}

	_leave("");
}

/*
 * kill the AFS mountpoint timer if it's still running
 */
void afs_mntpt_kill_timer(void)
{
	_enter("");

	ASSERT(list_empty(&afs_vfsmounts));
	cancel_delayed_work(&afs_mntpt_expiry_timer);
	flush_scheduled_work();
}

/*
 * begin unmount by attempting to remove all automounted mountpoints we added
 */
void afs_umount_begin(struct vfsmount *vfsmnt, int flags)
{
	shrink_submounts(vfsmnt, &afs_vfsmounts);
}
