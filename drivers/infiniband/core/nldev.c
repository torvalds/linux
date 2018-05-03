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
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <net/netlink.h>
#include <rdma/rdma_cm.h>
#include <rdma/rdma_netlink.h>

#include "core_priv.h"
#include "cma_priv.h"

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
	[RDMA_NLDEV_ATTR_RES_SUMMARY]	= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY]	= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY_NAME] = { .type = NLA_NUL_STRING,
					     .len = 16 },
	[RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY_CURR] = { .type = NLA_U64 },
	[RDMA_NLDEV_ATTR_RES_QP]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_QP_ENTRY]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_LQPN]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_RQPN]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_RQ_PSN]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_SQ_PSN]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_PATH_MIG_STATE] = { .type = NLA_U8 },
	[RDMA_NLDEV_ATTR_RES_TYPE]		= { .type = NLA_U8 },
	[RDMA_NLDEV_ATTR_RES_STATE]		= { .type = NLA_U8 },
	[RDMA_NLDEV_ATTR_RES_PID]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_KERN_NAME]		= { .type = NLA_NUL_STRING,
						    .len = TASK_COMM_LEN },
	[RDMA_NLDEV_ATTR_RES_CM_ID]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_CM_ID_ENTRY]	= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_PS]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_SRC_ADDR]	= {
			.len = sizeof(struct __kernel_sockaddr_storage) },
	[RDMA_NLDEV_ATTR_RES_DST_ADDR]	= {
			.len = sizeof(struct __kernel_sockaddr_storage) },
	[RDMA_NLDEV_ATTR_RES_CQ]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_CQ_ENTRY]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_CQE]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_USECNT]		= { .type = NLA_U64 },
	[RDMA_NLDEV_ATTR_RES_POLL_CTX]		= { .type = NLA_U8 },
	[RDMA_NLDEV_ATTR_RES_MR]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_MR_ENTRY]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_RKEY]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_LKEY]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_IOVA]		= { .type = NLA_U64 },
	[RDMA_NLDEV_ATTR_RES_MRLEN]		= { .type = NLA_U64 },
	[RDMA_NLDEV_ATTR_RES_PD]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_PD_ENTRY]		= { .type = NLA_NESTED },
	[RDMA_NLDEV_ATTR_RES_LOCAL_DMA_LKEY]	= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_RES_UNSAFE_GLOBAL_RKEY] = { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_NDEV_INDEX]		= { .type = NLA_U32 },
	[RDMA_NLDEV_ATTR_NDEV_NAME]		= { .type = NLA_NUL_STRING,
						    .len = IFNAMSIZ },
};

static int fill_nldev_handle(struct sk_buff *msg, struct ib_device *device)
{
	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_DEV_INDEX, device->index))
		return -EMSGSIZE;
	if (nla_put_string(msg, RDMA_NLDEV_ATTR_DEV_NAME, device->name))
		return -EMSGSIZE;

	return 0;
}

static int fill_dev_info(struct sk_buff *msg, struct ib_device *device)
{
	char fw[IB_FW_VERSION_NAME_MAX];

	if (fill_nldev_handle(msg, device))
		return -EMSGSIZE;

	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_PORT_INDEX, rdma_end_port(device)))
		return -EMSGSIZE;

	BUILD_BUG_ON(sizeof(device->attrs.device_cap_flags) != sizeof(u64));
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_CAP_FLAGS,
			      device->attrs.device_cap_flags,
			      RDMA_NLDEV_ATTR_PAD))
		return -EMSGSIZE;

	ib_get_device_fw_str(device, fw);
	/* Device without FW has strlen(fw) = 0 */
	if (strlen(fw) && nla_put_string(msg, RDMA_NLDEV_ATTR_FW_VERSION, fw))
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_NODE_GUID,
			      be64_to_cpu(device->node_guid),
			      RDMA_NLDEV_ATTR_PAD))
		return -EMSGSIZE;
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_SYS_IMAGE_GUID,
			      be64_to_cpu(device->attrs.sys_image_guid),
			      RDMA_NLDEV_ATTR_PAD))
		return -EMSGSIZE;
	if (nla_put_u8(msg, RDMA_NLDEV_ATTR_DEV_NODE_TYPE, device->node_type))
		return -EMSGSIZE;
	return 0;
}

static int fill_port_info(struct sk_buff *msg,
			  struct ib_device *device, u32 port,
			  const struct net *net)
{
	struct net_device *netdev = NULL;
	struct ib_port_attr attr;
	int ret;

	if (fill_nldev_handle(msg, device))
		return -EMSGSIZE;

	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_PORT_INDEX, port))
		return -EMSGSIZE;

	ret = ib_query_port(device, port, &attr);
	if (ret)
		return ret;

	BUILD_BUG_ON(sizeof(attr.port_cap_flags) > sizeof(u64));
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_CAP_FLAGS,
			      (u64)attr.port_cap_flags, RDMA_NLDEV_ATTR_PAD))
		return -EMSGSIZE;
	if (rdma_protocol_ib(device, port) &&
	    nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_SUBNET_PREFIX,
			      attr.subnet_prefix, RDMA_NLDEV_ATTR_PAD))
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

	if (device->get_netdev)
		netdev = device->get_netdev(device, port);

	if (netdev && net_eq(dev_net(netdev), net)) {
		ret = nla_put_u32(msg,
				  RDMA_NLDEV_ATTR_NDEV_INDEX, netdev->ifindex);
		if (ret)
			goto out;
		ret = nla_put_string(msg,
				     RDMA_NLDEV_ATTR_NDEV_NAME, netdev->name);
	}

out:
	if (netdev)
		dev_put(netdev);
	return ret;
}

static int fill_res_info_entry(struct sk_buff *msg,
			       const char *name, u64 curr)
{
	struct nlattr *entry_attr;

	entry_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY);
	if (!entry_attr)
		return -EMSGSIZE;

	if (nla_put_string(msg, RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY_NAME, name))
		goto err;
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY_CURR, curr,
			      RDMA_NLDEV_ATTR_PAD))
		goto err;

	nla_nest_end(msg, entry_attr);
	return 0;

err:
	nla_nest_cancel(msg, entry_attr);
	return -EMSGSIZE;
}

static int fill_res_info(struct sk_buff *msg, struct ib_device *device)
{
	static const char * const names[RDMA_RESTRACK_MAX] = {
		[RDMA_RESTRACK_PD] = "pd",
		[RDMA_RESTRACK_CQ] = "cq",
		[RDMA_RESTRACK_QP] = "qp",
		[RDMA_RESTRACK_CM_ID] = "cm_id",
		[RDMA_RESTRACK_MR] = "mr",
	};

	struct rdma_restrack_root *res = &device->res;
	struct nlattr *table_attr;
	int ret, i, curr;

	if (fill_nldev_handle(msg, device))
		return -EMSGSIZE;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_RES_SUMMARY);
	if (!table_attr)
		return -EMSGSIZE;

	for (i = 0; i < RDMA_RESTRACK_MAX; i++) {
		if (!names[i])
			continue;
		curr = rdma_restrack_count(res, i, task_active_pid_ns(current));
		ret = fill_res_info_entry(msg, names[i], curr);
		if (ret)
			goto err;
	}

	nla_nest_end(msg, table_attr);
	return 0;

err:
	nla_nest_cancel(msg, table_attr);
	return ret;
}

static int fill_res_name_pid(struct sk_buff *msg,
			     struct rdma_restrack_entry *res)
{
	/*
	 * For user resources, user is should read /proc/PID/comm to get the
	 * name of the task file.
	 */
	if (rdma_is_kernel_res(res)) {
		if (nla_put_string(msg, RDMA_NLDEV_ATTR_RES_KERN_NAME,
		    res->kern_name))
			return -EMSGSIZE;
	} else {
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_PID,
		    task_pid_vnr(res->task)))
			return -EMSGSIZE;
	}
	return 0;
}

static int fill_res_qp_entry(struct sk_buff *msg, struct netlink_callback *cb,
			     struct rdma_restrack_entry *res, uint32_t port)
{
	struct ib_qp *qp = container_of(res, struct ib_qp, res);
	struct ib_qp_init_attr qp_init_attr;
	struct nlattr *entry_attr;
	struct ib_qp_attr qp_attr;
	int ret;

	ret = ib_query_qp(qp, &qp_attr, 0, &qp_init_attr);
	if (ret)
		return ret;

	if (port && port != qp_attr.port_num)
		return 0;

	entry_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_RES_QP_ENTRY);
	if (!entry_attr)
		goto out;

	/* In create_qp() port is not set yet */
	if (qp_attr.port_num &&
	    nla_put_u32(msg, RDMA_NLDEV_ATTR_PORT_INDEX, qp_attr.port_num))
		goto err;

	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_LQPN, qp->qp_num))
		goto err;
	if (qp->qp_type == IB_QPT_RC || qp->qp_type == IB_QPT_UC) {
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_RQPN,
				qp_attr.dest_qp_num))
			goto err;
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_RQ_PSN,
				qp_attr.rq_psn))
			goto err;
	}

	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_SQ_PSN, qp_attr.sq_psn))
		goto err;

	if (qp->qp_type == IB_QPT_RC || qp->qp_type == IB_QPT_UC ||
	    qp->qp_type == IB_QPT_XRC_INI || qp->qp_type == IB_QPT_XRC_TGT) {
		if (nla_put_u8(msg, RDMA_NLDEV_ATTR_RES_PATH_MIG_STATE,
			       qp_attr.path_mig_state))
			goto err;
	}
	if (nla_put_u8(msg, RDMA_NLDEV_ATTR_RES_TYPE, qp->qp_type))
		goto err;
	if (nla_put_u8(msg, RDMA_NLDEV_ATTR_RES_STATE, qp_attr.qp_state))
		goto err;

	if (fill_res_name_pid(msg, res))
		goto err;

	nla_nest_end(msg, entry_attr);
	return 0;

err:
	nla_nest_cancel(msg, entry_attr);
out:
	return -EMSGSIZE;
}

static int fill_res_cm_id_entry(struct sk_buff *msg,
				struct netlink_callback *cb,
				struct rdma_restrack_entry *res, uint32_t port)
{
	struct rdma_id_private *id_priv =
				container_of(res, struct rdma_id_private, res);
	struct rdma_cm_id *cm_id = &id_priv->id;
	struct nlattr *entry_attr;

	if (port && port != cm_id->port_num)
		return 0;

	entry_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_RES_CM_ID_ENTRY);
	if (!entry_attr)
		goto out;

	if (cm_id->port_num &&
	    nla_put_u32(msg, RDMA_NLDEV_ATTR_PORT_INDEX, cm_id->port_num))
		goto err;

	if (id_priv->qp_num) {
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_LQPN, id_priv->qp_num))
			goto err;
		if (nla_put_u8(msg, RDMA_NLDEV_ATTR_RES_TYPE, cm_id->qp_type))
			goto err;
	}

	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_PS, cm_id->ps))
		goto err;

	if (nla_put_u8(msg, RDMA_NLDEV_ATTR_RES_STATE, id_priv->state))
		goto err;

	if (cm_id->route.addr.src_addr.ss_family &&
	    nla_put(msg, RDMA_NLDEV_ATTR_RES_SRC_ADDR,
		    sizeof(cm_id->route.addr.src_addr),
		    &cm_id->route.addr.src_addr))
		goto err;
	if (cm_id->route.addr.dst_addr.ss_family &&
	    nla_put(msg, RDMA_NLDEV_ATTR_RES_DST_ADDR,
		    sizeof(cm_id->route.addr.dst_addr),
		    &cm_id->route.addr.dst_addr))
		goto err;

	if (fill_res_name_pid(msg, res))
		goto err;

	nla_nest_end(msg, entry_attr);
	return 0;

err:
	nla_nest_cancel(msg, entry_attr);
out:
	return -EMSGSIZE;
}

static int fill_res_cq_entry(struct sk_buff *msg, struct netlink_callback *cb,
			     struct rdma_restrack_entry *res, uint32_t port)
{
	struct ib_cq *cq = container_of(res, struct ib_cq, res);
	struct nlattr *entry_attr;

	entry_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_RES_CQ_ENTRY);
	if (!entry_attr)
		goto out;

	if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_CQE, cq->cqe))
		goto err;
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_RES_USECNT,
			      atomic_read(&cq->usecnt), RDMA_NLDEV_ATTR_PAD))
		goto err;

	/* Poll context is only valid for kernel CQs */
	if (rdma_is_kernel_res(res) &&
	    nla_put_u8(msg, RDMA_NLDEV_ATTR_RES_POLL_CTX, cq->poll_ctx))
		goto err;

	if (fill_res_name_pid(msg, res))
		goto err;

	nla_nest_end(msg, entry_attr);
	return 0;

err:
	nla_nest_cancel(msg, entry_attr);
out:
	return -EMSGSIZE;
}

static int fill_res_mr_entry(struct sk_buff *msg, struct netlink_callback *cb,
			     struct rdma_restrack_entry *res, uint32_t port)
{
	struct ib_mr *mr = container_of(res, struct ib_mr, res);
	struct nlattr *entry_attr;

	entry_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_RES_MR_ENTRY);
	if (!entry_attr)
		goto out;

	if (netlink_capable(cb->skb, CAP_NET_ADMIN)) {
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_RKEY, mr->rkey))
			goto err;
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_LKEY, mr->lkey))
			goto err;
		if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_RES_IOVA,
				      mr->iova, RDMA_NLDEV_ATTR_PAD))
			goto err;
	}

	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_RES_MRLEN, mr->length,
			      RDMA_NLDEV_ATTR_PAD))
		goto err;

	if (fill_res_name_pid(msg, res))
		goto err;

	nla_nest_end(msg, entry_attr);
	return 0;

err:
	nla_nest_cancel(msg, entry_attr);
out:
	return -EMSGSIZE;
}

static int fill_res_pd_entry(struct sk_buff *msg, struct netlink_callback *cb,
			     struct rdma_restrack_entry *res, uint32_t port)
{
	struct ib_pd *pd = container_of(res, struct ib_pd, res);
	struct nlattr *entry_attr;

	entry_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_RES_PD_ENTRY);
	if (!entry_attr)
		goto out;

	if (netlink_capable(cb->skb, CAP_NET_ADMIN)) {
		if (nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_LOCAL_DMA_LKEY,
				pd->local_dma_lkey))
			goto err;
		if ((pd->flags & IB_PD_UNSAFE_GLOBAL_RKEY) &&
		    nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_UNSAFE_GLOBAL_RKEY,
				pd->unsafe_global_rkey))
			goto err;
	}
	if (nla_put_u64_64bit(msg, RDMA_NLDEV_ATTR_RES_USECNT,
			      atomic_read(&pd->usecnt), RDMA_NLDEV_ATTR_PAD))
		goto err;
	if ((pd->flags & IB_PD_UNSAFE_GLOBAL_RKEY) &&
	    nla_put_u32(msg, RDMA_NLDEV_ATTR_RES_UNSAFE_GLOBAL_RKEY,
			pd->unsafe_global_rkey))
		goto err;

	if (fill_res_name_pid(msg, res))
		goto err;

	nla_nest_end(msg, entry_attr);
	return 0;

err:
	nla_nest_cancel(msg, entry_attr);
out:
	return -EMSGSIZE;
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

	device = ib_device_get_by_index(index);
	if (!device)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto err;
	}

	nlh = nlmsg_put(msg, NETLINK_CB(skb).portid, nlh->nlmsg_seq,
			RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_GET),
			0, 0);

	err = fill_dev_info(msg, device);
	if (err)
		goto err_free;

	nlmsg_end(msg, nlh);

	put_device(&device->dev);
	return rdma_nl_unicast(msg, NETLINK_CB(skb).portid);

err_free:
	nlmsg_free(msg);
err:
	put_device(&device->dev);
	return err;
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
	device = ib_device_get_by_index(index);
	if (!device)
		return -EINVAL;

	port = nla_get_u32(tb[RDMA_NLDEV_ATTR_PORT_INDEX]);
	if (!rdma_is_port_valid(device, port)) {
		err = -EINVAL;
		goto err;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto err;
	}

	nlh = nlmsg_put(msg, NETLINK_CB(skb).portid, nlh->nlmsg_seq,
			RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_GET),
			0, 0);

	err = fill_port_info(msg, device, port, sock_net(skb->sk));
	if (err)
		goto err_free;

	nlmsg_end(msg, nlh);
	put_device(&device->dev);

	return rdma_nl_unicast(msg, NETLINK_CB(skb).portid);

err_free:
	nlmsg_free(msg);
err:
	put_device(&device->dev);
	return err;
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
	device = ib_device_get_by_index(ifindex);
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

		if (fill_port_info(skb, device, p, sock_net(skb->sk))) {
			nlmsg_cancel(skb, nlh);
			goto out;
		}
		idx++;
		nlmsg_end(skb, nlh);
	}

out:
	put_device(&device->dev);
	cb->args[0] = idx;
	return skb->len;
}

static int nldev_res_get_doit(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *extack)
{
	struct nlattr *tb[RDMA_NLDEV_ATTR_MAX];
	struct ib_device *device;
	struct sk_buff *msg;
	u32 index;
	int ret;

	ret = nlmsg_parse(nlh, 0, tb, RDMA_NLDEV_ATTR_MAX - 1,
			  nldev_policy, extack);
	if (ret || !tb[RDMA_NLDEV_ATTR_DEV_INDEX])
		return -EINVAL;

	index = nla_get_u32(tb[RDMA_NLDEV_ATTR_DEV_INDEX]);
	device = ib_device_get_by_index(index);
	if (!device)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto err;
	}

	nlh = nlmsg_put(msg, NETLINK_CB(skb).portid, nlh->nlmsg_seq,
			RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_RES_GET),
			0, 0);

	ret = fill_res_info(msg, device);
	if (ret)
		goto err_free;

	nlmsg_end(msg, nlh);
	put_device(&device->dev);
	return rdma_nl_unicast(msg, NETLINK_CB(skb).portid);

err_free:
	nlmsg_free(msg);
err:
	put_device(&device->dev);
	return ret;
}

static int _nldev_res_get_dumpit(struct ib_device *device,
				 struct sk_buff *skb,
				 struct netlink_callback *cb,
				 unsigned int idx)
{
	int start = cb->args[0];
	struct nlmsghdr *nlh;

	if (idx < start)
		return 0;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_RES_GET),
			0, NLM_F_MULTI);

	if (fill_res_info(skb, device)) {
		nlmsg_cancel(skb, nlh);
		goto out;
	}

	nlmsg_end(skb, nlh);

	idx++;

out:
	cb->args[0] = idx;
	return skb->len;
}

static int nldev_res_get_dumpit(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	return ib_enum_all_devs(_nldev_res_get_dumpit, skb, cb);
}

struct nldev_fill_res_entry {
	int (*fill_res_func)(struct sk_buff *msg, struct netlink_callback *cb,
			     struct rdma_restrack_entry *res, u32 port);
	enum rdma_nldev_attr nldev_attr;
	enum rdma_nldev_command nldev_cmd;
};

static const struct nldev_fill_res_entry fill_entries[RDMA_RESTRACK_MAX] = {
	[RDMA_RESTRACK_QP] = {
		.fill_res_func = fill_res_qp_entry,
		.nldev_cmd = RDMA_NLDEV_CMD_RES_QP_GET,
		.nldev_attr = RDMA_NLDEV_ATTR_RES_QP,
	},
	[RDMA_RESTRACK_CM_ID] = {
		.fill_res_func = fill_res_cm_id_entry,
		.nldev_cmd = RDMA_NLDEV_CMD_RES_CM_ID_GET,
		.nldev_attr = RDMA_NLDEV_ATTR_RES_CM_ID,
	},
	[RDMA_RESTRACK_CQ] = {
		.fill_res_func = fill_res_cq_entry,
		.nldev_cmd = RDMA_NLDEV_CMD_RES_CQ_GET,
		.nldev_attr = RDMA_NLDEV_ATTR_RES_CQ,
	},
	[RDMA_RESTRACK_MR] = {
		.fill_res_func = fill_res_mr_entry,
		.nldev_cmd = RDMA_NLDEV_CMD_RES_MR_GET,
		.nldev_attr = RDMA_NLDEV_ATTR_RES_MR,
	},
	[RDMA_RESTRACK_PD] = {
		.fill_res_func = fill_res_pd_entry,
		.nldev_cmd = RDMA_NLDEV_CMD_RES_PD_GET,
		.nldev_attr = RDMA_NLDEV_ATTR_RES_PD,
	},
};

static int res_get_common_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb,
				 enum rdma_restrack_type res_type)
{
	const struct nldev_fill_res_entry *fe = &fill_entries[res_type];
	struct nlattr *tb[RDMA_NLDEV_ATTR_MAX];
	struct rdma_restrack_entry *res;
	int err, ret = 0, idx = 0;
	struct nlattr *table_attr;
	struct ib_device *device;
	int start = cb->args[0];
	struct nlmsghdr *nlh;
	u32 index, port = 0;
	bool filled = false;

	err = nlmsg_parse(cb->nlh, 0, tb, RDMA_NLDEV_ATTR_MAX - 1,
			  nldev_policy, NULL);
	/*
	 * Right now, we are expecting the device index to get res information,
	 * but it is possible to extend this code to return all devices in
	 * one shot by checking the existence of RDMA_NLDEV_ATTR_DEV_INDEX.
	 * if it doesn't exist, we will iterate over all devices.
	 *
	 * But it is not needed for now.
	 */
	if (err || !tb[RDMA_NLDEV_ATTR_DEV_INDEX])
		return -EINVAL;

	index = nla_get_u32(tb[RDMA_NLDEV_ATTR_DEV_INDEX]);
	device = ib_device_get_by_index(index);
	if (!device)
		return -EINVAL;

	/*
	 * If no PORT_INDEX is supplied, we will return all QPs from that device
	 */
	if (tb[RDMA_NLDEV_ATTR_PORT_INDEX]) {
		port = nla_get_u32(tb[RDMA_NLDEV_ATTR_PORT_INDEX]);
		if (!rdma_is_port_valid(device, port)) {
			ret = -EINVAL;
			goto err_index;
		}
	}

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, fe->nldev_cmd),
			0, NLM_F_MULTI);

	if (fill_nldev_handle(skb, device)) {
		ret = -EMSGSIZE;
		goto err;
	}

	table_attr = nla_nest_start(skb, fe->nldev_attr);
	if (!table_attr) {
		ret = -EMSGSIZE;
		goto err;
	}

	down_read(&device->res.rwsem);
	hash_for_each_possible(device->res.hash, res, node, res_type) {
		if (idx < start)
			goto next;

		if ((rdma_is_kernel_res(res) &&
		     task_active_pid_ns(current) != &init_pid_ns) ||
		    (!rdma_is_kernel_res(res) && task_active_pid_ns(current) !=
		     task_active_pid_ns(res->task)))
			/*
			 * 1. Kern resources should be visible in init
			 *    namspace only
			 * 2. Present only resources visible in the current
			 *    namespace
			 */
			goto next;

		if (!rdma_restrack_get(res))
			/*
			 * Resource is under release now, but we are not
			 * relesing lock now, so it will be released in
			 * our next pass, once we will get ->next pointer.
			 */
			goto next;

		filled = true;

		up_read(&device->res.rwsem);
		ret = fe->fill_res_func(skb, cb, res, port);
		down_read(&device->res.rwsem);
		/*
		 * Return resource back, but it won't be released till
		 * the &device->res.rwsem will be released for write.
		 */
		rdma_restrack_put(res);

		if (ret == -EMSGSIZE)
			/*
			 * There is a chance to optimize here.
			 * It can be done by using list_prepare_entry
			 * and list_for_each_entry_continue afterwards.
			 */
			break;
		if (ret)
			goto res_err;
next:		idx++;
	}
	up_read(&device->res.rwsem);

	nla_nest_end(skb, table_attr);
	nlmsg_end(skb, nlh);
	cb->args[0] = idx;

	/*
	 * No more entries to fill, cancel the message and
	 * return 0 to mark end of dumpit.
	 */
	if (!filled)
		goto err;

	put_device(&device->dev);
	return skb->len;

res_err:
	nla_nest_cancel(skb, table_attr);
	up_read(&device->res.rwsem);

err:
	nlmsg_cancel(skb, nlh);

err_index:
	put_device(&device->dev);
	return ret;
}

static int nldev_res_get_qp_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	return res_get_common_dumpit(skb, cb, RDMA_RESTRACK_QP);
}

static int nldev_res_get_cm_id_dumpit(struct sk_buff *skb,
				      struct netlink_callback *cb)
{
	return res_get_common_dumpit(skb, cb, RDMA_RESTRACK_CM_ID);
}

static int nldev_res_get_cq_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	return res_get_common_dumpit(skb, cb, RDMA_RESTRACK_CQ);
}

static int nldev_res_get_mr_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	return res_get_common_dumpit(skb, cb, RDMA_RESTRACK_MR);
}

static int nldev_res_get_pd_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	return res_get_common_dumpit(skb, cb, RDMA_RESTRACK_PD);
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
	[RDMA_NLDEV_CMD_RES_GET] = {
		.doit = nldev_res_get_doit,
		.dump = nldev_res_get_dumpit,
	},
	[RDMA_NLDEV_CMD_RES_QP_GET] = {
		.dump = nldev_res_get_qp_dumpit,
		/*
		 * .doit is not implemented yet for two reasons:
		 * 1. It is not needed yet.
		 * 2. There is a need to provide identifier, while it is easy
		 * for the QPs (device index + port index + LQPN), it is not
		 * the case for the rest of resources (PD and CQ). Because it
		 * is better to provide similar interface for all resources,
		 * let's wait till we will have other resources implemented
		 * too.
		 */
	},
	[RDMA_NLDEV_CMD_RES_CM_ID_GET] = {
		.dump = nldev_res_get_cm_id_dumpit,
	},
	[RDMA_NLDEV_CMD_RES_CQ_GET] = {
		.dump = nldev_res_get_cq_dumpit,
	},
	[RDMA_NLDEV_CMD_RES_MR_GET] = {
		.dump = nldev_res_get_mr_dumpit,
	},
	[RDMA_NLDEV_CMD_RES_PD_GET] = {
		.dump = nldev_res_get_pd_dumpit,
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
