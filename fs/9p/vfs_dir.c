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
#include <linux/slab.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * struct p9_rdir - readdir accounting
 * @mutex: mutex protecting readdir
 * @head: start offset of current dirread buffer
 * @tail: end offset of current dirread buffer
 * @buf: dirread buffer
 *
 * private structure for keeping track of readdir
 * allocated on demand
 */

struct p9_rdir {
	struct mutex mutex;
	int head;
	int tail;
	uint8_t *buf;
};

/**
 * dt_type - return file type
 * @mistat: mistat structure
 *
 */

static inline int dt_type(struct p9_wstat *mistat)
{
	unsigned long perm = mistat->mode;
	int rettype = DT_REG;

	if (perm & P9_DMDIR)
		rettype = DT_DIR;
	if (perm & P9_DMSYMLINK)
		rettype = DT_LNK;

	return rettype;
}

static void p9stat_init(struct p9_wstat *stbuf)
{
	stbuf->name  = NULL;
	stbuf->uid   = NULL;
	stbuf->gid   = NULL;
	stbuf->muid  = NULL;
	stbuf->extension = NULL;
}

/**
 * v9fs_alloc_rdir_buf - Allocate buffer used for read and readdir
 * @filp: opened file structure
 * @buflen: Length in bytes of buffer to allocate
 *
 */

static int v9fs_alloc_rdir_buf(struct file *filp, int buflen)
{
	struct p9_rdir *rdir;
	struct p9_fid *fid;
	int err = 0;

	fid = filp->private_data;
	if (!fid->rdir) {
		rdir = kmalloc(sizeof(struct p9_rdir) + buflen, GFP_KERNEL);

		if (rdir == NULL) {
			err = -ENOMEM;
			goto exit;
		}
		spin_lock(&filp->f_dentry->d_lock);
		if (!fid->rdir) {
			rdir->buf = (uint8_t *)rdir + sizeof(struct p9_rdir);
			mutex_init(&rdir->mutex);
			rdir->head = rdir->tail = 0;
			fid->rdir = (void *) rdir;
			rdir = NULL;
		}
		spin_unlock(&filp->f_dentry->d_lock);
		kfree(rdir);
	}
exit:
	return err;
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
	struct p9_wstat st;
	int err = 0;
	struct p9_fid *fid;
	int buflen;
	int reclen = 0;
	struct p9_rdir *rdir;

	P9_DPRINTK(P9_DEBUG_VFS, "name %s\n", filp->f_path.dentry->d_name.name);
	fid = filp->private_data;

	buflen = fid->clnt->msize - P9_IOHDRSZ;

	err = v9fs_alloc_rdir_buf(filp, buflen);
	if (err)
		goto exit;
	rdir = (struct p9_rdir *) fid->rdir;

	err = mutex_lock_interruptible(&rdir->mutex);
	if (err)
		return err;
	while (err == 0) {
		if (rdir->tail == rdir->head) {
			err = v9fs_file_readn(filp, rdir->buf, NULL,
							buflen, filp->f_pos);
			if (err <= 0)
				goto unlock_and_exit;

			rdir->head = 0;
			rdir->tail = err;
		}
		while (rdir->head < rdir->tail) {
			p9stat_init(&st);
			err = p9stat_read(rdir->buf + rdir->head,
						rdir->tail - rdir->head, &st,
						fid->clnt->proto_version);
			if (err) {
				P9_DPRINTK(P9_DEBUG_VFS, "returned %d\n", err);
				err = -EIO;
				p9stat_free(&st);
				goto unlock_and_exit;
			}
			reclen = st.size+2;

			over = filldir(dirent, st.name, strlen(st.name),
			    filp->f_pos, v9fs_qid2ino(&st.qid), dt_type(&st));

			p9stat_free(&st);

			if (over) {
				err = 0;
				goto unlock_and_exit;
			}
			rdir->head += reclen;
			filp->f_pos += reclen;
		}
	}

unlock_and_exit:
	mutex_unlock(&rdir->mutex);
exit:
	return err;
}

/**
 * v9fs_dir_readdir_dotl - read a directory
 * @filp: opened file structure
 * @dirent: buffer to fill dirent structures
 * @filldir: function to populate dirent structures
 *
 */
static int v9fs_dir_readdir_dotl(struct file *filp, void *dirent,
						filldir_t filldir)
{
	int over;
	int err = 0;
	struct p9_fid *fid;
	int buflen;
	struct p9_rdir *rdir;
	struct p9_dirent curdirent;
	u64 oldoffset = 0;

	P9_DPRINTK(P9_DEBUG_VFS, "name %s\n", filp->f_path.dentry->d_name.name);
	fid = filp->private_data;

	buflen = fid->clnt->msize - P9_READDIRHDRSZ;

	err = v9fs_alloc_rdir_buf(filp, buflen);
	if (err)
		goto exit;
	rdir = (struct p9_rdir *) fid->rdir;

	err = mutex_lock_interruptible(&rdir->mutex);
	if (err)
		return err;

	while (err == 0) {
		if (rdir->tail == rdir->head) {
			err = p9_client_readdir(fid, rdir->buf, buflen,
								filp->f_pos);
			if (err <= 0)
				goto unlock_and_exit;

			rdir->head = 0;
			rdir->tail = err;
		}

		while (rdir->head < rdir->tail) {

			err = p9dirent_read(rdir->buf + rdir->head,
						rdir->tail - rdir->head,
						&curdirent,
						fid->clnt->proto_version);
			if (err < 0) {
				P9_DPRINTK(P9_DEBUG_VFS, "returned %d\n", err);
				err = -EIO;
				goto unlock_and_exit;
			}

			/* d_off in dirent structure tracks the offset into
			 * the next dirent in the dir. However, filldir()
			 * expects offset into the current dirent. Hence
			 * while calling filldir send the offset from the
			 * previous dirent structure.
			 */
			over = filldir(dirent, curdirent.d_name,
					strlen(curdirent.d_name),
					oldoffset, v9fs_qid2ino(&curdirent.qid),
					curdirent.d_type);
			oldoffset = curdirent.d_off;

			if (over) {
				err = 0;
				goto unlock_and_exit;
			}

			filp->f_pos = curdirent.d_off;
			rdir->head += err;
		}
	}

unlock_and_exit:
	mutex_unlock(&rdir->mutex);
exit:
	return err;
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
			"v9fs_dir_release: inode: %p filp: %p fid: %d\n",
			inode, filp, fid ? fid->fid : -1);
	filemap_write_and_wait(inode->i_mapping);
	if (fid)
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

const struct file_operations v9fs_dir_operations_dotl = {
	.read = generic_read_dir,
	.llseek = generic_file_llseek,
	.readdir = v9fs_dir_readdir_dotl,
	.open = v9fs_file_open,
	.release = v9fs_dir_release,
        .fsync = v9fs_file_fsync_dotl,
};
