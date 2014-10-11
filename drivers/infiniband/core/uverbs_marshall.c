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

void ib_copy_ah_attr_to_user(struct ib_uverbs_ah_attr *dst,
			     struct ib_ah_attr *src)
{
	memcpy(dst->grh.dgid, src->grh.dgid.raw, sizeof src->grh.dgid);
	dst->grh.flow_label        = src->grh.flow_label;
	dst->grh.sgid_index        = src->grh.sgid_index;
	dst->grh.hop_limit         = src->grh.hop_limit;
	dst->grh.traffic_class     = src->grh.traffic_class;
	memset(&dst->grh.reserved, 0, sizeof(dst->grh.reserved));
	dst->dlid 	    	   = src->dlid;
	dst->sl   	    	   = src->sl;
	dst->src_path_bits 	   = src->src_path_bits;
	dst->static_rate   	   = src->static_rate;
	dst->is_global             = src->ah_flags & IB_AH_GRH ? 1 : 0;
	dst->port_num 	    	   = src->port_num;
	dst->reserved 		   = 0;
}
EXPORT_SYMBOL(ib_copy_ah_attr_to_user);

void ib_copy_qp_attr_to_user(struct ib_uverbs_qp_attr *dst,
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

	ib_copy_ah_attr_to_user(&dst->ah_attr, &src->ah_attr);
	ib_copy_ah_attr_to_user(&dst->alt_ah_attr, &src->alt_ah_attr);

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

void ib_copy_path_rec_to_user(struct ib_user_path_rec *dst,
			      struct ib_sa_path_rec *src)
{
	memcpy(dst->dgid, src->dgid.raw, sizeof src->dgid);
	memcpy(dst->sgid, src->sgid.raw, sizeof src->sgid);

	dst->dlid		= src->dlid;
	dst->slid		= src->slid;
	dst->raw_traffic	= src->raw_traffic;
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
EXPORT_SYMBOL(ib_copy_path_rec_to_user);

void ib_copy_path_rec_from_user(struct ib_sa_path_rec *dst,
				struct ib_user_path_rec *src)
{
	memcpy(dst->dgid.raw, src->dgid, sizeof dst->dgid);
	memcpy(dst->sgid.raw, src->sgid, sizeof dst->sgid);

	dst->dlid		= src->dlid;
	dst->slid		= src->slid;
	dst->raw_traffic	= src->raw_traffic;
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

	memset(dst->smac, 0, sizeof(dst->smac));
	memset(dst->dmac, 0, sizeof(dst->dmac));
	dst->vlan_id = 0xffff;
}
EXPORT_SYMBOL(ib_copy_path_rec_from_user);
