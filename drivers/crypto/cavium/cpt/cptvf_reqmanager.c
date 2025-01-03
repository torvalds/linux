// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Cavium, Inc.
 */

#include "cptvf.h"
#include "cptvf_algs.h"
#include "request_manager.h"

/**
 * get_free_pending_entry - get free entry from pending queue
 * @q: pending queue
 * @qlen: queue length
 */
static struct pending_entry *get_free_pending_entry(struct pending_queue *q,
						    int qlen)
{
	struct pending_entry *ent = NULL;

	ent = &q->head[q->rear];
	if (unlikely(ent->busy)) {
		ent = NULL;
		goto no_free_entry;
	}

	q->rear++;
	if (unlikely(q->rear == qlen))
		q->rear = 0;

no_free_entry:
	return ent;
}

static inline void pending_queue_inc_front(struct pending_qinfo *pqinfo,
					   int qno)
{
	struct pending_queue *queue = &pqinfo->queue[qno];

	queue->front++;
	if (unlikely(queue->front == pqinfo->qlen))
		queue->front = 0;
}

static int setup_sgio_components(struct cpt_vf *cptvf, struct buf_ptr *list,
				 int buf_count, u8 *buffer)
{
	int ret = 0, i, j;
	int components;
	struct sglist_component *sg_ptr = NULL;
	struct pci_dev *pdev = cptvf->pdev;

	if (unlikely(!list)) {
		dev_err(&pdev->dev, "Input List pointer is NULL\n");
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
				dev_err(&pdev->dev, "DMA map kernel buffer failed for component: %d\n",
					i);
				ret = -EIO;
				goto sg_cleanup;
			}
		}
	}

	components = buf_count / 4;
	sg_ptr = (struct sglist_component *)buffer;
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
		fallthrough;
	case 2:
		sg_ptr->u.s.len1 = cpu_to_be16(list[i * 4 + 1].size);
		sg_ptr->ptr1 = cpu_to_be64(list[i * 4 + 1].dma_addr);
		fallthrough;
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

static inline int setup_sgio_list(struct cpt_vf *cptvf,
				  struct cpt_info_buffer *info,
				  struct cpt_request_info *req)
{
	u16 g_sz_bytes = 0, s_sz_bytes = 0;
	int ret = 0;
	struct pci_dev *pdev = cptvf->pdev;

	if (req->incnt > MAX_SG_IN_CNT || req->outcnt > MAX_SG_OUT_CNT) {
		dev_err(&pdev->dev, "Request SG components are higher than supported\n");
		ret = -EINVAL;
		goto  scatter_gather_clean;
	}

	/* Setup gather (input) components */
	g_sz_bytes = ((req->incnt + 3) / 4) * sizeof(struct sglist_component);
	info->gather_components = kzalloc(g_sz_bytes, req->may_sleep ? GFP_KERNEL : GFP_ATOMIC);
	if (!info->gather_components) {
		ret = -ENOMEM;
		goto  scatter_gather_clean;
	}

	ret = setup_sgio_components(cptvf, req->in,
				    req->incnt,
				    info->gather_components);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup gather list\n");
		ret = -EFAULT;
		goto  scatter_gather_clean;
	}

	/* Setup scatter (output) components */
	s_sz_bytes = ((req->outcnt + 3) / 4) * sizeof(struct sglist_component);
	info->scatter_components = kzalloc(s_sz_bytes, req->may_sleep ? GFP_KERNEL : GFP_ATOMIC);
	if (!info->scatter_components) {
		ret = -ENOMEM;
		goto  scatter_gather_clean;
	}

	ret = setup_sgio_components(cptvf, req->out,
				    req->outcnt,
				    info->scatter_components);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup gather list\n");
		ret = -EFAULT;
		goto  scatter_gather_clean;
	}

	/* Create and initialize DPTR */
	info->dlen = g_sz_bytes + s_sz_bytes + SG_LIST_HDR_SIZE;
	info->in_buffer = kzalloc(info->dlen, req->may_sleep ? GFP_KERNEL : GFP_ATOMIC);
	if (!info->in_buffer) {
		ret = -ENOMEM;
		goto  scatter_gather_clean;
	}

	((__be16 *)info->in_buffer)[0] = cpu_to_be16(req->outcnt);
	((__be16 *)info->in_buffer)[1] = cpu_to_be16(req->incnt);
	((__be16 *)info->in_buffer)[2] = 0;
	((__be16 *)info->in_buffer)[3] = 0;

	memcpy(&info->in_buffer[8], info->gather_components,
	       g_sz_bytes);
	memcpy(&info->in_buffer[8 + g_sz_bytes],
	       info->scatter_components, s_sz_bytes);

	info->dptr_baddr = dma_map_single(&pdev->dev,
					  (void *)info->in_buffer,
					  info->dlen,
					  DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&pdev->dev, info->dptr_baddr)) {
		dev_err(&pdev->dev, "Mapping DPTR Failed %d\n", info->dlen);
		ret = -EIO;
		goto  scatter_gather_clean;
	}

	/* Create and initialize RPTR */
	info->out_buffer = kzalloc(COMPLETION_CODE_SIZE, req->may_sleep ? GFP_KERNEL : GFP_ATOMIC);
	if (!info->out_buffer) {
		ret = -ENOMEM;
		goto scatter_gather_clean;
	}

	*((u64 *)info->out_buffer) = ~((u64)COMPLETION_CODE_INIT);
	info->alternate_caddr = (u64 *)info->out_buffer;
	info->rptr_baddr = dma_map_single(&pdev->dev,
					  (void *)info->out_buffer,
					  COMPLETION_CODE_SIZE,
					  DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&pdev->dev, info->rptr_baddr)) {
		dev_err(&pdev->dev, "Mapping RPTR Failed %d\n",
			COMPLETION_CODE_SIZE);
		ret = -EIO;
		goto  scatter_gather_clean;
	}

	return 0;

scatter_gather_clean:
	return ret;
}

static int send_cpt_command(struct cpt_vf *cptvf, union cpt_inst_s *cmd,
		     u32 qno)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct command_qinfo *qinfo = NULL;
	struct command_queue *queue;
	struct command_chunk *chunk;
	u8 *ent;
	int ret = 0;

	if (unlikely(qno >= cptvf->nr_queues)) {
		dev_err(&pdev->dev, "Invalid queue (qno: %d, nr_queues: %d)\n",
			qno, cptvf->nr_queues);
		return -EINVAL;
	}

	qinfo = &cptvf->cqinfo;
	queue = &qinfo->queue[qno];
	/* lock command queue */
	spin_lock(&queue->lock);
	ent = &queue->qhead->head[queue->idx * qinfo->cmd_size];
	memcpy(ent, (void *)cmd, qinfo->cmd_size);

	if (++queue->idx >= queue->qhead->size / 64) {
		hlist_for_each_entry(chunk, &queue->chead, nextchunk) {
			if (chunk == queue->qhead) {
				continue;
			} else {
				queue->qhead = chunk;
				break;
			}
		}
		queue->idx = 0;
	}
	/* make sure all memory stores are done before ringing doorbell */
	smp_wmb();
	cptvf_write_vq_doorbell(cptvf, 1);
	/* unlock command queue */
	spin_unlock(&queue->lock);

	return ret;
}

static void do_request_cleanup(struct cpt_vf *cptvf,
			struct cpt_info_buffer *info)
{
	int i;
	struct pci_dev *pdev = cptvf->pdev;
	struct cpt_request_info *req;

	if (info->dptr_baddr)
		dma_unmap_single(&pdev->dev, info->dptr_baddr,
				 info->dlen, DMA_BIDIRECTIONAL);

	if (info->rptr_baddr)
		dma_unmap_single(&pdev->dev, info->rptr_baddr,
				 COMPLETION_CODE_SIZE, DMA_BIDIRECTIONAL);

	if (info->comp_baddr)
		dma_unmap_single(&pdev->dev, info->comp_baddr,
				 sizeof(union cpt_res_s), DMA_BIDIRECTIONAL);

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

	kfree_sensitive(info->scatter_components);
	kfree_sensitive(info->gather_components);
	kfree_sensitive(info->out_buffer);
	kfree_sensitive(info->in_buffer);
	kfree_sensitive((void *)info->completion_addr);
	kfree_sensitive(info);
}

static void do_post_process(struct cpt_vf *cptvf, struct cpt_info_buffer *info)
{
	struct pci_dev *pdev = cptvf->pdev;

	if (!info) {
		dev_err(&pdev->dev, "incorrect cpt_info_buffer for post processing\n");
		return;
	}

	do_request_cleanup(cptvf, info);
}

static inline void process_pending_queue(struct cpt_vf *cptvf,
					 struct pending_qinfo *pqinfo,
					 int qno)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct pending_queue *pqueue = &pqinfo->queue[qno];
	struct pending_entry *pentry = NULL;
	struct cpt_info_buffer *info = NULL;
	union cpt_res_s *status = NULL;
	unsigned char ccode;

	while (1) {
		spin_lock_bh(&pqueue->lock);
		pentry = &pqueue->head[pqueue->front];
		if (unlikely(!pentry->busy)) {
			spin_unlock_bh(&pqueue->lock);
			break;
		}

		info = (struct cpt_info_buffer *)pentry->post_arg;
		if (unlikely(!info)) {
			dev_err(&pdev->dev, "Pending Entry post arg NULL\n");
			pending_queue_inc_front(pqinfo, qno);
			spin_unlock_bh(&pqueue->lock);
			continue;
		}

		status = (union cpt_res_s *)pentry->completion_addr;
		ccode = status->s.compcode;
		if ((status->s.compcode == CPT_COMP_E_FAULT) ||
		    (status->s.compcode == CPT_COMP_E_SWERR)) {
			dev_err(&pdev->dev, "Request failed with %s\n",
				(status->s.compcode == CPT_COMP_E_FAULT) ?
				"DMA Fault" : "Software error");
			pentry->completion_addr = NULL;
			pentry->busy = false;
			atomic64_dec((&pqueue->pending_count));
			pentry->post_arg = NULL;
			pending_queue_inc_front(pqinfo, qno);
			do_request_cleanup(cptvf, info);
			spin_unlock_bh(&pqueue->lock);
			break;
		} else if (status->s.compcode == COMPLETION_CODE_INIT) {
			/* check for timeout */
			if (time_after_eq(jiffies,
					  (info->time_in +
					  (CPT_COMMAND_TIMEOUT * HZ)))) {
				dev_err(&pdev->dev, "Request timed out");
				pentry->completion_addr = NULL;
				pentry->busy = false;
				atomic64_dec((&pqueue->pending_count));
				pentry->post_arg = NULL;
				pending_queue_inc_front(pqinfo, qno);
				do_request_cleanup(cptvf, info);
				spin_unlock_bh(&pqueue->lock);
				break;
			} else if ((*info->alternate_caddr ==
				(~COMPLETION_CODE_INIT)) &&
				(info->extra_time < TIME_IN_RESET_COUNT)) {
				info->time_in = jiffies;
				info->extra_time++;
				spin_unlock_bh(&pqueue->lock);
				break;
			}
		}

		pentry->completion_addr = NULL;
		pentry->busy = false;
		pentry->post_arg = NULL;
		atomic64_dec((&pqueue->pending_count));
		pending_queue_inc_front(pqinfo, qno);
		spin_unlock_bh(&pqueue->lock);

		do_post_process(info->cptvf, info);
		/*
		 * Calling callback after we find
		 * that the request has been serviced
		 */
		pentry->callback(ccode, pentry->callback_arg);
	}
}

int process_request(struct cpt_vf *cptvf, struct cpt_request_info *req)
{
	int ret = 0, clear = 0, queue = 0;
	struct cpt_info_buffer *info = NULL;
	struct cptvf_request *cpt_req = NULL;
	union ctrl_info *ctrl = NULL;
	union cpt_res_s *result = NULL;
	struct pending_entry *pentry = NULL;
	struct pending_queue *pqueue = NULL;
	struct pci_dev *pdev = cptvf->pdev;
	u8 group = 0;
	struct cpt_vq_command vq_cmd;
	union cpt_inst_s cptinst;

	info = kzalloc(sizeof(*info), req->may_sleep ? GFP_KERNEL : GFP_ATOMIC);
	if (unlikely(!info)) {
		dev_err(&pdev->dev, "Unable to allocate memory for info_buffer\n");
		return -ENOMEM;
	}

	cpt_req = (struct cptvf_request *)&req->req;
	ctrl = (union ctrl_info *)&req->ctrl;

	info->cptvf = cptvf;
	group = ctrl->s.grp;
	ret = setup_sgio_list(cptvf, info, req);
	if (ret) {
		dev_err(&pdev->dev, "Setting up SG list failed");
		goto request_cleanup;
	}

	cpt_req->dlen = info->dlen;
	/*
	 * Get buffer for union cpt_res_s response
	 * structure and its physical address
	 */
	info->completion_addr = kzalloc(sizeof(union cpt_res_s), req->may_sleep ? GFP_KERNEL : GFP_ATOMIC);
	if (unlikely(!info->completion_addr)) {
		dev_err(&pdev->dev, "Unable to allocate memory for completion_addr\n");
		ret = -ENOMEM;
		goto request_cleanup;
	}

	result = (union cpt_res_s *)info->completion_addr;
	result->s.compcode = COMPLETION_CODE_INIT;
	info->comp_baddr = dma_map_single(&pdev->dev,
					       (void *)info->completion_addr,
					       sizeof(union cpt_res_s),
					       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&pdev->dev, info->comp_baddr)) {
		dev_err(&pdev->dev, "mapping compptr Failed %lu\n",
			sizeof(union cpt_res_s));
		ret = -EFAULT;
		goto  request_cleanup;
	}

	/* Fill the VQ command */
	vq_cmd.cmd.u64 = 0;
	vq_cmd.cmd.s.opcode = cpu_to_be16(cpt_req->opcode.flags);
	vq_cmd.cmd.s.param1 = cpu_to_be16(cpt_req->param1);
	vq_cmd.cmd.s.param2 = cpu_to_be16(cpt_req->param2);
	vq_cmd.cmd.s.dlen   = cpu_to_be16(cpt_req->dlen);

	vq_cmd.dptr = info->dptr_baddr;
	vq_cmd.rptr = info->rptr_baddr;
	vq_cmd.cptr.u64 = 0;
	vq_cmd.cptr.s.grp = group;
	/* Get Pending Entry to submit command */
	/* Always queue 0, because 1 queue per VF */
	queue = 0;
	pqueue = &cptvf->pqinfo.queue[queue];

	if (atomic64_read(&pqueue->pending_count) > PENDING_THOLD) {
		dev_err(&pdev->dev, "pending threshold reached\n");
		process_pending_queue(cptvf, &cptvf->pqinfo, queue);
	}

get_pending_entry:
	spin_lock_bh(&pqueue->lock);
	pentry = get_free_pending_entry(pqueue, cptvf->pqinfo.qlen);
	if (unlikely(!pentry)) {
		spin_unlock_bh(&pqueue->lock);
		if (clear == 0) {
			process_pending_queue(cptvf, &cptvf->pqinfo, queue);
			clear = 1;
			goto get_pending_entry;
		}
		dev_err(&pdev->dev, "Get free entry failed\n");
		dev_err(&pdev->dev, "queue: %d, rear: %d, front: %d\n",
			queue, pqueue->rear, pqueue->front);
		ret = -EFAULT;
		goto request_cleanup;
	}

	pentry->completion_addr = info->completion_addr;
	pentry->post_arg = (void *)info;
	pentry->callback = req->callback;
	pentry->callback_arg = req->callback_arg;
	info->pentry = pentry;
	pentry->busy = true;
	atomic64_inc(&pqueue->pending_count);

	/* Send CPT command */
	info->pentry = pentry;
	info->time_in = jiffies;
	info->req = req;

	/* Create the CPT_INST_S type command for HW interpretation */
	cptinst.s.doneint = true;
	cptinst.s.res_addr = (u64)info->comp_baddr;
	cptinst.s.tag = 0;
	cptinst.s.grp = 0;
	cptinst.s.wq_ptr = 0;
	cptinst.s.ei0 = vq_cmd.cmd.u64;
	cptinst.s.ei1 = vq_cmd.dptr;
	cptinst.s.ei2 = vq_cmd.rptr;
	cptinst.s.ei3 = vq_cmd.cptr.u64;

	ret = send_cpt_command(cptvf, &cptinst, queue);
	spin_unlock_bh(&pqueue->lock);
	if (unlikely(ret)) {
		dev_err(&pdev->dev, "Send command failed for AE\n");
		ret = -EFAULT;
		goto request_cleanup;
	}

	return 0;

request_cleanup:
	dev_dbg(&pdev->dev, "Failed to submit CPT command\n");
	do_request_cleanup(cptvf, info);

	return ret;
}

void vq_post_process(struct cpt_vf *cptvf, u32 qno)
{
	struct pci_dev *pdev = cptvf->pdev;

	if (unlikely(qno > cptvf->nr_queues)) {
		dev_err(&pdev->dev, "Request for post processing on invalid pending queue: %u\n",
			qno);
		return;
	}

	process_pending_queue(cptvf, &cptvf->pqinfo, qno);
}

int cptvf_do_request(void *vfdev, struct cpt_request_info *req)
{
	struct cpt_vf *cptvf = (struct cpt_vf *)vfdev;
	struct pci_dev *pdev = cptvf->pdev;

	if (!cpt_device_ready(cptvf)) {
		dev_err(&pdev->dev, "CPT Device is not ready");
		return -ENODEV;
	}

	if ((cptvf->vftype == SE_TYPES) && (!req->ctrl.s.se_req)) {
		dev_err(&pdev->dev, "CPTVF-%d of SE TYPE got AE request",
			cptvf->vfid);
		return -EINVAL;
	} else if ((cptvf->vftype == AE_TYPES) && (req->ctrl.s.se_req)) {
		dev_err(&pdev->dev, "CPTVF-%d of AE TYPE got SE request",
			cptvf->vfid);
		return -EINVAL;
	}

	return process_request(cptvf, req);
}
