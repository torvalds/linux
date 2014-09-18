/*
 * Copyright (c) 2004, 2011 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING the madirectory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use source and binary forms, with or
 *     withmodification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retathe above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHWARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS THE
 * SOFTWARE.
 */
#if !defined(CM_MSGS_H)
#define CM_MSGS_H

#include <rdma/ib_mad.h>
#include <rdma/ib_cm.h>

/*
 * Parameters to routines below should be in network-byte order, and values
 * are returned in network-byte order.
 */

#define IB_CM_CLASS_VERSION	2 /* IB specification 1.2 */

enum cm_msg_sequence {
	CM_MSG_SEQUENCE_REQ,
	CM_MSG_SEQUENCE_LAP,
	CM_MSG_SEQUENCE_DREQ,
	CM_MSG_SEQUENCE_SIDR
};

struct cm_req_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 rsvd4;
	__be64 service_id;
	__be64 local_ca_guid;
	__be32 rsvd24;
	__be32 local_qkey;
	/* local QPN:24, responder resources:8 */
	__be32 offset32;
	/* local EECN:24, initiator depth:8 */
	__be32 offset36;
	/*
	 * remote EECN:24, remote CM response timeout:5,
	 * transport service type:2, end-to-end flow control:1
	 */
	__be32 offset40;
	/* starting PSN:24, local CM response timeout:5, retry count:3 */
	__be32 offset44;
	__be16 pkey;
	/* path MTU:4, RDC exists:1, RNR retry count:3. */
	u8 offset50;
	/* max CM Retries:4, SRQ:1, extended transport type:3 */
	u8 offset51;

	__be16 primary_local_lid;
	__be16 primary_remote_lid;
	union ib_gid primary_local_gid;
	union ib_gid primary_remote_gid;
	/* flow label:20, rsvd:6, packet rate:6 */
	__be32 primary_offset88;
	u8 primary_traffic_class;
	u8 primary_hop_limit;
	/* SL:4, subnet local:1, rsvd:3 */
	u8 primary_offset94;
	/* local ACK timeout:5, rsvd:3 */
	u8 primary_offset95;

	__be16 alt_local_lid;
	__be16 alt_remote_lid;
	union ib_gid alt_local_gid;
	union ib_gid alt_remote_gid;
	/* flow label:20, rsvd:6, packet rate:6 */
	__be32 alt_offset132;
	u8 alt_traffic_class;
	u8 alt_hop_limit;
	/* SL:4, subnet local:1, rsvd:3 */
	u8 alt_offset138;
	/* local ACK timeout:5, rsvd:3 */
	u8 alt_offset139;

	u8 private_data[IB_CM_REQ_PRIVATE_DATA_SIZE];

} __attribute__ ((packed));

static inline __be32 cm_req_get_local_qpn(struct cm_req_msg *req_msg)
{
	return cpu_to_be32(be32_to_cpu(req_msg->offset32) >> 8);
}

static inline void cm_req_set_local_qpn(struct cm_req_msg *req_msg, __be32 qpn)
{
	req_msg->offset32 = cpu_to_be32((be32_to_cpu(qpn) << 8) |
					 (be32_to_cpu(req_msg->offset32) &
					  0x000000FF));
}

static inline u8 cm_req_get_resp_res(struct cm_req_msg *req_msg)
{
	return (u8) be32_to_cpu(req_msg->offset32);
}

static inline void cm_req_set_resp_res(struct cm_req_msg *req_msg, u8 resp_res)
{
	req_msg->offset32 = cpu_to_be32(resp_res |
					(be32_to_cpu(req_msg->offset32) &
					 0xFFFFFF00));
}

static inline u8 cm_req_get_init_depth(struct cm_req_msg *req_msg)
{
	return (u8) be32_to_cpu(req_msg->offset36);
}

static inline void cm_req_set_init_depth(struct cm_req_msg *req_msg,
					 u8 init_depth)
{
	req_msg->offset36 = cpu_to_be32(init_depth |
					(be32_to_cpu(req_msg->offset36) &
					 0xFFFFFF00));
}

static inline u8 cm_req_get_remote_resp_timeout(struct cm_req_msg *req_msg)
{
	return (u8) ((be32_to_cpu(req_msg->offset40) & 0xF8) >> 3);
}

static inline void cm_req_set_remote_resp_timeout(struct cm_req_msg *req_msg,
						  u8 resp_timeout)
{
	req_msg->offset40 = cpu_to_be32((resp_timeout << 3) |
					 (be32_to_cpu(req_msg->offset40) &
					  0xFFFFFF07));
}

static inline enum ib_qp_type cm_req_get_qp_type(struct cm_req_msg *req_msg)
{
	u8 transport_type = (u8) (be32_to_cpu(req_msg->offset40) & 0x06) >> 1;
	switch(transport_type) {
	case 0: return IB_QPT_RC;
	case 1: return IB_QPT_UC;
	case 3:
		switch (req_msg->offset51 & 0x7) {
		case 1: return IB_QPT_XRC_TGT;
		default: return 0;
		}
	default: return 0;
	}
}

static inline void cm_req_set_qp_type(struct cm_req_msg *req_msg,
				      enum ib_qp_type qp_type)
{
	switch(qp_type) {
	case IB_QPT_UC:
		req_msg->offset40 = cpu_to_be32((be32_to_cpu(
						  req_msg->offset40) &
						   0xFFFFFFF9) | 0x2);
		break;
	case IB_QPT_XRC_INI:
		req_msg->offset40 = cpu_to_be32((be32_to_cpu(
						 req_msg->offset40) &
						   0xFFFFFFF9) | 0x6);
		req_msg->offset51 = (req_msg->offset51 & 0xF8) | 1;
		break;
	default:
		req_msg->offset40 = cpu_to_be32(be32_to_cpu(
						 req_msg->offset40) &
						  0xFFFFFFF9);
	}
}

static inline u8 cm_req_get_flow_ctrl(struct cm_req_msg *req_msg)
{
	return be32_to_cpu(req_msg->offset40) & 0x1;
}

static inline void cm_req_set_flow_ctrl(struct cm_req_msg *req_msg,
					u8 flow_ctrl)
{
	req_msg->offset40 = cpu_to_be32((flow_ctrl & 0x1) |
					 (be32_to_cpu(req_msg->offset40) &
					  0xFFFFFFFE));
}

static inline __be32 cm_req_get_starting_psn(struct cm_req_msg *req_msg)
{
	return cpu_to_be32(be32_to_cpu(req_msg->offset44) >> 8);
}

static inline void cm_req_set_starting_psn(struct cm_req_msg *req_msg,
					   __be32 starting_psn)
{
	req_msg->offset44 = cpu_to_be32((be32_to_cpu(starting_psn) << 8) |
			    (be32_to_cpu(req_msg->offset44) & 0x000000FF));
}

static inline u8 cm_req_get_local_resp_timeout(struct cm_req_msg *req_msg)
{
	return (u8) ((be32_to_cpu(req_msg->offset44) & 0xF8) >> 3);
}

static inline void cm_req_set_local_resp_timeout(struct cm_req_msg *req_msg,
						 u8 resp_timeout)
{
	req_msg->offset44 = cpu_to_be32((resp_timeout << 3) |
			    (be32_to_cpu(req_msg->offset44) & 0xFFFFFF07));
}

static inline u8 cm_req_get_retry_count(struct cm_req_msg *req_msg)
{
	return (u8) (be32_to_cpu(req_msg->offset44) & 0x7);
}

static inline void cm_req_set_retry_count(struct cm_req_msg *req_msg,
					  u8 retry_count)
{
	req_msg->offset44 = cpu_to_be32((retry_count & 0x7) |
			    (be32_to_cpu(req_msg->offset44) & 0xFFFFFFF8));
}

static inline u8 cm_req_get_path_mtu(struct cm_req_msg *req_msg)
{
	return req_msg->offset50 >> 4;
}

static inline void cm_req_set_path_mtu(struct cm_req_msg *req_msg, u8 path_mtu)
{
	req_msg->offset50 = (u8) ((req_msg->offset50 & 0xF) | (path_mtu << 4));
}

static inline u8 cm_req_get_rnr_retry_count(struct cm_req_msg *req_msg)
{
	return req_msg->offset50 & 0x7;
}

static inline void cm_req_set_rnr_retry_count(struct cm_req_msg *req_msg,
					      u8 rnr_retry_count)
{
	req_msg->offset50 = (u8) ((req_msg->offset50 & 0xF8) |
				  (rnr_retry_count & 0x7));
}

static inline u8 cm_req_get_max_cm_retries(struct cm_req_msg *req_msg)
{
	return req_msg->offset51 >> 4;
}

static inline void cm_req_set_max_cm_retries(struct cm_req_msg *req_msg,
					     u8 retries)
{
	req_msg->offset51 = (u8) ((req_msg->offset51 & 0xF) | (retries << 4));
}

static inline u8 cm_req_get_srq(struct cm_req_msg *req_msg)
{
	return (req_msg->offset51 & 0x8) >> 3;
}

static inline void cm_req_set_srq(struct cm_req_msg *req_msg, u8 srq)
{
	req_msg->offset51 = (u8) ((req_msg->offset51 & 0xF7) |
				  ((srq & 0x1) << 3));
}

static inline __be32 cm_req_get_primary_flow_label(struct cm_req_msg *req_msg)
{
	return cpu_to_be32(be32_to_cpu(req_msg->primary_offset88) >> 12);
}

static inline void cm_req_set_primary_flow_label(struct cm_req_msg *req_msg,
						 __be32 flow_label)
{
	req_msg->primary_offset88 = cpu_to_be32(
				    (be32_to_cpu(req_msg->primary_offset88) &
				     0x00000FFF) |
				     (be32_to_cpu(flow_label) << 12));
}

static inline u8 cm_req_get_primary_packet_rate(struct cm_req_msg *req_msg)
{
	return (u8) (be32_to_cpu(req_msg->primary_offset88) & 0x3F);
}

static inline void cm_req_set_primary_packet_rate(struct cm_req_msg *req_msg,
						  u8 rate)
{
	req_msg->primary_offset88 = cpu_to_be32(
				    (be32_to_cpu(req_msg->primary_offset88) &
				     0xFFFFFFC0) | (rate & 0x3F));
}

static inline u8 cm_req_get_primary_sl(struct cm_req_msg *req_msg)
{
	return (u8) (req_msg->primary_offset94 >> 4);
}

static inline void cm_req_set_primary_sl(struct cm_req_msg *req_msg, u8 sl)
{
	req_msg->primary_offset94 = (u8) ((req_msg->primary_offset94 & 0x0F) |
					  (sl << 4));
}

static inline u8 cm_req_get_primary_subnet_local(struct cm_req_msg *req_msg)
{
	return (u8) ((req_msg->primary_offset94 & 0x08) >> 3);
}

static inline void cm_req_set_primary_subnet_local(struct cm_req_msg *req_msg,
						   u8 subnet_local)
{
	req_msg->primary_offset94 = (u8) ((req_msg->primary_offset94 & 0xF7) |
					  ((subnet_local & 0x1) << 3));
}

static inline u8 cm_req_get_primary_local_ack_timeout(struct cm_req_msg *req_msg)
{
	return (u8) (req_msg->primary_offset95 >> 3);
}

static inline void cm_req_set_primary_local_ack_timeout(struct cm_req_msg *req_msg,
							u8 local_ack_timeout)
{
	req_msg->primary_offset95 = (u8) ((req_msg->primary_offset95 & 0x07) |
					  (local_ack_timeout << 3));
}

static inline __be32 cm_req_get_alt_flow_label(struct cm_req_msg *req_msg)
{
	return cpu_to_be32(be32_to_cpu(req_msg->alt_offset132) >> 12);
}

static inline void cm_req_set_alt_flow_label(struct cm_req_msg *req_msg,
					     __be32 flow_label)
{
	req_msg->alt_offset132 = cpu_to_be32(
				 (be32_to_cpu(req_msg->alt_offset132) &
				  0x00000FFF) |
				  (be32_to_cpu(flow_label) << 12));
}

static inline u8 cm_req_get_alt_packet_rate(struct cm_req_msg *req_msg)
{
	return (u8) (be32_to_cpu(req_msg->alt_offset132) & 0x3F);
}

static inline void cm_req_set_alt_packet_rate(struct cm_req_msg *req_msg,
					      u8 rate)
{
	req_msg->alt_offset132 = cpu_to_be32(
				 (be32_to_cpu(req_msg->alt_offset132) &
				  0xFFFFFFC0) | (rate & 0x3F));
}

static inline u8 cm_req_get_alt_sl(struct cm_req_msg *req_msg)
{
	return (u8) (req_msg->alt_offset138 >> 4);
}

static inline void cm_req_set_alt_sl(struct cm_req_msg *req_msg, u8 sl)
{
	req_msg->alt_offset138 = (u8) ((req_msg->alt_offset138 & 0x0F) |
				       (sl << 4));
}

static inline u8 cm_req_get_alt_subnet_local(struct cm_req_msg *req_msg)
{
	return (u8) ((req_msg->alt_offset138 & 0x08) >> 3);
}

static inline void cm_req_set_alt_subnet_local(struct cm_req_msg *req_msg,
					       u8 subnet_local)
{
	req_msg->alt_offset138 = (u8) ((req_msg->alt_offset138 & 0xF7) |
				       ((subnet_local & 0x1) << 3));
}

static inline u8 cm_req_get_alt_local_ack_timeout(struct cm_req_msg *req_msg)
{
	return (u8) (req_msg->alt_offset139 >> 3);
}

static inline void cm_req_set_alt_local_ack_timeout(struct cm_req_msg *req_msg,
						    u8 local_ack_timeout)
{
	req_msg->alt_offset139 = (u8) ((req_msg->alt_offset139 & 0x07) |
				       (local_ack_timeout << 3));
}

/* Message REJected or MRAed */
enum cm_msg_response {
	CM_MSG_RESPONSE_REQ = 0x0,
	CM_MSG_RESPONSE_REP = 0x1,
	CM_MSG_RESPONSE_OTHER = 0x2
};

 struct cm_mra_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;
	/* message MRAed:2, rsvd:6 */
	u8 offset8;
	/* service timeout:5, rsvd:3 */
	u8 offset9;

	u8 private_data[IB_CM_MRA_PRIVATE_DATA_SIZE];

} __attribute__ ((packed));

static inline u8 cm_mra_get_msg_mraed(struct cm_mra_msg *mra_msg)
{
	return (u8) (mra_msg->offset8 >> 6);
}

static inline void cm_mra_set_msg_mraed(struct cm_mra_msg *mra_msg, u8 msg)
{
	mra_msg->offset8 = (u8) ((mra_msg->offset8 & 0x3F) | (msg << 6));
}

static inline u8 cm_mra_get_service_timeout(struct cm_mra_msg *mra_msg)
{
	return (u8) (mra_msg->offset9 >> 3);
}

static inline void cm_mra_set_service_timeout(struct cm_mra_msg *mra_msg,
					      u8 service_timeout)
{
	mra_msg->offset9 = (u8) ((mra_msg->offset9 & 0x07) |
				 (service_timeout << 3));
}

struct cm_rej_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;
	/* message REJected:2, rsvd:6 */
	u8 offset8;
	/* reject info length:7, rsvd:1. */
	u8 offset9;
	__be16 reason;
	u8 ari[IB_CM_REJ_ARI_LENGTH];

	u8 private_data[IB_CM_REJ_PRIVATE_DATA_SIZE];

} __attribute__ ((packed));

static inline u8 cm_rej_get_msg_rejected(struct cm_rej_msg *rej_msg)
{
	return (u8) (rej_msg->offset8 >> 6);
}

static inline void cm_rej_set_msg_rejected(struct cm_rej_msg *rej_msg, u8 msg)
{
	rej_msg->offset8 = (u8) ((rej_msg->offset8 & 0x3F) | (msg << 6));
}

static inline u8 cm_rej_get_reject_info_len(struct cm_rej_msg *rej_msg)
{
	return (u8) (rej_msg->offset9 >> 1);
}

static inline void cm_rej_set_reject_info_len(struct cm_rej_msg *rej_msg,
					      u8 len)
{
	rej_msg->offset9 = (u8) ((rej_msg->offset9 & 0x1) | (len << 1));
}

struct cm_rep_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;
	__be32 local_qkey;
	/* local QPN:24, rsvd:8 */
	__be32 offset12;
	/* local EECN:24, rsvd:8 */
	__be32 offset16;
	/* starting PSN:24 rsvd:8 */
	__be32 offset20;
	u8 resp_resources;
	u8 initiator_depth;
	/* target ACK delay:5, failover accepted:2, end-to-end flow control:1 */
	u8 offset26;
	/* RNR retry count:3, SRQ:1, rsvd:5 */
	u8 offset27;
	__be64 local_ca_guid;

	u8 private_data[IB_CM_REP_PRIVATE_DATA_SIZE];

} __attribute__ ((packed));

static inline __be32 cm_rep_get_local_qpn(struct cm_rep_msg *rep_msg)
{
	return cpu_to_be32(be32_to_cpu(rep_msg->offset12) >> 8);
}

static inline void cm_rep_set_local_qpn(struct cm_rep_msg *rep_msg, __be32 qpn)
{
	rep_msg->offset12 = cpu_to_be32((be32_to_cpu(qpn) << 8) |
			    (be32_to_cpu(rep_msg->offset12) & 0x000000FF));
}

static inline __be32 cm_rep_get_local_eecn(struct cm_rep_msg *rep_msg)
{
	return cpu_to_be32(be32_to_cpu(rep_msg->offset16) >> 8);
}

static inline void cm_rep_set_local_eecn(struct cm_rep_msg *rep_msg, __be32 eecn)
{
	rep_msg->offset16 = cpu_to_be32((be32_to_cpu(eecn) << 8) |
			    (be32_to_cpu(rep_msg->offset16) & 0x000000FF));
}

static inline __be32 cm_rep_get_qpn(struct cm_rep_msg *rep_msg, enum ib_qp_type qp_type)
{
	return (qp_type == IB_QPT_XRC_INI) ?
		cm_rep_get_local_eecn(rep_msg) : cm_rep_get_local_qpn(rep_msg);
}

static inline __be32 cm_rep_get_starting_psn(struct cm_rep_msg *rep_msg)
{
	return cpu_to_be32(be32_to_cpu(rep_msg->offset20) >> 8);
}

static inline void cm_rep_set_starting_psn(struct cm_rep_msg *rep_msg,
					   __be32 starting_psn)
{
	rep_msg->offset20 = cpu_to_be32((be32_to_cpu(starting_psn) << 8) |
			    (be32_to_cpu(rep_msg->offset20) & 0x000000FF));
}

static inline u8 cm_rep_get_target_ack_delay(struct cm_rep_msg *rep_msg)
{
	return (u8) (rep_msg->offset26 >> 3);
}

static inline void cm_rep_set_target_ack_delay(struct cm_rep_msg *rep_msg,
					       u8 target_ack_delay)
{
	rep_msg->offset26 = (u8) ((rep_msg->offset26 & 0x07) |
				  (target_ack_delay << 3));
}

static inline u8 cm_rep_get_failover(struct cm_rep_msg *rep_msg)
{
	return (u8) ((rep_msg->offset26 & 0x06) >> 1);
}

static inline void cm_rep_set_failover(struct cm_rep_msg *rep_msg, u8 failover)
{
	rep_msg->offset26 = (u8) ((rep_msg->offset26 & 0xF9) |
				  ((failover & 0x3) << 1));
}

static inline u8 cm_rep_get_flow_ctrl(struct cm_rep_msg *rep_msg)
{
	return (u8) (rep_msg->offset26 & 0x01);
}

static inline void cm_rep_set_flow_ctrl(struct cm_rep_msg *rep_msg,
					    u8 flow_ctrl)
{
	rep_msg->offset26 = (u8) ((rep_msg->offset26 & 0xFE) |
				  (flow_ctrl & 0x1));
}

static inline u8 cm_rep_get_rnr_retry_count(struct cm_rep_msg *rep_msg)
{
	return (u8) (rep_msg->offset27 >> 5);
}

static inline void cm_rep_set_rnr_retry_count(struct cm_rep_msg *rep_msg,
					      u8 rnr_retry_count)
{
	rep_msg->offset27 = (u8) ((rep_msg->offset27 & 0x1F) |
				  (rnr_retry_count << 5));
}

static inline u8 cm_rep_get_srq(struct cm_rep_msg *rep_msg)
{
	return (u8) ((rep_msg->offset27 >> 4) & 0x1);
}

static inline void cm_rep_set_srq(struct cm_rep_msg *rep_msg, u8 srq)
{
	rep_msg->offset27 = (u8) ((rep_msg->offset27 & 0xEF) |
				  ((srq & 0x1) << 4));
}

struct cm_rtu_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;

	u8 private_data[IB_CM_RTU_PRIVATE_DATA_SIZE];

} __attribute__ ((packed));

struct cm_dreq_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;
	/* remote QPN/EECN:24, rsvd:8 */
	__be32 offset8;

	u8 private_data[IB_CM_DREQ_PRIVATE_DATA_SIZE];

} __attribute__ ((packed));

static inline __be32 cm_dreq_get_remote_qpn(struct cm_dreq_msg *dreq_msg)
{
	return cpu_to_be32(be32_to_cpu(dreq_msg->offset8) >> 8);
}

static inline void cm_dreq_set_remote_qpn(struct cm_dreq_msg *dreq_msg, __be32 qpn)
{
	dreq_msg->offset8 = cpu_to_be32((be32_to_cpu(qpn) << 8) |
			    (be32_to_cpu(dreq_msg->offset8) & 0x000000FF));
}

struct cm_drep_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;

	u8 private_data[IB_CM_DREP_PRIVATE_DATA_SIZE];

} __attribute__ ((packed));

struct cm_lap_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;

	__be32 rsvd8;
	/* remote QPN/EECN:24, remote CM response timeout:5, rsvd:3 */
	__be32 offset12;
	__be32 rsvd16;

	__be16 alt_local_lid;
	__be16 alt_remote_lid;
	union ib_gid alt_local_gid;
	union ib_gid alt_remote_gid;
	/* flow label:20, rsvd:4, traffic class:8 */
	__be32 offset56;
	u8 alt_hop_limit;
	/* rsvd:2, packet rate:6 */
	u8 offset61;
	/* SL:4, subnet local:1, rsvd:3 */
	u8 offset62;
	/* local ACK timeout:5, rsvd:3 */
	u8 offset63;

	u8 private_data[IB_CM_LAP_PRIVATE_DATA_SIZE];
} __attribute__  ((packed));

static inline __be32 cm_lap_get_remote_qpn(struct cm_lap_msg *lap_msg)
{
	return cpu_to_be32(be32_to_cpu(lap_msg->offset12) >> 8);
}

static inline void cm_lap_set_remote_qpn(struct cm_lap_msg *lap_msg, __be32 qpn)
{
	lap_msg->offset12 = cpu_to_be32((be32_to_cpu(qpn) << 8) |
					 (be32_to_cpu(lap_msg->offset12) &
					  0x000000FF));
}

static inline u8 cm_lap_get_remote_resp_timeout(struct cm_lap_msg *lap_msg)
{
	return (u8) ((be32_to_cpu(lap_msg->offset12) & 0xF8) >> 3);
}

static inline void cm_lap_set_remote_resp_timeout(struct cm_lap_msg *lap_msg,
						  u8 resp_timeout)
{
	lap_msg->offset12 = cpu_to_be32((resp_timeout << 3) |
					 (be32_to_cpu(lap_msg->offset12) &
					  0xFFFFFF07));
}

static inline __be32 cm_lap_get_flow_label(struct cm_lap_msg *lap_msg)
{
	return cpu_to_be32(be32_to_cpu(lap_msg->offset56) >> 12);
}

static inline void cm_lap_set_flow_label(struct cm_lap_msg *lap_msg,
					 __be32 flow_label)
{
	lap_msg->offset56 = cpu_to_be32(
				 (be32_to_cpu(lap_msg->offset56) & 0x00000FFF) |
				 (be32_to_cpu(flow_label) << 12));
}

static inline u8 cm_lap_get_traffic_class(struct cm_lap_msg *lap_msg)
{
	return (u8) be32_to_cpu(lap_msg->offset56);
}

static inline void cm_lap_set_traffic_class(struct cm_lap_msg *lap_msg,
					    u8 traffic_class)
{
	lap_msg->offset56 = cpu_to_be32(traffic_class |
					 (be32_to_cpu(lap_msg->offset56) &
					  0xFFFFFF00));
}

static inline u8 cm_lap_get_packet_rate(struct cm_lap_msg *lap_msg)
{
	return lap_msg->offset61 & 0x3F;
}

static inline void cm_lap_set_packet_rate(struct cm_lap_msg *lap_msg,
					  u8 packet_rate)
{
	lap_msg->offset61 = (packet_rate & 0x3F) | (lap_msg->offset61 & 0xC0);
}

static inline u8 cm_lap_get_sl(struct cm_lap_msg *lap_msg)
{
	return lap_msg->offset62 >> 4;
}

static inline void cm_lap_set_sl(struct cm_lap_msg *lap_msg, u8 sl)
{
	lap_msg->offset62 = (sl << 4) | (lap_msg->offset62 & 0x0F);
}

static inline u8 cm_lap_get_subnet_local(struct cm_lap_msg *lap_msg)
{
	return (lap_msg->offset62 >> 3) & 0x1;
}

static inline void cm_lap_set_subnet_local(struct cm_lap_msg *lap_msg,
					   u8 subnet_local)
{
	lap_msg->offset62 = ((subnet_local & 0x1) << 3) |
			     (lap_msg->offset61 & 0xF7);
}
static inline u8 cm_lap_get_local_ack_timeout(struct cm_lap_msg *lap_msg)
{
	return lap_msg->offset63 >> 3;
}

static inline void cm_lap_set_local_ack_timeout(struct cm_lap_msg *lap_msg,
						u8 local_ack_timeout)
{
	lap_msg->offset63 = (local_ack_timeout << 3) |
			    (lap_msg->offset63 & 0x07);
}

struct cm_apr_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;

	u8 info_length;
	u8 ap_status;
	__be16 rsvd;
	u8 info[IB_CM_APR_INFO_LENGTH];

	u8 private_data[IB_CM_APR_PRIVATE_DATA_SIZE];
} __attribute__ ((packed));

struct cm_sidr_req_msg {
	struct ib_mad_hdr hdr;

	__be32 request_id;
	__be16 pkey;
	__be16 rsvd;
	__be64 service_id;

	u8 private_data[IB_CM_SIDR_REQ_PRIVATE_DATA_SIZE];
} __attribute__ ((packed));

struct cm_sidr_rep_msg {
	struct ib_mad_hdr hdr;

	__be32 request_id;
	u8 status;
	u8 info_length;
	__be16 rsvd;
	/* QPN:24, rsvd:8 */
	__be32 offset8;
	__be64 service_id;
	__be32 qkey;
	u8 info[IB_CM_SIDR_REP_INFO_LENGTH];

	u8 private_data[IB_CM_SIDR_REP_PRIVATE_DATA_SIZE];
} __attribute__ ((packed));

static inline __be32 cm_sidr_rep_get_qpn(struct cm_sidr_rep_msg *sidr_rep_msg)
{
	return cpu_to_be32(be32_to_cpu(sidr_rep_msg->offset8) >> 8);
}

static inline void cm_sidr_rep_set_qpn(struct cm_sidr_rep_msg *sidr_rep_msg,
				       __be32 qpn)
{
	sidr_rep_msg->offset8 = cpu_to_be32((be32_to_cpu(qpn) << 8) |
					(be32_to_cpu(sidr_rep_msg->offset8) &
					 0x000000FF));
}

#endif /* CM_MSGS_H */
