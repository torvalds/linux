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

static inline enum ib_qp_type cm_req_get_qp_type(struct cm_req_msg *req_msg)
{
	u8 transport_type = IBA_GET(CM_REQ_TRANSPORT_SERVICE_TYPE, req_msg);
	switch (transport_type) {
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
	switch (qp_type) {
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

/* Message REJected or MRAed */
enum cm_msg_response {
	CM_MSG_RESPONSE_REQ = 0x0,
	CM_MSG_RESPONSE_REP = 0x1,
	CM_MSG_RESPONSE_OTHER = 0x2
};

static inline __be32 cm_rep_get_qpn(struct cm_rep_msg *rep_msg, enum ib_qp_type qp_type)
{
	return (qp_type == IB_QPT_XRC_INI) ?
		       cpu_to_be32(IBA_GET(CM_REP_LOCAL_EE_CONTEXT_NUMBER,
					   rep_msg)) :
		       cpu_to_be32(IBA_GET(CM_REP_LOCAL_QPN, rep_msg));
}

#endif /* CM_MSGS_H */
