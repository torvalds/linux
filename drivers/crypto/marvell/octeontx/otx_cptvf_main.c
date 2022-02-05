// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include "otx_cptvf.h"
#include "otx_cptvf_algs.h"
#include "otx_cptvf_reqmgr.h"

#define DRV_NAME	"octeontx-cptvf"
#define DRV_VERSION	"1.0"

static void vq_work_handler(unsigned long data)
{
	struct otx_cptvf_wqe_info *cwqe_info =
					(struct otx_cptvf_wqe_info *) data;

	otx_cpt_post_process(&cwqe_info->vq_wqe[0]);
}

static int init_worker_threads(struct otx_cptvf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct otx_cptvf_wqe_info *cwqe_info;
	int i;

	cwqe_info = kzalloc(sizeof(*cwqe_info), GFP_KERNEL);
	if (!cwqe_info)
		return -ENOMEM;

	if (cptvf->num_queues) {
		dev_dbg(&pdev->dev, "Creating VQ worker threads (%d)\n",
			cptvf->num_queues);
	}

	for (i = 0; i < cptvf->num_queues; i++) {
		tasklet_init(&cwqe_info->vq_wqe[i].twork, vq_work_handler,
			     (u64)cwqe_info);
		cwqe_info->vq_wqe[i].cptvf = cptvf;
	}
	cptvf->wqe_info = cwqe_info;

	return 0;
}

static void cleanup_worker_threads(struct otx_cptvf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct otx_cptvf_wqe_info *cwqe_info;
	int i;

	cwqe_info = (struct otx_cptvf_wqe_info *)cptvf->wqe_info;
	if (!cwqe_info)
		return;

	if (cptvf->num_queues) {
		dev_dbg(&pdev->dev, "Cleaning VQ worker threads (%u)\n",
			cptvf->num_queues);
	}

	for (i = 0; i < cptvf->num_queues; i++)
		tasklet_kill(&cwqe_info->vq_wqe[i].twork);

	kfree_sensitive(cwqe_info);
	cptvf->wqe_info = NULL;
}

static void free_pending_queues(struct otx_cpt_pending_qinfo *pqinfo)
{
	struct otx_cpt_pending_queue *queue;
	int i;

	for_each_pending_queue(pqinfo, queue, i) {
		if (!queue->head)
			continue;

		/* free single queue */
		kfree_sensitive((queue->head));
		queue->front = 0;
		queue->rear = 0;
		queue->qlen = 0;
	}
	pqinfo->num_queues = 0;
}

static int alloc_pending_queues(struct otx_cpt_pending_qinfo *pqinfo, u32 qlen,
				u32 num_queues)
{
	struct otx_cpt_pending_queue *queue = NULL;
	int ret;
	u32 i;

	pqinfo->num_queues = num_queues;

	for_each_pending_queue(pqinfo, queue, i) {
		queue->head = kcalloc(qlen, sizeof(*queue->head), GFP_KERNEL);
		if (!queue->head) {
			ret = -ENOMEM;
			goto pending_qfail;
		}

		queue->pending_count = 0;
		queue->front = 0;
		queue->rear = 0;
		queue->qlen = qlen;

		/* init queue spin lock */
		spin_lock_init(&queue->lock);
	}
	return 0;

pending_qfail:
	free_pending_queues(pqinfo);

	return ret;
}

static int init_pending_queues(struct otx_cptvf *cptvf, u32 qlen,
			       u32 num_queues)
{
	struct pci_dev *pdev = cptvf->pdev;
	int ret;

	if (!num_queues)
		return 0;

	ret = alloc_pending_queues(&cptvf->pqinfo, qlen, num_queues);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup pending queues (%u)\n",
			num_queues);
		return ret;
	}
	return 0;
}

static void cleanup_pending_queues(struct otx_cptvf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;

	if (!cptvf->num_queues)
		return;

	dev_dbg(&pdev->dev, "Cleaning VQ pending queue (%u)\n",
		cptvf->num_queues);
	free_pending_queues(&cptvf->pqinfo);
}

static void free_command_queues(struct otx_cptvf *cptvf,
				struct otx_cpt_cmd_qinfo *cqinfo)
{
	struct otx_cpt_cmd_queue *queue = NULL;
	struct otx_cpt_cmd_chunk *chunk = NULL;
	struct pci_dev *pdev = cptvf->pdev;
	int i;

	/* clean up for each queue */
	for (i = 0; i < cptvf->num_queues; i++) {
		queue = &cqinfo->queue[i];

		while (!list_empty(&cqinfo->queue[i].chead)) {
			chunk = list_first_entry(&cqinfo->queue[i].chead,
					struct otx_cpt_cmd_chunk, nextchunk);

			dma_free_coherent(&pdev->dev, chunk->size,
					  chunk->head,
					  chunk->dma_addr);
			chunk->head = NULL;
			chunk->dma_addr = 0;
			list_del(&chunk->nextchunk);
			kfree_sensitive(chunk);
		}
		queue->num_chunks = 0;
		queue->idx = 0;

	}
}

static int alloc_command_queues(struct otx_cptvf *cptvf,
				struct otx_cpt_cmd_qinfo *cqinfo,
				u32 qlen)
{
	struct otx_cpt_cmd_chunk *curr, *first, *last;
	struct otx_cpt_cmd_queue *queue = NULL;
	struct pci_dev *pdev = cptvf->pdev;
	size_t q_size, c_size, rem_q_size;
	u32 qcsize_bytes;
	int i;


	/* Qsize in dwords, needed for SADDR config, 1-next chunk pointer */
	cptvf->qsize = min(qlen, cqinfo->qchunksize) *
		       OTX_CPT_NEXT_CHUNK_PTR_SIZE + 1;
	/* Qsize in bytes to create space for alignment */
	q_size = qlen * OTX_CPT_INST_SIZE;

	qcsize_bytes = cqinfo->qchunksize * OTX_CPT_INST_SIZE;

	/* per queue initialization */
	for (i = 0; i < cptvf->num_queues; i++) {
		c_size = 0;
		rem_q_size = q_size;
		first = NULL;
		last = NULL;

		queue = &cqinfo->queue[i];
		INIT_LIST_HEAD(&queue->chead);
		do {
			curr = kzalloc(sizeof(*curr), GFP_KERNEL);
			if (!curr)
				goto cmd_qfail;

			c_size = (rem_q_size > qcsize_bytes) ? qcsize_bytes :
					rem_q_size;
			curr->head = dma_alloc_coherent(&pdev->dev,
					   c_size + OTX_CPT_NEXT_CHUNK_PTR_SIZE,
					   &curr->dma_addr, GFP_KERNEL);
			if (!curr->head) {
				dev_err(&pdev->dev,
				"Command Q (%d) chunk (%d) allocation failed\n",
					i, queue->num_chunks);
				goto free_curr;
			}
			curr->size = c_size;

			if (queue->num_chunks == 0) {
				first = curr;
				queue->base  = first;
			}
			list_add_tail(&curr->nextchunk,
				      &cqinfo->queue[i].chead);

			queue->num_chunks++;
			rem_q_size -= c_size;
			if (last)
				*((u64 *)(&last->head[last->size])) =
					(u64)curr->dma_addr;

			last = curr;
		} while (rem_q_size);

		/*
		 * Make the queue circular, tie back last chunk entry to head
		 */
		curr = first;
		*((u64 *)(&last->head[last->size])) = (u64)curr->dma_addr;
		queue->qhead = curr;
	}
	return 0;
free_curr:
	kfree(curr);
cmd_qfail:
	free_command_queues(cptvf, cqinfo);
	return -ENOMEM;
}

static int init_command_queues(struct otx_cptvf *cptvf, u32 qlen)
{
	struct pci_dev *pdev = cptvf->pdev;
	int ret;

	/* setup command queues */
	ret = alloc_command_queues(cptvf, &cptvf->cqinfo, qlen);
	if (ret) {
		dev_err(&pdev->dev, "Failed to allocate command queues (%u)\n",
			cptvf->num_queues);
		return ret;
	}
	return ret;
}

static void cleanup_command_queues(struct otx_cptvf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;

	if (!cptvf->num_queues)
		return;

	dev_dbg(&pdev->dev, "Cleaning VQ command queue (%u)\n",
		cptvf->num_queues);
	free_command_queues(cptvf, &cptvf->cqinfo);
}

static void cptvf_sw_cleanup(struct otx_cptvf *cptvf)
{
	cleanup_worker_threads(cptvf);
	cleanup_pending_queues(cptvf);
	cleanup_command_queues(cptvf);
}

static int cptvf_sw_init(struct otx_cptvf *cptvf, u32 qlen, u32 num_queues)
{
	struct pci_dev *pdev = cptvf->pdev;
	u32 max_dev_queues = 0;
	int ret;

	max_dev_queues = OTX_CPT_NUM_QS_PER_VF;
	/* possible cpus */
	num_queues = min_t(u32, num_queues, max_dev_queues);
	cptvf->num_queues = num_queues;

	ret = init_command_queues(cptvf, qlen);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup command queues (%u)\n",
			num_queues);
		return ret;
	}

	ret = init_pending_queues(cptvf, qlen, num_queues);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup pending queues (%u)\n",
			num_queues);
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

static void cptvf_free_irq_affinity(struct otx_cptvf *cptvf, int vec)
{
	irq_set_affinity_hint(pci_irq_vector(cptvf->pdev, vec), NULL);
	free_cpumask_var(cptvf->affinity_mask[vec]);
}

static void cptvf_write_vq_ctl(struct otx_cptvf *cptvf, bool val)
{
	union otx_cptx_vqx_ctl vqx_ctl;

	vqx_ctl.u = readq(cptvf->reg_base + OTX_CPT_VQX_CTL(0));
	vqx_ctl.s.ena = val;
	writeq(vqx_ctl.u, cptvf->reg_base + OTX_CPT_VQX_CTL(0));
}

void otx_cptvf_write_vq_doorbell(struct otx_cptvf *cptvf, u32 val)
{
	union otx_cptx_vqx_doorbell vqx_dbell;

	vqx_dbell.u = readq(cptvf->reg_base + OTX_CPT_VQX_DOORBELL(0));
	vqx_dbell.s.dbell_cnt = val * 8; /* Num of Instructions * 8 words */
	writeq(vqx_dbell.u, cptvf->reg_base + OTX_CPT_VQX_DOORBELL(0));
}

static void cptvf_write_vq_inprog(struct otx_cptvf *cptvf, u8 val)
{
	union otx_cptx_vqx_inprog vqx_inprg;

	vqx_inprg.u = readq(cptvf->reg_base + OTX_CPT_VQX_INPROG(0));
	vqx_inprg.s.inflight = val;
	writeq(vqx_inprg.u, cptvf->reg_base + OTX_CPT_VQX_INPROG(0));
}

static void cptvf_write_vq_done_numwait(struct otx_cptvf *cptvf, u32 val)
{
	union otx_cptx_vqx_done_wait vqx_dwait;

	vqx_dwait.u = readq(cptvf->reg_base + OTX_CPT_VQX_DONE_WAIT(0));
	vqx_dwait.s.num_wait = val;
	writeq(vqx_dwait.u, cptvf->reg_base + OTX_CPT_VQX_DONE_WAIT(0));
}

static u32 cptvf_read_vq_done_numwait(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_done_wait vqx_dwait;

	vqx_dwait.u = readq(cptvf->reg_base + OTX_CPT_VQX_DONE_WAIT(0));
	return vqx_dwait.s.num_wait;
}

static void cptvf_write_vq_done_timewait(struct otx_cptvf *cptvf, u16 time)
{
	union otx_cptx_vqx_done_wait vqx_dwait;

	vqx_dwait.u = readq(cptvf->reg_base + OTX_CPT_VQX_DONE_WAIT(0));
	vqx_dwait.s.time_wait = time;
	writeq(vqx_dwait.u, cptvf->reg_base + OTX_CPT_VQX_DONE_WAIT(0));
}


static u16 cptvf_read_vq_done_timewait(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_done_wait vqx_dwait;

	vqx_dwait.u = readq(cptvf->reg_base + OTX_CPT_VQX_DONE_WAIT(0));
	return vqx_dwait.s.time_wait;
}

static void cptvf_enable_swerr_interrupts(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_misc_ena_w1s vqx_misc_ena;

	vqx_misc_ena.u = readq(cptvf->reg_base + OTX_CPT_VQX_MISC_ENA_W1S(0));
	/* Enable SWERR interrupts for the requested VF */
	vqx_misc_ena.s.swerr = 1;
	writeq(vqx_misc_ena.u, cptvf->reg_base + OTX_CPT_VQX_MISC_ENA_W1S(0));
}

static void cptvf_enable_mbox_interrupts(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_misc_ena_w1s vqx_misc_ena;

	vqx_misc_ena.u = readq(cptvf->reg_base + OTX_CPT_VQX_MISC_ENA_W1S(0));
	/* Enable MBOX interrupt for the requested VF */
	vqx_misc_ena.s.mbox = 1;
	writeq(vqx_misc_ena.u, cptvf->reg_base + OTX_CPT_VQX_MISC_ENA_W1S(0));
}

static void cptvf_enable_done_interrupts(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_done_ena_w1s vqx_done_ena;

	vqx_done_ena.u = readq(cptvf->reg_base + OTX_CPT_VQX_DONE_ENA_W1S(0));
	/* Enable DONE interrupt for the requested VF */
	vqx_done_ena.s.done = 1;
	writeq(vqx_done_ena.u, cptvf->reg_base + OTX_CPT_VQX_DONE_ENA_W1S(0));
}

static void cptvf_clear_dovf_intr(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = readq(cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
	/* W1C for the VF */
	vqx_misc_int.s.dovf = 1;
	writeq(vqx_misc_int.u, cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
}

static void cptvf_clear_irde_intr(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = readq(cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
	/* W1C for the VF */
	vqx_misc_int.s.irde = 1;
	writeq(vqx_misc_int.u, cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
}

static void cptvf_clear_nwrp_intr(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = readq(cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
	/* W1C for the VF */
	vqx_misc_int.s.nwrp = 1;
	writeq(vqx_misc_int.u, cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
}

static void cptvf_clear_mbox_intr(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = readq(cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
	/* W1C for the VF */
	vqx_misc_int.s.mbox = 1;
	writeq(vqx_misc_int.u, cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
}

static void cptvf_clear_swerr_intr(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_misc_int vqx_misc_int;

	vqx_misc_int.u = readq(cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
	/* W1C for the VF */
	vqx_misc_int.s.swerr = 1;
	writeq(vqx_misc_int.u, cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
}

static u64 cptvf_read_vf_misc_intr_status(struct otx_cptvf *cptvf)
{
	return readq(cptvf->reg_base + OTX_CPT_VQX_MISC_INT(0));
}

static irqreturn_t cptvf_misc_intr_handler(int __always_unused irq,
					   void *arg)
{
	struct otx_cptvf *cptvf = arg;
	struct pci_dev *pdev = cptvf->pdev;
	u64 intr;

	intr = cptvf_read_vf_misc_intr_status(cptvf);
	/* Check for MISC interrupt types */
	if (likely(intr & OTX_CPT_VF_INTR_MBOX_MASK)) {
		dev_dbg(&pdev->dev, "Mailbox interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
		otx_cptvf_handle_mbox_intr(cptvf);
		cptvf_clear_mbox_intr(cptvf);
	} else if (unlikely(intr & OTX_CPT_VF_INTR_DOVF_MASK)) {
		cptvf_clear_dovf_intr(cptvf);
		/* Clear doorbell count */
		otx_cptvf_write_vq_doorbell(cptvf, 0);
		dev_err(&pdev->dev,
		"Doorbell overflow error interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
	} else if (unlikely(intr & OTX_CPT_VF_INTR_IRDE_MASK)) {
		cptvf_clear_irde_intr(cptvf);
		dev_err(&pdev->dev,
		"Instruction NCB read error interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
	} else if (unlikely(intr & OTX_CPT_VF_INTR_NWRP_MASK)) {
		cptvf_clear_nwrp_intr(cptvf);
		dev_err(&pdev->dev,
		"NCB response write error interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
	} else if (unlikely(intr & OTX_CPT_VF_INTR_SERR_MASK)) {
		cptvf_clear_swerr_intr(cptvf);
		dev_err(&pdev->dev,
			"Software error interrupt 0x%llx on CPT VF %d\n",
			intr, cptvf->vfid);
	} else {
		dev_err(&pdev->dev, "Unhandled interrupt in OTX_CPT VF %d\n",
			cptvf->vfid);
	}

	return IRQ_HANDLED;
}

static inline struct otx_cptvf_wqe *get_cptvf_vq_wqe(struct otx_cptvf *cptvf,
						     int qno)
{
	struct otx_cptvf_wqe_info *nwqe_info;

	if (unlikely(qno >= cptvf->num_queues))
		return NULL;
	nwqe_info = (struct otx_cptvf_wqe_info *)cptvf->wqe_info;

	return &nwqe_info->vq_wqe[qno];
}

static inline u32 cptvf_read_vq_done_count(struct otx_cptvf *cptvf)
{
	union otx_cptx_vqx_done vqx_done;

	vqx_done.u = readq(cptvf->reg_base + OTX_CPT_VQX_DONE(0));
	return vqx_done.s.done;
}

static inline void cptvf_write_vq_done_ack(struct otx_cptvf *cptvf,
					   u32 ackcnt)
{
	union otx_cptx_vqx_done_ack vqx_dack_cnt;

	vqx_dack_cnt.u = readq(cptvf->reg_base + OTX_CPT_VQX_DONE_ACK(0));
	vqx_dack_cnt.s.done_ack = ackcnt;
	writeq(vqx_dack_cnt.u, cptvf->reg_base + OTX_CPT_VQX_DONE_ACK(0));
}

static irqreturn_t cptvf_done_intr_handler(int __always_unused irq,
					   void *cptvf_dev)
{
	struct otx_cptvf *cptvf = (struct otx_cptvf *)cptvf_dev;
	struct pci_dev *pdev = cptvf->pdev;
	/* Read the number of completions */
	u32 intr = cptvf_read_vq_done_count(cptvf);

	if (intr) {
		struct otx_cptvf_wqe *wqe;

		/*
		 * Acknowledge the number of scheduled completions for
		 * processing
		 */
		cptvf_write_vq_done_ack(cptvf, intr);
		wqe = get_cptvf_vq_wqe(cptvf, 0);
		if (unlikely(!wqe)) {
			dev_err(&pdev->dev, "No work to schedule for VF (%d)\n",
				cptvf->vfid);
			return IRQ_NONE;
		}
		tasklet_hi_schedule(&wqe->twork);
	}

	return IRQ_HANDLED;
}

static void cptvf_set_irq_affinity(struct otx_cptvf *cptvf, int vec)
{
	struct pci_dev *pdev = cptvf->pdev;
	int cpu;

	if (!zalloc_cpumask_var(&cptvf->affinity_mask[vec],
				GFP_KERNEL)) {
		dev_err(&pdev->dev,
			"Allocation failed for affinity_mask for VF %d\n",
			cptvf->vfid);
		return;
	}

	cpu = cptvf->vfid % num_online_cpus();
	cpumask_set_cpu(cpumask_local_spread(cpu, cptvf->node),
			cptvf->affinity_mask[vec]);
	irq_set_affinity_hint(pci_irq_vector(pdev, vec),
			      cptvf->affinity_mask[vec]);
}

static void cptvf_write_vq_saddr(struct otx_cptvf *cptvf, u64 val)
{
	union otx_cptx_vqx_saddr vqx_saddr;

	vqx_saddr.u = val;
	writeq(vqx_saddr.u, cptvf->reg_base + OTX_CPT_VQX_SADDR(0));
}

static void cptvf_device_init(struct otx_cptvf *cptvf)
{
	u64 base_addr = 0;

	/* Disable the VQ */
	cptvf_write_vq_ctl(cptvf, 0);
	/* Reset the doorbell */
	otx_cptvf_write_vq_doorbell(cptvf, 0);
	/* Clear inflight */
	cptvf_write_vq_inprog(cptvf, 0);
	/* Write VQ SADDR */
	base_addr = (u64)(cptvf->cqinfo.queue[0].qhead->dma_addr);
	cptvf_write_vq_saddr(cptvf, base_addr);
	/* Configure timerhold / coalescence */
	cptvf_write_vq_done_timewait(cptvf, OTX_CPT_TIMER_HOLD);
	cptvf_write_vq_done_numwait(cptvf, OTX_CPT_COUNT_HOLD);
	/* Enable the VQ */
	cptvf_write_vq_ctl(cptvf, 1);
	/* Flag the VF ready */
	cptvf->flags |= OTX_CPT_FLAG_DEVICE_READY;
}

static ssize_t vf_type_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct otx_cptvf *cptvf = dev_get_drvdata(dev);
	char *msg;

	switch (cptvf->vftype) {
	case OTX_CPT_AE_TYPES:
		msg = "AE";
		break;

	case OTX_CPT_SE_TYPES:
		msg = "SE";
		break;

	default:
		msg = "Invalid";
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", msg);
}

static ssize_t vf_engine_group_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct otx_cptvf *cptvf = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", cptvf->vfgrp);
}

static ssize_t vf_engine_group_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct otx_cptvf *cptvf = dev_get_drvdata(dev);
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	if (val < 0)
		return -EINVAL;

	if (val >= OTX_CPT_MAX_ENGINE_GROUPS) {
		dev_err(dev, "Engine group >= than max available groups %d\n",
			OTX_CPT_MAX_ENGINE_GROUPS);
		return -EINVAL;
	}

	ret = otx_cptvf_send_vf_to_grp_msg(cptvf, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t vf_coalesc_time_wait_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct otx_cptvf *cptvf = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 cptvf_read_vq_done_timewait(cptvf));
}

static ssize_t vf_coalesc_num_wait_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct otx_cptvf *cptvf = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 cptvf_read_vq_done_numwait(cptvf));
}

static ssize_t vf_coalesc_time_wait_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct otx_cptvf *cptvf = dev_get_drvdata(dev);
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret != 0)
		return ret;

	if (val < OTX_CPT_COALESC_MIN_TIME_WAIT ||
	    val > OTX_CPT_COALESC_MAX_TIME_WAIT)
		return -EINVAL;

	cptvf_write_vq_done_timewait(cptvf, val);
	return count;
}

static ssize_t vf_coalesc_num_wait_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct otx_cptvf *cptvf = dev_get_drvdata(dev);
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret != 0)
		return ret;

	if (val < OTX_CPT_COALESC_MIN_NUM_WAIT ||
	    val > OTX_CPT_COALESC_MAX_NUM_WAIT)
		return -EINVAL;

	cptvf_write_vq_done_numwait(cptvf, val);
	return count;
}

static DEVICE_ATTR_RO(vf_type);
static DEVICE_ATTR_RW(vf_engine_group);
static DEVICE_ATTR_RW(vf_coalesc_time_wait);
static DEVICE_ATTR_RW(vf_coalesc_num_wait);

static struct attribute *otx_cptvf_attrs[] = {
	&dev_attr_vf_type.attr,
	&dev_attr_vf_engine_group.attr,
	&dev_attr_vf_coalesc_time_wait.attr,
	&dev_attr_vf_coalesc_num_wait.attr,
	NULL
};

static const struct attribute_group otx_cptvf_sysfs_group = {
	.attrs = otx_cptvf_attrs,
};

static int otx_cptvf_probe(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct otx_cptvf *cptvf;
	int err;

	cptvf = devm_kzalloc(dev, sizeof(*cptvf), GFP_KERNEL);
	if (!cptvf)
		return -ENOMEM;

	pci_set_drvdata(pdev, cptvf);
	cptvf->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto clear_drvdata;
	}
	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto disable_device;
	}
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get usable 48-bit DMA configuration\n");
		goto release_regions;
	}

	/* MAP PF's configuration registers */
	cptvf->reg_base = pci_iomap(pdev, OTX_CPT_VF_PCI_CFG_BAR, 0);
	if (!cptvf->reg_base) {
		dev_err(dev, "Cannot map config register space, aborting\n");
		err = -ENOMEM;
		goto release_regions;
	}

	cptvf->node = dev_to_node(&pdev->dev);
	err = pci_alloc_irq_vectors(pdev, OTX_CPT_VF_MSIX_VECTORS,
				    OTX_CPT_VF_MSIX_VECTORS, PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(dev, "Request for #%d msix vectors failed\n",
			OTX_CPT_VF_MSIX_VECTORS);
		goto unmap_region;
	}

	err = request_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_MISC),
			  cptvf_misc_intr_handler, 0, "CPT VF misc intr",
			  cptvf);
	if (err) {
		dev_err(dev, "Failed to request misc irq\n");
		goto free_vectors;
	}

	/* Enable mailbox interrupt */
	cptvf_enable_mbox_interrupts(cptvf);
	cptvf_enable_swerr_interrupts(cptvf);

	/* Check cpt pf status, gets chip ID / device Id from PF if ready */
	err = otx_cptvf_check_pf_ready(cptvf);
	if (err)
		goto free_misc_irq;

	/* CPT VF software resources initialization */
	cptvf->cqinfo.qchunksize = OTX_CPT_CMD_QCHUNK_SIZE;
	err = cptvf_sw_init(cptvf, OTX_CPT_CMD_QLEN, OTX_CPT_NUM_QS_PER_VF);
	if (err) {
		dev_err(dev, "cptvf_sw_init() failed\n");
		goto free_misc_irq;
	}
	/* Convey VQ LEN to PF */
	err = otx_cptvf_send_vq_size_msg(cptvf);
	if (err)
		goto sw_cleanup;

	/* CPT VF device initialization */
	cptvf_device_init(cptvf);
	/* Send msg to PF to assign currnet Q to required group */
	err = otx_cptvf_send_vf_to_grp_msg(cptvf, cptvf->vfgrp);
	if (err)
		goto sw_cleanup;

	cptvf->priority = 1;
	err = otx_cptvf_send_vf_priority_msg(cptvf);
	if (err)
		goto sw_cleanup;

	err = request_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_DONE),
			  cptvf_done_intr_handler, 0, "CPT VF done intr",
			  cptvf);
	if (err) {
		dev_err(dev, "Failed to request done irq\n");
		goto free_done_irq;
	}

	/* Enable done interrupt */
	cptvf_enable_done_interrupts(cptvf);

	/* Set irq affinity masks */
	cptvf_set_irq_affinity(cptvf, CPT_VF_INT_VEC_E_MISC);
	cptvf_set_irq_affinity(cptvf, CPT_VF_INT_VEC_E_DONE);

	err = otx_cptvf_send_vf_up(cptvf);
	if (err)
		goto free_irq_affinity;

	/* Initialize algorithms and set ops */
	err = otx_cpt_crypto_init(pdev, THIS_MODULE,
		    cptvf->vftype == OTX_CPT_SE_TYPES ? OTX_CPT_SE : OTX_CPT_AE,
		    cptvf->vftype, 1, cptvf->num_vfs);
	if (err) {
		dev_err(dev, "Failed to register crypto algs\n");
		goto free_irq_affinity;
	}

	err = sysfs_create_group(&dev->kobj, &otx_cptvf_sysfs_group);
	if (err) {
		dev_err(dev, "Creating sysfs entries failed\n");
		goto crypto_exit;
	}

	return 0;

crypto_exit:
	otx_cpt_crypto_exit(pdev, THIS_MODULE, cptvf->vftype);
free_irq_affinity:
	cptvf_free_irq_affinity(cptvf, CPT_VF_INT_VEC_E_DONE);
	cptvf_free_irq_affinity(cptvf, CPT_VF_INT_VEC_E_MISC);
free_done_irq:
	free_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_DONE), cptvf);
sw_cleanup:
	cptvf_sw_cleanup(cptvf);
free_misc_irq:
	free_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_MISC), cptvf);
free_vectors:
	pci_free_irq_vectors(cptvf->pdev);
unmap_region:
	pci_iounmap(pdev, cptvf->reg_base);
release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
clear_drvdata:
	pci_set_drvdata(pdev, NULL);

	return err;
}

static void otx_cptvf_remove(struct pci_dev *pdev)
{
	struct otx_cptvf *cptvf = pci_get_drvdata(pdev);

	if (!cptvf) {
		dev_err(&pdev->dev, "Invalid CPT-VF device\n");
		return;
	}

	/* Convey DOWN to PF */
	if (otx_cptvf_send_vf_down(cptvf)) {
		dev_err(&pdev->dev, "PF not responding to DOWN msg\n");
	} else {
		sysfs_remove_group(&pdev->dev.kobj, &otx_cptvf_sysfs_group);
		otx_cpt_crypto_exit(pdev, THIS_MODULE, cptvf->vftype);
		cptvf_free_irq_affinity(cptvf, CPT_VF_INT_VEC_E_DONE);
		cptvf_free_irq_affinity(cptvf, CPT_VF_INT_VEC_E_MISC);
		free_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_DONE), cptvf);
		free_irq(pci_irq_vector(pdev, CPT_VF_INT_VEC_E_MISC), cptvf);
		cptvf_sw_cleanup(cptvf);
		pci_free_irq_vectors(cptvf->pdev);
		pci_iounmap(pdev, cptvf->reg_base);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}

/* Supported devices */
static const struct pci_device_id otx_cptvf_id_table[] = {
	{PCI_VDEVICE(CAVIUM, OTX_CPT_PCI_VF_DEVICE_ID), 0},
	{ 0, }  /* end of table */
};

static struct pci_driver otx_cptvf_pci_driver = {
	.name = DRV_NAME,
	.id_table = otx_cptvf_id_table,
	.probe = otx_cptvf_probe,
	.remove = otx_cptvf_remove,
};

module_pci_driver(otx_cptvf_pci_driver);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell OcteonTX CPT Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, otx_cptvf_id_table);
