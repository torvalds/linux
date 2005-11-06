/*
 * linux/fs/9p/mux.c
 *
 * Protocol Multiplexer
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2004 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "transport.h"
#include "conv.h"
#include "mux.h"

/**
 * dprintcond - print condition of session info
 * @v9ses: session info structure
 * @req: RPC request structure
 *
 */

static inline int
dprintcond(struct v9fs_session_info *v9ses, struct v9fs_rpcreq *req)
{
	dprintk(DEBUG_MUX, "condition: %d, %p\n", v9ses->transport->status,
		req->rcall);
	return 0;
}

/**
 * xread - force read of a certain number of bytes
 * @v9ses: session info structure
 * @ptr: pointer to buffer
 * @sz: number of bytes to read
 *
 * Chuck Cranor CS-533 project1
 */

static int xread(struct v9fs_session_info *v9ses, void *ptr, unsigned long sz)
{
	int rd = 0;
	int ret = 0;
	while (rd < sz) {
		ret = v9ses->transport->read(v9ses->transport, ptr, sz - rd);
		if (ret <= 0) {
			dprintk(DEBUG_ERROR, "xread errno %d\n", ret);
			return ret;
		}
		rd += ret;
		ptr += ret;
	}
	return (rd);
}

/**
 * read_message - read a full 9P2000 fcall packet
 * @v9ses: session info structure
 * @rcall: fcall structure to read into
 * @rcalllen: size of fcall buffer
 *
 */

static int
read_message(struct v9fs_session_info *v9ses,
	     struct v9fs_fcall *rcall, int rcalllen)
{
	unsigned char buf[4];
	void *data;
	int size = 0;
	int res = 0;

	res = xread(v9ses, buf, sizeof(buf));
	if (res < 0) {
		dprintk(DEBUG_ERROR,
			"Reading of count field failed returned: %d\n", res);
		return res;
	}

	if (res < 4) {
		dprintk(DEBUG_ERROR,
			"Reading of count field failed returned: %d\n", res);
		return -EIO;
	}

	size = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
	dprintk(DEBUG_MUX, "got a packet count: %d\n", size);

	/* adjust for the four bytes of size */
	size -= 4;

	if (size > v9ses->maxdata) {
		dprintk(DEBUG_ERROR, "packet too big: %d\n", size);
		return -E2BIG;
	}

	data = kmalloc(size, GFP_KERNEL);
	if (!data) {
		eprintk(KERN_WARNING, "out of memory\n");
		return -ENOMEM;
	}

	res = xread(v9ses, data, size);
	if (res < size) {
		dprintk(DEBUG_ERROR, "Reading of fcall failed returned: %d\n",
			res);
		kfree(data);
		return res;
	}

	/* we now have an in-memory string that is the reply.
	 * deserialize it. There is very little to go wrong at this point
	 * save for v9fs_alloc errors.
	 */
	res = v9fs_deserialize_fcall(v9ses, size, data, v9ses->maxdata,
				     rcall, rcalllen);

	kfree(data);

	if (res < 0)
		return res;

	return 0;
}

/**
 * v9fs_recv - receive an RPC response for a particular tag
 * @v9ses: session info structure
 * @req: RPC request structure
 *
 */

static int v9fs_recv(struct v9fs_session_info *v9ses, struct v9fs_rpcreq *req)
{
	int ret = 0;

	dprintk(DEBUG_MUX, "waiting for response: %d\n", req->tcall->tag);
	ret = wait_event_interruptible(v9ses->read_wait,
		       ((v9ses->transport->status != Connected) ||
			(req->rcall != 0) || (req->err < 0) ||
			dprintcond(v9ses, req)));

	dprintk(DEBUG_MUX, "got it: rcall %p\n", req->rcall);

	spin_lock(&v9ses->muxlock);
	list_del(&req->next);
	spin_unlock(&v9ses->muxlock);

	if (req->err < 0)
		return req->err;

	if (v9ses->transport->status == Disconnected)
		return -ECONNRESET;

	return ret;
}

/**
 * v9fs_send - send a 9P request
 * @v9ses: session info structure
 * @req: RPC request to send
 *
 */

static int v9fs_send(struct v9fs_session_info *v9ses, struct v9fs_rpcreq *req)
{
	int ret = -1;
	void *data = NULL;
	struct v9fs_fcall *tcall = req->tcall;

	data = kmalloc(v9ses->maxdata + V9FS_IOHDRSZ, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	tcall->size = 0;	/* enforce size recalculation */
	ret =
	    v9fs_serialize_fcall(v9ses, tcall, data,
				 v9ses->maxdata + V9FS_IOHDRSZ);
	if (ret < 0)
		goto free_data;

	spin_lock(&v9ses->muxlock);
	list_add(&req->next, &v9ses->mux_fcalls);
	spin_unlock(&v9ses->muxlock);

	dprintk(DEBUG_MUX, "sending message: tag %d size %d\n", tcall->tag,
		tcall->size);
	ret = v9ses->transport->write(v9ses->transport, data, tcall->size);

	if (ret != tcall->size) {
		spin_lock(&v9ses->muxlock);
		list_del(&req->next);
		kfree(req->rcall);

		spin_unlock(&v9ses->muxlock);
		if (ret >= 0)
			ret = -EREMOTEIO;
	} else
		ret = 0;

      free_data:
	kfree(data);
	return ret;
}

/**
 * v9fs_mux_rpc - send a request, receive a response
 * @v9ses: session info structure
 * @tcall: fcall to send
 * @rcall: buffer to place response into
 *
 */

long
v9fs_mux_rpc(struct v9fs_session_info *v9ses, struct v9fs_fcall *tcall,
	     struct v9fs_fcall **rcall)
{
	int tid = -1;
	struct v9fs_fcall *fcall = NULL;
	struct v9fs_rpcreq req;
	int ret = -1;

	if (!v9ses)
		return -EINVAL;

	if (!v9ses->transport || v9ses->transport->status != Connected)
		return -EIO;

	if (rcall)
		*rcall = NULL;

	if (tcall->id != TVERSION) {
		tid = v9fs_get_idpool(&v9ses->tidpool);
		if (tid < 0)
			return -ENOMEM;
	}

	tcall->tag = tid;

	req.tcall = tcall;
	req.err = 0;
	req.rcall = NULL;

	ret = v9fs_send(v9ses, &req);

	if (ret < 0) {
		if (tcall->id != TVERSION)
			v9fs_put_idpool(tid, &v9ses->tidpool);
		dprintk(DEBUG_MUX, "error %d\n", ret);
		return ret;
	}

	ret = v9fs_recv(v9ses, &req);

	fcall = req.rcall;

	dprintk(DEBUG_MUX, "received: tag=%x, ret=%d\n", tcall->tag, ret);
	if (ret == -ERESTARTSYS) {
		if (v9ses->transport->status != Disconnected
		    && tcall->id != TFLUSH) {
			unsigned long flags;

			dprintk(DEBUG_MUX, "flushing the tag: %d\n",
				tcall->tag);
			clear_thread_flag(TIF_SIGPENDING);
			v9fs_t_flush(v9ses, tcall->tag);
			spin_lock_irqsave(&current->sighand->siglock, flags);
			recalc_sigpending();
			spin_unlock_irqrestore(&current->sighand->siglock,
					       flags);
			dprintk(DEBUG_MUX, "flushing done\n");
		}

		goto release_req;
	} else if (ret < 0)
		goto release_req;

	if (!fcall)
		ret = -EIO;
	else {
		if (fcall->id == RERROR) {
			ret = v9fs_errstr2errno(fcall->params.rerror.error);
			if (ret == 0) {	/* string match failed */
				if (fcall->params.rerror.errno)
					ret = -(fcall->params.rerror.errno);
				else
					ret = -ESERVERFAULT;
			}
		} else if (fcall->id != tcall->id + 1) {
			dprintk(DEBUG_ERROR,
				"fcall mismatch: expected %d, got %d\n",
				tcall->id + 1, fcall->id);
			ret = -EIO;
		}
	}

      release_req:
	if (tcall->id != TVERSION)
		v9fs_put_idpool(tid, &v9ses->tidpool);
	if (rcall)
		*rcall = fcall;
	else
		kfree(fcall);

	return ret;
}

/**
 * v9fs_mux_cancel_requests - cancels all pending requests
 *
 * @v9ses: session info structure
 * @err: error code to return to the requests
 */
void v9fs_mux_cancel_requests(struct v9fs_session_info *v9ses, int err)
{
	struct v9fs_rpcreq *rptr;
	struct v9fs_rpcreq *rreq;

	dprintk(DEBUG_MUX, " %d\n", err);
	spin_lock(&v9ses->muxlock);
	list_for_each_entry_safe(rreq, rptr, &v9ses->mux_fcalls, next) {
		rreq->err = err;
	}
	spin_unlock(&v9ses->muxlock);
	wake_up_all(&v9ses->read_wait);
}

/**
 * v9fs_recvproc - kproc to handle demultiplexing responses
 * @data: session info structure
 *
 */

static int v9fs_recvproc(void *data)
{
	struct v9fs_session_info *v9ses = (struct v9fs_session_info *)data;
	struct v9fs_fcall *rcall = NULL;
	struct v9fs_rpcreq *rptr;
	struct v9fs_rpcreq *req;
	struct v9fs_rpcreq *rreq;
	int err = 0;

	allow_signal(SIGKILL);
	set_current_state(TASK_INTERRUPTIBLE);
	complete(&v9ses->proccmpl);
	while (!kthread_should_stop() && err >= 0) {
		req = rptr = rreq = NULL;

		rcall = kmalloc(v9ses->maxdata + V9FS_IOHDRSZ, GFP_KERNEL);
		if (!rcall) {
			eprintk(KERN_ERR, "no memory for buffers\n");
			break;
		}

		err = read_message(v9ses, rcall, v9ses->maxdata + V9FS_IOHDRSZ);
		spin_lock(&v9ses->muxlock);
		if (err < 0) {
			list_for_each_entry_safe(rreq, rptr, &v9ses->mux_fcalls, next) {
				rreq->err = err;
			}
			if(err != -ERESTARTSYS)
				eprintk(KERN_ERR,
					"Transport error while reading message %d\n", err);
		} else {
			list_for_each_entry_safe(rreq, rptr, &v9ses->mux_fcalls, next) {
				if (rreq->tcall->tag == rcall->tag) {
					req = rreq;
					req->rcall = rcall;
					break;
				}
			}
		}

		if (req && (req->tcall->id == TFLUSH)) {
			struct v9fs_rpcreq *treq = NULL;
			list_for_each_entry_safe(treq, rptr, &v9ses->mux_fcalls, next) {
				if (treq->tcall->tag ==
				    req->tcall->params.tflush.oldtag) {
					list_del(&rptr->next);
					kfree(treq->rcall);
					break;
				}
			}
		}

		spin_unlock(&v9ses->muxlock);

		if (!req) {
			if (err >= 0)
				dprintk(DEBUG_ERROR,
					"unexpected response: id %d tag %d\n",
					rcall->id, rcall->tag);

			kfree(rcall);
		}

		wake_up_all(&v9ses->read_wait);
		set_current_state(TASK_INTERRUPTIBLE);
	}

	v9ses->transport->close(v9ses->transport);

	/* Inform all pending processes about the failure */
	wake_up_all(&v9ses->read_wait);

	if (signal_pending(current))
		complete(&v9ses->proccmpl);

	dprintk(DEBUG_MUX, "recvproc: end\n");
	v9ses->recvproc = NULL;

	return err >= 0;
}

/**
 * v9fs_mux_init - initialize multiplexer (spawn kproc)
 * @v9ses: session info structure
 * @dev_name: mount device information (to create unique kproc)
 *
 */

int v9fs_mux_init(struct v9fs_session_info *v9ses, const char *dev_name)
{
	char procname[60];

	strncpy(procname, dev_name, sizeof(procname));
	procname[sizeof(procname) - 1] = 0;

	init_waitqueue_head(&v9ses->read_wait);
	init_completion(&v9ses->fcread);
	init_completion(&v9ses->proccmpl);
	spin_lock_init(&v9ses->muxlock);
	INIT_LIST_HEAD(&v9ses->mux_fcalls);
	v9ses->recvproc = NULL;
	v9ses->curfcall = NULL;

	v9ses->recvproc = kthread_create(v9fs_recvproc, v9ses,
					 "v9fs_recvproc %s", procname);

	if (IS_ERR(v9ses->recvproc)) {
		eprintk(KERN_ERR, "cannot create receiving thread\n");
		v9fs_session_close(v9ses);
		return -ECONNABORTED;
	}

	wake_up_process(v9ses->recvproc);
	wait_for_completion(&v9ses->proccmpl);

	return 0;
}
