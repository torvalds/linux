/*
 * Copyright (c) 2010 Voltaire Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/export.h>
#include <net/netlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <rdma/rdma_netlink.h>
#include "core_priv.h"

#include "core_priv.h"

static DEFINE_MUTEX(rdma_nl_mutex);
static struct sock *nls;
static struct {
	const struct ibnl_client_cbs   *cb_table;
} rdma_nl_types[RDMA_NL_NUM_CLIENTS];

int ibnl_chk_listeners(unsigned int group)
{
	if (netlink_has_listeners(nls, group) == 0)
		return -1;
	return 0;
}

static bool is_nl_msg_valid(unsigned int type, unsigned int op)
{
	static const unsigned int max_num_ops[RDMA_NL_NUM_CLIENTS - 1] = {
				  RDMA_NL_RDMA_CM_NUM_OPS,
				  RDMA_NL_IWPM_NUM_OPS,
				  0,
				  RDMA_NL_LS_NUM_OPS,
				  0 };

	/*
	 * This BUILD_BUG_ON is intended to catch addition of new
	 * RDMA netlink protocol without updating the array above.
	 */
	BUILD_BUG_ON(RDMA_NL_NUM_CLIENTS != 6);

	if (type > RDMA_NL_NUM_CLIENTS - 1)
		return false;

	return (op < max_num_ops[type - 1]) ? true : false;
}

static bool is_nl_valid(unsigned int type, unsigned int op)
{
	if (!is_nl_msg_valid(type, op) ||
	    !rdma_nl_types[type].cb_table ||
	    !rdma_nl_types[type].cb_table[op].dump)
		return false;
	return true;
}

void rdma_nl_register(unsigned int index,
		      const struct ibnl_client_cbs cb_table[])
{
	mutex_lock(&rdma_nl_mutex);
	if (!is_nl_msg_valid(index, 0)) {
		/*
		 * All clients are not interesting in success/failure of
		 * this call. They want to see the print to error log and
		 * continue their initialization. Print warning for them,
		 * because it is programmer's error to be here.
		 */
		mutex_unlock(&rdma_nl_mutex);
		WARN(true,
		     "The not-valid %u index was supplied to RDMA netlink\n",
		     index);
		return;
	}

	if (rdma_nl_types[index].cb_table) {
		mutex_unlock(&rdma_nl_mutex);
		WARN(true,
		     "The %u index is already registered in RDMA netlink\n",
		     index);
		return;
	}

	rdma_nl_types[index].cb_table = cb_table;
	mutex_unlock(&rdma_nl_mutex);
}
EXPORT_SYMBOL(rdma_nl_register);

void rdma_nl_unregister(unsigned int index)
{
	mutex_lock(&rdma_nl_mutex);
	rdma_nl_types[index].cb_table = NULL;
	mutex_unlock(&rdma_nl_mutex);
}
EXPORT_SYMBOL(rdma_nl_unregister);

void *ibnl_put_msg(struct sk_buff *skb, struct nlmsghdr **nlh, int seq,
		   int len, int client, int op, int flags)
{
	unsigned char *prev_tail;

	prev_tail = skb_tail_pointer(skb);
	*nlh = nlmsg_put(skb, 0, seq, RDMA_NL_GET_TYPE(client, op),
			 len, flags);
	if (!*nlh)
		goto out_nlmsg_trim;
	(*nlh)->nlmsg_len = skb_tail_pointer(skb) - prev_tail;
	return nlmsg_data(*nlh);

out_nlmsg_trim:
	nlmsg_trim(skb, prev_tail);
	return NULL;
}
EXPORT_SYMBOL(ibnl_put_msg);

int ibnl_put_attr(struct sk_buff *skb, struct nlmsghdr *nlh,
		  int len, void *data, int type)
{
	unsigned char *prev_tail;

	prev_tail = skb_tail_pointer(skb);
	if (nla_put(skb, type, len, data))
		goto nla_put_failure;
	nlh->nlmsg_len += skb_tail_pointer(skb) - prev_tail;
	return 0;

nla_put_failure:
	nlmsg_trim(skb, prev_tail - nlh->nlmsg_len);
	return -EMSGSIZE;
}
EXPORT_SYMBOL(ibnl_put_attr);

static int rdma_nl_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh,
			   struct netlink_ext_ack *extack)
{
	int type = nlh->nlmsg_type;
	unsigned int index = RDMA_NL_GET_CLIENT(type);
	unsigned int op = RDMA_NL_GET_OP(type);
	struct netlink_callback cb = {};
	struct netlink_dump_control c = {};

	if (!is_nl_valid(index, op))
		return -EINVAL;

	if ((rdma_nl_types[index].cb_table[op].flags & RDMA_NL_ADMIN_PERM) &&
	    !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	/*
	 * For response or local service set_timeout request,
	 * there is no need to use netlink_dump_start.
	 */
	if (!(nlh->nlmsg_flags & NLM_F_REQUEST) ||
	    (index == RDMA_NL_LS && op == RDMA_NL_LS_OP_SET_TIMEOUT)) {
		cb.skb = skb;
		cb.nlh = nlh;
		cb.dump = rdma_nl_types[index].cb_table[op].dump;
		return cb.dump(skb, &cb);
	}

	c.dump = rdma_nl_types[index].cb_table[op].dump;
	return netlink_dump_start(nls, skb, nlh, &c);
}

/*
 * This function is similar to netlink_rcv_skb with one exception:
 * It calls to the callback for the netlink messages without NLM_F_REQUEST
 * flag. These messages are intended for RDMA_NL_LS consumer, so it is allowed
 * for that consumer only.
 */
static int rdma_nl_rcv_skb(struct sk_buff *skb, int (*cb)(struct sk_buff *,
						   struct nlmsghdr *,
						   struct netlink_ext_ack *))
{
	struct netlink_ext_ack extack = {};
	struct nlmsghdr *nlh;
	int err;

	while (skb->len >= nlmsg_total_size(0)) {
		int msglen;

		nlh = nlmsg_hdr(skb);
		err = 0;

		if (nlh->nlmsg_len < NLMSG_HDRLEN || skb->len < nlh->nlmsg_len)
			return 0;

		/*
		 * Generally speaking, the only requests are handled
		 * by the kernel, but RDMA_NL_LS is different, because it
		 * runs backward netlink scheme. Kernel initiates messages
		 * and waits for reply with data to keep pathrecord cache
		 * in sync.
		 */
		if (!(nlh->nlmsg_flags & NLM_F_REQUEST) &&
		    (RDMA_NL_GET_CLIENT(nlh->nlmsg_type) != RDMA_NL_LS))
			goto ack;

		/* Skip control messages */
		if (nlh->nlmsg_type < NLMSG_MIN_TYPE)
			goto ack;

		err = cb(skb, nlh, &extack);
		if (err == -EINTR)
			goto skip;

ack:
		if (nlh->nlmsg_flags & NLM_F_ACK || err)
			netlink_ack(skb, nlh, err, &extack);

skip:
		msglen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (msglen > skb->len)
			msglen = skb->len;
		skb_pull(skb, msglen);
	}

	return 0;
}

static void rdma_nl_rcv(struct sk_buff *skb)
{
	mutex_lock(&rdma_nl_mutex);
	rdma_nl_rcv_skb(skb, &rdma_nl_rcv_msg);
	mutex_unlock(&rdma_nl_mutex);
}

int ibnl_unicast(struct sk_buff *skb, struct nlmsghdr *nlh,
			__u32 pid)
{
	int err;

	err = netlink_unicast(nls, skb, pid, MSG_DONTWAIT);
	return (err < 0) ? err : 0;
}
EXPORT_SYMBOL(ibnl_unicast);

int ibnl_unicast_wait(struct sk_buff *skb, struct nlmsghdr *nlh,
		      __u32 pid)
{
	int err;

	err = netlink_unicast(nls, skb, pid, 0);
	return (err < 0) ? err : 0;
}
EXPORT_SYMBOL(ibnl_unicast_wait);

int ibnl_multicast(struct sk_buff *skb, struct nlmsghdr *nlh,
			unsigned int group, gfp_t flags)
{
	return nlmsg_multicast(nls, skb, 0, group, flags);
}
EXPORT_SYMBOL(ibnl_multicast);

int __init rdma_nl_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input	= rdma_nl_rcv,
	};

	nls = netlink_kernel_create(&init_net, NETLINK_RDMA, &cfg);
	if (!nls)
		return -ENOMEM;

	nls->sk_sndtimeo = 10 * HZ;
	return 0;
}

void rdma_nl_exit(void)
{
	int idx;

	for (idx = 0; idx < RDMA_NL_NUM_CLIENTS; idx++)
		rdma_nl_unregister(idx);

	netlink_kernel_release(nls);
}
