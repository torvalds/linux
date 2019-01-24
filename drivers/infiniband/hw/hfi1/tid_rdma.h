/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */
#ifndef HFI1_TID_RDMA_H
#define HFI1_TID_RDMA_H

#define TID_RDMA_MAX_SEGMENT_SIZE       BIT(18)   /* 256 KiB (for now) */

struct tid_rdma_params {
	struct rcu_head rcu_head;
	u32 qp;
	u32 max_len;
	u16 jkey;
	u8 max_read;
	u8 max_write;
	u8 timeout;
	u8 urg;
	u8 version;
};

struct tid_rdma_qp_params {
	struct tid_rdma_params local;
	struct tid_rdma_params __rcu *remote;
};

bool tid_rdma_conn_req(struct rvt_qp *qp, u64 *data);
bool tid_rdma_conn_reply(struct rvt_qp *qp, u64 data);
bool tid_rdma_conn_resp(struct rvt_qp *qp, u64 *data);
void tid_rdma_conn_error(struct rvt_qp *qp);
void tid_rdma_opfn_init(struct rvt_qp *qp, struct tid_rdma_params *p);

int hfi1_kern_exp_rcv_init(struct hfi1_ctxtdata *rcd, int reinit);

int hfi1_qp_priv_init(struct rvt_dev_info *rdi, struct rvt_qp *qp,
		      struct ib_qp_init_attr *init_attr);

#endif /* HFI1_TID_RDMA_H */
