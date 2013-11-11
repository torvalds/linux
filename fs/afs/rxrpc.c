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

static struct socket *afs_socket; /* my RxRPC socket */
static struct workqueue_struct *afs_async_calls;
static atomic_t afs_outstanding_calls;
static atomic_t afs_outstanding_skbs;

static void afs_wake_up_call_waiter(struct afs_call *);
static int afs_wait_for_call_to_complete(struct afs_call *);
static void afs_wake_up_async_call(struct afs_call *);
static int afs_dont_wait_for_call_to_complete(struct afs_call *);
static void afs_process_async_call(struct work_struct *);
static void afs_rx_interceptor(struct sock *, unsigned long, struct sk_buff *);
static int afs_deliver_cm_op_id(struct afs_call *, struct sk_buff *, bool);

/* synchronous call management */
const struct afs_wait_mode afs_sync_call = {
	.rx_wakeup	= afs_wake_up_call_waiter,
	.wait		= afs_wait_for_call_to_complete,
};

/* asynchronous call management */
const struct afs_wait_mode afs_async_call = {
	.rx_wakeup	= afs_wake_up_async_call,
	.wait		= afs_dont_wait_for_call_to_complete,
};

/* asynchronous incoming call management */
static const struct afs_wait_mode afs_async_incoming_call = {
	.rx_wakeup	= afs_wake_up_async_call,
};

/* asynchronous incoming call initial processing */
static const struct afs_call_type afs_RXCMxxxx = {
	.name		= "CB.xxxx",
	.deliver	= afs_deliver_cm_op_id,
	.abort_to_error	= afs_abort_to_error,
};

static void afs_collect_incoming_call(struct work_struct *);

static struct sk_buff_head afs_incoming_calls;
static DECLARE_WORK(afs_collect_incoming_call_work, afs_collect_incoming_call);

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

	skb_queue_head_init(&afs_incoming_calls);

	afs_async_calls = create_singlethread_workqueue("kafsd");
	if (!afs_async_calls) {
		_leave(" = -ENOMEM [wq]");
		return -ENOMEM;
	}

	ret = sock_create_kern(AF_RXRPC, SOCK_DGRAM, PF_INET, &socket);
	if (ret < 0) {
		destroy_workqueue(afs_async_calls);
		_leave(" = %d [socket]", ret);
		return ret;
	}

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
	if (ret < 0) {
		sock_release(socket);
		destroy_workqueue(afs_async_calls);
		_leave(" = %d [bind]", ret);
		return ret;
	}

	rxrpc_kernel_intercept_rx_messages(socket, afs_rx_interceptor);

	afs_socket = socket;
	_leave(" = 0");
	return 0;
}

/*
 * close the RxRPC socket AFS was using
 */
void afs_close_socket(void)
{
	_enter("");

	sock_release(afs_socket);

	_debug("dework");
	destroy_workqueue(afs_async_calls);

	ASSERTCMP(atomic_read(&afs_outstanding_skbs), ==, 0);
	ASSERTCMP(atomic_read(&afs_outstanding_calls), ==, 0);
	_leave("");
}

/*
 * note that the data in a socket buffer is now delivered and that the buffer
 * should be freed
 */
static void afs_data_delivered(struct sk_buff *skb)
{
	if (!skb) {
		_debug("DLVR NULL [%d]", atomic_read(&afs_outstanding_skbs));
		dump_stack();
	} else {
		_debug("DLVR %p{%u} [%d]",
		       skb, skb->mark, atomic_read(&afs_outstanding_skbs));
		if (atomic_dec_return(&afs_outstanding_skbs) == -1)
			BUG();
		rxrpc_kernel_data_delivered(skb);
	}
}

/*
 * free a socket buffer
 */
static void afs_free_skb(struct sk_buff *skb)
{
	if (!skb) {
		_debug("FREE NULL [%d]", atomic_read(&afs_outstanding_skbs));
		dump_stack();
	} else {
		_debug("FREE %p{%u} [%d]",
		       skb, skb->mark, atomic_read(&afs_outstanding_skbs));
		if (atomic_dec_return(&afs_outstanding_skbs) == -1)
			BUG();
		rxrpc_kernel_free_skb(skb);
	}
}

/*
 * free a call
 */
static void afs_free_call(struct afs_call *call)
{
	_debug("DONE %p{%s} [%d]",
	       call, call->type->name, atomic_read(&afs_outstanding_calls));
	if (atomic_dec_return(&afs_outstanding_calls) == -1)
		BUG();

	ASSERTCMP(call->rxcall, ==, NULL);
	ASSERT(!work_pending(&call->async_work));
	ASSERT(skb_queue_empty(&call->rx_queue));
	ASSERT(call->type->name != NULL);

	kfree(call->request);
	kfree(call);
}

/*
 * allocate a call with flat request and reply buffers
 */
struct afs_call *afs_alloc_flat_call(const struct afs_call_type *type,
				     size_t request_size, size_t reply_size)
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
	call->reply_max = reply_size;

	if (request_size) {
		call->request = kmalloc(request_size, GFP_NOFS);
		if (!call->request)
			goto nomem_free;
	}

	if (reply_size) {
		call->buffer = kmalloc(reply_size, GFP_NOFS);
		if (!call->buffer)
			goto nomem_free;
	}

	init_waitqueue_head(&call->waitq);
	skb_queue_head_init(&call->rx_queue);
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
			msg->msg_iov = (struct iovec *) iov;
			msg->msg_iovlen = 1;

			/* have to change the state *before* sending the last
			 * packet as RxRPC might give us the reply before it
			 * returns from sending the request */
			if (first + loop >= last)
				call->state = AFS_CALL_AWAIT_REPLY;
			ret = rxrpc_kernel_send_data(call->rxcall, msg,
						     to - offset);
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
	struct sk_buff *skb;

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
					 (unsigned long) call, gfp);
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
	msg.msg_iov		= (struct iovec *) iov;
	msg.msg_iovlen		= 1;
	msg.msg_control		= NULL;
	msg.msg_controllen	= 0;
	msg.msg_flags		= (call->send_pages ? MSG_MORE : 0);

	/* have to change the state *before* sending the last packet as RxRPC
	 * might give us the reply before it returns from sending the
	 * request */
	if (!call->send_pages)
		call->state = AFS_CALL_AWAIT_REPLY;
	ret = rxrpc_kernel_send_data(rxcall, &msg, call->request_size);
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
	rxrpc_kernel_abort_call(rxcall, RX_USER_ABORT);
	while ((skb = skb_dequeue(&call->rx_queue)))
		afs_free_skb(skb);
	rxrpc_kernel_end_call(rxcall);
	call->rxcall = NULL;
error_kill_call:
	call->type->destructor(call);
	afs_free_call(call);
	_leave(" = %d", ret);
	return ret;
}

/*
 * handles intercepted messages that were arriving in the socket's Rx queue
 * - called with the socket receive queue lock held to ensure message ordering
 * - called with softirqs disabled
 */
static void afs_rx_interceptor(struct sock *sk, unsigned long user_call_ID,
			       struct sk_buff *skb)
{
	struct afs_call *call = (struct afs_call *) user_call_ID;

	_enter("%p,,%u", call, skb->mark);

	_debug("ICPT %p{%u} [%d]",
	       skb, skb->mark, atomic_read(&afs_outstanding_skbs));

	ASSERTCMP(sk, ==, afs_socket->sk);
	atomic_inc(&afs_outstanding_skbs);

	if (!call) {
		/* its an incoming call for our callback service */
		skb_queue_tail(&afs_incoming_calls, skb);
		queue_work(afs_wq, &afs_collect_incoming_call_work);
	} else {
		/* route the messages directly to the appropriate call */
		skb_queue_tail(&call->rx_queue, skb);
		call->wait_mode->rx_wakeup(call);
	}

	_leave("");
}

/*
 * deliver messages to a call
 */
static void afs_deliver_to_call(struct afs_call *call)
{
	struct sk_buff *skb;
	bool last;
	u32 abort_code;
	int ret;

	_enter("");

	while ((call->state == AFS_CALL_AWAIT_REPLY ||
		call->state == AFS_CALL_AWAIT_OP_ID ||
		call->state == AFS_CALL_AWAIT_REQUEST ||
		call->state == AFS_CALL_AWAIT_ACK) &&
	       (skb = skb_dequeue(&call->rx_queue))) {
		switch (skb->mark) {
		case RXRPC_SKB_MARK_DATA:
			_debug("Rcv DATA");
			last = rxrpc_kernel_is_data_last(skb);
			ret = call->type->deliver(call, skb, last);
			switch (ret) {
			case 0:
				if (last &&
				    call->state == AFS_CALL_AWAIT_REPLY)
					call->state = AFS_CALL_COMPLETE;
				break;
			case -ENOTCONN:
				abort_code = RX_CALL_DEAD;
				goto do_abort;
			case -ENOTSUPP:
				abort_code = RX_INVALID_OPERATION;
				goto do_abort;
			default:
				abort_code = RXGEN_CC_UNMARSHAL;
				if (call->state != AFS_CALL_AWAIT_REPLY)
					abort_code = RXGEN_SS_UNMARSHAL;
			do_abort:
				rxrpc_kernel_abort_call(call->rxcall,
							abort_code);
				call->error = ret;
				call->state = AFS_CALL_ERROR;
				break;
			}
			afs_data_delivered(skb);
			skb = NULL;
			continue;
		case RXRPC_SKB_MARK_FINAL_ACK:
			_debug("Rcv ACK");
			call->state = AFS_CALL_COMPLETE;
			break;
		case RXRPC_SKB_MARK_BUSY:
			_debug("Rcv BUSY");
			call->error = -EBUSY;
			call->state = AFS_CALL_BUSY;
			break;
		case RXRPC_SKB_MARK_REMOTE_ABORT:
			abort_code = rxrpc_kernel_get_abort_code(skb);
			call->error = call->type->abort_to_error(abort_code);
			call->state = AFS_CALL_ABORTED;
			_debug("Rcv ABORT %u -> %d", abort_code, call->error);
			break;
		case RXRPC_SKB_MARK_NET_ERROR:
			call->error = -rxrpc_kernel_get_error_number(skb);
			call->state = AFS_CALL_ERROR;
			_debug("Rcv NET ERROR %d", call->error);
			break;
		case RXRPC_SKB_MARK_LOCAL_ERROR:
			call->error = -rxrpc_kernel_get_error_number(skb);
			call->state = AFS_CALL_ERROR;
			_debug("Rcv LOCAL ERROR %d", call->error);
			break;
		default:
			BUG();
			break;
		}

		afs_free_skb(skb);
	}

	/* make sure the queue is empty if the call is done with (we might have
	 * aborted the call early because of an unmarshalling error) */
	if (call->state >= AFS_CALL_COMPLETE) {
		while ((skb = skb_dequeue(&call->rx_queue)))
			afs_free_skb(skb);
		if (call->incoming) {
			rxrpc_kernel_end_call(call->rxcall);
			call->rxcall = NULL;
			call->type->destructor(call);
			afs_free_call(call);
		}
	}

	_leave("");
}

/*
 * wait synchronously for a call to complete
 */
static int afs_wait_for_call_to_complete(struct afs_call *call)
{
	struct sk_buff *skb;
	int ret;

	DECLARE_WAITQUEUE(myself, current);

	_enter("");

	add_wait_queue(&call->waitq, &myself);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		/* deliver any messages that are in the queue */
		if (!skb_queue_empty(&call->rx_queue)) {
			__set_current_state(TASK_RUNNING);
			afs_deliver_to_call(call);
			continue;
		}

		ret = call->error;
		if (call->state >= AFS_CALL_COMPLETE)
			break;
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
		rxrpc_kernel_abort_call(call->rxcall, RX_CALL_DEAD);
		while ((skb = skb_dequeue(&call->rx_queue)))
			afs_free_skb(skb);
	}

	_debug("call complete");
	rxrpc_kernel_end_call(call->rxcall);
	call->rxcall = NULL;
	call->type->destructor(call);
	afs_free_call(call);
	_leave(" = %d", ret);
	return ret;
}

/*
 * wake up a waiting call
 */
static void afs_wake_up_call_waiter(struct afs_call *call)
{
	wake_up(&call->waitq);
}

/*
 * wake up an asynchronous call
 */
static void afs_wake_up_async_call(struct afs_call *call)
{
	_enter("");
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
	struct afs_call *call =
		container_of(work, struct afs_call, async_work);

	_enter("");

	afs_free_call(call);

	_leave("");
}

/*
 * perform processing on an asynchronous call
 * - on a multiple-thread workqueue this work item may try to run on several
 *   CPUs at the same time
 */
static void afs_process_async_call(struct work_struct *work)
{
	struct afs_call *call =
		container_of(work, struct afs_call, async_work);

	_enter("");

	if (!skb_queue_empty(&call->rx_queue))
		afs_deliver_to_call(call);

	if (call->state >= AFS_CALL_COMPLETE && call->wait_mode) {
		if (call->wait_mode->async_complete)
			call->wait_mode->async_complete(call->reply,
							call->error);
		call->reply = NULL;

		/* kill the call */
		rxrpc_kernel_end_call(call->rxcall);
		call->rxcall = NULL;
		if (call->type->destructor)
			call->type->destructor(call);

		/* we can't just delete the call because the work item may be
		 * queued */
		PREPARE_WORK(&call->async_work, afs_delete_async_call);
		queue_work(afs_async_calls, &call->async_work);
	}

	_leave("");
}

/*
 * empty a socket buffer into a flat reply buffer
 */
void afs_transfer_reply(struct afs_call *call, struct sk_buff *skb)
{
	size_t len = skb->len;

	if (skb_copy_bits(skb, 0, call->buffer + call->reply_size, len) < 0)
		BUG();
	call->reply_size += len;
}

/*
 * accept the backlog of incoming calls
 */
static void afs_collect_incoming_call(struct work_struct *work)
{
	struct rxrpc_call *rxcall;
	struct afs_call *call = NULL;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&afs_incoming_calls))) {
		_debug("new call");

		/* don't need the notification */
		afs_free_skb(skb);

		if (!call) {
			call = kzalloc(sizeof(struct afs_call), GFP_KERNEL);
			if (!call) {
				rxrpc_kernel_reject_call(afs_socket);
				return;
			}

			INIT_WORK(&call->async_work, afs_process_async_call);
			call->wait_mode = &afs_async_incoming_call;
			call->type = &afs_RXCMxxxx;
			init_waitqueue_head(&call->waitq);
			skb_queue_head_init(&call->rx_queue);
			call->state = AFS_CALL_AWAIT_OP_ID;

			_debug("CALL %p{%s} [%d]",
			       call, call->type->name,
			       atomic_read(&afs_outstanding_calls));
			atomic_inc(&afs_outstanding_calls);
		}

		rxcall = rxrpc_kernel_accept_call(afs_socket,
						  (unsigned long) call);
		if (!IS_ERR(rxcall)) {
			call->rxcall = rxcall;
			call = NULL;
		}
	}

	if (call)
		afs_free_call(call);
}

/*
 * grab the operation ID from an incoming cache manager call
 */
static int afs_deliver_cm_op_id(struct afs_call *call, struct sk_buff *skb,
				bool last)
{
	size_t len = skb->len;
	void *oibuf = (void *) &call->operation_ID;

	_enter("{%u},{%zu},%d", call->offset, len, last);

	ASSERTCMP(call->offset, <, 4);

	/* the operation ID forms the first four bytes of the request data */
	len = min_t(size_t, len, 4 - call->offset);
	if (skb_copy_bits(skb, 0, oibuf + call->offset, len) < 0)
		BUG();
	if (!pskb_pull(skb, len))
		BUG();
	call->offset += len;

	if (call->offset < 4) {
		if (last) {
			_leave(" = -EBADMSG [op ID short]");
			return -EBADMSG;
		}
		_leave(" = 0 [incomplete]");
		return 0;
	}

	call->state = AFS_CALL_AWAIT_REQUEST;

	/* ask the cache manager to route the call (it'll change the call type
	 * if successful) */
	if (!afs_cm_incoming_call(call))
		return -ENOTSUPP;

	/* pass responsibility for the remainer of this message off to the
	 * cache manager op */
	return call->type->deliver(call, skb, last);
}

/*
 * send an empty reply
 */
void afs_send_empty_reply(struct afs_call *call)
{
	struct msghdr msg;
	struct iovec iov[1];

	_enter("");

	iov[0].iov_base		= NULL;
	iov[0].iov_len		= 0;
	msg.msg_name		= NULL;
	msg.msg_namelen		= 0;
	msg.msg_iov		= iov;
	msg.msg_iovlen		= 0;
	msg.msg_control		= NULL;
	msg.msg_controllen	= 0;
	msg.msg_flags		= 0;

	call->state = AFS_CALL_AWAIT_ACK;
	switch (rxrpc_kernel_send_data(call->rxcall, &msg, 0)) {
	case 0:
		_leave(" [replied]");
		return;

	case -ENOMEM:
		_debug("oom");
		rxrpc_kernel_abort_call(call->rxcall, RX_USER_ABORT);
	default:
		rxrpc_kernel_end_call(call->rxcall);
		call->rxcall = NULL;
		call->type->destructor(call);
		afs_free_call(call);
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
	struct iovec iov[1];
	int n;

	_enter("");

	iov[0].iov_base		= (void *) buf;
	iov[0].iov_len		= len;
	msg.msg_name		= NULL;
	msg.msg_namelen		= 0;
	msg.msg_iov		= iov;
	msg.msg_iovlen		= 1;
	msg.msg_control		= NULL;
	msg.msg_controllen	= 0;
	msg.msg_flags		= 0;

	call->state = AFS_CALL_AWAIT_ACK;
	n = rxrpc_kernel_send_data(call->rxcall, &msg, len);
	if (n >= 0) {
		_leave(" [replied]");
		return;
	}
	if (n == -ENOMEM) {
		_debug("oom");
		rxrpc_kernel_abort_call(call->rxcall, RX_USER_ABORT);
	}
	rxrpc_kernel_end_call(call->rxcall);
	call->rxcall = NULL;
	call->type->destructor(call);
	afs_free_call(call);
	_leave(" [error]");
}

/*
 * extract a piece of data from the received data socket buffers
 */
int afs_extract_data(struct afs_call *call, struct sk_buff *skb,
		     bool last, void *buf, size_t count)
{
	size_t len = skb->len;

	_enter("{%u},{%zu},%d,,%zu", call->offset, len, last, count);

	ASSERTCMP(call->offset, <, count);

	len = min_t(size_t, len, count - call->offset);
	if (skb_copy_bits(skb, 0, buf + call->offset, len) < 0 ||
	    !pskb_pull(skb, len))
		BUG();
	call->offset += len;

	if (call->offset < count) {
		if (last) {
			_leave(" = -EBADMSG [%d < %zu]", call->offset, count);
			return -EBADMSG;
		}
		_leave(" = -EAGAIN");
		return -EAGAIN;
	}
	return 0;
}
