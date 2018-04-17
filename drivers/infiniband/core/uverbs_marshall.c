/*
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
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

#include <linux/export.h>
#include <rdma/ib_marshall.h>

#define OPA_DEFAULT_GID_PREFIX cpu_to_be64(0xfe80000000000000ULL)
static int rdma_ah_conv_opa_to_ib(struct ib_device *dev,
				  struct rdma_ah_attr *ib,
				  struct rdma_ah_attr *opa)
{
	struct ib_port_attr port_attr;
	int ret = 0;

	/* Do structure copy and the over-write fields */
	*ib = *opa;

	ib->type = RDMA_AH_ATTR_TYPE_IB;
	rdma_ah_set_grh(ib, NULL, 0, 0, 1, 0);

	if (ib_query_port(dev, opa->port_num, &port_attr)) {
		/* Set to default subnet to indicate error */
		rdma_ah_set_subnet_prefix(ib, OPA_DEFAULT_GID_PREFIX);
		ret = -EINVAL;
	} else {
		rdma_ah_set_subnet_prefix(ib,
					  cpu_to_be64(port_attr.subnet_prefix));
	}
	rdma_ah_set_interface_id(ib, OPA_MAKE_ID(rdma_ah_get_dlid(opa)));
	return ret;
}

void ib_copy_ah_attr_to_user(struct ib_device *device,
			     struct ib_uverbs_ah_attr *dst,
			     struct rdma_ah_attr *ah_attr)
{
	struct rdma_ah_attr *src = ah_attr;
	struct rdma_ah_attr conv_ah;

	memset(&dst->grh.reserved, 0, sizeof(dst->grh.reserved));

	if ((ah_attr->type == RDMA_AH_ATTR_TYPE_OPA) &&
	    (rdma_ah_get_dlid(ah_attr) > be16_to_cpu(IB_LID_PERMISSIVE)) &&
	    (!rdma_ah_conv_opa_to_ib(device, &conv_ah, ah_attr)))
		src = &conv_ah;

	dst->dlid		   = rdma_ah_get_dlid(src);
	dst->sl			   = rdma_ah_get_sl(src);
	dst->src_path_bits	   = rdma_ah_get_path_bits(src);
	dst->static_rate	   = rdma_ah_get_static_rate(src);
	dst->is_global             = rdma_ah_get_ah_flags(src) &
					IB_AH_GRH ? 1 : 0;
	if (dst->is_global) {
		const struct ib_global_route *grh = rdma_ah_read_grh(src);

		memcpy(dst->grh.dgid, grh->dgid.raw, sizeof(grh->dgid));
		dst->grh.flow_label        = grh->flow_label;
		dst->grh.sgid_index        = grh->sgid_index;
		dst->grh.hop_limit         = grh->hop_limit;
		dst->grh.traffic_class     = grh->traffic_class;
	}
	dst->port_num		   = rdma_ah_get_port_num(src);
	dst->reserved 		   = 0;
}
EXPORT_SYMBOL(ib_copy_ah_attr_to_user);

void ib_copy_qp_attr_to_user(struct ib_device *device,
			     struct ib_uverbs_qp_attr *dst,
			     struct ib_qp_attr *src)
{
	dst->qp_state	        = src->qp_state;
	dst->cur_qp_state	= src->cur_qp_state;
	dst->path_mtu		= src->path_mtu;
	dst->path_mig_state	= src->path_mig_state;
	dst->qkey		= src->qkey;
	dst->rq_psn		= src->rq_psn;
	dst->sq_psn		= src->sq_psn;
	dst->dest_qp_num	= src->dest_qp_num;
	dst->qp_access_flags	= src->qp_access_flags;

	dst->max_send_wr	= src->cap.max_send_wr;
	dst->max_recv_wr	= src->cap.max_recv_wr;
	dst->max_send_sge	= src->cap.max_send_sge;
	dst->max_recv_sge	= src->cap.max_recv_sge;
	dst->max_inline_data	= src->cap.max_inline_data;

	ib_copy_ah_attr_to_user(device, &dst->ah_attr, &src->ah_attr);
	ib_copy_ah_attr_to_user(device, &dst->alt_ah_attr, &src->alt_ah_attr);

	dst->pkey_index		= src->pkey_index;
	dst->alt_pkey_index	= src->alt_pkey_index;
	dst->en_sqd_async_notify = src->en_sqd_async_notify;
	dst->sq_draining	= src->sq_draining;
	dst->max_rd_atomic	= src->max_rd_atomic;
	dst->max_dest_rd_atomic	= src->max_dest_rd_atomic;
	dst->min_rnr_timer	= src->min_rnr_timer;
	dst->port_num		= src->port_num;
	dst->timeout		= src->timeout;
	dst->retry_cnt		= src->retry_cnt;
	dst->rnr_retry		= src->rnr_retry;
	dst->alt_port_num	= src->alt_port_num;
	dst->alt_timeout	= src->alt_timeout;
	memset(dst->reserved, 0, sizeof(dst->reserved));
}
EXPORT_SYMBOL(ib_copy_qp_attr_to_user);

static void __ib_copy_path_rec_to_user(struct ib_user_path_rec *dst,
				       struct sa_path_rec *src)
{
	memcpy(dst->dgid, src->dgid.raw, sizeof(src->dgid));
	memcpy(dst->sgid, src->sgid.raw, sizeof(src->sgid));

	dst->dlid		= htons(ntohl(sa_path_get_dlid(src)));
	dst->slid		= htons(ntohl(sa_path_get_slid(src)));
	dst->raw_traffic	= sa_path_get_raw_traffic(src);
	dst->flow_label		= src->flow_label;
	dst->hop_limit		= src->hop_limit;
	dst->traffic_class	= src->traffic_class;
	dst->reversible		= src->reversible;
	dst->numb_path		= src->numb_path;
	dst->pkey		= src->pkey;
	dst->sl			= src->sl;
	dst->mtu_selector	= src->mtu_selector;
	dst->mtu		= src->mtu;
	dst->rate_selector	= src->rate_selector;
	dst->rate		= src->rate;
	dst->packet_life_time	= src->packet_life_time;
	dst->preference		= src->preference;
	dst->packet_life_time_selector = src->packet_life_time_selector;
}

void ib_copy_path_rec_to_user(struct ib_user_path_rec *dst,
			      struct sa_path_rec *src)
{
	struct sa_path_rec rec;

	if (src->rec_type == SA_PATH_REC_TYPE_OPA) {
		sa_convert_path_opa_to_ib(&rec, src);
		__ib_copy_path_rec_to_user(dst, &rec);
		return;
	}
	__ib_copy_path_rec_to_user(dst, src);
}
EXPORT_SYMBOL(ib_copy_path_rec_to_user);

void ib_copy_path_rec_from_user(struct sa_path_rec *dst,
				struct ib_user_path_rec *src)
{
	u32 slid, dlid;

	memset(dst, 0, sizeof(*dst));
	if ((ib_is_opa_gid((union ib_gid *)src->sgid)) ||
	    (ib_is_opa_gid((union ib_gid *)src->dgid))) {
		dst->rec_type = SA_PATH_REC_TYPE_OPA;
		slid = opa_get_lid_from_gid((union ib_gid *)src->sgid);
		dlid = opa_get_lid_from_gid((union ib_gid *)src->dgid);
	} else {
		dst->rec_type = SA_PATH_REC_TYPE_IB;
		slid = ntohs(src->slid);
		dlid = ntohs(src->dlid);
	}
	memcpy(dst->dgid.raw, src->dgid, sizeof dst->dgid);
	memcpy(dst->sgid.raw, src->sgid, sizeof dst->sgid);

	sa_path_set_dlid(dst, dlid);
	sa_path_set_slid(dst, slid);
	sa_path_set_raw_traffic(dst, src->raw_traffic);
	dst->flow_label		= src->flow_label;
	dst->hop_limit		= src->hop_limit;
	dst->traffic_class	= src->traffic_class;
	dst->reversible		= src->reversible;
	dst->numb_path		= src->numb_path;
	dst->pkey		= src->pkey;
	dst->sl			= src->sl;
	dst->mtu_selector	= src->mtu_selector;
	dst->mtu		= src->mtu;
	dst->rate_selector	= src->rate_selector;
	dst->rate		= src->rate;
	dst->packet_life_time	= src->packet_life_time;
	dst->preference		= src->preference;
	dst->packet_life_time_selector = src->packet_life_time_selector;

	/* TODO: No need to set this */
	sa_path_set_dmac_zero(dst);
	sa_path_set_ndev(dst, NULL);
	sa_path_set_ifindex(dst, 0);
}
EXPORT_SYMBOL(ib_copy_path_rec_from_user);
