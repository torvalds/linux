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
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/smp_lock.h>
#include <linux/namei.h>

static int return_EIO(void)
{
	return -EIO;
}

#define EIO_ERROR ((void *) (return_EIO))

static const struct file_operations bad_file_ops =
{
	.llseek		= EIO_ERROR,
	.aio_read	= EIO_ERROR,
	.read		= EIO_ERROR,
	.write		= EIO_ERROR,
	.aio_write	= EIO_ERROR,
	.readdir	= EIO_ERROR,
	.poll		= EIO_ERROR,
	.ioctl		= EIO_ERROR,
	.mmap		= EIO_ERROR,
	.open		= EIO_ERROR,
	.flush		= EIO_ERROR,
	.release	= EIO_ERROR,
	.fsync		= EIO_ERROR,
	.aio_fsync	= EIO_ERROR,
	.fasync		= EIO_ERROR,
	.lock		= EIO_ERROR,
	.sendfile	= EIO_ERROR,
	.sendpage	= EIO_ERROR,
	.get_unmapped_area = EIO_ERROR,
};

static struct inode_operations bad_inode_ops =
{
	.create		= EIO_ERROR,
	.lookup		= EIO_ERROR,
	.link		= EIO_ERROR,
	.unlink		= EIO_ERROR,
	.symlink	= EIO_ERROR,
	.mkdir		= EIO_ERROR,
	.rmdir		= EIO_ERROR,
	.mknod		= EIO_ERROR,
	.rename		= EIO_ERROR,
	.readlink	= EIO_ERROR,
	/* follow_link must be no-op, otherwise unmounting this inode
	   won't work */
	.truncate	= EIO_ERROR,
	.permission	= EIO_ERROR,
	.getattr	= EIO_ERROR,
	.setattr	= EIO_ERROR,
	.setxattr	= EIO_ERROR,
	.getxattr	= EIO_ERROR,
	.listxattr	= EIO_ERROR,
	.removexattr	= EIO_ERROR,
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
 
void make_bad_inode(struct inode * inode) 
{
	remove_inode_hash(inode);

	inode->i_mode = S_IFREG;
	inode->i_atime = inode->i_mtime = inode->i_ctime =
		current_fs_time(inode->i_sb);
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
 
int is_bad_inode(struct inode * inode) 
{
	return (inode->i_op == &bad_inode_ops);	
}

EXPORT_SYMBOL(is_bad_inode);
