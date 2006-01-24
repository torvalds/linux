/* cmservice.c: AFS Cache Manager Service
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include "server.h"
#include "cell.h"
#include "transport.h"
#include <rxrpc/rxrpc.h>
#include <rxrpc/transport.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include "cmservice.h"
#include "internal.h"

static unsigned afscm_usage;		/* AFS cache manager usage count */
static struct rw_semaphore afscm_sem;	/* AFS cache manager start/stop semaphore */

static int afscm_new_call(struct rxrpc_call *call);
static void afscm_attention(struct rxrpc_call *call);
static void afscm_error(struct rxrpc_call *call);
static void afscm_aemap(struct rxrpc_call *call);

static void _SRXAFSCM_CallBack(struct rxrpc_call *call);
static void _SRXAFSCM_InitCallBackState(struct rxrpc_call *call);
static void _SRXAFSCM_Probe(struct rxrpc_call *call);

typedef void (*_SRXAFSCM_xxxx_t)(struct rxrpc_call *call);

static const struct rxrpc_operation AFSCM_ops[] = {
	{
		.id	= 204,
		.asize	= RXRPC_APP_MARK_EOF,
		.name	= "CallBack",
		.user	= _SRXAFSCM_CallBack,
	},
	{
		.id	= 205,
		.asize	= RXRPC_APP_MARK_EOF,
		.name	= "InitCallBackState",
		.user	= _SRXAFSCM_InitCallBackState,
	},
	{
		.id	= 206,
		.asize	= RXRPC_APP_MARK_EOF,
		.name	= "Probe",
		.user	= _SRXAFSCM_Probe,
	},
#if 0
	{
		.id	= 207,
		.asize	= RXRPC_APP_MARK_EOF,
		.name	= "GetLock",
		.user	= _SRXAFSCM_GetLock,
	},
	{
		.id	= 208,
		.asize	= RXRPC_APP_MARK_EOF,
		.name	= "GetCE",
		.user	= _SRXAFSCM_GetCE,
	},
	{
		.id	= 209,
		.asize	= RXRPC_APP_MARK_EOF,
		.name	= "GetXStatsVersion",
		.user	= _SRXAFSCM_GetXStatsVersion,
	},
	{
		.id	= 210,
		.asize	= RXRPC_APP_MARK_EOF,
		.name	= "GetXStats",
		.user	= _SRXAFSCM_GetXStats,
	}
#endif
};

static struct rxrpc_service AFSCM_service = {
	.name		= "AFS/CM",
	.owner		= THIS_MODULE,
	.link		= LIST_HEAD_INIT(AFSCM_service.link),
	.new_call	= afscm_new_call,
	.service_id	= 1,
	.attn_func	= afscm_attention,
	.error_func	= afscm_error,
	.aemap_func	= afscm_aemap,
	.ops_begin	= &AFSCM_ops[0],
	.ops_end	= &AFSCM_ops[sizeof(AFSCM_ops) / sizeof(AFSCM_ops[0])],
};

static DECLARE_COMPLETION(kafscmd_alive);
static DECLARE_COMPLETION(kafscmd_dead);
static DECLARE_WAIT_QUEUE_HEAD(kafscmd_sleepq);
static LIST_HEAD(kafscmd_attention_list);
static LIST_HEAD(afscm_calls);
static DEFINE_SPINLOCK(afscm_calls_lock);
static DEFINE_SPINLOCK(kafscmd_attention_lock);
static int kafscmd_die;

/*****************************************************************************/
/*
 * AFS Cache Manager kernel thread
 */
static int kafscmd(void *arg)
{
	DECLARE_WAITQUEUE(myself, current);

	struct rxrpc_call *call;
	_SRXAFSCM_xxxx_t func;
	int die;

	printk(KERN_INFO "kAFS: Started kafscmd %d\n", current->pid);

	daemonize("kafscmd");

	complete(&kafscmd_alive);

	/* loop around looking for things to attend to */
	do {
		if (list_empty(&kafscmd_attention_list)) {
			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&kafscmd_sleepq, &myself);

			for (;;) {
				set_current_state(TASK_INTERRUPTIBLE);
				if (!list_empty(&kafscmd_attention_list) ||
				    signal_pending(current) ||
				    kafscmd_die)
					break;

				schedule();
			}

			remove_wait_queue(&kafscmd_sleepq, &myself);
			set_current_state(TASK_RUNNING);
		}

		die = kafscmd_die;

		/* dequeue the next call requiring attention */
		call = NULL;
		spin_lock(&kafscmd_attention_lock);

		if (!list_empty(&kafscmd_attention_list)) {
			call = list_entry(kafscmd_attention_list.next,
					  struct rxrpc_call,
					  app_attn_link);
			list_del_init(&call->app_attn_link);
			die = 0;
		}

		spin_unlock(&kafscmd_attention_lock);

		if (call) {
			/* act upon it */
			_debug("@@@ Begin Attend Call %p", call);

			func = call->app_user;
			if (func)
				func(call);

			rxrpc_put_call(call);

			_debug("@@@ End Attend Call %p", call);
		}

	} while(!die);

	/* and that's all */
	complete_and_exit(&kafscmd_dead, 0);

} /* end kafscmd() */

/*****************************************************************************/
/*
 * handle a call coming in to the cache manager
 * - if I want to keep the call, I must increment its usage count
 * - the return value will be negated and passed back in an abort packet if
 *   non-zero
 * - serialised by virtue of there only being one krxiod
 */
static int afscm_new_call(struct rxrpc_call *call)
{
	_enter("%p{cid=%u u=%d}",
	       call, ntohl(call->call_id), atomic_read(&call->usage));

	rxrpc_get_call(call);

	/* add to my current call list */
	spin_lock(&afscm_calls_lock);
	list_add(&call->app_link,&afscm_calls);
	spin_unlock(&afscm_calls_lock);

	_leave(" = 0");
	return 0;

} /* end afscm_new_call() */

/*****************************************************************************/
/*
 * queue on the kafscmd queue for attention
 */
static void afscm_attention(struct rxrpc_call *call)
{
	_enter("%p{cid=%u u=%d}",
	       call, ntohl(call->call_id), atomic_read(&call->usage));

	spin_lock(&kafscmd_attention_lock);

	if (list_empty(&call->app_attn_link)) {
		list_add_tail(&call->app_attn_link, &kafscmd_attention_list);
		rxrpc_get_call(call);
	}

	spin_unlock(&kafscmd_attention_lock);

	wake_up(&kafscmd_sleepq);

	_leave(" {u=%d}", atomic_read(&call->usage));
} /* end afscm_attention() */

/*****************************************************************************/
/*
 * handle my call being aborted
 * - clean up, dequeue and put my ref to the call
 */
static void afscm_error(struct rxrpc_call *call)
{
	int removed;

	_enter("%p{est=%s ac=%u er=%d}",
	       call,
	       rxrpc_call_error_states[call->app_err_state],
	       call->app_abort_code,
	       call->app_errno);

	spin_lock(&kafscmd_attention_lock);

	if (list_empty(&call->app_attn_link)) {
		list_add_tail(&call->app_attn_link, &kafscmd_attention_list);
		rxrpc_get_call(call);
	}

	spin_unlock(&kafscmd_attention_lock);

	removed = 0;
	spin_lock(&afscm_calls_lock);
	if (!list_empty(&call->app_link)) {
		list_del_init(&call->app_link);
		removed = 1;
	}
	spin_unlock(&afscm_calls_lock);

	if (removed)
		rxrpc_put_call(call);

	wake_up(&kafscmd_sleepq);

	_leave("");
} /* end afscm_error() */

/*****************************************************************************/
/*
 * map afs abort codes to/from Linux error codes
 * - called with call->lock held
 */
static void afscm_aemap(struct rxrpc_call *call)
{
	switch (call->app_err_state) {
	case RXRPC_ESTATE_LOCAL_ABORT:
		call->app_abort_code = -call->app_errno;
		break;
	case RXRPC_ESTATE_PEER_ABORT:
		call->app_errno = -ECONNABORTED;
		break;
	default:
		break;
	}
} /* end afscm_aemap() */

/*****************************************************************************/
/*
 * start the cache manager service if not already started
 */
int afscm_start(void)
{
	int ret;

	down_write(&afscm_sem);
	if (!afscm_usage) {
		ret = kernel_thread(kafscmd, NULL, 0);
		if (ret < 0)
			goto out;

		wait_for_completion(&kafscmd_alive);

		ret = rxrpc_add_service(afs_transport, &AFSCM_service);
		if (ret < 0)
			goto kill;

		afs_kafstimod_add_timer(&afs_mntpt_expiry_timer,
					afs_mntpt_expiry_timeout * HZ);
	}

	afscm_usage++;
	up_write(&afscm_sem);

	return 0;

 kill:
	kafscmd_die = 1;
	wake_up(&kafscmd_sleepq);
	wait_for_completion(&kafscmd_dead);

 out:
	up_write(&afscm_sem);
	return ret;

} /* end afscm_start() */

/*****************************************************************************/
/*
 * stop the cache manager service
 */
void afscm_stop(void)
{
	struct rxrpc_call *call;

	down_write(&afscm_sem);

	BUG_ON(afscm_usage == 0);
	afscm_usage--;

	if (afscm_usage == 0) {
		/* don't want more incoming calls */
		rxrpc_del_service(afs_transport, &AFSCM_service);

		/* abort any calls I've still got open (the afscm_error() will
		 * dequeue them) */
		spin_lock(&afscm_calls_lock);
		while (!list_empty(&afscm_calls)) {
			call = list_entry(afscm_calls.next,
					  struct rxrpc_call,
					  app_link);

			list_del_init(&call->app_link);
			rxrpc_get_call(call);
			spin_unlock(&afscm_calls_lock);

			rxrpc_call_abort(call, -ESRCH); /* abort, dequeue and
							 * put */

			_debug("nuking active call %08x.%d",
			       ntohl(call->conn->conn_id),
			       ntohl(call->call_id));
			rxrpc_put_call(call);
			rxrpc_put_call(call);

			spin_lock(&afscm_calls_lock);
		}
		spin_unlock(&afscm_calls_lock);

		/* get rid of my daemon */
		kafscmd_die = 1;
		wake_up(&kafscmd_sleepq);
		wait_for_completion(&kafscmd_dead);

		/* dispose of any calls waiting for attention */
		spin_lock(&kafscmd_attention_lock);
		while (!list_empty(&kafscmd_attention_list)) {
			call = list_entry(kafscmd_attention_list.next,
					  struct rxrpc_call,
					  app_attn_link);

			list_del_init(&call->app_attn_link);
			spin_unlock(&kafscmd_attention_lock);

			rxrpc_put_call(call);

			spin_lock(&kafscmd_attention_lock);
		}
		spin_unlock(&kafscmd_attention_lock);

		afs_kafstimod_del_timer(&afs_mntpt_expiry_timer);
	}

	up_write(&afscm_sem);

} /* end afscm_stop() */

/*****************************************************************************/
/*
 * handle the fileserver breaking a set of callbacks
 */
static void _SRXAFSCM_CallBack(struct rxrpc_call *call)
{
	struct afs_server *server;
	size_t count, qty, tmp;
	int ret = 0, removed;

	_enter("%p{acs=%s}", call, rxrpc_call_states[call->app_call_state]);

	server = afs_server_get_from_peer(call->conn->peer);

	switch (call->app_call_state) {
		/* we've received the last packet
		 * - drain all the data from the call and send the reply
		 */
	case RXRPC_CSTATE_SRVR_GOT_ARGS:
		ret = -EBADMSG;
		qty = call->app_ready_qty;
		if (qty < 8 || qty > 50 * (6 * 4) + 8)
			break;

		{
			struct afs_callback *cb, *pcb;
			int loop;
			__be32 *fp, *bp;

			fp = rxrpc_call_alloc_scratch(call, qty);

			/* drag the entire argument block out to the scratch
			 * space */
			ret = rxrpc_call_read_data(call, fp, qty, 0);
			if (ret < 0)
				break;

			/* and unmarshall the parameter block */
			ret = -EBADMSG;
			count = ntohl(*fp++);
			if (count>AFSCBMAX ||
			    (count * (3 * 4) + 8 != qty &&
			     count * (6 * 4) + 8 != qty))
				break;

			bp = fp + count*3;
			tmp = ntohl(*bp++);
			if (tmp > 0 && tmp != count)
				break;
			if (tmp == 0)
				bp = NULL;

			pcb = cb = rxrpc_call_alloc_scratch_s(
				call, struct afs_callback);

			for (loop = count - 1; loop >= 0; loop--) {
				pcb->fid.vid	= ntohl(*fp++);
				pcb->fid.vnode	= ntohl(*fp++);
				pcb->fid.unique	= ntohl(*fp++);
				if (bp) {
					pcb->version	= ntohl(*bp++);
					pcb->expiry	= ntohl(*bp++);
					pcb->type	= ntohl(*bp++);
				}
				else {
					pcb->version	= 0;
					pcb->expiry	= 0;
					pcb->type	= AFSCM_CB_UNTYPED;
				}
				pcb++;
			}

			/* invoke the actual service routine */
			ret = SRXAFSCM_CallBack(server, count, cb);
			if (ret < 0)
				break;
		}

		/* send the reply */
		ret = rxrpc_call_write_data(call, 0, NULL, RXRPC_LAST_PACKET,
					    GFP_KERNEL, 0, &count);
		if (ret < 0)
			break;
		break;

		/* operation complete */
	case RXRPC_CSTATE_COMPLETE:
		call->app_user = NULL;
		removed = 0;
		spin_lock(&afscm_calls_lock);
		if (!list_empty(&call->app_link)) {
			list_del_init(&call->app_link);
			removed = 1;
		}
		spin_unlock(&afscm_calls_lock);

		if (removed)
			rxrpc_put_call(call);
		break;

		/* operation terminated on error */
	case RXRPC_CSTATE_ERROR:
		call->app_user = NULL;
		break;

	default:
		break;
	}

	if (ret < 0)
		rxrpc_call_abort(call, ret);

	afs_put_server(server);

	_leave(" = %d", ret);

} /* end _SRXAFSCM_CallBack() */

/*****************************************************************************/
/*
 * handle the fileserver asking us to initialise our callback state
 */
static void _SRXAFSCM_InitCallBackState(struct rxrpc_call *call)
{
	struct afs_server *server;
	size_t count;
	int ret = 0, removed;

	_enter("%p{acs=%s}", call, rxrpc_call_states[call->app_call_state]);

	server = afs_server_get_from_peer(call->conn->peer);

	switch (call->app_call_state) {
		/* we've received the last packet - drain all the data from the
		 * call */
	case RXRPC_CSTATE_SRVR_GOT_ARGS:
		/* shouldn't be any args */
		ret = -EBADMSG;
		break;

		/* send the reply when asked for it */
	case RXRPC_CSTATE_SRVR_SND_REPLY:
		/* invoke the actual service routine */
		ret = SRXAFSCM_InitCallBackState(server);
		if (ret < 0)
			break;

		ret = rxrpc_call_write_data(call, 0, NULL, RXRPC_LAST_PACKET,
					    GFP_KERNEL, 0, &count);
		if (ret < 0)
			break;
		break;

		/* operation complete */
	case RXRPC_CSTATE_COMPLETE:
		call->app_user = NULL;
		removed = 0;
		spin_lock(&afscm_calls_lock);
		if (!list_empty(&call->app_link)) {
			list_del_init(&call->app_link);
			removed = 1;
		}
		spin_unlock(&afscm_calls_lock);

		if (removed)
			rxrpc_put_call(call);
		break;

		/* operation terminated on error */
	case RXRPC_CSTATE_ERROR:
		call->app_user = NULL;
		break;

	default:
		break;
	}

	if (ret < 0)
		rxrpc_call_abort(call, ret);

	afs_put_server(server);

	_leave(" = %d", ret);

} /* end _SRXAFSCM_InitCallBackState() */

/*****************************************************************************/
/*
 * handle a probe from a fileserver
 */
static void _SRXAFSCM_Probe(struct rxrpc_call *call)
{
	struct afs_server *server;
	size_t count;
	int ret = 0, removed;

	_enter("%p{acs=%s}", call, rxrpc_call_states[call->app_call_state]);

	server = afs_server_get_from_peer(call->conn->peer);

	switch (call->app_call_state) {
		/* we've received the last packet - drain all the data from the
		 * call */
	case RXRPC_CSTATE_SRVR_GOT_ARGS:
		/* shouldn't be any args */
		ret = -EBADMSG;
		break;

		/* send the reply when asked for it */
	case RXRPC_CSTATE_SRVR_SND_REPLY:
		/* invoke the actual service routine */
		ret = SRXAFSCM_Probe(server);
		if (ret < 0)
			break;

		ret = rxrpc_call_write_data(call, 0, NULL, RXRPC_LAST_PACKET,
					    GFP_KERNEL, 0, &count);
		if (ret < 0)
			break;
		break;

		/* operation complete */
	case RXRPC_CSTATE_COMPLETE:
		call->app_user = NULL;
		removed = 0;
		spin_lock(&afscm_calls_lock);
		if (!list_empty(&call->app_link)) {
			list_del_init(&call->app_link);
			removed = 1;
		}
		spin_unlock(&afscm_calls_lock);

		if (removed)
			rxrpc_put_call(call);
		break;

		/* operation terminated on error */
	case RXRPC_CSTATE_ERROR:
		call->app_user = NULL;
		break;

	default:
		break;
	}

	if (ret < 0)
		rxrpc_call_abort(call, ret);

	afs_put_server(server);

	_leave(" = %d", ret);

} /* end _SRXAFSCM_Probe() */
