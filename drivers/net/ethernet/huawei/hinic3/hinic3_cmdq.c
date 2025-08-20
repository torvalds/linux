// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>

#include "hinic3_cmdq.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"

#define CMDQ_BUF_SIZE             2048
#define CMDQ_WQEBB_SIZE           64

#define CMDQ_CTXT_CURR_WQE_PAGE_PFN_MASK  GENMASK_ULL(51, 0)
#define CMDQ_CTXT_EQ_ID_MASK              GENMASK_ULL(60, 53)
#define CMDQ_CTXT_CEQ_ARM_MASK            BIT_ULL(61)
#define CMDQ_CTXT_CEQ_EN_MASK             BIT_ULL(62)
#define CMDQ_CTXT_HW_BUSY_BIT_MASK        BIT_ULL(63)

#define CMDQ_CTXT_WQ_BLOCK_PFN_MASK       GENMASK_ULL(51, 0)
#define CMDQ_CTXT_CI_MASK                 GENMASK_ULL(63, 52)
#define CMDQ_CTXT_SET(val, member)  \
	FIELD_PREP(CMDQ_CTXT_##member##_MASK, val)

#define CMDQ_PFN(addr)  ((addr) >> 12)

/* cmdq work queue's chip logical address table is up to 512B */
#define CMDQ_WQ_CLA_SIZE  512

/* Completion codes: send, direct sync, force stop */
#define CMDQ_SEND_CMPT_CODE         10
#define CMDQ_DIRECT_SYNC_CMPT_CODE  11
#define CMDQ_FORCE_STOP_CMPT_CODE   12

#define CMDQ_WQE_NUM_WQEBBS  1

static struct cmdq_wqe *cmdq_read_wqe(struct hinic3_wq *wq, u16 *ci)
{
	if (hinic3_wq_get_used(wq) == 0)
		return NULL;

	*ci = wq->cons_idx & wq->idx_mask;

	return get_q_element(&wq->qpages, wq->cons_idx, NULL);
}

void hinic3_free_cmd_buf(struct hinic3_hwdev *hwdev,
			 struct hinic3_cmd_buf *cmd_buf)
{
	struct hinic3_cmdqs *cmdqs;

	if (!refcount_dec_and_test(&cmd_buf->ref_cnt))
		return;

	cmdqs = hwdev->cmdqs;

	dma_pool_free(cmdqs->cmd_buf_pool, cmd_buf->buf, cmd_buf->dma_addr);
	kfree(cmd_buf);
}

static void cmdq_clear_cmd_buf(struct hinic3_cmdq_cmd_info *cmd_info,
			       struct hinic3_hwdev *hwdev)
{
	if (cmd_info->buf_in) {
		hinic3_free_cmd_buf(hwdev, cmd_info->buf_in);
		cmd_info->buf_in = NULL;
	}
}

static void cmdq_init_queue_ctxt(struct hinic3_hwdev *hwdev, u8 cmdq_id,
				 struct comm_cmdq_ctxt_info *ctxt_info)
{
	const struct hinic3_cmdqs *cmdqs;
	u64 cmdq_first_block_paddr, pfn;
	const struct hinic3_wq *wq;

	cmdqs = hwdev->cmdqs;
	wq = &cmdqs->cmdq[cmdq_id].wq;
	pfn = CMDQ_PFN(hinic3_wq_get_first_wqe_page_addr(wq));

	ctxt_info->curr_wqe_page_pfn =
		cpu_to_le64(CMDQ_CTXT_SET(1, HW_BUSY_BIT) |
			    CMDQ_CTXT_SET(1, CEQ_EN)	|
			    CMDQ_CTXT_SET(1, CEQ_ARM)	|
			    CMDQ_CTXT_SET(0, EQ_ID) |
			    CMDQ_CTXT_SET(pfn, CURR_WQE_PAGE_PFN));

	if (!hinic3_wq_is_0_level_cla(wq)) {
		cmdq_first_block_paddr = cmdqs->wq_block_paddr;
		pfn = CMDQ_PFN(cmdq_first_block_paddr);
	}

	ctxt_info->wq_block_pfn = cpu_to_le64(CMDQ_CTXT_SET(wq->cons_idx, CI) |
					      CMDQ_CTXT_SET(pfn, WQ_BLOCK_PFN));
}

static int init_cmdq(struct hinic3_cmdq *cmdq, struct hinic3_hwdev *hwdev,
		     enum hinic3_cmdq_type q_type)
{
	int err;

	cmdq->cmdq_type = q_type;
	cmdq->wrapped = 1;
	cmdq->hwdev = hwdev;

	spin_lock_init(&cmdq->cmdq_lock);

	cmdq->cmd_infos = kcalloc(cmdq->wq.q_depth, sizeof(*cmdq->cmd_infos),
				  GFP_KERNEL);
	if (!cmdq->cmd_infos) {
		err = -ENOMEM;
		return err;
	}

	return 0;
}

static int hinic3_set_cmdq_ctxt(struct hinic3_hwdev *hwdev, u8 cmdq_id)
{
	struct comm_cmd_set_cmdq_ctxt cmdq_ctxt = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	cmdq_init_queue_ctxt(hwdev, cmdq_id, &cmdq_ctxt.ctxt);
	cmdq_ctxt.func_id = hinic3_global_func_id(hwdev);
	cmdq_ctxt.cmdq_id = cmdq_id;

	mgmt_msg_params_init_default(&msg_params, &cmdq_ctxt,
				     sizeof(cmdq_ctxt));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_SET_CMDQ_CTXT, &msg_params);
	if (err || cmdq_ctxt.head.status) {
		dev_err(hwdev->dev, "Failed to set cmdq ctxt, err: %d, status: 0x%x\n",
			err, cmdq_ctxt.head.status);
		return -EFAULT;
	}

	return 0;
}

static int hinic3_set_cmdq_ctxts(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cmdqs *cmdqs = hwdev->cmdqs;
	u8 cmdq_type;
	int err;

	for (cmdq_type = 0; cmdq_type < cmdqs->cmdq_num; cmdq_type++) {
		err = hinic3_set_cmdq_ctxt(hwdev, cmdq_type);
		if (err)
			return err;
	}

	cmdqs->status |= HINIC3_CMDQ_ENABLE;
	cmdqs->disable_flag = 0;

	return 0;
}

static int create_cmdq_wq(struct hinic3_hwdev *hwdev,
			  struct hinic3_cmdqs *cmdqs)
{
	u8 cmdq_type;
	int err;

	for (cmdq_type = 0; cmdq_type < cmdqs->cmdq_num; cmdq_type++) {
		err = hinic3_wq_create(hwdev, &cmdqs->cmdq[cmdq_type].wq,
				       CMDQ_DEPTH, CMDQ_WQEBB_SIZE);
		if (err) {
			dev_err(hwdev->dev, "Failed to create cmdq wq\n");
			goto err_destroy_wq;
		}
	}

	/* 1-level Chip Logical Address (CLA) must put all
	 * cmdq's wq page addr in one wq block
	 */
	if (!hinic3_wq_is_0_level_cla(&cmdqs->cmdq[HINIC3_CMDQ_SYNC].wq)) {
		if (cmdqs->cmdq[HINIC3_CMDQ_SYNC].wq.qpages.num_pages >
		    CMDQ_WQ_CLA_SIZE / sizeof(u64)) {
			err = -EINVAL;
			dev_err(hwdev->dev,
				"Cmdq number of wq pages exceeds limit: %lu\n",
				CMDQ_WQ_CLA_SIZE / sizeof(u64));
			goto err_destroy_wq;
		}

		cmdqs->wq_block_vaddr =
			dma_alloc_coherent(hwdev->dev, HINIC3_MIN_PAGE_SIZE,
					   &cmdqs->wq_block_paddr, GFP_KERNEL);
		if (!cmdqs->wq_block_vaddr) {
			err = -ENOMEM;
			goto err_destroy_wq;
		}

		for (cmdq_type = 0; cmdq_type < cmdqs->cmdq_num; cmdq_type++)
			memcpy((u8 *)cmdqs->wq_block_vaddr +
			       CMDQ_WQ_CLA_SIZE * cmdq_type,
			       cmdqs->cmdq[cmdq_type].wq.wq_block_vaddr,
			       cmdqs->cmdq[cmdq_type].wq.qpages.num_pages *
			       sizeof(__be64));
	}

	return 0;

err_destroy_wq:
	while (cmdq_type > 0) {
		cmdq_type--;
		hinic3_wq_destroy(hwdev, &cmdqs->cmdq[cmdq_type].wq);
	}

	return err;
}

static void destroy_cmdq_wq(struct hinic3_hwdev *hwdev,
			    struct hinic3_cmdqs *cmdqs)
{
	u8 cmdq_type;

	if (cmdqs->wq_block_vaddr)
		dma_free_coherent(hwdev->dev, HINIC3_MIN_PAGE_SIZE,
				  cmdqs->wq_block_vaddr, cmdqs->wq_block_paddr);

	for (cmdq_type = 0; cmdq_type < cmdqs->cmdq_num; cmdq_type++)
		hinic3_wq_destroy(hwdev, &cmdqs->cmdq[cmdq_type].wq);
}

static int init_cmdqs(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cmdqs *cmdqs;

	cmdqs = kzalloc(sizeof(*cmdqs), GFP_KERNEL);
	if (!cmdqs)
		return -ENOMEM;

	hwdev->cmdqs = cmdqs;
	cmdqs->hwdev = hwdev;
	cmdqs->cmdq_num = hwdev->max_cmdq;

	cmdqs->cmd_buf_pool = dma_pool_create("hinic3_cmdq", hwdev->dev,
					      CMDQ_BUF_SIZE, CMDQ_BUF_SIZE, 0);
	if (!cmdqs->cmd_buf_pool) {
		dev_err(hwdev->dev, "Failed to create cmdq buffer pool\n");
		kfree(cmdqs);
		return -ENOMEM;
	}

	return 0;
}

static void cmdq_flush_sync_cmd(struct hinic3_cmdq_cmd_info *cmd_info)
{
	if (cmd_info->cmd_type != HINIC3_CMD_TYPE_DIRECT_RESP)
		return;

	cmd_info->cmd_type = HINIC3_CMD_TYPE_FORCE_STOP;

	if (cmd_info->cmpt_code &&
	    *cmd_info->cmpt_code == CMDQ_SEND_CMPT_CODE)
		*cmd_info->cmpt_code = CMDQ_FORCE_STOP_CMPT_CODE;

	if (cmd_info->done) {
		complete(cmd_info->done);
		cmd_info->done = NULL;
		cmd_info->cmpt_code = NULL;
		cmd_info->direct_resp = NULL;
		cmd_info->errcode = NULL;
	}
}

static void hinic3_cmdq_flush_cmd(struct hinic3_cmdq *cmdq)
{
	struct hinic3_cmdq_cmd_info *cmd_info;
	u16 ci;

	spin_lock_bh(&cmdq->cmdq_lock);
	while (cmdq_read_wqe(&cmdq->wq, &ci)) {
		hinic3_wq_put_wqebbs(&cmdq->wq, CMDQ_WQE_NUM_WQEBBS);
		cmd_info = &cmdq->cmd_infos[ci];
		if (cmd_info->cmd_type == HINIC3_CMD_TYPE_DIRECT_RESP)
			cmdq_flush_sync_cmd(cmd_info);
	}
	spin_unlock_bh(&cmdq->cmdq_lock);
}

void hinic3_cmdq_flush_sync_cmd(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cmdq *cmdq;
	u16 wqe_cnt, wqe_idx, i;
	struct hinic3_wq *wq;

	cmdq = &hwdev->cmdqs->cmdq[HINIC3_CMDQ_SYNC];
	spin_lock_bh(&cmdq->cmdq_lock);
	wq = &cmdq->wq;
	wqe_cnt = hinic3_wq_get_used(wq);
	for (i = 0; i < wqe_cnt; i++) {
		wqe_idx = (wq->cons_idx + i) & wq->idx_mask;
		cmdq_flush_sync_cmd(cmdq->cmd_infos + wqe_idx);
	}
	spin_unlock_bh(&cmdq->cmdq_lock);
}

static void hinic3_cmdq_reset_all_cmd_buf(struct hinic3_cmdq *cmdq)
{
	u16 i;

	for (i = 0; i < cmdq->wq.q_depth; i++)
		cmdq_clear_cmd_buf(&cmdq->cmd_infos[i], cmdq->hwdev);
}

int hinic3_reinit_cmdq_ctxts(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cmdqs *cmdqs = hwdev->cmdqs;
	u8 cmdq_type;

	for (cmdq_type = 0; cmdq_type < cmdqs->cmdq_num; cmdq_type++) {
		hinic3_cmdq_flush_cmd(&cmdqs->cmdq[cmdq_type]);
		hinic3_cmdq_reset_all_cmd_buf(&cmdqs->cmdq[cmdq_type]);
		cmdqs->cmdq[cmdq_type].wrapped = 1;
		hinic3_wq_reset(&cmdqs->cmdq[cmdq_type].wq);
	}

	return hinic3_set_cmdq_ctxts(hwdev);
}

int hinic3_cmdqs_init(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cmdqs *cmdqs;
	void __iomem *db_base;
	u8 cmdq_type;
	int err;

	err = init_cmdqs(hwdev);
	if (err)
		goto err_out;

	cmdqs = hwdev->cmdqs;
	err = create_cmdq_wq(hwdev, cmdqs);
	if (err)
		goto err_free_cmdqs;

	err = hinic3_alloc_db_addr(hwdev, &db_base, NULL);
	if (err) {
		dev_err(hwdev->dev, "Failed to allocate doorbell address\n");
		goto err_destroy_cmdq_wq;
	}
	cmdqs->cmdqs_db_base = db_base;

	for (cmdq_type = 0; cmdq_type < cmdqs->cmdq_num; cmdq_type++) {
		err = init_cmdq(&cmdqs->cmdq[cmdq_type], hwdev, cmdq_type);
		if (err) {
			dev_err(hwdev->dev,
				"Failed to initialize cmdq type : %d\n",
				cmdq_type);
			goto err_free_cmd_infos;
		}
	}

	err = hinic3_set_cmdq_ctxts(hwdev);
	if (err)
		goto err_free_cmd_infos;

	return 0;

err_free_cmd_infos:
	while (cmdq_type > 0) {
		cmdq_type--;
		kfree(cmdqs->cmdq[cmdq_type].cmd_infos);
	}

	hinic3_free_db_addr(hwdev, cmdqs->cmdqs_db_base);

err_destroy_cmdq_wq:
	destroy_cmdq_wq(hwdev, cmdqs);

err_free_cmdqs:
	dma_pool_destroy(cmdqs->cmd_buf_pool);
	kfree(cmdqs);

err_out:
	return err;
}

void hinic3_cmdqs_free(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cmdqs *cmdqs = hwdev->cmdqs;
	u8 cmdq_type;

	cmdqs->status &= ~HINIC3_CMDQ_ENABLE;

	for (cmdq_type = 0; cmdq_type < cmdqs->cmdq_num; cmdq_type++) {
		hinic3_cmdq_flush_cmd(&cmdqs->cmdq[cmdq_type]);
		hinic3_cmdq_reset_all_cmd_buf(&cmdqs->cmdq[cmdq_type]);
		kfree(cmdqs->cmdq[cmdq_type].cmd_infos);
	}

	hinic3_free_db_addr(hwdev, cmdqs->cmdqs_db_base);
	destroy_cmdq_wq(hwdev, cmdqs);
	dma_pool_destroy(cmdqs->cmd_buf_pool);
	kfree(cmdqs);
}

bool hinic3_cmdq_idle(struct hinic3_cmdq *cmdq)
{
	return hinic3_wq_get_used(&cmdq->wq) == 0;
}
