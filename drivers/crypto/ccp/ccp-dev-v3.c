/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2013,2017 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/ccp.h>

#include "ccp-dev.h"

static u32 ccp_alloc_ksb(struct ccp_cmd_queue *cmd_q, unsigned int count)
{
	int start;
	struct ccp_device *ccp = cmd_q->ccp;

	for (;;) {
		mutex_lock(&ccp->sb_mutex);

		start = (u32)bitmap_find_next_zero_area(ccp->sb,
							ccp->sb_count,
							ccp->sb_start,
							count, 0);
		if (start <= ccp->sb_count) {
			bitmap_set(ccp->sb, start, count);

			mutex_unlock(&ccp->sb_mutex);
			break;
		}

		ccp->sb_avail = 0;

		mutex_unlock(&ccp->sb_mutex);

		/* Wait for KSB entries to become available */
		if (wait_event_interruptible(ccp->sb_queue, ccp->sb_avail))
			return 0;
	}

	return KSB_START + start;
}

static void ccp_free_ksb(struct ccp_cmd_queue *cmd_q, unsigned int start,
			 unsigned int count)
{
	struct ccp_device *ccp = cmd_q->ccp;

	if (!start)
		return;

	mutex_lock(&ccp->sb_mutex);

	bitmap_clear(ccp->sb, start - KSB_START, count);

	ccp->sb_avail = 1;

	mutex_unlock(&ccp->sb_mutex);

	wake_up_interruptible_all(&ccp->sb_queue);
}

static unsigned int ccp_get_free_slots(struct ccp_cmd_queue *cmd_q)
{
	return CMD_Q_DEPTH(ioread32(cmd_q->reg_status));
}

static int ccp_do_cmd(struct ccp_op *op, u32 *cr, unsigned int cr_count)
{
	struct ccp_cmd_queue *cmd_q = op->cmd_q;
	struct ccp_device *ccp = cmd_q->ccp;
	void __iomem *cr_addr;
	u32 cr0, cmd;
	unsigned int i;
	int ret = 0;

	/* We could read a status register to see how many free slots
	 * are actually available, but reading that register resets it
	 * and you could lose some error information.
	 */
	cmd_q->free_slots--;

	cr0 = (cmd_q->id << REQ0_CMD_Q_SHIFT)
	      | (op->jobid << REQ0_JOBID_SHIFT)
	      | REQ0_WAIT_FOR_WRITE;

	if (op->soc)
		cr0 |= REQ0_STOP_ON_COMPLETE
		       | REQ0_INT_ON_COMPLETE;

	if (op->ioc || !cmd_q->free_slots)
		cr0 |= REQ0_INT_ON_COMPLETE;

	/* Start at CMD_REQ1 */
	cr_addr = ccp->io_regs + CMD_REQ0 + CMD_REQ_INCR;

	mutex_lock(&ccp->req_mutex);

	/* Write CMD_REQ1 through CMD_REQx first */
	for (i = 0; i < cr_count; i++, cr_addr += CMD_REQ_INCR)
		iowrite32(*(cr + i), cr_addr);

	/* Tell the CCP to start */
	wmb();
	iowrite32(cr0, ccp->io_regs + CMD_REQ0);

	mutex_unlock(&ccp->req_mutex);

	if (cr0 & REQ0_INT_ON_COMPLETE) {
		/* Wait for the job to complete */
		ret = wait_event_interruptible(cmd_q->int_queue,
					       cmd_q->int_rcvd);
		if (ret || cmd_q->cmd_error) {
			/* On error delete all related jobs from the queue */
			cmd = (cmd_q->id << DEL_Q_ID_SHIFT)
			      | op->jobid;
			if (cmd_q->cmd_error)
				ccp_log_error(cmd_q->ccp,
					      cmd_q->cmd_error);

			iowrite32(cmd, ccp->io_regs + DEL_CMD_Q_JOB);

			if (!ret)
				ret = -EIO;
		} else if (op->soc) {
			/* Delete just head job from the queue on SoC */
			cmd = DEL_Q_ACTIVE
			      | (cmd_q->id << DEL_Q_ID_SHIFT)
			      | op->jobid;

			iowrite32(cmd, ccp->io_regs + DEL_CMD_Q_JOB);
		}

		cmd_q->free_slots = CMD_Q_DEPTH(cmd_q->q_status);

		cmd_q->int_rcvd = 0;
	}

	return ret;
}

static int ccp_perform_aes(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_AES << REQ1_ENGINE_SHIFT)
		| (op->u.aes.type << REQ1_AES_TYPE_SHIFT)
		| (op->u.aes.mode << REQ1_AES_MODE_SHIFT)
		| (op->u.aes.action << REQ1_AES_ACTION_SHIFT)
		| (op->sb_key << REQ1_KEY_KSB_SHIFT);
	cr[1] = op->src.u.dma.length - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (op->sb_ctx << REQ4_KSB_SHIFT)
		| (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);
	cr[4] = ccp_addr_lo(&op->dst.u.dma);
	cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->dst.u.dma);

	if (op->u.aes.mode == CCP_AES_MODE_CFB)
		cr[0] |= ((0x7f) << REQ1_AES_CFB_SIZE_SHIFT);

	if (op->eom)
		cr[0] |= REQ1_EOM;

	if (op->init)
		cr[0] |= REQ1_INIT;

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_xts_aes(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_XTS_AES_128 << REQ1_ENGINE_SHIFT)
		| (op->u.xts.action << REQ1_AES_ACTION_SHIFT)
		| (op->u.xts.unit_size << REQ1_XTS_AES_SIZE_SHIFT)
		| (op->sb_key << REQ1_KEY_KSB_SHIFT);
	cr[1] = op->src.u.dma.length - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (op->sb_ctx << REQ4_KSB_SHIFT)
		| (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);
	cr[4] = ccp_addr_lo(&op->dst.u.dma);
	cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->dst.u.dma);

	if (op->eom)
		cr[0] |= REQ1_EOM;

	if (op->init)
		cr[0] |= REQ1_INIT;

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_sha(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_SHA << REQ1_ENGINE_SHIFT)
		| (op->u.sha.type << REQ1_SHA_TYPE_SHIFT)
		| REQ1_INIT;
	cr[1] = op->src.u.dma.length - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (op->sb_ctx << REQ4_KSB_SHIFT)
		| (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);

	if (op->eom) {
		cr[0] |= REQ1_EOM;
		cr[4] = lower_32_bits(op->u.sha.msg_bits);
		cr[5] = upper_32_bits(op->u.sha.msg_bits);
	} else {
		cr[4] = 0;
		cr[5] = 0;
	}

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_rsa(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_RSA << REQ1_ENGINE_SHIFT)
		| (op->u.rsa.mod_size << REQ1_RSA_MOD_SIZE_SHIFT)
		| (op->sb_key << REQ1_KEY_KSB_SHIFT)
		| REQ1_EOM;
	cr[1] = op->u.rsa.input_len - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (op->sb_ctx << REQ4_KSB_SHIFT)
		| (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);
	cr[4] = ccp_addr_lo(&op->dst.u.dma);
	cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->dst.u.dma);

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_passthru(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = (CCP_ENGINE_PASSTHRU << REQ1_ENGINE_SHIFT)
		| (op->u.passthru.bit_mod << REQ1_PT_BW_SHIFT)
		| (op->u.passthru.byte_swap << REQ1_PT_BS_SHIFT);

	if (op->src.type == CCP_MEMTYPE_SYSTEM)
		cr[1] = op->src.u.dma.length - 1;
	else
		cr[1] = op->dst.u.dma.length - 1;

	if (op->src.type == CCP_MEMTYPE_SYSTEM) {
		cr[2] = ccp_addr_lo(&op->src.u.dma);
		cr[3] = (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
			| ccp_addr_hi(&op->src.u.dma);

		if (op->u.passthru.bit_mod != CCP_PASSTHRU_BITWISE_NOOP)
			cr[3] |= (op->sb_key << REQ4_KSB_SHIFT);
	} else {
		cr[2] = op->src.u.sb * CCP_SB_BYTES;
		cr[3] = (CCP_MEMTYPE_SB << REQ4_MEMTYPE_SHIFT);
	}

	if (op->dst.type == CCP_MEMTYPE_SYSTEM) {
		cr[4] = ccp_addr_lo(&op->dst.u.dma);
		cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
			| ccp_addr_hi(&op->dst.u.dma);
	} else {
		cr[4] = op->dst.u.sb * CCP_SB_BYTES;
		cr[5] = (CCP_MEMTYPE_SB << REQ6_MEMTYPE_SHIFT);
	}

	if (op->eom)
		cr[0] |= REQ1_EOM;

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static int ccp_perform_ecc(struct ccp_op *op)
{
	u32 cr[6];

	/* Fill out the register contents for REQ1 through REQ6 */
	cr[0] = REQ1_ECC_AFFINE_CONVERT
		| (CCP_ENGINE_ECC << REQ1_ENGINE_SHIFT)
		| (op->u.ecc.function << REQ1_ECC_FUNCTION_SHIFT)
		| REQ1_EOM;
	cr[1] = op->src.u.dma.length - 1;
	cr[2] = ccp_addr_lo(&op->src.u.dma);
	cr[3] = (CCP_MEMTYPE_SYSTEM << REQ4_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->src.u.dma);
	cr[4] = ccp_addr_lo(&op->dst.u.dma);
	cr[5] = (CCP_MEMTYPE_SYSTEM << REQ6_MEMTYPE_SHIFT)
		| ccp_addr_hi(&op->dst.u.dma);

	return ccp_do_cmd(op, cr, ARRAY_SIZE(cr));
}

static void ccp_disable_queue_interrupts(struct ccp_device *ccp)
{
	iowrite32(0x00, ccp->io_regs + IRQ_MASK_REG);
}

static void ccp_enable_queue_interrupts(struct ccp_device *ccp)
{
	iowrite32(ccp->qim, ccp->io_regs + IRQ_MASK_REG);
}

static void ccp_irq_bh(unsigned long data)
{
	struct ccp_device *ccp = (struct ccp_device *)data;
	struct ccp_cmd_queue *cmd_q;
	u32 q_int, status;
	unsigned int i;

	status = ioread32(ccp->io_regs + IRQ_STATUS_REG);

	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		q_int = status & (cmd_q->int_ok | cmd_q->int_err);
		if (q_int) {
			cmd_q->int_status = status;
			cmd_q->q_status = ioread32(cmd_q->reg_status);
			cmd_q->q_int_status = ioread32(cmd_q->reg_int_status);

			/* On error, only save the first error value */
			if ((q_int & cmd_q->int_err) && !cmd_q->cmd_error)
				cmd_q->cmd_error = CMD_Q_ERROR(cmd_q->q_status);

			cmd_q->int_rcvd = 1;

			/* Acknowledge the interrupt and wake the kthread */
			iowrite32(q_int, ccp->io_regs + IRQ_STATUS_REG);
			wake_up_interruptible(&cmd_q->int_queue);
		}
	}
	ccp_enable_queue_interrupts(ccp);
}

static irqreturn_t ccp_irq_handler(int irq, void *data)
{
	struct ccp_device *ccp = (struct ccp_device *)data;

	ccp_disable_queue_interrupts(ccp);
	if (ccp->use_tasklet)
		tasklet_schedule(&ccp->irq_tasklet);
	else
		ccp_irq_bh((unsigned long)ccp);

	return IRQ_HANDLED;
}

static int ccp_init(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;
	struct ccp_cmd_queue *cmd_q;
	struct dma_pool *dma_pool;
	char dma_pool_name[MAX_DMAPOOL_NAME_LEN];
	unsigned int qmr, i;
	int ret;

	/* Find available queues */
	ccp->qim = 0;
	qmr = ioread32(ccp->io_regs + Q_MASK_REG);
	for (i = 0; i < MAX_HW_QUEUES; i++) {
		if (!(qmr & (1 << i)))
			continue;

		/* Allocate a dma pool for this queue */
		snprintf(dma_pool_name, sizeof(dma_pool_name), "%s_q%d",
			 ccp->name, i);
		dma_pool = dma_pool_create(dma_pool_name, dev,
					   CCP_DMAPOOL_MAX_SIZE,
					   CCP_DMAPOOL_ALIGN, 0);
		if (!dma_pool) {
			dev_err(dev, "unable to allocate dma pool\n");
			ret = -ENOMEM;
			goto e_pool;
		}

		cmd_q = &ccp->cmd_q[ccp->cmd_q_count];
		ccp->cmd_q_count++;

		cmd_q->ccp = ccp;
		cmd_q->id = i;
		cmd_q->dma_pool = dma_pool;

		/* Reserve 2 KSB regions for the queue */
		cmd_q->sb_key = KSB_START + ccp->sb_start++;
		cmd_q->sb_ctx = KSB_START + ccp->sb_start++;
		ccp->sb_count -= 2;

		/* Preset some register values and masks that are queue
		 * number dependent
		 */
		cmd_q->reg_status = ccp->io_regs + CMD_Q_STATUS_BASE +
				    (CMD_Q_STATUS_INCR * i);
		cmd_q->reg_int_status = ccp->io_regs + CMD_Q_INT_STATUS_BASE +
					(CMD_Q_STATUS_INCR * i);
		cmd_q->int_ok = 1 << (i * 2);
		cmd_q->int_err = 1 << ((i * 2) + 1);

		cmd_q->free_slots = ccp_get_free_slots(cmd_q);

		init_waitqueue_head(&cmd_q->int_queue);

		/* Build queue interrupt mask (two interrupts per queue) */
		ccp->qim |= cmd_q->int_ok | cmd_q->int_err;

#ifdef CONFIG_ARM64
		/* For arm64 set the recommended queue cache settings */
		iowrite32(ccp->axcache, ccp->io_regs + CMD_Q_CACHE_BASE +
			  (CMD_Q_CACHE_INC * i));
#endif

		dev_dbg(dev, "queue #%u available\n", i);
	}
	if (ccp->cmd_q_count == 0) {
		dev_notice(dev, "no command queues available\n");
		ret = -EIO;
		goto e_pool;
	}
	dev_notice(dev, "%u command queues available\n", ccp->cmd_q_count);

	/* Disable and clear interrupts until ready */
	ccp_disable_queue_interrupts(ccp);
	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		ioread32(cmd_q->reg_int_status);
		ioread32(cmd_q->reg_status);
	}
	iowrite32(ccp->qim, ccp->io_regs + IRQ_STATUS_REG);

	/* Request an irq */
	ret = sp_request_ccp_irq(ccp->sp, ccp_irq_handler, ccp->name, ccp);
	if (ret) {
		dev_err(dev, "unable to allocate an IRQ\n");
		goto e_pool;
	}

	/* Initialize the ISR tasklet? */
	if (ccp->use_tasklet)
		tasklet_init(&ccp->irq_tasklet, ccp_irq_bh,
			     (unsigned long)ccp);

	dev_dbg(dev, "Starting threads...\n");
	/* Create a kthread for each queue */
	for (i = 0; i < ccp->cmd_q_count; i++) {
		struct task_struct *kthread;

		cmd_q = &ccp->cmd_q[i];

		kthread = kthread_create(ccp_cmd_queue_thread, cmd_q,
					 "%s-q%u", ccp->name, cmd_q->id);
		if (IS_ERR(kthread)) {
			dev_err(dev, "error creating queue thread (%ld)\n",
				PTR_ERR(kthread));
			ret = PTR_ERR(kthread);
			goto e_kthread;
		}

		cmd_q->kthread = kthread;
		wake_up_process(kthread);
	}

	dev_dbg(dev, "Enabling interrupts...\n");
	/* Enable interrupts */
	ccp_enable_queue_interrupts(ccp);

	dev_dbg(dev, "Registering device...\n");
	ccp_add_device(ccp);

	ret = ccp_register_rng(ccp);
	if (ret)
		goto e_kthread;

	/* Register the DMA engine support */
	ret = ccp_dmaengine_register(ccp);
	if (ret)
		goto e_hwrng;

	return 0;

e_hwrng:
	ccp_unregister_rng(ccp);

e_kthread:
	for (i = 0; i < ccp->cmd_q_count; i++)
		if (ccp->cmd_q[i].kthread)
			kthread_stop(ccp->cmd_q[i].kthread);

	sp_free_ccp_irq(ccp->sp, ccp);

e_pool:
	for (i = 0; i < ccp->cmd_q_count; i++)
		dma_pool_destroy(ccp->cmd_q[i].dma_pool);

	return ret;
}

static void ccp_destroy(struct ccp_device *ccp)
{
	struct ccp_cmd_queue *cmd_q;
	struct ccp_cmd *cmd;
	unsigned int i;

	/* Unregister the DMA engine */
	ccp_dmaengine_unregister(ccp);

	/* Unregister the RNG */
	ccp_unregister_rng(ccp);

	/* Remove this device from the list of available units */
	ccp_del_device(ccp);

	/* Disable and clear interrupts */
	ccp_disable_queue_interrupts(ccp);
	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		ioread32(cmd_q->reg_int_status);
		ioread32(cmd_q->reg_status);
	}
	iowrite32(ccp->qim, ccp->io_regs + IRQ_STATUS_REG);

	/* Stop the queue kthreads */
	for (i = 0; i < ccp->cmd_q_count; i++)
		if (ccp->cmd_q[i].kthread)
			kthread_stop(ccp->cmd_q[i].kthread);

	sp_free_ccp_irq(ccp->sp, ccp);

	for (i = 0; i < ccp->cmd_q_count; i++)
		dma_pool_destroy(ccp->cmd_q[i].dma_pool);

	/* Flush the cmd and backlog queue */
	while (!list_empty(&ccp->cmd)) {
		/* Invoke the callback directly with an error code */
		cmd = list_first_entry(&ccp->cmd, struct ccp_cmd, entry);
		list_del(&cmd->entry);
		cmd->callback(cmd->data, -ENODEV);
	}
	while (!list_empty(&ccp->backlog)) {
		/* Invoke the callback directly with an error code */
		cmd = list_first_entry(&ccp->backlog, struct ccp_cmd, entry);
		list_del(&cmd->entry);
		cmd->callback(cmd->data, -ENODEV);
	}
}

static const struct ccp_actions ccp3_actions = {
	.aes = ccp_perform_aes,
	.xts_aes = ccp_perform_xts_aes,
	.des3 = NULL,
	.sha = ccp_perform_sha,
	.rsa = ccp_perform_rsa,
	.passthru = ccp_perform_passthru,
	.ecc = ccp_perform_ecc,
	.sballoc = ccp_alloc_ksb,
	.sbfree = ccp_free_ksb,
	.init = ccp_init,
	.destroy = ccp_destroy,
	.get_free_slots = ccp_get_free_slots,
	.irqhandler = ccp_irq_handler,
};

const struct ccp_vdata ccpv3_platform = {
	.version = CCP_VERSION(3, 0),
	.setup = NULL,
	.perform = &ccp3_actions,
	.offset = 0,
	.rsamax = CCP_RSA_MAX_WIDTH,
};

const struct ccp_vdata ccpv3 = {
	.version = CCP_VERSION(3, 0),
	.setup = NULL,
	.perform = &ccp3_actions,
	.offset = 0x20000,
	.rsamax = CCP_RSA_MAX_WIDTH,
};
