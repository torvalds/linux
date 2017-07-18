/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
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

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <net/addrconf.h>
#include <linux/security.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_addr.h>
#include <rdma/rw.h>

#include "core_priv.h"

static const char * const ib_events[] = {
	[IB_EVENT_CQ_ERR]		= "CQ error",
	[IB_EVENT_QP_FATAL]		= "QP fatal error",
	[IB_EVENT_QP_REQ_ERR]		= "QP request error",
	[IB_EVENT_QP_ACCESS_ERR]	= "QP access error",
	[IB_EVENT_COMM_EST]		= "communication established",
	[IB_EVENT_SQ_DRAINED]		= "send queue drained",
	[IB_EVENT_PATH_MIG]		= "path migration successful",
	[IB_EVENT_PATH_MIG_ERR]		= "path migration error",
	[IB_EVENT_DEVICE_FATAL]		= "device fatal error",
	[IB_EVENT_PORT_ACTIVE]		= "port active",
	[IB_EVENT_PORT_ERR]		= "port error",
	[IB_EVENT_LID_CHANGE]		= "LID change",
	[IB_EVENT_PKEY_CHANGE]		= "P_key change",
	[IB_EVENT_SM_CHANGE]		= "SM change",
	[IB_EVENT_SRQ_ERR]		= "SRQ error",
	[IB_EVENT_SRQ_LIMIT_REACHED]	= "SRQ limit reached",
	[IB_EVENT_QP_LAST_WQE_REACHED]	= "last WQE reached",
	[IB_EVENT_CLIENT_REREGISTER]	= "client reregister",
	[IB_EVENT_GID_CHANGE]		= "GID changed",
};

const char *__attribute_const__ ib_event_msg(enum ib_event_type event)
{
	size_t index = event;

	return (index < ARRAY_SIZE(ib_events) && ib_events[index]) ?
			ib_events[index] : "unrecognized event";
}
EXPORT_SYMBOL(ib_event_msg);

static const char * const wc_statuses[] = {
	[IB_WC_SUCCESS]			= "success",
	[IB_WC_LOC_LEN_ERR]		= "local length error",
	[IB_WC_LOC_QP_OP_ERR]		= "local QP operation error",
	[IB_WC_LOC_EEC_OP_ERR]		= "local EE context operation error",
	[IB_WC_LOC_PROT_ERR]		= "local protection error",
	[IB_WC_WR_FLUSH_ERR]		= "WR flushed",
	[IB_WC_MW_BIND_ERR]		= "memory management operation error",
	[IB_WC_BAD_RESP_ERR]		= "bad response error",
	[IB_WC_LOC_ACCESS_ERR]		= "local access error",
	[IB_WC_REM_INV_REQ_ERR]		= "invalid request error",
	[IB_WC_REM_ACCESS_ERR]		= "remote access error",
	[IB_WC_REM_OP_ERR]		= "remote operation error",
	[IB_WC_RETRY_EXC_ERR]		= "transport retry counter exceeded",
	[IB_WC_RNR_RETRY_EXC_ERR]	= "RNR retry counter exceeded",
	[IB_WC_LOC_RDD_VIOL_ERR]	= "local RDD violation error",
	[IB_WC_REM_INV_RD_REQ_ERR]	= "remote invalid RD request",
	[IB_WC_REM_ABORT_ERR]		= "operation aborted",
	[IB_WC_INV_EECN_ERR]		= "invalid EE context number",
	[IB_WC_INV_EEC_STATE_ERR]	= "invalid EE context state",
	[IB_WC_FATAL_ERR]		= "fatal error",
	[IB_WC_RESP_TIMEOUT_ERR]	= "response timeout error",
	[IB_WC_GENERAL_ERR]		= "general error",
};

const char *__attribute_const__ ib_wc_status_msg(enum ib_wc_status status)
{
	size_t index = status;

	return (index < ARRAY_SIZE(wc_statuses) && wc_statuses[index]) ?
			wc_statuses[index] : "unrecognized status";
}
EXPORT_SYMBOL(ib_wc_status_msg);

__attribute_const__ int ib_rate_to_mult(enum ib_rate rate)
{
	switch (rate) {
	case IB_RATE_2_5_GBPS: return  1;
	case IB_RATE_5_GBPS:   return  2;
	case IB_RATE_10_GBPS:  return  4;
	case IB_RATE_20_GBPS:  return  8;
	case IB_RATE_30_GBPS:  return 12;
	case IB_RATE_40_GBPS:  return 16;
	case IB_RATE_60_GBPS:  return 24;
	case IB_RATE_80_GBPS:  return 32;
	case IB_RATE_120_GBPS: return 48;
	default:	       return -1;
	}
}
EXPORT_SYMBOL(ib_rate_to_mult);

__attribute_const__ enum ib_rate mult_to_ib_rate(int mult)
{
	switch (mult) {
	case 1:  return IB_RATE_2_5_GBPS;
	case 2:  return IB_RATE_5_GBPS;
	case 4:  return IB_RATE_10_GBPS;
	case 8:  return IB_RATE_20_GBPS;
	case 12: return IB_RATE_30_GBPS;
	case 16: return IB_RATE_40_GBPS;
	case 24: return IB_RATE_60_GBPS;
	case 32: return IB_RATE_80_GBPS;
	case 48: return IB_RATE_120_GBPS;
	default: return IB_RATE_PORT_CURRENT;
	}
}
EXPORT_SYMBOL(mult_to_ib_rate);

__attribute_const__ int ib_rate_to_mbps(enum ib_rate rate)
{
	switch (rate) {
	case IB_RATE_2_5_GBPS: return 2500;
	case IB_RATE_5_GBPS:   return 5000;
	case IB_RATE_10_GBPS:  return 10000;
	case IB_RATE_20_GBPS:  return 20000;
	case IB_RATE_30_GBPS:  return 30000;
	case IB_RATE_40_GBPS:  return 40000;
	case IB_RATE_60_GBPS:  return 60000;
	case IB_RATE_80_GBPS:  return 80000;
	case IB_RATE_120_GBPS: return 120000;
	case IB_RATE_14_GBPS:  return 14062;
	case IB_RATE_56_GBPS:  return 56250;
	case IB_RATE_112_GBPS: return 112500;
	case IB_RATE_168_GBPS: return 168750;
	case IB_RATE_25_GBPS:  return 25781;
	case IB_RATE_100_GBPS: return 103125;
	case IB_RATE_200_GBPS: return 206250;
	case IB_RATE_300_GBPS: return 309375;
	default:	       return -1;
	}
}
EXPORT_SYMBOL(ib_rate_to_mbps);

__attribute_const__ enum rdma_transport_type
rdma_node_get_transport(enum rdma_node_type node_type)
{
	switch (node_type) {
	case RDMA_NODE_IB_CA:
	case RDMA_NODE_IB_SWITCH:
	case RDMA_NODE_IB_ROUTER:
		return RDMA_TRANSPORT_IB;
	case RDMA_NODE_RNIC:
		return RDMA_TRANSPORT_IWARP;
	case RDMA_NODE_USNIC:
		return RDMA_TRANSPORT_USNIC;
	case RDMA_NODE_USNIC_UDP:
		return RDMA_TRANSPORT_USNIC_UDP;
	default:
		BUG();
		return 0;
	}
}
EXPORT_SYMBOL(rdma_node_get_transport);

enum rdma_link_layer rdma_port_get_link_layer(struct ib_device *device, u8 port_num)
{
	if (device->get_link_layer)
		return device->get_link_layer(device, port_num);

	switch (rdma_node_get_transport(device->node_type)) {
	case RDMA_TRANSPORT_IB:
		return IB_LINK_LAYER_INFINIBAND;
	case RDMA_TRANSPORT_IWARP:
	case RDMA_TRANSPORT_USNIC:
	case RDMA_TRANSPORT_USNIC_UDP:
		return IB_LINK_LAYER_ETHERNET;
	default:
		return IB_LINK_LAYER_UNSPECIFIED;
	}
}
EXPORT_SYMBOL(rdma_port_get_link_layer);

/* Protection domains */

/**
 * ib_alloc_pd - Allocates an unused protection domain.
 * @device: The device on which to allocate the protection domain.
 *
 * A protection domain object provides an association between QPs, shared
 * receive queues, address handles, memory regions, and memory windows.
 *
 * Every PD has a local_dma_lkey which can be used as the lkey value for local
 * memory operations.
 */
struct ib_pd *__ib_alloc_pd(struct ib_device *device, unsigned int flags,
		const char *caller)
{
	struct ib_pd *pd;
	int mr_access_flags = 0;

	pd = device->alloc_pd(device, NULL, NULL);
	if (IS_ERR(pd))
		return pd;

	pd->device = device;
	pd->uobject = NULL;
	pd->__internal_mr = NULL;
	atomic_set(&pd->usecnt, 0);
	pd->flags = flags;

	if (device->attrs.device_cap_flags & IB_DEVICE_LOCAL_DMA_LKEY)
		pd->local_dma_lkey = device->local_dma_lkey;
	else
		mr_access_flags |= IB_ACCESS_LOCAL_WRITE;

	if (flags & IB_PD_UNSAFE_GLOBAL_RKEY) {
		pr_warn("%s: enabling unsafe global rkey\n", caller);
		mr_access_flags |= IB_ACCESS_REMOTE_READ | IB_ACCESS_REMOTE_WRITE;
	}

	if (mr_access_flags) {
		struct ib_mr *mr;

		mr = pd->device->get_dma_mr(pd, mr_access_flags);
		if (IS_ERR(mr)) {
			ib_dealloc_pd(pd);
			return ERR_CAST(mr);
		}

		mr->device	= pd->device;
		mr->pd		= pd;
		mr->uobject	= NULL;
		mr->need_inval	= false;

		pd->__internal_mr = mr;

		if (!(device->attrs.device_cap_flags & IB_DEVICE_LOCAL_DMA_LKEY))
			pd->local_dma_lkey = pd->__internal_mr->lkey;

		if (flags & IB_PD_UNSAFE_GLOBAL_RKEY)
			pd->unsafe_global_rkey = pd->__internal_mr->rkey;
	}

	return pd;
}
EXPORT_SYMBOL(__ib_alloc_pd);

/**
 * ib_dealloc_pd - Deallocates a protection domain.
 * @pd: The protection domain to deallocate.
 *
 * It is an error to call this function while any resources in the pd still
 * exist.  The caller is responsible to synchronously destroy them and
 * guarantee no new allocations will happen.
 */
void ib_dealloc_pd(struct ib_pd *pd)
{
	int ret;

	if (pd->__internal_mr) {
		ret = pd->device->dereg_mr(pd->__internal_mr);
		WARN_ON(ret);
		pd->__internal_mr = NULL;
	}

	/* uverbs manipulates usecnt with proper locking, while the kabi
	   requires the caller to guarantee we can't race here. */
	WARN_ON(atomic_read(&pd->usecnt));

	/* Making delalloc_pd a void return is a WIP, no driver should return
	   an error here. */
	ret = pd->device->dealloc_pd(pd);
	WARN_ONCE(ret, "Infiniband HW driver failed dealloc_pd");
}
EXPORT_SYMBOL(ib_dealloc_pd);

/* Address handles */

struct ib_ah *rdma_create_ah(struct ib_pd *pd, struct rdma_ah_attr *ah_attr)
{
	struct ib_ah *ah;

	ah = pd->device->create_ah(pd, ah_attr, NULL);

	if (!IS_ERR(ah)) {
		ah->device  = pd->device;
		ah->pd      = pd;
		ah->uobject = NULL;
		ah->type    = ah_attr->type;
		atomic_inc(&pd->usecnt);
	}

	return ah;
}
EXPORT_SYMBOL(rdma_create_ah);

int ib_get_rdma_header_version(const union rdma_network_hdr *hdr)
{
	const struct iphdr *ip4h = (struct iphdr *)&hdr->roce4grh;
	struct iphdr ip4h_checked;
	const struct ipv6hdr *ip6h = (struct ipv6hdr *)&hdr->ibgrh;

	/* If it's IPv6, the version must be 6, otherwise, the first
	 * 20 bytes (before the IPv4 header) are garbled.
	 */
	if (ip6h->version != 6)
		return (ip4h->version == 4) ? 4 : 0;
	/* version may be 6 or 4 because the first 20 bytes could be garbled */

	/* RoCE v2 requires no options, thus header length
	 * must be 5 words
	 */
	if (ip4h->ihl != 5)
		return 6;

	/* Verify checksum.
	 * We can't write on scattered buffers so we need to copy to
	 * temp buffer.
	 */
	memcpy(&ip4h_checked, ip4h, sizeof(ip4h_checked));
	ip4h_checked.check = 0;
	ip4h_checked.check = ip_fast_csum((u8 *)&ip4h_checked, 5);
	/* if IPv4 header checksum is OK, believe it */
	if (ip4h->check == ip4h_checked.check)
		return 4;
	return 6;
}
EXPORT_SYMBOL(ib_get_rdma_header_version);

static enum rdma_network_type ib_get_net_type_by_grh(struct ib_device *device,
						     u8 port_num,
						     const struct ib_grh *grh)
{
	int grh_version;

	if (rdma_protocol_ib(device, port_num))
		return RDMA_NETWORK_IB;

	grh_version = ib_get_rdma_header_version((union rdma_network_hdr *)grh);

	if (grh_version == 4)
		return RDMA_NETWORK_IPV4;

	if (grh->next_hdr == IPPROTO_UDP)
		return RDMA_NETWORK_IPV6;

	return RDMA_NETWORK_ROCE_V1;
}

struct find_gid_index_context {
	u16 vlan_id;
	enum ib_gid_type gid_type;
};

static bool find_gid_index(const union ib_gid *gid,
			   const struct ib_gid_attr *gid_attr,
			   void *context)
{
	struct find_gid_index_context *ctx =
		(struct find_gid_index_context *)context;

	if (ctx->gid_type != gid_attr->gid_type)
		return false;

	if ((!!(ctx->vlan_id != 0xffff) == !is_vlan_dev(gid_attr->ndev)) ||
	    (is_vlan_dev(gid_attr->ndev) &&
	     vlan_dev_vlan_id(gid_attr->ndev) != ctx->vlan_id))
		return false;

	return true;
}

static int get_sgid_index_from_eth(struct ib_device *device, u8 port_num,
				   u16 vlan_id, const union ib_gid *sgid,
				   enum ib_gid_type gid_type,
				   u16 *gid_index)
{
	struct find_gid_index_context context = {.vlan_id = vlan_id,
						 .gid_type = gid_type};

	return ib_find_gid_by_filter(device, sgid, port_num, find_gid_index,
				     &context, gid_index);
}

int ib_get_gids_from_rdma_hdr(const union rdma_network_hdr *hdr,
			      enum rdma_network_type net_type,
			      union ib_gid *sgid, union ib_gid *dgid)
{
	struct sockaddr_in  src_in;
	struct sockaddr_in  dst_in;
	__be32 src_saddr, dst_saddr;

	if (!sgid || !dgid)
		return -EINVAL;

	if (net_type == RDMA_NETWORK_IPV4) {
		memcpy(&src_in.sin_addr.s_addr,
		       &hdr->roce4grh.saddr, 4);
		memcpy(&dst_in.sin_addr.s_addr,
		       &hdr->roce4grh.daddr, 4);
		src_saddr = src_in.sin_addr.s_addr;
		dst_saddr = dst_in.sin_addr.s_addr;
		ipv6_addr_set_v4mapped(src_saddr,
				       (struct in6_addr *)sgid);
		ipv6_addr_set_v4mapped(dst_saddr,
				       (struct in6_addr *)dgid);
		return 0;
	} else if (net_type == RDMA_NETWORK_IPV6 ||
		   net_type == RDMA_NETWORK_IB) {
		*dgid = hdr->ibgrh.dgid;
		*sgid = hdr->ibgrh.sgid;
		return 0;
	} else {
		return -EINVAL;
	}
}
EXPORT_SYMBOL(ib_get_gids_from_rdma_hdr);

/*
 * This function creates ah from the incoming packet.
 * Incoming packet has dgid of the receiver node on which this code is
 * getting executed and, sgid contains the GID of the sender.
 *
 * When resolving mac address of destination, the arrived dgid is used
 * as sgid and, sgid is used as dgid because sgid contains destinations
 * GID whom to respond to.
 *
 * This is why when calling rdma_addr_find_l2_eth_by_grh() function, the
 * position of arguments dgid and sgid do not match the order of the
 * parameters.
 */
int ib_init_ah_from_wc(struct ib_device *device, u8 port_num,
		       const struct ib_wc *wc, const struct ib_grh *grh,
		       struct rdma_ah_attr *ah_attr)
{
	u32 flow_class;
	u16 gid_index;
	int ret;
	enum rdma_network_type net_type = RDMA_NETWORK_IB;
	enum ib_gid_type gid_type = IB_GID_TYPE_IB;
	int hoplimit = 0xff;
	union ib_gid dgid;
	union ib_gid sgid;

	memset(ah_attr, 0, sizeof *ah_attr);
	ah_attr->type = rdma_ah_find_type(device, port_num);
	if (rdma_cap_eth_ah(device, port_num)) {
		if (wc->wc_flags & IB_WC_WITH_NETWORK_HDR_TYPE)
			net_type = wc->network_hdr_type;
		else
			net_type = ib_get_net_type_by_grh(device, port_num, grh);
		gid_type = ib_network_to_gid_type(net_type);
	}
	ret = ib_get_gids_from_rdma_hdr((union rdma_network_hdr *)grh, net_type,
					&sgid, &dgid);
	if (ret)
		return ret;

	if (rdma_protocol_roce(device, port_num)) {
		int if_index = 0;
		u16 vlan_id = wc->wc_flags & IB_WC_WITH_VLAN ?
				wc->vlan_id : 0xffff;
		struct net_device *idev;
		struct net_device *resolved_dev;

		if (!(wc->wc_flags & IB_WC_GRH))
			return -EPROTOTYPE;

		if (!device->get_netdev)
			return -EOPNOTSUPP;

		idev = device->get_netdev(device, port_num);
		if (!idev)
			return -ENODEV;

		ret = rdma_addr_find_l2_eth_by_grh(&dgid, &sgid,
						   ah_attr->roce.dmac,
						   wc->wc_flags & IB_WC_WITH_VLAN ?
						   NULL : &vlan_id,
						   &if_index, &hoplimit);
		if (ret) {
			dev_put(idev);
			return ret;
		}

		resolved_dev = dev_get_by_index(&init_net, if_index);
		rcu_read_lock();
		if (resolved_dev != idev && !rdma_is_upper_dev_rcu(idev,
								   resolved_dev))
			ret = -EHOSTUNREACH;
		rcu_read_unlock();
		dev_put(idev);
		dev_put(resolved_dev);
		if (ret)
			return ret;

		ret = get_sgid_index_from_eth(device, port_num, vlan_id,
					      &dgid, gid_type, &gid_index);
		if (ret)
			return ret;
	}

	rdma_ah_set_dlid(ah_attr, wc->slid);
	rdma_ah_set_sl(ah_attr, wc->sl);
	rdma_ah_set_path_bits(ah_attr, wc->dlid_path_bits);
	rdma_ah_set_port_num(ah_attr, port_num);

	if (wc->wc_flags & IB_WC_GRH) {
		if (!rdma_cap_eth_ah(device, port_num)) {
			if (dgid.global.interface_id != cpu_to_be64(IB_SA_WELL_KNOWN_GUID)) {
				ret = ib_find_cached_gid_by_port(device, &dgid,
								 IB_GID_TYPE_IB,
								 port_num, NULL,
								 &gid_index);
				if (ret)
					return ret;
			} else {
				gid_index = 0;
			}
		}

		flow_class = be32_to_cpu(grh->version_tclass_flow);
		rdma_ah_set_grh(ah_attr, &sgid,
				flow_class & 0xFFFFF,
				(u8)gid_index, hoplimit,
				(flow_class >> 20) & 0xFF);

	}
	return 0;
}
EXPORT_SYMBOL(ib_init_ah_from_wc);

struct ib_ah *ib_create_ah_from_wc(struct ib_pd *pd, const struct ib_wc *wc,
				   const struct ib_grh *grh, u8 port_num)
{
	struct rdma_ah_attr ah_attr;
	int ret;

	ret = ib_init_ah_from_wc(pd->device, port_num, wc, grh, &ah_attr);
	if (ret)
		return ERR_PTR(ret);

	return rdma_create_ah(pd, &ah_attr);
}
EXPORT_SYMBOL(ib_create_ah_from_wc);

int rdma_modify_ah(struct ib_ah *ah, struct rdma_ah_attr *ah_attr)
{
	if (ah->type != ah_attr->type)
		return -EINVAL;

	return ah->device->modify_ah ?
		ah->device->modify_ah(ah, ah_attr) :
		-ENOSYS;
}
EXPORT_SYMBOL(rdma_modify_ah);

int rdma_query_ah(struct ib_ah *ah, struct rdma_ah_attr *ah_attr)
{
	return ah->device->query_ah ?
		ah->device->query_ah(ah, ah_attr) :
		-ENOSYS;
}
EXPORT_SYMBOL(rdma_query_ah);

int rdma_destroy_ah(struct ib_ah *ah)
{
	struct ib_pd *pd;
	int ret;

	pd = ah->pd;
	ret = ah->device->destroy_ah(ah);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(rdma_destroy_ah);

/* Shared receive queues */

struct ib_srq *ib_create_srq(struct ib_pd *pd,
			     struct ib_srq_init_attr *srq_init_attr)
{
	struct ib_srq *srq;

	if (!pd->device->create_srq)
		return ERR_PTR(-ENOSYS);

	srq = pd->device->create_srq(pd, srq_init_attr, NULL);

	if (!IS_ERR(srq)) {
		srq->device    	   = pd->device;
		srq->pd        	   = pd;
		srq->uobject       = NULL;
		srq->event_handler = srq_init_attr->event_handler;
		srq->srq_context   = srq_init_attr->srq_context;
		srq->srq_type      = srq_init_attr->srq_type;
		if (srq->srq_type == IB_SRQT_XRC) {
			srq->ext.xrc.xrcd = srq_init_attr->ext.xrc.xrcd;
			srq->ext.xrc.cq   = srq_init_attr->ext.xrc.cq;
			atomic_inc(&srq->ext.xrc.xrcd->usecnt);
			atomic_inc(&srq->ext.xrc.cq->usecnt);
		}
		atomic_inc(&pd->usecnt);
		atomic_set(&srq->usecnt, 0);
	}

	return srq;
}
EXPORT_SYMBOL(ib_create_srq);

int ib_modify_srq(struct ib_srq *srq,
		  struct ib_srq_attr *srq_attr,
		  enum ib_srq_attr_mask srq_attr_mask)
{
	return srq->device->modify_srq ?
		srq->device->modify_srq(srq, srq_attr, srq_attr_mask, NULL) :
		-ENOSYS;
}
EXPORT_SYMBOL(ib_modify_srq);

int ib_query_srq(struct ib_srq *srq,
		 struct ib_srq_attr *srq_attr)
{
	return srq->device->query_srq ?
		srq->device->query_srq(srq, srq_attr) : -ENOSYS;
}
EXPORT_SYMBOL(ib_query_srq);

int ib_destroy_srq(struct ib_srq *srq)
{
	struct ib_pd *pd;
	enum ib_srq_type srq_type;
	struct ib_xrcd *uninitialized_var(xrcd);
	struct ib_cq *uninitialized_var(cq);
	int ret;

	if (atomic_read(&srq->usecnt))
		return -EBUSY;

	pd = srq->pd;
	srq_type = srq->srq_type;
	if (srq_type == IB_SRQT_XRC) {
		xrcd = srq->ext.xrc.xrcd;
		cq = srq->ext.xrc.cq;
	}

	ret = srq->device->destroy_srq(srq);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		if (srq_type == IB_SRQT_XRC) {
			atomic_dec(&xrcd->usecnt);
			atomic_dec(&cq->usecnt);
		}
	}

	return ret;
}
EXPORT_SYMBOL(ib_destroy_srq);

/* Queue pairs */

static void __ib_shared_qp_event_handler(struct ib_event *event, void *context)
{
	struct ib_qp *qp = context;
	unsigned long flags;

	spin_lock_irqsave(&qp->device->event_handler_lock, flags);
	list_for_each_entry(event->element.qp, &qp->open_list, open_list)
		if (event->element.qp->event_handler)
			event->element.qp->event_handler(event, event->element.qp->qp_context);
	spin_unlock_irqrestore(&qp->device->event_handler_lock, flags);
}

static void __ib_insert_xrcd_qp(struct ib_xrcd *xrcd, struct ib_qp *qp)
{
	mutex_lock(&xrcd->tgt_qp_mutex);
	list_add(&qp->xrcd_list, &xrcd->tgt_qp_list);
	mutex_unlock(&xrcd->tgt_qp_mutex);
}

static struct ib_qp *__ib_open_qp(struct ib_qp *real_qp,
				  void (*event_handler)(struct ib_event *, void *),
				  void *qp_context)
{
	struct ib_qp *qp;
	unsigned long flags;
	int err;

	qp = kzalloc(sizeof *qp, GFP_KERNEL);
	if (!qp)
		return ERR_PTR(-ENOMEM);

	qp->real_qp = real_qp;
	err = ib_open_shared_qp_security(qp, real_qp->device);
	if (err) {
		kfree(qp);
		return ERR_PTR(err);
	}

	qp->real_qp = real_qp;
	atomic_inc(&real_qp->usecnt);
	qp->device = real_qp->device;
	qp->event_handler = event_handler;
	qp->qp_context = qp_context;
	qp->qp_num = real_qp->qp_num;
	qp->qp_type = real_qp->qp_type;

	spin_lock_irqsave(&real_qp->device->event_handler_lock, flags);
	list_add(&qp->open_list, &real_qp->open_list);
	spin_unlock_irqrestore(&real_qp->device->event_handler_lock, flags);

	return qp;
}

struct ib_qp *ib_open_qp(struct ib_xrcd *xrcd,
			 struct ib_qp_open_attr *qp_open_attr)
{
	struct ib_qp *qp, *real_qp;

	if (qp_open_attr->qp_type != IB_QPT_XRC_TGT)
		return ERR_PTR(-EINVAL);

	qp = ERR_PTR(-EINVAL);
	mutex_lock(&xrcd->tgt_qp_mutex);
	list_for_each_entry(real_qp, &xrcd->tgt_qp_list, xrcd_list) {
		if (real_qp->qp_num == qp_open_attr->qp_num) {
			qp = __ib_open_qp(real_qp, qp_open_attr->event_handler,
					  qp_open_attr->qp_context);
			break;
		}
	}
	mutex_unlock(&xrcd->tgt_qp_mutex);
	return qp;
}
EXPORT_SYMBOL(ib_open_qp);

static struct ib_qp *ib_create_xrc_qp(struct ib_qp *qp,
		struct ib_qp_init_attr *qp_init_attr)
{
	struct ib_qp *real_qp = qp;

	qp->event_handler = __ib_shared_qp_event_handler;
	qp->qp_context = qp;
	qp->pd = NULL;
	qp->send_cq = qp->recv_cq = NULL;
	qp->srq = NULL;
	qp->xrcd = qp_init_attr->xrcd;
	atomic_inc(&qp_init_attr->xrcd->usecnt);
	INIT_LIST_HEAD(&qp->open_list);

	qp = __ib_open_qp(real_qp, qp_init_attr->event_handler,
			  qp_init_attr->qp_context);
	if (!IS_ERR(qp))
		__ib_insert_xrcd_qp(qp_init_attr->xrcd, real_qp);
	else
		real_qp->device->destroy_qp(real_qp);
	return qp;
}

struct ib_qp *ib_create_qp(struct ib_pd *pd,
			   struct ib_qp_init_attr *qp_init_attr)
{
	struct ib_device *device = pd ? pd->device : qp_init_attr->xrcd->device;
	struct ib_qp *qp;
	int ret;

	if (qp_init_attr->rwq_ind_tbl &&
	    (qp_init_attr->recv_cq ||
	    qp_init_attr->srq || qp_init_attr->cap.max_recv_wr ||
	    qp_init_attr->cap.max_recv_sge))
		return ERR_PTR(-EINVAL);

	/*
	 * If the callers is using the RDMA API calculate the resources
	 * needed for the RDMA READ/WRITE operations.
	 *
	 * Note that these callers need to pass in a port number.
	 */
	if (qp_init_attr->cap.max_rdma_ctxs)
		rdma_rw_init_qp(device, qp_init_attr);

	qp = device->create_qp(pd, qp_init_attr, NULL);
	if (IS_ERR(qp))
		return qp;

	ret = ib_create_qp_security(qp, device);
	if (ret) {
		ib_destroy_qp(qp);
		return ERR_PTR(ret);
	}

	qp->device     = device;
	qp->real_qp    = qp;
	qp->uobject    = NULL;
	qp->qp_type    = qp_init_attr->qp_type;
	qp->rwq_ind_tbl = qp_init_attr->rwq_ind_tbl;

	atomic_set(&qp->usecnt, 0);
	qp->mrs_used = 0;
	spin_lock_init(&qp->mr_lock);
	INIT_LIST_HEAD(&qp->rdma_mrs);
	INIT_LIST_HEAD(&qp->sig_mrs);

	if (qp_init_attr->qp_type == IB_QPT_XRC_TGT)
		return ib_create_xrc_qp(qp, qp_init_attr);

	qp->event_handler = qp_init_attr->event_handler;
	qp->qp_context = qp_init_attr->qp_context;
	if (qp_init_attr->qp_type == IB_QPT_XRC_INI) {
		qp->recv_cq = NULL;
		qp->srq = NULL;
	} else {
		qp->recv_cq = qp_init_attr->recv_cq;
		if (qp_init_attr->recv_cq)
			atomic_inc(&qp_init_attr->recv_cq->usecnt);
		qp->srq = qp_init_attr->srq;
		if (qp->srq)
			atomic_inc(&qp_init_attr->srq->usecnt);
	}

	qp->pd	    = pd;
	qp->send_cq = qp_init_attr->send_cq;
	qp->xrcd    = NULL;

	atomic_inc(&pd->usecnt);
	if (qp_init_attr->send_cq)
		atomic_inc(&qp_init_attr->send_cq->usecnt);
	if (qp_init_attr->rwq_ind_tbl)
		atomic_inc(&qp->rwq_ind_tbl->usecnt);

	if (qp_init_attr->cap.max_rdma_ctxs) {
		ret = rdma_rw_init_mrs(qp, qp_init_attr);
		if (ret) {
			pr_err("failed to init MR pool ret= %d\n", ret);
			ib_destroy_qp(qp);
			return ERR_PTR(ret);
		}
	}

	/*
	 * Note: all hw drivers guarantee that max_send_sge is lower than
	 * the device RDMA WRITE SGE limit but not all hw drivers ensure that
	 * max_send_sge <= max_sge_rd.
	 */
	qp->max_write_sge = qp_init_attr->cap.max_send_sge;
	qp->max_read_sge = min_t(u32, qp_init_attr->cap.max_send_sge,
				 device->attrs.max_sge_rd);

	return qp;
}
EXPORT_SYMBOL(ib_create_qp);

static const struct {
	int			valid;
	enum ib_qp_attr_mask	req_param[IB_QPT_MAX];
	enum ib_qp_attr_mask	opt_param[IB_QPT_MAX];
} qp_state_table[IB_QPS_ERR + 1][IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 },
		[IB_QPS_INIT]  = {
			.valid = 1,
			.req_param = {
				[IB_QPT_UD]  = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_QKEY),
				[IB_QPT_RAW_PACKET] = IB_QP_PORT,
				[IB_QPT_UC]  = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_RC]  = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_XRC_INI] = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_XRC_TGT] = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_SMI] = (IB_QP_PKEY_INDEX		|
						IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_PKEY_INDEX		|
						IB_QP_QKEY),
			}
		},
	},
	[IB_QPS_INIT]  = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 },
		[IB_QPS_INIT]  = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD]  = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_QKEY),
				[IB_QPT_UC]  = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_RC]  = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_XRC_INI] = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_XRC_TGT] = (IB_QP_PKEY_INDEX		|
						IB_QP_PORT			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_SMI] = (IB_QP_PKEY_INDEX		|
						IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_PKEY_INDEX		|
						IB_QP_QKEY),
			}
		},
		[IB_QPS_RTR]   = {
			.valid = 1,
			.req_param = {
				[IB_QPT_UC]  = (IB_QP_AV			|
						IB_QP_PATH_MTU			|
						IB_QP_DEST_QPN			|
						IB_QP_RQ_PSN),
				[IB_QPT_RC]  = (IB_QP_AV			|
						IB_QP_PATH_MTU			|
						IB_QP_DEST_QPN			|
						IB_QP_RQ_PSN			|
						IB_QP_MAX_DEST_RD_ATOMIC	|
						IB_QP_MIN_RNR_TIMER),
				[IB_QPT_XRC_INI] = (IB_QP_AV			|
						IB_QP_PATH_MTU			|
						IB_QP_DEST_QPN			|
						IB_QP_RQ_PSN),
				[IB_QPT_XRC_TGT] = (IB_QP_AV			|
						IB_QP_PATH_MTU			|
						IB_QP_DEST_QPN			|
						IB_QP_RQ_PSN			|
						IB_QP_MAX_DEST_RD_ATOMIC	|
						IB_QP_MIN_RNR_TIMER),
			},
			.opt_param = {
				 [IB_QPT_UD]  = (IB_QP_PKEY_INDEX		|
						 IB_QP_QKEY),
				 [IB_QPT_UC]  = (IB_QP_ALT_PATH			|
						 IB_QP_ACCESS_FLAGS		|
						 IB_QP_PKEY_INDEX),
				 [IB_QPT_RC]  = (IB_QP_ALT_PATH			|
						 IB_QP_ACCESS_FLAGS		|
						 IB_QP_PKEY_INDEX),
				 [IB_QPT_XRC_INI] = (IB_QP_ALT_PATH		|
						 IB_QP_ACCESS_FLAGS		|
						 IB_QP_PKEY_INDEX),
				 [IB_QPT_XRC_TGT] = (IB_QP_ALT_PATH		|
						 IB_QP_ACCESS_FLAGS		|
						 IB_QP_PKEY_INDEX),
				 [IB_QPT_SMI] = (IB_QP_PKEY_INDEX		|
						 IB_QP_QKEY),
				 [IB_QPT_GSI] = (IB_QP_PKEY_INDEX		|
						 IB_QP_QKEY),
			 },
		},
	},
	[IB_QPS_RTR]   = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.req_param = {
				[IB_QPT_UD]  = IB_QP_SQ_PSN,
				[IB_QPT_UC]  = IB_QP_SQ_PSN,
				[IB_QPT_RC]  = (IB_QP_TIMEOUT			|
						IB_QP_RETRY_CNT			|
						IB_QP_RNR_RETRY			|
						IB_QP_SQ_PSN			|
						IB_QP_MAX_QP_RD_ATOMIC),
				[IB_QPT_XRC_INI] = (IB_QP_TIMEOUT		|
						IB_QP_RETRY_CNT			|
						IB_QP_RNR_RETRY			|
						IB_QP_SQ_PSN			|
						IB_QP_MAX_QP_RD_ATOMIC),
				[IB_QPT_XRC_TGT] = (IB_QP_TIMEOUT		|
						IB_QP_SQ_PSN),
				[IB_QPT_SMI] = IB_QP_SQ_PSN,
				[IB_QPT_GSI] = IB_QP_SQ_PSN,
			},
			.opt_param = {
				 [IB_QPT_UD]  = (IB_QP_CUR_STATE		|
						 IB_QP_QKEY),
				 [IB_QPT_UC]  = (IB_QP_CUR_STATE		|
						 IB_QP_ALT_PATH			|
						 IB_QP_ACCESS_FLAGS		|
						 IB_QP_PATH_MIG_STATE),
				 [IB_QPT_RC]  = (IB_QP_CUR_STATE		|
						 IB_QP_ALT_PATH			|
						 IB_QP_ACCESS_FLAGS		|
						 IB_QP_MIN_RNR_TIMER		|
						 IB_QP_PATH_MIG_STATE),
				 [IB_QPT_XRC_INI] = (IB_QP_CUR_STATE		|
						 IB_QP_ALT_PATH			|
						 IB_QP_ACCESS_FLAGS		|
						 IB_QP_PATH_MIG_STATE),
				 [IB_QPT_XRC_TGT] = (IB_QP_CUR_STATE		|
						 IB_QP_ALT_PATH			|
						 IB_QP_ACCESS_FLAGS		|
						 IB_QP_MIN_RNR_TIMER		|
						 IB_QP_PATH_MIG_STATE),
				 [IB_QPT_SMI] = (IB_QP_CUR_STATE		|
						 IB_QP_QKEY),
				 [IB_QPT_GSI] = (IB_QP_CUR_STATE		|
						 IB_QP_QKEY),
				 [IB_QPT_RAW_PACKET] = IB_QP_RATE_LIMIT,
			 }
		}
	},
	[IB_QPS_RTS]   = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD]  = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
				[IB_QPT_UC]  = (IB_QP_CUR_STATE			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_ALT_PATH			|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_RC]  = (IB_QP_CUR_STATE			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_ALT_PATH			|
						IB_QP_PATH_MIG_STATE		|
						IB_QP_MIN_RNR_TIMER),
				[IB_QPT_XRC_INI] = (IB_QP_CUR_STATE		|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_ALT_PATH			|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_XRC_TGT] = (IB_QP_CUR_STATE		|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_ALT_PATH			|
						IB_QP_PATH_MIG_STATE		|
						IB_QP_MIN_RNR_TIMER),
				[IB_QPT_SMI] = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
				[IB_QPT_RAW_PACKET] = IB_QP_RATE_LIMIT,
			}
		},
		[IB_QPS_SQD]   = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD]  = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_UC]  = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_RC]  = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_XRC_INI] = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_XRC_TGT] = IB_QP_EN_SQD_ASYNC_NOTIFY, /* ??? */
				[IB_QPT_SMI] = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_GSI] = IB_QP_EN_SQD_ASYNC_NOTIFY
			}
		},
	},
	[IB_QPS_SQD]   = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD]  = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
				[IB_QPT_UC]  = (IB_QP_CUR_STATE			|
						IB_QP_ALT_PATH			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_RC]  = (IB_QP_CUR_STATE			|
						IB_QP_ALT_PATH			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_MIN_RNR_TIMER		|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_XRC_INI] = (IB_QP_CUR_STATE		|
						IB_QP_ALT_PATH			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_XRC_TGT] = (IB_QP_CUR_STATE		|
						IB_QP_ALT_PATH			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_MIN_RNR_TIMER		|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_SMI] = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
			}
		},
		[IB_QPS_SQD]   = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD]  = (IB_QP_PKEY_INDEX		|
						IB_QP_QKEY),
				[IB_QPT_UC]  = (IB_QP_AV			|
						IB_QP_ALT_PATH			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_PKEY_INDEX		|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_RC]  = (IB_QP_PORT			|
						IB_QP_AV			|
						IB_QP_TIMEOUT			|
						IB_QP_RETRY_CNT			|
						IB_QP_RNR_RETRY			|
						IB_QP_MAX_QP_RD_ATOMIC		|
						IB_QP_MAX_DEST_RD_ATOMIC	|
						IB_QP_ALT_PATH			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_PKEY_INDEX		|
						IB_QP_MIN_RNR_TIMER		|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_XRC_INI] = (IB_QP_PORT			|
						IB_QP_AV			|
						IB_QP_TIMEOUT			|
						IB_QP_RETRY_CNT			|
						IB_QP_RNR_RETRY			|
						IB_QP_MAX_QP_RD_ATOMIC		|
						IB_QP_ALT_PATH			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_PKEY_INDEX		|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_XRC_TGT] = (IB_QP_PORT			|
						IB_QP_AV			|
						IB_QP_TIMEOUT			|
						IB_QP_MAX_DEST_RD_ATOMIC	|
						IB_QP_ALT_PATH			|
						IB_QP_ACCESS_FLAGS		|
						IB_QP_PKEY_INDEX		|
						IB_QP_MIN_RNR_TIMER		|
						IB_QP_PATH_MIG_STATE),
				[IB_QPT_SMI] = (IB_QP_PKEY_INDEX		|
						IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_PKEY_INDEX		|
						IB_QP_QKEY),
			}
		}
	},
	[IB_QPS_SQE]   = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD]  = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
				[IB_QPT_UC]  = (IB_QP_CUR_STATE			|
						IB_QP_ACCESS_FLAGS),
				[IB_QPT_SMI] = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_CUR_STATE			|
						IB_QP_QKEY),
			}
		}
	},
	[IB_QPS_ERR] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 }
	}
};

int ib_modify_qp_is_ok(enum ib_qp_state cur_state, enum ib_qp_state next_state,
		       enum ib_qp_type type, enum ib_qp_attr_mask mask,
		       enum rdma_link_layer ll)
{
	enum ib_qp_attr_mask req_param, opt_param;

	if (cur_state  < 0 || cur_state  > IB_QPS_ERR ||
	    next_state < 0 || next_state > IB_QPS_ERR)
		return 0;

	if (mask & IB_QP_CUR_STATE  &&
	    cur_state != IB_QPS_RTR && cur_state != IB_QPS_RTS &&
	    cur_state != IB_QPS_SQD && cur_state != IB_QPS_SQE)
		return 0;

	if (!qp_state_table[cur_state][next_state].valid)
		return 0;

	req_param = qp_state_table[cur_state][next_state].req_param[type];
	opt_param = qp_state_table[cur_state][next_state].opt_param[type];

	if ((mask & req_param) != req_param)
		return 0;

	if (mask & ~(req_param | opt_param | IB_QP_STATE))
		return 0;

	return 1;
}
EXPORT_SYMBOL(ib_modify_qp_is_ok);

int ib_resolve_eth_dmac(struct ib_device *device,
			struct rdma_ah_attr *ah_attr)
{
	int           ret = 0;
	struct ib_global_route *grh;

	if (!rdma_is_port_valid(device, rdma_ah_get_port_num(ah_attr)))
		return -EINVAL;

	if (ah_attr->type != RDMA_AH_ATTR_TYPE_ROCE)
		return 0;

	grh = rdma_ah_retrieve_grh(ah_attr);

	if (rdma_link_local_addr((struct in6_addr *)grh->dgid.raw)) {
		rdma_get_ll_mac((struct in6_addr *)grh->dgid.raw,
				ah_attr->roce.dmac);
	} else {
		union ib_gid		sgid;
		struct ib_gid_attr	sgid_attr;
		int			ifindex;
		int			hop_limit;

		ret = ib_query_gid(device,
				   rdma_ah_get_port_num(ah_attr),
				   grh->sgid_index,
				   &sgid, &sgid_attr);

		if (ret || !sgid_attr.ndev) {
			if (!ret)
				ret = -ENXIO;
			goto out;
		}

		ifindex = sgid_attr.ndev->ifindex;

		ret =
		rdma_addr_find_l2_eth_by_grh(&sgid, &grh->dgid,
					     ah_attr->roce.dmac,
					     NULL, &ifindex, &hop_limit);

		dev_put(sgid_attr.ndev);

		grh->hop_limit = hop_limit;
	}
out:
	return ret;
}
EXPORT_SYMBOL(ib_resolve_eth_dmac);

/**
 * ib_modify_qp_with_udata - Modifies the attributes for the specified QP.
 * @qp: The QP to modify.
 * @attr: On input, specifies the QP attributes to modify.  On output,
 *   the current values of selected QP attributes are returned.
 * @attr_mask: A bit-mask used to specify which attributes of the QP
 *   are being modified.
 * @udata: pointer to user's input output buffer information
 *   are being modified.
 * It returns 0 on success and returns appropriate error code on error.
 */
int ib_modify_qp_with_udata(struct ib_qp *qp, struct ib_qp_attr *attr,
			    int attr_mask, struct ib_udata *udata)
{
	int ret;

	if (attr_mask & IB_QP_AV) {
		ret = ib_resolve_eth_dmac(qp->device, &attr->ah_attr);
		if (ret)
			return ret;
	}
	return ib_security_modify_qp(qp, attr, attr_mask, udata);
}
EXPORT_SYMBOL(ib_modify_qp_with_udata);

int ib_modify_qp(struct ib_qp *qp,
		 struct ib_qp_attr *qp_attr,
		 int qp_attr_mask)
{
	return ib_modify_qp_with_udata(qp, qp_attr, qp_attr_mask, NULL);
}
EXPORT_SYMBOL(ib_modify_qp);

int ib_query_qp(struct ib_qp *qp,
		struct ib_qp_attr *qp_attr,
		int qp_attr_mask,
		struct ib_qp_init_attr *qp_init_attr)
{
	return qp->device->query_qp ?
		qp->device->query_qp(qp->real_qp, qp_attr, qp_attr_mask, qp_init_attr) :
		-ENOSYS;
}
EXPORT_SYMBOL(ib_query_qp);

int ib_close_qp(struct ib_qp *qp)
{
	struct ib_qp *real_qp;
	unsigned long flags;

	real_qp = qp->real_qp;
	if (real_qp == qp)
		return -EINVAL;

	spin_lock_irqsave(&real_qp->device->event_handler_lock, flags);
	list_del(&qp->open_list);
	spin_unlock_irqrestore(&real_qp->device->event_handler_lock, flags);

	atomic_dec(&real_qp->usecnt);
	ib_close_shared_qp_security(qp->qp_sec);
	kfree(qp);

	return 0;
}
EXPORT_SYMBOL(ib_close_qp);

static int __ib_destroy_shared_qp(struct ib_qp *qp)
{
	struct ib_xrcd *xrcd;
	struct ib_qp *real_qp;
	int ret;

	real_qp = qp->real_qp;
	xrcd = real_qp->xrcd;

	mutex_lock(&xrcd->tgt_qp_mutex);
	ib_close_qp(qp);
	if (atomic_read(&real_qp->usecnt) == 0)
		list_del(&real_qp->xrcd_list);
	else
		real_qp = NULL;
	mutex_unlock(&xrcd->tgt_qp_mutex);

	if (real_qp) {
		ret = ib_destroy_qp(real_qp);
		if (!ret)
			atomic_dec(&xrcd->usecnt);
		else
			__ib_insert_xrcd_qp(xrcd, real_qp);
	}

	return 0;
}

int ib_destroy_qp(struct ib_qp *qp)
{
	struct ib_pd *pd;
	struct ib_cq *scq, *rcq;
	struct ib_srq *srq;
	struct ib_rwq_ind_table *ind_tbl;
	struct ib_qp_security *sec;
	int ret;

	WARN_ON_ONCE(qp->mrs_used > 0);

	if (atomic_read(&qp->usecnt))
		return -EBUSY;

	if (qp->real_qp != qp)
		return __ib_destroy_shared_qp(qp);

	pd   = qp->pd;
	scq  = qp->send_cq;
	rcq  = qp->recv_cq;
	srq  = qp->srq;
	ind_tbl = qp->rwq_ind_tbl;
	sec  = qp->qp_sec;
	if (sec)
		ib_destroy_qp_security_begin(sec);

	if (!qp->uobject)
		rdma_rw_cleanup_mrs(qp);

	ret = qp->device->destroy_qp(qp);
	if (!ret) {
		if (pd)
			atomic_dec(&pd->usecnt);
		if (scq)
			atomic_dec(&scq->usecnt);
		if (rcq)
			atomic_dec(&rcq->usecnt);
		if (srq)
			atomic_dec(&srq->usecnt);
		if (ind_tbl)
			atomic_dec(&ind_tbl->usecnt);
		if (sec)
			ib_destroy_qp_security_end(sec);
	} else {
		if (sec)
			ib_destroy_qp_security_abort(sec);
	}

	return ret;
}
EXPORT_SYMBOL(ib_destroy_qp);

/* Completion queues */

struct ib_cq *ib_create_cq(struct ib_device *device,
			   ib_comp_handler comp_handler,
			   void (*event_handler)(struct ib_event *, void *),
			   void *cq_context,
			   const struct ib_cq_init_attr *cq_attr)
{
	struct ib_cq *cq;

	cq = device->create_cq(device, cq_attr, NULL, NULL);

	if (!IS_ERR(cq)) {
		cq->device        = device;
		cq->uobject       = NULL;
		cq->comp_handler  = comp_handler;
		cq->event_handler = event_handler;
		cq->cq_context    = cq_context;
		atomic_set(&cq->usecnt, 0);
	}

	return cq;
}
EXPORT_SYMBOL(ib_create_cq);

int ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period)
{
	return cq->device->modify_cq ?
		cq->device->modify_cq(cq, cq_count, cq_period) : -ENOSYS;
}
EXPORT_SYMBOL(ib_modify_cq);

int ib_destroy_cq(struct ib_cq *cq)
{
	if (atomic_read(&cq->usecnt))
		return -EBUSY;

	return cq->device->destroy_cq(cq);
}
EXPORT_SYMBOL(ib_destroy_cq);

int ib_resize_cq(struct ib_cq *cq, int cqe)
{
	return cq->device->resize_cq ?
		cq->device->resize_cq(cq, cqe, NULL) : -ENOSYS;
}
EXPORT_SYMBOL(ib_resize_cq);

/* Memory regions */

int ib_dereg_mr(struct ib_mr *mr)
{
	struct ib_pd *pd = mr->pd;
	int ret;

	ret = mr->device->dereg_mr(mr);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(ib_dereg_mr);

/**
 * ib_alloc_mr() - Allocates a memory region
 * @pd:            protection domain associated with the region
 * @mr_type:       memory region type
 * @max_num_sg:    maximum sg entries available for registration.
 *
 * Notes:
 * Memory registeration page/sg lists must not exceed max_num_sg.
 * For mr_type IB_MR_TYPE_MEM_REG, the total length cannot exceed
 * max_num_sg * used_page_size.
 *
 */
struct ib_mr *ib_alloc_mr(struct ib_pd *pd,
			  enum ib_mr_type mr_type,
			  u32 max_num_sg)
{
	struct ib_mr *mr;

	if (!pd->device->alloc_mr)
		return ERR_PTR(-ENOSYS);

	mr = pd->device->alloc_mr(pd, mr_type, max_num_sg);
	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		mr->uobject = NULL;
		atomic_inc(&pd->usecnt);
		mr->need_inval = false;
	}

	return mr;
}
EXPORT_SYMBOL(ib_alloc_mr);

/* "Fast" memory regions */

struct ib_fmr *ib_alloc_fmr(struct ib_pd *pd,
			    int mr_access_flags,
			    struct ib_fmr_attr *fmr_attr)
{
	struct ib_fmr *fmr;

	if (!pd->device->alloc_fmr)
		return ERR_PTR(-ENOSYS);

	fmr = pd->device->alloc_fmr(pd, mr_access_flags, fmr_attr);
	if (!IS_ERR(fmr)) {
		fmr->device = pd->device;
		fmr->pd     = pd;
		atomic_inc(&pd->usecnt);
	}

	return fmr;
}
EXPORT_SYMBOL(ib_alloc_fmr);

int ib_unmap_fmr(struct list_head *fmr_list)
{
	struct ib_fmr *fmr;

	if (list_empty(fmr_list))
		return 0;

	fmr = list_entry(fmr_list->next, struct ib_fmr, list);
	return fmr->device->unmap_fmr(fmr_list);
}
EXPORT_SYMBOL(ib_unmap_fmr);

int ib_dealloc_fmr(struct ib_fmr *fmr)
{
	struct ib_pd *pd;
	int ret;

	pd = fmr->pd;
	ret = fmr->device->dealloc_fmr(fmr);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(ib_dealloc_fmr);

/* Multicast groups */

int ib_attach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid)
{
	int ret;

	if (!qp->device->attach_mcast)
		return -ENOSYS;
	if (gid->raw[0] != 0xff || qp->qp_type != IB_QPT_UD ||
	    lid < be16_to_cpu(IB_MULTICAST_LID_BASE) ||
	    lid == be16_to_cpu(IB_LID_PERMISSIVE))
		return -EINVAL;

	ret = qp->device->attach_mcast(qp, gid, lid);
	if (!ret)
		atomic_inc(&qp->usecnt);
	return ret;
}
EXPORT_SYMBOL(ib_attach_mcast);

int ib_detach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid)
{
	int ret;

	if (!qp->device->detach_mcast)
		return -ENOSYS;
	if (gid->raw[0] != 0xff || qp->qp_type != IB_QPT_UD ||
	    lid < be16_to_cpu(IB_MULTICAST_LID_BASE) ||
	    lid == be16_to_cpu(IB_LID_PERMISSIVE))
		return -EINVAL;

	ret = qp->device->detach_mcast(qp, gid, lid);
	if (!ret)
		atomic_dec(&qp->usecnt);
	return ret;
}
EXPORT_SYMBOL(ib_detach_mcast);

struct ib_xrcd *ib_alloc_xrcd(struct ib_device *device)
{
	struct ib_xrcd *xrcd;

	if (!device->alloc_xrcd)
		return ERR_PTR(-ENOSYS);

	xrcd = device->alloc_xrcd(device, NULL, NULL);
	if (!IS_ERR(xrcd)) {
		xrcd->device = device;
		xrcd->inode = NULL;
		atomic_set(&xrcd->usecnt, 0);
		mutex_init(&xrcd->tgt_qp_mutex);
		INIT_LIST_HEAD(&xrcd->tgt_qp_list);
	}

	return xrcd;
}
EXPORT_SYMBOL(ib_alloc_xrcd);

int ib_dealloc_xrcd(struct ib_xrcd *xrcd)
{
	struct ib_qp *qp;
	int ret;

	if (atomic_read(&xrcd->usecnt))
		return -EBUSY;

	while (!list_empty(&xrcd->tgt_qp_list)) {
		qp = list_entry(xrcd->tgt_qp_list.next, struct ib_qp, xrcd_list);
		ret = ib_destroy_qp(qp);
		if (ret)
			return ret;
	}

	return xrcd->device->dealloc_xrcd(xrcd);
}
EXPORT_SYMBOL(ib_dealloc_xrcd);

/**
 * ib_create_wq - Creates a WQ associated with the specified protection
 * domain.
 * @pd: The protection domain associated with the WQ.
 * @wq_init_attr: A list of initial attributes required to create the
 * WQ. If WQ creation succeeds, then the attributes are updated to
 * the actual capabilities of the created WQ.
 *
 * wq_init_attr->max_wr and wq_init_attr->max_sge determine
 * the requested size of the WQ, and set to the actual values allocated
 * on return.
 * If ib_create_wq() succeeds, then max_wr and max_sge will always be
 * at least as large as the requested values.
 */
struct ib_wq *ib_create_wq(struct ib_pd *pd,
			   struct ib_wq_init_attr *wq_attr)
{
	struct ib_wq *wq;

	if (!pd->device->create_wq)
		return ERR_PTR(-ENOSYS);

	wq = pd->device->create_wq(pd, wq_attr, NULL);
	if (!IS_ERR(wq)) {
		wq->event_handler = wq_attr->event_handler;
		wq->wq_context = wq_attr->wq_context;
		wq->wq_type = wq_attr->wq_type;
		wq->cq = wq_attr->cq;
		wq->device = pd->device;
		wq->pd = pd;
		wq->uobject = NULL;
		atomic_inc(&pd->usecnt);
		atomic_inc(&wq_attr->cq->usecnt);
		atomic_set(&wq->usecnt, 0);
	}
	return wq;
}
EXPORT_SYMBOL(ib_create_wq);

/**
 * ib_destroy_wq - Destroys the specified WQ.
 * @wq: The WQ to destroy.
 */
int ib_destroy_wq(struct ib_wq *wq)
{
	int err;
	struct ib_cq *cq = wq->cq;
	struct ib_pd *pd = wq->pd;

	if (atomic_read(&wq->usecnt))
		return -EBUSY;

	err = wq->device->destroy_wq(wq);
	if (!err) {
		atomic_dec(&pd->usecnt);
		atomic_dec(&cq->usecnt);
	}
	return err;
}
EXPORT_SYMBOL(ib_destroy_wq);

/**
 * ib_modify_wq - Modifies the specified WQ.
 * @wq: The WQ to modify.
 * @wq_attr: On input, specifies the WQ attributes to modify.
 * @wq_attr_mask: A bit-mask used to specify which attributes of the WQ
 *   are being modified.
 * On output, the current values of selected WQ attributes are returned.
 */
int ib_modify_wq(struct ib_wq *wq, struct ib_wq_attr *wq_attr,
		 u32 wq_attr_mask)
{
	int err;

	if (!wq->device->modify_wq)
		return -ENOSYS;

	err = wq->device->modify_wq(wq, wq_attr, wq_attr_mask, NULL);
	return err;
}
EXPORT_SYMBOL(ib_modify_wq);

/*
 * ib_create_rwq_ind_table - Creates a RQ Indirection Table.
 * @device: The device on which to create the rwq indirection table.
 * @ib_rwq_ind_table_init_attr: A list of initial attributes required to
 * create the Indirection Table.
 *
 * Note: The life time of ib_rwq_ind_table_init_attr->ind_tbl is not less
 *	than the created ib_rwq_ind_table object and the caller is responsible
 *	for its memory allocation/free.
 */
struct ib_rwq_ind_table *ib_create_rwq_ind_table(struct ib_device *device,
						 struct ib_rwq_ind_table_init_attr *init_attr)
{
	struct ib_rwq_ind_table *rwq_ind_table;
	int i;
	u32 table_size;

	if (!device->create_rwq_ind_table)
		return ERR_PTR(-ENOSYS);

	table_size = (1 << init_attr->log_ind_tbl_size);
	rwq_ind_table = device->create_rwq_ind_table(device,
				init_attr, NULL);
	if (IS_ERR(rwq_ind_table))
		return rwq_ind_table;

	rwq_ind_table->ind_tbl = init_attr->ind_tbl;
	rwq_ind_table->log_ind_tbl_size = init_attr->log_ind_tbl_size;
	rwq_ind_table->device = device;
	rwq_ind_table->uobject = NULL;
	atomic_set(&rwq_ind_table->usecnt, 0);

	for (i = 0; i < table_size; i++)
		atomic_inc(&rwq_ind_table->ind_tbl[i]->usecnt);

	return rwq_ind_table;
}
EXPORT_SYMBOL(ib_create_rwq_ind_table);

/*
 * ib_destroy_rwq_ind_table - Destroys the specified Indirection Table.
 * @wq_ind_table: The Indirection Table to destroy.
*/
int ib_destroy_rwq_ind_table(struct ib_rwq_ind_table *rwq_ind_table)
{
	int err, i;
	u32 table_size = (1 << rwq_ind_table->log_ind_tbl_size);
	struct ib_wq **ind_tbl = rwq_ind_table->ind_tbl;

	if (atomic_read(&rwq_ind_table->usecnt))
		return -EBUSY;

	err = rwq_ind_table->device->destroy_rwq_ind_table(rwq_ind_table);
	if (!err) {
		for (i = 0; i < table_size; i++)
			atomic_dec(&ind_tbl[i]->usecnt);
	}

	return err;
}
EXPORT_SYMBOL(ib_destroy_rwq_ind_table);

struct ib_flow *ib_create_flow(struct ib_qp *qp,
			       struct ib_flow_attr *flow_attr,
			       int domain)
{
	struct ib_flow *flow_id;
	if (!qp->device->create_flow)
		return ERR_PTR(-ENOSYS);

	flow_id = qp->device->create_flow(qp, flow_attr, domain);
	if (!IS_ERR(flow_id)) {
		atomic_inc(&qp->usecnt);
		flow_id->qp = qp;
	}
	return flow_id;
}
EXPORT_SYMBOL(ib_create_flow);

int ib_destroy_flow(struct ib_flow *flow_id)
{
	int err;
	struct ib_qp *qp = flow_id->qp;

	err = qp->device->destroy_flow(flow_id);
	if (!err)
		atomic_dec(&qp->usecnt);
	return err;
}
EXPORT_SYMBOL(ib_destroy_flow);

int ib_check_mr_status(struct ib_mr *mr, u32 check_mask,
		       struct ib_mr_status *mr_status)
{
	return mr->device->check_mr_status ?
		mr->device->check_mr_status(mr, check_mask, mr_status) : -ENOSYS;
}
EXPORT_SYMBOL(ib_check_mr_status);

int ib_set_vf_link_state(struct ib_device *device, int vf, u8 port,
			 int state)
{
	if (!device->set_vf_link_state)
		return -ENOSYS;

	return device->set_vf_link_state(device, vf, port, state);
}
EXPORT_SYMBOL(ib_set_vf_link_state);

int ib_get_vf_config(struct ib_device *device, int vf, u8 port,
		     struct ifla_vf_info *info)
{
	if (!device->get_vf_config)
		return -ENOSYS;

	return device->get_vf_config(device, vf, port, info);
}
EXPORT_SYMBOL(ib_get_vf_config);

int ib_get_vf_stats(struct ib_device *device, int vf, u8 port,
		    struct ifla_vf_stats *stats)
{
	if (!device->get_vf_stats)
		return -ENOSYS;

	return device->get_vf_stats(device, vf, port, stats);
}
EXPORT_SYMBOL(ib_get_vf_stats);

int ib_set_vf_guid(struct ib_device *device, int vf, u8 port, u64 guid,
		   int type)
{
	if (!device->set_vf_guid)
		return -ENOSYS;

	return device->set_vf_guid(device, vf, port, guid, type);
}
EXPORT_SYMBOL(ib_set_vf_guid);

/**
 * ib_map_mr_sg() - Map the largest prefix of a dma mapped SG list
 *     and set it the memory region.
 * @mr:            memory region
 * @sg:            dma mapped scatterlist
 * @sg_nents:      number of entries in sg
 * @sg_offset:     offset in bytes into sg
 * @page_size:     page vector desired page size
 *
 * Constraints:
 * - The first sg element is allowed to have an offset.
 * - Each sg element must either be aligned to page_size or virtually
 *   contiguous to the previous element. In case an sg element has a
 *   non-contiguous offset, the mapping prefix will not include it.
 * - The last sg element is allowed to have length less than page_size.
 * - If sg_nents total byte length exceeds the mr max_num_sge * page_size
 *   then only max_num_sg entries will be mapped.
 * - If the MR was allocated with type IB_MR_TYPE_SG_GAPS, none of these
 *   constraints holds and the page_size argument is ignored.
 *
 * Returns the number of sg elements that were mapped to the memory region.
 *
 * After this completes successfully, the  memory region
 * is ready for registration.
 */
int ib_map_mr_sg(struct ib_mr *mr, struct scatterlist *sg, int sg_nents,
		 unsigned int *sg_offset, unsigned int page_size)
{
	if (unlikely(!mr->device->map_mr_sg))
		return -ENOSYS;

	mr->page_size = page_size;

	return mr->device->map_mr_sg(mr, sg, sg_nents, sg_offset);
}
EXPORT_SYMBOL(ib_map_mr_sg);

/**
 * ib_sg_to_pages() - Convert the largest prefix of a sg list
 *     to a page vector
 * @mr:            memory region
 * @sgl:           dma mapped scatterlist
 * @sg_nents:      number of entries in sg
 * @sg_offset_p:   IN:  start offset in bytes into sg
 *                 OUT: offset in bytes for element n of the sg of the first
 *                      byte that has not been processed where n is the return
 *                      value of this function.
 * @set_page:      driver page assignment function pointer
 *
 * Core service helper for drivers to convert the largest
 * prefix of given sg list to a page vector. The sg list
 * prefix converted is the prefix that meet the requirements
 * of ib_map_mr_sg.
 *
 * Returns the number of sg elements that were assigned to
 * a page vector.
 */
int ib_sg_to_pages(struct ib_mr *mr, struct scatterlist *sgl, int sg_nents,
		unsigned int *sg_offset_p, int (*set_page)(struct ib_mr *, u64))
{
	struct scatterlist *sg;
	u64 last_end_dma_addr = 0;
	unsigned int sg_offset = sg_offset_p ? *sg_offset_p : 0;
	unsigned int last_page_off = 0;
	u64 page_mask = ~((u64)mr->page_size - 1);
	int i, ret;

	if (unlikely(sg_nents <= 0 || sg_offset > sg_dma_len(&sgl[0])))
		return -EINVAL;

	mr->iova = sg_dma_address(&sgl[0]) + sg_offset;
	mr->length = 0;

	for_each_sg(sgl, sg, sg_nents, i) {
		u64 dma_addr = sg_dma_address(sg) + sg_offset;
		u64 prev_addr = dma_addr;
		unsigned int dma_len = sg_dma_len(sg) - sg_offset;
		u64 end_dma_addr = dma_addr + dma_len;
		u64 page_addr = dma_addr & page_mask;

		/*
		 * For the second and later elements, check whether either the
		 * end of element i-1 or the start of element i is not aligned
		 * on a page boundary.
		 */
		if (i && (last_page_off != 0 || page_addr != dma_addr)) {
			/* Stop mapping if there is a gap. */
			if (last_end_dma_addr != dma_addr)
				break;

			/*
			 * Coalesce this element with the last. If it is small
			 * enough just update mr->length. Otherwise start
			 * mapping from the next page.
			 */
			goto next_page;
		}

		do {
			ret = set_page(mr, page_addr);
			if (unlikely(ret < 0)) {
				sg_offset = prev_addr - sg_dma_address(sg);
				mr->length += prev_addr - dma_addr;
				if (sg_offset_p)
					*sg_offset_p = sg_offset;
				return i || sg_offset ? i : ret;
			}
			prev_addr = page_addr;
next_page:
			page_addr += mr->page_size;
		} while (page_addr < end_dma_addr);

		mr->length += dma_len;
		last_end_dma_addr = end_dma_addr;
		last_page_off = end_dma_addr & ~page_mask;

		sg_offset = 0;
	}

	if (sg_offset_p)
		*sg_offset_p = 0;
	return i;
}
EXPORT_SYMBOL(ib_sg_to_pages);

struct ib_drain_cqe {
	struct ib_cqe cqe;
	struct completion done;
};

static void ib_drain_qp_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_drain_cqe *cqe = container_of(wc->wr_cqe, struct ib_drain_cqe,
						cqe);

	complete(&cqe->done);
}

/*
 * Post a WR and block until its completion is reaped for the SQ.
 */
static void __ib_drain_sq(struct ib_qp *qp)
{
	struct ib_cq *cq = qp->send_cq;
	struct ib_qp_attr attr = { .qp_state = IB_QPS_ERR };
	struct ib_drain_cqe sdrain;
	struct ib_send_wr swr = {}, *bad_swr;
	int ret;

	swr.wr_cqe = &sdrain.cqe;
	sdrain.cqe.done = ib_drain_qp_done;
	init_completion(&sdrain.done);

	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret) {
		WARN_ONCE(ret, "failed to drain send queue: %d\n", ret);
		return;
	}

	ret = ib_post_send(qp, &swr, &bad_swr);
	if (ret) {
		WARN_ONCE(ret, "failed to drain send queue: %d\n", ret);
		return;
	}

	if (cq->poll_ctx == IB_POLL_DIRECT)
		while (wait_for_completion_timeout(&sdrain.done, HZ / 10) <= 0)
			ib_process_cq_direct(cq, -1);
	else
		wait_for_completion(&sdrain.done);
}

/*
 * Post a WR and block until its completion is reaped for the RQ.
 */
static void __ib_drain_rq(struct ib_qp *qp)
{
	struct ib_cq *cq = qp->recv_cq;
	struct ib_qp_attr attr = { .qp_state = IB_QPS_ERR };
	struct ib_drain_cqe rdrain;
	struct ib_recv_wr rwr = {}, *bad_rwr;
	int ret;

	rwr.wr_cqe = &rdrain.cqe;
	rdrain.cqe.done = ib_drain_qp_done;
	init_completion(&rdrain.done);

	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret) {
		WARN_ONCE(ret, "failed to drain recv queue: %d\n", ret);
		return;
	}

	ret = ib_post_recv(qp, &rwr, &bad_rwr);
	if (ret) {
		WARN_ONCE(ret, "failed to drain recv queue: %d\n", ret);
		return;
	}

	if (cq->poll_ctx == IB_POLL_DIRECT)
		while (wait_for_completion_timeout(&rdrain.done, HZ / 10) <= 0)
			ib_process_cq_direct(cq, -1);
	else
		wait_for_completion(&rdrain.done);
}

/**
 * ib_drain_sq() - Block until all SQ CQEs have been consumed by the
 *		   application.
 * @qp:            queue pair to drain
 *
 * If the device has a provider-specific drain function, then
 * call that.  Otherwise call the generic drain function
 * __ib_drain_sq().
 *
 * The caller must:
 *
 * ensure there is room in the CQ and SQ for the drain work request and
 * completion.
 *
 * allocate the CQ using ib_alloc_cq().
 *
 * ensure that there are no other contexts that are posting WRs concurrently.
 * Otherwise the drain is not guaranteed.
 */
void ib_drain_sq(struct ib_qp *qp)
{
	if (qp->device->drain_sq)
		qp->device->drain_sq(qp);
	else
		__ib_drain_sq(qp);
}
EXPORT_SYMBOL(ib_drain_sq);

/**
 * ib_drain_rq() - Block until all RQ CQEs have been consumed by the
 *		   application.
 * @qp:            queue pair to drain
 *
 * If the device has a provider-specific drain function, then
 * call that.  Otherwise call the generic drain function
 * __ib_drain_rq().
 *
 * The caller must:
 *
 * ensure there is room in the CQ and RQ for the drain work request and
 * completion.
 *
 * allocate the CQ using ib_alloc_cq().
 *
 * ensure that there are no other contexts that are posting WRs concurrently.
 * Otherwise the drain is not guaranteed.
 */
void ib_drain_rq(struct ib_qp *qp)
{
	if (qp->device->drain_rq)
		qp->device->drain_rq(qp);
	else
		__ib_drain_rq(qp);
}
EXPORT_SYMBOL(ib_drain_rq);

/**
 * ib_drain_qp() - Block until all CQEs have been consumed by the
 *		   application on both the RQ and SQ.
 * @qp:            queue pair to drain
 *
 * The caller must:
 *
 * ensure there is room in the CQ(s), SQ, and RQ for drain work requests
 * and completions.
 *
 * allocate the CQs using ib_alloc_cq().
 *
 * ensure that there are no other contexts that are posting WRs concurrently.
 * Otherwise the drain is not guaranteed.
 */
void ib_drain_qp(struct ib_qp *qp)
{
	ib_drain_sq(qp);
	if (!qp->srq)
		ib_drain_rq(qp);
}
EXPORT_SYMBOL(ib_drain_qp);
