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

#include <linux/module.h>
#include <net/netlink.h>
#include <rdma/rdma_netlink.h>

#include "core_priv.h"

static const struct nla_policy nldev_policy[RDMA_NLDEV_ATTR_MAX] = {
	[RDMA_NLDEV_ATTR_DEV_INDEX]     = { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_DEV_NAME]	= { .type = NLA_NUL_STRING,
					    .len = IB_DEVICE_NAME_MAX - 1},
	[RDMA_NLDEV_ATTR_PORT_INDEX]	= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_FW_VERSION]	= { .type = NLA_NUL_STRING,
					    .len = IB_FW_VERSION_NAME_MAX - 1},
	[RDMA_NLDEV_ATTR_NODE_GUID]	= { .type = NLA_U64 },
	[RDMA_NLDEV_ATTR_SYS_IMAGE_GUID] = { .type = NLA_U64 },
	[RDMA_NLDEV_ATTR_SUBNET_PREFIX]	= { .type = NLA_U64 },
	[RDMA_NLDEV_ATTR_LID]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_SM_LID]	= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_LMC]		= { .type = NLA_U8 },
	[RDMA_NLDEV_ATTR_PORT_STATE]	= { .type = NLA_U8 },
	[RDMA_NLDEV_ATTR_PORT_PHYS_STATE] = { .type = NLA_U8 },
	[RDMA_NLDEV_ATTR_DEV_NODE_TYPE] = { .type = NLA_U8 },
};

static int fill_dev_info(struct sk_buff *msg, struct ib_device *device)
{
	char fw[IB_FW_VERSION_NAME_MAX];

	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_DEV_INDEX, device->index))
		return -EMSGSIZE;
	if (nla_put_string(msg, RDMA_NLDEV_ATTR_DEV_NAME, device->name))
		return -EMSGSIZE;
	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_PORT_INDEX, rdma_end_port(device)))
		return -EMSGSIZE;

	BUILD_BUG_ON(sizeof(device->attrs.device_cap_flags) != sizeof(u64));
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_CAP_FLAGS,
			      device->attrs.device_cap_flags, 0))
		return -EMSGSIZE;

	ib_get_device_fw_str(device, fw);
	/* Device without FW has strlen(fw) */
	if (strlen(fw) && nla_put_string(msg, RDMA_NLDEV_ATTR_FW_VERSION, fw))
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_NODE_GUID,
			      be64_to_cpu(device->node_guid), 0))
		return -EMSGSIZE;
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_SYS_IMAGE_GUID,
			      be64_to_cpu(device->attrs.sys_image_guid), 0))
		return -EMSGSIZE;
	if (nla_put_u8(msg, RDMA_NLDEV_ATTR_DEV_NODE_TYPE, device->node_type))
		return -EMSGSIZE;
	return 0;
}

static int fill_port_info(struct sk_buff *msg,
			  struct ib_device *device, u32 port)
{
	struct ib_port_attr attr;
	int ret;

	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_DEV_INDEX, device->index))
		return -EMSGSIZE;
	if (nla_put_string(msg, RDMA_NLDEV_ATTR_DEV_NAME, device->name))
		return -EMSGSIZE;
	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_PORT_INDEX, port))
		return -EMSGSIZE;

	ret = ib_query_port(device, port, &attr);
	if (ret)
		return ret;

	BUILD_BUG_ON(sizeof(attr.port_cap_flags) > sizeof(u64));
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_CAP_FLAGS,
			      (u64)attr.port_cap_flags, 0))
		return -EMSGSIZE;
	if (rdma_protocol_ib(device, port) &&
	    nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_SUBNET_PREFIX,
			      attr.subnet_prefix, 0))
		return -EMSGSIZE;
	if (rdma_protocol_ib(device, port)) {
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_LID, attr.lid))
			return -EMSGSIZE;
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_SM_LID, attr.sm_lid))
			return -EMSGSIZE;
		if (nla_put_u8(msg, RDMA_NLDEV_ATTR_LMC, attr.lmc))
			return -EMSGSIZE;
	}
	if (nla_put_u8(msg, RDMA_NLDEV_ATTR_PORT_STATE, attr.state))
		return -EMSGSIZE;
	if (nla_put_u8(msg, RDMA_NLDEV_ATTR_PORT_PHYS_STATE, attr.phys_state))
		return -EMSGSIZE;
	return 0;
}

static int nldev_get_doit(struct sk_buff *skb, struct nlmsghdr *nlh,
			  struct netlink_ext_ack *extack)
{
	struct nlattr *tb[RDMA_NLDEV_ATTR_MAX];
	struct ib_device *device;
	struct sk_buff *msg;
	u32 index;
	int err;

	err = nlmsg_parse(nlh, 0, tb, RDMA_NLDEV_ATTR_MAX - 1,
			  nldev_policy, extack);
	if (err || !tb[RDMA_NLDEV_ATTR_DEV_INDEX])
		return -EINVAL;

	index = nla_get_u32(tb[RDMA_NLDEV_ATTR_DEV_INDEX]);

	device = __ib_device_get_by_index(index);
	if (!device)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	nlh = nlmsg_put(msg, NETLINK_CB(skb).portid, nlh->nlmsg_seq,
			RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_GET),
			0, 0);

	err = fill_dev_info(msg, device);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	nlmsg_end(msg, nlh);

	return rdma_nl_unicast(msg, NETLINK_CB(skb).portid);
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

static int nldev_port_get_doit(struct sk_buff *skb, struct nlmsghdr *nlh,
			       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[RDMA_NLDEV_ATTR_MAX];
	struct ib_device *device;
	struct sk_buff *msg;
	u32 index;
	u32 port;
	int err;

	err = nlmsg_parse(nlh, 0, tb, RDMA_NLDEV_ATTR_MAX - 1,
			  nldev_policy, extack);
	if (err ||
	    !tb[RDMA_NLDEV_ATTR_DEV_INDEX] ||
	    !tb[RDMA_NLDEV_ATTR_PORT_INDEX])
		return -EINVAL;

	index = nla_get_u32(tb[RDMA_NLDEV_ATTR_DEV_INDEX]);
	device = __ib_device_get_by_index(index);
	if (!device)
		return -EINVAL;

	port = nla_get_u32(tb[RDMA_NLDEV_ATTR_PORT_INDEX]);
	if (!rdma_is_port_valid(device, port))
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	nlh = nlmsg_put(msg, NETLINK_CB(skb).portid, nlh->nlmsg_seq,
			RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_GET),
			0, 0);

	err = fill_port_info(msg, device, port);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	nlmsg_end(msg, nlh);

	return rdma_nl_unicast(msg, NETLINK_CB(skb).portid);
}

static int nldev_port_get_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	struct nlattr *tb[RDMA_NLDEV_ATTR_MAX];
	struct ib_device *device;
	int start = cb->args[0];
	struct nlmsghdr *nlh;
	u32 idx = 0;
	u32 ifindex;
	int err;
	u32 p;

	err = nlmsg_parse(cb->nlh, 0, tb, RDMA_NLDEV_ATTR_MAX - 1,
			  nldev_policy, NULL);
	if (err || !tb[RDMA_NLDEV_ATTR_DEV_INDEX])
		return -EINVAL;

	ifindex = nla_get_u32(tb[RDMA_NLDEV_ATTR_DEV_INDEX]);
	device = __ib_device_get_by_index(ifindex);
	if (!device)
		return -EINVAL;

	for (p = rdma_start_port(device); p <= rdma_end_port(device); ++p) {
		/*
		 * The dumpit function returns all information from specific
		 * index. This specific index is taken from the netlink
		 * messages request sent by user and it is available
		 * in cb->args[0].
		 *
		 * Usually, the user doesn't fill this field and it causes
		 * to return everything.
		 *
		 */
		if (idx < start) {
			idx++;
			continue;
		}

		nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid,
				cb->nlh->nlmsg_seq,
				RDMA_NL_GET_TYPE(RDMA_NL_NLDEV,
						 RDMA_NLDEV_CMD_PORT_GET),
				0, NLM_F_MULTI);

		if (fill_port_info(skb, device, p)) {
			nlmsg_cancel(skb, nlh);
			goto out;
		}
		idx++;
		nlmsg_end(skb, nlh);
	}

out:	cb->args[0] = idx;
	return skb->len;
}

static const struct rdma_nl_cbs nldev_cb_table[RDMA_NLDEV_NUM_OPS] = {
	[RDMA_NLDEV_CMD_GET] = {
		.doit = nldev_get_doit,
		.dump = nldev_get_dumpit,
	},
	[RDMA_NLDEV_CMD_PORT_GET] = {
		.doit = nldev_port_get_doit,
		.dump = nldev_port_get_dumpit,
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

MODULE_ALIAS_RDMA_NETLINK(RDMA_NL_NLDEV, 5);
