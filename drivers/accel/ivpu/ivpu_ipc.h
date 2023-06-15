/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_IPC_H__
#define __IVPU_IPC_H__

#include <linux/interrupt.h>
#include <linux/spinlock.h>

#include "vpu_jsm_api.h"

struct ivpu_bo;

/* VPU FW boot notification */
#define IVPU_IPC_CHAN_BOOT_MSG		0x3ff
#define IVPU_IPC_BOOT_MSG_DATA_ADDR	0x424f4f54

/* The alignment to be used for IPC Buffers and IPC Data. */
#define IVPU_IPC_ALIGNMENT	   64

#define IVPU_IPC_HDR_FREE	   0
#define IVPU_IPC_HDR_ALLOCATED	   1

/**
 * struct ivpu_ipc_hdr - The IPC message header structure, exchanged
 * with the VPU device firmware.
 * @data_addr: The VPU address of the payload (JSM message)
 * @data_size: The size of the payload.
 * @channel: The channel used.
 * @src_node: The Node ID of the sender.
 * @dst_node: The Node ID of the intended receiver.
 * @status: IPC buffer usage status
 */
struct ivpu_ipc_hdr {
	u32 data_addr;
	u32 data_size;
	u16 channel;
	u8 src_node;
	u8 dst_node;
	u8 status;
} __packed __aligned(IVPU_IPC_ALIGNMENT);

struct ivpu_ipc_consumer {
	struct list_head link;
	u32 channel;
	u32 tx_vpu_addr;
	u32 request_id;

	spinlock_t rx_msg_lock; /* Protects rx_msg_list */
	struct list_head rx_msg_list;
	wait_queue_head_t rx_msg_wq;
};

struct ivpu_ipc_info {
	struct gen_pool *mm_tx;
	struct ivpu_bo *mem_tx;
	struct ivpu_bo *mem_rx;

	atomic_t rx_msg_count;

	spinlock_t cons_list_lock; /* Protects cons_list */
	struct list_head cons_list;

	atomic_t request_id;
	struct mutex lock; /* Lock on status */
	bool on;
};

int ivpu_ipc_init(struct ivpu_device *vdev);
void ivpu_ipc_fini(struct ivpu_device *vdev);

void ivpu_ipc_enable(struct ivpu_device *vdev);
void ivpu_ipc_disable(struct ivpu_device *vdev);
void ivpu_ipc_reset(struct ivpu_device *vdev);

int ivpu_ipc_irq_handler(struct ivpu_device *vdev);

void ivpu_ipc_consumer_add(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons,
			   u32 channel);
void ivpu_ipc_consumer_del(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons);

int ivpu_ipc_receive(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons,
		     struct ivpu_ipc_hdr *ipc_buf, struct vpu_jsm_msg *ipc_payload,
		     unsigned long timeout_ms);

int ivpu_ipc_send_receive(struct ivpu_device *vdev, struct vpu_jsm_msg *req,
			  enum vpu_ipc_msg_type expected_resp_type,
			  struct vpu_jsm_msg *resp, u32 channel,
			  unsigned long timeout_ms);

#endif /* __IVPU_IPC_H__ */
