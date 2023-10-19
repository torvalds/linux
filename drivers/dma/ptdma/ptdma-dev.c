// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Passthru DMA device driver
 * -- Based on the CCP driver
 *
 * Copyright (C) 2016,2021 Advanced Micro Devices, Inc.
 *
 * Author: Sanjay R Mehta <sanju.mehta@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "ptdma.h"

/* Human-readable error strings */
static char *pt_error_codes[] = {
	"",
	"ERR 01: ILLEGAL_ENGINE",
	"ERR 03: ILLEGAL_FUNCTION_TYPE",
	"ERR 04: ILLEGAL_FUNCTION_MODE",
	"ERR 06: ILLEGAL_FUNCTION_SIZE",
	"ERR 08: ILLEGAL_FUNCTION_RSVD",
	"ERR 09: ILLEGAL_BUFFER_LENGTH",
	"ERR 10: VLSB_FAULT",
	"ERR 11: ILLEGAL_MEM_ADDR",
	"ERR 12: ILLEGAL_MEM_SEL",
	"ERR 13: ILLEGAL_CONTEXT_ID",
	"ERR 15: 0xF Reserved",
	"ERR 18: CMD_TIMEOUT",
	"ERR 19: IDMA0_AXI_SLVERR",
	"ERR 20: IDMA0_AXI_DECERR",
	"ERR 21: 0x15 Reserved",
	"ERR 22: IDMA1_AXI_SLAVE_FAULT",
	"ERR 23: IDMA1_AIXI_DECERR",
	"ERR 24: 0x18 Reserved",
	"ERR 27: 0x1B Reserved",
	"ERR 38: ODMA0_AXI_SLVERR",
	"ERR 39: ODMA0_AXI_DECERR",
	"ERR 40: 0x28 Reserved",
	"ERR 41: ODMA1_AXI_SLVERR",
	"ERR 42: ODMA1_AXI_DECERR",
	"ERR 43: LSB_PARITY_ERR",
};

static void pt_log_error(struct pt_device *d, int e)
{
	dev_err(d->dev, "PTDMA error: %s (0x%x)\n", pt_error_codes[e], e);
}

void pt_start_queue(struct pt_cmd_queue *cmd_q)
{
	/* Turn on the run bit */
	iowrite32(cmd_q->qcontrol | CMD_Q_RUN, cmd_q->reg_control);
}

void pt_stop_queue(struct pt_cmd_queue *cmd_q)
{
	/* Turn off the run bit */
	iowrite32(cmd_q->qcontrol & ~CMD_Q_RUN, cmd_q->reg_control);
}

static int pt_core_execute_cmd(struct ptdma_desc *desc, struct pt_cmd_queue *cmd_q)
{
	bool soc = FIELD_GET(DWORD0_SOC, desc->dw0);
	u8 *q_desc = (u8 *)&cmd_q->qbase[cmd_q->qidx];
	u32 tail;
	unsigned long flags;

	if (soc) {
		desc->dw0 |= FIELD_PREP(DWORD0_IOC, desc->dw0);
		desc->dw0 &= ~DWORD0_SOC;
	}
	spin_lock_irqsave(&cmd_q->q_lock, flags);

	/* Copy 32-byte command descriptor to hw queue. */
	memcpy(q_desc, desc, 32);
	cmd_q->qidx = (cmd_q->qidx + 1) % CMD_Q_LEN;

	/* The data used by this command must be flushed to memory */
	wmb();

	/* Write the new tail address back to the queue register */
	tail = lower_32_bits(cmd_q->qdma_tail + cmd_q->qidx * Q_DESC_SIZE);
	iowrite32(tail, cmd_q->reg_control + 0x0004);

	/* Turn the queue back on using our cached control register */
	pt_start_queue(cmd_q);
	spin_unlock_irqrestore(&cmd_q->q_lock, flags);

	return 0;
}

int pt_core_perform_passthru(struct pt_cmd_queue *cmd_q,
			     struct pt_passthru_engine *pt_engine)
{
	struct ptdma_desc desc;
	struct pt_device *pt = container_of(cmd_q, struct pt_device, cmd_q);

	cmd_q->cmd_error = 0;
	cmd_q->total_pt_ops++;
	memset(&desc, 0, sizeof(desc));
	desc.dw0 = CMD_DESC_DW0_VAL;
	desc.length = pt_engine->src_len;
	desc.src_lo = lower_32_bits(pt_engine->src_dma);
	desc.dw3.src_hi = upper_32_bits(pt_engine->src_dma);
	desc.dst_lo = lower_32_bits(pt_engine->dst_dma);
	desc.dw5.dst_hi = upper_32_bits(pt_engine->dst_dma);

	if (cmd_q->int_en)
		pt_core_enable_queue_interrupts(pt);
	else
		pt_core_disable_queue_interrupts(pt);

	return pt_core_execute_cmd(&desc, cmd_q);
}

static void pt_do_cmd_complete(unsigned long data)
{
	struct pt_tasklet_data *tdata = (struct pt_tasklet_data *)data;
	struct pt_cmd *cmd = tdata->cmd;
	struct pt_cmd_queue *cmd_q = &cmd->pt->cmd_q;
	u32 tail;

	if (cmd_q->cmd_error) {
	       /*
		* Log the error and flush the queue by
		* moving the head pointer
		*/
		tail = lower_32_bits(cmd_q->qdma_tail + cmd_q->qidx * Q_DESC_SIZE);
		pt_log_error(cmd_q->pt, cmd_q->cmd_error);
		iowrite32(tail, cmd_q->reg_control + 0x0008);
	}

	cmd->pt_cmd_callback(cmd->data, cmd->ret);
}

void pt_check_status_trans(struct pt_device *pt, struct pt_cmd_queue *cmd_q)
{
	u32 status;

	status = ioread32(cmd_q->reg_control + 0x0010);
	if (status) {
		cmd_q->int_status = status;
		cmd_q->q_status = ioread32(cmd_q->reg_control + 0x0100);
		cmd_q->q_int_status = ioread32(cmd_q->reg_control + 0x0104);

		/* On error, only save the first error value */
		if ((status & INT_ERROR) && !cmd_q->cmd_error)
			cmd_q->cmd_error = CMD_Q_ERROR(cmd_q->q_status);

		/* Acknowledge the completion */
		iowrite32(status, cmd_q->reg_control + 0x0010);
		pt_do_cmd_complete((ulong)&pt->tdata);
	}
}

static irqreturn_t pt_core_irq_handler(int irq, void *data)
{
	struct pt_device *pt = data;
	struct pt_cmd_queue *cmd_q = &pt->cmd_q;

	pt_core_disable_queue_interrupts(pt);
	pt->total_interrupts++;
	pt_check_status_trans(pt, cmd_q);
	pt_core_enable_queue_interrupts(pt);
	return IRQ_HANDLED;
}

int pt_core_init(struct pt_device *pt)
{
	char dma_pool_name[MAX_DMAPOOL_NAME_LEN];
	struct pt_cmd_queue *cmd_q = &pt->cmd_q;
	u32 dma_addr_lo, dma_addr_hi;
	struct device *dev = pt->dev;
	struct dma_pool *dma_pool;
	int ret;

	/* Allocate a dma pool for the queue */
	snprintf(dma_pool_name, sizeof(dma_pool_name), "%s_q", dev_name(pt->dev));

	dma_pool = dma_pool_create(dma_pool_name, dev,
				   PT_DMAPOOL_MAX_SIZE,
				   PT_DMAPOOL_ALIGN, 0);
	if (!dma_pool)
		return -ENOMEM;

	/* ptdma core initialisation */
	iowrite32(CMD_CONFIG_VHB_EN, pt->io_regs + CMD_CONFIG_OFFSET);
	iowrite32(CMD_QUEUE_PRIO, pt->io_regs + CMD_QUEUE_PRIO_OFFSET);
	iowrite32(CMD_TIMEOUT_DISABLE, pt->io_regs + CMD_TIMEOUT_OFFSET);
	iowrite32(CMD_CLK_GATE_CONFIG, pt->io_regs + CMD_CLK_GATE_CTL_OFFSET);
	iowrite32(CMD_CONFIG_REQID, pt->io_regs + CMD_REQID_CONFIG_OFFSET);

	cmd_q->pt = pt;
	cmd_q->dma_pool = dma_pool;
	spin_lock_init(&cmd_q->q_lock);

	/* Page alignment satisfies our needs for N <= 128 */
	cmd_q->qsize = Q_SIZE(Q_DESC_SIZE);
	cmd_q->qbase = dma_alloc_coherent(dev, cmd_q->qsize,
					  &cmd_q->qbase_dma,
					  GFP_KERNEL);
	if (!cmd_q->qbase) {
		dev_err(dev, "unable to allocate command queue\n");
		ret = -ENOMEM;
		goto e_destroy_pool;
	}

	cmd_q->qidx = 0;

	/* Preset some register values */
	cmd_q->reg_control = pt->io_regs + CMD_Q_STATUS_INCR;

	/* Turn off the queues and disable interrupts until ready */
	pt_core_disable_queue_interrupts(pt);

	cmd_q->qcontrol = 0; /* Start with nothing */
	iowrite32(cmd_q->qcontrol, cmd_q->reg_control);

	ioread32(cmd_q->reg_control + 0x0104);
	ioread32(cmd_q->reg_control + 0x0100);

	/* Clear the interrupt status */
	iowrite32(SUPPORTED_INTERRUPTS, cmd_q->reg_control + 0x0010);

	/* Request an irq */
	ret = request_irq(pt->pt_irq, pt_core_irq_handler, 0, dev_name(pt->dev), pt);
	if (ret) {
		dev_err(dev, "unable to allocate an IRQ\n");
		goto e_free_dma;
	}

	/* Update the device registers with queue information. */
	cmd_q->qcontrol &= ~CMD_Q_SIZE;
	cmd_q->qcontrol |= FIELD_PREP(CMD_Q_SIZE, QUEUE_SIZE_VAL);

	cmd_q->qdma_tail = cmd_q->qbase_dma;
	dma_addr_lo = lower_32_bits(cmd_q->qdma_tail);
	iowrite32((u32)dma_addr_lo, cmd_q->reg_control + 0x0004);
	iowrite32((u32)dma_addr_lo, cmd_q->reg_control + 0x0008);

	dma_addr_hi = upper_32_bits(cmd_q->qdma_tail);
	cmd_q->qcontrol |= (dma_addr_hi << 16);
	iowrite32(cmd_q->qcontrol, cmd_q->reg_control);

	pt_core_enable_queue_interrupts(pt);

	/* Register the DMA engine support */
	ret = pt_dmaengine_register(pt);
	if (ret)
		goto e_free_irq;

	/* Set up debugfs entries */
	ptdma_debugfs_setup(pt);

	return 0;

e_free_irq:
	free_irq(pt->pt_irq, pt);

e_free_dma:
	dma_free_coherent(dev, cmd_q->qsize, cmd_q->qbase, cmd_q->qbase_dma);

e_destroy_pool:
	dma_pool_destroy(pt->cmd_q.dma_pool);

	return ret;
}

void pt_core_destroy(struct pt_device *pt)
{
	struct device *dev = pt->dev;
	struct pt_cmd_queue *cmd_q = &pt->cmd_q;
	struct pt_cmd *cmd;

	/* Unregister the DMA engine */
	pt_dmaengine_unregister(pt);

	/* Disable and clear interrupts */
	pt_core_disable_queue_interrupts(pt);

	/* Turn off the run bit */
	pt_stop_queue(cmd_q);

	/* Clear the interrupt status */
	iowrite32(SUPPORTED_INTERRUPTS, cmd_q->reg_control + 0x0010);
	ioread32(cmd_q->reg_control + 0x0104);
	ioread32(cmd_q->reg_control + 0x0100);

	free_irq(pt->pt_irq, pt);

	dma_free_coherent(dev, cmd_q->qsize, cmd_q->qbase,
			  cmd_q->qbase_dma);

	/* Flush the cmd queue */
	while (!list_empty(&pt->cmd)) {
		/* Invoke the callback directly with an error code */
		cmd = list_first_entry(&pt->cmd, struct pt_cmd, entry);
		list_del(&cmd->entry);
		cmd->pt_cmd_callback(cmd->data, -ENODEV);
	}
}
