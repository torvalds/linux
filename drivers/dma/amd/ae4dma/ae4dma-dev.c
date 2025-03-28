// SPDX-License-Identifier: GPL-2.0
/*
 * AMD AE4DMA driver
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#include "ae4dma.h"

static unsigned int max_hw_q = 1;
module_param(max_hw_q, uint, 0444);
MODULE_PARM_DESC(max_hw_q, "max hw queues supported by engine (any non-zero value, default: 1)");

static void ae4_pending_work(struct work_struct *work)
{
	struct ae4_cmd_queue *ae4cmd_q = container_of(work, struct ae4_cmd_queue, p_work.work);
	struct pt_cmd_queue *cmd_q = &ae4cmd_q->cmd_q;
	struct pt_cmd *cmd;
	u32 cridx;

	for (;;) {
		wait_event_interruptible(ae4cmd_q->q_w,
					 ((atomic64_read(&ae4cmd_q->done_cnt)) <
					   atomic64_read(&ae4cmd_q->intr_cnt)));

		atomic64_inc(&ae4cmd_q->done_cnt);

		mutex_lock(&ae4cmd_q->cmd_lock);
		cridx = readl(cmd_q->reg_control + AE4_RD_IDX_OFF);
		while ((ae4cmd_q->dridx != cridx) && !list_empty(&ae4cmd_q->cmd)) {
			cmd = list_first_entry(&ae4cmd_q->cmd, struct pt_cmd, entry);
			list_del(&cmd->entry);

			ae4_check_status_error(ae4cmd_q, ae4cmd_q->dridx);
			cmd->pt_cmd_callback(cmd->data, cmd->ret);

			ae4cmd_q->q_cmd_count--;
			ae4cmd_q->dridx = (ae4cmd_q->dridx + 1) % CMD_Q_LEN;

			complete_all(&ae4cmd_q->cmp);
		}
		mutex_unlock(&ae4cmd_q->cmd_lock);
	}
}

static irqreturn_t ae4_core_irq_handler(int irq, void *data)
{
	struct ae4_cmd_queue *ae4cmd_q = data;
	struct pt_cmd_queue *cmd_q;
	struct pt_device *pt;
	u32 status;

	cmd_q = &ae4cmd_q->cmd_q;
	pt = cmd_q->pt;

	pt->total_interrupts++;
	atomic64_inc(&ae4cmd_q->intr_cnt);

	status = readl(cmd_q->reg_control + AE4_INTR_STS_OFF);
	if (status & BIT(0)) {
		status &= GENMASK(31, 1);
		writel(status, cmd_q->reg_control + AE4_INTR_STS_OFF);
	}

	wake_up(&ae4cmd_q->q_w);

	return IRQ_HANDLED;
}

void ae4_destroy_work(struct ae4_device *ae4)
{
	struct ae4_cmd_queue *ae4cmd_q;
	int i;

	for (i = 0; i < ae4->cmd_q_count; i++) {
		ae4cmd_q = &ae4->ae4cmd_q[i];

		if (!ae4cmd_q->pws)
			break;

		cancel_delayed_work_sync(&ae4cmd_q->p_work);
		destroy_workqueue(ae4cmd_q->pws);
	}
}

int ae4_core_init(struct ae4_device *ae4)
{
	struct pt_device *pt = &ae4->pt;
	struct ae4_cmd_queue *ae4cmd_q;
	struct device *dev = pt->dev;
	struct pt_cmd_queue *cmd_q;
	int i, ret = 0;

	writel(max_hw_q, pt->io_regs);

	for (i = 0; i < max_hw_q; i++) {
		ae4cmd_q = &ae4->ae4cmd_q[i];
		ae4cmd_q->id = ae4->cmd_q_count;
		ae4->cmd_q_count++;

		cmd_q = &ae4cmd_q->cmd_q;
		cmd_q->pt = pt;

		cmd_q->reg_control = pt->io_regs + ((i + 1) * AE4_Q_SZ);

		ret = devm_request_irq(dev, ae4->ae4_irq[i], ae4_core_irq_handler, 0,
				       dev_name(pt->dev), ae4cmd_q);
		if (ret)
			return ret;

		cmd_q->qsize = Q_SIZE(sizeof(struct ae4dma_desc));

		cmd_q->qbase = dmam_alloc_coherent(dev, cmd_q->qsize, &cmd_q->qbase_dma,
						   GFP_KERNEL);
		if (!cmd_q->qbase)
			return -ENOMEM;
	}

	for (i = 0; i < ae4->cmd_q_count; i++) {
		ae4cmd_q = &ae4->ae4cmd_q[i];

		cmd_q = &ae4cmd_q->cmd_q;

		cmd_q->reg_control = pt->io_regs + ((i + 1) * AE4_Q_SZ);

		/* Update the device registers with queue information. */
		writel(CMD_Q_LEN, cmd_q->reg_control + AE4_MAX_IDX_OFF);

		cmd_q->qdma_tail = cmd_q->qbase_dma;
		writel(lower_32_bits(cmd_q->qdma_tail), cmd_q->reg_control + AE4_Q_BASE_L_OFF);
		writel(upper_32_bits(cmd_q->qdma_tail), cmd_q->reg_control + AE4_Q_BASE_H_OFF);

		INIT_LIST_HEAD(&ae4cmd_q->cmd);
		init_waitqueue_head(&ae4cmd_q->q_w);

		ae4cmd_q->pws = alloc_ordered_workqueue("ae4dma_%d", WQ_MEM_RECLAIM, ae4cmd_q->id);
		if (!ae4cmd_q->pws) {
			ae4_destroy_work(ae4);
			return -ENOMEM;
		}
		INIT_DELAYED_WORK(&ae4cmd_q->p_work, ae4_pending_work);
		queue_delayed_work(ae4cmd_q->pws, &ae4cmd_q->p_work,  usecs_to_jiffies(100));

		init_completion(&ae4cmd_q->cmp);
	}

	ret = pt_dmaengine_register(pt);
	if (ret)
		ae4_destroy_work(ae4);
	else
		ptdma_debugfs_setup(pt);

	return ret;
}
