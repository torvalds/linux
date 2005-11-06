/*
 * linux/fs/9p/trans_fd.c
 *
 * File Descriptor Transport Layer
 *
 *  Copyright (C) 2005 by Eric Van Hensbergen <ericvh@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/un.h>
#include <asm/uaccess.h>
#include <linux/inet.h>
#include <linux/idr.h>
#include <linux/file.h>

#include "debug.h"
#include "v9fs.h"
#include "transport.h"

struct v9fs_trans_fd {
	struct file *in_file;
	struct file *out_file;
};

/**
 * v9fs_fd_recv - receive from a socket
 * @v9ses: session information
 * @v: buffer to receive data into
 * @len: size of receive buffer
 *
 */

static int v9fs_fd_recv(struct v9fs_transport *trans, void *v, int len)
{
	struct v9fs_trans_fd *ts = trans ? trans->priv : NULL;

	if (!trans || trans->status != Connected || !ts)
		return -EIO;

	return kernel_read(ts->in_file, ts->in_file->f_pos, v, len);
}

/**
 * v9fs_fd_send - send to a socket
 * @v9ses: session information
 * @v: buffer to send data from
 * @len: size of send buffer
 *
 */

static int v9fs_fd_send(struct v9fs_transport *trans, void *v, int len)
{
	struct v9fs_trans_fd *ts = trans ? trans->priv : NULL;
	mm_segment_t oldfs = get_fs();
	int ret = 0;

	if (!trans || trans->status != Connected || !ts)
		return -EIO;

	set_fs(get_ds());
	/* The cast to a user pointer is valid due to the set_fs() */
	ret = vfs_write(ts->out_file, (void __user *)v, len, &ts->out_file->f_pos);
	set_fs(oldfs);

	return ret;
}

/**
 * v9fs_fd_init - initialize file descriptor transport
 * @v9ses: session information
 * @addr: address of server to mount
 * @data: mount options
 *
 */

static int
v9fs_fd_init(struct v9fs_session_info *v9ses, const char *addr, char *data)
{
	struct v9fs_trans_fd *ts = NULL;
	struct v9fs_transport *trans = v9ses->transport;

	if((v9ses->wfdno == ~0) || (v9ses->rfdno == ~0)) {
		printk(KERN_ERR "v9fs: Insufficient options for proto=fd\n");
		return -ENOPROTOOPT;
	}

	sema_init(&trans->writelock, 1);
	sema_init(&trans->readlock, 1);

	ts = kmalloc(sizeof(struct v9fs_trans_fd), GFP_KERNEL);

	if (!ts)
		return -ENOMEM;

	ts->in_file = fget( v9ses->rfdno );
	ts->out_file = fget( v9ses->wfdno );

	if (!ts->in_file || !ts->out_file) {
		if (ts->in_file)
			fput(ts->in_file);

		if (ts->out_file)
			fput(ts->out_file);

		kfree(ts);
		return -EIO;
	}

	trans->priv = ts;
	trans->status = Connected;

	return 0;
}


/**
 * v9fs_fd_close - shutdown file descriptor
 * @trans: private socket structure
 *
 */

static void v9fs_fd_close(struct v9fs_transport *trans)
{
	struct v9fs_trans_fd *ts;

	if (!trans)
		return;

	trans->status = Disconnected;
	ts = trans->priv;

	if (!ts)
		return;

	if (ts->in_file)
		fput(ts->in_file);

	if (ts->out_file)
		fput(ts->out_file);

	kfree(ts);
}

struct v9fs_transport v9fs_trans_fd = {
	.init = v9fs_fd_init,
	.write = v9fs_fd_send,
	.read = v9fs_fd_recv,
	.close = v9fs_fd_close,
};

