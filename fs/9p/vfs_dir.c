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
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * dt_type - return file type
 * @mistat: mistat structure
 *
 */

static inline int dt_type(struct p9_stat *mistat)
{
	unsigned long perm = mistat->mode;
	int rettype = DT_REG;

	if (perm & P9_DMDIR)
		rettype = DT_DIR;
	if (perm & P9_DMSYMLINK)
		rettype = DT_LNK;

	return rettype;
}

/**
 * v9fs_dir_readdir - read a directory
 * @filp: opened file structure
 * @dirent: directory structure ???
 * @filldir: function to populate directory structure ???
 *
 */

static int v9fs_dir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	int over;
	struct p9_fid *fid;
	struct v9fs_session_info *v9ses;
	struct inode *inode;
	struct p9_stat *st;

	P9_DPRINTK(P9_DEBUG_VFS, "name %s\n", filp->f_path.dentry->d_name.name);
	inode = filp->f_path.dentry->d_inode;
	v9ses = v9fs_inode2v9ses(inode);
	fid = filp->private_data;
	while ((st = p9_client_dirread(fid, filp->f_pos)) != NULL) {
		if (IS_ERR(st))
			return PTR_ERR(st);

		over = filldir(dirent, st->name.str, st->name.len, filp->f_pos,
			v9fs_qid2ino(&st->qid), dt_type(st));

		if (over)
			break;

		filp->f_pos += st->size;
		kfree(st);
		st = NULL;
	}

	kfree(st);
	return 0;
}


/**
 * v9fs_dir_release - close a directory
 * @inode: inode of the directory
 * @filp: file pointer to a directory
 *
 */

int v9fs_dir_release(struct inode *inode, struct file *filp)
{
	struct p9_fid *fid;

	fid = filp->private_data;
	P9_DPRINTK(P9_DEBUG_VFS,
			"inode: %p filp: %p fid: %d\n", inode, filp, fid->fid);
	filemap_write_and_wait(inode->i_mapping);
	p9_client_clunk(fid);
	return 0;
}

const struct file_operations v9fs_dir_operations = {
	.read = generic_read_dir,
	.llseek = generic_file_llseek,
	.readdir = v9fs_dir_readdir,
	.open = v9fs_file_open,
	.release = v9fs_dir_release,
};
