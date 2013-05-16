/*
 * mailbox: interprocessor communication module
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MAILBOX_INTERNAL_H
#define MAILBOX_INTERNAL_H

#include <linux/device.h>
#include <linux/mailbox_controller.h>

/*
 * The length of circular buffer for queuing messages from a client.
 * 'msg_count' tracks the number of buffered messages while 'msg_free'
 * is the index where the next message would be buffered.
 * We shouldn't need it too big because every transferr is interrupt
 * triggered and if we have lots of data to transfer, the interrupt
 * latencies are going to be the bottleneck, not the buffer length.
 * Besides, ipc_send_message could be called from atomic context and
 * the client could also queue another message from the notifier 'txcb'
 * of the last transfer done.
 * REVIST: If too many platforms see the "Try increasing MBOX_TX_QUEUE_LEN"
 * print, it needs to be taken from config option or somesuch.
 */
#define MBOX_TX_QUEUE_LEN	20

#define TXDONE_BY_IRQ	(1 << 0) /* controller has remote RTR irq */
#define TXDONE_BY_POLL	(1 << 1) /* controller can read status of last TX */
#define TXDONE_BY_ACK	(1 << 2) /* S/W ACK recevied by Client ticks the TX */

struct ipc_chan {
	char name[16]; /* link_name */
	struct ipc_con *con; /* Parent Controller */
	unsigned txdone_method;

	/* Cached values from controller */
	struct ipc_link *link;
	struct ipc_link_ops *link_ops;

	/* Cached values from client */
	void *cl_id;
	void (*rxcb)(void *cl_id, void *mssg);
	void (*txcb)(void *cl_id, void *mssg, enum xfer_result r);
	bool tx_block;
	unsigned long tx_tout;
	struct completion tx_complete;

	void *active_req;
	unsigned msg_count, msg_free;
	void *msg_data[MBOX_TX_QUEUE_LEN];
	bool assigned;
	/* Serialize access to the channel */
	spinlock_t lock;
	/* Hook to add to the controller's list of channels */
	struct list_head node;
	/* Notifier to all clients waiting on aquiring this channel */
	struct blocking_notifier_head avail;
};

/* Internal representation of a controller */
struct ipc_con {
	char name[16]; /* controller_name */
	struct list_head channels;
	/*
	 * If the controller supports only TXDONE_BY_POLL,
	 * this timer polls all the links for txdone.
	 */
	struct timer_list poll;
	unsigned period;
	/* Hook to add to the global controller list */
	struct list_head node;
};

#endif /* MAILBOX_INTERNAL_H */
