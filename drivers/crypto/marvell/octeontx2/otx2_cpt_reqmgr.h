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

#define SG_COMPS_MAX    4
#define SGV2_COMPS_MAX  3

#define SG_COMP_3    3
#define SG_COMP_2    2
#define SG_COMP_1    1

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
	dma_addr_t cptr_dma;
	void *cptr;
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
	u64 gthr_sz;
	u64 sctr_sz;
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

struct cn10kb_cpt_sglist_component {
	u16 len0;
	u16 len1;
	u16 len2;
	u16 valid_segs;
	u64 ptr0;
	u64 ptr1;
	u64 ptr2;
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

static inline int setup_sgio_components(struct pci_dev *pdev,
					struct otx2_cpt_buf_ptr *list,
					int buf_count, u8 *buffer)
{
	struct otx2_cpt_sglist_component *sg_ptr;
	int components;
	int i, j;

	if (unlikely(!list)) {
		dev_err(&pdev->dev, "Input list pointer is NULL\n");
		return -EINVAL;
	}

	for (i = 0; i < buf_count; i++) {
		if (unlikely(!list[i].vptr))
			continue;
		list[i].dma_addr = dma_map_single(&pdev->dev, list[i].vptr,
						  list[i].size,
						  DMA_BIDIRECTIONAL);
		if (unlikely(dma_mapping_error(&pdev->dev, list[i].dma_addr))) {
			dev_err(&pdev->dev, "Dma mapping failed\n");
			goto sg_cleanup;
		}
	}
	components = buf_count / SG_COMPS_MAX;
	sg_ptr = (struct otx2_cpt_sglist_component *)buffer;
	for (i = 0; i < components; i++) {
		sg_ptr->len0 = cpu_to_be16(list[i * SG_COMPS_MAX + 0].size);
		sg_ptr->len1 = cpu_to_be16(list[i * SG_COMPS_MAX + 1].size);
		sg_ptr->len2 = cpu_to_be16(list[i * SG_COMPS_MAX + 2].size);
		sg_ptr->len3 = cpu_to_be16(list[i * SG_COMPS_MAX + 3].size);
		sg_ptr->ptr0 = cpu_to_be64(list[i * SG_COMPS_MAX + 0].dma_addr);
		sg_ptr->ptr1 = cpu_to_be64(list[i * SG_COMPS_MAX + 1].dma_addr);
		sg_ptr->ptr2 = cpu_to_be64(list[i * SG_COMPS_MAX + 2].dma_addr);
		sg_ptr->ptr3 = cpu_to_be64(list[i * SG_COMPS_MAX + 3].dma_addr);
		sg_ptr++;
	}
	components = buf_count % SG_COMPS_MAX;

	switch (components) {
	case SG_COMP_3:
		sg_ptr->len2 = cpu_to_be16(list[i * SG_COMPS_MAX + 2].size);
		sg_ptr->ptr2 = cpu_to_be64(list[i * SG_COMPS_MAX + 2].dma_addr);
		fallthrough;
	case SG_COMP_2:
		sg_ptr->len1 = cpu_to_be16(list[i * SG_COMPS_MAX + 1].size);
		sg_ptr->ptr1 = cpu_to_be64(list[i * SG_COMPS_MAX + 1].dma_addr);
		fallthrough;
	case SG_COMP_1:
		sg_ptr->len0 = cpu_to_be16(list[i * SG_COMPS_MAX + 0].size);
		sg_ptr->ptr0 = cpu_to_be64(list[i * SG_COMPS_MAX + 0].dma_addr);
		break;
	default:
		break;
	}
	return 0;

sg_cleanup:
	for (j = 0; j < i; j++) {
		if (list[j].dma_addr) {
			dma_unmap_single(&pdev->dev, list[j].dma_addr,
					 list[j].size, DMA_BIDIRECTIONAL);
		}

		list[j].dma_addr = 0;
	}
	return -EIO;
}

static inline int sgv2io_components_setup(struct pci_dev *pdev,
					  struct otx2_cpt_buf_ptr *list,
					  int buf_count, u8 *buffer)
{
	struct cn10kb_cpt_sglist_component *sg_ptr;
	int components;
	int i, j;

	if (unlikely(!list)) {
		dev_err(&pdev->dev, "Input list pointer is NULL\n");
		return -EFAULT;
	}

	for (i = 0; i < buf_count; i++) {
		if (unlikely(!list[i].vptr))
			continue;
		list[i].dma_addr = dma_map_single(&pdev->dev, list[i].vptr,
						  list[i].size,
						  DMA_BIDIRECTIONAL);
		if (unlikely(dma_mapping_error(&pdev->dev, list[i].dma_addr))) {
			dev_err(&pdev->dev, "Dma mapping failed\n");
			goto sg_cleanup;
		}
	}
	components = buf_count / SGV2_COMPS_MAX;
	sg_ptr = (struct cn10kb_cpt_sglist_component *)buffer;
	for (i = 0; i < components; i++) {
		sg_ptr->len0 = list[i * SGV2_COMPS_MAX + 0].size;
		sg_ptr->len1 = list[i * SGV2_COMPS_MAX + 1].size;
		sg_ptr->len2 = list[i * SGV2_COMPS_MAX + 2].size;
		sg_ptr->ptr0 = list[i * SGV2_COMPS_MAX + 0].dma_addr;
		sg_ptr->ptr1 = list[i * SGV2_COMPS_MAX + 1].dma_addr;
		sg_ptr->ptr2 = list[i * SGV2_COMPS_MAX + 2].dma_addr;
		sg_ptr->valid_segs = SGV2_COMPS_MAX;
		sg_ptr++;
	}
	components = buf_count % SGV2_COMPS_MAX;

	sg_ptr->valid_segs = components;
	switch (components) {
	case SG_COMP_2:
		sg_ptr->len1 = list[i * SGV2_COMPS_MAX + 1].size;
		sg_ptr->ptr1 = list[i * SGV2_COMPS_MAX + 1].dma_addr;
		fallthrough;
	case SG_COMP_1:
		sg_ptr->len0 = list[i * SGV2_COMPS_MAX + 0].size;
		sg_ptr->ptr0 = list[i * SGV2_COMPS_MAX + 0].dma_addr;
		break;
	default:
		break;
	}
	return 0;

sg_cleanup:
	for (j = 0; j < i; j++) {
		if (list[j].dma_addr) {
			dma_unmap_single(&pdev->dev, list[j].dma_addr,
					 list[j].size, DMA_BIDIRECTIONAL);
		}

		list[j].dma_addr = 0;
	}
	return -EIO;
}

static inline struct otx2_cpt_inst_info *
cn10k_sgv2_info_create(struct pci_dev *pdev, struct otx2_cpt_req_info *req,
		       gfp_t gfp)
{
	u32 dlen = 0, g_len, sg_len, info_len;
	int align = OTX2_CPT_DMA_MINALIGN;
	struct otx2_cpt_inst_info *info;
	u16 g_sz_bytes, s_sz_bytes;
	u32 total_mem_len;
	int i;

	g_sz_bytes = ((req->in_cnt + 2) / 3) *
		      sizeof(struct cn10kb_cpt_sglist_component);
	s_sz_bytes = ((req->out_cnt + 2) / 3) *
		      sizeof(struct cn10kb_cpt_sglist_component);

	g_len = ALIGN(g_sz_bytes, align);
	sg_len = ALIGN(g_len + s_sz_bytes, align);
	info_len = ALIGN(sizeof(*info), align);
	total_mem_len = sg_len + info_len + sizeof(union otx2_cpt_res_s);

	info = kzalloc(total_mem_len, gfp);
	if (unlikely(!info))
		return NULL;

	for (i = 0; i < req->in_cnt; i++)
		dlen += req->in[i].size;

	info->dlen = dlen;
	info->in_buffer = (u8 *)info + info_len;
	info->gthr_sz = req->in_cnt;
	info->sctr_sz = req->out_cnt;

	/* Setup gather (input) components */
	if (sgv2io_components_setup(pdev, req->in, req->in_cnt,
				    info->in_buffer)) {
		dev_err(&pdev->dev, "Failed to setup gather list\n");
		goto destroy_info;
	}

	if (sgv2io_components_setup(pdev, req->out, req->out_cnt,
				    &info->in_buffer[g_len])) {
		dev_err(&pdev->dev, "Failed to setup scatter list\n");
		goto destroy_info;
	}

	info->dma_len = total_mem_len - info_len;
	info->dptr_baddr = dma_map_single(&pdev->dev, info->in_buffer,
					  info->dma_len, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(&pdev->dev, info->dptr_baddr))) {
		dev_err(&pdev->dev, "DMA Mapping failed for cpt req\n");
		goto destroy_info;
	}
	info->rptr_baddr = info->dptr_baddr + g_len;
	/*
	 * Get buffer for union otx2_cpt_res_s response
	 * structure and its physical address
	 */
	info->completion_addr = info->in_buffer + sg_len;
	info->comp_baddr = info->dptr_baddr + sg_len;

	return info;

destroy_info:
	otx2_cpt_info_destroy(pdev, info);
	return NULL;
}

/* SG list header size in bytes */
#define SG_LIST_HDR_SIZE	8
static inline struct otx2_cpt_inst_info *
otx2_sg_info_create(struct pci_dev *pdev, struct otx2_cpt_req_info *req,
		    gfp_t gfp)
{
	int align = OTX2_CPT_DMA_MINALIGN;
	struct otx2_cpt_inst_info *info;
	u32 dlen, align_dlen, info_len;
	u16 g_sz_bytes, s_sz_bytes;
	u32 total_mem_len;

	if (unlikely(req->in_cnt > OTX2_CPT_MAX_SG_IN_CNT ||
		     req->out_cnt > OTX2_CPT_MAX_SG_OUT_CNT)) {
		dev_err(&pdev->dev, "Error too many sg components\n");
		return NULL;
	}

	g_sz_bytes = ((req->in_cnt + 3) / 4) *
		      sizeof(struct otx2_cpt_sglist_component);
	s_sz_bytes = ((req->out_cnt + 3) / 4) *
		      sizeof(struct otx2_cpt_sglist_component);

	dlen = g_sz_bytes + s_sz_bytes + SG_LIST_HDR_SIZE;
	align_dlen = ALIGN(dlen, align);
	info_len = ALIGN(sizeof(*info), align);
	total_mem_len = align_dlen + info_len + sizeof(union otx2_cpt_res_s);

	info = kzalloc(total_mem_len, gfp);
	if (unlikely(!info))
		return NULL;

	info->dlen = dlen;
	info->in_buffer = (u8 *)info + info_len;

	((u16 *)info->in_buffer)[0] = req->out_cnt;
	((u16 *)info->in_buffer)[1] = req->in_cnt;
	((u16 *)info->in_buffer)[2] = 0;
	((u16 *)info->in_buffer)[3] = 0;
	cpu_to_be64s((u64 *)info->in_buffer);

	/* Setup gather (input) components */
	if (setup_sgio_components(pdev, req->in, req->in_cnt,
				  &info->in_buffer[8])) {
		dev_err(&pdev->dev, "Failed to setup gather list\n");
		goto destroy_info;
	}

	if (setup_sgio_components(pdev, req->out, req->out_cnt,
				  &info->in_buffer[8 + g_sz_bytes])) {
		dev_err(&pdev->dev, "Failed to setup scatter list\n");
		goto destroy_info;
	}

	info->dma_len = total_mem_len - info_len;
	info->dptr_baddr = dma_map_single(&pdev->dev, info->in_buffer,
					  info->dma_len, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(&pdev->dev, info->dptr_baddr))) {
		dev_err(&pdev->dev, "DMA Mapping failed for cpt req\n");
		goto destroy_info;
	}
	/*
	 * Get buffer for union otx2_cpt_res_s response
	 * structure and its physical address
	 */
	info->completion_addr = info->in_buffer + align_dlen;
	info->comp_baddr = info->dptr_baddr + align_dlen;

	return info;

destroy_info:
	otx2_cpt_info_destroy(pdev, info);
	return NULL;
}

struct otx2_cptlf_wqe;
int otx2_cpt_do_request(struct pci_dev *pdev, struct otx2_cpt_req_info *req,
			int cpu_num);
void otx2_cpt_post_process(struct otx2_cptlf_wqe *wqe);
int otx2_cpt_get_kcrypto_eng_grp_num(struct pci_dev *pdev);

#endif /* __OTX2_CPT_REQMGR_H */
