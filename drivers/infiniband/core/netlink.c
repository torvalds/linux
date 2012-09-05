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

struct ibnl_client {
	struct list_head		list;
	int				index;
	int				nops;
	const struct ibnl_client_cbs   *cb_table;
};

static DEFINE_MUTEX(ibnl_mutex);
static struct sock *nls;
static LIST_HEAD(client_list);

int ibnl_add_client(int index, int nops,
		    const struct ibnl_client_cbs cb_table[])
{
	struct ibnl_client *cur;
	struct ibnl_client *nl_client;

	nl_client = kmalloc(sizeof *nl_client, GFP_KERNEL);
	if (!nl_client)
		return -ENOMEM;

	nl_client->index	= index;
	nl_client->nops		= nops;
	nl_client->cb_table	= cb_table;

	mutex_lock(&ibnl_mutex);

	list_for_each_entry(cur, &client_list, list) {
		if (cur->index == index) {
			pr_warn("Client for %d already exists\n", index);
			mutex_unlock(&ibnl_mutex);
			kfree(nl_client);
			return -EINVAL;
		}
	}

	list_add_tail(&nl_client->list, &client_list);

	mutex_unlock(&ibnl_mutex);

	return 0;
}
EXPORT_SYMBOL(ibnl_add_client);

int ibnl_remove_client(int index)
{
	struct ibnl_client *cur, *next;

	mutex_lock(&ibnl_mutex);
	list_for_each_entry_safe(cur, next, &client_list, list) {
		if (cur->index == index) {
			list_del(&(cur->list));
			mutex_unlock(&ibnl_mutex);
			kfree(cur);
			return 0;
		}
	}
	pr_warn("Can't remove callback for client idx %d. Not found\n", index);
	mutex_unlock(&ibnl_mutex);

	return -EINVAL;
}
EXPORT_SYMBOL(ibnl_remove_client);

void *ibnl_put_msg(struct sk_buff *skb, struct nlmsghdr **nlh, int seq,
		   int len, int client, int op)
{
	unsigned char *prev_tail;

	prev_tail = skb_tail_pointer(skb);
	*nlh = nlmsg_put(skb, 0, seq, RDMA_NL_GET_TYPE(client, op),
			 len, NLM_F_MULTI);
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

static int ibnl_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct ibnl_client *client;
	int type = nlh->nlmsg_type;
	int index = RDMA_NL_GET_CLIENT(type);
	int op = RDMA_NL_GET_OP(type);

	list_for_each_entry(client, &client_list, list) {
		if (client->index == index) {
			if (op < 0 || op >= client->nops ||
			    !client->cb_table[RDMA_NL_GET_OP(op)].dump)
				return -EINVAL;

			{
				struct netlink_dump_control c = {
					.dump = client->cb_table[op].dump,
				};
				return netlink_dump_start(nls, skb, nlh, &c);
			}
		}
	}

	pr_info("Index %d wasn't found in client list\n", index);
	return -EINVAL;
}

static void ibnl_rcv(struct sk_buff *skb)
{
	mutex_lock(&ibnl_mutex);
	netlink_rcv_skb(skb, &ibnl_rcv_msg);
	mutex_unlock(&ibnl_mutex);
}

int __init ibnl_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input	= ibnl_rcv,
	};

	nls = netlink_kernel_create(&init_net, NETLINK_RDMA, THIS_MODULE, &cfg);
	if (!nls) {
		pr_warn("Failed to create netlink socket\n");
		return -ENOMEM;
	}

	return 0;
}

void ibnl_cleanup(void)
{
	struct ibnl_client *cur, *next;

	mutex_lock(&ibnl_mutex);
	list_for_each_entry_safe(cur, next, &client_list, list) {
		list_del(&(cur->list));
		kfree(cur);
	}
	mutex_unlock(&ibnl_mutex);

	netlink_kernel_release(nls);
}
