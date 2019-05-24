/*
 * Copyright (C) 2016 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/module.h>

#include "cptvf.h"

#define DRV_NAME	"thunder-cptvf"
#define DRV_VERSION	"1.0"

struct cptvf_wqe {
	struct tasklet_struct twork;
	void *cptvf;
	u32 qno;
};

struct cptvf_wqe_info {
	struct cptvf_wqe vq_wqe[CPT_NUM_QS_PER_VF];
};

static void vq_work_handler(unsigned long data)
{
	struct cptvf_wqe_info *cwqe_info = (struct cptvf_wqe_info *)data;
	struct cptvf_wqe *cwqe = &cwqe_info->vq_wqe[0];

	vq_post_process(cwqe->cptvf, cwqe->qno);
}

static int init_worker_threads(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct cptvf_wqe_info *cwqe_info;
	int i;

	cwqe_info = kzalloc(sizeof(*cwqe_info), GFP_KERNEL);
	if (!cwqe_info)
		return -ENOMEM;

	if (cptvf->nr_queues) {
		dev_info(&pdev->dev, "Creating VQ worker threads (%d)\n",
			 cptvf->nr_queues);
	}

	for (i = 0; i < cptvf->nr_queues; i++) {
		tasklet_init(&cwqe_info->vq_wqe[i].twork, vq_work_handler,
			     (u64)cwqe_info);
		cwqe_info->vq_wqe[i].qno = i;
		cwqe_info->vq_wqe[i].cptvf = cptvf;
	}

	cptvf->wqe_info = cwqe_info;

	return 0;
}

static void cleanup_worker_threads(struct cpt_vf *cptvf)
{
	struct cptvf_wqe_info *cwqe_info;
	struct pci_dev *pdev = cptvf->pdev;
	int i;

	cwqe_info = (struct cptvf_wqe_info *)cptvf->wqe_info;
	if (!cwqe_info)
		return;

	if (cptvf->nr_queues) {
		dev_info(&pdev->dev, "Cleaning VQ worker threads (%u)\n",
			 cptvf->nr_queues);
	}

	for (i = 0; i < cptvf->nr_queues; i++)
		tasklet_kill(&cwqe_info->vq_wqe[i].twork);

	kzfree(cwqe_info);
	cptvf->wqe_info = NULL;
}

static void free_pending_queues(struct pending_qinfo *pqinfo)
{
	int i;
	struct pending_queue *queue;

	for_each_pending_queue(pqinfo, queue, i) {
		if (!queue->head)
			continue;

		/* free single queue */
		kzfree((queue->head));

		queue->front = 0;
		queue->rear = 0;

		return;
	}

	pqinfo->qlen = 0;
	pqinfo->nr_queues = 0;
}

static int alloc_pending_queues(struct pending_qinfo *pqinfo, u32 qlen,
				u32 nr_queues)
{
	u32 i;
	size_t size;
	int ret;
	struct pending_queue *queue = NULL;

	pqinfo->nr_queues = nr_queues;
	pqinfo->qlen = qlen;

	size = (qlen * sizeof(struct pending_entry));

	for_each_pending_queue(pqinfo, queue, i) {
		queue->head = kzalloc((size), GFP_KERNEL);
		if (!queue->head) {
			ret = -ENOMEM;
			goto pending_qfail;
		}

		queue->front = 0;
		queue->rear = 0;
		atomic64_set((&queue->pending_count), (0));

		/* init queue spin lock */
		spin_lock_init(&queue->lock);
	}

	return 0;

pending_qfail:
	free_pending_queues(pqinfo);

	return ret;
}

static int init_pending_queues(struct cpt_vf *cptvf, u32 qlen, u32 nr_queues)
{
	struct pci_dev *pdev = cptvf->pdev;
	int ret;

	if (!nr_queues)
		return 0;

	ret = alloc_pending_queues(&cptvf->pqinfo, qlen, nr_queues);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup pending queues (%u)\n",
			nr_queues);
		return ret;
	}

	return 0;
}

static void cleanup_pending_queues(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;

	if (!cptvf->nr_queues)
		return;

	dev_info(&pdev->dev, "Cleaning VQ pending queue (%u)\n",
		 cptvf->nr_queues);
	free_pending_queues(&cptvf->pqinfo);
}

static void free_command_queues(struct cpt_vf *cptvf,
				struct command_qinfo *cqinfo)
{
	int i;
	struct command_queue *queue = NULL;
	struct command_chunk *chunk = NULL;
	struct pci_dev *pdev = cptvf->pdev;
	struct hlist_node *node;

	/* clean up for each queue */
	for (i = 0; i < cptvf->nr_queues; i++) {
		queue = &cqinfo->queue[i];
		if (hlist_empty(&cqinfo->queue[i].chead))
			continue;

		hlist_for_each_entry_safe(chunk, node, &cqinfo->queue[i].chead,
					  nextchunk) {
			dma_free_coherent(&pdev->dev, chunk->size,
					  chunk->head,
					  chunk->dma_addr);
			chunk->head = NULL;
			chunk->dma_addr = 0;
			hlist_del(&chunk->nextchunk);
			kzfree(chunk);
		}

		queue->nchunks = 0;
		queue->idx = 0;
	}

	/* common cleanup */
	cqinfo->cmd_size = 0;
}

static int alloc_command_queues(struct cpt_vf *cptvf,
				struct command_qinfo *cqinfo, size_t cmd_size,
				u32 qlen)
{
	int i;
	size_t q_size;
	struct command_queue *queue = NULL;
	struct pci_dev *pdev = cptvf->pdev;

	/* common init */
	cqinfo->cmd_size = cmd_size;
	/* Qsize in dwords, needed for SADDR config, 1-next chunk pointer */
	cptvf->qsize = min(qlen, cqinfo->qchunksize) *
			CPT_NEXT_CHUNK_PTR_SIZE + 1;
	/* Qsize in bytes to create space for alignment */
	q_size = qlen * cqinfo->cmd_size;

	/* per queue initialization */
	for (i = 0; i < cptvf->nr_queues; i++) {
		size_t c_size = 0;
		size_t rem_q_size = q_size;
		struct command_chunk *curr = NULL, *first = NULL, *last = NULL;
		u32 qcsize_bytes = cqinfo->qchunksize * cqinfo->cmd_size;

		queue = &cqinfo->queue[i];
		INIT_HLIST_HEAD(&cqinfo->queue[i].chead);
		do {
			curr = kzalloc(sizeof(*curr), GFP_KERNEL);
			if (!curr)
				goto cmd_qfail;

			c_size = (rem_q_size > qcsize_bytes) ? qcsize_bytes :
					rem_q_size;
			curr->head = (u8 *)dma_alloc_coherent(&pdev->dev,
							      c_size + CPT_NEXT_CHUNK_PTR_SIZE,
							      &curr->dma_addr,
							      GFP_KERNEL);
			if (!curr->head) {
				dev_err(&pdev->dev, "Command Q (%d) chunk (%d) allocation failed\n",
					i, queue->nchunks);
				kfree(curr);
				goto cmd_qfail;
			}

			curr->size = c_size;
			if (queue->nchunks == 0) {
				hlist_add_head(&curr->nextchunk,
					       &cqinfo->queue[i].chead);
				first = curr;
			} else {
				hlist_add_behind(&curr->nextchunk,
						 &last->nextchunk);
			}

			queue->nchunks++;
			rem_q_size -= c_size;
			if (last)
				*((u64 *)(&last->head[last->size])) = (u64)curr->dma_addr;

			last = curr;
		} while (rem_q_size);

		/* Make the queue circular */
		/* Tie back last chunk entry to head */
		curr = first;
		*((u64 *)(&last->head[last->size])) = (u64)curr->dma_addr;
		queue->qhead = curr;
		spin_lock_init(&queue->lock);
	}
	return 0;

cmd_qfail:
	free_command_queues(cptvf, cqinfo);
	return -ENOMEM;
}

static int init_command_queues(struct cpt_vf *cptvf, u32 qlen)
{
	struct pci_dev *pdev = cptvf->pdev;
	int ret;

	/* setup AE command queues */
	ret = alloc_command_queues(cptvf, &cptvf->cqinfo, CPT_INST_SIZE,
				   qlen);
	if (ret) {
		dev_err(&pdev->dev, "failed to allocate AE command queues (%u)\n",
			cptvf->nr_queues);
		return ret;
	}

	return ret;
}

static void cleanup_command_queues(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;

	if (!cptvf->nr_queues)
		return;

	dev_info(&pdev->dev, "Cleaning VQ command queue (%u)\n",
		 cptvf->nr_queues);
	free_command_queues(cptvf, &cptvf->cqinfo);
}

static void cptvf_sw_cleanup(struct cpt_vf *cptvf)
{
	cleanup_worker_threads(cptvf);
	cleanup_pending_queues(cptvf);
	cleanup_command_queues(cptvf);
}

static int cptvf_sw_init(struct cpt_vf *cptvf, u32 qlen, u32 nr_queues)
{
	struct pci_dev *pdev = cptvf->pdev;
	int ret = 0;
	u32 max_dev_queues = 0;

	max_dev_queues = CPT_NUM_QS_PER_VF;
	/* possible cpus */
	nr_queues = min_t(u32, nr_queues, max_dev_queues);
	cptvf->nr_queues = nr_queues;

	ret = init_command_queues(cptvf, qlen);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup command queues (%u)\n",
			nr_queues);
		return ret;
	}

	ret = init_pending_queues(cptvf, qlen, nr_queues);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup pending queues (%u)\n",
			nr_queues);
		goto setup_pqfail;
	}

	/* Create worker threads for BH processing */
	ret = init_worker_threads(cptvf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup worker threads\n");
		goto init_work_fail;
	}

	return 0;

init_work_fail:
	cleanup_worker_threads(cptvf);
	cleanup_pending_queues(cptvf);

setup_pqfail:
	cleanup_command_queues(cptvf);

	return ret;
}

static void cptvf_free_irq_affinity(struct cpt_vf *cptvf, int vec)
{
	irq_set_affinity_hint(pci_irq_vector(cptvf->pdev, vec), NULL);
	free_cpumask_var(cptvf->affinity_mask[vec]);
}

static void cptvf_write_vq_ctl(struct cpt_vf *cptvf, bool val)
{
	union cptx_vqx_ctl vqx_ctl;

	vqx_ctl.u = cpt_read_csr64(cptvf->reg_base, CPTX_VQX_CTL(0, 0));
	vqx_ctl.s.ena = val;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_CTL(0, 0), vqx_ctl.u);
}

void cptvf_write_vq_doorbell(struct cpt_vf *cptvf, u32 val)
{
	union cptx_vqx_doorbell vqx_dbell;

	vqx_dbell.u = cpt_read_csr64(cptvf->reg_base,
				     CPTX_VQX_DOORBELL(0, 0));
	vqx_dbell.s.dbell_cnt = val * 8; /* Num of Instructions * 8 words */
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_DOORBELL(0, 0),
			vqx_dbell.u);
}

static void cptvf_write_vq_inprog(struct cpt_vf *cptvf, u8 val)
{
	union cptx_vqx_inprog vqx_inprg;

	vqx_inprg.u = cpt_read_csr64(cptvf->reg_base, CPTX_VQX_INPROG(0, 0));
	vqx_inprg.s.inflight = val;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_INPROG(0, 0), vqx_inprg.u);
}

static void cptvf_write_vq_done_numwait(struct cpt_vf *cptvf, u32 val)
{
	union cptx_vqx_done_wait vqx_dwait;

	vqx_dwait.u = cpt_read_csr64(cptvf->reg_base,
				     CPTX_VQX_DONE_WAIT(0, 0));
	vqx_dwait.s.num_wait = val;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_DONE_WAIT(0, 0),
			vqx_dwait.u);
}

static void cptvf_write_vq_done_timewait(struct cpt_vf *cptvf, u16 time)
{
	union cptx_vqx_done_wait vqx_dwait;

	vqx_dwait.u = cpt_read_csr64(cptvf->reg_base,
				     CPTX_VQX_DONE_WAIT(0, 0));
	vqx_dwait.s.time_wait = time;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_DONE_WAIT(0, 0),
			vqx_dwait.u);
}

static void cptvf_enable_swerr_interrupts(struct cpt_vf *cptvf)
{
	union cptx_vqx_misc_ena_w1s vqx_misc_ena;

	vqx_misc_ena.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_MISC_ENA_W1S(0, 0));
	/* Set mbox(0) interupts for the requested vf */
	vqx_misc_ena.s.swerr = 1;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_MISC_ENA_W1S(0, 0),
			vqx_misc_ena.u);
}

static void cptvf_enable_mbox_interrupts(struct cpt_vf *cptvf)
{
	union cptx_vqx_misc_ena_w1s vqx_misc_ena;

	vqx_misc_ena.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_MISC_ENA_W1S(0, 0));
	/* Set mbox(0) interupts for the requested vf */
	vqx_misc_ena.s.mbox = 1;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_MISC_ENA_W1S(0, 0),
			vqx_misc_ena.u);
}

static void cptvf_enable_done_interrupts(struct cpt_vf *cptvf)
{
	union cptx_vqx_done_ena_w1s vqx_done_ena;

	vqx_done_ena.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_DONE_ENA_W1S(0, 0));
	/* Set DONE interrupt for the requested vf */
	vqx_done_ena.s.done = 1;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_DONE_ENA_W1S(0, 0),
			vqx_done_ena.u);
}

static void cptvf_clear_dovf_intr(struct cpt_vf *cptvf)
{
	union cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_MISC_INT(0, 0));
	/* W1C for the VF */
	vqx_misc_int.s.dovf = 1;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_MISC_INT(0, 0),
			vqx_misc_int.u);
}

static void cptvf_clear_irde_intr(struct cpt_vf *cptvf)
{
	union cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_MISC_INT(0, 0));
	/* W1C for the VF */
	vqx_misc_int.s.irde = 1;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_MISC_INT(0, 0),
			vqx_misc_int.u);
}

static void cptvf_clear_nwrp_intr(struct cpt_vf *cptvf)
{
	union cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_MISC_INT(0, 0));
	/* W1C for the VF */
	vqx_misc_int.s.nwrp = 1;
	cpt_write_csr64(cptvf->reg_base,
			CPTX_VQX_MISC_INT(0, 0), vqx_misc_int.u);
}

static void cptvf_clear_mbox_intr(struct cpt_vf *cptvf)
{
	union cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_MISC_INT(0, 0));
	/* W1C for the VF */
	vqx_misc_int.s.mbox = 1;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_MISC_INT(0, 0),
			vqx_misc_int.u);
}

static void cptvf_clear_swerr_intr(struct cpt_vf *cptvf)
{
	union cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_MISC_INT(0, 0));
	/* W1C for the VF */
	vqx_misc_int.s.swerr = 1;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_MISC_INT(0, 0),
			vqx_misc_int.u);
}

static u64 cptvf_read_vf_misc_intr_status(struct cpt_vf *cptvf)
{
	return cpt_read_csr64(cptvf->reg_base, CPTX_VQX_MISC_INT(0, 0));
}

static irqreturn_t cptvf_misc_intr_handler(int irq, void *cptvf_irq)
{
	struct cpt_vf *cptvf = (struct cpt_vf *)cptvf_irq;
	struct pci_dev *pdev = cptvf->pdev;
	u64 intr;

	intr = cptvf_read_vf_misc_intr_status(cptvf);
	/*Check for MISC interrupt types*/
	if (likely(intr & CPT_VF_INTR_MBOX_MASK)) {
		dev_dbg(&pdev->dev, "Mailbox interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
		cptvf_handle_mbox_intr(cptvf);
		cptvf_clear_mbox_intr(cptvf);
	} else if (unlikely(intr & CPT_VF_INTR_DOVF_MASK)) {
		cptvf_clear_dovf_intr(cptvf);
		/*Clear doorbell count*/
		cptvf_write_vq_doorbell(cptvf, 0);
		dev_err(&pdev->dev, "Doorbell overflow error interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
	} else if (unlikely(intr & CPT_VF_INTR_IRDE_MASK)) {
		cptvf_clear_irde_intr(cptvf);
		dev_err(&pdev->dev, "Instruction NCB read error interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
	} else if (unlikely(intr & CPT_VF_INTR_NWRP_MASK)) {
		cptvf_clear_nwrp_intr(cptvf);
		dev_err(&pdev->dev, "NCB response write error interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
	} else if (unlikely(intr & CPT_VF_INTR_SERR_MASK)) {
		cptvf_clear_swerr_intr(cptvf);
		dev_err(&pdev->dev, "Software error interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
	} else {
		dev_err(&pdev->dev, "Unhandled interrupt in CPT VF %d\n",
			cptvf->vfid);
	}

	return IRQ_HANDLED;
}

static inline struct cptvf_wqe *get_cptvf_vq_wqe(struct cpt_vf *cptvf,
						 int qno)
{
	struct cptvf_wqe_info *nwqe_info;

	if (unlikely(qno >= cptvf->nr_queues))
		return NULL;
	nwqe_info = (struct cptvf_wqe_info *)cptvf->wqe_info;

	return &nwqe_info->vq_wqe[qno];
}

static inline u32 cptvf_read_vq_done_count(struct cpt_vf *cptvf)
{
	union cptx_vqx_done vqx_done;

	vqx_done.u = cpt_read_csr64(cptvf->reg_base, CPTX_VQX_DONE(0, 0));
	return vqx_done.s.done;
}

static inline void cptvf_write_vq_done_ack(struct cpt_vf *cptvf,
					   u32 ackcnt)
{
	union cptx_vqx_done_ack vqx_dack_cnt;

	vqx_dack_cnt.u = cpt_read_csr64(cptvf->reg_base,
					CPTX_VQX_DONE_ACK(0, 0));
	vqx_dack_cnt.s.done_ack = ackcnt;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_DONE_ACK(0, 0),
			vqx_dack_cnt.u);
}

static irqreturn_t cptvf_done_intr_handler(int irq, void *cptvf_irq)
{
	struct cpt_vf *cptvf = (struct cpt_vf *)cptvf_irq;
	struct pci_dev *pdev = cptvf->pdev;
	/* Read the number of completions */
	u32 intr = cptvf_read_vq_done_count(cptvf);

	if (intr) {
		struct cptvf_wqe *wqe;

		/* Acknowledge the number of
		 * scheduled completions for processing
		 */
		cptvf_write_vq_done_ack(cptvf, intr);
		wqe = get_cptvf_vq_wqe(cptvf, 0);
		if (unlikely(!wqe)) {
			dev_err(&pdev->dev, "No work to schedule for VF (%d)",
				cptvf->vfid);
			return IRQ_NONE;
		}
		tasklet_hi_schedule(&wqe->twork);
	}

	return IRQ_HANDLED;
}

static void cptvf_set_irq_affinity(struct cpt_vf *cptvf, int vec)
{
	struct pci_dev *pdev = cptvf->pdev;
	int cpu;

	if (!zalloc_cpumask_var(&cptvf->affinity_mask[vec],
				GFP_KERNEL)) {
		dev_err(&pdev->dev, "Allocation failed for affinity_mask for VF %d",
			cptvf->vfid);
		return;
	}

	cpu = cptvf->vfid % num_online_cpus();
	cpumask_set_cpu(cpumask_local_spread(cpu, cptvf->node),
			cptvf->affinity_mask[vec]);
	irq_set_affinity_hint(pci_irq_vector(pdev, vec),
			cptvf->affinity_mask[vec]);
}

static void cptvf_write_vq_saddr(struct cpt_vf *cptvf, u64 val)
{
	union cptx_vqx_saddr vqx_saddr;

	vqx_saddr.u = val;
	cpt_write_csr64(cptvf->reg_base, CPTX_VQX_SADDR(0, 0), vqx_saddr.u);
}

static void cptvf_device_init(struct cpt_vf *cptvf)
{
	u64 base_addr = 0;

	/* Disable the VQ */
	cptvf_write_vq_ctl(cptvf, 0);
	/* Reset the doorbell */
	cptvf_write_vq_doorbell(cptvf, 0);
	/* Clear inflight */
	cptvf_write_vq_inprog(cptvf, 0);
	/* Write VQ SADDR */
	/* TODO: for now only one queue, so hard coded */
	base_addr = (u64)(cptvf->cqinfo.queue[0].qhead->dma_addr);
	cptvf_write_vq_saddr(cptvf, base_addr);
	/* Configure timerhold / coalescence */
	cptvf_write_vq_done_timewait(cptvf, CPT_TIMER_THOLD);
	cptvf_write_vq_done_numwait(cptvf, 1);
	/* Enable the VQ */
	cptvf_write_vq_ctl(cptvf, 1);
	/* Flag the VF ready */
	cptvf->flags |= CPT_FLAG_DEVICE_READY;
}

static int cptvf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct cpt_vf *cptvf;
	int    err;

	cptvf = devm_kzalloc(dev, sizeof(*cptvf), GFP_KERNEL);
	if (!cptvf)
		return -ENOMEM;

	pci_set_drvdata(pdev, cptvf);
	cptvf->pdev = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		pci_set_drvdata(pdev, NULL);
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto cptvf_err_disable_device;
	}
	/* Mark as VF driver */
	cptvf->flags |= CPT_FLAG_VF_DRIVER;
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get usable DMA configuration\n");
		goto cptvf_err_release_regions;
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get 48-bit DMA for consistent allocations\n");
		goto cptvf_err_release_regions;
	}

	/* MAP PF's configuration registers */
	cptvf->reg_base = pcim_iomap(pdev, 0, 0);
	if (!cptvf->reg_base) {
		dev_err(dev, "Cannot map config register space, aborting\n");
		err = -ENOMEM;
		goto cptvf_err_release_regions;
	}

	cptvf->node = dev_to_node(&pdev->dev);
	err = pci_alloc_irq_vectors(pdev, CPT_VF_MSIX_VECTORS,
			CPT_VF_MSIX_VECTORS, PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(dev, "Request for #%d msix vectors failed\n",
			CPT_VF_MSIX_VECTORS);
		goto cptvf_err_release_regions;
	}

	err = request_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_MISC),
			  cptvf_misc_intr_handler, 0, "CPT VF misc intr",
			  cptvf);
	if (err) {
		dev_err(dev, "Request misc irq failed");
		goto cptvf_free_vectors;
	}

	/* Enable mailbox interrupt */
	cptvf_enable_mbox_interrupts(cptvf);
	cptvf_enable_swerr_interrupts(cptvf);

	/* Check ready with PF */
	/* Gets chip ID / device Id from PF if ready */
	err = cptvf_check_pf_ready(cptvf);
	if (err) {
		dev_err(dev, "PF not responding to READY msg");
		goto cptvf_free_misc_irq;
	}

	/* CPT VF software resources initialization */
	cptvf->cqinfo.qchunksize = CPT_CMD_QCHUNK_SIZE;
	err = cptvf_sw_init(cptvf, CPT_CMD_QLEN, CPT_NUM_QS_PER_VF);
	if (err) {
		dev_err(dev, "cptvf_sw_init() failed");
		goto cptvf_free_misc_irq;
	}
	/* Convey VQ LEN to PF */
	err = cptvf_send_vq_size_msg(cptvf);
	if (err) {
		dev_err(dev, "PF not responding to QLEN msg");
		goto cptvf_free_misc_irq;
	}

	/* CPT VF device initialization */
	cptvf_device_init(cptvf);
	/* Send msg to PF to assign currnet Q to required group */
	cptvf->vfgrp = 1;
	err = cptvf_send_vf_to_grp_msg(cptvf);
	if (err) {
		dev_err(dev, "PF not responding to VF_GRP msg");
		goto cptvf_free_misc_irq;
	}

	cptvf->priority = 1;
	err = cptvf_send_vf_priority_msg(cptvf);
	if (err) {
		dev_err(dev, "PF not responding to VF_PRIO msg");
		goto cptvf_free_misc_irq;
	}

	err = request_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_DONE),
			  cptvf_done_intr_handler, 0, "CPT VF done intr",
			  cptvf);
	if (err) {
		dev_err(dev, "Request done irq failed\n");
		goto cptvf_free_misc_irq;
	}

	/* Enable mailbox interrupt */
	cptvf_enable_done_interrupts(cptvf);

	/* Set irq affinity masks */
	cptvf_set_irq_affinity(cptvf, CPT_VF_INT_VEC_E_MISC);
	cptvf_set_irq_affinity(cptvf, CPT_VF_INT_VEC_E_DONE);

	err = cptvf_send_vf_up(cptvf);
	if (err) {
		dev_err(dev, "PF not responding to UP msg");
		goto cptvf_free_irq_affinity;
	}
	err = cvm_crypto_init(cptvf);
	if (err) {
		dev_err(dev, "Algorithm register failed\n");
		goto cptvf_free_irq_affinity;
	}
	return 0;

cptvf_free_irq_affinity:
	cptvf_free_irq_affinity(cptvf, CPT_VF_INT_VEC_E_DONE);
	cptvf_free_irq_affinity(cptvf, CPT_VF_INT_VEC_E_MISC);
cptvf_free_misc_irq:
	free_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_MISC), cptvf);
cptvf_free_vectors:
	pci_free_irq_vectors(cptvf->pdev);
cptvf_err_release_regions:
	pci_release_regions(pdev);
cptvf_err_disable_device:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	return err;
}

static void cptvf_remove(struct pci_dev *pdev)
{
	struct cpt_vf *cptvf = pci_get_drvdata(pdev);

	if (!cptvf) {
		dev_err(&pdev->dev, "Invalid CPT-VF device\n");
		return;
	}

	/* Convey DOWN to PF */
	if (cptvf_send_vf_down(cptvf)) {
		dev_err(&pdev->dev, "PF not responding to DOWN msg");
	} else {
		cptvf_free_irq_affinity(cptvf, CPT_VF_INT_VEC_E_DONE);
		cptvf_free_irq_affinity(cptvf, CPT_VF_INT_VEC_E_MISC);
		free_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_DONE), cptvf);
		free_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_MISC), cptvf);
		pci_free_irq_vectors(cptvf->pdev);
		cptvf_sw_cleanup(cptvf);
		pci_set_drvdata(pdev, NULL);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		cvm_crypto_exit();
	}
}

static void cptvf_shutdown(struct pci_dev *pdev)
{
	cptvf_remove(pdev);
}

/* Supported devices */
static const struct pci_device_id cptvf_id_table[] = {
	{PCI_VDEVICE(CAVIUM, CPT_81XX_PCI_VF_DEVICE_ID), 0},
	{ 0, }  /* end of table */
};

static struct pci_driver cptvf_pci_driver = {
	.name = DRV_NAME,
	.id_table = cptvf_id_table,
	.probe = cptvf_probe,
	.remove = cptvf_remove,
	.shutdown = cptvf_shutdown,
};

module_pci_driver(cptvf_pci_driver);

MODULE_AUTHOR("George Cherian <george.cherian@cavium.com>");
MODULE_DESCRIPTION("Cavium Thunder CPT Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, cptvf_id_table);
