// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include "npu_hw_access.h"
#include "npu_mgr.h"
#include "npu_firmware.h"
#include "npu_hw.h"
#include "npu_host_ipc.h"

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
/* HFI IPC interface */
#define TX_HDR_TYPE 0x01000000
#define RX_HDR_TYPE 0x00010000
#define HFI_QTBL_STATUS_ENABLED 0x00000001

#define QUEUE_TBL_VERSION 0x87654321

/* -------------------------------------------------------------------------
 * Data Structures
 * -------------------------------------------------------------------------
 */
struct npu_queue_tuple {
	uint32_t size;
	uint32_t hdr;
	uint32_t start_offset;
};

static struct npu_queue_tuple npu_q_setup[6] = {
	{ 1024, IPC_QUEUE_CMD_HIGH_PRIORITY | TX_HDR_TYPE | RX_HDR_TYPE, 0},
	{ 4096, IPC_QUEUE_APPS_EXEC         | TX_HDR_TYPE | RX_HDR_TYPE, 0},
	{ 4096, IPC_QUEUE_DSP_EXEC          | TX_HDR_TYPE | RX_HDR_TYPE, 0},
	{ 4096, IPC_QUEUE_APPS_RSP          | TX_HDR_TYPE | RX_HDR_TYPE, 0},
	{ 4096, IPC_QUEUE_DSP_RSP           | TX_HDR_TYPE | RX_HDR_TYPE, 0},
	{ 1024, IPC_QUEUE_LOG               | TX_HDR_TYPE | RX_HDR_TYPE, 0},
};

/* -------------------------------------------------------------------------
 * File Scope Function Prototypes
 * -------------------------------------------------------------------------
 */
static int npu_host_ipc_init_hfi(struct npu_device *npu_dev);
static int npu_host_ipc_send_cmd_hfi(struct npu_device *npu_dev,
		uint32_t q_idx, void *cmd_ptr);
static int npu_host_ipc_read_msg_hfi(struct npu_device *npu_dev,
		uint32_t q_idx, uint32_t *msg_ptr);
static int ipc_queue_read(struct npu_device *npu_dev, uint32_t target_que,
				uint8_t *packet, uint8_t *is_tx_req_set);
static int ipc_queue_write(struct npu_device *npu_dev, uint32_t target_que,
				uint8_t *packet, uint8_t *is_rx_req_set);

/* -------------------------------------------------------------------------
 * Function Definitions
 * -------------------------------------------------------------------------
 */
static int npu_host_ipc_init_hfi(struct npu_device *npu_dev)
{
	int status = 0;
	struct hfi_queue_tbl_header *q_tbl_hdr = NULL;
	struct hfi_queue_header *q_hdr_arr = NULL;
	struct hfi_queue_header *q_hdr = NULL;
	void *q_tbl_addr = NULL;
	uint32_t reg_val = 0;
	uint32_t q_idx = 0;
	uint32_t q_tbl_size = sizeof(struct hfi_queue_tbl_header) +
		(NPU_HFI_NUMBER_OF_QS * sizeof(struct hfi_queue_header));
	uint32_t q_size = 0;
	uint32_t cur_start_offset = 0;

	reg_val = REGR(npu_dev, REG_NPU_FW_CTRL_STATUS);

	/*
	 * If the firmware is already running and we're just attaching,
	 * we do not need to do this
	 */
	if ((reg_val & FW_CTRL_STATUS_LOG_READY_VAL) != 0)
		return status;

	/* check for valid interface queue table start address */
	q_tbl_addr = kzalloc(q_tbl_size, GFP_KERNEL);
	if (q_tbl_addr == NULL)
		return -ENOMEM;

	/* retrieve interface queue table start address */
	q_tbl_hdr = q_tbl_addr;
	q_hdr_arr = (struct hfi_queue_header *)((uint8_t *)q_tbl_addr +
		sizeof(struct hfi_queue_tbl_header));

	/* initialize the interface queue table header */
	q_tbl_hdr->qtbl_version = QUEUE_TBL_VERSION;
	q_tbl_hdr->qtbl_size = q_tbl_size;
	q_tbl_hdr->qtbl_qhdr0_offset = sizeof(struct hfi_queue_tbl_header);
	q_tbl_hdr->qtbl_qhdr_size = sizeof(struct hfi_queue_header);
	q_tbl_hdr->qtbl_num_q = NPU_HFI_NUMBER_OF_QS;
	q_tbl_hdr->qtbl_num_active_q = NPU_HFI_NUMBER_OF_ACTIVE_QS;

	cur_start_offset = q_tbl_size;

	for (q_idx = IPC_QUEUE_CMD_HIGH_PRIORITY;
		q_idx <= IPC_QUEUE_LOG; q_idx++) {
		q_hdr = &q_hdr_arr[q_idx];
		/* queue is active */
		q_hdr->qhdr_status = 0x01;
		q_hdr->qhdr_start_offset = cur_start_offset;
		npu_q_setup[q_idx].start_offset = cur_start_offset;
		q_size = npu_q_setup[q_idx].size;
		q_hdr->qhdr_type = npu_q_setup[q_idx].hdr;
		/* in bytes */
		q_hdr->qhdr_q_size = q_size;
		/* variable size packets */
		q_hdr->qhdr_pkt_size = 0;
		q_hdr->qhdr_pkt_drop_cnt = 0;
		q_hdr->qhdr_rx_wm = 0x1;
		q_hdr->qhdr_tx_wm = 0x1;
		/* since queue is initially empty */
		q_hdr->qhdr_rx_req = 0x1;
		q_hdr->qhdr_tx_req = 0x0;
		/* not used */
		q_hdr->qhdr_rx_irq_status = 0;
		/* not used */
		q_hdr->qhdr_tx_irq_status = 0;
		q_hdr->qhdr_read_idx = 0;
		q_hdr->qhdr_write_idx = 0;
		cur_start_offset += q_size;
	}

	MEMW(npu_dev, IPC_ADDR, (uint8_t *)q_tbl_hdr, q_tbl_size);
	kfree(q_tbl_addr);
	/* Write in the NPU's address for where IPC starts */
	REGW(npu_dev, (uint32_t)REG_NPU_HOST_CTRL_VALUE,
		(uint32_t)IPC_MEM_OFFSET_FROM_SSTCM);
	/* Set value bit */
	reg_val = REGR(npu_dev, (uint32_t)REG_NPU_HOST_CTRL_STATUS);
	REGW(npu_dev, (uint32_t)REG_NPU_HOST_CTRL_STATUS, reg_val |
		HOST_CTRL_STATUS_IPC_ADDRESS_READY_VAL);
	return status;
}

static int npu_host_ipc_send_cmd_hfi(struct npu_device *npu_dev,
		uint32_t q_idx, void *cmd_ptr)
{
	int status = 0;
	uint8_t is_rx_req_set = 0;
	uint32_t retry_cnt = 5;

	status = ipc_queue_write(npu_dev, q_idx, (uint8_t *)cmd_ptr,
		&is_rx_req_set);

	if (status == -ENOSPC) {
		do {
			msleep(20);
			status = ipc_queue_write(npu_dev, q_idx,
				(uint8_t *)cmd_ptr, &is_rx_req_set);
		} while ((status == -ENOSPC) && (--retry_cnt > 0));
	}

	if (status == 0) {
		if (is_rx_req_set == 1)
			status = INTERRUPT_RAISE_NPU(npu_dev);
	}

	if (status == 0)
		pr_debug("Cmd Msg put on Command Queue - SUCCESSS\n");
	else
		pr_err("Cmd Msg put on Command Queue - FAILURE\n");

	return status;
}

static int npu_host_ipc_read_msg_hfi(struct npu_device *npu_dev,
		uint32_t q_idx, uint32_t *msg_ptr)
{
	int status = 0;
	uint8_t is_tx_req_set;

	status = ipc_queue_read(npu_dev, q_idx, (uint8_t *)msg_ptr,
		&is_tx_req_set);

	if (status == 0) {
		/* raise interrupt if qhdr_tx_req is set */
		if (is_tx_req_set == 1)
			status = INTERRUPT_RAISE_NPU(npu_dev);
	}

	return status;
}

static int ipc_queue_read(struct npu_device *npu_dev,
			  uint32_t target_que, uint8_t *packet,
			  uint8_t *is_tx_req_set)
{
	int status = 0;
	struct hfi_queue_header queue;
	uint32_t packet_size, new_read_idx;
	size_t read_ptr;
	size_t offset = 0;

	offset = (size_t)IPC_ADDR + sizeof(struct hfi_queue_tbl_header) +
		target_que * sizeof(struct hfi_queue_header);

	if ((packet == NULL) || (is_tx_req_set == NULL))
		return -EINVAL;

	/* Read the queue */
	MEMR(npu_dev, (void *)((size_t)offset), (uint8_t *)&queue,
		HFI_QUEUE_HEADER_SIZE);

	if (queue.qhdr_type != npu_q_setup[target_que].hdr ||
		queue.qhdr_q_size != npu_q_setup[target_que].size ||
		queue.qhdr_read_idx >= queue.qhdr_q_size ||
		queue.qhdr_write_idx >= queue.qhdr_q_size ||
		queue.qhdr_start_offset !=
			npu_q_setup[target_que].start_offset) {
		pr_err("Invalid Queue header\n");
		status = -EIO;
		goto exit;
	}

	/* check if queue is empty */
	if (queue.qhdr_read_idx == queue.qhdr_write_idx) {
		/*
		 * set qhdr_rx_req, to inform the sender that the Interrupt
		 * needs to be raised with the next packet queued
		 */
		queue.qhdr_rx_req = 1;
		*is_tx_req_set = 0;
		status = -EPERM;
		goto exit;
	}

	read_ptr = ((size_t)(size_t)IPC_ADDR +
		queue.qhdr_start_offset + queue.qhdr_read_idx);

	/* Read packet size */
	MEMR(npu_dev, (void *)((size_t)read_ptr), packet, 4);
	packet_size = *((uint32_t *)packet);

	pr_debug("target_que: %d, packet_size: %d\n",
			target_que,
			packet_size);

	if ((packet_size == 0) ||
		(packet_size > NPU_IPC_BUF_LENGTH)) {
		pr_err("Invalid packet size %d\n", packet_size);
		status = -EINVAL;
		goto exit;
	}
	new_read_idx = queue.qhdr_read_idx + packet_size;

	if (new_read_idx < (queue.qhdr_q_size)) {
		MEMR(npu_dev, (void *)((size_t)read_ptr), packet, packet_size);
	} else {
		new_read_idx -= (queue.qhdr_q_size);

		MEMR(npu_dev, (void *)((size_t)read_ptr), packet,
			packet_size - new_read_idx);

		MEMR(npu_dev, (void *)((size_t)IPC_ADDR +
			queue.qhdr_start_offset),
			(void *)((size_t)packet + (packet_size-new_read_idx)),
			new_read_idx);
	}

	queue.qhdr_read_idx = new_read_idx;

	if (queue.qhdr_read_idx == queue.qhdr_write_idx)
		/*
		 * receiver wants an interrupt from transmitter
		 * (when next item queued) because queue is empty
		 */
		queue.qhdr_rx_req = 1;
	else
		/* clear qhdr_rx_req since the queue is not empty */
		queue.qhdr_rx_req = 0;

	if (queue.qhdr_tx_req == 1)
		/* transmitter requested an interrupt */
		*is_tx_req_set = 1;
	else
		*is_tx_req_set = 0;
exit:
	/* Update RX interrupt request -- queue.qhdr_rx_req */
	MEMW(npu_dev, (void *)((size_t)offset +
		(uint32_t)((size_t)&(queue.qhdr_rx_req) -
		(size_t)&queue)), (uint8_t *)&queue.qhdr_rx_req,
		sizeof(queue.qhdr_rx_req));
	/* Update Read pointer -- queue.qhdr_read_idx */
	MEMW(npu_dev, (void *)((size_t)offset + (uint32_t)(
		(size_t)&(queue.qhdr_read_idx) - (size_t)&queue)),
		(uint8_t *)&queue.qhdr_read_idx, sizeof(queue.qhdr_read_idx));

	return status;
}

static int ipc_queue_write(struct npu_device *npu_dev,
			uint32_t target_que, uint8_t *packet,
			uint8_t *is_rx_req_set)
{
	int status = 0;
	struct hfi_queue_header queue;
	uint32_t packet_size, new_write_idx;
	uint32_t empty_space;
	void *write_ptr;
	uint32_t read_idx;

	size_t offset = (size_t)IPC_ADDR +
		sizeof(struct hfi_queue_tbl_header) +
		target_que * sizeof(struct hfi_queue_header);

	if ((packet == NULL) || (is_rx_req_set == NULL))
		return -EINVAL;

	MEMR(npu_dev, (void *)((size_t)offset), (uint8_t *)&queue,
		HFI_QUEUE_HEADER_SIZE);

	if (queue.qhdr_type != npu_q_setup[target_que].hdr ||
		queue.qhdr_q_size != npu_q_setup[target_que].size ||
		queue.qhdr_read_idx >= queue.qhdr_q_size ||
		queue.qhdr_write_idx >= queue.qhdr_q_size ||
		queue.qhdr_start_offset !=
			npu_q_setup[target_que].start_offset) {
		pr_err("Invalid Queue header\n");
		status = -EIO;
		goto exit;
	}

	packet_size = (*(uint32_t *)packet);
	if (packet_size == 0) {
		/* assign failed status and return */
		status = -EPERM;
		goto exit;
	}

	/* sample Read Idx */
	read_idx = queue.qhdr_read_idx;

	/* Calculate Empty Space(UWord32) in the Queue */
	empty_space = (queue.qhdr_write_idx >= read_idx) ?
		((queue.qhdr_q_size) - (queue.qhdr_write_idx - read_idx)) :
		(read_idx - queue.qhdr_write_idx);

	if (empty_space <= packet_size) {
		/*
		 * If Queue is FULL/ no space for message
		 * set qhdr_tx_req.
		 */
		queue.qhdr_tx_req = 1;

		/*
		 * Queue is FULL, force raise an interrupt to Receiver
		 */
		*is_rx_req_set = 1;

		status = -ENOSPC;
		goto exit;
	}

	/*
	 * clear qhdr_tx_req so that receiver does not raise an interrupt
	 * on reading packets from Queue, since there is space to write
	 * the next packet
	 */
	queue.qhdr_tx_req = 0;

	new_write_idx = (queue.qhdr_write_idx + packet_size);

	write_ptr = (void *)(size_t)((size_t)IPC_ADDR +
		queue.qhdr_start_offset + queue.qhdr_write_idx);

	if (new_write_idx < queue.qhdr_q_size) {
		MEMW(npu_dev, (void *)((size_t)write_ptr), (uint8_t *)packet,
			packet_size);
	} else {
		/* wraparound case */
		new_write_idx -= (queue.qhdr_q_size);

		MEMW(npu_dev, (void *)((size_t)write_ptr), (uint8_t *)packet,
			packet_size - new_write_idx);

		MEMW(npu_dev, (void *)((size_t)((size_t)IPC_ADDR +
			queue.qhdr_start_offset)), (uint8_t *)(packet +
			(packet_size - new_write_idx)), new_write_idx);
	}

	/* Update qhdr_write_idx */
	queue.qhdr_write_idx = new_write_idx;

	*is_rx_req_set = (queue.qhdr_rx_req == 1) ? 1 : 0;

	/* Update Write pointer -- queue.qhdr_write_idx */
exit:
	/* Update TX request -- queue.qhdr_tx_req */
	MEMW(npu_dev, (void *)((size_t)(offset + (uint32_t)(
		(size_t)&(queue.qhdr_tx_req) - (size_t)&queue))),
		&queue.qhdr_tx_req, sizeof(queue.qhdr_tx_req));
	MEMW(npu_dev, (void *)((size_t)(offset + (uint32_t)(
		(size_t)&(queue.qhdr_write_idx) - (size_t)&queue))),
		&queue.qhdr_write_idx, sizeof(queue.qhdr_write_idx));

	return status;
}

/* -------------------------------------------------------------------------
 * IPC Interface functions
 * -------------------------------------------------------------------------
 */
int npu_host_ipc_send_cmd(struct npu_device *npu_dev, uint32_t q_idx,
		void *cmd_ptr)
{
	return npu_host_ipc_send_cmd_hfi(npu_dev, q_idx, cmd_ptr);
}

int npu_host_ipc_read_msg(struct npu_device *npu_dev, uint32_t q_idx,
		      uint32_t *msg_ptr)
{
	return npu_host_ipc_read_msg_hfi(npu_dev, q_idx, msg_ptr);
}

int npu_host_ipc_pre_init(struct npu_device *npu_dev)
{
	return npu_host_ipc_init_hfi(npu_dev);
}

int npu_host_ipc_post_init(struct npu_device *npu_dev)
{
	return 0;
}
