// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <net/devlink.h>
#include <asm/barrier.h>

#include "hinic_devlink.h"
#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"
#include "hinic_hw_api_cmd.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw_dev.h"

#define SYNC_MSG_ID_MASK                0x1FF

#define SYNC_MSG_ID(pf_to_mgmt)         ((pf_to_mgmt)->sync_msg_id)

#define SYNC_MSG_ID_INC(pf_to_mgmt)     (SYNC_MSG_ID(pf_to_mgmt) = \
					((SYNC_MSG_ID(pf_to_mgmt) + 1) & \
					 SYNC_MSG_ID_MASK))

#define MSG_SZ_IS_VALID(in_size)        ((in_size) <= MAX_MSG_LEN)

#define MGMT_MSG_LEN_MIN                20
#define MGMT_MSG_LEN_STEP               16
#define MGMT_MSG_RSVD_FOR_DEV           8

#define SEGMENT_LEN                     48

#define MAX_PF_MGMT_BUF_SIZE            2048

/* Data should be SEG LEN size aligned */
#define MAX_MSG_LEN                     2016

#define MSG_NOT_RESP                    0xFFFF

#define MGMT_MSG_TIMEOUT                5000

#define SET_FUNC_PORT_MBOX_TIMEOUT	30000

#define SET_FUNC_PORT_MGMT_TIMEOUT	25000

#define UPDATE_FW_MGMT_TIMEOUT		20000

#define mgmt_to_pfhwdev(pf_mgmt)        \
		container_of(pf_mgmt, struct hinic_pfhwdev, pf_to_mgmt)

enum msg_segment_type {
	NOT_LAST_SEGMENT = 0,
	LAST_SEGMENT     = 1,
};

enum mgmt_direction_type {
	MGMT_DIRECT_SEND = 0,
	MGMT_RESP        = 1,
};

enum msg_ack_type {
	MSG_ACK         = 0,
	MSG_NO_ACK      = 1,
};

/**
 * hinic_register_mgmt_msg_cb - register msg handler for a msg from a module
 * @pf_to_mgmt: PF to MGMT channel
 * @mod: module in the chip that this handler will handle its messages
 * @handle: private data for the callback
 * @callback: the handler that will handle messages
 **/
void hinic_register_mgmt_msg_cb(struct hinic_pf_to_mgmt *pf_to_mgmt,
				enum hinic_mod_type mod,
				void *handle,
				void (*callback)(void *handle,
						 u8 cmd, void *buf_in,
						 u16 in_size, void *buf_out,
						 u16 *out_size))
{
	struct hinic_mgmt_cb *mgmt_cb = &pf_to_mgmt->mgmt_cb[mod];

	mgmt_cb->cb = callback;
	mgmt_cb->handle = handle;
	mgmt_cb->state = HINIC_MGMT_CB_ENABLED;
}

/**
 * hinic_unregister_mgmt_msg_cb - unregister msg handler for a msg from a module
 * @pf_to_mgmt: PF to MGMT channel
 * @mod: module in the chip that this handler handles its messages
 **/
void hinic_unregister_mgmt_msg_cb(struct hinic_pf_to_mgmt *pf_to_mgmt,
				  enum hinic_mod_type mod)
{
	struct hinic_mgmt_cb *mgmt_cb = &pf_to_mgmt->mgmt_cb[mod];

	mgmt_cb->state &= ~HINIC_MGMT_CB_ENABLED;

	while (mgmt_cb->state & HINIC_MGMT_CB_RUNNING)
		schedule();

	mgmt_cb->cb = NULL;
}

/**
 * prepare_header - prepare the header of the message
 * @pf_to_mgmt: PF to MGMT channel
 * @msg_len: the length of the message
 * @mod: module in the chip that will get the message
 * @ack_type: ask for response
 * @direction: the direction of the message
 * @cmd: command of the message
 * @msg_id: message id
 *
 * Return the prepared header value
 **/
static u64 prepare_header(struct hinic_pf_to_mgmt *pf_to_mgmt,
			  u16 msg_len, enum hinic_mod_type mod,
			  enum msg_ack_type ack_type,
			  enum mgmt_direction_type direction,
			  u16 cmd, u16 msg_id)
{
	struct hinic_hwif *hwif = pf_to_mgmt->hwif;

	return HINIC_MSG_HEADER_SET(msg_len, MSG_LEN)           |
	       HINIC_MSG_HEADER_SET(mod, MODULE)                |
	       HINIC_MSG_HEADER_SET(SEGMENT_LEN, SEG_LEN)       |
	       HINIC_MSG_HEADER_SET(ack_type, NO_ACK)           |
	       HINIC_MSG_HEADER_SET(0, ASYNC_MGMT_TO_PF)        |
	       HINIC_MSG_HEADER_SET(0, SEQID)                   |
	       HINIC_MSG_HEADER_SET(LAST_SEGMENT, LAST)         |
	       HINIC_MSG_HEADER_SET(direction, DIRECTION)       |
	       HINIC_MSG_HEADER_SET(cmd, CMD)                   |
	       HINIC_MSG_HEADER_SET(HINIC_HWIF_PCI_INTF(hwif), PCI_INTF) |
	       HINIC_MSG_HEADER_SET(HINIC_HWIF_PF_IDX(hwif), PF_IDX)     |
	       HINIC_MSG_HEADER_SET(msg_id, MSG_ID);
}

/**
 * prepare_mgmt_cmd - prepare the mgmt command
 * @mgmt_cmd: pointer to the command to prepare
 * @header: pointer of the header for the message
 * @msg: the data of the message
 * @msg_len: the length of the message
 **/
static void prepare_mgmt_cmd(u8 *mgmt_cmd, u64 *header, u8 *msg, u16 msg_len)
{
	memset(mgmt_cmd, 0, MGMT_MSG_RSVD_FOR_DEV);

	mgmt_cmd += MGMT_MSG_RSVD_FOR_DEV;
	memcpy(mgmt_cmd, header, sizeof(*header));

	mgmt_cmd += sizeof(*header);
	memcpy(mgmt_cmd, msg, msg_len);
}

/**
 * mgmt_msg_len - calculate the total message length
 * @msg_data_len: the length of the message data
 *
 * Return the total message length
 **/
static u16 mgmt_msg_len(u16 msg_data_len)
{
	/* RSVD + HEADER_SIZE + DATA_LEN */
	u16 msg_len = MGMT_MSG_RSVD_FOR_DEV + sizeof(u64) + msg_data_len;

	if (msg_len > MGMT_MSG_LEN_MIN)
		msg_len = MGMT_MSG_LEN_MIN +
			   ALIGN((msg_len - MGMT_MSG_LEN_MIN),
				 MGMT_MSG_LEN_STEP);
	else
		msg_len = MGMT_MSG_LEN_MIN;

	return msg_len;
}

/**
 * send_msg_to_mgmt - send message to mgmt by API CMD
 * @pf_to_mgmt: PF to MGMT channel
 * @mod: module in the chip that will get the message
 * @cmd: command of the message
 * @data: the msg data
 * @data_len: the msg data length
 * @ack_type: ask for response
 * @direction: the direction of the original message
 * @resp_msg_id: msg id to response for
 *
 * Return 0 - Success, negative - Failure
 **/
static int send_msg_to_mgmt(struct hinic_pf_to_mgmt *pf_to_mgmt,
			    enum hinic_mod_type mod, u8 cmd,
			    u8 *data, u16 data_len,
			    enum msg_ack_type ack_type,
			    enum mgmt_direction_type direction,
			    u16 resp_msg_id)
{
	struct hinic_api_cmd_chain *chain;
	u64 header;
	u16 msg_id;

	msg_id = SYNC_MSG_ID(pf_to_mgmt);

	if (direction == MGMT_RESP) {
		header = prepare_header(pf_to_mgmt, data_len, mod, ack_type,
					direction, cmd, resp_msg_id);
	} else {
		SYNC_MSG_ID_INC(pf_to_mgmt);
		header = prepare_header(pf_to_mgmt, data_len, mod, ack_type,
					direction, cmd, msg_id);
	}

	prepare_mgmt_cmd(pf_to_mgmt->sync_msg_buf, &header, data, data_len);

	chain = pf_to_mgmt->cmd_chain[HINIC_API_CMD_WRITE_TO_MGMT_CPU];
	return hinic_api_cmd_write(chain, HINIC_NODE_ID_MGMT,
				   pf_to_mgmt->sync_msg_buf,
				   mgmt_msg_len(data_len));
}

/**
 * msg_to_mgmt_sync - send sync message to mgmt
 * @pf_to_mgmt: PF to MGMT channel
 * @mod: module in the chip that will get the message
 * @cmd: command of the message
 * @buf_in: the msg data
 * @in_size: the msg data length
 * @buf_out: response
 * @out_size: response length
 * @direction: the direction of the original message
 * @resp_msg_id: msg id to response for
 * @timeout: time-out period of waiting for response
 *
 * Return 0 - Success, negative - Failure
 **/
static int msg_to_mgmt_sync(struct hinic_pf_to_mgmt *pf_to_mgmt,
			    enum hinic_mod_type mod, u8 cmd,
			    u8 *buf_in, u16 in_size,
			    u8 *buf_out, u16 *out_size,
			    enum mgmt_direction_type direction,
			    u16 resp_msg_id, u32 timeout)
{
	struct hinic_hwif *hwif = pf_to_mgmt->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_recv_msg *recv_msg;
	struct completion *recv_done;
	unsigned long timeo;
	u16 msg_id;
	int err;

	/* Lock the sync_msg_buf */
	down(&pf_to_mgmt->sync_msg_lock);

	recv_msg = &pf_to_mgmt->recv_resp_msg_from_mgmt;
	recv_done = &recv_msg->recv_done;

	if (resp_msg_id == MSG_NOT_RESP)
		msg_id = SYNC_MSG_ID(pf_to_mgmt);
	else
		msg_id = resp_msg_id;

	init_completion(recv_done);

	err = send_msg_to_mgmt(pf_to_mgmt, mod, cmd, buf_in, in_size,
			       MSG_ACK, direction, resp_msg_id);
	if (err) {
		dev_err(&pdev->dev, "Failed to send sync msg to mgmt\n");
		goto unlock_sync_msg;
	}

	timeo = msecs_to_jiffies(timeout ? timeout : MGMT_MSG_TIMEOUT);

	if (!wait_for_completion_timeout(recv_done, timeo)) {
		dev_err(&pdev->dev, "MGMT timeout, MSG id = %d\n", msg_id);
		hinic_dump_aeq_info(pf_to_mgmt->hwdev);
		err = -ETIMEDOUT;
		goto unlock_sync_msg;
	}

	smp_rmb();      /* verify reading after completion */

	if (recv_msg->msg_id != msg_id) {
		dev_err(&pdev->dev, "incorrect MSG for id = %d\n", msg_id);
		err = -EFAULT;
		goto unlock_sync_msg;
	}

	if (buf_out && recv_msg->msg_len <= MAX_PF_MGMT_BUF_SIZE) {
		memcpy(buf_out, recv_msg->msg, recv_msg->msg_len);
		*out_size = recv_msg->msg_len;
	}

unlock_sync_msg:
	up(&pf_to_mgmt->sync_msg_lock);
	return err;
}

/**
 * msg_to_mgmt_async - send message to mgmt without response
 * @pf_to_mgmt: PF to MGMT channel
 * @mod: module in the chip that will get the message
 * @cmd: command of the message
 * @buf_in: the msg data
 * @in_size: the msg data length
 * @direction: the direction of the original message
 * @resp_msg_id: msg id to response for
 *
 * Return 0 - Success, negative - Failure
 **/
static int msg_to_mgmt_async(struct hinic_pf_to_mgmt *pf_to_mgmt,
			     enum hinic_mod_type mod, u8 cmd,
			     u8 *buf_in, u16 in_size,
			     enum mgmt_direction_type direction,
			     u16 resp_msg_id)
{
	int err;

	/* Lock the sync_msg_buf */
	down(&pf_to_mgmt->sync_msg_lock);

	err = send_msg_to_mgmt(pf_to_mgmt, mod, cmd, buf_in, in_size,
			       MSG_NO_ACK, direction, resp_msg_id);

	up(&pf_to_mgmt->sync_msg_lock);
	return err;
}

/**
 * hinic_msg_to_mgmt - send message to mgmt
 * @pf_to_mgmt: PF to MGMT channel
 * @mod: module in the chip that will get the message
 * @cmd: command of the message
 * @buf_in: the msg data
 * @in_size: the msg data length
 * @buf_out: response
 * @out_size: returned response length
 * @sync: sync msg or async msg
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_msg_to_mgmt(struct hinic_pf_to_mgmt *pf_to_mgmt,
		      enum hinic_mod_type mod, u8 cmd,
		      void *buf_in, u16 in_size, void *buf_out, u16 *out_size,
		      enum hinic_mgmt_msg_type sync)
{
	struct hinic_hwif *hwif = pf_to_mgmt->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u32 timeout = 0;

	if (sync != HINIC_MGMT_MSG_SYNC) {
		dev_err(&pdev->dev, "Invalid MGMT msg type\n");
		return -EINVAL;
	}

	if (!MSG_SZ_IS_VALID(in_size)) {
		dev_err(&pdev->dev, "Invalid MGMT msg buffer size\n");
		return -EINVAL;
	}

	if (HINIC_IS_VF(hwif)) {
		if (cmd == HINIC_PORT_CMD_SET_FUNC_STATE)
			timeout = SET_FUNC_PORT_MBOX_TIMEOUT;

		return hinic_mbox_to_pf(pf_to_mgmt->hwdev, mod, cmd, buf_in,
					in_size, buf_out, out_size, timeout);
	} else {
		if (cmd == HINIC_PORT_CMD_SET_FUNC_STATE)
			timeout = SET_FUNC_PORT_MGMT_TIMEOUT;
		else if (cmd == HINIC_PORT_CMD_UPDATE_FW)
			timeout = UPDATE_FW_MGMT_TIMEOUT;

		return msg_to_mgmt_sync(pf_to_mgmt, mod, cmd, buf_in, in_size,
				buf_out, out_size, MGMT_DIRECT_SEND,
				MSG_NOT_RESP, timeout);
	}
}

static void recv_mgmt_msg_work_handler(struct work_struct *work)
{
	struct hinic_mgmt_msg_handle_work *mgmt_work =
		container_of(work, struct hinic_mgmt_msg_handle_work, work);
	struct hinic_pf_to_mgmt *pf_to_mgmt = mgmt_work->pf_to_mgmt;
	struct pci_dev *pdev = pf_to_mgmt->hwif->pdev;
	u8 *buf_out = pf_to_mgmt->mgmt_ack_buf;
	struct hinic_mgmt_cb *mgmt_cb;
	unsigned long cb_state;
	u16 out_size = 0;

	memset(buf_out, 0, MAX_PF_MGMT_BUF_SIZE);

	if (mgmt_work->mod >= HINIC_MOD_MAX) {
		dev_err(&pdev->dev, "Unknown MGMT MSG module = %d\n",
			mgmt_work->mod);
		kfree(mgmt_work->msg);
		kfree(mgmt_work);
		return;
	}

	mgmt_cb = &pf_to_mgmt->mgmt_cb[mgmt_work->mod];

	cb_state = cmpxchg(&mgmt_cb->state,
			   HINIC_MGMT_CB_ENABLED,
			   HINIC_MGMT_CB_ENABLED | HINIC_MGMT_CB_RUNNING);

	if (cb_state == HINIC_MGMT_CB_ENABLED && mgmt_cb->cb)
		mgmt_cb->cb(mgmt_cb->handle, mgmt_work->cmd,
			    mgmt_work->msg, mgmt_work->msg_len,
			    buf_out, &out_size);
	else
		dev_err(&pdev->dev, "No MGMT msg handler, mod: %d, cmd: %d\n",
			mgmt_work->mod, mgmt_work->cmd);

	mgmt_cb->state &= ~HINIC_MGMT_CB_RUNNING;

	if (!mgmt_work->async_mgmt_to_pf)
		/* MGMT sent sync msg, send the response */
		msg_to_mgmt_async(pf_to_mgmt, mgmt_work->mod, mgmt_work->cmd,
				  buf_out, out_size, MGMT_RESP,
				  mgmt_work->msg_id);

	kfree(mgmt_work->msg);
	kfree(mgmt_work);
}

/**
 * mgmt_recv_msg_handler - handler for message from mgmt cpu
 * @pf_to_mgmt: PF to MGMT channel
 * @recv_msg: received message details
 **/
static void mgmt_recv_msg_handler(struct hinic_pf_to_mgmt *pf_to_mgmt,
				  struct hinic_recv_msg *recv_msg)
{
	struct hinic_mgmt_msg_handle_work *mgmt_work = NULL;

	mgmt_work = kzalloc(sizeof(*mgmt_work), GFP_KERNEL);
	if (!mgmt_work)
		return;

	if (recv_msg->msg_len) {
		mgmt_work->msg = kzalloc(recv_msg->msg_len, GFP_KERNEL);
		if (!mgmt_work->msg) {
			kfree(mgmt_work);
			return;
		}
	}

	mgmt_work->pf_to_mgmt = pf_to_mgmt;
	mgmt_work->msg_len = recv_msg->msg_len;
	memcpy(mgmt_work->msg, recv_msg->msg, recv_msg->msg_len);
	mgmt_work->msg_id = recv_msg->msg_id;
	mgmt_work->mod = recv_msg->mod;
	mgmt_work->cmd = recv_msg->cmd;
	mgmt_work->async_mgmt_to_pf = recv_msg->async_mgmt_to_pf;

	INIT_WORK(&mgmt_work->work, recv_mgmt_msg_work_handler);
	queue_work(pf_to_mgmt->workq, &mgmt_work->work);
}

/**
 * mgmt_resp_msg_handler - handler for a response message from mgmt cpu
 * @pf_to_mgmt: PF to MGMT channel
 * @recv_msg: received message details
 **/
static void mgmt_resp_msg_handler(struct hinic_pf_to_mgmt *pf_to_mgmt,
				  struct hinic_recv_msg *recv_msg)
{
	wmb();  /* verify writing all, before reading */

	complete(&recv_msg->recv_done);
}

/**
 * recv_mgmt_msg_handler - handler for a message from mgmt cpu
 * @pf_to_mgmt: PF to MGMT channel
 * @header: the header of the message
 * @recv_msg: received message details
 **/
static void recv_mgmt_msg_handler(struct hinic_pf_to_mgmt *pf_to_mgmt,
				  u64 *header, struct hinic_recv_msg *recv_msg)
{
	struct hinic_hwif *hwif = pf_to_mgmt->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int seq_id, seg_len;
	u8 *msg_body;

	seq_id = HINIC_MSG_HEADER_GET(*header, SEQID);
	seg_len = HINIC_MSG_HEADER_GET(*header, SEG_LEN);

	if (seq_id >= (MAX_MSG_LEN / SEGMENT_LEN)) {
		dev_err(&pdev->dev, "recv big mgmt msg\n");
		return;
	}

	msg_body = (u8 *)header + sizeof(*header);
	memcpy(recv_msg->msg + seq_id * SEGMENT_LEN, msg_body, seg_len);

	if (!HINIC_MSG_HEADER_GET(*header, LAST))
		return;

	recv_msg->cmd = HINIC_MSG_HEADER_GET(*header, CMD);
	recv_msg->mod = HINIC_MSG_HEADER_GET(*header, MODULE);
	recv_msg->async_mgmt_to_pf = HINIC_MSG_HEADER_GET(*header,
							  ASYNC_MGMT_TO_PF);
	recv_msg->msg_len = HINIC_MSG_HEADER_GET(*header, MSG_LEN);
	recv_msg->msg_id = HINIC_MSG_HEADER_GET(*header, MSG_ID);

	if (HINIC_MSG_HEADER_GET(*header, DIRECTION) == MGMT_RESP)
		mgmt_resp_msg_handler(pf_to_mgmt, recv_msg);
	else
		mgmt_recv_msg_handler(pf_to_mgmt, recv_msg);
}

/**
 * mgmt_msg_aeqe_handler - handler for a mgmt message event
 * @handle: PF to MGMT channel
 * @data: the header of the message
 * @size: unused
 **/
static void mgmt_msg_aeqe_handler(void *handle, void *data, u8 size)
{
	struct hinic_pf_to_mgmt *pf_to_mgmt = handle;
	struct hinic_recv_msg *recv_msg;
	u64 *header = (u64 *)data;

	recv_msg = HINIC_MSG_HEADER_GET(*header, DIRECTION) ==
		   MGMT_DIRECT_SEND ?
		   &pf_to_mgmt->recv_msg_from_mgmt :
		   &pf_to_mgmt->recv_resp_msg_from_mgmt;

	recv_mgmt_msg_handler(pf_to_mgmt, header, recv_msg);
}

/**
 * alloc_recv_msg - allocate receive message memory
 * @pf_to_mgmt: PF to MGMT channel
 * @recv_msg: pointer that will hold the allocated data
 *
 * Return 0 - Success, negative - Failure
 **/
static int alloc_recv_msg(struct hinic_pf_to_mgmt *pf_to_mgmt,
			  struct hinic_recv_msg *recv_msg)
{
	struct hinic_hwif *hwif = pf_to_mgmt->hwif;
	struct pci_dev *pdev = hwif->pdev;

	recv_msg->msg = devm_kzalloc(&pdev->dev, MAX_PF_MGMT_BUF_SIZE,
				     GFP_KERNEL);
	if (!recv_msg->msg)
		return -ENOMEM;

	recv_msg->buf_out = devm_kzalloc(&pdev->dev, MAX_PF_MGMT_BUF_SIZE,
					 GFP_KERNEL);
	if (!recv_msg->buf_out)
		return -ENOMEM;

	return 0;
}

/**
 * alloc_msg_buf - allocate all the message buffers of PF to MGMT channel
 * @pf_to_mgmt: PF to MGMT channel
 *
 * Return 0 - Success, negative - Failure
 **/
static int alloc_msg_buf(struct hinic_pf_to_mgmt *pf_to_mgmt)
{
	struct hinic_hwif *hwif = pf_to_mgmt->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int err;

	err = alloc_recv_msg(pf_to_mgmt,
			     &pf_to_mgmt->recv_msg_from_mgmt);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate recv msg\n");
		return err;
	}

	err = alloc_recv_msg(pf_to_mgmt,
			     &pf_to_mgmt->recv_resp_msg_from_mgmt);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate resp recv msg\n");
		return err;
	}

	pf_to_mgmt->sync_msg_buf = devm_kzalloc(&pdev->dev,
						MAX_PF_MGMT_BUF_SIZE,
						GFP_KERNEL);
	if (!pf_to_mgmt->sync_msg_buf)
		return -ENOMEM;

	pf_to_mgmt->mgmt_ack_buf = devm_kzalloc(&pdev->dev,
						MAX_PF_MGMT_BUF_SIZE,
						GFP_KERNEL);
	if (!pf_to_mgmt->mgmt_ack_buf)
		return -ENOMEM;

	return 0;
}

/**
 * hinic_pf_to_mgmt_init - initialize PF to MGMT channel
 * @pf_to_mgmt: PF to MGMT channel
 * @hwif: HW interface the PF to MGMT will use for accessing HW
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_pf_to_mgmt_init(struct hinic_pf_to_mgmt *pf_to_mgmt,
			  struct hinic_hwif *hwif)
{
	struct hinic_pfhwdev *pfhwdev = mgmt_to_pfhwdev(pf_to_mgmt);
	struct hinic_hwdev *hwdev = &pfhwdev->hwdev;
	struct pci_dev *pdev = hwif->pdev;
	int err;

	pf_to_mgmt->hwif = hwif;
	pf_to_mgmt->hwdev = hwdev;

	if (HINIC_IS_VF(hwif))
		return 0;

	err = hinic_health_reporters_create(hwdev->devlink_dev);
	if (err)
		return err;

	sema_init(&pf_to_mgmt->sync_msg_lock, 1);
	pf_to_mgmt->workq = create_singlethread_workqueue("hinic_mgmt");
	if (!pf_to_mgmt->workq) {
		dev_err(&pdev->dev, "Failed to initialize MGMT workqueue\n");
		hinic_health_reporters_destroy(hwdev->devlink_dev);
		return -ENOMEM;
	}
	pf_to_mgmt->sync_msg_id = 0;

	err = alloc_msg_buf(pf_to_mgmt);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate msg buffers\n");
		hinic_health_reporters_destroy(hwdev->devlink_dev);
		return err;
	}

	err = hinic_api_cmd_init(pf_to_mgmt->cmd_chain, hwif);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize cmd chains\n");
		hinic_health_reporters_destroy(hwdev->devlink_dev);
		return err;
	}

	hinic_aeq_register_hw_cb(&hwdev->aeqs, HINIC_MSG_FROM_MGMT_CPU,
				 pf_to_mgmt,
				 mgmt_msg_aeqe_handler);
	return 0;
}

/**
 * hinic_pf_to_mgmt_free - free PF to MGMT channel
 * @pf_to_mgmt: PF to MGMT channel
 **/
void hinic_pf_to_mgmt_free(struct hinic_pf_to_mgmt *pf_to_mgmt)
{
	struct hinic_pfhwdev *pfhwdev = mgmt_to_pfhwdev(pf_to_mgmt);
	struct hinic_hwdev *hwdev = &pfhwdev->hwdev;

	if (HINIC_IS_VF(hwdev->hwif))
		return;

	hinic_aeq_unregister_hw_cb(&hwdev->aeqs, HINIC_MSG_FROM_MGMT_CPU);
	hinic_api_cmd_free(pf_to_mgmt->cmd_chain);
	destroy_workqueue(pf_to_mgmt->workq);
	hinic_health_reporters_destroy(hwdev->devlink_dev);
}
