/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2004, 2011 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2019, Mellanox Technologies inc.  All rights reserved.
 */
#ifndef CM_MSGS_H
#define CM_MSGS_H

#include <rdma/ibta_vol1_c12.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_cm.h>

/*
 * Parameters to routines below should be in network-byte order, and values
 * are returned in network-byte order.
 */

#define IB_CM_CLASS_VERSION	2 /* IB specification 1.2 */

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

	u32 private_data[IB_CM_REQ_PRIVATE_DATA_SIZE / sizeof(u32)];

} __packed;

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

static inline enum ib_qp_type cm_req_get_qp_type(struct cm_req_msg *req_msg)
{
	u8 transport_type = IBA_GET(CM_REQ_TRANSPORT_SERVICE_TYPE, req_msg);
	switch(transport_type) {
	case 0: return IB_QPT_RC;
	case 1: return IB_QPT_UC;
	case 3:
		switch (IBA_GET(CM_REQ_EXTENDED_TRANSPORT_TYPE, req_msg)) {
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
		IBA_SET(CM_REQ_TRANSPORT_SERVICE_TYPE, req_msg, 1);
		break;
	case IB_QPT_XRC_INI:
		IBA_SET(CM_REQ_TRANSPORT_SERVICE_TYPE, req_msg, 3);
		IBA_SET(CM_REQ_EXTENDED_TRANSPORT_TYPE, req_msg, 1);
		break;
	default:
		IBA_SET(CM_REQ_TRANSPORT_SERVICE_TYPE, req_msg, 0);
	}
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

} __packed;

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

} __packed;

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

} __packed;

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

struct cm_rtu_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;

	u8 private_data[IB_CM_RTU_PRIVATE_DATA_SIZE];

} __packed;

struct cm_dreq_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;
	/* remote QPN/EECN:24, rsvd:8 */
	__be32 offset8;

	u8 private_data[IB_CM_DREQ_PRIVATE_DATA_SIZE];

} __packed;

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

} __packed;

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
} __packed;

static inline __be32 cm_lap_get_flow_label(struct cm_lap_msg *lap_msg)
{
	return cpu_to_be32(be32_to_cpu(lap_msg->offset56) >> 12);
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
} __packed;

struct cm_sidr_req_msg {
	struct ib_mad_hdr hdr;

	__be32 request_id;
	__be16 pkey;
	__be16 rsvd;
	__be64 service_id;

	u32 private_data[IB_CM_SIDR_REQ_PRIVATE_DATA_SIZE / sizeof(u32)];
} __packed;

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
} __packed;

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
