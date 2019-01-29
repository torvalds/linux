/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	   Redistribution and use in source and binary forms, with or
 *	   without modification, are permitted provided that the following
 *	   conditions are met:
 *
 *		- Redistributions of source code must retain the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer.
 *
 *		- Redistributions in binary form must reproduce the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer in the documentation and/or other materials
 *		  provided with the distribution.
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

#include "rxe.h"
#include "rxe_loc.h"

void rxe_init_av(struct rdma_ah_attr *attr, struct rxe_av *av)
{
	rxe_av_from_attr(rdma_ah_get_port_num(attr), av, attr);
	rxe_av_fill_ip_info(av, attr);
}

int rxe_av_chk_attr(struct rxe_dev *rxe, struct rdma_ah_attr *attr)
{
	struct rxe_port *port;

	port = &rxe->port;

	if (rdma_ah_get_ah_flags(attr) & IB_AH_GRH) {
		u8 sgid_index = rdma_ah_read_grh(attr)->sgid_index;

		if (sgid_index > port->attr.gid_tbl_len) {
			pr_warn("invalid sgid index = %d\n", sgid_index);
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

	rdma_gid2ip((struct sockaddr *)&av->sgid_addr, &sgid_attr->gid);
	rdma_gid2ip((struct sockaddr *)&av->dgid_addr,
		    &rdma_ah_read_grh(attr)->dgid);
	av->network_type = rdma_gid_attr_network_type(sgid_attr);
}

struct rxe_av *rxe_get_av(struct rxe_pkt_info *pkt)
{
	if (!pkt || !pkt->qp)
		return NULL;

	if (qp_type(pkt->qp) == IB_QPT_RC || qp_type(pkt->qp) == IB_QPT_UC)
		return &pkt->qp->pri_av;

	return (pkt->wqe) ? &pkt->wqe->av : NULL;
}
