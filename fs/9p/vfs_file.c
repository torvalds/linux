// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contians vfs file ops for 9P2000.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"
#include "cache.h"

static const struct vm_operations_struct v9fs_mmap_file_vm_ops;

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

	p9_debug(P9_DEBUG_VFS, "inode: %p file: %p\n", inode, file);
	v9ses = v9fs_inode2v9ses(inode);
	if (v9fs_proto_dotl(v9ses))
		omode = v9fs_open_to_dotl_flags(file->f_flags);
	else
		omode = v9fs_uflags2omode(file->f_flags,
					v9fs_proto_dotu(v9ses));
	fid = file->private_data;
	if (!fid) {
		fid = v9fs_fid_clone(file_dentry(file));
		if (IS_ERR(fid))
			return PTR_ERR(fid);

		if ((v9ses->cache & CACHE_WRITEBACK) && (omode & P9_OWRITE)) {
			int writeback_omode = (omode & ~P9_OWRITE) | P9_ORDWR;

			p9_debug(P9_DEBUG_CACHE, "write-only file with writeback enabled, try opening O_RDWR\n");
			err = p9_client_open(fid, writeback_omode);
			if (err < 0) {
				p9_debug(P9_DEBUG_CACHE, "could not open O_RDWR, disabling caches\n");
				err = p9_client_open(fid, omode);
				fid->mode |= P9L_DIRECT;
			}
		} else {
			err = p9_client_open(fid, omode);
		}
		if (err < 0) {
			p9_fid_put(fid);
			return err;
		}
		if ((file->f_flags & O_APPEND) &&
			(!v9fs_proto_dotu(v9ses) && !v9fs_proto_dotl(v9ses)))
			generic_file_llseek(file, 0, SEEK_END);

		file->private_data = fid;
	}

#ifdef CONFIG_9P_FSCACHE
	if (v9ses->cache & CACHE_FSCACHE)
		fscache_use_cookie(v9fs_inode_cookie(V9FS_I(inode)),
				   file->f_mode & FMODE_WRITE);
#endif
	v9fs_fid_add_modes(fid, v9ses->flags, v9ses->cache, file->f_flags);
	v9fs_open_fid_add(inode, &fid);
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
	struct inode *inode = file_inode(filp);

	p9_debug(P9_DEBUG_VFS, "filp: %p lock: %p\n", filp, fl);

	if ((IS_SETLK(cmd) || IS_SETLKW(cmd)) && fl->c.flc_type != F_UNLCK) {
		filemap_write_and_wait(inode->i_mapping);
		invalidate_mapping_pages(&inode->i_data, 0, -1);
	}

	return 0;
}

static int v9fs_file_do_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	struct p9_flock flock;
	struct p9_fid *fid;
	uint8_t status = P9_LOCK_ERROR;
	int res = 0;
	struct v9fs_session_info *v9ses;

	fid = filp->private_data;
	BUG_ON(fid == NULL);

	BUG_ON((fl->c.flc_flags & FL_POSIX) != FL_POSIX);

	res = locks_lock_file_wait(filp, fl);
	if (res < 0)
		goto out;

	/* convert posix lock to p9 tlock args */
	memset(&flock, 0, sizeof(flock));
	/* map the lock type */
	switch (fl->c.flc_type) {
	case F_RDLCK:
		flock.type = P9_LOCK_TYPE_RDLCK;
		break;
	case F_WRLCK:
		flock.type = P9_LOCK_TYPE_WRLCK;
		break;
	case F_UNLCK:
		flock.type = P9_LOCK_TYPE_UNLCK;
		break;
	}
	flock.start = fl->fl_start;
	if (fl->fl_end == OFFSET_MAX)
		flock.length = 0;
	else
		flock.length = fl->fl_end - fl->fl_start + 1;
	flock.proc_id = fl->c.flc_pid;
	flock.client_id = fid->clnt->name;
	if (IS_SETLKW(cmd))
		flock.flags = P9_LOCK_FLAGS_BLOCK;

	v9ses = v9fs_inode2v9ses(file_inode(filp));

	/*
	 * if its a blocked request and we get P9_LOCK_BLOCKED as the status
	 * for lock request, keep on trying
	 */
	for (;;) {
		res = p9_client_lock_dotl(fid, &flock, &status);
		if (res < 0)
			goto out_unlock;

		if (status != P9_LOCK_BLOCKED)
			break;
		if (status == P9_LOCK_BLOCKED && !IS_SETLKW(cmd))
			break;
		if (schedule_timeout_interruptible(v9ses->session_lock_timeout)
				!= 0)
			break;
		/*
		 * p9_client_lock_dotl overwrites flock.client_id with the
		 * server message, free and reuse the client name
		 */
		if (flock.client_id != fid->clnt->name) {
			kfree(flock.client_id);
			flock.client_id = fid->clnt->name;
		}
	}

	/* map 9p status to VFS status */
	switch (status) {
	case P9_LOCK_SUCCESS:
		res = 0;
		break;
	case P9_LOCK_BLOCKED:
		res = -EAGAIN;
		break;
	default:
		WARN_ONCE(1, "unknown lock status code: %d\n", status);
		fallthrough;
	case P9_LOCK_ERROR:
	case P9_LOCK_GRACE:
		res = -ENOLCK;
		break;
	}

out_unlock:
	/*
	 * incase server returned error for lock request, revert
	 * it locally
	 */
	if (res < 0 && fl->c.flc_type != F_UNLCK) {
		unsigned char type = fl->c.flc_type;

		fl->c.flc_type = F_UNLCK;
		/* Even if this fails we want to return the remote error */
		locks_lock_file_wait(filp, fl);
		fl->c.flc_type = type;
	}
	if (flock.client_id != fid->clnt->name)
		kfree(flock.client_id);
out:
	return res;
}

static int v9fs_file_getlock(struct file *filp, struct file_lock *fl)
{
	struct p9_getlock glock;
	struct p9_fid *fid;
	int res = 0;

	fid = filp->private_data;
	BUG_ON(fid == NULL);

	posix_test_lock(filp, fl);
	/*
	 * if we have a conflicting lock locally, no need to validate
	 * with server
	 */
	if (fl->c.flc_type != F_UNLCK)
		return res;

	/* convert posix lock to p9 tgetlock args */
	memset(&glock, 0, sizeof(glock));
	glock.type  = P9_LOCK_TYPE_UNLCK;
	glock.start = fl->fl_start;
	if (fl->fl_end == OFFSET_MAX)
		glock.length = 0;
	else
		glock.length = fl->fl_end - fl->fl_start + 1;
	glock.proc_id = fl->c.flc_pid;
	glock.client_id = fid->clnt->name;

	res = p9_client_getlock_dotl(fid, &glock);
	if (res < 0)
		goto out;
	/* map 9p lock type to os lock type */
	switch (glock.type) {
	case P9_LOCK_TYPE_RDLCK:
		fl->c.flc_type = F_RDLCK;
		break;
	case P9_LOCK_TYPE_WRLCK:
		fl->c.flc_type = F_WRLCK;
		break;
	case P9_LOCK_TYPE_UNLCK:
		fl->c.flc_type = F_UNLCK;
		break;
	}
	if (glock.type != P9_LOCK_TYPE_UNLCK) {
		fl->fl_start = glock.start;
		if (glock.length == 0)
			fl->fl_end = OFFSET_MAX;
		else
			fl->fl_end = glock.start + glock.length - 1;
		fl->c.flc_pid = -glock.proc_id;
	}
out:
	if (glock.client_id != fid->clnt->name)
		kfree(glock.client_id);
	return res;
}

/**
 * v9fs_file_lock_dotl - lock a file (or directory)
 * @filp: file to be locked
 * @cmd: lock command
 * @fl: file lock structure
 *
 */

static int v9fs_file_lock_dotl(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = file_inode(filp);
	int ret = -ENOLCK;

	p9_debug(P9_DEBUG_VFS, "filp: %p cmd:%d lock: %p name: %pD\n",
		 filp, cmd, fl, filp);

	if ((IS_SETLK(cmd) || IS_SETLKW(cmd)) && fl->c.flc_type != F_UNLCK) {
		filemap_write_and_wait(inode->i_mapping);
		invalidate_mapping_pages(&inode->i_data, 0, -1);
	}

	if (IS_SETLK(cmd) || IS_SETLKW(cmd))
		ret = v9fs_file_do_lock(filp, cmd, fl);
	else if (IS_GETLK(cmd))
		ret = v9fs_file_getlock(filp, fl);
	else
		ret = -EINVAL;
	return ret;
}

/**
 * v9fs_file_flock_dotl - lock a file
 * @filp: file to be locked
 * @cmd: lock command
 * @fl: file lock structure
 *
 */

static int v9fs_file_flock_dotl(struct file *filp, int cmd,
	struct file_lock *fl)
{
	struct inode *inode = file_inode(filp);
	int ret = -ENOLCK;

	p9_debug(P9_DEBUG_VFS, "filp: %p cmd:%d lock: %p name: %pD\n",
		 filp, cmd, fl, filp);

	if (!(fl->c.flc_flags & FL_FLOCK))
		goto out_err;

	if ((IS_SETLK(cmd) || IS_SETLKW(cmd)) && fl->c.flc_type != F_UNLCK) {
		filemap_write_and_wait(inode->i_mapping);
		invalidate_mapping_pages(&inode->i_data, 0, -1);
	}
	/* Convert flock to posix lock */
	fl->c.flc_flags |= FL_POSIX;
	fl->c.flc_flags ^= FL_FLOCK;

	if (IS_SETLK(cmd) | IS_SETLKW(cmd))
		ret = v9fs_file_do_lock(filp, cmd, fl);
	else
		ret = -EINVAL;
out_err:
	return ret;
}

/**
 * v9fs_file_read_iter - read from a file
 * @iocb: The operation parameters
 * @to: The buffer to read into
 *
 */
static ssize_t
v9fs_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct p9_fid *fid = iocb->ki_filp->private_data;

	p9_debug(P9_DEBUG_VFS, "fid %d count %zu offset %lld\n",
		 fid->fid, iov_iter_count(to), iocb->ki_pos);

	if (fid->mode & P9L_DIRECT)
		return netfs_unbuffered_read_iter(iocb, to);

	p9_debug(P9_DEBUG_VFS, "(cached)\n");
	return netfs_file_read_iter(iocb, to);
}

/*
 * v9fs_file_splice_read - splice-read from a file
 * @in: The 9p file to read from
 * @ppos: Where to find/update the file position
 * @pipe: The pipe to splice into
 * @len: The maximum amount of data to splice
 * @flags: SPLICE_F_* flags
 */
static ssize_t v9fs_file_splice_read(struct file *in, loff_t *ppos,
				     struct pipe_inode_info *pipe,
				     size_t len, unsigned int flags)
{
	struct p9_fid *fid = in->private_data;

	p9_debug(P9_DEBUG_VFS, "fid %d count %zu offset %lld\n",
		 fid->fid, len, *ppos);

	if (fid->mode & P9L_DIRECT)
		return copy_splice_read(in, ppos, pipe, len, flags);
	return filemap_splice_read(in, ppos, pipe, len, flags);
}

/**
 * v9fs_file_write_iter - write to a file
 * @iocb: The operation parameters
 * @from: The data to write
 *
 */
static ssize_t
v9fs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct p9_fid *fid = file->private_data;

	p9_debug(P9_DEBUG_VFS, "fid %d\n", fid->fid);

	if (fid->mode & (P9L_DIRECT | P9L_NOWRITECACHE))
		return netfs_unbuffered_write_iter(iocb, from);

	p9_debug(P9_DEBUG_CACHE, "(cached)\n");
	return netfs_file_write_iter(iocb, from);
}

static int v9fs_file_fsync(struct file *filp, loff_t start, loff_t end,
			   int datasync)
{
	struct p9_fid *fid;
	struct inode *inode = filp->f_mapping->host;
	struct p9_wstat wstat;
	int retval;

	retval = file_write_and_wait_range(filp, start, end);
	if (retval)
		return retval;

	inode_lock(inode);
	p9_debug(P9_DEBUG_VFS, "filp %p datasync %x\n", filp, datasync);

	fid = filp->private_data;
	v9fs_blank_wstat(&wstat);

	retval = p9_client_wstat(fid, &wstat);
	inode_unlock(inode);

	return retval;
}

int v9fs_file_fsync_dotl(struct file *filp, loff_t start, loff_t end,
			 int datasync)
{
	struct p9_fid *fid;
	struct inode *inode = filp->f_mapping->host;
	int retval;

	retval = file_write_and_wait_range(filp, start, end);
	if (retval)
		return retval;

	inode_lock(inode);
	p9_debug(P9_DEBUG_VFS, "filp %p datasync %x\n", filp, datasync);

	fid = filp->private_data;

	retval = p9_client_fsync(fid, datasync);
	inode_unlock(inode);

	return retval;
}

static int
v9fs_file_mmap_prepare(struct vm_area_desc *desc)
{
	int retval;
	struct file *filp = desc->file;
	struct inode *inode = file_inode(filp);
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);

	p9_debug(P9_DEBUG_MMAP, "filp :%p\n", filp);

	if (!(v9ses->cache & CACHE_WRITEBACK)) {
		p9_debug(P9_DEBUG_CACHE, "(read-only mmap mode)");
		return generic_file_readonly_mmap_prepare(desc);
	}

	retval = generic_file_mmap_prepare(desc);
	if (!retval)
		desc->vm_ops = &v9fs_mmap_file_vm_ops;

	return retval;
}

static vm_fault_t
v9fs_vm_page_mkwrite(struct vm_fault *vmf)
{
	return netfs_page_mkwrite(vmf, NULL);
}

static void v9fs_mmap_vm_close(struct vm_area_struct *vma)
{
	struct inode *inode;

	struct writeback_control wbc = {
		.nr_to_write = LONG_MAX,
		.sync_mode = WB_SYNC_ALL,
		.range_start = (loff_t)vma->vm_pgoff * PAGE_SIZE,
		 /* absolute end, byte at end included */
		.range_end = (loff_t)vma->vm_pgoff * PAGE_SIZE +
			(vma->vm_end - vma->vm_start - 1),
	};

	if (!(vma->vm_flags & VM_SHARED))
		return;

	p9_debug(P9_DEBUG_VFS, "9p VMA close, %p, flushing", vma);

	inode = file_inode(vma->vm_file);
	filemap_fdatawrite_wbc(inode->i_mapping, &wbc);
}

static const struct vm_operations_struct v9fs_mmap_file_vm_ops = {
	.close = v9fs_mmap_vm_close,
	.fault = filemap_fault,
	.map_pages = filemap_map_pages,
	.page_mkwrite = v9fs_vm_page_mkwrite,
};

const struct file_operations v9fs_file_operations = {
	.llseek = generic_file_llseek,
	.read_iter = v9fs_file_read_iter,
	.write_iter = v9fs_file_write_iter,
	.open = v9fs_file_open,
	.release = v9fs_dir_release,
	.lock = v9fs_file_lock,
	.mmap_prepare = generic_file_readonly_mmap_prepare,
	.splice_read = v9fs_file_splice_read,
	.splice_write = iter_file_splice_write,
	.fsync = v9fs_file_fsync,
	.setlease = simple_nosetlease,
};

const struct file_operations v9fs_file_operations_dotl = {
	.llseek = generic_file_llseek,
	.read_iter = v9fs_file_read_iter,
	.write_iter = v9fs_file_write_iter,
	.open = v9fs_file_open,
	.release = v9fs_dir_release,
	.lock = v9fs_file_lock_dotl,
	.flock = v9fs_file_flock_dotl,
	.mmap_prepare = v9fs_file_mmap_prepare,
	.splice_read = v9fs_file_splice_read,
	.splice_write = iter_file_splice_write,
	.fsync = v9fs_file_fsync_dotl,
	.setlease = simple_nosetlease,
};
