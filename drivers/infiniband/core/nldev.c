/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <net/netlink.h>
#include <rdma/rdma_netlink.h>

#include "core_priv.h"

static const struct nla_policy nldev_policy[RDMA_NLDEV_ATTR_MAX] = {
	[RDMA_NLDEV_ATTR_DEV_INDEX]     = { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_DEV_NAME]	= { .type = NLA_NUL_STRING,
					    .len = IB_DEVICE_NAME_MAX - 1},
	[RDMA_NLDEV_ATTR_PORT_INDEX]	= { .type = NLA_U32 },
};

static int fill_dev_info(struct sk_buff *msg, struct ib_device *device)
{
	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_DEV_INDEX, device->index))
		return -EMSGSIZE;
	if (nla_put_string(msg, RDMA_NLDEV_ATTR_DEV_NAME, device->name))
		return -EMSGSIZE;
	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_PORT_INDEX, rdma_end_port(device)))
		return -EMSGSIZE;
	return 0;
}

static int _nldev_get_dumpit(struct ib_device *device,
			     struct sk_buff *skb,
			     struct netlink_callback *cb,
			     unsigned int idx)
{
	int start = cb->args[0];
	struct nlmsghdr *nlh;

	if (idx < start)
		return 0;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_GET),
			0, NLM_F_MULTI);

	if (fill_dev_info(skb, device)) {
		nlmsg_cancel(skb, nlh);
		goto out;
	}

	nlmsg_end(skb, nlh);

	idx++;

out:	cb->args[0] = idx;
	return skb->len;
}

static int nldev_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	/*
	 * There is no need to take lock, because
	 * we are relying on ib_core's lists_rwsem
	 */
	return ib_enum_all_devs(_nldev_get_dumpit, skb, cb);
}

static const struct rdma_nl_cbs nldev_cb_table[] = {
	[RDMA_NLDEV_CMD_GET] = {
		.dump = nldev_get_dumpit,
	},
};

void __init nldev_init(void)
{
	rdma_nl_register(RDMA_NL_NLDEV, nldev_cb_table);
}

void __exit nldev_exit(void)
{
	rdma_nl_unregister(RDMA_NL_NLDEV);
}
