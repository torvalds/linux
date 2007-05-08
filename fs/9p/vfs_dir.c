/*
 * linux/fs/9p/vfs_dir.c
 *
 * This file contains vfs directory ops for the 9P2000 protocol.
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
#include <linux/sched.h>
#include <linux/inet.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "conv.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * dt_type - return file type
 * @mistat: mistat structure
 *
 */

static inline int dt_type(struct v9fs_stat *mistat)
{
	unsigned long perm = mistat->mode;
	int rettype = DT_REG;

	if (perm & V9FS_DMDIR)
		rettype = DT_DIR;
	if (perm & V9FS_DMSYMLINK)
		rettype = DT_LNK;

	return rettype;
}

/**
 * v9fs_dir_readdir - read a directory
 * @filep: opened file structure
 * @dirent: directory structure ???
 * @filldir: function to populate directory structure ???
 *
 */

static int v9fs_dir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct v9fs_fcall *fcall = NULL;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);
	struct v9fs_fid *file = filp->private_data;
	unsigned int i, n, s;
	int fid = -1;
	int ret = 0;
	struct v9fs_stat stat;
	int over = 0;

	dprintk(DEBUG_VFS, "name %s\n", filp->f_path.dentry->d_name.name);

	fid = file->fid;

	if (file->rdir_fcall && (filp->f_pos != file->rdir_pos)) {
		kfree(file->rdir_fcall);
		file->rdir_fcall = NULL;
	}

	if (file->rdir_fcall) {
		n = file->rdir_fcall->params.rread.count;
		i = file->rdir_fpos;
		while (i < n) {
			s = v9fs_deserialize_stat(
				file->rdir_fcall->params.rread.data + i,
				n - i, &stat, v9ses->extended);

			if (s == 0) {
				dprintk(DEBUG_ERROR,
					"error while deserializing stat\n");
				ret = -EIO;
				goto FreeStructs;
			}

			over = filldir(dirent, stat.name.str, stat.name.len,
				    filp->f_pos, v9fs_qid2ino(&stat.qid),
				    dt_type(&stat));

			if (over) {
				file->rdir_fpos = i;
				file->rdir_pos = filp->f_pos;
				break;
			}

			i += s;
			filp->f_pos += s;
		}

		if (!over) {
			kfree(file->rdir_fcall);
			file->rdir_fcall = NULL;
		}
	}

	while (!over) {
		ret = v9fs_t_read(v9ses, fid, filp->f_pos,
			v9ses->maxdata-V9FS_IOHDRSZ, &fcall);
		if (ret < 0) {
			dprintk(DEBUG_ERROR, "error while reading: %d: %p\n",
				ret, fcall);
			goto FreeStructs;
		} else if (ret == 0)
			break;

		n = ret;
		i = 0;
		while (i < n) {
			s = v9fs_deserialize_stat(fcall->params.rread.data + i,
				n - i, &stat, v9ses->extended);

			if (s == 0) {
				dprintk(DEBUG_ERROR,
					"error while deserializing stat\n");
				return -EIO;
			}

			over = filldir(dirent, stat.name.str, stat.name.len,
				    filp->f_pos, v9fs_qid2ino(&stat.qid),
				    dt_type(&stat));

			if (over) {
				file->rdir_fcall = fcall;
				file->rdir_fpos = i;
				file->rdir_pos = filp->f_pos;
				fcall = NULL;
				break;
			}

			i += s;
			filp->f_pos += s;
		}

		kfree(fcall);
	}

      FreeStructs:
	kfree(fcall);
	return ret;
}

/**
 * v9fs_dir_release - close a directory
 * @inode: inode of the directory
 * @filp: file pointer to a directory
 *
 */

int v9fs_dir_release(struct inode *inode, struct file *filp)
{
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);
	struct v9fs_fid *fid = filp->private_data;
	int fidnum = -1;

	dprintk(DEBUG_VFS, "inode: %p filp: %p fid: %d\n", inode, filp,
		fid->fid);
	fidnum = fid->fid;

	filemap_write_and_wait(inode->i_mapping);

	if (fidnum >= 0) {
		dprintk(DEBUG_VFS, "fidopen: %d v9f->fid: %d\n", fid->fidopen,
			fid->fid);

		if (v9fs_t_clunk(v9ses, fidnum))
			dprintk(DEBUG_ERROR, "clunk failed\n");

		kfree(fid->rdir_fcall);
		kfree(fid);

		filp->private_data = NULL;
	}

	return 0;
}

const struct file_operations v9fs_dir_operations = {
	.read = generic_read_dir,
	.readdir = v9fs_dir_readdir,
	.open = v9fs_file_open,
	.release = v9fs_dir_release,
};
