// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/bad_iyesde.c
 *
 *  Copyright (C) 1997, Stephen Tweedie
 *
 *  Provide stub functions for unreadable iyesdes
 *
 *  Fabian Frederick : August 2003 - All file operations assigned to EIO
 */

#include <linux/fs.h>
#include <linux/export.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/namei.h>
#include <linux/poll.h>

static int bad_file_open(struct iyesde *iyesde, struct file *filp)
{
	return -EIO;
}

static const struct file_operations bad_file_ops =
{
	.open		= bad_file_open,
};

static int bad_iyesde_create (struct iyesde *dir, struct dentry *dentry,
		umode_t mode, bool excl)
{
	return -EIO;
}

static struct dentry *bad_iyesde_lookup(struct iyesde *dir,
			struct dentry *dentry, unsigned int flags)
{
	return ERR_PTR(-EIO);
}

static int bad_iyesde_link (struct dentry *old_dentry, struct iyesde *dir,
		struct dentry *dentry)
{
	return -EIO;
}

static int bad_iyesde_unlink(struct iyesde *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_iyesde_symlink (struct iyesde *dir, struct dentry *dentry,
		const char *symname)
{
	return -EIO;
}

static int bad_iyesde_mkdir(struct iyesde *dir, struct dentry *dentry,
			umode_t mode)
{
	return -EIO;
}

static int bad_iyesde_rmdir (struct iyesde *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_iyesde_mkyesd (struct iyesde *dir, struct dentry *dentry,
			umode_t mode, dev_t rdev)
{
	return -EIO;
}

static int bad_iyesde_rename2(struct iyesde *old_dir, struct dentry *old_dentry,
			     struct iyesde *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	return -EIO;
}

static int bad_iyesde_readlink(struct dentry *dentry, char __user *buffer,
		int buflen)
{
	return -EIO;
}

static int bad_iyesde_permission(struct iyesde *iyesde, int mask)
{
	return -EIO;
}

static int bad_iyesde_getattr(const struct path *path, struct kstat *stat,
			     u32 request_mask, unsigned int query_flags)
{
	return -EIO;
}

static int bad_iyesde_setattr(struct dentry *direntry, struct iattr *attrs)
{
	return -EIO;
}

static ssize_t bad_iyesde_listxattr(struct dentry *dentry, char *buffer,
			size_t buffer_size)
{
	return -EIO;
}

static const char *bad_iyesde_get_link(struct dentry *dentry,
				      struct iyesde *iyesde,
				      struct delayed_call *done)
{
	return ERR_PTR(-EIO);
}

static struct posix_acl *bad_iyesde_get_acl(struct iyesde *iyesde, int type)
{
	return ERR_PTR(-EIO);
}

static int bad_iyesde_fiemap(struct iyesde *iyesde,
			    struct fiemap_extent_info *fieinfo, u64 start,
			    u64 len)
{
	return -EIO;
}

static int bad_iyesde_update_time(struct iyesde *iyesde, struct timespec64 *time,
				 int flags)
{
	return -EIO;
}

static int bad_iyesde_atomic_open(struct iyesde *iyesde, struct dentry *dentry,
				 struct file *file, unsigned int open_flag,
				 umode_t create_mode)
{
	return -EIO;
}

static int bad_iyesde_tmpfile(struct iyesde *iyesde, struct dentry *dentry,
			     umode_t mode)
{
	return -EIO;
}

static int bad_iyesde_set_acl(struct iyesde *iyesde, struct posix_acl *acl,
			     int type)
{
	return -EIO;
}

static const struct iyesde_operations bad_iyesde_ops =
{
	.create		= bad_iyesde_create,
	.lookup		= bad_iyesde_lookup,
	.link		= bad_iyesde_link,
	.unlink		= bad_iyesde_unlink,
	.symlink	= bad_iyesde_symlink,
	.mkdir		= bad_iyesde_mkdir,
	.rmdir		= bad_iyesde_rmdir,
	.mkyesd		= bad_iyesde_mkyesd,
	.rename		= bad_iyesde_rename2,
	.readlink	= bad_iyesde_readlink,
	.permission	= bad_iyesde_permission,
	.getattr	= bad_iyesde_getattr,
	.setattr	= bad_iyesde_setattr,
	.listxattr	= bad_iyesde_listxattr,
	.get_link	= bad_iyesde_get_link,
	.get_acl	= bad_iyesde_get_acl,
	.fiemap		= bad_iyesde_fiemap,
	.update_time	= bad_iyesde_update_time,
	.atomic_open	= bad_iyesde_atomic_open,
	.tmpfile	= bad_iyesde_tmpfile,
	.set_acl	= bad_iyesde_set_acl,
};


/*
 * When a filesystem is unable to read an iyesde due to an I/O error in
 * its read_iyesde() function, it can call make_bad_iyesde() to return a
 * set of stubs which will return EIO errors as required. 
 *
 * We only need to do limited initialisation: all other fields are
 * preinitialised to zero automatically.
 */
 
/**
 *	make_bad_iyesde - mark an iyesde bad due to an I/O error
 *	@iyesde: Iyesde to mark bad
 *
 *	When an iyesde canyest be read due to a media or remote network
 *	failure this function makes the iyesde "bad" and causes I/O operations
 *	on it to fail from this point on.
 */
 
void make_bad_iyesde(struct iyesde *iyesde)
{
	remove_iyesde_hash(iyesde);

	iyesde->i_mode = S_IFREG;
	iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime =
		current_time(iyesde);
	iyesde->i_op = &bad_iyesde_ops;	
	iyesde->i_opflags &= ~IOP_XATTR;
	iyesde->i_fop = &bad_file_ops;	
}
EXPORT_SYMBOL(make_bad_iyesde);

/*
 * This tests whether an iyesde has been flagged as bad. The test uses
 * &bad_iyesde_ops to cover the case of invalidated iyesdes as well as
 * those created by make_bad_iyesde() above.
 */
 
/**
 *	is_bad_iyesde - is an iyesde errored
 *	@iyesde: iyesde to test
 *
 *	Returns true if the iyesde in question has been marked as bad.
 */
 
bool is_bad_iyesde(struct iyesde *iyesde)
{
	return (iyesde->i_op == &bad_iyesde_ops);	
}

EXPORT_SYMBOL(is_bad_iyesde);

/**
 * iget_failed - Mark an under-construction iyesde as dead and release it
 * @iyesde: The iyesde to discard
 *
 * Mark an under-construction iyesde as dead and release it.
 */
void iget_failed(struct iyesde *iyesde)
{
	make_bad_iyesde(iyesde);
	unlock_new_iyesde(iyesde);
	iput(iyesde);
}
EXPORT_SYMBOL(iget_failed);
