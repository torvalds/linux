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
#include <rdma/lag.h>

#include "core_priv.h"
#include <trace/events/rdma_core.h>

static int ib_resolve_eth_dmac(struct ib_device *device,
			       struct rdma_ah_attr *ah_attr);

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
	[IB_WC_MW_BIND_ERR]		= "memory bind operation error",
	[IB_WC_BAD_RESP_ERR]		= "bad response error",
	[IB_WC_LOC_ACCESS_ERR]		= "local access error",
	[IB_WC_REM_INV_REQ_ERR]		= "remote invalid request error",
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
	case IB_RATE_2_5_GBPS: return   1;
	case IB_RATE_5_GBPS:   return   2;
	case IB_RATE_10_GBPS:  return   4;
	case IB_RATE_20_GBPS:  return   8;
	case IB_RATE_30_GBPS:  return  12;
	case IB_RATE_40_GBPS:  return  16;
	case IB_RATE_60_GBPS:  return  24;
	case IB_RATE_80_GBPS:  return  32;
	case IB_RATE_120_GBPS: return  48;
	case IB_RATE_14_GBPS:  return   6;
	case IB_RATE_56_GBPS:  return  22;
	case IB_RATE_112_GBPS: return  45;
	case IB_RATE_168_GBPS: return  67;
	case IB_RATE_25_GBPS:  return  10;
	case IB_RATE_100_GBPS: return  40;
	case IB_RATE_200_GBPS: return  80;
	case IB_RATE_300_GBPS: return 120;
	case IB_RATE_28_GBPS:  return  11;
	case IB_RATE_50_GBPS:  return  20;
	case IB_RATE_400_GBPS: return 160;
	case IB_RATE_600_GBPS: return 240;
	default:	       return  -1;
	}
}
EXPORT_SYMBOL(ib_rate_to_mult);

__attribute_const__ enum ib_rate mult_to_ib_rate(int mult)
{
	switch (mult) {
	case 1:   return IB_RATE_2_5_GBPS;
	case 2:   return IB_RATE_5_GBPS;
	case 4:   return IB_RATE_10_GBPS;
	case 8:   return IB_RATE_20_GBPS;
	case 12:  return IB_RATE_30_GBPS;
	case 16:  return IB_RATE_40_GBPS;
	case 24:  return IB_RATE_60_GBPS;
	case 32:  return IB_RATE_80_GBPS;
	case 48:  return IB_RATE_120_GBPS;
	case 6:   return IB_RATE_14_GBPS;
	case 22:  return IB_RATE_56_GBPS;
	case 45:  return IB_RATE_112_GBPS;
	case 67:  return IB_RATE_168_GBPS;
	case 10:  return IB_RATE_25_GBPS;
	case 40:  return IB_RATE_100_GBPS;
	case 80:  return IB_RATE_200_GBPS;
	case 120: return IB_RATE_300_GBPS;
	case 11:  return IB_RATE_28_GBPS;
	case 20:  return IB_RATE_50_GBPS;
	case 160: return IB_RATE_400_GBPS;
	case 240: return IB_RATE_600_GBPS;
	default:  return IB_RATE_PORT_CURRENT;
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
	case IB_RATE_28_GBPS:  return 28125;
	case IB_RATE_50_GBPS:  return 53125;
	case IB_RATE_400_GBPS: return 425000;
	case IB_RATE_600_GBPS: return 637500;
	default:	       return -1;
	}
}
EXPORT_SYMBOL(ib_rate_to_mbps);

__attribute_const__ enum rdma_transport_type
rdma_node_get_transport(unsigned int node_type)
{

	if (node_type == RDMA_NODE_USNIC)
		return RDMA_TRANSPORT_USNIC;
	if (node_type == RDMA_NODE_USNIC_UDP)
		return RDMA_TRANSPORT_USNIC_UDP;
	if (node_type == RDMA_NODE_RNIC)
		return RDMA_TRANSPORT_IWARP;
	if (node_type == RDMA_NODE_UNSPECIFIED)
		return RDMA_TRANSPORT_UNSPECIFIED;

	return RDMA_TRANSPORT_IB;
}
EXPORT_SYMBOL(rdma_node_get_transport);

enum rdma_link_layer rdma_port_get_link_layer(struct ib_device *device,
					      u32 port_num)
{
	enum rdma_transport_type lt;
	if (device->ops.get_link_layer)
		return device->ops.get_link_layer(device, port_num);

	lt = rdma_node_get_transport(device->node_type);
	if (lt == RDMA_TRANSPORT_IB)
		return IB_LINK_LAYER_INFINIBAND;

	return IB_LINK_LAYER_ETHERNET;
}
EXPORT_SYMBOL(rdma_port_get_link_layer);

/* Protection domains */

/**
 * __ib_alloc_pd - Allocates an unused protection domain.
 * @device: The device on which to allocate the protection domain.
 * @flags: protection domain flags
 * @caller: caller's build-time module name
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
	int ret;

	pd = rdma_zalloc_drv_obj(device, ib_pd);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	pd->device = device;
	pd->uobject = NULL;
	pd->__internal_mr = NULL;
	atomic_set(&pd->usecnt, 0);
	pd->flags = flags;

	rdma_restrack_new(&pd->res, RDMA_RESTRACK_PD);
	rdma_restrack_set_name(&pd->res, caller);

	ret = device->ops.alloc_pd(pd, NULL);
	if (ret) {
		rdma_restrack_put(&pd->res);
		kfree(pd);
		return ERR_PTR(ret);
	}
	rdma_restrack_add(&pd->res);

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

		mr = pd->device->ops.get_dma_mr(pd, mr_access_flags);
		if (IS_ERR(mr)) {
			ib_dealloc_pd(pd);
			return ERR_CAST(mr);
		}

		mr->device	= pd->device;
		mr->pd		= pd;
		mr->type        = IB_MR_TYPE_DMA;
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
 * ib_dealloc_pd_user - Deallocates a protection domain.
 * @pd: The protection domain to deallocate.
 * @udata: Valid user data or NULL for kernel object
 *
 * It is an error to call this function while any resources in the pd still
 * exist.  The caller is responsible to synchronously destroy them and
 * guarantee no new allocations will happen.
 */
int ib_dealloc_pd_user(struct ib_pd *pd, struct ib_udata *udata)
{
	int ret;

	if (pd->__internal_mr) {
		ret = pd->device->ops.dereg_mr(pd->__internal_mr, NULL);
		WARN_ON(ret);
		pd->__internal_mr = NULL;
	}

	/* uverbs manipulates usecnt with proper locking, while the kabi
	 * requires the caller to guarantee we can't race here.
	 */
	WARN_ON(atomic_read(&pd->usecnt));

	ret = pd->device->ops.dealloc_pd(pd, udata);
	if (ret)
		return ret;

	rdma_restrack_del(&pd->res);
	kfree(pd);
	return ret;
}
EXPORT_SYMBOL(ib_dealloc_pd_user);

/* Address handles */

/**
 * rdma_copy_ah_attr - Copy rdma ah attribute from source to destination.
 * @dest:       Pointer to destination ah_attr. Contents of the destination
 *              pointer is assumed to be invalid and attribute are overwritten.
 * @src:        Pointer to source ah_attr.
 */
void rdma_copy_ah_attr(struct rdma_ah_attr *dest,
		       const struct rdma_ah_attr *src)
{
	*dest = *src;
	if (dest->grh.sgid_attr)
		rdma_hold_gid_attr(dest->grh.sgid_attr);
}
EXPORT_SYMBOL(rdma_copy_ah_attr);

/**
 * rdma_replace_ah_attr - Replace valid ah_attr with new new one.
 * @old:        Pointer to existing ah_attr which needs to be replaced.
 *              old is assumed to be valid or zero'd
 * @new:        Pointer to the new ah_attr.
 *
 * rdma_replace_ah_attr() first releases any reference in the old ah_attr if
 * old the ah_attr is valid; after that it copies the new attribute and holds
 * the reference to the replaced ah_attr.
 */
void rdma_replace_ah_attr(struct rdma_ah_attr *old,
			  const struct rdma_ah_attr *new)
{
	rdma_destroy_ah_attr(old);
	*old = *new;
	if (old->grh.sgid_attr)
		rdma_hold_gid_attr(old->grh.sgid_attr);
}
EXPORT_SYMBOL(rdma_replace_ah_attr);

/**
 * rdma_move_ah_attr - Move ah_attr pointed by source to destination.
 * @dest:       Pointer to destination ah_attr to copy to.
 *              dest is assumed to be valid or zero'd
 * @src:        Pointer to the new ah_attr.
 *
 * rdma_move_ah_attr() first releases any reference in the destination ah_attr
 * if it is valid. This also transfers ownership of internal references from
 * src to dest, making src invalid in the process. No new reference of the src
 * ah_attr is taken.
 */
void rdma_move_ah_attr(struct rdma_ah_attr *dest, struct rdma_ah_attr *src)
{
	rdma_destroy_ah_attr(dest);
	*dest = *src;
	src->grh.sgid_attr = NULL;
}
EXPORT_SYMBOL(rdma_move_ah_attr);

/*
 * Validate that the rdma_ah_attr is valid for the device before passing it
 * off to the driver.
 */
static int rdma_check_ah_attr(struct ib_device *device,
			      struct rdma_ah_attr *ah_attr)
{
	if (!rdma_is_port_valid(device, ah_attr->port_num))
		return -EINVAL;

	if ((rdma_is_grh_required(device, ah_attr->port_num) ||
	     ah_attr->type == RDMA_AH_ATTR_TYPE_ROCE) &&
	    !(ah_attr->ah_flags & IB_AH_GRH))
		return -EINVAL;

	if (ah_attr->grh.sgid_attr) {
		/*
		 * Make sure the passed sgid_attr is consistent with the
		 * parameters
		 */
		if (ah_attr->grh.sgid_attr->index != ah_attr->grh.sgid_index ||
		    ah_attr->grh.sgid_attr->port_num != ah_attr->port_num)
			return -EINVAL;
	}
	return 0;
}

/*
 * If the ah requires a GRH then ensure that sgid_attr pointer is filled in.
 * On success the caller is responsible to call rdma_unfill_sgid_attr().
 */
static int rdma_fill_sgid_attr(struct ib_device *device,
			       struct rdma_ah_attr *ah_attr,
			       const struct ib_gid_attr **old_sgid_attr)
{
	const struct ib_gid_attr *sgid_attr;
	struct ib_global_route *grh;
	int ret;

	*old_sgid_attr = ah_attr->grh.sgid_attr;

	ret = rdma_check_ah_attr(device, ah_attr);
	if (ret)
		return ret;

	if (!(ah_attr->ah_flags & IB_AH_GRH))
		return 0;

	grh = rdma_ah_retrieve_grh(ah_attr);
	if (grh->sgid_attr)
		return 0;

	sgid_attr =
		rdma_get_gid_attr(device, ah_attr->port_num, grh->sgid_index);
	if (IS_ERR(sgid_attr))
		return PTR_ERR(sgid_attr);

	/* Move ownerhip of the kref into the ah_attr */
	grh->sgid_attr = sgid_attr;
	return 0;
}

static void rdma_unfill_sgid_attr(struct rdma_ah_attr *ah_attr,
				  const struct ib_gid_attr *old_sgid_attr)
{
	/*
	 * Fill didn't change anything, the caller retains ownership of
	 * whatever it passed
	 */
	if (ah_attr->grh.sgid_attr == old_sgid_attr)
		return;

	/*
	 * Otherwise, we need to undo what rdma_fill_sgid_attr so the caller
	 * doesn't see any change in the rdma_ah_attr. If we get here
	 * old_sgid_attr is NULL.
	 */
	rdma_destroy_ah_attr(ah_attr);
}

static const struct ib_gid_attr *
rdma_update_sgid_attr(struct rdma_ah_attr *ah_attr,
		      const struct ib_gid_attr *old_attr)
{
	if (old_attr)
		rdma_put_gid_attr(old_attr);
	if (ah_attr->ah_flags & IB_AH_GRH) {
		rdma_hold_gid_attr(ah_attr->grh.sgid_attr);
		return ah_attr->grh.sgid_attr;
	}
	return NULL;
}

static struct ib_ah *_rdma_create_ah(struct ib_pd *pd,
				     struct rdma_ah_attr *ah_attr,
				     u32 flags,
				     struct ib_udata *udata,
				     struct net_device *xmit_slave)
{
	struct rdma_ah_init_attr init_attr = {};
	struct ib_device *device = pd->device;
	struct ib_ah *ah;
	int ret;

	might_sleep_if(flags & RDMA_CREATE_AH_SLEEPABLE);

	if (!udata && !device->ops.create_ah)
		return ERR_PTR(-EOPNOTSUPP);

	ah = rdma_zalloc_drv_obj_gfp(
		device, ib_ah,
		(flags & RDMA_CREATE_AH_SLEEPABLE) ? GFP_KERNEL : GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	ah->device = device;
	ah->pd = pd;
	ah->type = ah_attr->type;
	ah->sgid_attr = rdma_update_sgid_attr(ah_attr, NULL);
	init_attr.ah_attr = ah_attr;
	init_attr.flags = flags;
	init_attr.xmit_slave = xmit_slave;

	if (udata)
		ret = device->ops.create_user_ah(ah, &init_attr, udata);
	else
		ret = device->ops.create_ah(ah, &init_attr, NULL);
	if (ret) {
		kfree(ah);
		return ERR_PTR(ret);
	}

	atomic_inc(&pd->usecnt);
	return ah;
}

/**
 * rdma_create_ah - Creates an address handle for the
 * given address vector.
 * @pd: The protection domain associated with the address handle.
 * @ah_attr: The attributes of the address vector.
 * @flags: Create address handle flags (see enum rdma_create_ah_flags).
 *
 * It returns 0 on success and returns appropriate error code on error.
 * The address handle is used to reference a local or global destination
 * in all UD QP post sends.
 */
struct ib_ah *rdma_create_ah(struct ib_pd *pd, struct rdma_ah_attr *ah_attr,
			     u32 flags)
{
	const struct ib_gid_attr *old_sgid_attr;
	struct net_device *slave;
	struct ib_ah *ah;
	int ret;

	ret = rdma_fill_sgid_attr(pd->device, ah_attr, &old_sgid_attr);
	if (ret)
		return ERR_PTR(ret);
	slave = rdma_lag_get_ah_roce_slave(pd->device, ah_attr,
					   (flags & RDMA_CREATE_AH_SLEEPABLE) ?
					   GFP_KERNEL : GFP_ATOMIC);
	if (IS_ERR(slave)) {
		rdma_unfill_sgid_attr(ah_attr, old_sgid_attr);
		return (void *)slave;
	}
	ah = _rdma_create_ah(pd, ah_attr, flags, NULL, slave);
	rdma_lag_put_ah_roce_slave(slave);
	rdma_unfill_sgid_attr(ah_attr, old_sgid_attr);
	return ah;
}
EXPORT_SYMBOL(rdma_create_ah);

/**
 * rdma_create_user_ah - Creates an address handle for the
 * given address vector.
 * It resolves destination mac address for ah attribute of RoCE type.
 * @pd: The protection domain associated with the address handle.
 * @ah_attr: The attributes of the address vector.
 * @udata: pointer to user's input output buffer information need by
 *         provider driver.
 *
 * It returns 0 on success and returns appropriate error code on error.
 * The address handle is used to reference a local or global destination
 * in all UD QP post sends.
 */
struct ib_ah *rdma_create_user_ah(struct ib_pd *pd,
				  struct rdma_ah_attr *ah_attr,
				  struct ib_udata *udata)
{
	const struct ib_gid_attr *old_sgid_attr;
	struct ib_ah *ah;
	int err;

	err = rdma_fill_sgid_attr(pd->device, ah_attr, &old_sgid_attr);
	if (err)
		return ERR_PTR(err);

	if (ah_attr->type == RDMA_AH_ATTR_TYPE_ROCE) {
		err = ib_resolve_eth_dmac(pd->device, ah_attr);
		if (err) {
			ah = ERR_PTR(err);
			goto out;
		}
	}

	ah = _rdma_create_ah(pd, ah_attr, RDMA_CREATE_AH_SLEEPABLE,
			     udata, NULL);

out:
	rdma_unfill_sgid_attr(ah_attr, old_sgid_attr);
	return ah;
}
EXPORT_SYMBOL(rdma_create_user_ah);

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
						     u32 port_num,
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
	struct find_gid_index_context *ctx = context;
	u16 vlan_id = 0xffff;
	int ret;

	if (ctx->gid_type != gid_attr->gid_type)
		return false;

	ret = rdma_read_gid_l2_fields(gid_attr, &vlan_id, NULL);
	if (ret)
		return false;

	return ctx->vlan_id == vlan_id;
}

static const struct ib_gid_attr *
get_sgid_attr_from_eth(struct ib_device *device, u32 port_num,
		       u16 vlan_id, const union ib_gid *sgid,
		       enum ib_gid_type gid_type)
{
	struct find_gid_index_context context = {.vlan_id = vlan_id,
						 .gid_type = gid_type};

	return rdma_find_gid_by_filter(device, sgid, port_num, find_gid_index,
				       &context);
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
		   net_type == RDMA_NETWORK_IB || RDMA_NETWORK_ROCE_V1) {
		*dgid = hdr->ibgrh.dgid;
		*sgid = hdr->ibgrh.sgid;
		return 0;
	} else {
		return -EINVAL;
	}
}
EXPORT_SYMBOL(ib_get_gids_from_rdma_hdr);

/* Resolve destination mac address and hop limit for unicast destination
 * GID entry, considering the source GID entry as well.
 * ah_attribute must have have valid port_num, sgid_index.
 */
static int ib_resolve_unicast_gid_dmac(struct ib_device *device,
				       struct rdma_ah_attr *ah_attr)
{
	struct ib_global_route *grh = rdma_ah_retrieve_grh(ah_attr);
	const struct ib_gid_attr *sgid_attr = grh->sgid_attr;
	int hop_limit = 0xff;
	int ret = 0;

	/* If destination is link local and source GID is RoCEv1,
	 * IP stack is not used.
	 */
	if (rdma_link_local_addr((struct in6_addr *)grh->dgid.raw) &&
	    sgid_attr->gid_type == IB_GID_TYPE_ROCE) {
		rdma_get_ll_mac((struct in6_addr *)grh->dgid.raw,
				ah_attr->roce.dmac);
		return ret;
	}

	ret = rdma_addr_find_l2_eth_by_grh(&sgid_attr->gid, &grh->dgid,
					   ah_attr->roce.dmac,
					   sgid_attr, &hop_limit);

	grh->hop_limit = hop_limit;
	return ret;
}

/*
 * This function initializes address handle attributes from the incoming packet.
 * Incoming packet has dgid of the receiver node on which this code is
 * getting executed and, sgid contains the GID of the sender.
 *
 * When resolving mac address of destination, the arrived dgid is used
 * as sgid and, sgid is used as dgid because sgid contains destinations
 * GID whom to respond to.
 *
 * On success the caller is responsible to call rdma_destroy_ah_attr on the
 * attr.
 */
int ib_init_ah_attr_from_wc(struct ib_device *device, u32 port_num,
			    const struct ib_wc *wc, const struct ib_grh *grh,
			    struct rdma_ah_attr *ah_attr)
{
	u32 flow_class;
	int ret;
	enum rdma_network_type net_type = RDMA_NETWORK_IB;
	enum ib_gid_type gid_type = IB_GID_TYPE_IB;
	const struct ib_gid_attr *sgid_attr;
	int hoplimit = 0xff;
	union ib_gid dgid;
	union ib_gid sgid;

	might_sleep();

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

	rdma_ah_set_sl(ah_attr, wc->sl);
	rdma_ah_set_port_num(ah_attr, port_num);

	if (rdma_protocol_roce(device, port_num)) {
		u16 vlan_id = wc->wc_flags & IB_WC_WITH_VLAN ?
				wc->vlan_id : 0xffff;

		if (!(wc->wc_flags & IB_WC_GRH))
			return -EPROTOTYPE;

		sgid_attr = get_sgid_attr_from_eth(device, port_num,
						   vlan_id, &dgid,
						   gid_type);
		if (IS_ERR(sgid_attr))
			return PTR_ERR(sgid_attr);

		flow_class = be32_to_cpu(grh->version_tclass_flow);
		rdma_move_grh_sgid_attr(ah_attr,
					&sgid,
					flow_class & 0xFFFFF,
					hoplimit,
					(flow_class >> 20) & 0xFF,
					sgid_attr);

		ret = ib_resolve_unicast_gid_dmac(device, ah_attr);
		if (ret)
			rdma_destroy_ah_attr(ah_attr);

		return ret;
	} else {
		rdma_ah_set_dlid(ah_attr, wc->slid);
		rdma_ah_set_path_bits(ah_attr, wc->dlid_path_bits);

		if ((wc->wc_flags & IB_WC_GRH) == 0)
			return 0;

		if (dgid.global.interface_id !=
					cpu_to_be64(IB_SA_WELL_KNOWN_GUID)) {
			sgid_attr = rdma_find_gid_by_port(
				device, &dgid, IB_GID_TYPE_IB, port_num, NULL);
		} else
			sgid_attr = rdma_get_gid_attr(device, port_num, 0);

		if (IS_ERR(sgid_attr))
			return PTR_ERR(sgid_attr);
		flow_class = be32_to_cpu(grh->version_tclass_flow);
		rdma_move_grh_sgid_attr(ah_attr,
					&sgid,
					flow_class & 0xFFFFF,
					hoplimit,
					(flow_class >> 20) & 0xFF,
					sgid_attr);

		return 0;
	}
}
EXPORT_SYMBOL(ib_init_ah_attr_from_wc);

/**
 * rdma_move_grh_sgid_attr - Sets the sgid attribute of GRH, taking ownership
 * of the reference
 *
 * @attr:	Pointer to AH attribute structure
 * @dgid:	Destination GID
 * @flow_label:	Flow label
 * @hop_limit:	Hop limit
 * @traffic_class: traffic class
 * @sgid_attr:	Pointer to SGID attribute
 *
 * This takes ownership of the sgid_attr reference. The caller must ensure
 * rdma_destroy_ah_attr() is called before destroying the rdma_ah_attr after
 * calling this function.
 */
void rdma_move_grh_sgid_attr(struct rdma_ah_attr *attr, union ib_gid *dgid,
			     u32 flow_label, u8 hop_limit, u8 traffic_class,
			     const struct ib_gid_attr *sgid_attr)
{
	rdma_ah_set_grh(attr, dgid, flow_label, sgid_attr->index, hop_limit,
			traffic_class);
	attr->grh.sgid_attr = sgid_attr;
}
EXPORT_SYMBOL(rdma_move_grh_sgid_attr);

/**
 * rdma_destroy_ah_attr - Release reference to SGID attribute of
 * ah attribute.
 * @ah_attr: Pointer to ah attribute
 *
 * Release reference to the SGID attribute of the ah attribute if it is
 * non NULL. It is safe to call this multiple times, and safe to call it on
 * a zero initialized ah_attr.
 */
void rdma_destroy_ah_attr(struct rdma_ah_attr *ah_attr)
{
	if (ah_attr->grh.sgid_attr) {
		rdma_put_gid_attr(ah_attr->grh.sgid_attr);
		ah_attr->grh.sgid_attr = NULL;
	}
}
EXPORT_SYMBOL(rdma_destroy_ah_attr);

struct ib_ah *ib_create_ah_from_wc(struct ib_pd *pd, const struct ib_wc *wc,
				   const struct ib_grh *grh, u32 port_num)
{
	struct rdma_ah_attr ah_attr;
	struct ib_ah *ah;
	int ret;

	ret = ib_init_ah_attr_from_wc(pd->device, port_num, wc, grh, &ah_attr);
	if (ret)
		return ERR_PTR(ret);

	ah = rdma_create_ah(pd, &ah_attr, RDMA_CREATE_AH_SLEEPABLE);

	rdma_destroy_ah_attr(&ah_attr);
	return ah;
}
EXPORT_SYMBOL(ib_create_ah_from_wc);

int rdma_modify_ah(struct ib_ah *ah, struct rdma_ah_attr *ah_attr)
{
	const struct ib_gid_attr *old_sgid_attr;
	int ret;

	if (ah->type != ah_attr->type)
		return -EINVAL;

	ret = rdma_fill_sgid_attr(ah->device, ah_attr, &old_sgid_attr);
	if (ret)
		return ret;

	ret = ah->device->ops.modify_ah ?
		ah->device->ops.modify_ah(ah, ah_attr) :
		-EOPNOTSUPP;

	ah->sgid_attr = rdma_update_sgid_attr(ah_attr, ah->sgid_attr);
	rdma_unfill_sgid_attr(ah_attr, old_sgid_attr);
	return ret;
}
EXPORT_SYMBOL(rdma_modify_ah);

int rdma_query_ah(struct ib_ah *ah, struct rdma_ah_attr *ah_attr)
{
	ah_attr->grh.sgid_attr = NULL;

	return ah->device->ops.query_ah ?
		ah->device->ops.query_ah(ah, ah_attr) :
		-EOPNOTSUPP;
}
EXPORT_SYMBOL(rdma_query_ah);

int rdma_destroy_ah_user(struct ib_ah *ah, u32 flags, struct ib_udata *udata)
{
	const struct ib_gid_attr *sgid_attr = ah->sgid_attr;
	struct ib_pd *pd;
	int ret;

	might_sleep_if(flags & RDMA_DESTROY_AH_SLEEPABLE);

	pd = ah->pd;

	ret = ah->device->ops.destroy_ah(ah, flags);
	if (ret)
		return ret;

	atomic_dec(&pd->usecnt);
	if (sgid_attr)
		rdma_put_gid_attr(sgid_attr);

	kfree(ah);
	return ret;
}
EXPORT_SYMBOL(rdma_destroy_ah_user);

/* Shared receive queues */

/**
 * ib_create_srq_user - Creates a SRQ associated with the specified protection
 *   domain.
 * @pd: The protection domain associated with the SRQ.
 * @srq_init_attr: A list of initial attributes required to create the
 *   SRQ.  If SRQ creation succeeds, then the attributes are updated to
 *   the actual capabilities of the created SRQ.
 * @uobject: uobject pointer if this is not a kernel SRQ
 * @udata: udata pointer if this is not a kernel SRQ
 *
 * srq_attr->max_wr and srq_attr->max_sge are read the determine the
 * requested size of the SRQ, and set to the actual values allocated
 * on return.  If ib_create_srq() succeeds, then max_wr and max_sge
 * will always be at least as large as the requested values.
 */
struct ib_srq *ib_create_srq_user(struct ib_pd *pd,
				  struct ib_srq_init_attr *srq_init_attr,
				  struct ib_usrq_object *uobject,
				  struct ib_udata *udata)
{
	struct ib_srq *srq;
	int ret;

	srq = rdma_zalloc_drv_obj(pd->device, ib_srq);
	if (!srq)
		return ERR_PTR(-ENOMEM);

	srq->device = pd->device;
	srq->pd = pd;
	srq->event_handler = srq_init_attr->event_handler;
	srq->srq_context = srq_init_attr->srq_context;
	srq->srq_type = srq_init_attr->srq_type;
	srq->uobject = uobject;

	if (ib_srq_has_cq(srq->srq_type)) {
		srq->ext.cq = srq_init_attr->ext.cq;
		atomic_inc(&srq->ext.cq->usecnt);
	}
	if (srq->srq_type == IB_SRQT_XRC) {
		srq->ext.xrc.xrcd = srq_init_attr->ext.xrc.xrcd;
		atomic_inc(&srq->ext.xrc.xrcd->usecnt);
	}
	atomic_inc(&pd->usecnt);

	rdma_restrack_new(&srq->res, RDMA_RESTRACK_SRQ);
	rdma_restrack_parent_name(&srq->res, &pd->res);

	ret = pd->device->ops.create_srq(srq, srq_init_attr, udata);
	if (ret) {
		rdma_restrack_put(&srq->res);
		atomic_dec(&srq->pd->usecnt);
		if (srq->srq_type == IB_SRQT_XRC)
			atomic_dec(&srq->ext.xrc.xrcd->usecnt);
		if (ib_srq_has_cq(srq->srq_type))
			atomic_dec(&srq->ext.cq->usecnt);
		kfree(srq);
		return ERR_PTR(ret);
	}

	rdma_restrack_add(&srq->res);

	return srq;
}
EXPORT_SYMBOL(ib_create_srq_user);

int ib_modify_srq(struct ib_srq *srq,
		  struct ib_srq_attr *srq_attr,
		  enum ib_srq_attr_mask srq_attr_mask)
{
	return srq->device->ops.modify_srq ?
		srq->device->ops.modify_srq(srq, srq_attr, srq_attr_mask,
					    NULL) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(ib_modify_srq);

int ib_query_srq(struct ib_srq *srq,
		 struct ib_srq_attr *srq_attr)
{
	return srq->device->ops.query_srq ?
		srq->device->ops.query_srq(srq, srq_attr) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(ib_query_srq);

int ib_destroy_srq_user(struct ib_srq *srq, struct ib_udata *udata)
{
	int ret;

	if (atomic_read(&srq->usecnt))
		return -EBUSY;

	ret = srq->device->ops.destroy_srq(srq, udata);
	if (ret)
		return ret;

	atomic_dec(&srq->pd->usecnt);
	if (srq->srq_type == IB_SRQT_XRC)
		atomic_dec(&srq->ext.xrc.xrcd->usecnt);
	if (ib_srq_has_cq(srq->srq_type))
		atomic_dec(&srq->ext.cq->usecnt);
	rdma_restrack_del(&srq->res);
	kfree(srq);

	return ret;
}
EXPORT_SYMBOL(ib_destroy_srq_user);

/* Queue pairs */

static void __ib_shared_qp_event_handler(struct ib_event *event, void *context)
{
	struct ib_qp *qp = context;
	unsigned long flags;

	spin_lock_irqsave(&qp->device->qp_open_list_lock, flags);
	list_for_each_entry(event->element.qp, &qp->open_list, open_list)
		if (event->element.qp->event_handler)
			event->element.qp->event_handler(event, event->element.qp->qp_context);
	spin_unlock_irqrestore(&qp->device->qp_open_list_lock, flags);
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

	spin_lock_irqsave(&real_qp->device->qp_open_list_lock, flags);
	list_add(&qp->open_list, &real_qp->open_list);
	spin_unlock_irqrestore(&real_qp->device->qp_open_list_lock, flags);

	return qp;
}

struct ib_qp *ib_open_qp(struct ib_xrcd *xrcd,
			 struct ib_qp_open_attr *qp_open_attr)
{
	struct ib_qp *qp, *real_qp;

	if (qp_open_attr->qp_type != IB_QPT_XRC_TGT)
		return ERR_PTR(-EINVAL);

	down_read(&xrcd->tgt_qps_rwsem);
	real_qp = xa_load(&xrcd->tgt_qps, qp_open_attr->qp_num);
	if (!real_qp) {
		up_read(&xrcd->tgt_qps_rwsem);
		return ERR_PTR(-EINVAL);
	}
	qp = __ib_open_qp(real_qp, qp_open_attr->event_handler,
			  qp_open_attr->qp_context);
	up_read(&xrcd->tgt_qps_rwsem);
	return qp;
}
EXPORT_SYMBOL(ib_open_qp);

static struct ib_qp *create_xrc_qp_user(struct ib_qp *qp,
					struct ib_qp_init_attr *qp_init_attr)
{
	struct ib_qp *real_qp = qp;
	int err;

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
	if (IS_ERR(qp))
		return qp;

	err = xa_err(xa_store(&qp_init_attr->xrcd->tgt_qps, real_qp->qp_num,
			      real_qp, GFP_KERNEL));
	if (err) {
		ib_close_qp(qp);
		return ERR_PTR(err);
	}
	return qp;
}

/**
 * ib_create_named_qp - Creates a kernel QP associated with the specified protection
 *   domain.
 * @pd: The protection domain associated with the QP.
 * @qp_init_attr: A list of initial attributes required to create the
 *   QP.  If QP creation succeeds, then the attributes are updated to
 *   the actual capabilities of the created QP.
 * @caller: caller's build-time module name
 *
 * NOTE: for user qp use ib_create_qp_user with valid udata!
 */
struct ib_qp *ib_create_named_qp(struct ib_pd *pd,
				 struct ib_qp_init_attr *qp_init_attr,
				 const char *caller)
{
	struct ib_device *device = pd ? pd->device : qp_init_attr->xrcd->device;
	struct ib_qp *qp;
	int ret;

	if (qp_init_attr->rwq_ind_tbl &&
	    (qp_init_attr->recv_cq ||
	    qp_init_attr->srq || qp_init_attr->cap.max_recv_wr ||
	    qp_init_attr->cap.max_recv_sge))
		return ERR_PTR(-EINVAL);

	if ((qp_init_attr->create_flags & IB_QP_CREATE_INTEGRITY_EN) &&
	    !(device->attrs.device_cap_flags & IB_DEVICE_INTEGRITY_HANDOVER))
		return ERR_PTR(-EINVAL);

	/*
	 * If the callers is using the RDMA API calculate the resources
	 * needed for the RDMA READ/WRITE operations.
	 *
	 * Note that these callers need to pass in a port number.
	 */
	if (qp_init_attr->cap.max_rdma_ctxs)
		rdma_rw_init_qp(device, qp_init_attr);

	qp = _ib_create_qp(device, pd, qp_init_attr, NULL, NULL, caller);
	if (IS_ERR(qp))
		return qp;

	ret = ib_create_qp_security(qp, device);
	if (ret)
		goto err;

	if (qp_init_attr->qp_type == IB_QPT_XRC_TGT) {
		struct ib_qp *xrc_qp =
			create_xrc_qp_user(qp, qp_init_attr);

		if (IS_ERR(xrc_qp)) {
			ret = PTR_ERR(xrc_qp);
			goto err;
		}
		return xrc_qp;
	}

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

	qp->send_cq = qp_init_attr->send_cq;
	qp->xrcd    = NULL;

	atomic_inc(&pd->usecnt);
	if (qp_init_attr->send_cq)
		atomic_inc(&qp_init_attr->send_cq->usecnt);
	if (qp_init_attr->rwq_ind_tbl)
		atomic_inc(&qp->rwq_ind_tbl->usecnt);

	if (qp_init_attr->cap.max_rdma_ctxs) {
		ret = rdma_rw_init_mrs(qp, qp_init_attr);
		if (ret)
			goto err;
	}

	/*
	 * Note: all hw drivers guarantee that max_send_sge is lower than
	 * the device RDMA WRITE SGE limit but not all hw drivers ensure that
	 * max_send_sge <= max_sge_rd.
	 */
	qp->max_write_sge = qp_init_attr->cap.max_send_sge;
	qp->max_read_sge = min_t(u32, qp_init_attr->cap.max_send_sge,
				 device->attrs.max_sge_rd);
	if (qp_init_attr->create_flags & IB_QP_CREATE_INTEGRITY_EN)
		qp->integrity_en = true;

	return qp;

err:
	ib_destroy_qp(qp);
	return ERR_PTR(ret);

}
EXPORT_SYMBOL(ib_create_named_qp);

static const struct {
	int			valid;
	enum ib_qp_attr_mask	req_param[IB_QPT_MAX];
	enum ib_qp_attr_mask	opt_param[IB_QPT_MAX];
} qp_state_table[IB_QPS_ERR + 1][IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = {
		[IB_QPS_RESET] = { .valid = 1 },
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

bool ib_modify_qp_is_ok(enum ib_qp_state cur_state, enum ib_qp_state next_state,
			enum ib_qp_type type, enum ib_qp_attr_mask mask)
{
	enum ib_qp_attr_mask req_param, opt_param;

	if (mask & IB_QP_CUR_STATE  &&
	    cur_state != IB_QPS_RTR && cur_state != IB_QPS_RTS &&
	    cur_state != IB_QPS_SQD && cur_state != IB_QPS_SQE)
		return false;

	if (!qp_state_table[cur_state][next_state].valid)
		return false;

	req_param = qp_state_table[cur_state][next_state].req_param[type];
	opt_param = qp_state_table[cur_state][next_state].opt_param[type];

	if ((mask & req_param) != req_param)
		return false;

	if (mask & ~(req_param | opt_param | IB_QP_STATE))
		return false;

	return true;
}
EXPORT_SYMBOL(ib_modify_qp_is_ok);

/**
 * ib_resolve_eth_dmac - Resolve destination mac address
 * @device:		Device to consider
 * @ah_attr:		address handle attribute which describes the
 *			source and destination parameters
 * ib_resolve_eth_dmac() resolves destination mac address and L3 hop limit It
 * returns 0 on success or appropriate error code. It initializes the
 * necessary ah_attr fields when call is successful.
 */
static int ib_resolve_eth_dmac(struct ib_device *device,
			       struct rdma_ah_attr *ah_attr)
{
	int ret = 0;

	if (rdma_is_multicast_addr((struct in6_addr *)ah_attr->grh.dgid.raw)) {
		if (ipv6_addr_v4mapped((struct in6_addr *)ah_attr->grh.dgid.raw)) {
			__be32 addr = 0;

			memcpy(&addr, ah_attr->grh.dgid.raw + 12, 4);
			ip_eth_mc_map(addr, (char *)ah_attr->roce.dmac);
		} else {
			ipv6_eth_mc_map((struct in6_addr *)ah_attr->grh.dgid.raw,
					(char *)ah_attr->roce.dmac);
		}
	} else {
		ret = ib_resolve_unicast_gid_dmac(device, ah_attr);
	}
	return ret;
}

static bool is_qp_type_connected(const struct ib_qp *qp)
{
	return (qp->qp_type == IB_QPT_UC ||
		qp->qp_type == IB_QPT_RC ||
		qp->qp_type == IB_QPT_XRC_INI ||
		qp->qp_type == IB_QPT_XRC_TGT);
}

/*
 * IB core internal function to perform QP attributes modification.
 */
static int _ib_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
			 int attr_mask, struct ib_udata *udata)
{
	u32 port = attr_mask & IB_QP_PORT ? attr->port_num : qp->port;
	const struct ib_gid_attr *old_sgid_attr_av;
	const struct ib_gid_attr *old_sgid_attr_alt_av;
	int ret;

	attr->xmit_slave = NULL;
	if (attr_mask & IB_QP_AV) {
		ret = rdma_fill_sgid_attr(qp->device, &attr->ah_attr,
					  &old_sgid_attr_av);
		if (ret)
			return ret;

		if (attr->ah_attr.type == RDMA_AH_ATTR_TYPE_ROCE &&
		    is_qp_type_connected(qp)) {
			struct net_device *slave;

			/*
			 * If the user provided the qp_attr then we have to
			 * resolve it. Kerne users have to provide already
			 * resolved rdma_ah_attr's.
			 */
			if (udata) {
				ret = ib_resolve_eth_dmac(qp->device,
							  &attr->ah_attr);
				if (ret)
					goto out_av;
			}
			slave = rdma_lag_get_ah_roce_slave(qp->device,
							   &attr->ah_attr,
							   GFP_KERNEL);
			if (IS_ERR(slave)) {
				ret = PTR_ERR(slave);
				goto out_av;
			}
			attr->xmit_slave = slave;
		}
	}
	if (attr_mask & IB_QP_ALT_PATH) {
		/*
		 * FIXME: This does not track the migration state, so if the
		 * user loads a new alternate path after the HW has migrated
		 * from primary->alternate we will keep the wrong
		 * references. This is OK for IB because the reference
		 * counting does not serve any functional purpose.
		 */
		ret = rdma_fill_sgid_attr(qp->device, &attr->alt_ah_attr,
					  &old_sgid_attr_alt_av);
		if (ret)
			goto out_av;

		/*
		 * Today the core code can only handle alternate paths and APM
		 * for IB. Ban them in roce mode.
		 */
		if (!(rdma_protocol_ib(qp->device,
				       attr->alt_ah_attr.port_num) &&
		      rdma_protocol_ib(qp->device, port))) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (rdma_ib_or_roce(qp->device, port)) {
		if (attr_mask & IB_QP_RQ_PSN && attr->rq_psn & ~0xffffff) {
			dev_warn(&qp->device->dev,
				 "%s rq_psn overflow, masking to 24 bits\n",
				 __func__);
			attr->rq_psn &= 0xffffff;
		}

		if (attr_mask & IB_QP_SQ_PSN && attr->sq_psn & ~0xffffff) {
			dev_warn(&qp->device->dev,
				 " %s sq_psn overflow, masking to 24 bits\n",
				 __func__);
			attr->sq_psn &= 0xffffff;
		}
	}

	/*
	 * Bind this qp to a counter automatically based on the rdma counter
	 * rules. This only set in RST2INIT with port specified
	 */
	if (!qp->counter && (attr_mask & IB_QP_PORT) &&
	    ((attr_mask & IB_QP_STATE) && attr->qp_state == IB_QPS_INIT))
		rdma_counter_bind_qp_auto(qp, attr->port_num);

	ret = ib_security_modify_qp(qp, attr, attr_mask, udata);
	if (ret)
		goto out;

	if (attr_mask & IB_QP_PORT)
		qp->port = attr->port_num;
	if (attr_mask & IB_QP_AV)
		qp->av_sgid_attr =
			rdma_update_sgid_attr(&attr->ah_attr, qp->av_sgid_attr);
	if (attr_mask & IB_QP_ALT_PATH)
		qp->alt_path_sgid_attr = rdma_update_sgid_attr(
			&attr->alt_ah_attr, qp->alt_path_sgid_attr);

out:
	if (attr_mask & IB_QP_ALT_PATH)
		rdma_unfill_sgid_attr(&attr->alt_ah_attr, old_sgid_attr_alt_av);
out_av:
	if (attr_mask & IB_QP_AV) {
		rdma_lag_put_ah_roce_slave(attr->xmit_slave);
		rdma_unfill_sgid_attr(&attr->ah_attr, old_sgid_attr_av);
	}
	return ret;
}

/**
 * ib_modify_qp_with_udata - Modifies the attributes for the specified QP.
 * @ib_qp: The QP to modify.
 * @attr: On input, specifies the QP attributes to modify.  On output,
 *   the current values of selected QP attributes are returned.
 * @attr_mask: A bit-mask used to specify which attributes of the QP
 *   are being modified.
 * @udata: pointer to user's input output buffer information
 *   are being modified.
 * It returns 0 on success and returns appropriate error code on error.
 */
int ib_modify_qp_with_udata(struct ib_qp *ib_qp, struct ib_qp_attr *attr,
			    int attr_mask, struct ib_udata *udata)
{
	return _ib_modify_qp(ib_qp->real_qp, attr, attr_mask, udata);
}
EXPORT_SYMBOL(ib_modify_qp_with_udata);

int ib_get_eth_speed(struct ib_device *dev, u32 port_num, u16 *speed, u8 *width)
{
	int rc;
	u32 netdev_speed;
	struct net_device *netdev;
	struct ethtool_link_ksettings lksettings;

	if (rdma_port_get_link_layer(dev, port_num) != IB_LINK_LAYER_ETHERNET)
		return -EINVAL;

	netdev = ib_device_get_netdev(dev, port_num);
	if (!netdev)
		return -ENODEV;

	rtnl_lock();
	rc = __ethtool_get_link_ksettings(netdev, &lksettings);
	rtnl_unlock();

	dev_put(netdev);

	if (!rc && lksettings.base.speed != (u32)SPEED_UNKNOWN) {
		netdev_speed = lksettings.base.speed;
	} else {
		netdev_speed = SPEED_1000;
		pr_warn("%s speed is unknown, defaulting to %u\n", netdev->name,
			netdev_speed);
	}

	if (netdev_speed <= SPEED_1000) {
		*width = IB_WIDTH_1X;
		*speed = IB_SPEED_SDR;
	} else if (netdev_speed <= SPEED_10000) {
		*width = IB_WIDTH_1X;
		*speed = IB_SPEED_FDR10;
	} else if (netdev_speed <= SPEED_20000) {
		*width = IB_WIDTH_4X;
		*speed = IB_SPEED_DDR;
	} else if (netdev_speed <= SPEED_25000) {
		*width = IB_WIDTH_1X;
		*speed = IB_SPEED_EDR;
	} else if (netdev_speed <= SPEED_40000) {
		*width = IB_WIDTH_4X;
		*speed = IB_SPEED_FDR10;
	} else {
		*width = IB_WIDTH_4X;
		*speed = IB_SPEED_EDR;
	}

	return 0;
}
EXPORT_SYMBOL(ib_get_eth_speed);

int ib_modify_qp(struct ib_qp *qp,
		 struct ib_qp_attr *qp_attr,
		 int qp_attr_mask)
{
	return _ib_modify_qp(qp->real_qp, qp_attr, qp_attr_mask, NULL);
}
EXPORT_SYMBOL(ib_modify_qp);

int ib_query_qp(struct ib_qp *qp,
		struct ib_qp_attr *qp_attr,
		int qp_attr_mask,
		struct ib_qp_init_attr *qp_init_attr)
{
	qp_attr->ah_attr.grh.sgid_attr = NULL;
	qp_attr->alt_ah_attr.grh.sgid_attr = NULL;

	return qp->device->ops.query_qp ?
		qp->device->ops.query_qp(qp->real_qp, qp_attr, qp_attr_mask,
					 qp_init_attr) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(ib_query_qp);

int ib_close_qp(struct ib_qp *qp)
{
	struct ib_qp *real_qp;
	unsigned long flags;

	real_qp = qp->real_qp;
	if (real_qp == qp)
		return -EINVAL;

	spin_lock_irqsave(&real_qp->device->qp_open_list_lock, flags);
	list_del(&qp->open_list);
	spin_unlock_irqrestore(&real_qp->device->qp_open_list_lock, flags);

	atomic_dec(&real_qp->usecnt);
	if (qp->qp_sec)
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
	down_write(&xrcd->tgt_qps_rwsem);
	ib_close_qp(qp);
	if (atomic_read(&real_qp->usecnt) == 0)
		xa_erase(&xrcd->tgt_qps, real_qp->qp_num);
	else
		real_qp = NULL;
	up_write(&xrcd->tgt_qps_rwsem);

	if (real_qp) {
		ret = ib_destroy_qp(real_qp);
		if (!ret)
			atomic_dec(&xrcd->usecnt);
	}

	return 0;
}

int ib_destroy_qp_user(struct ib_qp *qp, struct ib_udata *udata)
{
	const struct ib_gid_attr *alt_path_sgid_attr = qp->alt_path_sgid_attr;
	const struct ib_gid_attr *av_sgid_attr = qp->av_sgid_attr;
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

	rdma_counter_unbind_qp(qp, true);
	rdma_restrack_del(&qp->res);
	ret = qp->device->ops.destroy_qp(qp, udata);
	if (!ret) {
		if (alt_path_sgid_attr)
			rdma_put_gid_attr(alt_path_sgid_attr);
		if (av_sgid_attr)
			rdma_put_gid_attr(av_sgid_attr);
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
EXPORT_SYMBOL(ib_destroy_qp_user);

/* Completion queues */

struct ib_cq *__ib_create_cq(struct ib_device *device,
			     ib_comp_handler comp_handler,
			     void (*event_handler)(struct ib_event *, void *),
			     void *cq_context,
			     const struct ib_cq_init_attr *cq_attr,
			     const char *caller)
{
	struct ib_cq *cq;
	int ret;

	cq = rdma_zalloc_drv_obj(device, ib_cq);
	if (!cq)
		return ERR_PTR(-ENOMEM);

	cq->device = device;
	cq->uobject = NULL;
	cq->comp_handler = comp_handler;
	cq->event_handler = event_handler;
	cq->cq_context = cq_context;
	atomic_set(&cq->usecnt, 0);

	rdma_restrack_new(&cq->res, RDMA_RESTRACK_CQ);
	rdma_restrack_set_name(&cq->res, caller);

	ret = device->ops.create_cq(cq, cq_attr, NULL);
	if (ret) {
		rdma_restrack_put(&cq->res);
		kfree(cq);
		return ERR_PTR(ret);
	}

	rdma_restrack_add(&cq->res);
	return cq;
}
EXPORT_SYMBOL(__ib_create_cq);

int rdma_set_cq_moderation(struct ib_cq *cq, u16 cq_count, u16 cq_period)
{
	if (cq->shared)
		return -EOPNOTSUPP;

	return cq->device->ops.modify_cq ?
		cq->device->ops.modify_cq(cq, cq_count,
					  cq_period) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(rdma_set_cq_moderation);

int ib_destroy_cq_user(struct ib_cq *cq, struct ib_udata *udata)
{
	int ret;

	if (WARN_ON_ONCE(cq->shared))
		return -EOPNOTSUPP;

	if (atomic_read(&cq->usecnt))
		return -EBUSY;

	ret = cq->device->ops.destroy_cq(cq, udata);
	if (ret)
		return ret;

	rdma_restrack_del(&cq->res);
	kfree(cq);
	return ret;
}
EXPORT_SYMBOL(ib_destroy_cq_user);

int ib_resize_cq(struct ib_cq *cq, int cqe)
{
	if (cq->shared)
		return -EOPNOTSUPP;

	return cq->device->ops.resize_cq ?
		cq->device->ops.resize_cq(cq, cqe, NULL) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(ib_resize_cq);

/* Memory regions */

struct ib_mr *ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			     u64 virt_addr, int access_flags)
{
	struct ib_mr *mr;

	if (access_flags & IB_ACCESS_ON_DEMAND) {
		if (!(pd->device->attrs.device_cap_flags &
		      IB_DEVICE_ON_DEMAND_PAGING)) {
			pr_debug("ODP support not available\n");
			return ERR_PTR(-EINVAL);
		}
	}

	mr = pd->device->ops.reg_user_mr(pd, start, length, virt_addr,
					 access_flags, NULL);

	if (IS_ERR(mr))
		return mr;

	mr->device = pd->device;
	mr->pd = pd;
	mr->dm = NULL;
	atomic_inc(&pd->usecnt);

	rdma_restrack_new(&mr->res, RDMA_RESTRACK_MR);
	rdma_restrack_parent_name(&mr->res, &pd->res);
	rdma_restrack_add(&mr->res);

	return mr;
}
EXPORT_SYMBOL(ib_reg_user_mr);

int ib_advise_mr(struct ib_pd *pd, enum ib_uverbs_advise_mr_advice advice,
		 u32 flags, struct ib_sge *sg_list, u32 num_sge)
{
	if (!pd->device->ops.advise_mr)
		return -EOPNOTSUPP;

	if (!num_sge)
		return 0;

	return pd->device->ops.advise_mr(pd, advice, flags, sg_list, num_sge,
					 NULL);
}
EXPORT_SYMBOL(ib_advise_mr);

int ib_dereg_mr_user(struct ib_mr *mr, struct ib_udata *udata)
{
	struct ib_pd *pd = mr->pd;
	struct ib_dm *dm = mr->dm;
	struct ib_sig_attrs *sig_attrs = mr->sig_attrs;
	int ret;

	trace_mr_dereg(mr);
	rdma_restrack_del(&mr->res);
	ret = mr->device->ops.dereg_mr(mr, udata);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		if (dm)
			atomic_dec(&dm->usecnt);
		kfree(sig_attrs);
	}

	return ret;
}
EXPORT_SYMBOL(ib_dereg_mr_user);

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
struct ib_mr *ib_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			  u32 max_num_sg)
{
	struct ib_mr *mr;

	if (!pd->device->ops.alloc_mr) {
		mr = ERR_PTR(-EOPNOTSUPP);
		goto out;
	}

	if (mr_type == IB_MR_TYPE_INTEGRITY) {
		WARN_ON_ONCE(1);
		mr = ERR_PTR(-EINVAL);
		goto out;
	}

	mr = pd->device->ops.alloc_mr(pd, mr_type, max_num_sg);
	if (IS_ERR(mr))
		goto out;

	mr->device = pd->device;
	mr->pd = pd;
	mr->dm = NULL;
	mr->uobject = NULL;
	atomic_inc(&pd->usecnt);
	mr->need_inval = false;
	mr->type = mr_type;
	mr->sig_attrs = NULL;

	rdma_restrack_new(&mr->res, RDMA_RESTRACK_MR);
	rdma_restrack_parent_name(&mr->res, &pd->res);
	rdma_restrack_add(&mr->res);
out:
	trace_mr_alloc(pd, mr_type, max_num_sg, mr);
	return mr;
}
EXPORT_SYMBOL(ib_alloc_mr);

/**
 * ib_alloc_mr_integrity() - Allocates an integrity memory region
 * @pd:                      protection domain associated with the region
 * @max_num_data_sg:         maximum data sg entries available for registration
 * @max_num_meta_sg:         maximum metadata sg entries available for
 *                           registration
 *
 * Notes:
 * Memory registration page/sg lists must not exceed max_num_sg,
 * also the integrity page/sg lists must not exceed max_num_meta_sg.
 *
 */
struct ib_mr *ib_alloc_mr_integrity(struct ib_pd *pd,
				    u32 max_num_data_sg,
				    u32 max_num_meta_sg)
{
	struct ib_mr *mr;
	struct ib_sig_attrs *sig_attrs;

	if (!pd->device->ops.alloc_mr_integrity ||
	    !pd->device->ops.map_mr_sg_pi) {
		mr = ERR_PTR(-EOPNOTSUPP);
		goto out;
	}

	if (!max_num_meta_sg) {
		mr = ERR_PTR(-EINVAL);
		goto out;
	}

	sig_attrs = kzalloc(sizeof(struct ib_sig_attrs), GFP_KERNEL);
	if (!sig_attrs) {
		mr = ERR_PTR(-ENOMEM);
		goto out;
	}

	mr = pd->device->ops.alloc_mr_integrity(pd, max_num_data_sg,
						max_num_meta_sg);
	if (IS_ERR(mr)) {
		kfree(sig_attrs);
		goto out;
	}

	mr->device = pd->device;
	mr->pd = pd;
	mr->dm = NULL;
	mr->uobject = NULL;
	atomic_inc(&pd->usecnt);
	mr->need_inval = false;
	mr->type = IB_MR_TYPE_INTEGRITY;
	mr->sig_attrs = sig_attrs;

	rdma_restrack_new(&mr->res, RDMA_RESTRACK_MR);
	rdma_restrack_parent_name(&mr->res, &pd->res);
	rdma_restrack_add(&mr->res);
out:
	trace_mr_integ_alloc(pd, max_num_data_sg, max_num_meta_sg, mr);
	return mr;
}
EXPORT_SYMBOL(ib_alloc_mr_integrity);

/* Multicast groups */

static bool is_valid_mcast_lid(struct ib_qp *qp, u16 lid)
{
	struct ib_qp_init_attr init_attr = {};
	struct ib_qp_attr attr = {};
	int num_eth_ports = 0;
	unsigned int port;

	/* If QP state >= init, it is assigned to a port and we can check this
	 * port only.
	 */
	if (!ib_query_qp(qp, &attr, IB_QP_STATE | IB_QP_PORT, &init_attr)) {
		if (attr.qp_state >= IB_QPS_INIT) {
			if (rdma_port_get_link_layer(qp->device, attr.port_num) !=
			    IB_LINK_LAYER_INFINIBAND)
				return true;
			goto lid_check;
		}
	}

	/* Can't get a quick answer, iterate over all ports */
	rdma_for_each_port(qp->device, port)
		if (rdma_port_get_link_layer(qp->device, port) !=
		    IB_LINK_LAYER_INFINIBAND)
			num_eth_ports++;

	/* If we have at lease one Ethernet port, RoCE annex declares that
	 * multicast LID should be ignored. We can't tell at this step if the
	 * QP belongs to an IB or Ethernet port.
	 */
	if (num_eth_ports)
		return true;

	/* If all the ports are IB, we can check according to IB spec. */
lid_check:
	return !(lid < be16_to_cpu(IB_MULTICAST_LID_BASE) ||
		 lid == be16_to_cpu(IB_LID_PERMISSIVE));
}

int ib_attach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid)
{
	int ret;

	if (!qp->device->ops.attach_mcast)
		return -EOPNOTSUPP;

	if (!rdma_is_multicast_addr((struct in6_addr *)gid->raw) ||
	    qp->qp_type != IB_QPT_UD || !is_valid_mcast_lid(qp, lid))
		return -EINVAL;

	ret = qp->device->ops.attach_mcast(qp, gid, lid);
	if (!ret)
		atomic_inc(&qp->usecnt);
	return ret;
}
EXPORT_SYMBOL(ib_attach_mcast);

int ib_detach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid)
{
	int ret;

	if (!qp->device->ops.detach_mcast)
		return -EOPNOTSUPP;

	if (!rdma_is_multicast_addr((struct in6_addr *)gid->raw) ||
	    qp->qp_type != IB_QPT_UD || !is_valid_mcast_lid(qp, lid))
		return -EINVAL;

	ret = qp->device->ops.detach_mcast(qp, gid, lid);
	if (!ret)
		atomic_dec(&qp->usecnt);
	return ret;
}
EXPORT_SYMBOL(ib_detach_mcast);

/**
 * ib_alloc_xrcd_user - Allocates an XRC domain.
 * @device: The device on which to allocate the XRC domain.
 * @inode: inode to connect XRCD
 * @udata: Valid user data or NULL for kernel object
 */
struct ib_xrcd *ib_alloc_xrcd_user(struct ib_device *device,
				   struct inode *inode, struct ib_udata *udata)
{
	struct ib_xrcd *xrcd;
	int ret;

	if (!device->ops.alloc_xrcd)
		return ERR_PTR(-EOPNOTSUPP);

	xrcd = rdma_zalloc_drv_obj(device, ib_xrcd);
	if (!xrcd)
		return ERR_PTR(-ENOMEM);

	xrcd->device = device;
	xrcd->inode = inode;
	atomic_set(&xrcd->usecnt, 0);
	init_rwsem(&xrcd->tgt_qps_rwsem);
	xa_init(&xrcd->tgt_qps);

	ret = device->ops.alloc_xrcd(xrcd, udata);
	if (ret)
		goto err;
	return xrcd;
err:
	kfree(xrcd);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(ib_alloc_xrcd_user);

/**
 * ib_dealloc_xrcd_user - Deallocates an XRC domain.
 * @xrcd: The XRC domain to deallocate.
 * @udata: Valid user data or NULL for kernel object
 */
int ib_dealloc_xrcd_user(struct ib_xrcd *xrcd, struct ib_udata *udata)
{
	int ret;

	if (atomic_read(&xrcd->usecnt))
		return -EBUSY;

	WARN_ON(!xa_empty(&xrcd->tgt_qps));
	ret = xrcd->device->ops.dealloc_xrcd(xrcd, udata);
	if (ret)
		return ret;
	kfree(xrcd);
	return ret;
}
EXPORT_SYMBOL(ib_dealloc_xrcd_user);

/**
 * ib_create_wq - Creates a WQ associated with the specified protection
 * domain.
 * @pd: The protection domain associated with the WQ.
 * @wq_attr: A list of initial attributes required to create the
 * WQ. If WQ creation succeeds, then the attributes are updated to
 * the actual capabilities of the created WQ.
 *
 * wq_attr->max_wr and wq_attr->max_sge determine
 * the requested size of the WQ, and set to the actual values allocated
 * on return.
 * If ib_create_wq() succeeds, then max_wr and max_sge will always be
 * at least as large as the requested values.
 */
struct ib_wq *ib_create_wq(struct ib_pd *pd,
			   struct ib_wq_init_attr *wq_attr)
{
	struct ib_wq *wq;

	if (!pd->device->ops.create_wq)
		return ERR_PTR(-EOPNOTSUPP);

	wq = pd->device->ops.create_wq(pd, wq_attr, NULL);
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
 * ib_destroy_wq_user - Destroys the specified user WQ.
 * @wq: The WQ to destroy.
 * @udata: Valid user data
 */
int ib_destroy_wq_user(struct ib_wq *wq, struct ib_udata *udata)
{
	struct ib_cq *cq = wq->cq;
	struct ib_pd *pd = wq->pd;
	int ret;

	if (atomic_read(&wq->usecnt))
		return -EBUSY;

	ret = wq->device->ops.destroy_wq(wq, udata);
	if (ret)
		return ret;

	atomic_dec(&pd->usecnt);
	atomic_dec(&cq->usecnt);
	return ret;
}
EXPORT_SYMBOL(ib_destroy_wq_user);

int ib_check_mr_status(struct ib_mr *mr, u32 check_mask,
		       struct ib_mr_status *mr_status)
{
	if (!mr->device->ops.check_mr_status)
		return -EOPNOTSUPP;

	return mr->device->ops.check_mr_status(mr, check_mask, mr_status);
}
EXPORT_SYMBOL(ib_check_mr_status);

int ib_set_vf_link_state(struct ib_device *device, int vf, u32 port,
			 int state)
{
	if (!device->ops.set_vf_link_state)
		return -EOPNOTSUPP;

	return device->ops.set_vf_link_state(device, vf, port, state);
}
EXPORT_SYMBOL(ib_set_vf_link_state);

int ib_get_vf_config(struct ib_device *device, int vf, u32 port,
		     struct ifla_vf_info *info)
{
	if (!device->ops.get_vf_config)
		return -EOPNOTSUPP;

	return device->ops.get_vf_config(device, vf, port, info);
}
EXPORT_SYMBOL(ib_get_vf_config);

int ib_get_vf_stats(struct ib_device *device, int vf, u32 port,
		    struct ifla_vf_stats *stats)
{
	if (!device->ops.get_vf_stats)
		return -EOPNOTSUPP;

	return device->ops.get_vf_stats(device, vf, port, stats);
}
EXPORT_SYMBOL(ib_get_vf_stats);

int ib_set_vf_guid(struct ib_device *device, int vf, u32 port, u64 guid,
		   int type)
{
	if (!device->ops.set_vf_guid)
		return -EOPNOTSUPP;

	return device->ops.set_vf_guid(device, vf, port, guid, type);
}
EXPORT_SYMBOL(ib_set_vf_guid);

int ib_get_vf_guid(struct ib_device *device, int vf, u32 port,
		   struct ifla_vf_guid *node_guid,
		   struct ifla_vf_guid *port_guid)
{
	if (!device->ops.get_vf_guid)
		return -EOPNOTSUPP;

	return device->ops.get_vf_guid(device, vf, port, node_guid, port_guid);
}
EXPORT_SYMBOL(ib_get_vf_guid);
/**
 * ib_map_mr_sg_pi() - Map the dma mapped SG lists for PI (protection
 *     information) and set an appropriate memory region for registration.
 * @mr:             memory region
 * @data_sg:        dma mapped scatterlist for data
 * @data_sg_nents:  number of entries in data_sg
 * @data_sg_offset: offset in bytes into data_sg
 * @meta_sg:        dma mapped scatterlist for metadata
 * @meta_sg_nents:  number of entries in meta_sg
 * @meta_sg_offset: offset in bytes into meta_sg
 * @page_size:      page vector desired page size
 *
 * Constraints:
 * - The MR must be allocated with type IB_MR_TYPE_INTEGRITY.
 *
 * Return: 0 on success.
 *
 * After this completes successfully, the  memory region
 * is ready for registration.
 */
int ib_map_mr_sg_pi(struct ib_mr *mr, struct scatterlist *data_sg,
		    int data_sg_nents, unsigned int *data_sg_offset,
		    struct scatterlist *meta_sg, int meta_sg_nents,
		    unsigned int *meta_sg_offset, unsigned int page_size)
{
	if (unlikely(!mr->device->ops.map_mr_sg_pi ||
		     WARN_ON_ONCE(mr->type != IB_MR_TYPE_INTEGRITY)))
		return -EOPNOTSUPP;

	mr->page_size = page_size;

	return mr->device->ops.map_mr_sg_pi(mr, data_sg, data_sg_nents,
					    data_sg_offset, meta_sg,
					    meta_sg_nents, meta_sg_offset);
}
EXPORT_SYMBOL(ib_map_mr_sg_pi);

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
 *
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
	if (unlikely(!mr->device->ops.map_mr_sg))
		return -EOPNOTSUPP;

	mr->page_size = page_size;

	return mr->device->ops.map_mr_sg(mr, sg, sg_nents, sg_offset);
}
EXPORT_SYMBOL(ib_map_mr_sg);

/**
 * ib_sg_to_pages() - Convert the largest prefix of a sg list
 *     to a page vector
 * @mr:            memory region
 * @sgl:           dma mapped scatterlist
 * @sg_nents:      number of entries in sg
 * @sg_offset_p:   ==== =======================================================
 *                 IN   start offset in bytes into sg
 *                 OUT  offset in bytes for element n of the sg of the first
 *                      byte that has not been processed where n is the return
 *                      value of this function.
 *                 ==== =======================================================
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
	struct ib_rdma_wr swr = {
		.wr = {
			.next = NULL,
			{ .wr_cqe	= &sdrain.cqe, },
			.opcode	= IB_WR_RDMA_WRITE,
		},
	};
	int ret;

	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret) {
		WARN_ONCE(ret, "failed to drain send queue: %d\n", ret);
		return;
	}

	sdrain.cqe.done = ib_drain_qp_done;
	init_completion(&sdrain.done);

	ret = ib_post_send(qp, &swr.wr, NULL);
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
	struct ib_recv_wr rwr = {};
	int ret;

	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret) {
		WARN_ONCE(ret, "failed to drain recv queue: %d\n", ret);
		return;
	}

	rwr.wr_cqe = &rdrain.cqe;
	rdrain.cqe.done = ib_drain_qp_done;
	init_completion(&rdrain.done);

	ret = ib_post_recv(qp, &rwr, NULL);
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
	if (qp->device->ops.drain_sq)
		qp->device->ops.drain_sq(qp);
	else
		__ib_drain_sq(qp);
	trace_cq_drain_complete(qp->send_cq);
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
	if (qp->device->ops.drain_rq)
		qp->device->ops.drain_rq(qp);
	else
		__ib_drain_rq(qp);
	trace_cq_drain_complete(qp->recv_cq);
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

struct net_device *rdma_alloc_netdev(struct ib_device *device, u32 port_num,
				     enum rdma_netdev_t type, const char *name,
				     unsigned char name_assign_type,
				     void (*setup)(struct net_device *))
{
	struct rdma_netdev_alloc_params params;
	struct net_device *netdev;
	int rc;

	if (!device->ops.rdma_netdev_get_params)
		return ERR_PTR(-EOPNOTSUPP);

	rc = device->ops.rdma_netdev_get_params(device, port_num, type,
						&params);
	if (rc)
		return ERR_PTR(rc);

	netdev = alloc_netdev_mqs(params.sizeof_priv, name, name_assign_type,
				  setup, params.txqs, params.rxqs);
	if (!netdev)
		return ERR_PTR(-ENOMEM);

	return netdev;
}
EXPORT_SYMBOL(rdma_alloc_netdev);

int rdma_init_netdev(struct ib_device *device, u32 port_num,
		     enum rdma_netdev_t type, const char *name,
		     unsigned char name_assign_type,
		     void (*setup)(struct net_device *),
		     struct net_device *netdev)
{
	struct rdma_netdev_alloc_params params;
	int rc;

	if (!device->ops.rdma_netdev_get_params)
		return -EOPNOTSUPP;

	rc = device->ops.rdma_netdev_get_params(device, port_num, type,
						&params);
	if (rc)
		return rc;

	return params.initialize_rdma_netdev(device, port_num,
					     netdev, params.param);
}
EXPORT_SYMBOL(rdma_init_netdev);

void __rdma_block_iter_start(struct ib_block_iter *biter,
			     struct scatterlist *sglist, unsigned int nents,
			     unsigned long pgsz)
{
	memset(biter, 0, sizeof(struct ib_block_iter));
	biter->__sg = sglist;
	biter->__sg_nents = nents;

	/* Driver provides best block size to use */
	biter->__pg_bit = __fls(pgsz);
}
EXPORT_SYMBOL(__rdma_block_iter_start);

bool __rdma_block_iter_next(struct ib_block_iter *biter)
{
	unsigned int block_offset;

	if (!biter->__sg_nents || !biter->__sg)
		return false;

	biter->__dma_addr = sg_dma_address(biter->__sg) + biter->__sg_advance;
	block_offset = biter->__dma_addr & (BIT_ULL(biter->__pg_bit) - 1);
	biter->__sg_advance += BIT_ULL(biter->__pg_bit) - block_offset;

	if (biter->__sg_advance >= sg_dma_len(biter->__sg)) {
		biter->__sg_advance = 0;
		biter->__sg = sg_next(biter->__sg);
		biter->__sg_nents--;
	}

	return true;
}
EXPORT_SYMBOL(__rdma_block_iter_next);
