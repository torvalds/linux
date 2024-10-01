/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SSH packet transport layer.
 *
 * Copyright (C) 2019-2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _SURFACE_AGGREGATOR_SSH_PACKET_LAYER_H
#define _SURFACE_AGGREGATOR_SSH_PACKET_LAYER_H

#include <linux/atomic.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/serdev.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <linux/surface_aggregator/serial_hub.h>
#include "ssh_parser.h"

/**
 * enum ssh_ptl_state_flags - State-flags for &struct ssh_ptl.
 *
 * @SSH_PTL_SF_SHUTDOWN_BIT:
 *	Indicates that the packet transport layer has been shut down or is
 *	being shut down and should not accept any new packets/data.
 */
enum ssh_ptl_state_flags {
	SSH_PTL_SF_SHUTDOWN_BIT,
};

/**
 * struct ssh_ptl_ops - Callback operations for packet transport layer.
 * @data_received: Function called when a data-packet has been received. Both,
 *                 the packet layer on which the packet has been received and
 *                 the packet's payload data are provided to this function.
 */
struct ssh_ptl_ops {
	void (*data_received)(struct ssh_ptl *p, const struct ssam_span *data);
};

/**
 * struct ssh_ptl - SSH packet transport layer.
 * @serdev:        Serial device providing the underlying data transport.
 * @state:         State(-flags) of the transport layer.
 * @queue:         Packet submission queue.
 * @queue.lock:    Lock for modifying the packet submission queue.
 * @queue.head:    List-head of the packet submission queue.
 * @pending:       Set/list of pending packets.
 * @pending.lock:  Lock for modifying the pending set.
 * @pending.head:  List-head of the pending set/list.
 * @pending.count: Number of currently pending packets.
 * @tx:            Transmitter subsystem.
 * @tx.running:    Flag indicating (desired) transmitter thread state.
 * @tx.thread:     Transmitter thread.
 * @tx.thread_cplt_tx:  Completion for transmitter thread waiting on transfer.
 * @tx.thread_cplt_pkt: Completion for transmitter thread waiting on packets.
 * @tx.packet_wq:  Waitqueue-head for packet transmit completion.
 * @rx:            Receiver subsystem.
 * @rx.thread:     Receiver thread.
 * @rx.wq:         Waitqueue-head for receiver thread.
 * @rx.fifo:       Buffer for receiving data/pushing data to receiver thread.
 * @rx.buf:        Buffer for evaluating data on receiver thread.
 * @rx.blocked:    List of recent/blocked sequence IDs to detect retransmission.
 * @rx.blocked.seqs:   Array of blocked sequence IDs.
 * @rx.blocked.offset: Offset indicating where a new ID should be inserted.
 * @rtx_timeout:   Retransmission timeout subsystem.
 * @rtx_timeout.lock:    Lock for modifying the retransmission timeout reaper.
 * @rtx_timeout.timeout: Timeout interval for retransmission.
 * @rtx_timeout.expires: Time specifying when the reaper work is next scheduled.
 * @rtx_timeout.reaper:  Work performing timeout checks and subsequent actions.
 * @ops:           Packet layer operations.
 */
struct ssh_ptl {
	struct serdev_device *serdev;
	unsigned long state;

	struct {
		spinlock_t lock;
		struct list_head head;
	} queue;

	struct {
		spinlock_t lock;
		struct list_head head;
		atomic_t count;
	} pending;

	struct {
		atomic_t running;
		struct task_struct *thread;
		struct completion thread_cplt_tx;
		struct completion thread_cplt_pkt;
		struct wait_queue_head packet_wq;
	} tx;

	struct {
		struct task_struct *thread;
		struct wait_queue_head wq;
		struct kfifo fifo;
		struct sshp_buf buf;

		struct {
			u16 seqs[8];
			u16 offset;
		} blocked;
	} rx;

	struct {
		spinlock_t lock;
		ktime_t timeout;
		ktime_t expires;
		struct delayed_work reaper;
	} rtx_timeout;

	struct ssh_ptl_ops ops;
};

#define __ssam_prcond(func, p, fmt, ...)		\
	do {						\
		typeof(p) __p = (p);			\
							\
		if (__p)				\
			func(__p, fmt, ##__VA_ARGS__);	\
	} while (0)

#define ptl_dbg(p, fmt, ...)  dev_dbg(&(p)->serdev->dev, fmt, ##__VA_ARGS__)
#define ptl_info(p, fmt, ...) dev_info(&(p)->serdev->dev, fmt, ##__VA_ARGS__)
#define ptl_warn(p, fmt, ...) dev_warn(&(p)->serdev->dev, fmt, ##__VA_ARGS__)
#define ptl_err(p, fmt, ...)  dev_err(&(p)->serdev->dev, fmt, ##__VA_ARGS__)
#define ptl_dbg_cond(p, fmt, ...) __ssam_prcond(ptl_dbg, p, fmt, ##__VA_ARGS__)

#define to_ssh_ptl(ptr, member) \
	container_of(ptr, struct ssh_ptl, member)

int ssh_ptl_init(struct ssh_ptl *ptl, struct serdev_device *serdev,
		 struct ssh_ptl_ops *ops);

void ssh_ptl_destroy(struct ssh_ptl *ptl);

/**
 * ssh_ptl_get_device() - Get device associated with packet transport layer.
 * @ptl: The packet transport layer.
 *
 * Return: Returns the device on which the given packet transport layer builds
 * upon.
 */
static inline struct device *ssh_ptl_get_device(struct ssh_ptl *ptl)
{
	return ptl->serdev ? &ptl->serdev->dev : NULL;
}

int ssh_ptl_tx_start(struct ssh_ptl *ptl);
int ssh_ptl_tx_stop(struct ssh_ptl *ptl);
int ssh_ptl_rx_start(struct ssh_ptl *ptl);
int ssh_ptl_rx_stop(struct ssh_ptl *ptl);
void ssh_ptl_shutdown(struct ssh_ptl *ptl);

int ssh_ptl_submit(struct ssh_ptl *ptl, struct ssh_packet *p);
void ssh_ptl_cancel(struct ssh_packet *p);

int ssh_ptl_rx_rcvbuf(struct ssh_ptl *ptl, const u8 *buf, size_t n);

/**
 * ssh_ptl_tx_wakeup_transfer() - Wake up packet transmitter thread for
 * transfer.
 * @ptl: The packet transport layer.
 *
 * Wakes up the packet transmitter thread, notifying it that the underlying
 * transport has more space for data to be transmitted. If the packet
 * transport layer has been shut down, calls to this function will be ignored.
 */
static inline void ssh_ptl_tx_wakeup_transfer(struct ssh_ptl *ptl)
{
	if (test_bit(SSH_PTL_SF_SHUTDOWN_BIT, &ptl->state))
		return;

	complete(&ptl->tx.thread_cplt_tx);
}

void ssh_packet_init(struct ssh_packet *packet, unsigned long type,
		     u8 priority, const struct ssh_packet_ops *ops);

int ssh_ctrl_packet_cache_init(void);
void ssh_ctrl_packet_cache_destroy(void);

#endif /* _SURFACE_AGGREGATOR_SSH_PACKET_LAYER_H */
