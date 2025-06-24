/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_FW_H_
#define _IONIC_FW_H_

#include <linux/kernel.h>

/* completion queue v1 cqe */
struct ionic_v1_cqe {
	union {
		struct {
			__be16		cmd_idx;
			__u8		cmd_op;
			__u8		rsvd[17];
			__le16		old_sq_cindex;
			__le16		old_rq_cq_cindex;
		} admin;
		struct {
			__u64		wqe_id;
			__be32		src_qpn_op;
			__u8		src_mac[6];
			__be16		vlan_tag;
			__be32		imm_data_rkey;
		} recv;
		struct {
			__u8		rsvd[4];
			__be32		msg_msn;
			__u8		rsvd2[8];
			__u64		npg_wqe_id;
		} send;
	};
	__be32				status_length;
	__be32				qid_type_flags;
};

/* bits for cqe qid_type_flags */
enum ionic_v1_cqe_qtf_bits {
	IONIC_V1_CQE_COLOR		= BIT(0),
	IONIC_V1_CQE_ERROR		= BIT(1),
	IONIC_V1_CQE_TYPE_SHIFT		= 5,
	IONIC_V1_CQE_TYPE_MASK		= 0x7,
	IONIC_V1_CQE_QID_SHIFT		= 8,

	IONIC_V1_CQE_TYPE_ADMIN		= 0,
	IONIC_V1_CQE_TYPE_RECV		= 1,
	IONIC_V1_CQE_TYPE_SEND_MSN	= 2,
	IONIC_V1_CQE_TYPE_SEND_NPG	= 3,
};

static inline bool ionic_v1_cqe_color(struct ionic_v1_cqe *cqe)
{
	return cqe->qid_type_flags & cpu_to_be32(IONIC_V1_CQE_COLOR);
}

static inline bool ionic_v1_cqe_error(struct ionic_v1_cqe *cqe)
{
	return cqe->qid_type_flags & cpu_to_be32(IONIC_V1_CQE_ERROR);
}

static inline void ionic_v1_cqe_clean(struct ionic_v1_cqe *cqe)
{
	cqe->qid_type_flags |= cpu_to_be32(~0u << IONIC_V1_CQE_QID_SHIFT);
}

static inline u32 ionic_v1_cqe_qtf(struct ionic_v1_cqe *cqe)
{
	return be32_to_cpu(cqe->qid_type_flags);
}

static inline u8 ionic_v1_cqe_qtf_type(u32 qtf)
{
	return (qtf >> IONIC_V1_CQE_TYPE_SHIFT) & IONIC_V1_CQE_TYPE_MASK;
}

static inline u32 ionic_v1_cqe_qtf_qid(u32 qtf)
{
	return qtf >> IONIC_V1_CQE_QID_SHIFT;
}

#define ADMIN_WQE_STRIDE	64
#define ADMIN_WQE_HDR_LEN	4

/* admin queue v1 wqe */
struct ionic_v1_admin_wqe {
	__u8				op;
	__u8				rsvd;
	__le16				len;

	union {
	} cmd;
};

/* admin queue v1 cqe status */
enum ionic_v1_admin_status {
	IONIC_V1_ASTS_OK,
	IONIC_V1_ASTS_BAD_CMD,
	IONIC_V1_ASTS_BAD_INDEX,
	IONIC_V1_ASTS_BAD_STATE,
	IONIC_V1_ASTS_BAD_TYPE,
	IONIC_V1_ASTS_BAD_ATTR,
	IONIC_V1_ASTS_MSG_TOO_BIG,
};

/* event queue v1 eqe */
struct ionic_v1_eqe {
	__be32				evt;
};

/* bits for cqe queue_type_flags */
enum ionic_v1_eqe_evt_bits {
	IONIC_V1_EQE_COLOR		= BIT(0),
	IONIC_V1_EQE_TYPE_SHIFT		= 1,
	IONIC_V1_EQE_TYPE_MASK		= 0x7,
	IONIC_V1_EQE_CODE_SHIFT		= 4,
	IONIC_V1_EQE_CODE_MASK		= 0xf,
	IONIC_V1_EQE_QID_SHIFT		= 8,

	/* cq events */
	IONIC_V1_EQE_TYPE_CQ		= 0,
	/* cq normal events */
	IONIC_V1_EQE_CQ_NOTIFY		= 0,
	/* cq error events */
	IONIC_V1_EQE_CQ_ERR		= 8,

	/* qp and srq events */
	IONIC_V1_EQE_TYPE_QP		= 1,
	/* qp normal events */
	IONIC_V1_EQE_SRQ_LEVEL		= 0,
	IONIC_V1_EQE_SQ_DRAIN		= 1,
	IONIC_V1_EQE_QP_COMM_EST	= 2,
	IONIC_V1_EQE_QP_LAST_WQE	= 3,
	/* qp error events */
	IONIC_V1_EQE_QP_ERR		= 8,
	IONIC_V1_EQE_QP_ERR_REQUEST	= 9,
	IONIC_V1_EQE_QP_ERR_ACCESS	= 10,
};

static inline bool ionic_v1_eqe_color(struct ionic_v1_eqe *eqe)
{
	return eqe->evt & cpu_to_be32(IONIC_V1_EQE_COLOR);
}

static inline u32 ionic_v1_eqe_evt(struct ionic_v1_eqe *eqe)
{
	return be32_to_cpu(eqe->evt);
}

static inline u8 ionic_v1_eqe_evt_type(u32 evt)
{
	return (evt >> IONIC_V1_EQE_TYPE_SHIFT) & IONIC_V1_EQE_TYPE_MASK;
}

static inline u8 ionic_v1_eqe_evt_code(u32 evt)
{
	return (evt >> IONIC_V1_EQE_CODE_SHIFT) & IONIC_V1_EQE_CODE_MASK;
}

static inline u32 ionic_v1_eqe_evt_qid(u32 evt)
{
	return evt >> IONIC_V1_EQE_QID_SHIFT;
}

#endif /* _IONIC_FW_H_ */
