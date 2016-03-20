/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Intel Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Intel SCIF driver.
 *
 */
#ifndef SCIF_NODEQP
#define SCIF_NODEQP

#include "scif_rb.h"
#include "scif_peer_bus.h"

#define SCIF_INIT 1  /* First message sent to the peer node for discovery */
#define SCIF_EXIT 2  /* Last message from the peer informing intent to exit */
#define SCIF_EXIT_ACK 3 /* Response to SCIF_EXIT message */
#define SCIF_NODE_ADD 4  /* Tell Online nodes a new node exits */
#define SCIF_NODE_ADD_ACK 5  /* Confirm to mgmt node sequence is finished */
#define SCIF_NODE_ADD_NACK 6 /* SCIF_NODE_ADD failed */
#define SCIF_NODE_REMOVE 7 /* Request to deactivate a SCIF node */
#define SCIF_NODE_REMOVE_ACK 8 /* Response to a SCIF_NODE_REMOVE message */
#define SCIF_CNCT_REQ 9  /* Phys addr of Request connection to a port */
#define SCIF_CNCT_GNT 10  /* Phys addr of new Grant connection request */
#define SCIF_CNCT_GNTACK 11  /* Error type Reject a connection request */
#define SCIF_CNCT_GNTNACK 12  /* Error type Reject a connection request */
#define SCIF_CNCT_REJ 13  /* Error type Reject a connection request */
#define SCIF_DISCNCT 14 /* Notify peer that connection is being terminated */
#define SCIF_DISCNT_ACK 15 /* Notify peer that connection is being terminated */
#define SCIF_CLIENT_SENT 16 /* Notify the peer that data has been written */
#define SCIF_CLIENT_RCVD 17 /* Notify the peer that data has been read */
#define SCIF_GET_NODE_INFO 18 /* Get current node mask from the mgmt node*/
#define SCIF_REGISTER 19 /* Tell peer about a new registered window */
#define SCIF_REGISTER_ACK 20 /* Notify peer about unregistration success */
#define SCIF_REGISTER_NACK 21 /* Notify peer about registration success */
#define SCIF_UNREGISTER 22 /* Tell peer about unregistering a window */
#define SCIF_UNREGISTER_ACK 23 /* Notify peer about registration failure */
#define SCIF_UNREGISTER_NACK 24 /* Notify peer about unregistration failure */
#define SCIF_ALLOC_REQ 25 /* Request a mapped buffer */
#define SCIF_ALLOC_GNT 26 /* Notify peer about allocation success */
#define SCIF_ALLOC_REJ 27 /* Notify peer about allocation failure */
#define SCIF_FREE_VIRT 28 /* Free previously allocated virtual memory */
#define SCIF_MUNMAP 29 /* Acknowledgment for a SCIF_MMAP request */
#define SCIF_MARK 30 /* SCIF Remote Fence Mark Request */
#define SCIF_MARK_ACK 31 /* SCIF Remote Fence Mark Success */
#define SCIF_MARK_NACK 32 /* SCIF Remote Fence Mark Failure */
#define SCIF_WAIT 33 /* SCIF Remote Fence Wait Request */
#define SCIF_WAIT_ACK 34 /* SCIF Remote Fence Wait Success */
#define SCIF_WAIT_NACK 35 /* SCIF Remote Fence Wait Failure */
#define SCIF_SIG_LOCAL 36 /* SCIF Remote Fence Local Signal Request */
#define SCIF_SIG_REMOTE 37 /* SCIF Remote Fence Remote Signal Request */
#define SCIF_SIG_ACK 38 /* SCIF Remote Fence Remote Signal Success */
#define SCIF_SIG_NACK 39 /* SCIF Remote Fence Remote Signal Failure */
#define SCIF_MAX_MSG SCIF_SIG_NACK

/*
 * struct scifmsg - Node QP message format
 *
 * @src: Source information
 * @dst: Destination information
 * @uop: The message opcode
 * @payload: Unique payload format for each message
 */
struct scifmsg {
	struct scif_port_id src;
	struct scif_port_id dst;
	u32 uop;
	u64 payload[4];
} __packed;

/*
 * struct scif_allocmsg - Used with SCIF_ALLOC_REQ to request
 * the remote note to allocate memory
 *
 * phys_addr: Physical address of the buffer
 * vaddr: Virtual address of the buffer
 * size: Size of the buffer
 * state: Current state
 * allocwq: wait queue for status
 */
struct scif_allocmsg {
	dma_addr_t phys_addr;
	unsigned long vaddr;
	size_t size;
	enum scif_msg_state state;
	wait_queue_head_t allocwq;
};

/*
 * struct scif_qp - Node Queue Pair
 *
 * Interesting structure -- a little difficult because we can only
 * write across the PCIe, so any r/w pointer we need to read is
 * local. We only need to read the read pointer on the inbound_q
 * and read the write pointer in the outbound_q
 *
 * @magic: Magic value to ensure the peer sees the QP correctly
 * @outbound_q: The outbound ring buffer for sending messages
 * @inbound_q: The inbound ring buffer for receiving messages
 * @local_write: Local write index
 * @local_read: Local read index
 * @remote_qp: The remote queue pair
 * @local_buf: DMA address of local ring buffer
 * @local_qp: DMA address of the local queue pair data structure
 * @remote_buf: DMA address of remote ring buffer
 * @qp_state: QP state i.e. online or offline used for P2P
 * @send_lock: synchronize access to outbound queue
 * @recv_lock: Synchronize access to inbound queue
 */
struct scif_qp {
	u64 magic;
#define SCIFEP_MAGIC 0x5c1f000000005c1fULL
	struct scif_rb outbound_q;
	struct scif_rb inbound_q;

	u32 local_write __aligned(64);
	u32 local_read __aligned(64);
	struct scif_qp *remote_qp;
	dma_addr_t local_buf;
	dma_addr_t local_qp;
	dma_addr_t remote_buf;
	u32 qp_state;
#define SCIF_QP_OFFLINE 0xdead
#define SCIF_QP_ONLINE 0xc0de
	spinlock_t send_lock;
	spinlock_t recv_lock;
};

/*
 * struct scif_loopb_msg - An element in the loopback Node QP message list.
 *
 * @msg - The SCIF node QP message
 * @list - link in the list of messages
 */
struct scif_loopb_msg {
	struct scifmsg msg;
	struct list_head list;
};

int scif_nodeqp_send(struct scif_dev *scifdev, struct scifmsg *msg);
int _scif_nodeqp_send(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_nodeqp_intrhandler(struct scif_dev *scifdev, struct scif_qp *qp);
int scif_loopb_msg_handler(struct scif_dev *scifdev, struct scif_qp *qp);
int scif_setup_qp(struct scif_dev *scifdev);
int scif_qp_response(phys_addr_t phys, struct scif_dev *dev);
int scif_setup_qp_connect(struct scif_qp *qp, dma_addr_t *qp_offset,
			  int local_size, struct scif_dev *scifdev);
int scif_setup_qp_accept(struct scif_qp *qp, dma_addr_t *qp_offset,
			 dma_addr_t phys, int local_size,
			 struct scif_dev *scifdev);
int scif_setup_qp_connect_response(struct scif_dev *scifdev,
				   struct scif_qp *qp, u64 payload);
int scif_setup_loopback_qp(struct scif_dev *scifdev);
int scif_destroy_loopback_qp(struct scif_dev *scifdev);
void scif_poll_qp_state(struct work_struct *work);
void scif_destroy_p2p(struct scif_dev *scifdev);
void scif_send_exit(struct scif_dev *scifdev);
static inline struct device *scif_get_peer_dev(struct scif_dev *scifdev)
{
	struct scif_peer_dev *spdev;
	struct device *spdev_ret;

	rcu_read_lock();
	spdev = rcu_dereference(scifdev->spdev);
	if (spdev)
		spdev_ret = get_device(&spdev->dev);
	else
		spdev_ret = ERR_PTR(-ENODEV);
	rcu_read_unlock();
	return spdev_ret;
}

static inline void scif_put_peer_dev(struct device *dev)
{
	put_device(dev);
}
#endif  /* SCIF_NODEQP */
