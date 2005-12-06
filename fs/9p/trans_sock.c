/*
 * linux/fs/9p/trans_socket.c
 *
 * Socket Transport Layer
 *
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
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/un.h>
#include <asm/uaccess.h>
#include <linux/inet.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "transport.h"

#define V9FS_PORT 564

struct v9fs_trans_sock {
	struct socket *s;
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
	struct msghdr msg;
	struct kvec iov;
	int result;
	mm_segment_t oldfs;
	struct v9fs_trans_sock *ts = trans ? trans->priv : NULL;

	if (trans->status == Disconnected)
		return -EREMOTEIO;

	result = -EINVAL;

	oldfs = get_fs();
	set_fs(get_ds());

	iov.iov_base = v;
	iov.iov_len = len;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	msg.msg_flags = MSG_NOSIGNAL;

	result = kernel_recvmsg(ts->s, &msg, &iov, 1, len, 0);

	dprintk(DEBUG_TRANS, "socket state %d\n", ts->s->state);
	set_fs(oldfs);

	if (result <= 0) {
		if (result != -ERESTARTSYS)
			trans->status = Disconnected;
	}

	return result;
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
	struct kvec iov;
	struct msghdr msg;
	int result = -1;
	mm_segment_t oldfs;
	struct v9fs_trans_sock *ts = trans ? trans->priv : NULL;

	dprintk(DEBUG_TRANS, "Sending packet size %d (%x)\n", len, len);
	dump_data(v, len);

	down(&trans->writelock);

	oldfs = get_fs();
	set_fs(get_ds());
	iov.iov_base = v;
	iov.iov_len = len;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	msg.msg_flags = MSG_NOSIGNAL;
	result = kernel_sendmsg(ts->s, &msg, &iov, 1, len);
	set_fs(oldfs);

	if (result < 0) {
		if (result != -ERESTARTSYS)
			trans->status = Disconnected;
	}

	up(&trans->writelock);
	return result;
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

	sema_init(&trans->writelock, 1);
	sema_init(&trans->readlock, 1);

	ts = kmalloc(sizeof(struct v9fs_trans_sock), GFP_KERNEL);

	if (!ts)
		return -ENOMEM;

	trans->priv = ts;
	ts->s = NULL;

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
	ts->s = csocket;
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
	int rc;
	struct socket *csocket;
	struct sockaddr_un sun_server;
	struct v9fs_transport *trans;
	struct v9fs_trans_sock *ts;

	rc = 0;
	csocket = NULL;
	trans = v9ses->transport;

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

	sema_init(&trans->writelock, 1);
	sema_init(&trans->readlock, 1);

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
	ts->s = csocket;
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

	if ((ts) && (ts->s)) {
		dprintk(DEBUG_TRANS, "closing the socket %p\n", ts->s);
		sock_release(ts->s);
		ts->s = NULL;
		trans->status = Disconnected;
		dprintk(DEBUG_TRANS, "socket closed\n");
	}

	kfree(ts);

	trans->priv = NULL;
}

struct v9fs_transport v9fs_trans_tcp = {
	.init = v9fs_tcp_init,
	.write = v9fs_sock_send,
	.read = v9fs_sock_recv,
	.close = v9fs_sock_close,
};

struct v9fs_transport v9fs_trans_unix = {
	.init = v9fs_unix_init,
	.write = v9fs_sock_send,
	.read = v9fs_sock_recv,
	.close = v9fs_sock_close,
};
