/*
 * linux/fs/9p/trans_socket.c
 *
 * Socket Transport Layer
 *
 *  Copyright (C) 2004-2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 1997-2002 by Ron Minnich <rminnich@sarnoff.com>
 *  Copyright (C) 1995, 1996 by Olaf Kirch <okir@monad.swb.de>
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
#include <linux/in.h>
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

#define V9FS_PORT 564

struct v9fs_trans_sock {
	struct socket *s;
	struct file *filp;
};

/**
 * v9fs_sock_recv - receive from a socket
 * @v9ses: session information
 * @v: buffer to receive data into
 * @len: size of receive buffer
 *
 */

static int v9fs_sock_recv(struct v9fs_transport *trans, void *v, int len)
{
	int ret;
	struct v9fs_trans_sock *ts;

	if (!trans || trans->status == Disconnected) {
		dprintk(DEBUG_ERROR, "disconnected ...\n");
		return -EREMOTEIO;
	}

	ts = trans->priv;

	if (!(ts->filp->f_flags & O_NONBLOCK))
		dprintk(DEBUG_ERROR, "blocking read ...\n");

	ret = kernel_read(ts->filp, ts->filp->f_pos, v, len);
	if (ret <= 0) {
		if (ret != -ERESTARTSYS && ret != -EAGAIN)
			trans->status = Disconnected;
	}

	return ret;
}

/**
 * v9fs_sock_send - send to a socket
 * @v9ses: session information
 * @v: buffer to send data from
 * @len: size of send buffer
 *
 */

static int v9fs_sock_send(struct v9fs_transport *trans, void *v, int len)
{
	int ret;
	mm_segment_t oldfs;
	struct v9fs_trans_sock *ts;

	if (!trans || trans->status == Disconnected) {
		dprintk(DEBUG_ERROR, "disconnected ...\n");
		return -EREMOTEIO;
	}

	ts = trans->priv;
	if (!ts) {
		dprintk(DEBUG_ERROR, "no transport ...\n");
		return -EREMOTEIO;
	}

	if (!(ts->filp->f_flags & O_NONBLOCK))
		dprintk(DEBUG_ERROR, "blocking write ...\n");

	oldfs = get_fs();
	set_fs(get_ds());
	ret = vfs_write(ts->filp, (void __user *)v, len, &ts->filp->f_pos);
	set_fs(oldfs);

	if (ret < 0) {
		if (ret != -ERESTARTSYS)
			trans->status = Disconnected;
	}

	return ret;
}

static unsigned int v9fs_sock_poll(struct v9fs_transport *trans,
	struct poll_table_struct *pt) {

	int ret;
	struct v9fs_trans_sock *ts;
	mm_segment_t oldfs;

	if (!trans) {
		dprintk(DEBUG_ERROR, "no transport\n");
		return -EIO;
	}

	ts = trans->priv;
	if (trans->status != Connected || !ts) {
		dprintk(DEBUG_ERROR, "transport disconnected: %d\n", trans->status);
		return -EIO;
	}

	oldfs = get_fs();
	set_fs(get_ds());

	if (!ts->filp->f_op || !ts->filp->f_op->poll) {
		dprintk(DEBUG_ERROR, "no poll operation\n");
		ret = -EIO;
		goto end;
	}

	ret = ts->filp->f_op->poll(ts->filp, pt);

end:
	set_fs(oldfs);
	return ret;
}


/**
 * v9fs_tcp_init - initialize TCP socket
 * @v9ses: session information
 * @addr: address of server to mount
 * @data: mount options
 *
 */

static int
v9fs_tcp_init(struct v9fs_session_info *v9ses, const char *addr, char *data)
{
	struct socket *csocket = NULL;
	struct sockaddr_in sin_server;
	int rc = 0;
	struct v9fs_trans_sock *ts = NULL;
	struct v9fs_transport *trans = v9ses->transport;
	int fd;

	trans->status = Disconnected;

	ts = kmalloc(sizeof(struct v9fs_trans_sock), GFP_KERNEL);

	if (!ts)
		return -ENOMEM;

	trans->priv = ts;
	ts->s = NULL;
	ts->filp = NULL;

	if (!addr)
		return -EINVAL;

	dprintk(DEBUG_TRANS, "Connecting to %s\n", addr);

	sin_server.sin_family = AF_INET;
	sin_server.sin_addr.s_addr = in_aton(addr);
	sin_server.sin_port = htons(v9ses->port);
	sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &csocket);
	rc = csocket->ops->connect(csocket,
				   (struct sockaddr *)&sin_server,
				   sizeof(struct sockaddr_in), 0);
	if (rc < 0) {
		eprintk(KERN_ERR,
			"v9fs_trans_tcp: problem connecting socket to %s\n",
			addr);
		return rc;
	}
	csocket->sk->sk_allocation = GFP_NOIO;

	fd = sock_map_fd(csocket);
	if (fd < 0) {
		sock_release(csocket);
		kfree(ts);
		trans->priv = NULL;
		return fd;
	}

	ts->s = csocket;
	ts->filp = fget(fd);
	ts->filp->f_flags |= O_NONBLOCK;
	trans->status = Connected;

	return 0;
}

/**
 * v9fs_unix_init - initialize UNIX domain socket
 * @v9ses: session information
 * @dev_name: path to named pipe
 * @data: mount options
 *
 */

static int
v9fs_unix_init(struct v9fs_session_info *v9ses, const char *dev_name,
	       char *data)
{
	int rc, fd;
	struct socket *csocket;
	struct sockaddr_un sun_server;
	struct v9fs_transport *trans;
	struct v9fs_trans_sock *ts;

	rc = 0;
	csocket = NULL;
	trans = v9ses->transport;

	trans->status = Disconnected;

	if (strlen(dev_name) > UNIX_PATH_MAX) {
		eprintk(KERN_ERR, "v9fs_trans_unix: address too long: %s\n",
			dev_name);
		return -ENOMEM;
	}

	ts = kmalloc(sizeof(struct v9fs_trans_sock), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	trans->priv = ts;
	ts->s = NULL;
	ts->filp = NULL;

	sun_server.sun_family = PF_UNIX;
	strcpy(sun_server.sun_path, dev_name);
	sock_create_kern(PF_UNIX, SOCK_STREAM, 0, &csocket);
	rc = csocket->ops->connect(csocket, (struct sockaddr *)&sun_server,
		sizeof(struct sockaddr_un) - 1, 0);	/* -1 *is* important */
	if (rc < 0) {
		eprintk(KERN_ERR,
			"v9fs_trans_unix: problem connecting socket: %s: %d\n",
			dev_name, rc);
		return rc;
	}
	csocket->sk->sk_allocation = GFP_NOIO;

	fd = sock_map_fd(csocket);
	if (fd < 0) {
		sock_release(csocket);
		kfree(ts);
		trans->priv = NULL;
		return fd;
	}

	ts->s = csocket;
	ts->filp = fget(fd);
	ts->filp->f_flags |= O_NONBLOCK;
	trans->status = Connected;

	return 0;
}

/**
 * v9fs_sock_close - shutdown socket
 * @trans: private socket structure
 *
 */

static void v9fs_sock_close(struct v9fs_transport *trans)
{
	struct v9fs_trans_sock *ts;

	if (!trans)
		return;

	ts = trans->priv;

	if ((ts) && (ts->filp)) {
		fput(ts->filp);
		ts->filp = NULL;
		ts->s = NULL;
		trans->status = Disconnected;
	}

	kfree(ts);

	trans->priv = NULL;
}

struct v9fs_transport v9fs_trans_tcp = {
	.init = v9fs_tcp_init,
	.write = v9fs_sock_send,
	.read = v9fs_sock_recv,
	.close = v9fs_sock_close,
	.poll = v9fs_sock_poll,
};

struct v9fs_transport v9fs_trans_unix = {
	.init = v9fs_unix_init,
	.write = v9fs_sock_send,
	.read = v9fs_sock_recv,
	.close = v9fs_sock_close,
	.poll = v9fs_sock_poll,
};
