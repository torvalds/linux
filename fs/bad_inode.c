// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/bad_inode.c
 *
 *  Copyright (C) 1997, Stephen Tweedie
 *
 *  Provide stub functions for unreadable inodes
 *
 *  Fabian Frederick : August 2003 - All file operations assigned to EIO
 */

#include <linux/fs.h>
#include <linux/export.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/fiemap.h>

static int bad_file_open(struct inode *inode, struct file *filp)
{
	return -EIO;
}

static const struct file_operations bad_file_ops =
{
	.open		= bad_file_open,
};

static int bad_inode_create(struct mnt_idmap *idmap,
			    struct inode *dir, struct dentry *dentry,
			    umode_t mode, bool excl)
{
	return -EIO;
}

static struct dentry *bad_inode_lookup(struct inode *dir,
			struct dentry *dentry, unsigned int flags)
{
	return ERR_PTR(-EIO);
}

static int bad_inode_link (struct dentry *old_dentry, struct inode *dir,
		struct dentry *dentry)
{
	return -EIO;
}

static int bad_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_inode_symlink(struct mnt_idmap *idmap,
			     struct inode *dir, struct dentry *dentry,
			     const char *symname)
{
	return -EIO;
}

static int bad_inode_mkdir(struct mnt_idmap *idmap, struct inode *dir,
			   struct dentry *dentry, umode_t mode)
{
	return -EIO;
}

static int bad_inode_rmdir (struct inode *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_inode_mknod(struct mnt_idmap *idmap, struct inode *dir,
			   struct dentry *dentry, umode_t mode, dev_t rdev)
{
	return -EIO;
}

static int bad_inode_rename2(struct mnt_idmap *idmap,
			     struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	return -EIO;
}

static int bad_inode_readlink(struct dentry *dentry, char __user *buffer,
		int buflen)
{
	return -EIO;
}

static int bad_inode_permission(struct mnt_idmap *idmap,
				struct inode *inode, int mask)
{
	return -EIO;
}

static int bad_inode_getattr(struct mnt_idmap *idmap,
			     const struct path *path, struct kstat *stat,
			     u32 request_mask, unsigned int query_flags)
{
	return -EIO;
}

static int bad_inode_setattr(struct mnt_idmap *idmap,
			     struct dentry *direntry, struct iattr *attrs)
{
	return -EIO;
}

static ssize_t bad_inode_listxattr(struct dentry *dentry, char *buffer,
			size_t buffer_size)
{
	return -EIO;
}

static const char *bad_inode_get_link(struct dentry *dentry,
				      struct inode *inode,
				      struct delayed_call *done)
{
	return ERR_PTR(-EIO);
}

static struct posix_acl *bad_inode_get_acl(struct inode *inode, int type, bool rcu)
{
	return ERR_PTR(-EIO);
}

static int bad_inode_fiemap(struct inode *inode,
			    struct fiemap_extent_info *fieinfo, u64 start,
			    u64 len)
{
	return -EIO;
}

static int bad_inode_update_time(struct inode *inode, int flags)
{
	return -EIO;
}

static int bad_inode_atomic_open(struct inode *inode, struct dentry *dentry,
				 struct file *file, unsigned int open_flag,
				 umode_t create_mode)
{
	return -EIO;
}

static int bad_inode_tmpfile(struct mnt_idmap *idmap,
			     struct inode *inode, struct file *file,
			     umode_t mode)
{
	return -EIO;
}

static int bad_inode_set_acl(struct mnt_idmap *idmap,
			     struct dentry *dentry, struct posix_acl *acl,
			     int type)
{
	return -EIO;
}

static const struct inode_operations bad_inode_ops =
{
	.create		= bad_inode_create,
	.lookup		= bad_inode_lookup,
	.link		= bad_inode_link,
	.unlink		= bad_inode_unlink,
	.symlink	= bad_inode_symlink,
	.mkdir		= bad_inode_mkdir,
	.rmdir		= bad_inode_rmdir,
	.mknod		= bad_inode_mknod,
	.rename		= bad_inode_rename2,
	.readlink	= bad_inode_readlink,
	.permission	= bad_inode_permission,
	.getattr	= bad_inode_getattr,
	.setattr	= bad_inode_setattr,
	.listxattr	= bad_inode_listxattr,
	.get_link	= bad_inode_get_link,
	.get_inode_acl	= bad_inode_get_acl,
	.fiemap		= bad_inode_fiemap,
	.update_time	= bad_inode_update_time,
	.atomic_open	= bad_inode_atomic_open,
	.tmpfile	= bad_inode_tmpfile,
	.set_acl	= bad_inode_set_acl,
};


/*
 * When a filesystem is unable to read an inode due to an I/O error in
 * its read_inode() function, it can call make_bad_inode() to return a
 * set of stubs which will return EIO errors as required. 
 *
 * We only need to do limited initialisation: all other fields are
 * preinitialised to zero automatically.
 */
 
/**
 *	make_bad_inode - mark an inode bad due to an I/O error
 *	@inode: Inode to mark bad
 *
 *	When an inode cannot be read due to a media or remote network
 *	failure this function makes the inode "bad" and causes I/O operations
 *	on it to fail from this point on.
 */
 
void make_bad_inode(struct inode *inode)
{
	remove_inode_hash(inode);

	inode->i_mode = S_IFREG;
	inode->i_atime = inode->i_mtime = inode_set_ctime_current(inode);
	inode->i_op = &bad_inode_ops;	
	inode->i_opflags &= ~IOP_XATTR;
	inode->i_fop = &bad_file_ops;	
}
EXPORT_SYMBOL(make_bad_inode);

/*
 * This tests whether an inode has been flagged as bad. The test uses
 * &bad_inode_ops to cover the case of invalidated inodes as well as
 * those created by make_bad_inode() above.
 */
 
/**
 *	is_bad_inode - is an inode errored
 *	@inode: inode to test
 *
 *	Returns true if the inode in question has been marked as bad.
 */
 
bool is_bad_inode(struct inode *inode)
{
	return (inode->i_op == &bad_inode_ops);	
}

EXPORT_SYMBOL(is_bad_inode);

/**
 * iget_failed - Mark an under-construction inode as dead and release it
 * @inode: The inode to discard
 *
 * Mark an under-construction inode as dead and release it.
 */
void iget_failed(struct inode *inode)
{
	make_bad_inode(inode);
	unlock_new_inode(inode);
	iput(inode);
}
EXPORT_SYMBOL(iget_failed);
