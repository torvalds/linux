// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "efa_com.h"
#include "efa_regs_defs.h"

#define ADMIN_CMD_TIMEOUT_US 30000000 /* usecs */

#define EFA_REG_READ_TIMEOUT_US 50000 /* usecs */
#define EFA_MMIO_READ_INVALID 0xffffffff

#define EFA_POLL_INTERVAL_MS 100 /* msecs */

#define EFA_ASYNC_QUEUE_DEPTH 16
#define EFA_ADMIN_QUEUE_DEPTH 32

#define EFA_CTRL_MAJOR          0
#define EFA_CTRL_MINOR          0
#define EFA_CTRL_SUB_MINOR      1

enum efa_cmd_status {
	EFA_CMD_SUBMITTED,
	EFA_CMD_COMPLETED,
};

struct efa_comp_ctx {
	struct completion wait_event;
	struct efa_admin_acq_entry *user_cqe;
	u32 comp_size;
	enum efa_cmd_status status;
	u8 cmd_opcode;
	u8 occupied;
};

static const char *efa_com_cmd_str(u8 cmd)
{
#define EFA_CMD_STR_CASE(_cmd) case EFA_ADMIN_##_cmd: return #_cmd

	switch (cmd) {
	EFA_CMD_STR_CASE(CREATE_QP);
	EFA_CMD_STR_CASE(MODIFY_QP);
	EFA_CMD_STR_CASE(QUERY_QP);
	EFA_CMD_STR_CASE(DESTROY_QP);
	EFA_CMD_STR_CASE(CREATE_AH);
	EFA_CMD_STR_CASE(DESTROY_AH);
	EFA_CMD_STR_CASE(REG_MR);
	EFA_CMD_STR_CASE(DEREG_MR);
	EFA_CMD_STR_CASE(CREATE_CQ);
	EFA_CMD_STR_CASE(DESTROY_CQ);
	EFA_CMD_STR_CASE(GET_FEATURE);
	EFA_CMD_STR_CASE(SET_FEATURE);
	EFA_CMD_STR_CASE(GET_STATS);
	EFA_CMD_STR_CASE(ALLOC_PD);
	EFA_CMD_STR_CASE(DEALLOC_PD);
	EFA_CMD_STR_CASE(ALLOC_UAR);
	EFA_CMD_STR_CASE(DEALLOC_UAR);
	default: return "unknown command opcode";
	}
#undef EFA_CMD_STR_CASE
}

static u32 efa_com_reg_read32(struct efa_com_dev *edev, u16 offset)
{
	struct efa_com_mmio_read *mmio_read = &edev->mmio_read;
	struct efa_admin_mmio_req_read_less_resp *read_resp;
	unsigned long exp_time;
	u32 mmio_read_reg = 0;
	u32 err;

	read_resp = mmio_read->read_resp;

	spin_lock(&mmio_read->lock);
	mmio_read->seq_num++;

	/* trash DMA req_id to identify when hardware is done */
	read_resp->req_id = mmio_read->seq_num + 0x9aL;
	EFA_SET(&mmio_read_reg, EFA_REGS_MMIO_REG_READ_REG_OFF, offset);
	EFA_SET(&mmio_read_reg, EFA_REGS_MMIO_REG_READ_REQ_ID,
		mmio_read->seq_num);

	writel(mmio_read_reg, edev->reg_bar + EFA_REGS_MMIO_REG_READ_OFF);

	exp_time = jiffies + usecs_to_jiffies(mmio_read->mmio_read_timeout);
	do {
		if (READ_ONCE(read_resp->req_id) == mmio_read->seq_num)
			break;
		udelay(1);
	} while (time_is_after_jiffies(exp_time));

	if (read_resp->req_id != mmio_read->seq_num) {
		ibdev_err_ratelimited(
			edev->efa_dev,
			"Reading register timed out. expected: req id[%u] offset[%#x] actual: req id[%u] offset[%#x]\n",
			mmio_read->seq_num, offset, read_resp->req_id,
			read_resp->reg_off);
		err = EFA_MMIO_READ_INVALID;
		goto out;
	}

	if (read_resp->reg_off != offset) {
		ibdev_err_ratelimited(
			edev->efa_dev,
			"Reading register failed: wrong offset provided\n");
		err = EFA_MMIO_READ_INVALID;
		goto out;
	}

	err = read_resp->reg_val;
out:
	spin_unlock(&mmio_read->lock);
	return err;
}

static int efa_com_admin_init_sq(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_com_admin_sq *sq = &aq->sq;
	u16 size = aq->depth * sizeof(*sq->entries);
	u32 aq_caps = 0;
	u32 addr_high;
	u32 addr_low;

	sq->entries =
		dma_alloc_coherent(aq->dmadev, size, &sq->dma_addr, GFP_KERNEL);
	if (!sq->entries)
		return -ENOMEM;

	spin_lock_init(&sq->lock);

	sq->cc = 0;
	sq->pc = 0;
	sq->phase = 1;

	sq->db_addr = (u32 __iomem *)(edev->reg_bar + EFA_REGS_AQ_PROD_DB_OFF);

	addr_high = upper_32_bits(sq->dma_addr);
	addr_low = lower_32_bits(sq->dma_addr);

	writel(addr_low, edev->reg_bar + EFA_REGS_AQ_BASE_LO_OFF);
	writel(addr_high, edev->reg_bar + EFA_REGS_AQ_BASE_HI_OFF);

	EFA_SET(&aq_caps, EFA_REGS_AQ_CAPS_AQ_DEPTH, aq->depth);
	EFA_SET(&aq_caps, EFA_REGS_AQ_CAPS_AQ_ENTRY_SIZE,
		sizeof(struct efa_admin_aq_entry));

	writel(aq_caps, edev->reg_bar + EFA_REGS_AQ_CAPS_OFF);

	return 0;
}

static int efa_com_admin_init_cq(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_com_admin_cq *cq = &aq->cq;
	u16 size = aq->depth * sizeof(*cq->entries);
	u32 acq_caps = 0;
	u32 addr_high;
	u32 addr_low;

	cq->entries =
		dma_alloc_coherent(aq->dmadev, size, &cq->dma_addr, GFP_KERNEL);
	if (!cq->entries)
		return -ENOMEM;

	spin_lock_init(&cq->lock);

	cq->cc = 0;
	cq->phase = 1;

	addr_high = upper_32_bits(cq->dma_addr);
	addr_low = lower_32_bits(cq->dma_addr);

	writel(addr_low, edev->reg_bar + EFA_REGS_ACQ_BASE_LO_OFF);
	writel(addr_high, edev->reg_bar + EFA_REGS_ACQ_BASE_HI_OFF);

	EFA_SET(&acq_caps, EFA_REGS_ACQ_CAPS_ACQ_DEPTH, aq->depth);
	EFA_SET(&acq_caps, EFA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE,
		sizeof(struct efa_admin_acq_entry));
	EFA_SET(&acq_caps, EFA_REGS_ACQ_CAPS_ACQ_MSIX_VECTOR,
		aq->msix_vector_idx);

	writel(acq_caps, edev->reg_bar + EFA_REGS_ACQ_CAPS_OFF);

	return 0;
}

static int efa_com_admin_init_aenq(struct efa_com_dev *edev,
				   struct efa_aenq_handlers *aenq_handlers)
{
	struct efa_com_aenq *aenq = &edev->aenq;
	u32 addr_low, addr_high;
	u32 aenq_caps = 0;
	u16 size;

	if (!aenq_handlers) {
		ibdev_err(edev->efa_dev, "aenq handlers pointer is NULL\n");
		return -EINVAL;
	}

	size = EFA_ASYNC_QUEUE_DEPTH * sizeof(*aenq->entries);
	aenq->entries = dma_alloc_coherent(edev->dmadev, size, &aenq->dma_addr,
					   GFP_KERNEL);
	if (!aenq->entries)
		return -ENOMEM;

	aenq->aenq_handlers = aenq_handlers;
	aenq->depth = EFA_ASYNC_QUEUE_DEPTH;
	aenq->cc = 0;
	aenq->phase = 1;

	addr_low = lower_32_bits(aenq->dma_addr);
	addr_high = upper_32_bits(aenq->dma_addr);

	writel(addr_low, edev->reg_bar + EFA_REGS_AENQ_BASE_LO_OFF);
	writel(addr_high, edev->reg_bar + EFA_REGS_AENQ_BASE_HI_OFF);

	EFA_SET(&aenq_caps, EFA_REGS_AENQ_CAPS_AENQ_DEPTH, aenq->depth);
	EFA_SET(&aenq_caps, EFA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE,
		sizeof(struct efa_admin_aenq_entry));
	EFA_SET(&aenq_caps, EFA_REGS_AENQ_CAPS_AENQ_MSIX_VECTOR,
		aenq->msix_vector_idx);
	writel(aenq_caps, edev->reg_bar + EFA_REGS_AENQ_CAPS_OFF);

	/*
	 * Init cons_db to mark that all entries in the queue
	 * are initially available
	 */
	writel(edev->aenq.cc, edev->reg_bar + EFA_REGS_AENQ_CONS_DB_OFF);

	return 0;
}

/* ID to be used with efa_com_get_comp_ctx */
static u16 efa_com_alloc_ctx_id(struct efa_com_admin_queue *aq)
{
	u16 ctx_id;

	spin_lock(&aq->comp_ctx_lock);
	ctx_id = aq->comp_ctx_pool[aq->comp_ctx_pool_next];
	aq->comp_ctx_pool_next++;
	spin_unlock(&aq->comp_ctx_lock);

	return ctx_id;
}

static void efa_com_dealloc_ctx_id(struct efa_com_admin_queue *aq,
				   u16 ctx_id)
{
	spin_lock(&aq->comp_ctx_lock);
	aq->comp_ctx_pool_next--;
	aq->comp_ctx_pool[aq->comp_ctx_pool_next] = ctx_id;
	spin_unlock(&aq->comp_ctx_lock);
}

static inline void efa_com_put_comp_ctx(struct efa_com_admin_queue *aq,
					struct efa_comp_ctx *comp_ctx)
{
	u16 cmd_id = EFA_GET(&comp_ctx->user_cqe->acq_common_descriptor.command,
			     EFA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID);
	u16 ctx_id = cmd_id & (aq->depth - 1);

	ibdev_dbg(aq->efa_dev, "Put completion command_id %#x\n", cmd_id);
	comp_ctx->occupied = 0;
	efa_com_dealloc_ctx_id(aq, ctx_id);
}

static struct efa_comp_ctx *efa_com_get_comp_ctx(struct efa_com_admin_queue *aq,
						 u16 cmd_id, bool capture)
{
	u16 ctx_id = cmd_id & (aq->depth - 1);

	if (aq->comp_ctx[ctx_id].occupied && capture) {
		ibdev_err_ratelimited(
			aq->efa_dev,
			"Completion context for command_id %#x is occupied\n",
			cmd_id);
		return NULL;
	}

	if (capture) {
		aq->comp_ctx[ctx_id].occupied = 1;
		ibdev_dbg(aq->efa_dev,
			  "Take completion ctxt for command_id %#x\n", cmd_id);
	}

	return &aq->comp_ctx[ctx_id];
}

static struct efa_comp_ctx *__efa_com_submit_admin_cmd(struct efa_com_admin_queue *aq,
						       struct efa_admin_aq_entry *cmd,
						       size_t cmd_size_in_bytes,
						       struct efa_admin_acq_entry *comp,
						       size_t comp_size_in_bytes)
{
	struct efa_admin_aq_entry *aqe;
	struct efa_comp_ctx *comp_ctx;
	u16 queue_size_mask;
	u16 cmd_id;
	u16 ctx_id;
	u16 pi;

	queue_size_mask = aq->depth - 1;
	pi = aq->sq.pc & queue_size_mask;

	ctx_id = efa_com_alloc_ctx_id(aq);

	/* cmd_id LSBs are the ctx_id and MSBs are entropy bits from pc */
	cmd_id = ctx_id & queue_size_mask;
	cmd_id |= aq->sq.pc & ~queue_size_mask;
	cmd_id &= EFA_ADMIN_AQ_COMMON_DESC_COMMAND_ID_MASK;

	cmd->aq_common_descriptor.command_id = cmd_id;
	EFA_SET(&cmd->aq_common_descriptor.flags,
		EFA_ADMIN_AQ_COMMON_DESC_PHASE, aq->sq.phase);

	comp_ctx = efa_com_get_comp_ctx(aq, cmd_id, true);
	if (!comp_ctx) {
		efa_com_dealloc_ctx_id(aq, ctx_id);
		return ERR_PTR(-EINVAL);
	}

	comp_ctx->status = EFA_CMD_SUBMITTED;
	comp_ctx->comp_size = comp_size_in_bytes;
	comp_ctx->user_cqe = comp;
	comp_ctx->cmd_opcode = cmd->aq_common_descriptor.opcode;

	reinit_completion(&comp_ctx->wait_event);

	aqe = &aq->sq.entries[pi];
	memset(aqe, 0, sizeof(*aqe));
	memcpy(aqe, cmd, cmd_size_in_bytes);

	aq->sq.pc++;
	atomic64_inc(&aq->stats.submitted_cmd);

	if ((aq->sq.pc & queue_size_mask) == 0)
		aq->sq.phase = !aq->sq.phase;

	/* barrier not needed in case of writel */
	writel(aq->sq.pc, aq->sq.db_addr);

	return comp_ctx;
}

static inline int efa_com_init_comp_ctxt(struct efa_com_admin_queue *aq)
{
	size_t pool_size = aq->depth * sizeof(*aq->comp_ctx_pool);
	size_t size = aq->depth * sizeof(struct efa_comp_ctx);
	struct efa_comp_ctx *comp_ctx;
	u16 i;

	aq->comp_ctx = devm_kzalloc(aq->dmadev, size, GFP_KERNEL);
	aq->comp_ctx_pool = devm_kzalloc(aq->dmadev, pool_size, GFP_KERNEL);
	if (!aq->comp_ctx || !aq->comp_ctx_pool) {
		devm_kfree(aq->dmadev, aq->comp_ctx_pool);
		devm_kfree(aq->dmadev, aq->comp_ctx);
		return -ENOMEM;
	}

	for (i = 0; i < aq->depth; i++) {
		comp_ctx = efa_com_get_comp_ctx(aq, i, false);
		if (comp_ctx)
			init_completion(&comp_ctx->wait_event);

		aq->comp_ctx_pool[i] = i;
	}

	spin_lock_init(&aq->comp_ctx_lock);

	aq->comp_ctx_pool_next = 0;

	return 0;
}

static struct efa_comp_ctx *efa_com_submit_admin_cmd(struct efa_com_admin_queue *aq,
						     struct efa_admin_aq_entry *cmd,
						     size_t cmd_size_in_bytes,
						     struct efa_admin_acq_entry *comp,
						     size_t comp_size_in_bytes)
{
	struct efa_comp_ctx *comp_ctx;

	spin_lock(&aq->sq.lock);
	if (!test_bit(EFA_AQ_STATE_RUNNING_BIT, &aq->state)) {
		ibdev_err_ratelimited(aq->efa_dev, "Admin queue is closed\n");
		spin_unlock(&aq->sq.lock);
		return ERR_PTR(-ENODEV);
	}

	comp_ctx = __efa_com_submit_admin_cmd(aq, cmd, cmd_size_in_bytes, comp,
					      comp_size_in_bytes);
	spin_unlock(&aq->sq.lock);
	if (IS_ERR(comp_ctx))
		clear_bit(EFA_AQ_STATE_RUNNING_BIT, &aq->state);

	return comp_ctx;
}

static void efa_com_handle_single_admin_completion(struct efa_com_admin_queue *aq,
						   struct efa_admin_acq_entry *cqe)
{
	struct efa_comp_ctx *comp_ctx;
	u16 cmd_id;

	cmd_id = EFA_GET(&cqe->acq_common_descriptor.command,
			 EFA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID);

	comp_ctx = efa_com_get_comp_ctx(aq, cmd_id, false);
	if (!comp_ctx) {
		ibdev_err(aq->efa_dev,
			  "comp_ctx is NULL. Changing the admin queue running state\n");
		clear_bit(EFA_AQ_STATE_RUNNING_BIT, &aq->state);
		return;
	}

	comp_ctx->status = EFA_CMD_COMPLETED;
	memcpy(comp_ctx->user_cqe, cqe, comp_ctx->comp_size);

	if (!test_bit(EFA_AQ_STATE_POLLING_BIT, &aq->state))
		complete(&comp_ctx->wait_event);
}

static void efa_com_handle_admin_completion(struct efa_com_admin_queue *aq)
{
	struct efa_admin_acq_entry *cqe;
	u16 queue_size_mask;
	u16 comp_num = 0;
	u8 phase;
	u16 ci;

	queue_size_mask = aq->depth - 1;

	ci = aq->cq.cc & queue_size_mask;
	phase = aq->cq.phase;

	cqe = &aq->cq.entries[ci];

	/* Go over all the completions */
	while ((READ_ONCE(cqe->acq_common_descriptor.flags) &
		EFA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK) == phase) {
		/*
		 * Do not read the rest of the completion entry before the
		 * phase bit was validated
		 */
		dma_rmb();
		efa_com_handle_single_admin_completion(aq, cqe);

		ci++;
		comp_num++;
		if (ci == aq->depth) {
			ci = 0;
			phase = !phase;
		}

		cqe = &aq->cq.entries[ci];
	}

	aq->cq.cc += comp_num;
	aq->cq.phase = phase;
	aq->sq.cc += comp_num;
	atomic64_add(comp_num, &aq->stats.completed_cmd);
}

static int efa_com_comp_status_to_errno(u8 comp_status)
{
	switch (comp_status) {
	case EFA_ADMIN_SUCCESS:
		return 0;
	case EFA_ADMIN_RESOURCE_ALLOCATION_FAILURE:
		return -ENOMEM;
	case EFA_ADMIN_UNSUPPORTED_OPCODE:
		return -EOPNOTSUPP;
	case EFA_ADMIN_BAD_OPCODE:
	case EFA_ADMIN_MALFORMED_REQUEST:
	case EFA_ADMIN_ILLEGAL_PARAMETER:
	case EFA_ADMIN_UNKNOWN_ERROR:
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int efa_com_wait_and_process_admin_cq_polling(struct efa_comp_ctx *comp_ctx,
						     struct efa_com_admin_queue *aq)
{
	unsigned long timeout;
	unsigned long flags;
	int err;

	timeout = jiffies + usecs_to_jiffies(aq->completion_timeout);

	while (1) {
		spin_lock_irqsave(&aq->cq.lock, flags);
		efa_com_handle_admin_completion(aq);
		spin_unlock_irqrestore(&aq->cq.lock, flags);

		if (comp_ctx->status != EFA_CMD_SUBMITTED)
			break;

		if (time_is_before_jiffies(timeout)) {
			ibdev_err_ratelimited(
				aq->efa_dev,
				"Wait for completion (polling) timeout\n");
			/* EFA didn't have any completion */
			atomic64_inc(&aq->stats.no_completion);

			clear_bit(EFA_AQ_STATE_RUNNING_BIT, &aq->state);
			err = -ETIME;
			goto out;
		}

		msleep(aq->poll_interval);
	}

	err = efa_com_comp_status_to_errno(comp_ctx->user_cqe->acq_common_descriptor.status);
out:
	efa_com_put_comp_ctx(aq, comp_ctx);
	return err;
}

static int efa_com_wait_and_process_admin_cq_interrupts(struct efa_comp_ctx *comp_ctx,
							struct efa_com_admin_queue *aq)
{
	unsigned long flags;
	int err;

	wait_for_completion_timeout(&comp_ctx->wait_event,
				    usecs_to_jiffies(aq->completion_timeout));

	/*
	 * In case the command wasn't completed find out the root cause.
	 * There might be 2 kinds of errors
	 * 1) No completion (timeout reached)
	 * 2) There is completion but the device didn't get any msi-x interrupt.
	 */
	if (comp_ctx->status == EFA_CMD_SUBMITTED) {
		spin_lock_irqsave(&aq->cq.lock, flags);
		efa_com_handle_admin_completion(aq);
		spin_unlock_irqrestore(&aq->cq.lock, flags);

		atomic64_inc(&aq->stats.no_completion);

		if (comp_ctx->status == EFA_CMD_COMPLETED)
			ibdev_err_ratelimited(
				aq->efa_dev,
				"The device sent a completion but the driver didn't receive any MSI-X interrupt for admin cmd %s(%d) status %d (ctx: 0x%p, sq producer: %d, sq consumer: %d, cq consumer: %d)\n",
				efa_com_cmd_str(comp_ctx->cmd_opcode),
				comp_ctx->cmd_opcode, comp_ctx->status,
				comp_ctx, aq->sq.pc, aq->sq.cc, aq->cq.cc);
		else
			ibdev_err_ratelimited(
				aq->efa_dev,
				"The device didn't send any completion for admin cmd %s(%d) status %d (ctx 0x%p, sq producer: %d, sq consumer: %d, cq consumer: %d)\n",
				efa_com_cmd_str(comp_ctx->cmd_opcode),
				comp_ctx->cmd_opcode, comp_ctx->status,
				comp_ctx, aq->sq.pc, aq->sq.cc, aq->cq.cc);

		clear_bit(EFA_AQ_STATE_RUNNING_BIT, &aq->state);
		err = -ETIME;
		goto out;
	}

	err = efa_com_comp_status_to_errno(comp_ctx->user_cqe->acq_common_descriptor.status);
out:
	efa_com_put_comp_ctx(aq, comp_ctx);
	return err;
}

/*
 * There are two types to wait for completion.
 * Polling mode - wait until the completion is available.
 * Async mode - wait on wait queue until the completion is ready
 * (or the timeout expired).
 * It is expected that the IRQ called efa_com_handle_admin_completion
 * to mark the completions.
 */
static int efa_com_wait_and_process_admin_cq(struct efa_comp_ctx *comp_ctx,
					     struct efa_com_admin_queue *aq)
{
	if (test_bit(EFA_AQ_STATE_POLLING_BIT, &aq->state))
		return efa_com_wait_and_process_admin_cq_polling(comp_ctx, aq);

	return efa_com_wait_and_process_admin_cq_interrupts(comp_ctx, aq);
}

/**
 * efa_com_cmd_exec - Execute admin command
 * @aq: admin queue.
 * @cmd: the admin command to execute.
 * @cmd_size: the command size.
 * @comp: command completion return entry.
 * @comp_size: command completion size.
 * Submit an admin command and then wait until the device will return a
 * completion.
 * The completion will be copied into comp.
 *
 * @return - 0 on success, negative value on failure.
 */
int efa_com_cmd_exec(struct efa_com_admin_queue *aq,
		     struct efa_admin_aq_entry *cmd,
		     size_t cmd_size,
		     struct efa_admin_acq_entry *comp,
		     size_t comp_size)
{
	struct efa_comp_ctx *comp_ctx;
	int err;

	might_sleep();

	/* In case of queue FULL */
	down(&aq->avail_cmds);

	ibdev_dbg(aq->efa_dev, "%s (opcode %d)\n",
		  efa_com_cmd_str(cmd->aq_common_descriptor.opcode),
		  cmd->aq_common_descriptor.opcode);
	comp_ctx = efa_com_submit_admin_cmd(aq, cmd, cmd_size, comp, comp_size);
	if (IS_ERR(comp_ctx)) {
		ibdev_err_ratelimited(
			aq->efa_dev,
			"Failed to submit command %s (opcode %u) err %ld\n",
			efa_com_cmd_str(cmd->aq_common_descriptor.opcode),
			cmd->aq_common_descriptor.opcode, PTR_ERR(comp_ctx));

		up(&aq->avail_cmds);
		atomic64_inc(&aq->stats.cmd_err);
		return PTR_ERR(comp_ctx);
	}

	err = efa_com_wait_and_process_admin_cq(comp_ctx, aq);
	if (err) {
		ibdev_err_ratelimited(
			aq->efa_dev,
			"Failed to process command %s (opcode %u) comp_status %d err %d\n",
			efa_com_cmd_str(cmd->aq_common_descriptor.opcode),
			cmd->aq_common_descriptor.opcode,
			comp_ctx->user_cqe->acq_common_descriptor.status, err);
		atomic64_inc(&aq->stats.cmd_err);
	}

	up(&aq->avail_cmds);

	return err;
}

/**
 * efa_com_admin_destroy - Destroy the admin and the async events queues.
 * @edev: EFA communication layer struct
 */
void efa_com_admin_destroy(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_com_aenq *aenq = &edev->aenq;
	struct efa_com_admin_cq *cq = &aq->cq;
	struct efa_com_admin_sq *sq = &aq->sq;
	u16 size;

	clear_bit(EFA_AQ_STATE_RUNNING_BIT, &aq->state);

	devm_kfree(edev->dmadev, aq->comp_ctx_pool);
	devm_kfree(edev->dmadev, aq->comp_ctx);

	size = aq->depth * sizeof(*sq->entries);
	dma_free_coherent(edev->dmadev, size, sq->entries, sq->dma_addr);

	size = aq->depth * sizeof(*cq->entries);
	dma_free_coherent(edev->dmadev, size, cq->entries, cq->dma_addr);

	size = aenq->depth * sizeof(*aenq->entries);
	dma_free_coherent(edev->dmadev, size, aenq->entries, aenq->dma_addr);
}

/**
 * efa_com_set_admin_polling_mode - Set the admin completion queue polling mode
 * @edev: EFA communication layer struct
 * @polling: Enable/Disable polling mode
 *
 * Set the admin completion mode.
 */
void efa_com_set_admin_polling_mode(struct efa_com_dev *edev, bool polling)
{
	u32 mask_value = 0;

	if (polling)
		EFA_SET(&mask_value, EFA_REGS_INTR_MASK_EN, 1);

	writel(mask_value, edev->reg_bar + EFA_REGS_INTR_MASK_OFF);
	if (polling)
		set_bit(EFA_AQ_STATE_POLLING_BIT, &edev->aq.state);
	else
		clear_bit(EFA_AQ_STATE_POLLING_BIT, &edev->aq.state);
}

static void efa_com_stats_init(struct efa_com_dev *edev)
{
	atomic64_t *s = (atomic64_t *)&edev->aq.stats;
	int i;

	for (i = 0; i < sizeof(edev->aq.stats) / sizeof(*s); i++, s++)
		atomic64_set(s, 0);
}

/**
 * efa_com_admin_init - Init the admin and the async queues
 * @edev: EFA communication layer struct
 * @aenq_handlers: Those handlers to be called upon event.
 *
 * Initialize the admin submission and completion queues.
 * Initialize the asynchronous events notification queues.
 *
 * @return - 0 on success, negative value on failure.
 */
int efa_com_admin_init(struct efa_com_dev *edev,
		       struct efa_aenq_handlers *aenq_handlers)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	u32 timeout;
	u32 dev_sts;
	u32 cap;
	int err;

	dev_sts = efa_com_reg_read32(edev, EFA_REGS_DEV_STS_OFF);
	if (!EFA_GET(&dev_sts, EFA_REGS_DEV_STS_READY)) {
		ibdev_err(edev->efa_dev,
			  "Device isn't ready, abort com init %#x\n", dev_sts);
		return -ENODEV;
	}

	aq->depth = EFA_ADMIN_QUEUE_DEPTH;

	aq->dmadev = edev->dmadev;
	aq->efa_dev = edev->efa_dev;
	set_bit(EFA_AQ_STATE_POLLING_BIT, &aq->state);

	sema_init(&aq->avail_cmds, aq->depth);

	efa_com_stats_init(edev);

	err = efa_com_init_comp_ctxt(aq);
	if (err)
		return err;

	err = efa_com_admin_init_sq(edev);
	if (err)
		goto err_destroy_comp_ctxt;

	err = efa_com_admin_init_cq(edev);
	if (err)
		goto err_destroy_sq;

	efa_com_set_admin_polling_mode(edev, false);

	err = efa_com_admin_init_aenq(edev, aenq_handlers);
	if (err)
		goto err_destroy_cq;

	cap = efa_com_reg_read32(edev, EFA_REGS_CAPS_OFF);
	timeout = EFA_GET(&cap, EFA_REGS_CAPS_ADMIN_CMD_TO);
	if (timeout)
		/* the resolution of timeout reg is 100ms */
		aq->completion_timeout = timeout * 100000;
	else
		aq->completion_timeout = ADMIN_CMD_TIMEOUT_US;

	aq->poll_interval = EFA_POLL_INTERVAL_MS;

	set_bit(EFA_AQ_STATE_RUNNING_BIT, &aq->state);

	return 0;

err_destroy_cq:
	dma_free_coherent(edev->dmadev, aq->depth * sizeof(*aq->cq.entries),
			  aq->cq.entries, aq->cq.dma_addr);
err_destroy_sq:
	dma_free_coherent(edev->dmadev, aq->depth * sizeof(*aq->sq.entries),
			  aq->sq.entries, aq->sq.dma_addr);
err_destroy_comp_ctxt:
	devm_kfree(edev->dmadev, aq->comp_ctx);

	return err;
}

/**
 * efa_com_admin_q_comp_intr_handler - admin queue interrupt handler
 * @edev: EFA communication layer struct
 *
 * This method goes over the admin completion queue and wakes up
 * all the pending threads that wait on the commands wait event.
 *
 * Note: Should be called after MSI-X interrupt.
 */
void efa_com_admin_q_comp_intr_handler(struct efa_com_dev *edev)
{
	unsigned long flags;

	spin_lock_irqsave(&edev->aq.cq.lock, flags);
	efa_com_handle_admin_completion(&edev->aq);
	spin_unlock_irqrestore(&edev->aq.cq.lock, flags);
}

/*
 * efa_handle_specific_aenq_event:
 * return the handler that is relevant to the specific event group
 */
static efa_aenq_handler efa_com_get_specific_aenq_cb(struct efa_com_dev *edev,
						     u16 group)
{
	struct efa_aenq_handlers *aenq_handlers = edev->aenq.aenq_handlers;

	if (group < EFA_MAX_HANDLERS && aenq_handlers->handlers[group])
		return aenq_handlers->handlers[group];

	return aenq_handlers->unimplemented_handler;
}

/**
 * efa_com_aenq_intr_handler - AENQ interrupt handler
 * @edev: EFA communication layer struct
 * @data: Data of interrupt handler.
 *
 * Go over the async event notification queue and call the proper aenq handler.
 */
void efa_com_aenq_intr_handler(struct efa_com_dev *edev, void *data)
{
	struct efa_admin_aenq_common_desc *aenq_common;
	struct efa_com_aenq *aenq = &edev->aenq;
	struct efa_admin_aenq_entry *aenq_e;
	efa_aenq_handler handler_cb;
	u32 processed = 0;
	u8 phase;
	u32 ci;

	ci = aenq->cc & (aenq->depth - 1);
	phase = aenq->phase;
	aenq_e = &aenq->entries[ci]; /* Get first entry */
	aenq_common = &aenq_e->aenq_common_desc;

	/* Go over all the events */
	while ((READ_ONCE(aenq_common->flags) &
		EFA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK) == phase) {
		/*
		 * Do not read the rest of the completion entry before the
		 * phase bit was validated
		 */
		dma_rmb();

		/* Handle specific event*/
		handler_cb = efa_com_get_specific_aenq_cb(edev,
							  aenq_common->group);
		handler_cb(data, aenq_e); /* call the actual event handler*/

		/* Get next event entry */
		ci++;
		processed++;

		if (ci == aenq->depth) {
			ci = 0;
			phase = !phase;
		}
		aenq_e = &aenq->entries[ci];
		aenq_common = &aenq_e->aenq_common_desc;
	}

	aenq->cc += processed;
	aenq->phase = phase;

	/* Don't update aenq doorbell if there weren't any processed events */
	if (!processed)
		return;

	/* barrier not needed in case of writel */
	writel(aenq->cc, edev->reg_bar + EFA_REGS_AENQ_CONS_DB_OFF);
}

static void efa_com_mmio_reg_read_resp_addr_init(struct efa_com_dev *edev)
{
	struct efa_com_mmio_read *mmio_read = &edev->mmio_read;
	u32 addr_high;
	u32 addr_low;

	/* dma_addr_bits is unknown at this point */
	addr_high = (mmio_read->read_resp_dma_addr >> 32) & GENMASK(31, 0);
	addr_low = mmio_read->read_resp_dma_addr & GENMASK(31, 0);

	writel(addr_high, edev->reg_bar + EFA_REGS_MMIO_RESP_HI_OFF);
	writel(addr_low, edev->reg_bar + EFA_REGS_MMIO_RESP_LO_OFF);
}

int efa_com_mmio_reg_read_init(struct efa_com_dev *edev)
{
	struct efa_com_mmio_read *mmio_read = &edev->mmio_read;

	spin_lock_init(&mmio_read->lock);
	mmio_read->read_resp =
		dma_alloc_coherent(edev->dmadev, sizeof(*mmio_read->read_resp),
				   &mmio_read->read_resp_dma_addr, GFP_KERNEL);
	if (!mmio_read->read_resp)
		return -ENOMEM;

	efa_com_mmio_reg_read_resp_addr_init(edev);

	mmio_read->read_resp->req_id = 0;
	mmio_read->seq_num = 0;
	mmio_read->mmio_read_timeout = EFA_REG_READ_TIMEOUT_US;

	return 0;
}

void efa_com_mmio_reg_read_destroy(struct efa_com_dev *edev)
{
	struct efa_com_mmio_read *mmio_read = &edev->mmio_read;

	dma_free_coherent(edev->dmadev, sizeof(*mmio_read->read_resp),
			  mmio_read->read_resp, mmio_read->read_resp_dma_addr);
}

int efa_com_validate_version(struct efa_com_dev *edev)
{
	u32 min_ctrl_ver = 0;
	u32 ctrl_ver_masked;
	u32 min_ver = 0;
	u32 ctrl_ver;
	u32 ver;

	/*
	 * Make sure the EFA version and the controller version are at least
	 * as the driver expects
	 */
	ver = efa_com_reg_read32(edev, EFA_REGS_VERSION_OFF);
	ctrl_ver = efa_com_reg_read32(edev,
				      EFA_REGS_CONTROLLER_VERSION_OFF);

	ibdev_dbg(edev->efa_dev, "efa device version: %d.%d\n",
		  EFA_GET(&ver, EFA_REGS_VERSION_MAJOR_VERSION),
		  EFA_GET(&ver, EFA_REGS_VERSION_MINOR_VERSION));

	EFA_SET(&min_ver, EFA_REGS_VERSION_MAJOR_VERSION,
		EFA_ADMIN_API_VERSION_MAJOR);
	EFA_SET(&min_ver, EFA_REGS_VERSION_MINOR_VERSION,
		EFA_ADMIN_API_VERSION_MINOR);
	if (ver < min_ver) {
		ibdev_err(edev->efa_dev,
			  "EFA version is lower than the minimal version the driver supports\n");
		return -EOPNOTSUPP;
	}

	ibdev_dbg(
		edev->efa_dev,
		"efa controller version: %d.%d.%d implementation version %d\n",
		EFA_GET(&ctrl_ver, EFA_REGS_CONTROLLER_VERSION_MAJOR_VERSION),
		EFA_GET(&ctrl_ver, EFA_REGS_CONTROLLER_VERSION_MINOR_VERSION),
		EFA_GET(&ctrl_ver,
			EFA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION),
		EFA_GET(&ctrl_ver, EFA_REGS_CONTROLLER_VERSION_IMPL_ID));

	ctrl_ver_masked =
		EFA_GET(&ctrl_ver, EFA_REGS_CONTROLLER_VERSION_MAJOR_VERSION) |
		EFA_GET(&ctrl_ver, EFA_REGS_CONTROLLER_VERSION_MINOR_VERSION) |
		EFA_GET(&ctrl_ver,
			EFA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION);

	EFA_SET(&min_ctrl_ver, EFA_REGS_CONTROLLER_VERSION_MAJOR_VERSION,
		EFA_CTRL_MAJOR);
	EFA_SET(&min_ctrl_ver, EFA_REGS_CONTROLLER_VERSION_MINOR_VERSION,
		EFA_CTRL_MINOR);
	EFA_SET(&min_ctrl_ver, EFA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION,
		EFA_CTRL_SUB_MINOR);
	/* Validate the ctrl version without the implementation ID */
	if (ctrl_ver_masked < min_ctrl_ver) {
		ibdev_err(edev->efa_dev,
			  "EFA ctrl version is lower than the minimal ctrl version the driver supports\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * efa_com_get_dma_width - Retrieve physical dma address width the device
 * supports.
 * @edev: EFA communication layer struct
 *
 * Retrieve the maximum physical address bits the device can handle.
 *
 * @return: > 0 on Success and negative value otherwise.
 */
int efa_com_get_dma_width(struct efa_com_dev *edev)
{
	u32 caps = efa_com_reg_read32(edev, EFA_REGS_CAPS_OFF);
	int width;

	width = EFA_GET(&caps, EFA_REGS_CAPS_DMA_ADDR_WIDTH);

	ibdev_dbg(edev->efa_dev, "DMA width: %d\n", width);

	if (width < 32 || width > 64) {
		ibdev_err(edev->efa_dev, "DMA width illegal value: %d\n", width);
		return -EINVAL;
	}

	edev->dma_addr_bits = width;

	return width;
}

static int wait_for_reset_state(struct efa_com_dev *edev, u32 timeout, int on)
{
	u32 val, i;

	for (i = 0; i < timeout; i++) {
		val = efa_com_reg_read32(edev, EFA_REGS_DEV_STS_OFF);

		if (EFA_GET(&val, EFA_REGS_DEV_STS_RESET_IN_PROGRESS) == on)
			return 0;

		ibdev_dbg(edev->efa_dev, "Reset indication val %d\n", val);
		msleep(EFA_POLL_INTERVAL_MS);
	}

	return -ETIME;
}

/**
 * efa_com_dev_reset - Perform device FLR to the device.
 * @edev: EFA communication layer struct
 * @reset_reason: Specify what is the trigger for the reset in case of an error.
 *
 * @return - 0 on success, negative value on failure.
 */
int efa_com_dev_reset(struct efa_com_dev *edev,
		      enum efa_regs_reset_reason_types reset_reason)
{
	u32 stat, timeout, cap;
	u32 reset_val = 0;
	int err;

	stat = efa_com_reg_read32(edev, EFA_REGS_DEV_STS_OFF);
	cap = efa_com_reg_read32(edev, EFA_REGS_CAPS_OFF);

	if (!EFA_GET(&stat, EFA_REGS_DEV_STS_READY)) {
		ibdev_err(edev->efa_dev,
			  "Device isn't ready, can't reset device\n");
		return -EINVAL;
	}

	timeout = EFA_GET(&cap, EFA_REGS_CAPS_RESET_TIMEOUT);
	if (!timeout) {
		ibdev_err(edev->efa_dev, "Invalid timeout value\n");
		return -EINVAL;
	}

	/* start reset */
	EFA_SET(&reset_val, EFA_REGS_DEV_CTL_DEV_RESET, 1);
	EFA_SET(&reset_val, EFA_REGS_DEV_CTL_RESET_REASON, reset_reason);
	writel(reset_val, edev->reg_bar + EFA_REGS_DEV_CTL_OFF);

	/* reset clears the mmio readless address, restore it */
	efa_com_mmio_reg_read_resp_addr_init(edev);

	err = wait_for_reset_state(edev, timeout, 1);
	if (err) {
		ibdev_err(edev->efa_dev, "Reset indication didn't turn on\n");
		return err;
	}

	/* reset done */
	writel(0, edev->reg_bar + EFA_REGS_DEV_CTL_OFF);
	err = wait_for_reset_state(edev, timeout, 0);
	if (err) {
		ibdev_err(edev->efa_dev, "Reset indication didn't turn off\n");
		return err;
	}

	timeout = EFA_GET(&cap, EFA_REGS_CAPS_ADMIN_CMD_TO);
	if (timeout)
		/* the resolution of timeout reg is 100ms */
		edev->aq.completion_timeout = timeout * 100000;
	else
		edev->aq.completion_timeout = ADMIN_CMD_TIMEOUT_US;

	return 0;
}
