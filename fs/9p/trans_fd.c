/*
 * linux/fs/9p/trans_fd.c
 *
 * Fd transport layer.  Includes deprecated socket layer.
 *
 *  Copyright (C) 2006 by Russ Cox <rsc@swtch.com>
 *  Copyright (C) 2004-2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004-2005 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 1997-2002 by Ron Minnich <rminnich@sarnoff.com>
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

struct v9fs_trans_fd {
	struct file *rd;
	struct file *wr;
};

/**
 * v9fs_fd_read- read from a fd
 * @v9ses: session information
 * @v: buffer to receive data into
 * @len: size of receive buffer
 *
 */
static int v9fs_fd_read(struct v9fs_transport *trans, void *v, int len)
{
	int ret;
	struct v9fs_trans_fd *ts;

	if (!trans || trans->status == Disconnected || !(ts = trans->priv))
		return -EREMOTEIO;

	if (!(ts->rd->f_flags & O_NONBLOCK))
		dprintk(DEBUG_ERROR, "blocking read ...\n");

	ret = kernel_read(ts->rd, ts->rd->f_pos, v, len);
	if (ret <= 0 && ret != -ERESTARTSYS && ret != -EAGAIN)
		trans->status = Disconnected;
	return ret;
}

/**
 * v9fs_fd_write - write to a socket
 * @v9ses: session information
 * @v: buffer to send data from
 * @len: size of send buffer
 *
 */
static int v9fs_fd_write(struct v9fs_transport *trans, void *v, int len)
{
	int ret;
	mm_segment_t oldfs;
	struct v9fs_trans_fd *ts;

	if (!trans || trans->status == Disconnected || !(ts = trans->priv))
		return -EREMOTEIO;

	if (!(ts->wr->f_flags & O_NONBLOCK))
		dprintk(DEBUG_ERROR, "blocking write ...\n");

	oldfs = get_fs();
	set_fs(get_ds());
	/* The cast to a user pointer is valid due to the set_fs() */
	ret = vfs_write(ts->wr, (void __user *)v, len, &ts->wr->f_pos);
	set_fs(oldfs);

	if (ret <= 0 && ret != -ERESTARTSYS && ret != -EAGAIN)
		trans->status = Disconnected;
	return ret;
}

static unsigned int
v9fs_fd_poll(struct v9fs_transport *trans, struct poll_table_struct *pt)
{
	int ret, n;
	struct v9fs_trans_fd *ts;
	mm_segment_t oldfs;

	if (!trans || trans->status != Connected || !(ts = trans->priv))
		return -EREMOTEIO;

	if (!ts->rd->f_op || !ts->rd->f_op->poll)
		return -EIO;

	if (!ts->wr->f_op || !ts->wr->f_op->poll)
		return -EIO;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = ts->rd->f_op->poll(ts->rd, pt);
	if (ret < 0)
		goto end;

	if (ts->rd != ts->wr) {
		n = ts->wr->f_op->poll(ts->wr, pt);
		if (n < 0) {
			ret = n;
			goto end;
		}
		ret = (ret & ~POLLOUT) | (n & ~POLLIN);
	}

      end:
	set_fs(oldfs);
	return ret;
}

static int v9fs_fd_open(struct v9fs_session_info *v9ses, int rfd, int wfd)
{
	struct v9fs_transport *trans = v9ses->transport;
	struct v9fs_trans_fd *ts = kmalloc(sizeof(struct v9fs_trans_fd),
					   GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->rd = fget(rfd);
	ts->wr = fget(wfd);
	if (!ts->rd || !ts->wr) {
		if (ts->rd)
			fput(ts->rd);
		if (ts->wr)
			fput(ts->wr);
		kfree(ts);
		return -EIO;
	}

	trans->priv = ts;
	trans->status = Connected;

	return 0;
}

static int v9fs_fd_init(struct v9fs_session_info *v9ses, const char *addr,
			char *data)
{
	if (v9ses->rfdno == ~0 || v9ses->wfdno == ~0) {
		printk(KERN_ERR "v9fs: Insufficient options for proto=fd\n");
		return -ENOPROTOOPT;
	}

	return v9fs_fd_open(v9ses, v9ses->rfdno, v9ses->wfdno);
}

static int v9fs_socket_open(struct v9fs_session_info *v9ses,
			    struct socket *csocket)
{
	int fd, ret;

	csocket->sk->sk_allocation = GFP_NOIO;
	if ((fd = sock_map_fd(csocket)) < 0) {
		eprintk(KERN_ERR, "v9fs_socket_open: failed to map fd\n");
		ret = fd;
	      release_csocket:
		sock_release(csocket);
		return ret;
	}

	if ((ret = v9fs_fd_open(v9ses, fd, fd)) < 0) {
		sockfd_put(csocket);
		eprintk(KERN_ERR, "v9fs_socket_open: failed to open fd\n");
		goto release_csocket;
	}

	((struct v9fs_trans_fd *)v9ses->transport->priv)->rd->f_flags |=
	    O_NONBLOCK;
	return 0;
}

static int v9fs_tcp_init(struct v9fs_session_info *v9ses, const char *addr,
			 char *data)
{
	int ret;
	struct socket *csocket = NULL;
	struct sockaddr_in sin_server;

	sin_server.sin_family = AF_INET;
	sin_server.sin_addr.s_addr = in_aton(addr);
	sin_server.sin_port = htons(v9ses->port);
	sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &csocket);

	if (!csocket) {
		eprintk(KERN_ERR, "v9fs_trans_tcp: problem creating socket\n");
		return -1;
	}

	ret = csocket->ops->connect(csocket,
				    (struct sockaddr *)&sin_server,
				    sizeof(struct sockaddr_in), 0);
	if (ret < 0) {
		eprintk(KERN_ERR,
			"v9fs_trans_tcp: problem connecting socket to %s\n",
			addr);
		return ret;
	}

	return v9fs_socket_open(v9ses, csocket);
}

static int
v9fs_unix_init(struct v9fs_session_info *v9ses, const char *addr, char *data)
{
	int ret;
	struct socket *csocket;
	struct sockaddr_un sun_server;

	if (strlen(addr) > UNIX_PATH_MAX) {
		eprintk(KERN_ERR, "v9fs_trans_unix: address too long: %s\n",
			addr);
		return -ENAMETOOLONG;
	}

	sun_server.sun_family = PF_UNIX;
	strcpy(sun_server.sun_path, addr);
	sock_create_kern(PF_UNIX, SOCK_STREAM, 0, &csocket);
	ret = csocket->ops->connect(csocket, (struct sockaddr *)&sun_server,
			sizeof(struct sockaddr_un) - 1, 0);
	if (ret < 0) {
		eprintk(KERN_ERR,
			"v9fs_trans_unix: problem connecting socket: %s: %d\n",
			addr, ret);
		return ret;
	}

	return v9fs_socket_open(v9ses, csocket);
}

/**
 * v9fs_sock_close - shutdown socket
 * @trans: private socket structure
 *
 */
static void v9fs_fd_close(struct v9fs_transport *trans)
{
	struct v9fs_trans_fd *ts;

	if (!trans)
		return;

	ts = xchg(&trans->priv, NULL);

	if (!ts)
		return;

	trans->status = Disconnected;
	if (ts->rd)
		fput(ts->rd);
	if (ts->wr)
		fput(ts->wr);
	kfree(ts);
}

struct v9fs_transport v9fs_trans_fd = {
	.init = v9fs_fd_init,
	.write = v9fs_fd_write,
	.read = v9fs_fd_read,
	.close = v9fs_fd_close,
	.poll = v9fs_fd_poll,
};

struct v9fs_transport v9fs_trans_tcp = {
	.init = v9fs_tcp_init,
	.write = v9fs_fd_write,
	.read = v9fs_fd_read,
	.close = v9fs_fd_close,
	.poll = v9fs_fd_poll,
};

struct v9fs_transport v9fs_trans_unix = {
	.init = v9fs_unix_init,
	.write = v9fs_fd_write,
	.read = v9fs_fd_read,
	.close = v9fs_fd_close,
	.poll = v9fs_fd_poll,
};
