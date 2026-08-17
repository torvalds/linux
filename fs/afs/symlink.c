// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS filesystem symbolic link handling
 *
 * Copyright (C) 2026 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/iov_iter.h>
#include "internal.h"

static void afs_put_symlink(struct afs_symlink *symlink)
{
	if (refcount_dec_and_test(&symlink->ref))
		kfree_rcu(symlink, rcu);
}

static void afs_replace_symlink(struct afs_vnode *vnode, struct afs_symlink *symlink)
{
	struct afs_symlink *old;

	old = rcu_replace_pointer(vnode->symlink, symlink,
				  lockdep_is_held(&vnode->validate_lock));
	if (old)
		afs_put_symlink(old);
}

/*
 * In the event that a third-party update of a symlink occurs, dispose of the
 * copy of the old contents.  Called under ->validate_lock.
 */
void afs_invalidate_symlink(struct afs_vnode *vnode)
{
	afs_replace_symlink(vnode, NULL);
}

/*
 * Dispose of a symlink copy during inode deletion.
 */
void afs_evict_symlink(struct afs_vnode *vnode)
{
	struct afs_symlink *old;

	old = rcu_replace_pointer(vnode->symlink, NULL, true);
	if (old)
		afs_put_symlink(old);

}

/*
 * Set up a locally created symlink inode for immediate write to the cache.
 */
void afs_init_new_symlink(struct afs_vnode *vnode, struct afs_operation *op)
{
	struct afs_symlink *symlink = op->create.symlink;
	size_t dsize = 0;
	size_t size = strlen(symlink->content) + 1;
	char *p;

	rcu_assign_pointer(vnode->symlink, symlink);
	op->create.symlink = NULL;

	if (!fscache_cookie_enabled(netfs_i_cookie(&vnode->netfs)))
		return;

	if (netfs_alloc_folioq_buffer(NULL, &vnode->directory, &dsize, size,
				      mapping_gfp_mask(vnode->netfs.inode.i_mapping)) < 0)
		return;

	vnode->directory_size = dsize;
	p = kmap_local_folio(folioq_folio(vnode->directory, 0), 0);
	memcpy(p, symlink->content, size);
	kunmap_local(p);
	netfs_single_mark_inode_dirty(&vnode->netfs.inode);
}

/*
 * Read a symlink in a single download.
 */
static ssize_t afs_do_read_symlink(struct afs_vnode *vnode)
{
	struct afs_symlink *symlink;
	struct iov_iter iter;
	ssize_t ret;
	loff_t i_size;

	i_size = i_size_read(&vnode->netfs.inode);
	if (i_size > PAGE_SIZE - 1) {
		trace_afs_file_error(vnode, -EFBIG, afs_file_error_dir_big);
		return -EFBIG;
	}

	if (!vnode->directory) {
		size_t cur_size = 0;

		ret = netfs_alloc_folioq_buffer(NULL,
						&vnode->directory, &cur_size, PAGE_SIZE,
						mapping_gfp_mask(vnode->netfs.inode.i_mapping));
		vnode->directory_size = PAGE_SIZE - 1;
		if (ret < 0)
			return ret;
	}

	iov_iter_folio_queue(&iter, ITER_DEST, vnode->directory, 0, 0, PAGE_SIZE);

	/* AFS requires us to perform the read of a symlink as a single unit to
	 * avoid issues with the content being changed between reads.
	 */
	ret = netfs_read_single(&vnode->netfs.inode, NULL, &iter);
	if (ret >= 0) {
		i_size = ret;
		if (i_size > PAGE_SIZE - 1) {
			trace_afs_file_error(vnode, -EFBIG, afs_file_error_dir_big);
			return -EFBIG;
		}
		vnode->directory_size = i_size;

		/* Copy the symlink. */
		symlink = kmalloc_flex(struct afs_symlink, content, i_size + 1,
				       GFP_KERNEL);
		if (!symlink)
			return -ENOMEM;

		refcount_set(&symlink->ref, 1);
		symlink->content[i_size] = 0;

		const char *s = kmap_local_folio(folioq_folio(vnode->directory, 0), 0);

		memcpy(symlink->content, s, i_size);
		kunmap_local(s);

		afs_replace_symlink(vnode, symlink);
	}

	if (!fscache_cookie_enabled(netfs_i_cookie(&vnode->netfs))) {
		netfs_free_folioq_buffer(vnode->directory);
		vnode->directory = NULL;
		vnode->directory_size = 0;
	}

	return ret;
}

static ssize_t afs_read_symlink(struct afs_vnode *vnode)
{
	ssize_t ret;

	fscache_use_cookie(afs_vnode_cache(vnode), false);
	ret = afs_do_read_symlink(vnode);
	fscache_unuse_cookie(afs_vnode_cache(vnode), NULL, NULL);
	return ret;
}

static void afs_put_link(void *arg)
{
	afs_put_symlink(arg);
}

const char *afs_get_link(struct dentry *dentry, struct inode *inode,
			 struct delayed_call *callback)
{
	struct afs_symlink *symlink;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	ssize_t ret;

	if (!dentry) {
		/* RCU pathwalk. */
		symlink = rcu_dereference(vnode->symlink);
		if (!symlink || !afs_check_validity(vnode))
			return ERR_PTR(-ECHILD);
		set_delayed_call(callback, NULL, NULL);
		return symlink->content;
	}

	if (vnode->symlink) {
		ret = afs_validate(vnode, NULL);
		if (ret < 0)
			return ERR_PTR(ret);

		down_read(&vnode->validate_lock);
		if (vnode->symlink)
			goto good;
		up_read(&vnode->validate_lock);
	}

	if (down_write_killable(&vnode->validate_lock) < 0)
		return ERR_PTR(-ERESTARTSYS);
	if (!vnode->symlink) {
		ret = afs_read_symlink(vnode);
		if (ret < 0) {
			up_write(&vnode->validate_lock);
			return ERR_PTR(ret);
		}
	}

	downgrade_write(&vnode->validate_lock);
	
good:
	symlink = rcu_dereference_protected(vnode->symlink,
					    lockdep_is_held(&vnode->validate_lock));
	refcount_inc(&symlink->ref);
	up_read(&vnode->validate_lock);

	set_delayed_call(callback, afs_put_link, symlink);
	return symlink->content;
}

int afs_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	DEFINE_DELAYED_CALL(done);
	const char *content;
	int len;

	content = afs_get_link(dentry, d_inode(dentry), &done);
	if (IS_ERR(content)) {
		do_delayed_call(&done);
		return PTR_ERR(content);
	}

	len = umin(strlen(content), buflen);
	if (copy_to_user(buffer, content, len))
		len = -EFAULT;
	do_delayed_call(&done);
	return len;
}

/*
 * Write the symlink contents to the cache as a single blob.  We then throw
 * away the page we used to receive it.
 */
int afs_symlink_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	struct iov_iter iter;
	int ret = 0;

	if (!down_read_trylock(&vnode->validate_lock)) {
		if (wbc->sync_mode == WB_SYNC_NONE) {
			/* The VFS will have undirtied the inode. */
			netfs_single_mark_inode_dirty(&vnode->netfs.inode);
			return 0;
		}
		down_read(&vnode->validate_lock);
	}

	if (vnode->directory &&
	    atomic64_read(&vnode->cb_expires_at) != AFS_NO_CB_PROMISE) {
		iov_iter_folio_queue(&iter, ITER_SOURCE, vnode->directory, 0, 0,
				     i_size_read(&vnode->netfs.inode));
		ret = netfs_writeback_single(mapping, wbc, &iter);
	}

	if (ret == 0) {
		netfs_wb_begin(&vnode->netfs, false);
		netfs_free_folioq_buffer(vnode->directory);
		vnode->directory = NULL;
		vnode->directory_size = 0;
		netfs_wb_end(&vnode->netfs);
	} else if (ret == 1) {
		ret = 0; /* Skipped write due to lock conflict. */
	}

	up_read(&vnode->validate_lock);
	return ret;
}

const struct inode_operations afs_symlink_inode_operations = {
	.get_link	= afs_get_link,
	.readlink	= afs_readlink,
};

const struct address_space_operations afs_symlink_aops = {
	.writepages	= afs_symlink_writepages,
};
