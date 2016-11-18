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
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <rxrpc/packet.h>
#include "internal.h"
#include "afs_cm.h"

struct socket *afs_socket; /* my RxRPC socket */
static struct workqueue_struct *afs_async_calls;
static struct afs_call *afs_spare_incoming_call;
static atomic_t afs_outstanding_calls;

static void afs_free_call(struct afs_call *);
static void afs_wake_up_call_waiter(struct sock *, struct rxrpc_call *, unsigned long);
static int afs_wait_for_call_to_complete(struct afs_call *);
static void afs_wake_up_async_call(struct sock *, struct rxrpc_call *, unsigned long);
static int afs_dont_wait_for_call_to_complete(struct afs_call *);
static void afs_process_async_call(struct work_struct *);
static void afs_rx_new_call(struct sock *, struct rxrpc_call *, unsigned long);
static void afs_rx_discard_new_call(struct rxrpc_call *, unsigned long);
static int afs_deliver_cm_op_id(struct afs_call *);

/* synchronous call management */
const struct afs_wait_mode afs_sync_call = {
	.notify_rx	= afs_wake_up_call_waiter,
	.wait		= afs_wait_for_call_to_complete,
};

/* asynchronous call management */
const struct afs_wait_mode afs_async_call = {
	.notify_rx	= afs_wake_up_async_call,
	.wait		= afs_dont_wait_for_call_to_complete,
};

/* asynchronous incoming call management */
static const struct afs_wait_mode afs_async_incoming_call = {
	.notify_rx	= afs_wake_up_async_call,
};

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

	if (afs_spare_incoming_call) {
		atomic_inc(&afs_outstanding_calls);
		afs_free_call(afs_spare_incoming_call);
		afs_spare_incoming_call = NULL;
	}

	_debug("outstanding %u", atomic_read(&afs_outstanding_calls));
	wait_on_atomic_t(&afs_outstanding_calls, afs_wait_atomic_t,
			 TASK_UNINTERRUPTIBLE);
	_debug("no outstanding calls");

	flush_workqueue(afs_async_calls);
	kernel_sock_shutdown(afs_socket, SHUT_RDWR);
	flush_workqueue(afs_async_calls);
	sock_release(afs_socket);

	_debug("dework");
	destroy_workqueue(afs_async_calls);
	_leave("");
}

/*
 * free a call
 */
static void afs_free_call(struct afs_call *call)
{
	_debug("DONE %p{%s} [%d]",
	       call, call->type->name, atomic_read(&afs_outstanding_calls));

	ASSERTCMP(call->rxcall, ==, NULL);
	ASSERT(!work_pending(&call->async_work));
	ASSERT(call->type->name != NULL);

	kfree(call->request);
	kfree(call);

	if (atomic_dec_and_test(&afs_outstanding_calls))
		wake_up_atomic_t(&afs_outstanding_calls);
}

/*
 * End a call but do not free it
 */
static void afs_end_call_nofree(struct afs_call *call)
{
	if (call->rxcall) {
		rxrpc_kernel_end_call(afs_socket, call->rxcall);
		call->rxcall = NULL;
	}
	if (call->type->destructor)
		call->type->destructor(call);
}

/*
 * End a call and free it
 */
static void afs_end_call(struct afs_call *call)
{
	afs_end_call_nofree(call);
	afs_free_call(call);
}

/*
 * allocate a call with flat request and reply buffers
 */
struct afs_call *afs_alloc_flat_call(const struct afs_call_type *type,
				     size_t request_size, size_t reply_max)
{
	struct afs_call *call;

	call = kzalloc(sizeof(*call), GFP_NOFS);
	if (!call)
		goto nomem_call;

	_debug("CALL %p{%s} [%d]",
	       call, type->name, atomic_read(&afs_outstanding_calls));
	atomic_inc(&afs_outstanding_calls);

	call->type = type;
	call->request_size = request_size;
	call->reply_max = reply_max;

	if (request_size) {
		call->request = kmalloc(request_size, GFP_NOFS);
		if (!call->request)
			goto nomem_free;
	}

	if (reply_max) {
		call->buffer = kmalloc(reply_max, GFP_NOFS);
		if (!call->buffer)
			goto nomem_free;
	}

	init_waitqueue_head(&call->waitq);
	return call;

nomem_free:
	afs_free_call(call);
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

/*
 * attach the data from a bunch of pages on an inode to a call
 */
static int afs_send_pages(struct afs_call *call, struct msghdr *msg,
			  struct kvec *iov)
{
	struct page *pages[8];
	unsigned count, n, loop, offset, to;
	pgoff_t first = call->first, last = call->last;
	int ret;

	_enter("");

	offset = call->first_offset;
	call->first_offset = 0;

	do {
		_debug("attach %lx-%lx", first, last);

		count = last - first + 1;
		if (count > ARRAY_SIZE(pages))
			count = ARRAY_SIZE(pages);
		n = find_get_pages_contig(call->mapping, first, count, pages);
		ASSERTCMP(n, ==, count);

		loop = 0;
		do {
			msg->msg_flags = 0;
			to = PAGE_SIZE;
			if (first + loop >= last)
				to = call->last_to;
			else
				msg->msg_flags = MSG_MORE;
			iov->iov_base = kmap(pages[loop]) + offset;
			iov->iov_len = to - offset;
			offset = 0;

			_debug("- range %u-%u%s",
			       offset, to, msg->msg_flags ? " [more]" : "");
			iov_iter_kvec(&msg->msg_iter, WRITE | ITER_KVEC,
				      iov, 1, to - offset);

			/* have to change the state *before* sending the last
			 * packet as RxRPC might give us the reply before it
			 * returns from sending the request */
			if (first + loop >= last)
				call->state = AFS_CALL_AWAIT_REPLY;
			ret = rxrpc_kernel_send_data(afs_socket, call->rxcall,
						     msg, to - offset);
			kunmap(pages[loop]);
			if (ret < 0)
				break;
		} while (++loop < count);
		first += count;

		for (loop = 0; loop < count; loop++)
			put_page(pages[loop]);
		if (ret < 0)
			break;
	} while (first <= last);

	_leave(" = %d", ret);
	return ret;
}

/*
 * initiate a call
 */
int afs_make_call(struct in_addr *addr, struct afs_call *call, gfp_t gfp,
		  const struct afs_wait_mode *wait_mode)
{
	struct sockaddr_rxrpc srx;
	struct rxrpc_call *rxcall;
	struct msghdr msg;
	struct kvec iov[1];
	int ret;

	_enter("%x,{%d},", addr->s_addr, ntohs(call->port));

	ASSERT(call->type != NULL);
	ASSERT(call->type->name != NULL);

	_debug("____MAKE %p{%s,%x} [%d]____",
	       call, call->type->name, key_serial(call->key),
	       atomic_read(&afs_outstanding_calls));

	call->wait_mode = wait_mode;
	INIT_WORK(&call->async_work, afs_process_async_call);

	memset(&srx, 0, sizeof(srx));
	srx.srx_family = AF_RXRPC;
	srx.srx_service = call->service_id;
	srx.transport_type = SOCK_DGRAM;
	srx.transport_len = sizeof(srx.transport.sin);
	srx.transport.sin.sin_family = AF_INET;
	srx.transport.sin.sin_port = call->port;
	memcpy(&srx.transport.sin.sin_addr, addr, 4);

	/* create a call */
	rxcall = rxrpc_kernel_begin_call(afs_socket, &srx, call->key,
					 (unsigned long) call, gfp,
					 wait_mode->notify_rx);
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

	/* have to change the state *before* sending the last packet as RxRPC
	 * might give us the reply before it returns from sending the
	 * request */
	if (!call->send_pages)
		call->state = AFS_CALL_AWAIT_REPLY;
	ret = rxrpc_kernel_send_data(afs_socket, rxcall,
				     &msg, call->request_size);
	if (ret < 0)
		goto error_do_abort;

	if (call->send_pages) {
		ret = afs_send_pages(call, &msg, iov);
		if (ret < 0)
			goto error_do_abort;
	}

	/* at this point, an async call may no longer exist as it may have
	 * already completed */
	return wait_mode->wait(call);

error_do_abort:
	rxrpc_kernel_abort_call(afs_socket, rxcall, RX_USER_ABORT, -ret, "KSD");
error_kill_call:
	afs_end_call(call);
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
		case -ENOTCONN:
			abort_code = RX_CALL_DEAD;
			rxrpc_kernel_abort_call(afs_socket, call->rxcall,
						abort_code, -ret, "KNC");
			goto do_abort;
		case -ENOTSUPP:
			abort_code = RX_INVALID_OPERATION;
			rxrpc_kernel_abort_call(afs_socket, call->rxcall,
						abort_code, -ret, "KIV");
			goto do_abort;
		case -ENODATA:
		case -EBADMSG:
		case -EMSGSIZE:
		default:
			abort_code = RXGEN_CC_UNMARSHAL;
			if (call->state != AFS_CALL_AWAIT_REPLY)
				abort_code = RXGEN_SS_UNMARSHAL;
			rxrpc_kernel_abort_call(afs_socket, call->rxcall,
						abort_code, EBADMSG, "KUM");
			goto do_abort;
		}
	}

done:
	if (call->state == AFS_CALL_COMPLETE && call->incoming)
		afs_end_call(call);
out:
	_leave("");
	return;

do_abort:
	call->error = ret;
	call->state = AFS_CALL_COMPLETE;
	goto done;
}

/*
 * wait synchronously for a call to complete
 */
static int afs_wait_for_call_to_complete(struct afs_call *call)
{
	const char *abort_why;
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

		abort_why = "KWC";
		ret = call->error;
		if (call->state == AFS_CALL_COMPLETE)
			break;
		abort_why = "KWI";
		ret = -EINTR;
		if (signal_pending(current))
			break;
		schedule();
	}

	remove_wait_queue(&call->waitq, &myself);
	__set_current_state(TASK_RUNNING);

	/* kill the call */
	if (call->state < AFS_CALL_COMPLETE) {
		_debug("call incomplete");
		rxrpc_kernel_abort_call(afs_socket, call->rxcall,
					RX_CALL_DEAD, -ret, abort_why);
	}

	_debug("call complete");
	afs_end_call(call);
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

	call->need_attention = true;
	queue_work(afs_async_calls, &call->async_work);
}

/*
 * put a call into asynchronous mode
 * - mustn't touch the call descriptor as the call my have completed by the
 *   time we get here
 */
static int afs_dont_wait_for_call_to_complete(struct afs_call *call)
{
	_enter("");
	return -EINPROGRESS;
}

/*
 * delete an asynchronous call
 */
static void afs_delete_async_call(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, async_work);

	_enter("");

	afs_free_call(call);

	_leave("");
}

/*
 * perform processing on an asynchronous call
 */
static void afs_process_async_call(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, async_work);

	_enter("");

	if (call->state < AFS_CALL_COMPLETE && call->need_attention) {
		call->need_attention = false;
		afs_deliver_to_call(call);
	}

	if (call->state == AFS_CALL_COMPLETE && call->wait_mode) {
		if (call->wait_mode->async_complete)
			call->wait_mode->async_complete(call->reply,
							call->error);
		call->reply = NULL;

		/* kill the call */
		afs_end_call_nofree(call);

		/* we can't just delete the call because the work item may be
		 * queued */
		call->async_work.func = afs_delete_async_call;
		queue_work(afs_async_calls, &call->async_work);
	}

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
			call = kzalloc(sizeof(struct afs_call), GFP_KERNEL);
			if (!call)
				break;

			INIT_WORK(&call->async_work, afs_process_async_call);
			call->wait_mode = &afs_async_incoming_call;
			call->type = &afs_RXCMxxxx;
			init_waitqueue_head(&call->waitq);
			call->state = AFS_CALL_AWAIT_OP_ID;
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

	atomic_inc(&afs_outstanding_calls);
	call->rxcall = NULL;
	afs_free_call(call);
}

/*
 * Notification of an incoming call.
 */
static void afs_rx_new_call(struct sock *sk, struct rxrpc_call *rxcall,
			    unsigned long user_call_ID)
{
	atomic_inc(&afs_outstanding_calls);
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
					RX_USER_ABORT, ENOMEM, "KOO");
	default:
		afs_end_call(call);
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
					RX_USER_ABORT, ENOMEM, "KOO");
	}
	afs_end_call(call);
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
