/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OTX_CPTVF_REQUEST_MANAGER_H
#define __OTX_CPTVF_REQUEST_MANAGER_H

#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/pci.h>
#include "otx_cpt_hw_types.h"

/*
 * Maximum total number of SG buffers is 100, we divide it equally
 * between input and output
 */
#define OTX_CPT_MAX_SG_IN_CNT		50
#define OTX_CPT_MAX_SG_OUT_CNT		50

/* DMA mode direct or SG */
#define OTX_CPT_DMA_DIRECT_DIRECT	0
#define OTX_CPT_DMA_GATHER_SCATTER	1

/* Context source CPTR or DPTR */
#define OTX_CPT_FROM_CPTR		0
#define OTX_CPT_FROM_DPTR		1

/* CPT instruction queue alignment */
#define OTX_CPT_INST_Q_ALIGNMENT	128
#define OTX_CPT_MAX_REQ_SIZE		65535

/* Default command timeout in seconds */
#define OTX_CPT_COMMAND_TIMEOUT		4
#define OTX_CPT_TIMER_HOLD		0x03F
#define OTX_CPT_COUNT_HOLD		32
#define OTX_CPT_TIME_IN_RESET_COUNT     5

/* Minimum and maximum values for interrupt coalescing */
#define OTX_CPT_COALESC_MIN_TIME_WAIT	0x0
#define OTX_CPT_COALESC_MAX_TIME_WAIT	((1<<16)-1)
#define OTX_CPT_COALESC_MIN_NUM_WAIT	0x0
#define OTX_CPT_COALESC_MAX_NUM_WAIT	((1<<20)-1)

union otx_cpt_opcode_info {
	u16 flags;
	struct {
		u8 major;
		u8 minor;
	} s;
};

struct otx_cptvf_request {
	u32 param1;
	u32 param2;
	u16 dlen;
	union otx_cpt_opcode_info opcode;
};

struct otx_cpt_buf_ptr {
	u8 *vptr;
	dma_addr_t dma_addr;
	u16 size;
};

union otx_cpt_ctrl_info {
	u32 flags;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u32 reserved0:26;
		u32 grp:3;	/* Group bits */
		u32 dma_mode:2;	/* DMA mode */
		u32 se_req:1;	/* To SE core */
#else
		u32 se_req:1;	/* To SE core */
		u32 dma_mode:2;	/* DMA mode */
		u32 grp:3;	/* Group bits */
		u32 reserved0:26;
#endif
	} s;
};

/*
 * CPT_INST_S software command definitions
 * Words EI (0-3)
 */
union otx_cpt_iq_cmd_word0 {
	u64 u64;
	struct {
		u16 opcode;
		u16 param1;
		u16 param2;
		u16 dlen;
	} s;
};

union otx_cpt_iq_cmd_word3 {
	u64 u64;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 grp:3;
		u64 cptr:61;
#else
		u64 cptr:61;
		u64 grp:3;
#endif
	} s;
};

struct otx_cpt_iq_cmd {
	union otx_cpt_iq_cmd_word0 cmd;
	u64 dptr;
	u64 rptr;
	union otx_cpt_iq_cmd_word3 cptr;
};

struct otx_cpt_sglist_component {
	union {
		u64 len;
		struct {
			u16 len0;
			u16 len1;
			u16 len2;
			u16 len3;
		} s;
	} u;
	u64 ptr0;
	u64 ptr1;
	u64 ptr2;
	u64 ptr3;
};

struct otx_cpt_pending_entry {
	u64 *completion_addr;	/* Completion address */
	struct otx_cpt_info_buffer *info;
	/* Kernel async request callback */
	void (*callback)(int status, void *arg1, void *arg2);
	struct crypto_async_request *areq; /* Async request callback arg */
	u8 resume_sender;	/* Notify sender to resume sending requests */
	u8 busy;		/* Entry status (free/busy) */
};

struct otx_cpt_pending_queue {
	struct otx_cpt_pending_entry *head;	/* Head of the queue */
	u32 front;			/* Process work from here */
	u32 rear;			/* Append new work here */
	u32 pending_count;		/* Pending requests count */
	u32 qlen;			/* Queue length */
	spinlock_t lock;		/* Queue lock */
};

struct otx_cpt_req_info {
	/* Kernel async request callback */
	void (*callback)(int status, void *arg1, void *arg2);
	struct crypto_async_request *areq; /* Async request callback arg */
	struct otx_cptvf_request req;/* Request information (core specific) */
	union otx_cpt_ctrl_info ctrl;/* User control information */
	struct otx_cpt_buf_ptr in[OTX_CPT_MAX_SG_IN_CNT];
	struct otx_cpt_buf_ptr out[OTX_CPT_MAX_SG_OUT_CNT];
	u8 *iv_out;     /* IV to send back */
	u16 rlen;	/* Output length */
	u8 incnt;	/* Number of input buffers */
	u8 outcnt;	/* Number of output buffers */
	u8 req_type;	/* Type of request */
	u8 is_enc;	/* Is a request an encryption request */
	u8 is_trunc_hmac;/* Is truncated hmac used */
};

struct otx_cpt_info_buffer {
	struct otx_cpt_pending_entry *pentry;
	struct otx_cpt_req_info *req;
	struct pci_dev *pdev;
	u64 *completion_addr;
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

static inline void do_request_cleanup(struct pci_dev *pdev,
				      struct otx_cpt_info_buffer *info)
{
	struct otx_cpt_req_info *req;
	int i;

	if (info->dptr_baddr)
		dma_unmap_single(&pdev->dev, info->dptr_baddr,
				 info->dma_len, DMA_BIDIRECTIONAL);

	if (info->req) {
		req = info->req;
		for (i = 0; i < req->outcnt; i++) {
			if (req->out[i].dma_addr)
				dma_unmap_single(&pdev->dev,
						 req->out[i].dma_addr,
						 req->out[i].size,
						 DMA_BIDIRECTIONAL);
		}

		for (i = 0; i < req->incnt; i++) {
			if (req->in[i].dma_addr)
				dma_unmap_single(&pdev->dev,
						 req->in[i].dma_addr,
						 req->in[i].size,
						 DMA_BIDIRECTIONAL);
		}
	}
	kzfree(info);
}

struct otx_cptvf_wqe;
void otx_cpt_dump_sg_list(struct pci_dev *pdev, struct otx_cpt_req_info *req);
void otx_cpt_post_process(struct otx_cptvf_wqe *wqe);
int otx_cpt_do_request(struct pci_dev *pdev, struct otx_cpt_req_info *req,
		       int cpu_num);

#endif /* __OTX_CPTVF_REQUEST_MANAGER_H */
