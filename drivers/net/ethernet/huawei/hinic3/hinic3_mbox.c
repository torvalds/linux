// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/dma-mapping.h>

#include "hinic3_common.h"
#include "hinic3_csr.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"

#define MBOX_MSG_POLLING_TIMEOUT_MS  8000 // send msg seg timeout
#define MBOX_COMP_POLLING_TIMEOUT_MS 40000 // response

#define MBOX_MAX_BUF_SZ           2048
#define MBOX_HEADER_SZ            8

/* MBOX size is 64B, 8B for mbox_header, 8B reserved */
#define MBOX_SEG_LEN              48
#define MBOX_SEG_LEN_ALIGN        4
#define MBOX_WB_STATUS_LEN        16

#define MBOX_SEQ_ID_START_VAL     0
#define MBOX_SEQ_ID_MAX_VAL       42
#define MBOX_LAST_SEG_MAX_LEN  \
	(MBOX_MAX_BUF_SZ - MBOX_SEQ_ID_MAX_VAL * MBOX_SEG_LEN)

#define MBOX_DMA_MSG_QUEUE_DEPTH    32
#define MBOX_AREA(hwif)  \
	((hwif)->cfg_regs_base + HINIC3_FUNC_CSR_MAILBOX_DATA_OFF)

#define MBOX_MQ_CI_OFFSET  \
	(HINIC3_CFG_REGS_FLAG + HINIC3_FUNC_CSR_MAILBOX_DATA_OFF + \
	 MBOX_HEADER_SZ + MBOX_SEG_LEN)

#define MBOX_MQ_SYNC_CI_MASK   GENMASK(7, 0)
#define MBOX_MQ_ASYNC_CI_MASK  GENMASK(15, 8)
#define MBOX_MQ_CI_GET(val, field)  \
	FIELD_GET(MBOX_MQ_##field##_CI_MASK, val)

#define MBOX_MGMT_FUNC_ID         0x1FFF
#define MBOX_COMM_F_MBOX_SEGMENT  BIT(3)

static u8 *get_mobx_body_from_hdr(u8 *header)
{
	return header + MBOX_HEADER_SZ;
}

static struct hinic3_msg_desc *get_mbox_msg_desc(struct hinic3_mbox *mbox,
						 enum mbox_msg_direction_type dir,
						 u16 src_func_id)
{
	struct hinic3_msg_channel *msg_ch;

	msg_ch = (src_func_id == MBOX_MGMT_FUNC_ID) ?
		&mbox->mgmt_msg : mbox->func_msg;

	return (dir == MBOX_MSG_SEND) ?
		&msg_ch->recv_msg : &msg_ch->resp_msg;
}

static void resp_mbox_handler(struct hinic3_mbox *mbox,
			      const struct hinic3_msg_desc *msg_desc)
{
	spin_lock(&mbox->mbox_lock);
	if (msg_desc->msg_info.msg_id == mbox->send_msg_id &&
	    mbox->event_flag == MBOX_EVENT_START)
		mbox->event_flag = MBOX_EVENT_SUCCESS;
	spin_unlock(&mbox->mbox_lock);
}

static bool mbox_segment_valid(struct hinic3_mbox *mbox,
			       struct hinic3_msg_desc *msg_desc,
			       __le64 mbox_header)
{
	u8 seq_id, seg_len, msg_id, mod;
	__le16 src_func_idx, cmd;

	seq_id = MBOX_MSG_HEADER_GET(mbox_header, SEQID);
	seg_len = MBOX_MSG_HEADER_GET(mbox_header, SEG_LEN);
	msg_id = MBOX_MSG_HEADER_GET(mbox_header, MSG_ID);
	mod = MBOX_MSG_HEADER_GET(mbox_header, MODULE);
	cmd = cpu_to_le16(MBOX_MSG_HEADER_GET(mbox_header, CMD));
	src_func_idx = cpu_to_le16(MBOX_MSG_HEADER_GET(mbox_header,
						       SRC_GLB_FUNC_IDX));

	if (seq_id > MBOX_SEQ_ID_MAX_VAL || seg_len > MBOX_SEG_LEN ||
	    (seq_id == MBOX_SEQ_ID_MAX_VAL && seg_len > MBOX_LAST_SEG_MAX_LEN))
		goto err_seg;

	if (seq_id == 0) {
		msg_desc->seq_id = seq_id;
		msg_desc->msg_info.msg_id = msg_id;
		msg_desc->mod = mod;
		msg_desc->cmd = cmd;
	} else {
		if (seq_id != msg_desc->seq_id + 1 ||
		    msg_id != msg_desc->msg_info.msg_id ||
		    mod != msg_desc->mod || cmd != msg_desc->cmd)
			goto err_seg;

		msg_desc->seq_id = seq_id;
	}

	return true;

err_seg:
	dev_err(mbox->hwdev->dev,
		"Mailbox segment check failed, src func id: 0x%x, front seg info: seq id: 0x%x, msg id: 0x%x, mod: 0x%x, cmd: 0x%x\n",
		src_func_idx, msg_desc->seq_id, msg_desc->msg_info.msg_id,
		msg_desc->mod, msg_desc->cmd);
	dev_err(mbox->hwdev->dev,
		"Current seg info: seg len: 0x%x, seq id: 0x%x, msg id: 0x%x, mod: 0x%x, cmd: 0x%x\n",
		seg_len, seq_id, msg_id, mod, cmd);

	return false;
}

static void recv_mbox_handler(struct hinic3_mbox *mbox,
			      u8 *header, struct hinic3_msg_desc *msg_desc)
{
	__le64 mbox_header = *((__force __le64 *)header);
	u8 *mbox_body = get_mobx_body_from_hdr(header);
	u8 seq_id, seg_len;
	int pos;

	if (!mbox_segment_valid(mbox, msg_desc, mbox_header)) {
		msg_desc->seq_id = MBOX_SEQ_ID_MAX_VAL;
		return;
	}

	seq_id = MBOX_MSG_HEADER_GET(mbox_header, SEQID);
	seg_len = MBOX_MSG_HEADER_GET(mbox_header, SEG_LEN);

	pos = seq_id * MBOX_SEG_LEN;
	memcpy(msg_desc->msg + pos, mbox_body, seg_len);

	if (!MBOX_MSG_HEADER_GET(mbox_header, LAST))
		return;

	msg_desc->msg_len = cpu_to_le16(MBOX_MSG_HEADER_GET(mbox_header,
							    MSG_LEN));
	msg_desc->msg_info.status = MBOX_MSG_HEADER_GET(mbox_header, STATUS);

	if (MBOX_MSG_HEADER_GET(mbox_header, DIRECTION) == MBOX_MSG_RESP)
		resp_mbox_handler(mbox, msg_desc);
}

void hinic3_mbox_func_aeqe_handler(struct hinic3_hwdev *hwdev, u8 *header,
				   u8 size)
{
	__le64 mbox_header = *((__force __le64 *)header);
	enum mbox_msg_direction_type dir;
	struct hinic3_msg_desc *msg_desc;
	struct hinic3_mbox *mbox;
	u16 src_func_id;

	mbox = hwdev->mbox;
	dir = MBOX_MSG_HEADER_GET(mbox_header, DIRECTION);
	src_func_id = MBOX_MSG_HEADER_GET(mbox_header, SRC_GLB_FUNC_IDX);
	msg_desc = get_mbox_msg_desc(mbox, dir, src_func_id);
	recv_mbox_handler(mbox, header, msg_desc);
}

static int init_mbox_dma_queue(struct hinic3_hwdev *hwdev,
			       struct mbox_dma_queue *mq)
{
	u32 size;

	mq->depth = MBOX_DMA_MSG_QUEUE_DEPTH;
	mq->prod_idx = 0;
	mq->cons_idx = 0;

	size = mq->depth * MBOX_MAX_BUF_SZ;
	mq->dma_buf_vaddr = dma_alloc_coherent(hwdev->dev, size,
					       &mq->dma_buf_paddr,
					       GFP_KERNEL);
	if (!mq->dma_buf_vaddr)
		return -ENOMEM;

	return 0;
}

static void uninit_mbox_dma_queue(struct hinic3_hwdev *hwdev,
				  struct mbox_dma_queue *mq)
{
	dma_free_coherent(hwdev->dev, mq->depth * MBOX_MAX_BUF_SZ,
			  mq->dma_buf_vaddr, mq->dma_buf_paddr);
}

static int hinic3_init_mbox_dma_queue(struct hinic3_mbox *mbox)
{
	u32 val;
	int err;

	err = init_mbox_dma_queue(mbox->hwdev, &mbox->sync_msg_queue);
	if (err)
		return err;

	err = init_mbox_dma_queue(mbox->hwdev, &mbox->async_msg_queue);
	if (err) {
		uninit_mbox_dma_queue(mbox->hwdev, &mbox->sync_msg_queue);
		return err;
	}

	val = hinic3_hwif_read_reg(mbox->hwdev->hwif, MBOX_MQ_CI_OFFSET);
	val &= ~MBOX_MQ_SYNC_CI_MASK;
	val &= ~MBOX_MQ_ASYNC_CI_MASK;
	hinic3_hwif_write_reg(mbox->hwdev->hwif, MBOX_MQ_CI_OFFSET, val);

	return 0;
}

static void hinic3_uninit_mbox_dma_queue(struct hinic3_mbox *mbox)
{
	uninit_mbox_dma_queue(mbox->hwdev, &mbox->sync_msg_queue);
	uninit_mbox_dma_queue(mbox->hwdev, &mbox->async_msg_queue);
}

static int alloc_mbox_msg_channel(struct hinic3_msg_channel *msg_ch)
{
	msg_ch->resp_msg.msg = kzalloc(MBOX_MAX_BUF_SZ, GFP_KERNEL);
	if (!msg_ch->resp_msg.msg)
		return -ENOMEM;

	msg_ch->recv_msg.msg = kzalloc(MBOX_MAX_BUF_SZ, GFP_KERNEL);
	if (!msg_ch->recv_msg.msg) {
		kfree(msg_ch->resp_msg.msg);
		return -ENOMEM;
	}

	msg_ch->resp_msg.seq_id = MBOX_SEQ_ID_MAX_VAL;
	msg_ch->recv_msg.seq_id = MBOX_SEQ_ID_MAX_VAL;

	return 0;
}

static void free_mbox_msg_channel(struct hinic3_msg_channel *msg_ch)
{
	kfree(msg_ch->recv_msg.msg);
	kfree(msg_ch->resp_msg.msg);
}

static int init_mgmt_msg_channel(struct hinic3_mbox *mbox)
{
	int err;

	err = alloc_mbox_msg_channel(&mbox->mgmt_msg);
	if (err) {
		dev_err(mbox->hwdev->dev, "Failed to alloc mgmt message channel\n");
		return err;
	}

	err = hinic3_init_mbox_dma_queue(mbox);
	if (err) {
		dev_err(mbox->hwdev->dev, "Failed to init mbox dma queue\n");
		free_mbox_msg_channel(&mbox->mgmt_msg);
		return err;
	}

	return 0;
}

static void uninit_mgmt_msg_channel(struct hinic3_mbox *mbox)
{
	hinic3_uninit_mbox_dma_queue(mbox);
	free_mbox_msg_channel(&mbox->mgmt_msg);
}

static int hinic3_init_func_mbox_msg_channel(struct hinic3_hwdev *hwdev)
{
	struct hinic3_mbox *mbox;
	int err;

	mbox = hwdev->mbox;
	mbox->func_msg = kzalloc(sizeof(*mbox->func_msg), GFP_KERNEL);
	if (!mbox->func_msg)
		return -ENOMEM;

	err = alloc_mbox_msg_channel(mbox->func_msg);
	if (err)
		goto err_free_func_msg;

	return 0;

err_free_func_msg:
	kfree(mbox->func_msg);
	mbox->func_msg = NULL;

	return err;
}

static void hinic3_uninit_func_mbox_msg_channel(struct hinic3_hwdev *hwdev)
{
	struct hinic3_mbox *mbox = hwdev->mbox;

	free_mbox_msg_channel(mbox->func_msg);
	kfree(mbox->func_msg);
	mbox->func_msg = NULL;
}

static void prepare_send_mbox(struct hinic3_mbox *mbox)
{
	struct hinic3_send_mbox *send_mbox = &mbox->send_mbox;

	send_mbox->data = MBOX_AREA(mbox->hwdev->hwif);
}

static int alloc_mbox_wb_status(struct hinic3_mbox *mbox)
{
	struct hinic3_send_mbox *send_mbox = &mbox->send_mbox;
	struct hinic3_hwdev *hwdev = mbox->hwdev;
	u32 addr_h, addr_l;

	send_mbox->wb_vaddr = dma_alloc_coherent(hwdev->dev,
						 MBOX_WB_STATUS_LEN,
						 &send_mbox->wb_paddr,
						 GFP_KERNEL);
	if (!send_mbox->wb_vaddr)
		return -ENOMEM;

	addr_h = upper_32_bits(send_mbox->wb_paddr);
	addr_l = lower_32_bits(send_mbox->wb_paddr);
	hinic3_hwif_write_reg(hwdev->hwif, HINIC3_FUNC_CSR_MAILBOX_RESULT_H_OFF,
			      addr_h);
	hinic3_hwif_write_reg(hwdev->hwif, HINIC3_FUNC_CSR_MAILBOX_RESULT_L_OFF,
			      addr_l);

	return 0;
}

static void free_mbox_wb_status(struct hinic3_mbox *mbox)
{
	struct hinic3_send_mbox *send_mbox = &mbox->send_mbox;
	struct hinic3_hwdev *hwdev = mbox->hwdev;

	hinic3_hwif_write_reg(hwdev->hwif, HINIC3_FUNC_CSR_MAILBOX_RESULT_H_OFF,
			      0);
	hinic3_hwif_write_reg(hwdev->hwif, HINIC3_FUNC_CSR_MAILBOX_RESULT_L_OFF,
			      0);

	dma_free_coherent(hwdev->dev, MBOX_WB_STATUS_LEN,
			  send_mbox->wb_vaddr, send_mbox->wb_paddr);
}

static int hinic3_mbox_pre_init(struct hinic3_hwdev *hwdev,
				struct hinic3_mbox *mbox)
{
	mbox->hwdev = hwdev;
	mutex_init(&mbox->mbox_send_lock);
	spin_lock_init(&mbox->mbox_lock);

	mbox->workq = create_singlethread_workqueue(HINIC3_MBOX_WQ_NAME);
	if (!mbox->workq) {
		dev_err(hwdev->dev, "Failed to initialize MBOX workqueue\n");
		return -ENOMEM;
	}
	hwdev->mbox = mbox;

	return 0;
}

int hinic3_init_mbox(struct hinic3_hwdev *hwdev)
{
	struct hinic3_mbox *mbox;
	int err;

	mbox = kzalloc(sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	err = hinic3_mbox_pre_init(hwdev, mbox);
	if (err)
		goto err_free_mbox;

	err = init_mgmt_msg_channel(mbox);
	if (err)
		goto err_destroy_workqueue;

	err = hinic3_init_func_mbox_msg_channel(hwdev);
	if (err)
		goto err_uninit_mgmt_msg_ch;

	err = alloc_mbox_wb_status(mbox);
	if (err) {
		dev_err(hwdev->dev, "Failed to alloc mbox write back status\n");
		goto err_uninit_func_mbox_msg_ch;
	}

	prepare_send_mbox(mbox);

	return 0;

err_uninit_func_mbox_msg_ch:
	hinic3_uninit_func_mbox_msg_channel(hwdev);

err_uninit_mgmt_msg_ch:
	uninit_mgmt_msg_channel(mbox);

err_destroy_workqueue:
	destroy_workqueue(mbox->workq);

err_free_mbox:
	kfree(mbox);

	return err;
}

void hinic3_free_mbox(struct hinic3_hwdev *hwdev)
{
	struct hinic3_mbox *mbox = hwdev->mbox;

	destroy_workqueue(mbox->workq);
	free_mbox_wb_status(mbox);
	hinic3_uninit_func_mbox_msg_channel(hwdev);
	uninit_mgmt_msg_channel(mbox);
	kfree(mbox);
}

int hinic3_send_mbox_to_mgmt(struct hinic3_hwdev *hwdev, u8 mod, u16 cmd,
			     const struct mgmt_msg_params *msg_params)
{
	/* Completed by later submission due to LoC limit. */
	return -EFAULT;
}
