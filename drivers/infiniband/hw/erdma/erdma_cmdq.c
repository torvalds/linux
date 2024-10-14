// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#include "erdma.h"

static void arm_cmdq_cq(struct erdma_cmdq *cmdq)
{
	struct erdma_dev *dev = container_of(cmdq, struct erdma_dev, cmdq);
	u64 db_data = FIELD_PREP(ERDMA_CQDB_CI_MASK, cmdq->cq.ci) |
		      FIELD_PREP(ERDMA_CQDB_ARM_MASK, 1) |
		      FIELD_PREP(ERDMA_CQDB_CMDSN_MASK, cmdq->cq.cmdsn) |
		      FIELD_PREP(ERDMA_CQDB_IDX_MASK, cmdq->cq.cmdsn);

	*cmdq->cq.dbrec = db_data;
	writeq(db_data, dev->func_bar + ERDMA_CMDQ_CQDB_REG);

	atomic64_inc(&cmdq->cq.armed_num);
}

static void kick_cmdq_db(struct erdma_cmdq *cmdq)
{
	struct erdma_dev *dev = container_of(cmdq, struct erdma_dev, cmdq);
	u64 db_data = FIELD_PREP(ERDMA_CMD_HDR_WQEBB_INDEX_MASK, cmdq->sq.pi);

	*cmdq->sq.dbrec = db_data;
	writeq(db_data, dev->func_bar + ERDMA_CMDQ_SQDB_REG);
}

static struct erdma_comp_wait *get_comp_wait(struct erdma_cmdq *cmdq)
{
	int comp_idx;

	spin_lock(&cmdq->lock);
	comp_idx = find_first_zero_bit(cmdq->comp_wait_bitmap,
				       cmdq->max_outstandings);
	if (comp_idx == cmdq->max_outstandings) {
		spin_unlock(&cmdq->lock);
		return ERR_PTR(-ENOMEM);
	}

	__set_bit(comp_idx, cmdq->comp_wait_bitmap);
	spin_unlock(&cmdq->lock);

	return &cmdq->wait_pool[comp_idx];
}

static void put_comp_wait(struct erdma_cmdq *cmdq,
			  struct erdma_comp_wait *comp_wait)
{
	int used;

	cmdq->wait_pool[comp_wait->ctx_id].cmd_status = ERDMA_CMD_STATUS_INIT;
	spin_lock(&cmdq->lock);
	used = __test_and_clear_bit(comp_wait->ctx_id, cmdq->comp_wait_bitmap);
	spin_unlock(&cmdq->lock);

	WARN_ON(!used);
}

static int erdma_cmdq_wait_res_init(struct erdma_dev *dev,
				    struct erdma_cmdq *cmdq)
{
	int i;

	cmdq->wait_pool =
		devm_kcalloc(&dev->pdev->dev, cmdq->max_outstandings,
			     sizeof(struct erdma_comp_wait), GFP_KERNEL);
	if (!cmdq->wait_pool)
		return -ENOMEM;

	spin_lock_init(&cmdq->lock);
	cmdq->comp_wait_bitmap = devm_bitmap_zalloc(
		&dev->pdev->dev, cmdq->max_outstandings, GFP_KERNEL);
	if (!cmdq->comp_wait_bitmap)
		return -ENOMEM;

	for (i = 0; i < cmdq->max_outstandings; i++) {
		init_completion(&cmdq->wait_pool[i].wait_event);
		cmdq->wait_pool[i].ctx_id = i;
	}

	return 0;
}

static int erdma_cmdq_sq_init(struct erdma_dev *dev)
{
	struct erdma_cmdq *cmdq = &dev->cmdq;
	struct erdma_cmdq_sq *sq = &cmdq->sq;

	sq->wqebb_cnt = SQEBB_COUNT(ERDMA_CMDQ_SQE_SIZE);
	sq->depth = cmdq->max_outstandings * sq->wqebb_cnt;

	sq->qbuf = dma_alloc_coherent(&dev->pdev->dev, sq->depth << SQEBB_SHIFT,
				      &sq->qbuf_dma_addr, GFP_KERNEL);
	if (!sq->qbuf)
		return -ENOMEM;

	sq->dbrec = dma_pool_zalloc(dev->db_pool, GFP_KERNEL, &sq->dbrec_dma);
	if (!sq->dbrec)
		goto err_out;

	spin_lock_init(&sq->lock);

	erdma_reg_write32(dev, ERDMA_REGS_CMDQ_SQ_ADDR_H_REG,
			  upper_32_bits(sq->qbuf_dma_addr));
	erdma_reg_write32(dev, ERDMA_REGS_CMDQ_SQ_ADDR_L_REG,
			  lower_32_bits(sq->qbuf_dma_addr));
	erdma_reg_write32(dev, ERDMA_REGS_CMDQ_DEPTH_REG, sq->depth);
	erdma_reg_write64(dev, ERDMA_CMDQ_SQ_DB_HOST_ADDR_REG, sq->dbrec_dma);

	return 0;

err_out:
	dma_free_coherent(&dev->pdev->dev, sq->depth << SQEBB_SHIFT,
			  sq->qbuf, sq->qbuf_dma_addr);

	return -ENOMEM;
}

static int erdma_cmdq_cq_init(struct erdma_dev *dev)
{
	struct erdma_cmdq *cmdq = &dev->cmdq;
	struct erdma_cmdq_cq *cq = &cmdq->cq;

	cq->depth = cmdq->sq.depth;
	cq->qbuf = dma_alloc_coherent(&dev->pdev->dev, cq->depth << CQE_SHIFT,
				      &cq->qbuf_dma_addr, GFP_KERNEL);
	if (!cq->qbuf)
		return -ENOMEM;

	spin_lock_init(&cq->lock);

	cq->dbrec = dma_pool_zalloc(dev->db_pool, GFP_KERNEL, &cq->dbrec_dma);
	if (!cq->dbrec)
		goto err_out;

	atomic64_set(&cq->armed_num, 0);

	erdma_reg_write32(dev, ERDMA_REGS_CMDQ_CQ_ADDR_H_REG,
			  upper_32_bits(cq->qbuf_dma_addr));
	erdma_reg_write32(dev, ERDMA_REGS_CMDQ_CQ_ADDR_L_REG,
			  lower_32_bits(cq->qbuf_dma_addr));
	erdma_reg_write64(dev, ERDMA_CMDQ_CQ_DB_HOST_ADDR_REG, cq->dbrec_dma);

	return 0;

err_out:
	dma_free_coherent(&dev->pdev->dev, cq->depth << CQE_SHIFT, cq->qbuf,
			  cq->qbuf_dma_addr);

	return -ENOMEM;
}

static int erdma_cmdq_eq_init(struct erdma_dev *dev)
{
	struct erdma_cmdq *cmdq = &dev->cmdq;
	struct erdma_eq *eq = &cmdq->eq;
	int ret;

	ret = erdma_eq_common_init(dev, eq, cmdq->max_outstandings);
	if (ret)
		return ret;

	eq->db = dev->func_bar + ERDMA_REGS_CEQ_DB_BASE_REG;

	erdma_reg_write32(dev, ERDMA_REGS_CMDQ_EQ_ADDR_H_REG,
			  upper_32_bits(eq->qbuf_dma_addr));
	erdma_reg_write32(dev, ERDMA_REGS_CMDQ_EQ_ADDR_L_REG,
			  lower_32_bits(eq->qbuf_dma_addr));
	erdma_reg_write32(dev, ERDMA_REGS_CMDQ_EQ_DEPTH_REG, eq->depth);
	erdma_reg_write64(dev, ERDMA_CMDQ_EQ_DB_HOST_ADDR_REG, eq->dbrec_dma);

	return 0;
}

int erdma_cmdq_init(struct erdma_dev *dev)
{
	struct erdma_cmdq *cmdq = &dev->cmdq;
	int err;

	cmdq->max_outstandings = ERDMA_CMDQ_MAX_OUTSTANDING;
	cmdq->use_event = false;

	sema_init(&cmdq->credits, cmdq->max_outstandings);

	err = erdma_cmdq_wait_res_init(dev, cmdq);
	if (err)
		return err;

	err = erdma_cmdq_sq_init(dev);
	if (err)
		return err;

	err = erdma_cmdq_cq_init(dev);
	if (err)
		goto err_destroy_sq;

	err = erdma_cmdq_eq_init(dev);
	if (err)
		goto err_destroy_cq;

	set_bit(ERDMA_CMDQ_STATE_OK_BIT, &cmdq->state);

	return 0;

err_destroy_cq:
	dma_free_coherent(&dev->pdev->dev, cmdq->cq.depth << CQE_SHIFT,
			  cmdq->cq.qbuf, cmdq->cq.qbuf_dma_addr);

	dma_pool_free(dev->db_pool, cmdq->cq.dbrec, cmdq->cq.dbrec_dma);

err_destroy_sq:
	dma_free_coherent(&dev->pdev->dev, cmdq->sq.depth << SQEBB_SHIFT,
			  cmdq->sq.qbuf, cmdq->sq.qbuf_dma_addr);

	dma_pool_free(dev->db_pool, cmdq->sq.dbrec, cmdq->sq.dbrec_dma);

	return err;
}

void erdma_finish_cmdq_init(struct erdma_dev *dev)
{
	/* after device init successfully, change cmdq to event mode. */
	dev->cmdq.use_event = true;
	arm_cmdq_cq(&dev->cmdq);
}

void erdma_cmdq_destroy(struct erdma_dev *dev)
{
	struct erdma_cmdq *cmdq = &dev->cmdq;

	clear_bit(ERDMA_CMDQ_STATE_OK_BIT, &cmdq->state);

	erdma_eq_destroy(dev, &cmdq->eq);

	dma_free_coherent(&dev->pdev->dev, cmdq->sq.depth << SQEBB_SHIFT,
			  cmdq->sq.qbuf, cmdq->sq.qbuf_dma_addr);

	dma_pool_free(dev->db_pool, cmdq->sq.dbrec, cmdq->sq.dbrec_dma);

	dma_free_coherent(&dev->pdev->dev, cmdq->cq.depth << CQE_SHIFT,
			  cmdq->cq.qbuf, cmdq->cq.qbuf_dma_addr);

	dma_pool_free(dev->db_pool, cmdq->cq.dbrec, cmdq->cq.dbrec_dma);
}

static void *get_next_valid_cmdq_cqe(struct erdma_cmdq *cmdq)
{
	__be32 *cqe = get_queue_entry(cmdq->cq.qbuf, cmdq->cq.ci,
				      cmdq->cq.depth, CQE_SHIFT);
	u32 owner = FIELD_GET(ERDMA_CQE_HDR_OWNER_MASK,
			      be32_to_cpu(READ_ONCE(*cqe)));

	return owner ^ !!(cmdq->cq.ci & cmdq->cq.depth) ? cqe : NULL;
}

static void push_cmdq_sqe(struct erdma_cmdq *cmdq, u64 *req, size_t req_len,
			  struct erdma_comp_wait *comp_wait)
{
	__le64 *wqe;
	u64 hdr = *req;

	comp_wait->cmd_status = ERDMA_CMD_STATUS_ISSUED;
	reinit_completion(&comp_wait->wait_event);
	comp_wait->sq_pi = cmdq->sq.pi;

	wqe = get_queue_entry(cmdq->sq.qbuf, cmdq->sq.pi, cmdq->sq.depth,
			      SQEBB_SHIFT);
	memcpy(wqe, req, req_len);

	cmdq->sq.pi += cmdq->sq.wqebb_cnt;
	hdr |= FIELD_PREP(ERDMA_CMD_HDR_WQEBB_INDEX_MASK, cmdq->sq.pi) |
	       FIELD_PREP(ERDMA_CMD_HDR_CONTEXT_COOKIE_MASK,
			  comp_wait->ctx_id) |
	       FIELD_PREP(ERDMA_CMD_HDR_WQEBB_CNT_MASK, cmdq->sq.wqebb_cnt - 1);
	*wqe = cpu_to_le64(hdr);

	kick_cmdq_db(cmdq);
}

static int erdma_poll_single_cmd_completion(struct erdma_cmdq *cmdq)
{
	struct erdma_comp_wait *comp_wait;
	u32 hdr0, sqe_idx;
	__be32 *cqe;
	u16 ctx_id;
	u64 *sqe;

	cqe = get_next_valid_cmdq_cqe(cmdq);
	if (!cqe)
		return -EAGAIN;

	cmdq->cq.ci++;

	dma_rmb();
	hdr0 = be32_to_cpu(*cqe);
	sqe_idx = be32_to_cpu(*(cqe + 1));

	sqe = get_queue_entry(cmdq->sq.qbuf, sqe_idx, cmdq->sq.depth,
			      SQEBB_SHIFT);
	ctx_id = FIELD_GET(ERDMA_CMD_HDR_CONTEXT_COOKIE_MASK, *sqe);
	comp_wait = &cmdq->wait_pool[ctx_id];
	if (comp_wait->cmd_status != ERDMA_CMD_STATUS_ISSUED)
		return -EIO;

	comp_wait->cmd_status = ERDMA_CMD_STATUS_FINISHED;
	comp_wait->comp_status = FIELD_GET(ERDMA_CQE_HDR_SYNDROME_MASK, hdr0);
	cmdq->sq.ci += cmdq->sq.wqebb_cnt;
	/* Copy 16B comp data after cqe hdr to outer */
	be32_to_cpu_array(comp_wait->comp_data, cqe + 2, 4);

	if (cmdq->use_event)
		complete(&comp_wait->wait_event);

	return 0;
}

static void erdma_polling_cmd_completions(struct erdma_cmdq *cmdq)
{
	unsigned long flags;
	u16 comp_num;

	spin_lock_irqsave(&cmdq->cq.lock, flags);

	/* We must have less than # of max_outstandings
	 * completions at one time.
	 */
	for (comp_num = 0; comp_num < cmdq->max_outstandings; comp_num++)
		if (erdma_poll_single_cmd_completion(cmdq))
			break;

	if (comp_num && cmdq->use_event)
		arm_cmdq_cq(cmdq);

	spin_unlock_irqrestore(&cmdq->cq.lock, flags);
}

void erdma_cmdq_completion_handler(struct erdma_cmdq *cmdq)
{
	int got_event = 0;

	if (!test_bit(ERDMA_CMDQ_STATE_OK_BIT, &cmdq->state) ||
	    !cmdq->use_event)
		return;

	while (get_next_valid_eqe(&cmdq->eq)) {
		cmdq->eq.ci++;
		got_event++;
	}

	if (got_event) {
		cmdq->cq.cmdsn++;
		erdma_polling_cmd_completions(cmdq);
	}

	notify_eq(&cmdq->eq);
}

static int erdma_poll_cmd_completion(struct erdma_comp_wait *comp_ctx,
				     struct erdma_cmdq *cmdq, u32 timeout)
{
	unsigned long comp_timeout = jiffies + msecs_to_jiffies(timeout);

	while (1) {
		erdma_polling_cmd_completions(cmdq);
		if (comp_ctx->cmd_status != ERDMA_CMD_STATUS_ISSUED)
			break;

		if (time_is_before_jiffies(comp_timeout))
			return -ETIME;

		msleep(20);
	}

	return 0;
}

static int erdma_wait_cmd_completion(struct erdma_comp_wait *comp_ctx,
				     struct erdma_cmdq *cmdq, u32 timeout)
{
	unsigned long flags = 0;

	wait_for_completion_timeout(&comp_ctx->wait_event,
				    msecs_to_jiffies(timeout));

	if (unlikely(comp_ctx->cmd_status != ERDMA_CMD_STATUS_FINISHED)) {
		spin_lock_irqsave(&cmdq->cq.lock, flags);
		comp_ctx->cmd_status = ERDMA_CMD_STATUS_TIMEOUT;
		spin_unlock_irqrestore(&cmdq->cq.lock, flags);
		return -ETIME;
	}

	return 0;
}

void erdma_cmdq_build_reqhdr(u64 *hdr, u32 mod, u32 op)
{
	*hdr = FIELD_PREP(ERDMA_CMD_HDR_SUB_MOD_MASK, mod) |
	       FIELD_PREP(ERDMA_CMD_HDR_OPCODE_MASK, op);
}

int erdma_post_cmd_wait(struct erdma_cmdq *cmdq, void *req, u32 req_size,
			u64 *resp0, u64 *resp1)
{
	struct erdma_comp_wait *comp_wait;
	int ret;

	if (!test_bit(ERDMA_CMDQ_STATE_OK_BIT, &cmdq->state))
		return -ENODEV;

	down(&cmdq->credits);

	comp_wait = get_comp_wait(cmdq);
	if (IS_ERR(comp_wait)) {
		clear_bit(ERDMA_CMDQ_STATE_OK_BIT, &cmdq->state);
		set_bit(ERDMA_CMDQ_STATE_CTX_ERR_BIT, &cmdq->state);
		up(&cmdq->credits);
		return PTR_ERR(comp_wait);
	}

	spin_lock(&cmdq->sq.lock);
	push_cmdq_sqe(cmdq, req, req_size, comp_wait);
	spin_unlock(&cmdq->sq.lock);

	if (cmdq->use_event)
		ret = erdma_wait_cmd_completion(comp_wait, cmdq,
						ERDMA_CMDQ_TIMEOUT_MS);
	else
		ret = erdma_poll_cmd_completion(comp_wait, cmdq,
						ERDMA_CMDQ_TIMEOUT_MS);

	if (ret) {
		set_bit(ERDMA_CMDQ_STATE_TIMEOUT_BIT, &cmdq->state);
		clear_bit(ERDMA_CMDQ_STATE_OK_BIT, &cmdq->state);
		goto out;
	}

	if (comp_wait->comp_status)
		ret = -EIO;

	if (resp0 && resp1) {
		*resp0 = *((u64 *)&comp_wait->comp_data[0]);
		*resp1 = *((u64 *)&comp_wait->comp_data[2]);
	}
	put_comp_wait(cmdq, comp_wait);

out:
	up(&cmdq->credits);

	return ret;
}
