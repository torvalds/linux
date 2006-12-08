/*
 *  linux/fs/9p/vfs_addr.c
 *
 * This file contians vfs address (mmap) ops for 9P2000.
 *
 *  Copyright (C) 2005 by Eric Van Hensbergen <ericvh@gmail.com>
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
#include <linux/pagemap.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * v9fs_vfs_readpage - read an entire page in from 9P
 *
 * @file: file being read
 * @page: structure to page
 *
 */

static int v9fs_vfs_readpage(struct file *filp, struct page *page)
{
	char *buffer = NULL;
	int retval = -EIO;
	loff_t offset = page_offset(page);
	int count = PAGE_CACHE_SIZE;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);
	int rsize = v9ses->maxdata - V9FS_IOHDRSZ;
	struct v9fs_fid *v9f = filp->private_data;
	struct v9fs_fcall *fcall = NULL;
	int fid = v9f->fid;
	int total = 0;
	int result = 0;

	buffer = kmap(page);
	do {
		if (count < rsize)
			rsize = count;

		result = v9fs_t_read(v9ses, fid, offset, rsize, &fcall);

		if (result < 0) {
			printk(KERN_ERR "v9fs_t_read returned %d\n",
			       result);

			kfree(fcall);
			goto UnmapAndUnlock;
		} else
			offset += result;

		memcpy(buffer, fcall->params.rread.data, result);

		count -= result;
		buffer += result;
		total += result;

		kfree(fcall);

		if (result < rsize)
			break;
	} while (count);

	memset(buffer, 0, count);
	flush_dcache_page(page);
	SetPageUptodate(page);
	retval = 0;

UnmapAndUnlock:
	kunmap(page);
	unlock_page(page);
	return retval;
}

const struct address_space_operations v9fs_addr_operations = {
      .readpage = v9fs_vfs_readpage,
};
