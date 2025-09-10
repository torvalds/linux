// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cptvf.h"
#include "otx2_cpt_common.h"

/* Default timeout when waiting for free pending entry in us */
#define CPT_PENTRY_TIMEOUT	1000
#define CPT_PENTRY_STEP		50

/* Default threshold for stopping and resuming sender requests */
#define CPT_IQ_STOP_MARGIN	128
#define CPT_IQ_RESUME_MARGIN	512

/* Default command timeout in seconds */
#define CPT_COMMAND_TIMEOUT	4
#define CPT_TIME_IN_RESET_COUNT 5

static void otx2_cpt_dump_sg_list(struct pci_dev *pdev,
				  struct otx2_cpt_req_info *req)
{
	int i;

	pr_debug("Gather list size %d\n", req->in_cnt);
	for (i = 0; i < req->in_cnt; i++) {
		pr_debug("Buffer %d size %d, vptr 0x%p, dmaptr 0x%llx\n", i,
			 req->in[i].size, req->in[i].vptr,
			 req->in[i].dma_addr);
		pr_debug("Buffer hexdump (%d bytes)\n",
			 req->in[i].size);
		print_hex_dump_debug("", DUMP_PREFIX_NONE, 16, 1,
				     req->in[i].vptr, req->in[i].size, false);
	}
	pr_debug("Scatter list size %d\n", req->out_cnt);
	for (i = 0; i < req->out_cnt; i++) {
		pr_debug("Buffer %d size %d, vptr 0x%p, dmaptr 0x%llx\n", i,
			 req->out[i].size, req->out[i].vptr,
			 req->out[i].dma_addr);
		pr_debug("Buffer hexdump (%d bytes)\n", req->out[i].size);
		print_hex_dump_debug("", DUMP_PREFIX_NONE, 16, 1,
				     req->out[i].vptr, req->out[i].size, false);
	}
}

static inline struct otx2_cpt_pending_entry *get_free_pending_entry(
					struct otx2_cpt_pending_queue *q,
					int qlen)
{
	struct otx2_cpt_pending_entry *ent = NULL;

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

static inline void free_pentry(struct otx2_cpt_pending_entry *pentry)
{
	pentry->completion_addr = NULL;
	pentry->info = NULL;
	pentry->callback = NULL;
	pentry->areq = NULL;
	pentry->resume_sender = false;
	pentry->busy = false;
}

static int process_request(struct pci_dev *pdev, struct otx2_cpt_req_info *req,
			   struct otx2_cpt_pending_queue *pqueue,
			   struct otx2_cptlf_info *lf)
{
	struct otx2_cptvf_request *cpt_req = &req->req;
	struct otx2_cpt_pending_entry *pentry = NULL;
	union otx2_cpt_ctrl_info *ctrl = &req->ctrl;
	struct otx2_cpt_inst_info *info = NULL;
	union otx2_cpt_res_s *result = NULL;
	struct otx2_cpt_iq_command iq_cmd;
	union otx2_cpt_inst_s cptinst;
	int retry, ret = 0;
	u8 resume_sender;
	gfp_t gfp;

	gfp = (req->areq->flags & CRYPTO_TFM_REQ_MAY_SLEEP) ? GFP_KERNEL :
							      GFP_ATOMIC;
	if (unlikely(!otx2_cptlf_started(lf->lfs)))
		return -ENODEV;

	info = lf->lfs->ops->cpt_sg_info_create(pdev, req, gfp);
	if (unlikely(!info)) {
		dev_err(&pdev->dev, "Setting up cpt inst info failed");
		return -ENOMEM;
	}
	cpt_req->dlen = info->dlen;

	result = info->completion_addr;
	result->s.compcode = OTX2_CPT_COMPLETION_CODE_INIT;

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
		goto destroy_info;
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
	iq_cmd.cmd.u = 0;
	iq_cmd.cmd.s.opcode = cpu_to_be16(cpt_req->opcode.flags);
	iq_cmd.cmd.s.param1 = cpu_to_be16(cpt_req->param1);
	iq_cmd.cmd.s.param2 = cpu_to_be16(cpt_req->param2);
	iq_cmd.cmd.s.dlen   = cpu_to_be16(cpt_req->dlen);

	/* 64-bit swap for microcode data reads, not needed for addresses*/
	cpu_to_be64s(&iq_cmd.cmd.u);
	iq_cmd.dptr = info->dptr_baddr | info->gthr_sz << 60;
	iq_cmd.rptr = info->rptr_baddr | info->sctr_sz << 60;
	iq_cmd.cptr.s.cptr = cpt_req->cptr_dma;
	iq_cmd.cptr.s.grp = ctrl->s.grp;

	/* Fill in the CPT_INST_S type command for HW interpretation */
	otx2_cpt_fill_inst(&cptinst, &iq_cmd, info->comp_baddr);

	/* Print debug info if enabled */
	otx2_cpt_dump_sg_list(pdev, req);
	pr_debug("Cpt_inst_s hexdump (%d bytes)\n", OTX2_CPT_INST_SIZE);
	print_hex_dump_debug("", 0, 16, 1, &cptinst, OTX2_CPT_INST_SIZE, false);
	pr_debug("Dptr hexdump (%d bytes)\n", cpt_req->dlen);
	print_hex_dump_debug("", 0, 16, 1, info->in_buffer,
			     cpt_req->dlen, false);

	/* Send CPT command */
	lf->lfs->ops->send_cmd(&cptinst, 1, lf);

	/*
	 * We allocate and prepare pending queue entry in critical section
	 * together with submitting CPT instruction to CPT instruction queue
	 * to make sure that order of CPT requests is the same in both
	 * pending and instruction queues
	 */
	spin_unlock_bh(&pqueue->lock);

	ret = resume_sender ? -EBUSY : -EINPROGRESS;
	return ret;

destroy_info:
	spin_unlock_bh(&pqueue->lock);
	otx2_cpt_info_destroy(pdev, info);
	return ret;
}

int otx2_cpt_do_request(struct pci_dev *pdev, struct otx2_cpt_req_info *req,
			int cpu_num)
{
	struct otx2_cptvf_dev *cptvf = pci_get_drvdata(pdev);
	struct otx2_cptlfs_info *lfs = &cptvf->lfs;

	return process_request(lfs->pdev, req, &lfs->lf[cpu_num].pqueue,
			       &lfs->lf[cpu_num]);
}

static int cpt_process_ccode(struct otx2_cptlfs_info *lfs,
			     union otx2_cpt_res_s *cpt_status,
			     struct otx2_cpt_inst_info *info,
			     u32 *res_code)
{
	u8 uc_ccode = lfs->ops->cpt_get_uc_compcode(cpt_status);
	u8 ccode = lfs->ops->cpt_get_compcode(cpt_status);
	struct pci_dev *pdev = lfs->pdev;

	switch (ccode) {
	case OTX2_CPT_COMP_E_FAULT:
		dev_err(&pdev->dev,
			"Request failed with DMA fault\n");
		otx2_cpt_dump_sg_list(pdev, info->req);
		break;

	case OTX2_CPT_COMP_E_HWERR:
		dev_err(&pdev->dev,
			"Request failed with hardware error\n");
		otx2_cpt_dump_sg_list(pdev, info->req);
		break;

	case OTX2_CPT_COMP_E_INSTERR:
		dev_err(&pdev->dev,
			"Request failed with instruction error\n");
		otx2_cpt_dump_sg_list(pdev, info->req);
		break;

	case OTX2_CPT_COMP_E_NOTDONE:
		/* check for timeout */
		if (time_after_eq(jiffies, info->time_in +
				  CPT_COMMAND_TIMEOUT * HZ))
			dev_warn(&pdev->dev,
				 "Request timed out 0x%p", info->req);
		else if (info->extra_time < CPT_TIME_IN_RESET_COUNT) {
			info->time_in = jiffies;
			info->extra_time++;
		}
		return 1;

	case OTX2_CPT_COMP_E_GOOD:
	case OTX2_CPT_COMP_E_WARN:
		/*
		 * Check microcode completion code, it is only valid
		 * when completion code is CPT_COMP_E::GOOD
		 */
		if (uc_ccode != OTX2_CPT_UCC_SUCCESS) {
			/*
			 * If requested hmac is truncated and ucode returns
			 * s/g write length error then we report success
			 * because ucode writes as many bytes of calculated
			 * hmac as available in gather buffer and reports
			 * s/g write length error if number of bytes in gather
			 * buffer is less than full hmac size.
			 */
			if (info->req->is_trunc_hmac &&
			    uc_ccode == OTX2_CPT_UCC_SG_WRITE_LENGTH) {
				*res_code = 0;
				break;
			}

			pr_debug("Request failed with software error code 0x%x: algo = %s driver = %s\n",
				 cpt_status->s.uc_compcode,
				 info->req->areq->tfm->__crt_alg->cra_name,
				 info->req->areq->tfm->__crt_alg->cra_driver_name);
			otx2_cpt_dump_sg_list(pdev, info->req);
			break;
		}
		/* Request has been processed with success */
		*res_code = 0;
		break;

	default:
		dev_err(&pdev->dev,
			"Request returned invalid status %d\n", ccode);
		break;
	}
	return 0;
}

static inline void process_pending_queue(struct otx2_cptlfs_info *lfs,
					 struct otx2_cpt_pending_queue *pqueue)
{
	struct otx2_cpt_pending_entry *resume_pentry = NULL;
	void (*callback)(int status, void *arg, void *req);
	struct otx2_cpt_pending_entry *pentry = NULL;
	union otx2_cpt_res_s *cpt_status = NULL;
	struct otx2_cpt_inst_info *info = NULL;
	struct otx2_cpt_req_info *req = NULL;
	struct crypto_async_request *areq;
	struct pci_dev *pdev = lfs->pdev;
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

		info = pentry->info;
		if (unlikely(!info)) {
			dev_err(&pdev->dev, "Pending entry post arg NULL\n");
			goto process_pentry;
		}

		req = info->req;
		if (unlikely(!req)) {
			dev_err(&pdev->dev, "Request NULL\n");
			goto process_pentry;
		}

		cpt_status = pentry->completion_addr;
		if (unlikely(!cpt_status)) {
			dev_err(&pdev->dev, "Completion address NULL\n");
			goto process_pentry;
		}

		if (cpt_process_ccode(lfs, cpt_status, info, &res_code)) {
			spin_unlock_bh(&pqueue->lock);
			return;
		}
		info->pdev = pdev;

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
				callback(-EINPROGRESS, areq, info);
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
			callback(res_code, areq, info);
	}
}

void otx2_cpt_post_process(struct otx2_cptlf_wqe *wqe)
{
	process_pending_queue(wqe->lfs,
			      &wqe->lfs->lf[wqe->lf_num].pqueue);
}

int otx2_cpt_get_eng_grp_num(struct pci_dev *pdev,
			     enum otx2_cpt_eng_type eng_type)
{
	struct otx2_cptvf_dev *cptvf = pci_get_drvdata(pdev);

	switch (eng_type) {
	case OTX2_CPT_SE_TYPES:
		return cptvf->lfs.kcrypto_se_eng_grp_num;
	case OTX2_CPT_AE_TYPES:
		return cptvf->lfs.kcrypto_ae_eng_grp_num;
	default:
		dev_err(&cptvf->pdev->dev, "Unsupported engine type");
		break;
	}
	return -ENXIO;
}
