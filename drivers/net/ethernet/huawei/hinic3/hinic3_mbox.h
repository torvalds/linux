/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_MBOX_H_
#define _HINIC3_MBOX_H_

#include <linux/bitfield.h>
#include <linux/mutex.h>

struct hinic3_hwdev;
struct mgmt_msg_params;

#define MBOX_MSG_HEADER_SRC_GLB_FUNC_IDX_MASK  GENMASK_ULL(12, 0)
#define MBOX_MSG_HEADER_STATUS_MASK            BIT_ULL(13)
#define MBOX_MSG_HEADER_SOURCE_MASK            BIT_ULL(15)
#define MBOX_MSG_HEADER_AEQ_ID_MASK            GENMASK_ULL(17, 16)
#define MBOX_MSG_HEADER_MSG_ID_MASK            GENMASK_ULL(21, 18)
#define MBOX_MSG_HEADER_CMD_MASK               GENMASK_ULL(31, 22)
#define MBOX_MSG_HEADER_MSG_LEN_MASK           GENMASK_ULL(42, 32)
#define MBOX_MSG_HEADER_MODULE_MASK            GENMASK_ULL(47, 43)
#define MBOX_MSG_HEADER_SEG_LEN_MASK           GENMASK_ULL(53, 48)
#define MBOX_MSG_HEADER_NO_ACK_MASK            BIT_ULL(54)
#define MBOX_MSG_HEADER_DATA_TYPE_MASK         BIT_ULL(55)
#define MBOX_MSG_HEADER_SEQID_MASK             GENMASK_ULL(61, 56)
#define MBOX_MSG_HEADER_LAST_MASK              BIT_ULL(62)
#define MBOX_MSG_HEADER_DIRECTION_MASK         BIT_ULL(63)

#define MBOX_MSG_HEADER_SET(val, member) \
	FIELD_PREP(MBOX_MSG_HEADER_##member##_MASK, val)
#define MBOX_MSG_HEADER_GET(val, member) \
	FIELD_GET(MBOX_MSG_HEADER_##member##_MASK, le64_to_cpu(val))

/* identifies if a segment belongs to a message or to a response. A VF is only
 * expected to send messages and receive responses. PF driver could receive
 * messages and send responses.
 */
enum mbox_msg_direction_type {
	MBOX_MSG_SEND = 0,
	MBOX_MSG_RESP = 1,
};

/* Indicates if mbox message expects a response (ack) or not */
enum mbox_msg_ack_type {
	MBOX_MSG_ACK    = 0,
	MBOX_MSG_NO_ACK = 1,
};

enum mbox_msg_data_type {
	MBOX_MSG_DATA_INLINE = 0,
	MBOX_MSG_DATA_DMA    = 1,
};

enum mbox_msg_src_type {
	MBOX_MSG_FROM_MBOX = 1,
};

enum mbox_msg_aeq_type {
	MBOX_MSG_AEQ_FOR_EVENT = 0,
	MBOX_MSG_AEQ_FOR_MBOX  = 1,
};

#define HINIC3_MBOX_WQ_NAME  "hinic3_mbox"

struct mbox_msg_info {
	u8 msg_id;
	u8 status;
};

struct hinic3_msg_desc {
	u8                   *msg;
	__le16               msg_len;
	u8                   seq_id;
	u8                   mod;
	__le16               cmd;
	struct mbox_msg_info msg_info;
};

struct hinic3_msg_channel {
	struct   hinic3_msg_desc resp_msg;
	struct   hinic3_msg_desc recv_msg;
};

struct hinic3_send_mbox {
	u8 __iomem *data;
	void       *wb_vaddr;
	dma_addr_t wb_paddr;
};

enum mbox_event_state {
	MBOX_EVENT_START   = 0,
	MBOX_EVENT_FAIL    = 1,
	MBOX_EVENT_SUCCESS = 2,
	MBOX_EVENT_TIMEOUT = 3,
	MBOX_EVENT_END     = 4,
};

struct mbox_dma_msg {
	__le32 xor;
	__le32 dma_addr_high;
	__le32 dma_addr_low;
	__le32 msg_len;
	__le64 rsvd;
};

struct mbox_dma_queue {
	void       *dma_buf_vaddr;
	dma_addr_t dma_buf_paddr;
	u16        depth;
	u16        prod_idx;
	u16        cons_idx;
};

struct hinic3_mbox {
	struct hinic3_hwdev       *hwdev;
	/* lock for send mbox message and ack message */
	struct mutex              mbox_send_lock;
	struct hinic3_send_mbox   send_mbox;
	struct mbox_dma_queue     sync_msg_queue;
	struct mbox_dma_queue     async_msg_queue;
	struct workqueue_struct   *workq;
	/* driver and MGMT CPU */
	struct hinic3_msg_channel mgmt_msg;
	/* VF to PF */
	struct hinic3_msg_channel *func_msg;
	u8                        send_msg_id;
	enum mbox_event_state     event_flag;
	/* lock for mbox event flag */
	spinlock_t                mbox_lock;
};

void hinic3_mbox_func_aeqe_handler(struct hinic3_hwdev *hwdev, u8 *header,
				   u8 size);
int hinic3_init_mbox(struct hinic3_hwdev *hwdev);
void hinic3_free_mbox(struct hinic3_hwdev *hwdev);

int hinic3_send_mbox_to_mgmt(struct hinic3_hwdev *hwdev, u8 mod, u16 cmd,
			     const struct mgmt_msg_params *msg_params);
int hinic3_send_mbox_to_mgmt_no_ack(struct hinic3_hwdev *hwdev, u8 mod, u16 cmd,
				    const struct mgmt_msg_params *msg_params);

#endif
