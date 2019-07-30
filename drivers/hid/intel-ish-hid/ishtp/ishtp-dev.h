/*
 * Most ISHTP provider device and ISHTP logic declarations
 *
 * Copyright (c) 2003-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#ifndef _ISHTP_DEV_H_
#define _ISHTP_DEV_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include "bus.h"
#include "hbm.h"

#define	IPC_PAYLOAD_SIZE	128
#define ISHTP_RD_MSG_BUF_SIZE	IPC_PAYLOAD_SIZE
#define	IPC_FULL_MSG_SIZE	132

/* Number of messages to be held in ISR->BH FIFO */
#define	RD_INT_FIFO_SIZE	64

/*
 * Number of IPC messages to be held in Tx FIFO, to be sent by ISR -
 * Tx complete interrupt or RX_COMPLETE handler
 */
#define	IPC_TX_FIFO_SIZE	512

/*
 * Number of Maximum ISHTP Clients
 */
#define ISHTP_CLIENTS_MAX 256

/*
 * Number of File descriptors/handles
 * that can be opened to the driver.
 *
 * Limit to 255: 256 Total Clients
 * minus internal client for ISHTP Bus Messages
 */
#define ISHTP_MAX_OPEN_HANDLE_COUNT (ISHTP_CLIENTS_MAX - 1)

/* Internal Clients Number */
#define ISHTP_HOST_CLIENT_ID_ANY		(-1)
#define ISHTP_HBM_HOST_CLIENT_ID		0

#define	MAX_DMA_DELAY	20

/* ISHTP device states */
enum ishtp_dev_state {
	ISHTP_DEV_INITIALIZING = 0,
	ISHTP_DEV_INIT_CLIENTS,
	ISHTP_DEV_ENABLED,
	ISHTP_DEV_RESETTING,
	ISHTP_DEV_DISABLED,
	ISHTP_DEV_POWER_DOWN,
	ISHTP_DEV_POWER_UP
};
const char *ishtp_dev_state_str(int state);

struct ishtp_cl;

/**
 * struct ishtp_fw_client - representation of fw client
 *
 * @props - client properties
 * @client_id - fw client id
 */
struct ishtp_fw_client {
	struct ishtp_client_properties props;
	uint8_t client_id;
};

/**
 * struct ishtp_msg_data - ISHTP message data struct
 * @size:	Size of data in the *data
 * @data:	Pointer to data
 */
struct ishtp_msg_data {
	uint32_t size;
	unsigned char *data;
};

/*
 * struct ishtp_cl_rb - request block structure
 * @list:	Link to list members
 * @cl:		ISHTP client instance
 * @buffer:	message header
 * @buf_idx:	Index into buffer
 * @read_time:	 unused at this time
 */
struct ishtp_cl_rb {
	struct list_head list;
	struct ishtp_cl *cl;
	struct ishtp_msg_data buffer;
	unsigned long buf_idx;
	unsigned long read_time;
};

/*
 * Control info for IPC messages ISHTP/IPC sending FIFO -
 * list with inline data buffer
 * This structure will be filled with parameters submitted
 * by the caller glue layer
 * 'buf' may be pointing to the external buffer or to 'inline_data'
 * 'offset' will be initialized to 0 by submitting
 *
 * 'ipc_send_compl' is intended for use by clients that send fragmented
 * messages. When a fragment is sent down to IPC msg regs,
 * it will be called.
 * If it has more fragments to send, it will do it. With last fragment
 * it will send appropriate ISHTP "message-complete" flag.
 * It will remove the outstanding message
 * (mark outstanding buffer as available).
 * If counting flow control is in work and there are more flow control
 * credits, it can put the next client message queued in cl.
 * structure for IPC processing.
 *
 */
struct wr_msg_ctl_info {
	/* Will be called with 'ipc_send_compl_prm' as parameter */
	void (*ipc_send_compl)(void *);

	void *ipc_send_compl_prm;
	size_t length;
	struct list_head	link;
	unsigned char	inline_data[IPC_FULL_MSG_SIZE];
};

/*
 * The ISHTP layer talks to hardware IPC message using the following
 * callbacks
 */
struct ishtp_hw_ops {
	int	(*hw_reset)(struct ishtp_device *dev);
	int	(*ipc_reset)(struct ishtp_device *dev);
	uint32_t (*ipc_get_header)(struct ishtp_device *dev, int length,
				   int busy);
	int	(*write)(struct ishtp_device *dev,
		void (*ipc_send_compl)(void *), void *ipc_send_compl_prm,
		unsigned char *msg, int length);
	uint32_t	(*ishtp_read_hdr)(const struct ishtp_device *dev);
	int	(*ishtp_read)(struct ishtp_device *dev, unsigned char *buffer,
			unsigned long buffer_length);
	uint32_t	(*get_fw_status)(struct ishtp_device *dev);
	void	(*sync_fw_clock)(struct ishtp_device *dev);
};

/**
 * struct ishtp_device - ISHTP private device struct
 */
struct ishtp_device {
	struct device *devc;	/* pointer to lowest device */
	struct pci_dev *pdev;	/* PCI device to get device ids */

	/* waitq for waiting for suspend response */
	wait_queue_head_t suspend_wait;
	bool suspend_flag;	/* Suspend is active */

	/* waitq for waiting for resume response */
	wait_queue_head_t resume_wait;
	bool resume_flag;	/*Resume is active */

	/*
	 * lock for the device, for everything that doesn't have
	 * a dedicated spinlock
	 */
	spinlock_t device_lock;

	bool recvd_hw_ready;
	struct hbm_version version;
	int transfer_path; /* Choice of transfer path: IPC or DMA */

	/* ishtp device states */
	enum ishtp_dev_state dev_state;
	enum ishtp_hbm_state hbm_state;

	/* driver read queue */
	struct ishtp_cl_rb read_list;
	spinlock_t read_list_spinlock;

	/* list of ishtp_cl's */
	struct list_head cl_list;
	spinlock_t cl_list_lock;
	long open_handle_count;

	/* List of bus devices */
	struct list_head device_list;
	spinlock_t device_list_lock;

	/* waiting queues for receive message from FW */
	wait_queue_head_t wait_hw_ready;
	wait_queue_head_t wait_hbm_recvd_msg;

	/* FIFO for input messages for BH processing */
	unsigned char rd_msg_fifo[RD_INT_FIFO_SIZE * IPC_PAYLOAD_SIZE];
	unsigned int rd_msg_fifo_head, rd_msg_fifo_tail;
	spinlock_t rd_msg_spinlock;
	struct work_struct bh_hbm_work;

	/* IPC write queue */
	struct list_head wr_processing_list, wr_free_list;
	/* For both processing list  and free list */
	spinlock_t wr_processing_spinlock;

	struct ishtp_fw_client *fw_clients; /*Note:memory has to be allocated*/
	DECLARE_BITMAP(fw_clients_map, ISHTP_CLIENTS_MAX);
	DECLARE_BITMAP(host_clients_map, ISHTP_CLIENTS_MAX);
	uint8_t fw_clients_num;
	uint8_t fw_client_presentation_num;
	uint8_t fw_client_index;
	spinlock_t fw_clients_lock;

	/* TX DMA buffers and slots */
	int ishtp_host_dma_enabled;
	void *ishtp_host_dma_tx_buf;
	unsigned int ishtp_host_dma_tx_buf_size;
	uint64_t ishtp_host_dma_tx_buf_phys;
	int ishtp_dma_num_slots;

	/* map of 4k blocks in Tx dma buf: 0-free, 1-used */
	uint8_t *ishtp_dma_tx_map;
	spinlock_t ishtp_dma_tx_lock;

	/* RX DMA buffers and slots */
	void *ishtp_host_dma_rx_buf;
	unsigned int ishtp_host_dma_rx_buf_size;
	uint64_t ishtp_host_dma_rx_buf_phys;

	/* Dump to trace buffers if enabled*/
	__printf(2, 3) void (*print_log)(struct ishtp_device *dev,
					 const char *format, ...);

	/* Debug stats */
	unsigned int	ipc_rx_cnt;
	unsigned long long	ipc_rx_bytes_cnt;
	unsigned int	ipc_tx_cnt;
	unsigned long long	ipc_tx_bytes_cnt;

	const struct ishtp_hw_ops *ops;
	size_t	mtu;
	uint32_t	ishtp_msg_hdr;
	char hw[0] __aligned(sizeof(void *));
};

static inline unsigned long ishtp_secs_to_jiffies(unsigned long sec)
{
	return msecs_to_jiffies(sec * MSEC_PER_SEC);
}

/*
 * Register Access Function
 */
static inline int ish_ipc_reset(struct ishtp_device *dev)
{
	return dev->ops->ipc_reset(dev);
}

static inline int ish_hw_reset(struct ishtp_device *dev)
{
	return dev->ops->hw_reset(dev);
}

/* Exported function */
void	ishtp_device_init(struct ishtp_device *dev);
int	ishtp_start(struct ishtp_device *dev);

#endif /*_ISHTP_DEV_H_*/
