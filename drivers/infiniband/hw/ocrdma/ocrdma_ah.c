/*******************************************************************
 * This file is part of the Emulex RoCE Device Driver for          *
 * RoCE (RDMA over Converged Ethernet) adapters.                   *
 * Copyright (C) 2008-2012 Emulex. All rights reserved.            *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 *******************************************************************/

#include <net/neighbour.h>
#include <net/netevent.h>

#include <rdma/ib_addr.h>

#include "ocrdma.h"
#include "ocrdma_verbs.h"
#include "ocrdma_ah.h"
#include "ocrdma_hw.h"

static inline int set_av_attr(struct ocrdma_ah *ah,
				struct ib_ah_attr *attr, int pdid)
{
	int status = 0;
	u16 vlan_tag; bool vlan_enabled = false;
	struct ocrdma_dev *dev = ah->dev;
	struct ocrdma_eth_vlan eth;
	struct ocrdma_grh grh;
	int eth_sz;

	memset(&eth, 0, sizeof(eth));
	memset(&grh, 0, sizeof(grh));

	ah->sgid_index = attr->grh.sgid_index;

	vlan_tag = rdma_get_vlan_id(&attr->grh.dgid);
	if (vlan_tag && (vlan_tag < 0x1000)) {
		eth.eth_type = cpu_to_be16(0x8100);
		eth.roce_eth_type = cpu_to_be16(OCRDMA_ROCE_ETH_TYPE);
		vlan_tag |= (attr->sl & 7) << 13;
		eth.vlan_tag = cpu_to_be16(vlan_tag);
		eth_sz = sizeof(struct ocrdma_eth_vlan);
		vlan_enabled = true;
	} else {
		eth.eth_type = cpu_to_be16(OCRDMA_ROCE_ETH_TYPE);
		eth_sz = sizeof(struct ocrdma_eth_basic);
	}
	memcpy(&eth.smac[0], &dev->nic_info.mac_addr[0], ETH_ALEN);
	status = ocrdma_resolve_dgid(dev, &attr->grh.dgid, &eth.dmac[0]);
	if (status)
		return status;
	status = ocrdma_query_gid(&dev->ibdev, 1, attr->grh.sgid_index,
			(union ib_gid *)&grh.sgid[0]);
	if (status)
		return status;

	grh.tclass_flow = cpu_to_be32((6 << 28) |
			(attr->grh.traffic_class << 24) |
			attr->grh.flow_label);
	/* 0x1b is next header value in GRH */
	grh.pdid_hoplimit = cpu_to_be32((pdid << 16) |
			(0x1b << 8) | attr->grh.hop_limit);

	memcpy(&grh.dgid[0], attr->grh.dgid.raw, sizeof(attr->grh.dgid.raw));
	memcpy(&ah->av->eth_hdr, &eth, eth_sz);
	memcpy((u8 *)ah->av + eth_sz, &grh, sizeof(struct ocrdma_grh));
	if (vlan_enabled)
		ah->av->valid |= OCRDMA_AV_VLAN_VALID;
	return status;
}

struct ib_ah *ocrdma_create_ah(struct ib_pd *ibpd, struct ib_ah_attr *attr)
{
	u32 *ahid_addr;
	int status;
	struct ocrdma_ah *ah;
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibpd->device);

	if (!(attr->ah_flags & IB_AH_GRH))
		return ERR_PTR(-EINVAL);

	ah = kzalloc(sizeof *ah, GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);
	ah->dev = dev;

	status = ocrdma_alloc_av(dev, ah);
	if (status)
		goto av_err;
	status = set_av_attr(ah, attr, pd->id);
	if (status)
		goto av_conf_err;

	/* if pd is for the user process, pass the ah_id to user space */
	if ((pd->uctx) && (pd->uctx->ah_tbl.va)) {
		ahid_addr = pd->uctx->ah_tbl.va + attr->dlid;
		*ahid_addr = ah->id;
	}
	return &ah->ibah;

av_conf_err:
	ocrdma_free_av(dev, ah);
av_err:
	kfree(ah);
	return ERR_PTR(status);
}

int ocrdma_destroy_ah(struct ib_ah *ibah)
{
	struct ocrdma_ah *ah = get_ocrdma_ah(ibah);
	ocrdma_free_av(ah->dev, ah);
	kfree(ah);
	return 0;
}

int ocrdma_query_ah(struct ib_ah *ibah, struct ib_ah_attr *attr)
{
	struct ocrdma_ah *ah = get_ocrdma_ah(ibah);
	struct ocrdma_av *av = ah->av;
	struct ocrdma_grh *grh;
	attr->ah_flags |= IB_AH_GRH;
	if (ah->av->valid & Bit(1)) {
		grh = (struct ocrdma_grh *)((u8 *)ah->av +
				sizeof(struct ocrdma_eth_vlan));
		attr->sl = be16_to_cpu(av->eth_hdr.vlan_tag) >> 13;
	} else {
		grh = (struct ocrdma_grh *)((u8 *)ah->av +
					sizeof(struct ocrdma_eth_basic));
		attr->sl = 0;
	}
	memcpy(&attr->grh.dgid.raw[0], &grh->dgid[0], sizeof(grh->dgid));
	attr->grh.sgid_index = ah->sgid_index;
	attr->grh.hop_limit = be32_to_cpu(grh->pdid_hoplimit) & 0xff;
	attr->grh.traffic_class = be32_to_cpu(grh->tclass_flow) >> 24;
	attr->grh.flow_label = be32_to_cpu(grh->tclass_flow) & 0x00ffffffff;
	return 0;
}

int ocrdma_modify_ah(struct ib_ah *ibah, struct ib_ah_attr *attr)
{
	/* modify_ah is unsupported */
	return -ENOSYS;
}

int ocrdma_process_mad(struct ib_device *ibdev,
		       int process_mad_flags,
		       u8 port_num,
		       struct ib_wc *in_wc,
		       struct ib_grh *in_grh,
		       struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	return IB_MAD_RESULT_SUCCESS;
}
