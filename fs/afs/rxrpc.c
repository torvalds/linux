/* Maintain an RxRPC server socket to do AFS communications through
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/sched/signal.h>

#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "internal.h"
#include "afs_cm.h"

struct socket *afs_socket; /* my RxRPC socket */
static struct workqueue_struct *afs_async_calls;
static struct afs_call *afs_spare_incoming_call;
atomic_t afs_outstanding_calls;

static void afs_wake_up_call_waiter(struct sock *, struct rxrpc_call *, unsigned long);
static int afs_wait_for_call_to_complete(struct afs_call *);
static void afs_wake_up_async_call(struct sock *, struct rxrpc_call *, unsigned long);
static void afs_process_async_call(struct work_struct *);
static void afs_rx_new_call(struct sock *, struct rxrpc_call *, unsigned long);
static void afs_rx_discard_new_call(struct rxrpc_call *, unsigned long);
static int afs_deliver_cm_op_id(struct afs_call *);

/* asynchronous incoming call initial processing */
static const struct afs_call_type afs_RXCMxxxx = {
	.name		= "CB.xxxx",
	.deliver	= afs_deliver_cm_op_id,
	.abort_to_error	= afs_abort_to_error,
};

static void afs_charge_preallocation(struct work_struct *);

static DECLARE_WORK(afs_charge_preallocation_work, afs_charge_preallocation);

static int afs_wait_atomic_t(atomic_t *p)
{
	schedule();
	return 0;
}

/*
 * open an RxRPC socket and bind it to be a server for callback notifications
 * - the socket is left in blocking mode and non-blocking ops use MSG_DONTWAIT
 */
int afs_open_socket(void)
{
	struct sockaddr_rxrpc srx;
	struct socket *socket;
	int ret;

	_enter("");

	ret = -ENOMEM;
	afs_async_calls = alloc_workqueue("kafsd", WQ_MEM_RECLAIM, 0);
	if (!afs_async_calls)
		goto error_0;

	ret = sock_create_kern(&init_net, AF_RXRPC, SOCK_DGRAM, PF_INET, &socket);
	if (ret < 0)
		goto error_1;

	socket->sk->sk_allocation = GFP_NOFS;

	/* bind the callback manager's address to make this a server socket */
	srx.srx_family			= AF_RXRPC;
	srx.srx_service			= CM_SERVICE;
	srx.transport_type		= SOCK_DGRAM;
	srx.transport_len		= sizeof(srx.transport.sin);
	srx.transport.sin.sin_family	= AF_INET;
	srx.transport.sin.sin_port	= htons(AFS_CM_PORT);
	memset(&srx.transport.sin.sin_addr, 0,
	       sizeof(srx.transport.sin.sin_addr));

	ret = kernel_bind(socket, (struct sockaddr *) &srx, sizeof(srx));
	if (ret < 0)
		goto error_2;

	rxrpc_kernel_new_call_notification(socket, afs_rx_new_call,
					   afs_rx_discard_new_call);

	ret = kernel_listen(socket, INT_MAX);
	if (ret < 0)
		goto error_2;

	afs_socket = socket;
	afs_charge_preallocation(NULL);
	_leave(" = 0");
	return 0;

error_2:
	sock_release(socket);
error_1:
	destroy_workqueue(afs_async_calls);
error_0:
	_leave(" = %d", ret);
	return ret;
}

/*
 * close the RxRPC socket AFS was using
 */
void afs_close_socket(void)
{
	_enter("");

	kernel_listen(afs_socket, 0);
	flush_workqueue(afs_async_calls);

	if (afs_spare_incoming_call) {
		afs_put_call(afs_spare_incoming_call);
		afs_spare_incoming_call = NULL;
	}

	_debug("outstanding %u", atomic_read(&afs_outstanding_calls));
	wait_on_atomic_t(&afs_outstanding_calls, afs_wait_atomic_t,
			 TASK_UNINTERRUPTIBLE);
	_debug("no outstanding calls");

	kernel_sock_shutdown(afs_socket, SHUT_RDWR);
	flush_workqueue(afs_async_calls);
	sock_release(afs_socket);

	_debug("dework");
	destroy_workqueue(afs_async_calls);
	_leave("");
}

/*
 * Allocate a call.
 */
static struct afs_call *afs_alloc_call(const struct afs_call_type *type,
				       gfp_t gfp)
{
	struct afs_call *call;
	int o;

	call = kzalloc(sizeof(*call), gfp);
	if (!call)
		return NULL;

	call->type = type;
	atomic_set(&call->usage, 1);
	INIT_WORK(&call->async_work, afs_process_async_call);
	init_waitqueue_head(&call->waitq);

	o = atomic_inc_return(&afs_outstanding_calls);
	trace_afs_call(call, afs_call_trace_alloc, 1, o,
		       __builtin_return_address(0));
	return call;
}

/*
 * Dispose of a reference on a call.
 */
void afs_put_call(struct afs_call *call)
{
	int n = atomic_dec_return(&call->usage);
	int o = atomic_read(&afs_outstanding_calls);

	trace_afs_call(call, afs_call_trace_put, n + 1, o,
		       __builtin_return_address(0));

	ASSERTCMP(n, >=, 0);
	if (n == 0) {
		ASSERT(!work_pending(&call->async_work));
		ASSERT(call->type->name != NULL);

		if (call->rxcall) {
			rxrpc_kernel_end_call(afs_socket, call->rxcall);
			call->rxcall = NULL;
		}
		if (call->type->destructor)
			call->type->destructor(call);

		kfree(call->request);
		kfree(call);

		o = atomic_dec_return(&afs_outstanding_calls);
		trace_afs_call(call, afs_call_trace_free, 0, o,
			       __builtin_return_address(0));
		if (o == 0)
			wake_up_atomic_t(&afs_outstanding_calls);
	}
}

/*
 * Queue the call for actual work.  Returns 0 unconditionally for convenience.
 */
int afs_queue_call_work(struct afs_call *call)
{
	int u = atomic_inc_return(&call->usage);

	trace_afs_call(call, afs_call_trace_work, u,
		       atomic_read(&afs_outstanding_calls),
		       __builtin_return_address(0));

	INIT_WORK(&call->work, call->type->work);

	if (!queue_work(afs_wq, &call->work))
		afs_put_call(call);
	return 0;
}

/*
 * allocate a call with flat request and reply buffers
 */
struct afs_call *afs_alloc_flat_call(const struct afs_call_type *type,
				     size_t request_size, size_t reply_max)
{
	struct afs_call *call;

	call = afs_alloc_call(type, GFP_NOFS);
	if (!call)
		goto nomem_call;

	if (request_size) {
		call->request_size = request_size;
		call->request = kmalloc(request_size, GFP_NOFS);
		if (!call->request)
			goto nomem_free;
	}

	if (reply_max) {
		call->reply_max = reply_max;
		call->buffer = kmalloc(reply_max, GFP_NOFS);
		if (!call->buffer)
			goto nomem_free;
	}

	init_waitqueue_head(&call->waitq);
	return call;

nomem_free:
	afs_put_call(call);
nomem_call:
	return NULL;
}

/*
 * clean up a call with flat buffer
 */
void afs_flat_call_destructor(struct afs_call *call)
{
	_enter("");

	kfree(call->request);
	call->request = NULL;
	kfree(call->buffer);
	call->buffer = NULL;
}

#define AFS_BVEC_MAX 8

/*
 * Load the given bvec with the next few pages.
 */
static void afs_load_bvec(struct afs_call *call, struct msghdr *msg,
			  struct bio_vec *bv, pgoff_t first, pgoff_t last,
			  unsigned offset)
{
	struct page *pages[AFS_BVEC_MAX];
	unsigned int nr, n, i, to, bytes = 0;

	nr = min_t(pgoff_t, last - first + 1, AFS_BVEC_MAX);
	n = find_get_pages_contig(call->mapping, first, nr, pages);
	ASSERTCMP(n, ==, nr);

	msg->msg_flags |= MSG_MORE;
	for (i = 0; i < nr; i++) {
		to = PAGE_SIZE;
		if (first + i >= last) {
			to = call->last_to;
			msg->msg_flags &= ~MSG_MORE;
		}
		bv[i].bv_page = pages[i];
		bv[i].bv_len = to - offset;
		bv[i].bv_offset = offset;
		bytes += to - offset;
		offset = 0;
	}

	iov_iter_bvec(&msg->msg_iter, WRITE | ITER_BVEC, bv, nr, bytes);
}

/*
 * attach the data from a bunch of pages on an inode to a call
 */
static int afs_send_pages(struct afs_call *call, struct msghdr *msg)
{
	struct bio_vec bv[AFS_BVEC_MAX];
	unsigned int bytes, nr, loop, offset;
	pgoff_t first = call->first, last = call->last;
	int ret;

	offset = call->first_offset;
	call->first_offset = 0;

	do {
		afs_load_bvec(call, msg, bv, first, last, offset);
		offset = 0;
		bytes = msg->msg_iter.count;
		nr = msg->msg_iter.nr_segs;

		/* Have to change the state *before* sending the last
		 * packet as RxRPC might give us the reply before it
		 * returns from sending the request.
		 */
		if (first + nr - 1 >= last)
			call->state = AFS_CALL_AWAIT_REPLY;
		ret = rxrpc_kernel_send_data(afs_socket, call->rxcall,
					     msg, bytes);
		for (loop = 0; loop < nr; loop++)
			put_page(bv[loop].bv_page);
		if (ret < 0)
			break;

		first += nr;
	} while (first <= last);

	return ret;
}

/*
 * initiate a call
 */
int afs_make_call(struct in_addr *addr, struct afs_call *call, gfp_t gfp,
		  bool async)
{
	struct sockaddr_rxrpc srx;
	struct rxrpc_call *rxcall;
	struct msghdr msg;
	struct kvec iov[1];
	size_t offset;
	s64 tx_total_len;
	u32 abort_code;
	int ret;

	_enter("%x,{%d},", addr->s_addr, ntohs(call->port));

	ASSERT(call->type != NULL);
	ASSERT(call->type->name != NULL);

	_debug("____MAKE %p{%s,%x} [%d]____",
	       call, call->type->name, key_serial(call->key),
	       atomic_read(&afs_outstanding_calls));

	call->async = async;

	memset(&srx, 0, sizeof(srx));
	srx.srx_family = AF_RXRPC;
	srx.srx_service = call->service_id;
	srx.transport_type = SOCK_DGRAM;
	srx.transport_len = sizeof(srx.transport.sin);
	srx.transport.sin.sin_family = AF_INET;
	srx.transport.sin.sin_port = call->port;
	memcpy(&srx.transport.sin.sin_addr, addr, 4);

	/* Work out the length we're going to transmit.  This is awkward for
	 * calls such as FS.StoreData where there's an extra injection of data
	 * after the initial fixed part.
	 */
	tx_total_len = call->request_size;
	if (call->send_pages) {
		tx_total_len += call->last_to - call->first_offset;
		tx_total_len += (call->last - call->first) * PAGE_SIZE;
	}

	/* create a call */
	rxcall = rxrpc_kernel_begin_call(afs_socket, &srx, call->key,
					 (unsigned long)call,
					 tx_total_len, gfp,
					 (async ?
					  afs_wake_up_async_call :
					  afs_wake_up_call_waiter));
	call->key = NULL;
	if (IS_ERR(rxcall)) {
		ret = PTR_ERR(rxcall);
		goto error_kill_call;
	}

	call->rxcall = rxcall;

	/* send the request */
	iov[0].iov_base	= call->request;
	iov[0].iov_len	= call->request_size;

	msg.msg_name		= NULL;
	msg.msg_namelen		= 0;
	iov_iter_kvec(&msg.msg_iter, WRITE | ITER_KVEC, iov, 1,
		      call->request_size);
	msg.msg_control		= NULL;
	msg.msg_controllen	= 0;
	msg.msg_flags		= (call->send_pages ? MSG_MORE : 0);

	/* We have to change the state *before* sending the last packet as
	 * rxrpc might give us the reply before it returns from sending the
	 * request.  Further, if the send fails, we may already have been given
	 * a notification and may have collected it.
	 */
	if (!call->send_pages)
		call->state = AFS_CALL_AWAIT_REPLY;
	ret = rxrpc_kernel_send_data(afs_socket, rxcall,
				     &msg, call->request_size);
	if (ret < 0)
		goto error_do_abort;

	if (call->send_pages) {
		ret = afs_send_pages(call, &msg);
		if (ret < 0)
			goto error_do_abort;
	}

	/* at this point, an async call may no longer exist as it may have
	 * already completed */
	if (call->async)
		return -EINPROGRESS;

	return afs_wait_for_call_to_complete(call);

error_do_abort:
	call->state = AFS_CALL_COMPLETE;
	if (ret != -ECONNABORTED) {
		rxrpc_kernel_abort_call(afs_socket, rxcall, RX_USER_ABORT,
					ret, "KSD");
	} else {
		abort_code = 0;
		offset = 0;
		rxrpc_kernel_recv_data(afs_socket, rxcall, NULL, 0, &offset,
				       false, &abort_code);
		ret = call->type->abort_to_error(abort_code);
	}
error_kill_call:
	afs_put_call(call);
	_leave(" = %d", ret);
	return ret;
}

/*
 * deliver messages to a call
 */
static void afs_deliver_to_call(struct afs_call *call)
{
	u32 abort_code;
	int ret;

	_enter("%s", call->type->name);

	while (call->state == AFS_CALL_AWAIT_REPLY ||
	       call->state == AFS_CALL_AWAIT_OP_ID ||
	       call->state == AFS_CALL_AWAIT_REQUEST ||
	       call->state == AFS_CALL_AWAIT_ACK
	       ) {
		if (call->state == AFS_CALL_AWAIT_ACK) {
			size_t offset = 0;
			ret = rxrpc_kernel_recv_data(afs_socket, call->rxcall,
						     NULL, 0, &offset, false,
						     &call->abort_code);
			trace_afs_recv_data(call, 0, offset, false, ret);

			if (ret == -EINPROGRESS || ret == -EAGAIN)
				return;
			if (ret == 1 || ret < 0) {
				call->state = AFS_CALL_COMPLETE;
				goto done;
			}
			return;
		}

		ret = call->type->deliver(call);
		switch (ret) {
		case 0:
			if (call->state == AFS_CALL_AWAIT_REPLY)
				call->state = AFS_CALL_COMPLETE;
			goto done;
		case -EINPROGRESS:
		case -EAGAIN:
			goto out;
		case -ECONNABORTED:
			goto call_complete;
		case -ENOTCONN:
			abort_code = RX_CALL_DEAD;
			rxrpc_kernel_abort_call(afs_socket, call->rxcall,
						abort_code, ret, "KNC");
			goto save_error;
		case -ENOTSUPP:
			abort_code = RXGEN_OPCODE;
			rxrpc_kernel_abort_call(afs_socket, call->rxcall,
						abort_code, ret, "KIV");
			goto save_error;
		case -ENODATA:
		case -EBADMSG:
		case -EMSGSIZE:
		default:
			abort_code = RXGEN_CC_UNMARSHAL;
			if (call->state != AFS_CALL_AWAIT_REPLY)
				abort_code = RXGEN_SS_UNMARSHAL;
			rxrpc_kernel_abort_call(afs_socket, call->rxcall,
						abort_code, -EBADMSG, "KUM");
			goto save_error;
		}
	}

done:
	if (call->state == AFS_CALL_COMPLETE && call->incoming)
		afs_put_call(call);
out:
	_leave("");
	return;

save_error:
	call->error = ret;
call_complete:
	call->state = AFS_CALL_COMPLETE;
	goto done;
}

/*
 * wait synchronously for a call to complete
 */
static int afs_wait_for_call_to_complete(struct afs_call *call)
{
	int ret;

	DECLARE_WAITQUEUE(myself, current);

	_enter("");

	add_wait_queue(&call->waitq, &myself);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		/* deliver any messages that are in the queue */
		if (call->state < AFS_CALL_COMPLETE && call->need_attention) {
			call->need_attention = false;
			__set_current_state(TASK_RUNNING);
			afs_deliver_to_call(call);
			continue;
		}

		if (call->state == AFS_CALL_COMPLETE ||
		    signal_pending(current))
			break;
		schedule();
	}

	remove_wait_queue(&call->waitq, &myself);
	__set_current_state(TASK_RUNNING);

	/* Kill off the call if it's still live. */
	if (call->state < AFS_CALL_COMPLETE) {
		_debug("call interrupted");
		rxrpc_kernel_abort_call(afs_socket, call->rxcall,
					RX_USER_ABORT, -EINTR, "KWI");
	}

	ret = call->error;
	_debug("call complete");
	afs_put_call(call);
	_leave(" = %d", ret);
	return ret;
}

/*
 * wake up a waiting call
 */
static void afs_wake_up_call_waiter(struct sock *sk, struct rxrpc_call *rxcall,
				    unsigned long call_user_ID)
{
	struct afs_call *call = (struct afs_call *)call_user_ID;

	call->need_attention = true;
	wake_up(&call->waitq);
}

/*
 * wake up an asynchronous call
 */
static void afs_wake_up_async_call(struct sock *sk, struct rxrpc_call *rxcall,
				   unsigned long call_user_ID)
{
	struct afs_call *call = (struct afs_call *)call_user_ID;
	int u;

	trace_afs_notify_call(rxcall, call);
	call->need_attention = true;

	u = __atomic_add_unless(&call->usage, 1, 0);
	if (u != 0) {
		trace_afs_call(call, afs_call_trace_wake, u,
			       atomic_read(&afs_outstanding_calls),
			       __builtin_return_address(0));

		if (!queue_work(afs_async_calls, &call->async_work))
			afs_put_call(call);
	}
}

/*
 * Delete an asynchronous call.  The work item carries a ref to the call struct
 * that we need to release.
 */
static void afs_delete_async_call(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, async_work);

	_enter("");

	afs_put_call(call);

	_leave("");
}

/*
 * Perform I/O processing on an asynchronous call.  The work item carries a ref
 * to the call struct that we either need to release or to pass on.
 */
static void afs_process_async_call(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, async_work);

	_enter("");

	if (call->state < AFS_CALL_COMPLETE && call->need_attention) {
		call->need_attention = false;
		afs_deliver_to_call(call);
	}

	if (call->state == AFS_CALL_COMPLETE) {
		call->reply = NULL;

		/* We have two refs to release - one from the alloc and one
		 * queued with the work item - and we can't just deallocate the
		 * call because the work item may be queued again.
		 */
		call->async_work.func = afs_delete_async_call;
		if (!queue_work(afs_async_calls, &call->async_work))
			afs_put_call(call);
	}

	afs_put_call(call);
	_leave("");
}

static void afs_rx_attach(struct rxrpc_call *rxcall, unsigned long user_call_ID)
{
	struct afs_call *call = (struct afs_call *)user_call_ID;

	call->rxcall = rxcall;
}

/*
 * Charge the incoming call preallocation.
 */
static void afs_charge_preallocation(struct work_struct *work)
{
	struct afs_call *call = afs_spare_incoming_call;

	for (;;) {
		if (!call) {
			call = afs_alloc_call(&afs_RXCMxxxx, GFP_KERNEL);
			if (!call)
				break;

			call->async = true;
			call->state = AFS_CALL_AWAIT_OP_ID;
			init_waitqueue_head(&call->waitq);
		}

		if (rxrpc_kernel_charge_accept(afs_socket,
					       afs_wake_up_async_call,
					       afs_rx_attach,
					       (unsigned long)call,
					       GFP_KERNEL) < 0)
			break;
		call = NULL;
	}
	afs_spare_incoming_call = call;
}

/*
 * Discard a preallocated call when a socket is shut down.
 */
static void afs_rx_discard_new_call(struct rxrpc_call *rxcall,
				    unsigned long user_call_ID)
{
	struct afs_call *call = (struct afs_call *)user_call_ID;

	call->rxcall = NULL;
	afs_put_call(call);
}

/*
 * Notification of an incoming call.
 */
static void afs_rx_new_call(struct sock *sk, struct rxrpc_call *rxcall,
			    unsigned long user_call_ID)
{
	queue_work(afs_wq, &afs_charge_preallocation_work);
}

/*
 * Grab the operation ID from an incoming cache manager call.  The socket
 * buffer is discarded on error or if we don't yet have sufficient data.
 */
static int afs_deliver_cm_op_id(struct afs_call *call)
{
	int ret;

	_enter("{%zu}", call->offset);

	ASSERTCMP(call->offset, <, 4);

	/* the operation ID forms the first four bytes of the request data */
	ret = afs_extract_data(call, &call->tmp, 4, true);
	if (ret < 0)
		return ret;

	call->operation_ID = ntohl(call->tmp);
	call->state = AFS_CALL_AWAIT_REQUEST;
	call->offset = 0;

	/* ask the cache manager to route the call (it'll change the call type
	 * if successful) */
	if (!afs_cm_incoming_call(call))
		return -ENOTSUPP;

	trace_afs_cb_call(call);

	/* pass responsibility for the remainer of this message off to the
	 * cache manager op */
	return call->type->deliver(call);
}

/*
 * send an empty reply
 */
void afs_send_empty_reply(struct afs_call *call)
{
	struct msghdr msg;

	_enter("");

	rxrpc_kernel_set_tx_length(afs_socket, call->rxcall, 0);

	msg.msg_name		= NULL;
	msg.msg_namelen		= 0;
	iov_iter_kvec(&msg.msg_iter, WRITE | ITER_KVEC, NULL, 0, 0);
	msg.msg_control		= NULL;
	msg.msg_controllen	= 0;
	msg.msg_flags		= 0;

	call->state = AFS_CALL_AWAIT_ACK;
	switch (rxrpc_kernel_send_data(afs_socket, call->rxcall, &msg, 0)) {
	case 0:
		_leave(" [replied]");
		return;

	case -ENOMEM:
		_debug("oom");
		rxrpc_kernel_abort_call(afs_socket, call->rxcall,
					RX_USER_ABORT, -ENOMEM, "KOO");
	default:
		_leave(" [error]");
		return;
	}
}

/*
 * send a simple reply
 */
void afs_send_simple_reply(struct afs_call *call, const void *buf, size_t len)
{
	struct msghdr msg;
	struct kvec iov[1];
	int n;

	_enter("");

	rxrpc_kernel_set_tx_length(afs_socket, call->rxcall, len);

	iov[0].iov_base		= (void *) buf;
	iov[0].iov_len		= len;
	msg.msg_name		= NULL;
	msg.msg_namelen		= 0;
	iov_iter_kvec(&msg.msg_iter, WRITE | ITER_KVEC, iov, 1, len);
	msg.msg_control		= NULL;
	msg.msg_controllen	= 0;
	msg.msg_flags		= 0;

	call->state = AFS_CALL_AWAIT_ACK;
	n = rxrpc_kernel_send_data(afs_socket, call->rxcall, &msg, len);
	if (n >= 0) {
		/* Success */
		_leave(" [replied]");
		return;
	}

	if (n == -ENOMEM) {
		_debug("oom");
		rxrpc_kernel_abort_call(afs_socket, call->rxcall,
					RX_USER_ABORT, -ENOMEM, "KOO");
	}
	_leave(" [error]");
}

/*
 * Extract a piece of data from the received data socket buffers.
 */
int afs_extract_data(struct afs_call *call, void *buf, size_t count,
		     bool want_more)
{
	int ret;

	_enter("{%s,%zu},,%zu,%d",
	       call->type->name, call->offset, count, want_more);

	ASSERTCMP(call->offset, <=, count);

	ret = rxrpc_kernel_recv_data(afs_socket, call->rxcall,
				     buf, count, &call->offset,
				     want_more, &call->abort_code);
	trace_afs_recv_data(call, count, call->offset, want_more, ret);
	if (ret == 0 || ret == -EAGAIN)
		return ret;

	if (ret == 1) {
		switch (call->state) {
		case AFS_CALL_AWAIT_REPLY:
			call->state = AFS_CALL_COMPLETE;
			break;
		case AFS_CALL_AWAIT_REQUEST:
			call->state = AFS_CALL_REPLYING;
			break;
		default:
			break;
		}
		return 0;
	}

	if (ret == -ECONNABORTED)
		call->error = call->type->abort_to_error(call->abort_code);
	else
		call->error = ret;
	call->state = AFS_CALL_COMPLETE;
	return ret;
}
