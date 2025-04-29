/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_HFI_QUEUE_H__
#define __IRIS_HFI_QUEUE_H__

struct iris_core;

/*
 * Max 64 Buffers ( 32 input buffers and 32 output buffers)
 * can be queued by v4l2 framework at any given time.
 */
#define IFACEQ_MAX_BUF_COUNT		64
/*
 * Max session supported are 16.
 * this value is used to calcualte the size of
 * individual shared queue.
 */
#define IFACE_MAX_PARALLEL_SESSIONS	16
#define IFACEQ_DFLT_QHDR		0x0101
#define IFACEQ_MAX_PKT_SIZE		1024 /* Maximum size of a packet in the queue */

/*
 * SFR: Subsystem Failure Reason
 * when hardware goes into bad state/failure, firmware fills this memory
 * and driver will get to know the actual failure reason from this SFR buffer.
 */
#define SFR_SIZE			SZ_4K /* Iris hardware requires 4K queue alignment */

#define IFACEQ_QUEUE_SIZE		(IFACEQ_MAX_PKT_SIZE * \
					 IFACEQ_MAX_BUF_COUNT * IFACE_MAX_PARALLEL_SESSIONS)

/*
 * Memory layout of the shared queues:
 *
 *   ||=================||  ^        ^         ^
 *   ||                 ||  |        |         |
 *   ||    Queue Table  || 288 Bytes |         |
 *   ||      Header     ||  |        |         |
 *   ||                 ||  |        |         |
 *   ||-----------------||  V        |         |
 *   ||-----------------||  ^        |         |
 *   ||                 ||  |        |         |
 *   ||  Command Queue  || 56 Bytes  |         |
 *   ||     Header      ||  |        |         |
 *   ||                 ||  |        |         |
 *   ||-----------------||  V       456 Bytes  |
 *   ||-----------------||  ^        |         |
 *   ||                 ||  |        |         |
 *   ||  Message Queue  || 56 Bytes  |         |
 *   ||     Header      ||  |        |         |
 *   ||                 ||  |        |         |
 *   ||-----------------||  V        |         Buffer size aligned to 4k
 *   ||-----------------||  ^        |         Overall Queue Size = 2,404 KB
 *   ||                 ||  |        |         |
 *   ||   Debug Queue   || 56 Bytes  |         |
 *   ||     Header      ||  |        |         |
 *   ||                 ||  |        |         |
 *   ||=================||  V        V         |
 *   ||=================||           ^         |
 *   ||                 ||           |         |
 *   ||     Command     ||         800 KB      |
 *   ||      Queue      ||           |         |
 *   ||                 ||           |         |
 *   ||=================||           V         |
 *   ||=================||           ^         |
 *   ||                 ||           |         |
 *   ||     Message     ||         800 KB      |
 *   ||      Queue      ||           |         |
 *   ||                 ||           |         |
 *   ||=================||           V         |
 *   ||=================||           ^         |
 *   ||                 ||           |         |
 *   ||      Debug      ||         800 KB      |
 *   ||      Queue      ||           |         |
 *   ||                 ||           |         |
 *   ||=================||           V         |
 *   ||                 ||                     |
 *   ||=================||                     V
 */

/*
 * Shared queues are used for communication between driver and firmware.
 * There are 3 types of queues:
 * Command queue - driver to write any command to firmware.
 * Message queue - firmware to send any response to driver.
 * Debug queue - firmware to write debug message.
 */

/* Host-firmware shared queue ids */
enum iris_iface_queue {
	IFACEQ_CMDQ_ID,
	IFACEQ_MSGQ_ID,
	IFACEQ_DBGQ_ID,
	IFACEQ_NUMQ, /* not an index */
};

/**
 * struct iris_hfi_queue_header
 *
 * @status: Queue status, bits (7:0), 0x1 - active, 0x0 - inactive
 * @start_addr: Queue start address in non cached memory
 * @queue_type: Queue ID
 * @header_type: Default queue header
 * @q_size: Queue size
 *		Number of queue packets if pkt_size is non-zero
 *		Queue size in bytes if pkt_size is zero
 * @pkt_size: Size of queue packet entries
 *		0x0: variable queue packet size
 *		non zero: size of queue packet entry, fixed
 * @pkt_drop_cnt: Number of packets dropped by sender
 * @rx_wm: Receiver watermark, applicable in event driven mode
 * @tx_wm: Sender watermark, applicable in event driven mode
 * @rx_req: Receiver sets this bit if queue is empty
 * @tx_req: Sender sets this bit if queue is full
 * @rx_irq_status: Receiver sets this bit and triggers an interrupt to
 *		the sender after packets are dequeued. Sender clears this bit
 * @tx_irq_status: Sender sets this bit and triggers an interrupt to
 *		the receiver after packets are queued. Receiver clears this bit
 * @read_idx: Index till where receiver has consumed the packets from the queue.
 * @write_idx: Index till where sender has written the packets into the queue.
 */
struct iris_hfi_queue_header {
	u32 status;
	u32 start_addr;
	u16 queue_type;
	u16 header_type;
	u32 q_size;
	u32 pkt_size;
	u32 pkt_drop_cnt;
	u32 rx_wm;
	u32 tx_wm;
	u32 rx_req;
	u32 tx_req;
	u32 rx_irq_status;
	u32 tx_irq_status;
	u32 read_idx;
	u32 write_idx;
};

/**
 * struct iris_hfi_queue_table_header
 *
 * @version: Queue table version number
 * @size: Queue table size from version to last parametr in qhdr entry
 * @qhdr0_offset: Offset to the start of first qhdr
 * @qhdr_size: Queue header size in bytes
 * @num_q: Total number of queues in Queue table
 * @num_active_q: Total number of active queues
 * @device_addr: Device address of the queue
 * @name: Queue name in characters
 * @q_hdr: Array of queue headers
 */
struct iris_hfi_queue_table_header {
	u32 version;
	u32 size;
	u32 qhdr0_offset;
	u32 qhdr_size;
	u32 num_q;
	u32 num_active_q;
	void *device_addr;
	char name[256]; /* NUL-terminated array of characters */
	struct iris_hfi_queue_header q_hdr[IFACEQ_NUMQ];
};

struct iris_iface_q_info {
	struct iris_hfi_queue_header *qhdr;
	dma_addr_t	device_addr;
	void		*kernel_vaddr;
};

int iris_hfi_queues_init(struct iris_core *core);
void iris_hfi_queues_deinit(struct iris_core *core);

int iris_hfi_queue_cmd_write_locked(struct iris_core *core, void *pkt, u32 pkt_size);
int iris_hfi_queue_cmd_write(struct iris_core *core, void *pkt, u32 pkt_size);
int iris_hfi_queue_msg_read(struct iris_core *core, void *pkt);
int iris_hfi_queue_dbg_read(struct iris_core *core, void *pkt);

#endif
