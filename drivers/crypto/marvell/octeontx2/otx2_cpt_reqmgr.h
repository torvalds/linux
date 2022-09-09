/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPT_REQMGR_H
#define __OTX2_CPT_REQMGR_H

#include "otx2_cpt_common.h"

/* Completion code size and initial value */
#define OTX2_CPT_COMPLETION_CODE_SIZE 8
#define OTX2_CPT_COMPLETION_CODE_INIT OTX2_CPT_COMP_E_NOTDONE
/*
 * Maximum total number of SG buffers is 100, we divide it equally
 * between input and output
 */
#define OTX2_CPT_MAX_SG_IN_CNT  50
#define OTX2_CPT_MAX_SG_OUT_CNT 50

/* DMA mode direct or SG */
#define OTX2_CPT_DMA_MODE_DIRECT 0
#define OTX2_CPT_DMA_MODE_SG     1

/* Context source CPTR or DPTR */
#define OTX2_CPT_FROM_CPTR 0
#define OTX2_CPT_FROM_DPTR 1

#define OTX2_CPT_MAX_REQ_SIZE 65535

union otx2_cpt_opcode {
	u16 flags;
	struct {
		u8 major;
		u8 minor;
	} s;
};

struct otx2_cptvf_request {
	u32 param1;
	u32 param2;
	u16 dlen;
	union otx2_cpt_opcode opcode;
};

/*
 * CPT_INST_S software command definitions
 * Words EI (0-3)
 */
union otx2_cpt_iq_cmd_word0 {
	u64 u;
	struct {
		__be16 opcode;
		__be16 param1;
		__be16 param2;
		__be16 dlen;
	} s;
};

union otx2_cpt_iq_cmd_word3 {
	u64 u;
	struct {
		u64 cptr:61;
		u64 grp:3;
	} s;
};

struct otx2_cpt_iq_command {
	union otx2_cpt_iq_cmd_word0 cmd;
	u64 dptr;
	u64 rptr;
	union otx2_cpt_iq_cmd_word3 cptr;
};

struct otx2_cpt_pending_entry {
	void *completion_addr;	/* Completion address */
	void *info;
	/* Kernel async request callback */
	void (*callback)(int status, void *arg1, void *arg2);
	struct crypto_async_request *areq; /* Async request callback arg */
	u8 resume_sender;	/* Notify sender to resume sending requests */
	u8 busy;		/* Entry status (free/busy) */
};

struct otx2_cpt_pending_queue {
	struct otx2_cpt_pending_entry *head; /* Head of the queue */
	u32 front;		/* Process work from here */
	u32 rear;		/* Append new work here */
	u32 pending_count;	/* Pending requests count */
	u32 qlen;		/* Queue length */
	spinlock_t lock;	/* Queue lock */
};

struct otx2_cpt_buf_ptr {
	u8 *vptr;
	dma_addr_t dma_addr;
	u16 size;
};

union otx2_cpt_ctrl_info {
	u32 flags;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u32 reserved_6_31:26;
		u32 grp:3;	/* Group bits */
		u32 dma_mode:2;	/* DMA mode */
		u32 se_req:1;	/* To SE core */
#else
		u32 se_req:1;	/* To SE core */
		u32 dma_mode:2;	/* DMA mode */
		u32 grp:3;	/* Group bits */
		u32 reserved_6_31:26;
#endif
	} s;
};

struct otx2_cpt_req_info {
	/* Kernel async request callback */
	void (*callback)(int status, void *arg1, void *arg2);
	struct crypto_async_request *areq; /* Async request callback arg */
	struct otx2_cptvf_request req;/* Request information (core specific) */
	union otx2_cpt_ctrl_info ctrl;/* User control information */
	struct otx2_cpt_buf_ptr in[OTX2_CPT_MAX_SG_IN_CNT];
	struct otx2_cpt_buf_ptr out[OTX2_CPT_MAX_SG_OUT_CNT];
	u8 *iv_out;     /* IV to send back */
	u16 rlen;	/* Output length */
	u8 in_cnt;	/* Number of input buffers */
	u8 out_cnt;	/* Number of output buffers */
	u8 req_type;	/* Type of request */
	u8 is_enc;	/* Is a request an encryption request */
	u8 is_trunc_hmac;/* Is truncated hmac used */
};

struct otx2_cpt_inst_info {
	struct otx2_cpt_pending_entry *pentry;
	struct otx2_cpt_req_info *req;
	struct pci_dev *pdev;
	void *completion_addr;
	u8 *out_buffer;
	u8 *in_buffer;
	dma_addr_t dptr_baddr;
	dma_addr_t rptr_baddr;
	dma_addr_t comp_baddr;
	unsigned long time_in;
	u32 dlen;
	u32 dma_len;
	u8 extra_time;
};

struct otx2_cpt_sglist_component {
	__be16 len0;
	__be16 len1;
	__be16 len2;
	__be16 len3;
	__be64 ptr0;
	__be64 ptr1;
	__be64 ptr2;
	__be64 ptr3;
};

static inline void otx2_cpt_info_destroy(struct pci_dev *pdev,
					 struct otx2_cpt_inst_info *info)
{
	struct otx2_cpt_req_info *req;
	int i;

	if (info->dptr_baddr)
		dma_unmap_single(&pdev->dev, info->dptr_baddr,
				 info->dma_len, DMA_BIDIRECTIONAL);

	if (info->req) {
		req = info->req;
		for (i = 0; i < req->out_cnt; i++) {
			if (req->out[i].dma_addr)
				dma_unmap_single(&pdev->dev,
						 req->out[i].dma_addr,
						 req->out[i].size,
						 DMA_BIDIRECTIONAL);
		}

		for (i = 0; i < req->in_cnt; i++) {
			if (req->in[i].dma_addr)
				dma_unmap_single(&pdev->dev,
						 req->in[i].dma_addr,
						 req->in[i].size,
						 DMA_BIDIRECTIONAL);
		}
	}
	kfree(info);
}

struct otx2_cptlf_wqe;
int otx2_cpt_do_request(struct pci_dev *pdev, struct otx2_cpt_req_info *req,
			int cpu_num);
void otx2_cpt_post_process(struct otx2_cptlf_wqe *wqe);
int otx2_cpt_get_kcrypto_eng_grp_num(struct pci_dev *pdev);

#endif /* __OTX2_CPT_REQMGR_H */
