// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"
#include "rxe_loc.h"

void rxe_init_av(struct rdma_ah_attr *attr, struct rxe_av *av)
{
	rxe_av_from_attr(rdma_ah_get_port_num(attr), av, attr);
	rxe_av_fill_ip_info(av, attr);
	memcpy(av->dmac, attr->roce.dmac, ETH_ALEN);
}

int rxe_av_chk_attr(struct rxe_dev *rxe, struct rdma_ah_attr *attr)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(attr);
	struct rxe_port *port;
	int type;

	port = &rxe->port;

	if (rdma_ah_get_ah_flags(attr) & IB_AH_GRH) {
		if (grh->sgid_index > port->attr.gid_tbl_len) {
			pr_warn("invalid sgid index = %d\n",
					grh->sgid_index);
			return -EINVAL;
		}

		type = rdma_gid_attr_network_type(grh->sgid_attr);
		if (type < RDMA_NETWORK_IPV4 ||
		    type > RDMA_NETWORK_IPV6) {
			pr_warn("invalid network type for rdma_rxe = %d\n",
					type);
			return -EINVAL;
		}
	}

	return 0;
}

void rxe_av_from_attr(u8 port_num, struct rxe_av *av,
		     struct rdma_ah_attr *attr)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(attr);

	memset(av, 0, sizeof(*av));
	memcpy(av->grh.dgid.raw, grh->dgid.raw, sizeof(grh->dgid.raw));
	av->grh.flow_label = grh->flow_label;
	av->grh.sgid_index = grh->sgid_index;
	av->grh.hop_limit = grh->hop_limit;
	av->grh.traffic_class = grh->traffic_class;
	av->port_num = port_num;
}

void rxe_av_to_attr(struct rxe_av *av, struct rdma_ah_attr *attr)
{
	struct ib_global_route *grh = rdma_ah_retrieve_grh(attr);

	attr->type = RDMA_AH_ATTR_TYPE_ROCE;

	memcpy(grh->dgid.raw, av->grh.dgid.raw, sizeof(av->grh.dgid.raw));
	grh->flow_label = av->grh.flow_label;
	grh->sgid_index = av->grh.sgid_index;
	grh->hop_limit = av->grh.hop_limit;
	grh->traffic_class = av->grh.traffic_class;

	rdma_ah_set_ah_flags(attr, IB_AH_GRH);
	rdma_ah_set_port_num(attr, av->port_num);
}

void rxe_av_fill_ip_info(struct rxe_av *av, struct rdma_ah_attr *attr)
{
	const struct ib_gid_attr *sgid_attr = attr->grh.sgid_attr;
	int ibtype;
	int type;

	rdma_gid2ip((struct sockaddr *)&av->sgid_addr, &sgid_attr->gid);
	rdma_gid2ip((struct sockaddr *)&av->dgid_addr,
		    &rdma_ah_read_grh(attr)->dgid);

	ibtype = rdma_gid_attr_network_type(sgid_attr);

	switch (ibtype) {
	case RDMA_NETWORK_IPV4:
		type = RXE_NETWORK_TYPE_IPV4;
		break;
	case RDMA_NETWORK_IPV6:
		type = RXE_NETWORK_TYPE_IPV6;
		break;
	default:
		/* not reached - checked in rxe_av_chk_attr */
		type = 0;
		break;
	}

	av->network_type = type;
}

struct rxe_av *rxe_get_av(struct rxe_pkt_info *pkt)
{
	struct rxe_ah *ah;
	u32 ah_num;

	if (!pkt || !pkt->qp)
		return NULL;

	if (qp_type(pkt->qp) == IB_QPT_RC || qp_type(pkt->qp) == IB_QPT_UC)
		return &pkt->qp->pri_av;

	if (!pkt->wqe)
		return NULL;

	ah_num = pkt->wqe->wr.wr.ud.ah_num;
	if (ah_num) {
		/* only new user provider or kernel client */
		ah = rxe_pool_get_index(&pkt->rxe->ah_pool, ah_num);
		if (!ah || ah->ah_num != ah_num || rxe_ah_pd(ah) != pkt->qp->pd) {
			pr_warn("Unable to find AH matching ah_num\n");
			return NULL;
		}
		return &ah->av;
	}

	/* only old user provider for UD sends*/
	return &pkt->wqe->wr.wr.ud.av;
}
