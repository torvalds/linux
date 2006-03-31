/*
 *  linux/fs/9p/vfs_file.c
 *
 * This file contians vfs file ops for 9P2000.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/inet.h>
#include <linux/version.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * v9fs_file_open - open a file (or directory)
 * @inode: inode to be opened
 * @file: file being opened
 *
 */

int v9fs_file_open(struct inode *inode, struct file *file)
{
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);
	struct v9fs_fid *vfid;
	struct v9fs_fcall *fcall = NULL;
	int omode;
	int fid = V9FS_NOFID;
	int err;

	dprintk(DEBUG_VFS, "inode: %p file: %p \n", inode, file);

	vfid = v9fs_fid_lookup(file->f_dentry);
	if (!vfid) {
		dprintk(DEBUG_ERROR, "Couldn't resolve fid from dentry\n");
		return -EBADF;
	}

	fid = v9fs_get_idpool(&v9ses->fidpool);
	if (fid < 0) {
		eprintk(KERN_WARNING, "newfid fails!\n");
		return -ENOSPC;
	}

	err = v9fs_t_walk(v9ses, vfid->fid, fid, NULL, NULL);
	if (err < 0) {
		dprintk(DEBUG_ERROR, "rewalk didn't work\n");
		goto put_fid;
	}

	/* TODO: do special things for O_EXCL, O_NOFOLLOW, O_SYNC */
	/* translate open mode appropriately */
	omode = v9fs_uflags2omode(file->f_flags);
	err = v9fs_t_open(v9ses, fid, omode, &fcall);
	if (err < 0) {
		PRINT_FCALL_ERROR("open failed", fcall);
		goto clunk_fid;
	}

	vfid = kmalloc(sizeof(struct v9fs_fid), GFP_KERNEL);
	if (vfid == NULL) {
		dprintk(DEBUG_ERROR, "out of memory\n");
		err = -ENOMEM;
		goto clunk_fid;
	}

	file->private_data = vfid;
	vfid->fid = fid;
	vfid->fidopen = 1;
	vfid->fidclunked = 0;
	vfid->iounit = fcall->params.ropen.iounit;
	vfid->rdir_pos = 0;
	vfid->rdir_fcall = NULL;
	vfid->filp = file;
	kfree(fcall);

	return 0;

clunk_fid:
	v9fs_t_clunk(v9ses, fid);

put_fid:
	v9fs_put_idpool(fid, &v9ses->fidpool);
	kfree(fcall);

	return err;
}

/**
 * v9fs_file_lock - lock a file (or directory)
 * @inode: inode to be opened
 * @file: file being opened
 *
 * XXX - this looks like a local only lock, we should extend into 9P
 *       by using open exclusive
 */

static int v9fs_file_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	int res = 0;
	struct inode *inode = filp->f_dentry->d_inode;

	dprintk(DEBUG_VFS, "filp: %p lock: %p\n", filp, fl);

	/* No mandatory locks */
	if ((inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if ((IS_SETLK(cmd) || IS_SETLKW(cmd)) && fl->fl_type != F_UNLCK) {
		filemap_write_and_wait(inode->i_mapping);
		invalidate_inode_pages(&inode->i_data);
	}

	return res;
}

/**
 * v9fs_file_read - read from a file
 * @filep: file pointer to read
 * @data: data buffer to read data into
 * @count: size of buffer
 * @offset: offset at which to read data
 *
 */
static ssize_t
v9fs_file_read(struct file *filp, char __user * data, size_t count,
	       loff_t * offset)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);
	struct v9fs_fid *v9f = filp->private_data;
	struct v9fs_fcall *fcall = NULL;
	int fid = v9f->fid;
	int rsize = 0;
	int result = 0;
	int total = 0;
	int n;

	dprintk(DEBUG_VFS, "\n");

	rsize = v9ses->maxdata - V9FS_IOHDRSZ;
	if (v9f->iounit != 0 && rsize > v9f->iounit)
		rsize = v9f->iounit;

	do {
		if (count < rsize)
			rsize = count;

		result = v9fs_t_read(v9ses, fid, *offset, rsize, &fcall);

		if (result < 0) {
			printk(KERN_ERR "9P2000: v9fs_t_read returned %d\n",
			       result);

			kfree(fcall);
			return total;
		} else
			*offset += result;

		n = copy_to_user(data, fcall->params.rread.data, result);
		if (n) {
			dprintk(DEBUG_ERROR, "Problem copying to user %d\n", n);
			kfree(fcall);
			return -EFAULT;
		}

		count -= result;
		data += result;
		total += result;

		kfree(fcall);

		if (result < rsize)
			break;
	} while (count);

	return total;
}

/**
 * v9fs_file_write - write to a file
 * @filep: file pointer to write
 * @data: data buffer to write data from
 * @count: size of buffer
 * @offset: offset at which to write data
 *
 */

static ssize_t
v9fs_file_write(struct file *filp, const char __user * data,
		size_t count, loff_t * offset)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);
	struct v9fs_fid *v9fid = filp->private_data;
	struct v9fs_fcall *fcall;
	int fid = v9fid->fid;
	int result = -EIO;
	int rsize = 0;
	int total = 0;

	dprintk(DEBUG_VFS, "data %p count %d offset %x\n", data, (int)count,
		(int)*offset);
	rsize = v9ses->maxdata - V9FS_IOHDRSZ;
	if (v9fid->iounit != 0 && rsize > v9fid->iounit)
		rsize = v9fid->iounit;

	do {
		if (count < rsize)
			rsize = count;

		result = v9fs_t_write(v9ses, fid, *offset, rsize, data, &fcall);
		if (result < 0) {
			PRINT_FCALL_ERROR("error while writing", fcall);
			kfree(fcall);
			return result;
		} else
			*offset += result;

		kfree(fcall);
		fcall = NULL;

		if (result != rsize) {
			eprintk(KERN_ERR,
				"short write: v9fs_t_write returned %d\n",
				result);
			break;
		}

		count -= result;
		data += result;
		total += result;
	} while (count);

		invalidate_inode_pages2(inode->i_mapping);
	return total;
}

const struct file_operations v9fs_file_operations = {
	.llseek = generic_file_llseek,
	.read = v9fs_file_read,
	.write = v9fs_file_write,
	.open = v9fs_file_open,
	.release = v9fs_dir_release,
	.lock = v9fs_file_lock,
	.mmap = generic_file_mmap,
};
