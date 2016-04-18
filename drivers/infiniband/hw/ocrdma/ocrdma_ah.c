/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <net/neighbour.h>
#include <net/netevent.h>

#include <rdma/ib_addr.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_cache.h>

#include "ocrdma.h"
#include "ocrdma_verbs.h"
#include "ocrdma_ah.h"
#include "ocrdma_hw.h"
#include "ocrdma_stats.h"

#define OCRDMA_VID_PCP_SHIFT	0xD

static u16 ocrdma_hdr_type_to_proto_num(int devid, u8 hdr_type)
{
	switch (hdr_type) {
	case OCRDMA_L3_TYPE_IB_GRH:
		return (u16)0x8915;
	case OCRDMA_L3_TYPE_IPV4:
		return (u16)0x0800;
	case OCRDMA_L3_TYPE_IPV6:
		return (u16)0x86dd;
	default:
		pr_err("ocrdma%d: Invalid network header\n", devid);
		return 0;
	}
}

static inline int set_av_attr(struct ocrdma_dev *dev, struct ocrdma_ah *ah,
			struct ib_ah_attr *attr, union ib_gid *sgid,
			int pdid, bool *isvlan, u16 vlan_tag)
{
	int status;
	struct ocrdma_eth_vlan eth;
	struct ocrdma_grh grh;
	int eth_sz;
	u16 proto_num = 0;
	u8 nxthdr = 0x11;
	struct iphdr ipv4;
	union {
		struct sockaddr     _sockaddr;
		struct sockaddr_in  _sockaddr_in;
		struct sockaddr_in6 _sockaddr_in6;
	} sgid_addr, dgid_addr;

	memset(&eth, 0, sizeof(eth));
	memset(&grh, 0, sizeof(grh));

	/* Protocol Number */
	proto_num = ocrdma_hdr_type_to_proto_num(dev->id, ah->hdr_type);
	if (!proto_num)
		return -EINVAL;
	nxthdr = (proto_num == 0x8915) ? 0x1b : 0x11;
	/* VLAN */
	if (!vlan_tag || (vlan_tag > 0xFFF))
		vlan_tag = dev->pvid;
	if (vlan_tag || dev->pfc_state) {
		if (!vlan_tag) {
			pr_err("ocrdma%d:Using VLAN with PFC is recommended\n",
				dev->id);
			pr_err("ocrdma%d:Using VLAN 0 for this connection\n",
				dev->id);
		}
		eth.eth_type = cpu_to_be16(0x8100);
		eth.roce_eth_type = cpu_to_be16(proto_num);
		vlan_tag |= (dev->sl & 0x07) << OCRDMA_VID_PCP_SHIFT;
		eth.vlan_tag = cpu_to_be16(vlan_tag);
		eth_sz = sizeof(struct ocrdma_eth_vlan);
		*isvlan = true;
	} else {
		eth.eth_type = cpu_to_be16(proto_num);
		eth_sz = sizeof(struct ocrdma_eth_basic);
	}
	/* MAC */
	memcpy(&eth.smac[0], &dev->nic_info.mac_addr[0], ETH_ALEN);
	status = ocrdma_resolve_dmac(dev, attr, &eth.dmac[0]);
	if (status)
		return status;
	ah->sgid_index = attr->grh.sgid_index;
	/* Eth HDR */
	memcpy(&ah->av->eth_hdr, &eth, eth_sz);
	if (ah->hdr_type == RDMA_NETWORK_IPV4) {
		*((__be16 *)&ipv4) = htons((4 << 12) | (5 << 8) |
					   attr->grh.traffic_class);
		ipv4.id = cpu_to_be16(pdid);
		ipv4.frag_off = htons(IP_DF);
		ipv4.tot_len = htons(0);
		ipv4.ttl = attr->grh.hop_limit;
		ipv4.protocol = nxthdr;
		rdma_gid2ip(&sgid_addr._sockaddr, sgid);
		ipv4.saddr = sgid_addr._sockaddr_in.sin_addr.s_addr;
		rdma_gid2ip(&dgid_addr._sockaddr, &attr->grh.dgid);
		ipv4.daddr = dgid_addr._sockaddr_in.sin_addr.s_addr;
		memcpy((u8 *)ah->av + eth_sz, &ipv4, sizeof(struct iphdr));
	} else {
		memcpy(&grh.sgid[0], sgid->raw, sizeof(union ib_gid));
		grh.tclass_flow = cpu_to_be32((6 << 28) |
					      (attr->grh.traffic_class << 24) |
					      attr->grh.flow_label);
		memcpy(&grh.dgid[0], attr->grh.dgid.raw,
		       sizeof(attr->grh.dgid.raw));
		grh.pdid_hoplimit = cpu_to_be32((pdid << 16) |
						(nxthdr << 8) |
						attr->grh.hop_limit);
		memcpy((u8 *)ah->av + eth_sz, &grh, sizeof(struct ocrdma_grh));
	}
	if (*isvlan)
		ah->av->valid |= OCRDMA_AV_VLAN_VALID;
	ah->av->valid = cpu_to_le32(ah->av->valid);
	return status;
}

struct ib_ah *ocrdma_create_ah(struct ib_pd *ibpd, struct ib_ah_attr *attr)
{
	u32 *ahid_addr;
	int status;
	struct ocrdma_ah *ah;
	bool isvlan = false;
	u16 vlan_tag = 0xffff;
	struct ib_gid_attr sgid_attr;
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibpd->device);
	union ib_gid sgid;

	if (!(attr->ah_flags & IB_AH_GRH))
		return ERR_PTR(-EINVAL);

	if (atomic_cmpxchg(&dev->update_sl, 1, 0))
		ocrdma_init_service_level(dev);

	ah = kzalloc(sizeof(*ah), GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	status = ocrdma_alloc_av(dev, ah);
	if (status)
		goto av_err;

	status = ib_get_cached_gid(&dev->ibdev, 1, attr->grh.sgid_index, &sgid,
				   &sgid_attr);
	if (status) {
		pr_err("%s(): Failed to query sgid, status = %d\n",
		      __func__, status);
		goto av_conf_err;
	}
	if (sgid_attr.ndev) {
		if (is_vlan_dev(sgid_attr.ndev))
			vlan_tag = vlan_dev_vlan_id(sgid_attr.ndev);
		dev_put(sgid_attr.ndev);
	}
	/* Get network header type for this GID */
	ah->hdr_type = ib_gid_to_network_type(sgid_attr.gid_type, &sgid);

	if ((pd->uctx) &&
	    (!rdma_is_multicast_addr((struct in6_addr *)attr->grh.dgid.raw)) &&
	    (!rdma_link_local_addr((struct in6_addr *)attr->grh.dgid.raw))) {
		status = rdma_addr_find_l2_eth_by_grh(&sgid, &attr->grh.dgid,
						      attr->dmac, &vlan_tag,
						      &sgid_attr.ndev->ifindex,
						      NULL);
		if (status) {
			pr_err("%s(): Failed to resolve dmac from gid." 
				"status = %d\n", __func__, status);
			goto av_conf_err;
		}
	}

	status = set_av_attr(dev, ah, attr, &sgid, pd->id, &isvlan, vlan_tag);
	if (status)
		goto av_conf_err;

	/* if pd is for the user process, pass the ah_id to user space */
	if ((pd->uctx) && (pd->uctx->ah_tbl.va)) {
		ahid_addr = pd->uctx->ah_tbl.va + attr->dlid;
		*ahid_addr = 0;
		*ahid_addr |= ah->id & OCRDMA_AH_ID_MASK;
		if (ocrdma_is_udp_encap_supported(dev)) {
			*ahid_addr |= ((u32)ah->hdr_type &
				       OCRDMA_AH_L3_TYPE_MASK) <<
				       OCRDMA_AH_L3_TYPE_SHIFT;
		}
		if (isvlan)
			*ahid_addr |= (OCRDMA_AH_VLAN_VALID_MASK <<
				       OCRDMA_AH_VLAN_VALID_SHIFT);
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
	struct ocrdma_dev *dev = get_ocrdma_dev(ibah->device);

	ocrdma_free_av(dev, ah);
	kfree(ah);
	return 0;
}

int ocrdma_query_ah(struct ib_ah *ibah, struct ib_ah_attr *attr)
{
	struct ocrdma_ah *ah = get_ocrdma_ah(ibah);
	struct ocrdma_av *av = ah->av;
	struct ocrdma_grh *grh;
	attr->ah_flags |= IB_AH_GRH;
	if (ah->av->valid & OCRDMA_AV_VALID) {
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
		       const struct ib_wc *in_wc,
		       const struct ib_grh *in_grh,
		       const struct ib_mad_hdr *in, size_t in_mad_size,
		       struct ib_mad_hdr *out, size_t *out_mad_size,
		       u16 *out_mad_pkey_index)
{
	int status;
	struct ocrdma_dev *dev;
	const struct ib_mad *in_mad = (const struct ib_mad *)in;
	struct ib_mad *out_mad = (struct ib_mad *)out;

	if (WARN_ON_ONCE(in_mad_size != sizeof(*in_mad) ||
			 *out_mad_size != sizeof(*out_mad)))
		return IB_MAD_RESULT_FAILURE;

	switch (in_mad->mad_hdr.mgmt_class) {
	case IB_MGMT_CLASS_PERF_MGMT:
		dev = get_ocrdma_dev(ibdev);
		if (!ocrdma_pma_counters(dev, out_mad))
			status = IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
		else
			status = IB_MAD_RESULT_SUCCESS;
		break;
	default:
		status = IB_MAD_RESULT_SUCCESS;
		break;
	}
	return status;
}
