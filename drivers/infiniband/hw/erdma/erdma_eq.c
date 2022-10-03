// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "erdma.h"
#include "erdma_hw.h"
#include "erdma_verbs.h"

#define MAX_POLL_CHUNK_SIZE 16

void notify_eq(struct erdma_eq *eq)
{
	u64 db_data = FIELD_PREP(ERDMA_EQDB_CI_MASK, eq->ci) |
		      FIELD_PREP(ERDMA_EQDB_ARM_MASK, 1);

	*eq->db_record = db_data;
	writeq(db_data, eq->db_addr);

	atomic64_inc(&eq->notify_num);
}

void *get_next_valid_eqe(struct erdma_eq *eq)
{
	u64 *eqe = get_queue_entry(eq->qbuf, eq->ci, eq->depth, EQE_SHIFT);
	u32 owner = FIELD_GET(ERDMA_CEQE_HDR_O_MASK, READ_ONCE(*eqe));

	return owner ^ !!(eq->ci & eq->depth) ? eqe : NULL;
}

void erdma_aeq_event_handler(struct erdma_dev *dev)
{
	struct erdma_aeqe *aeqe;
	u32 cqn, qpn;
	struct erdma_qp *qp;
	struct erdma_cq *cq;
	struct ib_event event;
	u32 poll_cnt = 0;

	memset(&event, 0, sizeof(event));

	while (poll_cnt < MAX_POLL_CHUNK_SIZE) {
		aeqe = get_next_valid_eqe(&dev->aeq);
		if (!aeqe)
			break;

		dma_rmb();

		dev->aeq.ci++;
		atomic64_inc(&dev->aeq.event_num);
		poll_cnt++;

		if (FIELD_GET(ERDMA_AEQE_HDR_TYPE_MASK,
			      le32_to_cpu(aeqe->hdr)) == ERDMA_AE_TYPE_CQ_ERR) {
			cqn = le32_to_cpu(aeqe->event_data0);
			cq = find_cq_by_cqn(dev, cqn);
			if (!cq)
				continue;

			event.device = cq->ibcq.device;
			event.element.cq = &cq->ibcq;
			event.event = IB_EVENT_CQ_ERR;
			if (cq->ibcq.event_handler)
				cq->ibcq.event_handler(&event,
						       cq->ibcq.cq_context);
		} else {
			qpn = le32_to_cpu(aeqe->event_data0);
			qp = find_qp_by_qpn(dev, qpn);
			if (!qp)
				continue;

			event.device = qp->ibqp.device;
			event.element.qp = &qp->ibqp;
			event.event = IB_EVENT_QP_FATAL;
			if (qp->ibqp.event_handler)
				qp->ibqp.event_handler(&event,
						       qp->ibqp.qp_context);
		}
	}

	notify_eq(&dev->aeq);
}

int erdma_aeq_init(struct erdma_dev *dev)
{
	struct erdma_eq *eq = &dev->aeq;
	u32 buf_size;

	eq->depth = ERDMA_DEFAULT_EQ_DEPTH;
	buf_size = eq->depth << EQE_SHIFT;

	eq->qbuf =
		dma_alloc_coherent(&dev->pdev->dev, WARPPED_BUFSIZE(buf_size),
				   &eq->qbuf_dma_addr, GFP_KERNEL | __GFP_ZERO);
	if (!eq->qbuf)
		return -ENOMEM;

	spin_lock_init(&eq->lock);
	atomic64_set(&eq->event_num, 0);
	atomic64_set(&eq->notify_num, 0);

	eq->db_addr = (u64 __iomem *)(dev->func_bar + ERDMA_REGS_AEQ_DB_REG);
	eq->db_record = (u64 *)(eq->qbuf + buf_size);

	erdma_reg_write32(dev, ERDMA_REGS_AEQ_ADDR_H_REG,
			  upper_32_bits(eq->qbuf_dma_addr));
	erdma_reg_write32(dev, ERDMA_REGS_AEQ_ADDR_L_REG,
			  lower_32_bits(eq->qbuf_dma_addr));
	erdma_reg_write32(dev, ERDMA_REGS_AEQ_DEPTH_REG, eq->depth);
	erdma_reg_write64(dev, ERDMA_AEQ_DB_HOST_ADDR_REG,
			  eq->qbuf_dma_addr + buf_size);

	return 0;
}

void erdma_aeq_destroy(struct erdma_dev *dev)
{
	struct erdma_eq *eq = &dev->aeq;

	dma_free_coherent(&dev->pdev->dev,
			  WARPPED_BUFSIZE(eq->depth << EQE_SHIFT), eq->qbuf,
			  eq->qbuf_dma_addr);
}

void erdma_ceq_completion_handler(struct erdma_eq_cb *ceq_cb)
{
	struct erdma_dev *dev = ceq_cb->dev;
	struct erdma_cq *cq;
	u32 poll_cnt = 0;
	u64 *ceqe;
	int cqn;

	if (!ceq_cb->ready)
		return;

	while (poll_cnt < MAX_POLL_CHUNK_SIZE) {
		ceqe = get_next_valid_eqe(&ceq_cb->eq);
		if (!ceqe)
			break;

		dma_rmb();
		ceq_cb->eq.ci++;
		poll_cnt++;
		cqn = FIELD_GET(ERDMA_CEQE_HDR_CQN_MASK, READ_ONCE(*ceqe));

		cq = find_cq_by_cqn(dev, cqn);
		if (!cq)
			continue;

		if (rdma_is_kernel_res(&cq->ibcq.res))
			cq->kern_cq.cmdsn++;

		if (cq->ibcq.comp_handler)
			cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
	}

	notify_eq(&ceq_cb->eq);
}

static irqreturn_t erdma_intr_ceq_handler(int irq, void *data)
{
	struct erdma_eq_cb *ceq_cb = data;

	tasklet_schedule(&ceq_cb->tasklet);

	return IRQ_HANDLED;
}

static void erdma_intr_ceq_task(unsigned long data)
{
	erdma_ceq_completion_handler((struct erdma_eq_cb *)data);
}

static int erdma_set_ceq_irq(struct erdma_dev *dev, u16 ceqn)
{
	struct erdma_eq_cb *eqc = &dev->ceqs[ceqn];
	int err;

	snprintf(eqc->irq.name, ERDMA_IRQNAME_SIZE, "erdma-ceq%u@pci:%s", ceqn,
		 pci_name(dev->pdev));
	eqc->irq.msix_vector = pci_irq_vector(dev->pdev, ceqn + 1);

	tasklet_init(&dev->ceqs[ceqn].tasklet, erdma_intr_ceq_task,
		     (unsigned long)&dev->ceqs[ceqn]);

	cpumask_set_cpu(cpumask_local_spread(ceqn + 1, dev->attrs.numa_node),
			&eqc->irq.affinity_hint_mask);

	err = request_irq(eqc->irq.msix_vector, erdma_intr_ceq_handler, 0,
			  eqc->irq.name, eqc);
	if (err) {
		dev_err(&dev->pdev->dev, "failed to request_irq(%d)\n", err);
		return err;
	}

	irq_set_affinity_hint(eqc->irq.msix_vector,
			      &eqc->irq.affinity_hint_mask);

	return 0;
}

static void erdma_free_ceq_irq(struct erdma_dev *dev, u16 ceqn)
{
	struct erdma_eq_cb *eqc = &dev->ceqs[ceqn];

	irq_set_affinity_hint(eqc->irq.msix_vector, NULL);
	free_irq(eqc->irq.msix_vector, eqc);
}

static int create_eq_cmd(struct erdma_dev *dev, u32 eqn, struct erdma_eq *eq)
{
	struct erdma_cmdq_create_eq_req req;
	dma_addr_t db_info_dma_addr;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_CREATE_EQ);
	req.eqn = eqn;
	req.depth = ilog2(eq->depth);
	req.qbuf_addr = eq->qbuf_dma_addr;
	req.qtype = ERDMA_EQ_TYPE_CEQ;
	/* Vector index is the same as EQN. */
	req.vector_idx = eqn;
	db_info_dma_addr = eq->qbuf_dma_addr + (eq->depth << EQE_SHIFT);
	req.db_dma_addr_l = lower_32_bits(db_info_dma_addr);
	req.db_dma_addr_h = upper_32_bits(db_info_dma_addr);

	return erdma_post_cmd_wait(&dev->cmdq, (u64 *)&req,
				   sizeof(struct erdma_cmdq_create_eq_req),
				   NULL, NULL);
}

static int erdma_ceq_init_one(struct erdma_dev *dev, u16 ceqn)
{
	struct erdma_eq *eq = &dev->ceqs[ceqn].eq;
	u32 buf_size = ERDMA_DEFAULT_EQ_DEPTH << EQE_SHIFT;
	int ret;

	eq->qbuf =
		dma_alloc_coherent(&dev->pdev->dev, WARPPED_BUFSIZE(buf_size),
				   &eq->qbuf_dma_addr, GFP_KERNEL | __GFP_ZERO);
	if (!eq->qbuf)
		return -ENOMEM;

	spin_lock_init(&eq->lock);
	atomic64_set(&eq->event_num, 0);
	atomic64_set(&eq->notify_num, 0);

	eq->depth = ERDMA_DEFAULT_EQ_DEPTH;
	eq->db_addr =
		(u64 __iomem *)(dev->func_bar + ERDMA_REGS_CEQ_DB_BASE_REG +
				(ceqn + 1) * ERDMA_DB_SIZE);
	eq->db_record = (u64 *)(eq->qbuf + buf_size);
	eq->ci = 0;
	dev->ceqs[ceqn].dev = dev;

	/* CEQ indexed from 1, 0 rsvd for CMDQ-EQ. */
	ret = create_eq_cmd(dev, ceqn + 1, eq);
	dev->ceqs[ceqn].ready = ret ? false : true;

	return ret;
}

static void erdma_ceq_uninit_one(struct erdma_dev *dev, u16 ceqn)
{
	struct erdma_eq *eq = &dev->ceqs[ceqn].eq;
	u32 buf_size = ERDMA_DEFAULT_EQ_DEPTH << EQE_SHIFT;
	struct erdma_cmdq_destroy_eq_req req;
	int err;

	dev->ceqs[ceqn].ready = 0;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_DESTROY_EQ);
	/* CEQ indexed from 1, 0 rsvd for CMDQ-EQ. */
	req.eqn = ceqn + 1;
	req.qtype = ERDMA_EQ_TYPE_CEQ;
	req.vector_idx = ceqn + 1;

	err = erdma_post_cmd_wait(&dev->cmdq, (u64 *)&req, sizeof(req), NULL,
				  NULL);
	if (err)
		return;

	dma_free_coherent(&dev->pdev->dev, WARPPED_BUFSIZE(buf_size), eq->qbuf,
			  eq->qbuf_dma_addr);
}

int erdma_ceqs_init(struct erdma_dev *dev)
{
	u32 i, j;
	int err;

	for (i = 0; i < dev->attrs.irq_num - 1; i++) {
		err = erdma_ceq_init_one(dev, i);
		if (err)
			goto out_err;

		err = erdma_set_ceq_irq(dev, i);
		if (err) {
			erdma_ceq_uninit_one(dev, i);
			goto out_err;
		}
	}

	return 0;

out_err:
	for (j = 0; j < i; j++) {
		erdma_free_ceq_irq(dev, j);
		erdma_ceq_uninit_one(dev, j);
	}

	return err;
}

void erdma_ceqs_uninit(struct erdma_dev *dev)
{
	u32 i;

	for (i = 0; i < dev->attrs.irq_num - 1; i++) {
		erdma_free_ceq_irq(dev, i);
		erdma_ceq_uninit_one(dev, i);
	}
}
