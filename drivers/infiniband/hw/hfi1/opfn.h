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

/* STL Verbs Extended */
#define IB_BTHE_E_SHIFT           24

struct hfi1_opfn_data {
	/* serialize opfn function calls */
	spinlock_t lock;
};

#endif /* _HFI1_OPFN_H */
