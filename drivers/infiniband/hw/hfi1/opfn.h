/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */
#ifndef _HFI1_OPFN_H
#define _HFI1_OPFN_H

/**
 * DOC: Omni Path Feature Negotion (OPFN)
 *
 * OPFN is a discovery protocol for Intel Omni-Path fabric that
 * allows two RC QPs to negotiate a common feature that both QPs
 * can support. Currently, the only OPA feature that OPFN
 * supports is TID RDMA.
 *
 * Architecture
 *
 * OPFN involves the communication between two QPs on the HFI
 * level on an Omni-Path fabric, and ULPs have no knowledge of
 * OPFN at all.
 *
 * Implementation
 *
 * OPFN extends the existing IB RC protocol with the following
 * changes:
 * -- Uses Bit 24 (reserved) of DWORD 1 of Base Transport
 *    Header (BTH1) to indicate that the RC QP supports OPFN;
 * -- Uses a combination of RC COMPARE_SWAP opcode (0x13) and
 *    the address U64_MAX (0xFFFFFFFFFFFFFFFF) as an OPFN
 *    request; The 64-bit data carried with the request/response
 *    contains the parameters for negotiation and will be
 *    defined in tid_rdma.c file;
 * -- Defines IB_WR_RESERVED3 as IB_WR_OPFN.
 *
 * The OPFN communication will be triggered when an RC QP
 * receives a request with Bit 24 of BTH1 set. The responder QP
 * will then post send an OPFN request with its local
 * parameters, which will be sent to the requester QP once all
 * existing requests on the responder QP side have been sent.
 * Once the requester QP receives the OPFN request, it will
 * keep a copy of the responder QP's parameters, and return a
 * response packet with its own local parameters. The responder
 * QP receives the response packet and keeps a copy of the requester
 * QP's parameters. After this exchange, each side has the parameters
 * for both sides and therefore can select the right parameters
 * for future transactions
 */

#include <linux/workqueue.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdmavt_qp.h>

/* STL Verbs Extended */
#define IB_BTHE_E_SHIFT           24
#define HFI1_VERBS_E_ATOMIC_VADDR U64_MAX

enum hfi1_opfn_codes {
	STL_VERBS_EXTD_NONE = 0,
	STL_VERBS_EXTD_TID_RDMA,
	STL_VERBS_EXTD_MAX
};

struct hfi1_opfn_data {
	u8 extended;
	u16 requested;
	u16 completed;
	enum hfi1_opfn_codes curr;
	/* serialize opfn function calls */
	spinlock_t lock;
	struct work_struct opfn_work;
};

/* WR opcode for OPFN */
#define IB_WR_OPFN IB_WR_RESERVED3

void opfn_send_conn_request(struct work_struct *work);
void opfn_conn_response(struct rvt_qp *qp, struct rvt_ack_entry *e,
			struct ib_atomic_eth *ateth);
void opfn_conn_reply(struct rvt_qp *qp, u64 data);
void opfn_conn_error(struct rvt_qp *qp);
void opfn_qp_init(struct rvt_qp *qp, struct ib_qp_attr *attr, int attr_mask);
void opfn_trigger_conn_request(struct rvt_qp *qp, u32 bth1);
int opfn_init(void);
void opfn_exit(void);

#endif /* _HFI1_OPFN_H */
