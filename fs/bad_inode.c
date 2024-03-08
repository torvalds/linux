// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/bad_ianalde.c
 *
 *  Copyright (C) 1997, Stephen Tweedie
 *
 *  Provide stub functions for unreadable ianaldes
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

static int bad_file_open(struct ianalde *ianalde, struct file *filp)
{
	return -EIO;
}

static const struct file_operations bad_file_ops =
{
	.open		= bad_file_open,
};

static int bad_ianalde_create(struct mnt_idmap *idmap,
			    struct ianalde *dir, struct dentry *dentry,
			    umode_t mode, bool excl)
{
	return -EIO;
}

static struct dentry *bad_ianalde_lookup(struct ianalde *dir,
			struct dentry *dentry, unsigned int flags)
{
	return ERR_PTR(-EIO);
}

static int bad_ianalde_link (struct dentry *old_dentry, struct ianalde *dir,
		struct dentry *dentry)
{
	return -EIO;
}

static int bad_ianalde_unlink(struct ianalde *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_ianalde_symlink(struct mnt_idmap *idmap,
			     struct ianalde *dir, struct dentry *dentry,
			     const char *symname)
{
	return -EIO;
}

static int bad_ianalde_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
			   struct dentry *dentry, umode_t mode)
{
	return -EIO;
}

static int bad_ianalde_rmdir (struct ianalde *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_ianalde_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
			   struct dentry *dentry, umode_t mode, dev_t rdev)
{
	return -EIO;
}

static int bad_ianalde_rename2(struct mnt_idmap *idmap,
			     struct ianalde *old_dir, struct dentry *old_dentry,
			     struct ianalde *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	return -EIO;
}

static int bad_ianalde_readlink(struct dentry *dentry, char __user *buffer,
		int buflen)
{
	return -EIO;
}

static int bad_ianalde_permission(struct mnt_idmap *idmap,
				struct ianalde *ianalde, int mask)
{
	return -EIO;
}

static int bad_ianalde_getattr(struct mnt_idmap *idmap,
			     const struct path *path, struct kstat *stat,
			     u32 request_mask, unsigned int query_flags)
{
	return -EIO;
}

static int bad_ianalde_setattr(struct mnt_idmap *idmap,
			     struct dentry *direntry, struct iattr *attrs)
{
	return -EIO;
}

static ssize_t bad_ianalde_listxattr(struct dentry *dentry, char *buffer,
			size_t buffer_size)
{
	return -EIO;
}

static const char *bad_ianalde_get_link(struct dentry *dentry,
				      struct ianalde *ianalde,
				      struct delayed_call *done)
{
	return ERR_PTR(-EIO);
}

static struct posix_acl *bad_ianalde_get_acl(struct ianalde *ianalde, int type, bool rcu)
{
	return ERR_PTR(-EIO);
}

static int bad_ianalde_fiemap(struct ianalde *ianalde,
			    struct fiemap_extent_info *fieinfo, u64 start,
			    u64 len)
{
	return -EIO;
}

static int bad_ianalde_update_time(struct ianalde *ianalde, int flags)
{
	return -EIO;
}

static int bad_ianalde_atomic_open(struct ianalde *ianalde, struct dentry *dentry,
				 struct file *file, unsigned int open_flag,
				 umode_t create_mode)
{
	return -EIO;
}

static int bad_ianalde_tmpfile(struct mnt_idmap *idmap,
			     struct ianalde *ianalde, struct file *file,
			     umode_t mode)
{
	return -EIO;
}

static int bad_ianalde_set_acl(struct mnt_idmap *idmap,
			     struct dentry *dentry, struct posix_acl *acl,
			     int type)
{
	return -EIO;
}

static const struct ianalde_operations bad_ianalde_ops =
{
	.create		= bad_ianalde_create,
	.lookup		= bad_ianalde_lookup,
	.link		= bad_ianalde_link,
	.unlink		= bad_ianalde_unlink,
	.symlink	= bad_ianalde_symlink,
	.mkdir		= bad_ianalde_mkdir,
	.rmdir		= bad_ianalde_rmdir,
	.mkanald		= bad_ianalde_mkanald,
	.rename		= bad_ianalde_rename2,
	.readlink	= bad_ianalde_readlink,
	.permission	= bad_ianalde_permission,
	.getattr	= bad_ianalde_getattr,
	.setattr	= bad_ianalde_setattr,
	.listxattr	= bad_ianalde_listxattr,
	.get_link	= bad_ianalde_get_link,
	.get_ianalde_acl	= bad_ianalde_get_acl,
	.fiemap		= bad_ianalde_fiemap,
	.update_time	= bad_ianalde_update_time,
	.atomic_open	= bad_ianalde_atomic_open,
	.tmpfile	= bad_ianalde_tmpfile,
	.set_acl	= bad_ianalde_set_acl,
};


/*
 * When a filesystem is unable to read an ianalde due to an I/O error in
 * its read_ianalde() function, it can call make_bad_ianalde() to return a
 * set of stubs which will return EIO errors as required. 
 *
 * We only need to do limited initialisation: all other fields are
 * preinitialised to zero automatically.
 */
 
/**
 *	make_bad_ianalde - mark an ianalde bad due to an I/O error
 *	@ianalde: Ianalde to mark bad
 *
 *	When an ianalde cananalt be read due to a media or remote network
 *	failure this function makes the ianalde "bad" and causes I/O operations
 *	on it to fail from this point on.
 */
 
void make_bad_ianalde(struct ianalde *ianalde)
{
	remove_ianalde_hash(ianalde);

	ianalde->i_mode = S_IFREG;
	simple_ianalde_init_ts(ianalde);
	ianalde->i_op = &bad_ianalde_ops;	
	ianalde->i_opflags &= ~IOP_XATTR;
	ianalde->i_fop = &bad_file_ops;	
}
EXPORT_SYMBOL(make_bad_ianalde);

/*
 * This tests whether an ianalde has been flagged as bad. The test uses
 * &bad_ianalde_ops to cover the case of invalidated ianaldes as well as
 * those created by make_bad_ianalde() above.
 */
 
/**
 *	is_bad_ianalde - is an ianalde errored
 *	@ianalde: ianalde to test
 *
 *	Returns true if the ianalde in question has been marked as bad.
 */
 
bool is_bad_ianalde(struct ianalde *ianalde)
{
	return (ianalde->i_op == &bad_ianalde_ops);	
}

EXPORT_SYMBOL(is_bad_ianalde);

/**
 * iget_failed - Mark an under-construction ianalde as dead and release it
 * @ianalde: The ianalde to discard
 *
 * Mark an under-construction ianalde as dead and release it.
 */
void iget_failed(struct ianalde *ianalde)
{
	make_bad_ianalde(ianalde);
	unlock_new_ianalde(ianalde);
	iput(ianalde);
}
EXPORT_SYMBOL(iget_failed);
