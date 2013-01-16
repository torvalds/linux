/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_sync_user.c
 *
 */

#ifdef CONFIG_SYNC

#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/anon_inodes.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_sync.h"

static int mali_stream_close(struct inode * inode, struct file * file)
{
	struct sync_timeline * tl;
	tl = (struct sync_timeline*)file->private_data;
	BUG_ON(!tl);
	sync_timeline_destroy(tl);
	return 0;
}

static struct file_operations stream_fops =
{
	.owner = THIS_MODULE,
	.release = mali_stream_close,
};

_mali_osk_errcode_t mali_stream_create(const char * name, int *out_fd)
{
	struct sync_timeline * tl;
	BUG_ON(!out_fd);

	tl = mali_sync_timeline_alloc(name);
	if (!tl)
	{
		return _MALI_OSK_ERR_FAULT;
	}

	*out_fd = anon_inode_getfd(name, &stream_fops, tl, O_RDONLY | O_CLOEXEC);

	if (*out_fd < 0)
	{
		sync_timeline_destroy(tl);
		return _MALI_OSK_ERR_FAULT;
	}
	else
	{
		return _MALI_OSK_ERR_OK;
	}
}

mali_sync_pt *mali_stream_create_point(int tl_fd)
{
	struct sync_timeline *tl;
	struct sync_pt * pt;
	struct file *tl_file;

	tl_file = fget(tl_fd);
	if (tl_file == NULL)
		return NULL;

	if (tl_file->f_op != &stream_fops)
	{
		pt = NULL;
		goto out;
	}

	tl = tl_file->private_data;

	pt = mali_sync_pt_alloc(tl);
	if (!pt)
	{
		pt = NULL;
		goto out;
	}

out:
	fput(tl_file);

	return pt;
}

int mali_stream_create_fence(mali_sync_pt *pt)
{
	struct sync_fence *fence;
	struct fdtable * fdt;
	struct files_struct * files;
	int fd = -1;

	fence = sync_fence_create("mali_fence", pt);
	if (!fence)
	{
		sync_pt_free(pt);
		fd = -EFAULT;
		goto out;
	}

	/* create a fd representing the fence */
	fd = get_unused_fd();
	if (fd < 0)
	{
		sync_fence_put(fence);
		goto out;
	}

	files = current->files;
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	__set_close_on_exec(fd, fdt);
#else
	FD_SET(fd, fdt->close_on_exec);
#endif
	spin_unlock(&files->file_lock);

	/* bind fence to the new fd */
	sync_fence_install(fence, fd);

out:
	return fd;
}

_mali_osk_errcode_t mali_fence_validate(int fd)
{
	struct sync_fence * fence;
	fence = sync_fence_fdget(fd);
	if (NULL != fence)
	{
		sync_fence_put(fence);
		return _MALI_OSK_ERR_OK;
	}
	else
	{
		return _MALI_OSK_ERR_FAULT;
	}
}

#endif /* CONFIG_SYNC */
