/*
 * Copyright (c) 2016 Hisilicon Limited.
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

#ifndef _HNS_ROCE_EQ_H
#define _HNS_ROCE_EQ_H

#define HNS_ROCE_CEQ			1
#define HNS_ROCE_AEQ			2

#define HNS_ROCE_CEQ_ENTRY_SIZE		0x4
#define HNS_ROCE_AEQ_ENTRY_SIZE		0x10
#define HNS_ROCE_CEQC_REG_OFFSET	0x18

#define HNS_ROCE_CEQ_DEFAULT_INTERVAL	0x10
#define HNS_ROCE_CEQ_DEFAULT_BURST_NUM	0x10

#define HNS_ROCE_INT_MASK_DISABLE	0
#define HNS_ROCE_INT_MASK_ENABLE	1

#define EQ_ENABLE			1
#define EQ_DISABLE			0
#define CONS_INDEX_MASK			0xffff

#define CEQ_REG_OFFSET			0x18

enum {
	HNS_ROCE_EQ_STAT_INVALID  = 0,
	HNS_ROCE_EQ_STAT_VALID    = 2,
};

struct hns_roce_aeqe {
	u32 asyn;
	union {
		struct {
			u32 qp;
			u32 rsv0;
			u32 rsv1;
		} qp_event;

		struct {
			u32 cq;
			u32 rsv0;
			u32 rsv1;
		} cq_event;

		struct {
			u32 port;
			u32 rsv0;
			u32 rsv1;
		} port_event;

		struct {
			u32 ceqe;
			u32 rsv0;
			u32 rsv1;
		} ce_event;

		struct {
			__le64  out_param;
			__le16  token;
			u8	status;
			u8	rsv0;
		} __packed cmd;
	 } event;
};

#define HNS_ROCE_AEQE_U32_4_EVENT_TYPE_S 16
#define HNS_ROCE_AEQE_U32_4_EVENT_TYPE_M   \
	(((1UL << 8) - 1) << HNS_ROCE_AEQE_U32_4_EVENT_TYPE_S)

#define HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S 24
#define HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_M   \
	(((1UL << 7) - 1) << HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S)

#define HNS_ROCE_AEQE_U32_4_OWNER_S 31

#define HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_S 0
#define HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_M   \
	(((1UL << 24) - 1) << HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_S)

#define HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_S 0
#define HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_M   \
	(((1UL << 16) - 1) << HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_S)

#define HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_S 0
#define HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_M   \
	(((1UL << 5) - 1) << HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_S)

struct hns_roce_ceqe {
	union {
		int		comp;
	} ceqe;
};

#define HNS_ROCE_CEQE_CEQE_COMP_OWNER_S	0

#define HNS_ROCE_CEQE_CEQE_COMP_CQN_S 16
#define HNS_ROCE_CEQE_CEQE_COMP_CQN_M   \
	(((1UL << 16) - 1) << HNS_ROCE_CEQE_CEQE_COMP_CQN_S)

#endif /* _HNS_ROCE_EQ_H */
