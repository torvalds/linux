/*
 * MobiCore KernelApi module
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/netlink.h>
#include <linux/kthread.h>
#include <net/sock.h>

#include <linux/list.h>

#include "connection.h"
#include "common.h"

#define MC_DAEMON_NETLINK  17

struct mc_kernelapi_ctx {
	struct sock *sk;
	struct list_head peers;
	atomic_t counter;
};

struct mc_kernelapi_ctx *mod_ctx;

/* get a unique ID */
unsigned int mcapi_unique_id(void)
{
	return (unsigned int)atomic_inc_return(&(mod_ctx->counter));
}

static struct connection *mcapi_find_connection(uint32_t seq)
{
	struct connection *tmp;
	struct list_head *pos;

	/* Get session for session_id */
	list_for_each(pos, &mod_ctx->peers) {
		tmp = list_entry(pos, struct connection, list);
		if (tmp->sequence_magic == seq)
			return tmp;
	}

	return NULL;
}

void mcapi_insert_connection(struct connection *connection)
{
	list_add_tail(&(connection->list), &(mod_ctx->peers));
	connection->socket_descriptor = mod_ctx->sk;
}

void mcapi_remove_connection(uint32_t seq)
{
	struct connection *tmp;
	struct list_head *pos, *q;

	/*
	 * Delete all session objects. Usually this should not be needed as
	 * closeDevice() requires that all sessions have been closed before.
	 */
	list_for_each_safe(pos, q, &mod_ctx->peers) {
		tmp = list_entry(pos, struct connection, list);
		if (tmp->sequence_magic == seq) {
			list_del(pos);
			break;
		}
	}
}

static int mcapi_process(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct connection *c;
	int length;
	int seq;
	pid_t pid;
	int ret;

	pid = nlh->nlmsg_pid;
	length = nlh->nlmsg_len;
	seq = nlh->nlmsg_seq;
	MCDRV_DBG_VERBOSE("nlmsg len %d type %d pid 0x%X seq %d\n",
			  length, nlh->nlmsg_type, pid, seq);
	do {
		c = mcapi_find_connection(seq);
		if (!c) {
			MCDRV_ERROR("Invalid incomming connection - seq=%u!",
				    seq);
			ret = -1;
			break;
		}

		/* Pass the buffer to the appropriate connection */
		connection_process(c, skb);

		ret = 0;
	} while (false);
	return ret;
}

static void mcapi_callback(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	int len = skb->len;
	int err = 0;

	while (NLMSG_OK(nlh, len)) {
		err = mcapi_process(skb, nlh);

		/* if err or if this message says it wants a response */
		if (err || (nlh->nlmsg_flags & NLM_F_ACK))
			netlink_ack(skb, nlh, err);

		nlh = NLMSG_NEXT(nlh, len);
	}
}

static int __init mcapi_init(void)
{
#if defined(MC_NETLINK_COMPAT) || defined(MC_NETLINK_COMPAT_V37)
	struct netlink_kernel_cfg cfg = {
		.input  = mcapi_callback,
	};
#endif
	MCDRV_DBG("Mobicore API module initialized!\n");

	mod_ctx = kzalloc(sizeof(struct mc_kernelapi_ctx), GFP_KERNEL);
#ifdef MC_NETLINK_COMPAT_V37
	mod_ctx->sk = netlink_kernel_create(&init_net, MC_DAEMON_NETLINK,
					    &cfg);
#elif defined MC_NETLINK_COMPAT
	mod_ctx->sk = netlink_kernel_create(&init_net, MC_DAEMON_NETLINK,
					    THIS_MODULE, &cfg);
#else
	/* start kernel thread */
	mod_ctx->sk = netlink_kernel_create(&init_net, MC_DAEMON_NETLINK, 0,
					    mcapi_callback, NULL, THIS_MODULE);
#endif

	if (!mod_ctx->sk) {
		MCDRV_ERROR("register of recieve handler failed");
		return -EFAULT;
	}

	INIT_LIST_HEAD(&mod_ctx->peers);
	return 0;
}

static void __exit mcapi_exit(void)
{
	MCDRV_DBG("Unloading Mobicore API module.\n");

	if (mod_ctx->sk != NULL) {
		netlink_kernel_release(mod_ctx->sk);
		mod_ctx->sk = NULL;
	}
	kfree(mod_ctx);
	mod_ctx = NULL;
}

module_init(mcapi_init);
module_exit(mcapi_exit);

MODULE_AUTHOR("Giesecke & Devrient GmbH");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MobiCore API driver");
