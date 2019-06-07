/*
 * ISHTP client logic
 *
 * Copyright (c) 2003-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _ISHTP_CLIENT_H_
#define _ISHTP_CLIENT_H_

#include <linux/types.h>
#include "ishtp-dev.h"

/* Tx and Rx ring size */
#define	CL_DEF_RX_RING_SIZE	2
#define	CL_DEF_TX_RING_SIZE	2
#define	CL_MAX_RX_RING_SIZE	32
#define	CL_MAX_TX_RING_SIZE	32

#define DMA_SLOT_SIZE		4096
/* Number of IPC fragments after which it's worth sending via DMA */
#define	DMA_WORTH_THRESHOLD	3

/* DMA/IPC Tx paths. Other the default means enforcement */
#define	CL_TX_PATH_DEFAULT	0
#define	CL_TX_PATH_IPC		1
#define	CL_TX_PATH_DMA		2

/* Client Tx buffer list entry */
struct ishtp_cl_tx_ring {
	struct list_head	list;
	struct ishtp_msg_data	send_buf;
};

/* ISHTP client instance */
struct ishtp_cl {
	struct list_head	link;
	struct ishtp_device	*dev;
	enum cl_state		state;
	int			status;

	/* Link to ISHTP bus device */
	struct ishtp_cl_device	*device;

	/* ID of client connected */
	uint8_t	host_client_id;
	uint8_t	fw_client_id;
	uint8_t	ishtp_flow_ctrl_creds;
	uint8_t	out_flow_ctrl_creds;

	/* dma */
	int	last_tx_path;
	/* 0: ack wasn't received,1:ack was received */
	int	last_dma_acked;
	unsigned char	*last_dma_addr;
	/* 0: ack wasn't received,1:ack was received */
	int	last_ipc_acked;

	/* Rx ring buffer pool */
	unsigned int	rx_ring_size;
	struct ishtp_cl_rb	free_rb_list;
	spinlock_t	free_list_spinlock;
	/* Rx in-process list */
	struct ishtp_cl_rb	in_process_list;
	spinlock_t	in_process_spinlock;

	/* Client Tx buffers list */
	unsigned int	tx_ring_size;
	struct ishtp_cl_tx_ring	tx_list, tx_free_list;
	int		tx_ring_free_size;
	spinlock_t	tx_list_spinlock;
	spinlock_t	tx_free_list_spinlock;
	size_t	tx_offs;	/* Offset in buffer at head of 'tx_list' */

	/**
	 * if we get a FC, and the list is not empty, we must know whether we
	 * are at the middle of sending.
	 * if so -need to increase FC counter, otherwise, need to start sending
	 * the first msg in list
	 * (!)This is for counting-FC implementation only. Within single-FC the
	 * other party may NOT send FC until it receives complete message
	 */
	int	sending;

	/* Send FC spinlock */
	spinlock_t	fc_spinlock;

	/* wait queue for connect and disconnect response from FW */
	wait_queue_head_t	wait_ctrl_res;

	/* Error stats */
	unsigned int	err_send_msg;
	unsigned int	err_send_fc;

	/* Send/recv stats */
	unsigned int	send_msg_cnt_ipc;
	unsigned int	send_msg_cnt_dma;
	unsigned int	recv_msg_cnt_ipc;
	unsigned int	recv_msg_cnt_dma;
	unsigned int	recv_msg_num_frags;
	unsigned int	ishtp_flow_ctrl_cnt;
	unsigned int	out_flow_ctrl_cnt;

	/* Rx msg ... out FC timing */
	ktime_t ts_rx;
	ktime_t ts_out_fc;
	ktime_t ts_max_fc_delay;
	void *client_data;
};

/* Client connection managenment internal functions */
int ishtp_can_client_connect(struct ishtp_device *ishtp_dev, guid_t *uuid);
int ishtp_fw_cl_by_id(struct ishtp_device *dev, uint8_t client_id);
void ishtp_cl_send_msg(struct ishtp_device *dev, struct ishtp_cl *cl);
void recv_ishtp_cl_msg(struct ishtp_device *dev,
		       struct ishtp_msg_hdr *ishtp_hdr);
int ishtp_cl_read_start(struct ishtp_cl *cl);

/* Ring Buffer I/F */
int ishtp_cl_alloc_rx_ring(struct ishtp_cl *cl);
int ishtp_cl_alloc_tx_ring(struct ishtp_cl *cl);
void ishtp_cl_free_rx_ring(struct ishtp_cl *cl);
void ishtp_cl_free_tx_ring(struct ishtp_cl *cl);
int ishtp_cl_get_tx_free_buffer_size(struct ishtp_cl *cl);
int ishtp_cl_get_tx_free_rings(struct ishtp_cl *cl);

/* DMA I/F functions */
void recv_ishtp_cl_msg_dma(struct ishtp_device *dev, void *msg,
			   struct dma_xfer_hbm *hbm);
void ishtp_cl_alloc_dma_buf(struct ishtp_device *dev);
void ishtp_cl_free_dma_buf(struct ishtp_device *dev);
void *ishtp_cl_get_dma_send_buf(struct ishtp_device *dev,
				uint32_t size);
void ishtp_cl_release_dma_acked_mem(struct ishtp_device *dev,
				    void *msg_addr,
				    uint8_t size);

/* Request blocks alloc/free I/F */
struct ishtp_cl_rb *ishtp_io_rb_init(struct ishtp_cl *cl);
void ishtp_io_rb_free(struct ishtp_cl_rb *priv_rb);
int ishtp_io_rb_alloc_buf(struct ishtp_cl_rb *rb, size_t length);

/**
 * ishtp_cl_cmp_id - tells if file private data have same id
 * returns true  - if ids are the same and not NULL
 */
static inline bool ishtp_cl_cmp_id(const struct ishtp_cl *cl1,
				   const struct ishtp_cl *cl2)
{
	return cl1 && cl2 &&
		(cl1->host_client_id == cl2->host_client_id) &&
		(cl1->fw_client_id == cl2->fw_client_id);
}

#endif /* _ISHTP_CLIENT_H_ */
