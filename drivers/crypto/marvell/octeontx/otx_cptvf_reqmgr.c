// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "otx_cptvf.h"
#include "otx_cptvf_algs.h"

/* Completion code size and initial value */
#define COMPLETION_CODE_SIZE	8
#define COMPLETION_CODE_INIT	0

/* SG list header size in bytes */
#define SG_LIST_HDR_SIZE	8

/* Default timeout when waiting for free pending entry in us */
#define CPT_PENTRY_TIMEOUT	1000
#define CPT_PENTRY_STEP		50

/* Default threshold for stopping and resuming sender requests */
#define CPT_IQ_STOP_MARGIN	128
#define CPT_IQ_RESUME_MARGIN	512

#define CPT_DMA_ALIGN		128

void otx_cpt_dump_sg_list(struct pci_dev *pdev, struct otx_cpt_req_info *req)
{
	int i;

	pr_debug("Gather list size %d\n", req->incnt);
	for (i = 0; i < req->incnt; i++) {
		pr_debug("Buffer %d size %d, vptr 0x%p, dmaptr 0x%p\n", i,
			 req->in[i].size, req->in[i].vptr,
			 (void *) req->in[i].dma_addr);
		pr_debug("Buffer hexdump (%d bytes)\n",
			 req->in[i].size);
		print_hex_dump_debug("", DUMP_PREFIX_NONE, 16, 1,
				     req->in[i].vptr, req->in[i].size, false);
	}

	pr_debug("Scatter list size %d\n", req->outcnt);
	for (i = 0; i < req->outcnt; i++) {
		pr_debug("Buffer %d size %d, vptr 0x%p, dmaptr 0x%p\n", i,
			 req->out[i].size, req->out[i].vptr,
			 (void *) req->out[i].dma_addr);
		pr_debug("Buffer hexdump (%d bytes)\n", req->out[i].size);
		print_hex_dump_debug("", DUMP_PREFIX_NONE, 16, 1,
				     req->out[i].vptr, req->out[i].size, false);
	}
}

static inline struct otx_cpt_pending_entry *get_free_pending_entry(
						struct otx_cpt_pending_queue *q,
						int qlen)
{
	struct otx_cpt_pending_entry *ent = NULL;

	ent = &q->head[q->rear];
	if (unlikely(ent->busy))
		return NULL;

	q->rear++;
	if (unlikely(q->rear == qlen))
		q->rear = 0;

	return ent;
}

static inline u32 modulo_inc(u32 index, u32 length, u32 inc)
{
	if (WARN_ON(inc > length))
		inc = length;

	index += inc;
	if (unlikely(index >= length))
		index -= length;

	return index;
}

static inline void free_pentry(struct otx_cpt_pending_entry *pentry)
{
	pentry->completion_addr = NULL;
	pentry->info = NULL;
	pentry->callback = NULL;
	pentry->areq = NULL;
	pentry->resume_sender = false;
	pentry->busy = false;
}

static inline int setup_sgio_components(struct pci_dev *pdev,
					struct otx_cpt_buf_ptr *list,
					int buf_count, u8 *buffer)
{
	struct otx_cpt_sglist_component *sg_ptr = NULL;
	int ret = 0, i, j;
	int components;

	if (unlikely(!list)) {
		dev_err(&pdev->dev, "Input list pointer is NULL\n");
		return -EFAULT;
	}

	for (i = 0; i < buf_count; i++) {
		if (likely(list[i].vptr)) {
			list[i].dma_addr = dma_map_single(&pdev->dev,
							  list[i].vptr,
							  list[i].size,
							  DMA_BIDIRECTIONAL);
			if (unlikely(dma_mapping_error(&pdev->dev,
						       list[i].dma_addr))) {
				dev_err(&pdev->dev, "Dma mapping failed\n");
				ret = -EIO;
				goto sg_cleanup;
			}
		}
	}

	components = buf_count / 4;
	sg_ptr = (struct otx_cpt_sglist_component *)buffer;
	for (i = 0; i < components; i++) {
		sg_ptr->u.s.len0 = cpu_to_be16(list[i * 4 + 0].size);
		sg_ptr->u.s.len1 = cpu_to_be16(list[i * 4 + 1].size);
		sg_ptr->u.s.len2 = cpu_to_be16(list[i * 4 + 2].size);
		sg_ptr->u.s.len3 = cpu_to_be16(list[i * 4 + 3].size);
		sg_ptr->ptr0 = cpu_to_be64(list[i * 4 + 0].dma_addr);
		sg_ptr->ptr1 = cpu_to_be64(list[i * 4 + 1].dma_addr);
		sg_ptr->ptr2 = cpu_to_be64(list[i * 4 + 2].dma_addr);
		sg_ptr->ptr3 = cpu_to_be64(list[i * 4 + 3].dma_addr);
		sg_ptr++;
	}
	components = buf_count % 4;

	switch (components) {
	case 3:
		sg_ptr->u.s.len2 = cpu_to_be16(list[i * 4 + 2].size);
		sg_ptr->ptr2 = cpu_to_be64(list[i * 4 + 2].dma_addr);
		/* Fall through */
	case 2:
		sg_ptr->u.s.len1 = cpu_to_be16(list[i * 4 + 1].size);
		sg_ptr->ptr1 = cpu_to_be64(list[i * 4 + 1].dma_addr);
		/* Fall through */
	case 1:
		sg_ptr->u.s.len0 = cpu_to_be16(list[i * 4 + 0].size);
		sg_ptr->ptr0 = cpu_to_be64(list[i * 4 + 0].dma_addr);
		break;
	default:
		break;
	}
	return ret;

sg_cleanup:
	for (j = 0; j < i; j++) {
		if (list[j].dma_addr) {
			dma_unmap_single(&pdev->dev, list[i].dma_addr,
					 list[i].size, DMA_BIDIRECTIONAL);
		}

		list[j].dma_addr = 0;
	}
	return ret;
}

static inline int setup_sgio_list(struct pci_dev *pdev,
				  struct otx_cpt_info_buffer **pinfo,
				  struct otx_cpt_req_info *req, gfp_t gfp)
{
	u32 dlen, align_dlen, info_len, rlen;
	struct otx_cpt_info_buffer *info;
	u16 g_sz_bytes, s_sz_bytes;
	int align = CPT_DMA_ALIGN;
	u32 total_mem_len;

	if (unlikely(req->incnt > OTX_CPT_MAX_SG_IN_CNT ||
		     req->outcnt > OTX_CPT_MAX_SG_OUT_CNT)) {
		dev_err(&pdev->dev, "Error too many sg components\n");
		return -EINVAL;
	}

	g_sz_bytes = ((req->incnt + 3) / 4) *
		      sizeof(struct otx_cpt_sglist_component);
	s_sz_bytes = ((req->outcnt + 3) / 4) *
		      sizeof(struct otx_cpt_sglist_component);

	dlen = g_sz_bytes + s_sz_bytes + SG_LIST_HDR_SIZE;
	align_dlen = ALIGN(dlen, align);
	info_len = ALIGN(sizeof(*info), align);
	rlen = ALIGN(sizeof(union otx_cpt_res_s), align);
	total_mem_len = align_dlen + info_len + rlen + COMPLETION_CODE_SIZE;

	info = kzalloc(total_mem_len, gfp);
	if (unlikely(!info)) {
		dev_err(&pdev->dev, "Memory allocation failed\n");
		return -ENOMEM;
	}
	*pinfo = info;
	info->dlen = dlen;
	info->in_buffer = (u8 *)info + info_len;

	((u16 *)info->in_buffer)[0] = req->outcnt;
	((u16 *)info->in_buffer)[1] = req->incnt;
	((u16 *)info->in_buffer)[2] = 0;
	((u16 *)info->in_buffer)[3] = 0;
	*(u64 *)info->in_buffer = cpu_to_be64p((u64 *)info->in_buffer);

	/* Setup gather (input) components */
	if (setup_sgio_components(pdev, req->in, req->incnt,
				  &info->in_buffer[8])) {
		dev_err(&pdev->dev, "Failed to setup gather list\n");
		return -EFAULT;
	}

	if (setup_sgio_components(pdev, req->out, req->outcnt,
				  &info->in_buffer[8 + g_sz_bytes])) {
		dev_err(&pdev->dev, "Failed to setup scatter list\n");
		return -EFAULT;
	}

	info->dma_len = total_mem_len - info_len;
	info->dptr_baddr = dma_map_single(&pdev->dev, (void *)info->in_buffer,
					  info->dma_len, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(&pdev->dev, info->dptr_baddr))) {
		dev_err(&pdev->dev, "DMA Mapping failed for cpt req\n");
		return -EIO;
	}
	/*
	 * Get buffer for union otx_cpt_res_s response
	 * structure and its physical address
	 */
	info->completion_addr = (u64 *)(info->in_buffer + align_dlen);
	info->comp_baddr = info->dptr_baddr + align_dlen;

	/* Create and initialize RPTR */
	info->out_buffer = (u8 *)info->completion_addr + rlen;
	info->rptr_baddr = info->comp_baddr + rlen;

	*((u64 *) info->out_buffer) = ~((u64) COMPLETION_CODE_INIT);

	return 0;
}


static void cpt_fill_inst(union otx_cpt_inst_s *inst,
			  struct otx_cpt_info_buffer *info,
			  struct otx_cpt_iq_cmd *cmd)
{
	inst->u[0] = 0x0;
	inst->s.doneint = true;
	inst->s.res_addr = (u64)info->comp_baddr;
	inst->u[2] = 0x0;
	inst->s.wq_ptr = 0;
	inst->s.ei0 = cmd->cmd.u64;
	inst->s.ei1 = cmd->dptr;
	inst->s.ei2 = cmd->rptr;
	inst->s.ei3 = cmd->cptr.u64;
}

/*
 * On OcteonTX platform the parameter db_count is used as a count for ringing
 * door bell. The valid values for db_count are:
 * 0 - 1 CPT instruction will be enqueued however CPT will not be informed
 * 1 - 1 CPT instruction will be enqueued and CPT will be informed
 */
static void cpt_send_cmd(union otx_cpt_inst_s *cptinst, struct otx_cptvf *cptvf)
{
	struct otx_cpt_cmd_qinfo *qinfo = &cptvf->cqinfo;
	struct otx_cpt_cmd_queue *queue;
	struct otx_cpt_cmd_chunk *curr;
	u8 *ent;

	queue = &qinfo->queue[0];
	/*
	 * cpt_send_cmd is currently called only from critical section
	 * therefore no locking is required for accessing instruction queue
	 */
	ent = &queue->qhead->head[queue->idx * OTX_CPT_INST_SIZE];
	memcpy(ent, (void *) cptinst, OTX_CPT_INST_SIZE);

	if (++queue->idx >= queue->qhead->size / 64) {
		curr = queue->qhead;

		if (list_is_last(&curr->nextchunk, &queue->chead))
			queue->qhead = queue->base;
		else
			queue->qhead = list_next_entry(queue->qhead, nextchunk);
		queue->idx = 0;
	}
	/* make sure all memory stores are done before ringing doorbell */
	smp_wmb();
	otx_cptvf_write_vq_doorbell(cptvf, 1);
}

static int process_request(struct pci_dev *pdev, struct otx_cpt_req_info *req,
			   struct otx_cpt_pending_queue *pqueue,
			   struct otx_cptvf *cptvf)
{
	struct otx_cptvf_request *cpt_req = &req->req;
	struct otx_cpt_pending_entry *pentry = NULL;
	union otx_cpt_ctrl_info *ctrl = &req->ctrl;
	struct otx_cpt_info_buffer *info = NULL;
	union otx_cpt_res_s *result = NULL;
	struct otx_cpt_iq_cmd iq_cmd;
	union otx_cpt_inst_s cptinst;
	int retry, ret = 0;
	u8 resume_sender;
	gfp_t gfp;

	gfp = (req->areq->flags & CRYPTO_TFM_REQ_MAY_SLEEP) ? GFP_KERNEL :
							      GFP_ATOMIC;
	ret = setup_sgio_list(pdev, &info, req, gfp);
	if (unlikely(ret)) {
		dev_err(&pdev->dev, "Setting up SG list failed");
		goto request_cleanup;
	}
	cpt_req->dlen = info->dlen;

	result = (union otx_cpt_res_s *) info->completion_addr;
	result->s.compcode = COMPLETION_CODE_INIT;

	spin_lock_bh(&pqueue->lock);
	pentry = get_free_pending_entry(pqueue, pqueue->qlen);
	retry = CPT_PENTRY_TIMEOUT / CPT_PENTRY_STEP;
	while (unlikely(!pentry) && retry--) {
		spin_unlock_bh(&pqueue->lock);
		udelay(CPT_PENTRY_STEP);
		spin_lock_bh(&pqueue->lock);
		pentry = get_free_pending_entry(pqueue, pqueue->qlen);
	}

	if (unlikely(!pentry)) {
		ret = -ENOSPC;
		spin_unlock_bh(&pqueue->lock);
		goto request_cleanup;
	}

	/*
	 * Check if we are close to filling in entire pending queue,
	 * if so then tell the sender to stop/sleep by returning -EBUSY
	 * We do it only for context which can sleep (GFP_KERNEL)
	 */
	if (gfp == GFP_KERNEL &&
	    pqueue->pending_count > (pqueue->qlen - CPT_IQ_STOP_MARGIN)) {
		pentry->resume_sender = true;
	} else
		pentry->resume_sender = false;
	resume_sender = pentry->resume_sender;
	pqueue->pending_count++;

	pentry->completion_addr = info->completion_addr;
	pentry->info = info;
	pentry->callback = req->callback;
	pentry->areq = req->areq;
	pentry->busy = true;
	info->pentry = pentry;
	info->time_in = jiffies;
	info->req = req;

	/* Fill in the command */
	iq_cmd.cmd.u64 = 0;
	iq_cmd.cmd.s.opcode = cpu_to_be16(cpt_req->opcode.flags);
	iq_cmd.cmd.s.param1 = cpu_to_be16(cpt_req->param1);
	iq_cmd.cmd.s.param2 = cpu_to_be16(cpt_req->param2);
	iq_cmd.cmd.s.dlen   = cpu_to_be16(cpt_req->dlen);

	/* 64-bit swap for microcode data reads, not needed for addresses*/
	iq_cmd.cmd.u64 = cpu_to_be64(iq_cmd.cmd.u64);
	iq_cmd.dptr = info->dptr_baddr;
	iq_cmd.rptr = info->rptr_baddr;
	iq_cmd.cptr.u64 = 0;
	iq_cmd.cptr.s.grp = ctrl->s.grp;

	/* Fill in the CPT_INST_S type command for HW interpretation */
	cpt_fill_inst(&cptinst, info, &iq_cmd);

	/* Print debug info if enabled */
	otx_cpt_dump_sg_list(pdev, req);
	pr_debug("Cpt_inst_s hexdump (%d bytes)\n", OTX_CPT_INST_SIZE);
	print_hex_dump_debug("", 0, 16, 1, &cptinst, OTX_CPT_INST_SIZE, false);
	pr_debug("Dptr hexdump (%d bytes)\n", cpt_req->dlen);
	print_hex_dump_debug("", 0, 16, 1, info->in_buffer,
			     cpt_req->dlen, false);

	/* Send CPT command */
	cpt_send_cmd(&cptinst, cptvf);

	/*
	 * We allocate and prepare pending queue entry in critical section
	 * together with submitting CPT instruction to CPT instruction queue
	 * to make sure that order of CPT requests is the same in both
	 * pending and instruction queues
	 */
	spin_unlock_bh(&pqueue->lock);

	ret = resume_sender ? -EBUSY : -EINPROGRESS;
	return ret;

request_cleanup:
	do_request_cleanup(pdev, info);
	return ret;
}

int otx_cpt_do_request(struct pci_dev *pdev, struct otx_cpt_req_info *req,
		       int cpu_num)
{
	struct otx_cptvf *cptvf = pci_get_drvdata(pdev);

	if (!otx_cpt_device_ready(cptvf)) {
		dev_err(&pdev->dev, "CPT Device is not ready");
		return -ENODEV;
	}

	if ((cptvf->vftype == OTX_CPT_SE_TYPES) && (!req->ctrl.s.se_req)) {
		dev_err(&pdev->dev, "CPTVF-%d of SE TYPE got AE request",
			cptvf->vfid);
		return -EINVAL;
	} else if ((cptvf->vftype == OTX_CPT_AE_TYPES) &&
		   (req->ctrl.s.se_req)) {
		dev_err(&pdev->dev, "CPTVF-%d of AE TYPE got SE request",
			cptvf->vfid);
		return -EINVAL;
	}

	return process_request(pdev, req, &cptvf->pqinfo.queue[0], cptvf);
}

static int cpt_process_ccode(struct pci_dev *pdev,
			     union otx_cpt_res_s *cpt_status,
			     struct otx_cpt_info_buffer *cpt_info,
			     struct otx_cpt_req_info *req, u32 *res_code)
{
	u8 ccode = cpt_status->s.compcode;
	union otx_cpt_error_code ecode;

	ecode.u = be64_to_cpu(*((u64 *) cpt_info->out_buffer));
	switch (ccode) {
	case CPT_COMP_E_FAULT:
		dev_err(&pdev->dev,
			"Request failed with DMA fault\n");
		otx_cpt_dump_sg_list(pdev, req);
		break;

	case CPT_COMP_E_SWERR:
		dev_err(&pdev->dev,
			"Request failed with software error code %d\n",
			ecode.s.ccode);
		otx_cpt_dump_sg_list(pdev, req);
		break;

	case CPT_COMP_E_HWERR:
		dev_err(&pdev->dev,
			"Request failed with hardware error\n");
		otx_cpt_dump_sg_list(pdev, req);
		break;

	case COMPLETION_CODE_INIT:
		/* check for timeout */
		if (time_after_eq(jiffies, cpt_info->time_in +
				  OTX_CPT_COMMAND_TIMEOUT * HZ))
			dev_warn(&pdev->dev, "Request timed out 0x%p", req);
		else if (cpt_info->extra_time < OTX_CPT_TIME_IN_RESET_COUNT) {
			cpt_info->time_in = jiffies;
			cpt_info->extra_time++;
		}
		return 1;

	case CPT_COMP_E_GOOD:
		/* Check microcode completion code */
		if (ecode.s.ccode) {
			/*
			 * If requested hmac is truncated and ucode returns
			 * s/g write length error then we report success
			 * because ucode writes as many bytes of calculated
			 * hmac as available in gather buffer and reports
			 * s/g write length error if number of bytes in gather
			 * buffer is less than full hmac size.
			 */
			if (req->is_trunc_hmac &&
			    ecode.s.ccode == ERR_SCATTER_GATHER_WRITE_LENGTH) {
				*res_code = 0;
				break;
			}

			dev_err(&pdev->dev,
				"Request failed with software error code 0x%x\n",
				ecode.s.ccode);
			otx_cpt_dump_sg_list(pdev, req);
			break;
		}

		/* Request has been processed with success */
		*res_code = 0;
		break;

	default:
		dev_err(&pdev->dev, "Request returned invalid status\n");
		break;
	}

	return 0;
}

static inline void process_pending_queue(struct pci_dev *pdev,
					 struct otx_cpt_pending_queue *pqueue)
{
	void (*callback)(int status, void *arg1, void *arg2);
	struct otx_cpt_pending_entry *resume_pentry = NULL;
	struct otx_cpt_pending_entry *pentry = NULL;
	struct otx_cpt_info_buffer *cpt_info = NULL;
	union otx_cpt_res_s *cpt_status = NULL;
	struct otx_cpt_req_info *req = NULL;
	struct crypto_async_request *areq;
	u32 res_code, resume_index;

	while (1) {
		spin_lock_bh(&pqueue->lock);
		pentry = &pqueue->head[pqueue->front];

		if (WARN_ON(!pentry)) {
			spin_unlock_bh(&pqueue->lock);
			break;
		}

		res_code = -EINVAL;
		if (unlikely(!pentry->busy)) {
			spin_unlock_bh(&pqueue->lock);
			break;
		}

		if (unlikely(!pentry->callback)) {
			dev_err(&pdev->dev, "Callback NULL\n");
			goto process_pentry;
		}

		cpt_info = pentry->info;
		if (unlikely(!cpt_info)) {
			dev_err(&pdev->dev, "Pending entry post arg NULL\n");
			goto process_pentry;
		}

		req = cpt_info->req;
		if (unlikely(!req)) {
			dev_err(&pdev->dev, "Request NULL\n");
			goto process_pentry;
		}

		cpt_status = (union otx_cpt_res_s *) pentry->completion_addr;
		if (unlikely(!cpt_status)) {
			dev_err(&pdev->dev, "Completion address NULL\n");
			goto process_pentry;
		}

		if (cpt_process_ccode(pdev, cpt_status, cpt_info, req,
				      &res_code)) {
			spin_unlock_bh(&pqueue->lock);
			return;
		}
		cpt_info->pdev = pdev;

process_pentry:
		/*
		 * Check if we should inform sending side to resume
		 * We do it CPT_IQ_RESUME_MARGIN elements in advance before
		 * pending queue becomes empty
		 */
		resume_index = modulo_inc(pqueue->front, pqueue->qlen,
					  CPT_IQ_RESUME_MARGIN);
		resume_pentry = &pqueue->head[resume_index];
		if (resume_pentry &&
		    resume_pentry->resume_sender) {
			resume_pentry->resume_sender = false;
			callback = resume_pentry->callback;
			areq = resume_pentry->areq;

			if (callback) {
				spin_unlock_bh(&pqueue->lock);

				/*
				 * EINPROGRESS is an indication for sending
				 * side that it can resume sending requests
				 */
				callback(-EINPROGRESS, areq, cpt_info);
				spin_lock_bh(&pqueue->lock);
			}
		}

		callback = pentry->callback;
		areq = pentry->areq;
		free_pentry(pentry);

		pqueue->pending_count--;
		pqueue->front = modulo_inc(pqueue->front, pqueue->qlen, 1);
		spin_unlock_bh(&pqueue->lock);

		/*
		 * Call callback after current pending entry has been
		 * processed, we don't do it if the callback pointer is
		 * invalid.
		 */
		if (callback)
			callback(res_code, areq, cpt_info);
	}
}

void otx_cpt_post_process(struct otx_cptvf_wqe *wqe)
{
	process_pending_queue(wqe->cptvf->pdev, &wqe->cptvf->pqinfo.queue[0]);
}
