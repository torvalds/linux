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
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/idr.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"

static const struct file_operations v9fs_cached_file_operations;

/**
 * v9fs_file_open - open a file (or directory)
 * @inode: inode to be opened
 * @file: file being opened
 *
 */

int v9fs_file_open(struct inode *inode, struct file *file)
{
	int err;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	int omode;

	P9_DPRINTK(P9_DEBUG_VFS, "inode: %p file: %p \n", inode, file);
	v9ses = v9fs_inode2v9ses(inode);
	omode = v9fs_uflags2omode(file->f_flags, v9fs_extended(v9ses));
	fid = file->private_data;
	if (!fid) {
		fid = v9fs_fid_clone(file->f_path.dentry);
		if (IS_ERR(fid))
			return PTR_ERR(fid);

		err = p9_client_open(fid, omode);
		if (err < 0) {
			p9_client_clunk(fid);
			return err;
		}
		if (omode & P9_OTRUNC) {
			inode->i_size = 0;
			inode->i_blocks = 0;
		}
		if ((file->f_flags & O_APPEND) && (!v9fs_extended(v9ses)))
			generic_file_llseek(file, 0, SEEK_END);
	}

	file->private_data = fid;
	if ((fid->qid.version) && (v9ses->cache)) {
		P9_DPRINTK(P9_DEBUG_VFS, "cached");
		/* enable cached file options */
		if(file->f_op == &v9fs_file_operations)
			file->f_op = &v9fs_cached_file_operations;
	}

	return 0;
}

/**
 * v9fs_file_lock - lock a file (or directory)
 * @filp: file to be locked
 * @cmd: lock command
 * @fl: file lock structure
 *
 * Bugs: this looks like a local only lock, we should extend into 9P
 *       by using open exclusive
 */

static int v9fs_file_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	int res = 0;
	struct inode *inode = filp->f_path.dentry->d_inode;

	P9_DPRINTK(P9_DEBUG_VFS, "filp: %p lock: %p\n", filp, fl);

	/* No mandatory locks */
	if (__mandatory_lock(inode))
		return -ENOLCK;

	if ((IS_SETLK(cmd) || IS_SETLKW(cmd)) && fl->fl_type != F_UNLCK) {
		filemap_write_and_wait(inode->i_mapping);
		invalidate_mapping_pages(&inode->i_data, 0, -1);
	}

	return res;
}

/**
 * v9fs_file_read - read from a file
 * @filp: file pointer to read
 * @data: data buffer to read data into
 * @count: size of buffer
 * @offset: offset at which to read data
 *
 */
static ssize_t
v9fs_file_read(struct file *filp, char __user * data, size_t count,
	       loff_t * offset)
{
	int ret;
	struct p9_fid *fid;

	P9_DPRINTK(P9_DEBUG_VFS, "\n");
	fid = filp->private_data;
	ret = p9_client_uread(fid, data, *offset, count);
	if (ret > 0)
		*offset += ret;

	return ret;
}

/**
 * v9fs_file_write - write to a file
 * @filp: file pointer to write
 * @data: data buffer to write data from
 * @count: size of buffer
 * @offset: offset at which to write data
 *
 */

static ssize_t
v9fs_file_write(struct file *filp, const char __user * data,
		size_t count, loff_t * offset)
{
	int ret;
	struct p9_fid *fid;
	struct inode *inode = filp->f_path.dentry->d_inode;

	P9_DPRINTK(P9_DEBUG_VFS, "data %p count %d offset %x\n", data,
		(int)count, (int)*offset);

	fid = filp->private_data;
	ret = p9_client_uwrite(fid, data, *offset, count);
	if (ret > 0) {
		invalidate_inode_pages2_range(inode->i_mapping, *offset,
								*offset+ret);
		*offset += ret;
	}

	if (*offset > inode->i_size) {
		inode->i_size = *offset;
		inode->i_blocks = (inode->i_size + 512 - 1) >> 9;
	}

	return ret;
}

static const struct file_operations v9fs_cached_file_operations = {
	.llseek = generic_file_llseek,
	.read = do_sync_read,
	.aio_read = generic_file_aio_read,
	.write = v9fs_file_write,
	.open = v9fs_file_open,
	.release = v9fs_dir_release,
	.lock = v9fs_file_lock,
	.mmap = generic_file_readonly_mmap,
};

const struct file_operations v9fs_file_operations = {
	.llseek = generic_file_llseek,
	.read = v9fs_file_read,
	.write = v9fs_file_write,
	.open = v9fs_file_open,
	.release = v9fs_dir_release,
	.lock = v9fs_file_lock,
	.mmap = generic_file_readonly_mmap,
};
