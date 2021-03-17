/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __UM_VECTOR_KERN_H
#define __UM_VECTOR_KERN_H

#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include "vector_user.h"

/* Queue structure specially adapted for multiple enqueue/dequeue
 * in a mmsgrecv/mmsgsend context
 */

/* Dequeue method */

#define QUEUE_SENDMSG 0
#define QUEUE_SENDMMSG 1

#define VECTOR_RX 1
#define VECTOR_TX (1 << 1)
#define VECTOR_BPF (1 << 2)
#define VECTOR_QDISC_BYPASS (1 << 3)
#define VECTOR_BPF_FLASH (1 << 4)

#define ETH_MAX_PACKET 1500
#define ETH_HEADER_OTHER 32 /* just in case someone decides to go mad on QnQ */

#define MAX_FILTER_PROG (2 << 16)

struct vector_queue {
	struct mmsghdr *mmsg_vector;
	void **skbuff_vector;
	 /* backlink to device which owns us */
	struct net_device *dev;
	spinlock_t head_lock;
	spinlock_t tail_lock;
	int queue_depth, head, tail, max_depth, max_iov_frags;
	short options;
};

struct vector_estats {
	uint64_t rx_queue_max;
	uint64_t rx_queue_running_average;
	uint64_t tx_queue_max;
	uint64_t tx_queue_running_average;
	uint64_t rx_encaps_errors;
	uint64_t tx_timeout_count;
	uint64_t tx_restart_queue;
	uint64_t tx_kicks;
	uint64_t tx_flow_control_xon;
	uint64_t tx_flow_control_xoff;
	uint64_t rx_csum_offload_good;
	uint64_t rx_csum_offload_errors;
	uint64_t sg_ok;
	uint64_t sg_linearized;
};

#define VERIFY_HEADER_NOK -1
#define VERIFY_HEADER_OK 0
#define VERIFY_CSUM_OK 1

struct vector_private {
	struct list_head list;
	spinlock_t lock;
	struct net_device *dev;

	int unit;

	/* Timeout timer in TX */

	struct timer_list tl;

	/* Scheduled "remove device" work */
	struct work_struct reset_tx;
	struct vector_fds *fds;

	struct vector_queue *rx_queue;
	struct vector_queue *tx_queue;

	int rx_irq;
	int tx_irq;

	struct arglist *parsed;

	void *transport_data; /* transport specific params if needed */

	int max_packet;
	int req_size; /* different from max packet - used for TSO */
	int headroom;

	int options;

	/* remote address if any - some transports will leave this as null */

	int header_size;
	int rx_header_size;
	int coalesce;

	void *header_rxbuffer;
	void *header_txbuffer;

	int (*form_header)(uint8_t *header,
		struct sk_buff *skb, struct vector_private *vp);
	int (*verify_header)(uint8_t *header,
		struct sk_buff *skb, struct vector_private *vp);

	spinlock_t stats_lock;

	struct tasklet_struct tx_poll;
	bool rexmit_scheduled;
	bool opened;
	bool in_write_poll;
	bool in_error;

	/* guest allowed to use ethtool flash to load bpf */
	bool bpf_via_flash;

	/* ethtool stats */

	struct vector_estats estats;
	struct sock_fprog *bpf;

	char user[];
};

extern int build_transport_data(struct vector_private *vp);

#endif
