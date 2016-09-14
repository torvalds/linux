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

static int bad_file_open(struct inode *inode, struct file *filp)
{
	return -EIO;
}

static const struct file_operations bad_file_ops =
{
	.open		= bad_file_open,
};

static int bad_inode_create (struct inode *dir, struct dentry *dentry,
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

static int bad_inode_symlink (struct inode *dir, struct dentry *dentry,
		const char *symname)
{
	return -EIO;
}

static int bad_inode_mkdir(struct inode *dir, struct dentry *dentry,
			umode_t mode)
{
	return -EIO;
}

static int bad_inode_rmdir (struct inode *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_inode_mknod (struct inode *dir, struct dentry *dentry,
			umode_t mode, dev_t rdev)
{
	return -EIO;
}

static int bad_inode_rename2(struct inode *old_dir, struct dentry *old_dentry,
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

static int bad_inode_permission(struct inode *inode, int mask)
{
	return -EIO;
}

static int bad_inode_getattr(struct vfsmount *mnt, struct dentry *dentry,
			struct kstat *stat)
{
	return -EIO;
}

static int bad_inode_setattr(struct dentry *direntry, struct iattr *attrs)
{
	return -EIO;
}

static int bad_inode_setxattr(struct dentry *dentry, struct inode *inode,
		const char *name, const void *value, size_t size, int flags)
{
	return -EIO;
}

static ssize_t bad_inode_getxattr(struct dentry *dentry, struct inode *inode,
			const char *name, void *buffer, size_t size)
{
	return -EIO;
}

static ssize_t bad_inode_listxattr(struct dentry *dentry, char *buffer,
			size_t buffer_size)
{
	return -EIO;
}

static int bad_inode_removexattr(struct dentry *dentry, const char *name)
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
	.rename2	= bad_inode_rename2,
	.readlink	= bad_inode_readlink,
	/* follow_link must be no-op, otherwise unmounting this inode
	   won't work */
	/* put_link returns void */
	/* truncate returns void */
	.permission	= bad_inode_permission,
	.getattr	= bad_inode_getattr,
	.setattr	= bad_inode_setattr,
	.setxattr	= bad_inode_setxattr,
	.getxattr	= bad_inode_getxattr,
	.listxattr	= bad_inode_listxattr,
	.removexattr	= bad_inode_removexattr,
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
	inode->i_atime = inode->i_mtime = inode->i_ctime =
		current_time(inode);
	inode->i_op = &bad_inode_ops;	
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
